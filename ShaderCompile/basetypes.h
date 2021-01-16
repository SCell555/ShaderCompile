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