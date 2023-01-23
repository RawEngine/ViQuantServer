
#pragma once

enum class FileType : uint8_t
{
	Unknown,
	JPEG,
	AVI,
//	MP4,
};

class FileNameParser
{
public:
	FileNameParser(const std::string& rFileName);

	auto IsParsed() const { return mIsParsed; }
	auto IsFileType(FileType type) const { return mFileType == type; }
//	auto GetFileType() const	{ return mFileType; }
	auto GetTimestampMs() const { return mTimestampMs; }
	auto GetTimestampStr() const -> const std::string& { return mTimestampStr; }

private:

	FileType	mFileType = FileType::Unknown;
	uint16_t	mTimestampMs = 0;
	std::string mTimestampStr;
	bool		mIsParsed = false;
};
