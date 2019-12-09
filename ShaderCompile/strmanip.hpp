#ifndef STRMANIP_HPP
#define STRMANIP_HPP


static std::string PrettyPrintNumber( uint64 k )
{
	char chCompileString[50] = { 0 };
	char* pchPrint = chCompileString + sizeof( chCompileString ) - 3;
	for ( uint64 j = 0; k > 0; k /= 10, ++j )
	{
		( j && !( j % 3 ) ) ? ( *pchPrint-- = ',' ) : 0;
		*pchPrint-- = '0' + char( k % 10 );
	}
	*++pchPrint ? 0 : *pchPrint = 0;
	return pchPrint;
}

static void __PrettyPrintNumber( std::ios_base& s, uint64 k )
{
	dynamic_cast<std::ostream&>( s ) << PrettyPrintNumber( k );
}

static std::_Smanip<uint64> PrettyPrint( uint64 i )
{
	return { __PrettyPrintNumber, i };
}

static void __FormatTime( std::ios_base& s, int64 nInputSeconds )
{
	int64 nMinutes = nInputSeconds / 60;
	const int64 nSeconds = nInputSeconds - nMinutes * 60;
	const int64 nHours = nMinutes / 60;
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

static void __FormatTime2( std::ios_base& s, int64 nInputSeconds )
{
	int64 nMinutes = nInputSeconds / 60;
	const int64 nSeconds = nInputSeconds - nMinutes * 60;
	const int64 nHours = nMinutes / 60;
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

static std::_Smanip<int64> FormatTime( int64 i )
{
	return { __FormatTime, i };
}

static std::_Smanip<int64> FormatTimeShort( int64 i )
{
	return { __FormatTime2, i };
}

static FORCEINLINE bool PATHSEPARATOR( char c )
{
	return c == '\\' || c == '/';
}

static const char* V_GetFileExtension( const char* path )
{
	const char* src = path + ( strlen( path ) - 1 );

	while ( src != path && *( src - 1 ) != '.' )
		src--;

	if ( src == path || PATHSEPARATOR( *src ) )
		return nullptr; // no extension

	return src;
}

static FORCEINLINE bool V_IsAbsolutePath( const char* pStr )
{
	return pStr[0] && pStr[1] == ':' || pStr[0] == '/' || pStr[0] == '\\';
}

static void V_StripFilename( char* path )
{
	int length = static_cast<int>( strlen( path ) ) - 1;
	if ( length <= 0 )
		return;

	while ( length > 0 && !PATHSEPARATOR( path[length] ) )
		length--;

	path[length] = 0;
}

static void V_FixSlashes( char* pname, char separator = '\\' )
{
	while ( *pname )
	{
		if ( *pname == '/' || *pname == '\\' )
			*pname = separator;
		pname++;
	}
}

static void V_StrTrim( char* pStr )
{
	char* pSource = pStr;
	char* pDest = pStr;

	// skip white space at the beginning
	while ( *pSource != 0 && isspace( *pSource ) )
		pSource++;

	// copy everything else
	char* pLastWhiteBlock = nullptr;
	while ( *pSource != 0 )
	{
		*pDest = *pSource++;
		if ( isspace( *pDest ) )
		{
			if ( pLastWhiteBlock == nullptr )
				pLastWhiteBlock = pDest;
		}
		else
			pLastWhiteBlock = nullptr;
		pDest++;
	}
	*pDest = 0;

	// did we end in a whitespace block?
	if ( pLastWhiteBlock != nullptr )
		// yep; shorten the string
		*pLastWhiteBlock = 0;
}

#endif // STRMANIP_HPP