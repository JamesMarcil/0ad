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

// 1. ObjectKey Construction & Map Lookup Benchmark
// Replicates the exact hash/key lookup structure from CObjectManager::FindObjectVariation:
// struct ObjectKey { CStr id; std::vector<u8> choices; };
// std::map<ObjectKey, ...> m_Objects;
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
		std::string name = "art/actors/units/hellenes/infantry_spearman_" + std::to_string(i % 20);
		key.id = CStr(name.c_str());
		key.choices = { static_cast<u8>(i % 3), static_cast<u8>((i / 3) % 4), static_cast<u8>((i / 12) % 2) };
		objectCache[key] = static_cast<uint32_t>(i);
	}

	DeterministicRng rng(0x44332211ULL);
	std::vector<BenchObjectKey> queryKeys;
	queryKeys.reserve(1000);
	for (size_t i = 0; i < 1000; ++i)
	{
		BenchObjectKey key;
		size_t idx = rng.NextRange(0, mapSize - 1);
		std::string name = "art/actors/units/hellenes/infantry_spearman_" + std::to_string(idx % 20);
		key.id = CStr(name.c_str());
		key.choices = { static_cast<u8>(idx % 3), static_cast<u8>((idx / 3) % 4), static_cast<u8>((idx / 12) % 2) };
		queryKeys.push_back(key);
	}

	for (auto _ : state)
	{
		uint32_t sum = 0;
		for (const auto& key : queryKeys)
		{
			auto it = objectCache.find(key);
			if (it != objectCache.end())
			{
				sum += it->second;
			}
		}
		benchmark::DoNotOptimize(sum);
	}

	state.SetItemsProcessed(int64_t(state.iterations()) * 1000);
}
BENCHMARK(BM_ObjectManager_VariationKeyLookup)->RangeMultiplier(4)->Range(64, 1024);

// 2. Frustum Bounding-Sphere Culling Benchmark
// Exercises batch frustum culling from CCmpUnitRenderer::RenderSubmit
struct BoundingSphere
{
	CVector3D center;
	float radius;
};

struct FrustumPlane
{
	CVector3D normal;
	float dist;

	inline bool IsBehind(const CVector3D& center, float radius) const
	{
		return (normal.Dot(center) + dist) < -radius;
	}
};

static void BM_UnitRenderer_FrustumCulling(benchmark::State& state)
{
	const size_t unitCount = static_cast<size_t>(state.range(0));
	std::vector<BoundingSphere> spheres;
	spheres.reserve(unitCount);

	DeterministicRng rng(0x77889900ULL);
	for (size_t i = 0; i < unitCount; ++i)
	{
		BoundingSphere s;
		s.center = CVector3D(rng.NextFloat(-200.0f, 200.0f), rng.NextFloat(0.0f, 10.0f), rng.NextFloat(-200.0f, 200.0f));
		s.radius = rng.NextFloat(1.0f, 3.0f);
		spheres.push_back(s);
	}

	// 6 Frustum planes (left, right, bottom, top, near, far)
	FrustumPlane planes[6] = {
		{ CVector3D(1.0f, 0.0f, 0.0f).Normalized(), 100.0f },
		{ CVector3D(-1.0f, 0.0f, 0.0f).Normalized(), 100.0f },
		{ CVector3D(0.0f, 1.0f, 0.0f).Normalized(), 50.0f },
		{ CVector3D(0.0f, -1.0f, 0.0f).Normalized(), 50.0f },
		{ CVector3D(0.0f, 0.0f, 1.0f).Normalized(), 150.0f },
		{ CVector3D(0.0f, 0.0f, -1.0f).Normalized(), 150.0f }
	};

	std::vector<u8> visibility(unitCount, 1);

	for (auto _ : state)
	{
		size_t visibleCount = 0;
		for (size_t i = 0; i < unitCount; ++i)
		{
			u8 visible = 1;
			for (int p = 0; p < 6; ++p)
			{
				if (planes[p].IsBehind(spheres[i].center, spheres[i].radius))
				{
					visible = 0;
					break;
				}
			}
			visibility[i] = visible;
			if (visible) visibleCount++;
		}
		benchmark::DoNotOptimize(visibleCount);
		benchmark::DoNotOptimize(visibility.data());
	}

	state.SetItemsProcessed(int64_t(state.iterations()) * int64_t(unitCount));
}
BENCHMARK(BM_UnitRenderer_FrustumCulling)->RangeMultiplier(4)->Range(256, 4096);

// 3. Matrix Transform Interpolation Benchmark
static void BM_UnitRenderer_TransformInterpolation(benchmark::State& state)
{
	const size_t count = static_cast<size_t>(state.range(0));
	const float progress = 0.65f;

	std::vector<CMatrix3D> prevTransforms;
	std::vector<CMatrix3D> nextTransforms;
	std::vector<CMatrix3D> outTransforms;
	prevTransforms.resize(count);
	nextTransforms.resize(count);
	outTransforms.resize(count);

	for (size_t i = 0; i < count; ++i)
	{
		prevTransforms[i].SetIdentity();
		prevTransforms[i].Translate(static_cast<float>(i), 0.0f, static_cast<float>(i * 2));
		nextTransforms[i].SetIdentity();
		nextTransforms[i].Translate(static_cast<float>(i + 1), 1.0f, static_cast<float>(i * 2 + 1));
	}

	for (auto _ : state)
	{
		for (size_t i = 0; i < count; ++i)
		{
			// Simple affine matrix interpolation
			for (int row = 0; row < 4; ++row)
			{
				for (int col = 0; col < 4; ++col)
				{
					outTransforms[i]._data[row * 4 + col] =
						prevTransforms[i]._data[row * 4 + col] * (1.0f - progress) +
						nextTransforms[i]._data[row * 4 + col] * progress;
				}
			}
		}
		benchmark::DoNotOptimize(outTransforms.data());
	}

	state.SetItemsProcessed(int64_t(state.iterations()) * int64_t(count));
}
BENCHMARK(BM_UnitRenderer_TransformInterpolation)->RangeMultiplier(4)->Range(256, 4096);

} // anonymous namespace
