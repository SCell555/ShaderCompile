#pragma once

#include <iomanip>
#include <string_view>

template <class _Arg>
struct _Smanip2
{
	friend std::ostream& operator<<(std::ostream& _Ostr, const _Smanip2& _Manip)
	{
		(*_Manip._Pfun)(_Ostr, _Manip._Manarg);
		return _Ostr;
	}

	void(__cdecl* _Pfun)(std::ostream&, _Arg);
	_Arg _Manarg;
};

static inline void __PrettyPrintNumber( std::ostream& s, uint64_t k )
{
	char chCompileString[50] = { 0 };
	char* pchPrint = chCompileString + sizeof( chCompileString ) - 3;
	for ( uint64_t j = 0; k > 0; k /= 10, ++j )
	{
		( j && !( j % 3 ) ) ? ( *pchPrint-- = ',' ) : 0;
		*pchPrint-- = '0' + char( k % 10 );
	}
	*++pchPrint ? 0 : *pchPrint = 0;
	s << pchPrint;
}

static inline _Smanip2<uint64_t> PrettyPrint( uint64_t i )
{
	return { __PrettyPrintNumber, i };
}

static inline void __FormatTime( std::ostream& s, int64_t nInputSeconds )
{
	int64_t nMinutes = nInputSeconds / 60;
	const int64_t nSeconds = nInputSeconds - nMinutes * 60;
	const int64_t nHours = nMinutes / 60;
	nMinutes -= nHours * 60;

	static constexpr const std::string_view extra[2] = { "", "s" };

	s << std::setfill( '0' );
	if ( nHours > 0 )
		s << clr::green << nHours << clr::reset << " hour" << extra[nHours != 1] << ", " << clr::green << std::setw( 2 ) << nMinutes << clr::reset << " minute" << extra[nMinutes != 1] << ", " << clr::green << std::setw( 2 ) << nSeconds << clr::reset << " second" << extra[nSeconds != 1];
	else if ( nMinutes > 0 )
		s << clr::green << nMinutes << clr::reset << " minute" << extra[nMinutes != 1] << ", " << clr::green << std::setw( 2 ) << nSeconds << clr::reset << " second" << extra[nSeconds != 1];
	else
		s << clr::green << nSeconds << clr::reset << " second" << extra[nSeconds != 1];
	s << std::setfill( ' ' );
}

static inline void __FormatTime2( std::ostream& s, int64_t nInputSeconds )
{
	int64_t nMinutes = nInputSeconds / 60;
	const int64_t nSeconds = nInputSeconds - nMinutes * 60;
	const int64_t nHours = nMinutes / 60;
	nMinutes -= nHours * 60;

	static constexpr const std::string_view extra[2] = { "", "s" };

	s << std::setfill( '0' );
	if ( nHours > 0 )
		s << clr::green << nHours << clr::reset << ":" << clr::green << std::setw( 2 ) << nMinutes << clr::reset << ":" << clr::green << std::setw( 2 ) << nSeconds << clr::reset;
	else if ( nMinutes > 0 )
		s << clr::green << nMinutes << clr::reset << ":" << clr::green << std::setw( 2 ) << nSeconds << clr::reset;
	else
		s << clr::green << nSeconds << clr::reset << " second" << extra[nSeconds != 1];
	s << std::setfill( ' ' );
}

static inline _Smanip2<int64_t> FormatTime( int64_t i )
{
	return { __FormatTime, i };
}

static inline _Smanip2<int64_t> FormatTimeShort( int64_t i )
{
	return { __FormatTime2, i };
}

static inline bool V_IsAbsolutePath( const char* pStr )
{
	return ( pStr[0] && pStr[1] == ':' ) || pStr[0] == '/' || pStr[0] == '\\';
}