
#pragma once

namespace Database
{
	class Query
	{
	public:
		Query(Connection& rDB);
		~Query();

		bool Exec(const String& rQueryString);
		bool Next();
		U64 LastInsertId() const;
		U64 NumResults() const;

		auto Value(int index) { return mpRow[index]; }

		bool ValueBool(int index)
		{ 
			if (!mpRow[index])
				return false;

			return *mpRow[index] != '0';
		}

		U8 ValueU8(int index)
		{
			if (!mpRow[index])
				return 0;

			return static_cast<U8> (std::atoi(mpRow[index]));
		}

		U32 ValueU32(int index)
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
