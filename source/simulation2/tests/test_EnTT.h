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

/**
 * Placeholder / smoke-test demonstrating that the bundled EnTT
 * (https://github.com/skypjack/entt) headers are wired up correctly.
 *
 * EnTT is header-only (see libraries/source/entt/build.sh, which just
 * installs the headers - there's nothing to compile or link), so this
 * simply exercises a minimal registry/view round-trip to prove the
 * include path is set up (see the "entt" entry in
 * build/premake/extern_libs5.lua) and that the headers are usable from
 * engine code.
 */

#include "lib/self_test.h"

#include <entt/entt.hpp>

class TestEnTT : public CxxTest::TestSuite
{
public:
	void test_basic_registry()
	{
		entt::registry registry;

		struct Position
		{
			float x;
			float y;
		};

		const entt::entity entity = registry.create();
		registry.emplace<Position>(entity, 1.f, 2.f);

		TS_ASSERT(registry.valid(entity));
		TS_ASSERT(registry.all_of<Position>(entity));

		size_t count = 0;
		for (const entt::entity e : registry.view<Position>())
		{
			const Position& position = registry.get<Position>(e);
			TS_ASSERT_EQUALS(position.x, 1.f);
			TS_ASSERT_EQUALS(position.y, 2.f);
			++count;
		}
		TS_ASSERT_EQUALS(count, 1u);

		registry.destroy(entity);
		TS_ASSERT(!registry.valid(entity));
	}
};
