
#pragma once

class Main;

class EventManager
{
public:
	EventManager(Main& rApp, U32 eventSessionTimeoutSec);
	EventManager(const EventManager&) = delete;

	auto EventSessionTimeoutLock(EventSessionId sessionId) -> void;
	auto EventSessionTimeoutUnlock(EventSessionId sessionId) -> void;

	auto AddFootageNotice(EventId eventId, const String& rName, const String& rTimestampStr, U16 timestampMs) -> void;

	auto HasSession(const String& rHashKey, EventSessionId* pEventSessionId) const -> bool;
	auto AddSession(const String& rHashKey) -> EventSessionId;

	auto SetLastKnownFootageIndex(EventSessionId sessionId, U32 index) -> void;
	auto SetSessionTimepoint(EventSessionId sessionId, const TimePoint& rTP) -> void;
	auto SetSessionPath(EventSessionId sessionId, const String& rPath) -> void;
	auto SetSessionArmedState(EventSessionId sessionId, bool isArmed) -> void;

	auto HandleTimeouts() -> void;
	auto HandleQueuedFootageNotices() -> void;

	auto AuthenticateSession(EventSessionId sessionId, U32 userId, U32 siteId, U32 cameraId) -> EventId;

	auto& GetFootagePath(EventSessionId sessionId) const
	{ 
		return mSessionPaths.at(sessionId);
	}

	auto GetFootageIndex(EventSessionId sessionId) const
	{
		return mSessionFootageIndex.at(sessionId);
	}

	auto GetEventId(EventSessionId sessionId) const
	{
		return mSessionEventIds.at(sessionId);
	}

	auto GetCameraId(EventSessionId sessionId) const
	{
		return mSessionCameraIds.at(sessionId);
	}

	auto GetArmedState(EventSessionId sessionId) const
	{
		return mSessionArmedState.at(sessionId);
	}

private:

	auto WriteEventStart(U32 userId, U32 siteId, U32 cameraId) -> EventId;
	auto WriteEventEnd(EventId eventId) -> void;

private:

	Main& mMain;

	// Event session timeout in secons.
	// TODO: ? We might have different needs per different client for the event session timeouts?
	const U32 mEventSessionTimeoutSec;

	EventSessionId			mSessionIdCounter = 0;

	Vector<EventSessionId>	mSessionReleasedIds;

	Vector<TimePoint>		mSessionTimepoints;
	Vector<U32>				mSessionUserIds;
	Vector<U32>				mSessionSiteIds;
	Vector<U32>				mSessionCameraIds;
	Vector<EventId>			mSessionEventIds;
	Vector<U32>				mSessionFootageIndex;
	Vector<String>			mSessionPaths;

	Vector<bool>			mSessionArmedState;
	Vector<bool>			mSessionTimeoutLocks;
	std::mutex				mSessionTimeoutLocksMutex;

	// Event session lasts for a certain amount of time.
	// Event session is per-userId (retreived from DB - username and password)
	// Lasts until related client connections are sending the footage, or for the maximum "EventSession.timeout" seconds.
	// Event session doesn't care about the client disconnect events, because event session can last for multiple client connections for the same "username + password".
	UnorderedMap<String, EventSessionId> mSessionMap;

	struct
	{
		String eventInsert;
		String eventUpdate;
		String eventInsertFootage;
		String isArmed;
	} mSQLQuery;

	// NOTE: 
	// Event session might already be timed-out or closed but related data might still be in the queue.
	struct FootageInfo
	{
		FootageInfo() { };

		FootageInfo(EventId e, const String& rName, const String& rTimestampStr, U16 timestampMs)
			: eventId(e)
			, timestampMs(timestampMs)
			, name(rName)
			, timestampStr(rTimestampStr)
		{ }

		FootageInfo(const FootageInfo& r)
			: eventId(r.eventId)
			, timestampMs(r.timestampMs)
			, name(r.name)
			, timestampStr(r.timestampStr)
		{ }

		EventId	eventId = 0;
		U16		timestampMs = 0;

		String	name;
		String	timestampStr;
	};

	Vector<FootageInfo>	mFootageQueue;
	std::mutex			mFootageMutex;
};
