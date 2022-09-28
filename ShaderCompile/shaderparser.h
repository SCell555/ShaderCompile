#pragma once

#include <string>
#include <string_view>
#include <vector>
#include <filesystem>

namespace CfgProcessor
{
	struct ShaderConfig;
}

namespace Parser
{
	struct Combo
	{
		std::string name;
		int32_t minVal;
		int32_t maxVal;
		std::string initVal;

		Combo( const std::string& name, int32_t min, int32_t max, const std::string& init_val );
	};

	std::string ConstructName( const std::string& baseName, const std::string_view& target, const std::string_view& ver );
	std::string_view GetTarget( const std::string& baseName );
	bool ParseFile( const std::filesystem::path& name, const std::string& root, const std::string_view& target, const std::string_view& version, CfgProcessor::ShaderConfig& conf );
	void WriteInclude( const std::filesystem::path& fileName, const std::string& name, const std::string_view& target, const std::vector<Combo>& static_c,
		const std::vector<Combo>& dynamic_c, const std::vector<std::string>& skip, bool writeSCI );
	bool CheckCrc( const std::filesystem::path& sourceFile, const std::string& root, const std::string& name, uint32_t& crc32 );
}