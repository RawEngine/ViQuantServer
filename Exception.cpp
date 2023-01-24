
#include <PCH.hpp>

#include "Exception.hpp"

#include <string.h>	// strncpy
#include <cstdarg>	// va_start
#include <cstdio>	// vsnprintf

Exception::Exception(const char* pText)
{
//	auto length = strlen(pText);

	strncpy(mText, pText, ExceptionTextSize);

//	mText[length] = 0;
}

ExceptionVA::ExceptionVA(const char* pFormat, ...)
{
	va_list args;
	va_start(args, pFormat);

	// Returns the number of characters written (not including the terminating null) 
	// Or a negative value if an output error occurs.
	/*		const int length = */vsnprintf(mText, sizeof(mText) - 1, pFormat, args);
	//		const int length = vsprintf_s(mText, pFormat, args);

	va_end(args);

	mText[sizeof(mText) - 1] = 0;
}
