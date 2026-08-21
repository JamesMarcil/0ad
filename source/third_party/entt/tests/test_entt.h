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

#include <entt/entt.hpp>
#include "maths/FixedVector3D.h"
#include "ps/CStr.h"

class TestEnTT : public CxxTest::TestSuite
{
public:
	struct Position
	{
		float x{0.0f};
		float y{0.0f};
		float z{0.0f};
	};

	struct Velocity
	{
		float dx{0.0f};
		float dy{0.0f};
		float dz{0.0f};
	};

	struct UnitData
	{
		CStr name;
		CFixedVector3D spawnPoint;
		uint32_t health{100};
	};

	// Test 1: Basic Entity & Component Lifecycle (CRUD)
	void test_registry_entity_lifecycle()
	{
		entt::registry registry;

		entt::entity e1 = registry.create();
		TS_ASSERT(registry.valid(e1));

		// Emplace component
		registry.emplace<Position>(e1, 10.0f, 20.0f, 30.0f);
		TS_ASSERT(registry.all_of<Position>(e1));

		// Read component
		const Position& pos = registry.get<Position>(e1);
		TS_ASSERT_EQUALS(pos.x, 10.0f);
		TS_ASSERT_EQUALS(pos.y, 20.0f);
		TS_ASSERT_EQUALS(pos.z, 30.0f);

		// Replace component
		registry.replace<Position>(e1, 50.0f, 60.0f, 70.0f);
		TS_ASSERT_EQUALS(registry.get<Position>(e1).x, 50.0f);

		// Remove component
		registry.remove<Position>(e1);
		TS_ASSERT(!registry.all_of<Position>(e1));

		// Destroy entity
		registry.destroy(e1);
		TS_ASSERT(!registry.valid(e1));
	}

	// Test 2: Views and Multi-Component Iteration
	void test_registry_view_iteration()
	{
		entt::registry registry;

		for (int i = 0; i < 10; ++i)
		{
			auto entity = registry.create();
			registry.emplace<Position>(entity, static_cast<float>(i), 0.0f, 0.0f);
			if (i % 2 == 0)
				registry.emplace<Velocity>(entity, 1.0f, 2.0f, 3.0f);
		}

		// Iterate over entities with both Position and Velocity
		auto view = registry.view<Position, const Velocity>();
		size_t count = 0;
		view.each([&count](Position& pos, const Velocity& vel) {
			pos.x += vel.dx;
			pos.y += vel.dy;
			pos.z += vel.dz;
			++count;
		});

		TS_ASSERT_EQUALS(count, 5u);
	}

	// Test 3: Reactive Observers & Signals
	void test_signals_and_listeners()
	{
		entt::registry registry;
		size_t constructCount = 0;

		auto listener = [&constructCount](entt::registry&, entt::entity) {
			++constructCount;
		};

		registry.on_construct<Position>().connect<&decltype(listener)::operator()>(&listener);

		auto e1 = registry.create();
		registry.emplace<Position>(e1, 1.0f, 2.0f, 3.0f);

		auto e2 = registry.create();
		registry.emplace<Position>(e2, 4.0f, 5.0f, 6.0f);

		TS_ASSERT_EQUALS(constructCount, 2u);
	}

	// Test 4: Engine Type & Struct Compatibility (CStr, CFixedVector3D)
	void test_engine_types_compatibility()
	{
		entt::registry registry;
		auto e = registry.create();

		registry.emplace<UnitData>(e, CStr("Hoplite"), CFixedVector3D(fixed::FromInt(5), fixed::FromInt(0), fixed::FromInt(10)), 150u);

		TS_ASSERT(registry.all_of<UnitData>(e));
		const auto& data = registry.get<UnitData>(e);
		TS_ASSERT_EQUALS(data.name, "Hoplite");
		TS_ASSERT_EQUALS(data.health, 150u);
		TS_ASSERT_EQUALS(data.spawnPoint.X.ToInt_RoundToNearest(), 5);
		TS_ASSERT_EQUALS(data.spawnPoint.Z.ToInt_RoundToNearest(), 10);
	}
};
