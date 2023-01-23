
#pragma once

// Forward declarations.
class Log;
class Config;
class ThreadPool;
class EventManager;
class FTPServer;
class APIServer;
class Analytics;
class CGIManager;

extern std::atomic_bool gIsQuitRequested;

namespace Database { struct Info; class Connection; }

class Main
{
public:
	Main();
	~Main();

	int Run();

	auto CreateFootagePath(EventId eventId, U32 userId, U32 siteId, U32 cameraId) const -> String;

	const String& GetPathApplication();
	const String& GetPathFootage() const;

	UniquePtr<Log>					LogFilePtr;
	UniquePtr<Config>				ConfigPtr;
	UniquePtr<Database::Connection>	DatabasePtr;
	UniquePtr<ThreadPool>			ThreadPoolPtr;
	UniquePtr<EventManager>			EventManagerPtr;
	UniquePtr<Analytics>			AnalyticsPtr;
	UniquePtr<FTPServer>			FTPServerPtr;
	UniquePtr<APIServer>			APIServerPtr;
	UniquePtr<CGIManager>			CGIManagerPtr;

private:

	void SetupLogSystem();
	void SetupFootagePath();
	void SetupNotificationsManager();
	void SetupDatabaseConnection(Database::Info& rDBInfo);
	void SetupThreadPool();
	void SetupEventManager();
	void SetupAnalytics(const Database::Info& rDBInfo);
	void SetupFTPServer();
	void SetupAPIServer();

	struct
	{
		String application;
		String footage;
//		String appData;
//		String userDocuments;
	} mPathFor;
};
