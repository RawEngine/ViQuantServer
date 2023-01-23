
#pragma once

namespace Utils
{
	auto StringFromLocaltime(String& rDateTime, U16& rMS) -> void;
	auto StringFromLocaltime(bool needsDate = true, bool needsTime = true, bool needsMS = false) -> String;

	auto GetCurrentWorkPath() -> String;
	auto GetApplicationPath() -> String;
	auto MakePath(String path) -> bool;
	auto EndsWith(const String& rStr, const String& rEnding) -> bool;
	auto EndsWith(const String& rStr, const char c) -> bool;
	auto IsEqual(const String& a, const String& b) -> bool;
//	auto GetJPEGSize(const Vector<char>& rBuffer, U16& w, U16& h) -> bool;

	auto StripText(char* pBuffer, size_t length, size_t offset) -> char*;

	// Converts any type of value (integer, char, etc) to a bitmask string.
	// Sample:
	// uint8_t		   value = 1 << 1;
	// String mask	 = ValueToBitmaskString(value);
	// RESULT: Value "mask" will be = "00000010";
	template <typename T>
	auto ValueToBitmaskString(T item) -> String
	{
		std::ostringstream ss;

		int itemSize = sizeof(T) * 8;	// Item size in bits.

		for (int i = itemSize - 1; i >= 0; --i)
		{
			int bit = int((item >> i) & 1);
			ss << bit;

			if (!(i % 8))
				ss << '.';
		}

		return ss.str();
	}

	template <typename T>
	auto StringTo(const char* pValue, T& r) -> bool
	{
		try
		{
			r = static_cast<T> (std::stoi(pValue));
		}
		catch (const std::invalid_argument& e)
		{
			r = 0;
			return false;
		}
		catch (const std::out_of_range& e)
		{
			r = 0;
			return false;
		}

		return true;
	}
}

