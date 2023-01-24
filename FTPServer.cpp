
#include "PCH.hpp"

#include "Main.hpp"
#include "Utils.hpp"
#include "Socket.hpp"

#include "ThreadPool.hpp"

#include "Database/Database.hpp"
#include "Database/DatabaseQuery.hpp"
#include "Database/DatabaseTables.hpp"

#include "EventManager.hpp"

#include "Analytics/Analytics.hpp"

#include "FTPServer.hpp"
#include "FileNameParser.hpp"

#ifndef PLATFORM_WINDOWS
#include <unistd.h>

#include <sys/ioctl.h>
#include <sys/types.h> 
#include <sys/socket.h>
#include <netinet/in.h> // sockaddr_in
#include <arpa/inet.h>	// inet_ntoa
#endif 

#include <iostream>
#include <regex>

#include <string.h> // strerror

/*
	OBSERVATIONS


	DEVICE: AXIS (211A)
	Once connected, on event, camera can send multiple "PORT" and "STOR" commands (files)

	DEVICE: Panasonic BL-C10
	Camera's FTP settings contains the "Overwrite File" option, if selected, camera will send:
	1. "DELE nameold.jpg" (notice the prefix "old")
	2. "RNFR name.jpg"
	3. "RNTO nameold.jpg"
	4. "RNFR namenew.jpg" (notice the prefix "new")
	5. "RNTO name.jpg"
	In short: FTP should store only one "old" frame, and a current frame.
	In our system, we're ignoring this "Overwrite File" option and just adding received footage as usual.
	So we sould NOT USE "Overwrite File" option, as there are additional/unnecessary FTP commands involved.


	TESTED DEVICES:
	"AXIS (211A)"
	"Panasonic BL-C10"
	"Panasonic BL-C104"
*/

// http://ftpguide.com
// http://www.nsftools.com/tips/RawFTP.htm
// https://support.solarwinds.com/Success_Center/Serv-U_Managed_File_Transfer_Serv-U_FTP_Server/Knowledgebase_Articles/DELE_FTP_command

#define ENABLE_ANALYTICS 1

// NOTE: Port >1024 require root permissions.
FTPServer::FTPServer(Main& rApp, U32 passiveSockTimeoutSec)
	: mMain(rApp)
	, mPassiveSocketTimeoutSec(passiveSockTimeoutSec)
{
	this->SetupAuthSQLQuery();

	const size_t numPreAllocatedClients = 64;

	// Pre-allocated CLIENT components.
	mClientSockets.resize(numPreAllocatedClients);
	mClientTimePoints.resize(numPreAllocatedClients);
	mClientUsernames.resize(numPreAllocatedClients);
	mClientEventSessionIds.resize(numPreAllocatedClients);
	mClientEventSessionFootageOffsetIndexes.resize(numPreAllocatedClients);

	mClientTimeoutLocks.resize(numPreAllocatedClients);
}

FTPServer::~FTPServer()
{
	Socket::Close(mServerSocket);
}

bool FTPServer::Start(U16 port)
{
	try
	{
		mServerSocket = Socket::CreateServer(port, mMaxConnectionsQuery);
	}
	catch (const Exception& e)
	{
		LOG_ERROR(Log::Channel::FTP, e.GetText());
		return false;
	}

	LOG_MESSAGE(Log::Channel::FTP, "FTP Server started. (Port %d)", port);
	return true;
}

auto FTPServer::AddClient(SocketId socketId) -> ClientId
{
	ClientId id;

	if (mClientReleasedIds.empty())
	{
		// No client id's to re-use, so create a new one.
		id = mClientIdCounter++;

		// Allow vector to decide on the capacity pre-allications.
		if (mClientSockets.size() <= id)
		{
			const std::size_t newSize = id + 1;

			mClientSockets.resize(newSize);
			mClientTimePoints.resize(newSize);
			mClientUsernames.resize(newSize);
			mClientEventSessionIds.resize(newSize);
			mClientEventSessionFootageOffsetIndexes.resize(newSize);

			// NOTE:
			// Mutex is used here only to protect the simultaneous "resize" while other thread is trying to access the array. (See "FTPServer::ClientTimeoutUnlock")
			mClientTimeoutLocksMutex.lock();
			mClientTimeoutLocks.resize(newSize);
			mClientTimeoutLocksMutex.unlock();
		}
	}
	else
	{
		id = mClientReleasedIds.back();	// Returns a reference to the last element in the vector.
		mClientReleasedIds.pop_back();	// Removes the last element in the vector, effectively reducing the container size by one.
	}

	mClientSockets.at(id)		= socketId;
	mClientTimePoints.at(id)	= std::chrono::steady_clock::now();
	mClientUsernames.at(id).clear();
	mClientEventSessionIds.at(id) = InvalidEventSessionId;
	mClientEventSessionFootageOffsetIndexes.at(id) = 0;
	mClientTimeoutLocks.at(id) = false;

	return id;
}

// A new connection gets added to the "unknown" list until they are approved or timeout.
void FTPServer::HandleNewConnection()
{
	sockaddr_in from;
	socklen_t	fromSize = sizeof(sockaddr_in);

	auto clientSocket = accept(mServerSocket, (sockaddr*)&from, &fromSize);

	if (clientSocket == INVALID_SOCKET)
		return;

	// Sockets are "blocking" by default, so set it to a "non-blocking".
	Socket::SetNonBlocking(clientSocket);

	{
		const U16	 clientPort = ntohs(from.sin_port);
		const String clientIPString(inet_ntoa(from.sin_addr));

		LOG_MESSAGE(Log::Channel::FTP, "FTP connection from: %s:%d", clientIPString.c_str(), clientPort);
	}

	const auto clientId = this->AddClient(clientSocket);

	mClientActiveIds.push_back(clientId);

//	printf("CLIENT ID: %d - (List size: %d, pre-allocated: %d)\n", clientId, mClientActiveIds.size(), mClientSockets.size());

	// Immediately send "Welcome" message to the client and expect for fast response.

	// NOTE:
	// Even if the server did not sent any "welcome" message, some FTP clients (i.e. HikVision DS-2CD2142FWD-IWS) 
	// might try and send "USER" command to the server. (But this would take ~10 seconds)
	Socket::SendText(clientSocket, "220 Welcome \r\n");
}

// HM:
// Gali buti, kad nereikia kaskart sukurineti socketo, bo "FTPCommand::PORT" turetu 
// viena karta sukurti socketa ir per ji prisijungus mums turetu siusti multiple feimus?

// The "ACTIVE" mode (FTPCommand::PORT) is when we're connecting directly to the device and receiving data through the connected socket.

// IMPORTANT: 
// Not passing "path" and "filename" as a reference, because "DownloadFootage" is executend in a separate thread and reference might get lost.
void DownloadFootageActiveTask(FTPServer* pFTPServer, EventManager* pEventManager, ClientId clientId, EventId eventId, EventSessionId eventSessionId, U16 footageIndex, const String path, String filename, U32 cameraId, U32 ipAddress, U16 port)
{
	SocketId fileSocket = INVALID_SOCKET;

	try
	{
		if ((fileSocket = socket(AF_INET, SOCK_STREAM, 0)) == INVALID_SOCKET)
			throw Exception("Failed for \"socket\"!");

		// Set socket to non-blocking so that "connect" would not lock us up.
		Socket::SetNonBlocking(fileSocket);

		sockaddr_in addr{};

		addr.sin_family = AF_INET;
		addr.sin_addr.s_addr = /*htonl*/(ipAddress);
		addr.sin_port = htons(static_cast<U16>(port));

		LOG_MESSAGE(Log::Channel::FTP, "Connecting ACTIVE file socket... (CameraId: %u, Address: %s:%u)", cameraId, inet_ntoa(addr.sin_addr), port);

		// TODO:... Use "select" instead...
		int connectResult = 0;

		for (int i = 0; i < 100; ++i)
		{
			connectResult = connect(fileSocket, (sockaddr*)&addr, sizeof(addr));

			LOG_MESSAGE(Log::Channel::FTP, "Connect response: %d. (CameraId: %u)", connectResult, cameraId);

			if (connectResult != -1)
				break;

			if (gIsQuitRequested)
				break;

			std::this_thread::sleep_for(std::chrono::milliseconds(100));
		}

		if (connectResult == -1)
			throw Exception("Failed to connect the ACTIVE file socket!");

		// TODO TODO TODO TODO.....
		// Sutvarkyti situacija kai feilina prisijungti!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!


		// TODO: Optimizuoti: 
		// Galime iskart rasyti i faila, neskaityti is pradziu i atminti o po to i faila...
		// (Bet gal tai kaip tik negerai, nes HDD daugiau seekins ir darys write operaciju, nei kad viena dideli atminties gabala i faila irasytu?)
		// BET: mums gali reiketi ir failo, ir iskart siusti i Analytics serveri.
		Vector<char> dataBuffer;

		Socket::Read(fileSocket, dataBuffer);

		// Try to parse the footage timestamp from it's filename.
		FileNameParser fnParser(filename);

#if 0
		filename = std::to_string(footageIndex) + '_' + filename;
#else
		// Add footage index at the end of the filename for better filename sorting. 
		// (Tom was having problems, decided that we need index at the end instead of the beginning)
		const String suffix('_' + std::to_string(footageIndex));

		auto extensionPos = filename.find_last_of('.');

		if (extensionPos != String::npos)
			filename.insert(extensionPos, suffix);
		else
			filename += suffix;
#endif
//		printf("FOOTAGE PATH: %s\n", path.c_str());

		std::fstream file(path + filename, std::ios::out | std::fstream::binary);

		if (!file.is_open())
			throw ExceptionVA("Failed to write \"%s\" (Path: \"%s\"). Error: %s", filename.c_str(), path.c_str(), strerror(Socket::GetErrorCode()));

		file.write(dataBuffer.data(), dataBuffer.size());

		LOG_MESSAGE(Log::Channel::FTP, "File ready (Bytes %d)...", dataBuffer.size());

		// 2019-09-19
		// Mobotix filename can look like this: "mx16bd8d00"
		// So "FileNameParser" will fail to parse out the timestamp information.
		// In this case we will be using machine's local timestamp.
		if (!fnParser.IsParsed())
		{
			String dateTimeStr; U16 ms;
			Utils::StringFromLocaltime(dateTimeStr, ms);

			pEventManager->AddFootageNotice(eventId, filename, dateTimeStr, ms);
		}
		else
			pEventManager->AddFootageNotice(eventId, filename, fnParser.GetTimestampStr(), fnParser.GetTimestampMs());
	}
	catch (const Exception& e)
	{
		LOG_ERROR(Log::Channel::FTP, "%s (CameraId: %u)", e.GetText(), cameraId);
	}

	Socket::Close(fileSocket);

	pFTPServer->ClientTimeoutUnlock(clientId);
	pEventManager->EventSessionTimeoutUnlock(eventSessionId);
}

// The "PASSIVE" mode (FTPCommand::PASV) is when we're creating a "listening" socket that waits for the connections from the devices.
// Once new connection is available, data get's receiving through the newly connected socket.

// IMPORTANT: 
// Not passing "path" and "filename" as a reference, because "DownloadFootage" is executend in a separate thread and reference might get lost.
// NOTE:
// "passiveSocketId" is non-blocking.
void DownloadFootagePassiveTask(FTPServer* pFTPServer, EventManager* pEventManager, ClientId clientId, EventId eventId, EventSessionId eventSessionId, U16 footageIndex, const String path, String filename, SocketId passiveSocketId, U32 passiveSocketTimeoutSec)
{
	SocketId fileSocket = INVALID_SOCKET;

	try
	{
		// IMPORTANT:
		// The "passiveSocketId" is a non-blocking socket,
		// So we might expect "accept" to fail with Error code 11 ("Resource temporarily unavailable")
		// To solve this we're using "select". We're waiting for 
		fd_set fdSet;
		FD_ZERO(&fdSet);
		FD_SET(passiveSocketId, &fdSet);

		// TODO !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
		// "event_session_timeout_sec" might be < "ftp_passive_soc_timeout_sec"!
		// Handle timout situatin correctly...

		timeval	timeout = { passiveSocketTimeoutSec, 0 };

		// TODO: http://beesbuzz.biz/code/5739-The-problem-with-select-vs-poll
		int result = select(passiveSocketId + 1, &fdSet, 0, 0, &timeout);
		if (result < 0)
		{
			int errorCode = Socket::GetErrorCode();
			throw ExceptionVA("Failed for \"select\"! Error: %s, Code: %d", Socket::GetErrorString(errorCode), errorCode);
		}
		else if (result == 0)
			throw ExceptionVA("Passive connection timeout! (%u seconds)", passiveSocketTimeoutSec);
		

		sockaddr_in dataClientAddr;
		socklen_t dataClientAddrSize = sizeof(sockaddr_in);

		fileSocket = accept(passiveSocketId, (sockaddr*)&dataClientAddr, &dataClientAddrSize);

		if (fileSocket == INVALID_SOCKET)
		{
			int errorCode = Socket::GetErrorCode();
			throw ExceptionVA("Failed for \"accept\"! Error: %s, Code: %d", Socket::GetErrorString(errorCode), errorCode);
		}

		// Socket will be blocking by default, so set it to a non-blocking.
		Socket::SetNonBlocking(fileSocket);

		// TODO: Optimizuoti: 
		// Galime iskart rasyti i faila, neskaityti is pradziu i atminti o po to i faila...
		// (Bet gal tai kaip tik negerai, nes HDD daugiau seekins ir darys write operaciju, nei kad viena dideli atminties gabala i faila irasytu?)
		// BET: mums gali reiketi ir failo, ir iskart siusti i Analytics serveri.
		Vector<char> dataBuffer;

		Socket::Read(fileSocket, dataBuffer);

		// Try to parse the footage timestamp from it's filename.
		FileNameParser fnParser(filename);

#if 0
		// File name starts with a number (footage index) that get's incremented per-event session every time a new footage is received.
		// (We can have more than one consecutive client connection per-event session. 
		//  At the same time, single client connection can send multiple footages)
		filename = std::to_string(footageIndex) + '_' + filename;
#else
		// Add footage index at the end of the filename for better filename sorting. 
		// (Tom was having problems, decided that we need index at the end instead of the beginning)
		const String suffix('_' + std::to_string(footageIndex));

		auto extensionPos = filename.find_last_of('.');

		if (extensionPos != String::npos)
			filename.insert(extensionPos, suffix);
		else
			filename += suffix;
#endif

		std::fstream file(path + filename, std::ios::out | std::fstream::binary);

		if (!file.is_open())
			throw ExceptionVA("Failed to write \"%s\" (Path: \"%s\"). Error: %s", filename.c_str(), path.c_str(), strerror(Socket::GetErrorCode()));
		else
		{
			file.write(dataBuffer.data(), dataBuffer.size());

			LOG_MESSAGE(Log::Channel::FTP, "File ready (Bytes %d)...", dataBuffer.size());
		}

		// 2019-09-19
		// Mobotix filename can look like this: "mx16bd8d00"
		// So "FileNameParser" will fail to parse out the timestamp information.
		// In this case we will be using machine's local timestamp.
		if (!fnParser.IsParsed())
		{
			String dateTimeStr; U16 ms;
			Utils::StringFromLocaltime(dateTimeStr, ms);

			pEventManager->AddFootageNotice(eventId, filename, dateTimeStr, ms);
		}
		else
			pEventManager->AddFootageNotice(eventId, filename, fnParser.GetTimestampStr(), fnParser.GetTimestampMs());
	}
	catch (const Exception& e)
	{
		LOG_ERROR(Log::Channel::FTP, e.GetText());
	}

	Socket::Close(fileSocket);

	pFTPServer->ClientTimeoutUnlock(clientId);
	pEventManager->EventSessionTimeoutUnlock(eventSessionId);
}

void FTPServer::HandleClients(Database::Connection& rDatabase)
{
	const auto currentTP = std::chrono::steady_clock::now();

	char buffer[1024]{};

	for (auto& clientId : mClientActiveIds)
	{
		auto& rSocketId = mClientSockets.at(clientId);

		if (rSocketId == INVALID_SOCKET)
			continue;

		auto bytesReceived = recv(rSocketId, buffer, sizeof(buffer), 0);

		if (bytesReceived == SOCKET_ERROR) // NOTE: Socket is non-blocking.
			continue;

		if (bytesReceived == 0)
		{
			LOG_MESSAGE(Log::Channel::FTP, "Client shutdown");
			Socket::Close(rSocketId);
			continue;
		}

		// Don't timeout.
		mClientTimePoints.at(clientId) = currentTP;

		//==============================================================================
		const auto commandType = this->GetCommandType(buffer, bytesReceived);

		LOG_MESSAGE(Log::Channel::FTP, "FTP Command: %s", this->GetCommandName(commandType).c_str());

		switch (commandType)
		{
			case FTPCommand::AUTH:
			{
				// https://support.solarwinds.com/SuccessCenter/s/article/AUTH-FTP-command
				// A 502 code may be sent in response to any FTP command that the server does not support. 
				// It is a permanent negative reply, which means the client is discouraged from sending the command 
				// again since the server will respond with the same reply code. 

				// The original FTP specification dictates a minimum implementation for all FTP servers with a 
				// list of required commands. Because of this, a 502 reply code should not be sent in response to a required command.
				Socket::SendText(rSocketId, "502 \r\n"); // FileZilla - "502 Explicit TLS authentication not allowed \r\n"
			}
			break;

			case FTPCommand::USER:
				{
					mClientUsernames.at(clientId) = Utils::StripText(buffer, sizeof(buffer), 5);

					Socket::SendText(rSocketId, "331 Password required \r\n");
				}
				break;

			case FTPCommand::PASS:
				{
					// NOTE:
					// Noticed that when "530" (or some other error code) is sent as a response
					// HikVision camera will flood us with the retries.
					//=========================================================================

					const String username(mClientUsernames.at(clientId));
					const String password(Utils::StripText(buffer, sizeof(buffer), 5));
					const String hashKey(username + password);

					// NOTE: 
					// Client validation is checked once per Event Session.

					EventSessionId eventSessionId = 0;
	
					// Check the EventManager if we already have an event session for this specific HashKey.
					if (!mMain.EventManagerPtr->HasSession(hashKey, &eventSessionId))
					{
						eventSessionId = mMain.EventManagerPtr->AddSession(hashKey);

						mClientEventSessionIds.at(clientId) = eventSessionId;
						mClientEventSessionFootageOffsetIndexes.at(clientId) = 0; // Start from zero.

						printf("------------- NEW EVENT SESSION [id %u] -------------\n", eventSessionId);

						U32 userId;
						U32 siteId;
						U32 cameraId;
						bool isArmed;
						U8 personThreshold;

						// Check database and validate the client.
						// HM: If client is not valid - dont send any response?
						if (!this->CheckAuthentification(eventSessionId, username, password, &userId, &siteId, &cameraId, &isArmed, &personThreshold))
						{
//							Socket::SendText(rSocketId, "530 Failed to authenticate. \r\n");
							Socket::Close(rSocketId);
							break;
						}

						// If camera is "disarmed", don't accept any new footage and don't send it to Analytics/Event manager.
						if (!isArmed)
						{
							LOG_WARNING(Log::Channel::FTP, "Camera (id %u), user \"%s\" is not armed", cameraId, username.c_str());
							Socket::Close(rSocketId); // HM: Don't close the socket? HikVision will constantly try to re-send re request...
							break;
						}

						const auto eventId = mMain.EventManagerPtr->AuthenticateSession(eventSessionId, userId, siteId, cameraId);

						const String footagePath(mMain.CreateFootagePath(eventId, userId, siteId, cameraId));

						LOG_DEBUG(Log::Channel::FTP, "New validated event session. (ClientId: %u, EventId: %" PRIu64 ", EventSessionId: %u)", clientId, eventId, eventSessionId);

						mMain.EventManagerPtr->SetSessionPath(eventSessionId, footagePath);
						mMain.EventManagerPtr->SetSessionArmedState(eventSessionId, isArmed);

						// TODO: Move to EventManager::AddSession?
#if ENABLE_ANALYTICS
						mMain.AnalyticsPtr->AddEvent(eventId, cameraId, personThreshold, footagePath);
#endif
					}
					else
					{
						// IMPORTANT:
						// Event manager might already hold the corresponding session open.
						// But if device is not armed - don't take any actions.
						if (!mMain.EventManagerPtr->GetArmedState(eventSessionId))
						{
							LOG_DEBUG(Log::Channel::FTP, "Event session is DISARMED, ignoring... (ClientId: %u, EventSessionId: %u)", clientId, eventSessionId);
							// NOTE:
							// Not closing the socket because HikVsion will instantly try to reconnect and resend the requests...
							// To prevent the unnecessary flooding don't do anything, just allow session to time-out.
							// Socket::Close(rSocketId);
							break;
						}

						mMain.EventManagerPtr->SetSessionTimepoint(eventSessionId, currentTP);

						auto footageIndex = mMain.EventManagerPtr->GetFootageIndex(eventSessionId);

						mClientEventSessionIds.at(clientId) = eventSessionId;
						mClientEventSessionFootageOffsetIndexes.at(clientId) = footageIndex; // Start from the last known event session footage offset index.

						LOG_DEBUG(Log::Channel::FTP, "Using the existing event session. (ClientId: %u, EventSessionId: %u)", clientId, eventSessionId);
					//	printf("------------- EXISTING EVENT SESSION [id %u, footageIndex: %u] -------------\n", eventSessionId, footageIndex);
					}

					// If session already exists, keeps it updated so that it won't timeout.
					// Else, if dealing with a "new" session, we will need to fill this timepoint value anyway.
					// If user failed the validation, we will use this timepoint to determine the potential malicious connect attempts. (TODO)
//					mEventSessionTimepoints.at(eventSessionId) = currentTP;

//					Socket::SendText(rUnknownClient.socket, "530 Wrong password. \r\n");
					Socket::SendText(rSocketId, "230 \r\n"); // Login is ok.
				}
				break;

			case FTPCommand::TYPE:
				{
					// The TYPE command is issued to inform the server of the type of data that is being transferred by the client. 
					// Most modern Windows FTP clients deal only with type A (ASCII) and type I (image/binary).
					const char dataType = buffer[5];

					if (dataType == 'I')
					{
						Socket::SendText(rSocketId, "200 \r\n"); // "200 Switching to Binary mode. \r\n"
					}
					else if (dataType == 'A')
					{
						LOG_WARNING(Log::Channel::FTP, "Client is using unsupported ASCII data type! (Allowing to continue)");
						Socket::SendText(rSocketId, "200 \r\n");
					}
					else
					{
						LOG_ERROR(Log::Channel::FTP, "Unknown client data type! (%c)", dataType);
						Socket::SendText(rSocketId, "500 Unknown data type. \r\n");
						Socket::Close(rSocketId);
					}
				}
				break;

			case FTPCommand::PWD:
				// "Print Working Directory" (PWD)
				// Client requested for the "working directory"
				Socket::SendText(rSocketId, "257 \"/\" is current directory. \r\n");
				break;

			case FTPCommand::CWD:
				// Issued to change the client's current working directory to the path specified with the command.
				// i.e. "CWD DummyPath/office/hikvision-T"
				Socket::SendText(rSocketId, "250 \r\n");
				break;

			case FTPCommand::PASV:
				{
					// This command requests the server "listen" on a data port and to wait for a connection rather than to initiate 
					// one upon a transfer command thus making the Transfer Mode Passive. 
					// The response to this command includes the host and port address the server is listening on in most cases these have defaults..
					auto& rFTPSession = mFTPSessionMap[clientId];

					rFTPSession.mode = FTPSession::Mode::Passive;

					// The client might use same connection to send multiple images.
					// So we can re-use the same "passive server" socket.
					if (rFTPSession.passiveSocketId == INVALID_SOCKET)
					{
						// NOTE: "rFTPSession.port" filled by the "Socket::CreateServer".
						try
						{
							rFTPSession.passiveSocketId = Socket::CreateServer(rFTPSession.port, 1, false);
						}
						catch (const Exception& e)
						{
							LOG_ERROR(Log::Channel::FTP, "Failed to setup the FTP's PASSIVE server! (%s)", e.GetText());
							break;
						}

					//	if (rFTPSession.socketPassive == INVALID_SOCKET)
					//	{
					//		TODO
					//	}
					}

					U32 addressInt = 0;
					{
						sockaddr_in address;
						socklen_t addressLength = sizeof(sockaddr_in);
						getsockname(rSocketId, (sockaddr*) &address, &addressLength);

						addressInt = address.sin_addr.s_addr;
					}

					// "227 Entering Passive Mode (192,168,10,119,239,77)."

					// NOTE: 
					// When tested with Panasonic BLC-140 camera, we've failed to connect when "127, 0, 0, 1" was used.
					// Needed to use "192, 168, 10, 119" (the real IP address of the FTP machine)
//					Socket::SendTextVA(rSocketId, "227 Entering Passive Mode (%d, %d, %d, %d, %d, %d) \r\n", 192, 168, 10, 119, (rFTPSession.port >> 8), (rFTPSession.port & 0x00FF));
//					Socket::SendTextVA(rSocketId, "227 Entering Passive Mode (%d, %d, %d, %d, %d, %d) \r\n", 127, 0, 0, 1, (rFTPSession.port >> 8), (rFTPSession.port & 0x00FF));
#if 1
					{
						LOG_DEBUG(Log::Channel::FTP, "PASV (PASSIVE connection) (%d.%d.%d.%d:%d)", addressInt & 0xff, (addressInt >> 8) & 0xff, (addressInt >> 16) & 0xff, (addressInt >> 24) & 0xff, rFTPSession.port);
					}

					Socket::SendTextVA(rSocketId, "227 Entering Passive Mode (%d,%d,%d,%d,%d,%d) \r\n", addressInt & 0xff, (addressInt >> 8) & 0xff, (addressInt >> 16) & 0xff, (addressInt >> 24) & 0xff, rFTPSession.port >> 8, rFTPSession.port & 0xFF);
#else				
					// After this, camera should try and use the ACTIVE mode instead of PASSIVE.
					LOG_DEBUG(Log::Channel::FTP, "^^^^^^^^^^^^^^^^^^^^^^ SENDING NEGATIVE RESPONSE TO PASV ^^^^^^^^^^^^^^^^^^");
					Socket::SendTextVA(rSocketId, "500 \r\n");
#endif

					// Tada mums atsiuncia:
					// "STOR 192.168.0.64_01_20190130164419089_MOTION_DETECTION.jpg"
				}
				break;

			case FTPCommand::PORT: // Command is used during "active" mode transfers.
				{
					auto& rFTPSession = mFTPSessionMap[clientId];

					rFTPSession.mode = FTPSession::Mode::Active;


					// SAMPLE: "192,168,0,50,12,28"
					//         "192,168,1,64,171,126"
					const String content(Utils::StripText(buffer, sizeof(buffer), 5));

					LOG_DEBUG(Log::Channel::FTP, "Active address: %s", content.c_str());

					U32 ipAddress = 0;
					U32 port = 0;

					std::istringstream ss(content);

					for (int i = 0; i < 6; ++i)
					{
						U32 octet;

						ss >> octet;
						ss.ignore();

						if (i < 4)
							ipAddress |= octet << (i * 8);
						else
						{ // 4, 5
						//		 if (i == 4) port  = octet * 256;
						//	else if (i == 5) port += octet;

							port |= octet << ((i - 4) * 8);
						}
					}
					// TODO: In case of failing to parse, return error code "501".

					rFTPSession.address = ipAddress;
					rFTPSession.port = static_cast<U16> (port);

					{
						in_addr addr;
						addr.s_addr = ipAddress;
						LOG_DEBUG(Log::Channel::FTP, "PORT(ACTIVE connection) Address: %s:%u", inet_ntoa(addr), port);
					}

// !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
#if 0 // HM: Labai labai blogai cia laukti kol socket'as prisijungs...
// !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!

					if ((rFTPSession.socket = socket(AF_INET, SOCK_STREAM, 0)) == INVALID_SOCKET)
						throw Exception("Failed for \"socket\"!");

					// Set socket to non-blocking so that "connect" would not lock us up.
					Socket::SetNonBlocking(rFTPSession.socket);

					sockaddr_in addr{};

					addr.sin_family = AF_INET;
					addr.sin_addr.s_addr = /*htonl*/(rFTPSession.address);
					addr.sin_port = /*htons*/(rFTPSession.port);

					LOG_MESSAGE("Connecting file socket...");

					for (int i = 0; i < 100; ++i)
					{
						int st = connect(rFTPSession.socket, (sockaddr*)&addr, sizeof(addr));

						LOG_MESSAGE("Connect response: %d", st);

						if (st != -1)
							break;

//							if (mIsStopRequested)
//								break;

						std::this_thread::sleep_for(std::chrono::milliseconds(100));
					}
#endif

					// "200 PORT command success"
					Socket::SendText(rSocketId, "200 \r\n");
				}
				break;

				// TODO: Support "EPRT"
				// The EPSV command replaces the PASV command.
				// TODO: Read more in book "IPv6 Essentials by Silvia Hagen" (page 248)

			case FTPCommand::STOR:
				{
					// A client issues the STOR command after successfully establishing a data connection 
					// when it wishes to upload a copy of a local file to the server. 
					// The client provides the file name it wishes to use for the upload.

					// Sample HikVision: "STOR 192.168.0.64_01_20190130164419089_MOTION_DETECTION.jpg"
					// Sample AlXIS:      "STOR image83-12-09_23-43-44-35.jpg"

//					LOG_MESSAGE("STOR: %s", buffer);

					Socket::SendText(rSocketId, "150 \r\n");

					// Determine the event session id.
					auto eventSessionId = mClientEventSessionIds.at(clientId);
					auto eventId = mMain.EventManagerPtr->GetEventId(eventSessionId);

					// 2019-06-07 If "InvalidEventId" is received:
					// We're probably dealing with a footage that came for the 
					if (eventId == InvalidEventId)
					{
						LOG_ERROR(Log::Channel::FTP, "Ignoring footage download for the invalidated eventId. (ClientId: %u, EventSessionId: %u)", clientId, eventSessionId);
					}
					else
					{

						// Don't timeout the event session.
						mMain.EventManagerPtr->SetSessionTimepoint(eventSessionId, currentTP);
						mMain.EventManagerPtr->EventSessionTimeoutLock(eventSessionId); // 2019-06-07

						const String footagePath(mMain.EventManagerPtr->GetFootagePath(eventSessionId));
						const String fileName(Utils::StripText(buffer, sizeof(buffer), 5));

						// TODO: How about "rFTPSession" ?
						auto& rFTPSession = mFTPSessionMap[clientId];
						auto footageIndex = mClientEventSessionFootageOffsetIndexes.at(clientId)++;

						// Enter the "timout-lock" stage. (don't timeout while footage is downloading or queued for download)
						this->ClientTimeoutLock(clientId);

						// NOTE:
						// Download footage task is added to the thread pool task queue.
						// So there might be some footage that is still not processed fast enough and the event session might be timedout some time ago...
						if (rFTPSession.mode == FTPSession::Mode::Active)
						{
							const auto cameraId = mMain.EventManagerPtr->GetCameraId(eventSessionId);

							mMain.ThreadPoolPtr->Enqueue(DownloadFootageActiveTask, this, mMain.EventManagerPtr.get(), clientId, eventId, eventSessionId, footageIndex, footagePath, fileName, cameraId, rFTPSession.address, rFTPSession.port);
						}
						else
							mMain.ThreadPoolPtr->Enqueue(DownloadFootagePassiveTask, this, mMain.EventManagerPtr.get(), clientId, eventId, eventSessionId, footageIndex, footagePath, fileName, rFTPSession.passiveSocketId, mPassiveSocketTimeoutSec);
					}

					// NOTICE: 
					// Sending "completed" response while the footage might still be downloaded.
					// This MIGHT cause some problems, for as far as I've tested on multiple devices - everyting is ok.
					Socket::SendText(rSocketId, "226 Transfer completed \r\n");
				}
				break;

			case FTPCommand::QUIT:
				Socket::SendText(rSocketId, "221 \r\n");
				Socket::Close(rSocketId);
				break;

			case FTPCommand::NOOP:
				// The NOOP command does not cause the server to perform any action beyond acknowledging the receipt of the command.
				// This command can be issued to the server to prevent the client from being automatically disconnected for being idle.
				// It can also prevent modern routers / firewalls from closing a connection that it perceives as being idle as well.
				Socket::SendText(rSocketId, "200 \r\n");
				break;

			case FTPCommand::MODE:
				{
					// Noticed that "Panasonic BL-C140" is sending this command.
					// The command changes the transfer mode. 
					// The argument is a single Telnet character code specifying the data transfer modes described in the Section on Transmission Modes.
					const char* pMode = Utils::StripText(buffer, sizeof(buffer), 5);

					if (pMode[0] != 'S')
					{
						LOG_ERROR(Log::Channel::FTP, "FTP streaming mode %s is not supported", pMode);
						Socket::SendText(rSocketId, "504 \r\n");
						break;
					}

					Socket::SendText(rSocketId, "200 \r\n"); // "200 Mode set to S."
				}
				break;

			case FTPCommand::STRU:
				{
					// Noticed that "Panasonic BL-C140" is sending this command.
					// The command is issued with a single Telnet character parameter that specifies a file structure for the server to use for file transfers.
					const char* pMode = Utils::StripText(buffer, sizeof(buffer), 5);

					if (pMode[0] != 'F')
					{
						LOG_ERROR(Log::Channel::FTP, "FTP file transfer mode %s is not supported!", pMode);
						Socket::SendText(rSocketId, "504 \r\n");
						break;
					}

					Socket::SendText(rSocketId, "200 \r\n"); // "200 Structure set to F."
				}
				break;

			case FTPCommand::DELE:
				// Noticed that "Panasonic BL-C10" is sending this command.
				// We can ignore it and just send a "successful" response.
#if 1
				LOG_MESSAGE(Log::Channel::FTP, "DELE content: %s", Utils::StripText(buffer, sizeof(buffer), 5));
#endif
				Socket::SendText(rSocketId, "250 \r\n");
				break;

			case FTPCommand::RNFR:
				// Noticed that "Panasonic BL-C10" is sending this command.
				// Send the "350 Requested file action pending further information." 
				// The device should send an additional "RNTO" command.
#if 1
				LOG_MESSAGE(Log::Channel::FTP, "RNFR content: %s", Utils::StripText(buffer, sizeof(buffer), 5));
#endif
				Socket::SendText(rSocketId, "350 \r\n");
				break;

			case FTPCommand::RNTO:
				// Noticed that "Panasonic BL-C10" is sending this command.
				// This command is sent right after the "FTPCommand::RNFR" command.
				// We can ignore it and just send a "successful" response.
#if 1
				LOG_MESSAGE(Log::Channel::FTP, "RNTO content: %s", Utils::StripText(buffer, sizeof(buffer), 5));
#endif
				Socket::SendText(rSocketId, "250 \r\n");
				break;

			case FTPCommand::SYST:
			case FTPCommand::FEAT:
			case FTPCommand::LIST:
				// Noticed that "FileZilla" is sending this command.
				Socket::SendText(rSocketId, "501 \r\n");
				break;

			case FTPCommand::ABOR:
				// Noticed that "Panasonic BL-C10" is sending this command.

				// Issued by the client to abort the previous FTP command. 
				// If the previous FTP command is still in progress or has already completed, 
				// the server will terminate its execution and close any associated data connection. 
				// This command does not cause the connection to close.

				// The "226" signals that the current file transfer was successfully terminated.
				Socket::SendText(rSocketId, "226 \r\n");
				break;

			default:
				LOG_ERROR(Log::Channel::FTP, "Unknown FTP Command: \"%s\" [%s]!", FTPServer::GetCommandName(commandType).c_str(), buffer);
				break;

		} // switch (commandType)
	} // for loop
}

// Checks all "active" clients for the timeout.
// If client timeout gets detected, it's socket is closed.
void FTPServer::HandleTimeouts()
{
//	printf("---------- Timeout check [%d clients] --------------\n", mClients.list.size());

	const auto currentTP = std::chrono::steady_clock::now();

	for (auto clientId : mClientActiveIds)
	{
		auto& clientTP = mClientTimePoints.at(clientId);

		// Ignore the "locked" clients, they are not allowed to timeout.
		if (mClientTimeoutLocks.at(clientId))
		{
			clientTP = currentTP; // Keep client from instant timeout when unlocked.
			continue;
		}


		auto seconds = std::chrono::duration_cast<std::chrono::seconds>(currentTP - clientTP).count();

//		LOG_MESSAGE("Client[%d] - seconds: %d\n", clientId, seconds);

		if (seconds > ClientTimeout)
		{
			LOG_WARNING(Log::Channel::FTP, "Client timeout. (size: %d)", mClientActiveIds.size());

			Socket::Close(mClientSockets.at(clientId));
		}
	}
}

// Checks all "active" clients if they're socket is closed.
// If socket is closed - client is removed from the "active" list 
// and it's "ClientId" can be re-used for the newly connected clients.

// NOTE: The disconnected client doesn't mean that the event session it self has ended.
void FTPServer::HandleInactiveClients()
{
	if (mClientActiveIds.empty())
		return;

	auto IsInactive = [&](ClientId clientId)
	{
		if (mClientSockets.at(clientId) == INVALID_SOCKET)
		{
			LOG_MESSAGE(Log::Channel::FTP, "REMOVING CLIENT: %d", clientId);

			auto eventSessionId = mClientEventSessionIds.at(clientId);

			// NOTE:
			// If client was not validated, there will be no event session.
			if (eventSessionId != InvalidEventSessionId)
			{
				auto imageIndex	= mClientEventSessionFootageOffsetIndexes.at(clientId);

				mMain.EventManagerPtr->SetLastKnownFootageIndex(eventSessionId, imageIndex);
			}

			// Remove FTP session.
			auto it = mFTPSessionMap.find(clientId);
			if (it != mFTPSessionMap.end())
				mFTPSessionMap.erase(it);

			mClientReleasedIds.push_back(clientId);
			return true;
		}

		return false;
	};

	auto it = std::remove_if(mClientActiveIds.begin(), mClientActiveIds.end(), IsInactive);

	mClientActiveIds.erase(it, mClientActiveIds.end());
}

// THREAD: FTPServer thread (Main thread)
void FTPServer::ClientTimeoutLock(ClientId clientId)
{
	// When FTP client is trying to download the footage, we enter the "timeout lock" stage.
	// That gets unlocked when footage is downloaded or failed to be downloaded.

//	std::lock_guard<std::mutex> lock(mClientTimeoutLocksMutex);

	mClientTimeoutLocks.at(clientId) = true;
}

// THREAD: Any thread from the ThreadPool.
// Tasks: "DownloadFootageActiveTask" or "DownloadFootagePassiveTask".
void FTPServer::ClientTimeoutUnlock(ClientId clientId)
{
	// NOTE:
	// Mutex is used here only to protect of using "mClientTimeoutLocks" array while it's being resized.
	std::lock_guard<std::mutex> lock(mClientTimeoutLocksMutex);

	mClientTimeoutLocks.at(clientId) = false;
}

void FTPServer::SetupAuthSQLQuery()
{
	using namespace Database::Table;

	{
		std::ostringstream ss;

		ss	<< "SELECT "<< Sites::TableName		<< '.' << Sites::UserId
			<< ','		<< Users::TableName		<< '.' << Users::IsActive
			<< ','		<< Sites::TableName		<< '.' << Sites::Id
			<< ','		<< CameraFTP::TableName << '.' << CameraFTP::CameraId
			<< ','		<< Sites::TableName		<< '.' << Sites::DeletedAt
			<< ','		<< Cameras::TableName	<< '.' << Cameras::DeletedAt
			<< ','		<< Cameras::TableName	<< '.' << Cameras::IsArmed
			<< ','		<< Cameras::TableName	<< '.' << Cameras::PersonThreshold

			<< " FROM "	<< CameraFTP::TableName

			<< " JOIN "	<< Cameras::TableName	<< " ON " << Cameras::TableName << '.' << Cameras::Id
			<< '='		<< CameraFTP::TableName << '.' << CameraFTP::CameraId

			<< " JOIN "	<< Sites::TableName		<< " ON " << Sites::TableName	<< '.' << Sites::Id
			<< '='		<< Cameras::TableName	<< '.' << Cameras::SiteId

			<< " JOIN "	<< Users::TableName		<< " ON " << Users::TableName	<< '.' << Users::Id
			<< '='		<< Sites::TableName		<< '.' << Sites::UserId

			<< " WHERE " << CameraFTP::TableName << '.' << CameraFTP::Username << "=\'";

		mSQLQuery.authA = ss.str();
	}

	{
		std::ostringstream ss;

		ss << "' AND " << CameraFTP::TableName << '.' << CameraFTP::Password << "=\'";

		mSQLQuery.authB = ss.str();
	}

/*
	SELECT
	`vq_sites`.`user_id`,
	`vq_users`.`is_active`,
	`vq_sites`.`id`,
	`vq_camera_ftp`.`camera_id`,
	`vq_sites`.`deleted_at`,
	`vq_cameras`.`deleted_at`,
	`vq_cameras`.`is_armed`

	FROM `vq_camera_ftp`

	JOIN `vq_cameras` ON `vq_cameras`.`id`=`vq_camera_ftp`.`camera_id`
	JOIN `vq_sites` ON `vq_sites`.`id`=`vq_cameras`.`site_id`
	JOIN `vq_users` ON `vq_users`.`id`=`vq_sites`.`user_id`

	WHERE `vq_camera_ftp`.`username`='tomas67'
	AND `vq_camera_ftp`.`password`='159753'
*/
}

bool FTPServer::CheckAuthentification(EventSessionId eventSessionId, const String& rUsername, const String& rPassword, U32* pUserId, U32* pSiteId, U32* pCameraId, bool* pIsArmed, U8* pPersonThreshold)
{
	const String queryString(mSQLQuery.authA + mMain.DatabasePtr->EscapeString(rUsername) + mSQLQuery.authB + mMain.DatabasePtr->EscapeString(rPassword) + '\'');

	Database::Query query(*mMain.DatabasePtr);

	enum
	{
		FIELD_USER_ID = 0,
		FIELD_USER_ACTIVE,
		FIELD_SITE_ID,
		FIELD_CAMERA_ID,
		FIELD_SITE_DELETED,
		FIELD_CAMERA_DELETED,
		FIELD_CAMERA_ARMED,
		FIELD_CAMERA_PERSON_THRESHOLD
	};

	try
	{
		if (!query.Exec(queryString))
			throw Exception("SQL query failed for \"FTPServer::CheckAuthentification\"!");

		if (!query.Next())
			throw Exception("Not registered!"); // Device is not registered in the database.

		// If site or camera was "soft deleted" - don't authenticate.

		const U32 siteId = std::atoi(query.Value(FIELD_SITE_ID));

		if (query.Value(FIELD_SITE_DELETED)) // Site
			throw ExceptionVA("Site (id %u) was soft deleted!", siteId);

		const U32 cameraId = std::atoi(query.Value(FIELD_CAMERA_ID));

		if (query.Value(FIELD_CAMERA_DELETED)) // Camera
			throw ExceptionVA("Camera (id %u) was soft deleted!", cameraId);

		// IMPORTANT:
		// UserId value can hold maximum of 2147483647 (0x7FFFFFFF)
		// This is because we're reserved the MSB to store information if user "is active".
		const U32 userId = std::atoi(query.Value(FIELD_USER_ID));

//		if (userId & 0x80000000)
//			throw ExceptionVA("Database user id value overflow! (%u - %u)", userId, 0x7FFFFFFF);

		// If user is "active" - set MSB to "1".
		if (!query.ValueBool(FIELD_USER_ACTIVE))
			throw ExceptionVA("User (id %u) is not active!", userId);
		

//		userId |= 0x80000000;

		// Get the Database' user id value (Removes the MSB "is active" flag if present)
	//	U32 realUserId = userId & ~0x80000000;

		*pUserId = userId;
		*pSiteId = siteId;
		*pCameraId = cameraId;
		*pIsArmed = query.ValueBool(FIELD_CAMERA_ARMED);
		*pPersonThreshold = query.ValueU8(FIELD_CAMERA_PERSON_THRESHOLD);
	}
	catch (const Exception& e)
	{
		LOG_ERROR(Log::Channel::FTP, "Failed to authenticate FTP user \"%s\"! Reason: %s", rUsername.c_str(), e.GetText());
		return false;
	}

	return true;
}


struct FTPCommandType
{
	const char* name;
	FTPCommand command;
};

FTPCommandType FTPCommandInfo[] =
{
	{ "AUTH",	FTPCommand::AUTH },
	{ "USER",	FTPCommand::USER },
	{ "PASS",	FTPCommand::PASS },
	{ "TYPE",	FTPCommand::TYPE },
	{ "PWD",	FTPCommand::PWD	 },
	{ "CWD",	FTPCommand::CWD  },
	{ "PASV",	FTPCommand::PASV },
	{ "PORT",	FTPCommand::PORT },
	{ "STOR",	FTPCommand::STOR },
	{ "NOOP",	FTPCommand::NOOP },
	{ "MODE",	FTPCommand::MODE },
	{ "STRU",	FTPCommand::STRU },
	{ "DELE",	FTPCommand::DELE },
	{ "RNFR",	FTPCommand::RNFR },
	{ "RNTO",	FTPCommand::RNTO },
	{ "SYST",	FTPCommand::SYST },
	{ "FEAT",	FTPCommand::FEAT },
	{ "LIST",	FTPCommand::LIST },
	{ "ABOR",	FTPCommand::ABOR },
	{ "QUIT",	FTPCommand::QUIT },

	{ nullptr, FTPCommand::Unknown },
};

FTPCommand FTPServer::GetCommandType(char* pBuffer, ssize_t size)
{
	for (FTPCommandType* p = FTPCommandInfo; p->name; p++)
	{
		ssize_t i = 0;

		for (i = 0; i < size; ++i)
		{
			if (p->name[i] != pBuffer[i])
				break;
		}

		if (p->name[i] == '\0')
			return p->command;
	}

	return FTPCommand::Unknown;
}

String FTPServer::GetCommandName(FTPCommand commandType)
{
	if (FTPCommandInfo[static_cast<int>(commandType)].name)
		return FTPCommandInfo[static_cast<int>(commandType)].name;

	return "UNKNOWN";
}
