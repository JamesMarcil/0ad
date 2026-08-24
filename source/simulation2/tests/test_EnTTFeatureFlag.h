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
#include "simulation2/components/ICmpTest.h"
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

		man.ResetState();
	}

	// Regression test for a Slot-not-available crash in entt::sparse_set: 0 A.D.'s local/normal
	// tag bit (bit 29 of entity_id_t) falls inside entt::entity's 12-bit version field rather than
	// its 20-bit index field, so a normal and a local entity whose low-20-bit counters coincide
	// cast to the *same* entt::entity index. Before CComponentManager::GetComponentStorageId()
	// namespaced local vs. normal component storage, giving both entities the same component type
	// crashed entt::sparse_set::try_emplace with ENTT_ASSERT(elem == null, "Slot not available").
	void test_component_manager_local_normal_index_collision()
	{
		CSimContext context;
		CComponentManager man(context, *g_ScriptContext, true);
		man.LoadComponentTypes();

		CParamNode noParam;

		// Allocate through the same public counters real gameplay uses, rather than picking IDs
		// by hand: m_NextEntityId starts at SYSTEM_ENTITY+1, so the first normal entity is 2; the
		// third local entity is FIRST_LOCAL_ENTITY+2, which has the same low-20-bit index as id 2.
		entity_id_t normalId = man.AllocateNewEntity();
		man.AllocateNewLocalEntity();
		man.AllocateNewLocalEntity();
		entity_id_t localId = man.AllocateNewLocalEntity();

		TS_ASSERT_EQUALS(normalId, 2u);
		TS_ASSERT_EQUALS(localId, static_cast<entity_id_t>(FIRST_LOCAL_ENTITY) + 2);

		CEntityHandle hndNormal = man.LookupEntityHandle(normalId, true);
		CEntityHandle hndLocal = man.LookupEntityHandle(localId, true);

#if CONFIG_ENTT_ENTITY_REGISTRY
		// Confirm the raw indices really do collide, so this test actually exercises the bug.
		entt::entity enttNormal = static_cast<entt::entity>(normalId);
		entt::entity enttLocal = static_cast<entt::entity>(localId);
		TS_ASSERT_EQUALS(entt::to_entity(enttNormal), entt::to_entity(enttLocal));
		TS_ASSERT_DIFFERS(entt::to_version(enttNormal), entt::to_version(enttLocal));
#endif

		TS_ASSERT(man.AddComponent(hndNormal, CID_Test1A, noParam));
		TS_ASSERT(man.AddComponent(hndLocal, CID_Test1A, noParam));

		IComponent* cmpNormal = man.QueryInterface(normalId, IID_Test1);
		IComponent* cmpLocal = man.QueryInterface(localId, IID_Test1);
		TS_ASSERT(cmpNormal != NULL);
		TS_ASSERT(cmpLocal != NULL);
		TS_ASSERT_DIFFERS(cmpNormal, cmpLocal);

		man.ResetState();

		TS_ASSERT(man.QueryInterface(normalId, IID_Test1) == NULL);
		TS_ASSERT(man.QueryInterface(localId, IID_Test1) == NULL);
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

		auto e1 = static_cast<entt::entity>(100);
		auto e2 = static_cast<entt::entity>(101);

		const entt::id_type testCid = 42;
		auto& storage = registry.storage<IComponent*>(testCid);
		storage.emplace(e1, static_cast<IComponent*>(&comp1));
		storage.emplace(e2, static_cast<IComponent*>(&comp2));

		TS_ASSERT(storage.contains(e1));
		TS_ASSERT(storage.contains(e2));

		CMessageTurnStart msg;

		// Targeted message to e1
		if (storage.contains(e1))
		{
			IComponent* c = storage.get(e1);
			c->HandleMessage(msg, false);
		}

		TS_ASSERT_EQUALS(comp1.receivedMessages, 1);
		TS_ASSERT_EQUALS(comp1.lastPayload, 100);
		TS_ASSERT_EQUALS(comp2.receivedMessages, 0);

		// Component destruction cleanup from storage
		storage.erase(e1);
		TS_ASSERT(!storage.contains(e1));
		TS_ASSERT(storage.contains(e2));

		if (storage.contains(e1))
		{
			IComponent* c = storage.get(e1);
			c->HandleMessage(msg, false);
		}
		TS_ASSERT_EQUALS(comp1.receivedMessages, 1);
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

	void test_entt_unit_motion()
	{
		entt::registry registry;

		auto e1 = registry.create();
		auto e2 = registry.create();

		SPositionComponent pos1{ CFixedVector3D(fixed::FromInt(10), fixed::Zero(), fixed::FromInt(10)), CFixedVector3D(), fixed::Zero(), fixed::Zero() };
		SPositionComponent pos2{ CFixedVector3D(fixed::FromInt(50), fixed::Zero(), fixed::FromInt(50)), CFixedVector3D(), fixed::Zero(), fixed::Zero() };

		SMotionState motion1;
		motion1.pos = CFixedVector2D(fixed::FromInt(10), fixed::FromInt(10));
		motion1.speed = fixed::FromInt(5);
		motion1.maxSpeed = fixed::FromInt(10);
		motion1.heading = fixed::Zero();
		motion1.turnRate = fixed::FromFloat(3.0f);
		motion1.needUpdate = true;

		SMotionState motion2;
		motion2.pos = CFixedVector2D(fixed::FromInt(50), fixed::FromInt(50));
		motion2.speed = fixed::FromInt(6);
		motion2.maxSpeed = fixed::FromInt(12);
		motion2.heading = fixed::Pi();
		motion2.turnRate = fixed::FromFloat(3.0f);
		motion2.needUpdate = true;

		SWaypointData wp1{ CFixedVector2D(fixed::FromInt(20), fixed::FromInt(10)), fixed::FromInt(1), fixed::FromInt(5) };
		SWaypointData wp2{ CFixedVector2D(fixed::FromInt(50), fixed::FromInt(30)), fixed::FromInt(1), fixed::FromInt(6) };

		registry.emplace<SPositionComponent>(e1, pos1);
		registry.emplace<SPositionComponent>(e2, pos2);
		registry.emplace<SMotionState>(e1, motion1);
		registry.emplace<SMotionState>(e2, motion2);
		registry.emplace<SWaypointData>(e1, wp1);
		registry.emplace<SWaypointData>(e2, wp2);

		auto view = registry.view<SPositionComponent, SMotionState, SWaypointData>();
		size_t count = 0;
		const fixed dt = fixed::FromFloat(0.2f);
		for (auto entity : view)
		{
			auto& pos = view.get<SPositionComponent>(entity);
			auto& mot = view.get<SMotionState>(entity);
			auto& wp = view.get<SWaypointData>(entity);

			CFixedVector2D toTarget = wp.targetPos - mot.pos;
			if (!toTarget.IsZero())
			{
				fixed moveDist = mot.speed.Multiply(dt);
				mot.pos.X += moveDist;
				pos.position.X = mot.pos.X;
			}
			count++;
		}

		TS_ASSERT_EQUALS(count, 2u);
		TS_ASSERT(registry.get<SMotionState>(e1).pos.X > fixed::FromInt(10));
		TS_ASSERT(registry.get<SPositionComponent>(e1).position.X > fixed::FromInt(10));
	}

	void test_entt_render_submit()
	{
		entt::registry registry;

		auto e1 = registry.create();
		auto e2 = registry.create();

		SRenderTransform transform1{ CVector3D(10.0f, 0.0f, 20.0f), CVector3D(0.0f, 0.0f, 0.0f), 1.0f, CBoundingSphere(CVector3D(10.0f, 0.0f, 20.0f), 2.0f), false, true, false };
		SRenderTransform transform2{ CVector3D(30.0f, 0.0f, 40.0f), CVector3D(0.0f, 0.0f, 0.0f), 1.0f, CBoundingSphere(CVector3D(30.0f, 0.0f, 40.0f), 2.0f), false, true, false };

		SRenderModelKey model1{ 1u, 100u, 1u, 0u };
		SRenderModelKey model2{ 2u, 200u, 2u, 0u };

		registry.emplace<SRenderTransform>(e1, transform1);
		registry.emplace<SRenderTransform>(e2, transform2);
		registry.emplace<SRenderModelKey>(e1, model1);
		registry.emplace<SRenderModelKey>(e2, model2);

		auto view = registry.view<SRenderTransform, SRenderModelKey>();
		size_t submitCount = 0;
		for (auto entity : view)
		{
			auto& t = view.get<SRenderTransform>(entity);
			auto& m = view.get<SRenderModelKey>(entity);
			if (!t.culled && t.inWorld)
			{
				submitCount++;
				TS_ASSERT(m.modelIndex > 0);
			}
		}

		TS_ASSERT_EQUALS(submitCount, 2u);
	}
};
