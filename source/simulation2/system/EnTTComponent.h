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

#ifndef INCLUDED_ENTTCOMPONENT
#define INCLUDED_ENTTCOMPONENT

#include "lib/debug.h"
#include "simulation2/system/ComponentManager.h"
#include "simulation2/system/SimContext.h"
#include <cstddef>
#include <entt/entity/registry.hpp>
#include <tuple>
#include <type_traits>
#include <utility>

/**
 * CRTP mixin for components that store their state in an entt::registry.
 *
 * This mixin is used to bridge the gap between legacy IComponent-based components
 * and EnTT-backed storage. It provides efficient cached access to per-entity data
 * stored in separate structs on the registry, following the split-storage pattern
 * defined in ADR-001 Decision 3 and the storage struct conventions in
 * source/simulation2/docs/ADR-001-EnTT-IComponent-Coexistence.md Appendix A.
 *
 * Usage pattern (for a component CCmpFoo that implements ICmpFoo):
 *
 *   struct FooTemplate { ... };
 *   struct FooState { ... };
 *   struct FooDerived { ... };
 *
 *   class CCmpFoo : public ICmpFoo, public EnTTComponent<CCmpFoo, FooTemplate, FooState, FooDerived>
 *   {
 *       void Init(const CParamNode& paramNode) override {
 *           AttachStorage();
 *           // populate Get<FooTemplate>() and Get<FooState>()
 *       }
 *
 *       void Deinit() override {
 *           DetachStorage();
 *       }
 *
 *       void Deserialize(const CParamNode& paramNode, IDeserializer& deserialize) override {
 *           Init(paramNode);  // Re-derives Template + Derived, idempotent AttachStorage
 *           // deserialize into Get<FooState>()
 *       }
 *   };
 *   DEFAULT_COMPONENT_ALLOCATOR(Foo)
 *   REGISTER_COMPONENT_TYPE(Foo)
 *
 * Design note on allocator: DEFAULT_COMPONENT_ALLOCATOR's plain new CCmpFoo()
 * already produces exactly the individually-heap-allocated, address-stable proxy
 * that ADR-001 Decision 3 requires (referenced at ComponentManager.cpp:793).
 * The mixin adds only a couple of words of member data and needs no custom
 * construction. DO NOT pool or arena-allocate these proxies, as that would move
 * them and break SEntityComponentCache, CmpPtr, and the unit motion manager.
 *
 * Memory note: the mixin's state is (2 + N) words: entt::registry* (8 bytes),
 * entt::entity (4 bytes), and N pool pointers. For N=3 storage structs this is
 * ~36 bytes, which straddles past IComponent's ~32 bytes (vptr + CEntityHandle + ptr).
 * The cached pointers are effectively free: that cache line is already loaded to fetch
 * the vtable pointer for the virtual accessor call. Keeping N small is worthwhile.
 *
 * Critical corollary from ADR-001 Decision 3: never hold a T& or T* obtained from
 * Get<T>() across any call that could emplace or remove that storage type, because
 * entt pools reallocate. Read into a local, or re-Get at point of use.
 *
 * Component-data access only: this mixin provides per-entity access and introduces
 * no registry views or pool iteration. See ADR-001 Decision 4 for cross-component
 * ordering guarantees and the determinism rules that apply.
 *
 * Important: EnTTComponent.h is opt-in and must only be included by .cpp files
 * of components actually backed by the registry. It pulls in entt/entity/registry.hpp
 * which is heavyweight; Component.h does NOT include it.
 *
 * @tparam Derived The final component class (CRTP).
 * @tparam TStorage Parameter pack of storage struct types to attach.
 */
template<typename Derived, typename... TStorage>
class EnTTComponent
{
	// Validate that we have at least one storage type.
	static_assert(sizeof...(TStorage) > 0,
		"EnTTComponent requires at least one storage struct (TStorage...). "
		"Did you intend to create a registry-backed component?");

	// Validate that all storage types are distinct (required for std::get<T>).
	// Counts how many times each type appears in TStorage; all must be exactly 1.
	template<typename T>
	static constexpr std::size_t CountOf = ((std::is_same_v<T, TStorage> ? 1u : 0u) + ...);
	static_assert(((CountOf<TStorage> == 1u) && ...),
		"EnTTComponent's TStorage... types must all be distinct: the cached pool tuple is indexed by type (std::get<PoolPtr<T>>), which is ambiguous for a repeated type.");

	// Validate that no storage type is empty (entt applies empty optimization,
	// so empty types have no per-entity storage).
	static_assert((!std::is_empty_v<TStorage> && ...),
		"All storage structs in EnTTComponent must be non-empty. "
		"Empty types have no per-entity storage in entt and cannot be Get(). "
		"Use a field with a real value, or use a different mechanism (e.g., tag on the entity).");

	// Helper to extract pool pointer type.
	template<typename T>
	using PoolPtr = std::remove_reference_t<decltype(std::declval<entt::registry&>().template storage<T>())>*;

	// Tuple holding cached pool pointers, one per storage type.
	// std::get<PoolPtr<T>>(m_Pools) returns the pool for T.
	// CRITICAL: must be brace-initialized {} to guarantee all pointers start as nullptr.
	// Without it, an unattached Get<T>() would read indeterminate pointers (silent corruption)
	// rather than the loud null-deref that the API promises.
	std::tuple<PoolPtr<TStorage>...> m_Pools{};

	// Cached registry and entity handle for fast access.
	entt::registry* m_Registry = nullptr;
	entt::entity m_Entity = entt::null;

	// Helper to check if T is in TStorage... (used by Get<T>).
	template<typename T>
	static constexpr bool IsValidStorageType = (std::is_same_v<T, TStorage> || ...);

public:
	EnTTComponent() = default;

	// Not copyable or movable (proxies are heap-allocated once and fixed in place).
	EnTTComponent(const EnTTComponent&) = delete;
	EnTTComponent& operator=(const EnTTComponent&) = delete;
	EnTTComponent(EnTTComponent&&) = delete;
	EnTTComponent& operator=(EnTTComponent&&) = delete;

protected:
	// Non-virtual destructor. Nobody ever holds an EnTTComponent* (the mixin
	// is inherited by concrete components which are always accessed as ICmpX*), so there
	// is no need for a virtual destructor. Protected prevents accidental deletion through
	// the mixin interface.
	~EnTTComponent() = default;
	/**
	 * Attach storage to this component's entity on the registry.
	 *
	 * Idempotent: if already attached (m_Registry != nullptr), returns immediately.
	 * Otherwise, resolves the registry and entity handle, emplaces all storage types,
	 * and caches pool pointers for O(1) access. The main body runs exactly once per
	 * component lifetime, because nothing else removes this component's storage and
	 * the singleton constraint (one component instance per entity per type) is enforced
	 * by m_ComponentsByTypeId.
	 *
	 * Must be called in Init() or before the first Get<T>(), typically at the
	 * top of Init(). Safe to call from Deserialize() after Init(), since
	 * idempotency prevents double-attachment.
	 *
	 * ENSURE'd to find the entity valid on the registry.
	 */
	void AttachStorage()
	{
		// If already attached, return early. Since nothing else removes this component's
		// storage, this is a simple early-return pattern, not a full re-check.
		if (m_Registry != nullptr)
			return;

		// First attach: resolve registry and entity handle from derived component.
		Derived& self = static_cast<Derived&>(*this);
		CComponentManager& mgr = self.GetSimContext().GetComponentManager();
		m_Registry = &mgr.GetRegistry();
		m_Entity = mgr.LookupRegistryEntity(self.GetEntityId());

		ENSURE(m_Registry->valid(m_Entity));

		// Emplace all storage types.
		(m_Registry->template emplace<TStorage>(m_Entity), ...);

		// Cache pool pointers for O(1) access.
		((std::get<PoolPtr<TStorage>>(m_Pools) = &m_Registry->template storage<TStorage>()), ...);
	}

	/**
	 * Detach storage from this component's entity.
	 *
	 * Tolerant: if never attached (m_Registry == nullptr), return immediately.
	 * Otherwise, remove all TStorage types from the entity and clear cached
	 * registry/entity/pool pointers.
	 *
	 * Called in Deinit() to clean up before the component proxy is destroyed.
	 * It is safe (and expected) for this to happen: FlushDestroyedComponents
	 * calls Deinit() before DestroyRegistryEntity, so the entity is still valid
	 * in the registry at that point (see ComponentManager.cpp:510-540).
	 *
	 * After DetachStorage(), any Get<T>() will null-deref loudly on release,
	 * or assert-fail on debug, rather than reading stale/freed storage.
	 */
	void DetachStorage()
	{
		if (m_Registry == nullptr)
			return;

		if (m_Registry->valid(m_Entity))
		{
			// Tolerant remove: ignores types not present.
			(m_Registry->template remove<TStorage>(m_Entity), ...);
		}

		m_Registry = nullptr;
		m_Entity = entt::null;
		m_Pools = std::tuple<PoolPtr<TStorage>...>{};
	}

	/**
	 * Check if storage is currently attached and ready for Get<T>().
	 * @return true if AttachStorage() has been called and storage is present.
	 */
	[[nodiscard]] bool IsStorageAttached() const
	{
		return m_Registry != nullptr && m_Registry->valid(m_Entity)
			&& m_Registry->template all_of<TStorage...>(m_Entity);
	}

	/**
	 * Get a reference to storage of type T for this entity.
	 *
	 * Must only be called with T being one of the TStorage types.
	 * Uses cached pool pointer to perform O(1) sparse-set lookup,
	 * avoiding the hash map lookup that registry::get<T>() would incur.
	 *
	 * On debug: asserts storage is attached.
	 * On release: null-derefs immediately if not attached, a loud failure.
	 *
	 * @return Reference to the component data of type T for this entity.
	 */
	template<typename T>
	T& Get()
	{
		static_assert(IsValidStorageType<T>,
			"Get<T>() may only be called with T being one of this component's declared "
			"storage types (TStorage...). Cross-component access must go through ICmpX "
			"interfaces, not direct registry storage access. See ADR-001 Decision 5.");

		auto* pool = std::get<PoolPtr<T>>(m_Pools);
		ASSERT(pool != nullptr);  // AttachStorage() not called
		ASSERT(pool->contains(m_Entity));  // storage missing / already removed
		return pool->get(m_Entity);
	}

	/**
	 * Const version of Get<T>().
	 */
	template<typename T>
	const T& Get() const
	{
		static_assert(IsValidStorageType<T>,
			"Get<T>() may only be called with T being one of this component's declared "
			"storage types (TStorage...). Cross-component access must go through ICmpX "
			"interfaces, not direct registry storage access. See ADR-001 Decision 5.");

		auto* pool = std::get<PoolPtr<T>>(m_Pools);
		ASSERT(pool != nullptr);  // AttachStorage() not called
		ASSERT(pool->contains(m_Entity));  // storage missing / already removed
		return pool->get(m_Entity);
	}
};

#endif  // INCLUDED_ENTTCOMPONENT
