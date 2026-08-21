/* Copyright (C) 2026 Wildfire Games.
 * This file is part of 0 A.D.
 *
 * 0 A.D. is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 */

#include <benchmark/benchmark.h>
#include <cmath>
#include <cstdint>
#include <numeric>
#include <string>
#include <vector>

#include "maths/FixedVector3D.h"
#include "ps/CStr.h"

// 1. Synthetic CPU Arithmetic & Anti-Optimization Validation
static void BM_Synthetic_MathComputation(benchmark::State& state)
{
	int64_t items = 0;
	for (auto _ : state)
	{
		double x = 1.0;
		for (int i = 0; i < 100; ++i)
		{
			x = std::sin(x) + std::cos(x) * 0.5;
			benchmark::DoNotOptimize(x);
		}
		items += 100;
	}
	state.SetItemsProcessed(items);
}
BENCHMARK(BM_Synthetic_MathComputation);

// 2. Synthetic Container & Memory Bandwidth with Range Parameterization
static void BM_Synthetic_VectorAccumulation(benchmark::State& state)
{
	const size_t size = static_cast<size_t>(state.range(0));
	std::vector<int64_t> data(size);
	std::iota(data.begin(), data.end(), 1);

	for (auto _ : state)
	{
		int64_t sum = 0;
		for (size_t i = 0; i < size; ++i)
		{
			sum += data[i];
		}
		benchmark::DoNotOptimize(sum);
		benchmark::ClobberMemory();
	}

	state.SetBytesProcessed(int64_t(state.iterations()) * int64_t(size * sizeof(int64_t)));
	state.SetItemsProcessed(int64_t(state.iterations()) * int64_t(size));
}
BENCHMARK(BM_Synthetic_VectorAccumulation)->RangeMultiplier(4)->Range(64, 65536);

// 3. Synthetic Multi-Threaded Contention & Scaling
static void BM_Synthetic_MultiThreadedScaling(benchmark::State& state)
{
	for (auto _ : state)
	{
		uint64_t accumulator = 0;
		for (int i = 0; i < 1000; ++i)
		{
			accumulator += (static_cast<uint64_t>(i) * 31ULL) ^ (static_cast<uint64_t>(state.thread_index()) + 1ULL);
			benchmark::DoNotOptimize(accumulator);
		}
	}
}
BENCHMARK(BM_Synthetic_MultiThreadedScaling)->ThreadRange(1, 4);

// 4. Synthetic Engine Type Interoperability (CStr & CFixedVector3D)
static void BM_Synthetic_EngineTypesInteroperability(benchmark::State& state)
{
	const int count = static_cast<int>(state.range(0));
	CFixedVector3D origin(fixed::FromInt(0), fixed::FromInt(0), fixed::FromInt(0));
	CFixedVector3D step(fixed::FromInt(1), fixed::FromInt(2), fixed::FromInt(3));

	for (auto _ : state)
	{
		CFixedVector3D current = origin;
		for (int i = 0; i < count; ++i)
		{
			current += step;
			CStr formatted = current.X.ToString();
			benchmark::DoNotOptimize(formatted);
			benchmark::DoNotOptimize(current);
		}
	}

	state.SetItemsProcessed(int64_t(state.iterations()) * int64_t(count));
}
BENCHMARK(BM_Synthetic_EngineTypesInteroperability)->Range(10, 1000);
