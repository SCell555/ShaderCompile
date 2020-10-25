//====== Copyright © 1996-2006, Valve Corporation, All rights reserved. =======//
//
// Purpose: D3DX command implementation.
//
// $NoKeywords: $
//
//=============================================================================//

#ifndef D3DXFXC_H
#define D3DXFXC_H
#ifdef _WIN32
	#pragma once
#endif

#include "basetypes.h"
#include "cmdsink.h"
#include "robin_hood.h"
#include <vector>
#include <utility>

#define USE_PROXY_PROCESS

#ifndef USE_PROXY_PROCESS
class CSharedFile final : private std::vector<char>
{
public:
	CSharedFile( std::vector<char>&& data ) noexcept;
	using std::vector<char>::vector;
	~CSharedFile() = default;

	[[nodiscard]] const void* Data() const noexcept { return data(); }
	[[nodiscard]] size_t Size() const noexcept { return size(); }
};

class FileCache final
{
public:
	FileCache() = default;
	~FileCache() { Clear(); }

	void Add( const std::string& fileName, std::vector<char>&& data );

	[[nodiscard]] const CSharedFile* Get( const std::string& filename );

	void Clear();

protected:
	typedef robin_hood::unordered_node_map<std::string, CSharedFile> Mapping;
	Mapping m_map;
};
extern FileCache fileCache;
#else
extern void RegisterFileForCompiler( const char* fullPath, const char* regName );
#endif

namespace InterceptFxc
{
#ifndef USE_PROXY_PROCESS
	void ExecuteCommand( const char* pCommand, CmdSink::IResponse** ppResponse, unsigned long flags );
#else
	struct CompileData
	{
		const char* pszFileName;
		const char* pszVersion;
		std::vector<std::pair<const char*, int>> defines;
	};

	void ExecuteCommand( const CompileData& pCommand, CmdSink::IResponse** ppResponse, unsigned long flags );
#endif
} // namespace InterceptFxc

#endif // #ifndef D3DXFXC_H