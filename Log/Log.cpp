
#include "PCH.hpp"

#include "Main.hpp"
#include "Utils.hpp"
#include "Log.hpp"

#include <cstdarg>	// va_start

Log* gpLog = nullptr;

Log::Log(const String& rPath)
	: mLogPath(rPath)
	, mThread(&Log::ThreadProc, this)
{
	gpLog = this;

	Write(Channel::Main, Level::Info, __LINE__, __FUNCTION__, "=======================================================================");
	Write(Channel::Main, Level::Info, __LINE__, __FUNCTION__, "Start [LOG PATH: \"" + rPath + "\"]");
	Write(Channel::Main, Level::Info, __LINE__, __FUNCTION__, "=======================================================================");
}

Log::~Log()
{
	Write(Channel::Main, Level::Info, __LINE__, __FUNCTION__, "End.");

	mIsStopRequested = true;

	mCondition.notify_all();
	mThread.join();
}

void Log::Write(Channel channel, Level level, int line, const char* pFuncName, const String& rMessage)
{
	mQueueLock.lock();
	mQueue.emplace(channel, level, line, pFuncName, rMessage);
	mQueueLock.unlock();

	mCondition.notify_one();
}

void Log::WriteVA(Channel channel, Level level, int line, const char* pFunctionName, const char* pFormat, ...)
{
	char buffer[4096];

	va_list args;
	va_start(args, pFormat);

	// Returns the number of characters written (not including the terminating null) 
	// Or a negative value if an output error occurs.
	size_t length = vsnprintf(buffer, sizeof(buffer) - 1, pFormat, args);

	if (length > sizeof(buffer) - 1)
		length = sizeof(buffer) - 1;

	va_end(args);

	buffer[sizeof(buffer) - 1] = 0;

	Write(channel, level, line, pFunctionName, String(buffer, length));
}

void Log::ThreadProc()
{
	String fileName;
	{
		auto posixTime = time(nullptr);
		auto tm = localtime(&posixTime);

		char buffer[16];
		strftime(buffer, sizeof(buffer), "%Y%m%d_%H%M%S", tm);

		fileName = buffer;
		fileName += "_log.txt";
	}

	std::ofstream file(mLogPath + fileName, std::ios_base::out | std::ios_base::trunc);

	if (!file.is_open())
	{
		std::cout << "Failed to open the log file!" << std::endl;
		return;
	}

	for (;;)
	{
		std::unique_lock<std::mutex> lock(mQueueLock);

		mCondition.wait(lock, [this] { return !mQueue.empty() || mIsStopRequested; });

		if (mIsStopRequested && mQueue.empty())
			return;

		auto& rRecord = mQueue.front();

		std::ostringstream ss;

		ss << '[' << Utils::StringFromLocaltime(true, true, true) << "] | ";

		switch (rRecord.channel)
		{
			case Channel::Main:			ss << "[Main] | ";	break;
			case Channel::DB:			ss << "[DB]   | ";	break;
			case Channel::API:			ss << "[API]  | ";	break;
			case Channel::FTP:			ss << "[FTP]  | ";	break;
			case Channel::CGI:			ss << "[CGI]  | ";	break;
			case Channel::Analytics:	ss << "[ANL]  | ";	break;
			case Channel::Events:		ss << "[EVN]  | ";	break;
			default:
				break;
		}

		switch (rRecord.level)
		{
			case Level::Debug:		ss << 'D';	break;
			case Level::Info:		ss << 'I';	break;
			case Level::Warning:	ss << 'W';	break;
			case Level::Error:		ss << 'E';	break;
		}

		ss << " | " << rRecord.text;

		if (rRecord.level == Level::Error)
			ss << " (Func: " << rRecord.func << ", Line: " << rRecord.line << ")";

//#if _DEBUG
		std::cout << ss.str() << std::endl;
//#endif

		// NOTE: On some platforms "std::endl" will just write LF, but not CRLF. So using "\r\n" instead.
#if PLATFORM_WINDOWS
		file << ss.str() << std::endl;
#else
		file << ss.str() << "\r\n"; // std::endl;
#endif
		file.flush();

		mQueue.pop();
	}
}
