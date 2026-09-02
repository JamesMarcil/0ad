/* Copyright (C) 2025 Wildfire Games.
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

#include "precompiled.h"

#include "ICmpTest.h"

#include "simulation2/MessageTypes.h"
#include "simulation2/scripting/ScriptComponent.h"
#include "simulation2/system/Component.h"
#include "simulation2/system/EnTTComponent.h"
#include "simulation2/system/EnTTOrderedIteration.h"
#include "simulation2/system/Message.h"

#include <cstdint>
#include <string>

class CCmpTest1A : public ICmpTest1
{
public:
	static void ClassInit(CComponentManager& componentManager)
	{
		componentManager.SubscribeToMessageType(MT_TurnStart);
		componentManager.SubscribeToMessageType(MT_Interpolate);
		componentManager.SubscribeToMessageType(MT_Destroy);
	}

	DEFAULT_COMPONENT_ALLOCATOR(Test1A)

	int32_t m_x;

	static std::string GetSchema()
	{
		return "<a:component type='test'/><ref name='anything'/>";
	}

	void Init(const CParamNode& paramNode) override
	{
		if (paramNode.GetChild("x").IsOk())
			m_x = paramNode.GetChild("x").ToInt();
		else
			m_x = 11000;
	}

	void Deinit() override
	{
	}

	void Serialize(ISerializer& serialize) override
	{
		serialize.NumberI32_Unbounded("x", m_x);
	}

	void Deserialize(const CParamNode&, IDeserializer& deserialize) override
	{
		deserialize.NumberI32_Unbounded("x", m_x);
	}

	int GetX() override
	{
		return m_x;
	}

	void HandleMessage(const CMessage& msg, bool /*global*/) override
	{
		switch (msg.GetType())
		{
		case MT_Destroy:
			GetSimContext().GetComponentManager().DynamicSubscriptionNonsync(MT_RenderSubmit, this, false);
			break;
		case MT_TurnStart:
			m_x += 1;
			break;
		case MT_Interpolate:
			m_x += 2;
			break;
		default:
			m_x = 0;
			break;
		}
	}
};

REGISTER_COMPONENT_TYPE(Test1A)

class CCmpTest1B : public ICmpTest1
{
public:
	static void ClassInit(CComponentManager& componentManager)
	{
		componentManager.SubscribeToMessageType(MT_Update);
		componentManager.SubscribeGloballyToMessageType(MT_Interpolate);
	}

	DEFAULT_COMPONENT_ALLOCATOR(Test1B)

	int32_t m_x;

	static std::string GetSchema()
	{
		return "<a:component type='test'/><empty/>";
	}

	void Init(const CParamNode&) override
	{
		m_x = 12000;
	}

	void Deinit() override
	{
	}

	void Serialize(ISerializer& serialize) override
	{
		serialize.NumberI32_Unbounded("x", m_x);
	}

	void Deserialize(const CParamNode&, IDeserializer& deserialize) override
	{
		deserialize.NumberI32_Unbounded("x", m_x);
	}

	int GetX() override
	{
		return m_x;
	}

	void HandleMessage(const CMessage& msg, bool /*global*/) override
	{
		switch (msg.GetType())
		{
		case MT_Update:
			m_x += 10;
			break;
		case MT_Interpolate:
			m_x += 20;
			break;
		default:
			m_x = 0;
			break;
		}
	}
};

REGISTER_COMPONENT_TYPE(Test1B)

class CCmpTest2A : public ICmpTest2
{
public:
	static void ClassInit(CComponentManager& componentManager)
	{
		componentManager.SubscribeToMessageType(MT_TurnStart);
		componentManager.SubscribeToMessageType(MT_Update);
	}

	DEFAULT_COMPONENT_ALLOCATOR(Test2A)

	int32_t m_x;

	static std::string GetSchema()
	{
		return "<a:component type='test'/><empty/>";
	}

	void Init(const CParamNode&) override
	{
		m_x = 21000;
	}

	void Deinit() override
	{
	}

	void Serialize(ISerializer& serialize) override
	{
		serialize.NumberI32_Unbounded("x", m_x);
	}

	void Deserialize(const CParamNode&, IDeserializer& deserialize) override
	{
		deserialize.NumberI32_Unbounded("x", m_x);
	}

	int GetX() override
	{
		return m_x;
	}

	void HandleMessage(const CMessage& msg, bool /*global*/) override
	{
		switch (msg.GetType())
		{
		case MT_TurnStart:
			m_x += 50;
			break;
		case MT_Update:
			m_x += static_cast<const CMessageUpdate&> (msg).turnLength.ToInt_RoundToZero();
			break;
		default:
			m_x = 0;
			break;
		}
	}
};

REGISTER_COMPONENT_TYPE(Test2A)

////////////////////////////////////////////////////////////////

class CCmpTest1Scripted : public ICmpTest1
{
public:
	DEFAULT_SCRIPT_WRAPPER(Test1Scripted)

	int GetX() override
	{
		return m_Script.Call<int> ("GetX");
	}
};

REGISTER_COMPONENT_SCRIPT_WRAPPER(Test1Scripted)

////////////////////////////////////////////////////////////////

class CCmpTest2Scripted : public ICmpTest2
{
public:
	DEFAULT_SCRIPT_WRAPPER(Test2Scripted)

	int GetX() override
	{
		return m_Script.Call<int> ("GetX");
	}
};

REGISTER_COMPONENT_SCRIPT_WRAPPER(Test2Scripted)

////////////////////////////////////////////////////////////////

class CCmpTest1EnTT : public ICmpTest1, public EnTTComponent<CCmpTest1EnTT, Test1EnTTTemplate, Test1EnTTState, Test1EnTTDerived>
{
public:
	static void ClassInit(CComponentManager& componentManager)
	{
		componentManager.SubscribeToMessageType(MT_TurnStart);
		componentManager.SubscribeToMessageType(MT_Interpolate);
	}

	DEFAULT_COMPONENT_ALLOCATOR(Test1EnTT)

	static std::string GetSchema()
	{
		return "<a:component type='test'/><ref name='anything'/>";
	}

	void Init(const CParamNode& paramNode) override
	{
		AttachStorage();

		int32_t initialX = 11000;
		if (paramNode.GetChild("x").IsOk())
			initialX = paramNode.GetChild("x").ToInt();

		// Demonstrate the mandated idiom (ADR-001 Appendix A, D4): bind references once per
		// storage struct and read/write multiple fields through them. This is safe because
		// nothing in this scope emplaces or removes storage.
		Test1EnTTTemplate& templ = Get<Test1EnTTTemplate>();
		Test1EnTTState& state = Get<Test1EnTTState>();
		Test1EnTTDerived& derived = Get<Test1EnTTDerived>();

		templ.initialX = initialX;
		state.x = initialX;
		derived.messagesHandled = 0;
	}

	void Deinit() override
	{
		DetachStorage();
	}

	void Serialize(ISerializer& serialize) override
	{
		serialize.NumberI32_Unbounded("x", Get<Test1EnTTState>().x);
	}

	void Deserialize(const CParamNode& paramNode, IDeserializer& deserialize) override
	{
		Init(paramNode);
		// Use a local variable to receive the deserialized value, then assign it to pool storage.
		// This models the ADR-001 Decision 3 corollary: avoid holding references into pool storage
		// across calls that might reallocate it.
		int32_t x;
		deserialize.NumberI32_Unbounded("x", x);
		Get<Test1EnTTState>().x = x;
	}

	int GetX() override
	{
		return Get<Test1EnTTState>().x;
	}

	void HandleMessage(const CMessage& msg, bool /*global*/) override
	{
		// Demonstrate the mandated idiom (ADR-001 Appendix A, D4): bind references once per
		// storage struct at the top of the function. This is safe because nothing in this scope
		// emplaces or removes storage.
		Test1EnTTState& state = Get<Test1EnTTState>();
		Test1EnTTDerived& derived = Get<Test1EnTTDerived>();

		switch (msg.GetType())
		{
		case MT_TurnStart:
			state.x += 1;
			derived.messagesHandled += 1;
			break;
		case MT_Interpolate:
			state.x += 2;
			derived.messagesHandled += 1;
			break;
		default:
			state.x = 0;
			break;
		}
	}
};

REGISTER_COMPONENT_TYPE(Test1EnTT)

// Sanity check: compile-time instantiation test for ForEachOrderedByEntityId (debug only)
#ifndef NDEBUG
namespace {
// This function forces the template to be instantiated and compiled.
// Any compilation errors in EnTTOrderedIteration.h will be caught here.
extern void CCmpTest_ForEachOrderedByEntityId_InstantiationCheck(entt::registry&);
void CCmpTest_ForEachOrderedByEntityId_InstantiationCheck(entt::registry& registry)
{
	EnTTDeterminism::ForEachOrderedByEntityId<Test1EnTTState>(registry,
		[](entity_id_t, entt::entity, Test1EnTTState&) { /* no-op */ });

	const entt::registry& creg = registry;
	EnTTDeterminism::ForEachOrderedByEntityId<const Test1EnTTTemplate>(creg,
		[](entity_id_t, entt::entity) { /* no-op */ });
}
// Force instantiation by taking the address
[[maybe_unused]] static auto g_forEachCheck = &CCmpTest_ForEachOrderedByEntityId_InstantiationCheck;
}  // anonymous namespace
#endif  // NDEBUG
