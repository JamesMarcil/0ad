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
 * Placeholder / smoke-test benchmark demonstrating how to wire up a
 * Google Benchmark case in this project.
 *
 * Any *.cpp file placed in a "benchmarks" subdirectory anywhere under
 * source/ is automatically picked up by the "benchmarks" project (see
 * setup_benchmarks() in build/premake/premake5.lua) and linked together
 * with the engine's static libraries and Google Benchmark into a
 * standalone "benchmarks" executable.
 *
 * Run `benchmarks --benchmark_list_tests` to list available benchmarks,
 * or just run the executable to run them all.
 */

#include <benchmark/benchmark.h>

#include <string>

static void BM_StringConcatenation(benchmark::State& state)
{
	for (auto _ : state)
	{
		std::string result;
		for (int i = 0; i < 8; ++i)
			result += "0ad";
		benchmark::DoNotOptimize(result);
	}
}
BENCHMARK(BM_StringConcatenation);

BENCHMARK_MAIN();
