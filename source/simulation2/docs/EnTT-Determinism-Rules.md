# EnTT Determinism Rules

Status: active constraint for EnTT-backed components in simulation2.
Introduced by: bd_0ad-1u1.1.4

This document specifies the determinism rules that must be followed when using EnTT-backed storage for components, and provides a runbook for diagnosing out-of-sync errors.

See also:
- [ADR-001-EnTT-IComponent-Coexistence.md](ADR-001-EnTT-IComponent-Coexistence.md) for architectural context and Decision 4
- [source/simulation2/system/EnTTOrderedIteration.h](../system/EnTTOrderedIteration.h) for the `ForEachOrderedByEntityId` utility

## The Three Rules

### R1 — No order-dependent mutation during view iteration

EnTT views and pools iterate in swap-and-pop packed order, which is a function of insertion/removal history, not of `entity_id_t`. Any work whose result depends on visit order — accumulation into shared state, first-match-wins queries, CComponentManager::m_RNG consumption, message emission, entity creation/destruction — must be driven by ascending `entity_id_t`, exactly as `std::map<entity_id_t, IComponent*>` traversal does today. Use `ForEachOrderedByEntityId` (EnTTOrderedIteration.h). Raw `view.each()` is permitted only for per-entity pure updates that write only components of the entity being visited, and must carry a comment saying so.

### R2 — Serialized output must never depend on storage order

`SerializeState`, `ComputeStateHash`, and `DumpDebugState` (source/simulation2/system/ComponentManagerSerialization.cpp:179/106/57) derive their order from `m_ComponentsByTypeId`, i.e. ascending `ComponentTypeId` then ascending `entity_id_t`. A migrated component's `Serialize`/`Deserialize` may only read/write the storage of its own entity through `Get<T>()`. Never serialize, hash, or dump by iterating a view or pool.

### R3 — No reinterpret_cast between entity_id_t and entt::entity

`entt::entity` is index+version and is recycled; it is never serialized, hashed, ordered, or exposed to JS (ADR-001 Decision 2). Convert only via `CComponentManager::LookupRegistryEntity(entity_id_t)` (forward, ComponentManager.h:288) and the `SimEntityId` reverse-lookup component (ComponentManager.h:53). `ForEachOrderedByEntityId` hands the callback both values so no conversion is ever needed inside a loop.

## registry::sort verdict — Rejected as a standing invariant

`entt::registry::sort<T>` (registry.hpp:1127) is a one-shot `std::sort` of the packed array + sparse-index rewrite — it does NOT install an ordering invariant, since the next `emplace<T>` appends at the tail and the pool goes out of order again.

Keeping ascending `entity_id_t` order "at all times" would mean re-sorting after every `emplace`/`remove`:
- **Cost**: O(n log n) per entity creation → O(n² log n) over a load of n entities, with each comparison doing a `SimEntityId` sparse-set lookup. Unaffordable.
- **Distribution**: converts a local assertable rule into a distributed cross-client invariant (must run at exactly the same points on every client).

Secondary findings:
- `sort()` is compatible with `EnTTComponent.h`'s cached `PoolPtr<T>` scheme (permutes internal arrays, doesn't move/replace the pool object) but invalidates every `T&`/`T*` from `Get<T>()` (one more operation under the ADR-001 Decision 3 corollary).
- `ENTT_ASSERT(!owned<Type>())` at registry.hpp:1128 means pools owned by an `entt::group` can never be sorted — adopting sort-as-invariant would preemptively ban groups for those types.
- Benefit today is zero: no component iterates a view yet (CCmpTest1EnTT in source/simulation2/components/CCmpTest.cpp does per-entity access only).

**Conclusion**: rejected as standing invariant; if profiling ever shows `ForEachOrderedByEntityId`'s gather+sort dominating a hot per-turn loop, the shape to adopt instead is a single `registry.sort<T>(entity_id_t comparator)` at one fixed, documented point in `CSimulation2Impl::UpdateComponents` before the system phase, combined with a `CPoolStructureGuard` spanning the whole system phase to prove no `emplace`/`remove` happened after the sort — and even then it's complementary (`sort` gives order, guard proves it held, `ForEachOrderedByEntityId` remains the correct default for pools mutated during a turn).

## Diagnosis runbook for out-of-sync errors

### Tools and entry points

- **Multi-client desync detection** (CNetClientTurnManager::NotifyFinishedUpdate, source/network/NetClientTurnManager.cpp:82-93): every turn, `quick = !TurnNeedsFullHash(turn)`, `ENSURE(m_Simulation2.ComputeStateHash(hash, quick))`, then `m_Replay.Hash(hash, quick)` written to the replay's commands.txt as "hash-quick <hex>" / "hash <hex>" (source/ps/Replay.cpp near lines 108-113).

- **Hash frequency** (CTurnManager::TurnNeedsFullHash, source/simulation2/system/TurnManager.cpp:251): full hash on turn 1 and every turn divisible by 20; quick hash otherwise. Quick hash covers only `CID_Position` (ComponentManagerSerialization.cpp near line 123).

- **Sync error response** (CNetClientTurnManager::OnSyncError, source/network/NetClientTurnManager.cpp:116-133): recomputes the local hash, writes logs/oos_dump<postfix>.txt via `DumpDebugState` (OOS turn + local net turn as first two lines) and logs/oos_dump<postfix>.dat via `SerializeState`.

- **Replay desync detection** (CReplayTurnManager::NotifyFinishedUpdate, source/simulation2/system/ReplayTurnManager.cpp:75-105): after `DoTurn`, compares freshly computed hash against the replay-recorded one in `m_ReplayHash` (populated by `StoreReplayHash`), firing `EventNameReplayOutOfSync` on first mismatch.

- **Replay hash testing** (-replay non-visual path uses CReplayPlayer::TestHash, source/ps/Replay.cpp:346-361), enabled by --hash/--hash-quick; -ooslog (source/main.cpp near line 630 → SimulationDebugOptions::oosLog → CSimulation2Impl::DumpState, Simulation2.cpp:535, 636-650) writes a per-turn full-hash + DumpDebugState text file to logs/oos_logs/<date-index>/NNNNN.txt.

- **Serialization round-trip check**: -serializationtest=<turn> (Replay.cpp near line 194, SimulationDebugOptions::SerializationTest).

### Recommended steps to diagnose

1. **Confirm and bound the divergence turn** from both clients' logs/oos_dump<postfix>.txt "oos turn: N" line; note quick-hash turns (not N%20==0 and not turn 1) only cover `CID_Position`, so real divergence could be earlier and hidden up to 20 turns.

2. **Get a deterministic reproduction**: run `pyrogenesis -replay=<path> -ooslog` on the OOS'd client's commands.txt. If it reproduces, use per-turn dumps in logs/oos_logs/. If it does NOT reproduce, that's evidence of an order-dependent (history-dependent) bug since replay reaches the same logical state via one canonical path.

3. **Bisect the exact turn** using the full per-turn hashes from -ooslog output (exact, not approximate).

4. **Diff the dumps** (`diff logs/oos_logs/<runA>/NNNNN.txt logs/oos_logs/<runB>/NNNNN.txt`, or the two oos_dump<postfix>.txt files); `DumpDebugState` output is grouped by "- id: <entity_id_t>" ascending then component name ascending, so diffs are line-stable and name the offending entity/component directly.

5. **Decide whether EnTT is implicated**: suspect view-iteration order when:
   - (a) diff hunks are confined to a migrated component's State struct fields (Template/Derived are re-derived, should be identical),
   - (b) the set of "- id:" entries and per-entity component-name lists are identical between dumps (membership/legacy traversal order unchanged, only values differ),
   - (c) the two runs differ in history (entity creation/destruction interleaving, rejoin, serialization round-trip, replay vs live) rather than logical state.

   Conversely, if the entity set differs or a non-migrated component diverges first, EnTT ordering is not the likely cause.

6. **Confirm cheaply in a debug build**: `ForEachOrderedByEntityId` installs `CPoolStructureGuard` automatically, so mid-iteration structural mutation of a watched pool asserts with the pool type name and last-visited entity_id_t. Grep the suspect component for raw `view()`/`.each()` usages — those are the R1-exempt, unguarded loops. Wrapping one in a standalone `CPoolStructureGuard` is a one-line experiment.

7. **Rule out serialization asymmetry first**: `pyrogenesis -replay=<path> -serializationtest=<divergent turn>` checks `Serialize`/`Deserialize` round-trip identically. A failure here is an R2 violation (in the component's own Serialize/Deserialize), not an R1 violation.

8. **Fix by construction**: replace the offending raw loop with `ForEachOrderedByEntityId`, or gather-and-sort by `entity_id_t` before consuming results. Do not "fix" by adding a `registry.sort<T>` at the call site (see section 4 verdict).
