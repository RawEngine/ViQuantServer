
#pragma once

#include <future>

#include <mutex>
#include <queue>
#include <functional>

class ThreadPool
{
public:
	ThreadPool();
	ThreadPool(size_t numThreads);
	~ThreadPool();

	ThreadPool(const ThreadPool &) = delete;
	ThreadPool(ThreadPool &&) = delete;

	ThreadPool &operator=(const ThreadPool &) = delete;
	ThreadPool &operator=(ThreadPool &&) = delete;

	/// \brief Initialize the ThreadPool with a number of threads.
	/// This method does nothing if the thread pool is already running,
	/// i.e. ThreadPool( size_t ) was called.
	void initializeWithThreads(size_t numThreads);

//	void Enqueue(const std::function<void()>&);

	/// \brief a blocking function that waits until the threads have processed all the tasks in the queue.
	void waitAll() const;


	/*
	template<typename F, typename...Args>
	  auto submit(F&& f, Args&&... args) -> std::future<decltype(f(args...))>
	  {
		// Create a function with bounded parameters ready to execute
		std::function<decltype(f(args...))()> func = std::bind(std::forward<F>(f), std::forward<Args>(args)...);

		// Encapsulate it into a shared ptr in order to be able to copy construct / assign
		auto task_ptr = std::make_shared<std::packaged_task<decltype(f(args...))()>>(func);

		// Wrap packaged task into void function
		std::function<void()> wrapper_func = [task_ptr]() {
		  (*task_ptr)();
		};

		// Enqueue generic wrapper function
		m_queue.enqueue(wrapper_func);

		// Wake up one thread if its waiting
		m_conditional_lock.notify_one();

		// Return future from promise
		return task_ptr->get_future();
	  }
	*/

	// Variadic template.
	template<typename T, typename ...Args>
	auto Enqueue(T&& f, Args&&... args) -> std::future<decltype(f(args...))>
	{
		using returnType = decltype(f(args...));

		// Create a task function with bounded parameters.
		auto task = std::bind(std::forward<T>(f), std::forward<Args>(args)...);

		auto taskPtr = std::make_shared<std::packaged_task<returnType()>> (task);

//		std::function<void()> wrapper_func = [taskPtr]() { (*taskPtr)(); };
		{
			std::unique_lock<std::mutex> lock(mTaskLock);

			mTasks.emplace([taskPtr]() { (*taskPtr)(); });
//			mTasks.emplace(wrapper_func);
		}

		mNumTasks++;

		mCondition.notify_one();

		return taskPtr->get_future();
	}

private:

	Vector<std::thread>			mThreads;
	std::queue<std::function<void()>>	mTasks;
	std::mutex							mTaskLock;

	std::condition_variable				mCondition;

	std::atomic_bool	mIsStopRequested{ false };

	std::atomic_int		mNumTasks{ 0 };
};
