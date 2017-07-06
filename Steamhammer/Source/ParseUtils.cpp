#include "ParseUtils.h"
#include "JSONTools.h"
#include "BuildOrder.h"
#include "StrategyManager.h"

#include <random>
#include <regex>

using namespace UAlbertaBot;

// Parse the JSON configuration file into Config:: variables.
void ParseUtils::ParseConfigFile(const std::string & filename)
{
    rapidjson::Document doc;

	// Calculate our race and the matchup as C strings.
	// The race is spelled out: Terran Protoss Zerg Unknown
	// where "Unknown" means the enemy picked Random.
	// The matchup is abbreviated: ZvT

	const std::string ourRaceStr(BWAPI::Broodwar->self()->getRace().getName());
	const std::string theirRaceStr(BWAPI::Broodwar->enemy()->getRace().getName());
	const std::string matchupStr(ourRaceStr.substr(0, 1) + "v" + theirRaceStr.substr(0, 1));

	const char * ourRace = ourRaceStr.c_str();
	const char * matchup = matchupStr.c_str();

    std::string config = FileUtils::ReadFile(filename);

    if (config.length() == 0)
    {
        return;
    }

    Config::ConfigFile::ConfigFileFound = true;

    bool parsingFailed = doc.Parse(config.c_str()).HasParseError();
    if (parsingFailed)
    {
        return;
    }

    // Parse the Bot Info
    if (doc.HasMember("Bot Info") && doc["Bot Info"].IsObject())
    {
        const rapidjson::Value & info = doc["Bot Info"];
        JSONTools::ReadString("BotName", info, Config::BotInfo::BotName);
        JSONTools::ReadString("Authors", info, Config::BotInfo::Authors);
        JSONTools::ReadBool("PrintInfoOnStart", info, Config::BotInfo::PrintInfoOnStart);
    }

    // Parse the BWAPI Options
    if (doc.HasMember("BWAPI") && doc["BWAPI"].IsObject())
    {
        const rapidjson::Value & bwapi = doc["BWAPI"];
        JSONTools::ReadInt("SetLocalSpeed", bwapi, Config::BWAPIOptions::SetLocalSpeed);
        JSONTools::ReadInt("SetFrameSkip", bwapi, Config::BWAPIOptions::SetFrameSkip);
        JSONTools::ReadBool("UserInput", bwapi, Config::BWAPIOptions::EnableUserInput);
        JSONTools::ReadBool("CompleteMapInformation", bwapi, Config::BWAPIOptions::EnableCompleteMapInformation);
    }

    // Parse the Micro Options
    if (doc.HasMember("Micro") && doc["Micro"].IsObject())
    {
        const rapidjson::Value & micro = doc["Micro"];
        JSONTools::ReadBool("UseSparcraftSimulation", micro, Config::Micro::UseSparcraftSimulation);
        JSONTools::ReadBool("KiteWithRangedUnits", micro, Config::Micro::KiteWithRangedUnits);
        JSONTools::ReadBool("WorkersDefendRush", micro, Config::Micro::WorkersDefendRush);
        JSONTools::ReadInt("RetreatMeleeUnitShields", micro, Config::Micro::RetreatMeleeUnitShields);
        JSONTools::ReadInt("RetreatMeleeUnitHP", micro, Config::Micro::RetreatMeleeUnitHP);
        JSONTools::ReadInt("RegroupRadius", micro, Config::Micro::CombatRegroupRadius);
        JSONTools::ReadInt("UnitNearEnemyRadius", micro, Config::Micro::UnitNearEnemyRadius);
		JSONTools::ReadInt("ScoutDefenseRadius", micro, Config::Micro::ScoutDefenseRadius);

        if (micro.HasMember("KiteLongerRangedUnits") && micro["KiteLongerRangedUnits"].IsArray())
        {
            const rapidjson::Value & kite = micro["KiteLongerRangedUnits"];

            for (size_t i(0); i < kite.Size(); ++i)
            {
                if (kite[i].IsString())
                {
					// TODO what a crude way of doing this...
                    MacroAct type(kite[i].GetString());
                    Config::Micro::KiteLongerRangedUnits.insert(type.getUnitType());
                }
            }
        }
    }

    // Parse the Macro Options
    if (doc.HasMember("Macro") && doc["Macro"].IsObject())
    {
        const rapidjson::Value & macro = doc["Macro"];
        JSONTools::ReadInt("BOSSFrameLimit", macro, Config::Macro::BOSSFrameLimit);
        JSONTools::ReadInt("BuildingSpacing", macro, Config::Macro::BuildingSpacing);
        JSONTools::ReadInt("PylonSpacing", macro, Config::Macro::PylonSpacing);
        JSONTools::ReadInt("WorkersPerRefinery", macro, Config::Macro::WorkersPerRefinery);
    }

    // Parse the Debug Options
    if (doc.HasMember("Debug") && doc["Debug"].IsObject())
    {
        const rapidjson::Value & debug = doc["Debug"];
        JSONTools::ReadString("ErrorLogFilename", debug, Config::Debug::ErrorLogFilename);
        JSONTools::ReadBool("LogAssertToErrorFile", debug, Config::Debug::LogAssertToErrorFile);
        JSONTools::ReadBool("DrawGameInfo", debug, Config::Debug::DrawGameInfo);
		JSONTools::ReadBool("DrawStrategySketch", debug, Config::Debug::DrawStrategySketch);
		JSONTools::ReadBool("DrawBuildOrderSearchInfo", debug, Config::Debug::DrawBuildOrderSearchInfo);
        JSONTools::ReadBool("DrawUnitHealthBars", debug, Config::Debug::DrawUnitHealthBars);
        JSONTools::ReadBool("DrawResourceInfo", debug, Config::Debug::DrawResourceInfo);
        JSONTools::ReadBool("DrawWorkerInfo", debug, Config::Debug::DrawWorkerInfo);
        JSONTools::ReadBool("DrawProductionInfo", debug, Config::Debug::DrawProductionInfo);
        JSONTools::ReadBool("DrawScoutInfo", debug, Config::Debug::DrawScoutInfo);
        JSONTools::ReadBool("DrawSquadInfo", debug, Config::Debug::DrawSquadInfo);
        JSONTools::ReadBool("DrawCombatSimInfo", debug, Config::Debug::DrawCombatSimulationInfo);
        JSONTools::ReadBool("DrawBuildingInfo", debug, Config::Debug::DrawBuildingInfo);
        JSONTools::ReadBool("DrawModuleTimers", debug, Config::Debug::DrawModuleTimers);
        JSONTools::ReadBool("DrawMouseCursorInfo", debug, Config::Debug::DrawMouseCursorInfo);
        JSONTools::ReadBool("DrawEnemyUnitInfo", debug, Config::Debug::DrawEnemyUnitInfo);
        JSONTools::ReadBool("DrawBWTAInfo", debug, Config::Debug::DrawBWTAInfo);
        JSONTools::ReadBool("DrawMapGrid", debug, Config::Debug::DrawMapGrid);
		JSONTools::ReadBool("DrawBaseInfo", debug, Config::Debug::DrawBaseInfo);
		JSONTools::ReadBool("DrawStrategyBossInfo", debug, Config::Debug::DrawStrategyBossInfo);
		JSONTools::ReadBool("DrawUnitTargetInfo", debug, Config::Debug::DrawUnitTargetInfo);
        JSONTools::ReadBool("DrawReservedBuildingTiles", debug, Config::Debug::DrawReservedBuildingTiles);
        JSONTools::ReadBool("DrawBOSSStateInfo", debug, Config::Debug::DrawBOSSStateInfo); 
    }

    // Parse the Module Options
    if (doc.HasMember("Modules") && doc["Modules"].IsObject())
    {
        const rapidjson::Value & module = doc["Modules"];

        JSONTools::ReadBool("UseStrategyIO", module, Config::Modules::UsingStrategyIO);
    }

    // Parse the Tool Options
    if (doc.HasMember("Tools") && doc["Tools"].IsObject())
    {
        const rapidjson::Value & tool = doc["Tools"];

        JSONTools::ReadInt("MapGridSize", tool, Config::Tools::MAP_GRID_SIZE);
    }

    // Parse the Strategy Options
    if (doc.HasMember("Strategy") && doc["Strategy"].IsObject())
    {
        const rapidjson::Value & strategy = doc["Strategy"];

        // read in the various strategic elements
        JSONTools::ReadBool("ScoutHarassEnemy", strategy, Config::Strategy::ScoutHarassEnemy);
        JSONTools::ReadString("ReadDirectory", strategy, Config::Strategy::ReadDir);
        JSONTools::ReadString("WriteDirectory", strategy, Config::Strategy::WriteDir);

        // If we have set a strategy for the current matchup, set it.
		std::string strategyName;
		if (strategy.HasMember(matchup) && _ParseStrategy(strategy[matchup], strategyName))
        {
			Config::Strategy::StrategyName = strategyName;
		}
		// Failing that, look for a strategy for the current race.
		else if (strategy.HasMember(ourRace) && _ParseStrategy(strategy[ourRace], strategyName))
		{
			Config::Strategy::StrategyName = strategyName;
		}

        // check if we are using an enemy specific strategy
        JSONTools::ReadBool("UseEnemySpecificStrategy", strategy, Config::Strategy::UseEnemySpecificStrategy);
        if (Config::Strategy::UseEnemySpecificStrategy && strategy.HasMember("EnemySpecificStrategy") && strategy["EnemySpecificStrategy"].IsObject())
        {
            const std::string enemyName = BWAPI::Broodwar->enemy()->getName();
            const rapidjson::Value & specific = strategy["EnemySpecificStrategy"];

            // check to see if our current enemy name is listed anywhere in the specific strategies
            if (specific.HasMember(enemyName.c_str()) && specific[enemyName.c_str()].IsObject())
            {
                const rapidjson::Value & enemyStrategies = specific[enemyName.c_str()];

				// if that enemy has a strategy listed for our current race, use it                                              
				if (enemyStrategies.HasMember(ourRace) && enemyStrategies[ourRace].IsString())
				{
					Config::Strategy::StrategyName = enemyStrategies[ourRace].GetString();
					Config::Strategy::FoundEnemySpecificStrategy = true;
				}
				// Or if the enemy has a strategy mix, do that.
				else if (_ParseStrategy(enemyStrategies, strategyName))
                {
					Config::Strategy::StrategyName = strategyName;
                    Config::Strategy::FoundEnemySpecificStrategy = true;
                }
            }
        }

        // Parse all the Strategies
        if (strategy.HasMember("Strategies") && strategy["Strategies"].IsObject())
        {
            const rapidjson::Value & strategies = strategy["Strategies"];
            for (rapidjson::Value::ConstMemberIterator itr = strategies.MemberBegin(); itr != strategies.MemberEnd(); ++itr)
            {
                const std::string &         name = itr->name.GetString();
                const rapidjson::Value &    val  = itr->value;

                BWAPI::Race strategyRace;
                if (val.HasMember("Race") && val["Race"].IsString())
                {
                    strategyRace = GetRace(val["Race"].GetString());
                }
                else
                {
                    UAB_ASSERT_WARNING(false, "Strategy must have a Race string. Skipping strategy %s", name.c_str());
                    continue;
                }

                BuildOrder buildOrder(strategyRace);
                if (val.HasMember("OpeningBuildOrder") && val["OpeningBuildOrder"].IsArray())
                {
                    const rapidjson::Value & build = val["OpeningBuildOrder"];

                    for (size_t b(0); b < build.Size(); ++b)
                    {
                        if (build[b].IsString())
                        {
							std::string itemName = build[b].GetString();

							int unitCount = 1;    // the default count

							// You can specify a count, like "6 x mutalisk". The spaces are required.
							// Mostly useful for units!
							std::regex countRegex("([0-9]+)\\s+x\\s+([a-zA-Z_]+)");
							std::smatch m;
							if (std::regex_match(itemName, m, countRegex)) {
								unitCount = GetIntFromString(m[1].str());
								itemName = m[2].str();
							}

							MacroAct act(itemName);

							if (act.getRace() != BWAPI::Races::None || act.isCommand())
                            {
								for (int i = 0; i < unitCount; ++i)
								{
									buildOrder.add(act);
								}
                            }
                        }
                        else
                        {
                            UAB_ASSERT_WARNING(false, "Build order item must be a string %s", name.c_str());
                            continue;
                        }
                    }
                }

                StrategyManager::Instance().addStrategy(name, Strategy(name, strategyRace, buildOrder));
            }
        }
    }

    Config::ConfigFile::ConfigFileParsed = true;
}


void ParseUtils::ParseTextCommand(const std::string & commandString)
{
    std::stringstream ss(commandString);

    std::string command;
    std::transform(command.begin(), command.end(), command.begin(), ::tolower);

    std::string variableName;
    std::transform(variableName.begin(), variableName.end(), variableName.begin(), ::tolower);

    std::string val;

    ss >> command;
    ss >> variableName;
    ss >> val;

    if (command == "/set")
    {
        // BWAPI options
        if (variableName == "setlocalspeed") { Config::BWAPIOptions::SetLocalSpeed = GetIntFromString(val); BWAPI::Broodwar->setLocalSpeed(Config::BWAPIOptions::SetLocalSpeed); }
        else if (variableName == "setframeskip") { Config::BWAPIOptions::SetFrameSkip = GetIntFromString(val); BWAPI::Broodwar->setFrameSkip(Config::BWAPIOptions::SetFrameSkip); }
        else if (variableName == "userinput") { Config::BWAPIOptions::EnableUserInput = GetBoolFromString(val); if (Config::BWAPIOptions::EnableUserInput) BWAPI::Broodwar->enableFlag(BWAPI::Flag::UserInput); }
        else if (variableName == "completemapinformation") { Config::BWAPIOptions::EnableCompleteMapInformation = GetBoolFromString(val); if (Config::BWAPIOptions::EnableCompleteMapInformation) BWAPI::Broodwar->enableFlag(BWAPI::Flag::UserInput); }
        
        // Micro Options
        else if (variableName == "usesparcraftsimulation") { Config::Micro::UseSparcraftSimulation = GetBoolFromString(val); }
        else if (variableName == "workersdefendrush") { Config::Micro::WorkersDefendRush = GetBoolFromString(val); }
        else if (variableName == "regroupradius") { Config::Micro::CombatRegroupRadius = GetIntFromString(val); }
        else if (variableName == "unitnearenemyradius") { Config::Micro::UnitNearEnemyRadius = GetIntFromString(val); }

        // Macro Options
        else if (variableName == "buildingspacing") { Config::Macro::BuildingSpacing = GetIntFromString(val); }
        else if (variableName == "pylonspacing") { Config::Macro::PylonSpacing = GetIntFromString(val); }

        // Debug Options
        else if (variableName == "errorlogfilename") { Config::Debug::ErrorLogFilename = val; }
        else if (variableName == "drawbuildordersearchinfo") { Config::Debug::DrawBuildOrderSearchInfo = GetBoolFromString(val); }
        else if (variableName == "drawunithealthbars") { Config::Debug::DrawUnitHealthBars = GetBoolFromString(val); }
        else if (variableName == "drawproductioninfo") { Config::Debug::DrawProductionInfo = GetBoolFromString(val); }
        else if (variableName == "drawenemyunitinfo") { Config::Debug::DrawEnemyUnitInfo = GetBoolFromString(val); }
        else if (variableName == "drawmoduletimers") { Config::Debug::DrawModuleTimers = GetBoolFromString(val); }
        else if (variableName == "drawresourceinfo") { Config::Debug::DrawResourceInfo = GetBoolFromString(val); }
        else if (variableName == "drawcombatsiminfo") { Config::Debug::DrawCombatSimulationInfo = GetBoolFromString(val); }
        else if (variableName == "drawunittargetinfo") { Config::Debug::DrawUnitTargetInfo = GetBoolFromString(val); }
        else if (variableName == "drawbwtainfo") { Config::Debug::DrawBWTAInfo = GetBoolFromString(val); }
        else if (variableName == "drawmapgrid") { Config::Debug::DrawMapGrid = GetBoolFromString(val); }
        else if (variableName == "drawsquadinfo") { Config::Debug::DrawSquadInfo = GetBoolFromString(val); }
        else if (variableName == "drawworkerinfo") { Config::Debug::DrawWorkerInfo = GetBoolFromString(val); }
        else if (variableName == "drawmousecursorinfo") { Config::Debug::DrawMouseCursorInfo = GetBoolFromString(val); }
        else if (variableName == "drawbuildinginfo") { Config::Debug::DrawBuildingInfo = GetBoolFromString(val); }
        else if (variableName == "drawreservedbuildingtiles") { Config::Debug::DrawReservedBuildingTiles = GetBoolFromString(val); }

        // Module Options
        else if (variableName == "usestrategyio") { Config::Modules::UsingStrategyIO = GetBoolFromString(val); }

        else { UAB_ASSERT_WARNING(false, "Unknown variable name for /set: %s", variableName.c_str()); }
    }
    else
    {
        UAB_ASSERT_WARNING(false, "Unknown command: %s", command.c_str());
    }
}

BWAPI::Race ParseUtils::GetRace(const std::string & raceName)
{
    if (raceName == "Protoss")
    {
        return BWAPI::Races::Protoss;
    }

    if (raceName == "Terran")
    {
        return BWAPI::Races::Terran;
    }

    if (raceName == "Zerg")
    {
        return BWAPI::Races::Zerg;
    }

    if (raceName == "Random")
    {
        return BWAPI::Races::Random;
    }

    UAB_ASSERT_WARNING(false, "Race not found: %s", raceName.c_str());
    return BWAPI::Races::None;
}

// Parse a strategy declaration and return the chosen strategy.
// If a strategy, return it in stratName:
//   "Zerg_11Gas10Pool"
// If a strategy mix declaration, pick the strategy randomly:
//   { "StrategyMix" : [
//     { "Weight" : 30, "Strategy" : "Zerg_4PoolFast" },
//     { "Weight" : 20, "Strategy" : "Zerg_4PoolSlow" },
//     { "Weight" : 50, "Strategy" : "Zerg_5Pool" }
//   ]},
// Return whether parsing was successful.
// Internal routine not for public use.
bool ParseUtils::_ParseStrategy(const rapidjson::Value & item, std::string & stratName)
{
	if (item.IsString())
	{
		stratName = item.GetString();
		return true;
	}

	if (item.IsObject() && item.HasMember("StrategyMix") && item["StrategyMix"].IsArray())
	{
		const rapidjson::Value & mix = item["StrategyMix"];

		std::vector<std::string> strategies;    // strategy name
		std::vector<int> weights;               // cumulative weight of strategy
		int totalWeight = 0;                    // cumulative weight of last strategy (so far)

		// 1. Collect the weights and strategies.
		for (size_t i(0); i < mix.Size(); ++i)
		{
			if (mix[i].IsObject() &&
				mix[i].HasMember("Strategy") && mix[i]["Strategy"].IsString() &&
				mix[i].HasMember("Weight") && mix[i]["Weight"].IsNumber())
			{
				strategies.push_back(mix[i]["Strategy"].GetString());
				int weight = mix[i]["Weight"].GetInt();
				totalWeight += weight;
				weights.push_back(totalWeight);
			}
			else {
				return false;
			}
		}

		// 2. Choose a strategy at random by weight.
		std::uniform_int_distribution<int> uniform_dist(0, totalWeight-1);
		std::random_device rng_seed;
		std::mt19937 rng(rng_seed());
		int w = uniform_dist(rng);        // a lot of trouble to get one random number...
		for (size_t i = 0; i < weights.size(); ++i)
		{
			if (w < weights[i])
			{
				stratName = strategies[i];
				return true;
			}
		}
		UAB_ASSERT(false, "random strategy fell through");
	}

	return false;
}

bool ParseUtils::GetBoolFromString(const std::string & str)
{
	std::string boolStr(str);
	std::transform(boolStr.begin(), boolStr.end(), boolStr.begin(), ::tolower);

	if (boolStr == "true" || boolStr == "t")
	{
		return true;
	}
	else if (boolStr == "false" || boolStr == "f")
	{
		return false;
	}
	else
	{
		UAB_ASSERT_WARNING(false, "Unknown bool from string: %s", str.c_str());
	}

	return false;
}