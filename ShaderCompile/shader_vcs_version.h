//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose:
//
//=============================================================================//

#pragma once

#include "basetypes.h"

// 1 = hl2 shipped
// 2 = compressed with diffs version (lostcoast)
// 3 = compressed with bzip
// 4 = v2 + crc32
// 5 = v3 + crc32
// 6 = v5 + duplicate static combo records
static inline constexpr int SHADER_VCS_VERSION_NUMBER = 6;

static inline constexpr int MAX_SHADER_UNPACKED_BLOCK_SIZE = ( 1 << 17 );
static inline constexpr int MAX_SHADER_PACKED_SIZE         = ( 1 + MAX_SHADER_UNPACKED_BLOCK_SIZE );

#pragma pack( 1 )
struct ShaderHeader_t
{
	int32_t m_nVersion;
	int32_t m_nTotalCombos;
	int32_t m_nDynamicCombos;
	uint32_t m_nFlags;
	uint32_t m_nCentroidMask;
	uint32_t m_nNumStaticCombos; // includes sentinal key
	uint32_t m_nSourceCRC32;     // NOTE: If you move this, update copyshaders.pl, *_prep.pl, updateshaders.pl
};
#pragma pack()
static_assert( sizeof( ShaderHeader_t ) == 7 * 4 );

#pragma pack( 1 )
struct ShaderHeader_t_v4 // still used for assembly shaders
{
	int32_t m_nVersion;
	int32_t m_nTotalCombos;
	int32_t m_nDynamicCombos;
	uint32_t m_nFlags;
	uint32_t m_nCentroidMask;
	uint32_t m_nDiffReferenceSize;
	uint32_t m_nSourceCRC32; // NOTE: If you move this, update copyshaders.pl, *_prep.pl, updateshaders.pl
};
#pragma pack()
static_assert( sizeof( ShaderHeader_t_v4 ) == 7 * 4 );

// for old format files
struct ShaderDictionaryEntry_t
{
	int m_Offset;
	int m_Size;
};
static_assert( sizeof( ShaderDictionaryEntry_t ) == 2 * 4 );

// record for one static combo
struct StaticComboRecord_t
{
	uint32_t m_nStaticComboID;
	uint32_t m_nFileOffset;
};
static_assert( sizeof( StaticComboRecord_t ) == 2 * 4 );

struct StaticComboAliasRecord_t // for duplicate static combos
{
	uint32_t m_nStaticComboID;     // this combo
	uint32_t m_nSourceStaticCombo; // the combo it is the same as
};
static_assert( sizeof( StaticComboAliasRecord_t ) == 2 * 4 );