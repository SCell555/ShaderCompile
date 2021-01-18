//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose:
//
// $NoKeywords: $
//
// A growable memory class.
//===========================================================================//

#pragma once

#include "basetypes.h"
#include <cstring>
#include <malloc.h>
#include <utility>

//-----------------------------------------------------------------------------
// The CUtlMemory class:
// A growable memory class which doubles in size by default.
//-----------------------------------------------------------------------------
template <class T, class I = int>
class CUtlMemory
{
public:
	// constructor, destructor
	CUtlMemory( int nGrowSize = 0, int nInitSize = 0 );
	CUtlMemory( T* pMemory, int numElements );
	CUtlMemory( const T* pMemory, int numElements );
	CUtlMemory( CUtlMemory<T, I>&& src ) noexcept;
	~CUtlMemory();

	// Set the size by which the memory grows
	void Init( int nGrowSize = 0, int nInitSize = 0 );

	class Iterator_t
	{
	public:
		Iterator_t( I i )
			: index( i )
		{
		}
		I index;

		bool operator==( const Iterator_t& it ) const { return index == it.index; }
		bool operator!=( const Iterator_t& it ) const { return index != it.index; }
	};
	[[nodiscard]] Iterator_t First() const { return Iterator_t( IsIdxValid( 0 ) ? 0 : InvalidIndex() ); }
	[[nodiscard]] Iterator_t Next( const Iterator_t& it ) const { return Iterator_t( IsIdxValid( it.index + 1 ) ? it.index + 1 : InvalidIndex() ); }
	[[nodiscard]] I GetIndex( const Iterator_t& it ) const { return it.index; }
	[[nodiscard]] bool IsIdxAfter( I i, const Iterator_t& it ) const { return i > it.index; }
	[[nodiscard]] bool IsValidIterator( const Iterator_t& it ) const { return IsIdxValid( it.index ); }
	[[nodiscard]] Iterator_t InvalidIterator() const { return Iterator_t( InvalidIndex() ); }

	// element access
	[[nodiscard]] T& operator[]( I i );
	[[nodiscard]] const T& operator[]( I i ) const;
	[[nodiscard]] T& Element( I i );
	[[nodiscard]] const T& Element( I i ) const;

	// Can we use this index?
	[[nodiscard]] bool IsIdxValid( I i ) const;

	// Specify the invalid ('null') index that we'll only return on failure
	static const I INVALID_INDEX = static_cast<I>( -1 ); // For use with COMPILE_TIME_ASSERT
	[[nodiscard]] static I InvalidIndex() { return INVALID_INDEX; }

	// Gets the base address (can change when adding elements!)
	[[nodiscard]] T* Base();
	[[nodiscard]] const T* Base() const;

	// Attaches the buffer to external memory....
	void SetExternalBuffer( T* pMemory, int numElements );
	void SetExternalBuffer( const T* pMemory, int numElements );
	// Takes ownership of the passed memory, including freeing it when this buffer is destroyed.
	void AssumeMemory( T* pMemory, int nSize );

	// Fast swap
	void Swap( CUtlMemory<T, I>& mem );

	// Switches the buffer from an external memory buffer to a reallocatable buffer
	// Will copy the current contents of the external buffer to the reallocatable buffer
	void ConvertToGrowableMemory( int nGrowSize );

	// Size
	[[nodiscard]] int NumAllocated() const;
	[[nodiscard]] int Count() const;

	// Grows the memory, so that at least allocated + num elements are allocated
	void Grow( int num = 1 );

	// Makes sure we've got at least this much memory
	void EnsureCapacity( int num );

	// Memory deallocation
	void Purge();

	// Purge all but the given number of elements
	void Purge( int numElements );

	// is the memory externally allocated?
	[[nodiscard]] bool IsExternallyAllocated() const;

	// is the memory read only?
	[[nodiscard]] bool IsReadOnly() const;

	// Set the size by which the memory grows
	void SetGrowSize( int size );

protected:
	enum
	{
		EXTERNAL_BUFFER_MARKER       = -1,
		EXTERNAL_CONST_BUFFER_MARKER = -2,
	};

	T* m_pMemory;
	int m_nAllocationCount;
	int m_nGrowSize;
};

//-----------------------------------------------------------------------------
// The CUtlMemory class:
// A growable memory class which doubles in size by default.
//-----------------------------------------------------------------------------
template <class T, size_t SIZE, class I = int>
class CUtlMemoryFixedGrowable : public CUtlMemory<T, I>
{
	using BaseClass = CUtlMemory<T, I>;

public:
	CUtlMemoryFixedGrowable( int nGrowSize = 0, int nInitSize = SIZE )
		: BaseClass( m_pFixedMemory, SIZE )
	{
		Assert( nInitSize == 0 || nInitSize == SIZE );
		m_nMallocGrowSize = nGrowSize;
	}

	void Grow( int nCount = 1 )
	{
		if ( this->IsExternallyAllocated() )
			this->ConvertToGrowableMemory( m_nMallocGrowSize );
		BaseClass::Grow( nCount );
	}

	void EnsureCapacity( int num )
	{
		if ( this->m_nAllocationCount >= num )
			return;

		if ( this->IsExternallyAllocated() )
			// Can't grow a buffer whose memory was externally allocated
			this->ConvertToGrowableMemory( m_nMallocGrowSize );

		BaseClass::EnsureCapacity( num );
	}

private:
	int m_nMallocGrowSize;
	T m_pFixedMemory[SIZE];
};

//-----------------------------------------------------------------------------
// The CUtlMemoryConservative class:
// A dynamic memory class that tries to minimize overhead (itself small, no custom grow factor)
//-----------------------------------------------------------------------------
template <typename T>
class CUtlMemoryConservative
{
public:
	// constructor, destructor
	CUtlMemoryConservative( int nGrowSize = 0, int nInitSize = 0 )
		: m_pMemory( nullptr )
	{
	}
	CUtlMemoryConservative( T* pMemory, int numElements ) = delete;
	~CUtlMemoryConservative()
	{
		if ( m_pMemory )
			free( m_pMemory );
	}

	// Can we use this index?
	bool IsIdxValid( int i ) const { return i >= 0; }
	static int InvalidIndex() { return -1; }

	// Gets the base address
	T* Base() { return m_pMemory; }
	const T* Base() const { return m_pMemory; }

	// element access
	T& operator[]( int i )
	{
		Assert( IsIdxValid( i ) );
		return Base()[i];
	}
	const T& operator[]( int i ) const
	{
		Assert( IsIdxValid( i ) );
		return Base()[i];
	}
	T& Element( int i )
	{
		Assert( IsIdxValid( i ) );
		return Base()[i];
	}
	const T& Element( int i ) const
	{
		Assert( IsIdxValid( i ) );
		return Base()[i];
	}

	// Attaches the buffer to external memory....
	void SetExternalBuffer( T* pMemory, int numElements ) { Assert( 0 ); }

	[[nodiscard]] size_t AllocSize() const
	{
		return ( m_pMemory ) ? _msize( m_pMemory ) : 0;
	}

	[[nodiscard]] int NumAllocated() const
	{
		return AllocSize() / sizeof( T );
	}

	[[nodiscard]] int Count() const
	{
		return NumAllocated();
	}

	void ReAlloc( size_t sz )
	{
		m_pMemory = static_cast<T*>( realloc( m_pMemory, sz ) );
	}
	// Grows the memory, so that at least allocated + num elements are allocated
	void Grow( int num = 1 )
	{
		const int nCurN = NumAllocated();
		ReAlloc( ( nCurN + num ) * sizeof( T ) );
	}

	// Makes sure we've got at least this much memory
	void EnsureCapacity( int num )
	{
		const size_t nSize = sizeof( T ) * Max( num, Count() );
		ReAlloc( nSize );
	}

	// Memory deallocation
	void Purge()
	{
		free( m_pMemory );
		m_pMemory = nullptr;
	}

	// Purge all but the given number of elements
	void Purge( int numElements ) { ReAlloc( numElements * sizeof( T ) ); }

	// is the memory externally allocated?
	[[nodiscard]] bool IsExternallyAllocated() const { return false; }

	// Set the size by which the memory grows
	void SetGrowSize( int size ) {}

	class Iterator_t
	{
	public:
		Iterator_t( int i, int _limit )
			: index( i )
			, limit( _limit )
		{
		}
		int index;
		int limit;
		bool operator==( const Iterator_t& it ) const { return index == it.index; }
		bool operator!=( const Iterator_t& it ) const { return index != it.index; }
	};
	[[nodiscard]] Iterator_t First() const
	{
		int limit = NumAllocated();
		return Iterator_t( limit ? 0 : InvalidIndex(), limit );
	}
	[[nodiscard]] Iterator_t Next( const Iterator_t& it ) const { return Iterator_t( ( it.index + 1 < it.limit ) ? it.index + 1 : InvalidIndex(), it.limit ); }
	[[nodiscard]] int GetIndex( const Iterator_t& it ) const { return it.index; }
	[[nodiscard]] bool IsIdxAfter( int i, const Iterator_t& it ) const { return i > it.index; }
	[[nodiscard]] bool IsValidIterator( const Iterator_t& it ) const { return IsIdxValid( it.index ) && ( it.index < it.limit ); }
	[[nodiscard]] Iterator_t InvalidIterator() const { return Iterator_t( InvalidIndex(), 0 ); }

private:
	T* m_pMemory;
};

//-----------------------------------------------------------------------------
// constructor, destructor
//-----------------------------------------------------------------------------

template <class T, class I>
CUtlMemory<T, I>::CUtlMemory( int nGrowSize, int nInitAllocationCount )
	: m_pMemory( nullptr )
	, m_nAllocationCount( nInitAllocationCount )
	, m_nGrowSize( nGrowSize )
{
	Assert( nGrowSize >= 0 );
	if ( m_nAllocationCount )
	{
		m_pMemory = static_cast<T*>( malloc( m_nAllocationCount * sizeof( T ) ) );
	}
}

template <class T, class I>
CUtlMemory<T, I>::CUtlMemory( T* pMemory, int numElements )
	: m_pMemory( pMemory )
	, m_nAllocationCount( numElements )
{
	// Special marker indicating externally supplied modifyable memory
	m_nGrowSize = EXTERNAL_BUFFER_MARKER;
}

template <class T, class I>
CUtlMemory<T, I>::CUtlMemory( const T* pMemory, int numElements )
	: m_pMemory( const_cast<T*>( pMemory ) )
	, m_nAllocationCount( numElements )
{
	// Special marker indicating externally supplied modifyable memory
	m_nGrowSize = EXTERNAL_CONST_BUFFER_MARKER;
}

template <class T, class I>
CUtlMemory<T, I>::CUtlMemory( CUtlMemory<T, I>&& src ) noexcept
{
	// Default init this so when we destruct src it doesn't do anything.
	m_nGrowSize        = 0;
	m_pMemory          = nullptr;
	m_nAllocationCount = 0;

	Swap( src );
}

template <class T, class I>
CUtlMemory<T, I>::~CUtlMemory()
{
	Purge();
}

template <class T, class I>
void CUtlMemory<T, I>::Init( int nGrowSize /*= 0*/, int nInitSize /*= 0*/ )
{
	Purge();

	m_nGrowSize        = nGrowSize;
	m_nAllocationCount = nInitSize;
	Assert( nGrowSize >= 0 );
	if ( m_nAllocationCount )
	{
		m_pMemory = static_cast<T*>( malloc( m_nAllocationCount * sizeof( T ) ) );
	}
}

//-----------------------------------------------------------------------------
// Fast swap
//-----------------------------------------------------------------------------
template <class T, class I>
void CUtlMemory<T, I>::Swap( CUtlMemory<T, I>& mem )
{
	std::swap( m_nGrowSize, mem.m_nGrowSize );
	std::swap( m_pMemory, mem.m_pMemory );
	std::swap( m_nAllocationCount, mem.m_nAllocationCount );
}

//-----------------------------------------------------------------------------
// Switches the buffer from an external memory buffer to a reallocatable buffer
//-----------------------------------------------------------------------------
template <class T, class I>
void CUtlMemory<T, I>::ConvertToGrowableMemory( int nGrowSize )
{
	if ( !IsExternallyAllocated() )
		return;

	m_nGrowSize = nGrowSize;
	if ( m_nAllocationCount )
	{
		const int nNumBytes = m_nAllocationCount * sizeof( T );
		T* pMemory          = static_cast<T*>( malloc( nNumBytes ) );
		memcpy( pMemory, m_pMemory, nNumBytes );
		m_pMemory = pMemory;
	}
	else
		m_pMemory = nullptr;
}

//-----------------------------------------------------------------------------
// Attaches the buffer to external memory....
//-----------------------------------------------------------------------------
template <class T, class I>
void CUtlMemory<T, I>::SetExternalBuffer( T* pMemory, int numElements )
{
	// Blow away any existing allocated memory
	Purge();

	m_pMemory          = pMemory;
	m_nAllocationCount = numElements;

	// Indicate that we don't own the memory
	m_nGrowSize = EXTERNAL_BUFFER_MARKER;
}

template <class T, class I>
void CUtlMemory<T, I>::SetExternalBuffer( const T* pMemory, int numElements )
{
	// Blow away any existing allocated memory
	Purge();

	m_pMemory          = const_cast<T*>( pMemory );
	m_nAllocationCount = numElements;

	// Indicate that we don't own the memory
	m_nGrowSize = EXTERNAL_CONST_BUFFER_MARKER;
}

template <class T, class I>
void CUtlMemory<T, I>::AssumeMemory( T* pMemory, int numElements )
{
	// Blow away any existing allocated memory
	Purge();

	// Simply take the pointer but don't mark us as external
	m_pMemory          = pMemory;
	m_nAllocationCount = numElements;
}

//-----------------------------------------------------------------------------
// element access
//-----------------------------------------------------------------------------
template <class T, class I>
inline T& CUtlMemory<T, I>::operator[]( I i )
{
	// Avoid function calls in the asserts to improve debug build performance
	Assert( m_nGrowSize != EXTERNAL_CONST_BUFFER_MARKER ); //Assert( !IsReadOnly() );
	Assert( static_cast<uint32_t>( i ) < static_cast<uint32_t>( m_nAllocationCount ) );
	return m_pMemory[static_cast<uint32_t>( i )];
}

template <class T, class I>
inline const T& CUtlMemory<T, I>::operator[]( I i ) const
{
	// Avoid function calls in the asserts to improve debug build performance
	Assert( static_cast<uint32_t>( i ) < static_cast<uint32_t>( m_nAllocationCount ) );
	return m_pMemory[static_cast<uint32_t>( i )];
}

template <class T, class I>
inline T& CUtlMemory<T, I>::Element( I i )
{
	// Avoid function calls in the asserts to improve debug build performance
	Assert( m_nGrowSize != EXTERNAL_CONST_BUFFER_MARKER ); //Assert( !IsReadOnly() );
	Assert( static_cast<uint32_t>( i ) < static_cast<uint32_t>( m_nAllocationCount ) );
	return m_pMemory[static_cast<uint32_t>( i )];
}

template <class T, class I>
inline const T& CUtlMemory<T, I>::Element( I i ) const
{
	// Avoid function calls in the asserts to improve debug build performance
	Assert( static_cast<uint32_t>( i ) < static_cast<uint32_t>( m_nAllocationCount ) );
	return m_pMemory[static_cast<uint32_t>( i )];
}

//-----------------------------------------------------------------------------
// is the memory externally allocated?
//-----------------------------------------------------------------------------
template <class T, class I>
bool CUtlMemory<T, I>::IsExternallyAllocated() const
{
	return m_nGrowSize < 0;
}

//-----------------------------------------------------------------------------
// is the memory read only?
//-----------------------------------------------------------------------------
template <class T, class I>
bool CUtlMemory<T, I>::IsReadOnly() const
{
	return m_nGrowSize == EXTERNAL_CONST_BUFFER_MARKER;
}

template <class T, class I>
void CUtlMemory<T, I>::SetGrowSize( int nSize )
{
	Assert( !IsExternallyAllocated() );
	Assert( nSize >= 0 );
	m_nGrowSize = nSize;
}

//-----------------------------------------------------------------------------
// Gets the base address (can change when adding elements!)
//-----------------------------------------------------------------------------
template <class T, class I>
inline T* CUtlMemory<T, I>::Base()
{
	Assert( !IsReadOnly() );
	return m_pMemory;
}

template <class T, class I>
inline const T* CUtlMemory<T, I>::Base() const
{
	return m_pMemory;
}

//-----------------------------------------------------------------------------
// Size
//-----------------------------------------------------------------------------
template <class T, class I>
inline int CUtlMemory<T, I>::NumAllocated() const
{
	return m_nAllocationCount;
}

template <class T, class I>
inline int CUtlMemory<T, I>::Count() const
{
	return m_nAllocationCount;
}

//-----------------------------------------------------------------------------
// Is element index valid?
//-----------------------------------------------------------------------------
template <class T, class I>
inline bool CUtlMemory<T, I>::IsIdxValid( I i ) const
{
	// If we always cast 'i' and 'm_nAllocationCount' to unsigned then we can
	// do our range checking with a single comparison instead of two. This gives
	// a modest speedup in debug builds.
	return static_cast<uint32_t>( i ) < static_cast<uint32_t>( m_nAllocationCount );
}

//-----------------------------------------------------------------------------
// Grows the memory
//-----------------------------------------------------------------------------
inline int UtlMemory_CalcNewAllocationCount( int nAllocationCount, int nGrowSize, int nNewSize, int nBytesItem )
{
	if ( nGrowSize )
		nAllocationCount = ( ( 1 + ( ( nNewSize - 1 ) / nGrowSize ) ) * nGrowSize );
	else
	{
		if ( !nAllocationCount )
			// Compute an allocation which is at least as big as a cache line...
			nAllocationCount = ( 31 + nBytesItem ) / nBytesItem;

		while ( nAllocationCount < nNewSize )
			nAllocationCount *= 2;
	}

	return nAllocationCount;
}

template <class T, class I>
void CUtlMemory<T, I>::Grow( int num )
{
	Assert( num > 0 );

	if ( IsExternallyAllocated() )
	{
		// Can't grow a buffer whose memory was externally allocated
		Assert( 0 );
		return;
	}

	// Make sure we have at least numallocated + num allocations.
	// Use the grow rules specified for this memory (in m_nGrowSize)
	int nAllocationRequested = m_nAllocationCount + num;

	int nNewAllocationCount = UtlMemory_CalcNewAllocationCount( m_nAllocationCount, m_nGrowSize, nAllocationRequested, sizeof( T ) );

	// if m_nAllocationRequested wraps index type I, recalculate
	if ( static_cast<int>( static_cast<I>( nNewAllocationCount ) ) < nAllocationRequested )
	{
		if ( static_cast<int>( static_cast<I>( nNewAllocationCount ) ) == 0 && static_cast<int>( static_cast<I>( nNewAllocationCount - 1 ) ) >= nAllocationRequested )
			--nNewAllocationCount; // deal w/ the common case of m_nAllocationCount == MAX_USHORT + 1
		else
		{
			if ( static_cast<int>( static_cast<I>( nAllocationRequested ) ) != nAllocationRequested )
			{
				// we've been asked to grow memory to a size s.t. the index type can't address the requested amount of memory
				Assert( 0 );
				return;
			}
			while ( static_cast<int>( static_cast<I>( nNewAllocationCount ) ) < nAllocationRequested )
				nNewAllocationCount = ( nNewAllocationCount + nAllocationRequested ) / 2;
		}
	}

	m_nAllocationCount = nNewAllocationCount;

	if ( m_pMemory )
	{
		m_pMemory = static_cast<T*>( realloc( m_pMemory, m_nAllocationCount * sizeof( T ) ) );
		Assert( m_pMemory );
	}
	else
	{
		m_pMemory = static_cast<T*>( malloc( m_nAllocationCount * sizeof( T ) ) );
		Assert( m_pMemory );
	}
}

//-----------------------------------------------------------------------------
// Makes sure we've got at least this much memory
//-----------------------------------------------------------------------------
template <class T, class I>
inline void CUtlMemory<T, I>::EnsureCapacity( int num )
{
	if ( m_nAllocationCount >= num )
		return;

	if ( IsExternallyAllocated() )
	{
		// Can't grow a buffer whose memory was externally allocated
		Assert( 0 );
		return;
	}

	m_nAllocationCount = num;

	if ( m_pMemory )
		m_pMemory = static_cast<T*>( realloc( m_pMemory, m_nAllocationCount * sizeof( T ) ) );
	else
		m_pMemory = static_cast<T*>( malloc( m_nAllocationCount * sizeof( T ) ) );
}

//-----------------------------------------------------------------------------
// Memory deallocation
//-----------------------------------------------------------------------------
template <class T, class I>
void CUtlMemory<T, I>::Purge()
{
	if ( !IsExternallyAllocated() )
	{
		if ( m_pMemory )
		{
			free( m_pMemory );
			m_pMemory = nullptr;
		}
		m_nAllocationCount = 0;
	}
}

template <class T, class I>
void CUtlMemory<T, I>::Purge( int numElements )
{
	Assert( numElements >= 0 );

	if ( numElements > m_nAllocationCount )
	{
		// Ensure this isn't a grow request in disguise.
		Assert( numElements <= m_nAllocationCount );
		return;
	}

	// If we have zero elements, simply do a purge:
	if ( numElements == 0 )
		return Purge();

	if ( IsExternallyAllocated() )
		// Can't shrink a buffer whose memory was externally allocated, fail silently like purge
		return;

	// If the number of elements is the same as the allocation count, we are done.
	if ( numElements == m_nAllocationCount )
		return;

	if ( !m_pMemory )
	{
		// Allocation count is non zero, but memory is null.
		Assert( m_pMemory );
		return;
	}

	m_nAllocationCount = numElements;

	// Allocation count > 0, shrink it down.
	m_pMemory = static_cast<T*>( realloc( m_pMemory, m_nAllocationCount * sizeof( T ) ) );
}