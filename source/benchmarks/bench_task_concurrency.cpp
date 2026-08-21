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
#include <deque>
#include <functional>
#include <mutex>
#include <atomic>
#include <memory>

#include "bench_fixtures.h"

namespace
{

using namespace BenchmarkFixtures;

// 1. Multi-Threaded Task Queue Enqueue/Dequeue Contention Benchmark
// Replicates the centralized mutex-protected task queue in Threading::TaskManager
class BenchTaskQueue
{
public:
	void Push(std::function<void()> task)
	{
		std::lock_guard<std::mutex> lock(m_Mutex);
		m_Queue.push_back(std::move(task));
	}

	bool Pop(std::function<void()>& task)
	{
		std::lock_guard<std::mutex> lock(m_Mutex);
		if (m_Queue.empty())
			return false;
		task = std::move(m_Queue.front());
		m_Queue.pop_front();
		return true;
	}

private:
	std::mutex m_Mutex;
	std::deque<std::function<void()>> m_Queue;
};

static void BM_TaskManager_QueueContention(benchmark::State& state)
{
	static BenchTaskQueue s_Queue;
	const int itemsPerThread = 1000;

	for (auto _ : state)
	{
		if (state.thread_index() % 2 == 0)
		{
			// Producer
			for (int i = 0; i < itemsPerThread; ++i)
			{
				s_Queue.Push([]() {
					uint64_t dummy = 42;
					benchmark::DoNotOptimize(dummy);
				});
			}
		}
		else
		{
			// Consumer
			int consumed = 0;
			std::function<void()> task;
			while (consumed < itemsPerThread)
			{
				if (s_Queue.Pop(task))
				{
					task();
					consumed++;
				}
			}
		}
	}

	state.SetItemsProcessed(int64_t(state.iterations()) * itemsPerThread);
}
BENCHMARK(BM_TaskManager_QueueContention)->ThreadRange(2, 8);

// 2. Parallel Atomic Work-Stealing Loop Benchmark
static void BM_TaskManager_AtomicJobStealing(benchmark::State& state)
{
	const size_t totalJobs = 10000;
	static std::atomic<size_t> s_JobIndex{0};

	if (state.thread_index() == 0)
	{
		s_JobIndex.store(0, std::memory_order_relaxed);
	}

	for (auto _ : state)
	{
		uint64_t jobsCompleted = 0;
		while (true)
		{
			size_t job = s_JobIndex.fetch_add(1, std::memory_order_relaxed);
			if (job >= totalJobs)
				break;

			// Synthetic micro-task (~20 ns)
			uint64_t x = job * 31ULL;
			benchmark::DoNotOptimize(x);
			jobsCompleted++;
		}
		benchmark::DoNotOptimize(jobsCompleted);
	}

	if (state.thread_index() == 0)
	{
		s_JobIndex.store(0, std::memory_order_relaxed);
	}
}
BENCHMARK(BM_TaskManager_AtomicJobStealing)->ThreadRange(1, 8);

} // anonymous namespace
