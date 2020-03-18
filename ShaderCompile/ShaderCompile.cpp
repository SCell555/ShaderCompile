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

#include "DbgHelp.h"
#include "conio.h"
#include "d3dcompiler.h"
#include "direct.h"
#include "io.h"
#include "sys/stat.h"
#include <chrono>
#include <future>
#include <iomanip>
#include <regex>
#include <thread>
#include <filesystem>

#include "basetypes.h"
#include "cfgprocessor.h"
#include "cmdsink.h"
#include "d3dxfxc.h"
#include "shader_vcs_version.h"
#include "subprocess.h"
#include "utlbuffer.h"
#include "utlnodehash.h"
#include "utlstringmap.h"

#include "ezOptionParser.hpp"
#include "termcolor/style.hpp"
#include "gsl/string_span"

#include "CRC32.hpp"
#include "movingaverage.hpp"
#include "termcolors.hpp"
#include "strmanip.hpp"
#include "shaderparser.h"

extern "C" {
#define _7ZIP_ST

#include "C/7zTypes.h"
#include "C/LzFind.c"
#include "C/LzmaEnc.c"

#undef _7ZIP_ST
}

#include "LZMA.hpp"

#define STATIC_STRLEN( str ) ( ARRAYSIZE( str ) - 1 )

#pragma comment( lib, "DbgHelp" )

// Type conversions should be controlled by programmer explicitly - shadercompile makes use of 64-bit integer arithmetics
#pragma warning( error : 4244 )

static ez::ezOptionParser cmdLine;

// Dealing with job list
static std::unique_ptr<CfgProcessor::CfgEntryInfo[]> g_arrCompileEntries;
static uint64 g_numShaders = 0, g_numCompileCommands = 0, g_numStaticCombos = 0;

using Clock = std::chrono::high_resolution_clock;
std::string g_pShaderPath;
std::string g_pShaderVersion;
uint32_t g_pShaderCRC;
static Clock::time_point g_flStartTime;
bool g_bVerbose         = false;
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
	std::unique_ptr<uint8[]> m_ByteCode;

	CByteCodeBlock()
	{
		m_ByteCode = nullptr;
	}

	CByteCodeBlock( const void* pByteCode, size_t nCodeSize, uint64 nComboID )
	{
		m_ByteCode.reset( new uint8[nCodeSize] );
		m_nComboID  = nComboID;
		m_nCodeSize = nCodeSize;
		memcpy( m_ByteCode.get(), pByteCode, nCodeSize );
		m_nCRC32 = CRC32::ProcessSingleBuffer( m_ByteCode.get(), m_nCodeSize );
	}
};

static bool CompareDynamicComboIDs( const std::unique_ptr<CByteCodeBlock>& pA, const std::unique_ptr<CByteCodeBlock>& pB )
{
	return pA->m_nComboID < pB->m_nComboID;
}

struct CStaticCombo // all the data for one static combo
{
	struct PackedCode : protected std::unique_ptr<uint8[]>
	{
		[[nodiscard]] size_t GetLength() const
		{
			if ( uint8* pb = get() )
				return *reinterpret_cast<size_t*>( pb );
			return 0;
		}
		[[nodiscard]] uint8* GetData() const
		{
			if ( uint8* pb = get() )
				return pb + sizeof( size_t );
			return nullptr;
		}
		[[nodiscard]] uint8* AllocData( size_t len )
		{
			reset();
			if ( len )
			{
				reset( new uint8[len + sizeof( size_t )] );
				*reinterpret_cast<size_t*>( get() ) = len;
			}
			return GetData();
		}

		using std::unique_ptr<byte[]>::operator bool;
	};
	CStaticCombo *m_pNext, *m_pPrev;
private:
	uint64 m_nStaticComboID;

	std::vector<std::unique_ptr<CByteCodeBlock>> m_DynamicCombos;

	PackedCode m_abPackedCode; // Packed code for entire static combo

public:
	[[nodiscard]] __forceinline uint64 Key() const
	{
		return m_nStaticComboID;
	}

	[[nodiscard]] __forceinline uint64 ComboId() const
	{
		return m_nStaticComboID;
	}

	[[nodiscard]] __forceinline CStaticCombo* Next() const
	{
		return m_pNext;
	}

	[[nodiscard]] __forceinline const PackedCode& Code() const
	{
		return m_abPackedCode;
	}

	[[nodiscard]] __forceinline const std::vector<std::unique_ptr<CByteCodeBlock>>& DynamicCombos() const
	{
		return m_DynamicCombos;
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

template <typename... Args>
inline void Construct( StaticComboNodeHash_t** pMemory, const Args&... )
{
	::new ( pMemory ) StaticComboNodeHash_t*( nullptr ); // Explicitly new with NULL
}

using CShaderMap = CUtlStringMap<StaticComboNodeHash_t*>;
static CShaderMap g_ShaderByteCode;

static CStaticCombo* StaticComboFromDictAdd( const char* pszShaderName, uint64 nStaticComboId )
{
	StaticComboNodeHash_t* &rpNodeHash = g_ShaderByteCode[pszShaderName];
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

	[[nodiscard]] const std::string& GetFirstCommand() const { return m_sFirstCommand; }
	[[nodiscard]] int GetNumTimesReported() const { return m_numTimesReported; }

protected:
	std::string m_sFirstCommand;
	int m_numTimesReported;
};

static CUtlStringMap<uint8> g_Master_ShaderHadError;
static CUtlStringMap<uint8> g_Master_ShaderWrittenToDisk;
struct CompilerMsg
{
	CUtlStringMap<CompilerMsgInfo> warning;
	CUtlStringMap<CompilerMsgInfo> error;
};
static CUtlStringMap<CompilerMsg> g_Master_CompilerMsg;

namespace Threading
{
class CSTDMutex : public std::mutex
{
public:
	using std::mutex::mutex;

	FORCEINLINE void Lock()
	{
		lock();
	}

	FORCEINLINE void Unlock()
	{
		unlock();
	}
};

class CThreadNullMutex
{
public:
	static void Lock() {}
	static void Unlock() {}
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

	T* operator=( T* newValue )
	{
		InterlockedExchangePointer( reinterpret_cast<void* volatile*>( &m_value ), newValue );
		return newValue;
	}

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

	int operator=( int newValue )
	{
		InterlockedExchange( &m_value, newValue );
		return m_value;
	}

	void operator+=( int add ) { InterlockedExchangeAdd( &m_value, add ); }
	void operator-=( int subtract ) { operator+=( -subtract ); }
	void operator*=( int multiplier )
	{
		int original, result;
		do
		{
			original = m_value;
			result   = original * multiplier;
		} while ( !AssignIf( original, result ) );
	}
	void operator/=( int divisor )
	{
		int original, result;
		do
		{
			original = m_value;
			result   = original / divisor;
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

	[[nodiscard]] T Get() const
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
	eMultiThreaded  = 1
};

// A special object that makes single-threaded code incur no penalties
// and multithreaded code to be synchronized properly.
template <auto& mtx>
class CSwitchableMutex
{
	using mtx_type = std::decay_t<decltype( mtx )>;
public:
	FORCEINLINE explicit CSwitchableMutex() : m_pUseMtx( nullptr ) {}

	FORCEINLINE void SetThreadedMode( Mode eMode ) { m_pUseMtx = eMode ? &mtx : nullptr; }

	FORCEINLINE void Lock()
	{
		if ( mtx_type* pUseMtx = m_pUseMtx )
			pUseMtx->Lock();
	}

	FORCEINLINE void Unlock()
	{
		if ( mtx_type* pUseMtx = m_pUseMtx )
			pUseMtx->Unlock();
	}

private:
	CInterlockedPtr<mtx_type> m_pUseMtx;
};

namespace Private
{
	using MtMutexType_t = CSTDMutex;
	static MtMutexType_t g_mtxSyncObjMT;
}; // namespace Private

static CSwitchableMutex<Private::g_mtxSyncObjMT> g_mtxGlobal;
}; // namespace Threading

// Access to global data should be synchronized by these global locks
#define GLOBAL_DATA_MTX_LOCK()   Threading::g_mtxGlobal.Lock()
#define GLOBAL_DATA_MTX_UNLOCK() Threading::g_mtxGlobal.Unlock()

static void ErrMsgDispatchMsgLine( const char* szCommand, const char* szMsgLine, const char* szName )
{
	auto& msg = g_Master_CompilerMsg[szName];
	// Now store the message with the command it was generated from
	if ( strstr( szMsgLine, "warning X" ) )
		msg.warning[szMsgLine].SetMsgReportedCommand( szCommand );
	else
		msg.error[szMsgLine].SetMsgReportedCommand( szCommand );
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
	uint8* pCompressedShader = LZMA::Compress( reinterpret_cast<uint8*>( pDynamicComboBuffer.Base() ), pDynamicComboBuffer.TellPut(), &nCompressedSize );
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
	pDynamicComboBuffer.Clear(); // start over
}

static void OutputDynamicCombo( size_t& pnTotalFlushedSize, CUtlBuffer& pDynamicComboBuffer, CUtlBuffer& pBuf, uint64 nComboID, uint32 nComboSize, uint8* pComboCode )
{
	if ( pDynamicComboBuffer.TellPut() + nComboSize + 16 >= MAX_SHADER_UNPACKED_BLOCK_SIZE )
		FlushCombos( pnTotalFlushedSize, pDynamicComboBuffer, pBuf );

	pDynamicComboBuffer.PutUnsignedInt( gsl::narrow<uint32>( nComboID ) );
	pDynamicComboBuffer.PutUnsignedInt( nComboSize );
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
		std::filesystem::create_directories( pszMainOutFileName.data() );
	}

	strcat_s( pszMainOutFileName.data(), pszMainOutFileName.size(), "\\" );
	strcat_s( pszMainOutFileName.data(), pszMainOutFileName.size(), si.m_pShaderName );
	strcat_s( pszMainOutFileName.data(), pszMainOutFileName.size(), ".vcs" ); // Different extensions for main output file

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
	uint32 m_nCRC32; // CRC32 of packed data
	CStaticCombo* m_pByteCode;
};

static bool CompareComboIds( const StaticComboAuxInfo_t& pA, const StaticComboAuxInfo_t& pB )
{
	return pA.m_nStaticComboID < pB.m_nStaticComboID;
}

static void WriteShaderFiles( const char* pShaderName, bool end )
{
	using namespace std;
	if ( !g_Master_ShaderWrittenToDisk.Defined( pShaderName ) )
		g_Master_ShaderWrittenToDisk[pShaderName] = true;
	else
		return;

	const bool bShaderFailed                = g_Master_ShaderHadError.Defined( pShaderName );
	const char* const szShaderFileOperation = bShaderFailed ? "Removing failed" : "Writing";

	static int lastLine               = 0;
	static Clock::time_point lastTime = g_flStartTime;
	++lastLine;

	//
	// Progress indication
	//
	if ( !end )
		std::cout << ( "\033["s + std::to_string( lastLine ) + "B" );
	std::cout << "\r" << szShaderFileOperation << " " << ( bShaderFailed ? clr::red : clr::green ) << pShaderName << clr::reset << "...\r";

	//
	// Retrieve the data we are going to operate on
	// from global variables under lock.
	//
	GLOBAL_DATA_MTX_LOCK();
	StaticComboNodeHash_t* pByteCodeArray;
	{
		StaticComboNodeHash_t*& rp = g_ShaderByteCode[pShaderName]; // Get a static combo pointer, reset it as well
		pByteCodeArray             = rp;
		rp                         = nullptr;
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
		std::cout << "\r" << clr::red << pShaderName << clr::reset << " " << FormatTimeShort( std::chrono::duration_cast<std::chrono::seconds>( Clock::now() - lastTime ).count() ) << "                                        \r";
		std::cout << ( "\033["s + std::to_string( lastLine ) + "A" );
		lastTime = Clock::now();
		return;
	}

	if ( !pByteCodeArray )
		return;

	if ( g_bVerbose )
	{
		std::cout << "\033[B";
		std::cout << std::showbase << pShaderName << " : " << clr::green << shaderInfo.m_nTotalShaderCombos << clr::reset << " combos, centroid mask: " << clr::green << std::hex << shaderInfo.m_CentroidMask << std::dec << clr::reset << ", numDynamicCombos: " << clr::green << shaderInfo.m_nDynamicCombos << clr::reset << ", flags: " << clr::green << std::hex << shaderInfo.m_Flags << std::dec << clr::reset << std::noshowbase << std::endl;
		std::cout << "\033[A";
	}

	//
	// Static combo headers
	//
	std::vector<StaticComboAuxInfo_t> StaticComboHeaders;

	StaticComboHeaders.reserve( 1ULL + pByteCodeArray->Count() ); // we know how much ram we need

	std::vector<int> comboIndicesHashedByCRC32[STATIC_COMBO_HASH_SIZE];
	std::vector<StaticComboAliasRecord_t> duplicateCombos;

	// now, lets fill in our combo headers, sort, and write
	for ( uint32 nChain = 0; nChain < StaticComboNodeHash_t::NumChains; nChain++ )
	{
		for ( CStaticCombo* pStatic = pByteCodeArray->Chain( nChain ).Head(); pStatic; pStatic = pStatic->Next() )
		{
			const CStaticCombo::PackedCode& code = pStatic->Code();
			if ( code.GetLength() )
			{
				StaticComboAuxInfo_t hdr;
				hdr.m_nStaticComboID  = gsl::narrow<uint32>( pStatic->ComboId() );
				hdr.m_nFileOffset     = 0; // fill in later
				hdr.m_nCRC32          = CRC32::ProcessSingleBuffer( code.GetData(), code.GetLength() );
				const uint32 nHashIdx = hdr.m_nCRC32 % STATIC_COMBO_HASH_SIZE;
				hdr.m_pByteCode       = pStatic;
				// now, see if we have an identical static combo
				bool bIsDuplicate = false;
				for ( int i : comboIndicesHashedByCRC32[nHashIdx] )
				{
					const StaticComboAuxInfo_t& check = StaticComboHeaders[i];
					const CStaticCombo::PackedCode& checkCode = check.m_pByteCode->Code();
					if ( check.m_nCRC32 == hdr.m_nCRC32 && checkCode.GetLength() == code.GetLength() && memcmp( checkCode.GetData(), code.GetData(), checkCode.GetLength() ) == 0 )
					{
						// this static combo is the same as another one!!
						duplicateCombos.emplace_back( StaticComboAliasRecord_t { hdr.m_nStaticComboID, check.m_nStaticComboID } );
						bIsDuplicate = true;
						break;
					}
				}

				if ( !bIsDuplicate )
				{
					StaticComboHeaders.emplace_back( hdr );
					comboIndicesHashedByCRC32[nHashIdx].emplace_back( gsl::narrow<int>( StaticComboHeaders.size() - 1 ) );
				}
			}
		}
	}
	// add sentinel key
	StaticComboHeaders.emplace_back( StaticComboAuxInfo_t { { 0xffffffff, 0 }, 0, nullptr } );

	// now, sort. sentinel key will end up at end
	std::sort( StaticComboHeaders.begin(), StaticComboHeaders.end(), CompareComboIds );

	//const unsigned int crc32 = SourceCodeHasher::CalculateCRC( shaderInfo.m_pShaderSrc );

	//
	// Shader file stream buffer
	//
	std::ofstream ShaderFile( szVCSfilename, std::ios::binary | std::ios::trunc ); // Streaming buffer for vcs file (since this can blow memory)

	// ------ Header --------------
	const ShaderHeader_t header {
		SHADER_VCS_VERSION_NUMBER,
		gsl::narrow_cast<int32>( shaderInfo.m_nTotalShaderCombos ), // this is not actually used in vertexshaderdx8.cpp for combo checking
		gsl::narrow<int32>( shaderInfo.m_nDynamicCombos ),          // this is used
		shaderInfo.m_Flags,
		shaderInfo.m_CentroidMask,
		gsl::narrow<uint32>( StaticComboHeaders.size() ),
		g_pShaderCRC //crc32
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
		if ( SRec.m_nStaticComboID != 0xffffffff ) // sentinel key?
		{
			CStaticCombo* pStatic = pByteCodeArray->FindByKey( SRec.m_nStaticComboID );
			Assert( pStatic );

			// Put the packed chunk of code for this static combo
			if ( const auto& code = pStatic->Code() )
				ShaderFile.write( reinterpret_cast<const char*>( code.GetData() ), code.GetLength() );

			constexpr uint32 endMark = 0xffffffff; // end of dynamic combos
			ShaderFile.write( reinterpret_cast<const char*>( &endMark ), sizeof( endMark ) );
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

	if ( !end )
	{
		std::cout << "\r" << clr::green << pShaderName << clr::reset << " " << FormatTimeShort( std::chrono::duration_cast<std::chrono::seconds>( Clock::now() - lastTime ).count() ) << "                                        \r";
		std::cout << ( "\033["s + std::to_string( lastLine ) + "A" );
		lastTime = Clock::now();
	}
}

static DWORD gFlags = 0;

// Assemble a reply package to the master from the compiled bytecode
// return the length of the package.
static size_t AssembleWorkerReplyPackage( const CfgProcessor::CfgEntryInfo* pEntry, uint64 nComboOfEntry, CUtlBuffer& pBuf )
{
	GLOBAL_DATA_MTX_LOCK();
	CStaticCombo* pStComboRec             = StaticComboFromDict( pEntry->m_szName, nComboOfEntry );
	StaticComboNodeHash_t* pByteCodeArray = g_ShaderByteCode[pEntry->m_szName];
	GLOBAL_DATA_MTX_UNLOCK();

	size_t nBytesWritten = 0;

	if ( pStComboRec && !pStComboRec->DynamicCombos().empty() )
	{
		CUtlBuffer ubDynamicComboBuffer;

		pStComboRec->SortDynamicCombos();
		// iterate over all dynamic combos.
		for ( auto& combo : pStComboRec->DynamicCombos() )
		{
			CByteCodeBlock* pCode = combo.get();
			// check if we have already output an identical combo
			OutputDynamicCombo( nBytesWritten, ubDynamicComboBuffer, pBuf, pCode->m_nComboID,
								gsl::narrow<uint32>( pCode->m_nCodeSize ), pCode->m_ByteCode.get() );
		}
		FlushCombos( nBytesWritten, ubDynamicComboBuffer, pBuf );
	}

	// Time to limit amount of prints
	static Clock::time_point s_fLastInfoTime;
	static uint64 s_nLastEntry = nComboOfEntry;
	static CUtlMovingAverage<uint64, 60> s_averageProcess;
	static const char* s_lastShader = pEntry->m_szName;
	const Clock::time_point fCurTime = Clock::now();

	GLOBAL_DATA_MTX_LOCK();
	if ( pStComboRec )
		pByteCodeArray->DeleteByKey( nComboOfEntry );
	if ( std::chrono::duration_cast<std::chrono::seconds>( fCurTime - s_fLastInfoTime ).count() != 0 )
	{
		if ( s_lastShader != pEntry->m_szName )
		{
			s_averageProcess.Reset();
			s_lastShader = pEntry->m_szName;
			s_nLastEntry = nComboOfEntry;
		}

		s_averageProcess.PushValue( s_nLastEntry - nComboOfEntry );
		s_nLastEntry = nComboOfEntry;
		std::cout << "\rCompiling " << ( g_Master_ShaderHadError.Defined( pEntry->m_szName ) ? clr::red : clr::green ) << pEntry->m_szName << clr::reset << " [ " << clr::blue << PrettyPrint( nComboOfEntry ) << clr::reset << " remaining ("
				  << clr::green2 << s_averageProcess.GetAverage() << clr::reset << " c/m) ] " << FormatTimeShort( std::chrono::duration_cast<std::chrono::seconds>( fCurTime - g_flStartTime ).count() ) << " elapsed         \r";
		s_fLastInfoTime = fCurTime;
	}
	GLOBAL_DATA_MTX_UNLOCK();

	return nBytesWritten;
}

template <typename TMutexType>
class CWorkerAccumState
{
public:
	explicit CWorkerAccumState( TMutexType* pMutex )
		: m_pMutex( pMutex ), m_iFirstCommand( 0 )
		, m_iNextCommand( 0 ), m_iEndCommand( 0 )
		, m_iLastFinished( 0 ), m_hCombo( nullptr ) {}

	void RangeBegin( uint64 iFirstCommand, uint64 iEndCommand );
	void RangeFinished();

	void ExecuteCompileCommand( CfgProcessor::ComboHandle hCombo );
	void ExecuteCompileCommandThreaded( CfgProcessor::ComboHandle hCombo );
	void HandleCommandResponse( CfgProcessor::ComboHandle hCombo, CmdSink::IResponse* pResponse );

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

	void QuitSubs();
	void OnProcessST();

	void Stop()
	{
		m_bBreak = true;
	}

protected:
	Threading::CInterlockedInt m_bBreak;
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
		SubProcessKernelObjects* pCommObjs;
		std::future<int> pThread;
	};
	Threading::CThreadLocal<SubProcess*> m_lpSubProcessInfo;
	std::vector<SubProcess*> m_arrSubProcessInfos;
	uint64 m_iFirstCommand;
	uint64 m_iNextCommand;
	uint64 m_iEndCommand;

	uint64 m_iLastFinished;

	CfgProcessor::ComboHandle m_hCombo;

	bool OnProcess();
	void TryToPackageData( uint64 iCommandNumber );
	void PrepareSubProcess( SubProcess** ppSp, SubProcessKernelObjects** ppCommObjs );
};

template <typename TMutexType>
void CWorkerAccumState<TMutexType>::RangeBegin( uint64 iFirstCommand, uint64 iEndCommand )
{
	m_iFirstCommand = iFirstCommand;
	m_iNextCommand  = iFirstCommand;
	m_iEndCommand   = iEndCommand;
	m_iLastFinished = iFirstCommand;
	m_hCombo        = nullptr;
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
	using namespace std::chrono_literals;
	std::vector<HANDLE> m_arrWait;
	m_arrWait.reserve( m_arrSubProcessInfos.size() );
	std::vector<std::reference_wrapper<const std::future<int>>> m_arrWait2;
	m_arrWait2.reserve( m_arrSubProcessInfos.size() );

	for ( SubProcess* pSp : m_arrSubProcessInfos )
	{
		if ( !( pSp->pThread.valid() && pSp->pThread.wait_for( 0s ) == std::future_status::timeout ) )
			continue;
		SubProcessKernelObjects_Memory shrmem( pSp->pCommObjs );
		if ( void* pvMemory = shrmem.Lock() )
		{
			strcpy_s( static_cast<char*>( pvMemory ), 10, "quit" );
			shrmem.Unlock();
		}

		if ( pSp->pThread.valid() )
			m_arrWait2.emplace_back( pSp->pThread );
	}

	if ( !m_arrWait.empty() )
	{
		const DWORD dwWait = WaitForMultipleObjects( gsl::narrow<DWORD>( m_arrWait.size() ), m_arrWait.data(), TRUE, 2 * 1000 );
		if ( WAIT_TIMEOUT == dwWait )
			std::cout << clr::pinkish << "Timed out while waiting for sub-processes to shut down!" << clr::reset << std::endl;
	}

	if ( !m_arrWait2.empty() )
		std::for_each( m_arrWait2.begin(), m_arrWait2.end(), []( const std::future<int>& t ) { t.wait(); } );

	for ( SubProcess* pSp : m_arrSubProcessInfos )
	{
		delete pSp->pCommObjs;
		delete pSp;
	}
	m_arrSubProcessInfos.clear();
}

template <size_t N>
static __forceinline void PrepareFlagsForSubprocess( char ( &pBuf )[N] )
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

		char chBaseNameBuffer[0x30];
		sprintf_s( chBaseNameBuffer, "SHCMPL_SUB_%08lX_%08llX_%08lX", pSp->dwSvcThreadId, time( nullptr ), GetCurrentProcessId() );
		pCommObjs = pSp->pCommObjs = new SubProcessKernelObjects_Create( chBaseNameBuffer );

		pSp->pThread = std::async( std::launch::async, ShaderCompile_Subprocess_Main, std::string( chBaseNameBuffer ), gFlags );

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
	const uint64 iComboIndex                     = Combo_GetComboNum( hCombo );
	const uint64 iCommandNumber                  = Combo_GetCommandNum( hCombo );

	if ( pResponse->Succeeded() )
	{
		GLOBAL_DATA_MTX_LOCK();
		const uint64 nStComboIdx = iComboIndex / pEntryInfo->m_numDynamicCombos;
		const uint64 nDyComboIdx = iComboIndex - ( nStComboIdx * pEntryInfo->m_numDynamicCombos );
		StaticComboFromDictAdd( pEntryInfo->m_szName, nStComboIdx )->AddDynamicCombo( nDyComboIdx, pResponse->GetResultBuffer(), pResponse->GetResultBufferLen() );
		GLOBAL_DATA_MTX_UNLOCK();
	}
	else // Tell the master that this shader failed
	{
		GLOBAL_DATA_MTX_LOCK();
		ShaderHadErrorDispatchInt( pEntryInfo->m_szName );
		GLOBAL_DATA_MTX_UNLOCK();
	}

	// Process listing even if the shader succeeds for warnings
	const char* szListing = pResponse->GetListing();
	if ( szListing || !pResponse->Succeeded() )
	{
		char chUnreportedListing[0xFF];
		if ( !szListing )
		{
			sprintf_s( chUnreportedListing, "(%s): error 0000: Compiler failed without error description. Command number %llu", pEntryInfo->m_szShaderFileName, iCommandNumber );
			szListing = chUnreportedListing;
		}

		char chBuffer[4096];
		Combo_FormatCommandHumanReadable( hCombo, chBuffer );

		GLOBAL_DATA_MTX_LOCK();
		ErrMsgDispatchMsgLine( chBuffer, szListing, pEntryInfo->m_szName );
		GLOBAL_DATA_MTX_UNLOCK();
	}

	pResponse->Release();

	// Maybe zip things up
	TryToPackageData( iCommandNumber );
}

template <typename TMutexType>
void CWorkerAccumState<TMutexType>::TryToPackageData( uint64 iCommandNumber )
{
	if ( m_bBreak )
		return;
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
	CfgProcessor::ComboHandle hChEnd   = CfgProcessor::Combo_GetCombo( iFinishedByNow );

	Assert( hChBegin && hChEnd );

	const CfgProcessor::CfgEntryInfo* pInfoBegin = Combo_GetEntryInfo( hChBegin );
	const CfgProcessor::CfgEntryInfo* pInfoEnd   = Combo_GetEntryInfo( hChEnd );

	uint64 nComboBegin     = Combo_GetComboNum( hChBegin ) / pInfoBegin->m_numDynamicCombos;
	const uint64 nComboEnd = Combo_GetComboNum( hChEnd ) / pInfoEnd->m_numDynamicCombos;

	for ( ; pInfoBegin && ( pInfoBegin->m_iCommandStart < pInfoEnd->m_iCommandStart || nComboBegin > nComboEnd ); )
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
				pInfoBegin  = Combo_GetEntryInfo( hChBegin );
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

	uint64 iThreadCommand = ~0ULL;

	SubProcess* pSp = nullptr;
	PrepareSubProcess( &pSp, nullptr );

	for ( ;; )
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
			iThreadCommand       = ~0ULL;
			pSp->iRunningCommand = ~0ULL;
		}
		m_pMutex->Unlock();

		if ( hThreadCombo && !m_bBreak )
			ExecuteCompileCommandThreaded( hThreadCombo );
		else
			break;
	}

	Combo_Free( hThreadCombo );
	return false;
}

template <typename TMutexType>
void CWorkerAccumState<TMutexType>::OnProcessST()
{
	while ( m_hCombo && !m_bBreak )
	{
		ExecuteCompileCommand( m_hCombo );

		Combo_GetNext( m_iNextCommand, m_hCombo, m_iEndCommand );
	}
}

//
// ProcessCommandRange_Singleton
//
class ProcessCommandRange_Singleton
{
public:
	static ProcessCommandRange_Singleton*& Instance()
	{
		static ProcessCommandRange_Singleton* s_ptr = nullptr;
		return s_ptr;
	}

public:
	ProcessCommandRange_Singleton()
	{
		Assert( !Instance() );
		Instance() = this;
		Startup();
	}
	~ProcessCommandRange_Singleton()
	{
		Assert( Instance() == this );
		Instance() = nullptr;
		Shutdown();
	}

public:
	void ProcessCommandRange( uint64 shaderStart, uint64 shaderEnd );

	void Stop();
	bool Stoped() const { return m_bStopped; }

protected:
	void Startup();
	void Shutdown();

	//
	// Multi-threaded section
	//
	struct MT
	{
		MT() : pWorkerObj( nullptr ) {}

		using MultiThreadMutex_t = Threading::CSTDMutex;
		MultiThreadMutex_t mtx;

		using WorkerClass_t = CWorkerAccumState<MultiThreadMutex_t>;
		WorkerClass_t* pWorkerObj;
	} m_MT;

	//
	// Single-threaded section
	//
	struct ST
	{
		ST() : pWorkerObj( nullptr ) {}

		using SingleThreadMutex_t = Threading::CThreadNullMutex;
		SingleThreadMutex_t mtx;

		using WorkerClass_t = CWorkerAccumState<SingleThreadMutex_t>;
		WorkerClass_t* pWorkerObj;
	} m_ST;

	bool m_bStopped = false;
};

void ProcessCommandRange_Singleton::Startup()
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

void ProcessCommandRange_Singleton::Shutdown()
{
	if ( m_MT.pWorkerObj )
		delete m_MT.pWorkerObj;
	else if ( m_ST.pWorkerObj )
		delete m_ST.pWorkerObj;
}

void ProcessCommandRange_Singleton::Stop()
{
	m_bStopped = true;
	if ( m_MT.pWorkerObj )
		m_MT.pWorkerObj->Stop();
	else
		m_ST.pWorkerObj->Stop();
}


void ProcessCommandRange_Singleton::ProcessCommandRange( uint64 shaderStart, uint64 shaderEnd )
{
	if ( m_MT.pWorkerObj )
	{
		MT::WorkerClass_t* pWorkerObj = m_MT.pWorkerObj;

		pWorkerObj->RangeBegin( shaderStart, shaderEnd );
		unsigned long threads;
		cmdLine.get( "-threads" )->getULong( threads );
		pWorkerObj->Run( threads );
		pWorkerObj->RangeFinished();
		pWorkerObj->QuitSubs();
	}
	else
	{
		ST::WorkerClass_t* pWorkerObj = m_ST.pWorkerObj;

		pWorkerObj->RangeBegin( shaderStart, shaderEnd );
		pWorkerObj->OnProcessST();
		pWorkerObj->RangeFinished();
	}
}

static void Shader_ParseShaderInfoFromCompileCommands( const CfgProcessor::CfgEntryInfo* pEntry, ShaderInfo_t& shaderInfo )
{
	if ( CfgProcessor::ComboHandle hCombo = CfgProcessor::Combo_GetCombo( pEntry->m_iCommandStart ) )
	{
		CfgProcessor::CfgEntryInfo const* info = Combo_GetEntryInfo( hCombo );

		memset( &shaderInfo, 0, sizeof( ShaderInfo_t ) );

		strcpy_s( shaderInfo.m_szShaderModel, info->m_szShaderVersion );
		shaderInfo.m_CentroidMask       = info->m_nCentroidMask;
		shaderInfo.m_Flags              = 0; // not filled out by anything?
		shaderInfo.m_nShaderCombo       = 0;
		shaderInfo.m_nTotalShaderCombos = pEntry->m_numCombos;
		shaderInfo.m_nDynamicCombos     = pEntry->m_numDynamicCombos;
		shaderInfo.m_nStaticCombo       = 0;

		shaderInfo.m_pShaderName = pEntry->m_szName;
		shaderInfo.m_pShaderSrc  = pEntry->m_szShaderFileName;

		Combo_Free( hCombo );
	}
}

namespace ConfigurationProcessing
{
	extern void SetupConfigurationDirect( const std::string& name, const std::string& version, uint32_t centroidMask,
		const std::vector<Parser::Combo>& static_c, const std::vector<Parser::Combo>& dynamic_c,
		const std::vector<std::string>& skip, const std::vector<std::string>& includes );
}

static void Shared_ParseListOfCompileCommands()
{
	using namespace std::literals;
	const Clock::time_point tt_start = Clock::now();

	/*char fileListFileName[1024];
	if ( V_IsAbsolutePath( g_pShaderConfigFile.c_str() ) )
		strcpy_s( fileListFileName, g_pShaderConfigFile.c_str() );
	else
		sprintf_s( fileListFileName, "%s\\%s", g_pShaderPath.c_str(), g_pShaderConfigFile.c_str() );

	if ( !( _access( fileListFileName, 0 ) != -1 ) )
	{
		std::cout << clr::pinkish << "Can't open \"" << clr::red << fileListFileName << clr::pinkish << "\"!" << clr::reset << std::endl;
		exit( -1 );
	}

	CfgProcessor::ReadConfiguration( fileListFileName );*/

	const std::string name = Parser::ConstructName( std::filesystem::path( *cmdLine.lastArgs[0] ).filename().string(), g_pShaderVersion );
	if ( Parser::CheckCrc( ( std::filesystem::path( g_pShaderPath ) / *cmdLine.lastArgs[0] ).string(), name, g_pShaderCRC ) && !cmdLine.isSet( "-force" ) )
	{
		exit( 0 );
	}

	std::vector<Parser::Combo> static_c, dynamic_c;
	std::vector<std::string> skip;
	uint32_t centroid_mask = 0;
	std::vector<std::string> includes;

	if ( !Parser::ParseFile( ( std::filesystem::path( g_pShaderPath ) / *cmdLine.lastArgs[0] ).string(), g_pShaderVersion, static_c, dynamic_c, skip, centroid_mask, includes ) )
	{
		std::cout << clr::red << "Failed to parse " << *cmdLine.lastArgs[0] << clr::reset << std::endl;
		exit( -1 );
	}
	Parser::WriteInclude( ( std::filesystem::path( g_pShaderPath ) / "include"sv / ( name + ".inc" ) ).string(), name, static_c, dynamic_c, skip );
	ConfigurationProcessing::SetupConfigurationDirect( name, g_pShaderVersion, centroid_mask, static_c, dynamic_c, skip, includes );

	CfgProcessor::DescribeConfiguration( g_arrCompileEntries );

	for ( const CfgProcessor::CfgEntryInfo* pInfo = g_arrCompileEntries.get(); pInfo && pInfo->m_szName; ++pInfo )
	{
		++g_numShaders;
		g_numStaticCombos += pInfo->m_numStaticCombos;
		g_numCompileCommands = pInfo->m_iCommandEnd;
	}

	const Clock::time_point tt_end = Clock::now();

	std::cout << "\rCompiling " << clr::green << PrettyPrint( g_numCompileCommands ) << clr::reset << " commands, setup took " << clr::green << std::chrono::duration_cast<std::chrono::seconds>( tt_end - tt_start ).count() << clr::reset << " seconds.         \r";
}

static void CompileShaders()
{
	ProcessCommandRange_Singleton pcr;

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
		pcr.ProcessCommandRange( pEntry->m_iCommandStart, pEntry->m_iCommandEnd );

		if ( pcr.Stoped() )
			break;

		//
		// Now when the whole shader is finished we can write it
		//
		const char* szShaderToWrite = pEntry->m_szName;
		WriteShaderFiles( szShaderToWrite, false );
	}

	std::cout << "\r                                                                                           \r";
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
				 pTime.tm_year + 1900, /* Year less 2000 */
				 pTime.tm_mon + 1,     /* month (0 - 11 : 0 = January) */
				 pTime.tm_mday,        /* day of month (1 - 31) */
				 pTime.tm_hour,        /* hour (0 - 23) */
				 pTime.tm_min,         /* minutes (0 - 59) */
				 pTime.tm_sec,         /* seconds (0 - 59) */
				 0                     // ensures the filename is unique
	);

	BOOL bMinidumpResult = FALSE;
	const HANDLE hFile   = ::CreateFile( rgchFileName, GENERIC_WRITE, FILE_SHARE_WRITE, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr );

	if ( hFile )
	{
		// dump the exception information into the file
		_MINIDUMP_EXCEPTION_INFORMATION ExInfo;
		ExInfo.ThreadId          = GetCurrentThreadId();
		ExInfo.ExceptionPointers = pExceptionInfo;
		ExInfo.ClientPointers    = FALSE;

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
	constexpr const int iType = MiniDumpNormal | MiniDumpWithDataSegs | MiniDumpWithIndirectlyReferencedMemory | MiniDumpWithThreadInfo;

	WriteMiniDumpUsingExceptionInfo( ExceptionInfo, static_cast<MINIDUMP_TYPE>( iType ) );
	return EXCEPTION_CONTINUE_SEARCH;
}

static void PrintCompileErrors()
{
	// Write all the errors
	//////////////////////////////////////////////////////////////////////////
	//
	// Now deliver all our accumulated spew to the output
	//
	//////////////////////////////////////////////////////////////////////////

	if ( const int numShaderMsgs = g_Master_CompilerMsg.GetNumStrings() )
	{
		int totalWarnings = 0, totalErrors = 0;
		g_Master_CompilerMsg.ForEach( [&totalWarnings, &totalErrors]( const CompilerMsg& msg ) {
			totalWarnings += msg.warning.GetNumStrings();
			totalErrors += msg.error.GetNumStrings();
		} );
		std::cout << clr::yellow << "WARNINGS" << clr::reset << "/" << clr::red << "ERRORS " << clr::reset << totalWarnings << "/" << totalErrors << std::endl;

		const auto& trim = []( const char* str ) -> std::string {
			std::string s( str );
			s.erase( std::find_if( s.rbegin(), s.rend(), []( int ch ) { return !std::isspace( ch ); } ).base(), s.end() );
			return s;
		};

		char cwd[_MAX_PATH];
		_getcwd( cwd, sizeof( cwd ) );
		const size_t cwdLen = strlen( cwd ) + 1;

		for ( int i = 0; i < numShaderMsgs; i++ )
		{
			const auto& msg              = g_Master_CompilerMsg[gsl::narrow<UtlSymId_t>( i )];
			const char* const shaderName = g_Master_CompilerMsg.String( i );

			if ( const int warnings = msg.warning.GetNumStrings() )
				std::cout << shaderName << " " << clr::yellow << warnings << " WARNING(S):                                                         " << clr::reset << std::endl;

			for ( int k = 0, kEnd = msg.warning.GetNumStrings(); k < kEnd; ++k )
			{
				const char* const szMsg    = msg.warning.String( k );
				const CompilerMsgInfo& cmi = msg.warning[gsl::narrow<UtlSymId_t>( k )];
				const int numReported      = cmi.GetNumTimesReported();

				std::string m     = trim( szMsg );
				const size_t find = m.find( cwd );
				std::cout << std::quoted( find != std::string::npos ? m.replace( find, cwdLen, "" ) : m ) << "\nReported " << clr::green << numReported << clr::reset << " time(s)" << std::endl;
			}

			if ( const int errors = msg.error.GetNumStrings() )
				std::cout << shaderName << " " << clr::red << errors << " ERROR(S):                                                               " << clr::reset << std::endl;

			// Compiler spew
			for ( int k = 0, kEnd = msg.error.GetNumStrings(); k < kEnd; ++k )
			{
				const char* const szMsg          = msg.error.String( k );
				const CompilerMsgInfo& cmi       = msg.error[gsl::narrow<UtlSymId_t>( k )];
				const std::string& cmd           = cmi.GetFirstCommand();
				const int numReported            = cmi.GetNumTimesReported();

				std::string m     = trim( szMsg );
				const size_t find = m.find( cwd );
				std::cout << std::quoted( find != std::string::npos ? m.replace( find, cwdLen, "" ) : m ) << "\nReported " << clr::green << numReported << clr::reset << " time(s), example command: " << std::endl;

				std::cout << "    " << clr::green << cmd << " " << shaderName << ".fxc" << clr::reset << std::endl;
			}
		}
	}

	// Failed shaders summary
	for ( int k = 0, kEnd = g_Master_ShaderHadError.GetNumStrings(); k < kEnd; ++k )
	{
		const char* szShaderName = g_Master_ShaderHadError.String( k );
		if ( !g_Master_ShaderHadError[gsl::narrow<UtlSymId_t>( k )] )
			continue;

		std::cout << clr::pinkish << "FAILED: " << clr::red << szShaderName << clr::reset << std::endl;
	}
}

static bool s_write = true;
static BOOL WINAPI CtrlHandler( DWORD signal )
{
	if ( signal == CTRL_C_EVENT )
	{
		s_write = false;
		if ( auto inst = ProcessCommandRange_Singleton::Instance() )
			inst->Stop();
		PrintCompileErrors();
	}

	return FALSE;
}

static void WriteShaders()
{
	// Write everything that succeeded
	if ( s_write )
	{
		const int nStrings = g_ShaderByteCode.GetNumStrings();
		for ( int i = 0; i < nStrings; i++ )
			WriteShaderFiles( g_ShaderByteCode.String( i ), true );
		PrintCompileErrors();
	}

	//
	// End
	//
	const Clock::time_point end = Clock::now();

	std::cout << clr::green << FormatTime( std::chrono::duration_cast<std::chrono::seconds>( end - g_flStartTime ).count() ) << clr::reset << " elapsed                                           " << std::endl;
}

namespace PreprocessorDbg
{
	extern bool s_bNoOutput;
}

int main( int argc, const char* argv[] )
{
	{
		const HANDLE console = GetStdHandle( STD_OUTPUT_HANDLE );
		DWORD mode;
		GetConsoleMode( console, &mode );
		SetConsoleMode( console, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING );
		SetConsoleCtrlHandler( CtrlHandler, true );
	}

	cmdLine.overview = "Source shader compiler.";
	cmdLine.syntax   = "ShaderCompile [OPTIONS] file.fxc";
	cmdLine.add( "", true, 1, 0, "Sets shader version", "-ver", "/ver" );
	cmdLine.add( "", true, 1, 0, "Base path for shaders", "-shaderpath", "/shaderpath" );
	cmdLine.add( "", false, 0, 0, "Skip crc check during compilation", "-force", "/force" );
	cmdLine.add( "", false, 0, 0, "Calculate crc for shader", "-crc", "/crc" );
	cmdLine.add( "", false, 0, 0, "Generate only header", "-dynamic", "/dynamic" );
	cmdLine.add( "0", false, 1, 0, "Number of threads used, defaults to core count", "-threads", "/threads" );
	cmdLine.add( "", false, 0, 0, "Shows help", "-help", "-h", "/help", "/h" );

	cmdLine.add( "", false, 0, 0, "Verbose file cache and final shader info", "-verbose", "/verbose" );
	cmdLine.add( "", false, 0, 0, "Verbose compile commands", "-verbose2", "/verbose2" );
	cmdLine.add( "", false, 0, 0, "Enables preprocessor debug printing", "-verbose_preprocessor" );

	cmdLine.add( "", false, 0, 0, "Compiles shader with partial precission", "/Gpp", "-partial-precision" );
	cmdLine.add( "", false, 0, 0, "Skips shader validation", "/Vd", "-no-validation" );
	cmdLine.add( "", false, 0, 0, "Disables preshader generation", "/Op", "-disable-preshader" );
	cmdLine.add( "", false, 0, 0, "Directs the compiler to not use flow-control constructs where possible", "/Gfa", "-no-flow-control" );
	cmdLine.add( "", false, 0, 0, "Directs the compiler to use flow-control constructs where possible", "/Gfp", "-prefer-flow-control" );
	cmdLine.add( "", false, 0, 0, "Disables shader optimization", "/Od", "-disable-optimization" );

	cmdLine.parse( argc, argv );

	if ( cmdLine.isSet( "-help" ) )
	{
		CONSOLE_SCREEN_BUFFER_INFO csbi;
		GetConsoleScreenBufferInfo( GetStdHandle( STD_OUTPUT_HANDLE ), &csbi );
		std::string usage;
		cmdLine.getUsageDescriptions( usage, csbi.srWindow.Right - csbi.srWindow.Left + 1, ez::ezOptionParser::ALIGN );
		std::cout << cmdLine.overview << "\n\n"
				  << "Usage: " << cmdLine.syntax << "\n\n"
				  << clr::green << clr::bold << "OPTIONS:\n"
				  << clr::reset << usage << std::endl;
		return 0;
	}

	if ( cmdLine.isSet( "-verbose_preprocessor" ) )
		PreprocessorDbg::s_bNoOutput = false;

	g_flStartTime = Clock::now();

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

	std::vector<std::string> badOptions;
	if ( !cmdLine.gotRequired( badOptions ) || cmdLine.lastArgs.size() != 1 )
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

	cmdLine.get( "-ver" )->getString( g_pShaderVersion );
	if ( !Parser::ValidateVersion( g_pShaderVersion ) )
	{
		std::cout << clr::red << "Shader uses unknown shader version: " << clr::pinkish << g_pShaderVersion << clr::reset << std::endl;
		return -1;
	}

	cmdLine.get( "-shaderpath" )->getString( g_pShaderPath );
	if ( cmdLine.isSet( "-crc" ) )
	{
		const std::string name = Parser::ConstructName( std::filesystem::path( *cmdLine.lastArgs[0] ).filename().string(), g_pShaderVersion );
		uint32_t crc = 0;
		Parser::CheckCrc( ( std::filesystem::path( g_pShaderPath ) / *cmdLine.lastArgs[0] ).string(), name, crc );
		std::cout << crc << std::endl;
		return 0;
	}

	if ( cmdLine.isSet( "-dynamic" ) )
	{
		using namespace std::literals;
		std::vector<Parser::Combo> static_c, dynamic_c;
		std::vector<std::string> skip;
		uint32_t centroid_mask = 0;
		std::vector<std::string> includes;

		if ( !Parser::ParseFile( ( std::filesystem::path( g_pShaderPath ) / *cmdLine.lastArgs[0] ).string(), g_pShaderVersion, static_c, dynamic_c, skip, centroid_mask, includes ) )
		{
			std::cout << clr::red << "Failed to parse " << *cmdLine.lastArgs[0] << clr::reset << std::endl;
			return -1;
		}
		const std::string name = Parser::ConstructName( std::filesystem::path( *cmdLine.lastArgs[0] ).filename().string(), g_pShaderVersion );
		Parser::WriteInclude( ( std::filesystem::path( g_pShaderPath ) / "include"sv / ( name + ".inc" ) ).string(), name, static_c, dynamic_c, skip );
		return 0;
	}

	g_bVerbose = cmdLine.isSet( "-verbose" );
	g_bVerbose2 = cmdLine.isSet( "-verbose2" );

	// Setting up the minidump handlers
	SetUnhandledExceptionFilter( ToolsExceptionFilter );
	Shared_ParseListOfCompileCommands();

	std::cout << "\rCompiling " << clr::green << PrettyPrint( g_numCompileCommands ) << clr::reset << " commands in " << clr::green << PrettyPrint( g_numStaticCombos ) << clr::reset << " static combos.                      \r";
	CompileShaders();

	WriteShaders();

	return g_Master_ShaderHadError.GetNumStrings();
}