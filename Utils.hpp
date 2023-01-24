
#pragma once

namespace Utils
{
	void StringFromLocaltime(String& rDateTime, U16& rMS);
	String StringFromLocaltime(bool needsDate = true, bool needsTime = true, bool needsMS = false);

	String GetCurrentWorkPath();
	String GetApplicationPath();

	bool MakePath(String path);
	bool EndsWith(const String& rStr, const String& rEnding);
	bool EndsWith(const String& rStr, const char c);
	bool IsEqual(const String& a, const String& b);
//	bool GetJPEGSize(const Vector<char>& rBuffer, U16& w, U16& h);

	char* StripText(char* pBuffer, size_t length, size_t offset);

	// Converts any type of value (integer, char, etc) to a bitmask string.
	// Sample:
	// uint8_t		   value = 1 << 1;
	// String mask	 = ValueToBitmaskString(value);
	// RESULT: Value "mask" will be = "00000010";
	template <typename T>
	String ValueToBitmaskString(T item)
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
	bool StringTo(const char* pValue, T& r)
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
