//====== Copyright © 1996-2006, Valve Corporation, All rights reserved. =======//
//
// Purpose: D3DX command implementation.
//
// $NoKeywords: $
//
//=============================================================================//

#include "d3dxfxc.h"

#include "basetypes.h"
#include "cfgprocessor.h"
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


void Compiler::ExecuteCommand( const CfgProcessor::ComboBuildCommand& pCommand, CmdSink::IResponse* &pResponse, unsigned int flags )
{
	// Macros to be defined for D3DX
	std::vector<D3D_SHADER_MACRO> macros;
	macros.resize( pCommand.defines.size() + 1 );
	std::ranges::transform( pCommand.defines, macros.begin(), []( const auto& d ) { return D3D_SHADER_MACRO{ d.first.data(), d.second.data() }; } );

	ID3DBlob* pShader        = nullptr; // NOTE: Must release the COM interface later
	ID3DBlob* pErrorMessages = nullptr; // NOTE: Must release COM interface later

	LPCVOID lpcvData = nullptr;
	UINT numBytes    = 0;
	HRESULT hr       = s_incDxImpl.Open( D3D_INCLUDE_LOCAL, pCommand.fileName.data(), nullptr, &lpcvData, &numBytes );
	if ( !FAILED( hr ) )
	{
		hr = D3DCompile( lpcvData, numBytes, pCommand.fileName.data(), macros.data(), &s_incDxImpl, "main", pCommand.shaderModel.data(), flags, 0, &pShader, &pErrorMessages );

		// Close the file
		s_incDxImpl.Close( lpcvData );
	}

	pResponse = new( std::nothrow ) CResponse( pShader, pErrorMessages, hr );
}