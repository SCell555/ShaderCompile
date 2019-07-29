//====== Copyright c 1996-2007, Valve Corporation, All rights reserved. =======//
//
// Purpose:
//
// $NoKeywords: $
//
//=============================================================================//

#ifndef SUBPROCESS_H
#define SUBPROCESS_H
#ifdef _WIN32
	#pragma once
#endif

#include "cmdsink.h"
#include <string>

typedef void *HANDLE;

class SubProcessKernelObjects
{
	friend class SubProcessKernelObjects_Memory;

public:
	SubProcessKernelObjects();
	~SubProcessKernelObjects();

	SubProcessKernelObjects( SubProcessKernelObjects const& ) = delete;
	SubProcessKernelObjects& operator=( SubProcessKernelObjects const& ) = delete;

protected:
	bool Create( const char* szBaseName );
	bool Open( const char* szBaseName );

public:
	[[nodiscard]] bool IsValid() const;
	void Close();

protected:
	HANDLE m_hMemorySection;
	HANDLE m_hMutex;
	HANDLE m_hEvent[2];
	unsigned m_dwCookie;
};

class SubProcessKernelObjects_Create : public SubProcessKernelObjects
{
public:
	SubProcessKernelObjects_Create( const char* szBaseName ) { Create( szBaseName ), m_dwCookie = 1; }
};

class SubProcessKernelObjects_Open : public SubProcessKernelObjects
{
public:
	SubProcessKernelObjects_Open( const char* szBaseName ) { Open( szBaseName ), m_dwCookie = 0; }
};

class SubProcessKernelObjects_Memory
{
public:
	SubProcessKernelObjects_Memory( SubProcessKernelObjects* p )
		: m_pMemory( nullptr )
		, m_pObjs( p )
		, m_pLockData( nullptr )
	{
	}
	~SubProcessKernelObjects_Memory() { Unlock(); }

public:
	void* Lock();
	bool Unlock();

public:
	[[nodiscard]] bool IsValid() const { return m_pLockData != nullptr; }
	[[nodiscard]] void* GetMemory() const { return m_pMemory; }

protected:
	void* m_pMemory;

private:
	SubProcessKernelObjects* m_pObjs;
	void* m_pLockData;
};

//
// Response implementation
//
class CSubProcessResponse final : public CmdSink::IResponse
{
public:
	explicit CSubProcessResponse( const void* pvMemory );
	~CSubProcessResponse() override = default;

	bool Succeeded() override { return 1 == m_dwResult; }
	size_t GetResultBufferLen() override { return Succeeded() ? m_dwResultBufferLength : 0; }
	const void* GetResultBuffer() override { return Succeeded() ? m_pvResultBuffer : nullptr; }
	const char* GetListing() override { return m_szListing && *m_szListing ? m_szListing : nullptr; }

protected:
	const void* m_pvMemory;
	unsigned long m_dwResult;
	unsigned long m_dwResultBufferLength;
	const void* m_pvResultBuffer;
	const char* m_szListing;
};

int ShaderCompile_Subprocess_Main( std::string szSubProcessData, unsigned long flags, bool local );

#endif // #ifndef SUBPROCESS_H