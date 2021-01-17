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
#define NOMINMAX

#include <windows.h>

#include "DbgHelp.h"
#include "d3dcompiler.h"
#include <atomic>
#include <concepts>
#include <chrono>
#include <cstdlib>
#include <future>
#include <filesystem>
#include <iomanip>
#include <regex>
#include <set>
#include <thread>

#include "basetypes.h"
#include "cfgprocessor.h"
#include "cmdsink.h"
#include "d3dxfxc.h"
#include "shader_vcs_version.h"
#include "utlbuffer.h"
#include "utlnodehash.h"

#include "ezOptionParser.hpp"
#include "termcolor/style.hpp"
#include "gsl/gsl_narrow"
#include "robin_hood.h"

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

#pragma comment( lib, "DbgHelp" )

// Type conversions should be controlled by programmer explicitly - shadercompile makes use of 64-bit integer arithmetics
#pragma warning( error : 4244 )

namespace fs = std::filesystem;
namespace chrono = std::chrono;
using std::chrono::duration_cast;

using Clock = chrono::high_resolution_clock;
static fs::path g_pShaderPath;
static std::string g_pShaderVersion;
static Clock::time_point g_flStartTime;
static bool g_bVerbose	= false;
static bool g_bVerbose2 = false;
static bool g_bFastFail = false;

struct ShaderInfo_t
{
	ShaderInfo_t() { memset( this, 0, sizeof( *this ) ); }

	uint64_t m_nShaderCombo;
	uint64_t m_nTotalShaderCombos;
	std::string_view m_pShaderName;
	std::string_view m_pShaderSrc;
	unsigned m_CentroidMask;
	uint64_t m_nDynamicCombos;
	uint64_t m_nStaticCombo;
	uint32_t m_Crc32;
};
static robin_hood::unordered_node_map<std::string_view, ShaderInfo_t> g_ShaderToShaderInfo;

static void Shader_ParseShaderInfoFromCompileCommands( const CfgProcessor::CfgEntryInfo* pEntry, ShaderInfo_t& shaderInfo );

struct CByteCodeBlock : private std::unique_ptr<uint8_t[]>
{
	uint64_t m_nComboID;
	size_t m_nCodeSize;

	CByteCodeBlock( const void* pByteCode, size_t nCodeSize, uint64_t nComboID ) : std::unique_ptr<uint8_t[]>( new uint8_t[nCodeSize] )
	{
		m_nComboID  = nComboID;
		m_nCodeSize = nCodeSize;
		memcpy( get(), pByteCode, nCodeSize );
	}

	using std::unique_ptr<uint8_t[]>::get;
};

struct CStaticCombo // all the data for one static combo
{
	struct PackedCode : private std::unique_ptr<uint8_t[]>
	{
		[[nodiscard]] size_t GetLength() const
		{
			if ( uint8_t* pb = get() )
				return *reinterpret_cast<size_t*>( pb );
			return 0;
		}

		[[nodiscard]] uint8_t* GetData() const
		{
			if ( uint8_t* pb = get() )
				return pb + sizeof( size_t );
			return nullptr;
		}

		[[nodiscard]] uint8_t* AllocData( size_t len )
		{
			reset();
			if ( len )
			{
				reset( new uint8_t[len + sizeof( size_t )] );
				*reinterpret_cast<size_t*>( get() ) = len;
			}
			return GetData();
		}

		using std::unique_ptr<uint8_t[]>::operator bool;
	};
	CStaticCombo *m_pNext, *m_pPrev;
private:
	uint64_t m_nStaticComboID;

	std::vector<std::unique_ptr<CByteCodeBlock>> m_DynamicCombos;

	PackedCode m_abPackedCode; // Packed code for entire static combo

	static bool CompareDynamicComboIDs( const std::unique_ptr<CByteCodeBlock>& pA, const std::unique_ptr<CByteCodeBlock>& pB )
	{
		return pA->m_nComboID < pB->m_nComboID;
	}

public:
	[[nodiscard]] uint64_t Key() const
	{
		return m_nStaticComboID;
	}

	[[nodiscard]] uint64_t ComboId() const
	{
		return m_nStaticComboID;
	}

	[[nodiscard]] CStaticCombo* Next() const
	{
		return m_pNext;
	}

	[[nodiscard]] const PackedCode& Code() const
	{
		return m_abPackedCode;
	}

	[[nodiscard]] const std::vector<std::unique_ptr<CByteCodeBlock>>& DynamicCombos() const
	{
		return m_DynamicCombos;
	}

	CStaticCombo( uint64_t nComboID )
	{
		m_nStaticComboID = nComboID;
		m_pNext = nullptr;
		m_pPrev = nullptr;
	}

	~CStaticCombo() = default;

	void AddDynamicCombo( uint64_t nComboID, const void* pComboData, size_t nCodeSize )
	{
		m_DynamicCombos.emplace_back( std::make_unique<CByteCodeBlock>( pComboData, nCodeSize, nComboID ) );
	}

	void SortDynamicCombos()
	{
		std::sort( m_DynamicCombos.begin(), m_DynamicCombos.end(), CompareDynamicComboIDs );
	}

	[[nodiscard]] uint8_t* AllocPackedCodeBlock( size_t nPackedCodeSize )
	{
		return m_abPackedCode.AllocData( nPackedCodeSize );
	}
};

using StaticComboNodeHash_t = CUtlNodeHash<CStaticCombo, 7097, uint64_t>;
using CShaderMap = robin_hood::unordered_map<std::string_view, StaticComboNodeHash_t*>;
static CShaderMap g_ShaderByteCode;

static CStaticCombo* StaticComboFromDictAdd( std::string_view pszShaderName, uint64_t nStaticComboId )
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

static CStaticCombo* StaticComboFromDict( std::string_view pszShaderName, uint64_t nStaticComboId )
{
	if ( StaticComboNodeHash_t* pNodeHash = g_ShaderByteCode[pszShaderName] )
		return pNodeHash->FindByKey( nStaticComboId );
	return nullptr;
}

class CompilerMsgInfo
{
public:
	CompilerMsgInfo() : m_numTimesReported( 0 ) {}

	void SetMsgReportedCommand( const std::string& szCommand )
	{
		if ( !m_numTimesReported )
			m_sFirstCommand = szCommand;
		++m_numTimesReported;
	}

	[[nodiscard]] const std::string& GetFirstCommand() const { return m_sFirstCommand; }
	[[nodiscard]] uint64_t GetNumTimesReported() const { return m_numTimesReported; }

protected:
	std::string m_sFirstCommand;
	uint64_t m_numTimesReported;
};

static robin_hood::unordered_flat_set<std::string_view> g_ShaderHadError;
static robin_hood::unordered_flat_set<std::string_view> g_ShaderWrittenToDisk;
struct CompilerMsg
{
	robin_hood::unordered_node_map<std::string, CompilerMsgInfo> warning;
	robin_hood::unordered_node_map<std::string, CompilerMsgInfo> error;
};
static robin_hood::unordered_node_map<std::string_view, CompilerMsg> g_CompilerMsg;

namespace Threading
{
class null_mutex
{
public:
	void lock() noexcept {}
	void unlock() noexcept {}
};

template <class T>
concept Mutex = requires( T a )
{
	{ a.lock() } -> std::convertible_to<void>;
	{ a.unlock() } -> std::convertible_to<void>;
};

enum Mode
{
	eSingleThreaded = 0,
	eMultiThreaded  = 1
};

// A special object that makes single-threaded code incur no penalties
// and multithreaded code to be synchronized properly.
template <auto& mtx>
	requires Mutex<std::decay_t<decltype( mtx )>>
class CSwitchableMutex
{
	using mtx_type = std::decay_t<decltype( mtx )>;
public:
	FORCEINLINE explicit CSwitchableMutex() noexcept : m_pUseMtx( nullptr ) {}

	FORCEINLINE void SetThreadedMode( Mode eMode ) noexcept { m_pUseMtx = eMode ? &mtx : nullptr; }

	FORCEINLINE void Lock()
	{
		if ( mtx_type* pUseMtx = m_pUseMtx )
			pUseMtx->lock();
	}

	FORCEINLINE void Unlock()
	{
		if ( mtx_type* pUseMtx = m_pUseMtx )
			pUseMtx->unlock();
	}

private:
	std::atomic<mtx_type*> m_pUseMtx;
};

namespace Private
{
	static std::mutex g_mtxSyncObjMT;
	static std::mutex g_mtxSyncObjMT2;
}; // namespace Private

static CSwitchableMutex<Private::g_mtxSyncObjMT> g_mtxGlobal;
static CSwitchableMutex<Private::g_mtxSyncObjMT2> g_mtxMsgReport;
}; // namespace Threading

// Access to global data should be synchronized by these global locks
#define GLOBAL_DATA_MTX_LOCK()   Threading::g_mtxGlobal.Lock()
#define GLOBAL_DATA_MTX_UNLOCK() Threading::g_mtxGlobal.Unlock()

static void ErrMsgDispatchMsgLine( const char* szCommand, const char* szMsgLine, std::string_view szName )
{
	Threading::g_mtxMsgReport.Lock();
	auto& msg = g_CompilerMsg[szName];
	char* dupMsg = _strdup( szMsgLine );
	char *start = dupMsg, *end = dupMsg + strlen( dupMsg );
	char* start2 = start;

	// Now store the message with the command it was generated from
	for ( ; start2 < end && ( start = strchr( start2, '\n' ) ); start2 = start + 1 )
	{
		*start = 0;
		if ( strstr( start2, "warning X" ) )
			msg.warning[start2].SetMsgReportedCommand( szCommand );
		else
			msg.error[start2].SetMsgReportedCommand( szCommand );
	}

	if ( start2 < end )
	{
		if ( strstr( start2, "warning X" ) )
			msg.warning[start2].SetMsgReportedCommand( szCommand );
		else
			msg.error[start2].SetMsgReportedCommand( szCommand );
	}
	free( dupMsg );
	Threading::g_mtxMsgReport.Unlock();
}

static void ShaderHadErrorDispatchInt( std::string_view szShader )
{
	g_ShaderHadError.emplace( szShader );
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

static bool CompareDupComboIndices( const StaticComboAliasRecord_t& pA, const StaticComboAliasRecord_t& pB ) noexcept
{
	return pA.m_nStaticComboID < pB.m_nStaticComboID;
}

static void FlushCombos( size_t& pnTotalFlushedSize, CUtlBuffer& pDynamicComboBuffer, CUtlBuffer& pBuf )
{
	if ( !pDynamicComboBuffer.TellPut() )
		// Nothing to do here
		return;

	size_t nCompressedSize;
	uint8_t* pCompressedShader = LZMA::OpportunisticCompress( reinterpret_cast<uint8_t*>( pDynamicComboBuffer.Base() ), pDynamicComboBuffer.TellPut(), &nCompressedSize );
	// high 2 bits of length =
	// 00 = bzip2 compressed
	// 10 = uncompressed
	// 01 = lzma compressed
	// 11 = unused

	if ( !pCompressedShader )
	{
		// it grew
		const uint32_t lFlagSize = 0x80000000 | pDynamicComboBuffer.TellPut();
		pBuf.Put( &lFlagSize, sizeof( lFlagSize ) );
		pBuf.Put( pDynamicComboBuffer.Base(), pDynamicComboBuffer.TellPut() );
		pnTotalFlushedSize += sizeof( lFlagSize ) + pDynamicComboBuffer.TellPut();
	}
	else
	{
		const uint32_t lFlagSize = 0x40000000 | gsl::narrow<uint32_t>( nCompressedSize );
		pBuf.Put( &lFlagSize, sizeof( lFlagSize ) );
		pBuf.Put( pCompressedShader, gsl::narrow<uint32_t>( nCompressedSize ) );
		delete[] pCompressedShader;
		pnTotalFlushedSize += sizeof( lFlagSize ) + nCompressedSize;
	}
	pDynamicComboBuffer.Clear(); // start over
}

static void OutputDynamicCombo( size_t& pnTotalFlushedSize, CUtlBuffer& pDynamicComboBuffer, CUtlBuffer& pBuf, uint64_t nComboID, uint32_t nComboSize, const uint8_t* pComboCode )
{
	if ( pDynamicComboBuffer.TellPut() + nComboSize + 16 >= MAX_SHADER_UNPACKED_BLOCK_SIZE )
		FlushCombos( pnTotalFlushedSize, pDynamicComboBuffer, pBuf );

	pDynamicComboBuffer.PutUnsignedInt( gsl::narrow<uint32_t>( nComboID ) );
	pDynamicComboBuffer.PutUnsignedInt( nComboSize );
	pDynamicComboBuffer.Put( pComboCode, nComboSize );
}

static fs::path GetVCSFilenames( const ShaderInfo_t& si )
{
	using namespace std::literals;
	auto path = g_pShaderPath / "shaders"sv / "fxc"sv;

	fs::directory_entry status( path );
	if ( !status.exists() )
	{
		std::cout << clr::pinkish << "mkdir " << path << clr::reset;
		// doh. . need to make the directory that the vcs file is going to go into.
		std::error_code c;
		fs::create_directories( path, c );
		if ( c )
			std::cout << clr::red << " Failed! " << c.message() << clr::reset << std::endl;
		else
			std::cout << std::endl;
	}

	path /= si.m_pShaderName;
	path += ".vcs"sv;

	// Check status of vcs file...
	status.assign( path );
	if ( status.exists() )
	{
		// The file exists, let's see if it's writable.
		if ( ( status.status().permissions() & ( fs::perms::owner_read | fs::perms::owner_write ) ) != ( fs::perms::owner_read | fs::perms::owner_write ) )
		{
			// It isn't writable. . we'd better change its permissions (or check it out possibly)
			std::cout << clr::pinkish << "Warning: making " << clr::red << path << clr::pinkish << " writable!" << clr::reset;
			std::error_code c;
			fs::permissions( status, fs::perms::owner_read | fs::perms::owner_write, c );
			if ( c )
				std::cout << clr::red << " Failed! " << c.message() << clr::reset << std::endl;
			else
				std::cout << std::endl;
		}
	}

	return path;
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
static constexpr uint32_t STATIC_COMBO_HASH_SIZE = 73;

struct StaticComboAuxInfo_t : StaticComboRecord_t
{
	uint32_t m_nCRC32; // CRC32 of packed data
	CStaticCombo* m_pByteCode;
};

static bool CompareComboIds( const StaticComboAuxInfo_t& pA, const StaticComboAuxInfo_t& pB ) noexcept
{
	return pA.m_nStaticComboID < pB.m_nStaticComboID;
}

static void WriteShaderFiles( std::string_view pShaderName )
{
	if ( !g_ShaderWrittenToDisk.emplace( pShaderName ).second )
		return;

	const bool bShaderFailed                = g_ShaderHadError.contains( pShaderName );
	const char* const szShaderFileOperation = bShaderFailed ? "Removing failed" : "Writing";

	static Clock::time_point lastTime = g_flStartTime;

	//
	// Progress indication
	//
	std::cout << "\r\033[2K" << szShaderFileOperation << " " << ( bShaderFailed ? clr::red : clr::green ) << pShaderName << clr::reset << "...\r";

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
	GLOBAL_DATA_MTX_UNLOCK();

	if ( shaderInfo.m_pShaderName.empty() )
		return;

	//
	// Shader vcs file name
	//
	auto path = GetVCSFilenames( shaderInfo );

	if ( bShaderFailed )
	{
		std::error_code c;
		fs::remove( path, c );
		std::cout << "\r\033[2K" << clr::red << pShaderName << clr::reset << " " << FormatTimeShort( duration_cast<chrono::seconds>( Clock::now() - lastTime ).count() ) << std::endl;
		lastTime = Clock::now();
		return;
	}

	if ( !pByteCodeArray )
		return;

	if ( g_bVerbose )
		std::cout << "\033[2K" << std::showbase << pShaderName << ": " << clr::green << shaderInfo.m_nTotalShaderCombos << clr::reset << " combos, centroid mask: " << clr::green << std::hex << shaderInfo.m_CentroidMask << std::dec << clr::reset << ", numDynamicCombos: " << clr::green << shaderInfo.m_nDynamicCombos << clr::reset << std::endl;

	//
	// Static combo headers
	//
	std::vector<StaticComboAuxInfo_t> StaticComboHeaders;

	StaticComboHeaders.reserve( 1ULL + pByteCodeArray->Count() ); // we know how much ram we need

	std::vector<size_t> comboIndicesHashedByCRC32[STATIC_COMBO_HASH_SIZE];
	std::vector<StaticComboAliasRecord_t> duplicateCombos;

	// now, lets fill in our combo headers, sort, and write
	for ( int nChain = 0; nChain < StaticComboNodeHash_t::NumChains; ++nChain )
	{
		for ( CStaticCombo* pStatic = pByteCodeArray->Chain( nChain ).Head(); pStatic; pStatic = pStatic->Next() )
		{
			const CStaticCombo::PackedCode& code = pStatic->Code();
			if ( code.GetLength() )
			{
				StaticComboAuxInfo_t hdr {
					{
						.m_nStaticComboID = gsl::narrow<uint32_t>( pStatic->ComboId() ),
						.m_nFileOffset = 0,
					},
					CRC32::ProcessSingleBuffer( code.GetData(), code.GetLength() ),
					pStatic
				};
				const uint32_t nHashIdx = hdr.m_nCRC32 % STATIC_COMBO_HASH_SIZE;
				__assume( 0 <= nHashIdx && nHashIdx < STATIC_COMBO_HASH_SIZE );

				// now, see if we have an identical static combo
				auto& hash = comboIndicesHashedByCRC32[nHashIdx];
				bool bIsDuplicate = false;
				for ( const size_t i : hash )
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
					StaticComboHeaders.emplace_back( std::move( hdr ) );
					hash.emplace_back( StaticComboHeaders.size() - 1 );
				}
			}
		}
	}
	// add sentinel key
	StaticComboHeaders.emplace_back( StaticComboAuxInfo_t { { 0xffffffff, 0 }, 0, nullptr } );

	// now, sort. sentinel key will end up at end
	std::sort( StaticComboHeaders.begin(), StaticComboHeaders.end(), CompareComboIds );

	//
	// Shader file stream buffer
	//
	std::ofstream ShaderFile( path, std::ios::binary | std::ios::trunc ); // Streaming buffer for vcs file (since this can blow memory)

	// ------ Header --------------
	const ShaderHeader_t header {
		SHADER_VCS_VERSION_NUMBER,
		gsl::narrow_cast<int32_t>( shaderInfo.m_nTotalShaderCombos ), // this is not actually used in vertexshaderdx8.cpp for combo checking
		gsl::narrow<int32_t>( shaderInfo.m_nDynamicCombos ),          // this is used
		0,
		shaderInfo.m_CentroidMask,
		gsl::narrow<uint32_t>( StaticComboHeaders.size() ),
		shaderInfo.m_Crc32
	};
	ShaderFile.write( reinterpret_cast<const char*>( &header ), sizeof( header ) );

	// static combo dictionary
	const auto nDictionaryOffset = ShaderFile.tellp();

	// we will re write this one we know the offsets
	ShaderFile.write( reinterpret_cast<const char*>( StaticComboHeaders.data() ), sizeof( StaticComboRecord_t ) * StaticComboHeaders.size() ); // dummy write, 8 bytes per static combo

	const uint32_t dupl = gsl::narrow<uint32_t>( duplicateCombos.size() );
	ShaderFile.write( reinterpret_cast<const char*>( &dupl ), sizeof( dupl ) );

	// now, write out all duplicate header records
	// sort duplicate combo records for binary search
	std::sort( duplicateCombos.begin(), duplicateCombos.end(), CompareDupComboIndices );

	ShaderFile.write( reinterpret_cast<const char*>( duplicateCombos.data() ), sizeof( StaticComboAliasRecord_t ) * duplicateCombos.size() );

	// now, write out all static combos
	for ( StaticComboRecord_t& SRec : StaticComboHeaders )
	{
		SRec.m_nFileOffset = gsl::narrow<uint32_t>( ShaderFile.tellp() );
		if ( SRec.m_nStaticComboID != 0xffffffff ) // sentinel key?
		{
			CStaticCombo* pStatic = pByteCodeArray->FindByKey( SRec.m_nStaticComboID );
			Assert( pStatic );

			// Put the packed chunk of code for this static combo
			if ( const auto& code = pStatic->Code() )
				ShaderFile.write( reinterpret_cast<const char*>( code.GetData() ), code.GetLength() );

			constexpr uint32_t endMark = 0xffffffff; // end of dynamic combos
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

	std::cout << "\r\033[2K" << clr::green << pShaderName << clr::reset << " " << FormatTimeShort( duration_cast<chrono::seconds>( Clock::now() - lastTime ).count() ) << std::endl;
	lastTime = Clock::now();
}

// Assemble a reply package to the master from the compiled bytecode
// return the length of the package.
static size_t AssembleWorkerReplyPackage( const CfgProcessor::CfgEntryInfo* pEntry, uint64_t nComboOfEntry, CUtlBuffer& pBuf )
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
		for ( const auto& combo : pStComboRec->DynamicCombos() )
		{
			CByteCodeBlock* pCode = combo.get();
			// check if we have already output an identical combo
			OutputDynamicCombo( nBytesWritten, ubDynamicComboBuffer, pBuf, pCode->m_nComboID,
								gsl::narrow<uint32_t>( pCode->m_nCodeSize ), pCode->get() );
		}
		FlushCombos( nBytesWritten, ubDynamicComboBuffer, pBuf );
	}

	// Time to limit amount of prints
	static Clock::time_point s_fLastInfoTime;
	static uint64_t s_nLastEntry = nComboOfEntry;
	static CUtlMovingAverage<uint64_t, 60> s_averageProcess;
	static std::string_view s_lastShader = pEntry->m_szName;
	const Clock::time_point fCurTime = Clock::now();

	GLOBAL_DATA_MTX_LOCK();
	if ( pStComboRec )
	{
		CStaticCombo *pCombo = pByteCodeArray->FindByKey( nComboOfEntry );
		pByteCodeArray->DeleteByKey( nComboOfEntry );
		delete pCombo;
	}
	if ( duration_cast<chrono::seconds>( fCurTime - s_fLastInfoTime ).count() != 0 )
	{
		if ( s_lastShader.data() != pEntry->m_szName.data() )
		{
			s_averageProcess.Reset();
			s_lastShader = pEntry->m_szName;
			s_nLastEntry = nComboOfEntry;
		}

		s_averageProcess.PushValue( s_nLastEntry - nComboOfEntry );
		s_nLastEntry = nComboOfEntry;
		const auto avg = s_averageProcess.GetAverage();
		std::cout << "\r\033[2KCompiling " << ( g_ShaderHadError.contains( pEntry->m_szName ) ? clr::red : clr::green ) << pEntry->m_szName << clr::reset << " [" << clr::blue << PrettyPrint( nComboOfEntry ) << clr::reset << " remaining] "
			<< FormatTimeShort( duration_cast<chrono::seconds>( fCurTime - g_flStartTime ).count() ) << " elapsed ("<< clr::green2 << avg << clr::reset << " c/s, est. remaining " << FormatTimeShort( nComboOfEntry / std::max( avg, 1ULL ) ) << ")\r";
		s_fLastInfoTime = fCurTime;
	}
	GLOBAL_DATA_MTX_UNLOCK();

	return nBytesWritten;
}

template <Threading::Mutex TMutexType>
class CWorkerAccumState
{
public:
	explicit CWorkerAccumState( TMutexType* pMutex, uint32_t iFlags ) noexcept
		: m_pMutex( pMutex ), m_iFirstCommand( 0 ), m_iNextCommand( 0 ), m_iEndCommand( 0 )
		, m_iLastFinished( 0 ), m_hCombo( nullptr ), m_bBreak( false ), m_iFlags( iFlags ) {}

	void RangeBegin( uint64_t iFirstCommand, uint64_t iEndCommand );
	void RangeFinished();

	void ExecuteCompileCommand( CfgProcessor::ComboHandle hCombo );
	void HandleCommandResponse( CfgProcessor::ComboHandle hCombo, CmdSink::IResponse* pResponse );

	void Run( uint32_t i )
	{
		m_arrSubProcessInfos.clear();
		m_arrSubProcessInfos.reserve( i );

		while ( i-- > 0 )
		{
			++m_nActive;
			m_threads.emplace_back( DoExecute, this );
		}

		constexpr const std::chrono::milliseconds sleepTime{ 250 };
		while ( m_nActive )
		{
			_mm_pause();
			std::this_thread::sleep_for( sleepTime );
		}

		std::ranges::for_each( m_threads, []( std::jthread& t ) { t.join(); } );
		m_threads.clear();
	}

	void OnProcessST();

	void Stop() noexcept
	{
		m_bBreak = true;
		std::ranges::for_each( m_threads, []( std::jthread& t ) { t.request_stop(); } );
	}

private:
	std::vector<std::jthread> m_threads;
	std::atomic<int> m_nActive;
	TMutexType* m_pMutex;

	static void DoExecute( std::stop_token token, CWorkerAccumState* pThis )
	{
		while ( pThis->OnProcess( token ) )
			continue;

		--pThis->m_nActive;
	}

	std::vector<uint64_t> m_arrSubProcessInfos;
	uint64_t m_iFirstCommand;
	uint64_t m_iNextCommand;
	uint64_t m_iEndCommand;

	uint64_t m_iLastFinished;

	CfgProcessor::ComboHandle m_hCombo;

	bool m_bBreak;
	const uint32_t m_iFlags;

	bool OnProcess( std::stop_token stopToken );
	void TryToPackageData( uint64_t iCommandNumber );
};

template <Threading::Mutex TMutexType>
void CWorkerAccumState<TMutexType>::RangeBegin( uint64_t iFirstCommand, uint64_t iEndCommand )
{
	m_iFirstCommand = iFirstCommand;
	m_iNextCommand  = iFirstCommand;
	m_iEndCommand   = iEndCommand;
	m_iLastFinished = iFirstCommand;
	m_hCombo        = nullptr;
	CfgProcessor::Combo_GetNext( m_iNextCommand, m_hCombo, m_iEndCommand );
}

template <Threading::Mutex TMutexType>
void CWorkerAccumState<TMutexType>::RangeFinished()
{
	// Finish packaging data
	TryToPackageData( m_iEndCommand - 1 );
}

template <Threading::Mutex TMutexType>
void CWorkerAccumState<TMutexType>::ExecuteCompileCommand( CfgProcessor::ComboHandle hCombo )
{
	CmdSink::IResponse* pResponse = nullptr;

	if constexpr ( std::is_same_v<TMutexType, Threading::null_mutex> )
	{
		if ( g_bVerbose2 )
		{
			char chReadBuf[4096];
			Combo_FormatCommandHumanReadable( hCombo, chReadBuf );
			std::cout << "running: \"" << clr::green << chReadBuf << clr::reset << "\"" << std::endl;
		}
	}

	Compiler::ExecuteCommand( Combo_BuildCommand( hCombo ), pResponse, m_iFlags );

	HandleCommandResponse( hCombo, pResponse );
}

static void StopCommandRange();

template <Threading::Mutex TMutexType>
void CWorkerAccumState<TMutexType>::HandleCommandResponse( CfgProcessor::ComboHandle hCombo, CmdSink::IResponse* pResponse )
{
	Assert( pResponse );

	// Command info
	const CfgProcessor::CfgEntryInfo* pEntryInfo = Combo_GetEntryInfo( hCombo );
	const uint64_t iComboIndex                   = Combo_GetComboNum( hCombo );
	const uint64_t iCommandNumber                = Combo_GetCommandNum( hCombo );

	if ( pResponse->Succeeded() )
	{
		GLOBAL_DATA_MTX_LOCK();
		const uint64_t nStComboIdx = iComboIndex / pEntryInfo->m_numDynamicCombos;
		const uint64_t nDyComboIdx = iComboIndex - ( nStComboIdx * pEntryInfo->m_numDynamicCombos );
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
			sprintf_s( chUnreportedListing, "%s(0,0): error 0000: Compiler failed without error description. Command number %llu", pEntryInfo->m_szShaderFileName.data(), iCommandNumber );
			szListing = chUnreportedListing;
		}

		char chBuffer[4096];
		Combo_FormatCommandHumanReadable( hCombo, chBuffer );

		ErrMsgDispatchMsgLine( chBuffer, szListing, pEntryInfo->m_szName );
		if ( !pResponse->Succeeded() && g_bFastFail )
			StopCommandRange();
	}

	pResponse->Release();

	// Maybe zip things up
	TryToPackageData( iCommandNumber );
}

template <Threading::Mutex TMutexType>
void CWorkerAccumState<TMutexType>::TryToPackageData( uint64_t iCommandNumber )
{
	m_pMutex->lock();

	uint64_t iFinishedByNow = iCommandNumber + 1;

	// Check if somebody is running an earlier command
	for ( const auto& iRunningCommand : m_arrSubProcessInfos )
	{
		if ( iRunningCommand < iCommandNumber )
		{
			iFinishedByNow = 0;
			break;
		}
	}

	const uint64_t iLastFinished = m_iLastFinished;
	if ( iFinishedByNow > m_iLastFinished )
	{
		m_iLastFinished = iFinishedByNow;
		m_pMutex->unlock();
	}
	else
	{
		m_pMutex->unlock();
		return;
	}

	CfgProcessor::ComboHandle hChBegin = CfgProcessor::Combo_GetCombo( iLastFinished );
	CfgProcessor::ComboHandle hChEnd   = CfgProcessor::Combo_GetCombo( iFinishedByNow );

	Assert( hChBegin && hChEnd );

	const CfgProcessor::CfgEntryInfo* pInfoBegin = Combo_GetEntryInfo( hChBegin );
	const CfgProcessor::CfgEntryInfo* pInfoEnd   = Combo_GetEntryInfo( hChEnd );

	uint64_t nComboBegin     = Combo_GetComboNum( hChBegin ) / pInfoBegin->m_numDynamicCombos;
	const uint64_t nComboEnd = Combo_GetComboNum( hChEnd ) / pInfoEnd->m_numDynamicCombos;

	for ( ; pInfoBegin && ( pInfoBegin->m_iCommandStart < pInfoEnd->m_iCommandStart || nComboBegin > nComboEnd ); )
	{
		// Zip this combo
		CUtlBuffer mbPacked;
		const size_t nPackedLength = AssembleWorkerReplyPackage( pInfoBegin, nComboBegin, mbPacked );

		if ( nPackedLength )
		{
			// Packed buffer
			GLOBAL_DATA_MTX_LOCK();
			uint8_t* pCodeBuffer = StaticComboFromDictAdd( pInfoBegin->m_szName, nComboBegin )->AllocPackedCodeBlock( nPackedLength );
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

template <Threading::Mutex TMutexType>
bool CWorkerAccumState<TMutexType>::OnProcess( std::stop_token stopToken )
{
	m_pMutex->lock();
	CfgProcessor::ComboHandle hThreadCombo = m_hCombo ? Combo_Alloc( m_hCombo ) : nullptr;
	m_arrSubProcessInfos.resize( m_arrSubProcessInfos.size() + 1 );
	auto iCurrentId = &m_arrSubProcessInfos.back();
	m_pMutex->unlock();

	uint64_t iThreadCommand = ~0ULL;

	for ( ;; )
	{
		m_pMutex->lock();
		if ( m_hCombo )
		{
			Combo_Assign( hThreadCombo, m_hCombo );
			*iCurrentId = Combo_GetCommandNum( hThreadCombo );
			Combo_GetNext( iThreadCommand, m_hCombo, m_iEndCommand );
		}
		else
		{
			Combo_Free( hThreadCombo );
			iThreadCommand = ~0ULL;
			*iCurrentId  = ~0ULL;
		}
		m_pMutex->unlock();

		if ( hThreadCombo && !stopToken.stop_requested() )
			ExecuteCompileCommand( hThreadCombo );
		else
			break;
	}

	Combo_Free( hThreadCombo );
	return false;
}

template <Threading::Mutex TMutexType>
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
	ProcessCommandRange_Singleton( uint32_t threads, uint32_t flags ) : m_nThreads( threads )
	{
		Assert( !Instance() );
		Instance() = this;
		Startup( flags );
	}

	~ProcessCommandRange_Singleton()
	{
		Assert( Instance() == this );
		Instance() = nullptr;
		Shutdown();
	}

public:
	void ProcessCommandRange( uint64_t shaderStart, uint64_t shaderEnd );

	void Stop();
	bool Stoped() const { return m_bStopped; }

protected:
	void Startup( uint32_t flags );
	void Shutdown();

	//
	// Multi-threaded section
	//
	struct MT
	{
		MT() : pWorkerObj( nullptr ) {}

		std::mutex mtx;

		using WorkerClass_t = CWorkerAccumState<std::mutex>;
		WorkerClass_t* pWorkerObj;
	} m_MT;

	//
	// Single-threaded section
	//
	struct ST
	{
		ST() : pWorkerObj( nullptr ) {}

		Threading::null_mutex mtx;

		using WorkerClass_t = CWorkerAccumState<Threading::null_mutex>;
		WorkerClass_t* pWorkerObj;
	} m_ST;

	const uint32_t m_nThreads;
	bool m_bStopped = false;
};

// TODO: Cleanup this hack
static void StopCommandRange()
{
	ProcessCommandRange_Singleton::Instance()->Stop();
}

void ProcessCommandRange_Singleton::Startup( uint32_t flags )
{
	if ( m_nThreads > 1 )
	{
		// Make sure that our mutex is in multi-threaded mode
		Threading::g_mtxGlobal.SetThreadedMode( Threading::eMultiThreaded );
		Threading::g_mtxMsgReport.SetThreadedMode( Threading::eMultiThreaded );

		m_MT.pWorkerObj = new MT::WorkerClass_t( &m_MT.mtx, flags );
	}
	else
		// Otherwise initialize single-threaded mode
		m_ST.pWorkerObj = new ST::WorkerClass_t( &m_ST.mtx, flags );
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


void ProcessCommandRange_Singleton::ProcessCommandRange( uint64_t shaderStart, uint64_t shaderEnd )
{
	if ( m_MT.pWorkerObj )
	{
		MT::WorkerClass_t* pWorkerObj = m_MT.pWorkerObj;

		pWorkerObj->RangeBegin( shaderStart, shaderEnd );
		pWorkerObj->Run( m_nThreads );
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

static void Shader_ParseShaderInfoFromCompileCommands( const CfgProcessor::CfgEntryInfo* pEntry, ShaderInfo_t& shaderInfo )
{
	if ( CfgProcessor::ComboHandle hCombo = CfgProcessor::Combo_GetCombo( pEntry->m_iCommandStart ) )
	{
		CfgProcessor::CfgEntryInfo const* info = Combo_GetEntryInfo( hCombo );

		memset( &shaderInfo, 0, sizeof( ShaderInfo_t ) );

		shaderInfo.m_CentroidMask       = info->m_nCentroidMask;
		shaderInfo.m_nShaderCombo       = 0;
		shaderInfo.m_nTotalShaderCombos = pEntry->m_numCombos;
		shaderInfo.m_nDynamicCombos     = pEntry->m_numDynamicCombos;
		shaderInfo.m_nStaticCombo       = 0;

		shaderInfo.m_pShaderName		= pEntry->m_szName;
		shaderInfo.m_pShaderSrc			= pEntry->m_szShaderFileName;
		shaderInfo.m_Crc32				= pEntry->m_nCrc32;

		Combo_Free( hCombo );
	}
}

static std::unique_ptr<CfgProcessor::CfgEntryInfo[]> Shared_ParseListOfCompileCommands( std::set<std::string> files, bool bForce, bool bSpewSkips )
{
	using namespace std::literals;
	const Clock::time_point tt_start = Clock::now();

	bool failed = false;
	std::vector<CfgProcessor::ShaderConfig> configs;
	const auto root = g_pShaderPath.string();
	for ( const auto& file : files )
	{
		uint32_t crc;
		std::string name = Parser::ConstructName( file, g_pShaderVersion );
		if ( Parser::CheckCrc( g_pShaderPath / file, root, name, crc ) && !bForce )
			continue;

		CfgProcessor::ShaderConfig conf;
		if ( !Parser::ParseFile( g_pShaderPath / file, root, g_pShaderVersion, conf.static_c, conf.dynamic_c, conf.skip, conf.centroid_mask, conf.includes ) )
		{
			std::cout << clr::red << "Failed to parse " << file << clr::reset << std::endl;
			failed = true;
			continue;
		}
		Parser::WriteInclude( g_pShaderPath / "include"sv / ( name + ".inc" ), name, conf.static_c, conf.dynamic_c, conf.skip );
		conf.name = std::move( name );
		conf.crc32 = crc;
		configs.emplace_back( std::move( conf ) );
	}

	if ( failed )
		exit( -1 );

	if ( configs.empty() )
		exit( 0 );

	CfgProcessor::SetupConfiguration( configs, g_pShaderVersion, g_pShaderPath, g_bVerbose );

	auto arrEntries = CfgProcessor::DescribeConfiguration( bSpewSkips );

	uint64_t numCompileCommands = 0, numStaticCombos = 0;
	for ( const CfgProcessor::CfgEntryInfo* pInfo = arrEntries.get(); pInfo && !pInfo->m_szName.empty(); ++pInfo )
	{
		numStaticCombos += pInfo->m_numStaticCombos;
		numCompileCommands = pInfo->m_iCommandEnd;
	}

	const Clock::time_point tt_end = Clock::now();

	std::cout << "\r\033[2KCompiling " << clr::green << PrettyPrint( numCompileCommands ) << clr::reset << " commands  in " << clr::green << PrettyPrint( numStaticCombos ) << clr::reset << " static combos, setup took " << clr::green << duration_cast<chrono::seconds>( tt_end - tt_start ).count() << clr::reset << " seconds.\r";

	return arrEntries;
}

static void CompileShaders( std::unique_ptr<CfgProcessor::CfgEntryInfo[]> arrEntries, uint32_t threads, uint32_t flags )
{
	ProcessCommandRange_Singleton pcr{ threads, flags };

	//
	// We will iterate on the cfg entries and process them
	//
	for ( const CfgProcessor::CfgEntryInfo* pEntry = arrEntries.get(); pEntry && !pEntry->m_szName.empty(); ++pEntry )
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
		WriteShaderFiles( pEntry->m_szName );
	}

	std::cout << "\r\033[2K\r";
}

static LONG WINAPI ExceptionFilter( _EXCEPTION_POINTERS* pExceptionInfo )
{
	constexpr const auto iType = static_cast<MINIDUMP_TYPE>( MiniDumpNormal | MiniDumpWithDataSegs | MiniDumpWithIndirectlyReferencedMemory | MiniDumpWithThreadInfo );

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
		"%s_%d%.2d%2d%.2d%.2d%.2d.mdmp",
		pch,
		pTime.tm_year + 1900,	/* Year less 2000 */
		pTime.tm_mon + 1,		/* month (0 - 11 : 0 = January) */
		pTime.tm_mday,			/* day of month (1 - 31) */
		pTime.tm_hour,			/* hour (0 - 23) */
		pTime.tm_min,			/* minutes (0 - 59) */
		pTime.tm_sec			/* seconds (0 - 59) */
		);

	BOOL bMinidumpResult = FALSE;
	const HANDLE hFile = ::CreateFile( rgchFileName, GENERIC_WRITE, FILE_SHARE_WRITE, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr );

	if ( hFile )
	{
		// dump the exception information into the file
		MINIDUMP_EXCEPTION_INFORMATION ExInfo;
		ExInfo.ThreadId = GetCurrentThreadId();
		ExInfo.ExceptionPointers = pExceptionInfo;
		ExInfo.ClientPointers = FALSE;

		bMinidumpResult = MiniDumpWriteDump( ::GetCurrentProcess(), ::GetCurrentProcessId(), hFile, iType, &ExInfo, nullptr, nullptr );
		CloseHandle( hFile );
	}

	// mark any failed minidump writes by renaming them
	if ( !bMinidumpResult )
	{
		char rgchFailedFileName[_MAX_PATH];
		_snprintf_s( rgchFailedFileName, ARRAYSIZE( rgchFailedFileName ), "failed_%s", rgchFileName );
		std::error_code c;
		fs::rename( rgchFileName, rgchFailedFileName, c );
	}

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

	if ( !g_CompilerMsg.empty() )
	{
		size_t totalWarnings = 0, totalErrors = 0;
		for ( const auto& msg : g_CompilerMsg )
		{
			totalWarnings += msg.second.warning.size();
			totalErrors += msg.second.error.size();
		}
		std::cout << "\033[2K" << clr::yellow << "WARNINGS" << clr::reset << "/" << clr::red << "ERRORS " << clr::reset << totalWarnings << "/" << totalErrors << std::endl;

		const auto& trim = []( std::string s ) -> std::string
		{
			s.erase( std::find_if( s.rbegin(), s.rend(), []( int ch ) { return !std::isspace( ch ); } ).base(), s.end() );
			return s;
		};

		const size_t cwdLen = fs::current_path().string().length() + 1;

		for ( const auto& sMsg : g_CompilerMsg )
		{
			const auto& msg             = sMsg.second;
			const auto& shaderName      = sMsg.first;
			const std::string searchPat = std::string( g_ShaderToShaderInfo[shaderName].m_pShaderSrc ) + "(";

			if ( const size_t warnings = msg.warning.size() )
				std::cout << "\033[2K" << shaderName << " " << clr::yellow << warnings << " WARNING(S):" << clr::reset << std::endl;

			for ( const auto& warn : msg.warning )
			{
				const auto& szMsg          = warn.first;
				const CompilerMsgInfo& cmi = warn.second;
				const uint64_t numReported = cmi.GetNumTimesReported();

				std::string m = trim( szMsg );
				if ( size_t find = m.find( searchPat ); find != std::string::npos && find >= cwdLen )
					m = m.replace( find - cwdLen, cwdLen, "" );
				std::cout << "\033[2K" << m << "\nReported " << clr::green << numReported << clr::reset << " time(s)" << std::endl;
			}

			if ( const size_t errors = msg.error.size() )
				std::cout << "\033[2K" << shaderName << " " << clr::red << errors << " ERROR(S):" << clr::reset << std::endl;

			// Compiler spew
			for ( const auto& err : msg.error )
			{
				const auto& szMsg          = err.first;
				const CompilerMsgInfo& cmi = err.second;
				const std::string& cmd     = cmi.GetFirstCommand();
				const uint64_t numReported = cmi.GetNumTimesReported();

				std::string m = trim( szMsg );
				if ( size_t find = m.find( searchPat ); find != std::string::npos && find >= cwdLen )
					m = m.replace( find - cwdLen, cwdLen, "" );
				std::cout << "\033[2K" << m << "\nReported " << clr::green << numReported << clr::reset << " time(s), example command: " << std::endl;

				std::cout << "\033[2K" << "    " << clr::green << cmd << clr::reset << std::endl;
			}
		}
	}

	// Failed shaders summary
	for ( const auto& failed : g_ShaderHadError )
		std::cout << "\033[2K" << clr::pinkish << "FAILED: " << clr::red << failed << clr::reset << std::endl;
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
		SetThreadExecutionState( ES_CONTINUOUS );
	}

	return FALSE;
}

static void WriteStats()
{
	if ( s_write )
		PrintCompileErrors();

	//
	// End
	//
	const Clock::time_point end = Clock::now();

	std::cout << "\033[2K" << clr::green << FormatTime( duration_cast<chrono::seconds>( end - g_flStartTime ).count() ) << clr::reset << " elapsed" << std::endl;
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

	ez::ezOptionParser cmdLine{};
	cmdLine.overview = "Source shader compiler.";
	cmdLine.syntax   = "ShaderCompile [OPTIONS] file1.fxc [file2.fxc...]";
	cmdLine.add( "", true, 1, 0, "Sets shader version", "-ver", "/ver" );
	cmdLine.add( "", true, 1, 0, "Base path for shaders", "-shaderpath", "/shaderpath" );
	cmdLine.add( "", false, 0, 0, "Skip crc check during compilation", "-force", "/force" );
	cmdLine.add( "", false, 0, 0, "Calculate crc for shader", "-crc", "/crc" );
	cmdLine.add( "", false, 0, 0, "Generate only header", "-dynamic", "/dynamic" );
	cmdLine.add( "", false, 0, 0, "Stop on first error", "-fastfail", "/fastfail" );
	cmdLine.add( "0", false, 1, 0, "Number of threads used, defaults to core count", "-threads", "/threads" );
	cmdLine.add( "", false, 0, 0, "Shows help", "-help", "-h", "/help", "/h" );

	cmdLine.add( "", false, 0, 0, "Verbose file cache and final shader info", "-verbose", "/verbose" );
	cmdLine.add( "", false, 0, 0, "Verbose compile commands", "-verbose2", "/verbose2" );
	cmdLine.add( "", false, 0, 0, "Enables preprocessor debug printing", "-verbose_preprocessor" );

	cmdLine.add( "", false, 0, 0, "Skips shader validation", "/Vd", "-no-validation" );
	cmdLine.add( "", false, 0, 0, "Directs the compiler to not use flow-control constructs where possible", "/Gfa", "-no-flow-control" );
	cmdLine.add( "", false, 0, 0, "Directs the compiler to use flow-control constructs where possible", "/Gfp", "-prefer-flow-control" );
	cmdLine.add( "", false, 0, 0, "Disables shader optimization", "/Od", "-disable-optimization" );
	cmdLine.add( "", false, 0, 0, "Enable debugging information", "/Zi", "-debug-info" );
	cmdLine.add( "1", false, 1, 0, "Set optimization level (0-3)", "/O", "-optimize" );

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

	g_flStartTime = Clock::now();

	uint32_t flags = 0;
	if ( cmdLine.isSet( "/Vd" ) )
		flags |= D3DCOMPILE_SKIP_VALIDATION;

	// Flow control
	if ( cmdLine.isSet( "/Gfa" ) )
		flags |= D3DCOMPILE_AVOID_FLOW_CONTROL;
	else if ( cmdLine.isSet( "/Gfp" ) )
		flags |= D3DCOMPILE_PREFER_FLOW_CONTROL;

	if ( cmdLine.isSet( "/Zi" ) )
		flags |= D3DCOMPILE_DEBUG | D3DCOMPILE_DEBUG_NAME_FOR_SOURCE;

	int optLevel = 1;
	cmdLine.get( "/O" )->getInt( optLevel );
	switch ( optLevel )
	{
	case 0:
		flags |= D3DCOMPILE_OPTIMIZATION_LEVEL0;
		break;
	default:
		std::cout << "Unknown optimization level " << optLevel << ", using default!" << std::endl;
		break;
	case 1:
		flags |= D3DCOMPILE_OPTIMIZATION_LEVEL1;
		break;
	case 2:
		flags |= D3DCOMPILE_OPTIMIZATION_LEVEL2;
		break;
	case 3:
		flags |= D3DCOMPILE_OPTIMIZATION_LEVEL3;
		break;
	}

	std::vector<std::string> badOptions;
	if ( !cmdLine.gotRequired( badOptions ) || cmdLine.lastArgs.size() < 1 )
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

	std::set<std::string> files;
	for ( const auto& f : cmdLine.lastArgs )
		files.insert( fs::path( *f ).filename().string() );

	std::string path;
	cmdLine.get( "-shaderpath" )->getString( path );
	g_pShaderPath = fs::absolute( std::move( path ) );

	if ( cmdLine.isSet( "-crc" ) )
	{
		const auto root = g_pShaderPath.string();
		for ( const auto& file : files )
		{
			const std::string name = Parser::ConstructName( file, g_pShaderVersion );
			uint32_t crc = 0;
			Parser::CheckCrc( g_pShaderPath / file, root, name, crc );
			std::cout << crc << std::endl;
		}
		return 0;
	}

	if ( cmdLine.isSet( "-dynamic" ) )
	{
		using namespace std::literals;
		bool failed = false;
		const auto root = g_pShaderPath.string();
		for ( const auto& file : files )
		{
			std::vector<Parser::Combo> static_c, dynamic_c;
			std::vector<std::string> skip;
			uint32_t centroid_mask = 0;
			std::vector<std::string> includes;

			if ( !Parser::ParseFile( g_pShaderPath / file, root, g_pShaderVersion, static_c, dynamic_c, skip, centroid_mask, includes ) )
			{
				std::cout << clr::red << "Failed to parse " << file << clr::reset << std::endl;
				failed = true;
			}
			const std::string name = Parser::ConstructName( file, g_pShaderVersion );
			Parser::WriteInclude( g_pShaderPath / "include"sv / ( name + ".inc" ), name, static_c, dynamic_c, skip );
		}
		return failed ? -1 : 0;
	}

	g_bVerbose = cmdLine.isSet( "-verbose" );
	g_bVerbose2 = cmdLine.isSet( "-verbose2" );
	g_bFastFail = cmdLine.isSet( "-fastfail" );

	// Setting up the minidump handlers
	SetUnhandledExceptionFilter( ExceptionFilter );
	SetThreadExecutionState( ES_CONTINUOUS | ES_SYSTEM_REQUIRED );

	auto entries = Shared_ParseListOfCompileCommands( std::move( files ), cmdLine.isSet( "-force" ), cmdLine.isSet( "-verbose_preprocessor" ) );

	unsigned long threads;
	cmdLine.get( "-threads" )->getULong( threads );
	CompileShaders( std::move( entries ), threads ? threads : std::thread::hardware_concurrency(), flags );

	WriteStats();
	SetThreadExecutionState( ES_CONTINUOUS );

	return gsl::narrow_cast<int>( g_ShaderHadError.size() );
}