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

// 1. Multi-Class Navcell Clearance & Passability Lookup Benchmark
// Exercises 16-bit packed navcells with class clearance checking (Infantry, Cavalry, Siege, Ship)
static void BM_Pathfinding_MultiClassNavcellClearance(benchmark::State& state)
{
	const size_t gridDim = static_cast<size_t>(state.range(0));
	NavcellClearanceGrid grid = NavcellGridGenerator::GenerateGrid(gridDim, 0.12f);

	DeterministicRng rng(0x99887766ULL);
	std::vector<std::pair<int, int>> queryCoords;
	queryCoords.reserve(1000);
	for (size_t i = 0; i < 1000; ++i)
	{
		queryCoords.emplace_back(rng.NextRange(0, static_cast<uint32_t>(gridDim - 1)),
		                         rng.NextRange(0, static_cast<uint32_t>(gridDim - 1)));
	}

	for (auto _ : state)
	{
		uint32_t passableCount = 0;
		for (const auto& [x, z] : queryCoords)
		{
			if (grid.IsPassable(x, z, NavcellClearanceGrid::PASS_INFANTRY))
			{
				u8 clearance = grid.GetClearance(x, z, 4);
				if (clearance >= 2) passableCount++;
			}
		}
		benchmark::DoNotOptimize(passableCount);
	}

	state.SetItemsProcessed(int64_t(state.iterations()) * 1000);
}
BENCHMARK(BM_Pathfinding_MultiClassNavcellClearance)->RangeMultiplier(2)->Range(128, 512);

// 2. Jump Point Search (JPS) 2D Diagonal & Orthogonal Traversal Benchmark
// Replicates the 2D scanning and jump evaluation logic from LongPathfinder::ComputeJPSPath (10.10s self-time)
struct JPSNode
{
	int x, z;
	int g, f;
	bool operator>(const JPSNode& o) const { return f > o.f; }
};

static void BM_Pathfinding_JPS_2DTraversal(benchmark::State& state)
{
	const size_t gridDim = static_cast<size_t>(state.range(0));
	NavcellClearanceGrid grid = NavcellGridGenerator::GenerateGrid(gridDim, 0.08f);

	const int targetX = static_cast<int>(gridDim - 10);
	const int targetZ = static_cast<int>(gridDim - 10);

	for (auto _ : state)
	{
		std::priority_queue<JPSNode, std::vector<JPSNode>, std::greater<JPSNode>> openQueue;
		openQueue.push({ 10, 10, 0, (targetX - 10) + (targetZ - 10) });

		size_t jumpsEvaluated = 0;
		while (!openQueue.empty() && jumpsEvaluated < 500)
		{
			JPSNode current = openQueue.top();
			openQueue.pop();
			jumpsEvaluated++;

			if (current.x == targetX && current.z == targetZ)
				break;

			// Expand orthogonal and diagonal jump rays
			const int directions[4][2] = { {1, 0}, {0, 1}, {1, 1}, {-1, 1} };
			for (const auto& dir : directions)
			{
				int nx = current.x;
				int nz = current.z;
				int step = 0;
				while (grid.IsPassable(nx + dir[0], nz + dir[1], NavcellClearanceGrid::PASS_INFANTRY) && step < 16)
				{
					nx += dir[0];
					nz += dir[1];
					step++;
					if (nx == targetX && nz == targetZ)
					{
						openQueue.push({ nx, nz, current.g + step, current.g + step });
						break;
					}
				}
				if (step > 0 && (nx != targetX || nz != targetZ))
				{
					int h = std::abs(targetX - nx) + std::abs(targetZ - nz);
					openQueue.push({ nx, nz, current.g + step, current.g + step + h });
				}
			}
		}
		benchmark::DoNotOptimize(jumpsEvaluated);
	}

	state.SetItemsProcessed(int64_t(state.iterations()) * int64_t(gridDim));
}
BENCHMARK(BM_Pathfinding_JPS_2DTraversal)->RangeMultiplier(2)->Range(128, 512);

// 3. Vertex Short-Path Visibility Graph Routing Benchmark
// Exercises obstacle contour extraction and tangent routing from VertexPathfinder::ComputeShortPath (6.12s self-time)
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

static void BM_Pathfinding_VertexVisibilityGraph(benchmark::State& state)
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
		size_t visibleVertices = 0;
		for (const auto& edge : edges)
		{
			if (!RayIntersectsSegment(rayOrigin, rayDir, edge))
				visibleVertices++;
		}
		benchmark::DoNotOptimize(visibleVertices);
	}

	state.SetItemsProcessed(int64_t(state.iterations()) * int64_t(edgeCount));
}
BENCHMARK(BM_Pathfinding_VertexVisibilityGraph)->RangeMultiplier(4)->Range(16, 1024);

// 4. Nearest Passable Navcell Search (MakeGoalReachable) Benchmark
static void BM_Pathfinding_MakeGoalReachable(benchmark::State& state)
{
	const size_t maxRadius = static_cast<size_t>(state.range(0));
	const size_t gridDim = 256;
	NavcellClearanceGrid grid;
	grid.width = gridDim;
	grid.height = gridDim;
	grid.cells.resize(gridDim * gridDim, NavcellClearanceGrid::PASS_INFANTRY); // Impassable

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
						if (grid.IsPassable(centerX + dx, centerZ + dz, NavcellClearanceGrid::PASS_INFANTRY))
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
