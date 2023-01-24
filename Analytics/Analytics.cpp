
#include <PCH.hpp>

#include "Main.hpp"
#include "Config.hpp"
#include "Socket.hpp"

#include "Database/Database.hpp"
#include "Database/DatabaseQuery.hpp"
#include "Database/DatabaseTables.hpp"

#include "Analytics/Analytics.hpp"

#include "CGI/CGIManager.hpp"

#include "ThreadPool.hpp"

#include "TinyXML2/tinyxml2.h"

#ifndef PLATFORM_WINDOWS
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <netinet/in.h> // sockaddr_in
#include <arpa/inet.h>	// inet_pton
#endif

/*
	MINTYS

	Analytics socket'ai sukasi "Analytics" thread'e. (t.y. prisijungimas ir rezultatu readinimas)
	Footage siuntimas analizavimui atliekamas thread pool - atskiru threadu pagalba.

	Worker thread'as nuskaito faila is HDD ir nusiuncia ji i pacia Analytics programa.
	
	Vienas prisijungimas atspindi viena event sessija. 
	Event sessijai pasibaigus - atsijungiam nuo Analytics sistemos.
*/

Analytics::Analytics(Main& rApp, const Database::Info& rDBInfo, const String& rServerAddress, U16 serverPort, U16 connectTimeoutSec)
	: mMain(rApp)
	, mConnectTimeoutSec(connectTimeoutSec)
	, mServerPort(serverPort)
	, mDBInfo(rDBInfo)
	, mServerAddress(rServerAddress)
{
	mThreadPtr = std::make_unique<std::thread>(&Analytics::ThreadProc, this);

	mReadBuffer.reserve(4096);

	{
		std::ostringstream ss;

		ss << "INSERT INTO " << Database::Table::AnalyticsXML::TableName
			<< " (" << Database::Table::AnalyticsXML::CreatedAt
			<< ','	<< Database::Table::AnalyticsXML::EventFootageId
			<< ','	<< Database::Table::AnalyticsXML::Data
			<< ") VALUES (NOW(),'";

		mSQLQuery.analyticsInsertSQL = ss.str();
	}

	{
		std::ostringstream ss;

		ss << "INSERT INTO " << Database::Table::Analytics::TableName
			<< " (" << Database::Table::Analytics::EventFootageId
			<< ',' << Database::Table::Analytics::Frame
			<< ',' << Database::Table::Analytics::Type
			<< ',' << Database::Table::Analytics::Probability
			<< ',' << Database::Table::Analytics::X
			<< ',' << Database::Table::Analytics::Y
			<< ',' << Database::Table::Analytics::W
			<< ',' << Database::Table::Analytics::H
			<< ") VALUES ";

		mSQLQuery.analyticsInsertSQLParsed = ss.str();
	}

	// update vq_cameras_detections set count=count+1 where camera_id=[cameraID] and name='person'
	{
		std::ostringstream ss;

		ss	<< "UPDATE "	<< Database::Table::CameraDetections::TableName
			<< " SET "		<< Database::Table::CameraDetections::Count
			<< '='			<< Database::Table::CameraDetections::Count
			<< '+';

		mSQLQuery.analyticsUpdateStats = ss.str();
	}
}

Analytics::~Analytics()
{
	LOG_MESSAGE(Log::Channel::Analytics, "Analytics system is stopping.");

	mIsStopRequested = true;
	mThreadPtr->join();

	LOG_MESSAGE(Log::Channel::Analytics, "Analytics system is stopped.");
}

void Analytics::AddEvent(EventId eventId, U32 cameraId, U8 personThreshold, const String& rFootagePath)
{
	LOG_MESSAGE(Log::Channel::Analytics, "Analytics starts new session. (EventId: %" PRIu64 ", CameraId: %u, PersonThreshold: %u, Path: '%s')", eventId, cameraId, personThreshold, rFootagePath.c_str());

	SocketId socketId = INVALID_SOCKET;

	try
	{
		socketId = Socket::Create();

		Socket::SetNonBlocking(socketId);

		sockaddr_in addr{};

		addr.sin_family = AF_INET;
		addr.sin_port = htons(mServerPort);

		if (inet_pton(addr.sin_family, mServerAddress.c_str(), &addr.sin_addr) <= 0)
			throw ExceptionVA("Invalid address: %s:%d", mServerAddress.c_str(), mServerPort);

		LOG_MESSAGE(Log::Channel::Analytics, "Connecting to the Analytics: %s:%d", mServerAddress.c_str(), mServerPort);

		// NOTE:
		// "O_NONBLOCK is set for the file descriptor for the socket and the connection cannot be immediately established; 
		//  the connection shall be established asynchronously"
		if (connect(socketId, (struct sockaddr *)&addr, sizeof(addr)) == SOCKET_ERROR)
		{
			int errorCode = Socket::GetErrorCode();
#if PLATFORM_WINDOWS
			if (errorCode != WSAEWOULDBLOCK)
#else
			if (errorCode != EINPROGRESS)
#endif
				throw ExceptionVA("Failed for \"connect\"! (Error: %s, Code: %d)", Socket::GetErrorString(errorCode), errorCode);
		}
	}
	catch (const Exception& e)
	{
		LOG_ERROR(Log::Channel::Analytics, e.GetText());
		Socket::Close(socketId);
		return;
	}

	AnalyticsSessionId id;

	if (mAnalyticsReleasedIds.empty())
	{
		id = mAnalyticIdCounter++;

		if (mAnalyticsSockets.size() <= id)
		{
			const std::size_t newSize = id + 1;

			mAnalyticsSockets.resize(newSize);
			mAnalyticsCameraIds.resize(newSize);
			mAnalyticsThresholds.resize(newSize);
			mAnalyticsEvents.resize(newSize);
			mAnalyticsEventPaths.resize(newSize);
			mAnalyticsStatus.resize(newSize);
			mAnalyticsTimePoints.resize(newSize);
			mAnalyticsUniqueId.resize(newSize);
		}
	}
	else
	{
		id = mAnalyticsReleasedIds.back();
		mAnalyticsReleasedIds.pop_back();
	}

	mAnalyticsSockets.at(id) = socketId;
	mAnalyticsCameraIds.at(id) = cameraId;
	mAnalyticsThresholds.at(id) = personThreshold;
	mAnalyticsEvents.at(id) = eventId;
	mAnalyticsEventPaths.at(id) = rFootagePath;
	mAnalyticsStatus.at(id) = SatusFlags::Connecting;
	mAnalyticsTimePoints.at(id) = std::chrono::steady_clock::now();

	// Make sure eventMap element is created for the appropriate eventId.
//	mEventMap[eventId].isDone = false;
	mEventMap[eventId].sessionId = id;
}

void Analytics::EndEvent(EventId eventId)
{
#if 1
	auto it = mEventMap.find(eventId);
	if (it == mEventMap.end())
	{
		LOG_ERROR(Log::Channel::Analytics, "Analytics session can't end! (Event id: %" PRIu64 " not found)", eventId);
		return;
	}

	auto& rSession = it->second;

	const auto queueSize = rSession.footageQueue.size();
	
	// If analytics session was not marked as "DONE" and there are still some queued frames - defer the closure.
	// (So even though EVENT session might be closed, the defered ANALYTICS session might still wait for some frames to be analized)
	if (!rSession.isDone && queueSize > 0)
	{
		LOG_MESSAGE(Log::Channel::Analytics, "Analytics session (id: %u, Event id: %" PRIu64 ") still contains %u queued frames...", rSession.sessionId, eventId, queueSize);

		// TODO: HANDLE THE SITUATION.
	}
	else
	{
		LOG_MESSAGE(Log::Channel::Analytics, "Analytics session (id: %u, Event id: %" PRIu64 ") ended.", rSession.sessionId, eventId);

		ReleaseSession(rSession.sessionId);

		mEventMap.erase(it);
	}
#else

	LOG_MESSAGE(Log::Channel::Analytics, "Analytics session ended. Event id: %" PRIu64, eventId);

	const auto numSessions = static_cast<AnalyticsSessionId> (mAnalyticsEvents.size());

	for (AnalyticsSessionId i = 0; i < numSessions; ++i)
	{
		if (mAnalyticsEvents.at(i) == eventId)
		{
			ReleaseSession(i);
			break;
		}
	}

	mEventMap.erase(eventId);
#endif
}

// Add's information about the event's footage that should be shaduled for processing as soon as possible.
// Analytics manager works on a separate thread.
// All the queued footage will be handled by the "HandleQueuedFootageList".
void Analytics::AddFootage(EventId eventId, EventFootageId eventFootageId, const String& rName)
{
	mFootageMutex.lock();
	mFootageQueue.emplace_back(eventId, eventFootageId, rName);
	mFootageMutex.unlock();
}

void Analytics::ReleaseSession(AnalyticsSessionId id)
{
	Socket::Close(mAnalyticsSockets.at(id));

	mAnalyticsReleasedIds.emplace_back(id);
	mAnalyticsEvents.at(id) = 0;
	mAnalyticsStatus.at(id) = SatusFlags::Free;

//	mAnalyticsSessions.at(id).reset();
}

bool Analytics::HandleConnect(AnalyticsSessionId id, const TimePoint& rCurrentTP)
{
	const auto socketId = mAnalyticsSockets.at(id);

	fd_set fdSet;
	FD_ZERO(&fdSet);
	FD_SET(socketId, &fdSet);

	timeval	timeout = { 0, 0 }; // Don't block.

	// TODO: http://beesbuzz.biz/code/5739-The-problem-with-select-vs-poll
	// NOTE: 
	// Using "send check". If we can "send" - we're connected.
	int result = select(socketId + 1, 0, &fdSet, 0, &timeout);
	if (result > 0)
	{
		const auto eventId = mAnalyticsEvents.at(id);
		LOG_MESSAGE(Log::Channel::Analytics, "Analytics session (id: %u) connected with ANL server. (EventId: %" PRIu64 ")", id, eventId);
		mAnalyticsStatus.at(id) = SatusFlags::Connected;
		mAnalyticsTimePoints.at(id) = rCurrentTP;
		return true;
	}
	else if (result < 0)
	{
		// TODO: Proper cleanup?
		int errorCode = Socket::GetErrorCode();
		LOG_ERROR(Log::Channel::Analytics, "Analytics socket failed for \"select\"! (Error code: %d)", errorCode);
		ReleaseSession(id);
	}
	else
	{
		// Check for socket connect time-out.
		auto seconds = std::chrono::duration_cast<std::chrono::seconds>(rCurrentTP - mAnalyticsTimePoints.at(id)).count();

		if (seconds > mConnectTimeoutSec)
		{
			const auto eventId = mAnalyticsEvents.at(id);
			LOG_ERROR(Log::Channel::Analytics, "Analytics socket failed to connect to: %s:%d (Timeout %d sec, EventId: %" PRIu64 ")", mServerAddress.c_str(), mServerPort, mConnectTimeoutSec, eventId);
			ReleaseSession(id);
		}
	}

	return false;
}

void Analytics::HandleRead(AnalyticsSessionId id)
{
	const auto socketId = mAnalyticsSockets.at(id);

	static char readBuffer[1024];

	// Check if "handshake" was made.

	// If so, try and read any available response data.

	// Socket is connected, but actual analytics session was not yet initialized.
	// So try and read the initial data thad is sent to us by the Analytics server.
	if (mAnalyticsStatus.at(id) == SatusFlags::Connected)
	{
		auto bytesAvailable = recv(socketId, readBuffer, sizeof(readBuffer), MSG_PEEK);

		if (bytesAvailable < 6)
			return;

		const U32 clientId = *reinterpret_cast<const U32*>(readBuffer);
		const U16 protocolVersion = *reinterpret_cast<const U16*>(readBuffer + 4);

		LOG_DEBUG(Log::Channel::Analytics, "Analytics new session HANDSHAKE. (Id: %u, Client id: %u, protocol-version: %u)", id, clientId, protocolVersion);

		recv(socketId, readBuffer, sizeof(readBuffer), 0);

		mAnalyticsStatus.at(id)   = SatusFlags::Handshake;
		mAnalyticsUniqueId.at(id) = clientId;
	}
	else if (mAnalyticsStatus.at(id) == SatusFlags::Ready)
	{
#if PLATFORM_WINDOWS
		u_long bytesAvailable = 0;
		if (ioctlsocket(socketId, FIONREAD, &bytesAvailable) == SOCKET_ERROR)
#else
		size_t bytesAvailable = 0;
		if (ioctl(socketId, FIONREAD, (u_long*)&bytesAvailable) == SOCKET_ERROR)
#endif
		{
			LOG_ERROR(Log::Channel::Analytics, "Failed for \"ioctl\"!");
		}

//		printf("bytesAvailable[Event:%" PRIu64 "]: %d\n", mAnalyticsEvents.at(id), bytesAvailable);

		constexpr auto HeaderSize = 6;
/*
#pragma pack(push, 1)
		struct Header
		{
			quint8  mark;
			quint8	type;	// enum MessageType
			quint32	size;
		};
#pragma pack(pop)
*/

		// Check if we have enough bytes to read at least the header.
		if (bytesAvailable < HeaderSize)
			return;

		recv(socketId, readBuffer, sizeof(readBuffer), MSG_PEEK);

//		auto messageType = *reinterpret_cast<const uint8_t*>(&readBuffer[1]); // Skip the "mark" (1 byte)
		auto messageSize = *reinterpret_cast<const U32*>(&readBuffer[2]);

		// Check if we have enough bytes to read the whole response.
		if (bytesAvailable < messageSize)
			return;

		// If we're here, we can expect that the whole response data is available to be read.

		// Prepare the "ReadBuffer". 
		// (Note, the capacity is never reduced when resizing to smaller size)
		mReadBuffer.resize(messageSize);

		if (!Socket::Read(socketId, mReadBuffer.data(), mReadBuffer.size()))
		{
			LOG_ERROR(Log::Channel::Analytics, "Failed while reading respnse for event id: %" PRIu64, mAnalyticsEvents.at(id));
			return;
		}

		// TODO: 
		// Remove eventId? Leave just String..?
		const auto cameraId = mAnalyticsCameraIds.at(id);
		const EventId eventId = mAnalyticsEvents.at(id);

		mResultQueue.push({ id, cameraId, eventId, 0, String(mReadBuffer.data() + HeaderSize + 4, mReadBuffer.size() - (HeaderSize + 4))});

#if 0
		static int resultCounter;
		std::fstream file;
		file.open("XML_" + std::to_string(resultCounter++) + ".xml", std::ios::out | std::fstream::binary);

		if (!file.is_open())
			LOG_ERROR(Log::Channel::Analytics, "Failed to write Analytics file: %s", strerror(Socket::GetErrorCode()));

		file.write(mReadBuffer.data() + HeaderSize + 4, mReadBuffer.size() - (HeaderSize + 4));
#endif
	}
}

void Analytics::HandleSend(AnalyticsSessionId id)
{
	if (mAnalyticsStatus.at(id) == SatusFlags::Handshake)
	{
#pragma pack(push, 1)
		struct Handshake
		{
			U32 clientId = 0;
			U16 type = 0;			// TODO: Enumeration: Default, person, car, plant, etc.
			U16 quality = 0;		// STREAM_QUALITY_LOW = 0, STREAM_QUALITY_MED,  STREAM_QUALITY_HIGH
			U32 streamTypeA = 1;	// STREAM_TYPE_UNKNOWN = 0, STREAM_TYPE_JPEG = 1, STREAM_TYPE_VIDEO = 2,

			// Header.
			uint8_t mark = 7;
			uint8_t messageType = 1; // NET_MSG_TYPE_HELLO
			U32 size = 0;

			U16 protocolVersion = 0;
			int64_t	alarmId = 0;
			uint8_t threshold = 20; // Default.
			uint8_t streamTypeB = 1; // STREAM_TYPE_JPEG = 1

			U16 nameLength = 16;
			char name[16]{ "Test\0" };

			uint8_t findLicensePlates = 0;
		} handshake;
#pragma pack(pop)

		handshake.clientId = mAnalyticsUniqueId.at(id);

		handshake.size = 37;
		handshake.protocolVersion = 5;
		handshake.alarmId = mAnalyticsEvents.at(id);

		try
		{
			Socket::Send(mAnalyticsSockets.at(id), (char*)&handshake, sizeof(Handshake));
		}
		catch (const Exception& e)
		{
			LOG_ERROR(Log::Channel::Analytics, "Analytics::HandleSend[HANDSHAKE]: %s\n", e.GetText());
			ReleaseSession(id);
			return;
		}

		mAnalyticsStatus.at(id) = SatusFlags::Ready;
	}
/*
	else if (mAnalyticsStatus.at(id) == SatusFlags::Ready)
	{
		// TODO
	}
*/
}

void Analytics::ThreadProc()
{
	LOG_MESSAGE(Log::Channel::Analytics, "Analytics thread started.");
	LOG_MESSAGE(Log::Channel::Analytics, "Analytics server: %s:%d", mServerAddress.c_str(), mServerPort);

	try
	{
		LOG_MESSAGE(Log::Channel::Analytics, "Connecting to the Database (%s:%d)", mDBInfo.hostname.c_str(), mDBInfo.port);

		Database::Connection database;

		if (!database.Connect(mDBInfo, 7, 3))
			return;

		while (!mIsStopRequested)
		{
			const auto currentTP = std::chrono::steady_clock::now();
			const auto numSessions = static_cast<AnalyticsSessionId> (mAnalyticsSockets.size());

			for (AnalyticsSessionId id = 0; id < numSessions; ++id)
			{
				if (mAnalyticsStatus.at(id) == SatusFlags::Free)
					continue;

				if (mAnalyticsStatus.at(id) == SatusFlags::Connecting)
				{
					if (!HandleConnect(id, currentTP))
						continue; // If session is not connected, there is no point of continuing.
				}

				HandleRead(id);

				HandleSend(id);
			}

			WriteQueuedResults(database);

			HandleQueuedFootageList();

			HandleQueuedFootageMap();

			// TMEP?
			std::this_thread::sleep_for(std::chrono::milliseconds(100));
		}
	}
	catch (const Exception& e)
	{
		LOG_ERROR(Log::Channel::Analytics, "Analytics: %s\n", e.GetText());
	}

	LOG_MESSAGE(Log::Channel::Analytics, "Analytics thread stopped.");
}

void SendFootageTask(SocketId socketId, String path, String name, EventFootageId eventFootageId, std::shared_ptr<std::atomic_bool> footageSendAllowedPtr)
{
	const String fileName(path + name);
	Vector<char> fileBuffer;

	try
	{
		// Read footage from file.
		{
			std::ifstream fileStream(fileName, std::ios::in | std::ifstream::binary);

			if (!fileStream.is_open())
				throw ExceptionVA("Failed to open: \"%s\".", fileName.c_str());

			// Determine the file size.
			fileStream.seekg(0, std::ios_base::end);

			auto length = fileStream.tellg();

			if (length == 0)
				throw ExceptionVA("No data for: \"%s\".", fileName.c_str());

			// Pre-allocate the buffer for data storage.
			fileBuffer.resize(static_cast<size_t> (length));

			fileStream.seekg(0, std::ios_base::beg);
			fileStream.read(&fileBuffer[0], length);
		}

	#pragma pack(push, 1)
		struct Frame
		{
			// Header.
			uint8_t mark = 7;
			uint8_t type = 2; // NET_MSG_TYPE_FRAME
			U32 size = 0;

			U32 fileId = 777;
			U32 payloadSize = 0;

			// ... Toliau seka payloadas/image'as.
		} mFrame;
	#pragma pack(pop)

		mFrame.payloadSize = static_cast<U32>(fileBuffer.size());
		mFrame.size = static_cast<U32>(sizeof(Frame)) + mFrame.payloadSize;

		// TODO:
		// "fileId" buvo skirtas video failams, atspingi video faile esancio kadru numeri.
		// Bet kol kas video failu nepalaikome, o mums reikia perduoti "eventFootageId"

		// IMPORTANT:
		// fileId is 32 bits, eventFootageId is 64 bits of size.
		mFrame.fileId = static_cast<U32> (eventFootageId);

		Socket::Send(socketId, (char*)&mFrame, sizeof(Frame));
		Socket::Send(socketId, fileBuffer.data(), fileBuffer.size());
	}
	catch (const Exception& e)
	{
		LOG_ERROR(Log::Channel::Analytics, "Analytics::SendFootageTask: %s", e.GetText());
	}

	*footageSendAllowedPtr = true;
}

// Checks if there is any queued footage that needs to be mapped to the event queue.
void Analytics::HandleQueuedFootageList()
{
	std::lock_guard<std::mutex> lock(mFootageMutex);

	if (mFootageQueue.empty())
		return;

	for (auto& r : mFootageQueue)
	{
		auto it = mEventMap.find(r.eventId);
		if (it == mEventMap.end())
		{
			// Probably "Analytics::EndEvent" was called while still having some queued footage...
			// TODO: I should still analize all the queued footage...!
			LOG_ERROR(Log::Channel::Analytics, "Analytics event map is missing event id: %" PRIu64 ", can't process event footage id: %" PRIu64, r.eventId, r.eventFootageId);
			continue;
		}

		auto& rSession = it->second;

		// If "person" was detected with the appropriate threshold,
		// We will stop sending all other event associated footage to the analytics server.
		if (!rSession.isDone)
			rSession.footageQueue.push({ r.eventFootageId, r.name });
	}

	mFootageQueue.clear();
}

void Analytics::HandleQueuedFootageMap()
{
	if (mEventMap.empty())
		return;

	for (auto& r : mEventMap)
	{
		auto& rSession = r.second;

		if (rSession.footageQueue.empty())
			continue;

		const auto eventId = r.first;
		const auto sessionId = rSession.sessionId;

		if (mAnalyticsEvents.at(sessionId) != eventId)
		{
			LOG_ERROR(Log::Channel::Analytics, "Analytics session (id: %u) event id mismatch (%" PRIu64 " != %" PRIu64 ")", sessionId, mAnalyticsEvents.at(sessionId), eventId);
			continue;
		}

		if (mAnalyticsStatus.at(sessionId) == SatusFlags::Ready)
		{
			auto& rFrame = rSession.footageQueue.front();

			// 2019-06-06 FIX:
			// Only send one image per-socket, per-event.
			// Only separate event sessions are allowed to use a separate thread for sending image data.
			if (*rSession.footageSendAllowedPtr == false)
			{
				LOG_DEBUG(Log::Channel::Analytics, "Footage send delayed... (Still analyzing previous footage) [EventId: %" PRIu64 ", EventFootageId: %" PRIu64 "]: %s", eventId, rFrame.eventFootageId, rFrame.fileName.c_str());
			}
			else
			{
				LOG_DEBUG(Log::Channel::Analytics, "Analyzing footage [EventId: %" PRIu64 ", EventFootageId: %" PRIu64 "]: %s", eventId, rFrame.eventFootageId, rFrame.fileName.c_str());

				*rSession.footageSendAllowedPtr = false;

				mMain.ThreadPoolPtr->Enqueue(
					SendFootageTask,
					mAnalyticsSockets.at(sessionId),
					mAnalyticsEventPaths.at(sessionId),
					rFrame.fileName,
					rFrame.eventFootageId,
					rSession.footageSendAllowedPtr);

				rSession.footageQueue.pop();
			}
		}
		else
		{
			// Might still be trying to connect or Analytics server is taking long time to launch it's child.
			LOG_DEBUG(Log::Channel::Analytics, "Analytics is still connecting for session id: %u", sessionId);
		}
	}
}

void Analytics::WriteQueuedResults(Database::Connection& rDatabase)
{
	for (;;)
	{
		if (mResultQueue.empty())
			break;

		if (mIsStopRequested)
		{
			// TODO: Finish processing queued results?
			LOG_WARNING(Log::Channel::Analytics, "Analytics STOP requested while still holding %" PRIu64 " queued results!", mResultQueue.size());
			break;
		}

		// Return a reference to the first element in the queue.
		auto& rResult = mResultQueue.front();

		// If "person" was detected with the appropriate threshold,
		// We will stop sending all other event associated footage to the analytics server.

		// NOTE:
		// After "isDone" was set, we still might get some queued data from the analytics server, we can ignore those...
		auto it = mEventMap.find(rResult.eventId);
		if (it != mEventMap.end())
		{
			if (it->second.isDone)
				continue;
		}

		// "<Root incompleteResult="0" count="1" fileId="3295">"
		auto eventFootageId = WriteXMLParsedResults(rDatabase, rResult.sessionId, rResult.cameraId, rResult.eventId, rResult.name);

		WriteXML(rDatabase, eventFootageId, rResult.name);


		mResultQueue.pop();
	}
}

void Analytics::WriteXML(Database::Connection& rDatabase, EventFootageId eventFootageId, const String& rXML)
{
	std::ostringstream ss;

	ss  << mSQLQuery.analyticsInsertSQL << eventFootageId << "\',\'" << rXML << "\')";

	Database::Query query(rDatabase);

	query.Exec(ss.str());
}

EventFootageId Analytics::WriteXMLParsedResults(Database::Connection& rDatabase, AnalyticsSessionId sessionId, U32 cameraId, EventId eventId, const String& rXML)
{
	tinyxml2::XMLDocument xml;

	auto results = xml.Parse(rXML.c_str());

	if (results != tinyxml2::XML_SUCCESS)
	{
		LOG_ERROR(Log::Channel::Analytics, "Failed to parse the XML!");
		return 0;
	}

	auto pRootElement = xml.FirstChildElement("Root");
	if (!pRootElement)
	{
		LOG_ERROR(Log::Channel::Analytics, "XML root element not found!");
		return 0;
	}

	auto eventFootageId = static_cast<EventFootageId> (pRootElement->IntAttribute("fileId"));

	// IMPORTANT: 
	// The maximum number of rows in one VALUES clause is 1000 (TODO)
	std::ostringstream ss;
	ss << mSQLQuery.analyticsInsertSQLParsed;

	U32 frameIndex = 1;

	bool isFound = false;

	UnorderedMap<String, U32> statsMap;

	for (auto pResultElement = pRootElement->FirstChildElement(); pResultElement; pResultElement = pResultElement->NextSiblingElement()) 
	{
		int numObjects = pResultElement->IntAttribute("count");
		if (numObjects == 0)
			continue;

		int objectIndex = 0;

		for (auto pObjectElement = pResultElement->FirstChildElement(); pObjectElement; pObjectElement = pObjectElement->NextSiblingElement())
		{
			isFound = true;

			// TODO
			// "analyticsUpdateStats"
			{
/*
				// update vq_cameras_detections set count=count+1 where camera_id=[cameraID] and name='person'
				{
					std::ostringstream ss;

					ss << "UPDATE " << Database::Table::CameraDetections::TableName
						<< "SET " << Database::Table::CameraDetections::Count
						<< '=' << Database::Table::CameraDetections::Count
						<< '+';

					mSQLQuery.analyticsUpdateStats = ss.str();
*/
			}

			const String name(pObjectElement->Attribute("name"));
			const int probability(std::atoi(pObjectElement->Attribute("probability"))); // Database::Table::Analytics::Probability

			// If probability > personThreshold call CGI:
			// https://www.viquant.io/ui/inform-user.php?eventID=[EventID]
			if (name == "person")
			{
				const auto personThreshold = mAnalyticsThresholds.at(sessionId);

				if (probability > personThreshold)
				{
					std::ostringstream ss;

#if PLATFORM_WINDOWS
					ss	<< "/ui/inform-user.php?eventID=" << eventId
						<< "&eventFrameID=" << eventFootageId; // NOTE: ampersandas i single quotes required for "curl"
#else
					ss << "/ui/inform-user.php?eventID=" << eventId
						<< "'&'eventFrameID=" << eventFootageId; // NOTE: ampersandas i single quotes required for "curl"
#endif

					mMain.CGIManagerPtr->Add(ss.str());

					// If "person" was detected with the appropriate threshold,
					// We will stop sending all other event associated footage to the analytics server.
					auto it = mEventMap.find(eventId);
					if (it != mEventMap.end())
						it->second.isDone = true;
				}
			}

			auto& count = statsMap[name];
			count++;

			ss	<< "('"  << eventFootageId	// Database::Table::Analytics::EventFootageId
				<< "','" << frameIndex		// Database::Table::Analytics::Frame
				<< "','" << name			// Database::Table::Analytics::Type
				<< "','" << probability		// Database::Table::Analytics::Probability
				<< "','" << pObjectElement->Attribute("x")
				<< "','" << pObjectElement->Attribute("y")
				<< "','" << pObjectElement->Attribute("w")
				<< "','" << pObjectElement->Attribute("h")
				<< "')";

			if (objectIndex != numObjects - 1)
				ss << ',';

			objectIndex++;
		}

		frameIndex++;
	}

	// TODO: 
	// Optimize STATS
	// User might have only certain "objects" that he is interested in...
	if (!statsMap.empty())
	{
		for (auto& rStats : statsMap)
		{
			Database::Query query(rDatabase);

			std::ostringstream ss;

			ss	<< mSQLQuery.analyticsUpdateStats // "UPDATE `vq_cameras_detections` SET `count`=`count`+"
				<< rStats.second
				<< " WHERE "	<< Database::Table::CameraDetections::CameraId
				<< '='			<< cameraId
				<< " AND "		<< Database::Table::CameraDetections::Name
				<< "='"			<< rStats.first << "'";

			// SAMPLE:
			// "UPDATE `vq_cameras_detections` SET `count`=`count`+1 WHERE `camera_id`=6 AND `name`='person'"
			query.Exec(ss.str());
		}
	}

	if (isFound)
	{
		Database::Query query(rDatabase);

		query.Exec(ss.str());
	}

	return eventFootageId;
}
