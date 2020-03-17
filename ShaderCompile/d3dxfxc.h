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

#include "flat_map.hpp"

class CSharedFile final : private std::vector<char>
{
public:
	CSharedFile( std::vector<char>&& data );
	using std::vector<char>::vector;
	~CSharedFile() = default;

	[[nodiscard]] const void* Data() const { return data(); }
	[[nodiscard]] size_t Size() const { return size(); }
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
	typedef chobo::flat_map<std::string, CSharedFile> Mapping;
	Mapping m_map;
};

extern FileCache fileCache;

namespace InterceptFxc
{

bool TryExecuteCommand( const char* pCommand, CmdSink::IResponse** ppResponse, unsigned long flags );

}; // namespace InterceptFxc

#endif // #ifndef D3DXFXC_H