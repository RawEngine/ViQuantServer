
#pragma once

// https://stackoverflow.com/questions/4792449/c0x-has-no-semaphores-how-to-synchronize-threads

#include <mutex>
#include <condition_variable>

class Semaphore
{
public:
	void Notify()
	{
		std::lock_guard<std::mutex> lock{ mMutex };

		++mCount;

		mCV.notify_one();
	}

	void Wait() 
	{
		std::unique_lock<std::mutex> lock{ mMutex };

		mCV.wait(lock, [&] { return mCount > 0; });
//		while (!mCount) // Handle spurious wake-ups.
//			mCV.wait(lock);

		--mCount;
	}

	bool TryWait() 
	{
		std::lock_guard<std::mutex> lock{ mMutex };

		if (mCount > 0) 
		{
			--mCount;
			return true;
		}

		return false;
	}

private:

	std::mutex mMutex;
	std::condition_variable mCV;
	std::size_t mCount = 0;
};
