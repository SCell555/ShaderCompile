//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: Defines a symbol table
//
// $Header: $
// $NoKeywords: $
//===========================================================================//

#ifndef UTLSYMBOL_H
#define UTLSYMBOL_H

#ifdef _WIN32
#pragma once
#endif

#include <vector>
#include "utlmemory.h"


template <class I>
struct UtlRBTreeLinks_t
{
	enum NodeColor_t
	{
		RED = 0,
		BLACK
	};

	I  m_Left;
	I  m_Right;
	I  m_Parent;
	NodeColor_t  m_Tag;
};

template <class T, class I>
struct UtlRBTreeNode_t : UtlRBTreeLinks_t<I>
{
	T  m_Data;
};

template <class T, class I = unsigned short, typename L = bool ( * )( void*, const T&, const T& )>
class CUtlRBTree
{
public:
	typedef T KeyType_t;
	typedef T ElemType_t;
	typedef I IndexType_t;

	// Less func typedef
	// Returns true if the first parameter is "less" than the second
	typedef L LessFunc_t;

	// constructor, destructor
	// Left at growSize = 0, the memory will first allocate 1 element and double in size
	// at each increment.
	// LessFunc_t is required, but may be set after the constructor using SetLessFunc() below
	CUtlRBTree( int growSize = 0, int initSize = 0, const LessFunc_t& lessfunc = 0, void* context = nullptr );
	CUtlRBTree( const LessFunc_t& lessfunc, void* context = nullptr );
	~CUtlRBTree();

	void EnsureCapacity( int num );

	void CopyFrom( const CUtlRBTree<T, I, L>& other );

	// gets particular elements
	[[nodiscard]] T& Element( I i );
	[[nodiscard]] T const& Element( I i ) const;
	[[nodiscard]] T& operator[]( I i );
	[[nodiscard]] T const& operator[]( I i ) const;

	// Gets the root
	[[nodiscard]] I Root() const;

	// Num elements
	[[nodiscard]] unsigned int Count() const;

	// Max "size" of the vector
	// it's not generally safe to iterate from index 0 to MaxElement()-1
	// it IS safe to do so when using CUtlMemory as the allocator,
	// but we should really remove patterns using this anyways, for safety and generality
	[[nodiscard]] I  MaxElement() const;

	// Gets the children
	[[nodiscard]] I Parent( I i ) const;
	[[nodiscard]] I LeftChild( I i ) const;
	[[nodiscard]] I RightChild( I i ) const;

	// Tests if a node is a left or right child
	[[nodiscard]] bool  IsLeftChild( I i ) const;
	[[nodiscard]] bool  IsRightChild( I i ) const;

	// Tests if root or leaf
	[[nodiscard]] bool  IsRoot( I i ) const;
	[[nodiscard]] bool  IsLeaf( I i ) const;

	// Checks if a node is valid and in the tree
	[[nodiscard]] bool  IsValidIndex( I i ) const;

	// Checks if the tree as a whole is valid
	[[nodiscard]] bool  IsValid() const;

	// Invalid index
	static I InvalidIndex();

	// returns the tree depth (not a very fast operation)
	[[nodiscard]] int   Depth( I node ) const;
	[[nodiscard]] int   Depth() const;

	// Sets the less func
	void SetLessFunc( const LessFunc_t& func, void* context = nullptr );
	void SetContext( void* context ) { m_pLessFuncContext = context; }

	// Allocation method
	[[nodiscard]] I NewNode();

	// Insert method (inserts in order)
	[[nodiscard]] I Insert( T const& insert );
	[[nodiscard]] I Insert( T&& insert );
	void Insert( const T* pArray, int nItems );
	[[nodiscard]] I InsertIfNotFound( T const& insert );
	[[nodiscard]] I InsertIfNotFound( T&& insert );

	// Find method
	[[nodiscard]] I Find( T const& search ) const;

	// Remove methods
	void	RemoveAt( I i );
	bool	Remove( T const& remove );
	void	RemoveAll();
	void	Purge();

	[[nodiscard]] bool HasElement( T const& search ) const { return Find( search ) != InvalidIndex(); }

	// Allocation, deletion
	void  FreeNode( I i );

	// Iteration
	[[nodiscard]] I  FirstInorder() const;
	[[nodiscard]] I  NextInorder( I i ) const;
	[[nodiscard]] I  PrevInorder( I i ) const;
	[[nodiscard]] I  LastInorder() const;

	[[nodiscard]] I  FirstPreorder() const;
	[[nodiscard]] I  NextPreorder( I i ) const;
	[[nodiscard]] I  PrevPreorder( I i ) const;
	[[nodiscard]] I  LastPreorder() const;

	[[nodiscard]] I  FirstPostorder() const;
	[[nodiscard]] I  NextPostorder( I i ) const;

	// If you change the search key, this can be used to reinsert the
	// element into the tree.
	void	Reinsert( I elem );

	// swap in place
	void Swap( CUtlRBTree<T, I, L>& that );

	// Can't copy the tree this way!
	CUtlRBTree<T, I, L>& operator=( const CUtlRBTree<T, I, L>& other ) = delete;
protected:

	using Node_t = UtlRBTreeNode_t<T, I>;
	using Links_t = UtlRBTreeLinks_t<I>;
	using NodeColor_t = typename Links_t::NodeColor_t;

	// Sets the children
	void  SetParent( I i, I parent );
	void  SetLeftChild( I i, I child );
	void  SetRightChild( I i, I child );
	void  LinkToParent( I i, I parent, bool isLeft );

	// Gets at the links
	[[nodiscard]] Links_t const& Links( I i ) const;
	[[nodiscard]] Links_t& Links( I i );

	// Checks if a link is red or black
	[[nodiscard]] bool IsRed( I i ) const;
	[[nodiscard]] bool IsBlack( I i ) const;

	// Sets/gets node color
	[[nodiscard]] NodeColor_t Color( I i ) const;
	void        SetColor( I i, NodeColor_t c );

	// operations required to preserve tree balance
	void RotateLeft( I i );
	void RotateRight( I i );
	void InsertRebalance( I i );
	void RemoveRebalance( I i );

	// Insertion, removal
	[[nodiscard]] I InsertAt( I parent, bool leftchild );

	// copy constructors not allowed
	CUtlRBTree( CUtlRBTree<T, I, L> const& tree ) = delete;

	// Inserts a node into the tree, doesn't copy the data in.
	void FindInsertionPosition( T const& insert, I& parent, bool& leftchild );

	// Remove and add back an element in the tree.
	void	Unlink( I elem );
	void	Link( I elem );

	// Used for sorting.
	LessFunc_t m_LessFunc;
	void* m_pLessFuncContext;

	CUtlMemory<Node_t, I> m_Elements;
	I m_Root;
	I m_NumElements;
	I m_FirstFree;
	typename CUtlMemory<Node_t, I>::Iterator_t m_LastAlloc; // the last index allocated

	Node_t* m_pElements;

	[[nodiscard]] __forceinline CUtlMemory<Node_t, I> const& Elements() const
	{
		return m_Elements;
	}

	void ResetDbgInfo()
	{
		m_pElements = static_cast<Node_t*>( m_Elements.Base() );
	}
};


//-----------------------------------------------------------------------------
// forward declarations
//-----------------------------------------------------------------------------
class CUtlSymbolTable;


//-----------------------------------------------------------------------------
// This is a symbol, which is a easier way of dealing with strings.
//-----------------------------------------------------------------------------
typedef unsigned short UtlSymId_t;

#define UTL_INVAL_SYMBOL  ((UtlSymId_t)~0)

class CUtlSymbol
{
public:
	// constructor, destructor
	CUtlSymbol() : m_Id( UTL_INVAL_SYMBOL ) {}
	CUtlSymbol( UtlSymId_t id ) : m_Id( id ) {}
	CUtlSymbol( CUtlSymbol const& sym ) : m_Id( sym.m_Id ) {}

	// operator=
	CUtlSymbol& operator=( CUtlSymbol const& src ) { m_Id = src.m_Id; return *this; }

	// operator==
	bool operator==( CUtlSymbol const& src ) const { return m_Id == src.m_Id; }

	// Is valid?
	[[nodiscard]] bool IsValid() const { return m_Id != UTL_INVAL_SYMBOL; }

	// Gets at the symbol
	operator UtlSymId_t const() const { return m_Id; }

protected:
	UtlSymId_t   m_Id;
};


//-----------------------------------------------------------------------------
// CUtlSymbolTable:
// description:
//    This class defines a symbol table, which allows us to perform mappings
//    of strings to symbols and back. The symbol class itself contains
//    a static version of this class for creating global strings, but this
//    class can also be instanced to create local symbol tables.
//-----------------------------------------------------------------------------

class CUtlSymbolTable
{
public:
	// constructor, destructor
	CUtlSymbolTable( int growSize = 0, int initSize = 0, bool caseInsensitive = false );
	~CUtlSymbolTable();

	// Finds and/or creates a symbol based on the string
	CUtlSymbol AddString( const char* pString );

	// Finds the symbol for pString
	CUtlSymbol Find( const char* pString ) const;

	// Look up the string associated with a particular symbol
	const char* String( CUtlSymbol id ) const;

	// Remove all symbols in the table.
	void  RemoveAll();

	int GetNumStrings() const
	{
		return m_Lookup.Count();
	}

	class CStringPoolIndex
	{
	public:
		constexpr CStringPoolIndex() : m_iPool( 0 ), m_iOffset( 0 )
		{
		}

		constexpr CStringPoolIndex( unsigned short iPool, unsigned short iOffset ) : m_iPool( iPool ), m_iOffset( iOffset )
		{
		}

		constexpr bool operator==( const CStringPoolIndex &other )	const
		{
			return m_iPool == other.m_iPool && m_iOffset == other.m_iOffset;
		}

		unsigned short m_iPool;		// Index into m_StringPools.
		unsigned short m_iOffset;	// Index into the string pool.
	};
protected:

	class CLess
	{
	public:
		CLess( int unused = 0 ) {}
		bool operator!() const { return false; }
		bool operator()( void* ctx, const CStringPoolIndex &left, const CStringPoolIndex &right ) const;
	};

	// Stores the symbol lookup
	using CTree = CUtlRBTree<CStringPoolIndex, unsigned short, CLess>;

	struct StringPool_t
	{
		unsigned short m_TotalLen;		// How large is
		unsigned short m_SpaceUsed;
		char m_Data[1];
	};

	CTree m_Lookup;
	bool m_bInsensitive;
	mutable const char* m_pUserSearchString;

	// stores the string data
	std::vector<StringPool_t*> m_StringPools;

private:
	[[nodiscard]] size_t FindPoolWithSpace( unsigned short len ) const;
	[[nodiscard]] const char* StringFromIndex( const CStringPoolIndex &index ) const;

	friend class CLess;
};

// This creates a simple class that includes the underlying CUtlSymbol
//  as a private member and then instances a private symbol table to
//  manage those symbols.  Avoids the possibility of the code polluting the
//  'global'/default symbol table, while letting the code look like
//  it's just using = and .String() to look at CUtlSymbol type objects
//
// NOTE:  You can't pass these objects between .dlls in an interface (also true of CUtlSymbol of course)
//
#define DECLARE_PRIVATE_SYMBOLTYPE( typename )			\
	class typename										\
	{													\
	public:												\
		typename();										\
		typename( const char* pStr );					\
		typename( const typename& src );				\
		typename& operator=( typename const& src );		\
		bool operator==( typename const& src ) const;	\
		const char* String( ) const;					\
	private:											\
		CUtlSymbol m_SymbolId;							\
	};

// Put this in the .cpp file that uses the above typename
#define IMPLEMENT_PRIVATE_SYMBOLTYPE( typename )					\
	static CUtlSymbolTable g_##typename##SymbolTable;				\
	typename::typename()											\
	{																\
		m_SymbolId = UTL_INVAL_SYMBOL;								\
	}																\
	typename::typename( const char* pStr )							\
	{																\
		m_SymbolId = g_##typename##SymbolTable.AddString( pStr );	\
	}																\
	typename::typename( typename const& src )						\
	{																\
		m_SymbolId = src.m_SymbolId;								\
	}																\
	typename& typename::operator=( typename const& src )			\
	{																\
		m_SymbolId = src.m_SymbolId;								\
		return *this;												\
	}																\
	bool typename::operator==( typename const& src ) const			\
	{																\
		return ( m_SymbolId == src.m_SymbolId );					\
	}																\
	const char* typename::String( ) const							\
	{																\
		return g_##typename##SymbolTable.String( m_SymbolId );		\
	}


template <typename T, typename...Args>
inline T* Construct( T* pMemory, const Args&... args )
{
	__assume( pMemory != nullptr );
	return ::new( pMemory ) T{ args... };
}

template <typename T, typename... Args>
inline void CopyConstruct( T* pMemory, Args&&... src )
{
	__assume( pMemory != nullptr );
	::new( pMemory ) T( std::forward<Args>( src )... );
}

template <typename T>
inline void Destruct( T* p )
{
	p->~T();
}

template <class T, class I, typename L>
inline CUtlRBTree<T, I, L>::CUtlRBTree( int growSize, int initSize, const LessFunc_t& lessfunc, void* context ) :
	m_LessFunc( lessfunc ), m_pLessFuncContext( context ),
	m_Elements( growSize, initSize ), m_Root( InvalidIndex() ),
	m_NumElements( 0 ), m_FirstFree( InvalidIndex() ),
	m_LastAlloc( m_Elements.InvalidIterator() )
{
	ResetDbgInfo();
}

template <class T, class I, typename L>
inline CUtlRBTree<T, I, L>::CUtlRBTree( const LessFunc_t& lessfunc, void* context ) :
	m_LessFunc( lessfunc ), m_pLessFuncContext( context ),
	m_Elements( 0, 0 ), m_Root( InvalidIndex() ),
	m_NumElements( 0 ), m_FirstFree( InvalidIndex() ),
	m_LastAlloc( m_Elements.InvalidIterator() )
{
	ResetDbgInfo();
}

template <class T, class I, typename L>
inline CUtlRBTree<T, I, L>::~CUtlRBTree()
{
	Purge();
}

template <class T, class I, typename L>
inline void CUtlRBTree<T, I, L>::EnsureCapacity( int num )
{
	m_Elements.EnsureCapacity( num );
}

template <class T, class I, typename L>
inline void CUtlRBTree<T, I, L>::CopyFrom( const CUtlRBTree<T, I, L>& other )
{
	Purge();
	m_Elements.EnsureCapacity( other.m_Elements.Count() );
	memcpy( m_Elements.Base(), other.m_Elements.Base(), other.m_Elements.Count() * sizeof( T ) );
	m_LessFunc = other.m_LessFunc;
	m_pLessFuncContext = other.m_pLessFuncContext;
	m_Root = other.m_Root;
	m_NumElements = other.m_NumElements;
	m_FirstFree = other.m_FirstFree;
	m_LastAlloc = other.m_LastAlloc;
	ResetDbgInfo();
}

//-----------------------------------------------------------------------------
// gets particular elements
//-----------------------------------------------------------------------------

template <class T, class I, typename L>
inline T& CUtlRBTree<T, I, L>::Element( I i )
{
	return m_Elements[i].m_Data;
}

template <class T, class I, typename L>
inline T const& CUtlRBTree<T, I, L>::Element( I i ) const
{
	return m_Elements[i].m_Data;
}

template <class T, class I, typename L>
inline T& CUtlRBTree<T, I, L>::operator[]( I i )
{
	return Element( i );
}

template <class T, class I, typename L>
inline T const& CUtlRBTree<T, I, L>::operator[]( I i ) const
{
	return Element( i );
}

//-----------------------------------------------------------------------------
//
// various accessors
//
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
// Gets the root
//-----------------------------------------------------------------------------

template <class T, class I, typename L>
inline I CUtlRBTree<T, I, L>::Root() const
{
	return m_Root;
}

//-----------------------------------------------------------------------------
// Num elements
//-----------------------------------------------------------------------------

template <class T, class I, typename L>
inline unsigned int CUtlRBTree<T, I, L>::Count() const
{
	return static_cast<unsigned int>( m_NumElements );
}

//-----------------------------------------------------------------------------
// Max "size" of the vector
//-----------------------------------------------------------------------------

template <class T, class I, typename L>
inline I CUtlRBTree<T, I, L>::MaxElement() const
{
	return static_cast<I>( m_Elements.NumAllocated() );
}


//-----------------------------------------------------------------------------
// Gets the children
//-----------------------------------------------------------------------------

template <class T, class I, typename L>
inline I CUtlRBTree<T, I, L>::Parent( I i ) const
{
	return Links( i ).m_Parent;
}

template <class T, class I, typename L>
inline I CUtlRBTree<T, I, L>::LeftChild( I i ) const
{
	return Links( i ).m_Left;
}

template <class T, class I, typename L>
inline I CUtlRBTree<T, I, L>::RightChild( I i ) const
{
	return Links( i ).m_Right;
}

//-----------------------------------------------------------------------------
// Tests if a node is a left or right child
//-----------------------------------------------------------------------------

template <class T, class I, typename L>
inline bool CUtlRBTree<T, I, L>::IsLeftChild( I i ) const
{
	return LeftChild( Parent( i ) ) == i;
}

template <class T, class I, typename L>
inline bool CUtlRBTree<T, I, L>::IsRightChild( I i ) const
{
	return RightChild( Parent( i ) ) == i;
}


//-----------------------------------------------------------------------------
// Tests if root or leaf
//-----------------------------------------------------------------------------

template <class T, class I, typename L>
inline bool CUtlRBTree<T, I, L>::IsRoot( I i ) const
{
	return i == m_Root;
}

template <class T, class I, typename L>
inline bool CUtlRBTree<T, I, L>::IsLeaf( I i ) const
{
	return ( LeftChild( i ) == InvalidIndex() ) && ( RightChild( i ) == InvalidIndex() );
}


//-----------------------------------------------------------------------------
// Checks if a node is valid and in the tree
//-----------------------------------------------------------------------------

template <class T, class I, typename L>
inline bool CUtlRBTree<T, I, L>::IsValidIndex( I i ) const
{
	if ( !m_Elements.IsIdxValid( i ) )
		return false;

	if ( m_Elements.IsIdxAfter( i, m_LastAlloc ) )
		return false; // don't read values that have been allocated, but not constructed

	return LeftChild( i ) != i;
}


//-----------------------------------------------------------------------------
// Invalid index
//-----------------------------------------------------------------------------

template <class T, class I, typename L>
inline I CUtlRBTree<T, I, L>::InvalidIndex()
{
	return (I)CUtlMemory<UtlRBTreeNode_t<T, I>, I>::InvalidIndex();
}


//-----------------------------------------------------------------------------
// returns the tree depth (not a very fast operation)
//-----------------------------------------------------------------------------

template <class T, class I, typename L>
inline int CUtlRBTree<T, I, L>::Depth() const
{
	return Depth( Root() );
}

//-----------------------------------------------------------------------------
// Sets the children
//-----------------------------------------------------------------------------

template <class T, class I, typename L>
inline void  CUtlRBTree<T, I, L>::SetParent( I i, I parent )
{
	Links( i ).m_Parent = parent;
}

template <class T, class I, typename L>
inline void  CUtlRBTree<T, I, L>::SetLeftChild( I i, I child )
{
	Links( i ).m_Left = child;
}

template <class T, class I, typename L>
inline void  CUtlRBTree<T, I, L>::SetRightChild( I i, I child )
{
	Links( i ).m_Right = child;
}

//-----------------------------------------------------------------------------
// Gets at the links
//-----------------------------------------------------------------------------

template <class T, class I, typename L>
inline const typename CUtlRBTree<T, I, L>::Links_t& CUtlRBTree<T, I, L>::Links( I i ) const
{
	// Sentinel node, makes life easier
	static Links_t s_Sentinel =
	{
		InvalidIndex(), InvalidIndex(), InvalidIndex(), NodeColor_t::BLACK
	};

	return ( i != InvalidIndex() ) ? static_cast<const Links_t&>( m_Elements.Element( i ) ) : s_Sentinel;
}

template <class T, class I, typename L>
inline typename CUtlRBTree<T, I, L>::Links_t& CUtlRBTree<T, I, L>::Links( I i )
{
	Assert( i != InvalidIndex() );
	return *static_cast<Links_t*>( &m_Elements[i] );
}

//-----------------------------------------------------------------------------
// Checks if a link is red or black
//-----------------------------------------------------------------------------

template <class T, class I, typename L>
inline bool CUtlRBTree<T, I, L>::IsRed( I i ) const
{
	return ( Links( i ).m_Tag == NodeColor_t::RED );
}

template <class T, class I, typename L>
inline bool CUtlRBTree<T, I, L>::IsBlack( I i ) const
{
	return ( Links( i ).m_Tag == NodeColor_t::BLACK );
}


//-----------------------------------------------------------------------------
// Sets/gets node color
//-----------------------------------------------------------------------------

template <class T, class I, typename L>
inline typename CUtlRBTree<T, I, L>::NodeColor_t  CUtlRBTree<T, I, L>::Color( I i ) const
{
	return Links( i ).m_Tag;
}

template <class T, class I, typename L>
inline void CUtlRBTree<T, I, L>::SetColor( I i, typename CUtlRBTree<T, I, L>::NodeColor_t c )
{
	Links( i ).m_Tag = c;
}

//-----------------------------------------------------------------------------
// Allocates/ deallocates nodes
//-----------------------------------------------------------------------------
template <class T, class I, typename L>
inline I CUtlRBTree<T, I, L>::NewNode()
{
	I elem;

	// Nothing in the free list; add.
	if ( m_FirstFree == InvalidIndex() )
	{
		Assert( m_Elements.IsValidIterator( m_LastAlloc ) || m_NumElements == 0 );
		auto it = m_Elements.IsValidIterator( m_LastAlloc ) ? m_Elements.Next( m_LastAlloc ) : m_Elements.First();
		if ( !m_Elements.IsValidIterator( it ) )
		{
			m_Elements.Grow();

			it = m_Elements.IsValidIterator( m_LastAlloc ) ? m_Elements.Next( m_LastAlloc ) : m_Elements.First();

			Assert( m_Elements.IsValidIterator( it ) );
			if ( !m_Elements.IsValidIterator( it ) )
			{
				throw;
			}
		}
		m_LastAlloc = it;
		elem = m_Elements.GetIndex( m_LastAlloc );
		Assert( m_Elements.IsValidIterator( m_LastAlloc ) );
	}
	else
	{
		elem = m_FirstFree;
		m_FirstFree = Links( m_FirstFree ).m_Right;
	}

#ifdef _DEBUG
	// reset links to invalid....
	Links_t& node = Links( elem );
	node.m_Left = node.m_Right = node.m_Parent = InvalidIndex();
#endif

	Construct( &Element( elem ) );
	ResetDbgInfo();

	return elem;
}

template <class T, class I, typename L>
void  CUtlRBTree<T, I, L>::FreeNode( I i )
{
	Assert( IsValidIndex( i ) && ( i != InvalidIndex() ) );
	Destruct( &Element( i ) );
	SetLeftChild( i, i ); // indicates it's in not in the tree
	SetRightChild( i, m_FirstFree );
	m_FirstFree = i;
}


//-----------------------------------------------------------------------------
// Rotates node i to the left
//-----------------------------------------------------------------------------

template <class T, class I, typename L>
void CUtlRBTree<T, I, L>::RotateLeft( I elem )
{
	I rightchild = RightChild( elem );
	SetRightChild( elem, LeftChild( rightchild ) );
	if ( LeftChild( rightchild ) != InvalidIndex() )
		SetParent( LeftChild( rightchild ), elem );

	if ( rightchild != InvalidIndex() )
		SetParent( rightchild, Parent( elem ) );
	if ( !IsRoot( elem ) )
	{
		if ( IsLeftChild( elem ) )
			SetLeftChild( Parent( elem ), rightchild );
		else
			SetRightChild( Parent( elem ), rightchild );
	}
	else
		m_Root = rightchild;

	SetLeftChild( rightchild, elem );
	if ( elem != InvalidIndex() )
		SetParent( elem, rightchild );
}


//-----------------------------------------------------------------------------
// Rotates node i to the right
//-----------------------------------------------------------------------------

template <class T, class I, typename L>
void CUtlRBTree<T, I, L>::RotateRight( I elem )
{
	I leftchild = LeftChild( elem );
	SetLeftChild( elem, RightChild( leftchild ) );
	if ( RightChild( leftchild ) != InvalidIndex() )
		SetParent( RightChild( leftchild ), elem );

	if ( leftchild != InvalidIndex() )
		SetParent( leftchild, Parent( elem ) );
	if ( !IsRoot( elem ) )
	{
		if ( IsRightChild( elem ) )
			SetRightChild( Parent( elem ), leftchild );
		else
			SetLeftChild( Parent( elem ), leftchild );
	}
	else
		m_Root = leftchild;

	SetRightChild( leftchild, elem );
	if ( elem != InvalidIndex() )
		SetParent( elem, leftchild );
}


//-----------------------------------------------------------------------------
// Rebalances the tree after an insertion
//-----------------------------------------------------------------------------

template <class T, class I, typename L>
void CUtlRBTree<T, I, L>::InsertRebalance( I elem )
{
	while ( !IsRoot( elem ) && ( Color( Parent( elem ) ) == NodeColor_t::RED ) )
	{
		I parent = Parent( elem );
		I grandparent = Parent( parent );

		/* we have a violation */
		if ( IsLeftChild( parent ) )
		{
			I uncle = RightChild( grandparent );
			if ( IsRed( uncle ) )
			{
				/* uncle is RED */
				SetColor( parent, NodeColor_t::BLACK );
				SetColor( uncle, NodeColor_t::BLACK );
				SetColor( grandparent, NodeColor_t::RED );
				elem = grandparent;
			}
			else
			{
				/* uncle is BLACK */
				if ( IsRightChild( elem ) )
				{
					/* make x a left child, will change parent and grandparent */
					elem = parent;
					RotateLeft( elem );
					parent = Parent( elem );
					grandparent = Parent( parent );
				}
				/* recolor and rotate */
				SetColor( parent, NodeColor_t::BLACK );
				SetColor( grandparent, NodeColor_t::RED );
				RotateRight( grandparent );
			}
		}
		else
		{
			/* mirror image of above code */
			I uncle = LeftChild( grandparent );
			if ( IsRed( uncle ) )
			{
				/* uncle is RED */
				SetColor( parent, NodeColor_t::BLACK );
				SetColor( uncle, NodeColor_t::BLACK );
				SetColor( grandparent, NodeColor_t::RED );
				elem = grandparent;
			}
			else
			{
				/* uncle is BLACK */
				if ( IsLeftChild( elem ) )
				{
					/* make x a right child, will change parent and grandparent */
					elem = parent;
					RotateRight( parent );
					parent = Parent( elem );
					grandparent = Parent( parent );
				}
				/* recolor and rotate */
				SetColor( parent, NodeColor_t::BLACK );
				SetColor( grandparent, NodeColor_t::RED );
				RotateLeft( grandparent );
			}
		}
	}
	SetColor( m_Root, NodeColor_t::BLACK );
}


//-----------------------------------------------------------------------------
// Insert a node into the tree
//-----------------------------------------------------------------------------

template <class T, class I, typename L>
I CUtlRBTree<T, I, L>::InsertAt( I parent, bool leftchild )
{
	I i = NewNode();
	LinkToParent( i, parent, leftchild );
	++m_NumElements;

	Assert( IsValid() );

	return i;
}

template <class T, class I, typename L>
void CUtlRBTree<T, I, L>::LinkToParent( I i, I parent, bool isLeft )
{
	Links_t& elem = Links( i );
	elem.m_Parent = parent;
	elem.m_Left = elem.m_Right = InvalidIndex();
	elem.m_Tag = NodeColor_t::RED;

	/* insert node in tree */
	if ( parent != InvalidIndex() )
	{
		if ( isLeft )
			Links( parent ).m_Left = i;
		else
			Links( parent ).m_Right = i;
	}
	else
		m_Root = i;

	InsertRebalance( i );
}

//-----------------------------------------------------------------------------
// Rebalance the tree after a deletion
//-----------------------------------------------------------------------------

template <class T, class I, typename L>
void CUtlRBTree<T, I, L>::RemoveRebalance( I elem )
{
	while ( elem != m_Root && IsBlack( elem ) )
	{
		I parent = Parent( elem );

		// If elem is the left child of the parent
		if ( elem == LeftChild( parent ) )
		{
			// Get our sibling
			I sibling = RightChild( parent );
			if ( IsRed( sibling ) )
			{
				SetColor( sibling, NodeColor_t::BLACK );
				SetColor( parent, NodeColor_t::RED );
				RotateLeft( parent );

				// We may have a new parent now
				parent = Parent( elem );
				sibling = RightChild( parent );
			}
			if ( ( IsBlack( LeftChild( sibling ) ) ) && ( IsBlack( RightChild( sibling ) ) ) )
			{
				if ( sibling != InvalidIndex() )
					SetColor( sibling, NodeColor_t::RED );
				elem = parent;
			}
			else
			{
				if ( IsBlack( RightChild( sibling ) ) )
				{
					SetColor( LeftChild( sibling ), NodeColor_t::BLACK );
					SetColor( sibling, NodeColor_t::RED );
					RotateRight( sibling );

					// rotation may have changed this
					parent = Parent( elem );
					sibling = RightChild( parent );
				}
				SetColor( sibling, Color( parent ) );
				SetColor( parent, NodeColor_t::BLACK );
				SetColor( RightChild( sibling ), NodeColor_t::BLACK );
				RotateLeft( parent );
				elem = m_Root;
			}
		}
		else
		{
			// Elem is the right child of the parent
			I sibling = LeftChild( parent );
			if ( IsRed( sibling ) )
			{
				SetColor( sibling, NodeColor_t::BLACK );
				SetColor( parent, NodeColor_t::RED );
				RotateRight( parent );

				// We may have a new parent now
				parent = Parent( elem );
				sibling = LeftChild( parent );
			}
			if ( ( IsBlack( RightChild( sibling ) ) ) && ( IsBlack( LeftChild( sibling ) ) ) )
			{
				if ( sibling != InvalidIndex() )
					SetColor( sibling, NodeColor_t::RED );
				elem = parent;
			}
			else
			{
				if ( IsBlack( LeftChild( sibling ) ) )
				{
					SetColor( RightChild( sibling ), NodeColor_t::BLACK );
					SetColor( sibling, NodeColor_t::RED );
					RotateLeft( sibling );

					// rotation may have changed this
					parent = Parent( elem );
					sibling = LeftChild( parent );
				}
				SetColor( sibling, Color( parent ) );
				SetColor( parent, NodeColor_t::BLACK );
				SetColor( LeftChild( sibling ), NodeColor_t::BLACK );
				RotateRight( parent );
				elem = m_Root;
			}
		}
	}
	SetColor( elem, NodeColor_t::BLACK );
}

template <class T, class I, typename L>
void CUtlRBTree<T, I, L>::Unlink( I elem )
{
	if ( elem != InvalidIndex() )
	{
		I x, y;

		if ( ( LeftChild( elem ) == InvalidIndex() ) || ( RightChild( elem ) == InvalidIndex() ) )
		{
			/* y has a NIL node as a child */
			y = elem;
		}
		else
		{
			/* find tree successor with a NIL node as a child */
			y = RightChild( elem );
			while ( LeftChild( y ) != InvalidIndex() )
				y = LeftChild( y );
		}

		/* x is y's only child */
		if ( LeftChild( y ) != InvalidIndex() )
			x = LeftChild( y );
		else
			x = RightChild( y );

		/* remove y from the parent chain */
		if ( x != InvalidIndex() )
			SetParent( x, Parent( y ) );
		if ( !IsRoot( y ) )
		{
			if ( IsLeftChild( y ) )
				SetLeftChild( Parent( y ), x );
			else
				SetRightChild( Parent( y ), x );
		}
		else
			m_Root = x;

		// need to store this off now, we'll be resetting y's color
		NodeColor_t ycolor = Color( y );
		if ( y != elem )
		{
			// Standard implementations copy the data around, we cannot here.
			// Hook in y to link to the same stuff elem used to.
			SetParent( y, Parent( elem ) );
			SetRightChild( y, RightChild( elem ) );
			SetLeftChild( y, LeftChild( elem ) );

			if ( !IsRoot( elem ) )
				if ( IsLeftChild( elem ) )
					SetLeftChild( Parent( elem ), y );
				else
					SetRightChild( Parent( elem ), y );
			else
				m_Root = y;

			if ( LeftChild( y ) != InvalidIndex() )
				SetParent( LeftChild( y ), y );
			if ( RightChild( y ) != InvalidIndex() )
				SetParent( RightChild( y ), y );

			SetColor( y, Color( elem ) );
		}

		if ( ( x != InvalidIndex() ) && ( ycolor == NodeColor_t::BLACK ) )
			RemoveRebalance( x );
	}
}

template <class T, class I, typename L>
void CUtlRBTree<T, I, L>::Link( I elem )
{
	if ( elem != InvalidIndex() )
	{
		I parent;
		bool leftchild;

		FindInsertionPosition( Element( elem ), parent, leftchild );

		LinkToParent( elem, parent, leftchild );

		Assert( IsValid() );
	}
}

//-----------------------------------------------------------------------------
// Delete a node from the tree
//-----------------------------------------------------------------------------

template <class T, class I, typename L>
void CUtlRBTree<T, I, L>::RemoveAt( I elem )
{
	if ( elem != InvalidIndex() )
	{
		Unlink( elem );

		FreeNode( elem );
		--m_NumElements;

		Assert( IsValid() );
	}
}


//-----------------------------------------------------------------------------
// remove a node in the tree
//-----------------------------------------------------------------------------

template <class T, class I, typename L> bool CUtlRBTree<T, I, L>::Remove( T const& search )
{
	I node = Find( search );
	if ( node != InvalidIndex() )
	{
		RemoveAt( node );
		return true;
	}
	return false;
}


//-----------------------------------------------------------------------------
// Removes all nodes from the tree
//-----------------------------------------------------------------------------

template <class T, class I, typename L>
void CUtlRBTree<T, I, L>::RemoveAll()
{
	// Have to do some convoluted stuff to invoke the destructor on all
	// valid elements for the multilist case (since we don't have all elements
	// connected to each other in a list).

	if ( m_LastAlloc == m_Elements.InvalidIterator() )
	{
		Assert( m_Root == InvalidIndex() );
		Assert( m_FirstFree == InvalidIndex() );
		Assert( m_NumElements == 0 );
		return;
	}

	for ( auto it = m_Elements.First(); it != m_Elements.InvalidIterator(); it = m_Elements.Next( it ) )
	{
		I i = m_Elements.GetIndex( it );
		if ( IsValidIndex( i ) ) // skip elements in the free list
		{
			Destruct( &Element( i ) );
			SetRightChild( i, m_FirstFree );
			SetLeftChild( i, i );
			m_FirstFree = i;
		}

		if ( it == m_LastAlloc )
			break; // don't destruct elements that haven't ever been constucted
	}

	// Clear everything else out
	m_Root = InvalidIndex();
	// Technically, this iterator could become invalid. It will not, because it's
	// always the same iterator. If we don't clear this here, the state of this
	// container will be invalid after we start inserting elements again.
	m_LastAlloc = m_Elements.InvalidIterator();
	m_FirstFree = InvalidIndex();
	m_NumElements = 0;

	Assert( IsValid() );
}

//-----------------------------------------------------------------------------
// Removes all nodes from the tree and purges memory
//-----------------------------------------------------------------------------

template <class T, class I, typename L>
void CUtlRBTree<T, I, L>::Purge()
{
	RemoveAll();
	m_Elements.Purge();
}


//-----------------------------------------------------------------------------
// iteration
//-----------------------------------------------------------------------------

template <class T, class I, typename L>
I CUtlRBTree<T, I, L>::FirstInorder() const
{
	I i = m_Root;
	while ( LeftChild( i ) != InvalidIndex() )
		i = LeftChild( i );
	return i;
}

template <class T, class I, typename L>
I CUtlRBTree<T, I, L>::NextInorder( I i ) const
{
	// Don't go into an infinite loop if it's a bad index
	Assert( IsValidIndex( i ) );
	if ( !IsValidIndex( i ) )
		return InvalidIndex();

	if ( RightChild( i ) != InvalidIndex() )
	{
		i = RightChild( i );
		while ( LeftChild( i ) != InvalidIndex() )
			i = LeftChild( i );
		return i;
	}

	I parent = Parent( i );
	while ( IsRightChild( i ) )
	{
		i = parent;
		if ( i == InvalidIndex() ) break;
		parent = Parent( i );
	}
	return parent;
}

template <class T, class I, typename L>
I CUtlRBTree<T, I, L>::PrevInorder( I i ) const
{
	// Don't go into an infinite loop if it's a bad index
	Assert( IsValidIndex( i ) );
	if ( !IsValidIndex( i ) )
		return InvalidIndex();

	if ( LeftChild( i ) != InvalidIndex() )
	{
		i = LeftChild( i );
		while ( RightChild( i ) != InvalidIndex() )
			i = RightChild( i );
		return i;
	}

	I parent = Parent( i );
	while ( IsLeftChild( i ) )
	{
		i = parent;
		if ( i == InvalidIndex() ) break;
		parent = Parent( i );
	}
	return parent;
}

template <class T, class I, typename L>
I CUtlRBTree<T, I, L>::LastInorder() const
{
	I i = m_Root;
	while ( RightChild( i ) != InvalidIndex() )
		i = RightChild( i );
	return i;
}

template <class T, class I, typename L>
I CUtlRBTree<T, I, L>::FirstPreorder() const
{
	return m_Root;
}

template <class T, class I, typename L>
I CUtlRBTree<T, I, L>::NextPreorder( I i ) const
{
	if ( LeftChild( i ) != InvalidIndex() )
		return LeftChild( i );

	if ( RightChild( i ) != InvalidIndex() )
		return RightChild( i );

	I parent = Parent( i );
	while ( parent != InvalidIndex() )
	{
		if ( IsLeftChild( i ) && ( RightChild( parent ) != InvalidIndex() ) )
			return RightChild( parent );
		i = parent;
		parent = Parent( parent );
	}
	return InvalidIndex();
}

template <class T, class I, typename L>
I CUtlRBTree<T, I, L>::PrevPreorder( I i ) const
{
	Assert( 0 );  // not implemented yet
	return InvalidIndex();
}

template <class T, class I, typename L>
I CUtlRBTree<T, I, L>::LastPreorder() const
{
	I i = m_Root;
	while ( true )
	{
		while ( RightChild( i ) != InvalidIndex() )
			i = RightChild( i );

		if ( LeftChild( i ) != InvalidIndex() )
			i = LeftChild( i );
		else
			break;
	}
	return i;
}

template <class T, class I, typename L>
I CUtlRBTree<T, I, L>::FirstPostorder() const
{
	I i = m_Root;
	while ( !IsLeaf( i ) )
	{
		if ( LeftChild( i ) )
			i = LeftChild( i );
		else
			i = RightChild( i );
	}
	return i;
}

template <class T, class I, typename L>
I CUtlRBTree<T, I, L>::NextPostorder( I i ) const
{
	I parent = Parent( i );
	if ( parent == InvalidIndex() )
		return InvalidIndex();

	if ( IsRightChild( i ) )
		return parent;

	if ( RightChild( parent ) == InvalidIndex() )
		return parent;

	i = RightChild( parent );
	while ( !IsLeaf( i ) )
	{
		if ( LeftChild( i ) )
			i = LeftChild( i );
		else
			i = RightChild( i );
	}
	return i;
}


template <class T, class I, typename L>
void CUtlRBTree<T, I, L>::Reinsert( I elem )
{
	Unlink( elem );
	Link( elem );
}


//-----------------------------------------------------------------------------
// returns the tree depth (not a very fast operation)
//-----------------------------------------------------------------------------

template <class T, class I, typename L>
int CUtlRBTree<T, I, L>::Depth( I node ) const
{
	if ( node == InvalidIndex() )
		return 0;

	const int depthright = Depth( RightChild( node ) );
	const int depthleft = Depth( LeftChild( node ) );
	return Max( depthright, depthleft ) + 1;
}

//-----------------------------------------------------------------------------
// Makes sure the tree is valid after every operation
//-----------------------------------------------------------------------------

template <class T, class I, typename L>
bool CUtlRBTree<T, I, L>::IsValid() const
{
	if ( !Count() )
		return true;

	if ( m_LastAlloc == m_Elements.InvalidIterator() )
		return false;

	if ( !m_Elements.IsIdxValid( Root() ) )
		return false;

	if ( Parent( Root() ) != InvalidIndex() )
		return false;

	return true;
}


//-----------------------------------------------------------------------------
// Sets the less func
//-----------------------------------------------------------------------------

template <class T, class I, typename L>
void CUtlRBTree<T, I, L>::SetLessFunc( const typename CUtlRBTree<T, I, L>::LessFunc_t& func, void* context )
{
	if ( !m_LessFunc )
	{
		m_LessFunc = func;
		m_pLessFuncContext = context;
	}
	else if ( Count() > 0 )
	{
		// need to re-sort the tree here....
		Assert( 0 );
	}
}


//-----------------------------------------------------------------------------
// inserts a node into the tree
//-----------------------------------------------------------------------------

// Inserts a node into the tree, doesn't copy the data in.
template <class T, class I, typename L>
void CUtlRBTree<T, I, L>::FindInsertionPosition( T const& insert, I& parent, bool& leftchild )
{
	Assert( m_LessFunc );

	/* find where node belongs */
	I current = m_Root;
	parent = InvalidIndex();
	leftchild = false;
	while ( current != InvalidIndex() )
	{
		parent = current;
		if ( m_LessFunc( m_pLessFuncContext, insert, Element( current ) ) )
		{
			leftchild = true; current = LeftChild( current );
		}
		else
		{
			leftchild = false; current = RightChild( current );
		}
	}
}

template <class T, class I, typename L>
I CUtlRBTree<T, I, L>::Insert( T const& insert )
{
	// use copy constructor to copy it in
	I parent;
	bool leftchild;
	FindInsertionPosition( insert, parent, leftchild );
	I newNode = InsertAt( parent, leftchild );
	CopyConstruct( &Element( newNode ), insert );
	return newNode;
}

template <class T, class I, typename L>
I CUtlRBTree<T, I, L>::Insert( T&& insert )
{
	// use copy constructor to copy it in
	I parent;
	bool leftchild;
	FindInsertionPosition( insert, parent, leftchild );
	I newNode = InsertAt( parent, leftchild );
	CopyConstruct( &Element( newNode ), std::forward<T>( insert ) );
	return newNode;
}

template <class T, class I, typename L>
void CUtlRBTree<T, I, L>::Insert( const T* pArray, int nItems )
{
	while ( nItems-- )
	{
		Insert( *pArray++ );
	}
}


template <class T, class I, typename L>
I CUtlRBTree<T, I, L>::InsertIfNotFound( T const& insert )
{
	I current = m_Root;
	I parent = InvalidIndex();
	bool leftchild = false;
	while ( current != InvalidIndex() )
	{
		parent = current;
		if ( m_LessFunc( m_pLessFuncContext, insert, Element( current ) ) )
		{
			leftchild = true; current = LeftChild( current );
		}
		else if ( m_LessFunc( m_pLessFuncContext, Element( current ), insert ) )
		{
			leftchild = false; current = RightChild( current );
		}
		else
			// Match found, no insertion
			return InvalidIndex();
	}

	I newNode = InsertAt( parent, leftchild );
	CopyConstruct( &Element( newNode ), insert );
	return newNode;
}

template <class T, class I, typename L>
I CUtlRBTree<T, I, L>::InsertIfNotFound( T&& insert )
{
	I current = m_Root;
	I parent = InvalidIndex();
	bool leftchild = false;
	while ( current != InvalidIndex() )
	{
		parent = current;
		if ( m_LessFunc( m_pLessFuncContext, insert, Element( current ) ) )
		{
			leftchild = true; current = LeftChild( current );
		}
		else if ( m_LessFunc( m_pLessFuncContext, Element( current ), insert ) )
		{
			leftchild = false; current = RightChild( current );
		}
		else
			// Match found, no insertion
			return InvalidIndex();
	}

	I newNode = InsertAt( parent, leftchild );
	CopyConstruct( &Element( newNode ), std::forward<T>( insert ) );
	return newNode;
}


//-----------------------------------------------------------------------------
// finds a node in the tree
//-----------------------------------------------------------------------------
template <class T, class I, typename L>
I CUtlRBTree<T, I, L>::Find( T const& search ) const
{
	Assert( m_LessFunc );

	I current = m_Root;
	while ( current != InvalidIndex() )
	{
		if ( m_LessFunc( m_pLessFuncContext, search, Element( current ) ) )
			current = LeftChild( current );
		else if ( m_LessFunc( m_pLessFuncContext, Element( current ), search ) )
			current = RightChild( current );
		else
			break;
	}
	return current;
}


//-----------------------------------------------------------------------------
// swap in place
//-----------------------------------------------------------------------------
template <class T, class I, typename L>
void CUtlRBTree<T, I, L>::Swap( CUtlRBTree< T, I, L >& that )
{
	m_Elements.Swap( that.m_Elements );
	std::swap( m_LessFunc, that.m_LessFunc );
	std::swap( m_pLessFuncContext, that.m_pLessFuncContext );
	std::swap( m_Root, that.m_Root );
	std::swap( m_NumElements, that.m_NumElements );
	std::swap( m_FirstFree, that.m_FirstFree );
	std::swap( m_pElements, that.m_pElements );
	std::swap( m_LastAlloc, that.m_LastAlloc );
	Assert( IsValid() );
	Assert( m_Elements.IsValidIterator( m_LastAlloc ) || ( m_NumElements == 0 && m_FirstFree == InvalidIndex() ) );
}


#endif // UTLSYMBOL_H