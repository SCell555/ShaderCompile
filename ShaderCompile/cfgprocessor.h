//====== Copyright c 1996-2007, Valve Corporation, All rights reserved. =======//
//
// Purpose:
//
// $NoKeywords: $
//
//=============================================================================//

#ifndef CFGPROCESSOR_H
#define CFGPROCESSOR_H
#ifdef _WIN32
	#pragma once
#endif

#include "basetypes.h"
#include <span>
#include <memory>

/*

Layout of the internal structures is as follows:

|-------- shader1.fxc ---------||--- shader2.fxc ---||--------- shader3.fxc -----||-...
| 0 s s 3 s s s s 8 s 10 s s s || s s 2 3 4 s s s 8 || 0 s s s 4 s s s 8 9 s s s ||-...
| 0 1 2 3 4 5 6 7 8 9 10 * * *   14 * * * * *20 * *   23 * * *27 * * * * * * *35    * * *

GetSection( 10 ) -> shader1.fxc
GetSection( 27 ) -> shader3.fxc

GetNextCombo(  3,  3, 14 ) -> shader1.fxc : ( riCommandNumber =  8, rhCombo =  "8" )
GetNextCombo( 10, 10, 14 ) ->   NULL      : ( riCommandNumber = 14, rhCombo = NULL )
GetNextCombo( 22,  8, 36 ) -> shader3.fxc : ( riCommandNumber = 23, rhCombo =  "0" )
GetNextCombo( 29, -1, 36 ) -> shader3.fxc : ( riCommandNumber = 31, rhCombo =  "8" )

*/

namespace CfgProcessor
{
#if 0
// Working with configuration
void ReadConfiguration( const char* configFile );
#endif

struct CfgEntryInfo
{
	const char*	m_szName;			// Name of the shader, e.g. "shader_ps20b"
	const char*	m_szShaderFileName;	// Name of the src file, e.g. "shader_psxx.fxc"
	const char*	m_szShaderVersion;	// Version of shader
	uint64_t	m_numCombos;				// Total possible num of combos, e.g. 1024
	uint64_t	m_numDynamicCombos;		// Num of dynamic combos, e.g. 4
	uint64_t	m_numStaticCombos;		// Num of static combos, e.g. 256
	uint64_t	m_iCommandStart;			// Start command, e.g. 0
	uint64_t	m_iCommandEnd;			// End command, e.g. 1024
	int			m_nCentroidMask;			// Mask of centroid samplers
};

void DescribeConfiguration( std::unique_ptr<CfgEntryInfo[]>& rarrEntries );

// Working with combos
struct __ComboHandle
{
	int unused;
};
using ComboHandle = __ComboHandle*;

ComboHandle Combo_GetCombo( uint64_t iCommandNumber );
ComboHandle Combo_GetNext( uint64_t& riCommandNumber, ComboHandle& rhCombo, uint64_t iCommandEnd );
void Combo_FormatCommand( ComboHandle hCombo, std::span<char> pchBuffer );
void Combo_FormatCommandHumanReadable( ComboHandle hCombo, std::span<char> pchBuffer );
uint64_t Combo_GetCommandNum( ComboHandle hCombo ) noexcept;
uint64_t Combo_GetComboNum( ComboHandle hCombo ) noexcept;
const CfgEntryInfo* Combo_GetEntryInfo( ComboHandle hCombo ) noexcept;

ComboHandle Combo_Alloc( ComboHandle hComboCopyFrom ) noexcept;
void Combo_Assign( ComboHandle hComboDst, ComboHandle hComboSrc );
void Combo_Free( ComboHandle& rhComboFree ) noexcept;
}; // namespace CfgProcessor

#endif // #ifndef CFGPROCESSOR_H