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
#include <queue>
#include <algorithm>
#include <cmath>

#include "bench_fixtures.h"
#include "maths/FixedVector2D.h"

namespace
{

using namespace BenchmarkFixtures;

// 1. Grid Navcell Clearance & Passability Lookup Benchmark
// Exercises tile lookup and bitmask clearance checking corresponding to IS_PASSABLE(grid.get(x, z), passClass)
struct NavcellGrid
{
	size_t width;
	size_t height;
	std::vector<u16> cells;

	inline bool IsPassable(int x, int z, u16 passClass) const
	{
		if (x < 0 || x >= static_cast<int>(width) || z < 0 || z >= static_cast<int>(height))
			return false;
		return (cells[z * width + x] & passClass) == 0;
	}
};

static void BM_Pathfinding_NavcellClearance(benchmark::State& state)
{
	const size_t gridDim = static_cast<size_t>(state.range(0));
	NavcellGrid grid;
	grid.width = gridDim;
	grid.height = gridDim;
	grid.cells.resize(gridDim * gridDim, 0);

	// Populate 10% obstacle density
	DeterministicRng rng(0x99887766ULL);
	for (size_t i = 0; i < grid.cells.size(); ++i)
	{
		if (rng.NextFloat() < 0.10f)
			grid.cells[i] = 0x01; // Impassable
	}

	std::vector<std::pair<int, int>> queryCoords;
	queryCoords.reserve(1000);
	for (size_t i = 0; i < 1000; ++i)
	{
		queryCoords.emplace_back(rng.NextRange(0, gridDim - 1), rng.NextRange(0, gridDim - 1));
	}

	for (auto _ : state)
	{
		uint32_t passableCount = 0;
		for (const auto& [x, z] : queryCoords)
		{
			if (grid.IsPassable(x, z, 0x01))
				passableCount++;
		}
		benchmark::DoNotOptimize(passableCount);
	}

	state.SetItemsProcessed(int64_t(state.iterations()) * 1000);
}
BENCHMARK(BM_Pathfinding_NavcellClearance)->RangeMultiplier(2)->Range(128, 512);

// 2. Jump Point Search (JPS) Straight-Line Scan Benchmark
// Simulates JPS horizontal/vertical and diagonal jump line scanning across navcells
static void BM_Pathfinding_JPS_Scan(benchmark::State& state)
{
	const size_t pathLen = static_cast<size_t>(state.range(0));
	const size_t gridDim = 512;
	NavcellGrid grid;
	grid.width = gridDim;
	grid.height = gridDim;
	grid.cells.resize(gridDim * gridDim, 0);

	// Place an obstacle at pathLen
	grid.cells[100 * gridDim + (100 + pathLen)] = 0x01;

	for (auto _ : state)
	{
		int x = 100;
		int z = 100;
		int dx = 1;
		int dz = 0;

		while (grid.IsPassable(x + dx, z + dz, 0x01))
		{
			x += dx;
			z += dz;
		}

		benchmark::DoNotOptimize(x);
		benchmark::DoNotOptimize(z);
	}

	state.SetItemsProcessed(int64_t(state.iterations()) * int64_t(pathLen));
}
BENCHMARK(BM_Pathfinding_JPS_Scan)->RangeMultiplier(2)->Range(16, 256);

// 3. Vertex Short-Path Line-of-Sight Raycast Benchmark
// Exercises local obstacle avoidance raycasts in VertexPathfinder::ComputeShortPath
struct ObstacleEdge
{
	CFixedVector2D p0;
	CFixedVector2D p1;
};

static inline bool RayIntersectsSegment(const CFixedVector2D& rayOrigin, const CFixedVector2D& rayDir,
                                       const ObstacleEdge& edge)
{
	CFixedVector2D v1 = rayOrigin - edge.p0;
	CFixedVector2D v2 = edge.p1 - edge.p0;
	CFixedVector2D v3(-rayDir.Y, rayDir.X);

	fixed dot = v2.Dot(v3);
	if (dot.IsZero())
		return false;

	fixed t1 = (v2.X.Multiply(v1.Y) - v2.Y.Multiply(v1.X)) / dot;
	fixed t2 = v1.Dot(v3) / dot;

	return (t1 >= fixed::Zero() && t2 >= fixed::Zero() && t2 <= fixed::FromInt(1));
}

static void BM_Pathfinding_VertexRaycast(benchmark::State& state)
{
	const size_t edgeCount = static_cast<size_t>(state.range(0));
	DeterministicRng rng(0x55667788ULL);

	std::vector<ObstacleEdge> edges;
	edges.reserve(edgeCount);

	for (size_t i = 0; i < edgeCount; ++i)
	{
		fixed x = rng.NextFixed(fixed::FromInt(0), fixed::FromInt(100));
		fixed z = rng.NextFixed(fixed::FromInt(0), fixed::FromInt(100));
		edges.push_back({ CFixedVector2D(x, z), CFixedVector2D(x + fixed::FromInt(5), z + fixed::FromInt(5)) });
	}

	CFixedVector2D rayOrigin(fixed::FromInt(50), fixed::FromInt(50));
	CFixedVector2D rayDir(fixed::FromFloat(0.7071f), fixed::FromFloat(0.7071f));

	for (auto _ : state)
	{
		size_t hitCount = 0;
		for (const auto& edge : edges)
		{
			if (RayIntersectsSegment(rayOrigin, rayDir, edge))
				hitCount++;
		}
		benchmark::DoNotOptimize(hitCount);
	}

	state.SetItemsProcessed(int64_t(state.iterations()) * int64_t(edgeCount));
}
BENCHMARK(BM_Pathfinding_VertexRaycast)->RangeMultiplier(4)->Range(16, 1024);

// 4. Nearest Passable Navcell Search (MakeGoalReachable) Benchmark
static void BM_Pathfinding_MakeGoalReachable(benchmark::State& state)
{
	const size_t maxRadius = static_cast<size_t>(state.range(0));
	const size_t gridDim = 256;
	NavcellGrid grid;
	grid.width = gridDim;
	grid.height = gridDim;
	grid.cells.resize(gridDim * gridDim, 0x01); // All impassable except outer ring

	int centerX = 128;
	int centerZ = 128;

	// Set target passable navcell at distance maxRadius
	grid.cells[(centerZ + static_cast<int>(maxRadius)) * gridDim + centerX] = 0;

	for (auto _ : state)
	{
		int foundX = -1, foundZ = -1;

		for (int r = 1; r <= static_cast<int>(maxRadius); ++r)
		{
			for (int dx = -r; dx <= r && foundX == -1; ++dx)
			{
				for (int dz = -r; dz <= r && foundX == -1; ++dz)
				{
					if (std::max(std::abs(dx), std::abs(dz)) == r)
					{
						if (grid.IsPassable(centerX + dx, centerZ + dz, 0x01))
						{
							foundX = centerX + dx;
							foundZ = centerZ + dz;
							break;
						}
					}
				}
			}
		}

		benchmark::DoNotOptimize(foundX);
		benchmark::DoNotOptimize(foundZ);
	}

	state.SetItemsProcessed(int64_t(state.iterations()) * int64_t(maxRadius));
}
BENCHMARK(BM_Pathfinding_MakeGoalReachable)->RangeMultiplier(2)->Range(4, 32);

} // anonymous namespace
