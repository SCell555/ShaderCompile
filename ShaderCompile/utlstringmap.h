//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose:
//
//===========================================================================//

#ifndef UTLSTRINGMAP_H
#define UTLSTRINGMAP_H
#ifdef _WIN32
#pragma once
#endif

#include "utlsymbol.h"

template <class T>
class CUtlStringMap
{
public:
	CUtlStringMap( bool caseInsensitive = true ) : m_SymbolTable( 0, 32, caseInsensitive )
	{
	}

	// Get data by the string itself:
	T& operator[]( const char* pString )
	{
		const CUtlSymbol symbol = m_SymbolTable.AddString( pString );
		const size_t index = static_cast<size_t>( static_cast<UtlSymId_t>( symbol ) );
		if( m_Vector.size() <= index )
			m_Vector.resize( index + 1 );
		return m_Vector[index];
	}

	// Get data by the string's symbol table ID - only used to retrieve a pre-existing symbol, not create a new one!
	T& operator[]( UtlSymId_t n )
	{
		Assert( n >= 0 && n <= m_Vector.size() );
		return m_Vector[n];
	}

	const T& operator[]( UtlSymId_t n ) const
	{
		Assert( n >=0 && n <= m_Vector.size() );
		return m_Vector[n];
	}

	bool Defined( const char* pString ) const
	{
		return m_SymbolTable.Find( pString ) != UTL_INVAL_SYMBOL;
	}

	UtlSymId_t Find( const char* pString ) const
	{
		return m_SymbolTable.Find( pString );
	}

	static UtlSymId_t InvalidIndex()
	{
		return UTL_INVAL_SYMBOL;
	}

	int GetNumStrings() const
	{
		return m_SymbolTable.GetNumStrings();
	}

	const char* String( int n )	const
	{
		return m_SymbolTable.String( n );
	}

	template <typename F>
	void ForEach( F&& functor ) const
	{
		std::for_each( m_Vector.begin(), m_Vector.end(), std::forward<F>( functor ) );
	}

	// Clear all of the data from the map
	void Clear()
	{
		m_Vector.clear();
		m_SymbolTable.RemoveAll();
	}

	void Purge()
	{
		m_Vector.clear();
		m_SymbolTable.RemoveAll();
	}

	void PurgeAndDeleteElements()
	{
		m_Vector.clear();
		m_SymbolTable.RemoveAll();
	}

private:
	std::vector<T> m_Vector;
	CUtlSymbolTable m_SymbolTable;
};

#endif // UTLSTRINGMAP_H