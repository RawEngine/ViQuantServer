
#include "PCH.hpp"

#include "Main.hpp"
#include "Socket.hpp"

#include "CGI/CGIManager.hpp"

#ifndef PLATFORM_WINDOWS
//#include <sys/types.h>
//#include <sys/socket.h>
#include <netdb.h>
#endif

CGIManager::CGIManager(const String& rHostname, U16 port)
	: mHostname(rHostname)
	, mPort(port)
{
	LOG_MESSAGE(Log::Channel::CGI, "Notifications host: %s:%u", rHostname.c_str(), port);
/*
	addrinfo hints {};
	{
		hints.ai_family = AF_UNSPEC;	// To allow both IPv4 and IPv6 addresses.
		hints.ai_socktype = SOCK_STREAM;
		hints.ai_protocol = IPPROTO_TCP;
	}

	const String portString(std::to_string(mPort));

	int result = getaddrinfo(mHostname.c_str(), portString.c_str(), &hints, &mpAddrInfo);
	if (result != 0)
		throw ExceptionVA("Failed for \"getaddrinfo(\"%s:%s\")\"! (Error: %s, Code: %d)", mHostname.c_str(), portString.c_str(), gai_strerror(result), result);

	// TMEP: Testing...
	for (int i = 0; i < 5; ++i)
	{
		this->Add("/ui/inform-user.php?eventID=" + std::to_string(i));
	}
*/
}

CGIManager::~CGIManager()
{
//	if (mpAddrInfo)
//		freeaddrinfo(mpAddrInfo);
}

void CGIManager::Add(const String& rCGI)
{
	LOG_MESSAGE(Log::Channel::CGI, "Adding CGI for processing: \"%s\".", rCGI.c_str());

	mQueueMutex.lock();
	mQueue.emplace_back(rCGI);
	mQueueMutex.unlock();
}

void CGIManager::Process()
{
	mQueueMutex.lock();

	if (mQueue.empty())
	{
		mQueueMutex.unlock();
		return;
	}

	Vector<String> localQueue;
	localQueue.resize(mQueue.size());
	std::copy(mQueue.begin(), mQueue.end(), localQueue.begin());

	mQueue.clear();

	mQueueMutex.unlock();

	for (auto& rCGI : localQueue)
	{
		if (gIsQuitRequested)
		{
			// TODO: Finish processing queued results?
			LOG_WARNING(Log::Channel::CGI, "CGI manager detected STOP requested while still holding %" PRIu64 " queued CGI's!", mQueue.size());
			break;
		}

		LOG_MESSAGE(Log::Channel::CGI, "Processing CGI: \"%s\".", rCGI.c_str());

#if 1
		std::ostringstream ss;

#if PLATFORM_WINDOWS
		ss << "curl \"http://" << mHostname << rCGI << "\"";
#else
		ss << "curl -X GET https://" << mHostname << rCGI;
#endif

		system(ss.str().c_str());
#else
		String cgiContent;
		{
			std::ostringstream	ss;

			ss << "GET " << rCGI << " HTTP/1.1\r\n";
			ss << "Host: " << mHostname/* << ":" << mPort */<< "\r\n";
			ss << "Connection: keep-alive\r\n";
			ss << "\r\n";
			cgiContent = ss.str();
		}

		SocketId socketId = INVALID_SOCKET;

		try
		{
			if ((socketId = socket(mpAddrInfo->ai_family, mpAddrInfo->ai_socktype, mpAddrInfo->ai_protocol)) == INVALID_SOCKET)
			{
				int errorCode = Socket::GetErrorCode();
				throw ExceptionVA("Failed for \"socket\"! (Error: %s, Code: %d)", Socket::GetErrorString(errorCode), errorCode);
			}

			if (connect(socketId, mpAddrInfo->ai_addr, mpAddrInfo->ai_addrlen) == SOCKET_ERROR)
			{
				int errorCode = Socket::GetErrorCode();
				throw ExceptionVA("Failed for \"connect\"! (Error: %s, Code: %d)", Socket::GetErrorString(errorCode), errorCode);
			}

			auto bytesSent = send(socketId, cgiContent.c_str(), cgiContent.size(), 0);

			if (bytesSent == SOCKET_ERROR)
			{
				int errorCode = Socket::GetErrorCode();
				throw ExceptionVA("Failed for \"send\"! (Error: %s, Code: %d)", Socket::GetErrorString(errorCode), errorCode);
			}

			printf("bytesSent: %d\n", bytesSent);
		}
		catch (const Exception& e)
		{
			LOG_ERROR(Log::Channel::CGI, e.GetText());
		}

		Socket::Close(socketId);
#endif
	}
}
