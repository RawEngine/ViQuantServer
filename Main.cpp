
#include "PCH.hpp"

#include "Main.hpp"
#include "Config.hpp"
#include "ThreadPool.hpp"
#include "Socket.hpp"

#include "Database/Database.hpp"

#include "EventManager.hpp"

#include "Analytics/Analytics.hpp"

#include "FTPServer.hpp"
#include "Utils.hpp"

#include "API/APIServer.hpp"
#include "CGI/CGIManager.hpp"

#include <csignal>	// signal

std::atomic_bool gIsQuitRequested = {false};

void SignalHandler(int n)
{
	LOG_WARNING(Log::Channel::Main, "Signal \"%d\" detected! (Stopping the application)", n);

	gIsQuitRequested = true;
}

Main::Main()
{
#if PLATFORM_WINDOWS
	WSADATA wsaData;
	WSAStartup(MAKEWORD(2, 2), &wsaData);
#endif
	// This will let us to determine when our application will need to be closed.
	signal(SIGABRT, &SignalHandler);
	signal(SIGTERM, &SignalHandler);
	signal(SIGINT, &SignalHandler);

	mysql_library_init(0, nullptr, nullptr);
}

Main::~Main()
{
	mysql_library_end();

#if PLATFORM_WINDOWS
	WSACleanup();
#endif
}

String ThreadIdToString(const std::thread::id& id) 
{
	std::stringstream ss;
	ss << id;
	return ss.str();
}

int Main::Run()
{
	Database::Info dbInfo;

	try
	{
		this->ConfigPtr = std::make_unique<Config>(this->GetPathApplication() + "Server.conf");

		this->SetupLogSystem();

		LOG_MESSAGE(Log::Channel::Main, "Num CPU cores: %d", std::thread::hardware_concurrency());
		LOG_MESSAGE(Log::Channel::Main, "Main thread id: %s", ThreadIdToString(std::this_thread::get_id()).c_str());
		LOG_MESSAGE(Log::Channel::Main, "Work path: %s", this->GetPathApplication().c_str());

		this->SetupFootagePath();

		this->SetupNotificationsManager();

		this->SetupDatabaseConnection(dbInfo);

		this->SetupThreadPool();

		this->SetupEventManager();

		this->SetupAnalytics(dbInfo);

		// TEMP!
//		this->AnalyticsPtr->AddEvent(777, this->CreateFootagePath(1, 2, 3, 4));
//		this->AnalyticsPtr->AddFootage(777, "4_192.168.0.64_01_20190306121007711_MOTION_DETECTION.jpg");

		this->SetupFTPServer();

		this->SetupAPIServer();

		while (!gIsQuitRequested)
		{
			this->APIServerPtr->Update();
#if 1
			this->FTPServerPtr->HandleNewConnection();

			this->FTPServerPtr->HandleClients(*this->DatabasePtr);

			this->FTPServerPtr->HandleTimeouts();

			this->FTPServerPtr->HandleInactiveClients();
#endif
			this->EventManagerPtr->HandleTimeouts();

			this->EventManagerPtr->HandleQueuedFootageNotices();

			this->CGIManagerPtr->Process();

//			printf("Peu...\n");
//			std::this_thread::sleep_for(std::chrono::seconds(3));
		}

		// TEMP!
		this->AnalyticsPtr->EndEvent(777);
	}
	catch (const Exception& e)
	{
		if (this->LogFilePtr)
			this->LogFilePtr->Write(Log::Channel::Main, Log::Level::Error, __LINE__, __FUNCTION__, e.GetText());
		else
			std::cout << "ERROR: [LOG NOT INITIALIZED] : " << e.GetText() << std::endl;

		return EXIT_FAILURE;
	}

	return EXIT_SUCCESS;
}

auto Main::CreateFootagePath(EventId eventId, U32 userId, U32 siteId, U32 cameraId) const -> String
{
	String footagePath(this->GetPathFootage());
	{
		std::ostringstream ss;

		ss << userId << '/' << eventId << '/' << siteId << '/' << cameraId << '/';

		footagePath += ss.str();
	}

	Utils::MakePath(footagePath);

	return footagePath;
}

const String& Main::GetPathApplication()
{
	if (mPathFor.application.empty())
		mPathFor.application = Utils::GetApplicationPath();

	return mPathFor.application;
}

const String& Main::GetPathFootage() const
{
	return mPathFor.footage;
}

void Main::SetupLogSystem()
{
	String logPath;

	this->ConfigPtr->Read("log_path", logPath);

	if (logPath.empty())
		throw Exception("Config key \"log_path\" is missing the value!");

	if (!Utils::EndsWith(logPath, '/'))
		logPath += '/';

	logPath = this->GetPathApplication() + logPath;

	if (!Utils::MakePath(logPath))
		throw ExceptionVA("Failed to setup log path: \"%s\"!", logPath.c_str());

	this->LogFilePtr = std::make_unique<Log>(logPath);
}

void Main::SetupNotificationsManager()
{
	String hostname;
	U16 port;

	this->ConfigPtr->Read("notifications_host", hostname);
	this->ConfigPtr->Read("norifications_port", port);

	if (hostname.empty())	throw Exception("Config key \"notifications_host\" is missing the value!");
	if (port == 0)			throw Exception("Config key \"norifications_port\" is missing the value!");

	this->CGIManagerPtr = std::make_unique<CGIManager>(hostname, port);
}

void Main::SetupFootagePath()
{
	// TEMP.
	if (mPathFor.footage.empty())
		mPathFor.footage = this->GetPathApplication() + "footage/";

	Utils::MakePath(mPathFor.footage);
}

void Main::SetupDatabaseConnection(Database::Info& rDBInfo)
{
	this->ConfigPtr->Read("db_port", rDBInfo.port);
	this->ConfigPtr->Read("db_hostname", rDBInfo.hostname);
	this->ConfigPtr->Read("db_username", rDBInfo.username);
	this->ConfigPtr->Read("db_password", rDBInfo.password);
	this->ConfigPtr->Read("db_name", rDBInfo.database);

	// Connect to the Database.
	this->DatabasePtr = std::make_unique<Database::Connection>();

	LOG_MESSAGE(Log::Channel::Main, "Connecting to the Database (%s:%d)", rDBInfo.hostname.c_str(), rDBInfo.port);

	if (!this->DatabasePtr->Connect(rDBInfo, 7, 3))
		throw Exception("Failed to connect to the Database!");
}

void Main::SetupThreadPool()
{
	this->ThreadPoolPtr = std::make_unique<ThreadPool>(2);
}

void Main::SetupEventManager()
{
	U32 eventSessionTimoutSec;

	this->ConfigPtr->Read("event_session_timeout_sec", eventSessionTimoutSec);

	this->EventManagerPtr = std::make_unique<EventManager> (*this, eventSessionTimoutSec);
}

void Main::SetupAnalytics(const Database::Info& rDBInfo)
{
	String serverAddres;
	U16	serverPort;
	U16	connectTimeoutSec;

	this->ConfigPtr->Read("analytics_address", serverAddres);
	this->ConfigPtr->Read("analytics_port", serverPort);
	this->ConfigPtr->Read("analytics_connect_timeout_sec", connectTimeoutSec);

	this->AnalyticsPtr = std::make_unique<Analytics>(*this, rDBInfo, serverAddres, serverPort, connectTimeoutSec);
}

void Main::SetupFTPServer()
{
	U16 port;
	U32 passiveSocketTimeout;

	this->ConfigPtr->Read("ftp_port", port);
	this->ConfigPtr->Read("ftp_passive_soc_timeout_sec", passiveSocketTimeout);

	if (port == 0)
		throw Exception("Config is missing FTP server port!");

	if (passiveSocketTimeout == 0)
	{
		passiveSocketTimeout = 13;
		LOG_WARNING(Log::Channel::Main, "Config \"ftp_passive_soc_timeout_sec\" not set! (Using default, %u seconds)", passiveSocketTimeout);
	}

	this->FTPServerPtr = std::make_unique<FTPServer>(*this, passiveSocketTimeout);

	if (!this->FTPServerPtr->Start(port))
		throw Exception("FTP server failed to start!");
}

void Main::SetupAPIServer()
{
	U16 port;

	this->ConfigPtr->Read("api_port", port);

	if (port == 0)
		throw Exception("Config is missing API server port!");

	this->APIServerPtr = std::make_unique<APIServer>(*this);

	if (!this->APIServerPtr->Start(port))
		throw Exception("API server failed to start!");
}

// Main application entry point.
int main(int argc, char *argv[])
{
	int result = 0;

	try
	{
		Main app;

		result = app.Run();
	}
	catch (const Exception& e)
	{
		printf("AAAAAAAAAA: %s\n", e.GetText());
	}

	return result;
}