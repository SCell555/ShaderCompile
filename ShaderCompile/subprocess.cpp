//====== Copyright c 1996-2007, Valve Corporation, All rights reserved. =======//
//
// Purpose:
//
// $NoKeywords: $
//
//=============================================================================//

#include <windows.h>
#include "basetypes.h"
#include "cmdsink.h"
#include <string>
#include "subprocess.h"
#include "d3dxfxc.h"
#include <gsl/gsl_util>


//////////////////////////////////////////////////////////////////////////
//
// Base implementation of the shaderd kernel objects
//
//////////////////////////////////////////////////////////////////////////

SubProcessKernelObjects::SubProcessKernelObjects() : m_hMemorySection( nullptr ), m_hMutex( nullptr ), m_dwCookie( 0 )
{
	ZeroMemory( m_hEvent, sizeof( m_hEvent ) );
}

SubProcessKernelObjects::~SubProcessKernelObjects()
{
	Close();
}

BOOL SubProcessKernelObjects::Create( const char* szBaseName )
{
	char chBufferName[0x100] = { 0 };

	sprintf_s( chBufferName, "%s_msec", szBaseName );
	m_hMemorySection = CreateFileMapping( INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, 4 * 1024 * 1024, chBufferName ); // 4Mb for a piece
	if ( nullptr != m_hMemorySection )
	{
		if ( ERROR_ALREADY_EXISTS == GetLastError() )
		{
			CloseHandle( m_hMemorySection );
			m_hMemorySection = nullptr;

			Assert( !"CreateFileMapping - already exists!\n" );
		}
	}

	sprintf_s( chBufferName, "%s_mtx", szBaseName );
	m_hMutex = CreateMutex( nullptr, FALSE, chBufferName );

	for ( int k = 0; k < 2; ++ k )
	{
		sprintf_s( chBufferName, "%s_evt%d", szBaseName, k );
		m_hEvent[k] = CreateEvent( nullptr, FALSE, ( k ? TRUE /* = master */ : FALSE ), chBufferName );
	}

	return IsValid();
}

BOOL SubProcessKernelObjects::Open( const char* szBaseName )
{
	char chBufferName[0x100] = { 0 };

	sprintf_s( chBufferName, "%s_msec", szBaseName );
	m_hMemorySection = OpenFileMapping( FILE_MAP_ALL_ACCESS, FALSE, chBufferName );

	sprintf_s( chBufferName, "%s_mtx", szBaseName );
	m_hMutex = OpenMutex( MUTEX_ALL_ACCESS, FALSE, chBufferName );

	for ( int k = 0; k < 2; ++ k )
	{
		sprintf_s( chBufferName, "%s_evt%d", szBaseName, k );
		m_hEvent[k] = OpenEvent( EVENT_ALL_ACCESS, FALSE, chBufferName );
	}

	return IsValid();
}

BOOL SubProcessKernelObjects::IsValid() const
{
	return m_hMemorySection && m_hMutex && m_hEvent[0] && m_hEvent[1];
}

void SubProcessKernelObjects::Close()
{
	if ( m_hMemorySection )
		CloseHandle( m_hMemorySection );

	if ( m_hMutex )
		CloseHandle( m_hMutex );

	for ( HANDLE& k : m_hEvent )
		if ( k )
			CloseHandle( k );
}

//////////////////////////////////////////////////////////////////////////
//
// Helper class to send data back and forth
//
//////////////////////////////////////////////////////////////////////////

void* SubProcessKernelObjects_Memory::Lock()
{
	// Wait for our turn to act
	for ( unsigned iWaitAttempt = 0; iWaitAttempt < 13u; ++ iWaitAttempt )
	{
		const DWORD dwWait = ::WaitForSingleObject( m_pObjs->m_hEvent[m_pObjs->m_dwCookie], 10000 );
		switch ( dwWait )
		{
		case WAIT_OBJECT_0:
			{
				m_pLockData = MapViewOfFile( m_pObjs->m_hMemorySection, FILE_MAP_ALL_ACCESS, 0, 0, 0 );
				__assume( m_pLockData );
				if ( *static_cast<const DWORD*>( m_pLockData ) != m_pObjs->m_dwCookie )
				{
					// Yes, this is our turn, set our cookie in that memory segment
					*static_cast<DWORD*>( m_pLockData ) = m_pObjs->m_dwCookie;
					m_pMemory = static_cast<byte*>( m_pLockData ) + 2 * sizeof( DWORD );

					return m_pMemory;
				}
				else
				{
					// We just acted, still waiting for result
					UnmapViewOfFile( m_pLockData );
					m_pLockData = nullptr;

					SetEvent( m_pObjs->m_hEvent[!m_pObjs->m_dwCookie] );
					Sleep( 1 );

					continue;
				}
			}

		case WAIT_TIMEOUT:
			{
				char chMsg[0x100];
				sprintf_s( chMsg, "th%08lX> WAIT_TIMEOUT in Memory::Lock (attempt %d).\n", GetCurrentThreadId(), iWaitAttempt );
				OutputDebugString( chMsg );
			}
			continue; // retry

		default:
			OutputDebugString( "WAIT failure in Memory::Lock\n" );
			SetLastError( ERROR_BAD_UNIT );
			return nullptr;
		}
	}

	OutputDebugString( "Ran out of wait attempts in Memory::Lock\n" );
	SetLastError( ERROR_NOT_READY );
	return nullptr;
}

BOOL SubProcessKernelObjects_Memory::Unlock()
{
	if ( m_pLockData )
	{
		// Assert that the memory hasn't been spoiled
		Assert( m_pObjs->m_dwCookie == *static_cast<const DWORD*>( m_pLockData ) );

		UnmapViewOfFile( m_pLockData );
		m_pMemory = nullptr;
		m_pLockData = nullptr;

		SetEvent( m_pObjs->m_hEvent[!m_pObjs->m_dwCookie] );
		Sleep( 1 );

		return TRUE;
	}

	return FALSE;
}


//////////////////////////////////////////////////////////////////////////
//
// Implementation of the command subprocess:
//
// MASTER      ---- command ------->     SUB
//				string		-	zero terminated command string.
//
//
// MASTER     <---- result --------      SUB
//				dword		-	1 if succeeded, 0 if failed
//				dword		-	result buffer length, 0 if failed
//				<bytes>		-	result buffer data, none if result buffer length is 0
//				string		-	zero-terminated listing string
//
//////////////////////////////////////////////////////////////////////////


CSubProcessResponse::CSubProcessResponse( const void* pvMemory ) : m_pvMemory( pvMemory )
{
	const byte* pBytes = static_cast<const byte*>( pvMemory );

	m_dwResult = *reinterpret_cast<const DWORD*>( pBytes );
	pBytes += sizeof( DWORD );

	m_dwResultBufferLength = *reinterpret_cast<const DWORD*>( pBytes );
	pBytes += sizeof( DWORD );

	m_pvResultBuffer = pBytes;
	pBytes += m_dwResultBufferLength;

	m_szListing = reinterpret_cast<const char*>( *pBytes ? pBytes : nullptr );
}

static LONG __stdcall ShaderCompile_Subprocess_ExceptionHandler( struct _EXCEPTION_POINTERS* ExceptionInfo )
{
	Assert( !"ShaderCompile_Subprocess_ExceptionHandler" );
	::TerminateProcess( ::GetCurrentProcess(), ExceptionInfo->ExceptionRecord->ExceptionCode );
	return EXCEPTION_EXECUTE_HANDLER; // (never gets here anyway)
}

int ShaderCompile_Subprocess_Main( std::string szSubProcessData, DWORD flags, bool local )
{
	// Set our crash handler
	if ( !local )
		SetUnhandledExceptionFilter( ShaderCompile_Subprocess_ExceptionHandler );

	// Get our kernel objects
	SubProcessKernelObjects_Open objs( szSubProcessData.c_str() );

	if ( !objs.IsValid() )
		return -1;

	// Enter the command pumping loop
	SubProcessKernelObjects_Memory shrmem( &objs );
	for ( void* pvMemory = nullptr; nullptr != ( pvMemory = shrmem.Lock() ); shrmem.Unlock() )
	{
		// The memory is actually a command
		const char* szCommand = static_cast<const char*>( pvMemory );

		if ( !_stricmp( "keepalive", szCommand ) )
		{
			ZeroMemory( pvMemory, 4 * sizeof( DWORD ) );
			continue;
		}

		if ( !_stricmp( "quit", szCommand ) )
		{
			ZeroMemory( pvMemory, 4 * sizeof( DWORD ) );
			return 0;
		}

		CmdSink::IResponse* pResponse = nullptr;
		if ( InterceptFxc::TryExecuteCommand( szCommand, &pResponse, flags ) )
		{
			byte* pBytes = static_cast<byte*>( pvMemory );

			// Result
			const DWORD dwSucceededResult = pResponse->Succeeded() ? 1 : 0;
			*reinterpret_cast<DWORD*>( pBytes ) = dwSucceededResult;
			pBytes += sizeof( DWORD );

			// Result buffer len
			const DWORD dwBufferLength = gsl::narrow<DWORD>( pResponse->GetResultBufferLen() );
			*reinterpret_cast<DWORD*>( pBytes ) = dwBufferLength;
			pBytes += sizeof( DWORD );

			// Result buffer
			const void* pvResultBuffer = pResponse->GetResultBuffer();
			memcpy( pBytes, pvResultBuffer, dwBufferLength );
			pBytes += dwBufferLength;

			// Listing - copy string
			const char* szListing = pResponse->GetListing();
			if ( szListing )
				while ( 0 != ( *pBytes++ = *szListing++ ) ) continue;
			else
				*pBytes++ = 0;
		}
		else
		{
			ZeroMemory( pvMemory, 4 * sizeof( DWORD ) );
		}
	}

	return -2;
}