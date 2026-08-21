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

namespace
{

using namespace BenchmarkFixtures;

typedef int MessageTypeId;
typedef int ComponentTypeId;

class MockComponent
{
public:
	virtual ~MockComponent() = default;
	virtual void HandleMessage(int messageType, int payload)
	{
		m_Counter += (messageType ^ payload);
	}

	uint32_t GetCounter() const { return m_Counter; }

private:
	uint32_t m_Counter = 0;
};

// 1. BroadcastMessage Dispatch Benchmark (std::map Pointer-Chasing Architecture)
// Replicates the exact dispatch topology of CComponentManager::BroadcastMessage:
// std::map<MessageTypeId, std::vector<ComponentTypeId>> m_LocalMessageSubscriptions;
// std::map<ComponentTypeId, std::map<entity_id_t, IComponent*>> m_ComponentsByTypeId;
static void BM_ComponentManager_BroadcastMessage_Dense(benchmark::State& state)
{
	const size_t entityCount = static_cast<size_t>(state.range(0));
	const ComponentTypeId compTypeId = 1;
	const MessageTypeId msgTypeId = 100;

	std::vector<std::unique_ptr<MockComponent>> components;
	components.reserve(entityCount);

	std::map<MessageTypeId, std::vector<ComponentTypeId>> subscriptions;
	subscriptions[msgTypeId] = { compTypeId };

	std::map<ComponentTypeId, std::map<entity_id_t, MockComponent*>> componentsByTypeId;
	auto& compMap = componentsByTypeId[compTypeId];

	for (size_t i = 0; i < entityCount; ++i)
	{
		auto comp = std::make_unique<MockComponent>();
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

// 2. BroadcastMessage Sparse Subscription Benchmark
static void BM_ComponentManager_BroadcastMessage_Sparse(benchmark::State& state)
{
	const size_t entityCount = static_cast<size_t>(state.range(0));
	const ComponentTypeId sparseCompTypeId = 2;
	const MessageTypeId msgTypeId = 200;

	std::vector<std::unique_ptr<MockComponent>> components;
	components.reserve(entityCount);

	std::map<MessageTypeId, std::vector<ComponentTypeId>> subscriptions;
	subscriptions[msgTypeId] = { sparseCompTypeId };

	std::map<ComponentTypeId, std::map<entity_id_t, MockComponent*>> componentsByTypeId;
	auto& compMap = componentsByTypeId[sparseCompTypeId];

	// Only 5% of entities have this component type attached
	size_t sparseCount = std::max<size_t>(1, entityCount / 20);
	for (size_t i = 0; i < sparseCount; ++i)
	{
		auto comp = std::make_unique<MockComponent>();
		entity_id_t ent = static_cast<entity_id_t>(i * 20 + 100);
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
						comp->HandleMessage(msgTypeId, 99);
					}
				}
			}
		}
		benchmark::ClobberMemory();
	}

	state.SetItemsProcessed(int64_t(state.iterations()) * int64_t(sparseCount));
}
BENCHMARK(BM_ComponentManager_BroadcastMessage_Sparse)->RangeMultiplier(4)->Range(128, 4096);

// 3. Batch Entity Teardown (FlushDestroyedComponents) Benchmark
// Replicates the nested map lookups and teardown deallocation occurring in FlushDestroyedComponents
static void BM_ComponentManager_BatchEntityDestruction(benchmark::State& state)
{
	const size_t batchSize = static_cast<size_t>(state.range(0));
	const size_t totalTypes = 12; // Typical number of components attached to an entity

	for (auto _ : state)
	{
		state.PauseTiming();
		// Setup entities and component maps
		std::map<ComponentTypeId, std::map<entity_id_t, MockComponent*>> componentsByTypeId;
		std::vector<std::unique_ptr<MockComponent>> allocatedComponents;
		allocatedComponents.reserve(batchSize * totalTypes);

		std::vector<entity_id_t> destructionQueue;
		destructionQueue.reserve(batchSize);

		for (size_t e = 0; e < batchSize; ++e)
		{
			entity_id_t ent = static_cast<entity_id_t>(e + 500);
			destructionQueue.push_back(ent);
			for (ComponentTypeId cid = 0; cid < static_cast<ComponentTypeId>(totalTypes); ++cid)
			{
				auto comp = std::make_unique<MockComponent>();
				componentsByTypeId[cid][ent] = comp.get();
				allocatedComponents.push_back(std::move(comp));
			}
		}
		state.ResumeTiming();

		// Execute teardown loop
		for (entity_id_t ent : destructionQueue)
		{
			for (auto iit = componentsByTypeId.begin(); iit != componentsByTypeId.end(); ++iit)
			{
				auto eit = iit->second.find(ent);
				if (eit != iit->second.end())
				{
					iit->second.erase(eit);
				}
			}
		}
		benchmark::DoNotOptimize(componentsByTypeId.size());
	}

	state.SetItemsProcessed(int64_t(state.iterations()) * int64_t(batchSize));
}
BENCHMARK(BM_ComponentManager_BatchEntityDestruction)->RangeMultiplier(4)->Range(16, 256);

// 4. Entity Handle Component Cache vs Map Lookup Benchmark
struct MockComponentCache
{
	MockComponent* interfaces[32];
};

static void BM_ComponentManager_ComponentCacheLookup(benchmark::State& state)
{
	const size_t count = static_cast<size_t>(state.range(0));
	MockComponent mockComp;
	MockComponentCache cache;
	for (int i = 0; i < 32; ++i)
		cache.interfaces[i] = &mockComp;

	std::unordered_map<entity_id_t, MockComponentCache> cacheMap;
	for (size_t i = 0; i < count; ++i)
		cacheMap[static_cast<entity_id_t>(i + 1)] = cache;

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
			MockComponent* ptr = cacheMap[id].interfaces[3];
			benchmark::DoNotOptimize(ptr);
			sum += reinterpret_cast<uintptr_t>(ptr);
		}
		benchmark::DoNotOptimize(sum);
	}

	state.SetItemsProcessed(int64_t(state.iterations()) * 1000);
}
BENCHMARK(BM_ComponentManager_ComponentCacheLookup)->RangeMultiplier(4)->Range(64, 4096);

} // anonymous namespace
