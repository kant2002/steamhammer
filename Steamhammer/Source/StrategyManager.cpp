#include "StrategyManager.h"

#include "Bases.h"
#include "CombatCommander.h"
#include "MapTools.h"
#include "OpponentModel.h"
#include "ProductionManager.h"
#include "StrategyBossZerg.h"
#include "UnitUtil.h"

using namespace UAlbertaBot;

StrategyManager::StrategyManager() 
	: _selfRace(the.self()->getRace())
	, _enemyRace(the.enemy()->getRace())
    , _emptyBuildOrder(the.self()->getRace())
	, _openingGroup("")
	, _hasDropTech(false)
	, _highWaterBases(1)
	, _openingStaticDefenseDropped(false)
{
}

StrategyManager & StrategyManager::Instance() 
{
	static StrategyManager instance;
	return instance;
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

// This is used for terran and protoss.
const bool StrategyManager::shouldExpandNow() const
{
	// if there is no place to expand to, we can't expand
	// We check mineral expansions only.
	if (the.map.getNextExpansion(false, true, false) == BWAPI::TilePositions::None)
	{
		return false;
	}

	// if we have idle workers then we need a new expansion
	if (WorkerManager::Instance().getNumIdleWorkers() > 3)
	{
		return true;
	}

    // if we have excess minerals, expand
	if (the.self()->minerals() > 600)
    {
        return true;
    }

	size_t numDepots =
        the.my.all.count(BWAPI::UnitTypes::Terran_Command_Center) +
		the.my.all.count(BWAPI::UnitTypes::Protoss_Nexus);
	int minute = the.now() / (24 * 60);

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

	// These counts include uncompleted units (except for numNexusCompleted).
	int numPylons = the.my.all.count(BWAPI::UnitTypes::Protoss_Pylon);
    int numNexusCompleted = the.my.completed.count(BWAPI::UnitTypes::Protoss_Nexus);
	int numNexusAll = the.my.all.count(BWAPI::UnitTypes::Protoss_Nexus);
	int numProbes = the.my.all.count(BWAPI::UnitTypes::Protoss_Probe);
	int numCannon = the.my.all.count(BWAPI::UnitTypes::Protoss_Photon_Cannon);
	int numObservers = the.my.all.count(BWAPI::UnitTypes::Protoss_Observer);
	int numZealots = the.my.all.count(BWAPI::UnitTypes::Protoss_Zealot);
	int numDragoons = the.my.all.count(BWAPI::UnitTypes::Protoss_Dragoon);
	int numDarkTemplar = the.my.all.count(BWAPI::UnitTypes::Protoss_Dark_Templar);
	int numReavers = the.my.all.count(BWAPI::UnitTypes::Protoss_Reaver);
	int numCorsairs = the.my.all.count(BWAPI::UnitTypes::Protoss_Corsair);
	int numCarriers = the.my.all.count(BWAPI::UnitTypes::Protoss_Carrier);

	bool hasStargate = the.my.completed.count(BWAPI::UnitTypes::Protoss_Stargate) > 0;

	int maxProbes = WorkerManager::Instance().getMaxWorkers();

	PlayerSnapshot enemies(the.enemy());

	BWAPI::Player self = the.self();

	if (_openingGroup == "zealots")
	{
		goal.push_back(MetaPair(BWAPI::UnitTypes::Protoss_Zealot, numZealots + 6));

		if (numNexusAll >= 3)
		{
			// In the end, switch to carriers; not so many dragoons.
			goal.push_back(MetaPair(BWAPI::UpgradeTypes::Carrier_Capacity, 1));
			goal.push_back(MetaPair(BWAPI::UnitTypes::Protoss_Carrier, numCarriers + 1));
			goal.push_back(MetaPair(BWAPI::UnitTypes::Protoss_Dragoon, numDragoons + 1));
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
		if (the.my.all.count(BWAPI::UnitTypes::Protoss_Citadel_of_Adun) > 0)
		{
			goal.push_back(MetaPair(BWAPI::UnitTypes::Protoss_Templar_Archives, 1));
		}

		// If we have templar archives, make
		// 1. a small fixed number of dark templar to force a reaction, and
		// 2. an even number of high templar to merge into archons (so the high templar disappear quickly).
		if (the.my.completed.count(BWAPI::UnitTypes::Protoss_Templar_Archives) > 0)
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
		if (the.my.completed.count(BWAPI::UnitTypes::Protoss_Templar_Archives) > 0)
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
		_openingGroup = "dragoons";    // we're misconfigured, but try to do something
	}

	// If we're doing a corsair thing and it's still working, slowly add more.
	if (_enemyRace == BWAPI::Races::Zerg)
	{
		if (hasStargate)
		{
			if (numCorsairs < 6 && self->deadUnitCount(BWAPI::UnitTypes::Protoss_Corsair) == 0 ||
				numCorsairs < 9 && enemies.count(BWAPI::UnitTypes::Zerg_Mutalisk > numCorsairs))
			{
				goal.push_back(MetaPair(BWAPI::UnitTypes::Protoss_Corsair, numCorsairs + 1));
			}
		}
		else
		{
			// No stargate. Make one if it's useful.
			if (enemies.count(BWAPI::UnitTypes::Zerg_Mutalisk) > 3)
			{
				goal.push_back(MetaPair(BWAPI::UnitTypes::Protoss_Stargate, 1));
			}
		}
	}

	// Maybe get some static defense against air attack.
	const int enemyAirToGround =
		enemies.count(BWAPI::UnitTypes::Terran_Wraith) / 8 +
		enemies.count(BWAPI::UnitTypes::Terran_Battlecruiser) / 3 +
		enemies.count(BWAPI::UnitTypes::Protoss_Scout) / 5 +
		enemies.count(BWAPI::UnitTypes::Zerg_Mutalisk) / 6;
	if (enemyAirToGround > 0)
	{
		goal.push_back(std::pair<MacroAct, int>(BWAPI::UnitTypes::Protoss_Photon_Cannon, enemyAirToGround));
	}

	// Get observers if we have a second base, or if the enemy has cloaked units.
	if (numNexusCompleted >= 2 || InformationManager::Instance().enemyHasCloakTech())
	{
		goal.push_back(MetaPair(BWAPI::UnitTypes::Protoss_Robotics_Facility, 1));

		if (numObservers < 3 && self->completedUnitCount(BWAPI::UnitTypes::Protoss_Robotics_Facility) > 0)
		{
			goal.push_back(MetaPair(BWAPI::UnitTypes::Protoss_Observer, numObservers + 1));
		}
	}

	// Make more probes, up to a limit.
	goal.push_back(MetaPair(BWAPI::UnitTypes::Protoss_Probe, std::min(maxProbes, numProbes + 8)));

	// If the map has islands, get drop after we have 3 bases.
	if (Config::Macro::ExpandToIslands && numNexusCompleted >= 3 && the.bases.hasIslandBases())
	{
		goal.push_back(MetaPair(BWAPI::UnitTypes::Protoss_Shuttle, 1));
	}

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
	int numSCVs			= the.my.all.count(BWAPI::UnitTypes::Terran_SCV);
    int numCC           = the.my.all.count(BWAPI::UnitTypes::Terran_Command_Center);            
    int numRefineries   = the.my.all.count(BWAPI::UnitTypes::Terran_Refinery);            
    int numMarines      = the.my.all.count(BWAPI::UnitTypes::Terran_Marine);
	int numMedics       = the.my.all.count(BWAPI::UnitTypes::Terran_Medic);
	int numWraith       = the.my.all.count(BWAPI::UnitTypes::Terran_Wraith);
    int numVultures     = the.my.all.count(BWAPI::UnitTypes::Terran_Vulture);
	int numVessels		= the.my.all.count(BWAPI::UnitTypes::Terran_Science_Vessel);
	int numGoliaths		= the.my.all.count(BWAPI::UnitTypes::Terran_Goliath);
    int numTanks        = the.my.all.count(BWAPI::UnitTypes::Terran_Siege_Tank_Tank_Mode)
                        + the.my.all.count(BWAPI::UnitTypes::Terran_Siege_Tank_Siege_Mode);

	bool hasEBay		= the.my.completed.count(BWAPI::UnitTypes::Terran_Engineering_Bay) > 0;
	bool hasAcademy		= the.my.completed.count(BWAPI::UnitTypes::Terran_Academy) > 0;
	bool hasArmory		= the.my.completed.count(BWAPI::UnitTypes::Terran_Armory) > 0;

	int maxSCVs = WorkerManager::Instance().getMaxWorkers();

	bool makeVessel = false;

	BWAPI::Player self = the.self();

	if (_openingGroup == "anti-rush")
	{
		int numRax = the.my.all.count(BWAPI::UnitTypes::Terran_Barracks);

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
			// 1 medic for each 5 marines.
			int medicGoal = std::max(numMedics, numMarines / 5);
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
			int weaponsUps = self->getUpgradeLevel(BWAPI::UpgradeTypes::Terran_Infantry_Weapons);
			if (weaponsUps == 0 &&
				!self->isUpgrading(BWAPI::UpgradeTypes::Terran_Infantry_Weapons))
			{
				goal.push_back(std::pair<MacroAct, int>(BWAPI::UpgradeTypes::Terran_Infantry_Weapons, 1));
			}
			else if (weaponsUps > 0 &&
				self->getUpgradeLevel(BWAPI::UpgradeTypes::Terran_Infantry_Armor) == 0 &&
				!self->isUpgrading(BWAPI::UpgradeTypes::Terran_Infantry_Armor))
			{
				goal.push_back(std::pair<MacroAct, int>(BWAPI::UpgradeTypes::Terran_Infantry_Armor, 1));
			}
			else if (weaponsUps > 0 &&
				weaponsUps < 3 &&
				!self->isUpgrading(BWAPI::UpgradeTypes::Terran_Infantry_Weapons) &&
				numVessels > 0)
			{
goal.push_back(std::pair<MacroAct, int>(BWAPI::UpgradeTypes::Terran_Infantry_Weapons, weaponsUps + 1));
			}
		}

		// Add in tanks if they're useful.
		const int enemiesCounteredByTanks =
			the.your.seen.count(BWAPI::UnitTypes::Terran_Siege_Tank_Tank_Mode) +
			the.your.seen.count(BWAPI::UnitTypes::Terran_Siege_Tank_Siege_Mode) +
			the.your.seen.count(BWAPI::UnitTypes::Protoss_Dragoon) +
			the.your.seen.count(BWAPI::UnitTypes::Protoss_Reaver) +
			the.your.seen.count(BWAPI::UnitTypes::Zerg_Lurker) +
			the.your.seen.count(BWAPI::UnitTypes::Zerg_Ultralisk);
		const bool enemyHasStaticDefense =
			the.your.seen.count(BWAPI::UnitTypes::Terran_Bunker) > 0 ||
			the.your.seen.count(BWAPI::UnitTypes::Protoss_Photon_Cannon) > 0 ||
			the.your.seen.count(BWAPI::UnitTypes::Zerg_Sunken_Colony) > 0;
		if (enemiesCounteredByTanks > 0 || enemyHasStaticDefense)
		{
			int nTanksWanted;
			if (enemiesCounteredByTanks > 0)
			{
				nTanksWanted = std::min(numMarines / 4, enemiesCounteredByTanks);
				nTanksWanted = std::min(nTanksWanted, numTanks + 2);
			}
			else
			{
				nTanksWanted = numTanks;
				if (numTanks < 2)
				{
					nTanksWanted = numTanks + 1;
				}
			}
			goal.push_back(std::pair<MacroAct, int>(BWAPI::UnitTypes::Terran_Siege_Tank_Tank_Mode, nTanksWanted));
			goal.push_back(std::pair<MacroAct, int>(BWAPI::TechTypes::Tank_Siege_Mode, 1));
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
        ProductionManager::Instance().liftBuildings(BWAPI::UnitTypes::Terran_Barracks);
    }
	else if (_openingGroup == "tanks")
	{
		goal.push_back(std::pair<MacroAct, int>(BWAPI::UnitTypes::Terran_Vulture, numVultures + 4));
		goal.push_back(std::pair<MacroAct, int>(BWAPI::UnitTypes::Terran_Siege_Tank_Tank_Mode, numTanks + 2));
		goal.push_back(std::pair<MacroAct, int>(BWAPI::TechTypes::Tank_Siege_Mode, 1));

		if (numVultures > 0)
		{
			goal.push_back(std::pair<MacroAct, int>(BWAPI::UpgradeTypes::Ion_Thrusters, 1));
		}
		if (numTanks >= 6)
		{
			goal.push_back(std::pair<MacroAct, int>(BWAPI::UnitTypes::Terran_Goliath, numGoliaths + 4));
		}
		if (numGoliaths >= 4)
		{
			goal.push_back(std::pair<MacroAct, int>(BWAPI::UpgradeTypes::Charon_Boosters, 1));
		}
		if (self->hasResearched(BWAPI::TechTypes::Tank_Siege_Mode))
		{
			makeVessel = true;
		}
        ProductionManager::Instance().liftBuildings(BWAPI::UnitTypes::Terran_Barracks);
        if (hasEBay)
        {
            ProductionManager::Instance().liftBuildings(BWAPI::UnitTypes::Terran_Engineering_Bay);
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
		_openingGroup = "bio";       // we're misconfigured, but try to do something
	}

	if (numCC > 1 || InformationManager::Instance().enemyHasCloakTech())
	{
		goal.push_back(std::pair<MacroAct, int>(BWAPI::UnitTypes::Terran_Academy, 1));
		if (numRefineries == 0)
		{
			goal.push_back(std::pair<MacroAct, int>(BWAPI::UnitTypes::Terran_Refinery, 1));
		}
	}

	const int enemyAirToGround =
		the.your.seen.count(BWAPI::UnitTypes::Terran_Wraith) / 6 +
		the.your.seen.count(BWAPI::UnitTypes::Terran_Battlecruiser) / 2 +
		the.your.seen.count(BWAPI::UnitTypes::Protoss_Scout) / 3 +
		the.your.seen.count(BWAPI::UnitTypes::Zerg_Mutalisk) / 4;
	if (enemyAirToGround > 0)
	{
		goal.push_back(std::pair<MacroAct, int>(BWAPI::UnitTypes::Terran_Missile_Turret, enemyAirToGround));
	}

	if (numCC > 0 && hasAcademy)
	{
		goal.push_back(std::pair<MacroAct, int>(BWAPI::UnitTypes::Terran_Comsat_Station, the.my.completed.count(BWAPI::UnitTypes::Terran_Command_Center)));
	}

	if (makeVessel || InformationManager::Instance().enemyHasCloakTech())
	{
		// Maintain 1 vessel to spot for the ground squad and 1 to go with the recon squad.
		if (numVessels < 2)
		{
			goal.push_back(std::pair<MacroAct, int>(BWAPI::UnitTypes::Terran_Science_Vessel, numVessels + 1));
		}
	}

	if (hasArmory &&
		self->getUpgradeLevel(BWAPI::UpgradeTypes::Terran_Vehicle_Weapons) == 0 &&
		!self->isUpgrading(BWAPI::UpgradeTypes::Terran_Vehicle_Weapons))
	{
		goal.push_back(std::pair<MacroAct, int>(BWAPI::UpgradeTypes::Terran_Vehicle_Weapons, 1));
	}

	// Make more SCVs, up to a limit. The anti-rush strategy makes its own SCVs.
	if (_openingGroup != "anti-rush")
	{
		goal.push_back(MetaPair(BWAPI::UnitTypes::Terran_SCV, std::min(maxSCVs, numSCVs + 2 * numCC)));
	}

	// If the map has islands, get drop after we have 3 bases.
	if (Config::Macro::ExpandToIslands && numCC >= 3 && the.bases.hasIslandBases())
	{
		goal.push_back(MetaPair(BWAPI::UnitTypes::Terran_Dropship, 1));
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
	int nLairs = the.my.all.count(BWAPI::UnitTypes::Zerg_Lair);
	int nHives = the.my.all.count(BWAPI::UnitTypes::Zerg_Hive);
	int nHatches = the.my.all.count(BWAPI::UnitTypes::Zerg_Hatchery)
		+ nLairs + nHives;
	int nDrones = the.my.all.count(BWAPI::UnitTypes::Zerg_Drone);
	int nHydras = the.my.all.count(BWAPI::UnitTypes::Zerg_Hydralisk);

	const int droneMax = 48;             // number of drones not to exceed

	// Simple default strategy as an example in case you want to use this method.
	goal.push_back(std::pair<MacroAct, int>(BWAPI::UnitTypes::Zerg_Hydralisk, nHydras + 12));
	if (shouldExpandNow())
	{
		goal.push_back(std::pair<MacroAct, int>(BWAPI::UnitTypes::Zerg_Hatchery, nHatches + 1));
		goal.push_back(std::pair<MacroAct, int>(BWAPI::UnitTypes::Zerg_Drone, std::min(droneMax, nDrones + 10)));
	}
	else
	{
		goal.push_back(std::pair<MacroAct, int>(BWAPI::UnitTypes::Zerg_Drone, std::min(droneMax, nDrones + 2)));
	}

	return goal;
}

void StrategyManager::handleUrgentProductionIssues(BuildOrderQueue & queue)
{
	// This is the enemy plan that we have seen in action.
	OpeningPlan enemyPlan = OpponentModel::Instance().getEnemyPlan();

	// For all races, if we've just discovered that the enemy is going with a heavy macro opening,
	// drop any static defense that our opening build order told us to make.
	if (!ProductionManager::Instance().isOutOfBook() && !_openingStaticDefenseDropped)
	{
		// We're in the opening book and haven't dropped static defenses yet. Should we?
		if (enemyPlan == OpeningPlan::Turtle ||
			enemyPlan == OpeningPlan::SafeExpand)
			// enemyPlan == OpeningPlan::NakedExpand && _enemyRace != BWAPI::Races::Zerg) // could do this too
		{
			// 1. Remove upcoming defense buildings from the queue.
			queue.dropStaticDefenses();
			// 2. Cancel unfinished defense buildings.
			for (BWAPI::Unit unit : the.self()->getUnits())
			{
				if (UnitUtil::IsComingStaticDefense(unit->getType()) && unit->canCancelConstruction())
				{
					the.micro.Cancel(unit);
				}
			}
			// 3. Never do it again.
			_openingStaticDefenseDropped = true;
			if (Config::Debug::DrawQueueFixInfo)
			{
				BWAPI::Broodwar->printf("queue: any static defense dropped as unnecessary");
			}
		}
	}

	// All other considerations are handled separately by zerg.
	if (_selfRace == BWAPI::Races::Zerg)
	{
		StrategyBossZerg::Instance().handleUrgentProductionIssues(queue);
	}
	else
	{
		// Count resource depots.
		const BWAPI::UnitType resourceDepotType = _selfRace == BWAPI::Races::Terran
			? BWAPI::UnitTypes::Terran_Command_Center
			: BWAPI::UnitTypes::Protoss_Nexus;
        const int numDepots = the.my.all.count(resourceDepotType);

		// If we need to cope with an extreme emergency, don't do anything else.
		// If we have no resource depot, we can do nothing; that case is dealt with below.
		if (numDepots > 0 && handleExtremeEmergency(queue))
		{
			return;
		}

		// If there are no workers, many reactions can't happen.
		const bool anyWorkers =
			the.my.all.count(_selfRace == BWAPI::Races::Terran
			? BWAPI::UnitTypes::Terran_SCV
			: BWAPI::UnitTypes::Protoss_Probe) > 0;

		// detect if there's a supply block once per second
		if ((BWAPI::Broodwar->getFrameCount() % 24 == 1) && detectSupplyBlock(queue) && anyWorkers)
		{
			if (Config::Debug::DrawQueueFixInfo)
			{
				BWAPI::Broodwar->printf("queue: building supply");
			}

			queue.queueAsHighestPriority(MacroAct(the.self()->getRace().getSupplyProvider()));
		}

		const MacroAct * nextInQueuePtr = queue.isEmpty() ? nullptr : &(queue.getHighestPriorityItem().macroAct);

		// If we need gas, make sure it is turned on.
		int gas = the.self()->gas();
		if (nextInQueuePtr)
		{
			if (nextInQueuePtr->gasPrice() > gas)
			{
				WorkerManager::Instance().setCollectGas(true);
			}
		}

		// If we're protoss and building is stalled for lack of space,
		// schedule a pylon to make more space where buildings can be placed.
		if (BuildingManager::Instance().getStalledForLackOfSpace())
		{
			if (_selfRace == BWAPI::Races::Protoss && 
				(!nextInQueuePtr || !nextInQueuePtr->isBuilding() || nextInQueuePtr->getUnitType() != BWAPI::UnitTypes::Protoss_Pylon) &&
				!BuildingManager::Instance().isBeingBuilt(BWAPI::UnitTypes::Protoss_Pylon))
			{
				queue.queueAsHighestPriority(BWAPI::UnitTypes::Protoss_Pylon);
				return;				// and call it a day
			}
		}

		// If we have collected too much gas, turn it off.
		if (ProductionManager::Instance().isOutOfBook() &&
			gas > 400 &&
			gas > 4 * the.self()->minerals())
		{
			int queueMinerals, queueGas;
			queue.totalCosts(queueMinerals, queueGas);
			if (gas >= queueGas)
			{
				WorkerManager::Instance().setCollectGas(false);
			}
		}

		// If they have mobile cloaked units, get some static detection.
		if (InformationManager::Instance().enemyHasMobileCloakTech() && anyWorkers)
		{
			if (_selfRace == BWAPI::Races::Protoss)
			{
                if (the.my.all.count(BWAPI::UnitTypes::Protoss_Photon_Cannon) < 2 &&
					!queue.anyInQueue(BWAPI::UnitTypes::Protoss_Photon_Cannon) &&
					!BuildingManager::Instance().isBeingBuilt(BWAPI::UnitTypes::Protoss_Photon_Cannon))
				{
					queue.queueAsHighestPriority(MacroAct(BWAPI::UnitTypes::Protoss_Photon_Cannon));
					queue.queueAsHighestPriority(MacroAct(BWAPI::UnitTypes::Protoss_Photon_Cannon));

                    if (the.my.all.count(BWAPI::UnitTypes::Protoss_Forge) == 0 &&
						!BuildingManager::Instance().isBeingBuilt(BWAPI::UnitTypes::Protoss_Forge))
					{
						queue.queueAsHighestPriority(MacroAct(BWAPI::UnitTypes::Protoss_Forge));
					}
				}
			}
			else if (_selfRace == BWAPI::Races::Terran)
			{
                if (the.my.all.count(BWAPI::UnitTypes::Terran_Missile_Turret) < 3 &&
					!queue.anyInQueue(BWAPI::UnitTypes::Terran_Missile_Turret) &&
					!BuildingManager::Instance().isBeingBuilt(BWAPI::UnitTypes::Terran_Missile_Turret))
				{
					queue.queueAsHighestPriority(MacroAct(BWAPI::UnitTypes::Terran_Missile_Turret));
					queue.queueAsHighestPriority(MacroAct(BWAPI::UnitTypes::Terran_Missile_Turret));
					queue.queueAsHighestPriority(MacroAct(BWAPI::UnitTypes::Terran_Missile_Turret));

                    if (the.my.all.count(BWAPI::UnitTypes::Terran_Engineering_Bay) == 0 &&
						!BuildingManager::Instance().isBeingBuilt(BWAPI::UnitTypes::Terran_Engineering_Bay))
					{
						queue.queueAsHighestPriority(MacroAct(BWAPI::UnitTypes::Terran_Engineering_Bay));
					}
				}
			}
		}

		// This is the enemy plan that we have seen, or if none yet, the expected enemy plan.
		// Some checks can use the expected plan, some are better with the observed plan.
		OpeningPlan likelyEnemyPlan = OpponentModel::Instance().getBestGuessEnemyPlan();

		// If the opponent is rushing, make some defense.
		if (likelyEnemyPlan == OpeningPlan::Proxy ||
			likelyEnemyPlan == OpeningPlan::WorkerRush ||
			likelyEnemyPlan == OpeningPlan::FastRush)
			// enemyPlan == OpeningPlan::HeavyRush)           // we can react later to this
		{
			// If we are terran and have marines, make a bunker.
			if (_selfRace == BWAPI::Races::Terran)
			{
				if (!queue.anyInQueue(BWAPI::UnitTypes::Terran_Bunker) &&
                    the.my.all.count(BWAPI::UnitTypes::Terran_Marine) > 0 &&          // usefulness requirement
                    the.my.completed.count(BWAPI::UnitTypes::Terran_Barracks) > 0 &&  // tech requirement for a bunker
                    the.my.all.count(BWAPI::UnitTypes::Terran_Bunker) == 0 &&
					!BuildingManager::Instance().isBeingBuilt(BWAPI::UnitTypes::Terran_Bunker) &&
					anyWorkers)
				{
					queue.queueAsHighestPriority(MacroAct(BWAPI::UnitTypes::Terran_Bunker, MacroLocation::Front));
				}
			}

			// If we are protoss, make a shield battery.
			// NOTE This works, but is turned off because protoss can't use the battery yet.
			/*
			else if (_selfRace == BWAPI::Races::Protoss)
			{
			if (the.my.completed.count(BWAPI::UnitTypes::Protoss_Pylon) > 0 &&    // tech requirement
			the.my.completed.count(BWAPI::UnitTypes::Protoss_Gateway) > 0 &&  // tech requirement
			the.my.all.count(BWAPI::UnitTypes::Protoss_Shield_Battery) == 0 &&
			!queue.anyInQueue(BWAPI::UnitTypes::Protoss_Shield_Battery) &&
			!BuildingManager::Instance().isBeingBuilt(BWAPI::UnitTypes::Protoss_Shield_Battery) &&
			anyWorkers)
			{
			queue.queueAsHighestPriority(BWAPI::UnitTypes::Protoss_Shield_Battery);
			}
			}
			*/
		}

		if (numDepots > _highWaterBases)
		{
			_highWaterBases = numDepots;
		}
		bool makeResourceDepot = false;

		// If there is no resource depot, order one if we can afford it.
		// NOTE Does not check whether we have a worker to build it.
		if (numDepots == 0 && the.self()->minerals() >= 400)
		{
			makeResourceDepot = true;
		}

		// If the opponent fast expanded and we haven't taken the natural yet, do that immediately.
		// Not if the enemy is zerg, though. Zerg can be ahead in expansions.
		if (enemyPlan == OpeningPlan::SafeExpand || enemyPlan == OpeningPlan::NakedExpand)
		{
			// Use _highWaterBases instead of numDepots so we don't try to remake a destroyed natural.
			if (_highWaterBases == 1 && the.enemy()->getRace() != BWAPI::Races::Zerg)
			{
				makeResourceDepot = true;
			}
		}

		// We only care about the next item in the queue, not possible later resource depots in the queue.
		// This should be after other rules that may add something, so that no other emegency reaction
		// pushes down the resource depot in the queue. Otherwise the rule will fire repeatedly.
		if (makeResourceDepot &&
			anyWorkers &&
			(!nextInQueuePtr || !nextInQueuePtr->isUnit() || nextInQueuePtr->getUnitType() != resourceDepotType) &&
			!BuildingManager::Instance().isBeingBuilt(resourceDepotType))
		{
			queue.queueAsHighestPriority(MacroAct(resourceDepotType));
			return;    // and don't do anything else just yet
		}
	}
}

// Return true if we're supply blocked and should build supply.
// NOTE This understands zerg supply but is not used when we are zerg.
bool StrategyManager::detectSupplyBlock(BuildOrderQueue & queue) const
{
	// If the _queue is empty or supply is maxed, there is no block.
	if (queue.isEmpty() || the.self()->supplyTotal() >= 400)
	{
		return false;
	}

	// If supply is being built now, there's no block. Return right away.
	// Terran and protoss calculation:
	if (BuildingManager::Instance().isBeingBuilt(the.self()->getRace().getSupplyProvider()))
	{
		return false;
	}

	// Terran and protoss calculation:
	int supplyAvailable = the.self()->supplyTotal() - the.self()->supplyUsed();

	// Zerg calculation:
	// Zerg can create an overlord that doesn't count toward supply until the next check.
	// To work around it, add up the supply by hand, including hatcheries.
	if (the.self()->getRace() == BWAPI::Races::Zerg) {
		supplyAvailable = -the.self()->supplyUsed();
		for (auto unit : the.self()->getUnits())
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
		if (_selfRace == BWAPI::Races::Zerg
			&& BuildingManager::Instance().buildingsQueued().size() > 0
			&& the.self()->supplyTotal() <= 18)
		{
			return false;
		}
		return true;
	}

	return false;
}

// This tries to cope with 1 kind of severe emergency: We have desperately few workers.
// The caller promises that we have a resource depot, so we may be able to make more.
bool StrategyManager::handleExtremeEmergency(BuildOrderQueue & queue)
{
	const int minWorkers = 3;
	const BWAPI::UnitType workerType = _selfRace.getWorker();
	const int nWorkers = the.my.all.count(workerType);

	// NOTE This doesn't check whether the map has resources remaining!
	//      If not, we should produce workers only if needed for another purpose.
	// NOTE If we don't have enough minerals to make a worker, then we don't
	//      have enough minerals to make anything (since we're not zerg and can't make scourge).
	//      So don't bother.
	if (nWorkers < minWorkers && the.self()->minerals() >= 50)
	{
		// 1. If the next item in the queue is a worker, we're good. Otherwise, clear the queue.
		// This is a severe emergency and it doesn't make sense to continue business as usual.
		// But if we don't have enough 
		if (queue.size() > 0)
		{
			const MacroAct & act = queue.getHighestPriorityItem().macroAct;
			if (act.isUnit() && act.getUnitType() == workerType)
			{
				return false;
			}
			queue.clearAll();
		}
		// 2. Queue the minimum number of workers.
		for (int i = nWorkers; i < minWorkers; ++i)
		{
			queue.queueAsHighestPriority(workerType);
		}
		return true;
	}

	return false;
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
	for (BWAPI::Unit unit : the.self()->getUnits())
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

// Do we expect or plan to drop at some point during the game?
bool StrategyManager::dropIsPlanned() const
{
	// Don't drop in ZvZ.
	if (_selfRace == BWAPI::Races::Zerg && the.enemy()->getRace() == BWAPI::Races::Zerg)
	{
		return false;
	}

	// Otherwise plan drop if the opening says so, or if the map has islands to take.
	return
        getOpeningGroup() == "drop" ||
		Config::Macro::ExpandToIslands && the.bases.hasIslandBases();
}

// Whether we have the tech and transport to drop.
bool StrategyManager::hasDropTech()
{
	if (_selfRace == BWAPI::Races::Zerg)
	{
		// NOTE May be slow drop.
		return
            the.self()->getUpgradeLevel(BWAPI::UpgradeTypes::Ventral_Sacs) > 0 &&
			the.my.completed.count(BWAPI::UnitTypes::Zerg_Overlord) > 0;
	}
	if (_selfRace == BWAPI::Races::Protoss)
	{
		return the.my.completed.count(BWAPI::UnitTypes::Protoss_Shuttle) > 0;
	}
	if (_selfRace == BWAPI::Races::Terran)
	{
		return the.my.completed.count(BWAPI::UnitTypes::Terran_Dropship) > 0;
	}

	return false;
}