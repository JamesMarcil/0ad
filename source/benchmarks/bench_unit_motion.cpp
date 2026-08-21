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

#include "bench_fixtures.h"
#include "maths/Fixed.h"
#include "maths/FixedVector2D.h"
#include <entt/entt.hpp>

namespace
{

using namespace BenchmarkFixtures;

// 1. Unit Motion Physics & Step Integration with Fixed-Point Trigonometry
// Exercises heading rotation, trigonometric velocity vectoring, and position advancement from CCmpUnitMotion::Move
struct UnitSimState
{
	CFixedVector2D pos;
	CFixedVector2D targetPos;
	fixed speed;
	fixed maxSpeed;
	fixed heading;
	fixed turnRate;
};

static void BM_UnitMotion_StepMove(benchmark::State& state)
{
	const size_t unitCount = static_cast<size_t>(state.range(0));
	const fixed dt = fixed::FromFloat(0.2f); // 200 ms turn
	const fixed twoPi = fixed::Pi() * 2;

	std::vector<UnitSimState> units;
	units.reserve(unitCount);

	DeterministicRng rng(0x11223344ULL);
	for (size_t i = 0; i < unitCount; ++i)
	{
		UnitSimState u;
		u.pos = CFixedVector2D(rng.NextFixed(fixed::Zero(), fixed::FromInt(200)), rng.NextFixed(fixed::Zero(), fixed::FromInt(200)));
		u.targetPos = CFixedVector2D(rng.NextFixed(fixed::Zero(), fixed::FromInt(200)), rng.NextFixed(fixed::Zero(), fixed::FromInt(200)));
		u.speed = fixed::FromFloat(5.0f);
		u.maxSpeed = fixed::FromFloat(10.0f);
		u.heading = rng.NextFixed(fixed::Zero(), twoPi);
		u.turnRate = fixed::FromFloat(3.0f);
		units.push_back(u);
	}

	for (auto _ : state)
	{
		for (auto& u : units)
		{
			CFixedVector2D toTarget = u.targetPos - u.pos;
			if (!toTarget.IsZero())
			{
				fixed dist = toTarget.Length();
				fixed maxTurn = u.turnRate.Multiply(dt);
				u.heading = (u.heading + maxTurn);
				if (u.heading > twoPi) u.heading -= twoPi;

				fixed sinH, cosH;
				sincos_approx(u.heading, sinH, cosH);
				CFixedVector2D moveVec(sinH.Multiply(u.speed).Multiply(dt), cosH.Multiply(u.speed).Multiply(dt));
				u.pos += moveVec;
			}
		}
		benchmark::DoNotOptimize(units.data());
	}

	state.SetItemsProcessed(int64_t(state.iterations()) * int64_t(unitCount));
}
BENCHMARK(BM_UnitMotion_StepMove)->RangeMultiplier(4)->Range(64, 4096);

// 2. Bucketed Unit Pushing Collision Resolution Benchmark
// Replicates the spatially-bucketed pairwise pushing resolution logic from CCmpUnitMotionManager::Push
struct UnitPushState
{
	CFixedVector2D pos;
	CFixedVector2D push;
	fixed radius;
	bool isMoving;
};

static void BM_UnitMotion_BucketedPushResolution(benchmark::State& state)
{
	const size_t unitCount = static_cast<size_t>(state.range(0));
	const size_t bucketDim = 16;
	const fixed bucketSize = fixed::FromInt(8);

	std::vector<UnitPushState> units;
	units.reserve(unitCount);

	DeterministicRng rng(0x88776655ULL);
	for (size_t i = 0; i < unitCount; ++i)
	{
		UnitPushState u;
		u.pos = CFixedVector2D(rng.NextFixed(fixed::Zero(), fixed::FromInt(128)), rng.NextFixed(fixed::Zero(), fixed::FromInt(128)));
		u.push = CFixedVector2D();
		u.radius = fixed::FromFloat(1.2f);
		u.isMoving = (i % 2 == 0);
		units.push_back(u);
	}

	std::vector<std::vector<size_t>> buckets(bucketDim * bucketDim);

	for (auto _ : state)
	{
		for (auto& b : buckets) b.clear();
		for (size_t i = 0; i < units.size(); ++i)
		{
			units[i].push = CFixedVector2D();
			int bx = std::clamp((units[i].pos.X / bucketSize).ToInt_RoundToZero(), 0, static_cast<int>(bucketDim - 1));
			int bz = std::clamp((units[i].pos.Y / bucketSize).ToInt_RoundToZero(), 0, static_cast<int>(bucketDim - 1));
			buckets[bz * bucketDim + bx].push_back(i);
		}

		for (int bz = 0; bz < static_cast<int>(bucketDim); ++bz)
		{
			for (int bx = 0; bx < static_cast<int>(bucketDim); ++bx)
			{
				const auto& cellA = buckets[bz * bucketDim + bx];
				for (size_t i : cellA)
				{
					for (int dz = 0; dz <= 1; ++dz)
					{
						for (int dx = (dz == 0 ? 0 : -1); dx <= 1; ++dx)
						{
							int nx = bx + dx;
							int nz = bz + dz;
							if (nx >= 0 && nx < static_cast<int>(bucketDim) && nz >= 0 && nz < static_cast<int>(bucketDim))
							{
								const auto& cellB = buckets[nz * bucketDim + nx];
								for (size_t j : cellB)
								{
									if (i >= j) continue;
									CFixedVector2D delta = units[j].pos - units[i].pos;
									fixed minDist = units[i].radius + units[j].radius;
									if (delta.CompareLength(minDist) < 0)
									{
										fixed dist = delta.Length();
										if (dist > fixed::FromFloat(0.01f))
										{
											fixed overlap = minDist - dist;
											fixed scale = (overlap / dist).Multiply(fixed::FromFloat(0.5f));
											CFixedVector2D pushVec = delta.Multiply(scale);
											units[i].push -= pushVec;
											units[j].push += pushVec;
										}
									}
								}
							}
						}
					}
				}
			}
		}

		for (auto& u : units)
		{
			u.pos += u.push;
		}

		benchmark::DoNotOptimize(units.data());
	}

	state.SetItemsProcessed(int64_t(state.iterations()) * int64_t(unitCount));
}
BENCHMARK(BM_UnitMotion_BucketedPushResolution)->RangeMultiplier(4)->Range(64, 4096);

// 3. MotionMgr_PostMove Pipeline & Incremental LOS Update Benchmark
// Replicates the 6th hottest engine bottleneck in before.tracy (15.24s self-time)
static void BM_UnitMotion_PostMove(benchmark::State& state)
{
	const size_t unitCount = static_cast<size_t>(state.range(0));
	const size_t mapSize = 256;
	std::vector<u32> visibilityGrid(mapSize * mapSize, 0);

	auto entities = SyntheticGridGenerator::GenerateUniformGrid(unitCount, fixed::FromInt(256));
	CFixedVector2D step(fixed::FromFloat(1.5f), fixed::FromFloat(1.5f));
	const u32 playerMask = 1 << 1;
	const int visionRadius = 3;

	for (auto _ : state)
	{
		for (auto& ent : entities)
		{
			ent.pos += step;
			int cx = std::clamp(ent.pos.X.ToInt_RoundToZero(), 5, static_cast<int>(mapSize - 6));
			int cz = std::clamp(ent.pos.Y.ToInt_RoundToZero(), 5, static_cast<int>(mapSize - 6));

			// Incremental LOS bitmask calculation triggered by PostMove
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
		benchmark::DoNotOptimize(entities.data());
	}

	state.SetItemsProcessed(int64_t(state.iterations()) * int64_t(unitCount));
}
BENCHMARK(BM_UnitMotion_PostMove)->RangeMultiplier(4)->Range(64, 4096);

// ----------------------------------------------------------------------------
// EnTT Modernized ECS Comparative Counterparts
// ----------------------------------------------------------------------------

struct MotionComponentEnTT
{
	CFixedVector2D targetPos;
	fixed speed;
	fixed maxSpeed;
	fixed heading;
	fixed turnRate;
};

struct PositionComponentEnTT
{
	CFixedVector2D pos;
};

static void BM_UnitMotion_StepMove_EnTT(benchmark::State& state)
{
	const size_t unitCount = static_cast<size_t>(state.range(0));
	const fixed dt = fixed::FromFloat(0.2f);
	const fixed twoPi = fixed::Pi() * 2;

	entt::registry registry;
	DeterministicRng rng(0x11223344ULL);

	for (size_t i = 0; i < unitCount; ++i)
	{
		auto e = registry.create();
		registry.emplace<PositionComponentEnTT>(e, CFixedVector2D(rng.NextFixed(fixed::Zero(), fixed::FromInt(200)), rng.NextFixed(fixed::Zero(), fixed::FromInt(200))));
		registry.emplace<MotionComponentEnTT>(e,
			CFixedVector2D(rng.NextFixed(fixed::Zero(), fixed::FromInt(200)), rng.NextFixed(fixed::Zero(), fixed::FromInt(200))),
			fixed::FromFloat(5.0f),
			fixed::FromFloat(10.0f),
			rng.NextFixed(fixed::Zero(), twoPi),
			fixed::FromFloat(3.0f));
	}

	for (auto _ : state)
	{
		auto view = registry.view<PositionComponentEnTT, MotionComponentEnTT>();
		view.each([&](PositionComponentEnTT& posComp, MotionComponentEnTT& motionComp) {
			CFixedVector2D toTarget = motionComp.targetPos - posComp.pos;
			if (!toTarget.IsZero())
			{
				fixed maxTurn = motionComp.turnRate.Multiply(dt);
				motionComp.heading = (motionComp.heading + maxTurn);
				if (motionComp.heading > twoPi) motionComp.heading -= twoPi;

				fixed sinH, cosH;
				sincos_approx(motionComp.heading, sinH, cosH);
				CFixedVector2D moveVec(sinH.Multiply(motionComp.speed).Multiply(dt), cosH.Multiply(motionComp.speed).Multiply(dt));
				posComp.pos += moveVec;
			}
		});
		benchmark::ClobberMemory();
	}

	state.SetItemsProcessed(int64_t(state.iterations()) * int64_t(unitCount));
}
BENCHMARK(BM_UnitMotion_StepMove_EnTT)->RangeMultiplier(4)->Range(64, 4096);

static void BM_UnitMotion_PostMove_EnTT(benchmark::State& state)
{
	const size_t unitCount = static_cast<size_t>(state.range(0));
	const size_t mapSize = 256;
	std::vector<u32> visibilityGrid(mapSize * mapSize, 0);

	entt::registry registry;
	auto entities = SyntheticGridGenerator::GenerateUniformGrid(unitCount, fixed::FromInt(256));
	for (const auto& ent : entities)
	{
		auto e = registry.create();
		registry.emplace<PositionComponentEnTT>(e, CFixedVector2D(ent.pos.X, ent.pos.Y));
	}

	CFixedVector2D step(fixed::FromFloat(1.5f), fixed::FromFloat(1.5f));
	const u32 playerMask = 1 << 1;
	const int visionRadius = 3;

	for (auto _ : state)
	{
		auto view = registry.view<PositionComponentEnTT>();
		view.each([&](PositionComponentEnTT& posComp) {
			posComp.pos += step;
			int cx = std::clamp(posComp.pos.X.ToInt_RoundToZero(), 5, static_cast<int>(mapSize - 6));
			int cz = std::clamp(posComp.pos.Y.ToInt_RoundToZero(), 5, static_cast<int>(mapSize - 6));

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
		});
		benchmark::DoNotOptimize(visibilityGrid.data());
	}

	state.SetItemsProcessed(int64_t(state.iterations()) * int64_t(unitCount));
}
BENCHMARK(BM_UnitMotion_PostMove_EnTT)->RangeMultiplier(4)->Range(64, 4096);

} // anonymous namespace
