#pragma once

#include <cstdint>
#ifdef _DEBUG
	#include <cassert>
#endif

#ifdef _DEBUG
	#define Assert assert
#else
	#define Assert(e)
#endif

#ifndef WIN32
#ifndef strcpy_s
#define strcpy_s(dst, n, src) strncpy(dst, src, n)
#endif
#ifndef sprintf_s
#define sprintf_s snprintf
#endif
#define _strupr_s strupr
#define _strdup strdup
#endif