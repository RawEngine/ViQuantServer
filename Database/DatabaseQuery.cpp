
#include <PCH.hpp>

#include "Database.hpp"
#include "DatabaseQuery.hpp"

namespace Database
{
	Query::Query(Connection& rDB) 
		: mConnection(rDB)
	{ }

	Query::~Query()
	{
		if (mpResult)
			mysql_free_result(mpResult);
	}

	bool Query::Exec(const String& rQueryString)
	{
		auto handle = mConnection.GetHandle();

		if (mysql_ping(handle) != 0)
		{
			LOG_WARNING(Log::Channel::DB, "Disconnected from the Database! (Trying to reconnec)");
			mConnection.ReConnect();
		}

		if (mysql_query(handle, rQueryString.c_str()) != 0)
		{
			LOG_ERROR(Log::Channel::DB, "Query failed: \"%s\"! (Reason: %s)", rQueryString.c_str(), mysql_error(handle));
			return false;
		}

		mpResult = mysql_store_result(handle);

		return true;
	}

	bool Query::Next()
	{
		if (!mpResult)
			return false;

		mpRow = mysql_fetch_row(mpResult);

		if (!mpRow)
			return false;

		return true;
	}

	U64 Query::LastInsertId() const
	{
		return static_cast<U64> (mysql_insert_id(mConnection.GetHandle()));
	}

	U64 Query::NumResults() const
	{
		if (!mpResult)
			return 0;

		return static_cast<U64> (mysql_num_rows(mpResult));
	}
}
