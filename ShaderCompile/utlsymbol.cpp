//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: Defines a symbol table
//
// $Header: $
// $NoKeywords: $
//=============================================================================//

#pragma warning( disable : 4514 )

#include "utlsymbol.h"

#include "gsl/gsl_util"

static constexpr CUtlSymbolTable::CStringPoolIndex INVALID_STRING_INDEX( 0xFFFF, 0xFFFF );

static constexpr size_t MIN_STRING_POOL_SIZE = 2048;

//-----------------------------------------------------------------------------
// symbol table stuff
//-----------------------------------------------------------------------------

inline const char* CUtlSymbolTable::StringFromIndex( const CStringPoolIndex& index ) const
{
	Assert( index.m_iPool < m_StringPools.size() );
	Assert( index.m_iOffset < m_StringPools[index.m_iPool]->m_TotalLen );

	return &m_StringPools[index.m_iPool]->m_Data[index.m_iOffset];
}

bool CUtlSymbolTable::CLess::operator()( void* ctx, const CStringPoolIndex& i1, const CStringPoolIndex& i2 ) const
{
	CUtlSymbolTable* pTable = static_cast<CUtlSymbolTable*>( ctx );
	const char* str1        = i1 == INVALID_STRING_INDEX ? pTable->m_pUserSearchString : pTable->StringFromIndex( i1 );
	const char* str2        = i2 == INVALID_STRING_INDEX ? pTable->m_pUserSearchString : pTable->StringFromIndex( i2 );

	if ( !str1 && str2 )
		return false;
	if ( !str2 && str1 )
		return true;
	if ( !str1 && !str2 )
		return false;
	if ( !pTable->m_bInsensitive )
		return strcmp( str1, str2 ) < 0;
	else
		return _stricmp( str1, str2 ) < 0;
}

//-----------------------------------------------------------------------------
// constructor, destructor
//-----------------------------------------------------------------------------
CUtlSymbolTable::CUtlSymbolTable( int growSize, int initSize, bool caseInsensitive )
	: m_Lookup( growSize, initSize, 0, this )
	, m_bInsensitive( caseInsensitive )
{
}

CUtlSymbolTable::CUtlSymbolTable( const CUtlSymbolTable& other )
{
	m_Lookup.CopyFrom( other.m_Lookup );
	m_Lookup.SetContext( this );
	m_bInsensitive = other.m_bInsensitive;
	m_StringPools.reserve( other.m_StringPools.size() );
	for ( size_t i = 0; i < other.m_StringPools.size(); ++i )
	{
		const auto& pool   = other.m_StringPools[i];
		auto& pool2        = m_StringPools[i];
		pool2              = static_cast<StringPool_t*>( malloc( sizeof( StringPool_t ) + pool->m_TotalLen - 1 ) );
		pool2->m_TotalLen  = pool->m_TotalLen;
		pool2->m_SpaceUsed = pool->m_SpaceUsed;
		memcpy( pool2->m_Data, pool->m_Data, pool->m_TotalLen );
	}
}

CUtlSymbolTable::~CUtlSymbolTable()
{
	// Release the stringpool string data
	RemoveAll();
}

CUtlSymbol CUtlSymbolTable::Find( const char* pString ) const
{
	if ( !pString )
		return CUtlSymbol();

	// Store a special context used to help with insertion
	m_pUserSearchString = pString;

	// Passing this special invalid symbol makes the comparison function
	// use the string passed in the context
	const UtlSymId_t idx = m_Lookup.Find( INVALID_STRING_INDEX );

#ifdef _DEBUG
	m_pUserSearchString = nullptr;
#endif

	return CUtlSymbol( idx );
}

size_t CUtlSymbolTable::FindPoolWithSpace( unsigned short len ) const
{
	for ( size_t i = 0; i < m_StringPools.size(); i++ )
	{
		StringPool_t* pPool = m_StringPools[i];
		if ( pPool->m_TotalLen - pPool->m_SpaceUsed >= len )
			return i;
	}

	return ~0ULL;
}

//-----------------------------------------------------------------------------
// Finds and/or creates a symbol based on the string
//-----------------------------------------------------------------------------

CUtlSymbol CUtlSymbolTable::AddString( const char* pString )
{
	if ( !pString )
		return CUtlSymbol( UTL_INVAL_SYMBOL );

	CUtlSymbol id = Find( pString );

	if ( id.IsValid() )
		return id;

	const size_t len = strlen( pString ) + 1;

	// Find a pool with space for this string, or allocate a new one.
	size_t iPool = FindPoolWithSpace( gsl::narrow<uint16_t>( len ) );
	if ( iPool == ~0ULL )
	{
		// Add a new pool.
		const size_t newPoolSize = std::max( len, MIN_STRING_POOL_SIZE );
		StringPool_t* pPool      = static_cast<StringPool_t*>( malloc( sizeof( StringPool_t ) + newPoolSize - 1 ) );
		pPool->m_TotalLen        = gsl::narrow<uint16_t>( newPoolSize );
		pPool->m_SpaceUsed       = 0;
		iPool                    = m_StringPools.size();
		m_StringPools.emplace_back( pPool );
	}

	// Copy the string in.
	StringPool_t* pPool = m_StringPools[iPool];
	Assert( pPool->m_SpaceUsed < 0xFFFF ); // This should never happen, because if we had a string > 64k, it
										   // would have been given its entire own pool.

	const unsigned short iStringOffset = pPool->m_SpaceUsed;

	memcpy( &pPool->m_Data[pPool->m_SpaceUsed], pString, len );
	pPool->m_SpaceUsed += gsl::narrow<uint16_t>( len );

	// didn't find, insert the string into the vector.
	CStringPoolIndex index;
	index.m_iPool   = gsl::narrow<uint16_t>( iPool );
	index.m_iOffset = iStringOffset;

	const UtlSymId_t idx = m_Lookup.Insert( index );
	return CUtlSymbol( idx );
}

//-----------------------------------------------------------------------------
// Look up the string associated with a particular symbol
//-----------------------------------------------------------------------------

const char* CUtlSymbolTable::String( CUtlSymbol id ) const
{
	if ( !id.IsValid() )
		return "";

	Assert( m_Lookup.IsValidIndex( id ) );
	return StringFromIndex( m_Lookup[id] );
}

//-----------------------------------------------------------------------------
// Remove all symbols in the table.
//-----------------------------------------------------------------------------

void CUtlSymbolTable::RemoveAll()
{
	m_Lookup.Purge();

	for ( size_t i = 0; i < m_StringPools.size(); i++ )
		free( m_StringPools[i] );

	m_StringPools.clear();
}