//====== Copyright c 1996-2007, Valve Corporation, All rights reserved. =======//
//
// Purpose:
//
// $NoKeywords: $
//
//=============================================================================//

#include <cstdarg>
#include <ctime>

#include <unordered_map>
#include <map>
#include <set>
#include <vector>
#include <string>
#include <algorithm>

#include "cfgprocessor.h"
#include "utlbuffer.h"

// Type conversions should be controlled by programmer explicitly - shadercompile makes use of 64-bit integer arithmetics
#pragma warning( error : 4244 )

namespace
{

static bool s_bNoOutput = true;

#ifdef __RESHARPER__
[[rscpp::format( printf, 2, 3 )]]
#endif
void OutputF( FILE* f, char const* szFmt, ... )
{
	if ( s_bNoOutput )
		return;

	va_list args;
	va_start( args, szFmt );
	vfprintf( f, szFmt, args );
	va_end( args );
}

};

//////////////////////////////////////////////////////////////////////////
//
// Define class
//
//////////////////////////////////////////////////////////////////////////

class Define
{
public:
	explicit Define( char const* szName, int min, int max, bool bStatic ) : m_sName( szName ), m_min( min ), m_max( max ), m_bStatic( bStatic ) {}

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
	virtual ~IEvaluationContext() = default;
	virtual int GetVariableValue( int nSlot ) = 0;
	virtual char const* GetVariableName( int nSlot ) = 0;
	virtual int GetVariableSlot( char const* szVariableName ) = 0;
};

class IExpression
{
public:
	virtual ~IExpression() = default;
	virtual int64  Evaluate( IEvaluationContext* pCtx ) const = 0;
	virtual void Print( IEvaluationContext* pCtx ) const = 0;
};

#define EVAL virtual int64 Evaluate( [[maybe_unused]] IEvaluationContext* pCtx ) const override
#define PRNT virtual void Print( [[maybe_unused]] IEvaluationContext* pCtx ) const override

class CExprConstant : public IExpression
{
public:
	CExprConstant( int64 value ) : m_value( value ) {}
	EVAL { return m_value; }
	PRNT { OutputF( stdout, "%lld", m_value ); }
public:
	int64 m_value;
};

class CExprVariable : public IExpression
{
public:
	CExprVariable( int nSlot ) : m_nSlot( nSlot ) {}
	EVAL { return m_nSlot >= 0 ? pCtx->GetVariableValue( m_nSlot ) : 0; };
	PRNT { m_nSlot >= 0 ? OutputF( stdout, "$%s", pCtx->GetVariableName( m_nSlot ) ) : OutputF( stdout, "$**@**" ); }
public:
	int m_nSlot;
};

class CExprUnary : public IExpression
{
public:
	CExprUnary( IExpression* x ) : m_x( x ) {}
public:
	IExpression* m_x;
};

#define BEGIN_EXPR_UNARY( className ) class className : public CExprUnary { public: className( IExpression* x ) : CExprUnary( x ) {}
#define END_EXPR_UNARY() };

BEGIN_EXPR_UNARY( CExprUnary_Negate )
	EVAL { return !m_x->Evaluate( pCtx ); }
	PRNT { OutputF( stdout, "!" ); m_x->Print( pCtx ); }
END_EXPR_UNARY()

class CExprBinary : public IExpression
{
public:
	CExprBinary( IExpression* x, IExpression* y ) : m_x( x ), m_y( y ) {}
	[[nodiscard]] virtual int Priority() const = 0;
public:
	IExpression* m_x;
	IExpression* m_y;
};

#define BEGIN_EXPR_BINARY( className ) class className : public CExprBinary { public: className( IExpression* x, IExpression* y ) : CExprBinary( x, y ) {}
#define EXPR_BINARY_PRIORITY( nPriority ) virtual int Priority() const override { return nPriority; }
#define END_EXPR_BINARY() };

BEGIN_EXPR_BINARY( CExprBinary_And )
	EVAL { return m_x->Evaluate( pCtx ) && m_y->Evaluate( pCtx ); }
	PRNT { OutputF( stdout, "( " ); m_x->Print( pCtx ); OutputF( stdout, " && " ); m_y->Print( pCtx ); OutputF( stdout, " )" ); }
	EXPR_BINARY_PRIORITY( 1 );
END_EXPR_BINARY()

BEGIN_EXPR_BINARY( CExprBinary_Or )
	EVAL { return m_x->Evaluate( pCtx ) || m_y->Evaluate( pCtx ); }
	PRNT { OutputF( stdout, "( " ); m_x->Print( pCtx ); OutputF( stdout, " || " ); m_y->Print( pCtx ); OutputF( stdout, " )" ); }
	EXPR_BINARY_PRIORITY( 2 );
END_EXPR_BINARY()

BEGIN_EXPR_BINARY( CExprBinary_Eq )
	EVAL { return m_x->Evaluate( pCtx ) == m_y->Evaluate( pCtx ); }
	PRNT { OutputF( stdout, "( " ); m_x->Print( pCtx ); OutputF( stdout, " == " ); m_y->Print( pCtx ); OutputF( stdout, " )" ); }
	EXPR_BINARY_PRIORITY( 0 );
END_EXPR_BINARY()

BEGIN_EXPR_BINARY( CExprBinary_Neq )
	EVAL { return m_x->Evaluate( pCtx ) != m_y->Evaluate( pCtx ); }
	PRNT { OutputF( stdout, "( " ); m_x->Print( pCtx ); OutputF( stdout, " != " ); m_y->Print( pCtx ); OutputF( stdout, " )" ); }
	EXPR_BINARY_PRIORITY( 0 );
END_EXPR_BINARY()

BEGIN_EXPR_BINARY( CExprBinary_G )
	EVAL { return m_x->Evaluate( pCtx ) > m_y->Evaluate( pCtx ); }
	PRNT { OutputF( stdout, "( " ); m_x->Print( pCtx ); OutputF( stdout, " > " ); m_y->Print( pCtx ); OutputF( stdout, " )" ); }
	EXPR_BINARY_PRIORITY( 0 );
END_EXPR_BINARY()

BEGIN_EXPR_BINARY( CExprBinary_Ge )
	EVAL { return m_x->Evaluate( pCtx ) >= m_y->Evaluate( pCtx ); }
	PRNT { OutputF( stdout, "( " ); m_x->Print( pCtx ); OutputF( stdout, " >= " ); m_y->Print( pCtx ); OutputF( stdout, " )" ); }
	EXPR_BINARY_PRIORITY( 0 );
END_EXPR_BINARY()

BEGIN_EXPR_BINARY( CExprBinary_L )
	EVAL { return m_x->Evaluate( pCtx ) < m_y->Evaluate( pCtx ); }
	PRNT { OutputF( stdout, "( " ); m_x->Print( pCtx ); OutputF( stdout, " < " ); m_y->Print( pCtx ); OutputF( stdout, " )" ); }
	EXPR_BINARY_PRIORITY( 0 );
END_EXPR_BINARY()

BEGIN_EXPR_BINARY( CExprBinary_Le )
	EVAL { return m_x->Evaluate( pCtx ) <= m_y->Evaluate( pCtx ); }
	PRNT { OutputF( stdout, "( " ); m_x->Print( pCtx ); OutputF( stdout, " <= " ); m_y->Print( pCtx ); OutputF( stdout, " )" ); }
	EXPR_BINARY_PRIORITY( 0 );
END_EXPR_BINARY()


class CComplexExpression : public IExpression
{
public:
	CComplexExpression( IEvaluationContext* pCtx ) : m_pRoot( nullptr ), m_pContext( pCtx ), m_pDefTrue( nullptr ), m_pDefFalse( nullptr ) {}
	~CComplexExpression() override { Clear(); }

	void Parse( char const* szExpression );
	void Clear();

public:
	EVAL { return m_pRoot ? m_pRoot->Evaluate( pCtx ? pCtx : m_pContext ) : 0; }
	PRNT { OutputF( stdout, "[ " ); m_pRoot ? m_pRoot->Print( pCtx ? pCtx : m_pContext ) : OutputF( stdout, "**NEXPR**" ); OutputF( stdout, " ]\n" ); }

protected:
	IExpression* ParseTopLevel( char*& szExpression );
	IExpression* ParseInternal( char*& szExpression );
	IExpression* Allocated( IExpression* pExpression );
	IExpression* AbortedParse( char*& szExpression ) const { *szExpression = 0; return m_pDefFalse; }

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

	m_pDefTrue = Allocated( new CExprConstant( 1 ) );
	m_pDefFalse = Allocated( new CExprConstant( 0 ) );

	m_pRoot = m_pDefFalse;

	if ( szExpression )
	{
		std::string qs( szExpression );
		char* expression = qs.data();
		char* szExpectEnd = expression + qs.length();
		char* szParse = expression;
		m_pRoot = ParseTopLevel( szParse );

		if ( szParse != szExpectEnd )
			m_pRoot = m_pDefFalse;
	}
}

IExpression* CComplexExpression::ParseTopLevel( char*& szExpression )
{
	std::vector<CExprBinary*> exprStack;
	IExpression* pFirstToken = ParseInternal( szExpression );

	for ( ; ; )
	{
		// Skip whitespace
		while ( *szExpression && isspace( *szExpression ) )
			++ szExpression;

		// End of binary expression
		if ( !*szExpression || ( *szExpression == ')' ) )
			break;

		// Determine the binary expression type
		CExprBinary* pBinaryExpression = nullptr;

		if ( !strncmp( szExpression, "&&", 2 ) )
		{
			pBinaryExpression = new CExprBinary_And( nullptr, nullptr );
			szExpression += 2;
		}
		else if ( !strncmp( szExpression, "||", 2 ) )
		{
			pBinaryExpression = new CExprBinary_Or( nullptr, nullptr );
			szExpression += 2;
		}
		else if ( !strncmp( szExpression, ">=", 2 ) )
		{
			pBinaryExpression = new CExprBinary_Ge( nullptr, nullptr );
			szExpression += 2;
		}
		else if ( !strncmp( szExpression, "<=", 2 ) )
		{
			pBinaryExpression = new CExprBinary_Le( nullptr, nullptr );
			szExpression += 2;
		}
		else if ( !strncmp( szExpression, "==", 2 ) )
		{
			pBinaryExpression = new CExprBinary_Eq( nullptr, nullptr );
			szExpression += 2;
		}
		else if ( !strncmp( szExpression, "!=", 2 ) )
		{
			pBinaryExpression = new CExprBinary_Neq( nullptr, nullptr );
			szExpression += 2;
		}
		else if ( *szExpression == '>' )
		{
			pBinaryExpression = new CExprBinary_G( nullptr, nullptr );
			++szExpression;
		}
		else if ( *szExpression == '<' )
		{
			pBinaryExpression = new CExprBinary_L( nullptr, nullptr );
			++szExpression;
		}
		else
			return AbortedParse( szExpression );

		Allocated( pBinaryExpression );
		pBinaryExpression->m_y = ParseInternal( szExpression );

		// Figure out the expression priority
		const int nPriority = pBinaryExpression->Priority();
		IExpression* pLastExpr = pFirstToken;
		while ( !exprStack.empty() )
		{
			CExprBinary* pStickTo = exprStack.back();
			pLastExpr = pStickTo;

			if ( nPriority > pStickTo->Priority() )
				exprStack.pop_back();
			else
				break;
		}

		if ( !exprStack.empty() )
		{
			CExprBinary* pStickTo = exprStack.back();
			pBinaryExpression->m_x = pStickTo->m_y;
			pStickTo->m_y = pBinaryExpression;
		}
		else
			pBinaryExpression->m_x = pLastExpr;

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
		const uint64 lValue = strtoll( szExpression, &szExpression, 10 );
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
			++ szExpression;
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
	void AddDefine( Define const& df );
	[[nodiscard]] Define const* GetDefinesBase() const { return m_arrDefines.data(); }
	[[nodiscard]] Define const* GetDefinesEnd() const { return m_arrDefines.data() + m_arrDefines.size(); }

	[[nodiscard]] uint64 NumCombos() const;
	[[nodiscard]] uint64 NumCombos( bool bStaticCombos ) const;
	void RunAllCombos( CComplexExpression const& skipExpr );

	// IEvaluationContext
public:
	[[nodiscard]] int GetVariableValue( int nSlot ) override { return m_arrVarSlots[nSlot]; }
	[[nodiscard]] char const* GetVariableName( int nSlot ) override { return m_arrDefines[nSlot].Name(); }
	[[nodiscard]] int GetVariableSlot( char const* szVariableName ) override
	{
		const auto& find = m_mapDefines.find( szVariableName );
		if ( m_mapDefines.end() != find )
			return find->second;
		return -1;
	}

protected:
	std::vector<Define>	m_arrDefines;
	std::unordered_map<std::string, int>			m_mapDefines;
	std::vector<int>		m_arrVarSlots;
};

void ComboGenerator::AddDefine( Define const& df )
{
	m_mapDefines.emplace( df.Name(), gsl::narrow<int>( m_arrDefines.size() ) );
	m_arrDefines.emplace_back( df );
	m_arrVarSlots.emplace_back( 1 );
}

uint64 ComboGenerator::NumCombos() const
{
	uint64 numCombos = 1;

	for ( size_t k = 0, kEnd = m_arrDefines.size(); k < kEnd; ++k )
	{
		Define const& df = m_arrDefines[k];
		numCombos *= df.Max() - df.Min() + 1;
	}

	return numCombos;
}

uint64 ComboGenerator::NumCombos( bool bStaticCombos ) const
{
	uint64 numCombos = 1;

	for ( size_t k = 0, kEnd = m_arrDefines.size(); k < kEnd; ++ k )
	{
		Define const& df = m_arrDefines[k];
		df.IsStatic() == bStaticCombos ? numCombos *= ( df.Max() - df.Min() + 1 ) : 0;
	}

	return numCombos;
}


struct ComboEmission
{
	std::string m_sPrefix;
	std::string m_sSuffix;
} g_comboEmission;

static constexpr size_t const g_lenTmpBuffer = 1 * 1024 * 1024; // 1Mb buffer for tmp storage
static char g_chTmpBuffer[g_lenTmpBuffer];

void ComboGenerator::RunAllCombos( CComplexExpression const& skipExpr )
{
	// Combo numbers
	uint64 const nTotalCombos = NumCombos();

	// Get the pointers
	int* const pnValues = m_arrVarSlots.data();
	int* const pnValuesEnd = pnValues + m_arrVarSlots.size();
	int* pSetValues;

	// Defines
	Define const* const pDefVars = m_arrDefines.data();
	Define const* pSetDef;

	// Set all the variables to max values
	for ( pSetValues = pnValues, pSetDef = pDefVars; pSetValues < pnValuesEnd; ++pSetValues, ++pSetDef )
		*pSetValues = pSetDef->Max();

	// Expressions distributed [0] = skips, [1] = evaluated
	uint64 nSkipEvalCounters[2] = { 0, 0 };

	// Go ahead and run the iterations
	{
		uint64 nCurrentCombo = nTotalCombos;

	next_combo_iteration:
		--nCurrentCombo;
		int64 const valExprSkip = skipExpr.Evaluate( this );

		++nSkipEvalCounters[!valExprSkip];

		if ( !valExprSkip )
		{
			// ------- OnCombo( nCurrentCombo ); ----------
			OutputF( stderr, "%s ", g_comboEmission.m_sPrefix.data() );
			OutputF( stderr, "/DSHADERCOMBO=%llx ", nCurrentCombo );

			for ( pSetValues = pnValues, pSetDef = pDefVars;
				pSetValues < pnValuesEnd;
				++ pSetValues, ++ pSetDef )
			{
				OutputF( stderr, "/D%s=%d ", pSetDef->Name(), *pSetValues );
			}

			OutputF( stderr, "%s\n", g_comboEmission.m_sSuffix.data() );
			// ------- end of OnCombo ---------------------
		}

		// Do a next iteration
		for ( pSetValues = pnValues, pSetDef = pDefVars; pSetValues < pnValuesEnd; ++pSetValues, ++pSetDef )
		{
			if ( -- *pSetValues >= pSetDef->Min() )
				goto next_combo_iteration;

			*pSetValues = pSetDef->Max();
		}
	}

	OutputF( stdout, "Generated %lld combos: %lld evaluated, %lld skipped.\n", nTotalCombos, nSkipEvalCounters[1], nSkipEvalCounters[0] );
}


namespace ConfigurationProcessing
{
	class CfgEntry
	{
	public:
		CfgEntry() : m_szName( "" ), m_szShaderSrc( "" ), m_pCg( nullptr ), m_pExpr( nullptr ) { memset( &m_eiInfo, 0, sizeof( m_eiInfo ) ); }
		static void Destroy( CfgEntry const& x ) { delete x.m_pCg; delete x.m_pExpr; }

	public:
		bool operator<( CfgEntry const& x ) const { return m_pCg->NumCombos() < x.m_pCg->NumCombos(); }

	public:
		char const* m_szName;
		char const* m_szShaderSrc;
		ComboGenerator* m_pCg;
		CComplexExpression* m_pExpr;
		std::string m_sPrefix;
		std::string m_sSuffix;

		CfgProcessor::CfgEntryInfo m_eiInfo;
	};

	std::set<std::string> s_uniqueSections, s_strPool;
	std::multiset<CfgEntry> s_setEntries;

	class ComboHandleImpl : public IEvaluationContext
	{
	public:
		uint64 m_iTotalCommand;
		uint64 m_iComboNumber;
		uint64 m_numCombos;
		CfgEntry const* m_pEntry;

	public:
		ComboHandleImpl() : m_iTotalCommand( 0 ), m_iComboNumber( 0 ), m_numCombos( 0 ), m_pEntry( nullptr ) {}

		// IEvaluationContext
	public:
		std::vector<int>		m_arrVarSlots;
	public:
		int GetVariableValue( int nSlot ) override { return m_arrVarSlots[nSlot]; }
		char const* GetVariableName( int nSlot ) override { return m_pEntry->m_pCg->GetVariableName( nSlot ); }
		int GetVariableSlot( char const* szVariableName ) override { return m_pEntry->m_pCg->GetVariableSlot( szVariableName ); }

		// External implementation
	public:
		bool Initialize( uint64 iTotalCommand, const CfgEntry* pEntry );
		bool AdvanceCommands( uint64& riAdvanceMore );
		bool NextNotSkipped( uint64 iTotalCommand );
		bool IsSkipped() { return m_pEntry->m_pExpr->Evaluate( this ) != 0; }
		void FormatCommand( gsl::span<char> pchBuffer );
	};

	std::map<uint64, ComboHandleImpl> s_mapComboCommands;

	bool ComboHandleImpl::Initialize( uint64 iTotalCommand, const CfgEntry* pEntry )
	{
		m_iTotalCommand = iTotalCommand;
		m_pEntry = pEntry;
		m_numCombos = m_pEntry->m_pCg->NumCombos();

		// Defines
		Define const* const pDefVars = m_pEntry->m_pCg->GetDefinesBase();
		Define const* const pDefVarsEnd = m_pEntry->m_pCg->GetDefinesEnd();

		// Set all the variables to max values
		for ( Define const* pSetDef = pDefVars; pSetDef < pDefVarsEnd; ++pSetDef )
			m_arrVarSlots.emplace_back( pSetDef->Max() );

		m_iComboNumber = m_numCombos - 1;
		return true;
	}

	bool ComboHandleImpl::AdvanceCommands( uint64& riAdvanceMore )
	{
		if ( !riAdvanceMore )
			return true;

		// Get the pointers
		int* const pnValues = m_arrVarSlots.data();
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
			riAdvanceMore += ( pSetDef->Max() - *pSetValues );
			*pSetValues = pSetDef->Max();

			const int iInterval = ( pSetDef->Max() - pSetDef->Min() + 1 );
			*pSetValues -= int( riAdvanceMore % iInterval );
			riAdvanceMore /= iInterval;
		}

		return true;
	}

	bool ComboHandleImpl::NextNotSkipped( uint64 iTotalCommand )
	{
		// Get the pointers
		int* const pnValues = m_arrVarSlots.data();
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

	void ComboHandleImpl::FormatCommand( gsl::span<char> pchBuffer )
	{
		// Get the pointers
		const int* const pnValues = m_arrVarSlots.data();
		const int* const pnValuesEnd = pnValues + m_arrVarSlots.size();
		const int* pSetValues;

		// Defines
		Define const* const pDefVars = m_pEntry->m_pCg->GetDefinesBase();
		Define const* pSetDef;

		{
			// ------- OnCombo( nCurrentCombo ); ----------
			int o = sprintf_s( pchBuffer.data(), pchBuffer.size(), "%s ", m_pEntry->m_sPrefix.c_str() );

			o += sprintf_s( &pchBuffer[o], pchBuffer.size() - o, "/DSHADERCOMBO=%llx ", m_iComboNumber );

			for ( pSetValues = pnValues, pSetDef = pDefVars; pSetValues < pnValuesEnd; ++pSetValues, ++pSetDef )
				o += sprintf_s( &pchBuffer[o], pchBuffer.size() - o, "/D%s=%d ", pSetDef->Name(), *pSetValues );

			sprintf_s( &pchBuffer[o], pchBuffer.size() - o, "%s\n", m_pEntry->m_sSuffix.data() );
			// ------- end of OnCombo ---------------------
		}
	}

	struct CAutoDestroyEntries
	{
		~CAutoDestroyEntries()
		{
			std::for_each( s_setEntries.begin(), s_setEntries.end(), CfgEntry::Destroy );
		}
	} s_autoDestroyEntries;

	template <typename T>
	T*& GetInputStream();

	template <>
	FILE*& GetInputStream()
	{
		static FILE* s_fInput = stdin;
		return s_fInput;
	}

	template <>
	CUtlInplaceBuffer*& GetInputStream()
	{
		static CUtlInplaceBuffer *s_fInput = nullptr;
		return s_fInput;
	}

	char* GetLinePtr_Private()
	{
		if ( CUtlInplaceBuffer* pUtlBuffer = GetInputStream<CUtlInplaceBuffer>() )
			return pUtlBuffer->InplaceGetLinePtr();

		if ( FILE* fInput = GetInputStream<FILE>() )
			return fgets( g_chTmpBuffer, g_lenTmpBuffer, fInput );

		return nullptr;
	}

	bool LineEquals( char const* sz1, char const* sz2, int nLen )
	{
		return 0 == strncmp( sz1, sz2, nLen );
	}

	char* NextLine()
	{
		if ( char* szLine = GetLinePtr_Private() )
		{
			// Trim trailing whitespace as well
			size_t len = strlen( szLine );
			while ( len-- > 0 && isspace( szLine[len] ) )
				szLine[len] = 0;
			return szLine;
		}
		return nullptr;
	}

	char* WaitFor( char const* szWaitString, int nMatchLength )
	{
		while ( char* pchResult = NextLine() )
			if ( LineEquals( pchResult, szWaitString, nMatchLength ) )
				return pchResult;

		return nullptr;
	}

	bool ProcessSection( CfgEntry& cfge )
	{
		bool bStaticDefines;

		// Read the next line for the section src file
		if ( char* szLine = NextLine() )
			cfge.m_szShaderSrc = s_strPool.emplace( szLine ).first->c_str();

		if ( char* szLine = WaitFor( "#DEFINES-", 9 ) )
			bStaticDefines = ( szLine[9] == 'S' );
		else
			return false;

		// Combo generator
		ComboGenerator& cg = *( cfge.m_pCg = new ComboGenerator );
		CComplexExpression& exprSkip = *( cfge.m_pExpr = new CComplexExpression( &cg ) );

		// #DEFINES:
		while ( char* szLine = NextLine() )
		{
			if ( LineEquals( szLine, "#SKIP", 5 ) )
				break;

			// static defines
			if ( LineEquals( szLine, "#DEFINES-", 9 ) )
			{
				bStaticDefines = szLine[9] == 'S';
				continue;
			}

			while ( *szLine && isspace( *szLine ) )
				++szLine;

			// Find the eq
			char* pchEq = strchr( szLine, '=' );
			if ( !pchEq )
				continue;

			char* pchStartRange = pchEq + 1;
			*pchEq = 0;
			while ( -- pchEq >= szLine && isspace( *pchEq ) )
				*pchEq = 0;
			if ( !*szLine )
				continue;

			// Find the end of range
			char* pchEndRange = strstr( pchStartRange, ".." );
			if ( !pchEndRange )
				continue;
			pchEndRange += 2;

			// Create the define
			Define df( szLine, atoi( pchStartRange ), atoi( pchEndRange ), bStaticDefines );
			if ( df.Max() < df.Min() )
				continue;

			// Add the define
			cg.AddDefine( df );
		}

		// #SKIP:
		if ( char* szLine = NextLine() )
			exprSkip.Parse( szLine );
		else
			return false;

		// #COMMAND:
		if ( !WaitFor( "#COMMAND", 8 ) )
			return false;
		if ( char* szLine = NextLine() )
			cfge.m_sPrefix = szLine;
		if ( char* szLine = NextLine() )
			cfge.m_sSuffix = szLine;

		// #END
		if ( !WaitFor( "#END", 4 ) )
			return false;

		return true;
	}

	void UnrollSectionCommands( CfgEntry const& cfge )
	{
		// Execute the combo computation
		//
		//

		g_comboEmission.m_sPrefix = cfge.m_sPrefix;
		g_comboEmission.m_sSuffix = cfge.m_sSuffix;

		OutputF( stdout, "Preparing %lld combos for %s...\n", cfge.m_pCg->NumCombos(), cfge.m_szName );
		OutputF( stderr, "#%s\n", cfge.m_szName );

		const time_t tt_start = time( nullptr );
		cfge.m_pCg->RunAllCombos( *cfge.m_pExpr );
		const time_t tt_end = time( nullptr );

		OutputF( stderr, "#%s\n", cfge.m_szName );
		OutputF( stdout, "Prepared %s combos. %d sec.\n", cfge.m_szName, static_cast<int>( difftime( tt_end, tt_start ) ) );

		g_comboEmission.m_sPrefix = "";
		g_comboEmission.m_sSuffix = "";
	}

	void RunSection( CfgEntry const& cfge )
	{
		// Execute the combo computation
		//
		//

		g_comboEmission.m_sPrefix = cfge.m_sPrefix;
		g_comboEmission.m_sSuffix = cfge.m_sSuffix;

		OutputF( stdout, "Preparing %lld combos for %s...\n", cfge.m_pCg->NumCombos(), cfge.m_szName );
		OutputF( stderr, "#%s\n", cfge.m_szName );

		const time_t tt_start = time( nullptr );
		cfge.m_pCg->RunAllCombos( *cfge.m_pExpr );
		const time_t tt_end = time( nullptr );

		OutputF( stderr, "#%s\n", cfge.m_szName );
		OutputF( stdout, "Prepared %s combos. %d sec.\n", cfge.m_szName, static_cast<int>( difftime( tt_end, tt_start ) ) );

		g_comboEmission.m_sPrefix = "";
		g_comboEmission.m_sSuffix = "";
	}

	void ProcessConfiguration()
	{
		while ( char* szLine = WaitFor( "#BEGIN", 6 ) )
		{
			const auto& f = s_uniqueSections.emplace( szLine + 7 );
			if ( ' ' == szLine[6] && !f.second )
				continue;

			CfgEntry cfge;
			cfge.m_szName = f.first->c_str();
			ProcessSection( cfge );
			s_setEntries.insert( cfge );
		}

		uint64 nCurrentCommand = 0;
		for( auto it = s_setEntries.rbegin(), itEnd = s_setEntries.rend(); it != itEnd; ++ it )
		{
			// We establish a command mapping for the beginning of the entry
			ComboHandleImpl chi;
			chi.Initialize( nCurrentCommand, &*it );
			s_mapComboCommands.emplace( nCurrentCommand, chi );

			// We also establish mapping by either splitting the
			// combos into 500 intervals or stepping by every 1000 combos.
			const uint64 iPartStep = Max<uint64>( 1000, chi.m_numCombos / 500 );
			for ( uint64 iRecord = nCurrentCommand + iPartStep; iRecord < nCurrentCommand + chi.m_numCombos; iRecord += iPartStep )
			{
				uint64 iAdvance = iPartStep;
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
	CPCHI_t* FromHandle( ComboHandle hCombo ) { return reinterpret_cast<CPCHI_t*>( hCombo ); }
	ComboHandle AsHandle( CPCHI_t* pImpl ) { return reinterpret_cast<ComboHandle>( pImpl ); }

	void ReadConfiguration( FILE* fInputStream )
	{
		CAutoPushPop pushInputStream( ConfigurationProcessing::GetInputStream<FILE>(), fInputStream );
		ConfigurationProcessing::ProcessConfiguration();
	}

	void ReadConfiguration( CUtlInplaceBuffer* fInputStream )
	{
		CAutoPushPop pushInputStream( ConfigurationProcessing::GetInputStream<CUtlInplaceBuffer>(), fInputStream );
		ConfigurationProcessing::ProcessConfiguration();
	}

	void DescribeConfiguration( std::unique_ptr<CfgEntryInfo[]>& rarrEntries )
	{
		rarrEntries.reset( new CfgEntryInfo[ConfigurationProcessing::s_setEntries.size() + 1] );

		CfgEntryInfo* pInfo = rarrEntries.get();
		uint64 nCurrentCommand = 0;

		for ( auto it = ConfigurationProcessing::s_setEntries.rbegin(), itEnd = ConfigurationProcessing::s_setEntries.rend(); it != itEnd; ++it, ++pInfo )
		{
			ConfigurationProcessing::CfgEntry const& e = *it;

			pInfo->m_szName = e.m_szName;
			pInfo->m_szShaderFileName = e.m_szShaderSrc;

			pInfo->m_iCommandStart = nCurrentCommand;
			pInfo->m_numCombos = e.m_pCg->NumCombos();
			pInfo->m_numDynamicCombos = e.m_pCg->NumCombos( false );
			pInfo->m_numStaticCombos = e.m_pCg->NumCombos( true );
			pInfo->m_iCommandEnd = pInfo->m_iCommandStart + pInfo->m_numCombos;

			const_cast<CfgEntryInfo&>( e.m_eiInfo ) = *pInfo;

			nCurrentCommand += pInfo->m_numCombos;
		}

		// Terminator
		memset( pInfo, 0, sizeof( CfgEntryInfo ) );
		pInfo->m_iCommandStart = nCurrentCommand;
		pInfo->m_iCommandEnd = nCurrentCommand;
	}

	const CPCHI_t& GetLessOrEq( uint64& k,const CPCHI_t& v )
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

	ComboHandle Combo_GetCombo( uint64 iCommandNumber )
	{
		// Find earlier command
		uint64 iCommandFound = iCommandNumber;
		const CPCHI_t emptyCPCHI;
		CPCHI_t const& chiFound = GetLessOrEq( iCommandFound, emptyCPCHI );

		if ( chiFound.m_iTotalCommand < 0 || chiFound.m_iTotalCommand > iCommandNumber )
			 return nullptr;

		// Advance the handle as needed
		CPCHI_t* pImpl = new CPCHI_t( chiFound );

		uint64 iCommandFoundAdvance = iCommandNumber - iCommandFound;
		pImpl->AdvanceCommands( iCommandFoundAdvance );

		return AsHandle( pImpl );
	}

	ComboHandle Combo_GetNext( uint64& riCommandNumber, ComboHandle& rhCombo, uint64 iCommandEnd )
	{
		// Combo handle implementation
		CPCHI_t* pImpl = FromHandle( rhCombo );

		if ( !rhCombo )
		{
			// We don't have a combo handle that corresponds to the command

			// Find earlier command
			uint64 iCommandFound = riCommandNumber;
			const CPCHI_t emptyCPCHI;
			CPCHI_t const& chiFound = GetLessOrEq( iCommandFound, emptyCPCHI );

			if ( !chiFound.m_pEntry || !chiFound.m_pEntry->m_pCg || !chiFound.m_pEntry->m_pExpr || chiFound.m_iTotalCommand < 0 || chiFound.m_iTotalCommand > riCommandNumber )
				 return nullptr;

			// Advance the handle as needed
			pImpl = new CPCHI_t( chiFound );
			rhCombo = AsHandle( pImpl );

			uint64 iCommandFoundAdvance = riCommandNumber - iCommandFound;
			pImpl->AdvanceCommands( iCommandFoundAdvance );

			if ( !pImpl->IsSkipped() )
				return rhCombo;
		}

		for (;;)
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
				rhCombo = nullptr;
				riCommandNumber = iCommandEnd;
				return nullptr;
			}

			// Otherwise we just have to obtain the next combo handle
			riCommandNumber = pImpl->m_iTotalCommand + 1;

			// Delete the old combo handle
			delete pImpl;
			rhCombo = nullptr;

			// Retrieve the next combo handle data
			uint64 iCommandLookup = riCommandNumber;
			CPCHI_t emptyCPCHI;
			CPCHI_t const& chiNext = GetLessOrEq( iCommandLookup, emptyCPCHI );
			Assert( iCommandLookup == riCommandNumber && ( chiNext.m_pEntry ) );

			// Set up the new combo handle
			pImpl = new CPCHI_t( chiNext );
			rhCombo = AsHandle( pImpl );

			if ( !pImpl->IsSkipped() )
				return rhCombo;
		}
	}

	void Combo_FormatCommand( ComboHandle hCombo, gsl::span<char> pchBuffer )
	{
		CPCHI_t* pImpl = FromHandle( hCombo );
		pImpl->FormatCommand( pchBuffer );
	}

	uint64 Combo_GetCommandNum( ComboHandle hCombo )
	{
		if ( CPCHI_t* pImpl = FromHandle( hCombo ) )
			return pImpl->m_iTotalCommand;
		return ~0ULL;
	}

	uint64 Combo_GetComboNum( ComboHandle hCombo )
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