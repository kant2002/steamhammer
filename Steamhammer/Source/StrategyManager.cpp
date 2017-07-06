#include "StrategyManager.h"
#include "CombatCommander.h"
#include "ProductionManager.h"
#include "StrategyBossZerg.h"
#include "UnitUtil.h"

using namespace UAlbertaBot;

StrategyManager::StrategyManager() 
	: _selfRace(BWAPI::Broodwar->self()->getRace())
	, _enemyRace(BWAPI::Broodwar->enemy()->getRace())
    , _emptyBuildOrder(BWAPI::Broodwar->self()->getRace())
	, _openingGroup("")
{
}

StrategyManager & StrategyManager::Instance() 
{
	static StrategyManager instance;
	return instance;
}

const int StrategyManager::getScore(BWAPI::Player player) const
{
	return player->getBuildingScore() + player->getKillScore() + player->getRazingScore() + player->getUnitScore();
}

const BuildOrder & StrategyManager::getOpeningBookBuildOrder() const
{
    auto buildOrderIt = _strategies.find(Config::Strategy::StrategyName);

    // look for the build order in the build order map
	if (buildOrderIt != std::end(_strategies))
    {
        return (*buildOrderIt).second._buildOrder;
    }
    else
    {
        UAB_ASSERT_WARNING(false, "Strategy not found: %s, returning empty initial build order", Config::Strategy::StrategyName.c_str());
        return _emptyBuildOrder;
    }
}

const bool StrategyManager::shouldExpandNow() const
{
	// if there is no place to expand to, we can't expand
	if (MapTools::Instance().getNextExpansion() == BWAPI::TilePositions::None)
	{
		return false;
	}

	// if we have idle workers then we need a new expansion
	if (WorkerManager::Instance().getNumIdleWorkers() > 3)
	{
		return true;
	}

    // if we have excess minerals, expand
	if (BWAPI::Broodwar->self()->minerals() > 600)
    {
        return true;
    }

	size_t numDepots = UnitUtil::GetAllUnitCount(BWAPI::UnitTypes::Terran_Command_Center)
		+ UnitUtil::GetAllUnitCount(BWAPI::UnitTypes::Protoss_Nexus)
		+ UnitUtil::GetAllUnitCount(BWAPI::UnitTypes::Zerg_Hatchery)
		+ UnitUtil::GetAllUnitCount(BWAPI::UnitTypes::Zerg_Lair)
		+ UnitUtil::GetAllUnitCount(BWAPI::UnitTypes::Zerg_Hive);
	int frame = BWAPI::Broodwar->getFrameCount();
	int minute = frame / (24 * 60);

	// we will make expansion N after array[N] minutes have passed
    std::vector<int> expansionTimes = {5, 9, 13, 17, 21, 25};

    for (size_t i(0); i < expansionTimes.size(); ++i)
    {
        if (numDepots < (i+2) && minute > expansionTimes[i])
        {
            return true;
        }
    }

	return false;
}

void StrategyManager::addStrategy(const std::string & name, Strategy & strategy)
{
    _strategies[name] = strategy;
}

// Set _openingGroup depending on the current strategy, which in principle
// might be from the config file or from opening learning.
// This is part of initialization; it happens early on.
void StrategyManager::setOpeningGroup()
{
	auto buildOrderItr = _strategies.find(Config::Strategy::StrategyName);

	if (buildOrderItr != std::end(_strategies))
	{
		_openingGroup = (*buildOrderItr).second._openingGroup;
	}
}

const std::string & StrategyManager::getOpeningGroup() const
{
	return _openingGroup;
}

const MetaPairVector StrategyManager::getBuildOrderGoal()
{
    if (_selfRace == BWAPI::Races::Protoss)
    {
        return getProtossBuildOrderGoal();
    }
	else if (_selfRace == BWAPI::Races::Terran)
	{
		return getTerranBuildOrderGoal();
	}
	else if (_selfRace == BWAPI::Races::Zerg)
	{
		return getZergBuildOrderGoal();
	}

    return MetaPairVector();
}

const MetaPairVector StrategyManager::getProtossBuildOrderGoal()
{
	// the goal to return
	MetaPairVector goal;

	// These counts include uncompleted units.
	int numPylons = UnitUtil::GetAllUnitCount(BWAPI::UnitTypes::Protoss_Pylon);
	int numNexusCompleted = BWAPI::Broodwar->self()->completedUnitCount(BWAPI::UnitTypes::Protoss_Nexus);
	int numNexusAll = UnitUtil::GetAllUnitCount(BWAPI::UnitTypes::Protoss_Nexus);
	int numProbes = UnitUtil::GetAllUnitCount(BWAPI::UnitTypes::Protoss_Probe);
	int numCannon = UnitUtil::GetAllUnitCount(BWAPI::UnitTypes::Protoss_Photon_Cannon);
	int numZealots = UnitUtil::GetAllUnitCount(BWAPI::UnitTypes::Protoss_Zealot);
	int numDragoons = UnitUtil::GetAllUnitCount(BWAPI::UnitTypes::Protoss_Dragoon);
	int numDarkTemplar = UnitUtil::GetAllUnitCount(BWAPI::UnitTypes::Protoss_Dark_Templar);
	int numReavers = UnitUtil::GetAllUnitCount(BWAPI::UnitTypes::Protoss_Reaver);
	int numCorsairs = UnitUtil::GetAllUnitCount(BWAPI::UnitTypes::Protoss_Corsair);
	int numCarriers = UnitUtil::GetAllUnitCount(BWAPI::UnitTypes::Protoss_Carrier);

	bool hasStargate = UnitUtil::GetCompletedUnitCount(BWAPI::UnitTypes::Protoss_Stargate) > 0;

	int maxProbes = WorkerManager::Instance().getMaxWorkers();

	BWAPI::Player self = BWAPI::Broodwar->self();

	if (_openingGroup == "zealots")
    {
        goal.push_back(MetaPair(BWAPI::UnitTypes::Protoss_Zealot, numZealots + 6));

		if (numNexusAll >= 3)
		{
			// In the end, switch to carriers; no more dragoons.
			goal.push_back(MetaPair(BWAPI::UpgradeTypes::Carrier_Capacity, 1));
			goal.push_back(MetaPair(BWAPI::UnitTypes::Protoss_Carrier, numCarriers + 1));
		}
		else if (numNexusAll >= 2)
		{
			// Once we have a 2nd nexus, add dragoons.
			goal.push_back(MetaPair(BWAPI::UpgradeTypes::Singularity_Charge, 1));
			goal.push_back(MetaPair(BWAPI::UnitTypes::Protoss_Dragoon, numDragoons + 4));
		}

		// Once dragoons are out, get zealot speed.
		if (numDragoons > 0)
		{
			goal.push_back(MetaPair(BWAPI::UpgradeTypes::Leg_Enhancements, 1));
		}

		// Finally add templar archives.
		if (UnitUtil::GetAllUnitCount(BWAPI::UnitTypes::Protoss_Citadel_of_Adun) > 0)
		{
			goal.push_back(MetaPair(BWAPI::UnitTypes::Protoss_Templar_Archives, 1));
		}

		// If we have templar archives, make
		// 1. a small fixed number of dark templar to force a reaction, and
		// 2. an even number of high templar to merge into archons (so the high templar disappear quickly).
		if (UnitUtil::GetCompletedUnitCount(BWAPI::UnitTypes::Protoss_Templar_Archives) != 0)
		{
			goal.push_back(MetaPair(BWAPI::UnitTypes::Protoss_Dark_Templar, std::max(3, numDarkTemplar)));
			goal.push_back(MetaPair(BWAPI::UnitTypes::Protoss_High_Templar, 2));
		}
	}
	else if (_openingGroup == "dragoons")
    {
		goal.push_back(MetaPair(BWAPI::UpgradeTypes::Singularity_Charge, 1));
		goal.push_back(MetaPair(BWAPI::UnitTypes::Protoss_Dragoon, numDragoons + 6));

		// Once we have a 2nd nexus, add reavers.
		if (numNexusAll >= 2)
		{
			goal.push_back(MetaPair(BWAPI::UnitTypes::Protoss_Reaver, numReavers + 1));
		}

		// If we have templar archives, make a small fixed number of DTs to force a reaction.
		if (UnitUtil::GetCompletedUnitCount(BWAPI::UnitTypes::Protoss_Templar_Archives) != 0)
		{
			goal.push_back(MetaPair(BWAPI::UnitTypes::Protoss_Dark_Templar, std::max(3, numDarkTemplar)));
		}

	}
	else if (_openingGroup == "dark templar")
    {
        goal.push_back(MetaPair(BWAPI::UnitTypes::Protoss_Dark_Templar, numDarkTemplar + 2));

		// Once we have a 2nd nexus, add dragoons.
		if (numNexusAll >= 2)
        {
			goal.push_back(MetaPair(BWAPI::UpgradeTypes::Singularity_Charge, 1));
			goal.push_back(MetaPair(BWAPI::UnitTypes::Protoss_Dragoon, numDragoons + 4));
        }
    }
	else if (_openingGroup == "drop")
	{
		goal.push_back(MetaPair(BWAPI::UnitTypes::Protoss_Dark_Templar, numDarkTemplar + 2));

		// The drop prep is carried out entirely by the opening book.
		// Immediately transition into something else.
		_openingGroup = "dragoons";
	}
	else
    {
		UAB_ASSERT_WARNING(false, "Unknown Opening Group: %s", _openingGroup.c_str());
    }
    
	// If we're doing a corsair thing and it's still working, slowly add more.
	if (_enemyRace == BWAPI::Races::Zerg &&
		hasStargate &&
		numCorsairs < 6 &&
		self->deadUnitCount(BWAPI::UnitTypes::Protoss_Corsair) == 0)
	{
		goal.push_back(MetaPair(BWAPI::UnitTypes::Protoss_Corsair, numCorsairs + 1));
	}

	// Get observers if we have a second base, or if the enemy has cloaked units.
	if (numNexusCompleted >= 2 || InformationManager::Instance().enemyHasCloakTech())
	{
		goal.push_back(MetaPair(BWAPI::UnitTypes::Protoss_Robotics_Facility, 1));
		
		if (self->completedUnitCount(BWAPI::UnitTypes::Protoss_Robotics_Facility) > 0)
		{
			goal.push_back(MetaPair(BWAPI::UnitTypes::Protoss_Observer, 2));
		}
	}

	// Make more probes, up to a limit.
	goal.push_back(MetaPair(BWAPI::UnitTypes::Protoss_Probe, std::min(maxProbes, numProbes + 8)));

    // if we want to expand, insert a nexus into the build order
	if (shouldExpandNow())
	{
		goal.push_back(MetaPair(BWAPI::UnitTypes::Protoss_Nexus, numNexusAll + 1));
	}

	return goal;
}

const MetaPairVector StrategyManager::getTerranBuildOrderGoal()
{
	// the goal to return
	std::vector<MetaPair> goal;

	// These counts include uncompleted units.
	int numSCVs			= UnitUtil::GetAllUnitCount(BWAPI::UnitTypes::Terran_SCV);
    int numCC           = UnitUtil::GetAllUnitCount(BWAPI::UnitTypes::Terran_Command_Center);            
    int numRefineries   = UnitUtil::GetAllUnitCount(BWAPI::UnitTypes::Terran_Refinery);            
    int numMarines      = UnitUtil::GetAllUnitCount(BWAPI::UnitTypes::Terran_Marine);
	int numMedics       = UnitUtil::GetAllUnitCount(BWAPI::UnitTypes::Terran_Medic);
	int numWraith       = UnitUtil::GetAllUnitCount(BWAPI::UnitTypes::Terran_Wraith);
    int numVultures     = UnitUtil::GetAllUnitCount(BWAPI::UnitTypes::Terran_Vulture);
    int numGoliaths     = UnitUtil::GetAllUnitCount(BWAPI::UnitTypes::Terran_Goliath);
    int numTanks        = UnitUtil::GetAllUnitCount(BWAPI::UnitTypes::Terran_Siege_Tank_Tank_Mode)
                        + UnitUtil::GetAllUnitCount(BWAPI::UnitTypes::Terran_Siege_Tank_Siege_Mode);

	bool hasEBay		= UnitUtil::GetCompletedUnitCount(BWAPI::UnitTypes::Terran_Engineering_Bay) > 0;
	bool hasAcademy		= UnitUtil::GetCompletedUnitCount(BWAPI::UnitTypes::Terran_Academy) > 0;
	bool hasArmory		= UnitUtil::GetCompletedUnitCount(BWAPI::UnitTypes::Terran_Armory) > 0;

	int maxSCVs = WorkerManager::Instance().getMaxWorkers();

	BWAPI::Player self = BWAPI::Broodwar->self();

	if (_openingGroup == "anti-rush")
	{
		int numRax = UnitUtil::GetAllUnitCount(BWAPI::UnitTypes::Terran_Barracks);

		CombatCommander::Instance().setAggression(false);
		
		goal.push_back(std::pair<MacroAct, int>(BWAPI::UnitTypes::Terran_Marine, numMarines + numRax));
		goal.push_back(std::pair<MacroAct, int>(BWAPI::UnitTypes::Terran_SCV, std::min(maxSCVs, numSCVs + 1)));
		goal.push_back(std::pair<MacroAct, int>(BWAPI::UnitTypes::Terran_Bunker, 1));
		
		if (self->minerals() > 250)
		{
			goal.push_back(std::pair<MacroAct, int>(BWAPI::UnitTypes::Terran_Barracks, numRax + 1));
		}

		// If we survived long enough, transition to something more interesting.
		if (numMarines >= 10)
		{
			_openingGroup = "bio";
			CombatCommander::Instance().setAggression(true);
		}
	}
	else if (_openingGroup == "bio")
    {
	    goal.push_back(std::pair<MacroAct, int>(BWAPI::UnitTypes::Terran_Marine, numMarines + 8));

		if (numMarines >= 10)
		{
			goal.push_back(std::pair<MacroAct, int>(BWAPI::UnitTypes::Terran_Academy, 1));
			if (numRefineries == 0)
			{
				goal.push_back(std::pair<MacroAct, int>(BWAPI::UnitTypes::Terran_Refinery, 1));
			}
		}
		if (hasAcademy)
		{
			// 1 medic for each 6 marines.
			int medicGoal = std::max(numMedics, numMarines / 6);
			goal.push_back(std::pair<MacroAct, int>(BWAPI::UnitTypes::Terran_Medic, medicGoal));
			if (!self->hasResearched(BWAPI::TechTypes::Stim_Packs))
			{
				goal.push_back(std::pair<MacroAct, int>(BWAPI::TechTypes::Stim_Packs, 1));
			}
			else
			{
				goal.push_back(std::pair<MacroAct, int>(BWAPI::UpgradeTypes::U_238_Shells, 1));
			}
		}
        if (numMarines > 16)
        {
            goal.push_back(std::pair<MacroAct, int>(BWAPI::UnitTypes::Terran_Engineering_Bay, 1));
        }
		if (hasEBay)
		{
			if (self->getUpgradeLevel(BWAPI::UpgradeTypes::Terran_Infantry_Weapons) == 0 &&
				!self->isUpgrading(BWAPI::UpgradeTypes::Terran_Infantry_Weapons))
			{
				goal.push_back(std::pair<MacroAct, int>(BWAPI::UpgradeTypes::Terran_Infantry_Weapons, 1));
			}
			else if (self->getUpgradeLevel(BWAPI::UpgradeTypes::Terran_Infantry_Weapons) > 0 &&
				self->getUpgradeLevel(BWAPI::UpgradeTypes::Terran_Infantry_Armor) == 0 &&
				!self->isUpgrading(BWAPI::UpgradeTypes::Terran_Infantry_Armor))
			{
				goal.push_back(std::pair<MacroAct, int>(BWAPI::UpgradeTypes::Terran_Infantry_Armor, 1));
			}
		}
    }
	else if (_openingGroup == "vultures")
    {
        goal.push_back(std::pair<MacroAct, int>(BWAPI::UnitTypes::Terran_Vulture, numVultures + 3));
		goal.push_back(std::pair<MacroAct, int>(BWAPI::UpgradeTypes::Ion_Thrusters, 1));

        if (numVultures >= 6)
        {
			// The rush is over, transition out on the next call.
			_openingGroup = "tanks";
        }
    }
	else if (_openingGroup == "tanks")
    {
		goal.push_back(std::pair<MacroAct, int>(BWAPI::UnitTypes::Terran_Siege_Tank_Tank_Mode, numVultures + 1));
		goal.push_back(std::pair<MacroAct, int>(BWAPI::UnitTypes::Terran_Siege_Tank_Tank_Mode, numTanks + 3));
		goal.push_back(std::pair<MacroAct, int>(BWAPI::TechTypes::Tank_Siege_Mode, 1));

		if (numTanks >= 8)
		{
			goal.push_back(std::pair<MacroAct, int>(BWAPI::UnitTypes::Terran_Goliath, numGoliaths + 4));
			goal.push_back(std::pair<MacroAct, int>(BWAPI::UpgradeTypes::Ion_Thrusters, 1));
		}
		if (numGoliaths >= 4)
		{
			goal.push_back(std::pair<MacroAct, int>(BWAPI::UpgradeTypes::Charon_Boosters, 1));
		}
		if (self->hasResearched(BWAPI::TechTypes::Tank_Siege_Mode))
		{
			// Maintain 1 vessel to spot for the tanks.
			goal.push_back(std::pair<MacroAct, int>(BWAPI::UnitTypes::Terran_Science_Vessel, 1));
		}
    }
	else if (_openingGroup == "drop")
	{
		goal.push_back(std::pair<MacroAct, int>(BWAPI::UpgradeTypes::Ion_Thrusters, 1));
		goal.push_back(MetaPair(BWAPI::UnitTypes::Terran_Vulture, numVultures + 1));

		// The drop prep is carried out entirely by the opening book.
		// Immediately transition into something else.
		if (_enemyRace == BWAPI::Races::Zerg)
		{
			_openingGroup = "bio";
		}
		else
		{
			_openingGroup = "tanks";
		}
	}
	else
    {
		BWAPI::Broodwar->printf("Unknown Opening Group: %s", _openingGroup.c_str());
    }

	if (numCC > 1 || InformationManager::Instance().enemyHasCloakTech())
	{
		goal.push_back(std::pair<MacroAct, int>(BWAPI::UnitTypes::Terran_Academy, 1));
		if (numRefineries == 0)
		{
			goal.push_back(std::pair<MacroAct, int>(BWAPI::UnitTypes::Terran_Refinery, 1));
		}
	}

	if (numCC > 0 && hasAcademy)
	{
		goal.push_back(std::pair<MacroAct, int>(BWAPI::UnitTypes::Terran_Comsat_Station, UnitUtil::GetCompletedUnitCount(BWAPI::UnitTypes::Terran_Command_Center)));
	}

	if (hasArmory &&
		self->getUpgradeLevel(BWAPI::UpgradeTypes::Terran_Vehicle_Weapons) == 0 &&
		!self->isUpgrading(BWAPI::UpgradeTypes::Terran_Vehicle_Weapons))
	{
		goal.push_back(std::pair<MacroAct, int>(BWAPI::UpgradeTypes::Terran_Vehicle_Weapons, 1));
	}

	// Make more SCVs, up to a limit.
	if (_openingGroup != "anti-rush")
	{
		goal.push_back(MetaPair(BWAPI::UnitTypes::Terran_SCV, std::min(maxSCVs, numSCVs + 6)));
	}

	if (shouldExpandNow())
    {
        goal.push_back(std::pair<MacroAct, int>(BWAPI::UnitTypes::Terran_Command_Center, numCC + 1));
    }

	return goal;
}

// BOSS method of choosing a zerg production plan. UNUSED!
// See freshProductionPlan() for the current method.
const MetaPairVector StrategyManager::getZergBuildOrderGoal() const
{
	// the goal to return
	std::vector<MetaPair> goal;
	
	// These counts include uncompleted units.
	int nLairs			= UnitUtil::GetAllUnitCount(BWAPI::UnitTypes::Zerg_Lair);
	int nHives			= UnitUtil::GetAllUnitCount(BWAPI::UnitTypes::Zerg_Hive);
    int nHatches        = UnitUtil::GetAllUnitCount(BWAPI::UnitTypes::Zerg_Hatchery)
                        + nLairs + nHives;
    int nDrones			= UnitUtil::GetAllUnitCount(BWAPI::UnitTypes::Zerg_Drone);
	int nHydras			= UnitUtil::GetAllUnitCount(BWAPI::UnitTypes::Zerg_Hydralisk);

	const int droneMax = 48;             // number of drones not to exceed

	// Simple default strategy in case you want to use this method.
	goal.push_back(std::pair<MacroAct, int>(BWAPI::UnitTypes::Zerg_Hydralisk, nHydras + 12));
	goal.push_back(std::pair<MacroAct, int>(BWAPI::UpgradeTypes::Muscular_Augments, 1));
	goal.push_back(std::pair<MacroAct, int>(BWAPI::UpgradeTypes::Grooved_Spines, 1));
	goal.push_back(std::pair<MacroAct, int>(BWAPI::UnitTypes::Zerg_Drone, std::min(droneMax, nDrones + 2)));
	if (shouldExpandNow())
	{
		goal.push_back(std::pair<MacroAct, int>(BWAPI::UnitTypes::Zerg_Hatchery, nHatches + 1));
		goal.push_back(std::pair<MacroAct, int>(BWAPI::UnitTypes::Zerg_Drone, std::min(droneMax, nDrones + 10)));
	}

	return goal;
}

void StrategyManager::readResults()
{
    if (!Config::Modules::UsingStrategyIO)
    {
        return;
    }

    std::string enemyName = BWAPI::Broodwar->enemy()->getName();
    std::replace(enemyName.begin(), enemyName.end(), ' ', '_');

    std::string enemyResultsFile = Config::Strategy::ReadDir + enemyName + ".txt";
    
    std::string strategyName;
    int wins = 0;
    int losses = 0;

    FILE *file = fopen ( enemyResultsFile.c_str(), "r" );
    if ( file != nullptr )
    {
        char line [ 4096 ]; /* or other suitable maximum line size */
        while ( fgets ( line, sizeof line, file ) != nullptr ) /* read a line */
        {
            std::stringstream ss(line);

            ss >> strategyName;
            ss >> wins;
            ss >> losses;

            //BWAPI::Broodwar->printf("Results Found: %s %d %d", strategyName.c_str(), wins, losses);

            if (_strategies.find(strategyName) == _strategies.end())
            {
                //BWAPI::Broodwar->printf("Warning: Results file has unknown Strategy: %s", strategyName.c_str());
            }
            else
            {
                _strategies[strategyName]._wins = wins;
                _strategies[strategyName]._losses = losses;
            }
        }

        fclose ( file );
    }
    else
    {
        //BWAPI::Broodwar->printf("No results file found: %s", enemyResultsFile.c_str());
    }
}

void StrategyManager::writeResults()
{
    if (!Config::Modules::UsingStrategyIO)
    {
        return;
    }

    std::string enemyName = BWAPI::Broodwar->enemy()->getName();
    std::replace(enemyName.begin(), enemyName.end(), ' ', '_');

    std::string enemyResultsFile = Config::Strategy::WriteDir + enemyName + ".txt";

    std::stringstream ss;

    for (auto & kv : _strategies)
    {
        const Strategy & strategy = kv.second;

        ss << strategy._name << " " << strategy._wins << " " << strategy._losses << "\n";
    }

    Logger::LogOverwriteToFile(enemyResultsFile, ss.str());
}

void StrategyManager::onEnd(const bool isWinner)
{
    if (!Config::Modules::UsingStrategyIO)
    {
        return;
    }

    if (isWinner)
    {
        _strategies[Config::Strategy::StrategyName]._wins++;
    }
    else
    {
        _strategies[Config::Strategy::StrategyName]._losses++;
    }

    writeResults();
}

void StrategyManager::setLearnedStrategy()
{
    // we are currently not using this functionality for the competition so turn it off 
    return;

    if (!Config::Modules::UsingStrategyIO)
    {
        return;
    }

    const std::string & strategyName = Config::Strategy::StrategyName;
    Strategy & currentStrategy = _strategies[strategyName];

    int totalGamesPlayed = 0;
    int strategyGamesPlayed = currentStrategy._wins + currentStrategy._losses;
    double winRate = strategyGamesPlayed > 0 ? currentStrategy._wins / static_cast<double>(strategyGamesPlayed) : 0;

    // if we are using an enemy specific strategy
    if (Config::Strategy::FoundEnemySpecificStrategy)
    {        
        return;
    }

    // if our win rate with the current strategy is super high don't explore at all
    // also we're pretty confident in our base strategies so don't change if insufficient games have been played
    if (strategyGamesPlayed < 5 || (strategyGamesPlayed > 0 && winRate > 0.49))
    {
        BWAPI::Broodwar->printf("Still using default strategy");
        return;
    }

    // get the total number of games played so far with this race
    for (auto & kv : _strategies)
    {
        Strategy & strategy = kv.second;
		if (strategy._race == _selfRace)
        {
            totalGamesPlayed += strategy._wins + strategy._losses;
        }
    }

    // calculate the UCB value and store the highest
    double C = 0.5;
    std::string bestUCBStrategy;
    double bestUCBStrategyVal = std::numeric_limits<double>::lowest();
    for (auto & kv : _strategies)
    {
        Strategy & strategy = kv.second;
		if (strategy._race != _selfRace)
        {
            continue;
        }

        int sGamesPlayed = strategy._wins + strategy._losses;
		// TODO looks like an error: dividing by a number that is not the one tested
        double sWinRate = sGamesPlayed > 0 ? currentStrategy._wins / static_cast<double>(strategyGamesPlayed) : 0;
		// TODO looks like an error: and then dividing by a number that is not tested
		double ucbVal = C * sqrt(log((double)totalGamesPlayed / sGamesPlayed));
        double val = sWinRate + ucbVal;

        if (val > bestUCBStrategyVal)
        {
            bestUCBStrategy = strategy._name;
            bestUCBStrategyVal = val;
        }
    }

    Config::Strategy::StrategyName = bestUCBStrategy;
}

void StrategyManager::handleUrgentProductionIssues(BuildOrderQueue & queue)
{
	if (_selfRace == BWAPI::Races::Zerg)
	{
		StrategyBossZerg::Instance().handleUrgentProductionIssues(queue);
	}
	else
	{
		// detect if there's a supply block once per second
		if ((BWAPI::Broodwar->getFrameCount() % 24 == 1) && detectSupplyBlock(queue))
		{
			if (Config::Debug::DrawBuildOrderSearchInfo)
			{
				BWAPI::Broodwar->printf("Supply block, building supply!");
			}

			queue.queueAsHighestPriority(MacroAct(BWAPI::Broodwar->self()->getRace().getSupplyProvider()));
		}

		// If we need gas, make sure it is turned on.
		// NOTE Nothing for protoss or terran turns off gas if we get too much.
		if (!queue.isEmpty())
		{
			const MacroAct nextInQueue = queue.getHighestPriorityItem().macroAct;
			if (nextInQueue.gasPrice() > BWAPI::Broodwar->self()->gas())
			{
				WorkerManager::Instance().setCollectGas(true);
			}
		}

		// If they have mobile cloaked units, get some static detection.
		if (InformationManager::Instance().enemyHasMobileCloakTech())
		{
			if (BWAPI::Broodwar->self()->getRace() == BWAPI::Races::Protoss)
			{
				if (BWAPI::Broodwar->self()->allUnitCount(BWAPI::UnitTypes::Protoss_Photon_Cannon) < 2 &&
					!queue.anyInQueue(BWAPI::UnitTypes::Protoss_Photon_Cannon) &&
					!BuildingManager::Instance().isBeingBuilt(BWAPI::UnitTypes::Protoss_Photon_Cannon))
				{
					queue.queueAsHighestPriority(MacroAct(BWAPI::UnitTypes::Protoss_Photon_Cannon));
					queue.queueAsHighestPriority(MacroAct(BWAPI::UnitTypes::Protoss_Photon_Cannon));

					if (BWAPI::Broodwar->self()->allUnitCount(BWAPI::UnitTypes::Protoss_Forge) == 0 &&
						!BuildingManager::Instance().isBeingBuilt(BWAPI::UnitTypes::Protoss_Forge))
					{
						queue.queueAsHighestPriority(MacroAct(BWAPI::UnitTypes::Protoss_Forge));
					}
				}
			}
			else if (BWAPI::Broodwar->self()->getRace() == BWAPI::Races::Terran)
			{
				if (BWAPI::Broodwar->self()->allUnitCount(BWAPI::UnitTypes::Terran_Missile_Turret) < 3 &&
					!queue.anyInQueue(BWAPI::UnitTypes::Terran_Missile_Turret) &&
					!BuildingManager::Instance().isBeingBuilt(BWAPI::UnitTypes::Terran_Missile_Turret))
				{
					queue.queueAsHighestPriority(MacroAct(BWAPI::UnitTypes::Terran_Missile_Turret));
					queue.queueAsHighestPriority(MacroAct(BWAPI::UnitTypes::Terran_Missile_Turret));
					queue.queueAsHighestPriority(MacroAct(BWAPI::UnitTypes::Terran_Missile_Turret));

					if (BWAPI::Broodwar->self()->allUnitCount(BWAPI::UnitTypes::Terran_Engineering_Bay) == 0 &&
						!BuildingManager::Instance().isBeingBuilt(BWAPI::UnitTypes::Terran_Engineering_Bay))
					{
						queue.queueAsHighestPriority(MacroAct(BWAPI::UnitTypes::Terran_Engineering_Bay));
					}
				}
			}
		}
	}
}

// Called to refill the production queue when it is empty.
void StrategyManager::freshProductionPlan()
{
	if (_selfRace == BWAPI::Races::Zerg)
	{
		ProductionManager::Instance().setBuildOrder(StrategyBossZerg::Instance().freshProductionPlan());
	}
	else
	{
		performBuildOrderSearch();
	}
}

// Return true if we're supply blocked and should build supply.
// Note: This understands zerg supply but is not used when we are zerg.
bool StrategyManager::detectSupplyBlock(BuildOrderQueue & queue)
{
	// If the _queue is empty or supply is maxed, there is no block.
	if (queue.isEmpty() || BWAPI::Broodwar->self()->supplyTotal() >= 400)
	{
		return false;
	}

	// If supply is being built now, there's no block. Return right away.
	// Terran and protoss calculation:
	if (BuildingManager::Instance().isBeingBuilt(BWAPI::Broodwar->self()->getRace().getSupplyProvider()))
	{
		return false;
	}

	// Terran and protoss calculation:
	int supplyAvailable = BWAPI::Broodwar->self()->supplyTotal() - BWAPI::Broodwar->self()->supplyUsed();

	// Zerg calculation:
	// Zerg can create an overlord that doesn't count toward supply until the next check.
	// To work around it, add up the supply by hand, including hatcheries.
	if (BWAPI::Broodwar->self()->getRace() == BWAPI::Races::Zerg) {
		supplyAvailable = -BWAPI::Broodwar->self()->supplyUsed();
		for (auto & unit : BWAPI::Broodwar->self()->getUnits())
		{
			if (unit->getType() == BWAPI::UnitTypes::Zerg_Overlord)
			{
				supplyAvailable += 16;
			}
			else if (unit->getType() == BWAPI::UnitTypes::Zerg_Egg &&
				unit->getBuildType() == BWAPI::UnitTypes::Zerg_Overlord)
			{
				return false;    // supply is building, return immediately
				// supplyAvailable += 16;
			}
			else if ((unit->getType() == BWAPI::UnitTypes::Zerg_Hatchery && unit->isCompleted()) ||
				unit->getType() == BWAPI::UnitTypes::Zerg_Lair ||
				unit->getType() == BWAPI::UnitTypes::Zerg_Hive)
			{
				supplyAvailable += 2;
			}
		}
	}

	int supplyCost = queue.getHighestPriorityItem().macroAct.supplyRequired();
	// Available supply can be negative, which breaks the test below. Fix it.
	supplyAvailable = std::max(0, supplyAvailable);

	// if we don't have enough supply, we're supply blocked
	if (supplyAvailable < supplyCost)
	{
		// If we're zerg, check to see if a building is planned to be built.
		// Only count it as releasing supply very early in the game.
		if (BWAPI::Broodwar->self()->getRace() == BWAPI::Races::Zerg
			&& BuildingManager::Instance().buildingsQueued().size() > 0
			&& BWAPI::Broodwar->self()->supplyTotal() <= 18)
		{
			return false;
		}
		return true;
	}

	return false;
}

void StrategyManager::performBuildOrderSearch()
{
	if (!canPlanBuildOrderNow())
	{
		return;
	}

	BuildOrder & buildOrder = BOSSManager::Instance().getBuildOrder();

	if (buildOrder.size() > 0)
	{
		ProductionManager::Instance().setBuildOrder(buildOrder);
		BOSSManager::Instance().reset();
	}
	else
	{
		if (!BOSSManager::Instance().isSearchInProgress())
		{
			BOSSManager::Instance().startNewSearch(getBuildOrderGoal());
		}
	}
}

// this will return true if any unit is on the first frame of its training time remaining
// this can cause issues for the build order search system so don't plan a search on these frames
bool StrategyManager::canPlanBuildOrderNow() const
{
	for (const auto unit : BWAPI::Broodwar->self()->getUnits())
	{
		if (unit->getRemainingTrainTime() == 0)
		{
			continue;
		}

		BWAPI::UnitType trainType = unit->getLastCommand().getUnitType();

		if (unit->getRemainingTrainTime() == trainType.buildTime())
		{
			return false;
		}
	}

	return true;
}