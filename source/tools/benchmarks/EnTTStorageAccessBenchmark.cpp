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

/**
 * Benchmark: EnTT storage access patterns.
 *
 * Measures empirical costs of various component storage access patterns to settle design
 * questions before CCmpPosition (bd_0ad-1u1.2.2) porting:
 *
 * (a) Virtual accessor baseline (no EnTT)
 * (b) Per-call type-hash lookup via entt::registry::get<T>(e)
 * (c) Cached pool pointer + pool->get(e) (EnTTComponent's pattern)
 * (d) Multi-pool access (1/2/3 pools) to measure storage-split cost
 * (e) Direct pool iteration (sparse-set view)
 *
 * Axes: creation order vs shuffled order, 100% density vs 25% density.
 * N = 10,000 entities.
 */

#include <benchmark/benchmark.h>
#include <entt/entity/registry.hpp>

#include <algorithm>
#include <random>
#include <vector>

// Storage structs (small POD, per ADR-001 guidance)
struct State1
{
	float x;
	float y;
	int id;
};

struct State2
{
	float vx;
	float vy;
};

struct State3
{
	float ax;
	float ay;
};

// CCmpPosition-sized POD structs (~132 bytes total, split across three pools)
// Mirror the real component's field groupings for realistic cache behavior
// MSVC x64 actual measurements (verified with sizeof()): 36+52+44 = 132 bytes total

// Template-like: rarely-changing config data (36 bytes MSVC x64)
struct TemplateStateData
{
	int anchorType;              // 4 bytes
	bool floating;               // 1 byte
	float floatDepth;            // 4 bytes
	float rotYSpeed;             // 4 bytes
	bool relativeToGround;       // 1 byte
	float constructionProgress;  // 4 bytes
	float reservedPad[2];        // 8 bytes
	uint32_t pad1;               // 4 bytes
	// Total MSVC x64: 36 bytes (bool fields add padding for alignment)
};
static_assert(sizeof(TemplateStateData) == 36, "TemplateStateData must be 36 bytes (MSVC x64)");

// State-like: hot per-frame data (52 bytes MSVC x64)
struct DynamicStateData
{
	bool inWorld;                // 1 byte
	float x, z;                  // 8 bytes
	float lastX, lastZ;          // 8 bytes
	float prevX, prevZ;          // 8 bytes
	float y;                     // 4 bytes
	float lastYDifference;       // 4 bytes
	float rotX, rotY, rotZ;      // 12 bytes
	float reserved;              // 4 bytes
	// Total MSVC x64: 52 bytes (bool + 3 bytes padding for alignment)
};
static_assert(sizeof(DynamicStateData) == 52, "DynamicStateData must be 52 bytes (MSVC x64)");

// Derived-like: interpolation and turret data (44 bytes MSVC x64)
struct DerivedStateData
{
	uint32_t turretParent;       // 4 bytes
	float turretPosX, turretPosY, turretPosZ;  // 12 bytes
	float interpRotX, interpRotY, interpRotZ;  // 12 bytes
	float lastInterpRotX, lastInterpRotZ;      // 8 bytes
	uint32_t turretSetSize;      // 4 bytes
	float reserved[1];           // 4 bytes
	// Total MSVC x64: 44 bytes (no bool padding needed)
};
static_assert(sizeof(DerivedStateData) == 44, "DerivedStateData must be 44 bytes (MSVC x64)");

// Combined struct: all three field groups in one struct (132 bytes total)
// Used as the correct single-pool baseline for comparing against multi-pool variants
struct CombinedStateData
{
	// Template-like fields (36 bytes)
	int anchorType;
	bool floating;
	float floatDepth;
	float rotYSpeed;
	bool relativeToGround;
	float constructionProgress;
	float templateReserved[2];
	uint32_t templatePad1;

	// Dynamic-like fields (52 bytes)
	bool inWorld;
	float x, z;
	float lastX, lastZ;
	float prevX, prevZ;
	float y;
	float lastYDifference;
	float rotX, rotY, rotZ;
	float dynamicReserved;

	// Derived-like fields (44 bytes)
	uint32_t turretParent;
	float turretPosX, turretPosY, turretPosZ;
	float interpRotX, interpRotY, interpRotZ;
	float lastInterpRotX, lastInterpRotZ;
	uint32_t turretSetSize;
	float derivedReserved[1];

	// Total: 132 bytes (36 + 52 + 44)
};
static_assert(sizeof(CombinedStateData) == 132, "CombinedStateData must be 132 bytes (MSVC x64)");

// Virtual accessor baseline: abstract interface + virtual method
class IAccessor
{
public:
	virtual ~IAccessor() = default;
	virtual float GetX() const = 0;
};

class ConcreteAccessor : public IAccessor
{
public:
	explicit ConcreteAccessor(float x) : m_X(x) {}
	float GetX() const override { return m_X; }

private:
	float m_X;
};

// Helper to generate shuffled indices with fixed seed for reproducibility
std::vector<size_t> GenerateShuffledIndices(size_t count)
{
	std::vector<size_t> indices;
	indices.reserve(count);
	for (size_t i = 0; i < count; ++i)
		indices.push_back(i);

	std::mt19937 rng(42);  // Fixed seed for reproducibility
	std::shuffle(indices.begin(), indices.end(), rng);
	return indices;
}


// ===== Benchmarks =====

// (a) BASELINE: Virtual accessor (no EnTT)
static void BM_VirtualAccessor_Baseline(benchmark::State& state)
{
	const size_t NUM_ENTITIES = 10000;
	std::vector<std::unique_ptr<IAccessor>> accessors;
	for (size_t i = 0; i < NUM_ENTITIES; ++i)
		accessors.push_back(std::make_unique<ConcreteAccessor>(static_cast<float>(i)));

	for (auto _ : state)
	{
		benchmark::ClobberMemory();
		for (size_t i = 0; i < NUM_ENTITIES; ++i)
		{
			float result = accessors[i]->GetX();
			benchmark::DoNotOptimize(result);
		}
	}
}
BENCHMARK(BM_VirtualAccessor_Baseline);

// (b) REGISTRY::GET() per-call lookup (type-hash every access)
static void BM_RegistryGet_CreationOrder_100Density(benchmark::State& state)
{
	const size_t NUM_ENTITIES = 10000;
	entt::registry registry;
	std::vector<entt::entity> entities;

	for (size_t i = 0; i < NUM_ENTITIES; ++i)
	{
		auto e = registry.create();
		registry.emplace<State1>(e, static_cast<float>(i), static_cast<float>(i + 1), static_cast<int>(i));
		entities.push_back(e);
	}

	for (auto _ : state)
	{
		benchmark::ClobberMemory();
		for (const auto& e : entities)
		{
			float result = registry.get<State1>(e).x;
			benchmark::DoNotOptimize(result);
		}
	}
}
BENCHMARK(BM_RegistryGet_CreationOrder_100Density);

static void BM_RegistryGet_ShuffledOrder_100Density(benchmark::State& state)
{
	const size_t NUM_ENTITIES = 10000;
	entt::registry registry;
	std::vector<entt::entity> entities;

	for (size_t i = 0; i < NUM_ENTITIES; ++i)
	{
		auto e = registry.create();
		registry.emplace<State1>(e, static_cast<float>(i), static_cast<float>(i + 1), static_cast<int>(i));
		entities.push_back(e);
	}

	auto shuffled = GenerateShuffledIndices(NUM_ENTITIES);

	for (auto _ : state)
	{
		benchmark::ClobberMemory();
		for (size_t idx : shuffled)
		{
			float result = registry.get<State1>(entities[idx]).x;
			benchmark::DoNotOptimize(result);
		}
	}
}
BENCHMARK(BM_RegistryGet_ShuffledOrder_100Density);

static void BM_RegistryGet_CreationOrder_25Density(benchmark::State& state)
{
	const size_t NUM_ENTITIES = 10000;
	entt::registry registry;
	std::vector<entt::entity> entities;

	for (size_t i = 0; i < NUM_ENTITIES; ++i)
	{
		auto e = registry.create();
		entities.push_back(e);
		if (i % 4 == 0)
			registry.emplace<State1>(e, static_cast<float>(i), static_cast<float>(i + 1), static_cast<int>(i));
	}

	for (auto _ : state)
	{
		benchmark::ClobberMemory();
		for (const auto& e : entities)
		{
			auto* result = registry.try_get<State1>(e);
			if (result)
				benchmark::DoNotOptimize(result->x);
		}
	}
}
BENCHMARK(BM_RegistryGet_CreationOrder_25Density);

static void BM_RegistryGet_ShuffledOrder_25Density(benchmark::State& state)
{
	const size_t NUM_ENTITIES = 10000;
	entt::registry registry;
	std::vector<entt::entity> entities;

	for (size_t i = 0; i < NUM_ENTITIES; ++i)
	{
		auto e = registry.create();
		entities.push_back(e);
		if (i % 4 == 0)
			registry.emplace<State1>(e, static_cast<float>(i), static_cast<float>(i + 1), static_cast<int>(i));
	}

	auto shuffled = GenerateShuffledIndices(NUM_ENTITIES);

	for (auto _ : state)
	{
		benchmark::ClobberMemory();
		for (size_t idx : shuffled)
		{
			auto* result = registry.try_get<State1>(entities[idx]);
			if (result)
				benchmark::DoNotOptimize(result->x);
		}
	}
}
BENCHMARK(BM_RegistryGet_ShuffledOrder_25Density);

// (c) CACHED POOL POINTER + pool->get(e) (EnTTComponent pattern)
static void BM_CachedPool_CreationOrder_100Density(benchmark::State& state)
{
	const size_t NUM_ENTITIES = 10000;
	entt::registry registry;
	std::vector<entt::entity> entities;

	for (size_t i = 0; i < NUM_ENTITIES; ++i)
	{
		auto e = registry.create();
		registry.emplace<State1>(e, static_cast<float>(i), static_cast<float>(i + 1), static_cast<int>(i));
		entities.push_back(e);
	}

	auto* pool = &registry.storage<State1>();

	for (auto _ : state)
	{
		benchmark::ClobberMemory();
		for (const auto& e : entities)
		{
			float result = pool->get(e).x;
			benchmark::DoNotOptimize(result);
		}
	}
}
BENCHMARK(BM_CachedPool_CreationOrder_100Density);

static void BM_CachedPool_ShuffledOrder_100Density(benchmark::State& state)
{
	const size_t NUM_ENTITIES = 10000;
	entt::registry registry;
	std::vector<entt::entity> entities;

	for (size_t i = 0; i < NUM_ENTITIES; ++i)
	{
		auto e = registry.create();
		registry.emplace<State1>(e, static_cast<float>(i), static_cast<float>(i + 1), static_cast<int>(i));
		entities.push_back(e);
	}

	auto* pool = &registry.storage<State1>();
	auto shuffled = GenerateShuffledIndices(NUM_ENTITIES);

	for (auto _ : state)
	{
		benchmark::ClobberMemory();
		for (size_t idx : shuffled)
		{
			float result = pool->get(entities[idx]).x;
			benchmark::DoNotOptimize(result);
		}
	}
}
BENCHMARK(BM_CachedPool_ShuffledOrder_100Density);

static void BM_CachedPool_CreationOrder_25Density(benchmark::State& state)
{
	const size_t NUM_ENTITIES = 10000;
	entt::registry registry;
	std::vector<entt::entity> entities;

	for (size_t i = 0; i < NUM_ENTITIES; ++i)
	{
		auto e = registry.create();
		entities.push_back(e);
		if (i % 4 == 0)
			registry.emplace<State1>(e, static_cast<float>(i), static_cast<float>(i + 1), static_cast<int>(i));
	}

	auto* pool = &registry.storage<State1>();

	for (auto _ : state)
	{
		benchmark::ClobberMemory();
		for (const auto& e : entities)
		{
			if (pool->contains(e))
			{
				float result = pool->get(e).x;
				benchmark::DoNotOptimize(result);
			}
		}
	}
}
BENCHMARK(BM_CachedPool_CreationOrder_25Density);

static void BM_CachedPool_ShuffledOrder_25Density(benchmark::State& state)
{
	const size_t NUM_ENTITIES = 10000;
	entt::registry registry;
	std::vector<entt::entity> entities;

	for (size_t i = 0; i < NUM_ENTITIES; ++i)
	{
		auto e = registry.create();
		entities.push_back(e);
		if (i % 4 == 0)
			registry.emplace<State1>(e, static_cast<float>(i), static_cast<float>(i + 1), static_cast<int>(i));
	}

	auto* pool = &registry.storage<State1>();
	auto shuffled = GenerateShuffledIndices(NUM_ENTITIES);

	for (auto _ : state)
	{
		benchmark::ClobberMemory();
		for (size_t idx : shuffled)
		{
			if (pool->contains(entities[idx]))
			{
				float result = pool->get(entities[idx]).x;
				benchmark::DoNotOptimize(result);
			}
		}
	}
}
BENCHMARK(BM_CachedPool_ShuffledOrder_25Density);

// (d) MULTI-POOL ACCESS (1, 2, 3 pools)
static void BM_MultiPool_1Pool_CreationOrder(benchmark::State& state)
{
	const size_t NUM_ENTITIES = 10000;
	entt::registry registry;
	std::vector<entt::entity> entities;

	for (size_t i = 0; i < NUM_ENTITIES; ++i)
	{
		auto e = registry.create();
		registry.emplace<State1>(e, static_cast<float>(i), static_cast<float>(i + 1), static_cast<int>(i));
		entities.push_back(e);
	}

	auto* pool1 = &registry.storage<State1>();

	for (auto _ : state)
	{
		benchmark::ClobberMemory();
		for (const auto& e : entities)
		{
			float result = pool1->get(e).x;
			benchmark::DoNotOptimize(result);
		}
	}
}
BENCHMARK(BM_MultiPool_1Pool_CreationOrder);

static void BM_MultiPool_1Pool_ShuffledOrder(benchmark::State& state)
{
	const size_t NUM_ENTITIES = 10000;
	entt::registry registry;
	std::vector<entt::entity> entities;

	for (size_t i = 0; i < NUM_ENTITIES; ++i)
	{
		auto e = registry.create();
		registry.emplace<State1>(e, static_cast<float>(i), static_cast<float>(i + 1), static_cast<int>(i));
		entities.push_back(e);
	}

	auto* pool1 = &registry.storage<State1>();
	auto shuffled = GenerateShuffledIndices(NUM_ENTITIES);

	for (auto _ : state)
	{
		benchmark::ClobberMemory();
		for (size_t idx : shuffled)
		{
			float result = pool1->get(entities[idx]).x;
			benchmark::DoNotOptimize(result);
		}
	}
}
BENCHMARK(BM_MultiPool_1Pool_ShuffledOrder);

static void BM_MultiPool_2Pools_CreationOrder(benchmark::State& state)
{
	const size_t NUM_ENTITIES = 10000;
	entt::registry registry;
	std::vector<entt::entity> entities;

	for (size_t i = 0; i < NUM_ENTITIES; ++i)
	{
		auto e = registry.create();
		registry.emplace<State1>(e, static_cast<float>(i), static_cast<float>(i + 1), static_cast<int>(i));
		registry.emplace<State2>(e, static_cast<float>(i), static_cast<float>(i + 1));
		entities.push_back(e);
	}

	auto* pool1 = &registry.storage<State1>();
	auto* pool2 = &registry.storage<State2>();

	for (auto _ : state)
	{
		benchmark::ClobberMemory();
		for (const auto& e : entities)
		{
			float result = pool1->get(e).x + pool2->get(e).vx;
			benchmark::DoNotOptimize(result);
		}
	}
}
BENCHMARK(BM_MultiPool_2Pools_CreationOrder);

static void BM_MultiPool_2Pools_ShuffledOrder(benchmark::State& state)
{
	const size_t NUM_ENTITIES = 10000;
	entt::registry registry;
	std::vector<entt::entity> entities;

	for (size_t i = 0; i < NUM_ENTITIES; ++i)
	{
		auto e = registry.create();
		registry.emplace<State1>(e, static_cast<float>(i), static_cast<float>(i + 1), static_cast<int>(i));
		registry.emplace<State2>(e, static_cast<float>(i), static_cast<float>(i + 1));
		entities.push_back(e);
	}

	auto* pool1 = &registry.storage<State1>();
	auto* pool2 = &registry.storage<State2>();
	auto shuffled = GenerateShuffledIndices(NUM_ENTITIES);

	for (auto _ : state)
	{
		benchmark::ClobberMemory();
		for (size_t idx : shuffled)
		{
			auto e = entities[idx];
			float result = pool1->get(e).x + pool2->get(e).vx;
			benchmark::DoNotOptimize(result);
		}
	}
}
BENCHMARK(BM_MultiPool_2Pools_ShuffledOrder);

static void BM_MultiPool_3Pools_CreationOrder(benchmark::State& state)
{
	const size_t NUM_ENTITIES = 10000;
	entt::registry registry;
	std::vector<entt::entity> entities;

	for (size_t i = 0; i < NUM_ENTITIES; ++i)
	{
		auto e = registry.create();
		registry.emplace<State1>(e, static_cast<float>(i), static_cast<float>(i + 1), static_cast<int>(i));
		registry.emplace<State2>(e, static_cast<float>(i), static_cast<float>(i + 1));
		registry.emplace<State3>(e, static_cast<float>(i), static_cast<float>(i + 1));
		entities.push_back(e);
	}

	auto* pool1 = &registry.storage<State1>();
	auto* pool2 = &registry.storage<State2>();
	auto* pool3 = &registry.storage<State3>();

	for (auto _ : state)
	{
		benchmark::ClobberMemory();
		for (const auto& e : entities)
		{
			float result = pool1->get(e).x + pool2->get(e).vx + pool3->get(e).ax;
			benchmark::DoNotOptimize(result);
		}
	}
}
BENCHMARK(BM_MultiPool_3Pools_CreationOrder);

static void BM_MultiPool_3Pools_ShuffledOrder(benchmark::State& state)
{
	const size_t NUM_ENTITIES = 10000;
	entt::registry registry;
	std::vector<entt::entity> entities;

	for (size_t i = 0; i < NUM_ENTITIES; ++i)
	{
		auto e = registry.create();
		registry.emplace<State1>(e, static_cast<float>(i), static_cast<float>(i + 1), static_cast<int>(i));
		registry.emplace<State2>(e, static_cast<float>(i), static_cast<float>(i + 1));
		registry.emplace<State3>(e, static_cast<float>(i), static_cast<float>(i + 1));
		entities.push_back(e);
	}

	auto* pool1 = &registry.storage<State1>();
	auto* pool2 = &registry.storage<State2>();
	auto* pool3 = &registry.storage<State3>();
	auto shuffled = GenerateShuffledIndices(NUM_ENTITIES);

	for (auto _ : state)
	{
		benchmark::ClobberMemory();
		for (size_t idx : shuffled)
		{
			auto e = entities[idx];
			float result = pool1->get(e).x + pool2->get(e).vx + pool3->get(e).ax;
			benchmark::DoNotOptimize(result);
		}
	}
}
BENCHMARK(BM_MultiPool_3Pools_ShuffledOrder);

// (e) DIRECT ITERATION (view/pool iteration)
static void BM_DirectIteration_100Density(benchmark::State& state)
{
	const size_t NUM_ENTITIES = 10000;
	entt::registry registry;
	for (size_t i = 0; i < NUM_ENTITIES; ++i)
	{
		auto e = registry.create();
		registry.emplace<State1>(e, static_cast<float>(i), static_cast<float>(i + 1), static_cast<int>(i));
	}

	for (auto _ : state)
	{
		benchmark::ClobberMemory();
		auto view = registry.view<State1>();
		for (auto e : view)
		{
			float result = view.get<State1>(e).x;
			benchmark::DoNotOptimize(result);
		}
	}
}
BENCHMARK(BM_DirectIteration_100Density);

static void BM_DirectIteration_25Density(benchmark::State& state)
{
	const size_t NUM_ENTITIES = 10000;
	entt::registry registry;
	for (size_t i = 0; i < NUM_ENTITIES; ++i)
	{
		auto e = registry.create();
		if (i % 4 == 0)
			registry.emplace<State1>(e, static_cast<float>(i), static_cast<float>(i + 1), static_cast<int>(i));
	}

	for (auto _ : state)
	{
		benchmark::ClobberMemory();
		auto view = registry.view<State1>();
		for (auto e : view)
		{
			float result = view.get<State1>(e).x;
			benchmark::DoNotOptimize(result);
		}
	}
}
BENCHMARK(BM_DirectIteration_25Density);

// ===== NEW BENCHMARKS: CCmpPosition-sized structs (128 bytes total) with parameterized N =====
// These complement the small-struct benchmarks to measure cache effects with realistic component sizes

// (d) MULTI-POOL ACCESS with CCmpPosition-sized structs (1, 2, 3 pools)
// N is parameterized: 1k, 10k, 100k entities

// === COMBINED STRUCT BASELINE (single pool with all 128 bytes, correct 1-pool baseline) ===

static void BM_MultiPool_CCmpPositionSized_Combined_CreationOrder(benchmark::State& state)
{
	const size_t NUM_ENTITIES = state.range(0);
	entt::registry registry;
	std::vector<entt::entity> entities;

	for (size_t i = 0; i < NUM_ENTITIES; ++i)
	{
		auto e = registry.create();
		CombinedStateData c;
		// Template-like fields
		c.anchorType = static_cast<int>(i % 4);
		c.floating = (i % 2) == 0;
		c.floatDepth = static_cast<float>(i) * 0.1f;
		c.rotYSpeed = 1.5f;
		c.relativeToGround = (i % 3) == 0;
		c.constructionProgress = static_cast<float>(i % 100) / 100.f;
		// Dynamic-like fields
		c.inWorld = true;
		c.x = static_cast<float>(i);
		c.z = static_cast<float>(i + 1);
		c.lastX = static_cast<float>(i) - 1.f;
		c.lastZ = static_cast<float>(i);
		c.prevX = static_cast<float>(i) - 2.f;
		c.prevZ = static_cast<float>(i) - 1.f;
		c.y = 10.f;
		c.lastYDifference = 0.f;
		c.rotX = 0.f;
		c.rotY = static_cast<float>(i % 360);
		c.rotZ = 0.f;
		// Derived-like fields
		c.turretParent = UINT32_MAX;
		c.turretPosX = 0.f;
		c.turretPosY = 0.f;
		c.turretPosZ = 0.f;
		c.interpRotX = 0.f;
		c.interpRotY = static_cast<float>(i % 360);
		c.interpRotZ = 0.f;
		c.lastInterpRotX = 0.f;
		c.lastInterpRotZ = 0.f;
		c.turretSetSize = 0;
		c.derivedReserved[0] = 0.f;

		registry.emplace<CombinedStateData>(e, c);
		entities.push_back(e);
	}

	auto* pool = &registry.storage<CombinedStateData>();

	for (auto _ : state)
	{
		benchmark::ClobberMemory();
		for (const auto& e : entities)
		{
			float result = pool->get(e).floatDepth + pool->get(e).x + pool->get(e).interpRotY;
			benchmark::DoNotOptimize(result);
		}
	}
}
BENCHMARK(BM_MultiPool_CCmpPositionSized_Combined_CreationOrder)
	->Arg(1000)->Arg(10000)->Arg(100000);

static void BM_MultiPool_CCmpPositionSized_Combined_CreationOrder_25Density(benchmark::State& state)
{
	const size_t NUM_ENTITIES = state.range(0);
	entt::registry registry;
	std::vector<entt::entity> entities;

	for (size_t i = 0; i < NUM_ENTITIES; ++i)
	{
		auto e = registry.create();
		entities.push_back(e);
		if (i % 4 == 0)
		{
			CombinedStateData c;
			// Template-like fields
			c.anchorType = static_cast<int>(i % 4);
			c.floating = (i % 2) == 0;
			c.floatDepth = static_cast<float>(i) * 0.1f;
			c.rotYSpeed = 1.5f;
			c.relativeToGround = (i % 3) == 0;
			c.constructionProgress = static_cast<float>(i % 100) / 100.f;
				c.templateReserved[1] = 0.f;
			c.templatePad1 = 0;
			// Dynamic-like fields
			c.inWorld = true;
			c.x = static_cast<float>(i);
			c.z = static_cast<float>(i + 1);
			c.lastX = static_cast<float>(i) - 1.f;
			c.lastZ = static_cast<float>(i);
			c.prevX = static_cast<float>(i) - 2.f;
			c.prevZ = static_cast<float>(i) - 1.f;
			c.y = 10.f;
			c.lastYDifference = 0.f;
			c.rotX = 0.f;
			c.rotY = static_cast<float>(i % 360);
			c.rotZ = 0.f;
			c.dynamicReserved = 0.f;
			// Derived-like fields
			c.turretParent = UINT32_MAX;
			c.turretPosX = 0.f;
			c.turretPosY = 0.f;
			c.turretPosZ = 0.f;
			c.interpRotX = 0.f;
			c.interpRotY = static_cast<float>(i % 360);
			c.interpRotZ = 0.f;
			c.lastInterpRotX = 0.f;
			c.lastInterpRotZ = 0.f;
			c.turretSetSize = 0;
			c.derivedReserved[0] = 0.f;

			registry.emplace<CombinedStateData>(e, c);
		}
	}

	auto* pool = &registry.storage<CombinedStateData>();

	for (auto _ : state)
	{
		benchmark::ClobberMemory();
		for (const auto& e : entities)
		{
			if (pool->contains(e))
			{
				float result = pool->get(e).floatDepth + pool->get(e).x + pool->get(e).interpRotY;
				benchmark::DoNotOptimize(result);
			}
		}
	}
}
BENCHMARK(BM_MultiPool_CCmpPositionSized_Combined_CreationOrder_25Density)
	->Arg(1000)->Arg(10000)->Arg(100000);

static void BM_MultiPool_CCmpPositionSized_Combined_ShuffledOrder(benchmark::State& state)
{
	const size_t NUM_ENTITIES = state.range(0);
	entt::registry registry;
	std::vector<entt::entity> entities;

	for (size_t i = 0; i < NUM_ENTITIES; ++i)
	{
		auto e = registry.create();
		CombinedStateData c;
		// Template-like fields
		c.anchorType = static_cast<int>(i % 4);
		c.floating = (i % 2) == 0;
		c.floatDepth = static_cast<float>(i) * 0.1f;
		c.rotYSpeed = 1.5f;
		c.relativeToGround = (i % 3) == 0;
		c.constructionProgress = static_cast<float>(i % 100) / 100.f;
		// Dynamic-like fields
		c.inWorld = true;
		c.x = static_cast<float>(i);
		c.z = static_cast<float>(i + 1);
		c.lastX = static_cast<float>(i) - 1.f;
		c.lastZ = static_cast<float>(i);
		c.prevX = static_cast<float>(i) - 2.f;
		c.prevZ = static_cast<float>(i) - 1.f;
		c.y = 10.f;
		c.lastYDifference = 0.f;
		c.rotX = 0.f;
		c.rotY = static_cast<float>(i % 360);
		c.rotZ = 0.f;
		// Derived-like fields
		c.turretParent = UINT32_MAX;
		c.turretPosX = 0.f;
		c.turretPosY = 0.f;
		c.turretPosZ = 0.f;
		c.interpRotX = 0.f;
		c.interpRotY = static_cast<float>(i % 360);
		c.interpRotZ = 0.f;
		c.lastInterpRotX = 0.f;
		c.lastInterpRotZ = 0.f;
		c.turretSetSize = 0;
		c.derivedReserved[0] = 0.f;

		registry.emplace<CombinedStateData>(e, c);
		entities.push_back(e);
	}

	auto* pool = &registry.storage<CombinedStateData>();
	auto shuffled = GenerateShuffledIndices(NUM_ENTITIES);

	for (auto _ : state)
	{
		benchmark::ClobberMemory();
		for (size_t idx : shuffled)
		{
			auto e = entities[idx];
			float result = pool->get(e).floatDepth + pool->get(e).x + pool->get(e).interpRotY;
			benchmark::DoNotOptimize(result);
		}
	}
}
BENCHMARK(BM_MultiPool_CCmpPositionSized_Combined_ShuffledOrder)
	->Arg(1000)->Arg(10000)->Arg(100000);

static void BM_MultiPool_CCmpPositionSized_Combined_ShuffledOrder_25Density(benchmark::State& state)
{
	const size_t NUM_ENTITIES = state.range(0);
	entt::registry registry;
	std::vector<entt::entity> entities;

	for (size_t i = 0; i < NUM_ENTITIES; ++i)
	{
		auto e = registry.create();
		entities.push_back(e);
		if (i % 4 == 0)
		{
			CombinedStateData c;
			// Template-like fields
			c.anchorType = static_cast<int>(i % 4);
			c.floating = (i % 2) == 0;
			c.floatDepth = static_cast<float>(i) * 0.1f;
			c.rotYSpeed = 1.5f;
			c.relativeToGround = (i % 3) == 0;
			c.constructionProgress = static_cast<float>(i % 100) / 100.f;
				c.templateReserved[1] = 0.f;
			c.templatePad1 = 0;
			// Dynamic-like fields
			c.inWorld = true;
			c.x = static_cast<float>(i);
			c.z = static_cast<float>(i + 1);
			c.lastX = static_cast<float>(i) - 1.f;
			c.lastZ = static_cast<float>(i);
			c.prevX = static_cast<float>(i) - 2.f;
			c.prevZ = static_cast<float>(i) - 1.f;
			c.y = 10.f;
			c.lastYDifference = 0.f;
			c.rotX = 0.f;
			c.rotY = static_cast<float>(i % 360);
			c.rotZ = 0.f;
			c.dynamicReserved = 0.f;
			// Derived-like fields
			c.turretParent = UINT32_MAX;
			c.turretPosX = 0.f;
			c.turretPosY = 0.f;
			c.turretPosZ = 0.f;
			c.interpRotX = 0.f;
			c.interpRotY = static_cast<float>(i % 360);
			c.interpRotZ = 0.f;
			c.lastInterpRotX = 0.f;
			c.lastInterpRotZ = 0.f;
			c.turretSetSize = 0;
			c.derivedReserved[0] = 0.f;

			registry.emplace<CombinedStateData>(e, c);
		}
	}

	auto* pool = &registry.storage<CombinedStateData>();
	auto shuffled = GenerateShuffledIndices(NUM_ENTITIES);

	for (auto _ : state)
	{
		benchmark::ClobberMemory();
		for (size_t idx : shuffled)
		{
			auto e = entities[idx];
			if (pool->contains(e))
			{
				float result = pool->get(e).floatDepth + pool->get(e).x + pool->get(e).interpRotY;
				benchmark::DoNotOptimize(result);
			}
		}
	}
}
BENCHMARK(BM_MultiPool_CCmpPositionSized_Combined_ShuffledOrder_25Density)
	->Arg(1000)->Arg(10000)->Arg(100000);

// === OLD SEPARATED POOLS (kept for reference, but baseline comparisons should use Combined) ===

static void BM_MultiPool_CCmpPositionSized_1Pool_CreationOrder(benchmark::State& state)
{
	const size_t NUM_ENTITIES = state.range(0);
	entt::registry registry;
	std::vector<entt::entity> entities;

	for (size_t i = 0; i < NUM_ENTITIES; ++i)
	{
		auto e = registry.create();
		TemplateStateData t;
		t.anchorType = i % 4;
		t.floating = (i % 2) == 0;
		t.floatDepth = static_cast<float>(i) * 0.1f;
		t.rotYSpeed = 1.5f;
		t.relativeToGround = (i % 3) == 0;
		t.constructionProgress = static_cast<float>(i % 100) / 100.f;
		t.reservedPad[0] = 0.f;
		registry.emplace<TemplateStateData>(e, t);
		entities.push_back(e);
	}

	auto* pool1 = &registry.storage<TemplateStateData>();

	for (auto _ : state)
	{
		benchmark::ClobberMemory();
		for (const auto& e : entities)
		{
			float result = pool1->get(e).floatDepth;
			benchmark::DoNotOptimize(result);
		}
	}
}
BENCHMARK(BM_MultiPool_CCmpPositionSized_1Pool_CreationOrder)
	->Arg(1000)->Arg(10000)->Arg(100000);

static void BM_MultiPool_CCmpPositionSized_1Pool_ShuffledOrder(benchmark::State& state)
{
	const size_t NUM_ENTITIES = state.range(0);
	entt::registry registry;
	std::vector<entt::entity> entities;

	for (size_t i = 0; i < NUM_ENTITIES; ++i)
	{
		auto e = registry.create();
		TemplateStateData t;
		t.anchorType = i % 4;
		t.floating = (i % 2) == 0;
		t.floatDepth = static_cast<float>(i) * 0.1f;
		t.rotYSpeed = 1.5f;
		t.relativeToGround = (i % 3) == 0;
		t.constructionProgress = static_cast<float>(i % 100) / 100.f;
		t.reservedPad[0] = 0.f;
		registry.emplace<TemplateStateData>(e, t);
		entities.push_back(e);
	}

	auto* pool1 = &registry.storage<TemplateStateData>();
	auto shuffled = GenerateShuffledIndices(NUM_ENTITIES);

	for (auto _ : state)
	{
		benchmark::ClobberMemory();
		for (size_t idx : shuffled)
		{
			float result = pool1->get(entities[idx]).floatDepth;
			benchmark::DoNotOptimize(result);
		}
	}
}
BENCHMARK(BM_MultiPool_CCmpPositionSized_1Pool_ShuffledOrder)
	->Arg(1000)->Arg(10000)->Arg(100000);

static void BM_MultiPool_CCmpPositionSized_2Pools_CreationOrder(benchmark::State& state)
{
	const size_t NUM_ENTITIES = state.range(0);
	entt::registry registry;
	std::vector<entt::entity> entities;

	for (size_t i = 0; i < NUM_ENTITIES; ++i)
	{
		auto e = registry.create();
		TemplateStateData t;
		t.anchorType = i % 4;
		t.floating = (i % 2) == 0;
		t.floatDepth = static_cast<float>(i) * 0.1f;
		t.rotYSpeed = 1.5f;
		t.relativeToGround = (i % 3) == 0;
		t.constructionProgress = static_cast<float>(i % 100) / 100.f;
		t.reservedPad[0] = 0.f;
		registry.emplace<TemplateStateData>(e, t);

		DynamicStateData d;
		d.inWorld = true;
		d.x = static_cast<float>(i);
		d.z = static_cast<float>(i + 1);
		d.lastX = static_cast<float>(i - 1);
		d.lastZ = static_cast<float>(i);
		d.prevX = static_cast<float>(i - 2);
		d.prevZ = static_cast<float>(i - 1);
		d.y = 10.f;
		d.lastYDifference = 0.f;
		d.rotX = 0.f;
		d.rotY = static_cast<float>(i % 360);
		d.rotZ = 0.f;
		registry.emplace<DynamicStateData>(e, d);

		entities.push_back(e);
	}

	auto* pool1 = &registry.storage<TemplateStateData>();
	auto* pool2 = &registry.storage<DynamicStateData>();

	for (auto _ : state)
	{
		benchmark::ClobberMemory();
		for (const auto& e : entities)
		{
			float result = pool1->get(e).floatDepth + pool2->get(e).x;
			benchmark::DoNotOptimize(result);
		}
	}
}
BENCHMARK(BM_MultiPool_CCmpPositionSized_2Pools_CreationOrder)
	->Arg(1000)->Arg(10000)->Arg(100000);

static void BM_MultiPool_CCmpPositionSized_2Pools_ShuffledOrder(benchmark::State& state)
{
	const size_t NUM_ENTITIES = state.range(0);
	entt::registry registry;
	std::vector<entt::entity> entities;

	for (size_t i = 0; i < NUM_ENTITIES; ++i)
	{
		auto e = registry.create();
		TemplateStateData t;
		t.anchorType = i % 4;
		t.floating = (i % 2) == 0;
		t.floatDepth = static_cast<float>(i) * 0.1f;
		t.rotYSpeed = 1.5f;
		t.relativeToGround = (i % 3) == 0;
		t.constructionProgress = static_cast<float>(i % 100) / 100.f;
		t.reservedPad[0] = 0.f;
		registry.emplace<TemplateStateData>(e, t);

		DynamicStateData d;
		d.inWorld = true;
		d.x = static_cast<float>(i);
		d.z = static_cast<float>(i + 1);
		d.lastX = static_cast<float>(i - 1);
		d.lastZ = static_cast<float>(i);
		d.prevX = static_cast<float>(i - 2);
		d.prevZ = static_cast<float>(i - 1);
		d.y = 10.f;
		d.lastYDifference = 0.f;
		d.rotX = 0.f;
		d.rotY = static_cast<float>(i % 360);
		d.rotZ = 0.f;
		registry.emplace<DynamicStateData>(e, d);

		entities.push_back(e);
	}

	auto* pool1 = &registry.storage<TemplateStateData>();
	auto* pool2 = &registry.storage<DynamicStateData>();
	auto shuffled = GenerateShuffledIndices(NUM_ENTITIES);

	for (auto _ : state)
	{
		benchmark::ClobberMemory();
		for (size_t idx : shuffled)
		{
			auto e = entities[idx];
			float result = pool1->get(e).floatDepth + pool2->get(e).x;
			benchmark::DoNotOptimize(result);
		}
	}
}
BENCHMARK(BM_MultiPool_CCmpPositionSized_2Pools_ShuffledOrder)
	->Arg(1000)->Arg(10000)->Arg(100000);

static void BM_MultiPool_CCmpPositionSized_3Pools_CreationOrder(benchmark::State& state)
{
	const size_t NUM_ENTITIES = state.range(0);
	entt::registry registry;
	std::vector<entt::entity> entities;

	for (size_t i = 0; i < NUM_ENTITIES; ++i)
	{
		auto e = registry.create();
		TemplateStateData t;
		t.anchorType = i % 4;
		t.floating = (i % 2) == 0;
		t.floatDepth = static_cast<float>(i) * 0.1f;
		t.rotYSpeed = 1.5f;
		t.relativeToGround = (i % 3) == 0;
		t.constructionProgress = static_cast<float>(i % 100) / 100.f;
		t.reservedPad[0] = 0.f;
		registry.emplace<TemplateStateData>(e, t);

		DynamicStateData d;
		d.inWorld = true;
		d.x = static_cast<float>(i);
		d.z = static_cast<float>(i + 1);
		d.lastX = static_cast<float>(i - 1);
		d.lastZ = static_cast<float>(i);
		d.prevX = static_cast<float>(i - 2);
		d.prevZ = static_cast<float>(i - 1);
		d.y = 10.f;
		d.lastYDifference = 0.f;
		d.rotX = 0.f;
		d.rotY = static_cast<float>(i % 360);
		d.rotZ = 0.f;
		registry.emplace<DynamicStateData>(e, d);

		DerivedStateData dr;
		dr.turretParent = UINT32_MAX;
		dr.turretPosX = 0.f;
		dr.turretPosY = 0.f;
		dr.turretPosZ = 0.f;
		dr.interpRotX = 0.f;
		dr.interpRotY = static_cast<float>(i % 360);
		dr.interpRotZ = 0.f;
		dr.lastInterpRotX = 0.f;
		dr.lastInterpRotZ = 0.f;
		dr.turretSetSize = 0;
		dr.reserved[0] = 0.f;
		registry.emplace<DerivedStateData>(e, dr);

		entities.push_back(e);
	}

	auto* pool1 = &registry.storage<TemplateStateData>();
	auto* pool2 = &registry.storage<DynamicStateData>();
	auto* pool3 = &registry.storage<DerivedStateData>();

	for (auto _ : state)
	{
		benchmark::ClobberMemory();
		for (const auto& e : entities)
		{
			float result = pool1->get(e).floatDepth + pool2->get(e).x + pool3->get(e).interpRotY;
			benchmark::DoNotOptimize(result);
		}
	}
}
BENCHMARK(BM_MultiPool_CCmpPositionSized_3Pools_CreationOrder)
	->Arg(1000)->Arg(10000)->Arg(100000);

static void BM_MultiPool_CCmpPositionSized_3Pools_ShuffledOrder(benchmark::State& state)
{
	const size_t NUM_ENTITIES = state.range(0);
	entt::registry registry;
	std::vector<entt::entity> entities;

	for (size_t i = 0; i < NUM_ENTITIES; ++i)
	{
		auto e = registry.create();
		TemplateStateData t;
		t.anchorType = i % 4;
		t.floating = (i % 2) == 0;
		t.floatDepth = static_cast<float>(i) * 0.1f;
		t.rotYSpeed = 1.5f;
		t.relativeToGround = (i % 3) == 0;
		t.constructionProgress = static_cast<float>(i % 100) / 100.f;
		t.reservedPad[0] = 0.f;
		registry.emplace<TemplateStateData>(e, t);

		DynamicStateData d;
		d.inWorld = true;
		d.x = static_cast<float>(i);
		d.z = static_cast<float>(i + 1);
		d.lastX = static_cast<float>(i - 1);
		d.lastZ = static_cast<float>(i);
		d.prevX = static_cast<float>(i - 2);
		d.prevZ = static_cast<float>(i - 1);
		d.y = 10.f;
		d.lastYDifference = 0.f;
		d.rotX = 0.f;
		d.rotY = static_cast<float>(i % 360);
		d.rotZ = 0.f;
		registry.emplace<DynamicStateData>(e, d);

		DerivedStateData dr;
		dr.turretParent = UINT32_MAX;
		dr.turretPosX = 0.f;
		dr.turretPosY = 0.f;
		dr.turretPosZ = 0.f;
		dr.interpRotX = 0.f;
		dr.interpRotY = static_cast<float>(i % 360);
		dr.interpRotZ = 0.f;
		dr.lastInterpRotX = 0.f;
		dr.lastInterpRotZ = 0.f;
		dr.turretSetSize = 0;
		dr.reserved[0] = 0.f;
		registry.emplace<DerivedStateData>(e, dr);

		entities.push_back(e);
	}

	auto* pool1 = &registry.storage<TemplateStateData>();
	auto* pool2 = &registry.storage<DynamicStateData>();
	auto* pool3 = &registry.storage<DerivedStateData>();
	auto shuffled = GenerateShuffledIndices(NUM_ENTITIES);

	for (auto _ : state)
	{
		benchmark::ClobberMemory();
		for (size_t idx : shuffled)
		{
			auto e = entities[idx];
			float result = pool1->get(e).floatDepth + pool2->get(e).x + pool3->get(e).interpRotY;
			benchmark::DoNotOptimize(result);
		}
	}
}
BENCHMARK(BM_MultiPool_CCmpPositionSized_3Pools_ShuffledOrder)
	->Arg(1000)->Arg(10000)->Arg(100000);

// === 25% DENSITY VARIANTS FOR 3-POOL CASE ===

static void BM_MultiPool_CCmpPositionSized_3Pools_CreationOrder_25Density(benchmark::State& state)
{
	const size_t NUM_ENTITIES = state.range(0);
	entt::registry registry;
	std::vector<entt::entity> entities;

	for (size_t i = 0; i < NUM_ENTITIES; ++i)
	{
		auto e = registry.create();
		entities.push_back(e);
		if (i % 4 == 0)
		{
			TemplateStateData t;
			t.anchorType = static_cast<int>(i % 4);
			t.floating = (i % 2) == 0;
			t.floatDepth = static_cast<float>(i) * 0.1f;
			t.rotYSpeed = 1.5f;
			t.relativeToGround = (i % 3) == 0;
			t.constructionProgress = static_cast<float>(i % 100) / 100.f;
			t.reservedPad[0] = 0.f;
			registry.emplace<TemplateStateData>(e, t);

			DynamicStateData d;
			d.inWorld = true;
			d.x = static_cast<float>(i);
			d.z = static_cast<float>(i + 1);
			d.lastX = static_cast<float>(i) - 1.f;
			d.lastZ = static_cast<float>(i);
			d.prevX = static_cast<float>(i) - 2.f;
			d.prevZ = static_cast<float>(i) - 1.f;
			d.y = 10.f;
			d.lastYDifference = 0.f;
			d.rotX = 0.f;
			d.rotY = static_cast<float>(i % 360);
			d.rotZ = 0.f;
			registry.emplace<DynamicStateData>(e, d);

			DerivedStateData dr;
			dr.turretParent = UINT32_MAX;
			dr.turretPosX = 0.f;
			dr.turretPosY = 0.f;
			dr.turretPosZ = 0.f;
			dr.interpRotX = 0.f;
			dr.interpRotY = static_cast<float>(i % 360);
			dr.interpRotZ = 0.f;
			dr.lastInterpRotX = 0.f;
			dr.lastInterpRotZ = 0.f;
			dr.turretSetSize = 0;
			dr.reserved[0] = 0.f;
			registry.emplace<DerivedStateData>(e, dr);
		}
	}

	auto* pool1 = &registry.storage<TemplateStateData>();
	auto* pool2 = &registry.storage<DynamicStateData>();
	auto* pool3 = &registry.storage<DerivedStateData>();

	for (auto _ : state)
	{
		benchmark::ClobberMemory();
		for (const auto& e : entities)
		{
			if (pool1->contains(e) && pool2->contains(e) && pool3->contains(e))
			{
				float result = pool1->get(e).floatDepth + pool2->get(e).x + pool3->get(e).interpRotY;
				benchmark::DoNotOptimize(result);
			}
		}
	}
}
BENCHMARK(BM_MultiPool_CCmpPositionSized_3Pools_CreationOrder_25Density)
	->Arg(1000)->Arg(10000)->Arg(100000);

static void BM_MultiPool_CCmpPositionSized_3Pools_ShuffledOrder_25Density(benchmark::State& state)
{
	const size_t NUM_ENTITIES = state.range(0);
	entt::registry registry;
	std::vector<entt::entity> entities;

	for (size_t i = 0; i < NUM_ENTITIES; ++i)
	{
		auto e = registry.create();
		entities.push_back(e);
		if (i % 4 == 0)
		{
			TemplateStateData t;
			t.anchorType = static_cast<int>(i % 4);
			t.floating = (i % 2) == 0;
			t.floatDepth = static_cast<float>(i) * 0.1f;
			t.rotYSpeed = 1.5f;
			t.relativeToGround = (i % 3) == 0;
			t.constructionProgress = static_cast<float>(i % 100) / 100.f;
			t.reservedPad[0] = 0.f;
			registry.emplace<TemplateStateData>(e, t);

			DynamicStateData d;
			d.inWorld = true;
			d.x = static_cast<float>(i);
			d.z = static_cast<float>(i + 1);
			d.lastX = static_cast<float>(i) - 1.f;
			d.lastZ = static_cast<float>(i);
			d.prevX = static_cast<float>(i) - 2.f;
			d.prevZ = static_cast<float>(i) - 1.f;
			d.y = 10.f;
			d.lastYDifference = 0.f;
			d.rotX = 0.f;
			d.rotY = static_cast<float>(i % 360);
			d.rotZ = 0.f;
			registry.emplace<DynamicStateData>(e, d);

			DerivedStateData dr;
			dr.turretParent = UINT32_MAX;
			dr.turretPosX = 0.f;
			dr.turretPosY = 0.f;
			dr.turretPosZ = 0.f;
			dr.interpRotX = 0.f;
			dr.interpRotY = static_cast<float>(i % 360);
			dr.interpRotZ = 0.f;
			dr.lastInterpRotX = 0.f;
			dr.lastInterpRotZ = 0.f;
			dr.turretSetSize = 0;
			dr.reserved[0] = 0.f;
			registry.emplace<DerivedStateData>(e, dr);
		}
	}

	auto* pool1 = &registry.storage<TemplateStateData>();
	auto* pool2 = &registry.storage<DynamicStateData>();
	auto* pool3 = &registry.storage<DerivedStateData>();
	auto shuffled = GenerateShuffledIndices(NUM_ENTITIES);

	for (auto _ : state)
	{
		benchmark::ClobberMemory();
		for (size_t idx : shuffled)
		{
			auto e = entities[idx];
			if (pool1->contains(e) && pool2->contains(e) && pool3->contains(e))
			{
				float result = pool1->get(e).floatDepth + pool2->get(e).x + pool3->get(e).interpRotY;
				benchmark::DoNotOptimize(result);
			}
		}
	}
}
BENCHMARK(BM_MultiPool_CCmpPositionSized_3Pools_ShuffledOrder_25Density)
	->Arg(1000)->Arg(10000)->Arg(100000);
