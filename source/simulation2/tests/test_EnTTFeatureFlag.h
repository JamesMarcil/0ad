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

#include "scriptinterface/Interface.h"
#include "simulation2/MessageTypes.h"
#include "simulation2/helpers/Spatial.h"
#include "simulation2/system/ComponentManager.h"
#include "simulation2/system/EnTTConfig.h"
#include "simulation2/system/Entity.h"
#include "simulation2/system/SimContext.h"

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

	void test_component_manager_entity_registry()
	{
		CSimContext context;
		CComponentManager man(context, *g_ScriptContext, true);

		entity_id_t e1 = man.AllocateNewEntity();
		entity_id_t e2 = man.AllocateNewEntity();
		entity_id_t elocal = man.AllocateNewLocalEntity();

		TS_ASSERT_EQUALS(e1, 2u);
		TS_ASSERT_EQUALS(e2, 3u);
		TS_ASSERT_EQUALS(elocal, static_cast<entity_id_t>(FIRST_LOCAL_ENTITY));

#if CONFIG_ENTT_ENTITY_REGISTRY
		TS_ASSERT(man.GetRegistry().valid(static_cast<entt::entity>(e1)));
		TS_ASSERT(man.GetRegistry().valid(static_cast<entt::entity>(e2)));
		TS_ASSERT(man.GetRegistry().valid(static_cast<entt::entity>(elocal)));

		man.ResetState();
		TS_ASSERT(!man.GetRegistry().valid(static_cast<entt::entity>(e1)));
#else
		man.ResetState();
#endif
	}

	void test_entt_message_dispatch_storage()
	{
		entt::registry registry;

		struct MockDispatchComponent : public IComponent
		{
			DEFAULT_MOCK_COMPONENT()
			int receivedMessages{0};
			int lastPayload{0};

			JS::HandleValue GetJSInstance() const override { return JS::NullHandleValue; }
			bool NewJSObject(const Script::Interface&, JS::MutableHandleObject) const override { return false; }

			void HandleMessage(const CMessage& msg, bool) override
			{
				receivedMessages++;
				if (msg.GetType() == MT_TurnStart)
					lastPayload = 100;
			}
		};

		MockDispatchComponent comp1;
		MockDispatchComponent comp2;

		auto e1 = registry.create();
		auto e2 = registry.create();

		const entt::id_type testCid = 42;
		registry.storage<IComponent*>(testCid).emplace(e1, static_cast<IComponent*>(&comp1));
		registry.storage<IComponent*>(testCid).emplace(e2, static_cast<IComponent*>(&comp2));

		TS_ASSERT(registry.storage<IComponent*>(testCid).contains(e1));
		TS_ASSERT(registry.storage<IComponent*>(testCid).contains(e2));

		CMessageTurnStart msg;

		// Targeted message to e1
		const auto* storage = std::as_const(registry).storage<IComponent*>(testCid);
		TS_ASSERT(storage != nullptr);
		if (storage && storage->contains(e1))
		{
			IComponent* c = storage->get(e1);
			c->HandleMessage(msg, false);
		}

		TS_ASSERT_EQUALS(comp1.receivedMessages, 1);
		TS_ASSERT_EQUALS(comp1.lastPayload, 100);
		TS_ASSERT_EQUALS(comp2.receivedMessages, 0);

		// Broadcast message to all registered entities
		for (auto [ent, comp] : storage->each())
		{
			if (comp)
				comp->HandleMessage(msg, false);
		}

		TS_ASSERT_EQUALS(comp1.receivedMessages, 2);
		TS_ASSERT_EQUALS(comp2.receivedMessages, 1);
		TS_ASSERT_EQUALS(comp2.lastPayload, 100);
	}

	void test_entt_spatial_storage()
	{
		entt::registry registry;

		auto e1 = registry.create();
		auto e2 = registry.create();
		auto e3 = registry.create();

		SPositionComponent pos1{ CFixedVector3D(fixed::FromInt(10), fixed::Zero(), fixed::FromInt(20)), CFixedVector3D(), fixed::Zero(), fixed::Zero() };
		SPositionComponent pos2{ CFixedVector3D(fixed::FromInt(50), fixed::Zero(), fixed::FromInt(60)), CFixedVector3D(), fixed::Zero(), fixed::Zero() };
		SPositionComponent pos3{ CFixedVector3D(fixed::FromInt(100), fixed::Zero(), fixed::FromInt(100)), CFixedVector3D(), fixed::Zero(), fixed::Zero() };

		SObstructionComponent obs1{ 1u, 1u, fixed::FromInt(2), fixed::Zero(), fixed::Zero() };
		SObstructionComponent obs2{ 2u, 1u, fixed::FromInt(3), fixed::Zero(), fixed::Zero() };
		SObstructionComponent obs3{ 3u, 1u, fixed::FromInt(4), fixed::Zero(), fixed::Zero() };

		registry.emplace<SPositionComponent>(e1, pos1);
		registry.emplace<SPositionComponent>(e2, pos2);
		registry.emplace<SPositionComponent>(e3, pos3);

		registry.emplace<SObstructionComponent>(e1, obs1);
		registry.emplace<SObstructionComponent>(e2, obs2);
		registry.emplace<SObstructionComponent>(e3, obs3);

		size_t viewCount = 0;
		auto view = registry.view<SPositionComponent, SObstructionComponent>();
		for (auto entity : view)
		{
			auto& p = view.get<SPositionComponent>(entity);
			auto& o = view.get<SObstructionComponent>(entity);
			TS_ASSERT(p.position.X >= fixed::Zero());
			TS_ASSERT(o.radius > fixed::Zero());
			viewCount++;
		}
		TS_ASSERT_EQUALS(viewCount, 3u);

		// Test distance ordering with contiguous sorting
		CFixedVector2D source(fixed::Zero(), fixed::Zero());
		std::vector<std::pair<i64, entt::entity>> distList;
		for (auto entity : view)
		{
			const auto& p = view.get<SPositionComponent>(entity);
			CFixedVector2D diff(p.position.X, p.position.Z);
			i64 d2 = (SQUARE_U64_FIXED(diff.X) + SQUARE_U64_FIXED(diff.Y)) >> 1;
			distList.push_back({d2, entity});
		}
		std::sort(distList.begin(), distList.end());

		TS_ASSERT_EQUALS(distList[0].second, e1);
		TS_ASSERT_EQUALS(distList[1].second, e2);
		TS_ASSERT_EQUALS(distList[2].second, e3);
	}
};
