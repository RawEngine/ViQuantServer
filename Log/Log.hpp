
#pragma once

class Log
{
public:
	enum class Channel
	{
		Main,
		DB,
		API,
		FTP,
		CGI,
		Analytics,
		Events
	};

	enum class Level
	{
		Debug,
		Info,
		Warning,
		Error
	};

	Log(const String& rPath);
	~Log();

	void WriteVA(Channel channel, Level level, int line, const char* pFuncName, const char* pFormat, ...);
	void Write(Channel channel, Level level, int line, const char* pFuncName, const String& rMessage);

private:

	void ThreadProc();

	struct Info
	{
		Info(Channel channel, Level level, int line, const String& rFuncName, const String& rMessage)
			: channel(channel)
			, level(level)
			, line(line)
			, func(rFuncName)
			, text(rMessage)
			, timePoint(std::chrono::steady_clock::now())
		{ }

		Channel		channel;
		Level		level;
		int			line;
		String		func;
		String		text;
		TimePoint	timePoint;
	};

private:

	const String			mLogPath;

	std::atomic_bool		mIsStopRequested{ false };
	std::condition_variable	mCondition;

	std::thread				mThread;
	std::queue<Info>		mQueue;
	std::mutex				mQueueLock;
};

extern Log* gpLog;

#define LOG_DEBUG(channel, ...)		gpLog->WriteVA(channel, Log::Level::Debug, __LINE__, __FUNCTION__, __VA_ARGS__)
#define LOG_MESSAGE(channel, ...)	gpLog->WriteVA(channel, Log::Level::Info, __LINE__, __FUNCTION__, __VA_ARGS__)
#define LOG_WARNING(channel, ...)	gpLog->WriteVA(channel, Log::Level::Warning, __LINE__, __FUNCTION__, __VA_ARGS__)
#define LOG_ERROR(channel, ...)		gpLog->WriteVA(channel, Log::Level::Error, __LINE__, __FUNCTION__, __VA_ARGS__)
