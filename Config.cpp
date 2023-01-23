
#include <PCH.hpp>

#include "Config.hpp"

Config::Config(const String& rFilename)
{
	std::ifstream file(rFilename);

	if (!file.is_open())
		throw ExceptionVA("Failed to open: \"%s\"!", rFilename.c_str());

	for (String line; std::getline(file, line); )
	{
		// Skip empty lines and comments.
		if (line.empty() || line.find("//") == 0)
			continue;

		std::size_t pos = line.find_first_of('=');

		if (pos == String::npos)
			continue;

		mMap[line.substr(0, pos)] = line.substr(pos + 1);
	}
}

void Config::Read(const char* pKey, String& r) const
{
	try
	{
		r = mMap.at(pKey);
	}
	catch (const std::out_of_range& e)
	{
		LOG_ERROR(Log::Channel::Main, "Config key \"%s\" not found!", pKey);
	}
}

