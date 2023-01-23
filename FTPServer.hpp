
#pragma once

class Main;

enum class FTPCommand
{
	AUTH,
	USER,
	PASS,
	TYPE,
	PWD,
	CWD,
	PASV,
	PORT,
	STOR,
	NOOP,
	MODE,
	STRU,
	DELE,	// Delete the file
	RNFR,	// Rename the file.
	RNTO,	// 
	SYST,
	FEAT,
	LIST,
	ABOR,
	QUIT,
	Unknown,
};

struct FTPSession
{
	~FTPSession()
	{
		Socket::Close(passiveSocketId);
	}

	enum class Mode { Passive, Active } mode;

	U16 port = 0;
	U32 address = 0;	// IP address.

	// In case of PASSIVE mode - holds a listening socket.
	SocketId passiveSocketId = INVALID_SOCKET;
};

class FTPServer
{
public:
	FTPServer(Main& rApp, U32 passiveSockTimeoutSec);
	~FTPServer();

	bool Start(U16 port);

	void HandleNewConnection();
	void HandleClients(Database::Connection& rDatabase);
	void HandleTimeouts();
	void HandleInactiveClients();

	void ClientTimeoutLock(ClientId clientId);
	void ClientTimeoutUnlock(ClientId clientId);

private:

	auto AddClient(SocketId socketId) -> ClientId;

	void SetupAuthSQLQuery();

	bool CheckAuthentification(EventSessionId eventSessionId, const String& rUsername, const String& rPassword, U32* pUserId, U32* pSiteId, U32* pCameraId, bool* pIsArmed, U8* pPersonThreshold);

	auto GetCommandType(char* pBuffer, ssize_t size) -> FTPCommand;
	auto GetCommandName(FTPCommand commandType) -> String;

private:

	Main& mMain;

	static constexpr U32 ClientTimeout = 10; // In seconds.

	const U32	mPassiveSocketTimeoutSec;
	const int	mMaxConnectionsQuery = 32;

	SocketId				mServerSocket = INVALID_SOCKET;

	// Incremented each time a new client is created. (Not conting the re-used clients)
	ClientId				mClientIdCounter = 0;

	Vector<ClientId>		mClientReleasedIds;	// List of client id's that were released and can be reused.
	Vector<ClientId>		mClientActiveIds;	// List of currently active client ids.

	// Client Components.
	Vector<SocketId>		mClientSockets;
	Vector<TimePoint>		mClientTimePoints;
	Vector<String>			mClientUsernames;
	Vector<EventSessionId>	mClientEventSessionIds;
	Vector<U32>				mClientEventSessionFootageOffsetIndexes;

	Vector<bool>			mClientTimeoutLocks;
	std::mutex				mClientTimeoutLocksMutex; // Used only for "resize" protection while separate thread might be using the array.

	//==========================================================

	UnorderedMap<ClientId, FTPSession> mFTPSessionMap;

	struct
	{
		String authA;
		String authB;
	} mSQLQuery;
};
