
#include <PCH.hpp>

#include "Database.hpp"
#include "DatabaseQuery.hpp"
#include "DatabaseUsers.hpp"

namespace Database
{
	namespace Users
	{
		auto GetId(Connection& rConnection, const String& rUsername, const String& rPassword) -> U32
		{
			std::ostringstream ss;

			ss << "SELECT "		<< Id 
				<< " FROM "		<< TableName
				<< " WHERE "	<< Username << "=\'" << rConnection.EscapeString(rUsername)
				<< "\' AND "	<< Password << "=\'" << rConnection.EscapeString(rPassword) << "\'";

			Query query(rConnection);

			if (!query.Exec(ss.str()))
			{
				LOG_ERROR(Log::Channel::DB, "SQL query failed for \"Database::Users::GetId\"!");
				return 0;
			}

			if (!query.Next())
				return 0;

			return std::atoi(query.Value(0));
		}
	};
}
