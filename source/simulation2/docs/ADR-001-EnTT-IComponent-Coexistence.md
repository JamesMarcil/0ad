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
* `bd_0ad-1u1.1.4` (determinism guard rails) is the enforcement vehicle for these rules.

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
</content>
