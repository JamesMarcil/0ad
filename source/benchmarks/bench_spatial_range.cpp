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
#include <algorithm>
#include <iterator>
#include <map>
#include <unordered_map>
#include <mutex>
#include <atomic>
#include <optional>

#include "bench_fixtures.h"
#include "maths/FixedVector2D.h"
#include "maths/FixedVector3D.h"
#include "simulation2/helpers/Spatial.h"

// The benchmark project is built without the precompiled header, so the warning
// suppressions in lib/pch/pch_warnings.h do not apply here. EntityMap::insert uses
// debug_warn, which expands to ENSURE(0 && ...) and therefore trips C4127.
#if MSC_VERSION
# pragma warning(disable:4127)	// conditional expression is constant; rationale: see STMT in lib.h.
#endif
#include "simulation2/system/EntityMap.h"
#include <entt/entt.hpp>

namespace
{

using namespace BenchmarkFixtures;

// 1. Distance Ordering & Sorting Benchmark
// Exercises the exact sorting logic used in CCmpRangeManager::ExecuteActiveQueries:
// std::stable_sort(added.begin(), added.end(), EntityDistanceOrdering(...))
//
// The baseline deliberately resolves positions through EntityMap<T>, mirroring the
// engine's EntityDistanceOrdering, which holds an EntityMap<EntityData>. EntityMap
// is not a tree: its find() is a direct index into a single contiguous buffer
// (m_Buffer + key, see simulation2/system/EntityMap.h), so the legacy path is
// already a flat O(1) lookup over contiguous storage. Modelling the baseline as
// std::map<entity_id_t, T> would measure red-black tree traversal the engine never
// performs, and would overstate any gain from moving to EnTT sparse sets.
struct EntityPosData
{
	fixed x, z;
	u8 flags;
};

class BenchEntityDistanceOrdering
{
public:
	BenchEntityDistanceOrdering(const EntityMap<EntityPosData>& entities, const CFixedVector2D& source) :
		m_EntityData(entities), m_Source(source)
	{
	}

	bool operator()(entity_id_t a, entity_id_t b) const
	{
		const EntityPosData& da = m_EntityData.find(a)->second;
		const EntityPosData& db = m_EntityData.find(b)->second;
		CFixedVector2D vecA = CFixedVector2D(da.x, da.z) - m_Source;
		CFixedVector2D vecB = CFixedVector2D(db.x, db.z) - m_Source;
		return (vecA.CompareLength(vecB) < 0);
	}

private:
	const EntityMap<EntityPosData>& m_EntityData;
	CFixedVector2D m_Source;
};

static void BM_RangeManager_DistanceOrdering(benchmark::State& state)
{
	const size_t count = static_cast<size_t>(state.range(0));
	auto syntheticEntities = SyntheticGridGenerator::GenerateClusteredSwarm(count, fixed::FromInt(512));

	EntityMap<EntityPosData> entityMap;
	std::vector<entity_id_t> entityIds;
	entityIds.reserve(count);

	for (const auto& ent : syntheticEntities)
	{
		entityMap.insert(ent.id, EntityPosData{ ent.pos.X, ent.pos.Y, ent.flags });
		entityIds.push_back(ent.id);
	}

	CFixedVector2D sourcePos(fixed::FromInt(256), fixed::FromInt(256));

	for (auto _ : state)
	{
		state.PauseTiming();
		std::vector<entity_id_t> workList = entityIds;
		state.ResumeTiming();

		BenchEntityDistanceOrdering ordering(entityMap, sourcePos);
		std::stable_sort(workList.begin(), workList.end(), ordering);

		benchmark::DoNotOptimize(workList.data());
	}

	state.SetItemsProcessed(int64_t(state.iterations()) * int64_t(count));
}
BENCHMARK(BM_RangeManager_DistanceOrdering)->RangeMultiplier(4)->Range(64, 4096);

// 2. SpatialSubdivision GetNear (Engine Structure) Benchmark
static void BM_SpatialSubdivision_GetNear(benchmark::State& state)
{
	const size_t entityCount = static_cast<size_t>(state.range(0));
	const fixed worldSize = fixed::FromInt(512);
	const fixed divisionSize = fixed::FromInt(32);

	SpatialSubdivision spatial;
	spatial.Reset(worldSize, worldSize, divisionSize);

	auto entities = SyntheticGridGenerator::GenerateUniformGrid(entityCount, worldSize);
	for (const auto& ent : entities)
	{
		CFixedVector2D minPos = ent.pos - CFixedVector2D(ent.radius, ent.radius);
		CFixedVector2D maxPos = ent.pos + CFixedVector2D(ent.radius, ent.radius);
		spatial.Add(ent.id, minPos, maxPos);
	}

	const CFixedVector2D queryCenter(fixed::FromInt(256), fixed::FromInt(256));
	const entity_pos_t queryRadius = fixed::FromInt(48);

	std::vector<uint32_t> results;
	results.reserve(entityCount);

	for (auto _ : state)
	{
		results.clear();
		spatial.GetNear(results, queryCenter, queryRadius);
		benchmark::DoNotOptimize(results.data());
	}

	state.SetItemsProcessed(int64_t(state.iterations()) * int64_t(entityCount));
}
BENCHMARK(BM_SpatialSubdivision_GetNear)->RangeMultiplier(4)->Range(256, 4096);

// 3. SpatialSubdivision Dynamic Movement Benchmark
static void BM_SpatialSubdivision_Move(benchmark::State& state)
{
	const size_t entityCount = static_cast<size_t>(state.range(0));
	const fixed worldSize = fixed::FromInt(512);
	const fixed divisionSize = fixed::FromInt(32);

	SpatialSubdivision spatial;
	spatial.Reset(worldSize, worldSize, divisionSize);

	auto entities = SyntheticGridGenerator::GenerateUniformGrid(entityCount, worldSize);
	for (const auto& ent : entities)
	{
		CFixedVector2D minPos = ent.pos - CFixedVector2D(ent.radius, ent.radius);
		CFixedVector2D maxPos = ent.pos + CFixedVector2D(ent.radius, ent.radius);
		spatial.Add(ent.id, minPos, maxPos);
	}

	CFixedVector2D step(fixed::FromFloat(2.5f), fixed::FromFloat(1.5f));

	for (auto _ : state)
	{
		for (auto& ent : entities)
		{
			CFixedVector2D oldMin = ent.pos - CFixedVector2D(ent.radius, ent.radius);
			CFixedVector2D oldMax = ent.pos + CFixedVector2D(ent.radius, ent.radius);
			ent.pos += step;
			CFixedVector2D newMin = ent.pos - CFixedVector2D(ent.radius, ent.radius);
			CFixedVector2D newMax = ent.pos + CFixedVector2D(ent.radius, ent.radius);

			spatial.Move(ent.id, oldMin, oldMax, newMin, newMax);
		}
		benchmark::ClobberMemory();
	}

	state.SetItemsProcessed(int64_t(state.iterations()) * int64_t(entityCount));
}
BENCHMARK(BM_SpatialSubdivision_Move)->RangeMultiplier(4)->Range(256, 4096);

// 4. FastSpatialSubdivision Benchmark
static void BM_FastSpatialSubdivision_GetNear(benchmark::State& state)
{
	const size_t entityCount = static_cast<size_t>(state.range(0));
	const fixed worldSize = fixed::FromInt(512);

	FastSpatialSubdivision spatial;
	spatial.Reset(worldSize, worldSize);

	auto entities = SyntheticGridGenerator::GenerateUniformGrid(entityCount, worldSize);
	for (const auto& ent : entities)
	{
		spatial.Add(ent.id, ent.pos, ent.radius.ToInt_RoundToInfinity());
	}

	const CFixedVector2D queryCenter(fixed::FromInt(256), fixed::FromInt(256));
	const entity_pos_t queryRadius = fixed::FromInt(48);

	std::vector<entity_id_t> results;
	results.reserve(entityCount);

	for (auto _ : state)
	{
		results.clear();
		spatial.GetNear(results, queryCenter, queryRadius);
		benchmark::DoNotOptimize(results.data());
	}

	state.SetItemsProcessed(int64_t(state.iterations()) * int64_t(entityCount));
}
BENCHMARK(BM_FastSpatialSubdivision_GetNear)->RangeMultiplier(4)->Range(256, 4096);

// 5. Concurrent Range Query Execution with Realistic Workloads (Contention Simulation)
struct MockRangeQuery
{
	uint32_t tag;
	CFixedVector2D center;
	fixed radius;
	bool enabled;
	std::vector<entity_id_t> lastMatch;
};

static void BM_RangeManager_ConcurrentQueryExecution(benchmark::State& state)
{
	const size_t totalQueries = 500;
	static std::map<uint32_t, MockRangeQuery> s_Queries;
	static std::mutex s_Mutex;
	static std::map<uint32_t, MockRangeQuery>::iterator s_Iterator;

	if (state.thread_index() == 0)
	{
		s_Queries.clear();
		for (size_t i = 0; i < totalQueries; ++i)
		{
			MockRangeQuery q;
			q.tag = static_cast<uint32_t>(i);
			q.center = CFixedVector2D(fixed::FromInt(static_cast<int>(i % 200)), fixed::FromInt(static_cast<int>((i * 5) % 200)));
			q.radius = fixed::FromInt(30);
			q.enabled = true;
			for (size_t m = 0; m < 20; ++m)
				q.lastMatch.push_back(static_cast<entity_id_t>(m + 100));
			s_Queries[q.tag] = std::move(q);
		}
		s_Iterator = s_Queries.begin();
	}

	for (auto _ : state)
	{
		uint64_t processed = 0;
		std::vector<entity_id_t> results;
		std::vector<entity_id_t> added;
		std::vector<entity_id_t> removed;

		while (true)
		{
			std::map<uint32_t, MockRangeQuery>::iterator itCopy;
			{
				std::lock_guard<std::mutex> lock(s_Mutex);
				if (s_Iterator == s_Queries.end())
					break;
				itCopy = s_Iterator++;
			}

			MockRangeQuery& q = itCopy->second;
			if (!q.enabled)
				continue;

			// Perform realistic query match and delta calculations (~250 ns)
			results.clear();
			for (size_t r = 0; r < 25; ++r)
			{
				results.push_back(static_cast<entity_id_t>(r + 105));
			}

			added.clear();
			removed.clear();
			std::set_difference(results.begin(), results.end(), q.lastMatch.begin(), q.lastMatch.end(), std::back_inserter(added));
			std::set_difference(q.lastMatch.begin(), q.lastMatch.end(), results.begin(), results.end(), std::back_inserter(removed));

			q.lastMatch.swap(results);
			processed++;
			benchmark::DoNotOptimize(processed);
		}
	}

	if (state.thread_index() == 0)
	{
		s_Iterator = s_Queries.begin();
	}
}
BENCHMARK(BM_RangeManager_ConcurrentQueryExecution)->ThreadRange(1, 8);

// 6. Incremental LOS Bitmask Calculation Benchmark
static void BM_RangeManager_IncrementalLOS(benchmark::State& state)
{
	const size_t count = static_cast<size_t>(state.range(0));
	const size_t mapSize = 256;
	std::vector<u32> visibilityGrid(mapSize * mapSize, 0);

	DeterministicRng rng(0xabcdef01ULL);
	std::vector<std::pair<int, int>> tileCoords;
	tileCoords.reserve(count);
	for (size_t i = 0; i < count; ++i)
	{
		tileCoords.emplace_back(rng.NextRange(5, mapSize - 6), rng.NextRange(5, mapSize - 6));
	}

	const u32 playerMask = 1 << 1;
	const int visionRadius = 4;

	for (auto _ : state)
	{
		for (const auto& [cx, cz] : tileCoords)
		{
			for (int dz = -visionRadius; dz <= visionRadius; ++dz)
			{
				for (int dx = -visionRadius; dx <= visionRadius; ++dx)
				{
					if (dx * dx + dz * dz <= visionRadius * visionRadius)
					{
						size_t idx = (cz + dz) * mapSize + (cx + dx);
						visibilityGrid[idx] |= playerMask;
					}
				}
			}
		}
		benchmark::DoNotOptimize(visibilityGrid.data());
	}

	state.SetItemsProcessed(int64_t(state.iterations()) * int64_t(count));
}
BENCHMARK(BM_RangeManager_IncrementalLOS)->RangeMultiplier(4)->Range(64, 1024);

// ----------------------------------------------------------------------------
// EnTT Modernized ECS Comparative Counterparts
// ----------------------------------------------------------------------------

class BenchEntityDistanceOrderingEnTT
{
public:
	BenchEntityDistanceOrderingEnTT(const entt::storage<EntityPosData>& storage, const CFixedVector2D& source) :
		m_Storage(storage), m_Source(source)
	{
	}

	bool operator()(entt::entity a, entt::entity b) const
	{
		const EntityPosData& da = m_Storage.get(a);
		const EntityPosData& db = m_Storage.get(b);
		CFixedVector2D vecA = CFixedVector2D(da.x, da.z) - m_Source;
		CFixedVector2D vecB = CFixedVector2D(db.x, db.z) - m_Source;
		return (vecA.CompareLength(vecB) < 0);
	}

private:
	const entt::storage<EntityPosData>& m_Storage;
	CFixedVector2D m_Source;
};

static void BM_RangeManager_DistanceOrdering_EnTT(benchmark::State& state)
{
	const size_t count = static_cast<size_t>(state.range(0));
	auto syntheticEntities = SyntheticGridGenerator::GenerateClusteredSwarm(count, fixed::FromInt(512));

	entt::registry registry;
	std::vector<entt::entity> entityList;
	entityList.reserve(count);

	for (const auto& ent : syntheticEntities)
	{
		auto e = registry.create();
		registry.emplace<EntityPosData>(e, ent.pos.X, ent.pos.Y, ent.flags);
		entityList.push_back(e);
	}

	CFixedVector2D sourcePos(fixed::FromInt(256), fixed::FromInt(256));
	const auto& storage = registry.storage<EntityPosData>();

	for (auto _ : state)
	{
		state.PauseTiming();
		std::vector<entt::entity> workList = entityList;
		state.ResumeTiming();

		BenchEntityDistanceOrderingEnTT ordering(storage, sourcePos);
		std::stable_sort(workList.begin(), workList.end(), ordering);

		benchmark::DoNotOptimize(workList.data());
	}

	state.SetItemsProcessed(int64_t(state.iterations()) * int64_t(count));
}
BENCHMARK(BM_RangeManager_DistanceOrdering_EnTT)->RangeMultiplier(4)->Range(64, 4096);

} // anonymous namespace
