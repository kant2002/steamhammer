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
	const std::string matchupStr(ourRaceStr.substr(0, 1) + 'v' + theirRaceStr.substr(0, 1));

	const char * ourRace = ourRaceStr.c_str();
	const char * matchup = matchupStr.c_str();

	// Number of starting locations on the map.
	const int mapSize = BWTA::getStartLocations().size();
	UAB_ASSERT(mapSize >= 2 && mapSize <= 8, "bad map size");
	const std::string mapWeightString = std::string("Weight") + std::string("012345678").at(mapSize);

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

		Config::Micro::KiteWithRangedUnits = GetBoolByRace("KiteWithRangedUnits", micro);
		Config::Micro::WorkersDefendRush = GetBoolByRace("WorkersDefendRush", micro);
		
		Config::Micro::RetreatMeleeUnitShields = GetIntByRace("RetreatMeleeUnitShields", micro);
		Config::Micro::RetreatMeleeUnitHP = GetIntByRace("RetreatMeleeUnitHP", micro);
		Config::Micro::CombatRegroupRadius = GetIntByRace("RegroupRadius", micro);
		Config::Micro::UnitNearEnemyRadius = GetIntByRace("UnitNearEnemyRadius", micro);
		Config::Micro::ScoutDefenseRadius = GetIntByRace("ScoutDefenseRadius", micro);

        if (micro.HasMember("KiteLongerRangedUnits") && micro["KiteLongerRangedUnits"].IsArray())
        {
            const rapidjson::Value & kite = micro["KiteLongerRangedUnits"];

            for (size_t i(0); i < kite.Size(); ++i)
            {
                if (kite[i].IsString())
                {
					// TODO what a crude way of doing this...
                    MacroAct type(kite[i].GetString());
					if (type.isUnit())
					{
						Config::Micro::KiteLongerRangedUnits.insert(type.getUnitType());
					}
					else
					{
						BWAPI::Broodwar->printf("can't KiteLongerRangedUnits '%s'", kite[i].GetString());
					}
                }
            }
        }
    }

    // Parse the Macro Options
    if (doc.HasMember("Macro") && doc["Macro"].IsObject())
    {
        const rapidjson::Value & macro = doc["Macro"];
        JSONTools::ReadInt("BOSSFrameLimit", macro, Config::Macro::BOSSFrameLimit);
        JSONTools::ReadInt("PylonSpacing", macro, Config::Macro::PylonSpacing);

		Config::Macro::ProductionJamFrameLimit = GetIntByRace("ProductionJamFrameLimit", macro);
		Config::Macro::BuildingSpacing = GetIntByRace("BuildingSpacing", macro);
		Config::Macro::WorkersPerRefinery = GetIntByRace("WorkersPerRefinery", macro);
		Config::Macro::WorkersPerPatch = GetDoubleByRace("WorkersPerPatch", macro);
	}

    // Parse the Debug Options
    if (doc.HasMember("Debug") && doc["Debug"].IsObject())
    {
        const rapidjson::Value & debug = doc["Debug"];
        JSONTools::ReadString("ErrorLogFilename", debug, Config::Debug::ErrorLogFilename);
        JSONTools::ReadBool("LogAssertToErrorFile", debug, Config::Debug::LogAssertToErrorFile);
        JSONTools::ReadBool("DrawGameInfo", debug, Config::Debug::DrawGameInfo);
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
		JSONTools::ReadBool("DrawMapDistances", debug, Config::Debug::DrawMapDistances);
		JSONTools::ReadBool("DrawBaseInfo", debug, Config::Debug::DrawBaseInfo);
		JSONTools::ReadBool("DrawStrategyBossInfo", debug, Config::Debug::DrawStrategyBossInfo);
		JSONTools::ReadBool("DrawUnitTargetInfo", debug, Config::Debug::DrawUnitTargetInfo);
		JSONTools::ReadBool("DrawUnitOrders", debug, Config::Debug::DrawUnitOrders);
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

		const rapidjson::Value * strategyCombos = nullptr;
		if (strategy.HasMember("StrategyCombos") && strategy["StrategyCombos"].IsObject())
		{
			strategyCombos = &strategy["StrategyCombos"];
		}

		Config::Strategy::ScoutHarassEnemy = GetBoolByRace("ScoutHarassEnemy", strategy);
		Config::Strategy::ScoutHarassEnemy = GetBoolByRace("SurrenderWhenHopeIsLost", strategy);

        JSONTools::ReadString("ReadDirectory", strategy, Config::Strategy::ReadDir);
        JSONTools::ReadString("WriteDirectory", strategy, Config::Strategy::WriteDir);

		// check if we are using an enemy specific strategy
		JSONTools::ReadBool("UseEnemySpecificStrategy", strategy, Config::Strategy::UseEnemySpecificStrategy);
		if (Config::Strategy::UseEnemySpecificStrategy && strategy.HasMember("EnemySpecificStrategy") && strategy["EnemySpecificStrategy"].IsObject())
		{
			const rapidjson::Value & specific = strategy["EnemySpecificStrategy"];
			const std::string enemyName = BWAPI::Broodwar->enemy()->getName();

			// check to see if our current enemy name is listed in the specific strategies
			if (specific.HasMember(enemyName.c_str()))
			{
				std::string strategyName;

				// if that enemy has a strategy listed for our current race, use it                                              
				if (_ParseStrategy(specific[enemyName.c_str()], strategyName, mapWeightString, ourRaceStr, strategyCombos))
				{
					Config::Strategy::StrategyName = strategyName;
					Config::Strategy::FoundEnemySpecificStrategy = true;   // defaults to false; see Config.cpp
				}
			}
		}

		// If we don't have a strategy for this enemy, fall back on a more general strategy.
		if (!Config::Strategy::FoundEnemySpecificStrategy)
		{
			std::string strategyName;

			// If we have set a strategy for the current matchup, set it.
			if (strategy.HasMember(matchup) && _ParseStrategy(strategy[matchup], strategyName, mapWeightString, "StrategyMix", strategyCombos))
			{
				Config::Strategy::StrategyName = strategyName;
			}
			// Failing that, look for a strategy for the current race.
			else if (strategy.HasMember(ourRace) && _ParseStrategy(strategy[ourRace], strategyName, mapWeightString, "StrategyMix", strategyCombos))
			{
				Config::Strategy::StrategyName = strategyName;
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

				std::string openingGroup("");
				if (val.HasMember("OpeningGroup") && val["OpeningGroup"].IsString())
				{
					openingGroup = val["OpeningGroup"].GetString();
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
							std::regex countRegex("([0-9]+)\\s+x\\s+([a-zA-Z_ ]+)");
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

				StrategyManager::Instance().addStrategy(name, Strategy(name, strategyRace, openingGroup, buildOrder));
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
		else if (variableName == "drawmapdistances") { Config::Debug::DrawMapDistances = GetBoolFromString(val); }
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
// If a strategy mix declaration, pick the strategy randomly by weight:
//   { "StrategyMix" : [
//     { "Weight" : 30, "Strategy" : "Zerg_4PoolFast" },
//     { "Weight" : 20, "Strategy" : "Zerg_4PoolSlow" },
//     { "Weight" : 50, "Strategy" : "Zerg_5Pool" }
//   ]},
// Return whether parsing was successful.
// Internal routine not for wider use.
bool ParseUtils::_ParseStrategy(
	const rapidjson::Value & item,
	std::string & stratName,        // updated and returned
	const std::string & mapWeightString,
	const std::string & objectString,
	const rapidjson::Value * strategyCombos)
{
	if (item.IsString())
	{
		stratName = std::string(item.GetString());
		return _LookUpStrategyCombo(item, stratName, mapWeightString, objectString, strategyCombos);
	}

	if (item.IsObject() && item.HasMember(objectString.c_str()))
	{
		if (item[objectString.c_str()].IsString())
		{
			stratName = std::string(item[objectString.c_str()].GetString());
			return _LookUpStrategyCombo(item[objectString.c_str()], stratName, mapWeightString, objectString, strategyCombos);
		}
		else if (item[objectString.c_str()].IsArray())
		{
			const rapidjson::Value & mix = item[objectString.c_str()];

			std::vector<std::string> strategies;    // strategy name
			std::vector<int> weights;               // cumulative weight of strategy
			int totalWeight = 0;                    // cumulative weight of last strategy (so far)

			// 1. Collect the weights and strategies.
			for (size_t i(0); i < mix.Size(); ++i)
			{
				if (mix[i].IsObject() &&
					mix[i].HasMember("Strategy") && mix[i]["Strategy"].IsString())
				{
					int weight;
					if (mix[i].HasMember(mapWeightString.c_str()) && mix[i][mapWeightString.c_str()].IsInt())
					{
						weight = mix[i][mapWeightString.c_str()].GetInt();
					}
					else if (mix[i].HasMember("Weight") && mix[i]["Weight"].IsInt())
					{
						weight = mix[i]["Weight"].GetInt();
					}
					else
					{
						continue;
					}
					strategies.push_back(mix[i]["Strategy"].GetString());
					totalWeight += weight;
					weights.push_back(totalWeight);
				}
				else
				{
					return false;
				}
			}

			// 2. Choose a strategy at random by weight.
			std::uniform_int_distribution<int> uniform_dist(0, totalWeight - 1);
			std::random_device rng_seed;
			std::mt19937 rng(rng_seed());
			int w = uniform_dist(rng);        // a lot of trouble to get one random number...
			for (size_t i = 0; i < weights.size(); ++i)
			{
				if (w < weights[i])
				{
					stratName = strategies[i];
					return _LookUpStrategyCombo(item, stratName, mapWeightString, objectString, strategyCombos);
				}
			}
			UAB_ASSERT(false, "random strategy fell through");
		}
	}

	return false;
}

// Mutually recursive with _ParseStrategy above.
// This allows a strategy name in a strategy combo to refer to another strategy combo.
// DON'T CREATE A LOOP IN THE STRATEGY COMBOS or the program will fall into an infinite loop.
bool ParseUtils::_LookUpStrategyCombo(
	const rapidjson::Value & item,
	std::string & stratName,        // updated and returned
	const std::string & mapWeightString,
	const std::string & objectString,
	const rapidjson::Value * strategyCombos)
{
	if (strategyCombos && strategyCombos->HasMember(stratName.c_str()))
	{
		if ((*strategyCombos)[stratName.c_str()].IsString())
		{
			stratName = (*strategyCombos)[stratName.c_str()].GetString();
			return true;
		}
		if ((*strategyCombos)[stratName.c_str()].IsObject())
		{
			return _ParseStrategy((*strategyCombos)[stratName.c_str()], stratName, mapWeightString, objectString, strategyCombos);
		}
		// If it's neither of those things, complain.
		BWAPI::Broodwar->printf("StrategyCombo %s is not valid", stratName.c_str());
		return false;
	}

	// No need for further lookup. What we have is what we want.
	return true;
}

bool ParseUtils::GetBoolFromString(const std::string & str)
{
	std::string boolStr(str);
	std::transform(boolStr.begin(), boolStr.end(), boolStr.begin(), ::tolower);

	if (boolStr == "true")
	{
		return true;
	}
	else if (boolStr == "false")
	{
		return false;
	}
	else
	{
		UAB_ASSERT_WARNING(false, "Unknown bool from string: %s", str.c_str());
	}

	return false;
}

// Return an integer which may be different depending on our race. The forms are:
//     "Item" : 10
// and
//     "Item" : { "Zerg" : 1, "Protoss" : 2, "Terran" : 3 }
// Anything that is missing defaults to 0. So
//     "Item" : { "Zerg" : 1 }
// is the same as
//     "Item" : { "Zerg" : 1, "Protoss" : 0, "Terran" : 0 }
// "Item" itself must be given in one form or another, though, so we can catch typos.
int ParseUtils::GetIntByRace(const char * name, const rapidjson::Value & item)
{
	if (item.HasMember(name))
	{
		// "Item" : 10
		if (item[name].IsInt())
		{
			return item[name].GetInt();
		}

		// "Item" : { "Zerg" : 1, "Protoss" : 2, "Terran" : 3 }
		if (item[name].IsObject())
		{
			const std::string raceStr(BWAPI::Broodwar->self()->getRace().getName());
			if (item[name].HasMember(raceStr.c_str()) && item[name][raceStr.c_str()].IsInt())
			{
				return item[name][raceStr.c_str()].GetInt();
			}
		}
	}
	else
	{
		UAB_ASSERT_WARNING(false, "Wrong/missing config entry '%s'", name);
	}

	return 0;
}

// Similar to GetIntByRace(). Defaults to 0.0 (you probably don't want that).
double ParseUtils::GetDoubleByRace(const char * name, const rapidjson::Value & item)
{
	if (item.HasMember(name))
	{
		// "Item" : 10.0
		if (item[name].IsDouble())
		{
			return item[name].GetDouble();
		}

		// "Item" : { "Zerg" : 1.5, "Protoss" : 3.0, "Terran" : 4.5 }
		if (item[name].IsObject())
		{
			const std::string raceStr(BWAPI::Broodwar->self()->getRace().getName());
			if (item[name].HasMember(raceStr.c_str()) && item[name][raceStr.c_str()].IsDouble())
			{
				return item[name][raceStr.c_str()].GetDouble();
			}
		}
	}
	else
	{
		UAB_ASSERT_WARNING(false, "Wrong/missing config entry '%s'", name);
	}

	return 0.0;
}

// Similar to GetIntByRace(). Defaults to false.
bool ParseUtils::GetBoolByRace(const char * name, const rapidjson::Value & item)
{
	if (item.HasMember(name))
	{
		// "Item" : true
		if (item[name].IsBool())
		{
			return item[name].GetBool();
		}

		// "Item" : { "Zerg" : true, "Protoss" : false, "Terran" : false }
		if (item[name].IsObject())
		{
			const std::string raceStr(BWAPI::Broodwar->self()->getRace().getName());
			if (item[name].HasMember(raceStr.c_str()) && item[name][raceStr.c_str()].IsBool())
			{
				return item[name][raceStr.c_str()].GetBool();
			}
		}
	}
	else
	{
		UAB_ASSERT_WARNING(false, "Wrong/missing config entry '%s'", name);
	}

	return false;
}
