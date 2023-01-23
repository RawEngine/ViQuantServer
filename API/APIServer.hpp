
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

	auto HandleCGI(APIClientId clientId, const String& rCGI) -> void;
	auto HandleCGI_ArmState(const String& rCGI, bool isArmed) -> void;

	auto AddClient(SocketId socketId) -> APIClientId;

	auto HandleCameraArmState(U32 cameraId, bool isArmed) -> void;

	auto GetDatabaseFTPCamerasArmStatesBySite(U32 siteId, Vector<U32>& rList) -> bool;
	auto GetDatabaseFTPCameraHashKey(U32 cameraId, String& rHashKey, bool& rIsArmed) -> bool;
	auto SetDatabaseFTPCameraArmState(U32 cameraId, bool isArmed) -> void;
	auto SetDatabaseFTPCamerasSiteArmState(U32 siteId, bool isArmed) -> void;

private:

	static constexpr U32 ClientTimeout = 10; // In seconds.

	Main&		mMain;

	SocketId	mServerSocket = INVALID_SOCKET;

	APIClientId			mClientIdCounter = 0;

	Vector<APIClientId>	mClientReleasedIds;

	Vector<SocketId>	mClientSockets;
	Vector<TimePoint>	mClientTimePoints;
};