
#include <PCH.hpp>

#include "Database.hpp"
#include "DatabaseQuery.hpp"

// VisualStudio + Linux - MySQL http://hq.ipas.nl/?page_id=525

namespace Database
{
	Connection::Connection()
	{
		if (!(mpHandle = mysql_init(nullptr)))
			throw Exception("Failed for \"mysql_init\"!");

		mpHandle->reconnect = 1; // Automatic reconnect.
	}

	Connection::~Connection()
	{
		if (mpHandle)
			mysql_close(mpHandle);
	}

	bool Connection::Connect(const Info& rInfo, U32 connectTimeoutSecs /* = 7 */, int numRetries /* = 1 */)
	{
		mInfo = rInfo;

		for (int i = 0; i < numRetries; ++i)
		{
			// The connect timeout in seconds.
			mysql_options(mpHandle, MYSQL_OPT_CONNECT_TIMEOUT, &connectTimeoutSecs);

			if (mysql_real_connect(
				mpHandle,
				rInfo.hostname.c_str(),
				rInfo.username.c_str(),
				rInfo.password.c_str(),
				rInfo.database.c_str(),
				rInfo.port, nullptr, 0))
				break;

			if (i == (numRetries - 1))
			{
				LOG_ERROR(Log::Channel::DB, mysql_error(mpHandle));
				return false;
			}
			else
			{
				LOG_WARNING(Log::Channel::DB, "%s Retrying... (Attempt %d of %d)", mysql_error(mpHandle), i + 1, numRetries);
				continue;
			}
		}

		LOG_MESSAGE(Log::Channel::DB, "Successfully connected to the Database.");
		return true;
	}

	bool Connection::ReConnect()
	{
		return this->Connect(mInfo);
	}

#if 0
/*
#if 0
	Database::CmrFTP::Get(rDatabase,
		{ Database::CmrFTP::CameraId, Database::CmrFTP::SessionTimeout },
		{
			{ Database::CmrFTP::Username, username },
			{ Database::CmrFTP::Password, password },
		});
#endif
*/

	void Connection::Get(
		const String& rTableName,
		const Vector<String>& rSelectList,
		const Vector<std::pair<String, String>>& rFromList)
	{
		std::ostringstream ss;

		ss << "SELECT ";

		for (size_t i = 0; i < rSelectList.size(); ++i)
		{
			ss << rSelectList.at(i);

			if (i != rSelectList.size() - 1)
				ss << ',';
		}

		ss << " FROM " << rTableName << " WHERE ";

		for (size_t i = 0; i < rFromList.size(); ++i)
		{
			auto& r = rFromList.at(i);

			ss << r.first << "=\'" << r.second << "\'";

			if (i != rFromList.size() - 1)
				ss << " AND ";
		}

		Query query(*this);

		if (!query.Exec(ss.str()))
		{
			LOG_ERROR("SQL query failed for \"Database::Connection::Get\"!");
		//	return 0;
		}

		//if (!query.Next())
		//	return 0;

		std::unordered_map<String, Vector<char>> output;

		for (size_t i = 0; i < rSelectList.size(); ++i)
		{
			auto& r = output[rSelectList.at(i)];

		//	r = query.Value(i);
		//	output[rSelectList.at(i)] = Vector<char> (query.Value(i));
		}

	//	return std::atoi(query.Value(0));
	}
#endif

	// TODO: More secure...
	String Connection::EscapeString(const String& rInput)
	{
		Vector<char> output(rInput.length() * 2 + 1, 0);

//		mysql_real_escape_string_quote
		mysql_real_escape_string(mpHandle, output.data(), rInput.c_str(), static_cast<unsigned long>(rInput.length()));

		return output.data();
	}

	/*
	MYSQL_RES* Connection::Query(const String& rSQLQuery)
	{
		if (mysql_ping(mpHandle) != 0)
		{
			LOG_WARNING("Disconnected from the Database! (Trying to reconnec)");
			this->Connect(mDatabaseInfo);
			// TODO...
		}

		if (mysql_query(mpHandle, rSQLQuery.c_str()) != 0)
		{
			LOG_ERROR("Query failed: \"%s\"! (Reason: %s)", rSQLQuery.c_str(), mysql_error(mpHandle));
			return false;
		}

		return true;
	}
	*/
}
