
#pragma once

class Config
{
public:
	Config(const String& rFilename);

	void Read(const char* pKey, String& r) const;

	template<typename T>
	inline void Read(const char* pKey, T& r) const
	{
//		Utils::StringTo(mMap.at(pKey), r);

		try
		{
			r = static_cast<T> (std::stoi(mMap.at(pKey)));
		}
		catch (const std::invalid_argument& e)
		{
			LOG_ERROR(Log::Channel::Main, "Config key \"%s\" value is not integer!", pKey);
			r = 0;
		}
		catch (const std::out_of_range& e)
		{
			LOG_ERROR(Log::Channel::Main, "Config key \"%s\" not found!", pKey);
			r = 0;
		}
	}

private:

	std::unordered_map<String, String> mMap;
};