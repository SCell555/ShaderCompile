// CrcShader.cpp : This file contains the 'main' function. Program execution begins and ends there.
//

#include <vector>
#include <fstream>
#include <gsl/gsl>

using int8 = std::int8_t;
using int16 = std::int16_t;
using int32 = std::int32_t;
using int64 = std::int64_t;
using uint8 = std::uint8_t;
using uint16 = std::uint16_t;
using uint32 = std::uint32_t;
using uint64 = std::uint64_t;

#include "CRC32.hpp"

namespace SourceCodeHasher
{
	static __forceinline bool PATHSEPARATOR( char c )
	{
		return c == '\\' || c == '/';
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

	static const char* V_UnqualifiedFileName( const char* in )
	{
		// back up until the character after the first path separator we find,
		// or the beginning of the string
		const char* out = in + strlen( in ) - 1;
		while ( out > in && !PATHSEPARATOR( *( out - 1 ) ) )
			out--;
		return out;
	}

	std::string base_path;
	static void setup_base_path( const char* path )
	{
		char* p = _strdup( path );
		V_StripFilename( p );
		base_path = p;
		free( p );
		base_path += "\\";
	}

	static char* stb_include_load_file( const char* fileName, size_t& len )
	{
		std::ifstream file( base_path + fileName, std::ios::binary | std::ios::ate );
		if ( file.fail() )
			return nullptr;

		std::vector<char> data( gsl::narrow<size_t>( file.tellg() ) );
		{
			const auto& find = []( const char& a, const char& b ) { return a == '\r' && b == '\n'; };
			file.clear();
			file.seekg( 0, std::ios::beg );
			file.read( data.data(), data.size() );
			for ( std::vector<char>::iterator i; ( i = std::adjacent_find( data.begin(), data.end(), find ) ) != data.end(); )
				data.erase( i );

			len = data.size();
			char* text = static_cast<char*>( malloc( data.size() + 1 ) );
			memcpy( text, data.data(), data.size() );
			text[len] = 0;
			return text;
		}
	}

	struct include_info
	{
		intptr_t offset;
		intptr_t end;
		char* filename;
	};

	static include_info* stb_include_append_include( include_info* array, int len, intptr_t offset, intptr_t end, char* filename )
	{
		include_info* z = static_cast<include_info*>( realloc( array, sizeof( include_info ) * ( len + 1 ) ) );
		z[len].offset = offset;
		z[len].end = end;
		z[len].filename = filename;
		return z;
	}

	static void stb_include_free_includes( include_info* array, int len )
	{
		for ( int i = 0; i < len; ++i )
			free( array[i].filename );
		free( array );
	}

	static int stb_include_isspace( int ch )
	{
		return ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n';
	}

	// find location of all #include and #inject
	static int stb_include_find_includes( gsl::span<char> text, include_info** plist )
	{
		int inc_count = 0;
		char* s = text.data();
		char* end = text._Unchecked_end();
		include_info* list = nullptr;
		while ( s < end )
		{
			char* start = s;
			while ( *s == ' ' || *s == '\t' )
				++s;
			if ( *s == '#' )
			{
				++s;
				while ( *s == ' ' || *s == '\t' )
					++s;
				if ( 0 == strncmp( s, "include", 7 ) && stb_include_isspace( s[7] ) )
				{
					s += 7;
					while ( *s == ' ' || *s == '\t' )
						++s;
					if ( *s == '"' )
					{
						char* t = ++s;
						while ( *t != '"' && *t != '\n' && *t != '\r' && *t != 0 )
							++t;
						if ( *t == '"' )
						{
							char* filename = static_cast<char*>( malloc( t - s + 1 ) );
							memcpy( filename, s, t - s );
							filename[t - s] = 0;
							s = t;
							while ( *s != '\r' && *s != '\n' && *s != 0 )
								++s;
							// s points to the newline, so s-start is everything except the newline
							list = stb_include_append_include( list, inc_count++, start - text.data(), s - text.data(), filename );
						}
					}
				}
			}
			while ( *s != '\r' && *s != '\n' && *s != 0 )
				++s;
			if ( *s == '\r' || *s == '\n' )
				s = s + ( s[0] + s[1] == '\r' + '\n' ? 2 : 1 );
		}
		*plist = list;
		return inc_count;
	}

	static char* stb_include_append( char* str, size_t* curlen, gsl::span<char> addstr )
	{
		str = static_cast<char*>( realloc( str, *curlen + addstr.size() ) );
		memcpy( str + *curlen, addstr.data(), addstr.size() );
		*curlen += addstr.size();
		return str;
	}

	static char* stb_include_file( const char* filename, size_t& total );

	static char* stb_include_string( gsl::span<char> str, size_t& total )
	{
		include_info* inc_list;
		const int num = stb_include_find_includes( str, &inc_list );
		char* text = nullptr;
		size_t textlen = 0, last = 0;
		for ( int i = 0; i < num; ++i )
		{
			const include_info& info = inc_list[i];
			text = stb_include_append( text, &textlen, str.subspan( last, info.offset - last ) );
			{
				size_t len = 0;
				char* inc = stb_include_file( info.filename, len );
				total += len;
				if ( inc == nullptr )
				{
					stb_include_free_includes( inc_list, num );
					return nullptr;
				}
				text = stb_include_append( text, &textlen, gsl::make_span( inc, len ) );
				free( inc );
			}
			last = info.end;
			total -= info.end - info.offset;
		}
		text = stb_include_append( text, &textlen, str.subspan( last ) );
		stb_include_free_includes( inc_list, num );
		return text;
	}

	static char* stb_include_file( const char* filename, size_t& total )
	{
		size_t len;
		char* text = stb_include_load_file( filename, len );
		if ( text == nullptr )
			return nullptr;
		total += len;
		char* result = stb_include_string( gsl::make_span( text, len ), total );
		free( text );
		return result;
	}

	static CRC32::CRC32_t CalculateCRC( const char* fileName )
	{
		size_t length = 0;
		setup_base_path( fileName );

		char* src = stb_include_file( V_UnqualifiedFileName( fileName ), length );

		if ( src == nullptr )
			return 0;

		const auto& find = []( const char& a, const char& b ) { return a == '\r' && b == '\n'; };
		std::vector<char> data( src, src + length );
		free( src );
		for ( std::vector<char>::iterator i; ( i = std::adjacent_find( data.begin(), data.end(), find ) ) != data.end(); )
			data.erase( i );

		return CRC32::ProcessSingleBuffer( data.data(), data.size() );
	}
}

int main( int argc, const char* argv[] )
{
	if ( argc != 2 )
		return 0;

	const CRC32::CRC32_t crc = SourceCodeHasher::CalculateCRC( argv[1] );
	printf( "%u", crc );
	return crc;
}