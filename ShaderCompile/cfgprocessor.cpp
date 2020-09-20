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
#include <cstdarg>
#include <ctime>
#include <map>
#include <numeric>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>
#include <fstream>

#include "gsl/gsl_narrow"
#include "json/json.h"
#include "termcolor/style.hpp"
#include "termcolors.hpp"
#include "strmanip.hpp"
#include "shaderparser.h"

// Type conversions should be controlled by programmer explicitly - shadercompile makes use of 64-bit integer arithmetics
#pragma warning( error : 4244 )

namespace clr
{
	static const auto grey = _internal::ansi_color( color( 200, 200, 200 ) );
}

namespace PreprocessorDbg
{
	bool s_bNoOutput = true;
}; // namespace

//////////////////////////////////////////////////////////////////////////
//
// Define class
//
//////////////////////////////////////////////////////////////////////////

class Define
{
public:
	explicit Define( char const* szName, int min, int max, bool bStatic )
		: m_sName( szName ), m_min( min ), m_max( max ), m_bStatic( bStatic )
	{
	}

public:
	[[nodiscard]] const char* Name() const { return m_sName.c_str(); }
	[[nodiscard]] int Min() const { return m_min; }
	[[nodiscard]] int Max() const { return m_max; }
	[[nodiscard]] bool IsStatic() const { return m_bStatic; }

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
	virtual ~IEvaluationContext()									= default;
	virtual int GetVariableValue( int nSlot ) const					= 0;
	virtual char const* GetVariableName( int nSlot ) const			= 0;
	virtual int GetVariableSlot( char const* szVariableName ) const	= 0;
};

class IExpression
{
public:
	virtual ~IExpression()										= default;
	virtual int Evaluate( IEvaluationContext* pCtx ) const		= 0;
	virtual void Print( const IEvaluationContext* pCtx ) const	= 0;
};

#define EVAL int Evaluate( [[maybe_unused]] IEvaluationContext* pCtx ) const override
#define PRNT void Print( [[maybe_unused]] const IEvaluationContext* pCtx ) const override

class CExprConstant : public IExpression
{
public:
	CExprConstant( int value ) : m_value( value ) {}
	EVAL { return m_value; }
	PRNT
	{
		std::cout << clr::green << m_value << clr::reset;
	}

private:
	int m_value;
};

class CExprVariable : public IExpression
{
public:
	CExprVariable( int nSlot ) : m_nSlot( nSlot ) {}
	EVAL { return m_nSlot >= 0 ? pCtx->GetVariableValue( m_nSlot ) : 0; };
	PRNT
	{
		if ( m_nSlot >= 0 )
			std::cout << clr::blue << pCtx->GetVariableName( m_nSlot ) << clr::reset;
		else
			std::cout << clr::red << "$**@**" << clr::reset;
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

#define BEGIN_EXPR_UNARY( className )   \
	class className : public CExprUnary \
	{                                   \
	public:                             \
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
END_EXPR_UNARY()

class CExprBinary : public IExpression
{
public:
	CExprBinary( IExpression* x = nullptr, IExpression* y = nullptr ) : m_x( x ), m_y( y ) {}
	[[nodiscard]] virtual int Priority() const = 0;

	void SetX( IExpression* x ) { m_x = x; }
	void SetY( IExpression* y ) { m_y = y; }
	IExpression* GetY() const { return m_y; }

protected:
	IExpression* m_x;
	IExpression* m_y;
};

#define BEGIN_EXPR_BINARY( className )              \
	class className : public CExprBinary            \
	{                                               \
	public:                                         \
		using CExprBinary::CExprBinary;

#define EXPR_BINARY_PRIORITY( nPriority ) int Priority() const override { return nPriority; }
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
	EXPR_BINARY_PRIORITY( 0 );
END_EXPR_BINARY()

class CComplexExpression : public IExpression
{
public:
	CComplexExpression( IEvaluationContext* pCtx )
		: m_pRoot( nullptr ), m_pContext( pCtx )
		, m_pDefTrue( nullptr ), m_pDefFalse( nullptr )
	{
	}
	~CComplexExpression() override { Clear(); }

	void Parse( char const* szExpression );
	void Clear();

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

protected:
	IExpression* ParseTopLevel( char*& szExpression );
	IExpression* ParseInternal( char*& szExpression );
	IExpression* Allocated( IExpression* pExpression );
	IExpression* AbortedParse( char*& szExpression ) const
	{
		*szExpression = 0;
		return m_pDefFalse;
	}

protected:
	std::vector<IExpression*> m_arrAllExpressions;
	IExpression* m_pRoot;
	IEvaluationContext* m_pContext;

	IExpression* m_pDefTrue;
	IExpression* m_pDefFalse;
};

void CComplexExpression::Parse( char const* szExpression )
{
	Clear();

	m_pDefTrue  = Allocated( new CExprConstant( 1 ) );
	m_pDefFalse = Allocated( new CExprConstant( 0 ) );

	m_pRoot = m_pDefFalse;

	if ( szExpression )
	{
		std::string qs( szExpression );
		char* expression  = qs.data();
		char* szExpectEnd = expression + qs.length();
		char* szParse     = expression;
		m_pRoot           = ParseTopLevel( szParse );

		if ( szParse != szExpectEnd )
			m_pRoot = m_pDefFalse;
	}
}

IExpression* CComplexExpression::ParseTopLevel( char*& szExpression )
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
			pBinaryExpression = new CExprBinary_And();
			szExpression += 2;
		}
		else if ( !strncmp( szExpression, "||", 2 ) )
		{
			pBinaryExpression = new CExprBinary_Or();
			szExpression += 2;
		}
		else if ( !strncmp( szExpression, ">=", 2 ) )
		{
			pBinaryExpression = new CExprBinary_Ge();
			szExpression += 2;
		}
		else if ( !strncmp( szExpression, "<=", 2 ) )
		{
			pBinaryExpression = new CExprBinary_Le();
			szExpression += 2;
		}
		else if ( !strncmp( szExpression, "==", 2 ) )
		{
			pBinaryExpression = new CExprBinary_Eq();
			szExpression += 2;
		}
		else if ( !strncmp( szExpression, "!=", 2 ) )
		{
			pBinaryExpression = new CExprBinary_Neq();
			szExpression += 2;
		}
		else if ( *szExpression == '>' )
		{
			pBinaryExpression = new CExprBinary_G();
			++szExpression;
		}
		else if ( *szExpression == '<' )
		{
			pBinaryExpression = new CExprBinary_L();
			++szExpression;
		}
		else
			return AbortedParse( szExpression );

		Allocated( pBinaryExpression );
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

IExpression* CComplexExpression::ParseInternal( char*& szExpression )
{
	// Skip whitespace
	while ( *szExpression && isspace( *szExpression ) )
		++szExpression;

	if ( !*szExpression )
		return AbortedParse( szExpression );

	if ( isdigit( *szExpression ) )
	{
		const int lValue = strtol( szExpression, &szExpression, 10 );
		return Allocated( new CExprConstant( lValue ) );
	}
	else if ( !strncmp( szExpression, "defined", 7 ) )
	{
		szExpression += 7;
		IExpression* pNext = ParseInternal( szExpression );
		return Allocated( new CExprConstant( pNext->Evaluate( m_pContext ) ) );
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
		const int nSlot = m_pContext->GetVariableSlot( std::string( szExpression + 1, lenVariable ).c_str() );
		szExpression += lenVariable + 1;

		return Allocated( new CExprVariable( nSlot ) );
	}
	else if ( *szExpression == '!' )
	{
		++szExpression;
		IExpression* pNext = ParseInternal( szExpression );
		return Allocated( new CExprUnary_Negate( pNext ) );
	}

	return AbortedParse( szExpression );
}

IExpression* CComplexExpression::Allocated( IExpression* pExpression )
{
	m_arrAllExpressions.emplace_back( pExpression );
	return pExpression;
}

void CComplexExpression::Clear()
{
	std::for_each( m_arrAllExpressions.begin(), m_arrAllExpressions.end(), std::default_delete<IExpression>() );

	m_arrAllExpressions.clear();
	m_pRoot = nullptr;
}

#undef BEGIN_EXPR_UNARY
#undef BEGIN_EXPR_BINARY

#undef END_EXPR_UNARY
#undef END_EXPR_BINARY

#undef EVAL
#undef PRNT

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

	void AddDefine( Define const& df );
	[[nodiscard]] Define const* GetDefinesBase() const { return m_arrDefines.data(); }
	[[nodiscard]] Define const* GetDefinesEnd() const { return m_arrDefines.data() + m_arrDefines.size(); }

	[[nodiscard]] uint64_t NumCombos() const;
	[[nodiscard]] uint64_t NumCombos( bool bStaticCombos ) const;

	// IEvaluationContext
public:
	[[nodiscard]] int GetVariableValue( int nSlot ) const override { return m_arrVarSlots[nSlot]; }
	[[nodiscard]] char const* GetVariableName( int nSlot ) const override { return m_arrDefines[nSlot].Name(); }
	[[nodiscard]] int GetVariableSlot( char const* szVariableName ) const override
	{
		const auto& find = m_mapDefines.find( szVariableName );
		if ( m_mapDefines.end() != find )
			return find->second;
		return -1;
	}

protected:
	std::vector<Define> m_arrDefines;
	std::unordered_map<std::string, int> m_mapDefines;
	std::vector<int> m_arrVarSlots;
};

void ComboGenerator::AddDefine( Define const& df )
{
	m_mapDefines.emplace( df.Name(), gsl::narrow<int>( m_arrDefines.size() ) );
	m_arrDefines.emplace_back( df );
	m_arrVarSlots.emplace_back( 1 );
}

uint64_t ComboGenerator::NumCombos() const
{
	return std::transform_reduce( m_arrDefines.cbegin(), m_arrDefines.cend(), 1ULL,
		[]( const uint64_t& a, const uint64_t& b ) { return a * b; },
		[]( const Define& d ) { return static_cast<uint64_t>( d.Max() ) - d.Min() + 1ULL; } );
}

uint64_t ComboGenerator::NumCombos( bool bStaticCombos ) const
{
	return std::transform_reduce( m_arrDefines.cbegin(), m_arrDefines.cend(), 1ULL,
		[]( const uint64_t& a, const uint64_t& b ) { return a * b; },
		[bStaticCombos]( const Define& d ) { return d.IsStatic() == bStaticCombos ? static_cast<uint64_t>( d.Max() ) - d.Min() + 1ULL : 1ULL; } );
}

extern std::string g_pShaderPath;
extern bool g_bVerbose;
namespace ConfigurationProcessing
{
class CfgEntry
{
public:
	CfgEntry() : m_szName( "" ), m_szShaderSrc( "" ), m_pCg( nullptr ), m_pExpr( nullptr )
	{
		memset( &m_eiInfo, 0, sizeof( m_eiInfo ) );
	}

	static void Destroy( CfgEntry const& x )
	{
		delete x.m_pCg;
		delete x.m_pExpr;
	}

public:
	bool operator<( CfgEntry const& x ) const { return m_pCg->NumCombos() < x.m_pCg->NumCombos(); }

public:
	char const* m_szName;
	char const* m_szShaderSrc;
	ComboGenerator* m_pCg;
	CComplexExpression* m_pExpr;

	CfgProcessor::CfgEntryInfo m_eiInfo;
};

static std::set<std::string> s_strPool;
static std::multiset<CfgEntry> s_setEntries;

class ComboHandleImpl : public IEvaluationContext
{
public:
	uint64_t m_iTotalCommand;
	uint64_t m_iComboNumber;
	uint64_t m_numCombos;
	CfgEntry const* m_pEntry;

public:
	ComboHandleImpl() : m_iTotalCommand( 0 ), m_iComboNumber( 0 ), m_numCombos( 0 ), m_pEntry( nullptr ) {}

	// IEvaluationContext
private:
	std::vector<int> m_arrVarSlots;

public:
	int GetVariableValue( int nSlot ) const override { return m_arrVarSlots[nSlot]; }
	char const* GetVariableName( int nSlot ) const override { return m_pEntry->m_pCg->GetVariableName( nSlot ); }
	int GetVariableSlot( char const* szVariableName ) const override { return m_pEntry->m_pCg->GetVariableSlot( szVariableName ); }

	// External implementation
public:
	bool Initialize( uint64_t iTotalCommand, const CfgEntry* pEntry );
	bool AdvanceCommands( uint64_t& riAdvanceMore );
	bool NextNotSkipped( uint64_t iTotalCommand );
	bool IsSkipped() { return m_pEntry->m_pExpr->Evaluate( this ) != 0; }
	void FormatCommand( std::span<char> pchBuffer );
	void FormatCommandHumanReadable( std::span<char> pchBuffer );
};

static std::map<uint64_t, ComboHandleImpl> s_mapComboCommands;

bool ComboHandleImpl::Initialize( uint64_t iTotalCommand, const CfgEntry* pEntry )
{
	m_iTotalCommand = iTotalCommand;
	m_pEntry        = pEntry;
	m_numCombos     = m_pEntry->m_pCg->NumCombos();

	// Defines
	Define const* const pDefVars    = m_pEntry->m_pCg->GetDefinesBase();
	Define const* const pDefVarsEnd = m_pEntry->m_pCg->GetDefinesEnd();

	// Set all the variables to max values
	for ( Define const* pSetDef = pDefVars; pSetDef < pDefVarsEnd; ++pSetDef )
		m_arrVarSlots.emplace_back( pSetDef->Max() );

	m_iComboNumber = m_numCombos - 1;
	return true;
}

bool ComboHandleImpl::AdvanceCommands( uint64_t& riAdvanceMore )
{
	if ( !riAdvanceMore )
		return true;

	// Get the pointers
	int* const pnValues    = m_arrVarSlots.data();
	int* const pnValuesEnd = pnValues + m_arrVarSlots.size();
	int* pSetValues;

	// Defines
	Define const* const pDefVars = m_pEntry->m_pCg->GetDefinesBase();
	Define const* pSetDef;

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
		*pSetValues -= int( riAdvanceMore % iInterval );
		riAdvanceMore /= iInterval;
	}

	return true;
}

bool ComboHandleImpl::NextNotSkipped( uint64_t iTotalCommand )
{
	// Get the pointers
	int* const pnValues    = m_arrVarSlots.data();
	int* const pnValuesEnd = pnValues + m_arrVarSlots.size();
	int* pSetValues;

	// Defines
	Define const* const pDefVars = m_pEntry->m_pCg->GetDefinesBase();
	Define const* pSetDef;

	// Go ahead and run the iterations
	{
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
}

void ComboHandleImpl::FormatCommand( std::span<char> pchBuffer )
{
	// Get the pointers
	const int* const pnValues    = m_arrVarSlots.data();
	const int* const pnValuesEnd = pnValues + m_arrVarSlots.size();
	const int* pSetValues;

	// Defines
	Define const* const pDefVars = m_pEntry->m_pCg->GetDefinesBase();
	Define const* const pDefVarsEnd = m_pEntry->m_pCg->GetDefinesEnd();
	Define const* pSetDef;

	{
		// ------- OnCombo( nCurrentCombo ); ----------
		int o = sprintf_s( pchBuffer.data(), pchBuffer.size(), "command" ) + 1;
		o += sprintf_s( &pchBuffer[o], pchBuffer.size() - o, "%s", m_pEntry->m_szShaderSrc ) + 1;
		o += sprintf_s( &pchBuffer[o], pchBuffer.size() - o, "%s", m_pEntry->m_eiInfo.m_szShaderVersion ) + 1;

		o += sprintf_s( &pchBuffer[o], pchBuffer.size() - o, "SHADERCOMBO" ) + 1;
		o += sprintf_s( &pchBuffer[o], pchBuffer.size() - o, "%llx", m_iComboNumber ) + 1;

		char version[20];
		strcpy_s( version, m_pEntry->m_eiInfo.m_szShaderVersion );
		_strupr_s( version );
		o += sprintf_s( &pchBuffer[o], pchBuffer.size() - o, "SHADER_MODEL_%s", version ) + 1;
		o += sprintf_s( &pchBuffer[o], pchBuffer.size() - o, "1" ) + 1;

		for ( pSetValues = pnValues, pSetDef = pDefVars; pSetValues < pnValuesEnd && pDefVars < pDefVarsEnd; ++pSetValues, ++pSetDef )
		{
			o += sprintf_s( &pchBuffer[o], pchBuffer.size() - o, "%s", pSetDef->Name() ) + 1;
			o += sprintf_s( &pchBuffer[o], pchBuffer.size() - o, "%d", *pSetValues ) + 1;
		}

		pchBuffer[o] = '\0';
		pchBuffer[o + 1LL] = '\0';
		// ------- end of OnCombo ---------------------
	}
}

void ComboHandleImpl::FormatCommandHumanReadable( std::span<char> pchBuffer )
{
	// Get the pointers
	const int* const pnValues    = m_arrVarSlots.data();
	const int* const pnValuesEnd = pnValues + m_arrVarSlots.size();
	const int* pSetValues;

	// Defines
	Define const* const pDefVars    = m_pEntry->m_pCg->GetDefinesBase();
	Define const* const pDefVarsEnd = m_pEntry->m_pCg->GetDefinesEnd();
	Define const* pSetDef;

	{
		// ------- OnCombo( nCurrentCombo ); ----------
		char version[20];
		strcpy_s( version, m_pEntry->m_eiInfo.m_szShaderVersion );
		_strupr_s( version );
		int o = sprintf_s( pchBuffer.data(), pchBuffer.size(),
			"fxc.exe /DCENTROIDMASK=%d /DSHADERCOMBO=%llx /DSHADER_MODEL_%s=1 /T%s /Emain",
			m_pEntry->m_eiInfo.m_nCentroidMask, m_iComboNumber, version, m_pEntry->m_eiInfo.m_szShaderVersion );

		for ( pSetValues = pnValues, pSetDef = pDefVars; pSetValues < pnValuesEnd && pDefVars < pDefVarsEnd; ++pSetValues, ++pSetDef )
			o += sprintf_s( &pchBuffer[o], pchBuffer.size() - o, " /D%s=%d", pSetDef->Name(), *pSetValues );

		o += sprintf_s( &pchBuffer[o], pchBuffer.size() - o, " %s", m_pEntry->m_szShaderSrc );
		// ------- end of OnCombo ---------------------

		pchBuffer[o] = '\0';
	}
}

static struct CAutoDestroyEntries
{
	~CAutoDestroyEntries()
	{
		std::for_each( s_setEntries.begin(), s_setEntries.end(), CfgEntry::Destroy );
	}
} s_autoDestroyEntries;


static std::map<std::string, std::array<std::string, 2>> shaderVersionMapping =
{
	{ "20b", { "ps_2_b", "vs_2_0" } },
	{ "30", { "ps_3_0", "vs_3_0" } },
	{ "40", { "ps_4_0", "vs_4_0" } },
	{ "41", { "ps_4_1", "vs_4_1" } },
	{ "50", { "ps_5_0", "vs_5_0" } },
	{ "51", { "ps_5_1", "vs_5_1" } }
};

static int FastToLower( char c )
{
	int i = (unsigned char)c;
	if ( i < 0x80 )
		// Brutally fast branchless ASCII tolower():
		i += ( ( ( ( 'A' - 1 ) - i ) & ( i - ( 'Z' + 1 ) ) ) >> 26 ) & 0x20;
	else
		i += isupper( i ) ? 0x20 : 0;
	return i;
}

static char const* V_stristr( char const* pStr, char const* pSearch )
{
	if ( !pStr || !pSearch )
		return 0;

	char const* pLetter = pStr;

	// Check the entire string
	while ( *pLetter != 0 )
	{
		// Skip over non-matches
		if ( FastToLower( (unsigned char)*pLetter ) == FastToLower( (unsigned char)*pSearch ) )
		{
			// Check for match
			char const* pMatch = pLetter + 1;
			char const* pTest = pSearch + 1;
			while ( *pTest != 0 )
			{
				// We've run off the end; don't bother.
				if ( *pMatch == 0 )
					return 0;

				if ( FastToLower( (unsigned char)*pMatch ) != FastToLower( (unsigned char)*pTest ) )
					break;

				++pMatch;
				++pTest;
			}

			// Found a match!
			if ( *pTest == 0 )
				return pLetter;
		}

		++pLetter;
	}

	return 0;
}

void SetupConfigurationDirect( const std::string& name, const std::string& version, uint32_t centroidMask,
								const std::vector<Parser::Combo>& static_c, const std::vector<Parser::Combo>& dynamic_c,
								const std::vector<std::string>& skip, const std::vector<std::string>& includes )
{
	using namespace std::literals;

	const auto& AddCombos = []( ComboGenerator& cg, const std::vector<Parser::Combo>& combos, bool staticC )
	{
		for ( const Parser::Combo& combo : combos )
			cg.AddDefine( Define( combo.name.c_str(), combo.minVal, combo.maxVal, staticC ) );
	};

	CfgEntry cfg;
	cfg.m_szName = s_strPool.emplace( name ).first->c_str();
	cfg.m_szShaderSrc = s_strPool.emplace( includes[0] ).first->c_str();
	// Combo generator
	ComboGenerator& cg = *( cfg.m_pCg = new ComboGenerator );
	CComplexExpression& exprSkip = *( cfg.m_pExpr = new CComplexExpression( &cg ) );

	AddCombos( cg, dynamic_c, false );
	AddCombos( cg, static_c, true );
	exprSkip.Parse( ( std::accumulate( skip.begin(), skip.end(), "("s, []( const std::string& s, const std::string& sk ) { return s + sk + ")||("; } ) + "0)" ).c_str() );

	CfgProcessor::CfgEntryInfo& info = cfg.m_eiInfo;
	info.m_szName = cfg.m_szName;
	info.m_szShaderFileName = cfg.m_szShaderSrc;
	info.m_szShaderVersion = s_strPool.emplace( shaderVersionMapping[version][V_stristr( cfg.m_szName, "_vs" ) != nullptr] ).first->c_str();
	info.m_numCombos = cg.NumCombos();
	info.m_numDynamicCombos = cg.NumCombos( false );
	info.m_numStaticCombos = cg.NumCombos( true );
	info.m_nCentroidMask = centroidMask;

	s_setEntries.insert( std::move( cfg ) );

	char filename[1024];
	for ( const std::string& file : includes )
	{
		if ( V_IsAbsolutePath( file.c_str() ) )
			strcpy_s( filename, file.c_str() );
		else
			sprintf_s( filename, "%s\\%s", g_pShaderPath.c_str(), file.c_str() );

		std::ifstream src( filename, std::ios::binary | std::ios::ate );
		if ( !src )
		{
			std::cout << clr::pinkish << "Can't find \"" << clr::red << filename << clr::pinkish << "\"" << std::endl;
			continue;
		}

		char justFilename[MAX_PATH];
		const char* pLastSlash = std::max( strrchr( file.c_str(), '/' ), strrchr( file.c_str(), '\\' ) );
		if ( pLastSlash )
			strcpy_s( justFilename, pLastSlash + 1 );
		else
			strcpy_s( justFilename, file.c_str() );

		if ( g_bVerbose )
			std::cout << "adding file to cache: \"" << clr::green << justFilename << clr::reset << "\"" << std::endl;

		std::vector<char> data( gsl::narrow<size_t>( src.tellg() ) );
		src.clear();
		src.seekg( 0, std::ios::beg );
		src.read( data.data(), data.size() );

		fileCache.Add( justFilename, std::move( data ) );
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
		const uint64_t iPartStep = std::max<uint64_t>( 1000, chi.m_numCombos / 500 );
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

static void ProcessConfiguration( const char* pConfigFile )
{
	{
		std::set<std::string> usedFiles;
		Json::Value configFile;
		{
			Json::CharReaderBuilder builder;
			std::ifstream file( pConfigFile );
			JSONCPP_STRING errors;
			parseFromStream( builder, file, &configFile, &errors );
		}

		const auto& AddCombos = []( ComboGenerator& cg, const Json::Value& combos, bool staticC )
		{
			for ( const Json::Value& combo : combos )
				cg.AddDefine( Define( combo["name"].asCString(), combo["minVal"].asInt(), combo["maxVal"].asInt(), staticC ) );
		};

		const Json::Value::Members& shaders = configFile.getMemberNames();
		for ( const auto& shader : shaders )
		{
			const Json::Value& curShader = configFile[shader];
			const Json::Value& sourceFiles = curShader["files"]; // first file is shader
			const Json::Value& staticCombos = curShader["static"];
			const Json::Value& dynamicCombos = curShader["dynamic"];

			CfgEntry cfg;
			cfg.m_szName = s_strPool.emplace( shader ).first->c_str();
			cfg.m_szShaderSrc = s_strPool.emplace( sourceFiles[0].asString() ).first->c_str();
			// Combo generator
			ComboGenerator& cg				= *( cfg.m_pCg = new ComboGenerator );
			CComplexExpression& exprSkip	= *( cfg.m_pExpr = new CComplexExpression( &cg ) );

			AddCombos( cg, dynamicCombos, false );
			AddCombos( cg, staticCombos, true );
			exprSkip.Parse( curShader["skip"].asCString() );

			CfgProcessor::CfgEntryInfo& info = cfg.m_eiInfo;
			info.m_szName = cfg.m_szName;
			info.m_szShaderFileName = cfg.m_szShaderSrc;
			info.m_szShaderVersion = s_strPool.emplace( curShader["version"].asString() ).first->c_str();
			info.m_numCombos = cg.NumCombos();
			info.m_numDynamicCombos = cg.NumCombos( false );
			info.m_numStaticCombos = cg.NumCombos( true );
			info.m_nCentroidMask = curShader["centroid"].asInt();

			s_setEntries.insert( std::move( cfg ) );

			for ( const Json::Value& f : sourceFiles )
				usedFiles.emplace( f.asString() );
		}

		char filename[1024];
		for ( const std::string& file : usedFiles )
		{
			if ( V_IsAbsolutePath( file.c_str() ) )
				strcpy_s( filename, file.c_str() );
			else
				sprintf_s( filename, "%s\\%s", g_pShaderPath.c_str(), file.c_str() );

			std::ifstream src( filename, std::ios::binary | std::ios::ate );
			if ( !src )
			{
				std::cout << clr::pinkish << "Can't find \"" << clr::red << filename << clr::pinkish << "\"" << std::endl;
				continue;
			}

			char justFilename[MAX_PATH];
			const char* pLastSlash = std::max( strrchr( file.c_str(), '/' ), strrchr( file.c_str(), '\\' ) );
			if ( pLastSlash )
				strcpy_s( justFilename, pLastSlash + 1 );
			else
				strcpy_s( justFilename, file.c_str() );

			if ( g_bVerbose )
				std::cout << "adding file to cache: \"" << clr::green << justFilename << clr::reset << "\"" << std::endl;

			std::vector<char> data( gsl::narrow<size_t>( src.tellg() ) );
			src.clear();
			src.seekg( 0, std::ios::beg );
			src.read( data.data(), data.size() );

			fileCache.Add( justFilename, std::move( data ) );
		}
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
		const uint64_t iPartStep = std::max<uint64_t>( 1000, chi.m_numCombos / 500 );
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
		chi.m_pEntry        = &s_term;
		s_mapComboCommands.emplace( nCurrentCommand, chi );
	}
}

}; // namespace ConfigurationProcessing

namespace CfgProcessor
{
using CPCHI_t = ConfigurationProcessing::ComboHandleImpl;
static CPCHI_t* FromHandle( ComboHandle hCombo )
{
	return reinterpret_cast<CPCHI_t*>( hCombo );
}
static ComboHandle AsHandle( CPCHI_t* pImpl )
{
	return reinterpret_cast<ComboHandle>( pImpl );
}

void ReadConfiguration( const char* configFile )
{
	ConfigurationProcessing::ProcessConfiguration( configFile );
}

void DescribeConfiguration( std::unique_ptr<CfgEntryInfo[]>& rarrEntries )
{
	rarrEntries.reset( new CfgEntryInfo[ConfigurationProcessing::s_setEntries.size() + 1] );

	CfgEntryInfo* pInfo    = rarrEntries.get();
	uint64_t nCurrentCommand = 0;

	for ( auto it = ConfigurationProcessing::s_setEntries.rbegin(), itEnd = ConfigurationProcessing::s_setEntries.rend(); it != itEnd; ++it, ++pInfo )
	{
		const ConfigurationProcessing::CfgEntry& f = *it;
		ConfigurationProcessing::CfgEntry const& e = *it;
		*pInfo = e.m_eiInfo;

		pInfo->m_iCommandStart    = nCurrentCommand;
		pInfo->m_iCommandEnd      = pInfo->m_iCommandStart + pInfo->m_numCombos;

		const_cast<CfgEntryInfo&>( e.m_eiInfo ) = *pInfo;

		if ( !PreprocessorDbg::s_bNoOutput )
			e.m_pExpr->Print( nullptr );

		nCurrentCommand += pInfo->m_numCombos;
	}

	// Terminator
	memset( pInfo, 0, sizeof( CfgEntryInfo ) );
	pInfo->m_iCommandStart = nCurrentCommand;
	pInfo->m_iCommandEnd   = nCurrentCommand;
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
	CPCHI_t const& chiFound = GetLessOrEq( iCommandFound, emptyCPCHI );

	if ( chiFound.m_iTotalCommand < 0 || chiFound.m_iTotalCommand > iCommandNumber )
		return nullptr;

	// Advance the handle as needed
	CPCHI_t* pImpl = new CPCHI_t( chiFound );

	uint64_t iCommandFoundAdvance = iCommandNumber - iCommandFound;
	pImpl->AdvanceCommands( iCommandFoundAdvance );

	return AsHandle( pImpl );
}

ComboHandle Combo_GetNext( uint64_t& riCommandNumber, ComboHandle& rhCombo, uint64_t iCommandEnd )
{
	// Combo handle implementation
	CPCHI_t* pImpl = FromHandle( rhCombo );

	if ( !rhCombo )
	{
		// We don't have a combo handle that corresponds to the command

		// Find earlier command
		uint64_t iCommandFound = riCommandNumber;
		const CPCHI_t emptyCPCHI;
		CPCHI_t const& chiFound = GetLessOrEq( iCommandFound, emptyCPCHI );

		if ( !chiFound.m_pEntry || !chiFound.m_pEntry->m_pCg || !chiFound.m_pEntry->m_pExpr || chiFound.m_iTotalCommand < 0 || chiFound.m_iTotalCommand > riCommandNumber )
			return nullptr;

		// Advance the handle as needed
		pImpl   = new CPCHI_t( chiFound );
		rhCombo = AsHandle( pImpl );

		uint64_t iCommandFoundAdvance = riCommandNumber - iCommandFound;
		pImpl->AdvanceCommands( iCommandFoundAdvance );

		if ( !pImpl->IsSkipped() )
			return rhCombo;
	}

	for ( ;; )
	{
		// We have the combo handle now
		if ( pImpl->NextNotSkipped( iCommandEnd ) )
		{
			riCommandNumber = pImpl->m_iTotalCommand;
			return rhCombo;
		}

		// We failed to get the next combo command (out of range)
		if ( pImpl->m_iTotalCommand + 1 >= iCommandEnd )
		{
			delete pImpl;
			rhCombo         = nullptr;
			riCommandNumber = iCommandEnd;
			return nullptr;
		}

		// Otherwise we just have to obtain the next combo handle
		riCommandNumber = pImpl->m_iTotalCommand + 1;

		// Delete the old combo handle
		delete pImpl;
		rhCombo = nullptr;

		// Retrieve the next combo handle data
		uint64_t iCommandLookup = riCommandNumber;
		CPCHI_t emptyCPCHI;
		CPCHI_t const& chiNext = GetLessOrEq( iCommandLookup, emptyCPCHI );
		Assert( iCommandLookup == riCommandNumber && ( chiNext.m_pEntry ) );

		// Set up the new combo handle
		pImpl   = new CPCHI_t( chiNext );
		rhCombo = AsHandle( pImpl );

		if ( !pImpl->IsSkipped() )
			return rhCombo;
	}
}

void Combo_FormatCommand( ComboHandle hCombo, std::span<char> pchBuffer )
{
	CPCHI_t* pImpl = FromHandle( hCombo );
	pImpl->FormatCommand( pchBuffer );
}

void Combo_FormatCommandHumanReadable( ComboHandle hCombo, std::span<char> pchBuffer )
{
	CPCHI_t* pImpl = FromHandle( hCombo );
	pImpl->FormatCommandHumanReadable( pchBuffer );
}

uint64_t Combo_GetCommandNum( ComboHandle hCombo )
{
	if ( CPCHI_t* pImpl = FromHandle( hCombo ) )
		return pImpl->m_iTotalCommand;
	return ~0ULL;
}

uint64_t Combo_GetComboNum( ComboHandle hCombo )
{
	if ( CPCHI_t* pImpl = FromHandle( hCombo ) )
		return pImpl->m_iComboNumber;
	return ~0ULL;
}

CfgEntryInfo const* Combo_GetEntryInfo( ComboHandle hCombo )
{
	if ( CPCHI_t* pImpl = FromHandle( hCombo ) )
		return &pImpl->m_pEntry->m_eiInfo;
	return nullptr;
}

ComboHandle Combo_Alloc( ComboHandle hComboCopyFrom )
{
	if ( hComboCopyFrom )
		return AsHandle( new CPCHI_t( *FromHandle( hComboCopyFrom ) ) );
	return AsHandle( new CPCHI_t );
}

void Combo_Assign( ComboHandle hComboDst, ComboHandle hComboSrc )
{
	Assert( hComboDst );
	*FromHandle( hComboDst ) = *FromHandle( hComboSrc );
}

void Combo_Free( ComboHandle& rhComboFree )
{
	delete FromHandle( rhComboFree );
	rhComboFree = nullptr;
}
}; // namespace CfgProcessor