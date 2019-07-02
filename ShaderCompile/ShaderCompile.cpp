//========= Copyright © 1996-2005, Valve Corporation, All rights reserved. ============//
//
// Purpose:
//
// $NoKeywords: $
//
//=============================================================================//
// vmpi_bareshell.cpp : Defines the entry point for the console application.
//

#define WIN32_LEAN_AND_MEAN
#define NOWINRES
#define NOSERVICE
#define NOMCX
#define NOIME
#include <windows.h>
#undef PostMessage

#include "conio.h"
#include "process.h"
#include "direct.h"
#include "io.h"
#include "sys/stat.h"

#include "basetypes.h"
#include "utlnodehash.h"
#include "cmdsink.h"
#include "d3dxfxc.h"
#include "subprocess.h"
#include "cfgprocessor.h"
#include "utlbuffer.h"
#include "utlstringmap.h"
#include "shader_vcs_version.h"

#include "d3dcompiler.h"
#include "DbgHelp.h"

#include "gsl/string_span"

#include "ezOptionParser.hpp"
#include "termcolor/style.hpp"

#include <ctime>
#include <iomanip>
#include <thread>

extern "C" {
#define _7ZIP_ST

#include "C/7zTypes.h"
#include "C/LzFind.c"
#include "C/LzmaEnc.c"

#undef _7ZIP_ST
}

#pragma comment ( lib, "DbgHelp" )

// Type conversions should be controlled by programmer explicitly - shadercompile makes use of 64-bit integer arithmetics
#pragma warning( error : 4244 )

static ez::ezOptionParser cmdLine;

namespace clr
{
	using namespace termcolor;
	static const auto red = _internal::ansi_color( color( 222, 12, 17 ) );
	static const auto green = _internal::ansi_color( color( 33, 201, 41 ) );
	static const auto green2 = _internal::ansi_color( color( 12, 222, 154 ) );
	static const auto blue = _internal::ansi_color( color( 14, 70, 220 ) );
	static const auto pinkish = _internal::ansi_color( color( 254, 90, 90 ) );
}

template <typename StorageType, uint32 TBufferSize>
class CUtlMovingAverage
{
public:
	CUtlMovingAverage() : m_buffer{ 0 }, m_nValuesPushed( 0 ), m_nIndex( 0 ), m_total( 0 )
	{
	}

	void Reset()
	{
		m_nValuesPushed = 0;
		m_nIndex = 0;
		m_total = 0;
		memset( m_buffer, 0, sizeof( m_buffer ) );
	}

	[[nodiscard]] StorageType GetAverage() const
	{
		const uint32 n = Min( TBufferSize, m_nIndex );
		return gsl::narrow_cast<StorageType>( n ? ( m_total / static_cast<double>( n ) ) : 0 );
	}

	void PushValue( StorageType v )
	{
		uint32 nIndex = m_nValuesPushed % TBufferSize;
		m_nValuesPushed = nIndex + 1;
		m_nIndex = Max( m_nIndex, m_nValuesPushed );

		m_total -= m_buffer[nIndex];
		m_total += v;

		m_buffer[nIndex] = v;
	}

private:
	StorageType m_buffer[TBufferSize];
	uint32 m_nValuesPushed;
	uint32 m_nIndex;

	StorageType m_total;
};

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

namespace LZMA
{
	static constexpr int LZMA_ID = 'AMZL';
#pragma pack(1)
	struct lzma_header_t
	{
		uint32	id;
		uint32	actualSize;		// always little endian
		uint32	lzmaSize;		// always little endian
		uint8	properties[5];
	};
#pragma pack()
	static_assert( sizeof( lzma_header_t ) == 17 );

	static void* SzAlloc( void*, size_t size ) { return malloc( size ); }
	static void SzFree( void*, void* address ) { free( address ); }
	static ISzAlloc g_Alloc = { SzAlloc, SzFree };

	static uint8* LZMA_Compress( uint8* pInput, size_t inputSize, size_t* pOutputSize )
	{
		Byte* inBuffer = pInput;
		CLzmaEncProps props;
		LzmaEncProps_Init( &props );
		LzmaEncProps_Normalize( &props );

		size_t outSize = inputSize / 20 * 21 + ( 1 << 16 );
		Byte* outBuffer = static_cast<Byte*>( malloc( outSize ) );
		if ( outBuffer == nullptr )
			return nullptr;

		lzma_header_t* header = reinterpret_cast<lzma_header_t*>( outBuffer );
		header->id = LZMA_ID;
		header->actualSize = gsl::narrow<uint32>( inputSize );

		{
			size_t outSizeProcessed = outSize - sizeof( lzma_header_t );
			size_t outPropsSize = LZMA_PROPS_SIZE;

			const SRes res = LzmaEncode( outBuffer + sizeof( lzma_header_t ), &outSizeProcessed,
				inBuffer, inputSize, &props, header->properties, &outPropsSize, 0,
				nullptr, &g_Alloc, &g_Alloc );

			if ( res != SZ_OK )
			{
				free( outBuffer );
				return nullptr;
			}

			header->lzmaSize = gsl::narrow<uint32>( outSizeProcessed );
			outSize = sizeof( lzma_header_t ) + outSizeProcessed;
		}
		*pOutputSize = outSize;
		return outBuffer;
	}
}

namespace Plat
{
	static LARGE_INTEGER g_PerformanceFrequency;
	static LARGE_INTEGER g_MSPerformanceFrequency;
	static LARGE_INTEGER g_ClockStart;

	static void InitTime()
	{
		if ( !g_PerformanceFrequency.QuadPart )
		{
			QueryPerformanceFrequency( &g_PerformanceFrequency );
			g_MSPerformanceFrequency.QuadPart = g_PerformanceFrequency.QuadPart / 1000;
			QueryPerformanceCounter( &g_ClockStart );
		}
	}

	static double FloatTime()
	{
		InitTime();

		LARGE_INTEGER CurrentTime;

		QueryPerformanceCounter( &CurrentTime );

		return static_cast<double>( CurrentTime.QuadPart - g_ClockStart.QuadPart ) / static_cast<double>( g_PerformanceFrequency.QuadPart );
	}
}


// Dealing with job list
static std::unique_ptr<CfgProcessor::CfgEntryInfo[]> g_arrCompileEntries;
static uint64 g_numShaders = 0, g_numCompileCommands = 0, g_numStaticCombos = 0;
static uint64 g_numCommandsCompleted = 0;

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

static void __FormatTime( std::ios_base& s, uint64 nInputSeconds )
{
	uint64 nMinutes = nInputSeconds / 60;
	const uint64 nSeconds = nInputSeconds - nMinutes * 60;
	const uint64 nHours = nMinutes / 60;
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

static void __FormatTime2( std::ios_base& s, uint64 nInputSeconds )
{
	uint64 nMinutes = nInputSeconds / 60;
	const uint64 nSeconds = nInputSeconds - nMinutes * 60;
	const uint64 nHours = nMinutes / 60;
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

static std::_Smanip<uint64> FormatTime( uint64 i )
{
	return { __FormatTime, i };
}

static std::_Smanip<uint64> FormatTimeShort( uint64 i )
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
		return nullptr;  // no extension

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


static std::string g_pShaderPath;
static char g_ExeDir[MAX_PATH];
static double g_flStartTime;
static bool g_bVerbose = false;
static bool g_bVerbose2 = false;

struct ShaderInfo_t
{
	ShaderInfo_t() { memset( this, 0, sizeof( *this ) ); }

	uint64 m_nShaderCombo;
	uint64 m_nTotalShaderCombos;
	const char* m_pShaderName;
	const char* m_pShaderSrc;
	unsigned m_CentroidMask;
	uint64 m_nDynamicCombos;
	uint64 m_nStaticCombo;
	unsigned m_Flags; // from IShader.h
	char m_szShaderModel[12];
};

static void Shader_ParseShaderInfoFromCompileCommands( const CfgProcessor::CfgEntryInfo* pEntry, ShaderInfo_t& shaderInfo );

struct CByteCodeBlock
{
	CByteCodeBlock *m_pNext, *m_pPrev;
	int m_nCRC32;
	uint64 m_nComboID;
	size_t m_nCodeSize;
	uint8* m_ByteCode;

	CByteCodeBlock()
	{
		m_ByteCode = nullptr;
	}

	CByteCodeBlock( const void* pByteCode, size_t nCodeSize, uint64 nComboID )
	{
		m_ByteCode = new uint8[nCodeSize];
		m_nComboID = nComboID;
		m_nCodeSize = nCodeSize;
		memcpy( m_ByteCode, pByteCode, nCodeSize );
		m_nCRC32 = CRC32::ProcessSingleBuffer( pByteCode, nCodeSize );
	}

	~CByteCodeBlock()
	{
		if ( m_ByteCode )
			delete[] m_ByteCode;
	}
};

static bool CompareDynamicComboIDs( const std::unique_ptr<CByteCodeBlock>& pA, const std::unique_ptr<CByteCodeBlock>& pB )
{
	return pA->m_nComboID < pB->m_nComboID;
}

struct CStaticCombo									// all the data for one static combo
{
	CStaticCombo *m_pNext, *m_pPrev;

	uint64 m_nStaticComboID;

	std::vector<std::unique_ptr<CByteCodeBlock>> m_DynamicCombos;

	struct PackedCode : protected std::unique_ptr<uint8[]>
	{
		[[nodiscard]] size_t GetLength() const { if ( uint8* pb = get() ) return *reinterpret_cast<size_t*>( pb ); return 0; }
		[[nodiscard]] uint8* GetData() const { if ( uint8* pb = get() ) return pb + sizeof( size_t ); return nullptr; }
		[[nodiscard]] uint8* AllocData( size_t len ) { reset(); if ( len ) { reset( new uint8[len + sizeof( size_t )] ); *reinterpret_cast<size_t*>( get() ) = len; } return GetData(); }
	} m_abPackedCode;			// Packed code for entire static combo

	[[nodiscard]] uint64 Key() const
	{
		return m_nStaticComboID;
	}

	CStaticCombo( uint64 nComboID )
	{
		m_nStaticComboID = nComboID;
	}

	~CStaticCombo()
	{
		m_DynamicCombos.clear();
	}

	void AddDynamicCombo( uint64 nComboID, const void* pComboData, size_t nCodeSize )
	{
		m_DynamicCombos.emplace_back( std::make_unique<CByteCodeBlock>( pComboData, nCodeSize, nComboID ) );
	}

	void SortDynamicCombos()
	{
		std::sort( m_DynamicCombos.begin(), m_DynamicCombos.end(), CompareDynamicComboIDs );
	}

	[[nodiscard]] uint8* AllocPackedCodeBlock( size_t nPackedCodeSize )
	{
		return m_abPackedCode.AllocData( nPackedCodeSize );
	}
};

using StaticComboNodeHash_t = CUtlNodeHash<CStaticCombo, 7097, uint64>;

template <typename...Args>
inline void Construct( StaticComboNodeHash_t** pMemory, const Args&... )
{
	::new( pMemory ) StaticComboNodeHash_t*( nullptr ); // Explicitly new with NULL
}

using CShaderMap = CUtlStringMap<StaticComboNodeHash_t*>;
static CShaderMap g_ShaderByteCode;

static CStaticCombo* StaticComboFromDictAdd( const char* pszShaderName, uint64 nStaticComboId )
{
	StaticComboNodeHash_t*& rpNodeHash = g_ShaderByteCode[pszShaderName];
	if ( !rpNodeHash )
		rpNodeHash = new StaticComboNodeHash_t;

	// search for this static combo. make it if not found
	CStaticCombo* pStaticCombo = rpNodeHash->FindByKey( nStaticComboId );
	if ( !pStaticCombo )
	{
		pStaticCombo = new CStaticCombo( nStaticComboId );
		rpNodeHash->Add( pStaticCombo );
	}

	return pStaticCombo;
}

static CStaticCombo* StaticComboFromDict( const char* pszShaderName, uint64 nStaticComboId )
{
	if ( StaticComboNodeHash_t* pNodeHash = g_ShaderByteCode[pszShaderName] )
		return pNodeHash->FindByKey( nStaticComboId );
	else
		return nullptr;
}



static CUtlStringMap<ShaderInfo_t> g_ShaderToShaderInfo;

class CompilerMsgInfo
{
public:
	CompilerMsgInfo() : m_numTimesReported( 0 ) {}

	void SetMsgReportedCommand( const char* szCommand, int numTimesReported = 1 )
	{
		if ( !m_numTimesReported )
			m_sFirstCommand = szCommand;
		m_numTimesReported += numTimesReported;
	}

	[[nodiscard]] const char* GetFirstCommand() const { return m_sFirstCommand.c_str(); }
	[[nodiscard]] int GetNumTimesReported() const { return m_numTimesReported; }

protected:
	std::string m_sFirstCommand;
	int m_numTimesReported;
};

static CUtlStringMap<uint8> g_Master_ShaderHadError;
static CUtlStringMap<uint8> g_Master_ShaderWrittenToDisk;
static CUtlStringMap<CompilerMsgInfo> g_Master_CompilerMsgError;
static CUtlStringMap<CompilerMsgInfo> g_Master_CompilerMsgWarning;

namespace Threading
{
	class CThreadFastMutex
	{
	public:
		CThreadFastMutex() : m_ownerID( 0 ), m_depth( 0 ) {}

	private:
		FORCEINLINE bool TryLockInline( const uint32 threadId ) volatile
		{
			if ( threadId != m_ownerID && !InterlockedCompareExchange( &m_ownerID, threadId, 0 ) )
				return false;

			++m_depth;
			return true;
		}

		bool TryLock( const uint32 threadId ) volatile
		{
			return TryLockInline( threadId );
		}

		void Lock( const uint32 threadId, unsigned nSpinSleepTime ) volatile
		{
			int i;
			if ( nSpinSleepTime != INFINITE )
			{
				for ( i = 1000; i != 0; --i )
				{
					if ( TryLock( threadId ) )
						return;
					_mm_pause();
				}

				if ( !nSpinSleepTime && GetThreadPriority( GetCurrentThread() ) > THREAD_PRIORITY_NORMAL )
				{
					nSpinSleepTime = 1;
				}
				else if ( nSpinSleepTime )
				{
					for ( i = 4000; i != 0; --i )
					{
						if ( TryLock( threadId ) )
							return;

						_mm_pause();
						Sleep( 0 );
					}

				}

				for ( ;; ) // coded as for instead of while to make easy to breakpoint success
				{
					if ( TryLock( threadId ) )
						return;

					_mm_pause();
					Sleep( nSpinSleepTime );
				}
			}
			else
			{
				for ( ;; ) // coded as for instead of while to make easy to breakpoint success
				{
					if ( TryLock( threadId ) )
						return;

					_mm_pause();
				}
			}
		}

	public:
		bool TryLock() volatile
		{
			return TryLockInline( GetCurrentThreadId() );
		}

#ifndef _DEBUG
		FORCEINLINE
#endif
		void Lock( unsigned nSpinSleepTime = 0 ) volatile
		{
			const uint32 threadId = GetCurrentThreadId();

			if ( !TryLockInline( threadId ) )
			{
				_mm_pause();
				Lock( threadId, nSpinSleepTime );
			}
		}

#ifndef _DEBUG
		FORCEINLINE
#endif
		void Unlock() volatile
		{
			--m_depth;
			if ( !m_depth )
				InterlockedExchange( &m_ownerID, 0 );
		}

		bool TryLock() const volatile { return const_cast<CThreadFastMutex*>( this )->TryLock(); }
		void Lock( unsigned nSpinSleepTime = 1 ) const volatile { const_cast<CThreadFastMutex*>( this )->Lock( nSpinSleepTime ); }
		void Unlock() const	volatile { const_cast<CThreadFastMutex*>( this )->Unlock(); }

		[[nodiscard]] uint32 GetOwnerId() const { return m_ownerID; }
		[[nodiscard]] int GetDepth() const { return m_depth; }
	private:
		volatile uint32	m_ownerID;
		int				m_depth;
	};

	class CThreadMutex
	{
	public:
		CThreadMutex()
		{
			[[maybe_unused]] const BOOL res = InitializeCriticalSectionAndSpinCount( &m_CriticalSection, 4000 );
			Assert( res );
		}

		~CThreadMutex()
		{
			DeleteCriticalSection( &m_CriticalSection );
		}

		//------------------------------------------------------
		// Mutex acquisition/release. Const intentionally defeated.
		//------------------------------------------------------
		void Lock()
		{
			EnterCriticalSection( &m_CriticalSection );
		}

		void Lock() const
		{
			const_cast<CThreadMutex*>( this )->Lock();
		}

		void Unlock()
		{
			LeaveCriticalSection( &m_CriticalSection );
		}

		void Unlock() const
		{
			const_cast<CThreadMutex*>( this )->Unlock();
		}

		bool TryLock()
		{
			return TryEnterCriticalSection( &m_CriticalSection ) != FALSE;
		}

		bool TryLock() const
		{
			return const_cast<CThreadMutex*>( this )->TryLock();
		}

		// Disallow copying
		CThreadMutex( const CThreadMutex& ) = delete;
		CThreadMutex& operator=( const CThreadMutex& ) = delete;

		CRITICAL_SECTION m_CriticalSection;
	};

	class CThreadNullMutex
	{
	public:
		static void Lock() {}
		static void Unlock() {}

		static bool TryLock() { return true; }

		static uint32 GetOwnerId() { return 0; }
		static int GetDepth() { return 0; }
	};

	template <typename T>
	class CInterlockedPtr
	{
#ifdef _M_AMD64
		using cast_type = volatile unsigned long long*;
#else
		using cast_type = volatile unsigned long*;
#endif
	public:
		CInterlockedPtr() : m_value( nullptr ) {}
		CInterlockedPtr( T* value ) : m_value( value ) {}

		operator T*() const { return m_value; }

		bool operator!() const { return ( m_value == nullptr ); }
		bool operator==( T* rhs ) const { return ( m_value == rhs ); }
		bool operator!=( T* rhs ) const { return ( m_value != rhs ); }

		T* operator++() { return reinterpret_cast<T*>( InterlockedExchangeAdd( reinterpret_cast<cast_type>( &m_value ), sizeof( T ) ) ) + 1; }
		T* operator++( int ) { return reinterpret_cast<T*>( InterlockedExchangeAdd( reinterpret_cast<cast_type>( &m_value ), sizeof( T ) ) ); }

		T* operator--() { return reinterpret_cast<T*>( InterlockedExchangeAdd( reinterpret_cast<cast_type>( &m_value ), -sizeof( T ) ) ) - 1; }
		T* operator--( int ) { return reinterpret_cast<T*>( InterlockedExchangeAdd( reinterpret_cast<cast_type>( &m_value ), -sizeof( T ) ) ); }

		bool AssignIf( T* conditionValue, T* newValue ) { return InterlockedCompareExchangePointer( reinterpret_cast<void* volatile*>( &m_value ), newValue, conditionValue ) == conditionValue; }

		T* operator=( T* newValue ) { InterlockedExchangePointer( reinterpret_cast<void* volatile*>( &m_value ), newValue ); return newValue; }

		void operator+=( int add ) { InterlockedExchangeAdd( reinterpret_cast<cast_type>( &m_value ), add * sizeof( T ) ); }
		void operator-=( int subtract ) { operator+=( -subtract ); }

		T* operator+( int rhs ) const { return m_value + rhs; }
		T* operator-( int rhs ) const { return m_value - rhs; }
		T* operator+( unsigned rhs ) const { return m_value + rhs; }
		T* operator-( unsigned rhs ) const { return m_value - rhs; }
		size_t operator-( T* p ) const { return m_value - p; }
		intptr_t operator-( const CInterlockedPtr<T>& p ) const { return m_value - p.m_value; }

	private:
		T* volatile m_value;
	};

	class CInterlockedInt
	{
	public:
		CInterlockedInt() : m_value( 0 ) {}
		CInterlockedInt( int value ) : m_value( value ) {}

		operator int() const { return m_value; }

		bool operator!() const { return ( m_value == 0 ); }
		bool operator==( int rhs ) const { return ( m_value == rhs ); }
		bool operator!=( int rhs ) const { return ( m_value != rhs ); }

		int operator++() { return InterlockedIncrement( &m_value ); }
		int operator++( int ) { return operator++() - 1; }

		int operator--() { return InterlockedDecrement( &m_value ); }
		int operator--( int ) { return operator--() + 1; }

		bool AssignIf( int conditionValue, int newValue ) { return InterlockedCompareExchange( &m_value, newValue, conditionValue ) == conditionValue; }

		int operator=( int newValue ) { InterlockedExchange( &m_value, newValue ); return m_value; }

		void operator+=( int add ) { InterlockedExchangeAdd( &m_value, add ); }
		void operator-=( int subtract ) { operator+=( -subtract ); }
		void operator*=( int multiplier )
		{
			int original, result;
			do
			{
				original = m_value;
				result = original * multiplier;
			} while ( !AssignIf( original, result ) );
		}
		void operator/=( int divisor ) {
			int original, result;
			do
			{
				original = m_value;
				result = original / divisor;
			} while ( !AssignIf( original, result ) );
		}

		int operator+( int rhs ) const { return m_value + rhs; }
		int operator-( int rhs ) const { return m_value - rhs; }

	private:
		volatile long m_value;
	};

	template <class T>
	class CThreadLocal
	{
		static_assert( sizeof( T ) == sizeof( void* ) );
	public:
		CThreadLocal()
		{
			m_index = TlsAlloc();
		}

		~CThreadLocal()
		{
			TlsFree( m_index );
		}

		T Get() const
		{
			return reinterpret_cast<T>( TlsGetValue( m_index ) );
		}

		void Set( T val )
		{
			TlsSetValue( m_index, reinterpret_cast<void*>( val ) );
		}

	private:
		uint32 m_index;
	};

enum Mode
{
	eSingleThreaded = 0,
	eMultiThreaded = 1
};

// A special object that makes single-threaded code incur no penalties
// and multithreaded code to be synchronized properly.
template <class MT_MUTEX_TYPE = CThreadFastMutex>
class CSwitchableMutex
{
public:
	FORCEINLINE explicit CSwitchableMutex( Mode eMode, MT_MUTEX_TYPE* pMtMutex = nullptr ) : m_pMtx( pMtMutex ), m_pUseMtx( eMode ? pMtMutex : nullptr ) {}

public:
	FORCEINLINE void SetMtMutex( MT_MUTEX_TYPE* pMtMutex ) { m_pMtx = pMtMutex; m_pUseMtx = m_pUseMtx ? pMtMutex : nullptr; }
	FORCEINLINE void SetThreadedMode( Mode eMode ) { m_pUseMtx = eMode ? m_pMtx : nullptr; }

public:
	FORCEINLINE void Lock()							{ if ( MT_MUTEX_TYPE* pUseMtx = m_pUseMtx ) pUseMtx->Lock(); }
	FORCEINLINE void Unlock()						{ if ( MT_MUTEX_TYPE* pUseMtx = m_pUseMtx ) pUseMtx->Unlock(); }

	FORCEINLINE bool TryLock()						{ if ( MT_MUTEX_TYPE* pUseMtx = m_pUseMtx ) return pUseMtx->TryLock(); return true; }
	FORCEINLINE bool AssertOwnedByCurrentThread()	{ if ( MT_MUTEX_TYPE* pUseMtx = m_pUseMtx ) return pUseMtx->AssertOwnedByCurrentThread(); return true; }
	FORCEINLINE void SetTrace( bool b )				{ if ( MT_MUTEX_TYPE* pUseMtx = m_pUseMtx ) pUseMtx->SetTrace( b ); }

	FORCEINLINE uint32 GetOwnerId()					{ if ( MT_MUTEX_TYPE* pUseMtx = m_pUseMtx ) return pUseMtx->GetOwnerId(); return 0; }
	FORCEINLINE int	GetDepth()						{ if ( MT_MUTEX_TYPE* pUseMtx = m_pUseMtx ) return pUseMtx->GetDepth(); else return 0; }

private:
	MT_MUTEX_TYPE* m_pMtx;
	CInterlockedPtr<MT_MUTEX_TYPE> m_pUseMtx;
};


namespace Private
{
	using MtMutexType_t = CThreadMutex;
	static MtMutexType_t g_mtxSyncObjMT;
}; // namespace Private


static CSwitchableMutex<Private::MtMutexType_t> g_mtxGlobal( eSingleThreaded, &Private::g_mtxSyncObjMT );


class CGlobalMutexAutoLock
{
public:
	CGlobalMutexAutoLock()		{ g_mtxGlobal.Lock(); }
	~CGlobalMutexAutoLock()		{ g_mtxGlobal.Unlock(); }
};

}; // namespace Threading

namespace SourceCodeHasher
{
	static char* stb_include_load_file( const char* filename, size_t& plen )
	{
		CSharedFile* file = fileCache.Get( filename );
		plen = file->Size();
		char* text = static_cast<char*>( malloc( file->Size() + 1 ) );
		memcpy( text, file->Data(), file->Size() );
		text[file->Size()] = 0;
		return text;
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
		char* src = stb_include_file( fileName, length, error );

		const auto& find = []( const char& a, const char& b ) { return a == '\r' && b == '\n'; };
		std::vector<char> data( src, src + length );
		free( src );
		for ( std::vector<char>::iterator i; ( i = std::adjacent_find( data.begin(), data.end(), find ) ) != data.end(); )
			data.erase( i );

		return CRC32::ProcessSingleBuffer( data.data(), data.size() );
	}
}

// Access to global data should be synchronized by these global locks
#define GLOBAL_DATA_MTX_LOCK()			Threading::g_mtxGlobal.Lock()
#define GLOBAL_DATA_MTX_UNLOCK()		Threading::g_mtxGlobal.Unlock()
#define GLOBAL_DATA_MTX_LOCK_AUTO		Threading::CGlobalMutexAutoLock UNIQUE_ID;

// Consume all characters for which (isspace) is true
template <typename T>
static char* ConsumeCharacters( char* szString, T pred )
{
	if ( szString )
		while ( *szString && pred( *szString ) )
			++szString;

	return szString;
}

template <typename T>
static T* FindNext( T* szString, const char* szSearchSet )
{
	bool bFound = szString == nullptr;
	T* szNext = nullptr;

	if ( szString && szSearchSet )
	{
		for ( ; *szSearchSet; ++szSearchSet )
		{
			if ( T* szTmp = strchr( szString, *szSearchSet ) )
			{
				szNext = bFound ? ( Min( szNext, szTmp ) ) : szTmp;
				bFound = true;
			}
		}
	}

	return bFound ? szNext : nullptr;
}

static char* FindLast( char* szString, const char* szSearchSet )
{
	bool bFound = szString != nullptr;
	char* szNext = nullptr;

	if ( szString && szSearchSet )
	{
		for ( ; *szSearchSet; ++szSearchSet )
		{
			if ( char* szTmp = strrchr( szString, *szSearchSet ) )
			{
				szNext = bFound ? ( Max( szNext, szTmp ) ) : szTmp;
				bFound = true;
			}
		}
	}

	return bFound ? szNext : nullptr;
}

static void ErrMsgDispatchMsgLine( const char* szCommand, const char* szMsgLine )
{
	// Now store the message with the command it was generated from
	if ( strstr( szMsgLine, "warning X" ) )
		g_Master_CompilerMsgWarning[szMsgLine].SetMsgReportedCommand( szCommand );
	else
		g_Master_CompilerMsgError[szMsgLine].SetMsgReportedCommand( szCommand );
}

static void ShaderHadErrorDispatchInt( const char* szShader )
{
	g_Master_ShaderHadError[szShader] = true;
}

// new format:
// ver#
// total shader combos
// total dynamic combos
// flags
// centroid mask
// total non-skipped static combos
// [ (sorted by static combo id)
//   static combo id
//   file offset of packed dynamic combo
// ]
// 0xffffffff  (sentinel key)
// end of file offset (so can tell compressed size of last combo)
//
// # of duplicate static combos  (if version >= 6 )
// [ (sorted by static combo id)
//   static combo id
//   id of static bombo which is identical
// ]
//
// each packed dynamic combo for a given static combo is stored as a series of compressed blocks.
//  block 1:
//     ulong blocksize  (high bit set means uncompressed)
//     block data
//  block2..
//  0xffffffff  indicates no more blocks for this combo
//
// each block, when uncompressed, holds one or more dynamic combos:
//   dynamic combo id   (full id if v<6, dynamic combo id only id >=6)
//   size of shader
//   ..
// there is no terminator - the size of the uncompressed shader tells you when to stop




// this record is then bzip2'd.

// qsort driver function
// returns negative number if idA is less than idB, positive when idA is greater than idB
// and zero if the ids are equal

static bool CompareDupComboIndices( const StaticComboAliasRecord_t& pA, const StaticComboAliasRecord_t& pB )
{
	return pA.m_nStaticComboID < pB.m_nStaticComboID;
}

static void FlushCombos( size_t& pnTotalFlushedSize, CUtlBuffer& pDynamicComboBuffer, CUtlBuffer& pBuf )
{
	if ( !pDynamicComboBuffer.TellPut() )
		// Nothing to do here
		return;

	size_t nCompressedSize;
	uint8* pCompressedShader = LZMA::LZMA_Compress( reinterpret_cast<uint8*>( pDynamicComboBuffer.Base() ), pDynamicComboBuffer.TellPut(), &nCompressedSize );
	// high 2 bits of length =
	// 00 = bzip2 compressed
	// 10 = uncompressed
	// 01 = lzma compressed
	// 11 = unused

	if ( !pCompressedShader )
	{
		// it grew
		const unsigned long lFlagSize = 0x80000000 | pDynamicComboBuffer.TellPut();
		pBuf.Put( &lFlagSize, sizeof( lFlagSize ) );
		pBuf.Put( pDynamicComboBuffer.Base(), pDynamicComboBuffer.TellPut() );
		pnTotalFlushedSize += sizeof( lFlagSize ) + pDynamicComboBuffer.TellPut();
	}
	else
	{
		const unsigned long lFlagSize = 0x40000000 | gsl::narrow<uint32>( nCompressedSize );
		pBuf.Put( &lFlagSize, sizeof( lFlagSize ) );
		pBuf.Put( pCompressedShader, gsl::narrow<uint32>( nCompressedSize ) );
		delete[] pCompressedShader;
		pnTotalFlushedSize += sizeof( lFlagSize ) + nCompressedSize;
	}
	pDynamicComboBuffer.Clear();							// start over
}

static void OutputDynamicCombo( size_t& pnTotalFlushedSize, CUtlBuffer& pDynamicComboBuffer, CUtlBuffer& pBuf, uint64 nComboID, uint32 nComboSize, uint8* pComboCode )
{
	if ( pDynamicComboBuffer.TellPut() + nComboSize + 16 >= MAX_SHADER_UNPACKED_BLOCK_SIZE )
		FlushCombos( pnTotalFlushedSize, pDynamicComboBuffer, pBuf );

	pDynamicComboBuffer.PutUnsignedInt( gsl::narrow<uint32>( nComboID ) );
	pDynamicComboBuffer.PutUnsignedInt( nComboSize );
//	pDynamicComboBuffer.PutInt( CRC32_ProcessSingleBuffer( pComboCode, nComboSize ) );
	pDynamicComboBuffer.Put( pComboCode, nComboSize );
}

static void GetVCSFilenames( gsl::span<char> pszMainOutFileName, const ShaderInfo_t& si )
{
	sprintf_s( pszMainOutFileName.data(), pszMainOutFileName.size(), "%s\\shaders\\fxc", g_pShaderPath.c_str() );

	struct _stat buf;
	if ( _stat( pszMainOutFileName.data(), &buf ) == -1 )
	{
		std::cout << "mkdir " << pszMainOutFileName.data() << std::endl;
		// doh. . need to make the directory that the vcs file is going to go into.
		_mkdir( pszMainOutFileName.data() );
	}

	strcat_s( pszMainOutFileName.data(), pszMainOutFileName.size(), "\\" );
	strcat_s( pszMainOutFileName.data(), pszMainOutFileName.size(), si.m_pShaderName );
	strcat_s( pszMainOutFileName.data(), pszMainOutFileName.size(), ".vcs" );					// Different extensions for main output file

	// Check status of vcs file...
	if ( _stat( pszMainOutFileName.data(), &buf ) != -1 )
	{
		// The file exists, let's see if it's writable.
		if ( !( buf.st_mode & _S_IWRITE ) )
		{
			// It isn't writable. . we'd better change its permissions (or check it out possibly)
			std::cout << clr::pinkish << "Warning: making " << clr::red << pszMainOutFileName.data() << clr::pinkish << " writable!" << clr::reset << std::endl;
			_chmod( pszMainOutFileName.data(), _S_IREAD | _S_IWRITE );
		}
	}
}

// WriteShaderFiles
//
// should be called either on the main thread or
// on the async writing thread.
//
// So the function WriteShaderFiles should not be reentrant, however the
// data that it uses might be updated by the main thread when built pieces
// are received from the workers.
//
static constexpr uint32 STATIC_COMBO_HASH_SIZE = 73;

struct StaticComboAuxInfo_t : StaticComboRecord_t
{
	uint32 m_nCRC32;											// CRC32 of packed data
	struct CStaticCombo* m_pByteCode;
};

static bool CompareComboIds( const StaticComboAuxInfo_t& pA, const StaticComboAuxInfo_t& pB )
{
	return pA.m_nStaticComboID < pB.m_nStaticComboID;
}

static void WriteShaderFiles( const char* pShaderName )
{
	if ( !g_Master_ShaderWrittenToDisk.Defined( pShaderName ) )
		g_Master_ShaderWrittenToDisk[pShaderName] = true;
	else
		return;

	const bool bShaderFailed = g_Master_ShaderHadError.Defined( pShaderName );
	const char* const szShaderFileOperation = bShaderFailed ? "Removing failed" : "Writing";

	//
	// Progress indication
	//
	if ( g_numCommandsCompleted < g_numCompileCommands )
	{
		static constexpr const char chProgress[] = { '/', '-', '\\', '|' };
		static int iProgressSymbol = 0;
		std::cout << "\b" << chProgress[++iProgressSymbol % 4];
	}
	else
	{
		std::cout << "\r" << szShaderFileOperation << " " << ( bShaderFailed ? clr::red : clr::green ) << pShaderName << clr::reset << "...\r";
	}

	//
	// Retrieve the data we are going to operate on
	// from global variables under lock.
	//
	GLOBAL_DATA_MTX_LOCK();
	StaticComboNodeHash_t* pByteCodeArray;
	{
		StaticComboNodeHash_t*& rp = g_ShaderByteCode[pShaderName]; // Get a static combo pointer, reset it as well
		pByteCodeArray = rp;
		rp = nullptr;

		/*
		Assert( pByteCodeArray );
		if ( !pByteCodeArray )
			ShaderHadErrorDispatchInt( pShaderName );
		*/
	}
	ShaderInfo_t shaderInfo = g_ShaderToShaderInfo[pShaderName];
	if ( !shaderInfo.m_pShaderName )
	{
		for ( const CfgProcessor::CfgEntryInfo* pAnalyze = g_arrCompileEntries.get(); pAnalyze->m_szName; ++pAnalyze )
		{
			if ( !strcmp( pAnalyze->m_szName, pShaderName ) )
			{
				Shader_ParseShaderInfoFromCompileCommands( pAnalyze, shaderInfo );
				g_ShaderToShaderInfo[pShaderName] = shaderInfo;
				break;
			}
		}
	}
	GLOBAL_DATA_MTX_UNLOCK();

	if ( !shaderInfo.m_pShaderName )
		return;

	//
	// Shader vcs file name
	//
	char szVCSfilename[MAX_PATH];
	GetVCSFilenames( szVCSfilename, shaderInfo );

	if ( bShaderFailed )
	{
		_unlink( szVCSfilename );
		return;
	}

	if ( !pByteCodeArray )
		return;

	if ( g_bVerbose )
		std::cout << std::showbase << pShaderName << " : " << clr::green << shaderInfo.m_nTotalShaderCombos << clr::reset << " combos, centroid mask: " << clr::green << std::hex << shaderInfo.m_CentroidMask << std::dec <<
	clr::reset << ", numDynamicCombos: " << clr::green << shaderInfo.m_nDynamicCombos << clr::reset << ", flags: " << clr::green << std::hex << shaderInfo.m_Flags << std::dec << clr::reset << std::noshowbase << std::endl;

	//
	// Static combo headers
	//
	std::vector<StaticComboAuxInfo_t> StaticComboHeaders;

	StaticComboHeaders.reserve( 1 + pByteCodeArray->Count() ); // we know how much ram we need

	std::vector<int> comboIndicesHashedByCRC32[STATIC_COMBO_HASH_SIZE];
	std::vector<StaticComboAliasRecord_t> duplicateCombos;

	// now, lets fill in our combo headers, sort, and write
	for ( uint32 nChain = 0; nChain < ARRAYSIZE( pByteCodeArray->m_HashChains ); nChain++ )
	{
		for ( CStaticCombo* pStatic = pByteCodeArray->m_HashChains[nChain].m_pHead; pStatic; pStatic = pStatic->m_pNext )
		{
			if ( pStatic->m_abPackedCode.GetLength() )
			{
				StaticComboAuxInfo_t Hdr;
				Hdr.m_nStaticComboID = gsl::narrow<uint32>( pStatic->m_nStaticComboID );
				Hdr.m_nFileOffset = 0;							// fill in later
				Hdr.m_nCRC32 = CRC32::ProcessSingleBuffer( pStatic->m_abPackedCode.GetData(), pStatic->m_abPackedCode.GetLength() );
				const uint32 nHashIdx = Hdr.m_nCRC32 % STATIC_COMBO_HASH_SIZE;
				Hdr.m_pByteCode = pStatic;
				// now, see if we have an identical static combo
				bool bIsDuplicate = false;
				for ( int i : comboIndicesHashedByCRC32[nHashIdx] )
				{
					const StaticComboAuxInfo_t& check = StaticComboHeaders[i];
					if ( ( check.m_nCRC32 == Hdr.m_nCRC32 ) && ( check.m_pByteCode->m_abPackedCode.GetLength() == pStatic->m_abPackedCode.GetLength() ) &&
						( memcmp( check.m_pByteCode->m_abPackedCode.GetData(), pStatic->m_abPackedCode.GetData(), check.m_pByteCode->m_abPackedCode.GetLength() ) == 0 ) )
					{
						// this static combo is the same as another one!!
						duplicateCombos.emplace_back( StaticComboAliasRecord_t{ Hdr.m_nStaticComboID, check.m_nStaticComboID } );
						bIsDuplicate = true;
						break;
					}
				}

				if ( !bIsDuplicate )
				{
					StaticComboHeaders.emplace_back( Hdr );
					comboIndicesHashedByCRC32[nHashIdx].emplace_back( gsl::narrow<int>( StaticComboHeaders.size() - 1 ) );
				}
			}
		}
	}
	// add sentinel key
	StaticComboHeaders.emplace_back( StaticComboAuxInfo_t{ { 0xffffffff, 0 }, 0, nullptr } );

	// now, sort. sentinel key will end up at end
	std::sort( StaticComboHeaders.begin(), StaticComboHeaders.end(), CompareComboIds );

	const unsigned int crc32 = SourceCodeHasher::CalculateCRC( shaderInfo.m_pShaderSrc );

	//
	// Shader file stream buffer
	//
	std::ofstream ShaderFile( szVCSfilename, std::ios::binary | std::ios::trunc );			// Streaming buffer for vcs file (since this can blow memory)

	// ------ Header --------------
	const ShaderHeader_t header
	{
		SHADER_VCS_VERSION_NUMBER,
		gsl::narrow_cast<int32>( shaderInfo.m_nTotalShaderCombos ),	// this is not actually used in vertexshaderdx8.cpp for combo checking
		gsl::narrow<int32>( shaderInfo.m_nDynamicCombos ),			// this is used
		shaderInfo.m_Flags,
		shaderInfo.m_CentroidMask,
		gsl::narrow<uint32>( StaticComboHeaders.size() ),
		crc32
	};
	ShaderFile.write( reinterpret_cast<const char*>( &header ), sizeof( header ) );

	// static combo dictionary
	const auto nDictionaryOffset = ShaderFile.tellp();

	// we will re write this one we know the offsets
	ShaderFile.write( reinterpret_cast<const char*>( StaticComboHeaders.data() ), sizeof( StaticComboRecord_t ) * StaticComboHeaders.size() ); // dummy write, 8 bytes per static combo

	const uint32 dupl = gsl::narrow<uint32>( duplicateCombos.size() );
	ShaderFile.write( reinterpret_cast<const char*>( &dupl ), sizeof( dupl ) );

	// now, write out all duplicate header records
	// sort duplicate combo records for binary search
	std::sort( duplicateCombos.begin(), duplicateCombos.end(), CompareDupComboIndices );

	ShaderFile.write( reinterpret_cast<const char*>( duplicateCombos.data() ), sizeof( StaticComboAliasRecord_t ) * duplicateCombos.size() );

	// now, write out all static combos
	for ( StaticComboRecord_t& SRec : StaticComboHeaders )
	{
		SRec.m_nFileOffset = gsl::narrow<uint32>( ShaderFile.tellp() );
		if ( SRec.m_nStaticComboID != 0xffffffff )			// sentinel key?
		{
			CStaticCombo* pStatic = pByteCodeArray->FindByKey( SRec.m_nStaticComboID );
			Assert( pStatic );

			// Put the packed chunk of code for this static combo
			if ( const size_t nPackedLen = pStatic->m_abPackedCode.GetLength() )
				ShaderFile.write( reinterpret_cast<const char*>( pStatic->m_abPackedCode.GetData() ), nPackedLen );

			constexpr uint32 end = 0xffffffff; // end of dynamic combos
			ShaderFile.write( reinterpret_cast<const char*>( &end ), sizeof( end ) );
		}
	}

	//
	// Re-writing the combo header
	//
	ShaderFile.seekp( nDictionaryOffset, std::ios::beg );

	// now, rewrite header. data is already byte-swapped appropriately
	for ( const StaticComboRecord_t& SRec : StaticComboHeaders )
		ShaderFile.write( reinterpret_cast<const char*>( &SRec ), sizeof( StaticComboRecord_t ) );

	ShaderFile.close();

	// Finalize, free memory
	delete pByteCodeArray;

	if ( g_numCommandsCompleted >= g_numCompileCommands )
		std::cout << "\r                                                                \r";
}

static DWORD gFlags = 0;

// Assemble a reply package to the master from the compiled bytecode
// return the length of the package.
static size_t AssembleWorkerReplyPackage( const CfgProcessor::CfgEntryInfo* pEntry, uint64 nComboOfEntry, CUtlBuffer& pBuf )
{
	GLOBAL_DATA_MTX_LOCK();
		CStaticCombo* pStComboRec = StaticComboFromDict( pEntry->m_szName, nComboOfEntry );
		StaticComboNodeHash_t* pByteCodeArray = g_ShaderByteCode[pEntry->m_szName];
	GLOBAL_DATA_MTX_UNLOCK();

	size_t nBytesWritten = 0;

	if ( pStComboRec && !pStComboRec->m_DynamicCombos.empty() )
	{
		CUtlBuffer ubDynamicComboBuffer;

		pStComboRec->SortDynamicCombos();
		// iterate over all dynamic combos.
		for ( auto& combo : pStComboRec->m_DynamicCombos )
		{
			CByteCodeBlock* pCode = combo.get();
			// check if we have already output an identical combo
			OutputDynamicCombo( nBytesWritten, ubDynamicComboBuffer, pBuf, pCode->m_nComboID,
								gsl::narrow<uint32>( pCode->m_nCodeSize ), pCode->m_ByteCode );
		}
		FlushCombos( nBytesWritten, ubDynamicComboBuffer, pBuf );
	}

	// Time to limit amount of prints
	thread_local static double s_fLastInfoTime = 0;
	thread_local static uint64 s_nLastEntry = nComboOfEntry;
	thread_local static CUtlMovingAverage<uint64, 64> s_averageProcess;
	thread_local static const char* s_lastShader = pEntry->m_szName;
	const double fCurTime = Plat::FloatTime();

	GLOBAL_DATA_MTX_LOCK();
	if ( pStComboRec )
		pByteCodeArray->DeleteByKey( nComboOfEntry );
	if ( fabs( fCurTime - s_fLastInfoTime ) > 1.0 )
	{
		if ( s_lastShader != pEntry->m_szName )
		{
			s_averageProcess.Reset();
			s_lastShader = pEntry->m_szName;
			s_nLastEntry = nComboOfEntry;
		}

		s_averageProcess.PushValue( s_nLastEntry - nComboOfEntry );
		s_nLastEntry = nComboOfEntry;
		std::cout << "\rCompiling " << clr::green << pEntry->m_szName << clr::reset << " [ " << clr::blue << PrettyPrint( nComboOfEntry ) << clr::reset << " remaining ("
			<< clr::green2 << s_averageProcess.GetAverage() << clr::reset << " c/s) ] " << FormatTimeShort( static_cast<uint64>( fCurTime - g_flStartTime ) ) << " elapsed         \r";
		s_fLastInfoTime = fCurTime;
	}
	GLOBAL_DATA_MTX_UNLOCK();

	return nBytesWritten;
}

template <typename TMutexType>
class CWorkerAccumState
{
public:
	explicit CWorkerAccumState( TMutexType* pMutex ) :
		m_pMutex( pMutex ), m_iFirstCommand( 0 ), m_iNextCommand( 0 ),
		m_iEndCommand( 0 ), m_iLastFinished( 0 ),
		m_hCombo( nullptr )
		{
		}
	~CWorkerAccumState() { QuitSubs(); }

	void RangeBegin( uint64 iFirstCommand, uint64 iEndCommand );
	void RangeFinished();

	void ExecuteCompileCommand( CfgProcessor::ComboHandle hCombo );
	void ExecuteCompileCommandThreaded( CfgProcessor::ComboHandle hCombo );
	void HandleCommandResponse( CfgProcessor::ComboHandle hCombo, CmdSink::IResponse* pResponse );

public:
	void Run( uint32 ov = 0 )
	{
		uint32 i = ov ? ov : std::thread::hardware_concurrency();
		std::vector<std::thread> active;

		while ( i-- > 0 )
		{
			++m_nActive;
			active.emplace_back( DoExecute, this );
		}

		while ( m_nActive )
		{
			_mm_pause();
			Sleep( 250 );
		}

		std::for_each( active.begin(), active.end(), []( std::thread& t ) { t.join(); } );
	}

public:
	bool OnProcess();
	bool OnProcessST();

protected:
	Threading::CInterlockedInt m_nActive;
	TMutexType* m_pMutex;

	static void DoExecute( CWorkerAccumState* pThis )
	{
		while ( pThis->OnProcess() )
			continue;

		--pThis->m_nActive;
	}

protected:
	struct SubProcess
	{
		size_t dwIndex;
		DWORD dwSvcThreadId;
		uint64 iRunningCommand;
		PROCESS_INFORMATION pi;
		SubProcessKernelObjects* pCommObjs;
		std::thread* pThread;
	};
	Threading::CThreadLocal<SubProcess*> m_lpSubProcessInfo;
	std::vector<SubProcess*> m_arrSubProcessInfos;
	uint64 m_iFirstCommand;
	uint64 m_iNextCommand;
	uint64 m_iEndCommand;

	uint64 m_iLastFinished;

	CfgProcessor::ComboHandle m_hCombo;

	void QuitSubs();
	void TryToPackageData( uint64 iCommandNumber );
	void PrepareSubProcess( SubProcess** ppSp, SubProcessKernelObjects** ppCommObjs );
};

template <typename TMutexType>
void CWorkerAccumState<TMutexType>::RangeBegin( uint64 iFirstCommand, uint64 iEndCommand )
{
	m_iFirstCommand = iFirstCommand;
	m_iNextCommand = iFirstCommand;
	m_iEndCommand = iEndCommand;
	m_iLastFinished = iFirstCommand;
	m_hCombo = nullptr;
	CfgProcessor::Combo_GetNext( m_iNextCommand, m_hCombo, m_iEndCommand );

	// Notify all connected sub-processes that the master is still alive
	for ( SubProcess* pSp : m_arrSubProcessInfos )
	{
		SubProcessKernelObjects_Memory shrmem( pSp->pCommObjs );
		if ( void* pvMemory = shrmem.Lock() )
		{
			strcpy_s( static_cast<char*>( pvMemory ), 32, "keepalive" );
			shrmem.Unlock();
		}
	}
}

template <typename TMutexType>
void CWorkerAccumState<TMutexType>::RangeFinished()
{
	// Finish packaging data
	TryToPackageData( m_iEndCommand - 1 );
}

template <typename TMutexType>
void CWorkerAccumState<TMutexType>::QuitSubs()
{
	std::vector<HANDLE> m_arrWait;
	m_arrWait.reserve( m_arrSubProcessInfos.size() );
	std::vector<std::thread*> m_arrWait2;
	m_arrWait2.reserve( m_arrSubProcessInfos.size() );

	for ( SubProcess* pSp : m_arrSubProcessInfos )
	{
		SubProcessKernelObjects_Memory shrmem( pSp->pCommObjs );
		if ( void* pvMemory = shrmem.Lock() )
		{
			strcpy_s( static_cast<char*>( pvMemory ), 10, "quit" );
			shrmem.Unlock();
		}

		if ( pSp->pi.hProcess )
			m_arrWait.emplace_back( pSp->pi.hProcess );
		else if ( pSp->pThread )
			m_arrWait2.emplace_back( pSp->pThread );
	}

	if ( !m_arrWait.empty() )
	{
		const DWORD dwWait = WaitForMultipleObjects( gsl::narrow<DWORD>( m_arrWait.size() ), m_arrWait.data(), TRUE, 2 * 1000 );
		if ( WAIT_TIMEOUT == dwWait )
			std::cout << clr::pinkish << "Timed out while waiting for sub-processes to shut down!" << clr::reset << std::endl;
	}

	if ( !m_arrWait2.empty() )
		std::for_each( m_arrWait2.begin(), m_arrWait2.end(), []( std::thread* t ) { t->join(); } );

	for ( SubProcess* pSp : m_arrSubProcessInfos )
	{
		if ( pSp->pi.hThread )
			CloseHandle( pSp->pi.hThread );
		if ( pSp->pi.hProcess )
			CloseHandle( pSp->pi.hProcess );
		if ( pSp->pThread )
			delete pSp->pThread;

		delete pSp->pCommObjs;
		delete pSp;
	}
}

template <size_t N>
static void PrepareFlagsForSubprocess( char( &pBuf )[N] )
{
	if ( gFlags & D3DCOMPILE_PARTIAL_PRECISION )
		strcat_s( pBuf, "/Gpp " );

	if ( gFlags & D3DCOMPILE_SKIP_VALIDATION )
		strcat_s( pBuf, "/Vd " );

	if ( gFlags & D3DCOMPILE_NO_PRESHADER )
		strcat_s( pBuf, "/Op " );

	if ( gFlags & D3DCOMPILE_AVOID_FLOW_CONTROL )
		strcat_s( pBuf, "/Gfa " );
	else if ( gFlags & D3DCOMPILE_PREFER_FLOW_CONTROL )
		strcat_s( pBuf, "/Gfp " );

	if ( gFlags & D3DCOMPILE_SKIP_OPTIMIZATION )
		strcat_s( pBuf, "/Od" );

	V_StrTrim( pBuf );
}

template <typename TMutexType>
void CWorkerAccumState<TMutexType>::PrepareSubProcess( SubProcess** ppSp, SubProcessKernelObjects** ppCommObjs )
{
	SubProcess* pSp = m_lpSubProcessInfo.Get();
	SubProcessKernelObjects* pCommObjs;

	if ( pSp )
		pCommObjs = pSp->pCommObjs;
	else
	{
		pSp = new SubProcess;
		m_lpSubProcessInfo.Set( pSp );

		pSp->dwSvcThreadId = GetCurrentThreadId();
		pSp->pThread = nullptr;

		char chBaseNameBuffer[0x30];
		sprintf_s( chBaseNameBuffer, "SHCMPL_SUB_%08lX_%08llX_%08lX", pSp->dwSvcThreadId, time( nullptr ), GetCurrentProcessId() );
		pCommObjs = pSp->pCommObjs = new SubProcessKernelObjects_Create( chBaseNameBuffer );

		ZeroMemory( &pSp->pi, sizeof( pSp->pi ) );

		if ( !cmdLine.isSet( "-local" ) )
		{
			STARTUPINFO si;
			ZeroMemory( &si, sizeof( si ) );
			si.cb = sizeof( si );

			char flags[32] = { 0 };
			PrepareFlagsForSubprocess( flags );

			char chCommandLine[0x100], chFileName[0x100];
			sprintf_s( chFileName, "%s\\ShaderCompile.exe", g_ExeDir );
			sprintf_s( chCommandLine, "-subprocess %s %s", chBaseNameBuffer, flags );
			[[maybe_unused]] const BOOL bCreateResult = CreateProcess( chFileName, chCommandLine, nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, g_ExeDir, &si, &pSp->pi );
			Assert( bCreateResult );
		}
		else
		{
			pSp->pThread = new std::thread( ShaderCompile_Subprocess_Main, std::string( chBaseNameBuffer ), gFlags, true );
		}

		m_pMutex->Lock();
		pSp->dwIndex = m_arrSubProcessInfos.size();
		m_arrSubProcessInfos.emplace_back( pSp );
		m_pMutex->Unlock();
	}

	if ( ppSp )
		*ppSp = pSp;
	if ( ppCommObjs )
		*ppCommObjs = pCommObjs;
}

template <typename TMutexType>
void CWorkerAccumState<TMutexType>::ExecuteCompileCommandThreaded( CfgProcessor::ComboHandle hCombo )
{
	SubProcessKernelObjects* pCommObjs = nullptr;
	PrepareSubProcess( nullptr, &pCommObjs );

	// Execute the command
	SubProcessKernelObjects_Memory shrmem( pCommObjs );

	{
		void* pvMemory = shrmem.Lock();
		Assert( pvMemory );

		Combo_FormatCommand( hCombo, gsl::make_span( static_cast<char*>( pvMemory ), 4 * 1024 * 1024 - 2 * sizeof( DWORD ) ) );

		shrmem.Unlock();
	}

	// Obtain the command response
	{
		const void* pvMemory = shrmem.Lock();
		Assert( pvMemory );

		// TODO: Vitaliy :: TEMP fix:
		// Usually what happens if we fail to lock here is
		// when our subprocess dies and to recover we will
		// attempt to restart on another worker.
		if ( !pvMemory )
			// ::RaiseException( GetLastError(), EXCEPTION_NONCONTINUABLE, 0, NULL );
			TerminateProcess( GetCurrentProcess(), 1 );

		CmdSink::IResponse* pResponse;
		if ( pvMemory )
			pResponse = new CSubProcessResponse( pvMemory );
		else
			pResponse = new CmdSink::CResponseError;

		HandleCommandResponse( hCombo, pResponse );

		shrmem.Unlock();
	}
}

template <typename TMutexType>
void CWorkerAccumState<TMutexType>::ExecuteCompileCommand( CfgProcessor::ComboHandle hCombo )
{
	CmdSink::IResponse* pResponse = nullptr;

	char chBuffer[4096];
	Combo_FormatCommand( hCombo, chBuffer );

	if ( g_bVerbose2 )
		std::cout << "running: \"" << clr::green << chBuffer << clr::reset << "\"" << std::endl;

	InterceptFxc::TryExecuteCommand( chBuffer, &pResponse, gFlags );

	HandleCommandResponse( hCombo, pResponse );
}

template <typename TMutexType>
void CWorkerAccumState<TMutexType>::HandleCommandResponse( CfgProcessor::ComboHandle hCombo, CmdSink::IResponse* pResponse )
{
	Assert( pResponse );

	// Command info
	const CfgProcessor::CfgEntryInfo* pEntryInfo = Combo_GetEntryInfo( hCombo );
	const uint64 iComboIndex = Combo_GetComboNum( hCombo );
	const uint64 iCommandNumber = Combo_GetCommandNum( hCombo );

	if ( pResponse->Succeeded() )
	{
		GLOBAL_DATA_MTX_LOCK();
		const uint64 nStComboIdx = iComboIndex / pEntryInfo->m_numDynamicCombos;
		const uint64 nDyComboIdx = iComboIndex - ( nStComboIdx * pEntryInfo->m_numDynamicCombos );
		StaticComboFromDictAdd( pEntryInfo->m_szName, nStComboIdx )->AddDynamicCombo( nDyComboIdx , pResponse->GetResultBuffer(), pResponse->GetResultBufferLen() );
		GLOBAL_DATA_MTX_UNLOCK();
	}

	// Tell the master that this shader failed
	if ( !pResponse->Succeeded() )
	{
		GLOBAL_DATA_MTX_LOCK();
			ShaderHadErrorDispatchInt( pEntryInfo->m_szName );
		GLOBAL_DATA_MTX_UNLOCK();
	}

	// Process listing even if the shader succeeds for warnings
	const char* szListing = pResponse->GetListing();
	if ( szListing || !pResponse->Succeeded() )
	{
		char chCommandNumber[50];
		sprintf_s( chCommandNumber, "%I64u", iCommandNumber );

		char chUnreportedListing[0xFF];
		if ( !szListing )
		{
			sprintf_s( chUnreportedListing, "(%s): error 0000: Compiler failed without error description. Command number %s", pEntryInfo->m_szShaderFileName, chCommandNumber );
			szListing = chUnreportedListing;
		}

		char chBuffer[4096];
		Combo_FormatCommand( hCombo, chBuffer );

		GLOBAL_DATA_MTX_LOCK();
			ErrMsgDispatchMsgLine( chBuffer, szListing );
		GLOBAL_DATA_MTX_UNLOCK();
	}

	// Maybe zip things up
	TryToPackageData( iCommandNumber );
}

template <typename TMutexType>
void CWorkerAccumState<TMutexType>::TryToPackageData( uint64 iCommandNumber )
{
	m_pMutex->Lock();

	uint64 iFinishedByNow = iCommandNumber + 1;

	// Check if somebody is running an earlier command
	for ( SubProcess* pSp : m_arrSubProcessInfos )
	{
		if ( pSp->iRunningCommand < iCommandNumber )
		{
			iFinishedByNow = 0;
			break;
		}
	}

	const uint64 iLastFinished = m_iLastFinished;
	if ( iFinishedByNow > m_iLastFinished )
	{
		m_iLastFinished = iFinishedByNow;
		m_pMutex->Unlock();
	}
	else
	{
		m_pMutex->Unlock();
		return;
	}

	CfgProcessor::ComboHandle hChBegin = CfgProcessor::Combo_GetCombo( iLastFinished );
	CfgProcessor::ComboHandle hChEnd = CfgProcessor::Combo_GetCombo( iFinishedByNow );

	Assert( hChBegin && hChEnd );

	const CfgProcessor::CfgEntryInfo* pInfoBegin = Combo_GetEntryInfo( hChBegin );
	const CfgProcessor::CfgEntryInfo* pInfoEnd = Combo_GetEntryInfo( hChEnd );

	uint64 nComboBegin = Combo_GetComboNum( hChBegin ) / pInfoBegin->m_numDynamicCombos;
	const uint64 nComboEnd = Combo_GetComboNum( hChEnd ) / pInfoEnd->m_numDynamicCombos;

	for ( ; pInfoBegin && ( ( pInfoBegin->m_iCommandStart < pInfoEnd->m_iCommandStart ) || ( nComboBegin > nComboEnd ) ); )
	{
		// Zip this combo
		CUtlBuffer mbPacked;
		const size_t nPackedLength = AssembleWorkerReplyPackage( pInfoBegin, nComboBegin, mbPacked );

		if ( nPackedLength )
		{
			// Packed buffer
			GLOBAL_DATA_MTX_LOCK();
			uint8* pCodeBuffer = StaticComboFromDictAdd( pInfoBegin->m_szName, nComboBegin )->AllocPackedCodeBlock( nPackedLength );
			GLOBAL_DATA_MTX_UNLOCK();

			if ( pCodeBuffer )
			{
				mbPacked.SeekGet( CUtlBuffer::SEEK_HEAD, 0 );
				mbPacked.Get( pCodeBuffer, gsl::narrow<int>( nPackedLength ) );
			}
		}

		// Next iteration
		if ( !nComboBegin-- )
		{
			Combo_Free( hChBegin );
			if ( ( hChBegin = CfgProcessor::Combo_GetCombo( pInfoBegin->m_iCommandEnd ) ) != nullptr )
			{
				pInfoBegin = Combo_GetEntryInfo( hChBegin );
				nComboBegin = pInfoBegin->m_numStaticCombos - 1;
			}
		}
	}

	Combo_Free( hChBegin );
	Combo_Free( hChEnd );
}

template <typename TMutexType>
bool CWorkerAccumState<TMutexType>::OnProcess()
{
	m_pMutex->Lock();
		CfgProcessor::ComboHandle hThreadCombo = m_hCombo ? Combo_Alloc( m_hCombo ) : nullptr;
	m_pMutex->Unlock();

	uint64 iThreadCommand = ~uint64( 0 );

	SubProcess* pSp = nullptr;
	PrepareSubProcess( &pSp, nullptr );

	for (;;)
	{
		m_pMutex->Lock();
			if ( m_hCombo )
			{
				Combo_Assign( hThreadCombo, m_hCombo );
				pSp->iRunningCommand = Combo_GetCommandNum( hThreadCombo );
				Combo_GetNext( iThreadCommand, m_hCombo, m_iEndCommand );
			}
			else
			{
				Combo_Free( hThreadCombo );
				iThreadCommand = ~uint64(0);
				pSp->iRunningCommand = ~uint64(0);
			}
		m_pMutex->Unlock();

		if ( hThreadCombo )
			ExecuteCompileCommandThreaded( hThreadCombo );
		else
			break;
	}

	Combo_Free( hThreadCombo );
	return false;
}

template <typename TMutexType>
bool CWorkerAccumState<TMutexType>::OnProcessST()
{
	while ( m_hCombo )
	{
		ExecuteCompileCommand( m_hCombo );

		Combo_GetNext( m_iNextCommand, m_hCombo, m_iEndCommand );
	}
	return false;
}

//
// Worker_ProcessCommandRange_Singleton
//
class Worker_ProcessCommandRange_Singleton
{
public:
	static Worker_ProcessCommandRange_Singleton*& Instance() { static Worker_ProcessCommandRange_Singleton* s_ptr = nullptr; return s_ptr; }
	static Worker_ProcessCommandRange_Singleton* GetInstance() { Worker_ProcessCommandRange_Singleton* p = Instance(); Assert( p ); return p; }

public:
	Worker_ProcessCommandRange_Singleton() { Assert( !Instance() ); Instance() = this; Startup(); }
	~Worker_ProcessCommandRange_Singleton() { Assert( Instance() == this ); Instance() = nullptr; Shutdown(); }

public:
	void ProcessCommandRange( uint64 shaderStart, uint64 shaderEnd );

protected:
	void Startup();
	void Shutdown();

	//
	// Multi-threaded section
protected:
	struct MT
	{
		MT() : pWorkerObj( nullptr ) {}

		//using MultiThreadMutex_t = Threading::CThreadFastMutex;
		using MultiThreadMutex_t = Threading::CThreadMutex;
		MultiThreadMutex_t mtx;

		using WorkerClass_t = CWorkerAccumState<MultiThreadMutex_t>;
		WorkerClass_t* pWorkerObj;
	} m_MT;

	//
	// Single-threaded section
protected:
	struct ST
	{
		ST() : pWorkerObj( nullptr ) {}

		using SingleThreadMutex_t = Threading::CThreadNullMutex;
		SingleThreadMutex_t mtx;

		using WorkerClass_t = CWorkerAccumState<SingleThreadMutex_t>;
		WorkerClass_t* pWorkerObj;
	} m_ST;
};

void Worker_ProcessCommandRange_Singleton::Startup()
{
	unsigned long threads;
	cmdLine.get( "-threads" )->getULong( threads );
	if ( ( threads ? threads : std::thread::hardware_concurrency() ) > 1 )
	{
		// Make sure that our mutex is in multi-threaded mode
		Threading::g_mtxGlobal.SetThreadedMode( Threading::eMultiThreaded );

		m_MT.pWorkerObj = new MT::WorkerClass_t( &m_MT.mtx );
	}
	else
		// Otherwise initialize single-threaded mode
		m_ST.pWorkerObj = new ST::WorkerClass_t( &m_ST.mtx );
}

void Worker_ProcessCommandRange_Singleton::Shutdown()
{
	if ( m_MT.pWorkerObj )
		delete m_MT.pWorkerObj;
	else if ( m_ST.pWorkerObj )
		delete m_ST.pWorkerObj;
}

void Worker_ProcessCommandRange_Singleton::ProcessCommandRange( uint64 shaderStart, uint64 shaderEnd )
{
	if ( m_MT.pWorkerObj )
	{
		MT::WorkerClass_t* pWorkerObj = m_MT.pWorkerObj;

		pWorkerObj->RangeBegin( shaderStart, shaderEnd );
		unsigned long threads;
		cmdLine.get( "-threads" )->getULong( threads );
		pWorkerObj->Run( threads );
		pWorkerObj->RangeFinished();
	}
	else
	{
		ST::WorkerClass_t* pWorkerObj = m_ST.pWorkerObj;

		pWorkerObj->RangeBegin( shaderStart, shaderEnd );
		pWorkerObj->OnProcessST();
		pWorkerObj->RangeFinished();
	}
}

// You must process the work unit range.
static void Worker_ProcessCommandRange( uint64 shaderStart, uint64 shaderEnd )
{
	Worker_ProcessCommandRange_Singleton::GetInstance()->ProcessCommandRange( shaderStart, shaderEnd );
}

static void Shader_ParseShaderInfoFromCompileCommands( const CfgProcessor::CfgEntryInfo* pEntry, ShaderInfo_t& shaderInfo )
{
	if ( CfgProcessor::ComboHandle hCombo = CfgProcessor::Combo_GetCombo( pEntry->m_iCommandStart ) )
	{
		char cmd[4096] = { 0 };
		Combo_FormatCommand( hCombo, cmd );

		{
			memset( &shaderInfo, 0, sizeof( ShaderInfo_t ) );

			const char* pCentroidMask = strstr( cmd, "/DCENTROIDMASK=" );
			const char* pFlags = strstr( cmd, "/DFLAGS=0x" );
			const char* pShaderModel = strstr( cmd, "/DSHADER_MODEL_" );

			if ( !pCentroidMask || !pFlags || !pShaderModel )
			{
				Assert( !"!pCentroidMask || !pFlags || !pShaderModel" );
				return;
			}

			sscanf_s( pCentroidMask + strlen( "/DCENTROIDMASK=" ), "%u", &shaderInfo.m_CentroidMask );
			sscanf_s( pFlags + strlen( "/DFLAGS=0x" ), "%x", &shaderInfo.m_Flags );

			// Copy shader model
			pShaderModel += strlen( "/DSHADER_MODEL_" );
			for ( char *pszSm = shaderInfo.m_szShaderModel, * const pszEnd = pszSm + sizeof( shaderInfo.m_szShaderModel ) - 1; pszSm < pszEnd ; ++ pszSm )
			{
				char& rchLastChar = *pszSm = *pShaderModel++;
				if ( !rchLastChar || isspace( rchLastChar ) || '=' == rchLastChar )
				{
					rchLastChar = 0;
					break;
				}
			}

			shaderInfo.m_nShaderCombo = 0;
			shaderInfo.m_nTotalShaderCombos = pEntry->m_numCombos;
			shaderInfo.m_nDynamicCombos = pEntry->m_numDynamicCombos;
			shaderInfo.m_nStaticCombo = 0;

			shaderInfo.m_pShaderName = pEntry->m_szName;
			shaderInfo.m_pShaderSrc = pEntry->m_szShaderFileName;
		}

		Combo_Free( hCombo );
	}
}

static bool ReadFile( const char* fileName, CUtlBuffer& buf )
{
	std::ifstream file( fileName, std::ios::binary | std::ios::ate );
	if ( file.fail() )
		return false;
	std::vector<char> data( gsl::narrow<size_t>( file.tellg() ) );
	{
		const auto& find = []( const char& a, const char& b ) { return a == '\r' && b == '\n'; };
		file.clear();
		file.seekg( 0, std::ios::beg );
		file.read( data.data(), data.size() );
		for ( std::vector<char>::iterator i; ( i = std::adjacent_find( data.begin(), data.end(), find ) ) != data.end(); )
			data.erase( i );

		const int size = gsl::narrow<int>( data.size() );
		buf.EnsureCapacity( size );
		memcpy( static_cast<char*>( buf.PeekPut() ), data.data(), size );
		buf.SeekPut( CUtlBuffer::SEEK_CURRENT, size );
	}

	return true;
}

static void Worker_GetLocalCopyOfShaders()
{
	// Create virtual files for all of the stuff that we need to compile the shader
	// make sure and prefix the file name so that it doesn't find it locally.

	char filename[1024];
	sprintf_s( filename, "%s\\uniquefilestocopy.txt", g_pShaderPath.c_str() );

	CUtlInplaceBuffer bffr( 0, 0, CUtlBuffer::TEXT_BUFFER );
	if ( !ReadFile( filename, bffr ) )
	{
		std::cout << clr::pinkish << "Can't open " << clr::red << "uniquefilestocopy.txt" << clr::pinkish << "!" << clr::reset << std::endl;
		exit( -1 );
	}

	while ( char* pszLineToCopy = bffr.InplaceGetLinePtr() )
	{
		const char* ext = V_GetFileExtension( pszLineToCopy );
		V_StrTrim( pszLineToCopy );
		if ( _stricmp( "h", ext ) && _stricmp( "fxc", ext ) )
			continue;

		if ( V_IsAbsolutePath( pszLineToCopy ) )
			strcpy_s( filename, pszLineToCopy );
		else
			sprintf_s( filename, "%s\\%s", g_pShaderPath.c_str(), pszLineToCopy );

		std::ifstream src( filename, std::ios::binary | std::ios::ate );
		if ( !src )
		{
			std::cout << clr::pinkish <<  "Can't find \"" << clr::red << filename << clr::pinkish << "\"" << std::endl;
			continue;
		}

		char justFilename[MAX_PATH];
		char* pLastSlash = Max( strrchr( pszLineToCopy, '/' ), strrchr( pszLineToCopy, '\\' ) );
		if ( pLastSlash )
			strcpy_s( justFilename, pLastSlash + 1 );
		else
			strcpy_s( justFilename, pszLineToCopy );

		if ( g_bVerbose )
			std::cout << "adding file to cache: \"" << clr::green << justFilename << clr::reset << "\"" << std::endl;

		std::vector<char> data( gsl::narrow<size_t>( src.tellg() ) );
		src.clear();
		src.seekg( 0, std::ios::beg );
		src.read( data.data(), data.size() );

		fileCache.Add( justFilename, reinterpret_cast<const uint8*>( data.data() ), data.size() );
	}
}

static void Shared_ParseListOfCompileCommands()
{
	const double tt_start = Plat::FloatTime();

	char fileListFileName[1024];
	sprintf_s( fileListFileName, "%s\\filelist.txt", g_pShaderPath.c_str() );

	CUtlInplaceBuffer bffr( 0, 0, CUtlInplaceBuffer::TEXT_BUFFER );
	if ( !ReadFile( fileListFileName, bffr ) )
	{
		std::cout << clr::pinkish << "Can't open \"" << clr::red << fileListFileName << clr::pinkish << "\"!" << clr::reset << std::endl;
		exit( -1 );
	}

	CfgProcessor::ReadConfiguration( &bffr );
	CfgProcessor::DescribeConfiguration( g_arrCompileEntries );

	for ( const CfgProcessor::CfgEntryInfo* pInfo = g_arrCompileEntries.get(); pInfo && pInfo->m_szName; ++pInfo )
	{
		++g_numShaders;
		g_numStaticCombos += pInfo->m_numStaticCombos;
		g_numCompileCommands = pInfo->m_iCommandEnd;
	}

	const double tt_end = Plat::FloatTime();

	std::cout << "\rCompiling " << clr::green << PrettyPrint( g_numCompileCommands ) << clr::reset << " commands, setup took " << clr::green << ( tt_end - tt_start ) << clr::reset << " seconds.         \r";
}

static void SetupExeDir( int argc, const char* argv[] )
{
	strcpy_s( g_ExeDir, argv[0] );
	V_StripFilename( g_ExeDir );

	if ( g_ExeDir[0] == 0 )
		strcpy_s( g_ExeDir, ".\\" );

	V_FixSlashes( g_ExeDir );
}

static void SetupPaths( int argc, const char* argv[] )
{
	cmdLine.get( "-shaderpath" )->getString( g_pShaderPath );

	g_bVerbose = cmdLine.isSet( "-verbose" );
	g_bVerbose2 = cmdLine.isSet( "-verbose2" );
}

static void CompileShaders_NoVMPI()
{
	Worker_ProcessCommandRange_Singleton pcr;

	//
	// We will iterate on the cfg entries and process them
	//
	for ( const CfgProcessor::CfgEntryInfo* pEntry = g_arrCompileEntries.get(); pEntry && pEntry->m_szName; ++pEntry )
	{
		//
		// Stick the shader info
		//
		ShaderInfo_t siLastShaderInfo;
		memset( &siLastShaderInfo, 0, sizeof( siLastShaderInfo ) );

		Shader_ParseShaderInfoFromCompileCommands( pEntry, siLastShaderInfo );

		g_ShaderToShaderInfo[pEntry->m_szName] = siLastShaderInfo;

		//
		// Compile stuff
		//
		Worker_ProcessCommandRange( pEntry->m_iCommandStart, pEntry->m_iCommandEnd );

		//
		// Now when the whole shader is finished we can write it
		//
		const char* szShaderToWrite = pEntry->m_szName;
		g_numCommandsCompleted = g_numCompileCommands;
		WriteShaderFiles( szShaderToWrite );
		g_numCommandsCompleted = pEntry->m_iCommandEnd;
	}

	std::cout << "\r                                                  \r";
}

static bool WriteMiniDumpUsingExceptionInfo( _EXCEPTION_POINTERS* pExceptionInfo, MINIDUMP_TYPE minidumpType )
{
	// create a unique filename for the minidump based on the current time and module name
	time_t currTime = time( nullptr );
	struct tm pTime;
	localtime_s( &pTime, &currTime );

	// strip off the rest of the path from the .exe name
	char rgchModuleName[MAX_PATH];
	::GetModuleFileName( nullptr, rgchModuleName, ARRAYSIZE( rgchModuleName ) );
	char* pch1 = strchr( rgchModuleName, '.' );
	if ( pch1 )
		*pch1 = 0;
	const char* pch = strchr( rgchModuleName, '\\' );
	if ( pch )
		// move past the last slash
		pch++;
	else
		pch = "unknown";

	// can't use the normal string functions since we're in tier0
	char rgchFileName[MAX_PATH];
	_snprintf_s( rgchFileName, ARRAYSIZE( rgchFileName ),
				 "%s_%s_%d%.2d%2d%.2d%.2d%.2d_%d.mdmp",
				 pch,
				 "crash",
				 pTime.tm_year + 1900,	/* Year less 2000 */
				 pTime.tm_mon + 1,		/* month (0 - 11 : 0 = January) */
				 pTime.tm_mday,			/* day of month (1 - 31) */
				 pTime.tm_hour,			/* hour (0 - 23) */
				 pTime.tm_min,		    /* minutes (0 - 59) */
				 pTime.tm_sec,		    /* seconds (0 - 59) */
				 0		// ensures the filename is unique
	);

	BOOL bMinidumpResult = FALSE;
	const HANDLE hFile = ::CreateFile( rgchFileName, GENERIC_WRITE, FILE_SHARE_WRITE, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr );

	if ( hFile )
	{
		// dump the exception information into the file
		_MINIDUMP_EXCEPTION_INFORMATION	ExInfo;
		ExInfo.ThreadId = GetCurrentThreadId();
		ExInfo.ExceptionPointers = pExceptionInfo;
		ExInfo.ClientPointers = FALSE;

		bMinidumpResult = MiniDumpWriteDump( ::GetCurrentProcess(), ::GetCurrentProcessId(), hFile, minidumpType, &ExInfo, nullptr, nullptr );
		CloseHandle( hFile );
	}

	// mark any failed minidump writes by renaming them
	if ( !bMinidumpResult )
	{
		char rgchFailedFileName[_MAX_PATH];
		_snprintf_s( rgchFailedFileName, ARRAYSIZE( rgchFailedFileName ), "(failed)%s", rgchFileName );
		rename( rgchFileName, rgchFailedFileName );
	}

	return bMinidumpResult;
}

static LONG __stdcall ToolsExceptionFilter( struct _EXCEPTION_POINTERS* ExceptionInfo )
{
	// Non VMPI workers write a minidump and show a crash dialog like normal.
	constexpr const int iType = MiniDumpNormal | MiniDumpWithDataSegs | MiniDumpWithIndirectlyReferencedMemory;

	WriteMiniDumpUsingExceptionInfo( ExceptionInfo, static_cast<MINIDUMP_TYPE>( iType ) );
	return EXCEPTION_CONTINUE_SEARCH;
}


static int ShaderCompile_Main( int argc, const char* argv[] )
{
	{
		const HANDLE console = GetStdHandle( STD_OUTPUT_HANDLE );
		DWORD mode;
		GetConsoleMode( console, &mode );
		SetConsoleMode( console, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING );
	}

	cmdLine.overview = "Source shader compiler.";
	cmdLine.syntax = "ShaderCompile [OPTIONS]";
	cmdLine.add( "", false, 0, 0, "Shows help", "-help", "-h", "/help", "/h" );
	cmdLine.add( "", false, 0, 0, "Compiles shader with partial precission", "/Gpp", "--partial-precision" );
	cmdLine.add( "", false, 0, 0, "Skips shader validation", "/Vd", "--no-validation" );
	cmdLine.add( "", false, 0, 0, "Disables preshader generation", "/Op", "--disable-preshader" );
	cmdLine.add( "", false, 0, 0, "Directs the compiler to not use flow-control constructs where possible", "/Gfa", "--no-flow-control" );
	cmdLine.add( "", false, 0, 0, "Directs the compiler to use flow-control constructs where possible", "/Gfp", "--prefer-flow-control" );
	cmdLine.add( "", false, 0, 0, "Disables shader optimization", "/Od", "--disable-optimization" );
	cmdLine.add( "", false, 0, 0, "Enables extended state printing", "-verbose" );
	cmdLine.add( "", false, 0, 0, "Enables even more extended state printing", "-verbose2" );
	cmdLine.add( "", false, 0, 0, "Enables extended compile error messages", "-verbose_errors" );
	cmdLine.add( "0", false, 1, 0, "Number of threads used, defaults to core count", "-threads", "/threads" );
	cmdLine.add( "", true, 1, 0, "Base path for shaders", "-shaderpath" );
	cmdLine.add( "", false, 1, 0, " ", "-subprocess" );
	cmdLine.add( "", false, 0, 0, "Do not spawn any child subprocess", "-local" );

	cmdLine.parse( argc, argv );

	if ( cmdLine.isSet( "-help" ) )
	{
		std::string usage;
		cmdLine.getUsageDescriptions( usage, 80, ez::ezOptionParser::ALIGN );
		std::cout << cmdLine.overview << "\n\n" << clr::green << clr::bold << "OPTIONS:\n" << clr::reset << usage << std::endl;
		return 0;
	}

	g_flStartTime = Plat::FloatTime();

	if ( cmdLine.isSet( "/Gpp" ) )
		gFlags |= D3DCOMPILE_PARTIAL_PRECISION;
	if ( cmdLine.isSet( "/Vd" ) )
		gFlags |= D3DCOMPILE_SKIP_VALIDATION;
	if ( cmdLine.isSet( "/Op" ) )
		gFlags |= D3DCOMPILE_NO_PRESHADER;

	// Flow control
	if ( cmdLine.isSet( "/Gfa" ) )
		gFlags |= D3DCOMPILE_AVOID_FLOW_CONTROL;
	else if ( cmdLine.isSet( "/Gfp" ) )
		gFlags |= D3DCOMPILE_PREFER_FLOW_CONTROL;

	// Optimization
	if ( cmdLine.isSet( "/Od" ) )
		gFlags |= D3DCOMPILE_SKIP_OPTIMIZATION;

	/*
	Special section of code implementing "-subprocess" flag
	*/
	if ( cmdLine.isSet( "-subprocess" ) )
	{
		std::string subprocess;
		cmdLine.get( "-subprocess" )->getString( subprocess );
		return ShaderCompile_Subprocess_Main( subprocess, gFlags, false );
	}

	std::vector<std::string> badOptions;
	if ( !cmdLine.gotRequired( badOptions ) )
	{
		std::cout << clr::red << clr::bold << "ERROR: Missing argument" << ( badOptions.size() == 1 ? ": " : "s:\n" ) << clr::reset;
		for ( const auto& option : badOptions )
			std::cout << option << "\n";
		std::cout << clr::reset << std::endl;
		return -1;
	}

	if ( !cmdLine.gotExpected( badOptions ) )
	{
		std::cout << clr::red << clr::bold << "ERROR: Got unexpected number of arguments for option" << ( badOptions.size() == 1 ? ": " : "s:\n" ) << clr::reset;
		for ( const auto& option : badOptions )
			std::cout << option << "\n";
		std::cout << clr::reset << std::endl;
		return -1;
	}

	// This needs to get called before VMPI is setup because in SDK mode, VMPI will change the args around.
	SetupExeDir( argc, argv );

	SetupPaths( argc, argv );

	// Setting up the minidump handlers
	SetUnhandledExceptionFilter( ToolsExceptionFilter );
	Shared_ParseListOfCompileCommands();

	Worker_GetLocalCopyOfShaders();

	std::cout << "\rCompiling " << clr::green << PrettyPrint( g_numCompileCommands ) << clr::reset << " commands in " << clr::green << PrettyPrint( g_numStaticCombos ) << clr::reset << " static combos.                      \r";
	CompileShaders_NoVMPI();

	{
		char str[4096];

		// Write everything that succeeded
		const int nStrings = g_ShaderByteCode.GetNumStrings();
		for ( int i = 0; i < nStrings; i++ )
			WriteShaderFiles( g_ShaderByteCode.String( i ) );

		// Write all the errors
		//////////////////////////////////////////////////////////////////////////
		//
		// Now deliver all our accumulated spew to the output
		//
		//////////////////////////////////////////////////////////////////////////

		if ( const int warnings = g_Master_CompilerMsgWarning.GetNumStrings() )
			std::cout << clr::yellow << warnings << "WARNINGS:" << clr::reset << std::endl;

		for ( int k = 0, kEnd = g_Master_CompilerMsgWarning.GetNumStrings(); k < kEnd; ++k )
		{
			const char* const szMsg = g_Master_CompilerMsgError.String( k );
			const CompilerMsgInfo& cmi = g_Master_CompilerMsgError[gsl::narrow<UtlSymId_t>( k )];
			const int numReported = cmi.GetNumTimesReported();

			std::cout << std::quoted( szMsg ) << " Reported " << clr::green << numReported << clr::reset << " time(s): " << szMsg << std::endl;
		}

		if ( const int errors = g_Master_CompilerMsgError.GetNumStrings() )
			std::cout << clr::red << errors << "WARNINGS:" << clr::reset << std::endl;

		const bool bValveVerboseComboErrors = cmdLine.isSet( "-verbose_errors" );

		// Compiler spew
		for ( int k = 0, kEnd = g_Master_CompilerMsgError.GetNumStrings(); k < kEnd; ++k )
		{
			const char* const szMsg = g_Master_CompilerMsgError.String( k );
			const CompilerMsgInfo& cmi = g_Master_CompilerMsgError[gsl::narrow<UtlSymId_t>( k )];

			const char* const szFirstCmd = cmi.GetFirstCommand();
			const int numReported = cmi.GetNumTimesReported();

			uint64 iFirstCommand = _strtoui64( szFirstCmd, nullptr, 10 );
			CfgProcessor::ComboHandle hCombo = nullptr;
			const CfgProcessor::CfgEntryInfo* pComboEntryInfo = nullptr;
			if ( CfgProcessor::Combo_GetNext( iFirstCommand, hCombo, g_numCompileCommands ) )
			{
				Combo_FormatCommand( hCombo, str );
				pComboEntryInfo = Combo_GetEntryInfo( hCombo );
				Combo_Free( hCombo );
			}
			else
				sprintf_s( str, "cmd # %s", szFirstCmd );

			std::cout << std::quoted( szMsg ) << " Reported " << clr::green << numReported << clr::reset << " time(s), example command: " << std::endl;

			if ( bValveVerboseComboErrors )
			{
				std::cout << "    Verbose Description:\n";
				if ( pComboEntryInfo )
				{
					std::cout << "        Src File: " << clr::green << pComboEntryInfo->m_szShaderFileName << clr::reset << "\n";
					std::cout << "        Tgt File: " << clr::green << pComboEntryInfo->m_szName << clr::reset << "\n";
				}

				// Between     /DSHADERCOMBO=   and    /Dmain
				const char* pBegin = strstr( str, "/DSHADERCOMBO=" );
				const char* pEnd = strstr( str, "/Dmain" );
				if ( pBegin )
				{
					pBegin += strlen( "/DSHADERCOMBO=" ) ;
					const char* pSpace = strchr( pBegin, ' ' );
					if ( pSpace )
					{
						std::cout << "        Combo # : "<< clr::green << PrettyPrint( strtoull( pBegin, nullptr, 16 ) ) << clr::reset << std::endl;
					}
				}

				if ( !pEnd )
					pEnd = str + strlen( str );
				while ( pBegin && *pBegin && !isspace( *pBegin ) )
					++pBegin;
				while ( pBegin && *pBegin && isspace( *pBegin ) )
					++pBegin;

				// Now parse all combo defines in [pBegin, pEnd]
				while ( pBegin && *pBegin && ( pBegin < pEnd ) )
				{
					const char *pDefine = strstr( pBegin, "/D" );
					if ( !pDefine || pDefine >= pEnd )
						break;

					const char* pEqSign = strchr( pDefine, '=' );
					if ( !pEqSign || pEqSign >= pEnd )
						break;

					const char* pSpace = strchr( pEqSign, ' ' );
					if ( !pSpace || pSpace >= pEnd )
						pSpace = pEnd;

					pBegin = pSpace;

					std::cout << "                  " << clr::pinkish << std::string( pEqSign + 1 ).substr( 0, pSpace - pEqSign - 1 ) << " " << clr::red << std::string( pDefine + 2 ).substr( 0, pEqSign - pDefine - 2 ) << std::endl;
				}
				std::cout << clr::reset;
			}

			std::cout << "    " << clr::green << str << clr::reset << std::endl;
		}

		// Failed shaders summary
		for ( int k = 0, kEnd = g_Master_ShaderHadError.GetNumStrings(); k < kEnd; ++ k )
		{
			const char* szShaderName = g_Master_ShaderHadError.String( k );
			if ( !g_Master_ShaderHadError[gsl::narrow<UtlSymId_t>( k )] )
				continue;

			std::cout << clr::pinkish << "FAILED: " << clr::red << szShaderName << clr::reset << std::endl;
		}

		//
		// End
		//
		const double end = Plat::FloatTime();

		std::cout << clr::green << FormatTime( static_cast<uint64>( end - g_flStartTime ) ) << clr::reset << " elapsed" << std::endl;
	}

	return g_Master_ShaderHadError.GetNumStrings();
}

int main( int argc, const char* argv[] )
{
	try
	{
		return ShaderCompile_Main( argc, argv );
	}
	catch ( const std::exception& e )
	{
		std::cout << "Exception occured: "  << clr::red<< e.what() << clr::reset << std::endl;
		return -1;
	}
}