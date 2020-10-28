#ifndef STRMANIP_HPP
#define STRMANIP_HPP

#include <iomanip>

static inline std::string PrettyPrintNumber( uint64_t k )
{
	char chCompileString[50] = { 0 };
	char* pchPrint = chCompileString + sizeof( chCompileString ) - 3;
	for ( uint64_t j = 0; k > 0; k /= 10, ++j )
	{
		( j && !( j % 3 ) ) ? ( *pchPrint-- = ',' ) : 0;
		*pchPrint-- = '0' + char( k % 10 );
	}
	*++pchPrint ? 0 : *pchPrint = 0;
	return pchPrint;
}

static inline void __PrettyPrintNumber( std::ios_base& s, uint64_t k )
{
	dynamic_cast<std::ostream&>( s ) << PrettyPrintNumber( k );
}

static inline std::_Smanip<uint64_t> PrettyPrint( uint64_t i )
{
	return { __PrettyPrintNumber, i };
}

static inline void __FormatTime( std::ios_base& s, int64_t nInputSeconds )
{
	int64_t nMinutes = nInputSeconds / 60;
	const int64_t nSeconds = nInputSeconds - nMinutes * 60;
	const int64_t nHours = nMinutes / 60;
	nMinutes -= nHours * 60;

	constexpr const char* const extra[2] = { "", "s" };

	auto& str = dynamic_cast<std::ostream&>( s );
	if ( nHours > 0 )
		str << clr::green << nHours << clr::reset << " hour" << extra[nHours != 1] << ", " << clr::green << nMinutes << clr::reset << " minute" << extra[nMinutes != 1] << ", " << clr::green << nSeconds << clr::reset << " second" << extra[nSeconds != 1];
	else if ( nMinutes > 0 )
		str << clr::green << nMinutes << clr::reset << " minute" << extra[nMinutes != 1] << ", " << clr::green << nSeconds << clr::reset << " second" << extra[nSeconds != 1];
	else
		str << clr::green << nSeconds << clr::reset << " second" << extra[nSeconds != 1];
}

static inline void __FormatTime2( std::ios_base& s, int64_t nInputSeconds )
{
	int64_t nMinutes = nInputSeconds / 60;
	const int64_t nSeconds = nInputSeconds - nMinutes * 60;
	const int64_t nHours = nMinutes / 60;
	nMinutes -= nHours * 60;

	constexpr const char* const extra[2] = { "", "s" };

	auto& str = dynamic_cast<std::ostream&>( s );
	if ( nHours > 0 )
		str << clr::green << nHours << clr::reset << ":" << clr::green << nMinutes << clr::reset << ":" << clr::green << nSeconds << clr::reset;
	else if ( nMinutes > 0 )
		str << clr::green << nMinutes << clr::reset << ":" << clr::green << nSeconds << clr::reset;
	else
		str << clr::green << nSeconds << clr::reset << " second" << extra[nSeconds != 1];
}

static inline std::_Smanip<int64_t> FormatTime( int64_t i )
{
	return { __FormatTime, i };
}

static inline std::_Smanip<int64_t> FormatTimeShort( int64_t i )
{
	return { __FormatTime2, i };
}

static __forceinline bool V_IsAbsolutePath( const char* pStr )
{
	return ( pStr[0] && pStr[1] == ':' ) || pStr[0] == '/' || pStr[0] == '\\';
}

#endif // STRMANIP_HPP