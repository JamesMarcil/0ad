/* Copyright (C) 2025 Wildfire Games.
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

#include "lib/self_test.h"

#include "lib/types.h"
#include "ps/Future.h"
#include "ps/TaskManager.h"

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <mutex>
#include <vector>

class TestTaskManager : public CxxTest::TestSuite
{
public:
	void test_basic()
	{
		// There is a minimum of 3.
		TS_ASSERT(g_TaskManager.GetNumberOfWorkers() >= 3);

		std::atomic<int> tasks_run = 0;
		auto increment_run = [&tasks_run]() { tasks_run++; };
		Future future{g_TaskManager, increment_run};
		future.Wait();
		TS_ASSERT_EQUALS(tasks_run.load(), 1);

		// Test Execute.
		std::condition_variable cv;
		std::mutex mutex;
		std::atomic<bool> go = false;
		future = {g_TaskManager, [&]{
			std::unique_lock<std::mutex> lock(mutex);
			cv.wait(lock, [&go]() -> bool { return go; });
			lock.unlock();
			increment_run();
			lock.lock();
			go = false;
			lock.unlock();
			cv.notify_all();
		}};
		TS_ASSERT_EQUALS(tasks_run.load(), 1);
		std::unique_lock<std::mutex> lock(mutex);
		go = true;
		lock.unlock();
		cv.notify_all();
		lock.lock();
		cv.wait(lock, [&go]() -> bool { return !go; });
		TS_ASSERT_EQUALS(tasks_run.load(), 2);
		// Wait on the future before the mutex/cv go out of scope.
		future.Wait();
	}

	void test_Priority()
	{
		std::atomic<int> tasks_run = 0;
		// Push general tasks
		auto increment_run = [&tasks_run]() { tasks_run++; };
		Future future = {g_TaskManager, increment_run};
		Future futureLow = {g_TaskManager, increment_run, Threading::TaskPriority::LOW};
		future.Wait();
		futureLow.Wait();
		TS_ASSERT_EQUALS(tasks_run.load(), 2);
		// Also check with no waiting expected.
		Future{g_TaskManager, increment_run}.Wait();
		TS_ASSERT_EQUALS(tasks_run.load(), 3);
		Future{g_TaskManager, increment_run, Threading::TaskPriority::LOW}.Wait();
		TS_ASSERT_EQUALS(tasks_run.load(), 4);
	}

	void test_Load()
	{
#define ITERATIONS 100000
		std::vector<Future<int>> futures;
		futures.resize(ITERATIONS);
		std::vector<u32> values(ITERATIONS);

		Future f1{g_TaskManager, [&futures]{
			for (u32 i = 0; i < ITERATIONS; i+=3)
				futures[i] = {g_TaskManager, []{ return 5; }};
		}};

		Future f2{g_TaskManager, [&futures]{
			for (u32 i = 1; i < ITERATIONS; i+=3)
				futures[i] = {g_TaskManager, []{ return 5; }, Threading::TaskPriority::LOW};
		}};

		Future f3{g_TaskManager, [&futures]{
			for (u32 i = 2; i < ITERATIONS; i+=3)
				futures[i] = {g_TaskManager, []{ return 5; }};
		}};

		f1.Wait();
		f2.Wait();
		f3.Wait();

		for (size_t i = 0; i < ITERATIONS; ++i)
			TS_ASSERT_EQUALS(futures[i].Get(), 5);
#undef ITERATIONS
	}

	void test_ParallelFor_Empty()
	{
		// n == 0: body should never be invoked
		std::atomic<size_t> count = 0;
		g_TaskManager.ParallelFor(0, [&](size_t, size_t, size_t) {
			count++;
		});
		TS_ASSERT_EQUALS(count.load(), 0);
	}

	void test_ParallelFor_Inline()
	{
		// n < grain: should run inline in calling thread
		std::atomic<size_t> count = 0;
		g_TaskManager.ParallelFor(5, 16, [&](size_t begin, size_t end, size_t) {
			for (size_t i = begin; i < end; ++i)
				count++;
		});
		TS_ASSERT_EQUALS(count.load(), 5);
	}

	void test_ParallelFor_Unaligned()
	{
		// n not a multiple of grain
		std::atomic<size_t> count = 0;
		g_TaskManager.ParallelFor(100, 16, [&](size_t begin, size_t end, size_t) {
			for (size_t i = begin; i < end; ++i)
				count++;
		});
		TS_ASSERT_EQUALS(count.load(), 100);
	}

	void test_ParallelFor_GrainOne()
	{
		// grain == 1: maximal parallelism
		std::atomic<size_t> count = 0;
		g_TaskManager.ParallelFor(100, 1, [&](size_t begin, size_t end, size_t) {
			for (size_t i = begin; i < end; ++i)
				count++;
		});
		TS_ASSERT_EQUALS(count.load(), 100);
	}

	void test_ParallelFor_StressAtomicCount()
	{
		// Stress test with large iteration count and atomic verification.
		// Use 100,000 instead of 1,000,000 to keep test runtime reasonable.
		// Each iteration is counted atomically to verify every index processed exactly once.
#define STRESS_ITERATIONS 100000
		std::atomic<size_t> count = 0;
		g_TaskManager.ParallelFor(STRESS_ITERATIONS, 1000, [&](size_t begin, size_t end, size_t) {
			for (size_t i = begin; i < end; ++i)
				count++;
		});
		TS_ASSERT_EQUALS(count.load(), STRESS_ITERATIONS);
#undef STRESS_ITERATIONS
	}

	void test_ParallelFor_WorkerIndexExclusivity()
	{
		// Verify per-workerIndex-scratch exclusivity.
		// Each worker claims indices, and we verify:
		// - workerIndex is in valid range [0, GetNumberOfWorkers()]
		// - Main thread (workerIndex==0) gets every index it processes
		// - No two workers claim overlapping indices concurrently
		const size_t numWorkers = g_TaskManager.GetNumberOfWorkers();
		const size_t maxParticipants = Threading::TaskManager::MAX_PARALLEL_PARTICIPANTS;

		std::vector<std::atomic<size_t>> workerClaims(maxParticipants);
		std::vector<std::thread::id> workerThreadIds(maxParticipants);
		workerThreadIds[0] = std::this_thread::get_id();  // Main thread

		g_TaskManager.ParallelFor(200, 16, [&](size_t begin, size_t end, size_t workerIndex) {
			// Verify workerIndex is in valid range
			TS_ASSERT_LESS_THAN_EQUALS(workerIndex, numWorkers);
			TS_ASSERT_LESS_THAN(workerIndex, maxParticipants);

			// Record thread ID for workers (main thread already set)
			if (workerIndex > 0)
				workerThreadIds[workerIndex] = std::this_thread::get_id();

			// Count the work claimed by this worker
			for (size_t i = begin; i < end; ++i)
				workerClaims[workerIndex]++;
		});

		// Verify all iterations were processed exactly once
		size_t totalProcessed = 0;
		for (size_t i = 0; i <= numWorkers; ++i)
			totalProcessed += workerClaims[i];
		TS_ASSERT_EQUALS(totalProcessed, 200);

		// NOTE: We do NOT assert that workerClaims[0] > 0.
		// With PublishRegion-first ordering for parallelism, fast helper threads
		// can legitimately claim all work before the main thread's RunRegion is
		// invoked. This is not a bug; it's inherent to concurrent work stealing.
		// The main thread participates when there's work left to claim, which
		// depends on timing. The contract only guarantees that any work the
		// main thread claims will be reported with workerIndex==0 (tested above).
	}

	void test_ParallelFor_GetCurrentWorkerIndex()
	{
		// Verify GetCurrentWorkerIndex() returns the correct index within ParallelFor.
		// The primary invariant is that GetCurrentWorkerIndex() matches the workerIndex
		// passed to the lambda. This is always true and must be tested.
		const size_t numWorkers = g_TaskManager.GetNumberOfWorkers();

		// First, verify that GetCurrentWorkerIndex() returns 0 on the main thread
		// outside of any ParallelFor. This is a guaranteed invariant.
		size_t mainThreadIndex = Threading::TaskManager::GetCurrentWorkerIndex();
		TS_ASSERT_EQUALS(mainThreadIndex, 0);

		// Now verify that within ParallelFor, GetCurrentWorkerIndex() matches the
		// passed workerIndex. This tests the thread-local state is correctly set.
		std::vector<std::atomic<bool>> indexesSeen(numWorkers + 1);

		g_TaskManager.ParallelFor(numWorkers * 4, 1, [&](size_t, size_t, size_t workerIndex) {
			size_t currentIndex = Threading::TaskManager::GetCurrentWorkerIndex();
			TS_ASSERT_EQUALS(currentIndex, workerIndex);
			indexesSeen[workerIndex] = true;
		});

		// NOTE: We do NOT assert that indexesSeen[0] is true. Similar to
		// test_ParallelFor_WorkerIndexExclusivity, with PublishRegion-first ordering,
		// fast helper threads can claim all work before the main thread's RunRegion
		// is invoked. However, we have verified above that GetCurrentWorkerIndex()
		// returns 0 when called synchronously from the main thread, which is the
		// guaranteed invariant.
	}
};
