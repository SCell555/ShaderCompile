
#define WIN32_LEAN_AND_MEAN
#define NOWINRES
#define NOSERVICE
#define NOMCX
#define NOIME
#define NOMINMAX

#include <fstream>
#include <filesystem>
#include <iostream>
#include <numeric>
#include <vector>

#include "shaderparser.h"
#include "termcolor/style.hpp"
#include "termcolors.hpp"
#include "re2/re2.h"
#include "gsl/string_span"
#include "CRC32.hpp"

#pragma comment(lib, "re2.lib")

using namespace std::literals;
namespace fs = std::filesystem;

namespace r
{
	using namespace re2;
	static const RE2 inc( R"reg(#\s*include\s*"(.*)")reg" );
	static const RE2 xbox_reg( R"reg(\[XBOX\])reg" );
	static const RE2 pc_reg( R"reg(\[PC\])reg" );
	static const RE2 start( R"reg(^\s*//\s*(STATIC|DYNAMIC|SKIP|CENTROID)\s*:\s*(.*)$)reg" );
	static const RE2 init( R"reg(\[\s*=\s*([^\]]+)\])reg" );
	static const RE2 static_combo( R"reg(^\s*//\s*STATIC\s*:\s*"(.*)"\s+"(\d+)\.\.(\d+)".*)reg" );
	static const RE2 dynamic_combo( R"reg(^\s*//\s*DYNAMIC\s*:\s*"(.*)"\s+"(\d+)\.\.(\d+)".*)reg" );
	static const RE2 centroid( R"reg(^\s*//\s*CENTROID\s*:\s*TEXCOORD(\d+).*$)reg" );
	static const RE2 version( R"reg(^(.*_[vp]s)(\d\db|\d\d|\dx|xx))reg" );
	static const RE2 c_comment_start( R"reg(^(.*)\/\*)reg");
	static const RE2 c_comment_end( R"reg(\*\/(.*)$)reg");
	static const RE2 c_inline_comment( R"reg(^(.*)\/\*.*?\*\/(.*))reg");
	static const RE2 cpp_comment( R"reg(^(.*)\/\/$)reg");
}

static std::uint32_t lzcnt( std::uint32_t n )
{
	std::uint32_t r = 0;
	if ( n == 0 )
		return 32;

	if (n <= 0x0000FFFF) { r += 16; n <<= 16; }
	if (n <= 0x00FFFFFF) { r += 8;  n <<= 8; }
	if (n <= 0x0FFFFFFF) { r += 4;  n <<= 4; }
	if (n <= 0x3FFFFFFF) { r += 2;  n <<= 2; }
	if (n <= 0x7FFFFFFF) { r += 1;  n <<= 1; }

	return r;
}

Parser::Combo::Combo( const std::string& name, int32_t min, int32_t max, const std::string& init_val ) : name( name ), minVal( min ), maxVal( max ), initVal( init_val )
{
	const auto f = initVal.rfind( ';' );
	if ( f != std::string::npos )
		initVal = initVal.substr( 0, f );
}

bool Parser::ValidateVersion( const std::string& ver )
{
	return ver == "20b"sv || ver == "30"sv || ver == "40"sv || ver == "41"sv || ver == "50"sv || ver == "51"sv;
}

std::string Parser::ConstructName( const std::string& baseName, const std::string& ver )
{
	std::string name, fileVer;
	re2::RE2::PartialMatch( baseName, r::version, &name, &fileVer );
	if ( ver == "20b"sv && name.rfind( "_vs"sv ) == name.length() - 3 )
		return name + "20";
	return name + ver;
}

template <typename T>
static bool ReadFile( const fs::path& name, std::vector<std::string>& includes, T& func )
{
	const auto rawName = name.filename().string();
	const auto parent = name.parent_path();
	includes.emplace_back( rawName );
	std::ifstream file( name );
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
		if ( re2::RE2::PartialMatch( reducedLine.empty() ? line : reducedLine, r::inc, &incl ) && ( reducedLine.empty() ? line : reducedLine ).rfind( "//"sv, 0 ) != 0 )
		{
			reducedLine.clear();
			if ( !ReadFile( parent / incl, includes, func ) )
				return false;
			continue;
		}
		reducedLine.clear();
		func( line );
	}

	if ( cComment )
		std::cout << clr::red << "Unexpected end of  \""sv << rawName << clr::reset << std::endl;

	return !cComment;
}

bool Parser::ParseFile( const std::string& name, const std::string& _version, std::vector<Combo>& static_c, std::vector<Combo>& dynamic_c,
						std::vector<std::string>& skip, uint32_t& centroid_mask, std::vector<std::string>& includes )
{
	using re2::RE2;
	centroid_mask = 0U;
	const auto f = name.find_last_of( '.' );
	const bool isPs = RE2::PartialMatch( f != std::string::npos ? name.substr( 0, f ) : name, RE2( R"reg(_ps(\d\db|\d\d|\dx|xx)$)reg"sv ) );
	const RE2 shouldMatch( isPs ? R"reg(\[ps(\d+\w?)\])reg"sv : R"reg(\[vs(\d+\w?)\])reg"sv );
	const RE2 shouldNotMatch( isPs ? R"reg(\[vs\d+\w?\])reg"sv : R"reg(\[ps\d+\w?\])reg"sv );
	const std::string version = !isPs && _version == "20b"sv ? "20" : _version;

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
			combo( r::static_combo, line, init, static_c );
		else if ( name == "DYNAMIC"sv )
			combo( r::dynamic_combo, line, init, dynamic_c );
		else if ( name == "CENTROID"sv )
		{
			uint32_t v = 0;
			RE2::FullMatch( trim( line ), r::centroid, &v );
			centroid_mask |= 1 << v;
		}
		else
		{
			RE2::GlobalReplace( &value, shouldMatch, {} );
			RE2::Replace( &value, r::pc_reg, {} );
			skip.emplace_back( trim( std::move( value ) ) );
		}
	};

	return ReadFile( name, includes, read );
}

void Parser::WriteInclude( const std::string& fileName, const std::string& name, const std::vector<Combo>& static_c,
							const std::vector<Combo>& dynamic_c, const std::vector<std::string>& skip )
{
	const bool isVs = RE2::PartialMatch( name, RE2( R"reg(_vs(\d\db|\d\d|\dx|xx)$)reg"sv ) );
	if ( fs::exists( fileName ) )
		fs::permissions( fileName, fs::perms::owner_read | fs::perms::owner_write );

	{
		fs::create_directories( fs::path( fileName ).parent_path() );
		std::ofstream file( fileName, std::ios::trunc );
		const auto& writeVars = [&]( const std::string_view& suffix, const std::vector<Combo>& vars, const std::string_view& ctor, uint32_t scale )
		{
			file << "class "sv << name << "_"sv << suffix << "_Index\n{\n";
			const bool hasIfdef = std::find_if( vars.begin(), vars.end(), []( const Combo& c ) { return c.initVal.empty(); } ) != vars.end();
			for ( const Combo& c : vars )
				file << "\tunsigned int m_n"sv << c.name << " : "sv << ( 32 - lzcnt( c.maxVal - c.minVal + 1 ) ) << ";\n"sv;
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
			const std::string& pref = ( isVs ? "vsh_"s : "psh_"s ) + "forgot_to_set_"s + suffixLower + "_"s;
			file << "#define shader"sv << suffix << "Test_"sv << name << " "sv;
			if ( hasIfdef )
				file << std::accumulate( vars.begin(), vars.end(), ""s, [&pref]( const std::string& s, const Combo& c ) { return c.initVal.empty() ? ( s + " + " + pref + c.name ) : s; } ).substr( 3 );
			else
				file << "1"sv;
			file << "\n\n"sv;
		};

		std::string nameUpper( name.length(), ' ' );
		std::transform( name.begin(), name.end(), nameUpper.begin(), []( const char& c ) { return (char)std::toupper( c ); } );
		if ( !skip.empty() )
		{
			file << "// ALL SKIP STATEMENTS THAT AFFECT THIS SHADER!!!\n"sv;
			for ( auto& s : skip )
				file << "// "sv << s << "\n"sv;
			file << "\n"sv;
		}
		file << "#ifndef "sv << nameUpper << "_H\n#define "sv << nameUpper
			<< "_H\n\n" R"(#include "shaderapi/ishaderapi.h")" "\n" R"(#include "shaderapi/ishadershadow.h")" "\n" R"(#include "materialsystem/imaterialvar.h")" "\n\n"sv;

		writeVars( "Static"sv, static_c, "IShaderShadow* pShaderShadow, IMaterialVar** params"sv,
			std::accumulate( dynamic_c.begin(), dynamic_c.end(), 1U, []( uint32_t a, const Combo& b ) { return a * ( b.maxVal - b.minVal + 1 ); } ) );

		file << "\n"sv;

		writeVars( "Dynamic"sv, dynamic_c, "IShaderDynamicAPI* pShaderAPI"sv, 1U );

		file << "\n#endif\t// "sv << nameUpper << "_H"sv;
	}

	fs::permissions( fileName, fs::perms::owner_read );
}

bool Parser::CheckCrc( const std::string& sourceFile, const std::string& name, uint32_t& crc32 )
{
	uint32_t binCrc = 0;
	{
		const auto filePath = fs::path( sourceFile ).parent_path() / "shaders"sv / "fxc"sv / ( name + ".vcs" );
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
	if ( !ReadFile( sourceFile, includes, read ) )
		return false;

	crc32 = CRC32::ProcessSingleBuffer( file.c_str(), file.size() );
	return crc32 == binCrc;
}