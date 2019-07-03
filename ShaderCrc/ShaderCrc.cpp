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

namespace CRC32
{
	static constexpr auto CRC32_INIT_VALUE = 0xFFFFFFFFUL;
	static constexpr auto CRC32_XOR_VALUE = 0xFFFFFFFFUL;

	typedef unsigned int CRC32_t;

	static constexpr auto NUM_BYTES = 256;
	static constexpr const CRC32_t pulCRCTable[NUM_BYTES] =
	{
		0x00000000, 0x77073096, 0xee0e612c, 0x990951ba,
		0x076dc419, 0x706af48f, 0xe963a535, 0x9e6495a3,
		0x0edb8832, 0x79dcb8a4, 0xe0d5e91e, 0x97d2d988,
		0x09b64c2b, 0x7eb17cbd, 0xe7b82d07, 0x90bf1d91,
		0x1db71064, 0x6ab020f2, 0xf3b97148, 0x84be41de,
		0x1adad47d, 0x6ddde4eb, 0xf4d4b551, 0x83d385c7,
		0x136c9856, 0x646ba8c0, 0xfd62f97a, 0x8a65c9ec,
		0x14015c4f, 0x63066cd9, 0xfa0f3d63, 0x8d080df5,
		0x3b6e20c8, 0x4c69105e, 0xd56041e4, 0xa2677172,
		0x3c03e4d1, 0x4b04d447, 0xd20d85fd, 0xa50ab56b,
		0x35b5a8fa, 0x42b2986c, 0xdbbbc9d6, 0xacbcf940,
		0x32d86ce3, 0x45df5c75, 0xdcd60dcf, 0xabd13d59,
		0x26d930ac, 0x51de003a, 0xc8d75180, 0xbfd06116,
		0x21b4f4b5, 0x56b3c423, 0xcfba9599, 0xb8bda50f,
		0x2802b89e, 0x5f058808, 0xc60cd9b2, 0xb10be924,
		0x2f6f7c87, 0x58684c11, 0xc1611dab, 0xb6662d3d,
		0x76dc4190, 0x01db7106, 0x98d220bc, 0xefd5102a,
		0x71b18589, 0x06b6b51f, 0x9fbfe4a5, 0xe8b8d433,
		0x7807c9a2, 0x0f00f934, 0x9609a88e, 0xe10e9818,
		0x7f6a0dbb, 0x086d3d2d, 0x91646c97, 0xe6635c01,
		0x6b6b51f4, 0x1c6c6162, 0x856530d8, 0xf262004e,
		0x6c0695ed, 0x1b01a57b, 0x8208f4c1, 0xf50fc457,
		0x65b0d9c6, 0x12b7e950, 0x8bbeb8ea, 0xfcb9887c,
		0x62dd1ddf, 0x15da2d49, 0x8cd37cf3, 0xfbd44c65,
		0x4db26158, 0x3ab551ce, 0xa3bc0074, 0xd4bb30e2,
		0x4adfa541, 0x3dd895d7, 0xa4d1c46d, 0xd3d6f4fb,
		0x4369e96a, 0x346ed9fc, 0xad678846, 0xda60b8d0,
		0x44042d73, 0x33031de5, 0xaa0a4c5f, 0xdd0d7cc9,
		0x5005713c, 0x270241aa, 0xbe0b1010, 0xc90c2086,
		0x5768b525, 0x206f85b3, 0xb966d409, 0xce61e49f,
		0x5edef90e, 0x29d9c998, 0xb0d09822, 0xc7d7a8b4,
		0x59b33d17, 0x2eb40d81, 0xb7bd5c3b, 0xc0ba6cad,
		0xedb88320, 0x9abfb3b6, 0x03b6e20c, 0x74b1d29a,
		0xead54739, 0x9dd277af, 0x04db2615, 0x73dc1683,
		0xe3630b12, 0x94643b84, 0x0d6d6a3e, 0x7a6a5aa8,
		0xe40ecf0b, 0x9309ff9d, 0x0a00ae27, 0x7d079eb1,
		0xf00f9344, 0x8708a3d2, 0x1e01f268, 0x6906c2fe,
		0xf762575d, 0x806567cb, 0x196c3671, 0x6e6b06e7,
		0xfed41b76, 0x89d32be0, 0x10da7a5a, 0x67dd4acc,
		0xf9b9df6f, 0x8ebeeff9, 0x17b7be43, 0x60b08ed5,
		0xd6d6a3e8, 0xa1d1937e, 0x38d8c2c4, 0x4fdff252,
		0xd1bb67f1, 0xa6bc5767, 0x3fb506dd, 0x48b2364b,
		0xd80d2bda, 0xaf0a1b4c, 0x36034af6, 0x41047a60,
		0xdf60efc3, 0xa867df55, 0x316e8eef, 0x4669be79,
		0xcb61b38c, 0xbc66831a, 0x256fd2a0, 0x5268e236,
		0xcc0c7795, 0xbb0b4703, 0x220216b9, 0x5505262f,
		0xc5ba3bbe, 0xb2bd0b28, 0x2bb45a92, 0x5cb36a04,
		0xc2d7ffa7, 0xb5d0cf31, 0x2cd99e8b, 0x5bdeae1d,
		0x9b64c2b0, 0xec63f226, 0x756aa39c, 0x026d930a,
		0x9c0906a9, 0xeb0e363f, 0x72076785, 0x05005713,
		0x95bf4a82, 0xe2b87a14, 0x7bb12bae, 0x0cb61b38,
		0x92d28e9b, 0xe5d5be0d, 0x7cdcefb7, 0x0bdbdf21,
		0x86d3d2d4, 0xf1d4e242, 0x68ddb3f8, 0x1fda836e,
		0x81be16cd, 0xf6b9265b, 0x6fb077e1, 0x18b74777,
		0x88085ae6, 0xff0f6a70, 0x66063bca, 0x11010b5c,
		0x8f659eff, 0xf862ae69, 0x616bffd3, 0x166ccf45,
		0xa00ae278, 0xd70dd2ee, 0x4e048354, 0x3903b3c2,
		0xa7672661, 0xd06016f7, 0x4969474d, 0x3e6e77db,
		0xaed16a4a, 0xd9d65adc, 0x40df0b66, 0x37d83bf0,
		0xa9bcae53, 0xdebb9ec5, 0x47b2cf7f, 0x30b5ffe9,
		0xbdbdf21c, 0xcabac28a, 0x53b39330, 0x24b4a3a6,
		0xbad03605, 0xcdd70693, 0x54de5729, 0x23d967bf,
		0xb3667a2e, 0xc4614ab8, 0x5d681b02, 0x2a6f2b94,
		0xb40bbe37, 0xc30c8ea1, 0x5a05df1b, 0x2d02ef8d
	};

	static void Init( CRC32_t& pulCRC )
	{
		pulCRC = CRC32_INIT_VALUE;
	}

	static void ProcessBuffer( CRC32_t& pulCRC, const void* pBuffer, size_t nBuffer )
	{
		CRC32_t ulCrc = pulCRC;
		const auto* pb = static_cast<const uint8*>( pBuffer );

	JustAfew:
		switch ( nBuffer )
		{
		case 7:
			ulCrc = pulCRCTable[*pb++ ^ static_cast<uint8>( ulCrc )] ^ ( ulCrc >> 8 );

		case 6:
			ulCrc = pulCRCTable[*pb++ ^ static_cast<uint8>( ulCrc )] ^ ( ulCrc >> 8 );

		case 5:
			ulCrc = pulCRCTable[*pb++ ^ static_cast<uint8>( ulCrc )] ^ ( ulCrc >> 8 );

		case 4:
			ulCrc ^= *reinterpret_cast<const CRC32_t*>( pb );
			ulCrc = pulCRCTable[static_cast<uint8>( ulCrc )] ^ ( ulCrc >> 8 );
			ulCrc = pulCRCTable[static_cast<uint8>( ulCrc )] ^ ( ulCrc >> 8 );
			ulCrc = pulCRCTable[static_cast<uint8>( ulCrc )] ^ ( ulCrc >> 8 );
			ulCrc = pulCRCTable[static_cast<uint8>( ulCrc )] ^ ( ulCrc >> 8 );
			pulCRC = ulCrc;
			return;

		case 3:
			ulCrc = pulCRCTable[*pb++ ^ static_cast<uint8>( ulCrc )] ^ ( ulCrc >> 8 );

		case 2:
			ulCrc = pulCRCTable[*pb++ ^ static_cast<uint8>( ulCrc )] ^ ( ulCrc >> 8 );

		case 1:
			ulCrc = pulCRCTable[*pb++ ^ static_cast<uint8>( ulCrc )] ^ ( ulCrc >> 8 );

		case 0:
			pulCRC = ulCrc;
			return;
		}

		// We may need to do some alignment work up front, and at the end, so that
		// the main loop is aligned and only has to worry about 8 byte at a time.
		//
		// The low-order two bits of pb and nBuffer in total control the
		// upfront work.
		//
		const int nFront = gsl::narrow<int>( reinterpret_cast<intptr_t>( pb ) & 3 );
		nBuffer -= nFront;
		switch ( nFront )
		{
		case 3:
			ulCrc = pulCRCTable[*pb++ ^ static_cast<uint8>( ulCrc )] ^ ( ulCrc >> 8 );
		case 2:
			ulCrc = pulCRCTable[*pb++ ^ static_cast<uint8>( ulCrc )] ^ ( ulCrc >> 8 );
		case 1:
			ulCrc = pulCRCTable[*pb++ ^ static_cast<uint8>( ulCrc )] ^ ( ulCrc >> 8 );
		}

		size_t nMain = nBuffer >> 3;
		while ( nMain-- )
		{
			ulCrc ^= ( *reinterpret_cast<const CRC32_t*>( pb ) );
			ulCrc = pulCRCTable[static_cast<uint8>( ulCrc )] ^ ( ulCrc >> 8 );
			ulCrc = pulCRCTable[static_cast<uint8>( ulCrc )] ^ ( ulCrc >> 8 );
			ulCrc = pulCRCTable[static_cast<uint8>( ulCrc )] ^ ( ulCrc >> 8 );
			ulCrc = pulCRCTable[static_cast<uint8>( ulCrc )] ^ ( ulCrc >> 8 );
			ulCrc ^= *reinterpret_cast<const CRC32_t*>( pb + 4 );
			ulCrc = pulCRCTable[static_cast<uint8>( ulCrc )] ^ ( ulCrc >> 8 );
			ulCrc = pulCRCTable[static_cast<uint8>( ulCrc )] ^ ( ulCrc >> 8 );
			ulCrc = pulCRCTable[static_cast<uint8>( ulCrc )] ^ ( ulCrc >> 8 );
			ulCrc = pulCRCTable[static_cast<uint8>( ulCrc )] ^ ( ulCrc >> 8 );
			pb += 8;
		}

		nBuffer &= 7;
		goto JustAfew;
	}

	static void Final( CRC32_t& pulCRC )
	{
		pulCRC ^= CRC32_XOR_VALUE;
	}

	static CRC32_t ProcessSingleBuffer( const void* p, size_t len )
	{
		CRC32_t crc;

		Init( crc );
		ProcessBuffer( crc, p, len );
		Final( crc );

		return crc;
	}
}

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
		return ch == ' ' || ch == '\t' || ch == '\r' || ch == 'n';
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

	static char* stb_include_file( const char* filename, size_t& total, gsl::span<char> error );

	static char* stb_include_string( gsl::span<char> str, size_t& total, gsl::span<char> error )
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
				char* inc = stb_include_file( info.filename, len, error );
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

	static char* stb_include_file( const char* filename, size_t& total, gsl::span<char> error )
	{
		size_t len;
		char* text = stb_include_load_file( filename, len );
		if ( text == nullptr )
		{
			strcpy_s( error.data(), error.size(), "Error: couldn't load '" );
			strcat_s( error.data(), error.size(), filename );
			strcat_s( error.data(), error.size(), "'" );
			return nullptr;
		}
		total += len;
		char* result = stb_include_string( gsl::make_span( text, len ), total, error );
		free( text );
		return result;
	}

	static CRC32::CRC32_t CalculateCRC( const char* fileName )
	{
		size_t length = 0;
		char error[512] = { 0 };
		setup_base_path( fileName );

		char* src = stb_include_file( V_UnqualifiedFileName( fileName ), length, error );

		if ( error[0] != 0 )
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