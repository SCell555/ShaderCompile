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

#define _SILENCE_STDEXT_HASH_DEPRECATION_WARNINGS
#include <hash_map>

typedef void *HANDLE;

class CSharedFile
{
	CSharedFile();

public:
	~CSharedFile();
	static CSharedFile* CreateSharedFile( const char* fileName, const uint8* data, size_t size );
	static CSharedFile* CreateSharedFile( const char* fileName );

	[[nodiscard]] void* Data() const { return m_pData; }
	[[nodiscard]] size_t Size() const { return m_nSize; }

private:
	void* m_pBaseAddr;
	void* m_pData;
	size_t m_nSize;
	HANDLE m_pFile;
};

class FileCache
{
public:
	FileCache() = default;
	~FileCache() { Clear(); }

	void Add( const char* fileName, const uint8* data, size_t size );

	[[nodiscard]] CSharedFile* Get( char const* szFilename );

	void Clear();

protected:
	typedef stdext::hash_map<std::string, CSharedFile*> Mapping;
	Mapping m_map;
};

extern FileCache fileCache;

namespace InterceptFxc
{

bool TryExecuteCommand( const char* pCommand, CmdSink::IResponse** ppResponse, unsigned long flags );

}; // namespace InterceptFxc

#endif // #ifndef D3DXFXC_H