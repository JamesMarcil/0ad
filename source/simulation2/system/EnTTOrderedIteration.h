/* Copyright (C) 2026 Wildfire Games.
 * This file is part of 0 A.D.
 *
 * 0 A.D. is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * 0 A.D. is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with 0 A.D.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef INCLUDED_ENTTORDEREDITERATION
#define INCLUDED_ENTTORDEREDITERATION

#include "lib/debug.h"
#include "simulation2/system/ComponentManager.h"
#include <algorithm>
#include <array>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>
#include <entt/entity/registry.hpp>

namespace EnTTDeterminism {

/**
 * Debug assertion mechanism for EnTT pool structure invariance.
 *
 * Wraps a loop that iterates one or more watched pools, capturing snapshots of
 * their size, buffer pointer, and packed entity arrays at construction time.
 * On each Check() call (or at destruction for implicit end-of-loop check),
 * verifies that no structural mutations (emplace/remove/reallocation) occurred,
 * which would violate the R1 determinism rule (see EnTT-Determinism-Rules.md).
 *
 * Usage:
 *   auto view = registry.view<ComponentA>();
 *   CPoolStructureGuard<ComponentA> guard(registry, "my loop");
 *   for(auto entity : view) {
 *     // ... do work ...
 *     guard.Check(idFromComponentA);  // Cheap per-iteration check (O(1): size/pointer only)
 *   }
 *   // ~CPoolStructureGuard calls full Check() with the last id (packed array validation)
 *
 * Per-iteration Check(id) is a cheap O(1) verification of pool->size() and pool->data()
 * pointer, which catches every emplace/remove/reallocation. The destructor performs the
 * expensive full packed-array comparison (std::equal) to ensure ordering is preserved.
 *
 * In release builds (NDEBUG), this class has zero members and all methods are
 * no-ops, inlining away to nothing.
 */
template<typename... TWatched>
class CPoolStructureGuard
{
public:
	CPoolStructureGuard(const entt::registry& registry, const char* context);
	~CPoolStructureGuard();

	/**
	 * Cheap per-iteration check (O(1)): verify pool size and data pointer unchanged.
	 * Catches emplace/remove/reallocation events. Called on every iteration of a guarded loop.
	 * @param lastVisited The entity_id_t of the entity most recently visited, for diagnostics.
	 */
	void Check(entity_id_t lastVisited = INVALID_ENTITY) const;

	/**
	 * Full check (O(n)): verify complete packed array ordering unchanged.
	 * Called automatically at destruction; can also be called explicitly for intermediate checks.
	 * @param lastVisited The entity_id_t of the entity most recently visited, for diagnostics.
	 */
	void FullCheck(entity_id_t lastVisited = INVALID_ENTITY) const;

	// Non-copyable, non-movable.
	CPoolStructureGuard(const CPoolStructureGuard&) = delete;
	CPoolStructureGuard& operator=(const CPoolStructureGuard&) = delete;
	CPoolStructureGuard(CPoolStructureGuard&&) = delete;
	CPoolStructureGuard& operator=(CPoolStructureGuard&&) = delete;

#ifndef NDEBUG

private:
	struct PoolSnapshot
	{
		const char* typeName;
		size_t size;
		const entt::entity* data;
		std::vector<entt::entity> packed;
	};

	const entt::registry& m_Registry;
	const char* m_Context;
	std::array<PoolSnapshot, sizeof...(TWatched)> m_Snapshots;
	mutable entity_id_t m_LastChecked = INVALID_ENTITY;

	// Helper template for cheap O(1) per-iteration check (size and pointer only)
	template<size_t Index>
	void CheckPoolAtIndex_Cheap(entity_id_t lastVisited) const
	{
		if constexpr(Index < sizeof...(TWatched))
		{
			using PoolType = std::tuple_element_t<Index, std::tuple<TWatched...>>;
			const auto& snapshot = m_Snapshots[Index];
			// Note: storage<T>() on const registry returns a pointer, not a reference
			const auto* pool = m_Registry.template storage<PoolType>();
			const char* typeMsg = snapshot.typeName ? snapshot.typeName : "Unknown";

			// If pool was never created, snapshot should have size 0
			if(!pool)
			{
				if(snapshot.size != 0)
				{
					debug_printf("EnTT-Determinism-Rules.md R1 violation: pool %s was destroyed (size was %zu, now 0) at entity_id_t %u (context: %s)\n",
						typeMsg, snapshot.size, lastVisited, m_Context);
					ASSERT(false);
				}
			}
			else
			{
				// Check 1: size hasn't changed (catches emplace/remove)
				if(pool->size() != snapshot.size)
				{
					debug_printf("EnTT-Determinism-Rules.md R1 violation: pool %s size changed from %zu to %zu at entity_id_t %u (context: %s)\n",
						typeMsg, snapshot.size, pool->size(), lastVisited, m_Context);
					ASSERT(false);
				}

				// Check 2: buffer pointer hasn't changed (catches reallocation)
				if(pool->data() != snapshot.data)
				{
					debug_printf("EnTT-Determinism-Rules.md R1 violation: pool %s data pointer changed from %p to %p at entity_id_t %u (context: %s)\n",
						typeMsg, static_cast<const void*>(snapshot.data), static_cast<const void*>(pool->data()), lastVisited, m_Context);
					ASSERT(false);
				}
			}

			// Check next pool if any
			CheckPoolAtIndex_Cheap<Index + 1>(lastVisited);
		}
	}

	// Helper template for expensive O(n) full check (packed array contents)
	template<size_t Index>
	void CheckPoolAtIndex_Full(entity_id_t lastVisited) const
	{
		if constexpr(Index < sizeof...(TWatched))
		{
			using PoolType = std::tuple_element_t<Index, std::tuple<TWatched...>>;
			const auto& snapshot = m_Snapshots[Index];
			// Note: storage<T>() on const registry returns a pointer, not a reference
			const auto* pool = m_Registry.template storage<PoolType>();
			const char* typeMsg = snapshot.typeName ? snapshot.typeName : "Unknown";

			// If pool exists and size matches, verify packed array ordering
			if(pool && pool->size() == snapshot.size && pool->data() == snapshot.data)
			{
				// Check 3: packed array contents haven't changed
				if(!std::equal(snapshot.packed.begin(), snapshot.packed.end(), pool->data()))
				{
					debug_printf("EnTT-Determinism-Rules.md R1 violation: pool %s packed array order changed at entity_id_t %u (context: %s)\n",
						typeMsg, lastVisited, m_Context);
					ASSERT(false);
				}
			}

			// Check next pool if any
			CheckPoolAtIndex_Full<Index + 1>(lastVisited);
		}
	}

	// Helper template to capture snapshots at index
	template<size_t Index>
	void CaptureSnapshotsAtIndex()
	{
		if constexpr(Index < sizeof...(TWatched))
		{
			using PoolType = std::tuple_element_t<Index, std::tuple<TWatched...>>;
			// Note: storage<T>() on const registry returns a pointer, not a reference
			const auto* pool = m_Registry.template storage<PoolType>();

			auto& snapshot = m_Snapshots[Index];
			snapshot.typeName = entt::type_name<PoolType>::value().data();

			if(pool)
			{
				snapshot.size = pool->size();
				snapshot.data = pool->data();
				snapshot.packed.resize(snapshot.size);
				if(snapshot.size > 0)
				{
					std::copy(snapshot.data, snapshot.data + snapshot.size, snapshot.packed.begin());
				}
			}
			else
			{
				// Pool doesn't exist yet (will be emplaced during iteration)
				snapshot.size = 0;
				snapshot.data = nullptr;
				snapshot.packed.clear();
			}

			// Capture next snapshot if any
			CaptureSnapshotsAtIndex<Index + 1>();
		}
	}

#endif  // NDEBUG
};

// Debug version: full implementation with pool structure checks
#ifndef NDEBUG

template<typename... TWatched>
inline CPoolStructureGuard<TWatched...>::CPoolStructureGuard(const entt::registry& registry, const char* context)
	: m_Registry(registry), m_Context(context)
{
	CaptureSnapshotsAtIndex<0>();
}

template<typename... TWatched>
inline CPoolStructureGuard<TWatched...>::~CPoolStructureGuard()
{
	FullCheck(m_LastChecked);
}

template<typename... TWatched>
inline void CPoolStructureGuard<TWatched...>::Check(entity_id_t lastVisited) const
{
	m_LastChecked = lastVisited;
	CheckPoolAtIndex_Cheap<0>(lastVisited);
}

template<typename... TWatched>
inline void CPoolStructureGuard<TWatched...>::FullCheck(entity_id_t lastVisited) const
{
	m_LastChecked = lastVisited;
	CheckPoolAtIndex_Cheap<0>(lastVisited);
	CheckPoolAtIndex_Full<0>(lastVisited);
}

#else  // NDEBUG

// Release version: empty class, all methods are no-ops
template<typename... TWatched>
inline CPoolStructureGuard<TWatched...>::CPoolStructureGuard(const entt::registry&, const char*)
{
}

template<typename... TWatched>
inline CPoolStructureGuard<TWatched...>::~CPoolStructureGuard()
{
}

template<typename... TWatched>
inline void CPoolStructureGuard<TWatched...>::Check(entity_id_t) const
{
}

template<typename... TWatched>
inline void CPoolStructureGuard<TWatched...>::FullCheck(entity_id_t) const
{
}

#endif  // NDEBUG

/**
 * Iterate over entities with the given components in ascending entity_id_t order.
 *
 * This is the correct function to use when iteration order matters for determinism
 * (see EnTT-Determinism-Rules.md R1). It gathers entities from the view, sorts them
 * by entity_id_t, and invokes the callback for each in order.
 *
 * The callback signature must be one of:
 *   - func(entity_id_t id, entt::entity handle)
 *   - func(entity_id_t id, entt::entity handle, TComponents&... data)
 *
 * Example:
 *   ForEachOrderedByEntityId<ComponentA, ComponentB>(registry,
 *     [](entity_id_t id, entt::entity handle, ComponentA& a, ComponentB& b) {
 *       // Process entity id with components
 *     });
 *
 * @tparam TComponents Component types to iterate.
 * @tparam TFunc Callback function type.
 * @param registry The entt registry (non-const).
 * @param func Callback to invoke for each entity.
 */
template<typename... TComponents, typename TFunc>
void ForEachOrderedByEntityId(entt::registry& registry, TFunc&& func)
{
	static_assert(sizeof...(TComponents) > 0,
		"ForEachOrderedByEntityId requires at least one component type");
	static_assert((!std::is_same_v<std::remove_const_t<TComponents>, SimEntityId> && ...),
		"SimEntityId should not be passed to ForEachOrderedByEntityId; it is used internally");
	static_assert((!std::is_empty_v<TComponents> && ...),
		"ForEachOrderedByEntityId component types must be non-empty");

	// Verify the callback is invocable with one of the two expected signatures
	constexpr bool has_component_refs = std::is_invocable_v<TFunc&, entity_id_t, entt::entity, TComponents&...>;
	constexpr bool has_no_refs = std::is_invocable_v<TFunc&, entity_id_t, entt::entity>;
	static_assert(has_component_refs || has_no_refs,
		"ForEachOrderedByEntityId callback must be invocable as either "
		"func(entity_id_t, entt::entity) or func(entity_id_t, entt::entity, TComponents&...)");

	// Get the view and gather entities
	auto view = registry.view<TComponents...>();
	auto& idPool = registry.template storage<SimEntityId>();

	std::vector<std::pair<entity_id_t, entt::entity>> ordered;

	for(auto entity : view)
	{
		ASSERT(idPool.contains(entity));
		ordered.emplace_back(idPool.get(entity).id, entity);
	}

	// Sort by entity_id_t
	std::sort(ordered.begin(), ordered.end(),
		[](const auto& a, const auto& b) { return a.first < b.first; });

	// Debug check: no duplicate entity_id_t values (would indicate corruption)
#ifndef NDEBUG
	auto duplicate_it = std::adjacent_find(ordered.begin(), ordered.end(),
		[](const auto& a, const auto& b) { return a.first == b.first; });
	ASSERT(duplicate_it == ordered.end());
#endif

	// Guard the callback loop
	CPoolStructureGuard<TComponents...> guard(registry, "ForEachOrderedByEntityId");

	// Invoke callback for each entity in order
	for(const auto& [id, handle] : ordered)
	{
		if constexpr(has_component_refs)
		{
			func(id, handle, view.template get<TComponents>(handle)...);
		}
		else
		{
			func(id, handle);
		}
		guard.Check(id);
	}
}

/**
 * Const version of ForEachOrderedByEntityId.
 *
 * @tparam TComponents Component types to iterate (may be const).
 * @tparam TFunc Callback function type.
 * @param registry The entt registry (const).
 * @param func Callback to invoke for each entity.
 */
template<typename... TComponents, typename TFunc>
void ForEachOrderedByEntityId(const entt::registry& registry, TFunc&& func)
{
	static_assert(sizeof...(TComponents) > 0,
		"ForEachOrderedByEntityId requires at least one component type");
	static_assert((!std::is_same_v<std::remove_const_t<TComponents>, SimEntityId> && ...),
		"SimEntityId should not be passed to ForEachOrderedByEntityId; it is used internally");
	static_assert((!std::is_empty_v<TComponents> && ...),
		"ForEachOrderedByEntityId component types must be non-empty");

	// Verify the callback is invocable with one of the two expected signatures
	// Note: const overload uses const TComponents& (components are const-qualified in the view)
	constexpr bool has_component_refs = std::is_invocable_v<TFunc&, entity_id_t, entt::entity, const TComponents&...>;
	constexpr bool has_no_refs = std::is_invocable_v<TFunc&, entity_id_t, entt::entity>;
	static_assert(has_component_refs || has_no_refs,
		"ForEachOrderedByEntityId callback must be invocable as either "
		"func(entity_id_t, entt::entity) or func(entity_id_t, entt::entity, const TComponents&...)");

	// Get the const view and gather entities
	auto view = registry.view<const TComponents...>();
	const auto* idPool = registry.template storage<const SimEntityId>();

	std::vector<std::pair<entity_id_t, entt::entity>> ordered;

	for(auto entity : view)
	{
		ASSERT(idPool && idPool->contains(entity));
		ordered.emplace_back(idPool->get(entity).id, entity);
	}

	// Sort by entity_id_t
	std::sort(ordered.begin(), ordered.end(),
		[](const auto& a, const auto& b) { return a.first < b.first; });

	// Debug check: no duplicate entity_id_t values (would indicate corruption)
#ifndef NDEBUG
	auto duplicate_it = std::adjacent_find(ordered.begin(), ordered.end(),
		[](const auto& a, const auto& b) { return a.first == b.first; });
	ASSERT(duplicate_it == ordered.end());
#endif

	// Guard the callback loop
	CPoolStructureGuard<const TComponents...> guard(registry, "ForEachOrderedByEntityId (const)");

	// Invoke callback for each entity in order
	for(const auto& [id, handle] : ordered)
	{
		if constexpr(has_component_refs)
		{
			func(id, handle, view.template get<TComponents>(handle)...);
		}
		else
		{
			func(id, handle);
		}
		guard.Check(id);
	}
}

}  // namespace EnTTDeterminism

#endif  // INCLUDED_ENTTORDEREDITERATION
