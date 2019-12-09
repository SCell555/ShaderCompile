#ifndef BASETYPES_H
#define BASETYPES_H

#pragma once

#include <cstdint>
#ifdef _DEBUG
	#include <cassert>
#endif

using int8   = std::int8_t;
using int16  = std::int16_t;
using int32  = std::int32_t;
using int64  = std::int64_t;
using uint8  = std::uint8_t;
using uint16 = std::uint16_t;
using uint32 = std::uint32_t;
using uint64 = std::uint64_t;

#ifdef _DEBUG
	#define Assert assert
#else
	#define Assert __noop
#endif

template <class T>
constexpr T Clamp( T const& val, T const& minVal, T const& maxVal )
{
	if ( val < minVal )
		return minVal;
	else if ( val > maxVal )
		return maxVal;
	else
		return val;
}

template <class T>
constexpr T Min( T const& val1, T const& val2 )
{
	return val1 < val2 ? val1 : val2;
}

template <class T>
constexpr T Max( T const& val1, T const& val2 )
{
	return val1 > val2 ? val1 : val2;
}

template <typename T>
class CAutoPushPop
{
public:
	explicit CAutoPushPop( T& var )
		: m_rVar( var ), m_valPop( var )
	{
	}
	CAutoPushPop( T& var, T const& valPush )
		: m_rVar( var ), m_valPop( var )
	{
		m_rVar = valPush;
	}

	CAutoPushPop( T& var, T const& valPush, T const& valPop )
		: m_rVar( var ), m_valPop( valPop )
	{
		m_rVar = valPush;
	}

	~CAutoPushPop() { m_rVar = m_valPop; }

	CAutoPushPop( CAutoPushPop const& x ) = delete;
	CAutoPushPop& operator=( CAutoPushPop const& x ) = delete;

public:
	T& Get() { return m_rVar; }

private:
	T& m_rVar;
	T m_valPop;
};

#endif // BASETYPES_H