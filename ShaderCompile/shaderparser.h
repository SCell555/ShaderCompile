#pragma once

#include <string>
#include <vector>

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

	bool ValidateVersion( const std::string& ver );
	std::string ConstructName( const std::string& baseName, const std::string& ver );
	bool ParseFile( const std::string& name, const std::string& version, std::vector<Combo>& static_c, std::vector<Combo>& dynamic_c,
		std::vector<std::string>& skip, uint32_t& centroid_mask, std::vector<std::string>& includes );
	void WriteInclude( const std::string& fileName, const std::string& name, const std::vector<Combo>& static_c,
		const std::vector<Combo>& dynamic_c, const std::vector<std::string>& skip );
	bool CheckCrc( const std::string& sourceFile, const std::string& name, uint32_t& crc32 );
}