//====== Copyright c 1996-2007, Valve Corporation, All rights reserved. =======//
//
// Purpose:
//
// $NoKeywords: $
//
//=============================================================================//

#pragma once

#include "basetypes.h"
#include "gsl/span"
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace Parser
{
	struct Combo;
}

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
struct ShaderConfig
{
	std::string name;
	std::string_view version;
	std::string_view target;
	uint32_t centroid_mask;
	uint32_t crc32;
	std::vector<Parser::Combo> static_c;
	std::vector<Parser::Combo> dynamic_c;
	std::vector<std::string> skip;
	std::vector<std::string> includes;
};

void SetupConfiguration( const std::vector<ShaderConfig>& configs, const std::filesystem::path& root, bool bVerbose );

struct CfgEntryInfo
{
	std::string_view	m_szName;				// Name of the shader, e.g. "shader_ps20b"
	std::string_view	m_szShaderFileName;		// Name of the src file, e.g. "shader_psxx.fxc"
	std::string_view	m_szShaderVersion;		// Version of shader
	uint64_t			m_numCombos;			// Total possible num of combos, e.g. 1024
	uint64_t			m_numDynamicCombos;		// Num of dynamic combos, e.g. 4
	uint64_t			m_numStaticCombos;		// Num of static combos, e.g. 256
	uint64_t			m_iCommandStart;		// Start command, e.g. 0
	uint64_t			m_iCommandEnd;			// End command, e.g. 1024
	int					m_nCentroidMask;		// Mask of centroid samplers
	uint32_t			m_nCrc32;
};

std::unique_ptr<CfgProcessor::CfgEntryInfo[]> DescribeConfiguration( bool bPrintExpressions );

// Working with combos
struct __ComboHandle
{
	int unused;
};
using ComboHandle = __ComboHandle*;

ComboHandle Combo_GetCombo( uint64_t iCommandNumber );
void Combo_GetNext( uint64_t& riCommandNumber, ComboHandle& rhCombo, uint64_t iCommandEnd );
void Combo_FormatCommandHumanReadable( ComboHandle hCombo, gsl::span<char> pchBuffer );
uint64_t Combo_GetCommandNum( ComboHandle hCombo ) noexcept;
uint64_t Combo_GetComboNum( ComboHandle hCombo ) noexcept;
const CfgEntryInfo* Combo_GetEntryInfo( ComboHandle hCombo ) noexcept;

struct ComboBuildCommand
{
	std::string_view fileName;
	std::string_view shaderModel;
	std::vector<std::pair<std::string_view, std::string_view>> defines;
};
ComboBuildCommand Combo_BuildCommand( ComboHandle hCombo );

ComboHandle Combo_Alloc( ComboHandle hComboCopyFrom ) noexcept;
void Combo_Assign( ComboHandle hComboDst, ComboHandle hComboSrc );
void Combo_Free( ComboHandle& rhComboFree ) noexcept;
}; // namespace CfgProcessor