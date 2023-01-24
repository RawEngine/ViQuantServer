
#include <PCH.hpp>

#include "Main.hpp"

#include "Database/Database.hpp"
#include "Database/DatabaseQuery.hpp"
#include "Database/DatabaseTables.hpp"

#include "Analytics/Analytics.hpp"

#include "EventManager.hpp"

#include <iomanip> // std::put_time, std::setw

EventManager::EventManager(Main& rApp, U32 eventSessionTimeoutSec)
	: mMain(rApp)
	, mEventSessionTimeoutSec(eventSessionTimeoutSec)
{ }

// IMPORTANT: Can be called from any ThreadPool thread. (FTP Server)
// Queue will be handled by the "EventManager::HandleQueuedFootageNotices"
void EventManager::AddFootageNotice(EventId eventId, const String& rName, const String& rTimestampStr, U16 timestampMs)
{
	LOG_DEBUG(Log::Channel::Events, "AddFootageNotice - EventId: %u (%s) - %s:%d", eventId, rName.c_str(), rTimestampStr.c_str(), timestampMs);

	mFootageMutex.lock();
	mFootageQueue.emplace_back(eventId, rName, rTimestampStr, timestampMs);
	mFootageMutex.unlock();
}

bool EventManager::HasSession(const String& rHashKey, EventSessionId* pEventSessionId) const
{
	auto it = mSessionMap.find(rHashKey);

	if (it == mSessionMap.end())
		return false;

	*pEventSessionId = it->second;
	return true;
}

// Adds the unauthenticated event session.
// Sessions that are not authenticated in the certain amount of time will timeout.
EventSessionId EventManager::AddSession(const String& rHashKey)
{
	EventSessionId id;

	if (mSessionReleasedIds.empty())
	{
		id = mSessionIdCounter++;

		if (mSessionTimepoints.size() <= id)
		{
			const std::size_t newSize = id + 1;

			mSessionTimepoints.resize(newSize);
			mSessionUserIds.resize(newSize);
			mSessionSiteIds.resize(newSize);
			mSessionCameraIds.resize(newSize);
			mSessionEventIds.resize(newSize);
			mSessionFootageIndex.resize(newSize);
			mSessionPaths.resize(newSize);

			mSessionArmedState.resize(newSize);

			mSessionTimeoutLocksMutex.lock();
			mSessionTimeoutLocks.resize(newSize);
			mSessionTimeoutLocksMutex.unlock();
		}
	}
	else
	{
		id = mSessionReleasedIds.back();
		mSessionReleasedIds.pop_back();
	}

	mSessionMap.emplace(rHashKey, id);

	// Stores a footage index offset that get's incremented every time a new footage (i.e. JPEG image) is received.
	mSessionFootageIndex.at(id) = 0;

	mSessionTimepoints.at(id) = std::chrono::steady_clock::now();
	mSessionEventIds.at(id) = InvalidEventId;

	mSessionArmedState.at(id) = false;
	mSessionTimeoutLocks.at(id) = false;

	return id;
}

void EventManager::SetLastKnownFootageIndex(EventSessionId sessionId, U32 index)
{
	mSessionFootageIndex.at(sessionId) = index;
}

void EventManager::SetSessionTimepoint(EventSessionId sessionId, const TimePoint& rTP)
{
	mSessionTimepoints.at(sessionId) = rTP;
}

void EventManager::SetSessionPath(EventSessionId sessionId, const String& rPath)
{
	mSessionPaths.at(sessionId) = rPath;
}

void EventManager::SetSessionArmedState(EventSessionId sessionId, bool isArmed)
{
	mSessionArmedState.at(sessionId) = isArmed;
}

void EventManager::EventSessionTimeoutLock(EventSessionId sessionId)
{
//	std::lock_guard<std::mutex> lock(mSessionTimeoutLocksMutex);

	mSessionTimeoutLocks.at(sessionId) = true;
}

void EventManager::EventSessionTimeoutUnlock(EventSessionId sessionId)
{
	std::lock_guard<std::mutex> lock(mSessionTimeoutLocksMutex);

	mSessionTimeoutLocks.at(sessionId) = false;
}

void EventManager::HandleTimeouts()
{
	const auto currentTP = std::chrono::steady_clock::now();

	if (!mSessionMap.empty()) // Adds ~500ns of performance boost when not iterating through the empty map.
	{
		for (auto it = mSessionMap.begin(); it != mSessionMap.end(); )
		{
			const auto id = (*it).second;

			// 2019-06-07
			// Don't allow "locket" event sessions to timeout.
			if (mSessionTimeoutLocks.at(id))
			{
				mSessionTimepoints.at(id) = currentTP; // To prevent instant timeout when unlocked.
				continue;
			}

			auto seconds = std::chrono::duration_cast<std::chrono::seconds>(currentTP - mSessionTimepoints.at(id)).count();

			if (seconds >= mEventSessionTimeoutSec)
			{
				const auto eventId = mSessionEventIds.at(id);

				// http://jhshi.me/2014/07/11/print-uint64-t-properly-in-c/index.html#.XGLaRVz7SMo
				LOG_MESSAGE(Log::Channel::Events, "!!!!!!!!!!!!!!!! EVENT[DB id: %" PRIu64 ", EventSessionId: %u] SESSION TIMEOUT !!!!!!!!!!!!!!!!!", eventId, id);

				// NOTE:
				// The "eventId" value can be "0" when dealing with a non-validated connection.
				if (eventId != InvalidEventId)
				{
					this->WriteEventEnd(eventId);

					mMain.AnalyticsPtr->EndEvent(eventId);

					mSessionEventIds.at(id) = InvalidEventId;
				}

				it = mSessionMap.erase(it);

				mSessionReleasedIds.push_back(id);
			}
			else
				++it;
		}
	}

	//	auto end = std::chrono::steady_clock::now();
	//	auto diff = end - start;

	//	std::cout << "ERASING TOOK: " << std::chrono::duration<double, std::nano>(diff).count() << " ns" << std::endl;
}

// TODO: The "rFilename" is not secure from the SQL injections.
void EventManager::HandleQueuedFootageNotices()
{
	mFootageMutex.lock();

	if (mFootageQueue.empty())
	{
		mFootageMutex.unlock();
		return;
	}

	// Make a quick copy of the content and release the lock.
	Vector<FootageInfo> localQueue(mFootageQueue.size());
	std::copy(mFootageQueue.begin(), mFootageQueue.end(), localQueue.begin());

	mFootageQueue.clear();
	mFootageMutex.unlock();

	if (mSQLQuery.eventInsertFootage.empty())
	{
		using namespace Database::Table;

		std::ostringstream ss;

		ss  << "INSERT INTO " << Footage::TableName
			<< " (" << Footage::Timestamp
			<< ',' << Footage::Milliseconds
			<< ',' << Footage::EventId
			<< ',' << Footage::Name
			<< ") VALUES ";

		mSQLQuery.eventInsertFootage = ss.str();
	}

	// We need to get the "event footage id" for every INSERT.
	// So doing a single INSERT per multiple queue elements is not the right solution.

	// We could try though... but would it be 100% reliable?
	// ("If you insert multiple rows using a single INSERT statement, 
	//   LAST_INSERT_ID() returns the value generated for the first inserted row only. 
	//   The reason for this is to make it possible to reproduce easily the same INSERT statement against some other server.")
	// 
#if 1

	for (auto& r : localQueue)
	{
		std::ostringstream ss;
		
		ss	<< mSQLQuery.eventInsertFootage
			<< "('" << r.timestampStr 
			<< "'," << r.timestampMs 
			<< ',' << r.eventId 
			<< ",'" << r.name 
			<< "')";

		Database::Query query(*mMain.DatabasePtr);

		if (!query.Exec(ss.str()))
			LOG_ERROR(Log::Channel::Events, "SQL query failed for \"EventManager::HandleQueuedFootageNotices\"!");

		const auto eventFootageId = static_cast<EventFootageId>(query.LastInsertId());

		mMain.AnalyticsPtr->AddFootage(r.eventId, eventFootageId, r.name);
	}

#else
	// IMPORTANT: 
	// The maximum number of rows in one VALUES clause is 1000
	// Using 100 instead of 1000, don't want to push the limits...
	const size_t chunkSize = 100;

	for (size_t i = 0; i < infoList.size(); i += chunkSize)
	{
		const size_t begin = i;
		const size_t end = std::min(infoList.size(), i + chunkSize);

		std::ostringstream ss;

		ss << mSQLQuery.eventInsertFootage;

		for (size_t j = begin; j < end; j++)
		{
			auto& r = infoList.at(j);

		//	auto tm = localtime(&r.timePoint);

			ss << "('" << r.timestampStr << "'," << r.timestampMs << ',' << r.eventId << ",'" << r.name << "'," << r.footageIndex << ")";
		//	ss << "('" << std::put_time(tm, "%Y-%m-%d %H:%M:%S") << "'," << r.timestampMs << ',' << r.eventId << ",'" << r.name << "')";
		//	ss << "(NOW()," << r.eventId << ",'" << r.name << "')";

			if (j != end - 1)
				ss << ',';
		}

		Database::Query query(*mMain.DatabasePtr);

		if (!query.Exec(ss.str()))
			LOG_ERROR(Log::Channel::Events, "SQL query failed for \"EventManager::HandleQueuedFootageNotices\"!");
	}
#endif
}

// Returns the unique (Database related) id of the event.
EventId EventManager::AuthenticateSession(EventSessionId sessionId, U32 userId, U32 siteId, U32 cameraId)
{
	auto eventId = this->WriteEventStart(userId, siteId, cameraId);

	mSessionUserIds.at(sessionId) = userId;
	mSessionSiteIds.at(sessionId) = siteId;
	mSessionCameraIds.at(sessionId) = cameraId;
	mSessionEventIds.at(sessionId) = eventId;

	return eventId;
}

EventId EventManager::WriteEventStart(U32 userId, U32 siteId, U32 cameraId)
{
	using namespace Database::Table;

	std::ostringstream ss;

	if (mSQLQuery.eventInsert.empty())
	{
		ss << "INSERT INTO " << Events::TableName
			<< " ("	<< Events::UserId
			<< ','	<< Events::SiteId
			<< ','	<< Events::CameraId
			<< ','	<< Events::CreatedAt
			<< ") VALUES ('";

		mSQLQuery.eventInsert = ss.str();

		// https://stackoverflow.com/questions/624260/how-to-reuse-an-ostringstream/624291#624291
		ss.clear();
		ss.seekp(0);
	}

	ss << mSQLQuery.eventInsert
		<< userId << "\',\'" 
		<< siteId << "\',\'" 
		<< cameraId << "\',NOW()" 
		<< ')';

	Database::Query query(*mMain.DatabasePtr);

	if (!query.Exec(ss.str()))
	{
		LOG_ERROR(Log::Channel::Events, "SQL query failed for \"FTPServer::WriteEventStart\"!");
		return 0;
	}

	return query.LastInsertId();
}

void EventManager::WriteEventEnd(EventId eventId)
{
	if (mSQLQuery.eventUpdate.empty())
	{
		std::ostringstream ss;

		ss	<< "UPDATE "		<< Database::Table::Events::TableName
			<< " SET "			<< Database::Table::Events::EndedAt
			<< "=NOW() WHERE "	<< Database::Table::Events::Id << '=';

		mSQLQuery.eventUpdate = ss.str();
	}

	Database::Query query(*mMain.DatabasePtr);

	query.Exec(mSQLQuery.eventUpdate + std::to_string(eventId));
}
