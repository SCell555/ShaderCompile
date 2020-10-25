

#include <fstream>
#include <charconv>

#include "d3dcompiler.h"
#include "../ShaderCompile/include/robin_hood.h"
#include "gsl/gsl_narrow"
#include <Ice/Ice.h>
#include "CompilerInterface.h"

#pragma comment( lib, "D3DCompiler" )

static thread_local robin_hood::unordered_node_map<int, std::array<char, 8>> stringPool;
class D3DCompiler final : public Compiler::D3DCompiler, ID3DInclude
{
public:
	~D3DCompiler()
	{
		m_map.clear();
	}

	void AddInclude( std::string path, std::string internalName, const Ice::Current& current ) override
	{
		std::ifstream src( path, std::ios::binary | std::ios::ate );
		if ( !src )
			return;

		std::vector<char> data( gsl::narrow<size_t>( src.tellg() ) );
		src.clear();
		src.seekg( 0, std::ios::beg );
		src.read( data.data(), data.size() );

		Add( internalName, std::move( data ) );
	}

	Compiler::CompilerOutput Compile( std::string fileName, std::string shaderVersion, Compiler::Defines defines, int flags, const Ice::Current& current ) override
	{
		char versionUpr[16], version[26];
		strcpy_s( versionUpr, shaderVersion.c_str() );
		_strupr_s( versionUpr );
		sprintf_s( version, "SHADER_MODEL_%s", versionUpr );

		std::vector<D3D_SHADER_MACRO> macros;
		macros.emplace_back( D3D_SHADER_MACRO { version, "1" } );

		for ( size_t i = 0; i < defines.size(); ++i )
		{
			const auto& define = defines[i];
			D3D_SHADER_MACRO macro{ .Name = define.name.c_str() };
			if ( auto f = stringPool.find( define.value ); f != stringPool.end() )
				macro.Definition = f->second.data();
			else
			{
				auto& dest = stringPool.emplace( define.value, std::array<char, 8>{} ).first->second;
				std::to_chars( dest.data(), dest.data() + 8, define.value );
				macro.Definition = dest.data();
			}
			macros.emplace_back( std::move( macro ) );
		}

		macros.emplace_back( D3D_SHADER_MACRO { nullptr, nullptr } );

		ID3DBlob* pShader        = nullptr; // NOTE: Must release the COM interface later
		ID3DBlob* pErrorMessages = nullptr; // NOTE: Must release COM interface later

		LPCVOID lpcvData = nullptr;
		UINT numBytes    = 0;
		HRESULT hr       = Open( D3D_INCLUDE_LOCAL, fileName.c_str(), nullptr, &lpcvData, &numBytes );
		if ( !FAILED( hr ) )
		{
			hr = D3DCompile( lpcvData, numBytes, fileName.c_str(), macros.data(), this, "main", shaderVersion.c_str(), flags, 0, &pShader, &pErrorMessages );

			// Close the file
			Close( lpcvData );
		}

		Compiler::CompilerOutput out;
		if ( pShader )
		{
			out.bytecode.resize( pShader->GetBufferSize() );
			memcpy( out.bytecode.data(), pShader->GetBufferPointer(), out.bytecode.size() );
			pShader->Release();
		}

		if ( pErrorMessages )
		{
			out.error.resize( pErrorMessages->GetBufferSize() + 1 );
			memcpy( out.error.data(), pErrorMessages->GetBufferPointer(), out.error.size() - 1 );
			pErrorMessages->Release();
		}

		return out;
	}

	void Shutdown( const Ice::Current& current ) override
	{
		current.adapter->getCommunicator()->shutdown();
	}

private:
	STDMETHOD( Open )( THIS_ D3D_INCLUDE_TYPE, LPCSTR pFileName, LPCVOID, LPCVOID* ppData, UINT* pBytes ) override
	{
		const CSharedFile* file = Get( pFileName );
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

	class CSharedFile final : private std::vector<char>
	{
	public:
		CSharedFile( std::vector<char>&& data ) noexcept : std::vector<char>( std::forward<std::vector<char>>( data ) ) {}
		using std::vector<char>::vector;
		~CSharedFile() = default;

		[[nodiscard]] const void* Data() const noexcept { return data(); }
		[[nodiscard]] size_t Size() const noexcept { return size(); }
	};

	void Add( const std::string& fileName, std::vector<char>&& data )
	{
		const auto& it = m_map.find( fileName );
		if ( it != m_map.end() )
			return;

		CSharedFile file( std::forward<std::vector<char>>( data ) );
		m_map.emplace( fileName, std::move( file ) );
	}

	[[nodiscard]] const CSharedFile* Get( const std::string& filename )
	{
		// Search the cache first
		const auto& find = m_map.find( filename );
		if ( find != m_map.cend() )
			return &find->second;
		return nullptr;
	}

	typedef robin_hood::unordered_node_map<std::string, CSharedFile> Mapping;
	Mapping m_map;
};

int main( int argc, const char* argv[] )
{
	Ice::InitializationData initData;
	initData.properties = Ice::createProperties();
	initData.properties->setProperty( "Ice.ThreadPool.Server.SizeMax", std::to_string( std::thread::hardware_concurrency() ) );
	initData.properties->setProperty( "Ice.MessageSizeMax", "16384" );
	Ice::CommunicatorHolder ich( argc, argv, initData );
	auto adapter = ich->createObjectAdapterWithEndpoints( "CompilerAdapter", "default -h 127.0.0.1 -p 10000" );
	const auto s = std::make_shared<D3DCompiler>();
	adapter->add( s, Ice::stringToIdentity( "D3DCompiler" ) );
	adapter->activate();
	ich->waitForShutdown();
}