
#include "PCH.hpp"

#if PLATFORM_WINDOWS
#include <direct.h>
#else
#include <sys/stat.h>	// mkdir
#include <unistd.h>		// readlink
#endif

#include <iomanip> // std::put_time, std::setw

#include "Utils.hpp"
#include "Exception.hpp"

namespace Utils
{
	auto StringFromLocaltime(String& rDateTime, U16& rMS) -> void
	{
		auto now = std::chrono::system_clock::now();
		auto posixTime = std::chrono::system_clock::to_time_t(now);

		auto tm = localtime(&posixTime);

		std::ostringstream ss;
		ss << std::put_time(tm, "%Y-%m-%d %H:%M:%S");
		rDateTime = ss.str();

		auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;
		rMS = ms.count();
	}

	auto StringFromLocaltime(bool needsDate /* = true */, bool needsTime /* = true */, bool needsMS /* = false */) -> String
	{
		auto now = std::chrono::system_clock::now();
		auto posixTime = std::chrono::system_clock::to_time_t(now);

		auto tm = localtime(&posixTime);

		String format;

		if (needsDate && needsTime)
			format = "%Y-%m-%d %H:%M:%S";
		else if (needsDate)
			format = "%Y-%m-%d";
		else
			format = "%H:%M:%S";

		std::ostringstream ss;

		ss << std::put_time(tm, format.c_str());

		if (needsMS)
		{
			auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;

			ss.fill('0');
			ss << '.' << std::setw(3) << ms.count();
		}

		return ss.str();
	}


	// NOTE: Might change at runtime.
	auto Utils::GetCurrentWorkPath() -> String
	{
#if PLATFORM_WINDOWS
		char output[MAX_PATH];
		GetCurrentDirectoryA(MAX_PATH, output);
#else
		// Get current working directory.
		char* path = get_current_dir_name();

		String output(path);

		free(path);
#endif

		return output;
	}

	auto Utils::GetApplicationPath() -> String
	{
#if PLATFORM_WINDOWS
		char buffer[1024];
		::GetModuleFileNameA(nullptr, buffer, 1024);

		String path(buffer);

		const size_t pos = path.find_last_of("\\/");

		path = path.substr(0, pos) + '/';

		std::replace(path.begin(), path.end(), '\\', '/');
#else
		char buffer[1024]{};

		// i.e. "/home/user/projects/Server/bin/x64/Debug/Server.out"
		const auto length = ::readlink("/proc/self/exe", buffer, sizeof(buffer) - 1);

		if (length == -1)
			throw Exception("GetApplicationPath failed for \"readlink\"!");

		String path(buffer);

		const size_t pos = path.find_last_of('/');

		if (pos != String::npos)
			path = path.substr(0, pos + 1); // Get rid of "Server.out"
#endif

		return path; // "/home/user/projects/Server/bin/x64/Debug/"
	}

	// https://stackoverflow.com/questions/675039/how-can-i-create-directory-tree-in-c-linux
	auto Utils::MakePath(String path) -> bool
	{
		//	printf("Utils::MakePath: %s\n", path.c_str());

		if (path[path.size() - 1] != '/')
			path += '/';

#ifndef PLATFORM_WINDOWS
		const mode_t mode = 0777;
#endif
		std::size_t pos = 0;

		while ((pos = path.find_first_of('/', pos)) != String::npos)
		{
			const String dir(path.substr(0, pos++));

			if (dir.size() == 0)
				continue;

#if PLATFORM_WINDOWS
			if (_mkdir(dir.c_str()) != 0 && errno != EEXIST)
#else
			if (mkdir(dir.c_str(), mode) != 0 && errno != EEXIST)
#endif
			{
				printf("Failed to create \"%s\" dir while creating path: \"%s\"!\n", dir.c_str(), path.c_str());
				return false;
			}
		}

		return true;
	}

	// NOTE: "String::ends_with" contains this, but only from C++ 20
	auto Utils::EndsWith(const String& rStr, const String& rEnding) -> bool
	{
		if (rStr.length() < rEnding.length())
			return false;

		return rStr.compare(rStr.length() - rEnding.length(), rEnding.length(), rEnding) == 0;
	}

	auto Utils::EndsWith(const String& rStr, const char c) -> bool
	{
		if (rStr.size() > 0 && rStr[rStr.size() - 1] == c)
			return true;
		/*
			const size_t pos = rStr.find_last_of(c);

			if (pos != String::npos && pos == (rStr.length() - 1))
				return true;
		*/
		return false;
	}

	auto Utils::IsEqual(const String& a, const String& b) -> bool
	{
		return std::equal(
			a.begin(), a.end(),
			b.begin(), b.end(),
			[](char a, char b) { return tolower(a) == tolower(b); });
	}

	auto Utils::StripText(char* pBuffer, size_t length, size_t offset) -> char*
	{
		char* pText = &pBuffer[offset];

		// Remove "CRLF".
		for (size_t i = 0; i < length - offset; ++i)
		{
			if (pText[i] == '\r' || pText[i] == '\n')
			{
				pText[i] = 0;
				break;
			}
		}

		return pText;
	}

	/*
	//Gets the JPEG size from the array of data passed to the function, file reference: http://www.obrador.com/essentialjpeg/headerinfo.htm
	static char get_jpeg_size(unsigned char* data, unsigned int data_size, unsigned short *width, unsigned short *height)
	{
	   //Check for valid JPEG image
	   int i=0;   // Keeps track of the position within the file
	   if(data[i] == 0xFF && data[i+1] == 0xD8 && data[i+2] == 0xFF && data[i+3] == 0xE0)
	   {
		  i += 4;

		  // Check for valid JPEG header (null terminated JFIF)
		  if(data[i+2] == 'J' && data[i+3] == 'F' && data[i+4] == 'I' && data[i+5] == 'F' && data[i+6] == 0x00)
		  {
			 //Retrieve the block length of the first block since the first block will not contain the size of file
			 unsigned short block_length = data[i] * 256 + data[i+1];

			 while(i<data_size)
			 {
				i+=block_length;               //Increase the file index to get to the next block
				if(i >= data_size) return false;   //Check to protect against segmentation faults
				if(data[i] != 0xFF) return false;   //Check that we are truly at the start of another block
				if(data[i+1] == 0xC0)
				{
					//0xFFC0 is the "Start of frame" marker which contains the file size
					//The structure of the 0xFFC0 block is quite simple [0xFFC0][ushort length][uchar precision][ushort x][ushort y]
				   *height = data[i+5]*256 + data[i+6];
				   *width = data[i+7]*256 + data[i+8];
				   return true;
				}
				else
				{
				   i+=2;                              //Skip the block marker
				   block_length = data[i] * 256 + data[i+1];   //Go to the next block
				}
			 }
			 return false;                     //If this point is reached then no size was found
		  }else{ return false; }                  //Not a valid JFIF string

	   }else{ return false; }                     //Not a valid SOI header
	}
	*/
	/*
	// HM: Nera taip paprasta tiesiog surasti markerius + remtis palei JFIF ar tai yra tikrai JPEG'as
	// Yra nemazai JPEG'u kuriuose isviso nebuna JFIF arba jis buna nustumtas kur nors toliau/giliau faile...
	auto Utils::GetJPEGSize(const Vector<char>& rBuffer, U16& w, U16& h) -> bool
	{
		size_t i = 0;

		if (static_cast<uint8_t>(rBuffer[i    ]) == 0xFF &&
			static_cast<uint8_t>(rBuffer[i + 1]) == 0xD8 &&
			static_cast<uint8_t>(rBuffer[i + 2]) == 0xFF &&
			static_cast<uint8_t>(rBuffer[i + 3]) == 0xE0)
		{
			i += 4;

		//	if (data[i + 2] == 'J' && data[i + 3] == 'F' && data[i + 4] == 'I' && data[i + 5] == 'F' && data[i + 6] == 0x00)
		//	{
		//	}
		}

		return false;
	}
	*/
}
