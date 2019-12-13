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

	char* stb_include_load_file( const char* fileName, size_t& len )
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
}

int main( int argc, const char* argv[] )
{
	if ( argc != 2 )
		return 0;

	SourceCodeHasher::setup_base_path( argv[1] );
	const CRC32::CRC32_t crc = SourceCodeHasher::CalculateCRC( SourceCodeHasher::V_UnqualifiedFileName( argv[1] ) );
	printf( "%u", crc );
	return crc;
}