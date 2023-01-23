
#pragma once

namespace Socket
{
	auto CreateServer(U16& rPort, int maxConnectionsQuery, bool isBlocking = false) -> SocketId;

	auto Create() -> SocketId;
	auto Close(SocketId& rSocketId) -> void;

//	auto Connect(SocketId socketId, const String& rAddress, U16 port, int connectAttempts) -> void;

	void SendText(SocketId socketId, const String& rText);
	void SendTextVA(SocketId socketId, const char* pFormat, ...);

	auto Send(SocketId socketId, const char* pBuffer, const ssize_t bufferSize) -> void;

	auto Read(SocketId socketId, char* pBuffer, const size_t bufferSize) -> bool;
	auto Read(SocketId socketId, Vector<char>& rDataBuffer) -> void;

	void SetReusable(SocketId socketId);
	void SetNonBlocking(SocketId socketId);
	bool IsBlocking(SocketId socketId);

	char* GetErrorString(int errorCode);

	int GetErrorCode();

//	 bool IsValid() const { return Handle != INVALID_SOCKET; }
};