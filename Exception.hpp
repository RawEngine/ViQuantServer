
#pragma once

constexpr int ExceptionTextSize = 2048;

class Exception
{
public:
	Exception() { };
	Exception(const char* pText);

	const char* GetText() const { return mText; }

protected:

	char mText[ExceptionTextSize];
};

class ExceptionVA : public Exception
{
public:
	ExceptionVA(const char* pFormat, ...);
};
