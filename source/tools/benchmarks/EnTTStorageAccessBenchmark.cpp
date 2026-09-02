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
