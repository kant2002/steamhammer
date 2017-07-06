#include "Common.h"
#include "WorkerManager.h"
#include "Micro.h"
#include "UnitUtil.h"

using namespace UAlbertaBot;

WorkerManager::WorkerManager() 
	: previousClosestWorker(nullptr)
	, _collectGas(true)
{
}

WorkerManager & WorkerManager::Instance() 
{
	static WorkerManager instance;
	return instance;
}

void WorkerManager::update() 
{
	updateWorkerStatus();
	handleGasWorkers();
	handleIdleWorkers();
	handleReturnCargoWorkers();
	handleMoveWorkers();
	handleCombatWorkers();

	drawResourceDebugInfo();
	drawWorkerInformation(450,20);

	workerData.drawDepotDebugInfo();

    handleRepairWorkers();
}

// Adjust worker jobs. This is done first, before handling each job.
// NOTE A mineral worker may go briefly idle after collecting minerals.
// That's OK; we don't change its status then.
void WorkerManager::updateWorkerStatus() 
{
	// for each of our Workers
	for (auto & worker : workerData.getWorkers())
	{
		if (!worker->isCompleted())
		{
			continue;     // the worker list includes drones in the egg
		}

		// TODO temporary debugging - see Micro::SmartMove
		// UAB_ASSERT(UnitUtil::IsValidUnit(worker), "bad worker");

		// If it's supposed to be on minerals but is actually collecting gas, fix it.
		// This can happen when we stop collecting gas; the worker can be mis-assigned.
		if (workerData.getWorkerJob(worker) == WorkerData::Minerals &&
			(worker->getOrder() == BWAPI::Orders::MoveToGas ||
			 worker->getOrder() == BWAPI::Orders::WaitForGas ||
			 worker->getOrder() == BWAPI::Orders::ReturnGas))
		{
			workerData.setWorkerJob(worker, WorkerData::Idle, nullptr);
		}

		// Idleness.
		// Order can be PlayerGuard for a drone that tries to build and fails.
		// There are other causes.
		if ((worker->isIdle() || worker->getOrder() == BWAPI::Orders::PlayerGuard) &&
			workerData.getWorkerJob(worker) != WorkerData::Minerals &&
			workerData.getWorkerJob(worker) != WorkerData::Build &&
			workerData.getWorkerJob(worker) != WorkerData::Move &&
			workerData.getWorkerJob(worker) != WorkerData::Scout)
		{
			workerData.setWorkerJob(worker, WorkerData::Idle, nullptr);
		}

		else if (workerData.getWorkerJob(worker) == WorkerData::Gas)
		{
			BWAPI::Unit refinery = workerData.getWorkerResource(worker);

			// If the refinery is gone.
			// A missing resource depot is dealt with in handleGasWorkers().
			if (!refinery || !refinery->exists() || refinery->getHitPoints() <= 0)
			{
				workerData.setWorkerJob(worker, WorkerData::Idle, nullptr);
			}
			else
			{
				// Self-defense.
				BWAPI::Unit target = getClosestEnemyUnit(worker);

				if (target &&
					(!target->isMoving() || target->isStuck()) &&
					worker->getDistance(target) <= 64)
				{
					Micro::SmartAttackUnit(worker, target);
				}
				else if (worker->getOrder() != BWAPI::Orders::MoveToGas &&
					worker->getOrder() != BWAPI::Orders::WaitForGas &&
					worker->getOrder() != BWAPI::Orders::HarvestGas &&
					worker->getOrder() != BWAPI::Orders::ReturnGas &&
					worker->getOrder() != BWAPI::Orders::ResetCollision)
				{
					workerData.setWorkerJob(worker, WorkerData::Idle, nullptr);
				}
			}
		}
		
		// If the worker is busy mining and an enemy comes near, maybe fight it.
		else if (workerData.getWorkerJob(worker) == WorkerData::Minerals)
		{
			BWAPI::Unit target = getClosestEnemyUnit(worker);

			if (target &&
				(!target->isMoving() || target->isStuck()) &&
				worker->getDistance(target) <= 64)
			{
				Micro::SmartAttackUnit(worker, target);
			}
			else if (worker->getOrder() != BWAPI::Orders::MoveToMinerals &&
				worker->getOrder() != BWAPI::Orders::WaitForMinerals &&
				worker->getOrder() != BWAPI::Orders::MiningMinerals &&
				worker->getOrder() != BWAPI::Orders::ReturnMinerals &&
				worker->getOrder() != BWAPI::Orders::ResetCollision)
			{
				workerData.setWorkerJob(worker, WorkerData::Idle, nullptr);
			}
		}
	}
}

void WorkerManager::setRepairWorker(BWAPI::Unit worker, BWAPI::Unit unitToRepair)
{
    workerData.setWorkerJob(worker, WorkerData::Repair, unitToRepair);
}

void WorkerManager::stopRepairing(BWAPI::Unit worker)
{
    workerData.setWorkerJob(worker, WorkerData::Idle, nullptr);
}

void WorkerManager::handleGasWorkers() 
{
	for (auto unit : BWAPI::Broodwar->self()->getUnits())
	{
		// if that unit is a refinery
		if (unit->getType().isRefinery() && unit->isCompleted())
		{
			// Don't collect gas if gas collection is off, or if the resource depot is missing.
			if (_collectGas && refineryHasDepot(unit))
			{
				// Gather gas: If too few are assigned, add more.
				int numAssigned = workerData.getNumAssignedWorkers(unit);
				for (int i = 0; i < (Config::Macro::WorkersPerRefinery - numAssigned); ++i)
				{
					BWAPI::Unit gasWorker = getGasWorker(unit);
					if (gasWorker)
					{
						workerData.setWorkerJob(gasWorker, WorkerData::Gas, unit);
					}
					else {
						return;    // won't find any more, either for this refinery or others
					}
				}
			}
			else {
				// Don't gather gas: If any workers are assigned, take them off.
				std::set<BWAPI::Unit> gasWorkers;
				workerData.getGasWorkers(gasWorkers);
				for (auto gasWorker : gasWorkers)
				{
					if (gasWorker->getOrder() != BWAPI::Orders::HarvestGas)    // not inside the refinery
					{
						workerData.setWorkerJob(gasWorker, WorkerData::Idle, nullptr);
						// An idle worker carrying gas will become a ReturnCargo worker,
						// so gas will not be lost needlessly.
					}
				}
			}
		}
	}
}

// Is our refinery near a resource depot that it can deliver gas to?
bool WorkerManager::refineryHasDepot(BWAPI::Unit refinery)
{
	// Iterate through units, not bases, because even if the main hatchery is destroyed
	// (so the base is considered gone), a macro hatchery may be close enough.
	// TODO could iterate through bases (from InfoMan) instead of units
	for (auto unit : BWAPI::Broodwar->self()->getUnits())
	{
		if (unit->getType().isResourceDepot() &&
			(unit->isCompleted() || unit->getType() == BWAPI::UnitTypes::Zerg_Lair || unit->getType() == BWAPI::UnitTypes::Zerg_Hive) &&
			unit->getDistance(refinery) < 600)
		{
			return true;
		}
	}

	return false;
}

void WorkerManager::handleIdleWorkers() 
{
	for (auto & worker : workerData.getWorkers())
	{
        UAB_ASSERT(worker != nullptr, "Worker was null");

		if (workerData.getWorkerJob(worker) == WorkerData::Idle) 
		{
			if (worker->isCarryingMinerals() || worker->isCarryingGas())
			{
				// It's carrying something, set it to hand in its cargo.
				setReturnCargoWorker(worker);         // only happens if there's a resource depot
			}
			else {
				// Otherwise send it to mine minerals.
				setMineralWorker(worker);             // only happens if there's a resource depot
			}
		}
	}
}

void WorkerManager::handleReturnCargoWorkers()
{
	for (auto & worker : workerData.getWorkers())
	{
		UAB_ASSERT(worker != nullptr, "Worker was null");

		if (workerData.getWorkerJob(worker) == WorkerData::ReturnCargo)
		{
			// If it still needs to return cargo, return it; otherwise go idle.
			// We have to make sure it has a resource depot to return cargo to.
			BWAPI::Unit depot;
			if ((worker->isCarryingMinerals() || worker->isCarryingGas()) &&
				(depot = getClosestDepot(worker)) &&
				worker->getDistance(depot) < 600)
			{
				Micro::SmartReturnCargo(worker);
			}
			else
			{
				// Can't return cargo. Let's be a mineral worker instead--if possible.
				setMineralWorker(worker);
			}
		}
	}
}

void WorkerManager::handleRepairWorkers()
{
    if (BWAPI::Broodwar->self()->getRace() != BWAPI::Races::Terran)
    {
        return;
    }

    for (auto & unit : BWAPI::Broodwar->self()->getUnits())
    {
        if (unit->getType().isBuilding() && (unit->getHitPoints() < unit->getType().maxHitPoints()))
        {
            BWAPI::Unit repairWorker = getClosestMineralWorkerTo(unit);
            setRepairWorker(repairWorker, unit);
            break;
        }
    }
}

void WorkerManager::handleCombatWorkers()
{
	for (auto & worker : workerData.getWorkers())
	{
        UAB_ASSERT(worker != nullptr, "Worker was null");

		if (workerData.getWorkerJob(worker) == WorkerData::Combat)
		{
			if (Config::Debug::DrawWorkerInfo) {
				BWAPI::Broodwar->drawCircleMap(worker->getPosition().x, worker->getPosition().y, 4, BWAPI::Colors::Yellow, true);
			}

			BWAPI::Unit target = getBestEnemyTarget(worker);
			if (target)
			{
				Micro::SmartAttackUnit(worker, target);
			}
		}
	}
}

BWAPI::Unit WorkerManager::getBestEnemyTarget(BWAPI::Unit worker)
{
	UAB_ASSERT(worker != nullptr, "Worker was null");

	BWAPI::Unit bestTarget = nullptr;
	int score = 9999;         // smaller is better
	int bestScore = 9999;

	for (auto & unit : BWAPI::Broodwar->enemy()->getUnits())
	{
		if (unit->isFlying())
		{
			continue;
		}

		score = unit->getDistance(worker);
		if (!unit->isMoving() || unit->isBraking() || unit->isStuck())
		{
			score -= 128;
		}
		if (unit->getType().topSpeed() >= worker->getType().topSpeed())
		{
			score += 64;
		}

		if (score < bestScore)
		{
			bestTarget = unit;
			bestScore = score;
		}
	}

	return bestTarget;
}

// Used for worker self-defense.
// Only count enemy units that can be targeted by workers.
BWAPI::Unit WorkerManager::getClosestEnemyUnit(BWAPI::Unit worker)
{
    UAB_ASSERT(worker != nullptr, "Worker was null");

	BWAPI::Unit closestUnit = nullptr;
	int closestDist = 1000;         // ignore anything farther away

	for (auto & unit : BWAPI::Broodwar->enemy()->getUnits())
	{
		if (unit->isVisible() && unit->isDetected() && !unit->isFlying())
		{
			int dist = unit->getDistance(worker);

			if (dist < closestDist)
			{
				closestUnit = unit;
				closestDist = dist;
			}
		}
	}

	return closestUnit;
}

void WorkerManager::finishedWithCombatWorkers()
{
	for (auto & worker : workerData.getWorkers())
	{
        UAB_ASSERT(worker != nullptr, "Worker was null");

		if (workerData.getWorkerJob(worker) == WorkerData::Combat)
		{
			workerData.setWorkerJob(worker, WorkerData::Idle, nullptr);
		}
	}
}

BWAPI::Unit WorkerManager::getClosestMineralWorkerTo(BWAPI::Unit enemyUnit)
{
    UAB_ASSERT(enemyUnit != nullptr, "enemyUnit was null");

    BWAPI::Unit closestMineralWorker = nullptr;
    double closestDist = 100000;

	// Former closest worker may have died or (if zerg) morphed into a building.
	if (UnitUtil::IsValidUnit(previousClosestWorker) && previousClosestWorker->getType().isWorker())
	{
		return previousClosestWorker;
    }

	for (auto & worker : workerData.getWorkers())
	{
        UAB_ASSERT(worker != nullptr, "Worker was null");

        if (workerData.getWorkerJob(worker) == WorkerData::Minerals) 
		{
			double dist = worker->getDistance(enemyUnit);
			if (worker->isCarryingMinerals() || worker->isCarryingGas())
			{
				// If it has cargo, pretend it is farther away.
				// That way we prefer empty workers and lose less cargo.
				dist += 64;
			}

            if (dist < closestDist)
            {
                closestMineralWorker = worker;
                dist = closestDist;
            }
		}
	}

    previousClosestWorker = closestMineralWorker;
    return closestMineralWorker;
}

BWAPI::Unit WorkerManager::getWorkerScout()
{
	for (auto & worker : workerData.getWorkers())
	{
        UAB_ASSERT(worker != nullptr, "Worker was null");
        if (workerData.getWorkerJob(worker) == WorkerData::Scout) 
		{
			return worker;
		}
	}

    return nullptr;
}

void WorkerManager::handleMoveWorkers() 
{
	for (auto & worker : workerData.getWorkers())
	{
        UAB_ASSERT(worker != nullptr, "Worker was null");

		if (workerData.getWorkerJob(worker) == WorkerData::Move) 
		{
			BWAPI::Unit depot;
			if ((worker->isCarryingMinerals() || worker->isCarryingGas()) &&
				(depot = getClosestDepot(worker)) &&
				worker->getDistance(depot) <= 256)
			{
				// A move worker is being sent to build or something.
				// Don't let it carry minerals or gas around wastefully.
				Micro::SmartReturnCargo(worker);
			}
			else
			{
				// UAB_ASSERT(worker->exists(), "bad worker");  // TODO temporary debugging - see Micro::SmartMove
				WorkerMoveData data = workerData.getWorkerMoveData(worker);
				Micro::SmartMove(worker, data.position);
			}
		}
	}
}

// Send the worker to mine minerals at the closest resource depot, if any.
void WorkerManager::setMineralWorker(BWAPI::Unit unit)
{
    UAB_ASSERT(unit != nullptr, "Unit was null");

	BWAPI::Unit depot = getClosestDepot(unit);

	if (depot)
	{
		workerData.setWorkerJob(unit, WorkerData::Minerals, depot);
	}
	else
	{
		// BWAPI::Broodwar->printf("No depot for mineral worker");
	}
}

// Worker is carrying minerals or gas. Tell it to hand them in.
void WorkerManager::setReturnCargoWorker(BWAPI::Unit unit)
{
	UAB_ASSERT(unit != nullptr, "Unit was null");

	BWAPI::Unit depot = getClosestDepot(unit);

	if (depot)
	{
		workerData.setWorkerJob(unit, WorkerData::ReturnCargo, depot);
	}
	else
	{
		// BWAPI::Broodwar->printf("No depot to accept return cargo");
	}
}

BWAPI::Unit WorkerManager::getClosestDepot(BWAPI::Unit worker)
{
	UAB_ASSERT(worker != nullptr, "Worker was null");

	BWAPI::Unit closestDepot = nullptr;
	double closestDistance = 0;

	for (auto & unit : BWAPI::Broodwar->self()->getUnits())
	{
        UAB_ASSERT(unit != nullptr, "Unit was null");

		if (unit->getType().isResourceDepot() &&
			(unit->isCompleted() || unit->getType() == BWAPI::UnitTypes::Zerg_Lair || unit->getType() == BWAPI::UnitTypes::Zerg_Hive) &&
			!workerData.depotIsFull(unit))
		{
			double distance = unit->getDistance(worker);
			if (!closestDepot || distance < closestDistance)
			{
				closestDepot = unit;
				closestDistance = distance;
			}
		}
	}

	return closestDepot;
}

// other managers that need workers call this when they're done with a unit
void WorkerManager::finishedWithWorker(BWAPI::Unit unit) 
{
	UAB_ASSERT(unit != nullptr, "Unit was null");

	//BWAPI::Broodwar->printf("finished with worker %d", unit->getID());
	workerData.setWorkerJob(unit, WorkerData::Idle, nullptr);
}

// Find a worker to be reassigned to gas duty.
BWAPI::Unit WorkerManager::getGasWorker(BWAPI::Unit refinery)
{
	UAB_ASSERT(refinery != nullptr, "Refinery was null");

	BWAPI::Unit closestWorker = nullptr;
	double closestDistance = 0;

	for (auto & unit : workerData.getWorkers())
	{
		UAB_ASSERT(unit != nullptr, "Unit was null");

		if (workerData.getWorkerJob(unit) == WorkerData::Minerals)
		{
			// Don't waste minerals. It's OK (and unlikely) to already be carrying gas.
			if (unit->isCarryingMinerals() ||                       // doesn't have minerals and
				unit->getOrder() == BWAPI::Orders::MiningMinerals)  // isn't about to get them
			{
				continue;
			}

			double distance = unit->getDistance(refinery);
			if (!closestWorker || distance < closestDistance)
			{
				closestWorker = unit;
				closestDistance = distance;
			}
		}
	}

	return closestWorker;
}

void WorkerManager::setBuildingWorker(BWAPI::Unit worker, Building & b)
{
     UAB_ASSERT(worker != nullptr, "Worker was null");

     workerData.setWorkerJob(worker, WorkerData::Build, b.type);
}

// Get a builder for BuildingManager.
// if setJobAsBuilder is true (default), it will be flagged as a builder unit
// set 'setJobAsBuilder' to false if we just want to see which worker will build a building
BWAPI::Unit WorkerManager::getBuilder(const Building & b, bool setJobAsBuilder)
{
	// variables to hold the closest worker of each type to the building
	BWAPI::Unit closestMovingWorker = nullptr;
	BWAPI::Unit closestMiningWorker = nullptr;
	double closestMovingWorkerDistance = 0;
	double closestMiningWorkerDistance = 0;

	// look through each worker that had moved there first
	for (auto & unit : workerData.getWorkers())
	{
        UAB_ASSERT(unit != nullptr, "Unit was null");

        // gas steal building uses scout worker
        if (b.isGasSteal && (workerData.getWorkerJob(unit) == WorkerData::Scout))
        {
            if (setJobAsBuilder)
            {
                workerData.setWorkerJob(unit, WorkerData::Build, b.type);
            }
            return unit;
        }

		// mining worker check
		if (unit->isCompleted() && (workerData.getWorkerJob(unit) == WorkerData::Minerals))
		{
			// if it is a new closest distance, set the pointer
			double distance = unit->getDistance(BWAPI::Position(b.finalPosition));
			if (unit->isCarryingMinerals() || unit->isCarryingGas() ||
				unit->getOrder() == BWAPI::Orders::MiningMinerals)
			{
				// If it has cargo or is busy getting some, pretend it is farther away.
				// That way we prefer empty workers and lose less cargo.
				distance += 96;
			}
			if (!closestMiningWorker || distance < closestMiningWorkerDistance)
			{
				closestMiningWorker = unit;
				closestMiningWorkerDistance = distance;
			}
		}

		// moving worker check
		if (unit->isCompleted() && (workerData.getWorkerJob(unit) == WorkerData::Move))
		{
			// if it is a new closest distance, set the pointer
			double distance = unit->getDistance(BWAPI::Position(b.finalPosition));
			if (unit->isCarryingMinerals() || unit->isCarryingGas() ||
				unit->getOrder() == BWAPI::Orders::MiningMinerals) {
				// If it has cargo or is busy getting some, pretend it is farther away.
				// That way we prefer empty workers and lose less cargo.
				distance += 96;
			}
			if (!closestMovingWorker || distance < closestMovingWorkerDistance)
			{
				closestMovingWorker = unit;
				closestMovingWorkerDistance = distance;
			}
		}
	}

	// if we found a moving worker, use it, otherwise using a mining worker
	BWAPI::Unit chosenWorker = closestMovingWorker ? closestMovingWorker : closestMiningWorker;

	// if the worker exists (one may not have been found in rare cases)
	if (chosenWorker && setJobAsBuilder)
	{
		workerData.setWorkerJob(chosenWorker, WorkerData::Build, b.type);
	}

	return chosenWorker;
}

// sets a worker as a scout
void WorkerManager::setScoutWorker(BWAPI::Unit worker)
{
	UAB_ASSERT(worker != nullptr, "Worker was null");

	workerData.setWorkerJob(worker, WorkerData::Scout, nullptr);
}

// gets a worker which will move to a current location
BWAPI::Unit WorkerManager::getMoveWorker(BWAPI::Position p)
{
	// set up the pointer
	BWAPI::Unit closestWorker = nullptr;
	double closestDistance = 0;

	// for each worker we currently have
	for (auto & unit : workerData.getWorkers())
	{
        UAB_ASSERT(unit != nullptr, "Unit was null");

		// only consider it if it's a mineral worker
		if (unit->isCompleted() && workerData.getWorkerJob(unit) == WorkerData::Minerals)
		{
			// if it is a new closest distance, set the pointer
			double distance = unit->getDistance(p);
			if (unit->isCarryingMinerals() || unit->isCarryingGas() ||
				unit->getOrder() == BWAPI::Orders::MiningMinerals) {
				// If it has cargo or is busy getting some, pretend it is farther away.
				// That way we prefer empty workers and lose less cargo.
				distance += 96;
			}
			if (!closestWorker || distance < closestDistance)
			{
				closestWorker = unit;
				closestDistance = distance;
			}
		}
	}

	// return the worker
	return closestWorker;
}

// sets a worker to move to a given location
void WorkerManager::setMoveWorker(int mineralsNeeded, int gasNeeded, BWAPI::Position p)
{
	// set up the pointer
	BWAPI::Unit closestWorker = nullptr;
	double closestDistance = 0;

	// for each worker we currently have
	for (auto & unit : workerData.getWorkers())
	{
        UAB_ASSERT(unit != nullptr, "Unit was null");

		// only consider it if it's a mineral worker
		if (unit->isCompleted() && workerData.getWorkerJob(unit) == WorkerData::Minerals)
		{
			// if it is a new closest distance, set the pointer
			double distance = unit->getDistance(p);
			if (unit->isCarryingMinerals() || unit->isCarryingGas()) {
				// If it has cargo, pretend it is farther away.
				// That way we prefer empty workers and lose less cargo.
				distance += 64;
			}
			if (!closestWorker || distance < closestDistance)
			{
				closestWorker = unit;
				closestDistance = distance;
			}
		}
	}

	if (closestWorker)
	{
		//BWAPI::Broodwar->printf("Setting worker job Move for worker %d", closestWorker->getID());
		workerData.setWorkerJob(closestWorker, WorkerData::Move, WorkerMoveData(mineralsNeeded, gasNeeded, p));
	}
	else
	{
		//BWAPI::Broodwar->printf("no worker found");
	}
}

// will we have the required resources by the time a worker can travel a certain distance
bool WorkerManager::willHaveResources(int mineralsRequired, int gasRequired, double distance)
{
	// if we don't require anything, we will have it
	if (mineralsRequired <= 0 && gasRequired <= 0)
	{
		return true;
	}

	// the speed of the worker unit
	double speed = BWAPI::Broodwar->self()->getRace().getWorker().topSpeed();

    UAB_ASSERT(speed > 0, "Speed is negative");

	// how many frames it will take us to move to the building location
	// add a second to account for worker getting stuck. better early than late
	double framesToMove = (distance / speed) + 50;

	// magic numbers to predict income rates
	double mineralRate = getNumMineralWorkers() * 0.045;
	double gasRate     = getNumGasWorkers() * 0.07;

	// calculate if we will have enough by the time the worker gets there
	if (mineralRate * framesToMove >= mineralsRequired &&
		gasRate * framesToMove >= gasRequired)
	{
		return true;
	}
	else
	{
		return false;
	}
}

void WorkerManager::setCombatWorker(BWAPI::Unit worker)
{
	UAB_ASSERT(worker != nullptr, "Worker was null");

	workerData.setWorkerJob(worker, WorkerData::Combat, nullptr);
}

void WorkerManager::onUnitMorph(BWAPI::Unit unit)
{
	UAB_ASSERT(unit != nullptr, "Unit was null");

	// if something morphs into a worker, add it
	if (unit->getType().isWorker() && unit->getPlayer() == BWAPI::Broodwar->self() && unit->getHitPoints() >= 0)
	{
		workerData.addWorker(unit);
	}

	// if something morphs into a building, was it a drone?
	if (unit->getType().isBuilding() && unit->getPlayer() == BWAPI::Broodwar->self() && unit->getPlayer()->getRace() == BWAPI::Races::Zerg)
	{
		//BWAPI::Broodwar->printf("A Drone started building");
		workerData.workerDestroyed(unit);
	}
}

void WorkerManager::onUnitShow(BWAPI::Unit unit)
{
	UAB_ASSERT(unit && unit->exists(), "bad unit");

	// add the depot if it exists
	if (unit->getType().isResourceDepot() && unit->getPlayer() == BWAPI::Broodwar->self())
	{
		workerData.addDepot(unit);
	}

	// if something morphs into a worker, add it
	if (unit->getType().isWorker() && unit->getPlayer() == BWAPI::Broodwar->self() && unit->getHitPoints() >= 0)
	{
		//BWAPI::Broodwar->printf("A worker was shown %d", unit->getID());
		workerData.addWorker(unit);
	}
}

void WorkerManager::rebalanceWorkers()
{
	// for each worker
	for (auto & worker : workerData.getWorkers())
	{
        UAB_ASSERT(worker != nullptr, "Worker was null");

		if (!workerData.getWorkerJob(worker) == WorkerData::Minerals)
		{
			continue;
		}

		BWAPI::Unit depot = workerData.getWorkerDepot(worker);

		if (depot && workerData.depotIsFull(depot))
		{
			workerData.setWorkerJob(worker, WorkerData::Idle, nullptr);
		}
		else if (!depot)
		{
			workerData.setWorkerJob(worker, WorkerData::Idle, nullptr);
		}
	}
}

void WorkerManager::onUnitDestroy(BWAPI::Unit unit) 
{
	UAB_ASSERT(unit != nullptr, "Unit was null");

	if (unit->getType().isResourceDepot() && unit->getPlayer() == BWAPI::Broodwar->self())
	{
		workerData.removeDepot(unit);
	}

	if (unit->getType().isWorker() && unit->getPlayer() == BWAPI::Broodwar->self()) 
	{
		workerData.workerDestroyed(unit);
	}

	if (unit->getType() == BWAPI::UnitTypes::Resource_Mineral_Field)
	{
		rebalanceWorkers();
	}
}

void WorkerManager::drawResourceDebugInfo() 
{
    if (!Config::Debug::DrawResourceInfo)
    {
        return;
    }

	for (auto & worker : workerData.getWorkers()) 
    {
        UAB_ASSERT(worker != nullptr, "Worker was null");

		char job = workerData.getJobCode(worker);

		BWAPI::Position pos = worker->getTargetPosition();

		BWAPI::Broodwar->drawTextMap(worker->getPosition().x, worker->getPosition().y - 5, "\x07%c", job);
		BWAPI::Broodwar->drawTextMap(worker->getPosition().x, worker->getPosition().y + 5, "\x03%s", worker->getOrder().getName().c_str());

		BWAPI::Broodwar->drawLineMap(worker->getPosition().x, worker->getPosition().y, pos.x, pos.y, BWAPI::Colors::Cyan);

		BWAPI::Unit depot = workerData.getWorkerDepot(worker);
		if (depot)
		{
			BWAPI::Broodwar->drawLineMap(worker->getPosition().x, worker->getPosition().y, depot->getPosition().x, depot->getPosition().y, BWAPI::Colors::Orange);
		}
	}
}

void WorkerManager::drawWorkerInformation(int x, int y) 
{
    if (!Config::Debug::DrawWorkerInfo)
    {
        return;
    }

	BWAPI::Broodwar->drawTextScreen(x, y, "\x04 Workers %d", workerData.getNumMineralWorkers());
	BWAPI::Broodwar->drawTextScreen(x, y+20, "\x04 UnitID");
	BWAPI::Broodwar->drawTextScreen(x+50, y+20, "\x04 State");

	int yspace = 0;

	for (auto & unit : workerData.getWorkers())
	{
        UAB_ASSERT(unit != nullptr, "Worker was null");

		BWAPI::Broodwar->drawTextScreen(x, y+40+((yspace)*10), "\x03 %d", unit->getID());
		BWAPI::Broodwar->drawTextScreen(x+50, y+40+((yspace++)*10), "\x03 %c", workerData.getJobCode(unit));
	}
}

bool WorkerManager::isFree(BWAPI::Unit worker)
{
    UAB_ASSERT(worker != nullptr, "Worker was null");

	return workerData.getWorkerJob(worker) == WorkerData::Minerals || workerData.getWorkerJob(worker) == WorkerData::Idle;
}

bool WorkerManager::isWorkerScout(BWAPI::Unit worker)
{
    UAB_ASSERT(worker != nullptr, "Worker was null");

	return (workerData.getWorkerJob(worker) == WorkerData::Scout);
}

bool WorkerManager::isBuilder(BWAPI::Unit worker)
{
    UAB_ASSERT(worker != nullptr, "Worker was null");

	return (workerData.getWorkerJob(worker) == WorkerData::Build);
}

int WorkerManager::getNumMineralWorkers() const
{
	return workerData.getNumMineralWorkers();	
}

int WorkerManager::getNumGasWorkers() const
{
	return workerData.getNumGasWorkers();
}

int WorkerManager::getNumReturnCargoWorkers() const
{
	return workerData.getNumReturnCargoWorkers();
}

int WorkerManager::getNumCombatWorkers() const
{
	return workerData.getNumCombatWorkers();
}

int WorkerManager::getNumIdleWorkers() const
{
	return workerData.getNumIdleWorkers();
}

// The largest number of workers that it is efficient to have right now.
// Does not take into account possible preparations for future expansions.
// May not exceed Config::Macro::AbsoluteMaxWorkers.
int WorkerManager::getMaxWorkers() const
{
	int patches = InformationManager::Instance().getMyNumMineralPatches();
	int refineries = InformationManager::Instance().getMyNumRefineries();

	// Never let the max number of workers fall to 0!
	// Set aside 1 for future opportunities.
	return std::min(
			Config::Macro::AbsoluteMaxWorkers,
			1 + int(Config::Macro::WorkersPerPatch * patches + Config::Macro::WorkersPerRefinery * refineries + 0.5)
		);
}
