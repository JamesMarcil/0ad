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

#include "lib/file/file_system.h"
#include "lib/file/vfs/vfs.h"
#include "lib/path.h"
#include "lib/types.h"
#include "maths/Fixed.h"
#include "ps/CLogger.h"
#include "ps/Errors.h"
#include "ps/Filesystem.h"
#include "ps/XML/Xeromyces.h"
#include "scriptinterface/Interface.h"
#include "simulation2/MessageTypes.h"
#include "simulation2/components/ICmpTemplateManager.h"
#include "simulation2/components/ICmpTest.h"
#include "simulation2/system/Component.h"
#include "simulation2/system/ComponentTest.h"
#include "simulation2/system/Entity.h"

#include <cstddef>
#include <entt/entt.hpp>
#include <memory>
#include <optional>
#include <sstream>
#include <string>

#define TS_ASSERT_STREAM(stream, len, buffer) \
	TS_ASSERT_EQUALS(stream.str().length(), (size_t)len); \
	TS_ASSERT_SAME_DATA(stream.str().data(), buffer, len)


#define TS_ASSERT_THROWS_PSERROR(e, t, s) \
	TS_ASSERT_THROWS_EQUALS(e, const t& ex, std::string(ex.what()), s)

// Mock components for testing coexistence with EnTT-backed components.
class MockTest1 : public ICmpTest1
{
public:
	DEFAULT_MOCK_COMPONENT()
	int GetX() override { return 9999; }
};

class MockTest2 : public ICmpTest2
{
public:
	DEFAULT_MOCK_COMPONENT()
	int GetX() override { return 4242; }
};

class TestComponentManager : public CxxTest::TestSuite
{
	std::optional<CXeromycesEngine> xeromycesEngine;
public:
	void setUp()
	{
		g_VFS = CreateVfs();
		TS_ASSERT_OK(g_VFS->Mount(L"", DataDir() / "mods" / "_test.sim" / "", VFS_MOUNT_MUST_EXIST));
		TS_ASSERT_OK(g_VFS->Mount(L"cache", DataDir() / "_testcache" / "", 0, VFS_MAX_PRIORITY));
		xeromycesEngine.emplace();
	}

	void tearDown()
	{
		xeromycesEngine.reset();
		g_VFS.reset();
		DeleteDirectory(DataDir()/"_testcache");
	}

	void test_Load()
	{
		CSimContext context;
		CComponentManager man(context, *g_ScriptContext);
		man.LoadComponentTypes();
	}

	void test_LookupCID()
	{
		CSimContext context;
		CComponentManager man(context, *g_ScriptContext);
		man.LoadComponentTypes();

		TS_ASSERT_EQUALS(man.LookupCID("Test1A"), (int)CID_Test1A);
		TS_ASSERT_EQUALS(man.LookupCID("Test1B"), (int)CID_Test1B);
	}

	void test_AllocateNewEntity()
	{
		CSimContext context;
		CComponentManager man(context, *g_ScriptContext);

		TS_ASSERT_EQUALS(man.AllocateNewEntity(), (u32)2);
		TS_ASSERT_EQUALS(man.AllocateNewEntity(), (u32)3);
		TS_ASSERT_EQUALS(man.AllocateNewEntity(), (u32)4);
		TS_ASSERT_EQUALS(man.AllocateNewEntity(100), (u32)100);
		TS_ASSERT_EQUALS(man.AllocateNewEntity(), (u32)101);
		// TODO:
		// TS_ASSERT_EQUALS(man.AllocateNewEntity(3), (u32)102);

		TS_ASSERT_EQUALS(man.AllocateNewLocalEntity(), (u32)FIRST_LOCAL_ENTITY);
		TS_ASSERT_EQUALS(man.AllocateNewLocalEntity(), (u32)FIRST_LOCAL_ENTITY+1);

		man.ResetState();

		TS_ASSERT_EQUALS(man.AllocateNewEntity(), (u32)2);
		TS_ASSERT_EQUALS(man.AllocateNewEntity(3), (u32)3);
		TS_ASSERT_EQUALS(man.AllocateNewLocalEntity(), (u32)FIRST_LOCAL_ENTITY);
	}

	void test_rng()
	{
		// Ensure we get the same random number with the same seed
		double first;
		{
			CSimContext context;
			CComponentManager man(context, *g_ScriptContext);
			man.SetRNGSeed(123);

			if (!man.m_ScriptInterface.MathRandom(first))
				TS_FAIL("Couldn't get random number!");
		}

		double second;
		{
			CSimContext context;
			CComponentManager man(context, *g_ScriptContext);
			man.SetRNGSeed(123);

			if (!man.m_ScriptInterface.MathRandom(second))
				TS_FAIL("Couldn't get random number!");
		}

		TS_ASSERT_EQUALS(first, second);
	}

	void test_AddComponent_errors()
	{
		CSimContext context;
		CComponentManager man(context, *g_ScriptContext);
		man.LoadComponentTypes();
		CEntityHandle hnd1 = man.AllocateEntityHandle(1);

		CParamNode noParam;
		TS_ASSERT(man.AddComponent(hnd1, CID_Test1A, noParam));

		{
			TestLogger log;
			TS_ASSERT(! man.AddComponent(hnd1, 12345, noParam));
			TS_ASSERT_STR_CONTAINS(log.GetOutput(), "ERROR: Invalid component id 12345");
		}

		{
			TestLogger log;
			TS_ASSERT(! man.AddComponent(hnd1, CID_Test1B, noParam));
			TS_ASSERT_STR_CONTAINS(log.GetOutput(), "ERROR: Multiple components for interface ");
		}
	}

	void test_QueryInterface()
	{
		CSimContext context;
		CComponentManager man(context, *g_ScriptContext);
		man.LoadComponentTypes();

		entity_id_t ent1 = 1, ent2 = 2;
		CEntityHandle hnd1 = man.AllocateEntityHandle(ent1);
		CEntityHandle hnd2 = man.AllocateEntityHandle(ent2);
		CParamNode noParam;

		man.AddComponent(hnd1, CID_Test1A, noParam);
		TS_ASSERT(man.QueryInterface(ent1, IID_Test1) != NULL);
		TS_ASSERT(man.QueryInterface(ent1, IID_Test2) == NULL);
		TS_ASSERT(man.QueryInterface(ent2, IID_Test1) == NULL);
		TS_ASSERT(man.QueryInterface(ent2, IID_Test2) == NULL);

		man.AddComponent(hnd2, CID_Test1B, noParam);
		TS_ASSERT(man.QueryInterface(ent2, IID_Test1) != NULL);
		TS_ASSERT(man.QueryInterface(ent2, IID_Test2) == NULL);
		man.AddComponent(hnd2, CID_Test2A, noParam);
		TS_ASSERT(man.QueryInterface(ent2, IID_Test1) != NULL);
		TS_ASSERT(man.QueryInterface(ent2, IID_Test2) != NULL);
	}

	void test_SendMessage()
	{
		CSimContext context;
		CComponentManager man(context, *g_ScriptContext);
		man.LoadComponentTypes();

		entity_id_t ent1 = 1, ent2 = 2, ent3 = 3, ent4 = 4;
		CEntityHandle hnd1 = man.AllocateEntityHandle(ent1);
		CEntityHandle hnd2 = man.AllocateEntityHandle(ent2);
		CEntityHandle hnd3 = man.AllocateEntityHandle(ent3);
		CEntityHandle hnd4 = man.AllocateEntityHandle(ent4);
		CParamNode noParam;

		man.AddComponent(hnd1, CID_Test1A, noParam);
		man.AddComponent(hnd2, CID_Test1B, noParam);
		man.AddComponent(hnd3, CID_Test2A, noParam);
		man.AddComponent(hnd4, CID_Test1A, noParam);
		man.AddComponent(hnd4, CID_Test2A, noParam);

		CMessageTurnStart msg1;
		CMessageUpdate msg2(fixed::FromInt(100));
		CMessageInterpolate msg3(0, 0, 0);

		TS_ASSERT_EQUALS(static_cast<ICmpTest1*> (man.QueryInterface(ent1, IID_Test1))->GetX(), 11000);
		TS_ASSERT_EQUALS(static_cast<ICmpTest1*> (man.QueryInterface(ent2, IID_Test1))->GetX(), 12000);
		TS_ASSERT_EQUALS(static_cast<ICmpTest2*> (man.QueryInterface(ent3, IID_Test2))->GetX(), 21000);
		TS_ASSERT_EQUALS(static_cast<ICmpTest1*> (man.QueryInterface(ent4, IID_Test1))->GetX(), 11000);
		TS_ASSERT_EQUALS(static_cast<ICmpTest2*> (man.QueryInterface(ent4, IID_Test2))->GetX(), 21000);

		// Test_1A subscribed locally to msg1, nothing subscribed globally
		man.PostMessage(ent1, msg1);
		man.PostMessage(ent1, msg2);

		TS_ASSERT_EQUALS(static_cast<ICmpTest1*> (man.QueryInterface(ent1, IID_Test1))->GetX(), 11001);
		TS_ASSERT_EQUALS(static_cast<ICmpTest1*> (man.QueryInterface(ent2, IID_Test1))->GetX(), 12000);
		TS_ASSERT_EQUALS(static_cast<ICmpTest2*> (man.QueryInterface(ent3, IID_Test2))->GetX(), 21000);
		TS_ASSERT_EQUALS(static_cast<ICmpTest1*> (man.QueryInterface(ent4, IID_Test1))->GetX(), 11000);
		TS_ASSERT_EQUALS(static_cast<ICmpTest2*> (man.QueryInterface(ent4, IID_Test2))->GetX(), 21000);

		man.BroadcastMessage(msg1);

		TS_ASSERT_EQUALS(static_cast<ICmpTest1*> (man.QueryInterface(ent1, IID_Test1))->GetX(), 11002);
		TS_ASSERT_EQUALS(static_cast<ICmpTest1*> (man.QueryInterface(ent2, IID_Test1))->GetX(), 12000);
		TS_ASSERT_EQUALS(static_cast<ICmpTest2*> (man.QueryInterface(ent3, IID_Test2))->GetX(), 21050);
		TS_ASSERT_EQUALS(static_cast<ICmpTest1*> (man.QueryInterface(ent4, IID_Test1))->GetX(), 11001);
		TS_ASSERT_EQUALS(static_cast<ICmpTest2*> (man.QueryInterface(ent4, IID_Test2))->GetX(), 21050);

		// Test_1B, Test_2A subscribed locally to msg2, nothing subscribed globally
		man.BroadcastMessage(msg2);

		TS_ASSERT_EQUALS(static_cast<ICmpTest1*> (man.QueryInterface(ent1, IID_Test1))->GetX(), 11002);
		TS_ASSERT_EQUALS(static_cast<ICmpTest1*> (man.QueryInterface(ent2, IID_Test1))->GetX(), 12010);
		TS_ASSERT_EQUALS(static_cast<ICmpTest2*> (man.QueryInterface(ent3, IID_Test2))->GetX(), 21150);
		TS_ASSERT_EQUALS(static_cast<ICmpTest1*> (man.QueryInterface(ent4, IID_Test1))->GetX(), 11001);
		TS_ASSERT_EQUALS(static_cast<ICmpTest2*> (man.QueryInterface(ent4, IID_Test2))->GetX(), 21150);

		// Test_1A subscribed locally to msg3, Test_1B subscribed globally
		man.BroadcastMessage(msg3);

		TS_ASSERT_EQUALS(static_cast<ICmpTest1*> (man.QueryInterface(ent1, IID_Test1))->GetX(), 11004); // local
		TS_ASSERT_EQUALS(static_cast<ICmpTest1*> (man.QueryInterface(ent2, IID_Test1))->GetX(), 12030); // global
		TS_ASSERT_EQUALS(static_cast<ICmpTest2*> (man.QueryInterface(ent3, IID_Test2))->GetX(), 21150);
		TS_ASSERT_EQUALS(static_cast<ICmpTest1*> (man.QueryInterface(ent4, IID_Test1))->GetX(), 11003); // local
		TS_ASSERT_EQUALS(static_cast<ICmpTest2*> (man.QueryInterface(ent4, IID_Test2))->GetX(), 21150);

		man.PostMessage(ent1, msg3);

		TS_ASSERT_EQUALS(static_cast<ICmpTest1*> (man.QueryInterface(ent1, IID_Test1))->GetX(), 11006); // local
		TS_ASSERT_EQUALS(static_cast<ICmpTest1*> (man.QueryInterface(ent2, IID_Test1))->GetX(), 12050); // global
		TS_ASSERT_EQUALS(static_cast<ICmpTest2*> (man.QueryInterface(ent3, IID_Test2))->GetX(), 21150);
		TS_ASSERT_EQUALS(static_cast<ICmpTest1*> (man.QueryInterface(ent4, IID_Test1))->GetX(), 11003); // local - skipped
		TS_ASSERT_EQUALS(static_cast<ICmpTest2*> (man.QueryInterface(ent4, IID_Test2))->GetX(), 21150);
	}

	void test_ParamNode()
	{
		CSimContext context;
		CComponentManager man(context, *g_ScriptContext);
		man.LoadComponentTypes();

		entity_id_t ent1 = 1, ent2 = 2;
		CEntityHandle hnd1 = man.AllocateEntityHandle(ent1);
		CEntityHandle hnd2 = man.AllocateEntityHandle(ent2);
		CParamNode noParam;

		CParamNode testParam;
		TS_ASSERT_EQUALS(CParamNode::LoadXMLString(testParam, "<x>1234</x>"), PSRETURN_OK);

		man.AddComponent(hnd1, CID_Test1A, noParam);
		man.AddComponent(hnd2, CID_Test1A, testParam);

		TS_ASSERT_EQUALS(static_cast<ICmpTest1*> (man.QueryInterface(ent1, IID_Test1))->GetX(), 11000);
		TS_ASSERT_EQUALS(static_cast<ICmpTest1*> (man.QueryInterface(ent2, IID_Test1))->GetX(), 1234);
	}

	void test_script_basic()
	{
		CSimContext context;
		CComponentManager man(context, *g_ScriptContext);
		man.LoadComponentTypes();
		TS_ASSERT(man.LoadScript(L"simulation/components/test.js"));

		TS_ASSERT_EQUALS(man.LookupCID("TestScript1A"), (int)CID__LastNative);
		TS_ASSERT_EQUALS(man.LookupCID("TestScript1B"), (int)CID__LastNative+1);

		entity_id_t ent1 = 1, ent2 = 2, ent3 = 3;
		CEntityHandle hnd1 = man.AllocateEntityHandle(ent1);
		CEntityHandle hnd2 = man.AllocateEntityHandle(ent2);
		CEntityHandle hnd3 = man.AllocateEntityHandle(ent3);
		CParamNode noParam;

		man.AddComponent(hnd1, CID_Test1A, noParam);
		man.AddComponent(hnd2, man.LookupCID("TestScript1A"), noParam);
		man.AddComponent(hnd3, man.LookupCID("TestScript1B"), noParam);
		man.AddComponent(hnd3, man.LookupCID("TestScript2A"), noParam);

		TS_ASSERT_EQUALS(static_cast<ICmpTest1*> (man.QueryInterface(ent1, IID_Test1))->GetX(), 11000);
		TS_ASSERT_EQUALS(static_cast<ICmpTest1*> (man.QueryInterface(ent2, IID_Test1))->GetX(), 101000);
		TS_ASSERT_EQUALS(static_cast<ICmpTest1*> (man.QueryInterface(ent3, IID_Test1))->GetX(), 102000);
		TS_ASSERT_EQUALS(static_cast<ICmpTest2*> (man.QueryInterface(ent3, IID_Test2))->GetX(), 201000);

		CMessageTurnStart msg1;
		CMessageUpdate msg2(fixed::FromInt(25));

		man.BroadcastMessage(msg1);

		TS_ASSERT_EQUALS(static_cast<ICmpTest1*> (man.QueryInterface(ent1, IID_Test1))->GetX(), 11001);
		TS_ASSERT_EQUALS(static_cast<ICmpTest1*> (man.QueryInterface(ent2, IID_Test1))->GetX(), 101001);
		TS_ASSERT_EQUALS(static_cast<ICmpTest1*> (man.QueryInterface(ent3, IID_Test1))->GetX(), 102001);
		TS_ASSERT_EQUALS(static_cast<ICmpTest2*> (man.QueryInterface(ent3, IID_Test2))->GetX(), 201000);

		man.BroadcastMessage(msg2);

		TS_ASSERT_EQUALS(static_cast<ICmpTest1*> (man.QueryInterface(ent1, IID_Test1))->GetX(), 11001);
		TS_ASSERT_EQUALS(static_cast<ICmpTest1*> (man.QueryInterface(ent2, IID_Test1))->GetX(), 101001);
		TS_ASSERT_EQUALS(static_cast<ICmpTest1*> (man.QueryInterface(ent3, IID_Test1))->GetX(), 102001);
		TS_ASSERT_EQUALS(static_cast<ICmpTest2*> (man.QueryInterface(ent3, IID_Test2))->GetX(), 201025);
	}

	void test_script_helper_basic()
	{
		CSimContext context;
		CComponentManager man(context, *g_ScriptContext);
		man.LoadComponentTypes();
		TS_ASSERT(man.LoadScript(L"simulation/components/test-helper.js"));
		TS_ASSERT(man.LoadScript(L"simulation/helpers/test-helper.js"));

		entity_id_t ent1 = 1;
		CEntityHandle hnd1 = man.AllocateEntityHandle(ent1);
		CParamNode noParam;

		man.AddComponent(hnd1, man.LookupCID("TestScript1_Helper"), noParam);

		TS_ASSERT_EQUALS(static_cast<ICmpTest1*> (man.QueryInterface(ent1, IID_Test1))->GetX(), 3);
	}

	void test_script_global_helper()
	{
		CSimContext context;
		CComponentManager man(context, *g_ScriptContext);
		man.LoadComponentTypes();
		TS_ASSERT(man.LoadScript(L"simulation/components/test-global-helper.js"));

		entity_id_t ent1 = 1;
		CEntityHandle hnd1 = man.AllocateEntityHandle(ent1);
		CParamNode noParam;

		man.AddComponent(hnd1, man.LookupCID("TestScript1_GlobalHelper"), noParam);

		TS_ASSERT_EQUALS(static_cast<ICmpTest1*> (man.QueryInterface(ent1, IID_Test1))->GetX(), 2);
	}

	void test_script_interface()
	{
		CSimContext context;
		CComponentManager man(context, *g_ScriptContext);
		man.LoadComponentTypes();
		TS_ASSERT(man.LoadScript(L"simulation/components/interfaces/test-interface.js"));
		TS_ASSERT(man.LoadScript(L"simulation/components/test-interface.js"));

		entity_id_t ent1 = 1;
		CEntityHandle hnd1 = man.AllocateEntityHandle(ent1);
		CParamNode noParam;

		man.AddComponent(hnd1, man.LookupCID("TestScript1_Interface"), noParam);
		man.AddComponent(hnd1, man.LookupCID("TestScript2_Interface"), noParam);

		TS_ASSERT_EQUALS(static_cast<ICmpTest1*> (man.QueryInterface(ent1, IID_Test1))->GetX(), 1000 + IID__LastNative);
	}

	void test_script_errors()
	{
		CSimContext context;
		CComponentManager man(context, *g_ScriptContext);
		ScriptTestSetup(man.m_ScriptInterface);
		man.LoadComponentTypes();

		TestLogger log;
		TS_ASSERT(man.LoadScript(L"simulation/components/error.js"));
		// The following exception is caught and dropped by the JS script, and should not appear in the logs.
		TS_ASSERT_STR_NOT_CONTAINS(log.GetOutput(), "ERROR: JavaScript error: simulation/components/error.js line 4\nInvalid interface id");
		// The following exception is not caught by the JS script.
		TS_ASSERT_STR_CONTAINS(log.GetOutput(), "ERROR: No script wrapper found for interface id 12345");
	}

	void test_script_entityID()
	{
		CSimContext context;
		CComponentManager man(context, *g_ScriptContext);
		ScriptTestSetup(man.m_ScriptInterface);
		man.LoadComponentTypes();
		TS_ASSERT(man.LoadScript(L"simulation/components/test-entityid.js"));

		entity_id_t ent1 = 1, ent2 = 234;
		CEntityHandle hnd1 = man.AllocateEntityHandle(ent1);
		CEntityHandle hnd2 = man.AllocateEntityHandle(ent2);
		CParamNode noParam;

		man.AddComponent(hnd1, man.LookupCID("TestScript1A"), noParam);
		man.AddComponent(hnd2, man.LookupCID("TestScript1A"), noParam);

		TS_ASSERT_EQUALS(static_cast<ICmpTest1*> (man.QueryInterface(ent1, IID_Test1))->GetX(), (int)ent1);
		TS_ASSERT_EQUALS(static_cast<ICmpTest1*> (man.QueryInterface(ent2, IID_Test1))->GetX(), (int)ent2);
	}

	void test_script_QueryInterface()
	{
		CSimContext context;
		CComponentManager man(context, *g_ScriptContext);
		man.LoadComponentTypes();
		TS_ASSERT(man.LoadScript(L"simulation/components/test-query.js"));

		entity_id_t ent1 = 1, ent2 = 2;
		CEntityHandle hnd1 = man.AllocateEntityHandle(ent1);
		CEntityHandle hnd2 = man.AllocateEntityHandle(ent2);
		CParamNode noParam;

		man.AddComponent(hnd1, man.LookupCID("TestScript1A"), noParam);
		man.AddComponent(hnd1, man.LookupCID("TestScript2A"), noParam);
		man.AddComponent(hnd2, man.LookupCID("TestScript1A"), noParam);
		man.AddComponent(hnd2, CID_Test2A, noParam);

		TS_ASSERT_EQUALS(static_cast<ICmpTest1*> (man.QueryInterface(ent1, IID_Test1))->GetX(), 400);
		TS_ASSERT_EQUALS(static_cast<ICmpTest1*> (man.QueryInterface(ent2, IID_Test1))->GetX(), 21000);
	}

	void test_script_AddEntity()
	{
		CSimContext context;
		CComponentManager man(context, *g_ScriptContext);
		man.LoadComponentTypes();
		TS_ASSERT(man.LoadScript(L"simulation/components/test-addentity.js"));
		TS_ASSERT(man.LoadScript(L"simulation/components/addentity/test-addentity.js"));
		man.InitSystemEntity();

		entity_id_t ent1 = man.AllocateNewEntity();
		entity_id_t ent2 = ent1 + 2;
		CEntityHandle hnd1 = man.AllocateEntityHandle(ent1);
		CParamNode noParam;

		TS_ASSERT(man.AddComponent(man.GetSystemEntity(), CID_TemplateManager, noParam));

		TS_ASSERT(man.AddComponent(hnd1, man.LookupCID("TestScript1_AddEntity"), noParam));

		TS_ASSERT(man.QueryInterface(ent2, IID_Test1) == NULL);
		TS_ASSERT(man.QueryInterface(ent2, IID_Test2) == NULL);

		{
			TestLogger logger; // ignore bogus-template warnings
			TS_ASSERT_EQUALS(static_cast<ICmpTest1*> (man.QueryInterface(ent1, IID_Test1))->GetX(), (int)ent2);
		}

		TS_ASSERT(man.QueryInterface(ent2, IID_Test1) != NULL);
		TS_ASSERT(man.QueryInterface(ent2, IID_Test2) != NULL);

		TS_ASSERT_EQUALS(static_cast<ICmpTest1*> (man.QueryInterface(ent2, IID_Test1))->GetX(), 999);
		TS_ASSERT_EQUALS(static_cast<ICmpTest2*> (man.QueryInterface(ent2, IID_Test2))->GetX(), 12345);
	}

	void test_script_AddLocalEntity()
	{
		CSimContext context;
		CComponentManager man(context, *g_ScriptContext);
		man.LoadComponentTypes();
		TS_ASSERT(man.LoadScript(L"simulation/components/test-addentity.js"));
		TS_ASSERT(man.LoadScript(L"simulation/components/addentity/test-addentity.js"));
		man.InitSystemEntity();

		entity_id_t ent1 = man.AllocateNewEntity();
		entity_id_t ent2 = man.AllocateNewLocalEntity() + 2;
		CEntityHandle hnd1 = man.AllocateEntityHandle(ent1);
		CParamNode noParam;

		TS_ASSERT(man.AddComponent(man.GetSystemEntity(), CID_TemplateManager, noParam));

		TS_ASSERT(man.AddComponent(hnd1, man.LookupCID("TestScript1_AddLocalEntity"), noParam));

		TS_ASSERT(man.QueryInterface(ent2, IID_Test1) == NULL);
		TS_ASSERT(man.QueryInterface(ent2, IID_Test2) == NULL);

		{
			TestLogger logger; // ignore bogus-template warnings
			TS_ASSERT_EQUALS(static_cast<ICmpTest1*> (man.QueryInterface(ent1, IID_Test1))->GetX(), (int)ent2);
		}

		TS_ASSERT(man.QueryInterface(ent2, IID_Test1) != NULL);
		TS_ASSERT(man.QueryInterface(ent2, IID_Test2) != NULL);

		TS_ASSERT_EQUALS(static_cast<ICmpTest1*> (man.QueryInterface(ent2, IID_Test1))->GetX(), 999);
		TS_ASSERT_EQUALS(static_cast<ICmpTest2*> (man.QueryInterface(ent2, IID_Test2))->GetX(), 12345);
	}

	void test_script_DestroyEntity()
	{
		CSimContext context;
		CComponentManager man(context, *g_ScriptContext);
		man.LoadComponentTypes();
		TS_ASSERT(man.LoadScript(L"simulation/components/test-destroyentity.js"));

		entity_id_t ent1 = 10;
		CEntityHandle hnd1 = man.AllocateEntityHandle(ent1);
		CParamNode noParam;

		TS_ASSERT(man.AddComponent(hnd1, man.LookupCID("TestScript1_DestroyEntity"), noParam));

		TS_ASSERT(man.QueryInterface(ent1, IID_Test1) != NULL);
		static_cast<ICmpTest1*> (man.QueryInterface(ent1, IID_Test1))->GetX();
		TS_ASSERT(man.QueryInterface(ent1, IID_Test1) != NULL);
		man.FlushDestroyedComponents();
		TS_ASSERT(man.QueryInterface(ent1, IID_Test1) == NULL);
	}

	void test_script_messages()
	{
		CSimContext context;
		CComponentManager man(context, *g_ScriptContext);
		man.LoadComponentTypes();
		TS_ASSERT(man.LoadScript(L"simulation/components/test-msg.js"));

		entity_id_t ent1 = 1, ent2 = 2, ent3 = 3;
		CEntityHandle hnd1 = man.AllocateEntityHandle(ent1);
		CEntityHandle hnd2 = man.AllocateEntityHandle(ent2);
		CEntityHandle hnd3 = man.AllocateEntityHandle(ent3);
		CParamNode noParam;

		man.AddComponent(hnd1, man.LookupCID("TestScript1A"), noParam);
		man.AddComponent(hnd1, man.LookupCID("TestScript2A"), noParam);
		man.AddComponent(hnd2, man.LookupCID("TestScript1A"), noParam);
		man.AddComponent(hnd2, CID_Test2A, noParam);
		man.AddComponent(hnd3, man.LookupCID("TestScript1B"), noParam);

		TS_ASSERT_EQUALS(static_cast<ICmpTest1*> (man.QueryInterface(ent1, IID_Test1))->GetX(), 100);
		TS_ASSERT_EQUALS(static_cast<ICmpTest1*> (man.QueryInterface(ent2, IID_Test1))->GetX(), 100);
		TS_ASSERT_EQUALS(static_cast<ICmpTest2*> (man.QueryInterface(ent2, IID_Test2))->GetX(), 21000);
		TS_ASSERT_EQUALS(static_cast<ICmpTest1*> (man.QueryInterface(ent3, IID_Test1))->GetX(), 100);

		// This GetX broadcasts messages
		TS_ASSERT_EQUALS(static_cast<ICmpTest2*> (man.QueryInterface(ent1, IID_Test2))->GetX(), 200);

		TS_ASSERT_EQUALS(static_cast<ICmpTest1*> (man.QueryInterface(ent1, IID_Test1))->GetX(), 650);
		TS_ASSERT_EQUALS(static_cast<ICmpTest1*> (man.QueryInterface(ent2, IID_Test1))->GetX(), 5150);
		TS_ASSERT_EQUALS(static_cast<ICmpTest2*> (man.QueryInterface(ent2, IID_Test2))->GetX(), 26050);
		TS_ASSERT_EQUALS(static_cast<ICmpTest1*> (man.QueryInterface(ent3, IID_Test1))->GetX(), 5650);
	}

	void test_script_template()
	{
		CSimContext context;
		CComponentManager man(context, *g_ScriptContext);
		man.LoadComponentTypes();
		TS_ASSERT(man.LoadScript(L"simulation/components/test-param.js"));

		entity_id_t ent1 = 1, ent2 = 2;
		CEntityHandle hnd1 = man.AllocateEntityHandle(ent1);
		CEntityHandle hnd2 = man.AllocateEntityHandle(ent2);
		CParamNode noParam;

		CParamNode testParam;
		TS_ASSERT_EQUALS(CParamNode::LoadXMLString(testParam, "<node><x>1</x><y>1<z w='100'><a>1000</a></z>0</y></node>"), PSRETURN_OK);

		man.AddComponent(hnd1, man.LookupCID("TestScript1_Init"), noParam);
		man.AddComponent(hnd2, man.LookupCID("TestScript1_Init"), testParam.GetChild("node"));

		TS_ASSERT_EQUALS(static_cast<ICmpTest1*> (man.QueryInterface(ent1, IID_Test1))->GetX(), 100);
		TS_ASSERT_EQUALS(static_cast<ICmpTest1*> (man.QueryInterface(ent2, IID_Test1))->GetX(), 1+10+100+1000);
	}

	void test_script_template_readonly()
	{
		CSimContext context;
		CComponentManager man(context, *g_ScriptContext);
		man.LoadComponentTypes();
		TS_ASSERT(man.LoadScript(L"simulation/components/test-param.js"));

		entity_id_t ent1 = 1, ent2 = 2;
		CEntityHandle hnd1 = man.AllocateEntityHandle(ent1);
		CEntityHandle hnd2 = man.AllocateEntityHandle(ent2);
		CParamNode noParam;

		CParamNode testParam;
		TS_ASSERT_EQUALS(CParamNode::LoadXMLString(testParam, "<x>100</x>"), PSRETURN_OK);

		man.AddComponent(hnd1, man.LookupCID("TestScript1_readonly"), testParam);
		man.AddComponent(hnd2, man.LookupCID("TestScript1_readonly"), testParam);

		TS_ASSERT_EQUALS(static_cast<ICmpTest1*> (man.QueryInterface(ent1, IID_Test1))->GetX(), 102);
		TS_ASSERT_EQUALS(static_cast<ICmpTest1*> (man.QueryInterface(ent2, IID_Test1))->GetX(), 102);
	}

	void test_script_hotload()
	{
		CSimContext context;
		CComponentManager man(context, *g_ScriptContext);
		man.LoadComponentTypes();

		TS_ASSERT(man.LoadScript(L"simulation/components/test-hotload1.js"));

		entity_id_t ent1 = 1, ent2 = 2, ent3 = 3, ent4 = 4;
		CEntityHandle hnd1 = man.AllocateEntityHandle(ent1);
		CEntityHandle hnd2 = man.AllocateEntityHandle(ent2);
		CEntityHandle hnd3 = man.AllocateEntityHandle(ent3);
		CEntityHandle hnd4 = man.AllocateEntityHandle(ent4);

		CParamNode testParam;
		TS_ASSERT_EQUALS(CParamNode::LoadXMLString(testParam, "<x>100</x>"), PSRETURN_OK);

		man.AddComponent(hnd1, man.LookupCID("HotloadA"), testParam);
		man.AddComponent(hnd2, man.LookupCID("HotloadB"), testParam);
		man.AddComponent(hnd2, man.LookupCID("HotloadC"), testParam);

		TS_ASSERT_EQUALS(static_cast<ICmpTest1*> (man.QueryInterface(ent1, IID_Test1))->GetX(), 100);
		TS_ASSERT_EQUALS(static_cast<ICmpTest1*> (man.QueryInterface(ent2, IID_Test1))->GetX(), 200);

		TS_ASSERT(man.LoadScript(L"simulation/components/test-hotload2.js", true));

		TS_ASSERT_EQUALS(static_cast<ICmpTest1*> (man.QueryInterface(ent1, IID_Test1))->GetX(), 1000);
		TS_ASSERT_EQUALS(static_cast<ICmpTest1*> (man.QueryInterface(ent2, IID_Test1))->GetX(), 200);

		man.AddComponent(hnd3, man.LookupCID("HotloadA"), testParam);
		man.AddComponent(hnd4, man.LookupCID("HotloadB"), testParam);

		TS_ASSERT_EQUALS(static_cast<ICmpTest1*> (man.QueryInterface(ent3, IID_Test1))->GetX(), 1000);
		TS_ASSERT_EQUALS(static_cast<ICmpTest1*> (man.QueryInterface(ent4, IID_Test1))->GetX(), 200);
	}

	void test_script_modding()
	{
		CSimContext context;
		CComponentManager man(context, *g_ScriptContext);
		man.LoadComponentTypes();

		CParamNode testParam;
		TS_ASSERT_EQUALS(CParamNode::LoadXMLString(testParam, "<x>100</x>"), PSRETURN_OK);

		entity_id_t ent1 = 1, ent2 = 2;
		CEntityHandle hnd1 = man.AllocateEntityHandle(ent1);
		CEntityHandle hnd2 = man.AllocateEntityHandle(ent2);

		TS_ASSERT(man.LoadScript(L"simulation/components/test-modding1.js"));

		man.AddComponent(hnd1, man.LookupCID("Modding"), testParam);
		TS_ASSERT_EQUALS(static_cast<ICmpTest1*> (man.QueryInterface(ent1, IID_Test1))->GetX(), 100);

		TS_ASSERT(man.LoadScript(L"simulation/components/test-modding2.js"));

		TS_ASSERT_EQUALS(static_cast<ICmpTest1*> (man.QueryInterface(ent1, IID_Test1))->GetX(), 1000);
		man.AddComponent(hnd2, man.LookupCID("Modding"), testParam);
		TS_ASSERT_EQUALS(static_cast<ICmpTest1*> (man.QueryInterface(ent2, IID_Test1))->GetX(), 1000);
	}

	void test_serialization()
	{
		CSimContext context;
		CComponentManager man(context, *g_ScriptContext);
		man.LoadComponentTypes();

		entity_id_t ent1 = 10, ent2 = 20, ent3 = FIRST_LOCAL_ENTITY;
		CEntityHandle hnd1 = man.AllocateEntityHandle(ent1);
		CEntityHandle hnd2 = man.AllocateEntityHandle(ent2);
		CEntityHandle hnd3 = man.AllocateEntityHandle(ent3);
		CParamNode noParam;

		CParamNode testParam;
		TS_ASSERT_EQUALS(CParamNode::LoadXMLString(testParam, "<x>1234</x>"), PSRETURN_OK);

		man.AddComponent(hnd1, CID_Test1A, noParam);
		man.AddComponent(hnd1, CID_Test2A, noParam);
		man.AddComponent(hnd2, CID_Test1A, testParam);
		man.AddComponent(hnd3, CID_Test2A, noParam);

		TS_ASSERT_EQUALS(static_cast<ICmpTest1*> (man.QueryInterface(ent1, IID_Test1))->GetX(), 11000);
		TS_ASSERT_EQUALS(static_cast<ICmpTest2*> (man.QueryInterface(ent1, IID_Test2))->GetX(), 21000);
		TS_ASSERT_EQUALS(static_cast<ICmpTest1*> (man.QueryInterface(ent2, IID_Test1))->GetX(), 1234);
		TS_ASSERT_EQUALS(static_cast<ICmpTest2*> (man.QueryInterface(ent3, IID_Test2))->GetX(), 21000);

		std::stringstream debugStream;
		TS_ASSERT(man.DumpDebugState(debugStream, true));
		TS_ASSERT_STR_EQUALS(debugStream.str(),
				"rng: \"78606\"\n"
				"entities:\n"
				"- id: 10\n"
				"  Test1A:\n"
				"    x: 11000\n"
				"  Test2A:\n"
				"    x: 21000\n"
				"\n"
				"- id: 20\n"
				"  Test1A:\n"
				"    x: 1234\n"
				"\n"
				"- id: 536870912\n"
				"  type: local\n"
				"  Test2A:\n"
				"    x: 21000\n"
				"\n"
		);

		std::string hash;
		TS_ASSERT(man.ComputeStateHash(hash, false));
		TS_ASSERT_EQUALS(hash.length(), (size_t)16);
		TS_ASSERT_SAME_DATA(hash.data(), "\x3c\x25\x6e\x22\x58\x23\x09\x58\x38\xca\xb2\x1e\x0b\x8c\xac\xcf", 16);
		// echo -en "\x05\x00\x00\x0078606\x02\0\0\0\x01\0\0\0\x0a\0\0\0\xf8\x2a\0\0\x14\0\0\0\xd2\x04\0\0\x04\0\0\0\x0a\0\0\0\x08\x52\0\0" | md5sum | perl -pe 's/([0-9a-f]{2})/\\x$1/g'
		//           ^^^^^^^^ rng ^^^^^^^^ ^^next^^ ^^Test1A^^ ^^^ent1^^ ^^^11000^^^ ^^^ent2^^ ^^^1234^^^ ^^Test2A^^ ^^ent1^^ ^^^21000^^^

		std::stringstream stateStream;
		TS_ASSERT(man.SerializeState(stateStream));
		TS_ASSERT_STREAM(stateStream, 73,
				"\x05\x00\x00\x00\x37\x38\x36\x30\x36" // RNG
				"\x02\x00\x00\x00" // next entity ID
				"\x00\x00\x00\x00" // num system component types
				"\x02\x00\x00\x00" // num component types
				"\x06\x00\x00\x00Test1A"
				"\x02\x00\x00\x00" // num ents
				"\x0a\x00\x00\x00" // ent1
				"\xf8\x2a\x00\x00" // 11000
				"\x14\x00\x00\x00" // ent2
				"\xd2\x04\x00\x00" // 1234
				"\x06\x00\x00\x00Test2A"
				"\x01\x00\x00\x00" // num ents
				"\x0a\x00\x00\x00" // ent1
				"\x08\x52\x00\x00" // 21000
		);

		CSimContext context2;
		CComponentManager man2(context2, *g_ScriptContext);
		man2.LoadComponentTypes();

		TS_ASSERT(man2.QueryInterface(ent1, IID_Test1) == NULL);
		TS_ASSERT(man2.QueryInterface(ent1, IID_Test2) == NULL);
		TS_ASSERT(man2.QueryInterface(ent2, IID_Test1) == NULL);
		TS_ASSERT(man2.QueryInterface(ent3, IID_Test2) == NULL);

		TS_ASSERT(man2.DeserializeState(stateStream));

		TS_ASSERT_EQUALS(static_cast<ICmpTest1*> (man2.QueryInterface(ent1, IID_Test1))->GetX(), 11000);
		TS_ASSERT_EQUALS(static_cast<ICmpTest2*> (man2.QueryInterface(ent1, IID_Test2))->GetX(), 21000);
		TS_ASSERT_EQUALS(static_cast<ICmpTest1*> (man2.QueryInterface(ent2, IID_Test1))->GetX(), 1234);
		TS_ASSERT(man2.QueryInterface(ent3, IID_Test2) == NULL);
	}

	void test_script_serialization()
	{
		CSimContext context;

		CComponentManager man(context, *g_ScriptContext);
		ScriptTestSetup(man.m_ScriptInterface);
		man.LoadComponentTypes();
		TS_ASSERT(man.LoadScript(L"simulation/components/test-serialize.js"));

		entity_id_t ent1 = 1, ent2 = 2, ent3 = 3, ent4 = 4;
		CEntityHandle hnd1 = man.AllocateEntityHandle(ent1);
		CEntityHandle hnd2 = man.AllocateEntityHandle(ent2);
		CEntityHandle hnd3 = man.AllocateEntityHandle(ent3);
		CEntityHandle hnd4 = man.AllocateEntityHandle(ent4);
		CParamNode noParam;

		CParamNode testParam;
		TS_ASSERT_EQUALS(CParamNode::LoadXMLString(testParam, "<x>1234</x>"), PSRETURN_OK);

		man.AddComponent(hnd1, man.LookupCID("TestScript1_values"), testParam);
		man.AddComponent(hnd2, man.LookupCID("TestScript1_entity"), testParam);

		// TODO: Since the upgrade to SpiderMonkey v24 this test won't be able to correctly represent
		// non-tree structures because sharp variables were removed (bug 566700).
		// This also affects the debug serializer and it could make sense to implement correct serialization again.
		man.AddComponent(hnd3, man.LookupCID("TestScript1_nontree"), testParam);

		man.AddComponent(hnd4, man.LookupCID("TestScript1_custom"), testParam);

		TS_ASSERT_EQUALS(static_cast<ICmpTest1*> (man.QueryInterface(ent1, IID_Test1))->GetX(), 1234);
		{
			TestLogger log; // swallow warnings about this.entity being read-only
			TS_ASSERT_EQUALS(static_cast<ICmpTest1*> (man.QueryInterface(ent2, IID_Test1))->GetX(), (int)ent2);
		}
		TS_ASSERT_EQUALS(static_cast<ICmpTest1*> (man.QueryInterface(ent3, IID_Test1))->GetX(), 8);

		std::stringstream debugStream;
		TS_ASSERT(man.DumpDebugState(debugStream, true));
		TS_ASSERT_STR_EQUALS(debugStream.str(),
				"rng: \"78606\"\n\
entities:\n\
- id: 1\n\
  TestScript1_values:\n\
    comp: {\n\
  \"x\": 1234,\n\
  \"str\": \"this is a string\",\n\
  \"things\": {\n\
    \"a\": 1,\n\
    \"b\": \"2\",\n\
    \"c\": [\n\
      3,\n\
      \"4\",\n\
      [\n\
        5,\n\
        []\n\
      ]\n\
    ]\n\
  }\n\
}\n\
\n\
- id: 2\n\
  TestScript1_entity:\n\
    comp: {}\n\
\n\
- id: 3\n\
  TestScript1_nontree:\n\
    comp: ({x:[[2], [2], [], {y:[2]}]})\n\
\n\
- id: 4\n\
  TestScript1_custom:\n\
    comp: {\n\
  \"c\": 1\n\
}\n\
\n"
		);

		std::stringstream stateStream;
		TS_ASSERT(man.SerializeState(stateStream));

		CSimContext context2;
		CComponentManager man2(context2, *g_ScriptContext);
		man2.LoadComponentTypes();
		TS_ASSERT(man2.LoadScript(L"simulation/components/test-serialize.js"));

		TS_ASSERT(man2.QueryInterface(ent1, IID_Test1) == NULL);
		TS_ASSERT(man2.DeserializeState(stateStream));
		TS_ASSERT_EQUALS(static_cast<ICmpTest1*> (man2.QueryInterface(ent1, IID_Test1))->GetX(), 1234);
		{
			TestLogger log; // swallow warnings about this.entity being read-only
			TS_ASSERT_EQUALS(static_cast<ICmpTest1*> (man2.QueryInterface(ent2, IID_Test1))->GetX(), (int)ent2);
		}
		TS_ASSERT_EQUALS(static_cast<ICmpTest1*> (man2.QueryInterface(ent3, IID_Test1))->GetX(), 12);
	}

	void test_script_serialization_errors()
	{
		CSimContext context;
		CComponentManager man(context, *g_ScriptContext);
		man.LoadComponentTypes();
		TS_ASSERT(man.LoadScript(L"simulation/components/test-serialize.js"));

		entity_id_t ent1 = 1;
		CEntityHandle hnd1 = man.AllocateEntityHandle(ent1);
		CParamNode noParam;

		man.AddComponent(hnd1, man.LookupCID("TestScript1_getter"), noParam);

		TestLogger log;
		std::stringstream stateStream;
		TS_ASSERT_THROWS_PSERROR(man.SerializeState(stateStream), PSERROR_Serialize_ScriptError, "Cannot serialize property getters");
		// (The script will die if the getter is executed)
	}

	void test_script_serialization_template()
	{
		CSimContext context;

		CComponentManager man(context, *g_ScriptContext);
		man.LoadComponentTypes();
		TS_ASSERT(man.LoadScript(L"simulation/components/test-serialize.js"));
		man.InitSystemEntity();

		entity_id_t ent2 = 2;
		CEntityHandle hnd2 = man.AllocateEntityHandle(ent2);
		CParamNode noParam;

		// The template manager takes care of reloading templates on deserialization,
		// so we need to use it here
		TS_ASSERT(man.AddComponent(man.GetSystemEntity(), CID_TemplateManager, noParam));
		ICmpTemplateManager* tempMan = static_cast<ICmpTemplateManager*> (man.QueryInterface(SYSTEM_ENTITY, IID_TemplateManager));

		const CParamNode* testParam = tempMan->LoadTemplate(ent2, "template-serialize");

		man.AddComponent(hnd2, man.LookupCID("TestScript1_consts"), testParam->GetChild("TestScript1_consts"));

		TS_ASSERT_EQUALS(static_cast<ICmpTest1*> (man.QueryInterface(ent2, IID_Test1))->GetX(), 12347);

		std::stringstream stateStream;
		TS_ASSERT(man.SerializeState(stateStream));

		CSimContext context2;
		CComponentManager man2(context2, *g_ScriptContext);
		man2.LoadComponentTypes();
		TS_ASSERT(man2.LoadScript(L"simulation/components/test-serialize.js"));

		TS_ASSERT(man2.DeserializeState(stateStream));
		TS_ASSERT_EQUALS(static_cast<ICmpTest1*> (man2.QueryInterface(ent2, IID_Test1))->GetX(), 12347);
	}

	void test_dynamic_subscription()
	{
		CSimContext context;
		CComponentManager man(context, *g_ScriptContext);
		man.LoadComponentTypes();

		entity_id_t ent1 = 1;
		CEntityHandle hnd1 = man.AllocateEntityHandle(ent1);

		CParamNode noParam;

		man.AddComponent(hnd1, CID_Test1A, noParam);
		man.AddComponent(hnd1, CID_Test2A, noParam);

		man.DynamicSubscriptionNonsync(MT_RenderSubmit, man.QueryInterface(ent1, IID_Test1), true);
		man.DynamicSubscriptionNonsync(MT_RenderSubmit, man.QueryInterface(ent1, IID_Test2), true);

		man.DestroyComponentsSoon(ent1);
		man.FlushDestroyedComponents();
	}

	// EnTT coexistence layer (ADR-001, bd_0ad-1u1.1.2). No component uses the registry yet;
	// these tests only assert the lifecycle plumbing (registry + entity_id_t <-> entt::entity
	// mapping) stays consistent across a fresh sim, a ResetState, and a save/load cycle.

	void test_EnTT_freshSim()
	{
		CSimContext context;
		CComponentManager man(context, *g_ScriptContext);
		man.LoadComponentTypes();
		man.InitSystemEntity();

		// SYSTEM_ENTITY is always the first registry entity, deterministically.
		entt::entity sysHandle = man.LookupRegistryEntity(SYSTEM_ENTITY);
		TS_ASSERT(man.GetRegistry().valid(sysHandle));
		TS_ASSERT(man.GetRegistry().get<SimEntityId>(sysHandle).id == SYSTEM_ENTITY);

		entity_id_t ent1 = man.AllocateNewEntity();
		CEntityHandle hnd1 = man.AllocateEntityHandle(ent1);
		entt::entity handle1 = man.LookupRegistryEntity(ent1);
		TS_ASSERT(man.GetRegistry().valid(handle1));
		TS_ASSERT(man.GetRegistry().get<SimEntityId>(handle1).id == ent1);

		entity_id_t localEnt = man.AllocateNewLocalEntity();
		CEntityHandle localHnd = man.AllocateEntityHandle(localEnt);
		entt::entity localHandle = man.LookupRegistryEntity(localEnt);
		TS_ASSERT(man.GetRegistry().valid(localHandle));
		TS_ASSERT(man.GetRegistry().get<SimEntityId>(localHandle).id == localEnt);

		// Destroying an entity removes its registry entity and mapping.
		man.DestroyComponentsSoon(ent1);
		man.FlushDestroyedComponents();
		TS_ASSERT(man.LookupRegistryEntity(ent1) == entt::null);
		TS_ASSERT(!man.GetRegistry().valid(handle1));

		(void)hnd1;
		(void)localHnd;
	}

	void test_EnTT_resetState()
	{
		CSimContext context;
		CComponentManager man(context, *g_ScriptContext);
		man.LoadComponentTypes();
		man.InitSystemEntity();

		entity_id_t ent1 = man.AllocateNewEntity();
		man.AllocateEntityHandle(ent1);
		TS_ASSERT(man.GetRegistry().valid(man.LookupRegistryEntity(ent1)));

		man.ResetState();

		// The mapping is fully cleared, and SYSTEM_ENTITY no longer resolves.
		TS_ASSERT(man.LookupRegistryEntity(SYSTEM_ENTITY) == entt::null);
		TS_ASSERT(man.LookupRegistryEntity(ent1) == entt::null);

		// A subsequent fresh init produces the same deterministic first registry entity id
		// (index 0) as an untouched, freshly-constructed manager would.
		man.InitSystemEntity();
		entt::entity sysHandleAfterReset = man.LookupRegistryEntity(SYSTEM_ENTITY);
		TS_ASSERT(man.GetRegistry().valid(sysHandleAfterReset));

		CSimContext freshContext;
		CComponentManager freshMan(freshContext, *g_ScriptContext);
		freshMan.LoadComponentTypes();
		freshMan.InitSystemEntity();
		entt::entity freshSysHandle = freshMan.LookupRegistryEntity(SYSTEM_ENTITY);
		TS_ASSERT_EQUALS((u32)entt::to_integral(sysHandleAfterReset), (u32)entt::to_integral(freshSysHandle));
	}

	void test_EnTT_saveLoadCycle()
	{
		CSimContext context;
		CComponentManager man(context, *g_ScriptContext);
		man.LoadComponentTypes();

		entity_id_t ent1 = 10, ent2 = 20;
		CEntityHandle hnd1 = man.AllocateEntityHandle(ent1);
		CEntityHandle hnd2 = man.AllocateEntityHandle(ent2);
		CParamNode noParam;

		man.AddComponent(hnd1, CID_Test1A, noParam);
		man.AddComponent(hnd2, CID_Test1A, noParam);

		TS_ASSERT(man.GetRegistry().valid(man.LookupRegistryEntity(ent1)));
		TS_ASSERT(man.GetRegistry().valid(man.LookupRegistryEntity(ent2)));

		std::stringstream stateStream;
		TS_ASSERT(man.SerializeState(stateStream));

		CSimContext context2;
		CComponentManager man2(context2, *g_ScriptContext);
		man2.LoadComponentTypes();

		TS_ASSERT(man2.LookupRegistryEntity(ent1) == entt::null);

		TS_ASSERT(man2.DeserializeState(stateStream));

		// DeserializeState resets, re-inits the system entity, and reconstructs every
		// component; the registry mapping must come back in lockstep for every entity ID
		// that was in the save.
		entt::entity sysHandle = man2.LookupRegistryEntity(SYSTEM_ENTITY);
		TS_ASSERT(man2.GetRegistry().valid(sysHandle));
		TS_ASSERT(man2.GetRegistry().get<SimEntityId>(sysHandle).id == SYSTEM_ENTITY);

		entt::entity handle1 = man2.LookupRegistryEntity(ent1);
		TS_ASSERT(man2.GetRegistry().valid(handle1));
		TS_ASSERT(man2.GetRegistry().get<SimEntityId>(handle1).id == ent1);

		entt::entity handle2 = man2.LookupRegistryEntity(ent2);
		TS_ASSERT(man2.GetRegistry().valid(handle2));
		TS_ASSERT(man2.GetRegistry().get<SimEntityId>(handle2).id == ent2);
	}

	void test_EnTT_helperRoundtrip()
	{
		// Test the EnTTComponent mixin via Roundtrip, the literal acceptance criterion.
		ComponentTestHelper helper(*g_ScriptContext);
		ICmpTest1* cmp = helper.Add<ICmpTest1>(CID_Test1EnTT, "<x>1234</x>", 10);
		TS_ASSERT_EQUALS(cmp->GetX(), 1234);
		helper.Roundtrip();
	}

	void test_EnTT_helperLifecycle()
	{
		// Test the component lifecycle: add, access storage, handle messages, destroy.
		CSimContext context;
		CComponentManager man(context, *g_ScriptContext);
		man.LoadComponentTypes();

		entity_id_t ent = 42;
		CEntityHandle handle = man.AllocateEntityHandle(ent);
		CParamNode noParam;
		TS_ASSERT(man.AddComponent(handle, CID_Test1EnTT, noParam));

		ICmpTest1* cmp = static_cast<ICmpTest1*>(man.QueryInterface(ent, IID_Test1));
		TS_ASSERT(cmp != NULL);

		// Verify that all three storage structs are present on the registry entity.
		entt::entity regEnt = man.LookupRegistryEntity(ent);
		TS_ASSERT(man.GetRegistry().valid(regEnt));
		bool hasAllStorage = man.GetRegistry().all_of<Test1EnTTTemplate, Test1EnTTState, Test1EnTTDerived>(regEnt);
		TS_ASSERT(hasAllStorage);

		// Initial value should be 11000 (default).
		TS_ASSERT_EQUALS(cmp->GetX(), 11000);

		// Access storage directly and verify it's live.
		TS_ASSERT_EQUALS(man.GetRegistry().get<Test1EnTTState>(regEnt).x, 11000);
		TS_ASSERT_EQUALS(man.GetRegistry().get<Test1EnTTDerived>(regEnt).messagesHandled, 0);

		// Broadcast messages and verify both State and Derived update.
		CMessageTurnStart turnMsg;
		man.BroadcastMessage(turnMsg);
		TS_ASSERT_EQUALS(cmp->GetX(), 11001);  // +1 from MT_TurnStart
		TS_ASSERT_EQUALS(man.GetRegistry().get<Test1EnTTState>(regEnt).x, 11001);
		TS_ASSERT_EQUALS(man.GetRegistry().get<Test1EnTTDerived>(regEnt).messagesHandled, 1);

		CMessageInterpolate interpMsg(0, 0, 0);
		man.BroadcastMessage(interpMsg);
		TS_ASSERT_EQUALS(cmp->GetX(), 11003);  // +2 from MT_Interpolate
		TS_ASSERT_EQUALS(man.GetRegistry().get<Test1EnTTState>(regEnt).x, 11003);
		TS_ASSERT_EQUALS(man.GetRegistry().get<Test1EnTTDerived>(regEnt).messagesHandled, 2);

		// Destroy and verify cleanup.
		man.DestroyComponentsSoon(ent);
		man.FlushDestroyedComponents();
		TS_ASSERT(man.LookupRegistryEntity(ent) == entt::null);
		TS_ASSERT(!man.GetRegistry().valid(regEnt));
	}

	void test_EnTT_helperMigrationIsByteInvariant()
	{
		// Verify that Test1EnTT and Test1A produce byte-identical serialization.
		// This is the ADR-001 Decision 4 acceptance criterion.
		// The Roundtrip test already verifies hash invariance; here we focus on debug/std streams.

		std::stringstream stream_1a_debug, stream_1a_std;
		std::stringstream stream_1entt_debug, stream_1entt_std;

		{
			ComponentTestHelper helper(*g_ScriptContext);
			ICmpTest1* cmp1a = helper.Add<ICmpTest1>(CID_Test1A, "<x>5678</x>", 10);

			CDebugSerializer dbg1a(helper.GetScriptInterface(), stream_1a_debug);
			cmp1a->Serialize(dbg1a);

			CStdSerializer std1a(helper.GetScriptInterface(), stream_1a_std);
			cmp1a->Serialize(std1a);
		}

		{
			ComponentTestHelper helper(*g_ScriptContext);
			ICmpTest1* cmp1entt = helper.Add<ICmpTest1>(CID_Test1EnTT, "<x>5678</x>", 10);

			CDebugSerializer dbg1entt(helper.GetScriptInterface(), stream_1entt_debug);
			cmp1entt->Serialize(dbg1entt);

			CStdSerializer std1entt(helper.GetScriptInterface(), stream_1entt_std);
			cmp1entt->Serialize(std1entt);
		}

		// Compare byte streams; hash invariance is validated by test_EnTT_helperRoundtrip.
		TS_ASSERT_EQUALS(stream_1a_debug.str(), stream_1entt_debug.str());
		TS_ASSERT_EQUALS(stream_1a_std.str(), stream_1entt_std.str());
	}

	void test_EnTT_helperResetState()
	{
		// Verify that an EnTT-backed component survives ResetState() without crashing,
		// and that the entity is properly cleaned up afterward.
		CSimContext context;
		CComponentManager man(context, *g_ScriptContext);
		man.LoadComponentTypes();

		entity_id_t ent = 77;
		CEntityHandle handle = man.AllocateEntityHandle(ent);
		CParamNode noParam;
		TS_ASSERT(man.AddComponent(handle, CID_Test1EnTT, noParam));

		entt::entity regEnt = man.LookupRegistryEntity(ent);
		TS_ASSERT(man.GetRegistry().valid(regEnt));

		// ResetState() should not crash; Deinit() must cleanly detach storage.
		man.ResetState();

		// After reset, the mapping must be cleared.
		TS_ASSERT(man.LookupRegistryEntity(ent) == entt::null);
	}

	void test_EnTT_helperMockComponentUnaffected()
	{
		// Verify that mock components coexist peacefully with EnTT-backed components,
		// and that mocks do not create registry storage.
		CSimContext context;
		CComponentManager man(context, *g_ScriptContext);
		man.LoadComponentTypes();

		// First entity: real EnTT-backed Test1 + mock Test2 on the same entity.
		entity_id_t ent1 = 55;
		CEntityHandle handle1 = man.AllocateEntityHandle(ent1);

		CParamNode paramNode;
		TS_ASSERT(man.AddComponent(handle1, CID_Test1EnTT, paramNode));

		ICmpTest1* realCmp = static_cast<ICmpTest1*>(man.QueryInterface(ent1, IID_Test1));
		TS_ASSERT(realCmp != NULL);
		TS_ASSERT_EQUALS(realCmp->GetX(), 11000);

		// Add a mock Test2 component on the same entity to verify coexistence.
		MockTest2 mockTest2;
		man.AddMockComponent(handle1, IID_Test2, mockTest2);

		ICmpTest2* test2Mock = static_cast<ICmpTest2*>(man.QueryInterface(ent1, IID_Test2));
		TS_ASSERT(test2Mock != NULL);
		TS_ASSERT_EQUALS(test2Mock->GetX(), 4242);

		// Verify the real component still works.
		TS_ASSERT_EQUALS(realCmp->GetX(), 11000);

		// Verify the real Test1EnTT component's storage is intact alongside the mock.
		entt::entity regEnt1 = man.LookupRegistryEntity(ent1);
		TS_ASSERT(man.GetRegistry().valid(regEnt1));
		bool hasRealStorage = man.GetRegistry().all_of<Test1EnTTTemplate, Test1EnTTState, Test1EnTTDerived>(regEnt1);
		TS_ASSERT(hasRealStorage);

		// Second entity: test that a mock can stand in for an EnTT-backed component type.
		// This proves mocks are registry-independent and can coexist with any component type.
		entity_id_t ent2 = 66;
		CEntityHandle handle2 = man.AllocateEntityHandle(ent2);

		MockTest1 mockTest1;
		man.AddMockComponent(handle2, IID_Test1, mockTest1);

		ICmpTest1* test1Mock = static_cast<ICmpTest1*>(man.QueryInterface(ent2, IID_Test1));
		TS_ASSERT(test1Mock != NULL);
		TS_ASSERT_EQUALS(test1Mock->GetX(), 9999);  // Mock value

		// Verify the mock did NOT create registry-backed storage.
		// The entity exists in the registry (AllocateEntityHandle creates one unconditionally),
		// but it carries NONE of the Test1EnTT storage structs.
		entt::entity regEnt2 = man.LookupRegistryEntity(ent2);
		TS_ASSERT(man.GetRegistry().valid(regEnt2));
		bool hasAnyStorage = man.GetRegistry().any_of<Test1EnTTTemplate, Test1EnTTState, Test1EnTTDerived>(regEnt2);
		TS_ASSERT(!hasAnyStorage);
	}

};
