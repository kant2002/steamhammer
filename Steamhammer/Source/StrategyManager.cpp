#include "Common.h"
#include "StrategyManager.h"
#include "UnitUtil.h"

using namespace UAlbertaBot;

// constructor
StrategyManager::StrategyManager() 
	: _selfRace(BWAPI::Broodwar->self()->getRace())
	, _enemyRace(BWAPI::Broodwar->enemy()->getRace())
    , _emptyBuildOrder(BWAPI::Broodwar->self()->getRace())
{
}

// get an instance of this
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
    BWAPI::Race myRace = BWAPI::Broodwar->self()->getRace();

    if (myRace == BWAPI::Races::Protoss)
    {
        return getProtossBuildOrderGoal();
    }
    else if (myRace == BWAPI::Races::Terran)
	{
		return getTerranBuildOrderGoal();
	}
    else if (myRace == BWAPI::Races::Zerg)
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
	if (InformationManager::Instance().enemyHasCloakedUnits())
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
	    goal.push_back(std::pair<MetaType, int>(BWAPI::UnitTypes::Terran_Marine, numMarines + 8));

        if (numMarines > 5)
        {
            goal.push_back(std::pair<MetaType, int>(BWAPI::UnitTypes::Terran_Engineering_Bay, 1));
        }
    }
    else if (Config::Strategy::StrategyName == "Terran_4RaxMarines")
    {
	    goal.push_back(std::pair<MetaType, int>(BWAPI::UnitTypes::Terran_Marine, numMarines + 8));
    }
    else if (Config::Strategy::StrategyName == "Terran_VultureRush")
    {
        goal.push_back(std::pair<MetaType, int>(BWAPI::UnitTypes::Terran_Vulture, numVultures + 8));

        if (numVultures > 8)
        {
            goal.push_back(std::pair<MetaType, int>(BWAPI::TechTypes::Tank_Siege_Mode, 1));
            goal.push_back(std::pair<MetaType, int>(BWAPI::UnitTypes::Terran_Siege_Tank_Tank_Mode, 4));
        }
    }
    else if (Config::Strategy::StrategyName == "Terran_TankPush")
    {
        goal.push_back(std::pair<MetaType, int>(BWAPI::UnitTypes::Terran_Siege_Tank_Tank_Mode, 6));
        goal.push_back(std::pair<MetaType, int>(BWAPI::UnitTypes::Terran_Goliath, numGoliath + 6));
        goal.push_back(std::pair<MetaType, int>(BWAPI::TechTypes::Tank_Siege_Mode, 1));
    }
    else
    {
        BWAPI::Broodwar->printf("Warning: No build order goal for Terran Strategy: %s", Config::Strategy::StrategyName.c_str());
    }

    if (shouldExpandNow())
    {
        goal.push_back(std::pair<MetaType, int>(BWAPI::UnitTypes::Terran_Command_Center, numCC + 1));
        goal.push_back(std::pair<MetaType, int>(BWAPI::UnitTypes::Terran_SCV, numWorkers + 10));
    }

	return goal;
}

const MetaPairVector StrategyManager::getZergBuildOrderGoal() const
{
	// the goal to return
	std::vector<MetaPair> goal;
	
	const int droneMax = 48;             // number of drones not to exceed

	int minerals		= BWAPI::Broodwar->self()->minerals();
	int gas				= BWAPI::Broodwar->self()->gas();

	// These counts include uncompleted units.
	int nLairs			= UnitUtil::GetAllUnitCount(BWAPI::UnitTypes::Zerg_Lair);
	int nHives			= UnitUtil::GetAllUnitCount(BWAPI::UnitTypes::Zerg_Hive);
    int nHatches        = UnitUtil::GetAllUnitCount(BWAPI::UnitTypes::Zerg_Hatchery)
                        + nLairs + nHives;
	int nGas			= UnitUtil::GetAllUnitCount(BWAPI::UnitTypes::Zerg_Extractor);
	bool hasQueensNest	= UnitUtil::GetAllUnitCount(BWAPI::UnitTypes::Zerg_Queens_Nest) > 0;
	bool hasGreaterSpire = UnitUtil::GetAllUnitCount(BWAPI::UnitTypes::Zerg_Greater_Spire) > 0;
	int nEvo			= UnitUtil::GetAllUnitCount(BWAPI::UnitTypes::Zerg_Evolution_Chamber);

	int nMutas			= UnitUtil::GetAllUnitCount(BWAPI::UnitTypes::Zerg_Mutalisk);
    int nDrones			= UnitUtil::GetAllUnitCount(BWAPI::UnitTypes::Zerg_Drone);
    int nLings			= UnitUtil::GetAllUnitCount(BWAPI::UnitTypes::Zerg_Zergling);
	int nHydras			= UnitUtil::GetAllUnitCount(BWAPI::UnitTypes::Zerg_Hydralisk);
	int nLurkers		= UnitUtil::GetAllUnitCount(BWAPI::UnitTypes::Zerg_Lurker);
    int nScourge		= UnitUtil::GetAllUnitCount(BWAPI::UnitTypes::Zerg_Scourge);
    int nGuardians		= UnitUtil::GetAllUnitCount(BWAPI::UnitTypes::Zerg_Guardian);
	int nDevourers		= UnitUtil::GetAllUnitCount(BWAPI::UnitTypes::Zerg_Devourer);

	BWAPI::Player self = BWAPI::Broodwar->self();

	if (Config::Strategy::StrategyName == "Zerg_4Pool" ||
		Config::Strategy::StrategyName == "Zerg_5Pool" ||
		Config::Strategy::StrategyName == "Zerg_9Pool")
	{
		// Zerglings.
        goal.push_back(std::pair<MetaType, int>(BWAPI::UnitTypes::Zerg_Zergling, nLings + 6));
		if (shouldExpandNow())
		{
			goal.push_back(std::pair<MetaType, int>(BWAPI::UnitTypes::Zerg_Hatchery, nHatches + 1));
			goal.push_back(std::pair<MetaType, int>(BWAPI::UnitTypes::Zerg_Drone, std::min(droneMax, nDrones + 12)));
		}
	}
	else if (Config::Strategy::StrategyName == "Zerg_9Pool>Lurk")
	{
		// Lurkling.
		// TODO This fails due to BOSS bug--all BOSS searches return no solution,
		// even if the goal is only to make 4 zerglings and it could be done in 1 step.
		// See to-do list.
		bool addingHatch = false;
		// Decide about hatcheries and drones.
		if (nHatches == 1 && minerals > 300 && nLurkers >= 4 && nDrones >= 8 * nHatches) {
			addingHatch = true;
			goal.push_back(std::pair<MetaType, int>(BWAPI::UnitTypes::Zerg_Hatchery, nHatches + 1));
			goal.push_back(std::pair<MetaType, int>(BWAPI::UnitTypes::Zerg_Drone, std::min(droneMax, nDrones + 6)));
		}
		else if (minerals > 500 && nLurkers >= 4 && nDrones >= 8 * nHatches) {
			if (minerals > 1000) {
				addingHatch = true;
				goal.push_back(std::pair<MetaType, int>(BWAPI::UnitTypes::Zerg_Hatchery, nHatches + 2));
				goal.push_back(std::pair<MetaType, int>(BWAPI::UnitTypes::Zerg_Drone, std::min(droneMax, nDrones + 9)));
			}
			else {
				addingHatch = true;
				goal.push_back(std::pair<MetaType, int>(BWAPI::UnitTypes::Zerg_Hatchery, nHatches + 1));
				goal.push_back(std::pair<MetaType, int>(BWAPI::UnitTypes::Zerg_Drone, std::min(droneMax, nDrones + 6)));
			}
		}
		else if (minerals > 600 && nLurkers >= 6 && nDrones < 8 * nHatches) {
			goal.push_back(std::pair<MetaType, int>(BWAPI::UnitTypes::Zerg_Drone, std::min(droneMax, nDrones + 6)));
		}
		else if (nHatches == 0 && minerals >= 300) {
			addingHatch = true;
			goal.push_back(std::pair<MetaType, int>(BWAPI::UnitTypes::Zerg_Hatchery, 1));
			goal.push_back(std::pair<MetaType, int>(BWAPI::UnitTypes::Zerg_Drone, std::min(droneMax, nDrones + 4)));
		}
		else {
			goal.push_back(std::pair<MetaType, int>(BWAPI::UnitTypes::Zerg_Drone, std::min(droneMax, nDrones + 3)));
		}
		// Decide about unit mix based on gas and minerals.
		// If low on gas, get lings and research ling upgrades.
		if (gas <= 100 && minerals >= 300 && !addingHatch) {
			goal.push_back(std::pair<MetaType, int>(BWAPI::UnitTypes::Zerg_Zergling, nLings + 4));
			goal.push_back(std::pair<MetaType, int>(BWAPI::UpgradeTypes::Metabolic_Boost, 1));
			if (nGas < nHatches) {
				goal.push_back(std::pair<MetaType, int>(BWAPI::UnitTypes::Zerg_Extractor, nGas + 1));
			}
			if (nHives > 0) {
				goal.push_back(std::pair<MetaType, int>(BWAPI::UpgradeTypes::Adrenal_Glands, 1));
			}
		}
		else {
			int nHydrasDone = BWAPI::Broodwar->self()->completedUnitCount(BWAPI::UnitTypes::Zerg_Hydralisk);
			//if (nHydrasDone > 0) {
			//	goal.push_back(std::pair<MetaType, int>(BWAPI::UnitTypes::Zerg_Lurker, nLurkers + nHydrasDone));
			//}
			//else {
			//	goal.push_back(std::pair<MetaType, int>(BWAPI::UnitTypes::Zerg_Hydralisk, 2));
			//}
			goal.push_back(std::pair<MetaType, int>(BWAPI::UnitTypes::Zerg_Zergling, nLings + 4));
		}
		// Decide about upgrades.
		if (minerals > 600 && gas > 400 && !addingHatch) {
			BWAPI::UpgradeType missile = BWAPI::UpgradeTypes::Zerg_Missile_Attacks;
			BWAPI::UpgradeType armor = BWAPI::UpgradeTypes::Zerg_Carapace;
			int missileUps = self->getUpgradeLevel(missile);
			int armorUps = self->getUpgradeLevel(armor);
			if (nEvo == 0) {
				goal.push_back(std::pair<MetaType, int>(BWAPI::UnitTypes::Zerg_Evolution_Chamber, 1));
			}
			else if (armorUps == 0 && !self->isUpgrading(armor)) {
				goal.push_back(std::pair<MetaType, int>(armor, 1));
			}
			else if (nHatches > 0 && nLairs + nHives == 0) {
				// Get lair if we somehow lost it.
				goal.push_back(std::pair<MetaType, int>(BWAPI::UnitTypes::Zerg_Lair, 1));
			}
			else if (armorUps == 1 && nLairs + nHives > 0 && !self->isUpgrading(armor)) {
				goal.push_back(std::pair<MetaType, int>(armor, 2));
			}
			else if (nLairs > 0 && !hasQueensNest) {
				goal.push_back(std::pair<MetaType, int>(BWAPI::UnitTypes::Zerg_Queens_Nest, 1));
			}
			else if (nLairs > 0 && hasQueensNest && nHives == 0) {
				goal.push_back(std::pair<MetaType, int>(BWAPI::UnitTypes::Zerg_Hive, 1));
			}
			else if (armorUps == 2 && !self->isUpgrading(armor) && nHives > 0) {
				goal.push_back(std::pair<MetaType, int>(armor, 3));
			}
			else if (armorUps == 3 && missileUps < 3 && !self->isUpgrading(missile) && nHives > 0) {
				goal.push_back(std::pair<MetaType, int>(armor, missileUps + 1));
			}
		}
	}
	else if (Config::Strategy::StrategyName == "Zerg_2Hatch>Hydra" ||
		     Config::Strategy::StrategyName == "Zerg_9Pool>Hydra")
    {
		// Hydras.
		// Decide about hatcheries and drones.
		if (minerals > 600 && nHydras >= 12 && nDrones >= 8 * nHatches) {
			if (minerals > 1200 && nHatches > 2) {
				goal.push_back(std::pair<MetaType, int>(BWAPI::UnitTypes::Zerg_Hatchery, nHatches + 2));
				goal.push_back(std::pair<MetaType, int>(BWAPI::UnitTypes::Zerg_Drone, std::min(droneMax, nDrones + 9)));
			}
			else {
				goal.push_back(std::pair<MetaType, int>(BWAPI::UnitTypes::Zerg_Hatchery, nHatches + 1));
				goal.push_back(std::pair<MetaType, int>(BWAPI::UnitTypes::Zerg_Drone, std::min(droneMax, nDrones + 6)));
			}
		}
		else if (minerals > 600 && nHydras >= 12 && nDrones < 8 * nHatches) {
			goal.push_back(std::pair<MetaType, int>(BWAPI::UnitTypes::Zerg_Drone, std::min(droneMax, nDrones + 6)));
		}
		else if (nHatches == 0 && minerals >= 300) {
			goal.push_back(std::pair<MetaType, int>(BWAPI::UnitTypes::Zerg_Hatchery, 1));
			goal.push_back(std::pair<MetaType, int>(BWAPI::UnitTypes::Zerg_Drone, std::min(droneMax, nDrones + 2)));
		}
		else {
			goal.push_back(std::pair<MetaType, int>(BWAPI::UnitTypes::Zerg_Drone, std::min(droneMax, nDrones + 4)));
		}
		// Decide about gas.
		if (nHatches > 0 && minerals / (gas + 24) > 5 && nGas < nHatches && nDrones >= 6 * nHatches) {
			goal.push_back(std::pair<MetaType, int>(BWAPI::UnitTypes::Zerg_Extractor, nGas + 1));
		}
		// Decide about combat units.
		if (gas <= 100 && minerals >= 700) {
			goal.push_back(std::pair<MetaType, int>(BWAPI::UnitTypes::Zerg_Zergling, nLings + 12));
			goal.push_back(std::pair<MetaType, int>(BWAPI::UpgradeTypes::Metabolic_Boost, 1));
			if (nHives > 0) {
				goal.push_back(std::pair<MetaType, int>(BWAPI::UpgradeTypes::Adrenal_Glands, 1));
			}
		} else {
			goal.push_back(std::pair<MetaType, int>(BWAPI::UnitTypes::Zerg_Hydralisk, nHydras + 8));
			if (self->getUpgradeLevel(BWAPI::UpgradeTypes::Muscular_Augments) == 0) {
				goal.push_back(std::pair<MetaType, int>(BWAPI::UpgradeTypes::Muscular_Augments, 1));
			}
			else if (self->getUpgradeLevel(BWAPI::UpgradeTypes::Grooved_Spines) == 0) {
				goal.push_back(std::pair<MetaType, int>(BWAPI::UpgradeTypes::Grooved_Spines, 1));
			}
		}
		// Decide about upgrades.
		// Have to do it step by step because BOSS makes a stupid blocking build order.
		if (minerals > 600 && gas > 400) {
			BWAPI::UpgradeType missile = BWAPI::UpgradeTypes::Zerg_Missile_Attacks;
			BWAPI::UpgradeType armor = BWAPI::UpgradeTypes::Zerg_Carapace;
			int missileUps = self->getUpgradeLevel(missile);
			int armorUps = self->getUpgradeLevel(armor);
			if (missileUps == 0 && !self->isUpgrading(missile)) {
				goal.push_back(std::pair<MetaType, int>(missile, 1));
			}
			else if (nLairs + nHives == 0) {
				goal.push_back(std::pair<MetaType, int>(BWAPI::UnitTypes::Zerg_Lair, 1));
			}
			else if (nLairs + nHives > 0 && missileUps == 1 && !self->isUpgrading(missile)) {
				goal.push_back(std::pair<MetaType, int>(missile, 2));
			}
			else if (nLairs > 0 && !hasQueensNest && nHives == 0) {
				goal.push_back(std::pair<MetaType, int>(BWAPI::UnitTypes::Zerg_Queens_Nest, 1));
			}
			else if (nLairs > 0 && hasQueensNest && nHives == 0) {
				goal.push_back(std::pair<MetaType, int>(BWAPI::UnitTypes::Zerg_Hive, 1));
			}
			else if (nHives > 0 && missileUps == 2 && !self->isUpgrading(missile)) {
				goal.push_back(std::pair<MetaType, int>(missile, 3));
			}
			else if (missileUps == 3 && armorUps < 3 && !self->isUpgrading(armor)) {
				goal.push_back(std::pair<MetaType, int>(armor, armorUps + 1));
			}
		}

	}
	else if (Config::Strategy::StrategyName == "Zerg_vsRush"
		  || Config::Strategy::StrategyName == "ZvZ_12Pool"
		  || Config::Strategy::StrategyName == "ZvT_12Pool")
	{
		// Mutalisks or lings, depending.
		// Decide about units.
		if (gas <= 100 && minerals >= 600) {
			goal.push_back(std::pair<MetaType, int>(BWAPI::UnitTypes::Zerg_Zergling, nLings + 8));
			goal.push_back(std::pair<MetaType, int>(BWAPI::UpgradeTypes::Metabolic_Boost, 1));
			if (nHives > 0) {
				goal.push_back(std::pair<MetaType, int>(BWAPI::UpgradeTypes::Adrenal_Glands, 1));
			}
		}
		else {
			goal.push_back(std::pair<MetaType, int>(BWAPI::UnitTypes::Zerg_Mutalisk, nMutas + 4));
		}
		// Decide about gas.
		if (minerals / (gas + 24) > 4 && nGas < nHatches && nDrones >= 5 * nHatches) {
			goal.push_back(std::pair<MetaType, int>(BWAPI::UnitTypes::Zerg_Extractor, nGas + 1));
		}
		// Decide about upgrades.
		if (minerals > 600 && gas > 400) {
			BWAPI::UpgradeType airArmor = BWAPI::UpgradeTypes::Zerg_Flyer_Carapace;
			int armorUps = self->getUpgradeLevel(airArmor);
			if (armorUps == 0 && !self->isUpgrading(airArmor)) {
				goal.push_back(std::pair<MetaType, int>(airArmor, 1));
			}
			else if (armorUps == 1 && !self->isUpgrading(airArmor)) {
				goal.push_back(std::pair<MetaType, int>(airArmor, 2));
			}
			else if (nHives == 0 && armorUps == 2 && !hasQueensNest) {
				goal.push_back(std::pair<MetaType, int>(BWAPI::UnitTypes::Zerg_Queens_Nest, 1));
			}
			else if (nHives == 0 && armorUps == 2 && hasQueensNest) {
				goal.push_back(std::pair<MetaType, int>(BWAPI::UnitTypes::Zerg_Hive, 1));
			}
			else if (nHives > 0 && armorUps == 2 && !self->isUpgrading(airArmor)) {
				goal.push_back(std::pair<MetaType, int>(airArmor, 3));
			}
		}
		// Decide about hatcheries and drones.
		if (minerals > 400 && nMutas >= 6 && nDrones >= 8 * nHatches) {
			if (minerals > 1000 && nHatches >= 2) {
				goal.push_back(std::pair<MetaType, int>(BWAPI::UnitTypes::Zerg_Hatchery, nHatches + 2));
				goal.push_back(std::pair<MetaType, int>(BWAPI::UnitTypes::Zerg_Drone, std::min(droneMax, nDrones + 12)));
			}
			else {
				goal.push_back(std::pair<MetaType, int>(BWAPI::UnitTypes::Zerg_Hatchery, nHatches + 1));
				goal.push_back(std::pair<MetaType, int>(BWAPI::UnitTypes::Zerg_Drone, std::min(droneMax, nDrones + 8)));
			}
		}
		else if (minerals > 400 && nMutas >= 6 && nDrones < 8 * nHatches) {
			goal.push_back(std::pair<MetaType, int>(BWAPI::UnitTypes::Zerg_Drone, std::min(droneMax, nDrones + 8)));
		}
		else if (nHatches == 0 && minerals >= 300) {
			goal.push_back(std::pair<MetaType, int>(BWAPI::UnitTypes::Zerg_Hatchery, 1));
			goal.push_back(std::pair<MetaType, int>(BWAPI::UnitTypes::Zerg_Drone, std::min(droneMax, nDrones + 8)));
		}
		else {
			goal.push_back(std::pair<MetaType, int>(BWAPI::UnitTypes::Zerg_Drone, std::min(droneMax, nDrones + 4)));
		}
	}
	else if (Config::Strategy::StrategyName == "Zerg_3Hatch>HydraLing")
	{
		// Hydraling.
		// Decide about hatcheries and drones.
		if (minerals > 600 && nHydras >= 12 && nDrones >= 8 * nHatches) {
			if (minerals > 1200 && nHatches > 2) {
				goal.push_back(std::pair<MetaType, int>(BWAPI::UnitTypes::Zerg_Hatchery, nHatches + 2));
				goal.push_back(std::pair<MetaType, int>(BWAPI::UnitTypes::Zerg_Drone, std::min(droneMax, nDrones + 9)));
			}
			else {
				goal.push_back(std::pair<MetaType, int>(BWAPI::UnitTypes::Zerg_Hatchery, nHatches + 1));
				goal.push_back(std::pair<MetaType, int>(BWAPI::UnitTypes::Zerg_Drone, std::min(droneMax, nDrones + 6)));
			}
		}
		else if (minerals > 600 && nHydras >= 12 && nDrones < 8 * nHatches) {
			goal.push_back(std::pair<MetaType, int>(BWAPI::UnitTypes::Zerg_Drone, std::min(droneMax, nDrones + 8)));
		}
		else if (nHatches == 0 && minerals >= 300) {
			goal.push_back(std::pair<MetaType, int>(BWAPI::UnitTypes::Zerg_Hatchery, 1));
			goal.push_back(std::pair<MetaType, int>(BWAPI::UnitTypes::Zerg_Drone, std::min(droneMax, nDrones + 8)));
		}
		else {
			goal.push_back(std::pair<MetaType, int>(BWAPI::UnitTypes::Zerg_Drone, std::min(droneMax, nDrones + 4)));
		}
		// Decide about unit mix based on gas and minerals.
		// If low on gas, get lings and research ling upgrades.
		if (gas <= 100 && minerals >= 600) {
			goal.push_back(std::pair<MetaType, int>(BWAPI::UnitTypes::Zerg_Zergling, nLings + 10));
			goal.push_back(std::pair<MetaType, int>(BWAPI::UpgradeTypes::Metabolic_Boost, 1));
			if (nHatches > 0 && nGas < nHatches) {
				goal.push_back(std::pair<MetaType, int>(BWAPI::UnitTypes::Zerg_Extractor, nGas + 1));
			}
			if (nHives > 0) {
				goal.push_back(std::pair<MetaType, int>(BWAPI::UpgradeTypes::Adrenal_Glands, 1));
			}
		}
		else {
			goal.push_back(std::pair<MetaType, int>(BWAPI::UnitTypes::Zerg_Hydralisk, nHydras + 8));
			goal.push_back(std::pair<MetaType, int>(BWAPI::UnitTypes::Zerg_Zergling, nLings + 2));
		}
		// Decide about upgrades.
		if (minerals > 600 && gas > 400) {
			BWAPI::UpgradeType missile = BWAPI::UpgradeTypes::Zerg_Missile_Attacks;
			BWAPI::UpgradeType armor = BWAPI::UpgradeTypes::Zerg_Carapace;
			int missileUps = self->getUpgradeLevel(missile);
			int armorUps = self->getUpgradeLevel(armor);
			if (missileUps == 0 && !self->isUpgrading(missile)) {
				goal.push_back(std::pair<MetaType, int>(missile, 1));
			}
			// Get lair in parallel with missile attack upgrade.
			if (nHatches > 0 && nLairs + nHives == 0) {
				goal.push_back(std::pair<MetaType, int>(BWAPI::UnitTypes::Zerg_Lair, 1));
			}
			else if (missileUps == 1 && nLairs + nHives > 0 && !self->isUpgrading(missile)) {
				goal.push_back(std::pair<MetaType, int>(missile, 2));
			}
			else if (nLairs > 0 && !hasQueensNest) {
				goal.push_back(std::pair<MetaType, int>(BWAPI::UnitTypes::Zerg_Queens_Nest, 1));
			}
			else if (nLairs > 0 && hasQueensNest && nHives == 0) {
				goal.push_back(std::pair<MetaType, int>(BWAPI::UnitTypes::Zerg_Hive, 1));
			}
			else if (missileUps == 2 && !self->isUpgrading(missile) && nHives > 0) {
				goal.push_back(std::pair<MetaType, int>(missile, 3));
			}
			else if (missileUps == 3 && armorUps < 3 && !self->isUpgrading(armor) && nHives > 0) {
				goal.push_back(std::pair<MetaType, int>(armor, armorUps + 1));
			}
		}
	}
	else if (Config::Strategy::StrategyName == "Zerg_3HatchMuta" ||
			 Config::Strategy::StrategyName == "Zerg_3Hatch>Muta")
    {
		// Mutaling, adding guardian.
		// Decide about hatcheries and drones.
		if (minerals > 600 && nMutas >= 12 && nDrones >= 10 * nHatches) {
			if (minerals > 1200 && nHatches > 2) {
				goal.push_back(std::pair<MetaType, int>(BWAPI::UnitTypes::Zerg_Hatchery, nHatches + 2));
				goal.push_back(std::pair<MetaType, int>(BWAPI::UnitTypes::Zerg_Drone, std::min(droneMax, nDrones + 10)));
			}
			else {
				goal.push_back(std::pair<MetaType, int>(BWAPI::UnitTypes::Zerg_Hatchery, nHatches + 1));
				goal.push_back(std::pair<MetaType, int>(BWAPI::UnitTypes::Zerg_Drone, std::min(droneMax, nDrones + 6)));
			}
		}
		else if (minerals > 600 && nMutas >= 12 && nDrones < 8 * nHatches) {
			goal.push_back(std::pair<MetaType, int>(BWAPI::UnitTypes::Zerg_Drone, std::min(droneMax, nDrones + 8)));
		}
		else if (nHatches == 0 && minerals >= 300) {
			goal.push_back(std::pair<MetaType, int>(BWAPI::UnitTypes::Zerg_Hatchery, 1));
			goal.push_back(std::pair<MetaType, int>(BWAPI::UnitTypes::Zerg_Drone, std::min(droneMax, nDrones + 8)));
		}
		else {
			goal.push_back(std::pair<MetaType, int>(BWAPI::UnitTypes::Zerg_Drone, std::min(droneMax, nDrones + 4)));
		}
		// Decide about unit mix based on gas and minerals.
		// If low on gas, get lings and research ling upgrades.
		if (gas < 100 && minerals >= 600) {
			goal.push_back(std::pair<MetaType, int>(BWAPI::UnitTypes::Zerg_Zergling, nLings + 10));
			if (nHatches > 0 && nGas < nHatches) {
				goal.push_back(std::pair<MetaType, int>(BWAPI::UnitTypes::Zerg_Extractor, nGas + 1));
			}
			if (nHives > 0) {
				goal.push_back(std::pair<MetaType, int>(BWAPI::UpgradeTypes::Adrenal_Glands, 1));
			}
		}
		else if (nGas >= 4) {
			goal.push_back(std::pair<MetaType, int>(BWAPI::UnitTypes::Zerg_Mutalisk, nMutas + 6));
			goal.push_back(std::pair<MetaType, int>(BWAPI::UnitTypes::Zerg_Mutalisk, nGuardians + 2));
			goal.push_back(std::pair<MetaType, int>(BWAPI::UnitTypes::Zerg_Zergling, nLings + 4));
		}
		else {
			goal.push_back(std::pair<MetaType, int>(BWAPI::UnitTypes::Zerg_Mutalisk, nMutas + 8));
			goal.push_back(std::pair<MetaType, int>(BWAPI::UnitTypes::Zerg_Zergling, nLings + 4));
		}
		// Decide about upgrades.
		if (minerals > 500 && nGas >= 2) {
			// Air armor first.
			BWAPI::UpgradeType attack = BWAPI::UpgradeTypes::Zerg_Flyer_Attacks;
			BWAPI::UpgradeType armor = BWAPI::UpgradeTypes::Zerg_Flyer_Carapace;
			int attackUps = self->getUpgradeLevel(attack);
			int armorUps = self->getUpgradeLevel(armor);
			// Lair and hive.
			if (nHatches > 0 && nLairs + nHives == 0) {
				// Get lair in parallel if we've lost it.
				goal.push_back(std::pair<MetaType, int>(BWAPI::UnitTypes::Zerg_Lair, 1));
			}
			else if (nLairs > 0 && nHives == 0 && nGas >= 3) {
				// Otherwise go straight to hive if we have enough gas.
				if (hasQueensNest) {
					goal.push_back(std::pair<MetaType, int>(BWAPI::UnitTypes::Zerg_Hive, 1));
				}
				else {
					goal.push_back(std::pair<MetaType, int>(BWAPI::UnitTypes::Zerg_Queens_Nest, 1));
				}
			}
			// Upgrades, including greater spire.
			if (nHives > 0 && nGas >= 4 && !self->isUpgrading(armor) && !self->isUpgrading(attack)) {
				// We have enough gas, get greater spire as next priority.
				goal.push_back(std::pair<MetaType, int>(BWAPI::UnitTypes::Zerg_Greater_Spire, 1));
			}
			else if (armorUps < 2 && !self->isUpgrading(armor)) {
				goal.push_back(std::pair<MetaType, int>(armor, armorUps + 1));
			}
			else if (armorUps == 2 && !self->isUpgrading(armor) && nHives > 0) {
				goal.push_back(std::pair<MetaType, int>(armor, 3));
			}
			else if (armorUps == 3 && attackUps < 3 && !self->isUpgrading(attack) && nHives > 0) {
				goal.push_back(std::pair<MetaType, int>(armor, attackUps + 1));
			}
		}
	}
    else if (Config::Strategy::StrategyName == "Zerg_3HatchScourge")
    {
        if (nScourge > 40)
        {
            goal.push_back(std::pair<MetaType, int>(BWAPI::UnitTypes::Zerg_Hydralisk, nHydras + 12));
			goal.push_back(std::pair<MetaType, int>(BWAPI::UpgradeTypes::Muscular_Augments, 1));
			goal.push_back(std::pair<MetaType, int>(BWAPI::UpgradeTypes::Grooved_Spines, 1));
		}
        else
        {
            goal.push_back(std::pair<MetaType, int>(BWAPI::UnitTypes::Zerg_Scourge, nScourge + 12));
        }

		goal.push_back(std::pair<MetaType, int>(BWAPI::UnitTypes::Zerg_Drone, std::min(droneMax, nDrones + 5)));
		if (shouldExpandNow())
		{
			goal.push_back(std::pair<MetaType, int>(BWAPI::UnitTypes::Zerg_Hatchery, nHatches + 1));
			goal.push_back(std::pair<MetaType, int>(BWAPI::UnitTypes::Zerg_Drone, std::min(droneMax, nDrones + 10)));
		}
	}
	else {
		// Any other strategy.
		goal.push_back(std::pair<MetaType, int>(BWAPI::UnitTypes::Zerg_Hydralisk, nHydras + 12));
		goal.push_back(std::pair<MetaType, int>(BWAPI::UpgradeTypes::Muscular_Augments, 1));
		goal.push_back(std::pair<MetaType, int>(BWAPI::UpgradeTypes::Grooved_Spines, 1));
		goal.push_back(std::pair<MetaType, int>(BWAPI::UnitTypes::Zerg_Drone, std::min(droneMax, nDrones + 2)));
		if (shouldExpandNow())
		{
			goal.push_back(std::pair<MetaType, int>(BWAPI::UnitTypes::Zerg_Hatchery, nHatches + 1));
			goal.push_back(std::pair<MetaType, int>(BWAPI::UnitTypes::Zerg_Drone, std::min(droneMax, nDrones + 10)));
		}
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
        if (strategy._race == BWAPI::Broodwar->self()->getRace())
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
        if (strategy._race != BWAPI::Broodwar->self()->getRace())
        {
            continue;
        }

        int sGamesPlayed = strategy._wins + strategy._losses;
        double sWinRate = sGamesPlayed > 0 ? currentStrategy._wins / static_cast<double>(strategyGamesPlayed) : 0;
        double ucbVal = C * sqrt( log( (double)totalGamesPlayed / sGamesPlayed ) );
        double val = sWinRate + ucbVal;

        if (val > bestUCBStrategyVal)
        {
            bestUCBStrategy = strategy._name;
            bestUCBStrategyVal = val;
        }
    }

    Config::Strategy::StrategyName = bestUCBStrategy;
}