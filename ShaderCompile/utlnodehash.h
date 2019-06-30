//====== Copyright © 1996-2005, Valve Corporation, All rights reserved. =======//
//
// Purpose: hashed intrusive linked list.
//
// $NoKeywords: $
//
// Serialization/unserialization buffer
//=============================================================================//

#ifndef UTLNODEHASH_H
#define UTLNODEHASH_H

#ifdef _WIN32
#pragma once
#endif

#include "utlintrusivelist.h"

// to use this class, your list node class must have a Key() function defined which returns an
// integer type. May add this class to main utl tier when i'm happy w/ it.
template <class T, int HASHSIZE = 7907, class K = int>
class CUtlNodeHash
{
	int m_nNumNodes;
public:
	CUtlIntrusiveDList<T> m_HashChains[HASHSIZE];

	CUtlNodeHash()
	{
		m_nNumNodes = 0;
	}

	[[nodiscard]] T* FindByKey( K nMatchKey, int* pChainNumber = nullptr )
	{
		unsigned int nChain = static_cast<unsigned int>( nMatchKey );
		nChain %= HASHSIZE;
		if ( pChainNumber )
			*pChainNumber = nChain;
		for ( T* pNode = m_HashChains[nChain].m_pHead; pNode; pNode = pNode->m_pNext )
			if ( pNode->Key() == nMatchKey )
				return pNode;
		return nullptr;
	}

	void Add( T* pNode )
	{
		unsigned int nChain = static_cast<unsigned int>( pNode->Key() );
		nChain %= HASHSIZE;
		m_HashChains[nChain].AddToHead( pNode );
		m_nNumNodes++;
	}

	void Purge()
	{
		m_nNumNodes = 0;
		// delete all nodes
		for ( int i = 0; i < HASHSIZE; i++ )
			m_HashChains[i].Purge();
	}

	[[nodiscard]] int Count() const
	{
		return m_nNumNodes;
	}

	void DeleteByKey( K nMatchKey )
	{
		int nChain;
		T* pSearch = FindByKey( nMatchKey, &nChain );
		if ( pSearch )
		{
			m_HashChains[nChain].RemoveNode( pSearch );
			m_nNumNodes--;
		}
	}

	~CUtlNodeHash()
	{
		// delete all lists
		Purge();
	}
};


#endif