#include "StrategyBossZerg.h"

#include "ProductionManager.h"
#include "UnitUtil.h"

using namespace UAlbertaBot;

StrategyBossZerg::StrategyBossZerg()
	: _self(BWAPI::Broodwar->self())
	, _existingSupply(-1)
	, _pendingSupply(-1)
{
}

StrategyBossZerg & StrategyBossZerg::Instance()
{
	static StrategyBossZerg instance;
	return instance;
}

// Existing and pending supply.
void StrategyBossZerg::figureSupply()
{
	int existingSupply = 0;
	int pendingSupply = 0;

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
		else if (unit->getType() == BWAPI::UnitTypes::Zerg_Egg &&
			unit->getBuildType() == BWAPI::UnitTypes::Zerg_Overlord)
		{
			pendingSupply += 16;
		}
		else if (unit->getType() == BWAPI::UnitTypes::Zerg_Hatchery && !unit->isCompleted())
		{
			pendingSupply += 2;
		}
		else if (unit->getType().isResourceDepot())
		{
			// This condition is correct only because incomplete hatcheries are counted above.
			existingSupply += 2;
		}
	}

	_existingSupply = std::min(existingSupply, 400);
	_pendingSupply = pendingSupply;

	if (_existingSupply != _self->supplyTotal())
	{
		BWAPI::Broodwar->printf("supply %d /= %d", _existingSupply, _self->supplyTotal());
	}
}

void StrategyBossZerg::figure()
{
	figureSupply();
}

// Solve urgent production issues. Called once per frame.
// If we're in trouble, clear the production queue and/or add emergency actions.
// Or if we just need overlords, make them.
// This routine is allowed to take direct actions or cancel stuff to get or preserve resources.
void StrategyBossZerg::handleUrgentProductionIssues(BuildOrderQueue & queue)
{
	int minerals = _self->minerals();
	int gas = _self->gas();

	// Unit stuff, including uncompleted units.
	int nLairs = UnitUtil::GetAllUnitCount(BWAPI::UnitTypes::Zerg_Lair);
	int nHives = UnitUtil::GetAllUnitCount(BWAPI::UnitTypes::Zerg_Hive);
	int nHatches = UnitUtil::GetAllUnitCount(BWAPI::UnitTypes::Zerg_Hatchery)
		+ nLairs + nHives;
	int nGas = UnitUtil::GetAllUnitCount(BWAPI::UnitTypes::Zerg_Extractor);
	int nSpores = UnitUtil::GetAllUnitCount(BWAPI::UnitTypes::Zerg_Spore_Colony);

	int nDrones = UnitUtil::GetAllUnitCount(BWAPI::UnitTypes::Zerg_Drone);
	int nLarvas = UnitUtil::GetAllUnitCount(BWAPI::UnitTypes::Zerg_Larva);

	// Tech stuff. It has to be completed for the tech to be available.
	int nEvo = UnitUtil::GetCompletedUnitCount(BWAPI::UnitTypes::Zerg_Evolution_Chamber);
	bool hasPool = UnitUtil::GetCompletedUnitCount(BWAPI::UnitTypes::Zerg_Spawning_Pool) > 0;
	bool hasDen = UnitUtil::GetCompletedUnitCount(BWAPI::UnitTypes::Zerg_Hydralisk_Den) > 0;
	bool hasSpire = UnitUtil::GetCompletedUnitCount(BWAPI::UnitTypes::Zerg_Spire) > 0 ||
		UnitUtil::GetAllUnitCount(BWAPI::UnitTypes::Zerg_Greater_Spire) > 0;
	bool hasQueensNest = UnitUtil::GetCompletedUnitCount(BWAPI::UnitTypes::Zerg_Queens_Nest) > 0;

	// hasLairTech means "can research stuff in the lair" (not "can research stuff that needs lair").
	bool hasHiveTech = UnitUtil::GetCompletedUnitCount(BWAPI::UnitTypes::Zerg_Hive) > 0;
	bool hasLairTech = hasHiveTech || UnitUtil::GetCompletedUnitCount(BWAPI::UnitTypes::Zerg_Lair) > 0;

	bool outOfBook = ProductionManager::Instance().isOutOfBook();

	// We don't need overlords now. Drop any that are queued next and continue.
	if ((outOfBook || nDrones < 8) && _self->supplyTotal() - _self->supplyUsed() > 16)
	{
		while (!queue.isEmpty() &&
			queue.getHighestPriorityItem().macroAct.isUnit() &&
			queue.getHighestPriorityItem().macroAct.getUnitType() == BWAPI::UnitTypes::Zerg_Overlord)
		{
			queue.removeHighestPriorityItem();
		}
		// TODO if we're really badly off, also cancel overlords in the egg
	}

	// We are too poor to make a hatchery now.
	if (nDrones < 6 && minerals < 600 && nHatches > 0)
	{
		while (!queue.isEmpty() &&
			queue.getHighestPriorityItem().macroAct.isUnit() &&
			queue.getHighestPriorityItem().macroAct.getUnitType() == BWAPI::UnitTypes::Zerg_Hatchery)
		{
			queue.removeHighestPriorityItem();
		}
		// TODO if we're really badly off, also cancel building hatcheries
	}

	// Find the next thing in the queue, but only if it is a unit.
	BWAPI::UnitType nextInQueue = BWAPI::UnitTypes::None;
	if (!queue.isEmpty())
	{
		MacroAct & act = queue.getHighestPriorityItem().macroAct;
		if (act.isUnit())
		{
			nextInQueue = act.getUnitType();
		}
	}

	// There are no drones.
	if (nDrones == 0)
	{
		if (nextInQueue == BWAPI::UnitTypes::Zerg_Drone)
		{
			return;
		}
		queue.clearAll();
		// Queue one drone to mine minerals.
		WorkerManager::Instance().setCollectGas(false);
		queue.queueAsLowestPriority(MacroAct(BWAPI::UnitTypes::Zerg_Drone));
		if (nHatches == 0)
		{
			// No hatcheries either. Queue a second drone to become a hatchery.
			queue.queueAsLowestPriority(MacroAct(BWAPI::UnitTypes::Zerg_Drone));
			queue.queueAsLowestPriority(MacroAct(BWAPI::UnitTypes::Zerg_Hatchery));
			// cancelStuff(400,0);
		}
		else {
			// cancelStuff(50,0);
		}
		return;
	}

	// There are no hatcheries.
	if (nHatches == 0)
	{
		if (nextInQueue == BWAPI::UnitTypes::Zerg_Hatchery ||
			BuildingManager::Instance().isBeingBuilt(BWAPI::UnitTypes::Zerg_Hatchery))
		{
			return;
		}
		queue.clearAll();
		queue.queueAsLowestPriority(MacroAct(BWAPI::UnitTypes::Zerg_Hatchery));
		if (nDrones == 1)
		{
			queue.queueAsHighestPriority(MacroAct(BWAPI::UnitTypes::Zerg_Drone));
			// cancelStuff(350,0);
		}
		else {
			// cancelStuff(300,0);
		}
		return;
	}

	// There are < 3 drones. Make up to 3.
	// Making more than 3 breaks 4 pool openings.
	if (nDrones < 3 && nextInQueue != BWAPI::UnitTypes::Zerg_Drone)
	{
		queue.queueAsHighestPriority(MacroAct(BWAPI::UnitTypes::Zerg_Drone));
		if (nDrones < 2)
		{
			queue.queueAsHighestPriority(MacroAct(BWAPI::UnitTypes::Zerg_Drone));
		}
		// Don't cancel stuff. A drone should be mining, it's not that big an emergency.
		return;
	}

	// There are no drones on minerals. Turn off gas collection.
	// TODO late game there may be no minerals, but we should still get gas
	// TODO we may want to switch between gas and minerals to make tech units
	if (WorkerManager::Instance().getNumMineralWorkers() == 0 &&
		WorkerManager::Instance().isCollectingGas())
	{
		// Leave the queue in place.
		WorkerManager::Instance().setCollectGas(false);
		return;
	}

	// We lost the tech for the next unit in the queue. Clear the queue and bail.
	// Don't cancel too early: The tech may still be building.
	if (nextInQueue == BWAPI::UnitTypes::Zerg_Zergling && UnitUtil::GetAllUnitCount(BWAPI::UnitTypes::Zerg_Spawning_Pool) == 0 && !BuildingManager::Instance().isBeingBuilt(BWAPI::UnitTypes::Zerg_Spawning_Pool) ||
		nextInQueue == BWAPI::UnitTypes::Zerg_Hydralisk && UnitUtil::GetAllUnitCount(BWAPI::UnitTypes::Zerg_Hydralisk_Den) == 0 && !BuildingManager::Instance().isBeingBuilt(BWAPI::UnitTypes::Zerg_Hydralisk_Den) ||
		(nextInQueue == BWAPI::UnitTypes::Zerg_Mutalisk || nextInQueue == BWAPI::UnitTypes::Zerg_Scourge) && UnitUtil::GetAllUnitCount(BWAPI::UnitTypes::Zerg_Spire) == 0 && UnitUtil::GetAllUnitCount(BWAPI::UnitTypes::Zerg_Greater_Spire) == 0 && !BuildingManager::Instance().isBeingBuilt(BWAPI::UnitTypes::Zerg_Spire))
	{
		queue.clearAll();
		return;
	}

	// A sunken or spore is in the queue, but the corresponding creep colony died.
	if ((nextInQueue == BWAPI::UnitTypes::Zerg_Sunken_Colony || nextInQueue == BWAPI::UnitTypes::Zerg_Spore_Colony) &&
		UnitUtil::GetAllUnitCount(BWAPI::UnitTypes::Zerg_Creep_Colony) == 0 &&
		!BuildingManager::Instance().isBeingBuilt(BWAPI::UnitTypes::Zerg_Creep_Colony))
	{
		queue.removeHighestPriorityItem();
		return;
	}

	// TODO another morphed unit is in the queue and has no parent unit: lurker, guardian, devourer; lair, hive

	// Other emergencies are less urgent. Don't check every frame.
	if (BWAPI::Broodwar->getFrameCount() % 32 != 0)
	{
		return;
	}

	// TODO Ignores scourge in the egg.
	int totalScourge = UnitUtil::GetAllUnitCount(BWAPI::UnitTypes::Zerg_Scourge) +
		2 * queue.numInQueue(BWAPI::UnitTypes::Zerg_Scourge);

	// Enemy has known air units. Make scourge if possible.
	// TODO should break out of opening book?
	if (hasSpire && nGas > 0 && InformationManager::Instance().enemyHasAirTech())
	{
		int nScourgeNeeded = InformationManager::Instance().nScourgeNeeded();
		if (nScourgeNeeded > totalScourge)
		{
			int nPairs = (nScourgeNeeded - totalScourge + 1) / 2;
			int limit = 4;          // how many pairs at a time, max?
			if (nLarvas > 6 && gas > 6 * 75)
			{
				// Allow more if we have plenty of resources.
				limit = 8;
			}
			for (int i = 0; i < std::min(nPairs, limit); ++i)
			{
				queue.queueAsHighestPriority(MacroAct(BWAPI::UnitTypes::Zerg_Scourge));
			}
		}
		else if (totalScourge == 0)
		{
			// We apparently saw air tech but no air units yet.
			// Should we make one pair of scourge as insurance?
			queue.queueAsHighestPriority(MacroAct(BWAPI::UnitTypes::Zerg_Scourge));
		}
		// And keep going.
	}

	int queueMinerals, queueGas;
	queue.totalCosts(queueMinerals, queueGas);

	// We have too much gas. Turn off gas collection.
	// This ties in via ELSE with the next check!
	if (outOfBook &&
		WorkerManager::Instance().isCollectingGas() &&
		gas >= queueGas &&
		((minerals < 100 && gas > 400) || (minerals >= 100 && gas > 3 * minerals)))
	{
		// Don't mess with the queue.
		WorkerManager::Instance().setCollectGas(false);
		// And keep going.
	}

	// Gas is turned off, and upcoming items cost more gas than we have. Get gas.
	// Note ELSE!
	// TODO can blunder when gas is in the process of being turned off
	// TODO consider making an extractor if possible
	else if (outOfBook && !WorkerManager::Instance().isCollectingGas() && queueGas > gas)
	{
		if (nGas == 0)
		{
			// Well, we can't collect gas.
			// Make enough drones to get an extractor.
			if (nDrones >= 5)
			{
				queue.queueAsHighestPriority(MacroAct(BWAPI::UnitTypes::Zerg_Extractor));
			}
			else if (nextInQueue != BWAPI::UnitTypes::Zerg_Drone)
			{
				queue.queueAsHighestPriority(MacroAct(BWAPI::UnitTypes::Zerg_Drone));
			}
		}
		else {
			WorkerManager::Instance().setCollectGas(true);
		}
		// And keep going.
	}

	// We're in book and should have enough gas but it's off. Something went wrong.
	// Note ELSE!
	else if (!outOfBook && nextInQueue != BWAPI::UnitTypes::None && nextInQueue.gasPrice() > gas &&
		!WorkerManager::Instance().isCollectingGas())
	{
		if (nGas == 0 || nDrones < 6)
		{
			// Emergency. Give up and clear the queue.
			queue.clearAll();
			return;
		}
		// Not such an emergency. Turn gas on and keep going.
		WorkerManager::Instance().setCollectGas(true);
	}

	// We need a macro hatchery.
	if (minerals >= 400 && nLarvas == 0 && nHatches < 14 &&
		!BuildingManager::Instance().isBeingBuilt(BWAPI::UnitTypes::Zerg_Hatchery) &&
		!queue.anyInQueue(BWAPI::UnitTypes::Zerg_Hatchery))
	{
		MacroLocation loc = MacroLocation::Macro;
		if (nHatches % 2 != 0)
		{
			// Expand with every 2nd hatchery. Worth a try.
			loc = MacroLocation::Expo;
		}
		queue.queueAsHighestPriority(MacroAct(BWAPI::UnitTypes::Zerg_Hatchery, loc));
		// And keep going.
	}
	else if (outOfBook && minerals >= 1000 && nLarvas == 0 && nHatches < 14 &&
		!BuildingManager::Instance().isBeingBuilt(BWAPI::UnitTypes::Zerg_Hatchery) &&
		nextInQueue != BWAPI::UnitTypes::Zerg_Hatchery)
	{
		queue.queueAsHighestPriority(MacroAct(BWAPI::UnitTypes::Zerg_Hatchery, MacroLocation::Macro));
		// And keep going.
	}

	// We need overlords.
	figureSupply();
	int totalSupply = std::min(_existingSupply + _pendingSupply, 400);
	if (totalSupply < 400 && nextInQueue != BWAPI::UnitTypes::Zerg_Overlord)
	{
		int supplyDiff = totalSupply - _self->supplyUsed();
		if (nextInQueue != BWAPI::UnitTypes::None)
		{
			if (nextInQueue.isBuilding())
			{
				supplyDiff += 2;   // for the drone that will be used
			}
			else
			{
				supplyDiff -= nextInQueue.supplyRequired();
			}
		}
		supplyDiff += 2 * BuildingManager::Instance().buildingsQueued().size();    // for each drone to be used
		if (supplyDiff < 0)
		{
			for (int diff = supplyDiff; diff < 0; diff += 16)
			{
				queue.queueAsHighestPriority(BWAPI::UnitTypes::Zerg_Overlord);
			}
		}
		else if (_existingSupply > 20 && supplyDiff <= 4)     // 20 = overlord + 2 hatcheries
		{
			queue.queueAsHighestPriority(BWAPI::UnitTypes::Zerg_Overlord);
		}
		else if (_existingSupply > 128 && supplyDiff <= 8)
		{
			queue.queueAsHighestPriority(BWAPI::UnitTypes::Zerg_Overlord);
		}
		else if (_existingSupply > 196 && supplyDiff <= 12)
		{
			queue.queueAsHighestPriority(BWAPI::UnitTypes::Zerg_Overlord);
		}
		else if (_existingSupply > 256 && supplyDiff <= 16)
		{
			queue.queueAsHighestPriority(BWAPI::UnitTypes::Zerg_Overlord);
		}
	}

	// If the enemy has cloaked stuff, get overlord speed or a spore colony.
	// Also if the enemy has overlord hunters such as corsairs.
	if (InformationManager::Instance().enemyHasMobileCloakTech() ||
		InformationManager::Instance().enemyHasOverlordHunters())
	{
		if (_self->getUpgradeLevel(BWAPI::UpgradeTypes::Pneumatized_Carapace) == 0 &&
			!_self->isUpgrading(BWAPI::UpgradeTypes::Pneumatized_Carapace) &&
			hasLairTech &&
			minerals > 150 && gas > 150 &&
			!queue.anyInQueue(BWAPI::UpgradeTypes::Pneumatized_Carapace))
		{
			queue.queueAsHighestPriority(MacroAct(BWAPI::UpgradeTypes::Pneumatized_Carapace));
		}
		else if (nEvo > 0 && nDrones > 8 && nSpores == 0 &&
			!queue.anyInQueue(BWAPI::UnitTypes::Zerg_Spore_Colony) &&
			!BuildingManager::Instance().isBeingBuilt(BWAPI::UnitTypes::Zerg_Spore_Colony))
		{
			queue.queueAsHighestPriority(MacroAct(BWAPI::UnitTypes::Zerg_Spore_Colony));
			queue.queueAsHighestPriority(MacroAct(BWAPI::UnitTypes::Zerg_Creep_Colony));
		}
		// And keep going.
	}
}
