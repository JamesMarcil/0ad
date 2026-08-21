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
#include <string>
#include <map>
#include <unordered_map>
#include <cmath>

#include "bench_fixtures.h"
#include "ps/CStr.h"
#include "maths/Vector3D.h"
#include "maths/Matrix3D.h"

namespace
{

using namespace BenchmarkFixtures;

// 1. ObjectKey Dynamic Allocation & Map Lookup Benchmark
// Replicates CObjectManager::FindObjectVariation (7.01s self-time, 508k calls):
// Heap allocation of std::vector<u8> choices + std::map<ObjectKey, uint32_t> lookup
struct BenchObjectKey
{
	CStr id;
	std::vector<u8> choices;

	bool operator<(const BenchObjectKey& other) const
	{
		if (id != other.id)
			return id < other.id;
		return choices < other.choices;
	}
};

static void BM_ObjectManager_VariationKeyLookup(benchmark::State& state)
{
	const size_t mapSize = static_cast<size_t>(state.range(0));
	std::map<BenchObjectKey, uint32_t> objectCache;

	for (size_t i = 0; i < mapSize; ++i)
	{
		BenchObjectKey key;
		key.id = CStr("art/actors/units/hellenes/infantry_spearman");
		key.choices = { static_cast<u8>(i % 4), static_cast<u8>((i / 4) % 3), static_cast<u8>((i / 12) % 2) };
		objectCache[key] = static_cast<uint32_t>(i);
	}

	DeterministicRng rng(0x44332211ULL);

	for (auto _ : state)
	{
		uint32_t sum = 0;
		for (size_t i = 0; i < 500; ++i)
		{
			// Heap allocate variation vector per callsite (exact engine bottleneck)
			std::vector<u8> choices = {
				static_cast<u8>(rng.NextRange(0, 3)),
				static_cast<u8>(rng.NextRange(0, 2)),
				static_cast<u8>(rng.NextRange(0, 1))
			};

			BenchObjectKey queryKey;
			queryKey.id = CStr("art/actors/units/hellenes/infantry_spearman");
			queryKey.choices = std::move(choices);

			auto it = objectCache.find(queryKey);
			if (it != objectCache.end())
				sum += it->second;
		}
		benchmark::DoNotOptimize(sum);
	}

	state.SetItemsProcessed(int64_t(state.iterations()) * 500);
}
BENCHMARK(BM_ObjectManager_VariationKeyLookup)->RangeMultiplier(4)->Range(64, 1024);

// 2. UnitRenderer::RenderSubmit Pipeline Benchmark
// Replicates the 5th hottest engine bottleneck in before.tracy (16.08s self-time):
// Component interface retrieval, coordinate transformation, frustum sphere culling, and bucketed sorting
struct FrustumPlane
{
	CVector3D normal;
	float dist;

	inline bool IsBehind(const CVector3D& center, float radius) const
	{
		return (normal.Dot(center) + dist) < -radius;
	}
};

struct UnitRenderData
{
	CFixedVector3D simPos;
	fixed simHeading;
	float radius;
	uint32_t modelDefId;
};

static void BM_UnitRenderer_RenderSubmit_Pipeline(benchmark::State& state)
{
	const size_t unitCount = static_cast<size_t>(state.range(0));
	std::vector<UnitRenderData> units;
	units.reserve(unitCount);

	DeterministicRng rng(0x77889900ULL);
	for (size_t i = 0; i < unitCount; ++i)
	{
		UnitRenderData u;
		u.simPos = CFixedVector3D(rng.NextFixed(fixed::FromInt(-200), fixed::FromInt(200)),
		                          fixed::FromInt(0),
		                          rng.NextFixed(fixed::FromInt(-200), fixed::FromInt(200)));
		u.simHeading = rng.NextFixed(fixed::Zero(), fixed::Pi() * 2);
		u.radius = rng.NextFloat(1.2f, 2.5f);
		u.modelDefId = static_cast<uint32_t>(i % 16); // 16 model buckets
		units.push_back(u);
	}

	FrustumPlane planes[6] = {
		{ CVector3D(1.0f, 0.0f, 0.0f).Normalized(), 100.0f },
		{ CVector3D(-1.0f, 0.0f, 0.0f).Normalized(), 100.0f },
		{ CVector3D(0.0f, 1.0f, 0.0f).Normalized(), 50.0f },
		{ CVector3D(0.0f, -1.0f, 0.0f).Normalized(), 50.0f },
		{ CVector3D(0.0f, 0.0f, 1.0f).Normalized(), 150.0f },
		{ CVector3D(0.0f, 0.0f, -1.0f).Normalized(), 150.0f }
	};

	std::vector<std::vector<CMatrix3D>> modelBuckets(16);

	for (auto _ : state)
	{
		for (auto& b : modelBuckets) b.clear();

		size_t visibleCount = 0;
		for (const auto& u : units)
		{
			CVector3D worldPos(u.simPos.X.ToFloat(), u.simPos.Y.ToFloat(), u.simPos.Z.ToFloat());

			bool culled = false;
			for (int p = 0; p < 6; ++p)
			{
				if (planes[p].IsBehind(worldPos, u.radius))
				{
					culled = true;
					break;
				}
			}

			if (!culled)
			{
				CMatrix3D transform;
				transform.SetIdentity();
				transform.RotateY(u.simHeading.ToFloat());
				transform.Translate(worldPos.X, worldPos.Y, worldPos.Z);
				modelBuckets[u.modelDefId].push_back(transform);
				visibleCount++;
			}
		}

		benchmark::DoNotOptimize(visibleCount);
		benchmark::DoNotOptimize(modelBuckets.data());
	}

	state.SetItemsProcessed(int64_t(state.iterations()) * int64_t(unitCount));
}
BENCHMARK(BM_UnitRenderer_RenderSubmit_Pipeline)->RangeMultiplier(4)->Range(256, 4096);

// 3. Angular and Coordinate Transform Interpolation Benchmark
// Replicates UnitRenderer::Interpolate (4.30s in profile)
static void BM_UnitRenderer_TransformInterpolation(benchmark::State& state)
{
	const size_t count = static_cast<size_t>(state.range(0));
	const float progress = 0.65f;

	struct SimTransformState
	{
		CFixedVector3D prevPos;
		CFixedVector3D nextPos;
		fixed prevHeading;
		fixed nextHeading;
	};

	std::vector<SimTransformState> states;
	states.reserve(count);
	DeterministicRng rng(0x8899aabbULL);

	for (size_t i = 0; i < count; ++i)
	{
		SimTransformState s;
		s.prevPos = CFixedVector3D(fixed::FromInt(static_cast<int>(i)), fixed::Zero(), fixed::FromInt(static_cast<int>(i * 2)));
		s.nextPos = CFixedVector3D(fixed::FromInt(static_cast<int>(i + 2)), fixed::FromFloat(0.5f), fixed::FromInt(static_cast<int>(i * 2 + 2)));
		s.prevHeading = rng.NextFixed(fixed::Zero(), fixed::Pi() * 2);
		s.nextHeading = s.prevHeading + fixed::FromFloat(0.3f);
		states.push_back(s);
	}

	std::vector<CMatrix3D> outTransforms(count);

	for (auto _ : state)
	{
		for (size_t i = 0; i < count; ++i)
		{
			const auto& s = states[i];
			float posX = s.prevPos.X.ToFloat() * (1.0f - progress) + s.nextPos.X.ToFloat() * progress;
			float posY = s.prevPos.Y.ToFloat() * (1.0f - progress) + s.nextPos.Y.ToFloat() * progress;
			float posZ = s.prevPos.Z.ToFloat() * (1.0f - progress) + s.nextPos.Z.ToFloat() * progress;
			float heading = s.prevHeading.ToFloat() * (1.0f - progress) + s.nextHeading.ToFloat() * progress;

			outTransforms[i].SetIdentity();
			outTransforms[i].RotateY(heading);
			outTransforms[i].Translate(posX, posY, posZ);
		}
		benchmark::DoNotOptimize(outTransforms.data());
	}

	state.SetItemsProcessed(int64_t(state.iterations()) * int64_t(count));
}
BENCHMARK(BM_UnitRenderer_TransformInterpolation)->RangeMultiplier(4)->Range(256, 4096);

} // anonymous namespace
