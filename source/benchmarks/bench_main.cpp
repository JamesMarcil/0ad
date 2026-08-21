/* Copyright (C) 2026 Wildfire Games.
 * This file is part of 0 A.D.
 *
 * 0 A.D. is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 */

#include "lib/self_test.h"
#include <benchmark/benchmark.h>
#include <iostream>

int main(int argc, char** argv)
{
	std::cout << "====================================================" << std::endl;
	std::cout << "  0 A.D. (Pyrogenesis) Google Benchmark Suite       " << std::endl;
	std::cout << "  Google Benchmark Version: 1.9.5                   " << std::endl;
	std::cout << "====================================================" << std::endl;

	benchmark::Initialize(&argc, argv);
	if (benchmark::ReportUnrecognizedArguments(argc, argv))
		return 1;

	benchmark::RunSpecifiedBenchmarks();
	benchmark::Shutdown();

	return 0;
}
