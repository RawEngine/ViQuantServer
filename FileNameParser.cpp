
#include "PCH.hpp"

#include <regex>

#include "Utils.hpp"
#include "FileNameParser.hpp"

// HikVision FTP file name sample:

// Default:     "192.168.0.64_01_20190319093422939_MOTION_DETECTION.jpg"
//              "10.11.37.189_01_20150917094425492_FACE_DETECTION.jpg"
//              "IP address_channel number_capture time_event type.jpg"

// Prefix "gg":    "gg_733804851_20190319124752348_MOTION_DETECTION.jpg"
//                   |     |_____________________________________________ Camera's serial number.
//					 |___________________________________________________ Prefix.

FileNameParser::FileNameParser(const std::string& rFileName)
{
	// Look for "_20", this will indicate that we're dealing with a timestamp.
	// TODO: Support the device specific filename parsing.

	// https://www.rexegg.com/regex-quickstart.html
	// http://coliru.stacked-crooked.com/a/09d7fa6569086885
//	std::regex r("_(20\\d{15})");
//	std::regex r("_(20\\d{2})(\\d{2})(\\d{2})(\\d{2})(\\d{2})(\\d{2})(\\d{3})");
	std::regex r("_(20\\d{2})(\\d{2})(\\d{2})(\\d{2})(\\d{2})(\\d{2})(\\d{3}).*\\.(\\w{1,4}$)");
	std::smatch m;

	std::ostringstream ss;

	if (std::regex_search(rFileName, m, r))
	{
		ss << m[1] << '-' << m[2] << '-' << m[3] << ' ' << m[4] << ':' << m[5] << ':' << m[6];

		try
		{
			mTimestampMs = static_cast<uint16_t> (std::stoi(m[7].str()));
		}
		catch (const std::invalid_argument) {}
		catch (const std::out_of_range) {}

		mTimestampStr = ss.str(); // "2019-03-14 09:50:09"

		if (Utils::IsEqual(m[8], "jpg") || Utils::IsEqual(m[8], "jpeg"))
		{
			mFileType = FileType::JPEG;
		}
		else if (Utils::IsEqual(m[8], "avi"))
		{
			mFileType = FileType::AVI;
		}
//		else if (Utils::IsEqual(m[8], "mp4"))
//		{
//			mFileType = FileType::MP4;
//		}

		mIsParsed = true;
	}
}

#if 0
String	StringFromDateTime(const DateTime& rTime, bool showMilliseconds /* = false */)
{
	std::stringstream	ss;

	ss << rTime.mYear << "-";
	ss << std::setfill('0') << std::setw(2) << rTime.mMonth << "-";
	ss << std::setfill('0') << std::setw(2) << rTime.mDay << " ";
	ss << std::setfill('0') << std::setw(2) << rTime.mHour << ":";
	ss << std::setfill('0') << std::setw(2) << rTime.mMinute << ":";
	ss << std::setfill('0') << std::setw(2) << rTime.mSecond;

	// Handle milliseconds.
	if (showMilliseconds)
		ss << "." << std::setfill('0') << std::setw(4) << rTime.mMilliseconds;

	return ss.str();
}
#endif
