
#include "PCH.hpp"

#include "Main.hpp"
#include "Utils.hpp"
#include "Socket.hpp"

#include "API/APIServer.hpp"

#include "Database/Database.hpp"
#include "Database/DatabaseQuery.hpp"
#include "Database/DatabaseTables.hpp"

#include "EventManager.hpp"

#include <string.h> // strtok

#ifndef PLATFORM_WINDOWS
#include <sys/socket.h>
#include <netinet/in.h> // sockaddr_in
#include <arpa/inet.h>	// inet_ntoa
#endif

/*
	tavo-serverio-ip:portas/arm?camid=[cameraID]
	tavo-serverio-ip:portas/arm?siteid=[siteID]
*/

APIServer::APIServer(Main& rApp)
	: mMain(rApp)
{ }

APIServer::~APIServer()
{
	Socket::Close(mServerSocket);
}

bool APIServer::Start(U16 port)
{
	try
	{
		mServerSocket = Socket::CreateServer(port, 32);
	}
	catch (const Exception& e)
	{
		LOG_ERROR(Log::Channel::FTP, e.GetText());
		return false;
	}

	LOG_MESSAGE(Log::Channel::FTP, "API Server started. (Port %d)", port);
	return true;
}

enum class HTTPElement
{
	GET,
	Unknown,
};

struct HTTPElementType
{
	const char* name;
	HTTPElement type;
};

auto GetHTTPElementType(char* pBuffer, ssize_t size) -> HTTPElement
{
	static HTTPElementType HTTPElementInfo[] =
	{
		{ "GET", HTTPElement::GET },
		{ nullptr, HTTPElement::Unknown },
	};

	for (HTTPElementType* p = HTTPElementInfo; p->name; p++)
	{
		ssize_t i = 0;

		for (i = 0; i < size; ++i)
		{
			if (p->name[i] != pBuffer[i])
				break;
		}

		if (p->name[i] == '\0')
			return p->type;
	}

	return HTTPElement::Unknown;
}

void APIServer::Update()
{
	this->HandleNewConnection();

	if (mClientSockets.empty())
		return;

	const auto numClients = mClientSockets.size();
	const auto currentTP = std::chrono::steady_clock::now();

	// Should be enough for reading a single HTTP header. 
	static char readBuffer[1024];

	for (size_t i = 0; i < numClients; ++i)
	{
		auto& rSocketId = mClientSockets.at(i);

		// Ignore free slots.
		if (rSocketId == INVALID_SOCKET)
			continue;

		

		//=============================================
		// Handle reads.
		{
			auto bytesReaded = recv(rSocketId, readBuffer, sizeof(readBuffer), 0/*MSG_PEEK*/);

			if (bytesReaded > 0)
			{
//				{
//					std::fstream file("APIrequest.raw", std::ios::out | std::fstream::binary);
//					file.write(readBuffer, bytesReaded);
//				}

				// Don't timeout.
			//	mClientTimePoints.at(i) = currentTP;

				const char* pLineEnd = "\r\n";

				char* pToken;
				char* pRest = readBuffer;

#if PLATFORM_WINDOWS
				while ((pToken = strtok_s(pRest, pLineEnd, &pRest)))
#else
				while ((pToken = strtok_r(pRest, pLineEnd, &pRest)))
#endif
				{
					const auto length = strlen(pToken);
					const auto type   = GetHTTPElementType(pToken, length);

					// For now we only care about "GET"...
					if (type == HTTPElement::GET)
					{
						// SAMPLE: "/arm?camid=4 HTTP/1.1"
						String cgi(Utils::StripText(pToken, length, 4));

						// Get rid of " HTTP/1.1"
						auto pos = cgi.find_first_of(' ');
						if (pos != String::npos)
							cgi = cgi.substr(0, pos + 1);

						this->HandleCGI(static_cast<APIClientId>(i), cgi);
						break;
					}
				}

//				std::fstream testFile("apiRequest.raw", std::ios::out | std::ios::binary);
//				testFile.write(readBuffer, bytesReaded);

			}
		}

		//=============================================
		// Handle timeouts.
		const auto& clientTP = mClientTimePoints.at(i);

		auto seconds = std::chrono::duration_cast<std::chrono::seconds>(currentTP - clientTP).count();

		if (seconds > ClientTimeout)
		{
			LOG_WARNING(Log::Channel::API, "Client timeout. (size: %d)", numClients);

			Socket::Close(rSocketId);

			// Allow slot to be re-used.
			mClientReleasedIds.push_back(static_cast<APIClientId>(i));
		}
	}
}

void APIServer::HandleNewConnection()
{
	sockaddr_in from;
	socklen_t	fromSize = sizeof(sockaddr_in);

	auto clientSocket = accept(mServerSocket, (sockaddr*)&from, &fromSize);

	if (clientSocket == INVALID_SOCKET)
		return;

	// Sockets are "blocking" by default, so set it to a "non-blocking".
	Socket::SetNonBlocking(clientSocket);

	{
		String	clientIPString = inet_ntoa(from.sin_addr);
		U16	clientPort = ntohs(from.sin_port);

		LOG_MESSAGE(Log::Channel::API, "API connection from: %s:%d", clientIPString.c_str(), clientPort);
	}

	this->AddClient(clientSocket);
}

// SAMPLE: 
// "/arm?camid=4"
// "/arm?camid=4&camid=7&siteid=12" (Support for multiple cameras and sites)
auto APIServer::HandleCGI(APIClientId clientId, const String& rCGI) -> void
{
	if (rCGI.find("/arm?") != String::npos)
	{
		this->HandleCGI_ArmState(rCGI.substr(5), true); // Get rid of "/arm?".
	}
	else if (rCGI.find("/disarm?") != String::npos)
	{
		this->HandleCGI_ArmState(rCGI.substr(8), false); // Get rid of "/arm?".
	}
	else
	{
		LOG_WARNING(Log::Channel::API, "Received the unknown CGI request: \"%s\"!", rCGI.c_str());
	}

	Socket::Close(mClientSockets.at(clientId));
}

auto APIServer::HandleCGI_ArmState(const String& rCGI, bool isArmed) -> void
{
	if (isArmed)
		LOG_MESSAGE(Log::Channel::API, "Received ARM request: %s", rCGI.c_str());
	else
		LOG_MESSAGE(Log::Channel::API, "Received DISARM request: %s", rCGI.c_str());

	std::stringstream ss(rCGI);
	std::string token;

	// Support for multiple cameras and sites
	// SAMPLE: "/arm?camid=4&camid=7&siteid=12"
	while (std::getline(ss, token, '&'))
	{
		auto pos = token.find_first_of('=');

		if (pos == std::string::npos)
			continue;

		const auto key(token.substr(0, pos)); // "camid" or "siteid"
		const auto value(token.substr(pos + 1));

		// "Camera" or "Site" database id value.
		U32 id;

		if (!Utils::StringTo(value.c_str(), id))
		{
			LOG_ERROR(Log::Channel::API, "Failed to get id from key %s value %s!", key.c_str(), value.c_str());
			continue;
		}

		if (id == 0)
			continue;

		// Determine if we're dealing with "camid" or "siteid".
		if (rCGI.find("camid") != String::npos)
		{
			this->HandleCameraArmState(id, isArmed);
		}
		else if (rCGI.find("siteid") != String::npos)
		{
			// List of database camera id's that are associated with the specific site id.
			Vector<U32> list;

			if (!this->GetDatabaseFTPCamerasArmStatesBySite(id, list))
				continue;

			// Update the entire site's is_armed state.
			this->SetDatabaseFTPCamerasSiteArmState(id, isArmed);

			for (auto& rCamera : list)
				this->HandleCameraArmState(rCamera, isArmed);
		}
		else
		{
			LOG_ERROR(Log::Channel::API, "Can't process ARM/DISARM request! (Unknown key: %s)", key.c_str());
			continue;
		}
	}
}

auto APIServer::HandleCameraArmState(U32 cameraId, bool isArmed) -> void
{
	String hashKey;
	bool isAlreadyArmed;

	if (!this->GetDatabaseFTPCameraHashKey(cameraId, hashKey, isAlreadyArmed))
		return;

	if (isArmed && isAlreadyArmed)
	{
		LOG_WARNING(Log::Channel::API, "Camera (id %u) is already armed!", cameraId);
		return;
	}
	else if (!isArmed && !isAlreadyArmed)
	{
		LOG_WARNING(Log::Channel::API, "Camera (id %u) is already disarmed!", cameraId);
		return;
	}

	this->SetDatabaseFTPCameraArmState(cameraId, isArmed);

	// Check EventManager to see if we already have a session open 
	// and if we need to arm or disarm the device at runtime.
	EventSessionId eventSessionId;

	if (mMain.EventManagerPtr->HasSession(hashKey, &eventSessionId))
		mMain.EventManagerPtr->SetSessionArmedState(eventSessionId, isArmed);
}

auto APIServer::AddClient(SocketId socketId) -> APIClientId
{
	APIClientId id;

	if (mClientReleasedIds.empty())
	{
		id = mClientIdCounter++;

		if (mClientSockets.size() <= id)
		{
			const std::size_t newSize = id + 1;

			mClientSockets.resize(newSize);
			mClientTimePoints.resize(newSize);
		}
	}
	else
	{
		id = mClientReleasedIds.back();
		mClientReleasedIds.pop_back();
	}

	mClientSockets.at(id) = socketId;
	mClientTimePoints.at(id) = std::chrono::steady_clock::now();

	return id;
}

auto APIServer::GetDatabaseFTPCamerasArmStatesBySite(U32 siteId, Vector<U32>& rList) -> bool
{
	using namespace Database::Table;

	std::ostringstream ss;

	ss	<< "SELECT "	<< Cameras::Id
		<< " FROM "		<< Cameras::TableName
		<< " WHERE "	<< Cameras::SiteId
		<< '='			<< siteId;

	Database::Query query(*mMain.DatabasePtr);

	if (!query.Exec(ss.str()))
	{
		LOG_ERROR(Log::Channel::API, "Failed to get cameras for site id: %u", siteId);
		return false;
	}

	const auto numResults = query.NumResults();

	if (numResults == 0)
	{
		LOG_WARNING(Log::Channel::API, "Site (id %u) contains no cameras!", siteId);
		return false;
	}

	rList.reserve(numResults);

	while (query.Next())
		rList.emplace_back(query.ValueU32(0));

	return true;
}

auto APIServer::GetDatabaseFTPCameraHashKey(U32 cameraId, String& rHashKey, bool& rIsArmed) -> bool
{
	using namespace Database::Table;

	std::ostringstream ss;

	ss	<< "SELECT "	<< CameraFTP::TableName << '.' << CameraFTP::Username
		<< ','			<< CameraFTP::TableName << '.' << CameraFTP::Password
		<< ','			<< Cameras::TableName	<< '.' << Cameras::IsArmed
		<< " FROM "		<< CameraFTP::TableName
		<< " JOIN "		<< Cameras::TableName	
		<< " ON "		<< Cameras::TableName	<< '.' << Cameras::Id << '=' << CameraFTP::TableName << '.' << CameraFTP::CameraId
		<< " WHERE "	<< CameraFTP::TableName << '.' << CameraFTP::CameraId 
		<< '='			<< cameraId;
/*
	SELECT 
	`vq_camera_ftp`.`username`,
	`vq_camera_ftp`.`password`,
	`vq_cameras`.`is_armed` 
	FROM `vq_camera_ftp` 
	JOIN `vq_cameras` ON `vq_cameras`.`id`=`vq_camera_ftp`.`camera_id` 
	WHERE `vq_camera_ftp`.`camera_id`=6
*/
	Database::Query query(*mMain.DatabasePtr);

	if (!query.Exec(ss.str()))
	{
		LOG_ERROR(Log::Channel::API, "Failed to get camera details for camera id: %u", cameraId);
		return false;
	}

	if (!query.Next())
	{
		LOG_WARNING(Log::Channel::API, "No results for camera id: %u", cameraId);
		return false;
	}

	rHashKey = String(query.Value(0)) + String(query.Value(1));
	rIsArmed = query.ValueBool(2);

	return true;
}

auto APIServer::SetDatabaseFTPCameraArmState(U32 cameraId, bool isArmed) -> void
{
	std::ostringstream ss;

	ss	<< "UPDATE "	<< Database::Table::Cameras::TableName
		<< " SET "		<< Database::Table::Cameras::IsArmed << '=' << isArmed
		<< " WHERE "	<< Database::Table::Cameras::Id << '=' << cameraId
		<< " AND "		<< Database::Table::Cameras::DeletedAt << " IS NULL"; 

	Database::Query query(*mMain.DatabasePtr);

	query.Exec(ss.str());
}

// Sample: "UPDATE `vq_sites` SET `is_armed`=0 WHERE `id`=1 AND `deleted_at` IS NULL"
auto APIServer::SetDatabaseFTPCamerasSiteArmState(U32 siteId, bool isArmed) -> void
{
	std::ostringstream ss;

	ss	<< "UPDATE "	<< Database::Table::Sites::TableName
		<< " SET "		<< Database::Table::Sites::IsArmed << '=' << isArmed
		<< " WHERE "	<< Database::Table::Sites::Id << '=' << siteId
		<< " AND "		<< Database::Table::Sites::DeletedAt << " IS NULL";

	Database::Query query(*mMain.DatabasePtr);

	query.Exec(ss.str());
}
