# ADR-001: EnTT / IComponent coexistence pattern

Status: **Proposed** (awaiting review, bd_0ad-1u1.1.1)
Scope: incremental migration of native `simulation2` components to EnTT-backed storage.

## Context

We are migrating native components (`CCmpX : ICmpX : IComponent`) to keep their state in an
`entt::registry`, one component type at a time. Everything outside the component itself
(`CComponentManager`, message dispatch, `CmpPtr`/`QueryInterface`, serialization, the JS
interface) must be unaffected. Determinism for lockstep multiplayer and replay reproduction
via `ComputeStateHash` is a hard constraint.

Facts established from the code:

* `libraries/source/entt` is EnTT **4.0.0**, header-only, wired in
  `build/premake/extern_libs5.lua` (the `entt` block does only `add_source_include_paths`)
  and listed as an extern lib of the `simulation2` static lib in `premake5.lua`.
  `ENTT_ID_TYPE` is left at its default `uint32_t`.
* `entt_traits<uint32_t>` (`entt/entity/entity.hpp`) has `entity_mask = 0xFFFFF` (20-bit
  index, max 1,048,575) and `version_mask = 0xFFF`. Sparse sets are paged with
  `ENTT_SPARSE_PAGE = 4096`.
* `Entity.h`: `typedef u32 entity_id_t`, `INVALID_ENTITY = 0`, `SYSTEM_ENTITY = 1`,
  `ENTITY_TAGMASK = (1 << 29)`, `FIRST_LOCAL_ENTITY = ENTITY_TAGMASK` (536,870,912).
  Ids come from `m_NextEntityId` / `m_NextLocalEntityId` and are never reused.
  `SEntityComponentCache` is a malloc-allocated variable-length struct of `IComponent*`
  indexed by **InterfaceId**.
* `CComponentManager` (ComponentManager.h:331-366) stores
  `std::map<ComponentTypeId, std::map<entity_id_t, IComponent*>> m_ComponentsByTypeId`,
  `std::vector<std::unordered_map<entity_id_t, IComponent*>> m_ComponentsByInterface`,
  `std::unordered_map<entity_id_t, SEntityComponentCache*> m_ComponentCaches`.
* `ConstructComponent` (ComponentManager.cpp:756-814) allocates via `ct.alloc` =
  `DEFAULT_COMPONENT_ALLOCATOR` (Component.h:39-45) = plain `new CCmpX()`, and registers the
  raw pointer in three places. Line 793 states: *"NB: The unit motion manager relies on
  components not moving in memory once constructed."*
* `CSimContext` and `CComponentManager` reference each other but neither owns the other;
  `CSimulation2Impl` owns both by value, context declared first (Simulation2.cpp:156-157) so
  the context outlives the manager. `CSimContext::SetComponentManager` has
  `ENSURE(!m_ComponentManager)`: one-shot binding. A **second** pair exists for the
  serialization test path (`m_SecondaryContext` / `m_SecondaryComponentManager`,
  Simulation2.cpp:439-441), so no registry may be global or static.
* `DeserializeState` lives in `ComponentManagerSerialization.cpp:296-397` and begins with
  `ResetState(); InitSystemEntity();`, then rebuilds every component with
  `ConstructComponent` + `Deserialize`. No component addresses survive a deserialize.

---

## Decision 1: `CComponentManager` owns the registry

The `entt::registry` is a **private member of `CComponentManager`**, exposed as
`entt::registry& GetRegistry()`. It is not owned by `CSimContext`, and there is no new
`CEnTTWorld` type.

Rationale:

* The registry is component *storage*, and `CComponentManager` is already the storage owner.
  They must be created, reset and destroyed as one unit; splitting them adds a lifetime
  ordering hazard for no benefit.
* `CSimContext` outlives its manager and is a thin, mostly non-owning view (terrain, unit
  manager, system entity handle, back-pointer). Putting mutable per-entity state there
  inverts that relationship and would leave a live registry attached to a context whose
  manager has been destroyed.
* Two manager/context pairs exist at runtime. One registry per manager falls out for free.
* Components already get `SetSimContext` and `SetEntityHandle` at construction, so a proxy
  reaches the registry via `GetSimContext().GetComponentManager().GetRegistry()` with zero new
  plumbing, and caches the pointer (see Decision 3).

Lifecycle contract:

* **Construction**: registry is default-constructed with the manager; nothing is created in
  it until `InitSystemEntity()`.
* **`InitSystemEntity()`** (ComponentManager.cpp:875-880): immediately after
  `m_SystemEntity = AllocateEntityHandle(SYSTEM_ENTITY)`, the manager creates the registry
  entity for `SYSTEM_ENTITY` and records the mapping. `SYSTEM_ENTITY` is therefore always the
  first registry entity, deterministically, on every load and every reset.
* **`ResetState()`** (ComponentManager.cpp:501-540): registry teardown happens **after** the
  existing reverse-order `Deinit()` + `dealloc()` loop, so proxies may still legally touch
  their storage inside `Deinit()`. Then clear the registry and the id maps, next to the
  existing `m_ComponentCaches` free/clear and the `m_NextEntityId` / `m_NextLocalEntityId`
  reset. **Requirement**: after `ResetState()` the registry must be observably identical to a
  freshly constructed one, including that no storage pool retains stale entities; otherwise
  "reset then load" and "fresh load" could diverge in state hash. If `registry.clear()`
  proves not to give that guarantee for our usage, assign a fresh `entt::registry{}` instead.
* **`DeserializeState()`**: unchanged in shape. It resets, re-inits the system entity, and
  each rebuilt proxy `Deserialize()` writes into the registry exactly as `Serialize()` read
  from it. The registry is **never** snapshotted or restored as a blob and gets no
  serialization path of its own, so the save format is untouched.

## Decision 2: bidirectional map, **not** `registry.create(hint)`

`CComponentManager` owns an explicit bidirectional mapping:

* forward: `entity_id_t -> entt::entity` in a hash map;
* reverse: a `SimEntityId { entity_id_t id; }` component on every registry entity, so the
  reverse lookup is O(1) and cannot desynchronise from registry entity lifetime.

Forcing `entt::entity == entity_id_t` via `create(hint)` is **rejected**, and not on style
grounds; it does not work:

* With the default `ENTT_ID_TYPE = uint32_t` the entity index field is 20 bits
  (`entity_mask = 0xFFFFF`). `FIRST_LOCAL_ENTITY = 1 << 29` is far outside that range, so
  every local entity id is unrepresentable, and normal ids would be capped at ~1.05M per run.
* Widening to a 64-bit custom entity type makes the ids representable but EnTT sparse sets
  are paged at `ENTT_SPARSE_PAGE = 4096` and the page directory is sized to the largest index
  in use. A single local entity at index 2^29 forces a ~131,072-pointer (~1 MB) page
  directory **per storage pool**, i.e. per migrated component type, plus the registry own
  entity storage. Unacceptable memory for a cosmetic identity.
* It would also require globally disabling entity recycling so version bits cannot corrupt
  the identity, discarding the main EnTT lifecycle safety net.

Accepted consequences and the constraints that make them safe:

* **Memory**: one hash map entry plus one 4-byte `SimEntityId` per live entity. Bounded and
  proportional to live entities, unlike the paged-sparse-set alternative.
* **Determinism**: the forward map is a *lookup structure only*. Its iteration order is
  implementation-defined and it **must never be iterated** by anything affecting simulation
  state; the existing `std::map` ordering in the manager stays the only ordering used by
  state-affecting code (Decision 4). Registry entity values (index+version, recycled) are
  never serialized, hashed, ordered, or exposed to JS. Only `entity_id_t` crosses those
  boundaries, so recycling inside the registry is invisible outside the storage layer.
* Mapping entries are created in `AllocateEntityHandle` (ComponentManager.cpp:844-858), the
  single choke point where an `entity_id_t` first becomes live, and destroyed in
  `FlushDestroyedComponents` (938-995) next to the `free()` of the component cache. The map
  *contents* are therefore a pure function of the deterministic simulation even though its
  *layout* is not.

## Decision 3: proxy object model unchanged, one heap instance per entity

Locked in: a migrated `CCmpX` remains a normal one-instance-per-entity `IComponent` subclass
allocated by `DEFAULT_COMPONENT_ALLOCATOR` (`new CCmpX()`) and registered in
`m_ComponentsByInterface`, `m_ComponentsByTypeId` and the `SEntityComponentCache` of the
entity exactly as today. Registration code in `ConstructComponent` does not change at all.

Only the class contents change:

* Member data fields are removed and replaced by registry storage. The proxy keeps at most a
  cached `entt::registry*` and its `entt::entity`, both resolved once at construction /
  `Init()` from the already-supplied `CEntityHandle` and `CSimContext`.
* All `ICmpX` accessors and `HandleMessage` / `Serialize` / `Deserialize` forward to
  `m_Registry->get<...>(m_Entity)`.
* The proxy address is **stable for the component lifetime**: heap-allocated once, freed
  only in `ResetState()` or `FlushDestroyedComponents()`. `std::map` nodes are stable and
  `m_ComponentsByInterface` is resized once in the constructor, so nothing moves. This
  preserves the invariant called out at ComponentManager.cpp:793 and keeps
  `SEntityComponentCache`, `CmpPtr`, and every cached `ICmpX*` in the engine valid.
* Corollary: no caller may hold a pointer or reference *into* registry storage across any
  call that can add or remove that component type, because EnTT pools reallocate. Proxy
  pointers are stable; component-data references are not. Accessors return by value or
  perform the `get<>` at point of use.
* `AddMockComponent` (ComponentManager.cpp:816-829) keeps working unchanged since it only
  touches the interface map and the cache; mocks are not registry-backed.

## Decision 4: serialization invariance, and the EnTT ordering hazard

All three state-observing functions live in `ComponentManagerSerialization.cpp` and derive
their order entirely from `m_ComponentsByTypeId`, i.e. from `std::map` over stable
deterministic integer ids:

* `SerializeState` (179-294): ascending `ComponentTypeId`, then ascending `entity_id_t`;
  local entities and `SYSTEM_ENTITY` split out; component **names** written, not ids.
* `ComputeStateHash` (106-150): ascending `ComponentTypeId`, then ascending `entity_id_t`,
  skipping local entities.
* `DumpDebugState` (57-104): inverts into `std::map<entity_id_t, std::map<ComponentTypeId,
  IComponent*>>`, so ascending `entity_id_t` then ascending `ComponentTypeId`.

Because Decision 3 leaves proxy registration completely unchanged, **these traversals and
their output are byte-identical before and after a component is migrated**, provided the
migrated `Serialize`/`Deserialize` read and write the same fields in the same order. That is
the acceptance test for every migration task: same replay, identical `ComputeStateHash` and
identical `DumpDebugState` output.

The hazard: EnTT views and pools iterate in **swap-and-pop order**, which depends on
insertion/removal history and on which pool drives the view. It is not `entity_id_t` order
and is not stable across equivalent-but-differently-ordered histories. Therefore:

* Never serialize, hash, or dump by iterating a view or a storage pool.
* Never perform order-sensitive state mutation in view-iteration order: accumulation into
  shared state, "first match wins" queries, consumption of `m_RNG`, message emission order,
  or entity creation/destruction order.
* View iteration is permitted only for genuinely order-independent work (per-entity pure
  updates writing only components of that same entity), or when results are gathered and
  sorted by `entity_id_t` before being consumed. Where a system needs deterministic order,
  sort a gathered `entity_id_t` list, or use `registry.sort<T>()` with an `entity_id_t`
  comparator, and state that in a comment at the loop.
* Determinism guard rails are documented in [source/simulation2/docs/EnTT-Determinism-Rules.md](EnTT-Determinism-Rules.md), delivered by `bd_0ad-1u1.1.4`.

## Decision 5: boundary with not-yet-migrated components

Migrated components talk to non-migrated components **exactly as they do today**: via
`CmpPtr<ICmpX>` / `CComponentManager::QueryInterface` against the `ICmpX` interface, and via
the existing message system. No new coupling is introduced.

* A migrated component must not reach into the registry storage of another component
  directly, even when that other component is also migrated. Cross-component access goes
  through the `ICmpX` interface. This keeps migration order free and prevents a half-migrated
  engine from growing hidden dependencies on which components happen to be ported.
* No non-migrated component gains any knowledge of the registry.
* Multi-component EnTT views spanning several migrated types are a **later, explicitly scoped
  optimisation** (a real "system"), not something individual migration tasks may introduce ad
  hoc. Each such view must ship with a determinism argument per Decision 4.

## Consequences

* The migration is per-component and independently revertible; at every commit the engine
  behaves and serializes identically.
* Cost of the coexistence layer: one hash map entry plus one 4-byte component per entity,
  one pointer/handle pair per migrated proxy instance, and one indirection per state access.
  Removing that indirection requires deleting the proxy layer, which is out of scope here.
* The near-term win is data layout and cache locality within the storage of a migrated
  component, plus the ability to write real systems later. It is not an immediate reduction
  in per-access cost, and migration tasks should not be justified on that basis.

## Appendix A: Storage struct conventions (bd_0ad-1u1.1.3)

Each migrated component splits its per-entity state into separate entt component structs by
lifetime and serialization role, so that future cache-sensitive iteration does not drag
cold data through the CPU cache:

1. **`<Name>Template`** — Values parsed from the entity's template in `Init()`. Read-only
   thereafter. This struct is cold and accessed infrequently. It is re-derived on
   `Deserialize()` (which is why `Deserialize` receives the `CParamNode`). Never written to
   the save stream.

2. **`<Name>State`** — Mutable simulation state, the **only** struct whose contents are
   read/written by `Serialize`/`Deserialize` and hashed by `ComputeStateHash`. Must be POD-ish
   and small; this is the hot struct that future systems will iterate at high frequency.

3. **`<Name>Derived`** — Non-serialized derived/interpolation/render-facing state, fully
   reconstructible from `<Name>Template` + `<Name>State`. Must never be serialized or hashed,
   and must be re-derived on `Deserialize()`.

**Rules:**
* One struct per kind (Template, State, Derived) per `IComponent` type.
* Never share a struct between two `IComponent` types.
* No self-referential pointers or pointers-into-sibling-structs (entt pools reallocate on
  emplace/remove, so inter-struct references are invalid after pool reallocation).
* A component may only `Get<T>()` its own declared structs (enforced by the mixin's
  static_assert in `source/simulation2/system/EnTTComponent.h`), never another component's
  storage. Cross-component access goes through `ICmpX` interfaces per Decision 5.
* Corollary from Decision 3: never hold a `T&`/`T*` obtained from `Get<T>()` across any call
  that could emplace or remove that storage type, because pools reallocate. Read into a
  local, or re-`Get()` at point of use.

**Allocator / proxy lifetime:**
`DEFAULT_COMPONENT_ALLOCATOR`'s plain `new CCmpX()` already produces the individually-
heap-allocated, address-stable proxy that Decision 3 requires (see ComponentManager.cpp:793).
The `EnTTComponent` mixin adds only a couple of words of member data and requires no custom
construction. **These proxies must NEVER be pooled or arena-allocated**, as that would move
them and break `SEntityComponentCache`, `CmpPtr`, and the unit motion manager.

**Determinism:**
See Decision 4 for the constraints on state-observing code (serialization, hashing, debug
dumps). The EnTT backing changes only per-component storage layout, not the traversal order
of state-observable functions; they still iterate `m_ComponentsByTypeId` in ascending
`ComponentTypeId` order, so serialization output is byte-identical before and after migration.

### Cost of the indirection

The Consequences section states the migration is "not an immediate reduction in per-access
cost". That understates the regression and deserves sharpening: even with the pool pointer
cached, `Get<T>()` is `basic_storage::get` → `element_at(index(entity))`, and both the sparse
index and the payload are paged (`ENTT_SPARSE_PAGE=4096`, `ENTT_PACKED_PAGE=1024`). This means
~5-6 serially dependent loads across 3-4 cache lines, versus 1 load (usually already-hot) for
the plain member variable it replaces. That is ~15-30 extra L1-latency cycles per access.

However, each accessor call already sits behind a virtual `ICmpX` interface boundary (and
often a `CComponentManager::QueryInterface` hash lookup to fetch the proxy), so the marginal
cost per accessor *call* is more like +30-60% than 6x. But it is a measurable *increase*, not
neutral.

**The payoff arrives only when real systems iterate pools directly**, i.e. when migration
enables a system to hold the registry lock and walk all instances of a single storage struct
in tight-loop order without the virtual-call indirection. Until then, porting a component
should be justified on per-component code clarity and future-proofing, not immediate
performance.

### Why `Get<T>()` does not call `registry::get<T>()`

This is the central reason the `EnTTComponent` mixin exists at all. `entt::registry::get<T>()`
(registry.hpp:857) routes through `assure<T>()` (registry.hpp:221), which does
`pools.find(type_hash<T>::value())` on an `entt::dense_map`: ~4-5 serially dependent loads
across 3 cache lines, roughly doubling the access path.

Also: `type_hash<T>::value()` is a compile-time constant *only* because the build does not
define `ENTT_STANDARD_CPP`; if that ever changes, it degrades to a non-inlinable `ENTT_API`
function call with a function-local static guard per access.

Finally: in any "release with asserts" configuration, `registry.hpp:229`'s
`ENTT_ASSERT(it->second->info() == type_id<Type>())` is a **virtual call per access**. The
mixin's cached pool pointer makes the entire implementation immune to all three.

### Cached-pool-pointer validity rules

Pool addresses are stable for the registry object's lifetime because `pools` holds
`std::allocate_shared`'d storage objects; growing/rehashing `pools` moves shared_ptrs, not
the pooled storage objects themselves.

The only operations that invalidate a cached pool pointer:

* **`registry::reset(id)` (registry.hpp:435)** — erases the pool outright. **Must never be called
  on an active EnTT-backed component's storage id.** ResetState() is safe because it destroys
  all component proxies (and calls Deinit, which detaches storage) before resetting pools.

* **`registry::swap()` and `operator=(basic_registry&&)`** (registry.hpp:343) — implemented as
  swap, which orphans the old registry. **Hard invariant: every proxy must be destroyed before
  the registry object is replaced.** `ResetState()` satisfies this (Deinit + dealloc loop
  runs before `m_Registry = std::make_unique<...>()`). This invariant is currently only
  implied; a follow-up bead will add ENSURE-guards at ComponentManager call sites.

* **NOT an invalidator: `registry::clear()`** (registry.hpp:922) — clears pool contents, never
  erases pools themselves.

* **Named-id storage:** `storage<T>(id)` accepts a non-default id; two different ids give two
  distinct pools of the same type. Storage structs **must use default ids only**.

* **Owning groups:** permanently reorder an owned pool's packed array (moves elements, not the
  pool). Safe here because the mixin never caches element addresses, only the pool itself.

### Mandated access idiom

An accessor (whether public `GetX()` or message handler) must do **one `Get<T>()` call per
storage struct and read/write multiple fields off that single reference**, never one `Get<T>()`
per field. Each `Get()` is a fresh sparse-set walk (index lookup), so grouping accesses saves
time and helps readability. Bind the result to a reference at the top of the function, e.g.:

```cpp
Test1EnTTState& state = Get<Test1EnTTState>();
Test1EnTTDerived& derived = Get<Test1EnTTDerived>();
state.x += 1;
derived.messagesHandled += 1;
```

This idiom is safe because nothing in that scope can emplace or remove storage — exactly the
ADR-001 Decision 3 corollary.

Mock components (`AddMockComponent`) are **not registry-backed** and must not inherit
`EnTTComponent`. They are registered only in `m_ComponentsByInterface`, never in the registry.

### Open question: the three-way split

The Template/State/Derived convention is specified above and kept for now, but the perf review
has raised a valid concern: it may be wrong for random per-entity access patterns in the
proxy era.

Three pools means up to 3x the dependent-load chains and up to 12 cache lines touched (vs 4)
when reading multiple structs together, plus ~3x the per-entity index overhead (each pool
carries its own sparse pages and packed entity arrays — roughly 240 KB vs 80 KB of pure index
at 10k entities for a component like CCmpPosition). The real win comes only when a system
streams **one struct** across many entities; the split loses if one entity's structs are read
together.

**Splitting wins when:** a system iterates all `Position::State` (and only State) across 10k
entities in tight cache-efficient order.

**Splitting loses when:** an accessor always touches State + Derived together (wasted indices,
wasted cache lines, wasted page faults).

**Crossover rule:** if an accessor is found touching `Template` per-frame, that field is
misclassified and belongs in `State`. If `Derived` is *always* accessed together with `State`,
consider merging them (breaking the three-way split). Both are reversible.

A benchmark bead will settle this empirically before CCmpPosition is ported. Until then, the
convention is deliberately reversible and should be left as specified.

See also: `source/simulation2/system/EnTTComponent.h` for the CRTP helper and usage pattern.
</content>
