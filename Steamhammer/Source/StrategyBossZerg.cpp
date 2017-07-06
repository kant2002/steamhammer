#include "StrategyBossZerg.h"

#include "InformationManager.h"
#include "ProductionManager.h"
#include "ScoutManager.h"
#include "UnitUtil.h"

using namespace UAlbertaBot;

StrategyBossZerg::StrategyBossZerg()
	: _self(BWAPI::Broodwar->self())
	, _enemy(BWAPI::Broodwar->enemy())
	, _enemyRace(_enemy->getRace())
	, _techTarget(TechTarget::None)
	, _extraDronesWanted(0)
	, _latestBuildOrder(BWAPI::Races::Zerg)
	, _emergencyGroundDefense(false)
	, _emergencyStartFrame(-1)
	, _existingSupply(-1)
	, _pendingSupply(-1)
	, _lastUpdateFrame(-1)
{
	setUnitMix(BWAPI::UnitTypes::Zerg_Drone, BWAPI::UnitTypes::None);
	chooseEconomyRatio();
}

// -- -- -- -- -- -- -- -- -- -- --
// Private methods.

// Calculate supply existing, pending, and used.
// FOr pending supply, we need to know about overlords just hatching.
// For supply used, the BWAPI self->supplyUsed() can be slightly wrong,
// especially when a unit is just started or just died. 
void StrategyBossZerg::updateSupply()
{
	int existingSupply = 0;
	int pendingSupply = 0;
	int supplyUsed = 0;

	for (auto & unit : _self->getUnits())
	{
		if (unit->getType() == BWAPI::UnitTypes::Zerg_Overlord)
		{
			if (unit->getOrder() == BWAPI::Orders::ZergBirth)
			{
				// Overlord is just hatching and doesn't provide supply yet.
				pendingSupply += 16;
			}
			else
			{
				existingSupply += 16;
			}
		}
		else if (unit->getType() == BWAPI::UnitTypes::Zerg_Egg)
		{
			if (unit->getBuildType() == BWAPI::UnitTypes::Zerg_Overlord) {
				pendingSupply += 16;
			}
			else if (unit->getBuildType().isTwoUnitsInOneEgg())
			{
				supplyUsed += 2 * unit->getBuildType().supplyRequired();
			}
			else
			{
				supplyUsed += unit->getBuildType().supplyRequired();
			}
		}
		else if (unit->getType() == BWAPI::UnitTypes::Zerg_Hatchery && !unit->isCompleted())
		{
			// Don't count this. Hatcheries build too slowly and provide too little.
			// pendingSupply += 2;
		}
		else if (unit->getType().isResourceDepot())
		{
			// Only counts complete hatcheries because incomplete hatcheries are checked above.
			// Also counts lairs and hives whether complete or not, of course.
			existingSupply += 2;
		}
		else
		{
			supplyUsed += unit->getType().supplyRequired();
		}
	}

	_existingSupply = std::min(existingSupply, absoluteMaxSupply);
	_pendingSupply = pendingSupply;
	_supplyUsed = supplyUsed;

	// Note: _existingSupply is less than _self->supplyTotal() when an overlord
	// has just died. In other words, it recognizes the lost overlord sooner,
	// which is better for planning.

	//if (_self->supplyUsed() != _supplyUsed)
	//{
	//	BWAPI::Broodwar->printf("official supply used /= measured supply used %d /= %d", _self->supplyUsed(), supplyUsed);
	//}
}

// Called once per frame, possibly more.
// Includes screen drawing calls.
void StrategyBossZerg::updateGameState()
{
	if (_lastUpdateFrame == BWAPI::Broodwar->getFrameCount())
	{
		// No need to update more than once per frame.
		return;
	}
	_lastUpdateFrame = BWAPI::Broodwar->getFrameCount();

	if (_emergencyGroundDefense && _lastUpdateFrame >= _emergencyStartFrame + (20 * 24))
	{
		// Danger has been past for long enough. Declare the end of the emergency.
		_emergencyGroundDefense = false;
	}

	minerals = std::max(0, _self->minerals() - BuildingManager::Instance().getReservedMinerals());
	gas = std::max(0, _self->gas() - BuildingManager::Instance().getReservedGas());

	// Unit stuff, including uncompleted units.
	nLairs = UnitUtil::GetAllUnitCount(BWAPI::UnitTypes::Zerg_Lair);
	nHives = UnitUtil::GetAllUnitCount(BWAPI::UnitTypes::Zerg_Hive);
	nHatches = UnitUtil::GetAllUnitCount(BWAPI::UnitTypes::Zerg_Hatchery)
		+ nLairs + nHives;
	nCompletedHatches = UnitUtil::GetCompletedUnitCount(BWAPI::UnitTypes::Zerg_Hatchery)
		+ nLairs + nHives;
	nSpores = UnitUtil::GetAllUnitCount(BWAPI::UnitTypes::Zerg_Spore_Colony);

	// nGas = number of geysers ready to mine (extractor must be complete)
	// nFreeGas = number of geysers free to be taken (no extractor, even uncompleted)
	nGas = InformationManager::Instance().getMyNumRefineries();
	nFreeGas = InformationManager::Instance().getMyNumGeysers() -
		UnitUtil::GetAllUnitCount(BWAPI::UnitTypes::Zerg_Extractor);

	// TODO this assert can fail: 1. bug in getMyNumRefineries 2. extractor not at a base (maybe the base died)
	// UAB_ASSERT(nFreeGas >= 0, "negative free geysers");
	// Therefore we force nFreeGas into a valid range, as a temporary workaround.
	if (nFreeGas < 0)
	{
		nFreeGas = 0;
	}

	nDrones = UnitUtil::GetAllUnitCount(BWAPI::UnitTypes::Zerg_Drone);
	nMineralDrones = WorkerManager::Instance().getNumMineralWorkers();
	nGasDrones = WorkerManager::Instance().getNumGasWorkers();
	nLarvas = UnitUtil::GetAllUnitCount(BWAPI::UnitTypes::Zerg_Larva);

	nLings = UnitUtil::GetAllUnitCount(BWAPI::UnitTypes::Zerg_Zergling);
	nHydras = UnitUtil::GetAllUnitCount(BWAPI::UnitTypes::Zerg_Hydralisk);
	nMutas = UnitUtil::GetAllUnitCount(BWAPI::UnitTypes::Zerg_Mutalisk);

	// Tech stuff. It has to be completed for the tech to be available.
	nEvo = UnitUtil::GetCompletedUnitCount(BWAPI::UnitTypes::Zerg_Evolution_Chamber);
	hasPool = UnitUtil::GetCompletedUnitCount(BWAPI::UnitTypes::Zerg_Spawning_Pool) > 0;
	hasDen = UnitUtil::GetCompletedUnitCount(BWAPI::UnitTypes::Zerg_Hydralisk_Den) > 0;
	hasHydraSpeed = _self->getUpgradeLevel(BWAPI::UpgradeTypes::Muscular_Augments) != 0;
	hasSpire = UnitUtil::GetCompletedUnitCount(BWAPI::UnitTypes::Zerg_Spire) > 0 ||
		UnitUtil::GetAllUnitCount(BWAPI::UnitTypes::Zerg_Greater_Spire) > 0;
	hasQueensNest = UnitUtil::GetCompletedUnitCount(BWAPI::UnitTypes::Zerg_Queens_Nest) > 0;
	hasUltra = UnitUtil::GetCompletedUnitCount(BWAPI::UnitTypes::Zerg_Ultralisk_Cavern) > 0;
	// Enough upgrades that it is worth making ultras: Speed done, armor underway.
	hasUltraUps = _self->getUpgradeLevel(BWAPI::UpgradeTypes::Anabolic_Synthesis) != 0 &&
		(_self->getUpgradeLevel(BWAPI::UpgradeTypes::Chitinous_Plating) != 0 ||
		_self->isUpgrading(BWAPI::UpgradeTypes::Chitinous_Plating));

	// hasLair means "can research stuff in the lair", like overlord speed.
	// hasLairTech means "can do stuff that needs lair", like research lurker aspect.
	// NOTE The two are different in game, but even more different in the bot because
	//      of a BWAPI 4.1.2 bug: You can't do lair research in a hive.
	//      This code reflects the bug so we can work around it as much as possible.
	hasHiveTech = UnitUtil::GetCompletedUnitCount(BWAPI::UnitTypes::Zerg_Hive) > 0;
	hasLair = UnitUtil::GetCompletedUnitCount(BWAPI::UnitTypes::Zerg_Lair) > 0;
	hasLairTech = hasLair || nHives > 0;
	
	outOfBook = ProductionManager::Instance().isOutOfBook();
	nBases = InformationManager::Instance().getNumBases(_self);
	nFreeBases = InformationManager::Instance().getNumFreeLandBases();
	nMineralPatches = InformationManager::Instance().getMyNumMineralPatches();
	maxDrones = WorkerManager::Instance().getMaxWorkers();

	updateSupply();

	drawStrategyBossInformation();
}

// How many of our eggs will hatch into the given unit type?
// This does not adjust for zerglings or scourge, which are 2 to an egg.
int StrategyBossZerg::numInEgg(BWAPI::UnitType type) const
{
	int count = 0;

	for (auto & unit : _self->getUnits())
	{
		if (unit->getType() == BWAPI::UnitTypes::Zerg_Egg && unit->getBuildType() == type)
		{
			++count;
		}
	}

	return count;
}

// Return true if the building is in the building queue with any status.
bool StrategyBossZerg::isBeingBuilt(const BWAPI::UnitType unitType) const
{
	UAB_ASSERT(unitType.isBuilding(), "not a building");
	return BuildingManager::Instance().isBeingBuilt(unitType);
}

// Severe emergency: We are out of drones and/or hatcheries.
// Cancel items to release their resources.
// TODO pay attention to priority: the least essential first
// TODO cancel research
void StrategyBossZerg::cancelStuff(int mineralsNeeded)
{
	int mineralsSoFar = _self->minerals();

	for (BWAPI::Unit u : _self->getUnits())
	{
		if (mineralsSoFar >= mineralsNeeded)
		{
			return;
		}
		if (u->getType() == BWAPI::UnitTypes::Zerg_Egg && u->getBuildType() == BWAPI::UnitTypes::Zerg_Overlord)
		{
			if (_self->supplyTotal() - _supplyUsed >= 6)  // enough to add 3 drones
			{
				mineralsSoFar += 100;
				u->cancelMorph();
			}
		}
		else if (u->getType() == BWAPI::UnitTypes::Zerg_Egg && u->getBuildType() != BWAPI::UnitTypes::Zerg_Drone ||
			u->getType() == BWAPI::UnitTypes::Zerg_Lair && !u->isCompleted() ||
			u->getType() == BWAPI::UnitTypes::Zerg_Creep_Colony && !u->isCompleted() ||
			u->getType() == BWAPI::UnitTypes::Zerg_Evolution_Chamber && !u->isCompleted() ||
			u->getType() == BWAPI::UnitTypes::Zerg_Hydralisk_Den && !u->isCompleted() ||
			u->getType() == BWAPI::UnitTypes::Zerg_Queens_Nest && !u->isCompleted() ||
			u->getType() == BWAPI::UnitTypes::Zerg_Hatchery && !u->isCompleted() && nHatches > 1)
		{
			mineralsSoFar += u->getType().mineralPrice();
			u->cancelMorph();
		}
	}
}

// The next item in the queue is useless and can be dropped.
// Top goal: Do not freeze the production queue by asking the impossible.
// But also try to reduce wasted production.
// NOTE Useless stuff is not always removed before it is built.
//      The order of events is: this check -> queue filling -> production.
bool StrategyBossZerg::nextInQueueIsUseless(BuildOrderQueue & queue) const
{
	if (queue.isEmpty())
	{
		return false;
	}

	const MacroAct act = queue.getHighestPriorityItem().macroAct;

	// It costs gas that we don't have and won't get.
	if (nGas == 0 && act.gasPrice() > gas && !isBeingBuilt(BWAPI::UnitTypes::Zerg_Extractor))
	{
		return true;
	}

	if (act.isUpgrade())
	{
		const BWAPI::UpgradeType upInQueue = act.getUpgradeType();

		// Already have it or already getting it (due to a race condition).
		if (_self->getUpgradeLevel(upInQueue) == (upInQueue).maxRepeats() || _self->isUpgrading(upInQueue))
		{
			return true;
		}

		// Lost the building for it in the meantime.
		if (upInQueue == BWAPI::UpgradeTypes::Anabolic_Synthesis || upInQueue == BWAPI::UpgradeTypes::Chitinous_Plating)
		{
			return !hasUltra;
		}

		if (upInQueue == BWAPI::UpgradeTypes::Pneumatized_Carapace)
		{
			return !hasLair;
		}

		if (upInQueue == BWAPI::UpgradeTypes::Muscular_Augments || upInQueue == BWAPI::UpgradeTypes::Grooved_Spines)
		{
			return !hasDen && UnitUtil::GetAllUnitCount(BWAPI::UnitTypes::Zerg_Hydralisk_Den) == 0;
		}

		if (upInQueue == BWAPI::UpgradeTypes::Metabolic_Boost)
		{
			return !hasPool && UnitUtil::GetAllUnitCount(BWAPI::UnitTypes::Zerg_Spawning_Pool) == 0;
		}

		if (upInQueue == BWAPI::UpgradeTypes::Adrenal_Glands)
		{
			return !hasPool || !hasHiveTech;
		}

		// Coordinate these two with the single/double upgrading plan.
		if (upInQueue == BWAPI::UpgradeTypes::Zerg_Carapace)
		{
			return nEvo == 0;
		}
		if (upInQueue == BWAPI::UpgradeTypes::Zerg_Melee_Attacks)
		{
			return nEvo < 2;     // disallows getting melee attack after finishing carapace
		}

		return false;
	}

	/* Not yet needed.
	if (act.isTech())
	{
		const BWAPI::TechType techInQueue = act.getTechType();

		return false;
	}
	*/

	// After that, we only care about units.
	if (!act.isUnit())
	{
		return false;
	}

	const BWAPI::UnitType nextInQueue = act.getUnitType();

	if (nextInQueue == BWAPI::UnitTypes::Zerg_Overlord)
	{
		// We don't need overlords now. Opening book sometimes deliberately includes extras.
		// This is coordinated with makeOverlords() but skips less important steps.
		if (outOfBook)
		{
			int totalSupply = _existingSupply + _pendingSupply;
			int supplyExcess = totalSupply - _supplyUsed;
			return totalSupply >= absoluteMaxSupply ||
				totalSupply > 32 && supplyExcess >= totalSupply / 8 + 16;
		}
	}
	if (nextInQueue == BWAPI::UnitTypes::Zerg_Drone)
	{
		// We are planning more than the maximum reasonable number of drones.
		// nDrones can go slightly over maxDrones when queue filling adds drones.
		// It can also go over when maxDrones decreases (bases lost, minerals mined out).
		return nDrones >= maxDrones;
	}
	if (nextInQueue == BWAPI::UnitTypes::Zerg_Zergling)
	{
		// We lost the tech.
		return !hasPool &&
			UnitUtil::GetAllUnitCount(BWAPI::UnitTypes::Zerg_Spawning_Pool) == 0 &&
			!isBeingBuilt(BWAPI::UnitTypes::Zerg_Spawning_Pool);
	}
	if (nextInQueue == BWAPI::UnitTypes::Zerg_Hydralisk)
	{
		// We lost the tech.
		return !hasPool &&
			UnitUtil::GetAllUnitCount(BWAPI::UnitTypes::Zerg_Spawning_Pool) == 0 &&
			!isBeingBuilt(BWAPI::UnitTypes::Zerg_Spawning_Pool);
	}
	if (nextInQueue == BWAPI::UnitTypes::Zerg_Mutalisk || nextInQueue == BWAPI::UnitTypes::Zerg_Scourge)
	{
		// We lost the tech. We currently do not ever make a greater spire.
		return !hasSpire &&
			UnitUtil::GetAllUnitCount(BWAPI::UnitTypes::Zerg_Spire) == 0 &&
			!isBeingBuilt(BWAPI::UnitTypes::Zerg_Spire);
	}
	if (nextInQueue == BWAPI::UnitTypes::Zerg_Ultralisk)
	{
		// We lost the tech.
		return UnitUtil::GetAllUnitCount(BWAPI::UnitTypes::Zerg_Ultralisk_Cavern) == 0;
	}

	if (nextInQueue == BWAPI::UnitTypes::Zerg_Hatchery)
	{
		// We're planning a hatchery but no longer have the drones to support it.
		// 3 drones/hatchery is the minimum: It can support ling production.
		// Also, it may still be OK if we have lots of minerals to spend.
		return nDrones < 3 * nHatches &&
			minerals < 50 + 300 * nCompletedHatches &&
			nCompletedHatches > 0;
	}
	if (nextInQueue == BWAPI::UnitTypes::Zerg_Lair)
	{
		return !hasPool && UnitUtil::GetAllUnitCount(BWAPI::UnitTypes::Zerg_Spawning_Pool) == 0 && !isBeingBuilt(BWAPI::UnitTypes::Zerg_Spawning_Pool) ||
			UnitUtil::GetAllUnitCount(BWAPI::UnitTypes::Zerg_Hatchery) == 0 && !isBeingBuilt(BWAPI::UnitTypes::Zerg_Hatchery);
	}
	if (nextInQueue == BWAPI::UnitTypes::Zerg_Hive)
	{
		return UnitUtil::GetAllUnitCount(BWAPI::UnitTypes::Zerg_Queens_Nest) == 0 && !isBeingBuilt(BWAPI::UnitTypes::Zerg_Queens_Nest) ||
			UnitUtil::GetAllUnitCount(BWAPI::UnitTypes::Zerg_Lair) == 0 ||
			_self->isUpgrading(BWAPI::UpgradeTypes::Pneumatized_Carapace) ||
			_self->isUpgrading(BWAPI::UpgradeTypes::Ventral_Sacs);
	}
	if (nextInQueue == BWAPI::UnitTypes::Zerg_Sunken_Colony)
	{
		return UnitUtil::GetAllUnitCount(BWAPI::UnitTypes::Zerg_Spawning_Pool) == 0 && !isBeingBuilt(BWAPI::UnitTypes::Zerg_Spawning_Pool) ||
			UnitUtil::GetAllUnitCount(BWAPI::UnitTypes::Zerg_Creep_Colony) == 0 && !isBeingBuilt(BWAPI::UnitTypes::Zerg_Creep_Colony);
	}
	if (nextInQueue == BWAPI::UnitTypes::Zerg_Spore_Colony)
	{
		return UnitUtil::GetAllUnitCount(BWAPI::UnitTypes::Zerg_Evolution_Chamber) == 0 && !isBeingBuilt(BWAPI::UnitTypes::Zerg_Evolution_Chamber) ||
			UnitUtil::GetAllUnitCount(BWAPI::UnitTypes::Zerg_Creep_Colony) == 0 && !isBeingBuilt(BWAPI::UnitTypes::Zerg_Creep_Colony);
	}
	if (nextInQueue == BWAPI::UnitTypes::Zerg_Spire)
	{
		return nLairs + nHives == 0;
	}
	//if (nextInQueue == BWAPI::UnitTypes::Zerg_Greater_Spire)
	//{
	//	return nHives == 0 ||
	//		UnitUtil::GetAllUnitCount(BWAPI::UnitTypes::Zerg_Spire) == 0 && !isBeingBuilt(BWAPI::UnitTypes::Zerg_Spire);
	//}
	//if (nextInQueue == BWAPI::UnitTypes::Zerg_Guardian || nextInQueue == BWAPI::UnitTypes::Zerg_Devourer)
	//{
	//	return nMutas == 0 ||
	//		UnitUtil::GetAllUnitCount(BWAPI::UnitTypes::Zerg_Greater_Spire) == 0 &&
	//		!isBeingBuilt(BWAPI::UnitTypes::Zerg_Greater_Spire);
	//}
	if (nextInQueue == BWAPI::UnitTypes::Zerg_Hydralisk_Den)
	{
		return !hasPool &&
			UnitUtil::GetAllUnitCount(BWAPI::UnitTypes::Zerg_Spawning_Pool) == 0 && !isBeingBuilt(BWAPI::UnitTypes::Zerg_Spawning_Pool);
	}
	if (nextInQueue == BWAPI::UnitTypes::Zerg_Extractor)
	{
		return nFreeGas == 0;
	}
	if (nextInQueue == BWAPI::UnitTypes::Zerg_Lurker)
	{
		return nHydras == 0 ||
			!_self->hasResearched(BWAPI::TechTypes::Lurker_Aspect) && !_self->isResearching(BWAPI::TechTypes::Lurker_Aspect);
	}

	return false;
}

void StrategyBossZerg::produce(const MacroAct & act)
{
	_latestBuildOrder.add(act);
	if (act.isUnit())
	{
		++_economyTotal;
		if (act.getUnitType() == BWAPI::UnitTypes::Zerg_Drone)
		{
			++_economyDrones;
		}
	}
}

// Make a drone instead of a combat unit with this larva?
bool StrategyBossZerg::needDroneNext()
{
	return !_emergencyGroundDefense &&
		nDrones < maxDrones &&
		double(_economyDrones) / double(1 + _economyTotal) < _economyRatio;
}

// We need overlords.
// Do this last so that nothing gets pushed in front of the overlords.
// NOTE: If you change this, coordinate the change with nextInQueueIsUseless(),
// which has a feature to recognize unneeded overlords (e.g. after big army losses).
void StrategyBossZerg::makeOverlords(BuildOrderQueue & queue)
{
	BWAPI::UnitType nextInQueue = queue.getNextUnit();

	// If an overlord is next up anyway, we have nothing to do.
	if (nextInQueue == BWAPI::UnitTypes::Zerg_Overlord)
	{
		return;
	}

	int totalSupply = std::min(_existingSupply + _pendingSupply, absoluteMaxSupply);
	if (totalSupply < absoluteMaxSupply)
	{
		int supplyExcess = totalSupply - _supplyUsed;
		// Adjust the number to account for the next queue item and pending buildings.
		if (nextInQueue != BWAPI::UnitTypes::None)
		{
			if (nextInQueue.isBuilding())
			{
				if (!UnitUtil::IsMorphedBuildingType(nextInQueue))
				{
					supplyExcess += 2;   // for the drone that will be used
				}
			}
			else
			{
				supplyExcess -= nextInQueue.supplyRequired();
			}
		}
		// The number of drones set to be used up making buildings.
		supplyExcess += 2 * BuildingManager::Instance().buildingsQueued().size();

		// If we're behind, catch up.
		for (; supplyExcess < 0; supplyExcess += 16)
		{
			queue.queueAsHighestPriority(BWAPI::UnitTypes::Zerg_Overlord);
		}
		// If we're only a little ahead, stay ahead depending on the supply.
		// This is a crude calculation. It seems not too far off.
		if (totalSupply > 20 && supplyExcess <= 0)                       // > overlord + 2 hatcheries
		{
			queue.queueAsHighestPriority(BWAPI::UnitTypes::Zerg_Overlord);
		}
		else if (totalSupply > 32 && supplyExcess <= totalSupply / 8 - 2)    // >= 2 overlords + 1 hatchery
		{
			queue.queueAsHighestPriority(BWAPI::UnitTypes::Zerg_Overlord);
		}
	}
}

// If necessary, take an emergency action and return true.
// Otherwise return false.
bool StrategyBossZerg::takeUrgentAction(BuildOrderQueue & queue)
{
	// Find the next thing remaining in the queue, but only if it is a unit.
	const BWAPI::UnitType nextInQueue = queue.getNextUnit();

	// There are no drones.
	// NOTE maxDrones is never zero.
	if (nDrones == 0)
	{
		WorkerManager::Instance().setCollectGas(false);
		BuildingManager::Instance().cancelQueuedBuildings();
		if (nHatches == 0)
		{
			// No hatcheries either. Queue drones for a hatchery and mining.
			ProductionManager::Instance().goOutOfBook();
			queue.queueAsLowestPriority(MacroAct(BWAPI::UnitTypes::Zerg_Drone));
			queue.queueAsLowestPriority(MacroAct(BWAPI::UnitTypes::Zerg_Drone));
			queue.queueAsLowestPriority(MacroAct(BWAPI::UnitTypes::Zerg_Hatchery));
			cancelStuff(400);
		}
		else
		{
			if (nextInQueue != BWAPI::UnitTypes::Zerg_Drone && numInEgg(BWAPI::UnitTypes::Zerg_Drone) == 0)
			{
				// Queue one drone to mine minerals.
				ProductionManager::Instance().goOutOfBook();
				queue.queueAsLowestPriority(MacroAct(BWAPI::UnitTypes::Zerg_Drone));
				cancelStuff(50);
			}
			BuildingManager::Instance().cancelBuildingType(BWAPI::UnitTypes::Zerg_Hatchery);
		}
		return true;
	}

	// There are no hatcheries.
	if (nHatches == 0 &&
		nextInQueue != BWAPI::UnitTypes::Zerg_Hatchery &&
		!isBeingBuilt(BWAPI::UnitTypes::Zerg_Hatchery))
	{
		ProductionManager::Instance().goOutOfBook();
		queue.queueAsLowestPriority(MacroAct(BWAPI::UnitTypes::Zerg_Hatchery));
		if (nDrones == 1)
		{
			ScoutManager::Instance().releaseWorkerScout();
			queue.queueAsHighestPriority(MacroAct(BWAPI::UnitTypes::Zerg_Drone));
			cancelStuff(350);
		}
		else {
			cancelStuff(300);
		}
		return true;
	}

	// There are < 3 drones. Make up to 3.
	// Making more than 3 breaks 4 pool openings.
	if (nDrones < 3 &&
		nextInQueue != BWAPI::UnitTypes::Zerg_Drone &&
		nextInQueue != BWAPI::UnitTypes::Zerg_Overlord)
	{
		ScoutManager::Instance().releaseWorkerScout();
		queue.queueAsHighestPriority(MacroAct(BWAPI::UnitTypes::Zerg_Drone));
		if (nDrones < 2)
		{
			queue.queueAsHighestPriority(MacroAct(BWAPI::UnitTypes::Zerg_Drone));
		}
		// Don't cancel other stuff. A drone should be mining, it's not that big an emergency.
		return true;
	}

	// There are no drones on minerals. Turn off gas collection.
	// TODO we may want to switch between gas and minerals to make tech units
	// TODO more efficient test in WorkerMan
	if (_lastUpdateFrame >= 24 &&           // give it time!
		WorkerManager::Instance().isCollectingGas() &&
		nMineralPatches > 0 &&
		WorkerManager::Instance().getNumMineralWorkers() == 0 &&
		WorkerManager::Instance().getNumReturnCargoWorkers() == 0 &&
		WorkerManager::Instance().getNumCombatWorkers() == 0 &&
		WorkerManager::Instance().getNumIdleWorkers() == 0)
	{
		// Leave the queue in place.
		ScoutManager::Instance().releaseWorkerScout();
		WorkerManager::Instance().setCollectGas(false);
		if (nHatches >= 2)
		{
			BuildingManager::Instance().cancelBuildingType(BWAPI::UnitTypes::Zerg_Hatchery);
		}
		return true;
	}

	return false;
}

// React to lesser emergencies.
void StrategyBossZerg::makeUrgentReaction(BuildOrderQueue & queue)
{
	// Find the next thing remaining in the queue, but only if it is a unit.
	const BWAPI::UnitType nextInQueue = queue.getNextUnit();

	// Enemy has air. Make scourge if possible.
	if (hasSpire && nGas > 0 &&
		InformationManager::Instance().enemyHasAirTech() &&
		nextInQueue != BWAPI::UnitTypes::Zerg_Scourge)
	{
		int totalScourge = UnitUtil::GetAllUnitCount(BWAPI::UnitTypes::Zerg_Scourge) +
			2 * numInEgg(BWAPI::UnitTypes::Zerg_Scourge) +
			2 * queue.numInQueue(BWAPI::UnitTypes::Zerg_Scourge);

		// Not too much, and not too much at once. They cost a lot of gas.
		int nScourgeNeeded = std::min(18, InformationManager::Instance().nScourgeNeeded());
		int nToMake = 0;
		if (nScourgeNeeded > totalScourge && nLarvas > 0)
		{
			int nPairs = std::min(1 + gas / 75, (nScourgeNeeded - totalScourge + 1) / 2);
			int limit = 3;          // how many pairs at a time, max?
			if (nLarvas > 6 && gas > 6 * 75)
			{
				// Allow more if we have plenty of resources.
				limit = 6;
			}
			nToMake = std::min(nPairs, limit);
		}
		else if (totalScourge == 0)
		{
			// We may have seen air tech but no air units.
			// Wait for now. Sometimes it's good to get scourge early, sometimes not.
			nToMake = 0;
		}
		for (int i = 0; i < nToMake; ++i)
		{
			queue.queueAsHighestPriority(BWAPI::UnitTypes::Zerg_Scourge);
		}
		// And keep going.
	}

	int queueMinerals, queueGas;
	queue.totalCosts(queueMinerals, queueGas);

	// We have too much gas. Turn off gas collection.
	// Opening book sometimes collects extra gas on purpose.
	// This ties in via ELSE with the next check!
	if (outOfBook &&
		WorkerManager::Instance().isCollectingGas() &&
		gas >= queueGas &&
		((minerals < 100 && gas > 400) || (minerals >= 100 && gas > 3 * minerals)))
	{
		WorkerManager::Instance().setCollectGas(false);
		// And keep going.
	}

	// We're in book and should have enough gas but it's off. Something went wrong.
	// Note ELSE!
	else if (!outOfBook && queue.getNextGasCost(1) > gas &&
		!WorkerManager::Instance().isCollectingGas())
	{
		if (nGas == 0 || nDrones < 9)
		{
			// Emergency. Give up and clear the queue.
			ProductionManager::Instance().goOutOfBook();
			return;
		}
		// Not such an emergency. Turn gas on and keep going.
		WorkerManager::Instance().setCollectGas(true);
	}

	// Another cause of production freezes: nGas can be wrong after a hatchery is destroyed.
	// Note ELSE!
	else if (outOfBook && queue.getNextGasCost(1) > gas && nGas > 0 && nGasDrones == 0 &&
		WorkerManager::Instance().isCollectingGas())
	{
		// Deadlock. Can't get gas. Give up and clear the queue.
		ProductionManager::Instance().goOutOfBook();
		return;
	}

	// Gas is turned off, and upcoming items cost more gas than we have. Get gas.
	// NOTE isCollectingGas() can return false when gas is in the process of being turned off,
	// and some will still be collected.
	// Note ELSE!
	else if (outOfBook && queue.getNextGasCost(4) > gas && !WorkerManager::Instance().isCollectingGas())
	{
		if (nGas > 0 && nDrones > 3 * nGas)
		{
			// Leave it to the regular queue refill to add more extractors.
			WorkerManager::Instance().setCollectGas(true);
		}
		// TODO what if nGas > 0 but not enough drones?
		else
		{
			// Well, we can't collect gas.
			// Make enough drones to get an extractor.
			ScoutManager::Instance().releaseWorkerScout();   // don't throw off the drone count
			if (nDrones >= 5 && nFreeGas > 0 &&
				nextInQueue != BWAPI::UnitTypes::Zerg_Extractor &&
				!isBeingBuilt(BWAPI::UnitTypes::Zerg_Extractor))
			{
				queue.queueAsHighestPriority(MacroAct(BWAPI::UnitTypes::Zerg_Extractor));
			}
			else if (nDrones >= 4 && isBeingBuilt(BWAPI::UnitTypes::Zerg_Extractor))
			{
				// We have an unfinished extractor. Wait for it to finish.
				// Need 4 drones so that 1 can keep mining minerals (or the rules will loop).
				WorkerManager::Instance().setCollectGas(true);
			}
			else if (nextInQueue != BWAPI::UnitTypes::Zerg_Drone && nFreeGas > 0)
			{
				queue.queueAsHighestPriority(BWAPI::UnitTypes::Zerg_Drone);
			}
		}
		// And keep going.
	}

	// We're in book and want to make zerglings next, but we also want extra drones.
	// Change the zerglings to a drone, since they have the same cost.
	// When we want extra drones, _economyDrones is decreased, so we recognize that by negative values.
	// Don't make all the extra drones in book, save a couple for later, because it could mess stuff up.
	if (!outOfBook && _economyDrones < -2 && nextInQueue == BWAPI::UnitTypes::Zerg_Zergling)
	{
		queue.removeHighestPriorityItem();
		queue.queueAsHighestPriority(BWAPI::UnitTypes::Zerg_Drone);
		++_economyDrones;
		// And keep going.
	}

	// We need a macro hatchery.
	// Division of labor: Macro hatcheries are here, expansions are regular production.
	// However, some macro hatcheries may be placed at expansions (it helps assert map control).
	// Macro hatcheries are automatic only out of book. Book openings must take care of themselves.
	if (outOfBook && minerals >= 300 && nLarvas == 0 && nHatches < 15 && nDrones > 9 &&
		!isBeingBuilt(BWAPI::UnitTypes::Zerg_Hatchery) &&
		!queue.anyInQueue(BWAPI::UnitTypes::Zerg_Hatchery))
	{
		MacroLocation loc = MacroLocation::Macro;
		if (nHatches % 2 != 0 && nFreeBases > 4)
		{
			// Expand with some macro hatcheries unless it's late game.
			loc = MacroLocation::MinOnly;
		}
		queue.queueAsHighestPriority(MacroAct(BWAPI::UnitTypes::Zerg_Hatchery, loc));
		// And keep going.
	}

	// If the enemy has cloaked stuff, consider overlord speed.
	if (InformationManager::Instance().enemyHasMobileCloakTech())
	{
		if (hasLair &&
			minerals >= 150 && gas >= 150 &&
			_self->getUpgradeLevel(BWAPI::UpgradeTypes::Pneumatized_Carapace) == 0 &&
			!_self->isUpgrading(BWAPI::UpgradeTypes::Pneumatized_Carapace) &&
			!queue.anyInQueue(BWAPI::UpgradeTypes::Pneumatized_Carapace))
		{
			queue.queueAsHighestPriority(MacroAct(BWAPI::UpgradeTypes::Pneumatized_Carapace));
		}
		// And keep going.
	}
	
	// If the enemy has overlord hunters such as corsairs, prepare appropriately.
	if (InformationManager::Instance().enemyHasOverlordHunters())
	{
		if (nEvo > 0 && nDrones >= 9 && nSpores == 0 &&
			!queue.anyInQueue(BWAPI::UnitTypes::Zerg_Spore_Colony) &&
			!isBeingBuilt(BWAPI::UnitTypes::Zerg_Spore_Colony))
		{
			queue.queueAsHighestPriority(BWAPI::UnitTypes::Zerg_Spore_Colony);
			queue.queueAsHighestPriority(BWAPI::UnitTypes::Zerg_Creep_Colony);
		}
		else if (nEvo == 0 && nDrones >= 9 && outOfBook && hasPool &&
			!queue.anyInQueue(BWAPI::UnitTypes::Zerg_Evolution_Chamber) &&
			UnitUtil::GetAllUnitCount(BWAPI::UnitTypes::Zerg_Evolution_Chamber) == 0 &&
			!isBeingBuilt(BWAPI::UnitTypes::Zerg_Evolution_Chamber))
		{
			queue.queueAsHighestPriority(BWAPI::UnitTypes::Zerg_Evolution_Chamber);
		}
		else if (!hasSpire && hasLairTech && outOfBook &&
			minerals >= 200 && gas >= 150 && nGas > 0 && nDrones > 9 &&
			!queue.anyInQueue(BWAPI::UnitTypes::Zerg_Spire) &&
			UnitUtil::GetAllUnitCount(BWAPI::UnitTypes::Zerg_Spire) == 0 &&
			!isBeingBuilt(BWAPI::UnitTypes::Zerg_Spire))
		{
			queue.queueAsHighestPriority(MacroAct(BWAPI::UnitTypes::Zerg_Spire));
		}
		else if (hasLair &&
			minerals >= 150 && gas >= 150 &&
			_enemyRace != BWAPI::Races::Zerg &&
			_self->getUpgradeLevel(BWAPI::UpgradeTypes::Pneumatized_Carapace) == 0 &&
			!_self->isUpgrading(BWAPI::UpgradeTypes::Pneumatized_Carapace) &&
			!queue.anyInQueue(BWAPI::UpgradeTypes::Pneumatized_Carapace))
		{
			queue.queueAsHighestPriority(MacroAct(BWAPI::UpgradeTypes::Pneumatized_Carapace));
		}
	}

	// We have excess minerals and larvas.
	// Make some zerglings.
	if (outOfBook && hasPool &&
		((_emergencyGroundDefense && minerals >= 50 && nLarvas > 0) || (gas < 100 && minerals > 450 && nLarvas > 4)))
	{
		int n;
		if (_emergencyGroundDefense)
		{
			n = std::min(minerals / 50, nLarvas);
		}
		else
		{
			n = std::min((minerals - 300) / 50, nLarvas - 3);
		}

		for (int i = 0; i < n; ++i)
		{
			queue.queueAsHighestPriority(BWAPI::UnitTypes::Zerg_Zergling);
		}
	}
}

// Check for possible ground attacks that we are may have trouble handling.
// If it seems necessary, make a limited number of sunkens.
// If a deadly attack seems impending, declare an emergency so that the
// regular production plan will concentrate on combat units.
void StrategyBossZerg::checkGroundDefenses(BuildOrderQueue & queue)
{
	// 1. Figure out where our front defense line is.
	MacroLocation front = MacroLocation::Anywhere;
	BWAPI::Unit ourHatchery = nullptr;

	if (InformationManager::Instance().getMyNaturalLocation())
	{
		ourHatchery =
			InformationManager::Instance().getBaseDepot(InformationManager::Instance().getMyNaturalLocation());
		if (UnitUtil::IsValidUnit(ourHatchery))
		{
			front = MacroLocation::Natural;
		}
	}
	if (front == MacroLocation::Anywhere)
	{
		ourHatchery =
			InformationManager::Instance().getBaseDepot(InformationManager::Instance().getMyMainBaseLocation());
		if (UnitUtil::IsValidUnit(ourHatchery))
		{
			front = MacroLocation::Macro;
		}
	}
	if (!ourHatchery || front == MacroLocation::Anywhere)
	{
		// We don't have a place to put static defense. It's that bad.
		return;
	}

	// 2. Count enemy ground power.
	int enemyPower = 0;
	int enemyPowerNearby = 0;
	bool enemyHasVultures = false;
	for (const auto & kv : InformationManager::Instance().getUnitData(_enemy).getUnits())
	{
		const UnitInfo & ui(kv.second);

		if (!ui.type.isBuilding() && !ui.type.isWorker() &&
			ui.type.groundWeapon() != BWAPI::WeaponTypes::None &&
			!ui.type.isFlyer())
		{
			enemyPower += ui.type.supplyRequired();
			if (ui.updateFrame >= _lastUpdateFrame - 30 * 24 &&          // seen in the last 30 seconds
				ui.lastPosition.isValid() &&
				ourHatchery->getDistance(ui.lastPosition) < 1500)		 // not far from our front base
			{
				enemyPowerNearby += ui.type.supplyRequired();
			}
			if (ui.type == BWAPI::UnitTypes::Terran_Vulture)
			{
				enemyHasVultures = true;
			}
		}
	}

	// 3. Count our anti-ground power, including air units.
	int ourPower = 0;
	int ourSunkens = 0;
	for (const BWAPI::Unit u : _self->getUnits())
	{
		if (!u->getType().isBuilding() && !u->getType().isWorker() &&
			u->getType().groundWeapon() != BWAPI::WeaponTypes::None)
		{
			ourPower += u->getType().supplyRequired();
		}
		else if (u->getType() == BWAPI::UnitTypes::Zerg_Sunken_Colony ||
			u->getType() == BWAPI::UnitTypes::Zerg_Creep_Colony)          // blindly assume it will be a sunken
		{
			if (ourHatchery->getDistance(u) < 600)
			{
				++ourSunkens;
			}
		}
	}

	int queuedSunkens = queue.numInQueue(BWAPI::UnitTypes::Zerg_Sunken_Colony);  // without checking location
	ourPower += 5 * (ourSunkens + queuedSunkens);

	// 4. Build sunkens.
	// The nHatches term adjusts for what we may be able to build before they arrive.
	if (hasPool)
	{
		if (enemyPower > ourPower + 6 * nHatches)
		{
			// Make up to 4 sunkens at the front, one at a time.
			// During an emergency sunks will die and/or cause jams, so don't bother then.
			if (queuedSunkens == 0 && ourSunkens < 4 && nDrones >= 9 && !_emergencyGroundDefense)
			{
				queue.queueAsHighestPriority(MacroAct(BWAPI::UnitTypes::Zerg_Sunken_Colony, front));
				queue.queueAsHighestPriority(BWAPI::UnitTypes::Zerg_Drone);
				queue.queueAsHighestPriority(MacroAct(BWAPI::UnitTypes::Zerg_Creep_Colony, front));
			}
		}
		else if (enemyHasVultures)
		{
			// Make 1 sunken at the front.
			if (queuedSunkens == 0 && ourSunkens == 0 && nDrones >= 9)
			{
				queue.queueAsHighestPriority(MacroAct(BWAPI::UnitTypes::Zerg_Sunken_Colony, front));
				queue.queueAsHighestPriority(BWAPI::UnitTypes::Zerg_Drone);
				queue.queueAsHighestPriority(MacroAct(BWAPI::UnitTypes::Zerg_Creep_Colony, front));
			}
		}
	}

	// 5. Declare an emergency.
	// The nHatches term adjusts for what we may be able to build before the enemy arrives.
	if (enemyPowerNearby > ourPower + nHatches)
	{
		_emergencyGroundDefense = true;
		_emergencyStartFrame = _lastUpdateFrame;
	}
}

// If the enemy expanded or made static defense, we can spawn extra drones.
void StrategyBossZerg::analyzeExtraDrones()
{
	const double droneCost = 62.5;    // 50 + 1/8 of an overlord
	double extraDrones = 0;

	/* turned off for now
	// Enemy bases beyond the main.
	int nBases = 0;
	for (BWTA::BaseLocation * base : BWTA::getBaseLocations())
	{
		if (InformationManager::Instance().getBaseOwner(base) == _enemyRace)
		{
			++nBases;
		}
	}
	if (nBases > 1)
	{
		extraDrones += (nBases - 1) * 300.0 / droneCost;
	}
	*/

	// Enemy static defenses.
	for (const auto & kv : InformationManager::Instance().getUnitData(_enemy).getUnits())
	{
		const UnitInfo & ui(kv.second);

		if (ui.type.isBuilding())
		{
			if (ui.type == BWAPI::UnitTypes::Zerg_Creep_Colony)
			{
				extraDrones += 1.0 + 75.0 / droneCost;
			}
			else if (ui.type == BWAPI::UnitTypes::Zerg_Sunken_Colony || ui.type == BWAPI::UnitTypes::Zerg_Spore_Colony)
			{
				extraDrones += 1.0 + 125.0 / droneCost;
			}
			else if (ui.type.groundWeapon() != BWAPI::WeaponTypes::None ||
				ui.type.airWeapon() != BWAPI::WeaponTypes::None ||
				ui.type == BWAPI::UnitTypes::Terran_Bunker)
			{
				extraDrones += ui.type.mineralPrice() / droneCost;
			}
		}
	}

	// Enemy bases/static defense may have been added or destroyed, or both.
	// We don't keep track of what is destroyed, and react only to what is added since last check.
	int nExtraDrones = int(trunc(extraDrones));
	if (nExtraDrones > _extraDronesWanted)
	{
		_economyDrones -= nExtraDrones - _extraDronesWanted;   // pretend we made fewer drones
	}
	_extraDronesWanted = nExtraDrones;
}

// Versus protoss, decide whether hydras or mutas are more valuable.
// Decide by looking at the protoss unit mix.
bool StrategyBossZerg::vProtossGroundOverAir()
{
	// Bias.
	int denScore   = 3;
	int spireScore = 0;

	// Hysteresis. Make it bigger than the bias.
	if (_techTarget == TechTarget::Hydralisks) { denScore   += 13; };
	if (_techTarget == TechTarget::Mutalisks)  { spireScore +=  9; };

	for (const auto & kv : InformationManager::Instance().getUnitData(_enemy).getUnits())
	{
		const UnitInfo & ui(kv.second);

		if (!ui.type.isWorker() && !ui.type.isBuilding())
		{
			// Enemy mobile combat units.
			if (ui.type.airWeapon() == BWAPI::WeaponTypes::None)
			{
				spireScore += ui.type.supplyRequired();

				// Some units have no weapons and are counted for both sides.
				// Double count them here if they favor spire.
				if (ui.type == BWAPI::UnitTypes::Protoss_High_Templar ||
					ui.type == BWAPI::UnitTypes::Protoss_Shuttle ||
					ui.type == BWAPI::UnitTypes::Protoss_Observer)
				{
					spireScore += ui.type.supplyRequired();
				}
				// Reavers especially favor spire.
				else if (ui.type == BWAPI::UnitTypes::Protoss_Reaver)
				{
					spireScore += ui.type.supplyRequired() + 6;
				}
			}

			if (ui.type.groundWeapon() == BWAPI::WeaponTypes::None ||     // corsairs, etc.
				ui.type == BWAPI::UnitTypes::Protoss_Archon ||
				ui.type == BWAPI::UnitTypes::Protoss_Dragoon ||
				ui.type == BWAPI::UnitTypes::Protoss_Scout)
			{
				denScore += ui.type.supplyRequired();
			}
		}
		else if (ui.type == BWAPI::UnitTypes::Protoss_Photon_Cannon)
		{
			// Hydralisks are efficient against cannons.
			denScore += 2;
		}
		else if (ui.type == BWAPI::UnitTypes::Protoss_Robotics_Facility)
		{
			// Spire is good against anything from the robo fac.
			spireScore += 2;
		}
		else if (ui.type == BWAPI::UnitTypes::Protoss_Robotics_Support_Bay)
		{
			// Spire is especially good against reavers.
			// Suspicion of reavers is worth more than the bias toward hydras.
			spireScore += 6;
		}
	}

	return denScore > spireScore;
}

// Versus terran, decide whether hydras or mutas are more valuable.
// Decide by looking at the terran unit mix.
bool StrategyBossZerg::vTerranGroundOverAir()
{
	// Bias.
	int denScore   = 0;
	int spireScore = 3;

	// Hysteresis. Make it bigger than the bias.
	if (_techTarget == TechTarget::Hydralisks) { denScore   +=  9; };
	if (_techTarget == TechTarget::Mutalisks)  { spireScore += 13; };

	for (const auto & kv : InformationManager::Instance().getUnitData(_enemy).getUnits())
	{
		const UnitInfo & ui(kv.second);

		if (ui.type == BWAPI::UnitTypes::Terran_Marine)
		{
			spireScore += 1;
		}
		else if (ui.type == BWAPI::UnitTypes::Terran_Missile_Turret)
		{
			denScore += 1;
		}
		else if (ui.type == BWAPI::UnitTypes::Terran_Goliath)
		{
			denScore += 2;
		}
		else if (ui.type == BWAPI::UnitTypes::Terran_Valkyrie ||
			ui.type == BWAPI::UnitTypes::Terran_Battlecruiser)
		{
			denScore += 4;
		}
		else if (ui.type == BWAPI::UnitTypes::Terran_Wraith)
		{
			denScore += 1;
		}
		else if (ui.type == BWAPI::UnitTypes::Terran_Siege_Tank_Siege_Mode ||
			ui.type == BWAPI::UnitTypes::Terran_Siege_Tank_Tank_Mode)
		{
			spireScore += 5;
		}
		else if (!ui.type.isWorker() && !ui.type.isBuilding())
		{
			if (ui.type.airWeapon() == BWAPI::WeaponTypes::None)
			{
				spireScore += 2;
			}
		}
	}

	return denScore > spireScore;
}

// Versus zerg, decide whether zerglings or mutas are more valuable.
// Decide by looking at the enemy's static defense.
bool StrategyBossZerg::vZergGroundOverAir()
{
	// Bias.
	int lingScore  = 0;
	int spireScore = 1;

	// Hysteresis. Make it bigger than the bias.
	if (_techTarget == TechTarget::Ultralisks) { lingScore  += 2; };
	if (_techTarget == TechTarget::Mutalisks)  { spireScore += 2; };

	for (const auto & kv : InformationManager::Instance().getUnitData(_enemy).getUnits())
	{
		const UnitInfo & ui(kv.second);

		if (ui.type == BWAPI::UnitTypes::Zerg_Sunken_Colony)
		{
			spireScore += 1;
		}
		else if (ui.type == BWAPI::UnitTypes::Zerg_Spore_Colony)
		{
			lingScore += 1;
		}
	}

	return lingScore > spireScore;
}

// Are ground or air units more valuable?
// true = hydras, or straight lings vs zerg, or ultras; false = mutas
bool StrategyBossZerg::chooseGroundOverAir()
{
	if (_enemyRace == BWAPI::Races::Protoss)
	{
		return vProtossGroundOverAir();
	}
	if (_enemyRace == BWAPI::Races::Terran)
	{
		return vTerranGroundOverAir();
	}
	if (_enemyRace == BWAPI::Races::Zerg)
	{
		return vZergGroundOverAir();
	}
	// Otherwise enemy went random. Always choose air.
	return false;
}

// Choose the next tech to aim for, whether sooner or later.
// This tells freshProductionPlan() what to move toward, not when to take each step.
void StrategyBossZerg::chooseTechTarget(bool groundOverAir)
{
	const bool ZvZ = _enemyRace == BWAPI::Races::Zerg;

	// True if we DON'T want it: We have it or choose to skip it.
	const bool den = ZvZ || hasDen || isBeingBuilt(BWAPI::UnitTypes::Zerg_Hydralisk_Den) || UnitUtil::GetAllUnitCount(BWAPI::UnitTypes::Zerg_Hydralisk_Den);
	const bool spire = hasSpire || isBeingBuilt(BWAPI::UnitTypes::Zerg_Spire) || UnitUtil::GetAllUnitCount(BWAPI::UnitTypes::Zerg_Spire);
	const bool ultra = hasUltra || isBeingBuilt(BWAPI::UnitTypes::Zerg_Ultralisk_Cavern) || UnitUtil::GetAllUnitCount(BWAPI::UnitTypes::Zerg_Ultralisk_Cavern);

	// Default. Value at the start of the game and after all tech is available.
	_techTarget = TechTarget::None;

	// From low tech to high.
	if (!den && !spire)                     // excludes ZvZ
	{
		_techTarget = groundOverAir ? TechTarget::Hydralisks : TechTarget::Mutalisks;
	}
	else if (!den && spire && !ultra)       // excludes ZvZ
	{
		_techTarget = groundOverAir ? TechTarget::Ultralisks : TechTarget::Hydralisks;
	}
	else if (den && !spire && !ultra)       // includes ZvZ before spire
	{
		_techTarget = groundOverAir ? TechTarget::Ultralisks : TechTarget::Mutalisks;
	}
	else if (!ultra)
	{
		_techTarget = TechTarget::Ultralisks;
	}
}

// Set _mineralUnit and _gasUnit depending on our tech and the game situation.
// Current universe of units: Drone, Zergling, Hydralisk, Mutalisk, Ultralisk.
// This tells freshProductionPlan() what units to make.
void StrategyBossZerg::chooseUnitMix(bool groundOverAir)
{
	const bool ZvZ = _enemyRace == BWAPI::Races::Zerg;

	BWAPI::UnitType minUnit = BWAPI::UnitTypes::Zerg_Drone;
	BWAPI::UnitType gasUnit = BWAPI::UnitTypes::None;

	// From high to low tech.
	if (hasPool && hasUltra && hasUltraUps &&
		nDrones >= 24 && nGas >= 3 && InformationManager::Instance().getAir2GroundSupply(_enemy) < 32)
	{
		minUnit = BWAPI::UnitTypes::Zerg_Zergling;
		gasUnit = BWAPI::UnitTypes::Zerg_Ultralisk;
	}
	else if (groundOverAir && hasDen && hasUltra && hasUltraUps &&
		nDrones >= 30 && nGas >= 4)
	{
		// This should be a rare case. In version 1.2.2 it was much too common.
		minUnit = BWAPI::UnitTypes::Zerg_Hydralisk;
		gasUnit = BWAPI::UnitTypes::Zerg_Ultralisk;
	}
	else if (!hasPool && hasDen && hasHydraSpeed && hasSpire && nGas >= 3)
	{
		// Hydra + muta until the spawning pool is remade.
		// Sometimes we might want to do this even with a spawning pool.
		minUnit = BWAPI::UnitTypes::Zerg_Hydralisk;
		gasUnit = BWAPI::UnitTypes::Zerg_Mutalisk;
	}
	else if (hasPool && hasDen && hasHydraSpeed && hasSpire && nGas > 0)
	{
		minUnit = BWAPI::UnitTypes::Zerg_Zergling;
		gasUnit = groundOverAir ? BWAPI::UnitTypes::Zerg_Hydralisk : BWAPI::UnitTypes::Zerg_Mutalisk;
	}
	else if (ZvZ && hasPool && groundOverAir)
	{
		// Even if we have a spire, we choose zerglings over mutalisks.
		minUnit = BWAPI::UnitTypes::Zerg_Zergling;
		gasUnit = BWAPI::UnitTypes::None;
	}
	else if (hasPool && hasSpire && nGas > 0)
	{
		minUnit = BWAPI::UnitTypes::Zerg_Zergling;
		gasUnit = BWAPI::UnitTypes::Zerg_Mutalisk;
	}
	else if (hasPool && hasDen && hasHydraSpeed && nDrones >= 12)
	{
		minUnit = BWAPI::UnitTypes::Zerg_Zergling;
		gasUnit = BWAPI::UnitTypes::Zerg_Hydralisk;
	}
	else if (!hasPool && hasSpire && nGas > 0)
	{
		// Drone + mutalisk until the spawning pool is remade.
		minUnit = BWAPI::UnitTypes::Zerg_Drone;
		gasUnit = BWAPI::UnitTypes::Zerg_Mutalisk;
	}
	else if (!hasPool && hasDen)
	{
		// Drone + hydralisk until the spawning pool is remade.
		minUnit = BWAPI::UnitTypes::Zerg_Drone;
		gasUnit = BWAPI::UnitTypes::Zerg_Hydralisk;
	}
	else if (hasPool)
	{
		minUnit = BWAPI::UnitTypes::Zerg_Zergling;
		gasUnit = BWAPI::UnitTypes::None;
	}
	else
	{
		// No tech. Drone up.
		minUnit = BWAPI::UnitTypes::Zerg_Drone;
		gasUnit = BWAPI::UnitTypes::None;
	}

	setUnitMix(minUnit, gasUnit);
}

// Set the economy ratio according to the enemy race.
// If the enemy went random, the enemy race may change!
// This resets the drone/economy counts, so don't call it too often
// or you will get nothing but drones.
void StrategyBossZerg::chooseEconomyRatio()
{
	if (_enemyRace == BWAPI::Races::Zerg)
	{
		setEconomyRatio(0.15);
	}
	else if (_enemyRace == BWAPI::Races::Terran)
	{
		setEconomyRatio(0.45);
	}
	else if (_enemyRace == BWAPI::Races::Protoss)
	{
		setEconomyRatio(0.35);
	}
	else
	{
		// Enemy went random, race is still unknown. Choose cautiously.
		// We should find the truth soon enough.
		setEconomyRatio(0.20);
	}
}

// Choose current unit mix and next tech target to aim for.
// Called when the queue is empty and no future production is planned yet.
void StrategyBossZerg::chooseStrategy()
{
	bool groundOverAir = chooseGroundOverAir();
	chooseTechTarget(groundOverAir);
	chooseUnitMix(groundOverAir);

	// Reset the economy ratio if the enemy's race has changed.
	// It can change from Unknown to another race if the enemy went random.
	if (_enemyRace != _enemy->getRace())
	{
		_enemyRace = _enemy->getRace();
		chooseEconomyRatio();
	}
}

std::string StrategyBossZerg::techTargetToString(TechTarget target)
{
	if (target == TechTarget::Hydralisks) return "Hydras";
	if (target == TechTarget::Mutalisks ) return "Mutas";
	if (target == TechTarget::Ultralisks) return "Ultras";
	return "[none]";
}

// Draw various internal information bits, by default on the right side left of Bases.
void StrategyBossZerg::drawStrategyBossInformation()
{
	if (!Config::Debug::DrawStrategyBossInfo)
	{
		return;
	}

	const int x = 500;
	int y = 30;

	BWAPI::Broodwar->drawTextScreen(x, y, "%cStrat Boss", white);
	y += 13;
	BWAPI::Broodwar->drawTextScreen(x, y, "%cbases %c%d/%d", yellow, cyan, nBases, nBases+nFreeBases);
	y += 10;
	BWAPI::Broodwar->drawTextScreen(x, y, "%cpatches %c%d", yellow, cyan, nMineralPatches);
	y += 10;
	BWAPI::Broodwar->drawTextScreen(x, y, "%cgeysers %c%d/%d", yellow, cyan, nGas, nGas + nFreeGas);
	y += 10;
	BWAPI::Broodwar->drawTextScreen(x, y, "%cdrones%c %d/%d", yellow, cyan, nDrones, maxDrones);
	y += 10;
	BWAPI::Broodwar->drawTextScreen(x, y, "%c mins %c%d", yellow, cyan, nMineralDrones);
	y += 10;
	BWAPI::Broodwar->drawTextScreen(x, y, "%c gas %c%d", yellow, cyan, nGasDrones);
	y += 10;
	BWAPI::Broodwar->drawTextScreen(x, y, "%c react%c +%d", yellow, cyan, _extraDronesWanted);
	y += 10;
	BWAPI::Broodwar->drawTextScreen(x, y, "%clarvas %c%d", yellow, cyan, nLarvas);
	y += 13;
	if (outOfBook)
	{
		BWAPI::Broodwar->drawTextScreen(x, y, "%cecon %c%.2f", yellow, cyan, _economyRatio);
		y += 10;
		BWAPI::Broodwar->drawTextScreen(x, y, "%cunits %c%s", yellow, cyan, chooseGroundOverAir() ? "ground" : "air");
		y += 10;
		BWAPI::Broodwar->drawTextScreen(x, y, "%c%s", green, UnitTypeName(_mineralUnit).c_str());
		y += 10;
		BWAPI::Broodwar->drawTextScreen(x, y, "%c%s", green, UnitTypeName(_gasUnit).c_str());
		if (_techTarget != TechTarget::None)
		{
			y += 10;
			BWAPI::Broodwar->drawTextScreen(x, y, "%cplan %c%s", white, orange,
				techTargetToString(_techTarget).c_str());
		}
	}
	else
	{
		BWAPI::Broodwar->drawTextScreen(x, y, "%c[book]", white);
	}
	if (_emergencyGroundDefense)
	{
		y += 13;
		BWAPI::Broodwar->drawTextScreen(x, y, "%cEMERGENCY", red);
	}
}

// -- -- -- -- -- -- -- -- -- -- --
// Public methods.

StrategyBossZerg & StrategyBossZerg::Instance()
{
	static StrategyBossZerg instance;
	return instance;
}

// Set the unit mix.
// The mineral unit will can set to Drone, but cannot be None.
// The mineral unit must be less gas-intensive than the gas unit.
// The idea is to make as many gas units as gas allows, and use any extra minerals
// on the mineral units (which may want gas too).
void StrategyBossZerg::setUnitMix(BWAPI::UnitType minUnit, BWAPI::UnitType gasUnit)
{
	// The mineral unit must be given.
	if (minUnit == BWAPI::UnitTypes::None)
	{
		// BWAPI::Broodwar->printf("mineral unit should be given");
		minUnit = BWAPI::UnitTypes::Zerg_Drone;
	}

	if (gasUnit != BWAPI::UnitTypes::None && minUnit.gasPrice() > gasUnit.gasPrice())
	{
		// BWAPI::Broodwar->printf("mineral unit cannot want more gas");
		gasUnit = BWAPI::UnitTypes::None;
	}

	_mineralUnit = minUnit;
	_gasUnit = gasUnit;
}

void StrategyBossZerg::setEconomyRatio(double ratio)
{
	_economyRatio = ratio;
	_economyDrones = 0;
	_economyTotal = 0;
}

// Solve urgent production issues. Called once per frame.
// If we're in trouble, clear the production queue and/or add emergency actions.
// Or if we just need overlords, make them.
// This routine is allowed to take direct actions or cancel stuff to get or preserve resources.
void StrategyBossZerg::handleUrgentProductionIssues(BuildOrderQueue & queue)
{
	updateGameState();

	while (nextInQueueIsUseless(queue))
	{
		// BWAPI::Broodwar->printf("removing useless %s", queue.getHighestPriorityItem().macroAct.getName().c_str());

		if (queue.getHighestPriorityItem().macroAct.isUnit() &&
			queue.getHighestPriorityItem().macroAct.getUnitType() == BWAPI::UnitTypes::Zerg_Hatchery)
		{
			// We only cancel a hatchery in case of dire emergency. Get the scout drone back home.
			ScoutManager::Instance().releaseWorkerScout();
			// Also cancel hatcheries already sent away for.
			BuildingManager::Instance().cancelBuildingType(BWAPI::UnitTypes::Zerg_Hatchery);
		}
		queue.removeHighestPriorityItem();
	}

	// Check for the most urgent actions once per frame.
	if (takeUrgentAction(queue))
	{
		// These are serious emergencies, and it's no help to check further.
		makeOverlords(queue);
	}
	else
	{
		// Check for less urgent reactions less often.
		int frameOffset = BWAPI::Broodwar->getFrameCount() % 32;
		if (frameOffset == 0)
		{
			makeUrgentReaction(queue);
			makeOverlords(queue);
		}
		else if (frameOffset == 16)
		{
			checkGroundDefenses(queue);
			makeOverlords(queue);
		}
		else if (frameOffset == 24)
		{
			analyzeExtraDrones();      // no need to make overlords
		}
	}
}

// Called when the queue is empty, which means that we are out of book.
// Fill up the production queue with new stuff.
BuildOrder & StrategyBossZerg::freshProductionPlan()
{
	_latestBuildOrder.clearAll();

	updateGameState();
	chooseStrategy();
	
	int larvasLeft = nLarvas;
	int mineralsLeft = minerals;

	// 1. Add up to 9 drones if we're below.
	for (int i = nDrones; i < std::min(9, maxDrones); ++i)
	{
		produce(BWAPI::UnitTypes::Zerg_Drone);
		--larvasLeft;
		mineralsLeft -= 50;
	}

	// 2. If there is no spawning pool, we always need that.
	if (!hasPool &&
		!isBeingBuilt(BWAPI::UnitTypes::Zerg_Spawning_Pool) &&
		UnitUtil::GetAllUnitCount(BWAPI::UnitTypes::Zerg_Spawning_Pool) == 0)
	{
		produce(BWAPI::UnitTypes::Zerg_Spawning_Pool);
		// If we're low on drones, replace the drone.
		if (nDrones <= 9 && nDrones <= maxDrones)
		{
			produce(BWAPI::UnitTypes::Zerg_Drone);
			--larvasLeft;
			mineralsLeft -= 50;
		}
	}

	// 3. We want to expand.
	// Division of labor: Expansions are here, macro hatcheries are "urgent production issues".
	// However, some macro hatcheries may be placed at expansions.
	if (nDrones > nMineralPatches + 3 * nGas && nFreeBases > 0 &&
		!isBeingBuilt(BWAPI::UnitTypes::Zerg_Hatchery))
	{
		MacroLocation loc = MacroLocation::Expo;
		// Be a little generous with minonly expansions
		if (_gasUnit == BWAPI::UnitTypes::None || nHatches % 2 == 0)
		{
			loc = MacroLocation::MinOnly;
		}
		produce(MacroAct(BWAPI::UnitTypes::Zerg_Hatchery, loc));
	}

	// 4. Get gas. If necessary, expand for it.
	// 4.A. If we have enough economy, get gas.
	if (nGas == 0 && gas < 300 && nFreeGas > 0 && nDrones >= 9 && hasPool &&
		!isBeingBuilt(BWAPI::UnitTypes::Zerg_Extractor))
	{
		produce(BWAPI::UnitTypes::Zerg_Extractor);
		if (!WorkerManager::Instance().isCollectingGas())
		{
			produce(MacroCommandType::StartGas);
		}
	}
	// 4.B. Or make more extractors if we have a low ratio of gas to minerals.
	else if (_gasUnit != BWAPI::UnitTypes::None &&
		nFreeGas > 0 &&
		nDrones > 3 * InformationManager::Instance().getNumBases(_self) + 3 * nGas + 4 &&
		(minerals + 100) / (gas + 100) >= 3 &&
		!isBeingBuilt(BWAPI::UnitTypes::Zerg_Extractor))
	{
		produce(BWAPI::UnitTypes::Zerg_Extractor);
	}
	// 4.C. At least make a second extractor if we're going muta.
	else if (hasPool && _gasUnit == BWAPI::UnitTypes::Zerg_Mutalisk && nGas < 2 && nFreeGas > 0 && nDrones >= 10 &&
		!isBeingBuilt(BWAPI::UnitTypes::Zerg_Extractor))
	{
		produce(BWAPI::UnitTypes::Zerg_Extractor);
	}
	// 4.D. Or expand if we are out of free geysers.
	else if ((_mineralUnit.gasPrice() > 0 || _gasUnit != BWAPI::UnitTypes::None) &&
		nFreeGas == 0 && nFreeBases > 0 &&
		nDrones > 3 * InformationManager::Instance().getNumBases(_self) + 3 * nGas + 5 &&
		(minerals + 100) / (gas + 100) >= 3 && minerals > 350 &&
		!isBeingBuilt(BWAPI::UnitTypes::Zerg_Extractor) &&
		!isBeingBuilt(BWAPI::UnitTypes::Zerg_Hatchery))
	{
		// This asks for a gas base, but we didn't check whether any are available.
		// If none are left, we'll get a mineral only.
		produce(MacroAct(BWAPI::UnitTypes::Zerg_Hatchery));
	}

	// Get zergling speed if at all sensible.
	if (hasPool && nDrones >= 9 && nGas > 0 &&
		(nLings >= 6 || _mineralUnit == BWAPI::UnitTypes::Zerg_Zergling) &&
		_self->getUpgradeLevel(BWAPI::UpgradeTypes::Metabolic_Boost) == 0 &&
		!_self->isUpgrading(BWAPI::UpgradeTypes::Metabolic_Boost))
	{
		produce(BWAPI::UpgradeTypes::Metabolic_Boost);
	}

	// Ditto zergling attack rate.
	if (hasPool && hasHiveTech && nDrones >= 12 && nGas > 0 &&
		(nLings >= 8 || _mineralUnit == BWAPI::UnitTypes::Zerg_Zergling) && 
		_self->getUpgradeLevel(BWAPI::UpgradeTypes::Adrenal_Glands) == 0 &&
		!_self->isUpgrading(BWAPI::UpgradeTypes::Adrenal_Glands))
	{
		produce(BWAPI::UpgradeTypes::Adrenal_Glands);
	}

	// Get hydralisk den if it's next.
	if (hasPool && _techTarget == TechTarget::Hydralisks && nDrones >= 13 && nGas > 0 &&
		!isBeingBuilt(BWAPI::UnitTypes::Zerg_Hydralisk_Den) &&
		UnitUtil::GetAllUnitCount(BWAPI::UnitTypes::Zerg_Hydralisk_Den) == 0)
	{
		produce(BWAPI::UnitTypes::Zerg_Hydralisk_Den);
	}

	// If we have a hydra den, get hydra speed. After a few hydras, get range.
	// It's no fun to block the first hydras by starting the range upgrade first.
	if (hasDen && nDrones >= 13 && nGas > 0)
	{
		if (_self->getUpgradeLevel(BWAPI::UpgradeTypes::Muscular_Augments) == 0 &&
			!_self->isUpgrading(BWAPI::UpgradeTypes::Muscular_Augments))
		{
			produce(BWAPI::UpgradeTypes::Muscular_Augments);
		}
		else if (nHydras >= 3 &&
			(_mineralUnit == BWAPI::UnitTypes::Zerg_Hydralisk || _gasUnit == BWAPI::UnitTypes::Zerg_Hydralisk) &&
			_self->getUpgradeLevel(BWAPI::UpgradeTypes::Grooved_Spines) == 0 &&
			!_self->isUpgrading(BWAPI::UpgradeTypes::Grooved_Spines))
		{
			produce(BWAPI::UpgradeTypes::Grooved_Spines);
		}
	}

	// Prepare an evo chamber or two.
	// Terran doesn't want the first evo until after den or spire.
	if (hasPool && nGas > 0 && !_emergencyGroundDefense &&
		!isBeingBuilt(BWAPI::UnitTypes::Zerg_Evolution_Chamber))
	{
		if (nEvo == 0 && nDrones >= 18 && (_enemyRace != BWAPI::Races::Terran || hasDen || hasSpire || hasUltra) ||
			nEvo == 1 && nDrones >= 30 && nGas >= 2 && (hasDen || hasSpire || hasUltra) && _self->isUpgrading(BWAPI::UpgradeTypes::Zerg_Carapace))
		{
			produce(BWAPI::UnitTypes::Zerg_Evolution_Chamber);
		}
	}

	// If we're in reasonable shape, get carapace upgrades.
	int armorUps = _self->getUpgradeLevel(BWAPI::UpgradeTypes::Zerg_Carapace);
	if (nEvo > 0 && nDrones >= 12 && nGas > 0 && !_emergencyGroundDefense &&
		hasPool &&
		!_self->isUpgrading(BWAPI::UpgradeTypes::Zerg_Carapace))
	{
		if (armorUps == 0 ||
			armorUps == 1 && hasLairTech ||
			armorUps == 2 && hasHiveTech)
		{
			// But delay if we're going mutas and don't have many yet. They want the gas.
			if (!(hasSpire && _gasUnit == BWAPI::UnitTypes::Zerg_Mutalisk && gas < 600 && nMutas < 6))
			{
				produce(BWAPI::UpgradeTypes::Zerg_Carapace);
			}
		}
	}

	// If we have 2 evos, or if carapace upgrades are done, also get melee attack.
	if ((nEvo >= 2 || nEvo > 0 && armorUps == 3) && nDrones >= 14 && nGas >= 2 && !_emergencyGroundDefense &&
		hasPool && (hasDen || hasSpire || hasUltra) &&
		!_self->isUpgrading(BWAPI::UpgradeTypes::Zerg_Melee_Attacks))
	{
		int attackUps = _self->getUpgradeLevel(BWAPI::UpgradeTypes::Zerg_Melee_Attacks);
		if (attackUps == 0 ||
			attackUps == 1 && hasLairTech ||
			attackUps == 2 && hasHiveTech)
		{
			// But delay if we're going mutas and don't have many yet. They want the gas.
			if (!(hasSpire && _gasUnit == BWAPI::UnitTypes::Zerg_Mutalisk && gas < 400 && nMutas < 6))
			{
				produce(BWAPI::UpgradeTypes::Zerg_Melee_Attacks);
			}
		}
	}

	// Make a lair. Make it earlier in ZvZ. Make it later if we only want it for ultras.
	if ((_techTarget == TechTarget::Mutalisks || armorUps > 0) &&
		hasPool && nLairs + nHives == 0 && nGas > 0 &&
		(nDrones >= 12 || _enemyRace == BWAPI::Races::Zerg && nDrones >= 9))
	{
		produce(BWAPI::UnitTypes::Zerg_Lair);
	}
	else if (_techTarget == TechTarget::Ultralisks &&
		hasPool && nLairs + nHives == 0 && nGas > 0 && nDrones >= 16)
	{
		produce(BWAPI::UnitTypes::Zerg_Lair);
	}

	// Make a spire. Make it earlier in ZvZ.
	if (!hasSpire && _techTarget == TechTarget::Mutalisks && hasLairTech && nGas > 0 &&
		(nDrones >= 13 && nGas + nFreeGas >= 2 || _enemyRace == BWAPI::Races::Zerg && nDrones >= 9) &&
		!isBeingBuilt(BWAPI::UnitTypes::Zerg_Spire) &&
		UnitUtil::GetAllUnitCount(BWAPI::UnitTypes::Zerg_Spire) == 0)
	{
		produce(BWAPI::UnitTypes::Zerg_Spire);
	}

	// Make a queen's nest. Make it later versus zerg.
	if (!hasQueensNest && hasLair && nGas >= 2 && !_emergencyGroundDefense &&
		(_techTarget == TechTarget::Ultralisks && nDrones >= 16 ||
		 nDrones >= 26 ||
		 _enemyRace != BWAPI::Races::Zerg && nDrones >= 20) &&
		 !isBeingBuilt(BWAPI::UnitTypes::Zerg_Queens_Nest) &&
		 UnitUtil::GetAllUnitCount(BWAPI::UnitTypes::Zerg_Queens_Nest) == 0)
	{
		// Get some intermediate units before moving toward hive.
		if ((_mineralUnit == BWAPI::UnitTypes::Zerg_Hydralisk || _gasUnit == BWAPI::UnitTypes::Zerg_Hydralisk) &&
			nHydras >= 12)
		{
			produce(BWAPI::UnitTypes::Zerg_Queens_Nest);
		}
		else if (_gasUnit == BWAPI::UnitTypes::Zerg_Mutalisk && nMutas >= 6)
		{
			produce(BWAPI::UnitTypes::Zerg_Queens_Nest);
		}
	}

	// Make a hive.
	// Ongoing lair research will delay the hive.
	if ((_techTarget == TechTarget::Ultralisks || armorUps >= 2) &&
		nHives == 0 && hasLair && hasQueensNest && nDrones >= 16 && nGas >= 2 &&
		!_self->isUpgrading(BWAPI::UpgradeTypes::Pneumatized_Carapace) &&
		!_self->isUpgrading(BWAPI::UpgradeTypes::Ventral_Sacs))
	{
		produce(BWAPI::UnitTypes::Zerg_Hive);
	}

	// Move toward ultralisks.
	if (_techTarget == TechTarget::Ultralisks && !hasUltra && hasHiveTech && nDrones >= 24 && nGas >= 3 &&
		!isBeingBuilt(BWAPI::UnitTypes::Zerg_Ultralisk_Cavern) &&
		UnitUtil::GetAllUnitCount(BWAPI::UnitTypes::Zerg_Ultralisk_Cavern) == 0)
	{
		produce(BWAPI::UnitTypes::Zerg_Ultralisk_Cavern);
	}
	else if (hasUltra && nDrones >= 24 && nGas >= 3)
	{
		if (_self->getUpgradeLevel(BWAPI::UpgradeTypes::Anabolic_Synthesis) == 0 &&
			!_self->isUpgrading(BWAPI::UpgradeTypes::Anabolic_Synthesis))
		{
			produce(BWAPI::UpgradeTypes::Anabolic_Synthesis);
		}
		else if (_self->getUpgradeLevel(BWAPI::UpgradeTypes::Anabolic_Synthesis) != 0 &&
			_self->getUpgradeLevel(BWAPI::UpgradeTypes::Chitinous_Plating) == 0 &&
			!_self->isUpgrading(BWAPI::UpgradeTypes::Chitinous_Plating))
		{
			produce(BWAPI::UpgradeTypes::Chitinous_Plating);
		}
	}
	
	// If we have resources left, make units too.
	// Include drones according to _economyRatio.
	// NOTE Mineral usage counts drones made earlier, but not buildings or research.
	// NOTE Also, earlier gas usage is not counted at all.
	if (_gasUnit == BWAPI::UnitTypes::None)
	{
		// Only the mineral unit is set. Its gas price should be 0.
		while (larvasLeft > 0 && mineralsLeft > 0)
		{
			BWAPI::UnitType type = needDroneNext() ? BWAPI::UnitTypes::Zerg_Drone : _mineralUnit;
			produce(type);
			--larvasLeft;
			mineralsLeft -= type.mineralPrice();
		}
	}
	else
	{
		// The gas unit is also set. The mineral unit may also need gas.
		// Make as many gas units as gas allows, mixing in mineral units if possible.
		int nGasUnits = 1 + gas / _gasUnit.gasPrice();    // number remaining to make
		bool gasUnitNext = true;
		while (larvasLeft > 0 && mineralsLeft > 0)
		{
			BWAPI::UnitType type;
			if (nGasUnits > 0 && gasUnitNext)
			{
				type = needDroneNext() ? BWAPI::UnitTypes::Zerg_Drone : _gasUnit;
				// If we expect to want mineral units, mix them in.
				if (nGasUnits < larvasLeft && nGasUnits * _gasUnit.mineralPrice() < mineralsLeft)
				{
					gasUnitNext = false;
				}
				if (type == _gasUnit)
				{
					--nGasUnits;
				}
			}
			else
			{
				type = needDroneNext() ? BWAPI::UnitTypes::Zerg_Drone : _mineralUnit;
				gasUnitNext = true;
			}
			produce(type);
			--larvasLeft;
			mineralsLeft -= type.mineralPrice();
		}
	}
	
	return _latestBuildOrder;
}
