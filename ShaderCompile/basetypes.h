#ifndef BASETYPES_H
#define BASETYPES_H

#pragma once

#include <cstdint>
#ifdef _DEBUG
	#include <cassert>
#endif

#ifdef _DEBUG
	#define Assert assert
#else
	#define Assert __noop
#endif

#endif // BASETYPES_H