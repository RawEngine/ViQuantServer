
#include <PCH.hpp>

#include "Database.hpp"
#include "DatabaseQuery.hpp"

namespace Database
{
	Query::Query(Connection& rDB) : mConnection(rDB)
	{ }

	Query::~Query()
	{
		if (mpResult)
			mysql_free_result(mpResult);
	}

	auto Query::Exec(const String& rQueryString) -> bool
	{
		auto handle = mConnection.GetHandle();

		if (mysql_ping(handle) != 0)
		{
			LOG_WARNING(Log::Channel::DB, "Disconnected from the Database! (Trying to reconnec)");

			mConnection.ReConnect();
			// TODO...
		}

		if (mysql_query(handle, rQueryString.c_str()) != 0)
		{
			LOG_ERROR(Log::Channel::DB, "Query failed: \"%s\"! (Reason: %s)", rQueryString.c_str(), mysql_error(handle));
			return false;
		}

		mpResult = mysql_store_result(handle);

		return true;
	}

	auto Query::Next() -> bool
	{
		if (!mpResult)
			return false;

		mpRow = mysql_fetch_row(mpResult);

		if (!mpRow)
			return false;

		return true;
	}

	auto Query::LastInsertId() const -> U64
	{
		return static_cast<U64> (mysql_insert_id(mConnection.GetHandle()));
	}

	auto Query::NumResults() const -> U64
	{
		if (!mpResult)
			return 0;

		return static_cast<U64> (mysql_num_rows(mpResult));
	}
}
