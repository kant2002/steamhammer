#include "StrategyManager.h"
#include "ProductionManager.h"
#include "StrategyBossZerg.h"
#include "UnitUtil.h"

using namespace UAlbertaBot;

StrategyManager::StrategyManager() 
	: _selfRace(BWAPI::Broodwar->self()->getRace())
	, _enemyRace(BWAPI::Broodwar->enemy()->getRace())
    , _emptyBuildOrder(BWAPI::Broodwar->self()->getRace())
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
        //BWAPI::Broodwar->printf("No valid expansion location");
		return false;
	}

	size_t numDepots    = UnitUtil::GetAllUnitCount(BWAPI::UnitTypes::Terran_Command_Center)
                        + UnitUtil::GetAllUnitCount(BWAPI::UnitTypes::Protoss_Nexus)
                        + UnitUtil::GetAllUnitCount(BWAPI::UnitTypes::Zerg_Hatchery)
                        + UnitUtil::GetAllUnitCount(BWAPI::UnitTypes::Zerg_Lair)
                        + UnitUtil::GetAllUnitCount(BWAPI::UnitTypes::Zerg_Hive);
	int frame           = BWAPI::Broodwar->getFrameCount();
    int minute          = frame / (24*60);

	// if we have a ton of idle workers then we need a new expansion
	if (WorkerManager::Instance().getNumIdleWorkers() > 10)
	{
		return true;
	}

    // if we have a ridiculous stockpile of minerals, expand
	if (BWAPI::Broodwar->self()->minerals() > 600)
    {
        return true;
    }

    // we will make expansion N after array[N] minutes have passed
    std::vector<int> expansionTimes = {5, 10, 14, 17, 20, 22};

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

const MetaPairVector StrategyManager::getProtossBuildOrderGoal() const
{
	// the goal to return
	MetaPairVector goal;

	int numZealots          = UnitUtil::GetAllUnitCount(BWAPI::UnitTypes::Protoss_Zealot);
    int numPylons           = UnitUtil::GetAllUnitCount(BWAPI::UnitTypes::Protoss_Pylon);
	int numDragoons         = UnitUtil::GetAllUnitCount(BWAPI::UnitTypes::Protoss_Dragoon);
	int numProbes           = UnitUtil::GetAllUnitCount(BWAPI::UnitTypes::Protoss_Probe);
	int numNexusCompleted   = BWAPI::Broodwar->self()->completedUnitCount(BWAPI::UnitTypes::Protoss_Nexus);
	int numNexusAll         = UnitUtil::GetAllUnitCount(BWAPI::UnitTypes::Protoss_Nexus);
	int numCyber            = BWAPI::Broodwar->self()->completedUnitCount(BWAPI::UnitTypes::Protoss_Cybernetics_Core);
	int numCannon           = UnitUtil::GetAllUnitCount(BWAPI::UnitTypes::Protoss_Photon_Cannon);
    int numScout            = UnitUtil::GetAllUnitCount(BWAPI::UnitTypes::Protoss_Corsair);
    int numReaver           = UnitUtil::GetAllUnitCount(BWAPI::UnitTypes::Protoss_Reaver);
    int numDarkTeplar       = UnitUtil::GetAllUnitCount(BWAPI::UnitTypes::Protoss_Dark_Templar);

    if (Config::Strategy::StrategyName == "Protoss_ZealotRush")
    {
        goal.push_back(MetaPair(BWAPI::UnitTypes::Protoss_Zealot, numZealots + 8));

        // once we have a 2nd nexus start making dragoons
        if (numNexusAll >= 2)
        {
            goal.push_back(MetaPair(BWAPI::UnitTypes::Protoss_Dragoon, numDragoons + 4));
        }
    }
    else if (Config::Strategy::StrategyName == "Protoss_DragoonRush")
    {
        goal.push_back(MetaPair(BWAPI::UnitTypes::Protoss_Dragoon, numDragoons + 6));
    }
    else if (Config::Strategy::StrategyName == "Protoss_Drop")
    {
        if (numZealots == 0)
        {
            goal.push_back(MetaPair(BWAPI::UnitTypes::Protoss_Zealot, numZealots + 4));
            goal.push_back(MetaPair(BWAPI::UnitTypes::Protoss_Shuttle, 1));
        }
        else
        {
            goal.push_back(MetaPair(BWAPI::UnitTypes::Protoss_Zealot, numZealots + 8));
        }
    }
    else if (Config::Strategy::StrategyName == "Protoss_DTRush")
    {
        goal.push_back(MetaPair(BWAPI::UnitTypes::Protoss_Dark_Templar, numDarkTeplar + 2));

        // if we have a 2nd nexus then get some goons out
        if (numNexusAll >= 2)
        {
            goal.push_back(MetaPair(BWAPI::UnitTypes::Protoss_Dragoon, numDragoons + 4));
        }
    }
    else
    {
        UAB_ASSERT_WARNING(false, "Unknown Protoss Strategy Name: %s", Config::Strategy::StrategyName.c_str());
    }

    // if we have 3 nexus, make an observer
    if (numNexusCompleted >= 3)
    {
        goal.push_back(MetaPair(BWAPI::UnitTypes::Protoss_Observer, 1));
    }
    
    // add observer to the goal if the enemy has cloaked units
	if (InformationManager::Instance().enemyHasCloakTech())
	{
		goal.push_back(MetaPair(BWAPI::UnitTypes::Protoss_Robotics_Facility, 1));
		
		if (BWAPI::Broodwar->self()->completedUnitCount(BWAPI::UnitTypes::Protoss_Robotics_Facility) > 0)
		{
			goal.push_back(MetaPair(BWAPI::UnitTypes::Protoss_Observatory, 1));
		}
		if (BWAPI::Broodwar->self()->completedUnitCount(BWAPI::UnitTypes::Protoss_Observatory) > 0)
		{
			goal.push_back(MetaPair(BWAPI::UnitTypes::Protoss_Observer, 1));
		}
	}

    // if we want to expand, insert a nexus into the build order
	if (shouldExpandNow())
	{
		goal.push_back(MetaPair(BWAPI::UnitTypes::Protoss_Nexus, numNexusAll + 1));
	}

	return goal;
}

const MetaPairVector StrategyManager::getTerranBuildOrderGoal() const
{
	// the goal to return
	std::vector<MetaPair> goal;

    int numWorkers      = UnitUtil::GetAllUnitCount(BWAPI::UnitTypes::Terran_SCV);
    int numCC           = UnitUtil::GetAllUnitCount(BWAPI::UnitTypes::Terran_Command_Center);            
    int numMarines      = UnitUtil::GetAllUnitCount(BWAPI::UnitTypes::Terran_Marine);
	int numMedics       = UnitUtil::GetAllUnitCount(BWAPI::UnitTypes::Terran_Medic);
	int numWraith       = UnitUtil::GetAllUnitCount(BWAPI::UnitTypes::Terran_Wraith);
    int numVultures     = UnitUtil::GetAllUnitCount(BWAPI::UnitTypes::Terran_Vulture);
    int numGoliath      = UnitUtil::GetAllUnitCount(BWAPI::UnitTypes::Terran_Goliath);
    int numTanks        = UnitUtil::GetAllUnitCount(BWAPI::UnitTypes::Terran_Siege_Tank_Tank_Mode)
                        + UnitUtil::GetAllUnitCount(BWAPI::UnitTypes::Terran_Siege_Tank_Siege_Mode);
    int numBay          = UnitUtil::GetAllUnitCount(BWAPI::UnitTypes::Terran_Engineering_Bay);

    if (Config::Strategy::StrategyName == "Terran_MarineRush")
    {
	    goal.push_back(std::pair<MacroAct, int>(BWAPI::UnitTypes::Terran_Marine, numMarines + 8));

        if (numMarines > 5)
        {
            goal.push_back(std::pair<MacroAct, int>(BWAPI::UnitTypes::Terran_Engineering_Bay, 1));
        }
    }
    else if (Config::Strategy::StrategyName == "Terran_4RaxMarines")
    {
	    goal.push_back(std::pair<MacroAct, int>(BWAPI::UnitTypes::Terran_Marine, numMarines + 8));
    }
    else if (Config::Strategy::StrategyName == "Terran_VultureRush")
    {
        goal.push_back(std::pair<MacroAct, int>(BWAPI::UnitTypes::Terran_Vulture, numVultures + 8));

        if (numVultures > 8)
        {
            goal.push_back(std::pair<MacroAct, int>(BWAPI::TechTypes::Tank_Siege_Mode, 1));
            goal.push_back(std::pair<MacroAct, int>(BWAPI::UnitTypes::Terran_Siege_Tank_Tank_Mode, 4));
        }
    }
    else if (Config::Strategy::StrategyName == "Terran_TankPush")
    {
        goal.push_back(std::pair<MacroAct, int>(BWAPI::UnitTypes::Terran_Siege_Tank_Tank_Mode, 6));
        goal.push_back(std::pair<MacroAct, int>(BWAPI::UnitTypes::Terran_Goliath, numGoliath + 6));
        goal.push_back(std::pair<MacroAct, int>(BWAPI::TechTypes::Tank_Siege_Mode, 1));
    }
    else
    {
        BWAPI::Broodwar->printf("Warning: No build order goal for Terran Strategy: %s", Config::Strategy::StrategyName.c_str());
    }

    if (shouldExpandNow())
    {
        goal.push_back(std::pair<MacroAct, int>(BWAPI::UnitTypes::Terran_Command_Center, numCC + 1));
        goal.push_back(std::pair<MacroAct, int>(BWAPI::UnitTypes::Terran_SCV, numWorkers + 10));
    }

	return goal;
}

// BOSS method of choosing a zerg production plan. CURRENTLY UNUSED!
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

		// If they have mobile cloaked units, get some static detection.
		if (InformationManager::Instance().enemyHasMobileCloakTech())
		{
			if (BWAPI::Broodwar->self()->getRace() == BWAPI::Races::Protoss)
			{
				if (BWAPI::Broodwar->self()->allUnitCount(BWAPI::UnitTypes::Protoss_Photon_Cannon) < 2)
				{
					queue.queueAsHighestPriority(MacroAct(BWAPI::UnitTypes::Protoss_Photon_Cannon));
					queue.queueAsHighestPriority(MacroAct(BWAPI::UnitTypes::Protoss_Photon_Cannon));
				}
				if (BWAPI::Broodwar->self()->allUnitCount(BWAPI::UnitTypes::Protoss_Forge) == 0)
				{
					queue.queueAsHighestPriority(MacroAct(BWAPI::UnitTypes::Protoss_Forge));
				}
			}
			else if (BWAPI::Broodwar->self()->getRace() == BWAPI::Races::Terran)
			{
				if (BWAPI::Broodwar->self()->allUnitCount(BWAPI::UnitTypes::Terran_Missile_Turret) < 2)
				{
					queue.queueAsHighestPriority(MacroAct(BWAPI::UnitTypes::Terran_Missile_Turret));
					queue.queueAsHighestPriority(MacroAct(BWAPI::UnitTypes::Terran_Missile_Turret));
				}
				if (BWAPI::Broodwar->self()->allUnitCount(BWAPI::UnitTypes::Terran_Engineering_Bay) == 0)
				{
					queue.queueAsHighestPriority(MacroAct(BWAPI::UnitTypes::Terran_Engineering_Bay));
				}
			}

			if (Config::Debug::DrawBuildOrderSearchInfo)
			{
				BWAPI::Broodwar->printf("Enemy has cloaking tech!");
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
	for (const auto & unit : BWAPI::Broodwar->self()->getUnits())
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