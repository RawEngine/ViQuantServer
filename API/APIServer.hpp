
#pragma once

class Main;

using APIClientId = U32;

class APIServer
{
public:
	APIServer(Main& rApp);
	~APIServer();

	bool Start(U16 port);

	void Update();

private:

	void HandleNewConnection();
//	void HandleRead();
//	void HandleTimeouts();

	void HandleCGI(APIClientId clientId, const String& rCGI);
	void HandleCGI_ArmState(const String& rCGI, bool isArmed);

	APIClientId AddClient(SocketId socketId);

	void HandleCameraArmState(U32 cameraId, bool isArmed);

	bool GetDatabaseFTPCamerasArmStatesBySite(U32 siteId, Vector<U32>& rList);
	bool GetDatabaseFTPCameraHashKey(U32 cameraId, String& rHashKey, bool& rIsArmed);
	void SetDatabaseFTPCameraArmState(U32 cameraId, bool isArmed);
	void SetDatabaseFTPCamerasSiteArmState(U32 siteId, bool isArmed);

private:

	static constexpr U32 ClientTimeout = 10; // In seconds.

	Main&		mMain;

	SocketId	mServerSocket = INVALID_SOCKET;

	APIClientId			mClientIdCounter = 0;

	Vector<APIClientId>	mClientReleasedIds;

	Vector<SocketId>	mClientSockets;
	Vector<TimePoint>	mClientTimePoints;
};
