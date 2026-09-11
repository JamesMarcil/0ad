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

#include "precompiled.h"

#include "TaskManager.h"

#include "lib/debug.h"
#include "maths/MathUtil.h"
#include "ps/Profiler2.h"
#include "ps/Threading.h"

#include <tracy/Tracy.hpp>

#include <atomic>
#include <condition_variable>
#include <deque>
#include <functional>
#include <limits>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

namespace Threading
{

namespace
{
/**
 * Minimum number of TaskManager workers.
 */
constexpr size_t MIN_WORKERS = 3;

/**
 * Maximum number of TaskManager workers.
 */
constexpr size_t MAX_WORKERS = 32;

static_assert(MAX_WORKERS + 1 == TaskManager::MAX_PARALLEL_PARTICIPANTS);

/**
 * Override for worker count, used for testing specific parallel widths.
 * If set (not -1), this value will be used instead of computing from hardware_concurrency.
 */
static size_t g_WorkerCountOverride = std::numeric_limits<size_t>::max();

/**
 * Force ParallelFor to run fully serially for determinism testing.
 * When enabled, ParallelFor executes the entire range inline on the calling thread.
 * Plain non-atomic bool with no synchronization — safe only when toggled from single-threaded test-setup context, not concurrent with in-flight ParallelFor.
 */
static bool g_ForceSerialForTesting = false;

size_t GetDefaultNumberOfWorkers()
{
	// If an override has been set, use it (allowing 1 worker for determinism testing)
	if (g_WorkerCountOverride != std::numeric_limits<size_t>::max())
	{
		return Clamp<size_t>(g_WorkerCountOverride, 1, MAX_WORKERS);
	}

	const size_t hardware_concurrency = std::thread::hardware_concurrency();
	return hardware_concurrency ? Clamp(hardware_concurrency - 1, MIN_WORKERS, MAX_WORKERS) : MIN_WORKERS;
}

} // anonymous namespace

/**
 * Thread-local worker index for current thread.
 * 0 for non-worker threads, 1..MAX_WORKERS for worker threads, during ParallelFor.
 */
thread_local size_t t_WorkerIndex = 0;

#ifndef NDEBUG
/**
 * Debug guard to detect nested ParallelFor calls.
 */
thread_local bool t_InParallelRegion = false;
#endif

/**
 * Trivially-copyable task descriptor: function pointer and context.
 */
struct TaskRef
{
	void (*fn)(void*);
	void* ctx;
};

/**
 * Parallel region descriptor for ParallelFor.
 * Laid out across multiple cache lines to avoid false sharing of hot atomics.
 */
struct alignas(64) ParallelRegion
{
	// cache line 0: hot, contended
	std::atomic<size_t> m_Cursor;
	char m_Pad0[64 - sizeof(std::atomic<size_t>)];

	// cache line 1: hot, contended (separate line from cursor)
	std::atomic<size_t> m_Remaining;
	char m_Pad1[64 - sizeof(std::atomic<size_t>)];

	// cache line 2+: read-only after publish
	const std::function<void(size_t, size_t, size_t)>* m_Body;
	size_t m_Count;
	size_t m_Grain;
	char m_Pad2[64 - (sizeof(void*) + 2 * sizeof(size_t))];
};

/**
 * Helper invoked by workers to call and delete a heap-allocated std::function.
 * Used to adapt std::function<void()> into the TaskRef POD queue.
 */
inline void InvokeAndDeleteStdFunction(void* ctx)
{
	auto* func = static_cast<std::function<void()>*>(ctx);
	(*func)();
	delete func;
}

/**
 * Trampoline for ParallelFor worker threads and main thread.
 * Processes work from the ParallelRegion until completion.
 */
void RunRegion(void* ctx)
{
	ParallelRegion& r = *static_cast<ParallelRegion*>(ctx);
	const size_t workerIndex = t_WorkerIndex;
	for (;;)
	{
		const size_t begin = r.m_Cursor.fetch_add(r.m_Grain, std::memory_order_relaxed);
		if (begin >= r.m_Count)
			break;
		(*r.m_Body)(begin, std::min(begin + r.m_Grain, r.m_Count), workerIndex);
	}
	r.m_Remaining.fetch_sub(1, std::memory_order_release);
}

/**
 * Light wrapper around std::thread. Ensures Join has been called.
 */
class Thread
{
public:
	Thread() = default;
	Thread(const Thread&) = delete;
	Thread(Thread&&) = delete;

	template<typename T, void(T::* callable)()>
	void Start(T* object)
	{
		m_Thread = std::thread(HandleExceptions<DoStart<T, callable>>::Wrapper, object);
	}
	template<typename T, void(T::* callable)()>
	static void DoStart(T* object);

protected:
	~Thread()
	{
		ENSURE(!m_Thread.joinable());
	}

	std::thread m_Thread;
	std::atomic<bool> m_Kill = false;
};

/**
 * Worker thread: process the taskManager queues until killed.
 */
class WorkerThread : public Thread
{
public:
	WorkerThread(TaskManager::Impl& taskManager, size_t workerIndex);
	~WorkerThread();

protected:
	void RunUntilDeath();

	TaskManager::Impl& m_TaskManager;
	const size_t m_WorkerIndex;
};

/**
 * PImpl-ed implementation of the Task manager.
 *
 * The normal priority queue is processed first, the low priority only if there are no higher-priority tasks
 */
class TaskManager::Impl
{
	friend class TaskManager;
	friend class WorkerThread;
	friend class TaskBatch;
public:
	Impl() = default;
	~Impl()
	{
		std::lock_guard<std::mutex> lock(m_Mutex);
		ENSURE(m_GlobalQueue.empty());
		ENSURE(m_GlobalLowPriorityQueue.empty());
	}

	/**
	 * 2-phase init to avoid having to think too hard about the order of class members.
	 */
	void SetupWorkers(size_t numberOfWorkers);

	/**
	 * Push a task on the global queue.
	 * Takes ownership of @a task.
	 * May be called from any thread.
	 */
	void PushTask(std::function<void()>&& task, TaskPriority priority);

	/**
	 * Push multiple tasks on the global queue at once.
	 * Takes ownership of tasks.
	 * May be called from any thread.
	 */
	void PushTasks(std::vector<std::function<void()>>&& tasks, TaskPriority priority);

	/**
	 * Publish a ParallelRegion to workers.
	 * Enqueues RunRegion tasks for 'count' workers, sets m_HasWork, and notifies all workers.
	 * Called by ParallelFor to distribute the region to idle workers.
	 */
	void PublishRegion(ParallelRegion& region, size_t count);

protected:
	void ClearQueue();

	template<TaskPriority Priority>
	bool PopTask(TaskRef& taskOut);

	std::atomic<bool> m_HasWork = false;
	std::atomic<bool> m_HasLowPriorityWork = false;
	std::mutex m_Mutex;
	std::condition_variable m_ConditionVariable;
	std::deque<TaskRef> m_GlobalQueue;
	std::deque<TaskRef> m_GlobalLowPriorityQueue;

	// Ideally this would be a vector, since it does get iterated, but that requires movable types.
	std::deque<WorkerThread> m_Workers;
};

TaskManager::TaskManager() : TaskManager(GetDefaultNumberOfWorkers())
{
}

TaskManager::TaskManager(size_t numberOfWorkers)
	: m{std::make_unique<Impl>()}
{
	// If an override is set, only clamp to MAX_WORKERS (allowing 1 for testing).
	// Otherwise, enforce the full [MIN_WORKERS, MAX_WORKERS] range.
	if (g_WorkerCountOverride != std::numeric_limits<size_t>::max())
	{
		numberOfWorkers = Clamp<size_t>(numberOfWorkers, 1, MAX_WORKERS);
	}
	else
	{
		numberOfWorkers = Clamp<size_t>(numberOfWorkers, MIN_WORKERS, MAX_WORKERS);
	}
	m->SetupWorkers(numberOfWorkers);
}

TaskManager::~TaskManager() = default;

void TaskManager::SetWorkerCountOverride(size_t count)
{
	g_WorkerCountOverride = count;
}

void TaskManager::SetForceSerialForTesting(bool force)
{
	g_ForceSerialForTesting = force;
}

void TaskManager::Impl::SetupWorkers(size_t numberOfWorkers)
{
	for (size_t i = 0; i < numberOfWorkers; ++i)
		m_Workers.emplace_back(*this, i + 1);
}

size_t TaskManager::GetNumberOfWorkers() const
{
	return m->m_Workers.size();
}

void TaskManager::PushTask(std::function<void()> task, TaskPriority priority)
{
	m->PushTask(std::move(task), priority);
}

void TaskManager::PushTasks(std::vector<std::function<void()>> tasks, TaskPriority priority)
{
	m->PushTasks(std::move(tasks), priority);
}

void TaskManager::ParallelFor(size_t n, const std::function<void(size_t, size_t, size_t)>& body)
{
	ParallelFor(n, DefaultGrain(), body);
}

void TaskManager::ParallelFor(size_t n, size_t grainSize, const std::function<void(size_t, size_t, size_t)>& body)
{
	if (n == 0)
		return;
	if (grainSize == 0)
		grainSize = 1;

	// Force serial execution for determinism testing: run inline on calling thread only.
	if (g_ForceSerialForTesting)
	{
		body(0, n, 0);
		return;
	}

	// Short region: run inline, zero queue traffic, zero atomics, zero wakes.
	if (n <= grainSize)
	{
		body(0, n, 0);
		return;
	}

	const size_t chunks = (n + grainSize - 1) / grainSize;
	const size_t helpers = std::min(m->m_Workers.size(), chunks - 1);
	if (helpers == 0)
	{
		body(0, n, 0);
		return;
	}

#ifndef NDEBUG
	ENSURE(!t_InParallelRegion);
	t_InParallelRegion = true;
#endif

	ParallelRegion region;
	region.m_Cursor.store(0, std::memory_order_relaxed);
	region.m_Remaining.store(helpers + 1, std::memory_order_relaxed);
	region.m_Body = &body;
	region.m_Count = n;
	region.m_Grain = grainSize;

	m->PublishRegion(region, helpers);   // Enqueues tasks, wakes workers

	RunRegion(&region);                  // Main thread participates

	while (region.m_Remaining.load(std::memory_order_acquire) != 0)
		std::this_thread::yield();

#ifndef NDEBUG
	t_InParallelRegion = false;
#endif
}

size_t TaskManager::GetCurrentWorkerIndex()
{
	return t_WorkerIndex;
}

void TaskManager::Impl::PushTask(std::function<void()>&& task, TaskPriority priority)
{
	std::deque<TaskRef>& queue = priority == TaskPriority::NORMAL ? m_GlobalQueue : m_GlobalLowPriorityQueue;
	std::atomic<bool>& hasWork = priority == TaskPriority::NORMAL ? m_HasWork : m_HasLowPriorityWork;
	{
		std::lock_guard<std::mutex> lock(m_Mutex);
		auto* heapFunc = new std::function<void()>(std::move(task));
		queue.emplace_back(TaskRef{&InvokeAndDeleteStdFunction, heapFunc});
		hasWork = true;
	}
	m_ConditionVariable.notify_one();
}

void TaskManager::Impl::PushTasks(std::vector<std::function<void()>>&& tasks, TaskPriority priority)
{
	if (tasks.empty())
		return;

	std::deque<TaskRef>& queue = priority == TaskPriority::NORMAL ? m_GlobalQueue : m_GlobalLowPriorityQueue;
	std::atomic<bool>& hasWork = priority == TaskPriority::NORMAL ? m_HasWork : m_HasLowPriorityWork;

	{
		std::lock_guard<std::mutex> lock(m_Mutex);
		for (auto& task : tasks)
		{
			auto* heapFunc = new std::function<void()>(std::move(task));
			queue.emplace_back(TaskRef{&InvokeAndDeleteStdFunction, heapFunc});
		}
		hasWork = true;
	}

	m_ConditionVariable.notify_all();
}

void TaskManager::Impl::PublishRegion(ParallelRegion& region, size_t count)
{
	{
		std::lock_guard<std::mutex> lock(m_Mutex);
		for (size_t i = 0; i < count; ++i)
			m_GlobalQueue.emplace_back(TaskRef{&RunRegion, &region});
		m_HasWork = true;
	}
	m_ConditionVariable.notify_all();
}

template<TaskPriority Priority>
bool TaskManager::Impl::PopTask(TaskRef& taskOut)
{
	std::deque<TaskRef>& queue = Priority == TaskPriority::NORMAL ? m_GlobalQueue : m_GlobalLowPriorityQueue;
	std::atomic<bool>& hasWork = Priority == TaskPriority::NORMAL ? m_HasWork : m_HasLowPriorityWork;

	// Particularly critical section since we're locking the global queue.
	std::lock_guard<std::mutex> lock(m_Mutex);
	if (!queue.empty())
	{
		taskOut = queue.front();
		queue.pop_front();
		hasWork = !queue.empty();
		return true;
	}
	return false;
}

// Thread definition

WorkerThread::WorkerThread(TaskManager::Impl& taskManager, size_t workerIndex)
	: m_TaskManager(taskManager), m_WorkerIndex(workerIndex)
{
	Start<WorkerThread, &WorkerThread::RunUntilDeath>(this);
}

WorkerThread::~WorkerThread()
{
	m_Kill = true;
	{
		std::lock_guard<std::mutex> lock(m_TaskManager.m_Mutex);
	}
	m_TaskManager.m_ConditionVariable.notify_all();
	if (m_Thread.joinable())
		m_Thread.join();
}


void WorkerThread::RunUntilDeath()
{
	t_WorkerIndex = m_WorkerIndex;

	// The profiler does better if the names are unique.
	static std::atomic<int> n = 0;
	std::string name = "Task Mgr #" + std::to_string(n++);
	debug_SetThreadName(name.c_str());
	tracy::SetThreadName(name.c_str());
	g_Profiler2.RegisterCurrentThread(name);

	TaskRef task;
	bool hasTask = false;
	std::unique_lock<std::mutex> lock(m_TaskManager.m_Mutex, std::defer_lock);

	while (!m_Kill)
	{
		lock.lock();
		m_TaskManager.m_ConditionVariable.wait(lock, [this](){
			return m_Kill || m_TaskManager.m_HasWork || m_TaskManager.m_HasLowPriorityWork;
		});
		lock.unlock();

		if (m_Kill)
			break;

		// Fetch work from the global queues.
		hasTask = m_TaskManager.PopTask<TaskPriority::NORMAL>(task);
		if (!hasTask)
			hasTask = m_TaskManager.PopTask<TaskPriority::LOW>(task);
		if (hasTask)
			task.fn(task.ctx);
	}
}

// Defined here - needs access to derived types.
template<typename T, void(T::* callable)()>
void Thread::DoStart(T* object)
{
	std::invoke(callable, object);
}

TaskBatch::TaskBatch(TaskManager& taskManager, size_t reserve)
	: m_TaskManager(taskManager)
{
	m_Normal.reserve(reserve);
}

TaskBatch::~TaskBatch()
{
	Flush();
}

void TaskBatch::Flush()
{
	if (!m_Normal.empty())
	{
		m_TaskManager.PushTasks(std::move(m_Normal), TaskPriority::NORMAL);
		m_Normal.clear();
	}
	if (!m_Low.empty())
	{
		m_TaskManager.PushTasks(std::move(m_Low), TaskPriority::LOW);
		m_Low.clear();
	}
}

} // namespace Threading
