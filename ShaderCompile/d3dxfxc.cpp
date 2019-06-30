//====== Copyright © 1996-2006, Valve Corporation, All rights reserved. =======//
//
// Purpose: D3DX command implementation.
//
// $NoKeywords: $
//
//=============================================================================//

#include "basetypes.h"

#include "d3dcompiler.h"

#include "gsl/span"

#include "d3dxfxc.h"
#include "cmdsink.h"

#include <malloc.h>
#include <vector>

#define _SILENCE_STDEXT_HASH_DEPRECATION_WARNINGS
#include <hash_map>

#pragma comment ( lib, "D3DCompiler" )

CSharedFile::CSharedFile(): m_pBaseAddr( nullptr ), m_pData( nullptr ), m_nSize( 0 ), m_pFile( nullptr )
{
}

CSharedFile::~CSharedFile()
{
	UnmapViewOfFile( m_pBaseAddr );
	CloseHandle( m_pFile );
}

CSharedFile* CSharedFile::CreateSharedFile( const char* fileName, const uint8* data, size_t size )
{
	CSharedFile* file = new CSharedFile();

	char chBufferName[0x100] = { 0 };
	sprintf_s( chBufferName, "shadercompile_file_%s", fileName );
	file->m_pFile = CreateFileMapping( INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, gsl::narrow<DWORD>( size + sizeof( size_t ) ), chBufferName );
	void* vptr = file->m_pBaseAddr = MapViewOfFile( file->m_pFile, FILE_MAP_ALL_ACCESS, 0, 0, 0 );
	uint8* ptr = static_cast<uint8*>( vptr );
	*reinterpret_cast<size_t*>( ptr ) = size;
	file->m_nSize = size;
	ptr += sizeof( size_t );
	file->m_pData = ptr;
	memcpy_s( ptr, size, data, size );
	return file;
}

CSharedFile* CSharedFile::CreateSharedFile( const char* fileName )
{
	CSharedFile* file = new CSharedFile();

	char chBufferName[0x100] = { 0 };
	sprintf_s( chBufferName, "shadercompile_file_%s", fileName );
	file->m_pFile = OpenFileMapping( FILE_MAP_ALL_ACCESS, FALSE, chBufferName );
	if ( !file->m_pFile )
	{
		delete file;
		return nullptr;
	}
	void* vptr = file->m_pBaseAddr = MapViewOfFile( file->m_pFile, FILE_MAP_ALL_ACCESS, 0, 0, 0 );
	uint8* ptr = static_cast<uint8*>( vptr );
	file->m_nSize = *reinterpret_cast<size_t*>( ptr );
	ptr += sizeof( size_t );
	file->m_pData = ptr;
	return file;
}

void FileCache::Add( const char* fileName, const uint8* data, size_t size )
{
	const auto& it = m_map.find( fileName );
	if ( it != m_map.end() )
		return;

	m_map.insert( { fileName, CSharedFile::CreateSharedFile( fileName, data, size ) } );;
}

CSharedFile* FileCache::Get( char const* szFilename )
{
	// Search the cache first
	const auto& it = m_map.find( szFilename );
	if ( it != m_map.end() )
		return it->second;

	// Create the cached file data
	CSharedFile* pData = CSharedFile::CreateSharedFile( szFilename );
	if ( pData )
		m_map.insert( { szFilename, pData } );

	return pData;
}

void FileCache::Clear()
{
	for ( auto& it : m_map )
		delete it.second;

	m_map.clear();
}

FileCache fileCache;

namespace InterceptFxc
{
	// The command that is intercepted by this namespace routines
	static constexpr const char s_pszCommand[] = "fxc.exe ";
	static constexpr size_t s_uCommandLen = ARRAYSIZE( s_pszCommand ) - 1;

	namespace Private
	{
		struct DxIncludeImpl : public ID3DInclude
		{
			STDMETHOD( Open )( THIS_ D3D_INCLUDE_TYPE, LPCSTR pFileName, LPCVOID, LPCVOID* ppData, UINT* pBytes ) override
			{
				CSharedFile* file = fileCache.Get( pFileName );
				if ( !file )
					return E_FAIL;

				*ppData = file->Data();
				*pBytes = gsl::narrow<UINT>( file->Size() );

				return S_OK;
			}

			STDMETHOD( Close )( THIS_ LPCVOID ) override
			{
				return S_OK;
			}

			virtual ~DxIncludeImpl() = default;
		};
		static DxIncludeImpl s_incDxImpl;

		static FORCEINLINE bool PATHSEPARATOR( char c )
		{
			return c == '\\' || c == '/';
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

		//
		// Response implementation
		//
		class CResponse : public CmdSink::IResponse
		{
		public:
			explicit CResponse( ID3DBlob* pShader, ID3DBlob* pListing, HRESULT hr );
			~CResponse() override;

		public:
			bool Succeeded() override { return m_pShader && m_hr == S_OK; }
			size_t GetResultBufferLen() override { return Succeeded() ? m_pShader->GetBufferSize() : 0; }
			const void* GetResultBuffer() override { return Succeeded() ? m_pShader->GetBufferPointer() : nullptr; }
			const char* GetListing() override { return static_cast<const char*>( m_pListing ? m_pListing->GetBufferPointer() : nullptr ); }

		protected:
			ID3DBlob* m_pShader;
			ID3DBlob* m_pListing;
			HRESULT m_hr;
		};

		CResponse::CResponse( ID3DBlob* pShader, ID3DBlob* pListing, HRESULT hr ) : m_pShader( pShader ), m_pListing( pListing ), m_hr( hr )
		{
		}

		CResponse::~CResponse()
		{
			if ( m_pShader )
				m_pShader->Release();

			if ( m_pListing )
				m_pListing->Release();
		}

		//
		// Perform a fast shader file compilation.
		//
		// @param pszFilename		the filename to compile (e.g. "debugdrawenvmapmask_vs20.fxc")
		// @param pMacros			null-terminated array of macro-defines
		// @param pszModel			shader model for compilation
		//
		void FastShaderCompile( const char* pszFilename, gsl::span<const D3D_SHADER_MACRO> pMacros, const char* pszModel, CmdSink::IResponse** ppResponse, DWORD flags )
		{
			ID3DBlob* pShader = nullptr; // NOTE: Must release the COM interface later
			ID3DBlob* pErrorMessages = nullptr; // NOTE: Must release COM interface later

			const LPCSTR fName = V_UnqualifiedFileName( pszFilename );

			LPCVOID lpcvData = nullptr;
			UINT numBytes = 0;
			HRESULT hr = s_incDxImpl.Open( D3D_INCLUDE_LOCAL, pszFilename, nullptr, &lpcvData, &numBytes );
			if ( !FAILED( hr ) )
			{
				hr = D3DCompile2( lpcvData, numBytes, fName, pMacros.data(), &s_incDxImpl, "main", pszModel, flags, 0, 0, nullptr, 0, &pShader, &pErrorMessages );

				// Close the file
				s_incDxImpl.Close( lpcvData );
			}

			if ( ppResponse )
				*ppResponse = new CResponse( pShader, pErrorMessages, hr );
			else
			{
				if ( pShader )
					pShader->Release();

				if ( pErrorMessages )
					pErrorMessages->Release();
			}
		}

	}; // namespace Private

	//
	// Completely mimic the behaviour of "fxc.exe" in the specific cases related
	// to shader compilations.
	//
	// @param pCommand       the command in form
	//		"fxc.exe /DSHADERCOMBO=1 /DTOTALSHADERCOMBOS=4 /DCENTROIDMASK=0 /DNUMDYNAMICCOMBOS=4 /DFLAGS=0x0 /DNUM_BONES=1 /Dmain=main /Emain /Tvs_2_0 /DSHADER_MODEL_VS_2_0=1 /D_X360=1 /nologo /Foshader.o debugdrawenvmapmask_vs20.fxc>output.txt 2>&1"
	//
	void ExecuteCommand( const char* pCommand, CmdSink::IResponse** ppResponse, DWORD flags )
	{
		// Expect that the command passed is exactly "fxc.exe"
		Assert( !strncmp( pCommand, s_pszCommand, s_uCommandLen ) );
		pCommand += s_uCommandLen;

		const size_t len = strlen( pCommand );
		// A duplicate portion of memory for modifications
		char* pEditableCommand = static_cast<char*>( alloca( len + 1 ) );
		strcpy_s( pEditableCommand, len + 1, pCommand );

		// Macros to be defined for D3DX
		std::vector<D3D_SHADER_MACRO> macros;

		// Shader model (determined when parsing "/D" flags)
		const char* pszShaderModel = nullptr;

		// Iterate over the command line and find all "/D...=..." settings
		for ( char* pszFlag = pEditableCommand; pszFlag && ( pszFlag = strstr( pszFlag, "/D" ) ) != nullptr; /* advance inside */ )
		{
			// Make sure this is a command-line flag (basic check for preceding space)
			if ( pszFlag > pEditableCommand && pszFlag[-1] && ' ' != pszFlag[-1] )
			{
				++pszFlag;
				continue;
			}

			// Name is immediately after "/D"
			char* pszFlagName = pszFlag + 2; // 2 = length of "/D"
			// Value will be determined later
			const char* pszValue = "";

			if ( char* pchEq = strchr( pszFlag, '=' ) )
			{
				// Value is after '=' sign
				*pchEq = 0;
				pszValue = pchEq + 1;
				pszFlag = pchEq + 1;
			}

			if ( char* pchSpace = strchr( pszFlag, ' ' ) )
			{
				// Space is designating the end of the flag
				*pchSpace = 0;
				pszFlag = pchSpace + 1;
			}
			else
				// Reached end of command line
				pszFlag = nullptr;

			// Shader model extraction
			if ( !strncmp( pszFlagName, "SHADER_MODEL_", 13 ) )
				pszShaderModel = pszFlagName + 13;

			// Add the macro definition to the macros array
			macros.emplace_back( D3D_SHADER_MACRO{ pszFlagName, pszValue } );
		}

		// Add a NULL-terminator
		macros.emplace_back( D3D_SHADER_MACRO{ nullptr, nullptr } );

		// Convert shader model to lowercase
		char chShaderModel[20] = {0};
		if ( pszShaderModel )
			strcpy_s( chShaderModel, pszShaderModel );
		_strlwr_s( chShaderModel );

		// Determine the file name (at the end of the command line before redirection)
		char const* pszFilename = "";
		if ( const char* pchCmdRedirect = strstr( pCommand, ">output.txt " ) )
		{
			const size_t uCmdEndOffset = ( pchCmdRedirect - pCommand );

			pEditableCommand[uCmdEndOffset] = 0;
			pszFilename = &pEditableCommand[uCmdEndOffset];

			while ( pszFilename > pEditableCommand && pszFilename[-1] && ' ' != pszFilename[-1] )
				--pszFilename;
		}

		// Compile the stuff
		Private::FastShaderCompile( pszFilename, macros, chShaderModel, ppResponse, flags );
	}

	bool TryExecuteCommand( const char* pCommand, CmdSink::IResponse** ppResponse, DWORD flags )
	{
		if ( !strncmp(pCommand, s_pszCommand, s_uCommandLen ) )
		{
			ExecuteCommand( pCommand, ppResponse, flags );
			return true;
		}
		return false;
	}

}; // namespace InterceptFxc