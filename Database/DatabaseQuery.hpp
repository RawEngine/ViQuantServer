
#pragma once

namespace Database
{
	class Query
	{
	public:
		Query(Connection& rDB);
		~Query();

		auto Exec(const String& rQueryString) -> bool;
		auto Next() -> bool;
		auto LastInsertId() const -> U64;
		auto NumResults() const -> U64;

		auto Value(int index) { return mpRow[index]; }

		auto ValueBool(int index) -> bool 
		{ 
			if (!mpRow[index])
				return false;

			return *mpRow[index] != '0';
		}

		auto ValueU8(int index) -> U8
		{
			if (!mpRow[index])
				return 0;

			return static_cast<U8> (std::atoi(mpRow[index]));
		}

		auto ValueU32(int index) -> U32
		{
			if (!mpRow[index])
				return 0;

			return static_cast<U32> (std::atoi(mpRow[index]));
		}

	private:

		Connection&	mConnection;
		MYSQL_RES*	mpResult = nullptr;
		MYSQL_ROW	mpRow = nullptr;
	};
}
