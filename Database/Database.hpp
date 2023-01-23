
#pragma once

#include <mysql/mysql.h>

namespace Database
{
	struct Info
	{
		U16	port = 0;
		String hostname;
		String username;
		String password;
		String database;
	};

	class Connection
	{
		friend class Query;
	public:
		Connection();
		~Connection();

		bool Connect(const Info& rInfo, U32 connectTimeoutSecs = 7, int numRetries = 1);
		bool ReConnect();

#if 0
		auto Get(
			const String& rTableName,
			const Vector<String>& rSelectList,
			const Vector<std::pair<String, String>>& rFromList) -> void;
#endif
		auto EscapeString(const String& rInput) -> String;

		auto GetHandle() { return mpHandle; }

	private:

		Info	mInfo;
		MYSQL*	mpHandle = nullptr;
	};

} // namespace Database
