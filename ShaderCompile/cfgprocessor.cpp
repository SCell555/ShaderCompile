//====== Copyright c 1996-2007, Valve Corporation, All rights reserved. =======//
//
// Purpose:
//
// $NoKeywords: $
//
//=============================================================================//

#define WIN32_LEAN_AND_MEAN
#define NOWINRES
#define NOSERVICE
#define NOMCX
#define NOIME
#define NOMINMAX

#include "cfgprocessor.h"
#include "d3dxfxc.h"

#include "utlbuffer.h"
#include <algorithm>
#include <charconv>
#include <cstdarg>
#include <ctime>
#include <filesystem>
#include <map>
#include <numeric>
#include <set>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>
#include <fstream>
#include <inttypes.h>

#include "gsl/narrow"
#include "termcolor/style.hpp"
#include "termcolors.hpp"
#include "strmanip.hpp"
#include "shaderparser.h"

// Type conversions should be controlled by programmer explicitly - shadercompile makes use of 64-bit integer arithmetics
#pragma warning( error : 4244 )

namespace clr
{
	static constexpr auto grey = _internal::ansi_color( color( 200, 200, 200 ) );
}

//////////////////////////////////////////////////////////////////////////
//
// Define class
//
//////////////////////////////////////////////////////////////////////////

class Define
{
public:
	explicit Define( const std::string& szName, int min, int max, bool bStatic )
		: m_sName( szName ), m_min( min ), m_max( max ), m_bStatic( bStatic )
	{
	}

public:
	[[nodiscard]] const std::string& Name() const noexcept { return m_sName; }
	[[nodiscard]] int Min() const noexcept { return m_min; }
	[[nodiscard]] int Max() const noexcept { return m_max; }
	[[nodiscard]] bool IsStatic() const noexcept { return m_bStatic; }

protected:
	std::string m_sName;
	int m_min, m_max;
	bool m_bStatic;
};

//////////////////////////////////////////////////////////////////////////
//
// Expression parser
//
//////////////////////////////////////////////////////////////////////////

class IEvaluationContext
{
public:
	virtual ~IEvaluationContext()													= default;
	virtual int GetVariableValue( int nSlot ) const noexcept						= 0;
	virtual const std::string& GetVariableName( int nSlot ) const noexcept			= 0;
	virtual int GetVariableSlot( const std::string& szVariableName ) const noexcept	= 0;
};

class IExpression
{
public:
	virtual ~IExpression()																			= default;
	virtual int Evaluate( const IEvaluationContext* pCtx ) const noexcept							= 0;
	virtual void Print( const IEvaluationContext* pCtx ) const										= 0;
	virtual std::string Build( const std::string& pPrefix, const IEvaluationContext* pCtx ) const	= 0;
	virtual bool IsValid() const																	= 0;
};

#define EVAL int Evaluate( [[maybe_unused]] const IEvaluationContext* pCtx ) const noexcept override
#define PRNT void Print( [[maybe_unused]] const IEvaluationContext* pCtx ) const override
#define BUILD std::string Build( [[maybe_unused]] const std::string& pPrefix, [[maybe_unused]] const IEvaluationContext* pCtx ) const override
#define CHECK bool IsValid() const override

class CExprConstant : public IExpression
{
public:
	CExprConstant( int value ) noexcept : m_value( value ) {}
	EVAL { return m_value; }
	PRNT
	{
		std::cout << clr::green << m_value << clr::reset;
	}
	BUILD
	{
		return std::to_string( m_value );
	}
	CHECK
	{
		return true;
	}

private:
	int m_value;
};

class CExprVariable : public IExpression
{
public:
	CExprVariable( int nSlot ) noexcept : m_nSlot( nSlot ) {}
	EVAL { return m_nSlot >= 0 ? pCtx->GetVariableValue( m_nSlot ) : 0; };
	PRNT
	{
		if ( m_nSlot >= 0 )
			std::cout << clr::blue << pCtx->GetVariableName( m_nSlot ) << clr::reset;
		else
			std::cout << clr::red << "**@**" << clr::reset;
	}
	BUILD
	{
		if ( m_nSlot >= 0 )
			return pPrefix + pCtx->GetVariableName( m_nSlot );
		else
			return {};
	}
	CHECK
	{
		return m_nSlot >= 0;
	}

private:
	int m_nSlot;
};

class CExprUnary : public IExpression
{
public:
	CExprUnary( IExpression* x ) : m_x( x ) {}

protected:
	IExpression* m_x;
};

#define BEGIN_EXPR_UNARY( className )         \
	class className final : public CExprUnary \
	{                                         \
	public:                                   \
		using CExprUnary::CExprUnary;

#define END_EXPR_UNARY() };

BEGIN_EXPR_UNARY( CExprUnary_Negate )
	EVAL
	{
		return !m_x->Evaluate( pCtx );
	}
	PRNT
	{
		std::cout << clr::grey << "!";
		m_x->Print( pCtx );
	}
	BUILD
	{
		return "!" + m_x->Build( pPrefix, pCtx );
	}
	CHECK
	{
		return m_x->IsValid();
	}
END_EXPR_UNARY()

class CExprBinary : public IExpression
{
public:
	CExprBinary( IExpression* x = nullptr, IExpression* y = nullptr ) noexcept : m_x( x ), m_y( y ) {}
	[[nodiscard]] virtual int Priority() const noexcept = 0;

	void SetX( IExpression* x ) noexcept { m_x = x; }
	void SetY( IExpression* y ) noexcept { m_y = y; }
	IExpression* GetY() const noexcept { return m_y; }

	CHECK
	{
		return m_x->IsValid() && m_y->IsValid();
	}
protected:
	IExpression* m_x;
	IExpression* m_y;
};

#define BEGIN_EXPR_BINARY( className )              \
	class className final : public CExprBinary      \
	{                                               \
	public:                                         \
		using CExprBinary::CExprBinary;

#define EXPR_BINARY_PRIORITY( nPriority ) int Priority() const noexcept override { return nPriority; }
#define END_EXPR_BINARY() };

BEGIN_EXPR_BINARY( CExprBinary_And )
	EVAL
	{
		return m_x->Evaluate( pCtx ) && m_y->Evaluate( pCtx );
	}
	PRNT
	{
		std::cout << clr::grey << "( ";
		m_x->Print( pCtx );
		std::cout << clr::grey << " && ";
		m_y->Print( pCtx );
		std::cout << clr::grey << " )" << clr::reset;
	}
	BUILD
	{
		return "( " + m_x->Build( pPrefix, pCtx ) + " && " + m_y->Build( pPrefix, pCtx ) + " )";
	}
	EXPR_BINARY_PRIORITY( 1 );
END_EXPR_BINARY()

BEGIN_EXPR_BINARY( CExprBinary_Or )
	EVAL
	{
		return m_x->Evaluate( pCtx ) || m_y->Evaluate( pCtx );
	}
	PRNT
	{
		std::cout << clr::grey << "( ";
		m_x->Print( pCtx );
		std::cout << clr::grey << " || ";
		m_y->Print( pCtx );
		std::cout << clr::grey << " )" << clr::reset;
	}
	BUILD
	{
		return "( " + m_x->Build( pPrefix, pCtx ) + " || " + m_y->Build( pPrefix, pCtx ) + " )";
	}
	EXPR_BINARY_PRIORITY( 2 );
END_EXPR_BINARY()

BEGIN_EXPR_BINARY( CExprBinary_Eq )
	EVAL
	{
		return m_x->Evaluate( pCtx ) == m_y->Evaluate( pCtx );
	}
	PRNT
	{
		std::cout << clr::grey << "( ";
		m_x->Print( pCtx );
		std::cout << clr::grey << " == ";
		m_y->Print( pCtx );
		std::cout << clr::grey << " )" << clr::reset;
	}
	BUILD
	{
		return "( " + m_x->Build( pPrefix, pCtx ) + " == " + m_y->Build( pPrefix, pCtx ) + " )";
	}
	EXPR_BINARY_PRIORITY( 0 );
END_EXPR_BINARY()

BEGIN_EXPR_BINARY( CExprBinary_Neq )
	EVAL
	{
		return m_x->Evaluate( pCtx ) != m_y->Evaluate( pCtx );
	}
	PRNT
	{
		std::cout << clr::grey << "( ";
		m_x->Print( pCtx );
		std::cout << clr::grey << " != ";
		m_y->Print( pCtx );
		std::cout << clr::grey << " )" << clr::reset;
	}
	BUILD
	{
		return "( " + m_x->Build( pPrefix, pCtx ) + " != " + m_y->Build( pPrefix, pCtx ) + " )";
	}
	EXPR_BINARY_PRIORITY( 0 );
END_EXPR_BINARY()

BEGIN_EXPR_BINARY( CExprBinary_G )
	EVAL
	{
		return m_x->Evaluate( pCtx ) > m_y->Evaluate( pCtx );
	}
	PRNT
	{
		std::cout << clr::grey << "( ";
		m_x->Print( pCtx );
		std::cout << clr::grey << " > ";
		m_y->Print( pCtx );
		std::cout << clr::grey << " )" << clr::reset;
	}
	BUILD
	{
		return "( " + m_x->Build( pPrefix, pCtx ) + " > " + m_y->Build( pPrefix, pCtx ) + " )";
	}
	EXPR_BINARY_PRIORITY( 0 );
END_EXPR_BINARY()

BEGIN_EXPR_BINARY( CExprBinary_Ge )
	EVAL
	{
		return m_x->Evaluate( pCtx ) >= m_y->Evaluate( pCtx );
	}
	PRNT
	{
		std::cout << clr::grey << "( ";
		m_x->Print( pCtx );
		std::cout << clr::grey << " >= ";
		m_y->Print( pCtx );
		std::cout << clr::grey << " )" << clr::reset;
	}
	BUILD
	{
		return "( " + m_x->Build( pPrefix, pCtx ) + " >= " + m_y->Build( pPrefix, pCtx ) + " )";
	}
	EXPR_BINARY_PRIORITY( 0 );
END_EXPR_BINARY()

BEGIN_EXPR_BINARY( CExprBinary_L )
	EVAL
	{
		return m_x->Evaluate( pCtx ) < m_y->Evaluate( pCtx );
	}
	PRNT
	{
		std::cout << clr::grey << "( ";
		m_x->Print( pCtx );
		std::cout << clr::grey << " < ";
		m_y->Print( pCtx );
		std::cout << clr::grey << " )" << clr::reset;
	}
	BUILD
	{
		return "( " + m_x->Build( pPrefix, pCtx ) + " < " + m_y->Build( pPrefix, pCtx ) + " )";
	}
	EXPR_BINARY_PRIORITY( 0 );
END_EXPR_BINARY()

BEGIN_EXPR_BINARY( CExprBinary_Le )
	EVAL
	{
		return m_x->Evaluate( pCtx ) <= m_y->Evaluate( pCtx );
	}
	PRNT
	{
		std::cout << clr::grey << "( ";
		m_x->Print( pCtx );
		std::cout << clr::grey << " <= ";
		m_y->Print( pCtx );
		std::cout << clr::grey << " )" << clr::reset;
	}
	BUILD
	{
		return "( " + m_x->Build( pPrefix, pCtx ) + " <= " + m_y->Build( pPrefix, pCtx ) + " )";
	}
	EXPR_BINARY_PRIORITY( 0 );
END_EXPR_BINARY()

class CComplexExpression : public IExpression
{
public:
	CComplexExpression( IEvaluationContext* pCtx ) noexcept
		: m_pRoot( nullptr ), m_pContext( pCtx ), m_pDefFalse( nullptr )
	{
	}
	~CComplexExpression() override { Clear(); }

	void Parse( std::string szExpression );
	void Clear() noexcept;

public:
	EVAL { return m_pRoot ? m_pRoot->Evaluate( pCtx ? pCtx : m_pContext ) : 0; }
	PRNT
	{
		std::cout << clr::grey << "[ ";
		if ( m_pRoot )
			m_pRoot->Print( pCtx ? pCtx : m_pContext );
		else
			std::cout << clr::red << "**NEXPR**";
		std::cout << clr::grey << " ]" << clr::reset << std::endl;
	}
	BUILD
	{
		if ( m_pRoot )
			return m_pRoot->Build( pPrefix, pCtx ? pCtx : m_pContext );
		return {};
	}
	CHECK
	{
		return m_pRoot && m_pRoot != m_pDefFalse && m_pRoot->IsValid();
	}

protected:
	IExpression* ParseTopLevel( char*& szExpression );
	IExpression* ParseInternal( char*& szExpression );
	template <typename T, typename... Args>
	auto Expression( Args&&... args ) -> std::enable_if_t<std::conjunction_v<std::is_base_of<IExpression, T>, std::is_constructible<T, Args...>>, T*>;
	IExpression* AbortedParse( char* &szExpression ) const noexcept
	{
		*szExpression = 0;
		return m_pDefFalse;
	}

protected:
	std::vector<std::unique_ptr<IExpression>> m_arrAllExpressions;
	IExpression* m_pRoot;
	IEvaluationContext* m_pContext;

	IExpression* m_pDefFalse;
};

#undef BEGIN_EXPR_UNARY
#undef BEGIN_EXPR_BINARY

#undef END_EXPR_UNARY
#undef END_EXPR_BINARY

#undef EXPR_BINARY_PRIORITY

#undef EVAL
#undef PRNT
#undef BUILD
#undef CHECK

void CComplexExpression::Parse( std::string szExpression )
{
	Clear();

	m_pDefFalse = Expression<CExprConstant>( 0 );

	char* expression		= szExpression.data();
	char* const szExpectEnd	= expression + szExpression.length();
	char* szParse			= expression;
	m_pRoot					= ParseTopLevel( szParse );

	if ( szParse != szExpectEnd )
		m_pRoot = m_pDefFalse;
}

IExpression* CComplexExpression::ParseTopLevel( char* &szExpression )
{
	std::vector<CExprBinary*> exprStack;
	IExpression* pFirstToken = ParseInternal( szExpression );

	for ( ;; )
	{
		// Skip whitespace
		while ( *szExpression && isspace( *szExpression ) )
			++szExpression;

		// End of binary expression
		if ( !*szExpression || ( *szExpression == ')' ) )
			break;

		// Determine the binary expression type
		CExprBinary* pBinaryExpression = nullptr;

		if ( !strncmp( szExpression, "&&", 2 ) )
		{
			pBinaryExpression = Expression<CExprBinary_And>();
			szExpression += 2;
		}
		else if ( !strncmp( szExpression, "||", 2 ) )
		{
			pBinaryExpression = Expression<CExprBinary_Or>();
			szExpression += 2;
		}
		else if ( !strncmp( szExpression, ">=", 2 ) )
		{
			pBinaryExpression = Expression<CExprBinary_Ge>();
			szExpression += 2;
		}
		else if ( !strncmp( szExpression, "<=", 2 ) )
		{
			pBinaryExpression = Expression<CExprBinary_Le>();
			szExpression += 2;
		}
		else if ( !strncmp( szExpression, "==", 2 ) )
		{
			pBinaryExpression = Expression<CExprBinary_Eq>();
			szExpression += 2;
		}
		else if ( !strncmp( szExpression, "!=", 2 ) )
		{
			pBinaryExpression = Expression<CExprBinary_Neq>();
			szExpression += 2;
		}
		else if ( *szExpression == '>' )
		{
			pBinaryExpression = Expression<CExprBinary_G>();
			++szExpression;
		}
		else if ( *szExpression == '<' )
		{
			pBinaryExpression = Expression<CExprBinary_L>();
			++szExpression;
		}
		else
			return AbortedParse( szExpression );

		pBinaryExpression->SetY( ParseInternal( szExpression ) );

		// Figure out the expression priority
		const int nPriority    = pBinaryExpression->Priority();
		IExpression* pLastExpr = pFirstToken;
		while ( !exprStack.empty() )
		{
			CExprBinary* pStickTo = exprStack.back();
			pLastExpr             = pStickTo;

			if ( nPriority > pStickTo->Priority() )
				exprStack.pop_back();
			else
				break;
		}

		if ( !exprStack.empty() )
		{
			CExprBinary* pStickTo  = exprStack.back();
			pBinaryExpression->SetX( pStickTo->GetY() );
			pStickTo->SetY( pBinaryExpression );
		}
		else
			pBinaryExpression->SetX( pLastExpr );

		exprStack.push_back( pBinaryExpression );
	}

	// Tip-of-the-tree retrieval
	{
		IExpression* pLastExpr = pFirstToken;
		while ( !exprStack.empty() )
		{
			pLastExpr = exprStack.back();
			exprStack.pop_back();
		}

		return pLastExpr;
	}
}

IExpression* CComplexExpression::ParseInternal( char* &szExpression )
{
	// Skip whitespace
	while ( *szExpression && isspace( *szExpression ) )
		++szExpression;

	if ( !*szExpression )
		return AbortedParse( szExpression );

	if ( isdigit( *szExpression ) )
	{
		const int lValue = strtol( szExpression, &szExpression, 10 );
		return Expression<CExprConstant>( lValue );
	}
	else if ( !strncmp( szExpression, "defined", 7 ) )
	{
		szExpression += 7;
		IExpression* pNext = ParseInternal( szExpression );
		return Expression<CExprConstant>( pNext->Evaluate( m_pContext ) );
	}
	else if ( *szExpression == '(' )
	{
		++szExpression;
		IExpression* pBracketed = ParseTopLevel( szExpression );
		if ( ')' == *szExpression )
		{
			++szExpression;
			return pBracketed;
		}

		return AbortedParse( szExpression );
	}
	else if ( *szExpression == '$' )
	{
		size_t lenVariable = 0;
		for ( char* szEndVar = szExpression + 1; *szEndVar; ++szEndVar, ++lenVariable )
		{
			if ( !isalnum( *szEndVar ) )
			{
				switch ( *szEndVar )
				{
				case '_':
					break;
				default:
					goto parsed_variable_name;
				}
			}
		}

	parsed_variable_name:
		const int nSlot = m_pContext->GetVariableSlot( std::string( szExpression + 1, lenVariable ) );
		szExpression += lenVariable + 1;

		return Expression<CExprVariable>( nSlot );
	}
	else if ( *szExpression == '!' )
	{
		++szExpression;
		IExpression* pNext = ParseInternal( szExpression );
		return Expression<CExprUnary_Negate>( pNext );
	}

	return AbortedParse( szExpression );
}

template <typename T, typename... Args>
auto CComplexExpression::Expression( Args&&... args ) -> std::enable_if_t<std::conjunction_v<std::is_base_of<IExpression, T>, std::is_constructible<T, Args...>>, T*>
{
	return static_cast<T*>( m_arrAllExpressions.emplace_back( std::make_unique<T>( std::forward<Args>( args )... ) ).get() );
}

void CComplexExpression::Clear() noexcept
{
	m_arrAllExpressions.clear();
	m_pRoot = nullptr;
}

//////////////////////////////////////////////////////////////////////////
//
// Combo Generator class
//
//////////////////////////////////////////////////////////////////////////

class ComboGenerator : public IEvaluationContext
{
public:
	ComboGenerator() = default;
	ComboGenerator( const ComboGenerator& ) = default;
	ComboGenerator( ComboGenerator&& old ) noexcept : m_arrDefines( std::move( old.m_arrDefines ) ), m_mapDefines( std::move( old.m_mapDefines ) ), m_arrVarSlots( std::move( old.m_arrVarSlots ) ) {}

	void AddDefine( const Define& df );
	[[nodiscard]] const Define* GetDefinesBase() const noexcept { return m_arrDefines.data(); }
	[[nodiscard]] const Define* GetDefinesEnd() const noexcept { return m_arrDefines.data() + m_arrDefines.size(); }
	[[nodiscard]] size_t DefineCount() const noexcept { return m_arrDefines.size(); }

	[[nodiscard]] uint64_t NumCombos() const noexcept;
	[[nodiscard]] uint64_t NumCombos( bool bStaticCombos ) const noexcept;

	// IEvaluationContext
public:
	[[nodiscard]] int GetVariableValue( int nSlot ) const noexcept override { return m_arrVarSlots[nSlot]; }
	[[nodiscard]] const std::string& GetVariableName( int nSlot ) const noexcept override { return m_arrDefines[nSlot].Name(); }
	[[nodiscard]] int GetVariableSlot( const std::string& szVariableName ) const noexcept override
	{
		const auto& find = m_mapDefines.find( szVariableName );
		if ( m_mapDefines.end() != find )
			return find->second;
		return -1;
	}

protected:
	std::vector<Define> m_arrDefines;
	robin_hood::unordered_node_map<std::string, int> m_mapDefines;
	std::vector<int> m_arrVarSlots;
};

void ComboGenerator::AddDefine( const Define& df )
{
	m_mapDefines.emplace( df.Name(), gsl::narrow<int>( m_arrDefines.size() ) );
	m_arrDefines.emplace_back( df );
	m_arrVarSlots.emplace_back( 1 );
}

uint64_t ComboGenerator::NumCombos() const noexcept
{
	return std::transform_reduce( m_arrDefines.cbegin(), m_arrDefines.cend(), 1ULL,
		[]( const uint64_t& a, const uint64_t& b ) noexcept { return a * b; },
		[]( const Define& d ) noexcept { return static_cast<uint64_t>( d.Max() ) - d.Min() + 1ULL; } );
}

uint64_t ComboGenerator::NumCombos( bool bStaticCombos ) const noexcept
{
	return std::transform_reduce( m_arrDefines.cbegin(), m_arrDefines.cend(), 1ULL,
		[]( const uint64_t& a, const uint64_t& b ) noexcept { return a * b; },
		[bStaticCombos]( const Define& d ) noexcept { return d.IsStatic() == bStaticCombos ? static_cast<uint64_t>( d.Max() ) - d.Min() + 1ULL : 1ULL; } );
}

namespace ConfigurationProcessing
{
class CfgEntry
{
public:
	CfgEntry() noexcept : m_szName( "" ), m_szShaderSrc( "" ), m_pCg( nullptr ), m_pExpr( nullptr )
	{
		memset( &m_eiInfo, 0, sizeof( m_eiInfo ) );
	}

	bool operator<( const CfgEntry& x ) const noexcept { return m_pCg->NumCombos() < x.m_pCg->NumCombos(); }

	std::string_view m_szName;
	std::string_view m_szShaderSrc;
	std::unique_ptr<ComboGenerator> m_pCg;
	std::unique_ptr<CComplexExpression> m_pExpr;

	CfgProcessor::CfgEntryInfo m_eiInfo;
};

static robin_hood::unordered_node_set<std::string> s_strPool;
static std::multiset<CfgEntry> s_setEntries;

class ComboHandleImpl : public IEvaluationContext
{
public:
	uint64_t m_iTotalCommand;
	uint64_t m_iComboNumber;
	uint64_t m_numCombos;
	const CfgEntry* m_pEntry;

public:
	ComboHandleImpl() noexcept : m_iTotalCommand( 0 ), m_iComboNumber( 0 ), m_numCombos( 0 ), m_pEntry( nullptr ) {}
	ComboHandleImpl( const ComboHandleImpl& ) = default;

	// IEvaluationContext
private:
	std::vector<int> m_arrVarSlots;

public:
	int GetVariableValue( int nSlot ) const noexcept override { return m_arrVarSlots[nSlot]; }
	const std::string& GetVariableName( int nSlot ) const noexcept override { return m_pEntry->m_pCg->GetVariableName( nSlot ); }
	int GetVariableSlot( const std::string& szVariableName ) const noexcept override { return m_pEntry->m_pCg->GetVariableSlot( szVariableName ); }

	// External implementation
public:
	bool Initialize( uint64_t iTotalCommand, const CfgEntry* pEntry );
	bool AdvanceCommands( uint64_t& riAdvanceMore ) noexcept;
	bool NextNotSkipped( uint64_t iTotalCommand ) noexcept;
	bool IsSkipped() const noexcept { return m_pEntry->m_pExpr->Evaluate( this ) != 0; }
	CfgProcessor::ComboBuildCommand BuildCommand() const;
	void FormatCommandHumanReadable( gsl::span<char> pchBuffer ) const;
};

static std::map<uint64_t, ComboHandleImpl> s_mapComboCommands;

bool ComboHandleImpl::Initialize( uint64_t iTotalCommand, const CfgEntry* pEntry )
{
	m_iTotalCommand = iTotalCommand;
	m_pEntry        = pEntry;
	m_numCombos     = m_pEntry->m_pCg->NumCombos();

	// Defines
	const Define* const pDefVars    = m_pEntry->m_pCg->GetDefinesBase();
	const Define* const pDefVarsEnd = m_pEntry->m_pCg->GetDefinesEnd();

	// Set all the variables to max values
	for ( const Define* pSetDef = pDefVars; pSetDef < pDefVarsEnd; ++pSetDef )
		m_arrVarSlots.emplace_back( pSetDef->Max() );

	m_iComboNumber = m_numCombos - 1;
	return true;
}

bool ComboHandleImpl::AdvanceCommands( uint64_t& riAdvanceMore ) noexcept
{
	if ( !riAdvanceMore )
		return true;

	// Get the pointers
	int* const pnValues    = m_arrVarSlots.data();
	int* const pnValuesEnd = pnValues + m_arrVarSlots.size();
	int* pSetValues;

	// Defines
	const Define* const pDefVars = m_pEntry->m_pCg->GetDefinesBase();
	const Define* pSetDef;

	if ( m_iComboNumber < riAdvanceMore )
	{
		riAdvanceMore -= m_iComboNumber;
		return false;
	}

	// Do the advance
	m_iTotalCommand += riAdvanceMore;
	m_iComboNumber -= riAdvanceMore;
	for ( pSetValues = pnValues, pSetDef = pDefVars; ( pSetValues < pnValuesEnd ) && ( riAdvanceMore > 0 ); ++pSetValues, ++pSetDef )
	{
		riAdvanceMore += ( static_cast<uint64_t>( pSetDef->Max() ) - *pSetValues );
		*pSetValues = pSetDef->Max();

		const int iInterval = ( pSetDef->Max() - pSetDef->Min() + 1 );
		*pSetValues -= static_cast<int>( riAdvanceMore % iInterval );
		riAdvanceMore /= iInterval;
	}

	return true;
}

bool ComboHandleImpl::NextNotSkipped( uint64_t iTotalCommand ) noexcept
{
	// Get the pointers
	int* const pnValues    = m_arrVarSlots.data();
	int* const pnValuesEnd = pnValues + m_arrVarSlots.size();
	int* pSetValues;

	// Defines
	const Define* const pDefVars = m_pEntry->m_pCg->GetDefinesBase();
	const Define* pSetDef;

	// Go ahead and run the iterations
next_combo_iteration:
	if ( m_iTotalCommand + 1 >= iTotalCommand || !m_iComboNumber )
		return false;

	--m_iComboNumber;
	++m_iTotalCommand;

	// Do a next iteration
	for ( pSetValues = pnValues, pSetDef = pDefVars; pSetValues < pnValuesEnd; ++pSetValues, ++pSetDef )
	{
		if ( --*pSetValues >= pSetDef->Min() )
			goto have_combo_iteration;

		*pSetValues = pSetDef->Max();
	}

	return false;

have_combo_iteration:
	if ( m_pEntry->m_pExpr->Evaluate( this ) )
		goto next_combo_iteration;

	return true;
}

static thread_local robin_hood::unordered_node_set<std::string> s_tlPool;
template <typename T>
static std::string_view String( const T& str )
{
	return *s_tlPool.emplace( str ).first;
}

CfgProcessor::ComboBuildCommand ComboHandleImpl::BuildCommand() const
{
	// Get the pointers
	const int* const pnValues    = m_arrVarSlots.data();
	const int* const pnValuesEnd = pnValues + m_arrVarSlots.size();
	const int* pSetValues;

	// Defines
	const Define* const pDefVars = m_pEntry->m_pCg->GetDefinesBase();
	const Define* const pDefVarsEnd = m_pEntry->m_pCg->GetDefinesEnd();
	const Define* pSetDef;

	CfgProcessor::ComboBuildCommand command{ m_pEntry->m_szShaderSrc, m_pEntry->m_eiInfo.m_szShaderVersion };
	command.defines.reserve( m_pEntry->m_pCg->DefineCount() + 2 );

	char tmpBuf[24]{};
	std::to_chars( std::begin( tmpBuf ), std::end( tmpBuf ), m_iComboNumber, 16 );

	command.defines.emplace_back( "SHADERCOMBO", String( tmpBuf ) );

	char version[16];
	strcpy_s( version, sizeof( version ), m_pEntry->m_eiInfo.m_szShaderVersion.data() );
	_strupr_s( version );
	sprintf_s( tmpBuf, sizeof( tmpBuf ), "SHADER_MODEL_%6.6s", version );

	command.defines.emplace_back( String( tmpBuf ), "1" );

	for ( pSetValues = pnValues, pSetDef = pDefVars; pSetValues < pnValuesEnd && pDefVars < pDefVarsEnd; ++pSetValues, ++pSetDef )
	{
		*std::to_chars( std::begin( tmpBuf ), std::end( tmpBuf ), *pSetValues ).ptr = 0;
		command.defines.emplace_back( pSetDef->Name(), String( tmpBuf ) );
	}

	return command;
}

void ComboHandleImpl::FormatCommandHumanReadable( gsl::span<char> pchBuffer ) const
{
	// Get the pointers
	const int* const pnValues    = m_arrVarSlots.data();
	const int* const pnValuesEnd = pnValues + m_arrVarSlots.size();
	const int* pSetValues;

	// Defines
	const Define* const pDefVars    = m_pEntry->m_pCg->GetDefinesBase();
	const Define* const pDefVarsEnd = m_pEntry->m_pCg->GetDefinesEnd();
	const Define* pSetDef;

	// ------- OnCombo( nCurrentCombo ); ----------
	char version[20];
	strcpy_s( version, sizeof( version ), m_pEntry->m_eiInfo.m_szShaderVersion.data() );
	_strupr_s( version );
	int o = sprintf_s( pchBuffer.data(), pchBuffer.size(),
		"fxc /DCENTROIDMASK=%d /DSHADERCOMBO=%llx /DSHADER_MODEL_%s=1 /T%s /Emain",
		m_pEntry->m_eiInfo.m_nCentroidMask, m_iComboNumber, version, m_pEntry->m_eiInfo.m_szShaderVersion.data() );

	for ( pSetValues = pnValues, pSetDef = pDefVars; pSetValues < pnValuesEnd && pDefVars < pDefVarsEnd; ++pSetValues, ++pSetDef )
		o += sprintf_s( &pchBuffer[o], pchBuffer.size() - o, " /D%s=%d", pSetDef->Name().c_str(), *pSetValues );

	o += sprintf_s( &pchBuffer[o], pchBuffer.size() - o, " %s", m_pEntry->m_szShaderSrc.data() );
	// ------- end of OnCombo ---------------------

	pchBuffer[o] = '\0';
}

std::vector<std::pair<std::string, std::string>> GenerateSkipAsserts( const std::vector<Parser::Combo>& combos, const std::vector<std::string>& skips )
{
	ComboGenerator cg{};
	CComplexExpression exprSkip{ &cg };
	for ( const Parser::Combo& combo : combos )
		cg.AddDefine( Define( combo.name, combo.minVal, combo.maxVal, false ) );

	std::vector<std::pair<std::string, std::string>> asserts;
	for ( const auto& skip : skips )
	{
		exprSkip.Parse( skip );

		if ( !exprSkip.IsValid() )
			continue;

		asserts.emplace_back( exprSkip.Build( {}, nullptr ), exprSkip.Build( "m_n", nullptr ) );
	}

	return asserts;
}

static void SetupConfiguration( const std::vector<CfgProcessor::ShaderConfig>& configs, const std::filesystem::path& root, bool bVerbose )
{
	using namespace std::literals;
	const auto& AddCombos = []( ComboGenerator& cg, const std::vector<Parser::Combo>& combos, bool staticC )
	{
		for ( const Parser::Combo& combo : combos )
			cg.AddDefine( Define( combo.name, combo.minVal, combo.maxVal, staticC ) );
	};

	char baseTemplate[] = { " s_ _ " };

	robin_hood::unordered_node_set<std::string> includes;
	for ( const auto& conf : configs )
	{
		CfgEntry cfg;
		cfg.m_szName = *s_strPool.emplace( conf.name ).first;
		cfg.m_szShaderSrc = *s_strPool.emplace( conf.includes[0] ).first;
		// Combo generator
		cfg.m_pCg = std::make_unique<ComboGenerator>();
		cfg.m_pExpr = std::make_unique<CComplexExpression>( cfg.m_pCg.get() );
		ComboGenerator& cg = *cfg.m_pCg;
		CComplexExpression& exprSkip = *cfg.m_pExpr;

		AddCombos( cg, conf.dynamic_c, false );
		AddCombos( cg, conf.static_c, true );
		exprSkip.Parse( ( std::accumulate( conf.skip.begin(), conf.skip.end(), "("s, []( const std::string& s, const std::string& sk ) { return s + sk + ")||("; } ) + "0)" ) );

		baseTemplate[0] = conf.target[0];
		baseTemplate[3] = conf.version[0];
		baseTemplate[5] = conf.version.size() == 3 ? 'b' : conf.version[1];

		CfgProcessor::CfgEntryInfo& info = cfg.m_eiInfo;
		info.m_szName = cfg.m_szName;
		info.m_szShaderFileName = cfg.m_szShaderSrc;
		info.m_szShaderVersion = *s_strPool.emplace( baseTemplate ).first;
		info.m_numCombos = cg.NumCombos();
		info.m_numDynamicCombos = cg.NumCombos( false );
		info.m_numStaticCombos = cg.NumCombos( true );
		info.m_nCentroidMask = conf.centroid_mask;
		info.m_nCrc32 = conf.crc32;

		s_setEntries.insert( std::move( cfg ) );

		includes.insert( conf.includes.cbegin(), conf.includes.cend() );
	}

	for ( const std::string& file : includes )
	{
		std::ifstream src( root / file, std::ios::binary | std::ios::ate );
		if ( !src )
		{
			std::cout << clr::pinkish << "Can't find \"" << clr::red << file << clr::pinkish << "\"" << std::endl;
			continue;
		}

		if ( bVerbose )
			std::cout << "adding file to cache: \"" << clr::green << file << clr::reset << "\"" << std::endl;

		std::vector<char> data( gsl::narrow<size_t>( src.tellg() ) );
		src.clear();
		src.seekg( 0, std::ios::beg );
		src.read( data.data(), data.size() );

		fileCache.Add( file, std::move( data ) );
	}

	uint64_t nCurrentCommand = 0;
	for ( auto it = s_setEntries.rbegin(), itEnd = s_setEntries.rend(); it != itEnd; ++it )
	{
		// We establish a command mapping for the beginning of the entry
		ComboHandleImpl chi;
		chi.Initialize( nCurrentCommand, &*it );
		s_mapComboCommands.emplace( nCurrentCommand, chi );

		// We also establish mapping by either splitting the
		// combos into 500 intervals or stepping by every 1000 combos.
		const uint64_t iPartStep = std::max( 1000ULL, chi.m_numCombos / 500 );
		for ( uint64_t iRecord = nCurrentCommand + iPartStep; iRecord < nCurrentCommand + chi.m_numCombos; iRecord += iPartStep )
		{
			uint64_t iAdvance = iPartStep;
			chi.AdvanceCommands( iAdvance );
			s_mapComboCommands.emplace( iRecord, chi );
		}

		nCurrentCommand += chi.m_numCombos;
	}

	// Establish the last command terminator
	{
		static CfgEntry s_term;
		s_term.m_eiInfo.m_iCommandStart = s_term.m_eiInfo.m_iCommandEnd = nCurrentCommand;
		s_term.m_eiInfo.m_numCombos = s_term.m_eiInfo.m_numStaticCombos = s_term.m_eiInfo.m_numDynamicCombos = 1;
		s_term.m_eiInfo.m_szName = s_term.m_eiInfo.m_szShaderFileName = "";
		ComboHandleImpl chi;
		chi.m_iTotalCommand = nCurrentCommand;
		chi.m_pEntry = &s_term;
		s_mapComboCommands.emplace( nCurrentCommand, chi );
	}
}
}; // namespace ConfigurationProcessing

namespace CfgProcessor
{
using CPCHI_t = ConfigurationProcessing::ComboHandleImpl;
static CPCHI_t* FromHandle( ComboHandle hCombo ) noexcept
{
	return reinterpret_cast<CPCHI_t*>( hCombo );
}
static ComboHandle AsHandle( CPCHI_t* pImpl ) noexcept
{
	return reinterpret_cast<ComboHandle>( pImpl );
}

void SetupConfiguration( const std::vector<ShaderConfig>& configs, const std::filesystem::path& root, bool bVerbose )
{
	ConfigurationProcessing::SetupConfiguration( configs, root, bVerbose );
}

std::unique_ptr<CfgProcessor::CfgEntryInfo[]> DescribeConfiguration( bool bPrintExpressions )
{
	auto arrEntries = std::make_unique<CfgEntryInfo[]>( ConfigurationProcessing::s_setEntries.size() + 1 );

	CfgEntryInfo* pInfo      = arrEntries.get();
	uint64_t nCurrentCommand = 0;

	for ( auto it = ConfigurationProcessing::s_setEntries.rbegin(), itEnd = ConfigurationProcessing::s_setEntries.rend(); it != itEnd; ++it, ++pInfo )
	{
		const ConfigurationProcessing::CfgEntry& e = *it;
		*pInfo = e.m_eiInfo;

		pInfo->m_iCommandStart    = nCurrentCommand;
		pInfo->m_iCommandEnd      = pInfo->m_iCommandStart + pInfo->m_numCombos;

		const_cast<CfgEntryInfo&>( e.m_eiInfo ) = *pInfo;

		if ( bPrintExpressions )
			e.m_pExpr->Print( nullptr );

		nCurrentCommand += pInfo->m_numCombos;
	}

	// Terminator
	memset( pInfo, 0, sizeof( CfgEntryInfo ) );
	pInfo->m_iCommandStart = nCurrentCommand;
	pInfo->m_iCommandEnd   = nCurrentCommand;

	return arrEntries;
}

static const CPCHI_t& GetLessOrEq( uint64_t& k, const CPCHI_t& v )
{
	auto it = ConfigurationProcessing::s_mapComboCommands.lower_bound( k );
	if ( ConfigurationProcessing::s_mapComboCommands.end() == it )
	{
		if ( ConfigurationProcessing::s_mapComboCommands.empty() )
			return v;
		--it;
	}

	if ( k < it->first )
	{
		if ( ConfigurationProcessing::s_mapComboCommands.begin() == it )
			return v;
		--it;
	}

	k = it->first;
	return it->second;
}

ComboHandle Combo_GetCombo( uint64_t iCommandNumber )
{
	// Find earlier command
	uint64_t iCommandFound = iCommandNumber;
	const CPCHI_t emptyCPCHI;
	const CPCHI_t& chiFound = GetLessOrEq( iCommandFound, emptyCPCHI );

	if ( chiFound.m_iTotalCommand < 0 || chiFound.m_iTotalCommand > iCommandNumber )
		return nullptr;

	// Advance the handle as needed
	CPCHI_t* pImpl = new CPCHI_t( chiFound );

	uint64_t iCommandFoundAdvance = iCommandNumber - iCommandFound;
	pImpl->AdvanceCommands( iCommandFoundAdvance );

	return AsHandle( pImpl );
}

void Combo_GetNext( uint64_t& riCommandNumber, ComboHandle& rhCombo, uint64_t iCommandEnd )
{
	// Combo handle implementation
	CPCHI_t* pImpl = FromHandle( rhCombo );

	if ( !rhCombo )
	{
		// We don't have a combo handle that corresponds to the command

		// Find earlier command
		uint64_t iCommandFound = riCommandNumber;
		const CPCHI_t emptyCPCHI;
		const CPCHI_t& chiFound = GetLessOrEq( iCommandFound, emptyCPCHI );

		if ( !chiFound.m_pEntry || !chiFound.m_pEntry->m_pCg || !chiFound.m_pEntry->m_pExpr || chiFound.m_iTotalCommand < 0 || chiFound.m_iTotalCommand > riCommandNumber )
			return;

		// Advance the handle as needed
		pImpl   = new CPCHI_t( chiFound );
		rhCombo = AsHandle( pImpl );

		uint64_t iCommandFoundAdvance = riCommandNumber - iCommandFound;
		pImpl->AdvanceCommands( iCommandFoundAdvance );

		if ( !pImpl->IsSkipped() )
			return;
	}

	for ( ;; )
	{
		// We have the combo handle now
		if ( pImpl->NextNotSkipped( iCommandEnd ) )
		{
			riCommandNumber = pImpl->m_iTotalCommand;
			return;
		}

		// We failed to get the next combo command (out of range)
		if ( pImpl->m_iTotalCommand + 1 >= iCommandEnd )
		{
			delete pImpl;
			rhCombo         = nullptr;
			riCommandNumber = iCommandEnd;
			return;
		}

		// Otherwise we just have to obtain the next combo handle
		riCommandNumber = pImpl->m_iTotalCommand + 1;

		// Delete the old combo handle
		delete pImpl;
		rhCombo = nullptr;

		// Retrieve the next combo handle data
		uint64_t iCommandLookup = riCommandNumber;
		CPCHI_t emptyCPCHI;
		const CPCHI_t& chiNext = GetLessOrEq( iCommandLookup, emptyCPCHI );
		Assert( iCommandLookup == riCommandNumber && ( chiNext.m_pEntry ) );

		// Set up the new combo handle
		pImpl   = new CPCHI_t( chiNext );
		rhCombo = AsHandle( pImpl );

		if ( !pImpl->IsSkipped() )
			return;
	}
}

ComboBuildCommand Combo_BuildCommand( ComboHandle hCombo )
{
	const auto pImpl = FromHandle( hCombo );
	return pImpl->BuildCommand();
}

void Combo_FormatCommandHumanReadable( ComboHandle hCombo, gsl::span<char> pchBuffer )
{
	const auto pImpl = FromHandle( hCombo );
	pImpl->FormatCommandHumanReadable( pchBuffer );
}

uint64_t Combo_GetCommandNum( ComboHandle hCombo ) noexcept
{
	if ( const auto pImpl = FromHandle( hCombo ) )
		return pImpl->m_iTotalCommand;
	return ~0ULL;
}

uint64_t Combo_GetComboNum( ComboHandle hCombo ) noexcept
{
	if ( const auto pImpl = FromHandle( hCombo ) )
		return pImpl->m_iComboNumber;
	return ~0ULL;
}

const CfgEntryInfo* Combo_GetEntryInfo( ComboHandle hCombo ) noexcept
{
	if ( const auto pImpl = FromHandle( hCombo ) )
		return &pImpl->m_pEntry->m_eiInfo;
	return nullptr;
}

ComboHandle Combo_Alloc( ComboHandle hComboCopyFrom ) noexcept
{
	if ( hComboCopyFrom )
		return AsHandle( new CPCHI_t( *FromHandle( hComboCopyFrom ) ) );
	return AsHandle( new( std::nothrow ) CPCHI_t );
}

void Combo_Assign( ComboHandle hComboDst, ComboHandle hComboSrc )
{
	Assert( hComboDst );
	*FromHandle( hComboDst ) = *FromHandle( hComboSrc );
}

void Combo_Free( ComboHandle& rhComboFree ) noexcept
{
	delete FromHandle( rhComboFree );
	rhComboFree = nullptr;
}
}; // namespace CfgProcessor