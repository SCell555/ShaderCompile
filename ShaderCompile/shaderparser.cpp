
#define WIN32_LEAN_AND_MEAN
#define NOWINRES
#define NOSERVICE
#define NOMCX
#define NOIME
#define NOMINMAX

#include <bit>
#include <fstream>
#include <filesystem>
#include <iostream>
#include <numeric>
#include <vector>

#include "shaderparser.h"
#include "cfgprocessor.h"
#include "termcolor/style.hpp"
#include "termcolors.hpp"
#include "re2/re2.h"
#include "gsl/narrow"
#include "CRC32.hpp"
#include "strmanip.hpp"

// gcc9 for some reason doesn't have this
template <class T, std::enable_if_t<std::is_unsigned_v<T>, int> = 0>
[[nodiscard]] constexpr T bit_width( const T _Val ) noexcept
{
	return static_cast<T>( std::numeric_limits<T>::digits - std::countl_zero( _Val ) );
}

namespace ConfigurationProcessing
{
	std::vector<std::pair<std::string, std::string>> GenerateSkipAsserts( const std::vector<Parser::Combo>& combos, const std::vector<std::string>& skips );
}

using namespace std::literals;
namespace fs = std::filesystem;

namespace r
{
	using namespace re2;
	static const RE2 inc( R"reg(#\s*include\s*"(.*)")reg" );
	static const RE2 xbox_reg( R"reg(\[XBOX\])reg" );
	static const RE2 pc_reg( R"reg(\[PC\])reg" );
	static const RE2 start( R"reg(^\s*//\s*(STATIC|DYNAMIC|SKIP|CENTROID|[VPGDH]S_MAIN)\s*:\s*(.*)$)reg" );
	static const RE2 init( R"reg(\[\s*=\s*([^\]]+)\])reg" );
	static const RE2 static_combo( R"reg(^\s*//\s*STATIC\s*:\s*"(.*)"\s+"(\d+)\.\.(\d+)".*)reg" );
	static const RE2 dynamic_combo( R"reg(^\s*//\s*DYNAMIC\s*:\s*"(.*)"\s+"(\d+)\.\.(\d+)".*)reg" );
	static const RE2 centroid( R"reg(^\s*//\s*CENTROID\s*:\s*TEXCOORD(\d+).*$)reg" );
	static const RE2 base_name( R"reg(^(.*)_[vpgdh]s(\d\db|\d\d|\dx|xx))reg" );
	static const RE2 target( R"reg(^.*_([vpgdh]s)(\d\db|\d\d|\dx|xx))reg" );
	static const RE2 c_comment_start( R"reg(^(.*)\/\*)reg");
	static const RE2 c_comment_end( R"reg(\*\/(.*)$)reg");
	static const RE2 c_inline_comment( R"reg(^(.*)\/\*.*?\*\/(.*))reg");
	static const RE2 cpp_comment( R"reg(^(.*)\/\/$)reg");
}

Parser::Combo::Combo( const std::string& name, int32_t min, int32_t max, const std::string& init_val ) : name( name ), minVal( min ), maxVal( max ), initVal( init_val )
{
	const auto f = initVal.rfind( ';' );
	if ( f != std::string::npos )
		initVal = initVal.substr( 0, f );
}

std::string Parser::ConstructName( const std::string& baseName, const std::string_view& target, const std::string_view& ver )
{
	std::string name;
	if ( re2::RE2::PartialMatch( baseName, r::base_name, &name ) )
		return name + "_"s + std::string( target ) + std::string( ver );
	return fs::path( baseName ).stem().string() + "_"s + std::string( target ) + std::string( ver );
}

std::string_view Parser::GetTarget( const std::string& baseName )
{
	re2::StringPiece target;
	re2::RE2::PartialMatch( baseName, r::target, &target );
	return { target.data(), target.size() };
}

template <typename T>
static bool ReadFile( const fs::path& name, const std::string& srcPath, std::vector<std::string>& includes, T& func )
{
	const auto fullPath = fs::absolute( name );
	const auto parent = fullPath.parent_path();
	if ( parent.string().size() < srcPath.size() )
	{
		std::cout << clr::red << "Leaving root directory!"sv << clr::reset << std::endl;
		return false;
	}

	auto rawName = fullPath.string().substr( srcPath.size() + 1 );
	std::for_each( rawName.begin(), rawName.end(), []( char& c ) { if ( c == '\\' ) c = '/'; } );
	includes.emplace_back( rawName );
	std::ifstream file( fullPath );
	if ( file.fail() )
	{
		std::cout << clr::red << "File \""sv << rawName << "\" does not exist"sv << clr::reset << std::endl;
		return false;
	}

	bool cComment = false;
	for ( std::string line, reducedLine, incl, c1, c2; std::getline( file, line ); )
	{
		if ( !cComment )
		{
			while ( re2::RE2::FullMatch( line, r::c_inline_comment, &c1, &c2 ) )
				line = c1 + c2;
		}

		/*if ( !cComment && re2::RE2::FullMatch( line, r::c_comment_start, &c1 ) )
		{
			line = c1;
			cComment = true;
		}
		else if ( cComment && re2::RE2::FullMatch( line, r::c_comment_end, &c1 ) )
		{
			line = c1;
			cComment = false;
		}
		else if ( cComment )
			continue;*/
		re2::RE2::FullMatch( line, r::cpp_comment, &reducedLine );
		if ( re2::RE2::PartialMatch( reducedLine.empty() ? line : reducedLine, r::inc, &incl ) && !( reducedLine.empty() ? line : reducedLine ).starts_with( "//"sv ) )
		{
			if ( V_IsAbsolutePath( incl.c_str() ) )
			{
				std::cout << clr::red << "Absolute path \""sv << incl << "\" in #include, aborting!"sv << clr::reset << std::endl;
				return false;
			}

			reducedLine.clear();
			ReadFile( parent / incl, srcPath, includes, func );
			continue;
		}
		reducedLine.clear();
		func( line );
	}

	if ( cComment )
		std::cout << clr::red << "Unexpected end of  \""sv << rawName << clr::reset << std::endl;

	return !cComment;
}

static constexpr const char validL[] = { 'v', 'p', 'g', 'h', 'd' };
static constexpr const char validU[] = { 'V', 'P', 'G', 'H', 'D' };
bool Parser::ParseFile( const fs::path& name, const std::string& root, const std::string_view& target, const std::string_view& version, CfgProcessor::ShaderConfig& conf )
{
	using re2::RE2;
	conf.centroid_mask = 0U;
	const auto nameS = name.string();
	const auto f = nameS.find_last_of( '.' );
	char regMatch[] = { R"reg(\[ s(\d+\w?)\])reg" };
	char regNotMatch[] = { R"reg(\[[    ]s\d+\w?\])reg" };
	std::string mainCat = " S_MAIN"s;

	regMatch[2] = target[0];
	for ( int i = 0, j = 0; i < 5; ++i )
		if ( validL[i] != target[0] )
			regNotMatch[3 + j++] = validL[i];
	mainCat[0] = toupper( target[0] );
	const RE2 shouldMatch( regMatch );
	const RE2 shouldNotMatch( regNotMatch );
	conf.main = "main"s;

	const auto& trim = []( std::string s ) -> std::string
	{
		s.erase( std::find_if( s.rbegin(), s.rend(), []( int ch ) { return !std::isspace( ch ); } ).base(), s.end() );
		return s;
	};

	const auto& combo = [&shouldMatch, &trim]( const RE2& regex, std::string line, const std::string& init, std::vector<Combo>& out )
	{
		std::string name;
		int32_t min, max;
		RE2::GlobalReplace( &line, shouldMatch, {} );
		RE2::Replace( &line, r::pc_reg, {} );
		RE2::Replace( &line, r::init, {} );
		RE2::FullMatch( trim( std::move( line ) ), regex, &name, &min, &max );
		out.emplace_back( name, min, max, init );
	};

	const auto& read = [&]( const std::string& line ) -> void
	{
		std::string name, value, matchVer, init;
		if ( !RE2::FullMatch( line, r::start, &name, &value ) )
			return;
		if ( RE2::PartialMatch( line, r::xbox_reg ) )
			return;
		if ( RE2::PartialMatch( line, shouldNotMatch ) )
			return;

		bool matched = true;
		re2::StringPiece p( line );
		while ( RE2::FindAndConsume( &p, shouldMatch, &matchVer ) )
		{
			if ( matchVer == version )
			{
				matched = true;
				break;
			}
			matched = false;
		}
		if ( !matched )
			return;
		RE2::PartialMatch( line, r::init, &init );
		if ( name == "STATIC"sv )
			combo( r::static_combo, line, init, conf.static_c );
		else if ( name == "DYNAMIC"sv )
			combo( r::dynamic_combo, line, init, conf.dynamic_c );
		else if ( name == "CENTROID"sv )
		{
			uint32_t v = 0;
			RE2::FullMatch( trim( line ), r::centroid, &v );
			conf.centroid_mask |= 1 << v;
		}
		else if ( name == "SKIP"sv )
		{
			RE2::GlobalReplace( &value, shouldMatch, {} );
			RE2::Replace( &value, r::pc_reg, {} );
			conf.skip.emplace_back( trim( std::move( value ) ) );
		}
		else if ( name == mainCat )
		{
			conf.main = value;
		}
	};

	return ReadFile( name, root, conf.includes, read );
}

void Parser::WriteInclude( const fs::path& fileName, const std::string& name, const std::string_view& target, const std::vector<Combo>& static_c,
							const std::vector<Combo>& dynamic_c, const std::vector<std::string>& skip, bool writeSCI )
{
	if ( fs::exists( fileName ) )
		fs::permissions( fileName, fs::perms::owner_read | fs::perms::owner_write );

	char prefix[] = { " sh_" };
	prefix[0] = target[0];

	{
		fs::create_directories( fileName.parent_path() );
		std::ofstream file( fileName, std::ios::trunc | std::ios::binary );
		const auto& writeVars = [&]( const std::string_view& suffix, const std::vector<Combo>& vars, const std::string_view& ctor, uint32_t scale, bool dynamic )
		{
			file << "class "sv << name << "_"sv << suffix << "_Index\n{\n";
			const bool hasIfdef = std::find_if( vars.begin(), vars.end(), []( const Combo& c ) { return c.initVal.empty(); } ) != vars.end();
			for ( const Combo& c : vars )
				file << "\tunsigned int m_n"sv << c.name << " : "sv << bit_width( uint32_t( c.maxVal - c.minVal + 1 ) ) << ";\n"sv;
			if ( hasIfdef )
				file << "#ifdef _DEBUG\n"sv;
			for ( const Combo& c : vars )
				if ( c.initVal.empty() )
					file << "\tbool m_b"sv << c.name << " : 1;\n"sv;
			if ( hasIfdef )
				file << "#endif\t// _DEBUG\n"sv;
			file << "public:\n"sv;
			for ( const Combo& c : vars )
			{
				file << "\tvoid Set"sv << c.name << "( int i )\n\t{\n"sv;
				file << "\t\tAssert( i >= "sv << c.minVal << " && i <= "sv << c.maxVal << " );\n"sv;
				if ( c.minVal == 0 )
					file << "\t\tm_n"sv << c.name << " = i;\n"sv;
				else
					file << "\t\tm_n"sv << c.name << " = i - "sv << c.minVal << ";\n"sv;
				if ( c.initVal.empty() )
					file << "#ifdef _DEBUG\n\t\tm_b"sv << c.name << " = true;\n#endif\t// _DEBUG\n"sv;
				file << "\t}\n\n"sv;
			}
			file << "\t"sv << name << "_"sv << suffix << "_Index( "sv << ctor << " )\n\t{\n"sv;
			for ( const Combo& c : vars )
				file << "\t\tm_n"sv << c.name << " = "sv << ( c.initVal.empty() ? "0"sv : c.initVal ) << ";\n"sv;
			if ( hasIfdef )
				file << "#ifdef _DEBUG\n"sv;
			for ( const Combo& c : vars )
				if ( c.initVal.empty() )
					file << "\t\tm_b"sv << c.name << " = false;\n"sv;
			if ( hasIfdef )
				file << "#endif\t// _DEBUG\n"sv;
			file << "\t}\n\n\tint GetIndex() const\n\t{\n"sv;
			if ( vars.empty() )
				file << "\t\treturn 0;\n"sv;
			else
			{
				if ( hasIfdef )
					file << "\t\tAssert( "sv << std::accumulate( vars.begin(), vars.end(), ""s, []( const std::string& s, const Combo& c ) { return c.initVal.empty() ? ( s + " && m_b" + c.name ) : s; } ).substr( 4 ) << " );\n"sv;
				const auto skipAsserts = ConfigurationProcessing::GenerateSkipAsserts( dynamic ? dynamic_c : static_c, skip );
				for ( const auto& [msg, check] : skipAsserts )
					file << "\t\tAssertMsg( !"sv << check << ", \"Invalid combo combination "sv << msg << "\" );\n"sv;
				file << "\t\treturn "sv;
				for ( const Combo& c : vars )
				{
					file << "( "sv << scale << " * m_n"sv << c.name << " ) + "sv;
					scale *= c.maxVal - c.minVal + 1;
				}
				file << "0;\n"sv;
			}
			file << "\t}\n};\n\n"sv;

			std::string suffixLower( suffix.length(), ' ' );
			std::transform( suffix.begin(), suffix.end(), suffixLower.begin(), []( const char& c ) { return (char)std::tolower( c ); } );
			const std::string& pref = prefix + "forgot_to_set_"s + suffixLower + "_"s;
			file << "#define shader"sv << suffix << "Test_"sv << name << " "sv;
			if ( hasIfdef )
				file << std::accumulate( vars.begin(), vars.end(), ""s, [&pref]( const std::string& s, const Combo& c ) { return c.initVal.empty() ? ( s + " + " + pref + c.name ) : s; } ).substr( 3 );
			else
				file << "1"sv;
			file << "\n\n"sv;
		};

		if ( !skip.empty() )
		{
			file << "// ALL SKIP STATEMENTS THAT AFFECT THIS SHADER!!!\n"sv;
			for ( auto& s : skip )
				file << "// "sv << s << "\n"sv;
			file << "\n"sv;
		}

		file << "#pragma once\n" R"(#include "shaderlib/cshader.h")" "\n"sv;

		writeVars( "Static"sv, static_c, "IShaderShadow* pShaderShadow, IMaterialVar** params"sv,
			std::accumulate( dynamic_c.begin(), dynamic_c.end(), 1U, []( uint32_t a, const Combo& b ) { return a * ( b.maxVal - b.minVal + 1 ); } ), false );

		file << "\n"sv;

		writeVars( "Dynamic"sv, dynamic_c, "IShaderDynamicAPI* pShaderAPI"sv, 1U, true );

		if ( writeSCI )
		{
			file << "\n"sv;

			const auto& writeComboArray = [&file, &name]( bool dynamic, const std::vector<Combo>& combos )
			{
				file << "static constexpr ShaderComboInformation_t s_"sv << ( dynamic ? "Dynamic"sv : "Static"sv ) << "ComboArray_"sv << name << "[] =\n{\n"sv;
				for ( const Combo& c : combos )
					file << "\t{ \""sv << c.name << "\", "sv << c.minVal << ", "sv << c.maxVal << " },\n"sv;
				file << "};\n"sv;
			};

			if ( !dynamic_c.empty() )
				writeComboArray( true, dynamic_c );

			if ( !static_c.empty() )
				writeComboArray( false, static_c );

			file << "static constexpr ShaderComboSemantics_t "sv << name << "_combos =\n{\n\t\""sv << name << "\", "sv;

			if ( !dynamic_c.empty() )
				file << "s_DynamicComboArray_"sv << name << ", "sv << dynamic_c.size() << ", "sv;
			else
				file << "nullptr, 0, "sv;

			if ( !static_c.empty() )
				file << "s_StaticComboArray_"sv << name << ", "sv << static_c.size();
			else
				file << "nullptr, 0"sv;

			file << "\n};\n"sv;

			file << "inline const class ConstructMe_"sv << name << "\n{\npublic:\n\tConstructMe_"sv << name << "()\n\t{\n\t\tGetShaderDLL()->AddShaderComboInformation( &"sv << name << "_combos );\n\t}\n} s_ConstuctMe_"sv << name << ";"sv;
		}
	}

	fs::permissions( fileName, fs::perms::owner_read );
}

bool Parser::CheckCrc( const fs::path& sourceFile, const std::string& root, const std::string& name, uint32_t& crc32 )
{
	uint32_t binCrc = 0;
	{
		const auto filePath = sourceFile.parent_path() / "shaders"sv / "fxc"sv / ( name + ".vcs" );
		std::ifstream file( filePath, std::ios::binary );
		if ( file )
		{
			file.seekg( 6 * 4, std::ios_base::beg );
			file.read( reinterpret_cast<char*>( &binCrc ), sizeof( uint32_t ) );
		}
	}

	std::string file;
	std::vector<std::string> includes;
	const auto& read = [&file]( const std::string& line )
	{
		file += line + "\n";
	};
	if ( !ReadFile( sourceFile, root, includes, read ) )
		return false;

	crc32 = CRC32::ProcessSingleBuffer( file.c_str(), file.size() );
	return crc32 == binCrc;
}