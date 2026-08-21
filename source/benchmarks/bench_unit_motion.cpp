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

namespace
{

using namespace BenchmarkFixtures;

// 1. Unit Motion Physics & Step Integration Benchmark
// Exercises heading rotation, velocity dampening, and position advancement from CCmpUnitMotion::Move
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
		u.heading = rng.NextFixed(fixed::Zero(), fixed::FromFloat(6.2831853f));
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
				u.heading = (u.heading + maxTurn) % fixed::FromFloat(6.2831853f);

				toTarget.Normalize(std::min(dist, u.speed.Multiply(dt)));
				u.pos += toTarget;
			}
		}
		benchmark::DoNotOptimize(units.data());
	}

	state.SetItemsProcessed(int64_t(state.iterations()) * int64_t(unitCount));
}
BENCHMARK(BM_UnitMotion_StepMove)->RangeMultiplier(4)->Range(64, 4096);

// 2. Unit Pushing Collision Resolution Benchmark
// Replicates the pairwise pushing resolution logic from CCmpUnitMotionManager::Push
struct UnitPushState
{
	CFixedVector2D pos;
	CFixedVector2D push;
	fixed radius;
	bool isMoving;
};

static void BM_UnitMotion_PushResolution(benchmark::State& state)
{
	const size_t unitCount = static_cast<size_t>(state.range(0));

	std::vector<UnitPushState> units;
	units.reserve(unitCount);

	DeterministicRng rng(0x88776655ULL);
	// Place units tightly packed in a 50x50 area
	for (size_t i = 0; i < unitCount; ++i)
	{
		UnitPushState u;
		u.pos = CFixedVector2D(rng.NextFixed(fixed::Zero(), fixed::FromInt(50)), rng.NextFixed(fixed::Zero(), fixed::FromInt(50)));
		u.push = CFixedVector2D();
		u.radius = fixed::FromFloat(1.2f);
		u.isMoving = (i % 2 == 0);
		units.push_back(u);
	}

	for (auto _ : state)
	{
		for (size_t i = 0; i < units.size(); ++i)
		{
			units[i].push = CFixedVector2D();
		}

		for (size_t i = 0; i < units.size(); ++i)
		{
			for (size_t j = i + 1; j < units.size(); ++j)
			{
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

		for (auto& u : units)
		{
			u.pos += u.push;
		}

		benchmark::DoNotOptimize(units.data());
	}

	state.SetItemsProcessed(int64_t(state.iterations()) * int64_t(unitCount));
}
BENCHMARK(BM_UnitMotion_PushResolution)->RangeMultiplier(2)->Range(32, 256);

} // anonymous namespace
