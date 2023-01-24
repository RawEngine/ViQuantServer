
#pragma once

namespace Socket
{
	SocketId CreateServer(U16& rPort, int maxConnectionsQuery, bool isBlocking = false);

	SocketId Create();
	void     Close(SocketId& rSocketId);

//	void Connect(SocketId socketId, const String& rAddress, U16 port, int connectAttempts);

	void SendText(SocketId socketId, const String& rText);
	void SendTextVA(SocketId socketId, const char* pFormat, ...);

	void Send(SocketId socketId, const char* pBuffer, const ssize_t bufferSize);

	bool Read(SocketId socketId, char* pBuffer, const size_t bufferSize);
	void Read(SocketId socketId, Vector<char>& rDataBuffer);

	void SetReusable(SocketId socketId);
	void SetNonBlocking(SocketId socketId);
	bool IsBlocking(SocketId socketId);

	char* GetErrorString(int errorCode);

	int GetErrorCode();

//	 bool IsValid() const { return Handle != INVALID_SOCKET; }
};
