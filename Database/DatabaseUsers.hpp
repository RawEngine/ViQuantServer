
#pragma once

namespace Database
{
	namespace Users
	{
		constexpr auto TableName = "`users`";

		constexpr auto Id		= "`id`";
		constexpr auto Username = "`username`";
		constexpr auto Password = "`password`";

		U32 GetId(Connection& rConnection, const String& rUsername, const String& rPassword);
	};
}
