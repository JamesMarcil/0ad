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

namespace
{

using namespace BenchmarkFixtures;

// 1. Distance Ordering & Sorting Benchmark
// Exercises the exact sorting logic used in CCmpRangeManager::ExecuteActiveQueries:
// std::stable_sort(added.begin(), added.end(), EntityDistanceOrdering(...))
struct EntityPosData
{
	fixed x, z;
	u8 flags;
};

class BenchEntityDistanceOrdering
{
public:
	BenchEntityDistanceOrdering(const std::unordered_map<entity_id_t, EntityPosData>& entities, const CFixedVector2D& source) :
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
	const std::unordered_map<entity_id_t, EntityPosData>& m_EntityData;
	CFixedVector2D m_Source;
};

static void BM_RangeManager_DistanceOrdering(benchmark::State& state)
{
	const size_t count = static_cast<size_t>(state.range(0));
	auto syntheticEntities = SyntheticGridGenerator::GenerateClusteredSwarm(count, fixed::FromInt(512));

	std::unordered_map<entity_id_t, EntityPosData> entityMap;
	entityMap.reserve(count);
	std::vector<entity_id_t> entityIds;
	entityIds.reserve(count);

	for (const auto& ent : syntheticEntities)
	{
		entityMap[ent.id] = { ent.pos.X, ent.pos.Y, ent.flags };
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

// 2. Set Difference (Added/Removed Computation) Benchmark
// Exercises std::set_difference used for query delta calculation in ExecuteActiveQueries
static void BM_RangeManager_SetDifference(benchmark::State& state)
{
	const size_t matchSize = static_cast<size_t>(state.range(0));
	DeterministicRng rng(0x45678901ULL);

	std::vector<entity_id_t> previousMatch;
	previousMatch.reserve(matchSize);
	for (size_t i = 0; i < matchSize; ++i)
		previousMatch.push_back(static_cast<entity_id_t>(i * 2 + 100));

	std::vector<entity_id_t> currentMatch;
	currentMatch.reserve(matchSize);
	for (size_t i = 0; i < matchSize; ++i)
	{
		// 80% overlap, 20% different
		if (rng.NextFloat() < 0.8f)
			currentMatch.push_back(previousMatch[i]);
		else
			currentMatch.push_back(static_cast<entity_id_t>((i + matchSize) * 2 + 100));
	}
	std::sort(currentMatch.begin(), currentMatch.end());

	std::vector<entity_id_t> added;
	std::vector<entity_id_t> removed;
	added.reserve(matchSize);
	removed.reserve(matchSize);

	for (auto _ : state)
	{
		added.clear();
		removed.clear();

		std::set_difference(currentMatch.begin(), currentMatch.end(),
		                    previousMatch.begin(), previousMatch.end(),
		                    std::back_inserter(added));

		std::set_difference(previousMatch.begin(), previousMatch.end(),
		                    currentMatch.begin(), currentMatch.end(),
		                    std::back_inserter(removed));

		benchmark::DoNotOptimize(added.data());
		benchmark::DoNotOptimize(removed.data());
	}

	state.SetItemsProcessed(int64_t(state.iterations()) * int64_t(matchSize));
}
BENCHMARK(BM_RangeManager_SetDifference)->RangeMultiplier(4)->Range(16, 1024);

// 3. Spatial Grid Subdivision 2D Range Query Benchmark
// Simulates 2D circular entity range queries against a spatial hash grid
static void BM_RangeManager_SpatialGridQuery2D(benchmark::State& state)
{
	const size_t entityCount = static_cast<size_t>(state.range(0));
	const fixed worldSize = fixed::FromInt(512);
	const size_t gridDim = 16; // 512 / 32

	auto entities = SyntheticGridGenerator::GenerateUniformGrid(entityCount, worldSize);

	// Spatial grid: cell (x, z) -> list of entity IDs
	std::vector<std::vector<entity_id_t>> grid(gridDim * gridDim);
	for (const auto& ent : entities)
	{
		int cx = std::clamp(ent.pos.X.ToInt_RoundToZero() / 32, 0, static_cast<int>(gridDim - 1));
		int cz = std::clamp(ent.pos.Y.ToInt_RoundToZero() / 32, 0, static_cast<int>(gridDim - 1));
		grid[cz * gridDim + cx].push_back(ent.id);
	}

	const CFixedVector2D queryCenter(fixed::FromInt(256), fixed::FromInt(256));
	const fixed queryRadius = fixed::FromInt(48);

	std::vector<entity_id_t> results;
	results.reserve(entityCount);

	for (auto _ : state)
	{
		results.clear();

		int minCellX = std::max(0, (queryCenter.X - queryRadius).ToInt_RoundToZero() / 32);
		int maxCellX = std::min(static_cast<int>(gridDim - 1), (queryCenter.X + queryRadius).ToInt_RoundToZero() / 32);
		int minCellZ = std::max(0, (queryCenter.Y - queryRadius).ToInt_RoundToZero() / 32);
		int maxCellZ = std::min(static_cast<int>(gridDim - 1), (queryCenter.Y + queryRadius).ToInt_RoundToZero() / 32);

		for (int cz = minCellZ; cz <= maxCellZ; ++cz)
		{
			for (int cx = minCellX; cx <= maxCellX; ++cx)
			{
				const auto& cellEnts = grid[cz * gridDim + cx];
				for (entity_id_t id : cellEnts)
				{
					const auto& ent = entities[id - 100];
					CFixedVector2D diff = ent.pos - queryCenter;
					if (diff.CompareLength(queryRadius) <= 0)
					{
						results.push_back(id);
					}
				}
			}
		}

		benchmark::DoNotOptimize(results.data());
	}

	state.SetItemsProcessed(int64_t(state.iterations()) * int64_t(entityCount));
}
BENCHMARK(BM_RangeManager_SpatialGridQuery2D)->RangeMultiplier(4)->Range(256, 4096);

// 4. Concurrent Active Query Work-Stealing vs Mutex Contention Benchmark
// Replicates the exact contention behavior observed on RangeManager QueryMutex across threads
struct MockRangeQuery
{
	uint32_t tag;
	CFixedVector2D center;
	fixed radius;
	bool enabled;
};

static void BM_RangeManager_ConcurrentQueryMutex(benchmark::State& state)
{
	const size_t totalQueries = 1000;
	static std::vector<MockRangeQuery> s_Queries;
	static std::mutex s_Mutex;
	static size_t s_CurrentIdx = 0;

	if (state.thread_index() == 0)
	{
		s_Queries.resize(totalQueries);
		for (size_t i = 0; i < totalQueries; ++i)
		{
			s_Queries[i] = { static_cast<uint32_t>(i), CFixedVector2D(fixed::FromInt(static_cast<int>(i % 100)), fixed::FromInt(static_cast<int>((i * 7) % 100))), fixed::FromInt(20), true };
		}
		s_CurrentIdx = 0;
	}

	for (auto _ : state)
	{
		uint64_t processed = 0;
		while (true)
		{
			MockRangeQuery q;
			{
				std::lock_guard<std::mutex> lock(s_Mutex);
				if (s_CurrentIdx >= s_Queries.size())
					break;
				q = s_Queries[s_CurrentIdx++];
			}

			// Simulate query evaluation workload (~50 ns)
			CFixedVector2D offset = q.center.Multiply(fixed::FromFloat(0.5f));
			benchmark::DoNotOptimize(offset);
			processed++;
		}
		benchmark::DoNotOptimize(processed);
	}

	if (state.thread_index() == 0)
	{
		s_CurrentIdx = 0;
	}
}
BENCHMARK(BM_RangeManager_ConcurrentQueryMutex)->ThreadRange(1, 8);

// 5. Incremental LOS Bitmask Calculation Benchmark
// Simulates CCmpRangeManager::LosUpdateHelperIncremental
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

} // anonymous namespace
