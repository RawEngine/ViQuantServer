
#include "PCH.hpp"

#include "Socket.hpp"

#include <fcntl.h>

#ifndef PLATFORM_WINDOWS
#include <unistd.h>		// close
#include <sys/socket.h>	// socket
#include <netinet/in.h> // sockaddr_in
#include <sys/ioctl.h>	// ioctl
#include <arpa/inet.h>	// inet_pton
#endif

#include <cstdarg>	// va_start
#include <string.h>	// strncpy

namespace Socket
{
	// IMPORTANT: Exception is thrown on failure. User is responsible for handling the exception.
	// port - If set to zero, the service provider assigns a unique port to the application with a value between 1024 and 5000.
	SocketId CreateServer(U16& rPort, int maxConnectionsQuery, bool isBlocking /* = false */)
	{
		SocketId socketId = Socket::Create();

		Socket::SetReusable(socketId);

		if (!isBlocking)
			Socket::SetNonBlocking(socketId);

		sockaddr_in addr{};

		addr.sin_family = AF_INET;
		addr.sin_addr.s_addr = htonl(INADDR_ANY);
		addr.sin_port = htons(rPort);

		if (bind(socketId, (struct sockaddr*) &addr, sizeof(sockaddr_in)) == SOCKET_ERROR)
		{
			int errorCode = Socket::GetErrorCode();
			throw ExceptionVA("Failed for \"bind\"! (Error: %s, Code: %d)", Socket::GetErrorString(errorCode), errorCode);
		}

		// If set to zero, the service provider assigns a unique port to the application with a value between 1024 and 5000.
		if (rPort == 0)
		{
			struct sockaddr_in sin;
			socklen_t len = sizeof(sin);

			// TODO: Check for errors.
			if (getsockname(socketId, (struct sockaddr *)&sin, &len) != -1)
				rPort = ntohs(sin.sin_port);
		}

		if (listen(socketId, maxConnectionsQuery) == -1)
			throw Exception("Failed for \"listen\"!");

		return socketId;
	}

	SocketId Create()
	{
		SocketId socketId = socket(AF_INET, SOCK_STREAM, 0);

		if (socketId == SOCKET_ERROR)
		{
			int errorCode = Socket::GetErrorCode();
			throw ExceptionVA("Failed for \"socket\"! (Error: %s, Code: %d)", Socket::GetErrorString(errorCode), errorCode);
		}

		return socketId;
	}

	void Close(SocketId& rSocketId)
	{
		if (rSocketId != INVALID_SOCKET)
		{
#ifdef PLATFORM_WINDOWS
			closesocket(rSocketId);
#else
			close(rSocketId);
#endif

			rSocketId = INVALID_SOCKET;
		}
	}
/*
	void Connect(SocketId socketId, const String& rAddress, U16 port, int connectAttempts)
	{
		sockaddr_in addr{};

		addr.sin_family = AF_INET;
		addr.sin_port = htons(port);

		// Convert IPv4 and IPv6 addresses from text to binary form.
		if (inet_pton(addr.sin_family, rAddress.c_str(), &addr.sin_addr) <= 0)
			throw ExceptionVA("Invalid address: %s:%d", rAddress.c_str(), port);

		for (int i = 0; i < connectAttempts; ++i)
		{
			int st = connect(socketId, (struct sockaddr *)&addr, sizeof(addr));
			{
				LOG_MESSAGE("Connect analytics response: %d", st);

				if (st != -1)
					break;

				if (mIsStopRequested)
					break;

				std::this_thread::sleep_for(std::chrono::milliseconds(100));
			}

			if (i == connectAttempts - 1)
				throw ExceptionVA("Failed to connect to the Analytics server: %s:%d", rAddress.c_str(), port);

			printf("Connect attempt: %d, max: %d\n", i, connectAttempts);
		}
	}
*/
	void SendText(SocketId socketId, const String& rText)
	{
#if PLATFORM_WINDOWS
#define MSG_NOSIGNAL 0
#endif

		// MSG_NOSIGNAL - Requests not to send SIGPIPE on errors on stream oriented sockets when the other end breaks the connection.
		const ssize_t bytesSent = send(socketId, rText.c_str(), rText.size(), MSG_NOSIGNAL);

		// TODO: Resend missing bytes.

		if (bytesSent < static_cast<ssize_t>(rText.size()))
			throw ExceptionVA("Socket::Send(\"%s\") failed to send!", rText.c_str());
	}

	void SendTextVA(SocketId socketId, const char* pFormat, ...)
	{
		char buffer[2048]{};

		va_list args;
		va_start(args, pFormat);
		vsnprintf(buffer, sizeof(buffer) - 1, pFormat, args);
		va_end(args);

		Socket::SendText(socketId, buffer);
	}

	void Send(SocketId socketId, const char* pBuffer, const ssize_t bufferSize)
	{
		// MSG_NOSIGNAL - Requests not to send SIGPIPE on errors on stream oriented sockets when the other end breaks the connection.
		ssize_t numBytesSent = 0;

		while (numBytesSent < bufferSize)
		{
			const ssize_t bytesSent = send(socketId, pBuffer + numBytesSent, bufferSize - numBytesSent, MSG_NOSIGNAL);

			if (bytesSent == 0)
				break; // Socket was closed.

			if (bytesSent == SOCKET_ERROR)
			{
				int errorCode = Socket::GetErrorCode();

				// Can occur when dealing with the non-blocking socket.
#if PLATFORM_WINDOWS
				if (errorCode == WSAEWOULDBLOCK) // 10035
#else
				if (errorCode == EWOULDBLOCK) // "Resource temporarily unavailable", code: 11
#endif
				{
					std::this_thread::yield();
					continue;
				}

				// Throwing exception instead of LOG_ERROR because it will be more clearer on where did the send function failed.
				throw ExceptionVA("Failed for \"send\"! (Error: %s, Code: %d)", Socket::GetErrorString(errorCode), errorCode);
			//	LOG_ERROR("Failed for \"send\"! (Error: %s, Code: %d)", Socket::GetErrorString(errorCode), errorCode);
			//	break;
			}

			numBytesSent += bytesSent;
		}

//		LOG_MESSAGE("Sent %" PRIu64 " bytes. (Total: %" PRIu64 " bytes)", numBytesSent, bufferSize);
	}

	bool Read(SocketId socketId, char* pBuffer, const size_t bufferSize)
	{
		size_t bytesReceivedTotal = 0;

		while (bytesReceivedTotal < bufferSize)
		{
			ssize_t bytesReceived = recv(socketId, &pBuffer[bytesReceivedTotal], bufferSize - bytesReceivedTotal, 0);

			if (bytesReceived == 0)
				break; // No more data.

			if (bytesReceived == SOCKET_ERROR)
			{
				int errorCode = Socket::GetErrorCode();

				// Can occur when dealing with the non-blocking socket.
#if PLATFORM_WINDOWS
				if (errorCode == WSAEWOULDBLOCK) // 10035
#else
				if (errorCode == EWOULDBLOCK) // "Resource temporarily unavailable", code: 11
#endif
				{
					std::this_thread::yield();
					continue;
				}

				// TODO: Handle timeout. (WSAETIMEDOUT)

				LOG_ERROR(Log::Channel::Main, "Failed for \"recv\"! (Error: %s, Code: %d)", Socket::GetErrorString(errorCode), errorCode);
				break;
			}

			bytesReceivedTotal += bytesReceived;
		}

		return (bytesReceivedTotal == bufferSize);
	}

	void Read(SocketId socketId, Vector<char>& rDataBuffer)
	{
		char buffer[1024];

		for (;;)
		{
			ssize_t bytesReceived = recv(socketId, buffer, sizeof(buffer), 0);

			if (bytesReceived == 0)
			{
				printf("Read socket was closed\n");
				break; // No more data.
			}

			if (bytesReceived == SOCKET_ERROR)
			{
				int errorCode = Socket::GetErrorCode();

				// Can occur when dealing with the non-blocking socket.
#if PLATFORM_WINDOWS
				if (errorCode == WSAEWOULDBLOCK) // 10035
#else
				if (errorCode == EWOULDBLOCK) // "Resource temporarily unavailable", code: 11
#endif
				{
					std::this_thread::yield();
					continue;
				}

				// TODO: Handle timeout. (WSAETIMEDOUT)

				LOG_ERROR(Log::Channel::Main, "Failed for \"recv\"! (Error: %s, Code: %d)", Socket::GetErrorString(errorCode), errorCode);
				return;
			}

			rDataBuffer.insert(rDataBuffer.end(), buffer, buffer + bytesReceived);
		}
	}

	void SetReusable(SocketId socketId)
	{
		int enable = 1;
#if PLATFORM_WINDOWS
		if (setsockopt(socketId, SOL_SOCKET, SO_REUSEADDR, (char*)&enable, sizeof(enable)) < 0)
#else
		if (setsockopt(socketId, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(enable)) < 0)
#endif
		{
			int errorCode = Socket::GetErrorCode();
			throw ExceptionVA("Failed for \"setsockopt(SO_REUSEADDR)\"! (Error: %s, Code: %d)", Socket::GetErrorString(errorCode), errorCode);
		}
	}

	// Make socket the "non-blocking".
	void SetNonBlocking(SocketId socketId)
	{
		u_long isOn = 1;
	#if PLATFORM_WINDOWS
		if (ioctlsocket(socketId, FIONBIO, &isOn) == SOCKET_ERROR)
	#else
		if (ioctl(socketId, FIONBIO, &isOn) == SOCKET_ERROR)
	#endif
		{
			int errorCode = Socket::GetErrorCode();
			throw ExceptionVA("Failed to set socket a non-blocking! (Error: %s, Code: %d)", Socket::GetErrorString(errorCode), errorCode);
		}
	}

	bool IsBlocking(SocketId socketId)
	{
#if PLATFORM_WINDOWS
		return false; // No way to determine this on Windows platform?
#else
		int value = fcntl(socketId, F_GETFL, 0);

		// TODO: Check for errors.

		return !(value & O_NONBLOCK);
#endif
	}

	char* GetErrorString(int errorCode)
	{
		return strerror(errorCode); // Return a string describing error number.
	}

	int GetErrorCode()
	{
#if PLATFORM_WINDOWS
		return WSAGetLastError();
#else
		return errno;
#endif
	}
}
