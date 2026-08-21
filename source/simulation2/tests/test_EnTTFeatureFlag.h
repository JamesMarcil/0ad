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

#include "lib/self_test.h"

#include "simulation2/system/EnTTConfig.h"
#include "simulation2/system/Entity.h"

#include <entt/entt.hpp>
#include <type_traits>

class TestEnTTFeatureFlag : public CxxTest::TestSuite
{
public:
	void test_macro_definitions()
	{
		// Master switch is defined
#ifdef CONFIG_ENABLE_ENTT_ECS
		TS_ASSERT(CONFIG_ENABLE_ENTT_ECS == 0 || CONFIG_ENABLE_ENTT_ECS == 1);
#else
		TS_FAIL("CONFIG_ENABLE_ENTT_ECS must be defined");
#endif

		// Subsystem flags are defined and default to master switch value
#ifdef CONFIG_ENTT_ENTITY_REGISTRY
		TS_ASSERT(CONFIG_ENTT_ENTITY_REGISTRY == 0 || CONFIG_ENTT_ENTITY_REGISTRY == 1);
#else
		TS_FAIL("CONFIG_ENTT_ENTITY_REGISTRY must be defined");
#endif

#ifdef CONFIG_ENTT_MESSAGE_DISPATCH
		TS_ASSERT(CONFIG_ENTT_MESSAGE_DISPATCH == 0 || CONFIG_ENTT_MESSAGE_DISPATCH == 1);
#else
		TS_FAIL("CONFIG_ENTT_MESSAGE_DISPATCH must be defined");
#endif

#ifdef CONFIG_ENTT_SPATIAL_STORAGE
		TS_ASSERT(CONFIG_ENTT_SPATIAL_STORAGE == 0 || CONFIG_ENTT_SPATIAL_STORAGE == 1);
#else
		TS_FAIL("CONFIG_ENTT_SPATIAL_STORAGE must be defined");
#endif

#ifdef CONFIG_ENTT_UNIT_MOTION
		TS_ASSERT(CONFIG_ENTT_UNIT_MOTION == 0 || CONFIG_ENTT_UNIT_MOTION == 1);
#else
		TS_FAIL("CONFIG_ENTT_UNIT_MOTION must be defined");
#endif

#ifdef CONFIG_ENTT_RENDER_SUBMIT
		TS_ASSERT(CONFIG_ENTT_RENDER_SUBMIT == 0 || CONFIG_ENTT_RENDER_SUBMIT == 1);
#else
		TS_FAIL("CONFIG_ENTT_RENDER_SUBMIT must be defined");
#endif
	}

	void test_entt_version_and_types()
	{
		TS_ASSERT_LESS_THAN_EQUALS(4, ENTT_VERSION_MAJOR);

		// Verify entt::entity integral conversion compatibility with entity_id_t
		static_assert(sizeof(entt::entity) == sizeof(entity_id_t), "entt::entity and entity_id_t must be 32-bit");
		static_assert(std::is_same_v<std::underlying_type_t<entt::entity>, uint32_t>, "entt::entity underlying type must be uint32_t");

		entity_id_t id = 42;
		entt::entity e = static_cast<entt::entity>(id);
		TS_ASSERT_EQUALS(static_cast<entity_id_t>(entt::to_integral(e)), 42u);
	}

	void test_hybrid_registry_interop()
	{
		entt::registry registry;
		auto e = registry.create();
		TS_ASSERT(registry.valid(e));

		struct TestComponent
		{
			entity_id_t ownerId;
			int value;
		};

		registry.emplace<TestComponent>(e, 100u, 999);
		TS_ASSERT(registry.all_of<TestComponent>(e));

		const auto& comp = registry.get<TestComponent>(e);
		TS_ASSERT_EQUALS(comp.ownerId, 100u);
		TS_ASSERT_EQUALS(comp.value, 999);

		registry.destroy(e);
		TS_ASSERT(!registry.valid(e));
	}
};
