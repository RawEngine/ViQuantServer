
#include "PCH.hpp"

#include "ThreadPool.hpp"

// https://codereview.stackexchange.com/questions/79323/simple-c-thread-pool
// https://github.com/progschj/ThreadPool
// https://github.com/mtrebi/thread-pool

ThreadPool::ThreadPool()
{ }

ThreadPool::ThreadPool(size_t numThreads) 
{
	initializeWithThreads(numThreads);
}

ThreadPool::~ThreadPool() 
{
	mIsStopRequested = true;

	mCondition.notify_all();

	for (auto& t : mThreads)
		t.join();
}

void ThreadPool::initializeWithThreads(size_t numThreads) 
{
	for (size_t i = 0; i < numThreads; ++i)
	{
		mThreads.emplace_back([this]
		{
			for (;;)
			{
				std::function<void()> task;
				{
					std::unique_lock<std::mutex> lock(mTaskLock);

					mCondition.wait(lock, [this] { return !mTasks.empty() || mIsStopRequested; });

					if (mIsStopRequested && mTasks.empty())
						return;

					task = std::move(mTasks.front());

					mTasks.pop();
				}
				
				task();

				mNumTasks--;
			}
		});
	}
}
/*
void ThreadPool::Enqueue(const std::function<void()>& task)
{
	mTaskLock.lock();
	mTasks.push(task);
	mTaskLock.unlock();

	mNumTasks++;

	mCondition.notify_one();
}
*/

void ThreadPool::waitAll() const
{
	while (mNumTasks != 0u)
	{
	//	std::this_thread::yield
		std::this_thread::sleep_for(std::chrono::microseconds(1));
	}
}
