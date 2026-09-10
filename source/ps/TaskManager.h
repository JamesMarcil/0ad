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

private:
	TaskManager(size_t numberOfWorkers);

	class Impl;
	const std::unique_ptr<Impl> m;
};
} // namespace Threading

#define g_TaskManager Threading::TaskManager::GetSingleton()

#endif // INCLUDED_THREADING_TASKMANAGER
