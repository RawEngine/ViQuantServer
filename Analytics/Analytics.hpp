
#pragma once

#include "Semaphore.hpp"

class Analytics
{
public:
	Analytics(Main& rApp, const Database::Info& rDBInfo, const String& rServerAddress, U16 serverPort, U16 connectTimeoutSec);
	~Analytics();

	auto AddEvent(EventId eventId, U32 cameraId, U8 personThreshold, const String& rFootagePath) -> void;
	auto EndEvent(EventId eventId) -> void;

	auto AddFootage(EventId eventId, EventFootageId eventFootageId, const String& rName) -> void;

private:

	auto ReleaseSession(AnalyticsSessionId id) -> void;

	auto HandleConnect(AnalyticsSessionId id, const TimePoint& rCurrentTP) -> bool;
	auto HandleRead(AnalyticsSessionId id) -> void;
	auto HandleSend(AnalyticsSessionId id) -> void;
	auto HandleQueuedFootageList() -> void;
	auto HandleQueuedFootageMap() -> void;

private:

	auto ThreadProc() -> void;

	void WriteQueuedResults(Database::Connection& rDatabase);

	auto WriteXMLParsedResults(Database::Connection& rDatabase, AnalyticsSessionId sessionId, U32 cameraId, EventId eventId, const String& rXML) -> EventFootageId;
	auto WriteXML(Database::Connection& rDatabase, EventFootageId eventId, const String& rXML) -> void;

private:

	Main& mMain;

	const U16				mConnectTimeoutSec;
	const U16				mServerPort;
	const Database::Info	mDBInfo;
	const String			mServerAddress;

	std::atomic_bool		mIsStopRequested{ false };

	std::unique_ptr<std::thread> mThreadPtr;


	Vector<char> mReadBuffer;

	U8	mMessageType = 0;
	U32	mMessageSize = 0;

	struct
	{
		String analyticsInsertSQL;
		String analyticsInsertSQLParsed;
		String analyticsUpdateStats;
	} mSQLQuery;

	struct ResultsInfo
	{
		ResultsInfo() { };

		ResultsInfo(AnalyticsSessionId id, U32 c, EventId e, U16 footageIndex, const String& rName)
			: sessionId(id)
			, footageIndex(footageIndex)
			, cameraId(c)
			, eventId(e)
			, name(rName)
		{ }

		ResultsInfo(const ResultsInfo& r)
			: sessionId(r.sessionId)
			, footageIndex(r.footageIndex)
			, cameraId(r.cameraId)
			, eventId(r.eventId)
			, name(r.name)
		{ }

		AnalyticsSessionId sessionId = 0;

		U16		footageIndex = 0;
		U32		cameraId = 0;
		EventId eventId = 0;
		String	name; // TEMP: For "mResultQueue" this XML result.
	};

	// Result's that were analyzed by the Analytics server 
	// and are ready to be written to the Database.
	// TODO: SITAM QUEUE REIKIA SUKURTI ATSKIRA STRUCT'A (NEBENAUDOTI "FootageInfo")
	// NES mResultQueue REIKIA "sessionId", "cameraId" ir t.t. TUO TARPU "mFootageQueue" TO NEREIKIA!!!!
	std::queue<ResultsInfo>		mResultQueue;


	//===================================================================================
	struct FootageInfo
	{
		FootageInfo() { };

		FootageInfo(EventId eventId, EventFootageId eventFootageId, const String& rName)
			: eventId(eventId)
			, eventFootageId(eventFootageId)
			, name(rName)
		{ }

		FootageInfo(const FootageInfo& r)
			: eventId(r.eventId)
			, eventFootageId(r.eventFootageId)
			, name(r.name)
		{ }

		EventId eventId = 0;
		EventFootageId eventFootageId = 0;
		String	name; // Footage filename (without the path)
	};

	Vector<FootageInfo>	mFootageQueue;
	std::mutex			mFootageMutex;

	//===================================================================================

	enum class SatusFlags : uint8_t
	{
		Free = 0,
		Failed,
		Connecting, // Socket is trying to connect.
		Connected,	// Socket is connected, but still not initialized.
		Handshake,
		Ready
	};

	AnalyticsSessionId			mAnalyticIdCounter = 0;
	Vector<AnalyticsSessionId>	mAnalyticsReleasedIds;

	Vector<SocketId>			mAnalyticsSockets;
	Vector<U32>					mAnalyticsCameraIds;
	Vector<U8>					mAnalyticsThresholds; // Person for now, use struct of Thresholds in the future?
	Vector<EventId>				mAnalyticsEvents;
	Vector<String>				mAnalyticsEventPaths;
	Vector<SatusFlags>			mAnalyticsStatus;
	Vector<TimePoint>			mAnalyticsTimePoints;
	Vector<U32>					mAnalyticsUniqueId;

	struct Session
	{
		Session()
			: footageSendAllowedPtr(std::make_shared<std::atomic_bool>(true))
		{ }

		struct Footage
		{
			EventFootageId eventFootageId;
			String fileName;
		};

		// If "person" was detected with the appropriate threshold,
		// We will stop sending all other event associated footage to the analytics server.
		bool isDone = false;
		AnalyticsSessionId	sessionId = 0; // Analytics session id.
		std::queue<Footage> footageQueue;
		std::shared_ptr<std::atomic_bool> footageSendAllowedPtr;
	};

	// TODO: Gal mums MAP'o visai cia nereikia, gal tiktu tiesiog Vector su pointeriu i QUEUE (std::queue<String>)?
	std::unordered_map<EventId, Session> mEventMap;

//	Vector<std::unique_ptr<Session>> mAnalyticsSessions;
};
