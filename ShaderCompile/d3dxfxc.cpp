//====== Copyright © 1996-2006, Valve Corporation, All rights reserved. =======//
//
// Purpose: D3DX command implementation.
//
// $NoKeywords: $
//
//=============================================================================//

#include "d3dxfxc.h"

#include "basetypes.h"
#include "cmdsink.h"
#include "d3dcompiler.h"
#include "gsl/gsl_narrow"
#include <span>
#include <malloc.h>
#include <vector>

#pragma comment( lib, "D3DCompiler" )

CSharedFile::CSharedFile( std::vector<char>&& data ) noexcept : std::vector<char>( std::forward<std::vector<char>>( data ) )
{
}

void FileCache::Add( const std::string& fileName, std::vector<char>&& data )
{
	const auto& it = m_map.find( fileName );
	if ( it != m_map.end() )
		return;

	CSharedFile file( std::forward<std::vector<char>>( data ) );
	m_map.emplace( fileName, std::move( file ) );
}

const CSharedFile* FileCache::Get( const std::string& filename ) const
{
	// Search the cache first
	const auto find = m_map.find( filename );
	if ( find != m_map.cend() )
		return &find->second;
	return nullptr;
}

void FileCache::Clear()
{
	m_map.clear();
}

FileCache fileCache;

namespace Compiler
{
// The command that is intercepted by this namespace routines
static constexpr const char s_pszCommand[] = "command";
static constexpr size_t s_uCommandLen      = ARRAYSIZE( s_pszCommand );

static struct DxIncludeImpl final : public ID3DInclude
{
	STDMETHOD( Open )( THIS_ D3D_INCLUDE_TYPE, LPCSTR pFileName, LPCVOID, LPCVOID* ppData, UINT* pBytes ) override
	{
		const CSharedFile* file = fileCache.Get( pFileName );
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
} s_incDxImpl;

class CResponse final : public CmdSink::IResponse
{
public:
	explicit CResponse( ID3DBlob* pShader, ID3DBlob* pListing, HRESULT hr ) noexcept
		: m_pShader( pShader )
		, m_pListing( pListing )
		, m_hr( hr )
	{
	}

	~CResponse() override
	{
		if ( m_pShader )
			m_pShader->Release();

		if ( m_pListing )
			m_pListing->Release();
	}

	bool Succeeded() const noexcept override { return m_pShader && m_hr == S_OK; }
	size_t GetResultBufferLen() const override { return Succeeded() ? m_pShader->GetBufferSize() : 0; }
	const void* GetResultBuffer() const override { return Succeeded() ? m_pShader->GetBufferPointer() : nullptr; }
	const char* GetListing() const override { return static_cast<const char*>( m_pListing ? m_pListing->GetBufferPointer() : nullptr ); }

protected:
	ID3DBlob* m_pShader;
	ID3DBlob* m_pListing;
	HRESULT m_hr;
};


//
// Completely mimic the behaviour of "fxc.exe" in the specific cases related
// to shader compilations.
//
// @param pCommand       the command in form
//		"fxc.exe /DSHADERCOMBO=1 /DTOTALSHADERCOMBOS=4 /DCENTROIDMASK=0 /DNUMDYNAMICCOMBOS=4 /DFLAGS=0x0 /DNUM_BONES=1 /Dmain=main /Emain /Tvs_2_0 /DSHADER_MODEL_VS_2_0=1 /D_X360=1 /nologo /Foshader.o debugdrawenvmapmask_vs20.fxc>output.txt 2>&1"
//
void ExecuteCommand( const char* pCommand, CmdSink::IResponse* &pResponse, unsigned long flags )
{
	// Expect that the command passed is exactly "fxc.exe"
	Assert( !strncmp( pCommand, s_pszCommand, s_uCommandLen ) );
	pCommand += s_uCommandLen;

	// Macros to be defined for D3DX
	std::vector<D3D_SHADER_MACRO> macros;

	const char* curIter = pCommand;
	char const* pszFilename = curIter;
	curIter += strlen( curIter ) + 1;
	char const* szShaderModel = curIter;
	curIter += strlen( curIter ) + 1;
	while ( *curIter )
	{
		D3D_SHADER_MACRO macro;
		macro.Name = curIter;
		curIter += strlen( curIter ) + 1;
		macro.Definition = curIter;
		curIter += strlen( curIter ) + 1;
		macros.emplace_back( std::move( macro ) );
	}

	// Add a NULL-terminator
	macros.emplace_back( D3D_SHADER_MACRO { nullptr, nullptr } );

	ID3DBlob* pShader        = nullptr; // NOTE: Must release the COM interface later
	ID3DBlob* pErrorMessages = nullptr; // NOTE: Must release COM interface later

	LPCVOID lpcvData = nullptr;
	UINT numBytes    = 0;
	HRESULT hr       = s_incDxImpl.Open( D3D_INCLUDE_LOCAL, pszFilename, nullptr, &lpcvData, &numBytes );
	if ( !FAILED( hr ) )
	{
		hr = D3DCompile( lpcvData, numBytes, pszFilename, macros.data(), &s_incDxImpl, "main", szShaderModel, flags, 0, &pShader, &pErrorMessages );

		// Close the file
		s_incDxImpl.Close( lpcvData );
	}

	pResponse = new( std::nothrow ) CResponse( pShader, pErrorMessages, hr );
}
} // namespace InterceptFxc