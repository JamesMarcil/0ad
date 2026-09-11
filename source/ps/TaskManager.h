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

#ifndef INCLUDED_THREADING_TASKMANAGER
#define INCLUDED_THREADING_TASKMANAGER

#include "ps/Singleton.h"

#include <cstddef>
#include <functional>
#include <memory>
#include <type_traits>
#include <utility>
#include <vector>

namespace Threading
{
enum class TaskPriority
{
	NORMAL,
	LOW
};

class TaskManager;

/**
 * Helper class to batch multiple tasks before pushing them all at once.
 * This reduces lock contention and wakes compared to pushing tasks individually.
 */
class TaskBatch
{
public:
	explicit TaskBatch(TaskManager& taskManager, size_t reserve = 0);
	~TaskBatch();
	TaskBatch(const TaskBatch&) = delete;
	TaskBatch& operator=(const TaskBatch&) = delete;
	TaskBatch(TaskBatch&&) = delete;
	TaskBatch& operator=(TaskBatch&&) = delete;

	template<typename F>
	void PushTask(F&& f, TaskPriority priority = TaskPriority::NORMAL)
	{
		(priority == TaskPriority::NORMAL ? m_Normal : m_Low).emplace_back(std::forward<F>(f));
	}

	void Flush();

private:
	TaskManager& m_TaskManager;
	std::vector<std::function<void()>> m_Normal;
	std::vector<std::function<void()>> m_Low;
};

/**
 * The task manager creates all worker threads on initialisation,
 * and manages the task queues.
 * See implementation for additional comments.
 */
class TaskManager : public Singleton<TaskManager>
{
	friend class WorkerThread;
	friend class TaskBatch;
public:
	TaskManager();
	~TaskManager();
	TaskManager(const TaskManager&) = delete;
	TaskManager(TaskManager&&) = delete;
	TaskManager& operator=(const TaskManager&) = delete;
	TaskManager& operator=(TaskManager&&) = delete;

	/**
	 * Set an override for the number of workers. This must be called before the
	 * TaskManager is instantiated. Used for testing with specific worker thread counts.
	 * @param count the number of workers to use (1 to MAX_WORKERS); bypasses MIN_WORKERS floor
	 */
	static void SetWorkerCountOverride(size_t count);

	/**
	 * @return the number of threaded workers.
	 */
	size_t GetNumberOfWorkers() const;

	/**
	 * Push a task to be executed.
	 */
	void PushTask(std::function<void()> func, TaskPriority priority = TaskPriority::NORMAL);

	/**
	 * Push multiple tasks to be executed at once.
	 * More efficient than calling PushTask multiple times.
	 */
	void PushTasks(std::vector<std::function<void()>> tasks, TaskPriority priority = TaskPriority::NORMAL);

	/**
	 * Maximum number of parallel participants in a ParallelFor region.
	 * Equals MAX_WORKERS + 1 (main thread + worker threads).
	 */
	static constexpr size_t MAX_PARALLEL_PARTICIPANTS = 33;

	/**
	 * Default grain size for ParallelFor if not specified.
	 */
	static constexpr size_t DefaultGrain() { return 16; }

	/**
	 * Execute a parallel loop with optional explicit grain size.
	 *
	 * The body function is invoked concurrently by the main thread and worker threads,
	 * each processing a disjoint range of [begin, end) indices. The loop partitions
	 * the range [0, n) into chunks of size grainSize (with the last chunk possibly smaller).
	 *
	 * Contract:
	 * - Each index in [0, n) is processed exactly once by exactly one thread.
	 * - Ranges are disjoint; no synchronization needed within the body.
	 * - Results must be order-independent (determinism is caller's responsibility).
	 * - No nesting of ParallelFor is allowed.
	 * - May only be called from the thread that owns the TaskManager (typically main thread).
	 * - workerIndex is stable and can be used as an index into per-worker scratch arrays.
	 *   workerIndex == 0 is always the calling thread; workerIndex in [1, GetNumberOfWorkers()]
	 *   are the worker threads. This matches the convention in CCmpPathfinder::m_VertexPathfinders.
	 * - Safe to call inside a component's methods on the main thread.
	 *
	 * @param n Number of iterations (0 to process none).
	 * @param grainSize Minimum size of each work chunk; smaller is more fine-grained parallelism
	 *                  but more overhead. If <= 0, treated as 1.
	 * @param body Callable that takes (size_t begin, size_t end, size_t workerIndex).
	 *             Must not perform blocking I/O or acquire locks held by the main thread.
	 */
	void ParallelFor(size_t n, size_t grainSize,
	                  const std::function<void(size_t begin, size_t end, size_t workerIndex)>& body);

	/**
	 * Execute a parallel loop with default grain size.
	 * Equivalent to ParallelFor(n, DefaultGrain(), body).
	 */
	void ParallelFor(size_t n,
	                  const std::function<void(size_t begin, size_t end, size_t workerIndex)>& body);

	/**
	 * Get the index of the current worker, for use inside a ParallelFor body.
	 *
	 * Returns 0 for the calling (main) thread, and [1, GetNumberOfWorkers()] for worker threads.
	 * Returns 0 for any other thread not participating in the ParallelFor.
	 * Safe to use as an index into per-worker scratch arrays of size GetNumberOfWorkers() + 1.
	 * Only meaningful and safe within a ParallelFor body.
	 */
	static size_t GetCurrentWorkerIndex();

private:
	TaskManager(size_t numberOfWorkers);

	class Impl;
	const std::unique_ptr<Impl> m;
};
} // namespace Threading

#define g_TaskManager Threading::TaskManager::GetSingleton()

#endif // INCLUDED_THREADING_TASKMANAGER
