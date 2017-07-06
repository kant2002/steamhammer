#pragma once

#include "Common.h"
#include "rapidjson\document.h"

namespace UAlbertaBot
{
namespace ParseUtils
{
    void ParseConfigFile(const std::string & filename);
    void ParseTextCommand(const std::string & commandLine);
    BWAPI::Race GetRace(const std::string & raceName);
	bool _ParseStrategy(const rapidjson::Value & strategy, std::string & stratName);

    bool GetBoolFromString(const std::string & str);
	int GetIntByRace(const char * name, const rapidjson::Value & item);
	double GetDoubleByRace(const char * name, const rapidjson::Value & item);
	bool GetBoolByRace(const char * name, const rapidjson::Value & item);
}
}
