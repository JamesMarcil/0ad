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

#include <benchmark/benchmark.h>
#include <vector>
#include <map>
#include <unordered_map>
#include <memory>
#include <string>

#include "bench_fixtures.h"
#include "simulation2/system/Entity.h"
#include <entt/entt.hpp>

namespace
{

using namespace BenchmarkFixtures;

typedef int MessageTypeId;
typedef int ComponentTypeId;

// 1. BroadcastMessage Dispatch Benchmark with Realistic 256-Byte Components
// Replicates the exact dispatch topology of CComponentManager::BroadcastMessage:
// std::map<MessageTypeId, std::vector<ComponentTypeId>> m_LocalMessageSubscriptions;
// std::map<ComponentTypeId, std::map<entity_id_t, IComponent*>> m_ComponentsByTypeId;
static void BM_ComponentManager_BroadcastMessage_Dense(benchmark::State& state)
{
	const size_t entityCount = static_cast<size_t>(state.range(0));
	const ComponentTypeId compTypeId = 1;
	const MessageTypeId msgTypeId = 100;

	std::vector<std::unique_ptr<RealisticComponent>> components;
	components.reserve(entityCount);

	std::map<MessageTypeId, std::vector<ComponentTypeId>> subscriptions;
	subscriptions[msgTypeId] = { compTypeId };

	std::map<ComponentTypeId, std::map<entity_id_t, RealisticComponent*>> componentsByTypeId;
	auto& compMap = componentsByTypeId[compTypeId];

	for (size_t i = 0; i < entityCount; ++i)
	{
		auto comp = std::make_unique<RealisticComponent>();
		entity_id_t ent = static_cast<entity_id_t>(i + 100);
		compMap[ent] = comp.get();
		components.push_back(std::move(comp));
	}

	for (auto _ : state)
	{
		auto subIt = subscriptions.find(msgTypeId);
		if (subIt != subscriptions.end())
		{
			for (ComponentTypeId cid : subIt->second)
			{
				auto emapIt = componentsByTypeId.find(cid);
				if (emapIt != componentsByTypeId.end())
				{
					for (auto& [ent, comp] : emapIt->second)
					{
						comp->HandleMessage(msgTypeId, 42);
					}
				}
			}
		}
		benchmark::ClobberMemory();
	}

	state.SetItemsProcessed(int64_t(state.iterations()) * int64_t(entityCount));
}
BENCHMARK(BM_ComponentManager_BroadcastMessage_Dense)->RangeMultiplier(4)->Range(64, 4096);

// 2. Multi-Receiver Turn Message Broadcast (Realistic 6 Component Types per Entity)
static void BM_ComponentManager_MultiReceiverBroadcast(benchmark::State& state)
{
	const size_t entityCount = static_cast<size_t>(state.range(0));
	const MessageTypeId turnMsgType = 101; // e.g. TurnStart / RenderSubmit
	const std::vector<ComponentTypeId> attachedTypes = { 1, 2, 3, 4, 5, 6 };

	std::vector<std::unique_ptr<RealisticComponent>> components;
	components.reserve(entityCount * attachedTypes.size());

	std::map<MessageTypeId, std::vector<ComponentTypeId>> subscriptions;
	subscriptions[turnMsgType] = attachedTypes;

	std::map<ComponentTypeId, std::map<entity_id_t, RealisticComponent*>> componentsByTypeId;

	for (ComponentTypeId cid : attachedTypes)
	{
		auto& compMap = componentsByTypeId[cid];
		for (size_t i = 0; i < entityCount; ++i)
		{
			auto comp = std::make_unique<RealisticComponent>();
			entity_id_t ent = static_cast<entity_id_t>(i + 100);
			compMap[ent] = comp.get();
			components.push_back(std::move(comp));
		}
	}

	for (auto _ : state)
	{
		auto subIt = subscriptions.find(turnMsgType);
		if (subIt != subscriptions.end())
		{
			for (ComponentTypeId cid : subIt->second)
			{
				auto emapIt = componentsByTypeId.find(cid);
				if (emapIt != componentsByTypeId.end())
				{
					for (auto& [ent, comp] : emapIt->second)
					{
						comp->HandleMessage(turnMsgType, 1);
					}
				}
			}
		}
		benchmark::ClobberMemory();
	}

	state.SetItemsProcessed(int64_t(state.iterations()) * int64_t(entityCount * attachedTypes.size()));
}
BENCHMARK(BM_ComponentManager_MultiReceiverBroadcast)->RangeMultiplier(4)->Range(64, 2048);

// 3. Batch Entity Teardown (FlushDestroyedComponents Full Lifecycle) Benchmark
struct MockComponentCache
{
	RealisticComponent* interfaces[32];
};

static void BM_ComponentManager_BatchEntityDestruction(benchmark::State& state)
{
	const size_t batchSize = static_cast<size_t>(state.range(0));
	const size_t totalTypes = 8;

	for (auto _ : state)
	{
		state.PauseTiming();
		std::map<ComponentTypeId, std::map<entity_id_t, RealisticComponent*>> componentsByTypeId;
		std::vector<std::unique_ptr<RealisticComponent>> allocatedComponents;
		allocatedComponents.reserve(batchSize * totalTypes);

		std::unordered_map<entity_id_t, MockComponentCache> entityCaches;
		std::vector<entity_id_t> destructionQueue;
		destructionQueue.reserve(batchSize);

		for (size_t e = 0; e < batchSize; ++e)
		{
			entity_id_t ent = static_cast<entity_id_t>(e + 500);
			destructionQueue.push_back(ent);
			MockComponentCache cache;
			for (int i = 0; i < 32; ++i) cache.interfaces[i] = nullptr;

			for (ComponentTypeId cid = 0; cid < static_cast<ComponentTypeId>(totalTypes); ++cid)
			{
				auto comp = std::make_unique<RealisticComponent>();
				componentsByTypeId[cid][ent] = comp.get();
				cache.interfaces[cid] = comp.get();
				allocatedComponents.push_back(std::move(comp));
			}
			entityCaches[ent] = cache;
		}
		state.ResumeTiming();

		// Full 7-stage teardown loop (FlushDestroyedComponents)
		for (entity_id_t ent : destructionQueue)
		{
			auto& cache = entityCaches[ent];
			for (auto iit = componentsByTypeId.begin(); iit != componentsByTypeId.end(); ++iit)
			{
				auto eit = iit->second.find(ent);
				if (eit != iit->second.end())
				{
					eit->second->Deinit();
					cache.interfaces[iit->first] = nullptr;
					iit->second.erase(eit);
				}
			}
		}
		benchmark::DoNotOptimize(componentsByTypeId.size());
	}

	state.SetItemsProcessed(int64_t(state.iterations()) * int64_t(batchSize));
}
BENCHMARK(BM_ComponentManager_BatchEntityDestruction)->RangeMultiplier(4)->Range(16, 256);

// 4. Entity Handle Component Cache Lookup Benchmark
static void BM_ComponentManager_ComponentCacheLookup(benchmark::State& state)
{
	const size_t count = static_cast<size_t>(state.range(0));
	RealisticComponent mockComp;
	MockComponentCache cache;
	for (int i = 0; i < 32; ++i)
		cache.interfaces[i] = &mockComp;

	std::vector<MockComponentCache> cacheArray(count + 1, cache);

	DeterministicRng rng(0x33445566ULL);
	std::vector<entity_id_t> accessPattern;
	accessPattern.reserve(1000);
	for (size_t i = 0; i < 1000; ++i)
		accessPattern.push_back(static_cast<entity_id_t>(rng.NextRange(1, count)));

	for (auto _ : state)
	{
		uint64_t sum = 0;
		for (entity_id_t id : accessPattern)
		{
			RealisticComponent* ptr = cacheArray[id].interfaces[3];
			benchmark::DoNotOptimize(ptr);
			sum += reinterpret_cast<uintptr_t>(ptr);
		}
		benchmark::DoNotOptimize(sum);
	}

	state.SetItemsProcessed(int64_t(state.iterations()) * 1000);
}
BENCHMARK(BM_ComponentManager_ComponentCacheLookup)->RangeMultiplier(4)->Range(64, 4096);

// ----------------------------------------------------------------------------
// EnTT Modernized ECS Comparative Counterparts
// ----------------------------------------------------------------------------

// 5. EnTT BroadcastMessage Dispatch Benchmark
static void BM_ComponentManager_BroadcastMessage_EnTT(benchmark::State& state)
{
	const size_t entityCount = static_cast<size_t>(state.range(0));
	const MessageTypeId msgTypeId = 100;

	entt::registry registry;
	for (size_t i = 0; i < entityCount; ++i)
	{
		auto e = registry.create();
		registry.emplace<RealisticComponent>(e);
	}

	for (auto _ : state)
	{
		auto view = registry.view<RealisticComponent>();
		for (auto entity : view)
		{
			auto& comp = view.get<RealisticComponent>(entity);
			comp.HandleMessage(msgTypeId, 42);
		}
		benchmark::ClobberMemory();
	}

	state.SetItemsProcessed(int64_t(state.iterations()) * int64_t(entityCount));
}
BENCHMARK(BM_ComponentManager_BroadcastMessage_EnTT)->RangeMultiplier(4)->Range(64, 4096);

// 6. EnTT Multi-Receiver Turn Message Broadcast (6 Component Types per Entity)
struct CompTag1 { RealisticComponent comp; };
struct CompTag2 { RealisticComponent comp; };
struct CompTag3 { RealisticComponent comp; };
struct CompTag4 { RealisticComponent comp; };
struct CompTag5 { RealisticComponent comp; };
struct CompTag6 { RealisticComponent comp; };

static void BM_ComponentManager_MultiReceiverBroadcast_EnTT(benchmark::State& state)
{
	const size_t entityCount = static_cast<size_t>(state.range(0));
	const MessageTypeId turnMsgType = 101;

	entt::registry registry;
	for (size_t i = 0; i < entityCount; ++i)
	{
		auto e = registry.create();
		registry.emplace<CompTag1>(e);
		registry.emplace<CompTag2>(e);
		registry.emplace<CompTag3>(e);
		registry.emplace<CompTag4>(e);
		registry.emplace<CompTag5>(e);
		registry.emplace<CompTag6>(e);
	}

	for (auto _ : state)
	{
		registry.view<CompTag1>().each([&](CompTag1& c) { c.comp.HandleMessage(turnMsgType, 1); });
		registry.view<CompTag2>().each([&](CompTag2& c) { c.comp.HandleMessage(turnMsgType, 1); });
		registry.view<CompTag3>().each([&](CompTag3& c) { c.comp.HandleMessage(turnMsgType, 1); });
		registry.view<CompTag4>().each([&](CompTag4& c) { c.comp.HandleMessage(turnMsgType, 1); });
		registry.view<CompTag5>().each([&](CompTag5& c) { c.comp.HandleMessage(turnMsgType, 1); });
		registry.view<CompTag6>().each([&](CompTag6& c) { c.comp.HandleMessage(turnMsgType, 1); });
		benchmark::ClobberMemory();
	}

	state.SetItemsProcessed(int64_t(state.iterations()) * int64_t(entityCount * 6));
}
BENCHMARK(BM_ComponentManager_MultiReceiverBroadcast_EnTT)->RangeMultiplier(4)->Range(64, 2048);

// 7. EnTT Batch Entity Teardown (Fast O(1) swap-and-pop sparse set erasures)
static void BM_ComponentManager_BatchEntityDestruction_EnTT(benchmark::State& state)
{
	const size_t batchSize = static_cast<size_t>(state.range(0));

	for (auto _ : state)
	{
		state.PauseTiming();
		entt::registry registry;
		std::vector<entt::entity> destructionQueue;
		destructionQueue.reserve(batchSize);

		for (size_t e = 0; e < batchSize; ++e)
		{
			auto ent = registry.create();
			destructionQueue.push_back(ent);
			registry.emplace<CompTag1>(ent);
			registry.emplace<CompTag2>(ent);
			registry.emplace<CompTag3>(ent);
			registry.emplace<CompTag4>(ent);
			registry.emplace<CompTag5>(ent);
			registry.emplace<CompTag6>(ent);
			registry.emplace<RealisticComponent>(ent);
		}
		state.ResumeTiming();

		for (auto ent : destructionQueue)
		{
			registry.destroy(ent);
		}
		benchmark::DoNotOptimize(registry.storage<RealisticComponent>().empty());
	}

	state.SetItemsProcessed(int64_t(state.iterations()) * int64_t(batchSize));
}
BENCHMARK(BM_ComponentManager_BatchEntityDestruction_EnTT)->RangeMultiplier(4)->Range(16, 256);

// 8. EnTT Component Lookup Benchmark (Direct Sparse Set $O(1)$)
static void BM_ComponentManager_ComponentCacheLookup_EnTT(benchmark::State& state)
{
	const size_t count = static_cast<size_t>(state.range(0));
	entt::registry registry;
	std::vector<entt::entity> entities;
	entities.reserve(count + 1);

	for (size_t i = 0; i <= count; ++i)
	{
		auto e = registry.create();
		registry.emplace<RealisticComponent>(e);
		entities.push_back(e);
	}

	DeterministicRng rng(0x33445566ULL);
	std::vector<entt::entity> accessPattern;
	accessPattern.reserve(1000);
	for (size_t i = 0; i < 1000; ++i)
		accessPattern.push_back(entities[rng.NextRange(1, count)]);

	for (auto _ : state)
	{
		uint64_t sum = 0;
		for (auto ent : accessPattern)
		{
			RealisticComponent& comp = registry.get<RealisticComponent>(ent);
			benchmark::DoNotOptimize(&comp);
			sum += reinterpret_cast<uintptr_t>(&comp);
		}
		benchmark::DoNotOptimize(sum);
	}

	state.SetItemsProcessed(int64_t(state.iterations()) * 1000);
}
BENCHMARK(BM_ComponentManager_ComponentCacheLookup_EnTT)->RangeMultiplier(4)->Range(64, 4096);

} // anonymous namespace
