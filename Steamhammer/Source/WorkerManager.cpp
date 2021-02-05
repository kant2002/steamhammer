#include "WorkerManager.h"

#include "Bases.h"
#include "BuildingPlacer.h"
#include "MacroAct.h"
#include "MapTools.h"
#include "Micro.h"
#include "ProductionManager.h"
#include "ScoutManager.h"
#include "The.h"
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

bool WorkerManager::isBusy(BWAPI::Unit worker) const
{
    return busy.contains(worker);
}

void WorkerManager::makeBusy(BWAPI::Unit worker)
{
    busy.insert(worker);
}

void WorkerManager::update()
{
    busy.clear();

	// NOTE Combat workers are placed in a combat squad and get their orders there.
	//      We ignore them here.
	updateWorkerStatus();
    clearBlockingMinerals();

	handleGasWorkers();
	handleIdleWorkers();
	handleReturnCargoWorkers();
	handleRepairWorkers();
	handleMineralWorkers();
    handleUnblockWorkers();
    handlePostedWorkers();

	drawWorkerInformation();
	workerData.drawDepotDebugInfo();
}

// Adjust worker jobs. This is done first, before handling each job.
// NOTE A mineral worker may go briefly idle after collecting minerals.
// That's OK; we don't change its status then.
void WorkerManager::updateWorkerStatus() 
{
	// If any buildings are due for construction, assume that builders are not idle.
	// This is still necessary; some bug remains that abandons workers.
	const bool catchIdleBuilders =
		!BuildingManager::Instance().anythingBeingBuilt() &&
		!ProductionManager::Instance().nextIsBuilding();

	for (BWAPI::Unit worker : workerData.getWorkers())
	{
        if (!worker ||
            !worker->getType().isWorker() ||
            !worker->exists() ||
            worker->getPlayer() != the.self())
        {
            UAB_ASSERT(false, "bad worker");
            continue;
        }

		if (!worker->isCompleted())
		{
			continue;     // the worker list includes drones in the egg
		}

        if (isBusy(worker))
        {
            continue;
        }

        // Deal with workers burrowed due to irradiate.
        if (worker->isBurrowed() && burrowedForSafety.find(worker) == burrowedForSafety.end())
        {
            if (!inIrradiateDanger(worker))
            {
                the.micro.Unburrow(worker);
            }
            continue;
        }

        if (worker->canBurrow() && inIrradiateDanger(worker))
        {
            // Burrow an irradiated drone, if possible, to prevent damage to friendly units.
            // Also burrow any drones near an irradiated unit, for their safety.
            // This affects only zerg.
            // Set burrowed workers to Idle, so that they aren't issued other orders.
            workerData.setWorkerJob(worker, WorkerData::Idle, nullptr);
            the.micro.Burrow(worker);
            continue;
        }

        if (the.now() % 29 == 0)
        {
            // Consider whether to unburrow workers that were burrowed for safety.
            maybeUnburrow();
        }

		// If it's supposed to be on minerals but is actually collecting gas, fix it.
		// This can happen when we stop collecting gas; the worker can be mis-assigned.
		if (workerData.getWorkerJob(worker) == WorkerData::Minerals &&
			(worker->getOrder() == BWAPI::Orders::MoveToGas ||
			 worker->getOrder() == BWAPI::Orders::WaitForGas ||
			 worker->getOrder() == BWAPI::Orders::ReturnGas))
		{
			workerData.setWorkerJob(worker, WorkerData::Idle, nullptr);
		}

		// Work around a bug that can cause building drones to go idle.
		// If there should be no builders, then ensure any idle drone is marked idle.
		if (catchIdleBuilders &&
			worker->getOrder() == BWAPI::Orders::PlayerGuard &&
			workerData.getWorkerJob(worker) == WorkerData::Build)
		{
			workerData.setWorkerJob(worker, WorkerData::Idle, nullptr);
		}

		// The worker's original job. It may change as we go!
		WorkerData::WorkerJob job = workerData.getWorkerJob(worker);

		// Idleness.
		// Order can be PlayerGuard for a drone that tries to build and fails.
		// There are other causes, e.g. being done with a job like repair.
		if ((worker->isIdle() || worker->getOrder() == BWAPI::Orders::PlayerGuard) &&
			job != WorkerData::Minerals &&
			job != WorkerData::Build &&
			job != WorkerData::Scout &&
            job != WorkerData::Posted &&
            job != WorkerData::PostedBuild)
		{
			//BWAPI::Broodwar->printf("idle worker");
			workerData.setWorkerJob(worker, WorkerData::Idle, nullptr);
		}

		else if (job == WorkerData::Gas)
		{
			BWAPI::Unit refinery = workerData.getWorkerResource(worker);

			// If the refinery is gone.
			// A missing resource depot is dealt with in handleGasWorkers().
            // Some of the conditions checked here may be impossible, but let's be thorough.
			if (!refinery ||
                !refinery->exists() ||
                refinery->getHitPoints() == 0 ||
                !refinery->getType().isRefinery() ||
                refinery->getPlayer() != the.self())
			{
				workerData.setWorkerJob(worker, WorkerData::Idle, nullptr);
			}
            // If the worker is busy mining and an enemy comes near, maybe fight it.
            else if (defendSelf(worker, workerData.getWorkerResource(worker)))
            {
                // defendSelf() does the work.
            }
            else if (maybeFleeDanger(worker))
            {
                // maybeFleeDanger() does the work.
            }
            else if (!UnitUtil::GasOrder(worker))
            {
                workerData.setWorkerJob(worker, WorkerData::Idle, nullptr);
            }
        }
		
		else if (job == WorkerData::Minerals)
		{
			// If the worker is busy mining and an enemy comes near, maybe fight it.
			if (defendSelf(worker, workerData.getWorkerResource(worker)))
			{
				// defendSelf() does the work.
			}
            else if (maybeFleeDanger(worker))
            {
                // maybeFleeDanger() does the work.
            }
            else if (
                worker->getOrder() == BWAPI::Orders::MoveToMinerals ||
				worker->getOrder() == BWAPI::Orders::WaitForMinerals)
			{
				// If the mineral patch is mined out, release the worker from its job.
				BWAPI::Unit patch = workerData.getWorkerResource(worker);
				if (!patch || !patch->exists())
				{
					workerData.setWorkerJob(worker, WorkerData::Idle, nullptr);
				}
			}
            else if (!UnitUtil::MineralOrder(worker))
			{
				// The worker is not actually mining. Release it.
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

// If appropriate, mine out any blocking mineral patches near our bases.
void WorkerManager::clearBlockingMinerals()
{
    if (the.now() % 49 != 0 ||
        the.bases.getSmallMinerals().empty() ||
        the.my.completed.count(the.selfRace().getWorker()) < 18 ||
        the.bases.baseCount(the.self()) < 2 ||
        workerData.anyUnblocker())
    {
        return;
    }

    const int radiusOfConcern = 600;

    // We'll check bases we own, and bases we reserved, and the next base we may want.
    Base * nextExpo = the.map.nextExpansion(false, true, true);
    //BWAPI::Broodwar->printf("check blocking mineral, next base %d", nextExpo ? nextExpo->getID() : -1);

    // Look for a blocking mineral that should be mined out.
    BWAPI::Unit worstBlock = nullptr;
    int closestDist = radiusOfConcern;
    for (BWAPI::Unit block : the.bases.getSmallMinerals())
    {
        if (block->getInitialTilePosition().isValid() && 0 == the.groundAttacks.at(block->getInitialTilePosition()))
        {
            for (const Base * base : the.bases.getAll())
            {
                if (base->getOwner() == the.self() ||
                    the.placer.isReserved(base->getTilePosition()) ||
                    base == nextExpo)
                {
                    int dist = base->getCenter().getApproxDistance(block->getInitialPosition());
                    if (dist < closestDist)
                    {
                        //BWAPI::Broodwar->printf("blocking mineral @ %d,%d dist %d", block->getInitialPosition().x, block->getInitialPosition().y, dist);
                        worstBlock = block;
                        closestDist = dist;
                    }
                }
            }
        }
    }

    // If we found one, choose a worker to mine it out.
    if (worstBlock)
    {
        BWAPI::Unit worker = getUnencumberedWorker(worstBlock->getInitialPosition(), INT_MAX);
        if (worker)
        {
            workerData.setWorkerJob(worker, worstBlock->getInitialTilePosition());
        }
    }
}

// Move gas workers on or off gas as necessary.
// NOTE A worker inside a refinery does not accept orders.
void WorkerManager::handleGasWorkers() 
{
	if (_collectGas)
	{
		int nBases = the.bases.completedBaseCount(the.self());

		for (const Base * base : the.bases.getAll())
		{
			BWAPI::Unit depot = base->getDepot();

			for (BWAPI::Unit geyser : base->getGeysers())
			{
				// Don't add more workers to a refinery at a base under attack (unless it is
				// the only base). Limit losses to the workers that are already there.
				if (base->getOwner() == the.self() &&
					geyser->getType().isRefinery() &&
					geyser->isCompleted() &&
					geyser->getPlayer() == the.self() &&
					UnitUtil::IsCompletedResourceDepot(depot) &&
					(!base->inWorkerDanger() || nBases == 1))
				{
					// This is a good refinery. Gather from it.
					// If too few workers are assigned, add more.
					int numAssigned = workerData.getNumAssignedWorkers(geyser);
					for (int i = 0; i < (Config::Macro::WorkersPerRefinery - numAssigned); ++i)
					{
						BWAPI::Unit gasWorker = getGasWorker(geyser);
						if (gasWorker)
						{
							workerData.setWorkerJob(gasWorker, WorkerData::Gas, geyser);
						}
						else
						{
							return;    // won't find any more, either for this refinery or others
						}
					}
				}
				else
				{
					// The refinery is gone or otherwise no good. Remove any gas workers.
					std::set<BWAPI::Unit> gasWorkers;
					workerData.getGasWorkers(gasWorkers);
                    for (BWAPI::Unit gasWorker : gasWorkers)
					{
						if (geyser == workerData.getWorkerResource(gasWorker) &&
							gasWorker->getOrder() != BWAPI::Orders::HarvestGas)  // not inside the refinery
						{
							workerData.setWorkerJob(gasWorker, WorkerData::Idle, nullptr);
						}
					}
				}
			}
		}
	}
	else
	{
		// Don't gather gas: If workers are assigned to gas anywhere, take them off.
		std::set<BWAPI::Unit> gasWorkers;
		workerData.getGasWorkers(gasWorkers);
		for (BWAPI::Unit gasWorker : gasWorkers)
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

// Is the refinery near a resource depot that it can deliver gas to?
bool WorkerManager::refineryHasDepot(BWAPI::Unit refinery)
{
	// Iterate through units, not bases, because even if the main hatchery is destroyed
	// (so the base is considered gone), a macro hatchery may be close enough.
	for (BWAPI::Unit unit : the.self()->getUnits())
	{
		if (UnitUtil::IsCompletedResourceDepot(unit) &&
			unit->getDistance(refinery) < 400)
		{
			return true;
		}
	}

	return false;
}

void WorkerManager::handleIdleWorkers() 
{
	for (BWAPI::Unit worker : workerData.getWorkers())
	{
		if (workerData.getWorkerJob(worker) == WorkerData::Idle) 
		{
            if (worker->isBurrowed())
            {
                // All burrowed workers are set to Idle.
            }
            else if (maybeFleeDanger(worker, weaponsMarginPlus))
            {
                // maybeFleeDanger does the work. Notes:
                // 1. A worker fleeing danger is set to Idle.
                // 2. An Idle worker fleeing danger has a wider safety margin.
                //    Thus working workers are reluctant to run, but run far when they do,
                //    while workers once idle try to stay safe.
            }
            else if (worker->isCarryingMinerals() || worker->isCarryingGas())
			{
				// It's carrying something, set it to hand in its cargo.
				setReturnCargoWorker(worker);         // only happens if there's a resource depot
			}
			else
			{
				// Otherwise send it to mine minerals.
				setMineralWorker(worker);             // only happens if there's a resource depot
			}
		}
	}
}

void WorkerManager::handleReturnCargoWorkers()
{
	for (BWAPI::Unit worker : workerData.getWorkers())
	{
		if (workerData.getWorkerJob(worker) == WorkerData::ReturnCargo)
		{
			// If it still needs to return cargo, return it.
			// We have to make sure it has a resource depot to return cargo to.
			BWAPI::Unit depot;
			if ((worker->isCarryingMinerals() || worker->isCarryingGas()) &&
				(depot = getAnyClosestDepot(worker)) &&
				worker->getDistance(depot) < 600)
			{
				the.micro.ReturnCargo(worker);
                makeBusy(worker);
			}
			else
			{
				// Can't return cargo. Let's be a mineral worker instead--if possible.
				setMineralWorker(worker);
			}
		}
	}
}

// Terran can assign SCVs to repair.
void WorkerManager::handleRepairWorkers()
{
    if (the.self()->getRace() != BWAPI::Races::Terran ||
        the.my.completed.count(BWAPI::UnitTypes::Terran_SCV) == 0 ||
        the.self()->minerals() == 0)
    {
        return;
    }

    size_t maxRepairers =
        1 + the.my.completed.count(BWAPI::UnitTypes::Terran_SCV) / 6;
    int nAlreadyRepairing = workerData.getNumRepairWorkers();
    if (size_t(nAlreadyRepairing) >= maxRepairers)
    {
        return;
    }
    maxRepairers -= nAlreadyRepairing;

    for (BWAPI::Unit unit : the.self()->getUnits())
    {
        if (unit->getType().isBuilding() &&
            unit->getHitPoints() < unit->getType().maxHitPoints() &&
            unit->isCompleted())
        {
            size_t nRepairers = 0;
            if (unit->getType() == BWAPI::UnitTypes::Terran_Bunker ||
                unit->getType() == BWAPI::UnitTypes::Terran_Missile_Turret)
            {
                nRepairers = std::max(size_t(1), the.info.getEnemyFireteam(unit).size());
            }
            else if (unit->getHitPoints() < unit->getType().maxHitPoints() / 2)
            {
                nRepairers = 1;
            }

            for (size_t i = nRepairers; i > 0; --i)
            {
                BWAPI::Unit repairWorker = getClosestMineralWorkerTo(unit);
                setRepairWorker(repairWorker, unit);
                maxRepairers -= 1;
                if (maxRepairers == 0)
                {
                    return;
                }
            }
        }
    }
}

// Steamhammer's mineral locking is modeled after Locutus (but different in detail).
// This implements the "wait for the previous worker to be done" part of mineral locking.
void WorkerManager::handleMineralWorkers()
{
	for (BWAPI::Unit worker : workerData.getWorkers())
	{
		if (workerData.getWorkerJob(worker) == WorkerData::Minerals)
		{
			if (worker->getOrder() == BWAPI::Orders::MoveToMinerals ||
				worker->getOrder() == BWAPI::Orders::WaitForMinerals)
			{
				BWAPI::Unit patch = workerData.getWorkerResource(worker);
				if (patch && patch->exists() && worker->getOrderTarget() != patch)
				{
					the.micro.MineMinerals(worker, patch);
                    makeBusy(worker);
				}
			}
		}
	}
}

// Workers assigned to mine out blocking minerals.
void WorkerManager::handleUnblockWorkers()
{
    for (BWAPI::Unit worker : workerData.getWorkers())
    {
        if (workerData.getWorkerJob(worker) == WorkerData::Unblock)
        {
            BWAPI::TilePosition tile = workerData.getWorkerTile(worker);
            if (worker->isCarryingMinerals())
            {
                // The blocking mineral patch must have contained minerals. Return what we mined.
                the.micro.ReturnCargo(worker);
                makeBusy(worker);
            }
            else if (!BWAPI::Broodwar->isVisible(tile))
            {
                // Move the worker into sight range of the target tile.
                the.micro.MoveNear(worker, BWAPI::Position(tile));
                makeBusy(worker);
            }
            else
            {
                BWAPI::Unitset patches =
                    BWAPI::Broodwar->getUnitsOnTile(tile, BWAPI::Filter::IsMineralField);
                if (patches.empty() || the.groundAttacks.at(tile) > 0)
                {
                    workerData.setWorkerJob(worker, WorkerData::Idle, nullptr);
                }
                else if (!worker->getTarget() || worker->getTarget()->getTilePosition() != tile)
                {
                    the.micro.RightClick(worker, *patches.begin());
                    makeBusy(worker);
                }
            }
        }
    }
}

// Workers assigned to specific posts on the map, identified by MacroLocation.
void WorkerManager::handlePostedWorkers()
{
    for (BWAPI::Unit worker : workerData.getWorkers())
    {
        if (workerData.getWorkerJob(worker) == WorkerData::Posted)
        {
            if (maybeFleeDanger(worker))
            {
                // maybeFleeDanger() does the work.
            }
            else if (worker->isCarryingMinerals() || worker->isCarryingGas())
            {
                the.micro.ReturnCargo(worker);
                makeBusy(worker);
            }
            else
            {
                BWAPI::Position pos = workerData.getWorkerPostPosition(worker);
                if (worker->getDistance(pos) > 8 * 32)
                {
                    //BWAPI::Broodwar->printf("moving posted worker %d to %d,%d", worker->getID(), pos.x, pos.y);
                    the.micro.MoveNear(worker, pos);
                    makeBusy(worker);
                }
            }
        }
    }
}

// If the worker is in danger from the enemy, try to flee.
// Exception: If the worker's destination is <= unlessWithinDistance, then ignore any danger.
// It's valid to pass -1 as the unlessWithinDistance to ignore it.
// Return true if we fled, otherwise false to continue with regular behavior.
// margin is the margin of safety from enemy weapons fire.
bool WorkerManager::maybeFleeDanger(BWAPI::Unit worker, int margin)
{
    UAB_ASSERT(worker, "Worker was null");

    if (worker->getPosition().isValid() &&          // false if we're e.g. in a refinery
        worker->canMove())
    {
        // We might be (for example) inside tank range but outside the range of a vulture which is nearer.
        // We ignore the possibility and consider only the closest enemy unit.
        BWAPI::Unit enemy = the.micro.inWeaponsDanger(worker, margin);
        if (enemy)
        {
            workerData.setWorkerJob(worker, WorkerData::Idle, nullptr);
            if (worker->canBurrow() && !UnitUtil::EnemyDetectorInRange(worker))
            {
                the.micro.Burrow(worker);
                burrowedForSafety[worker] = the.now();
                //BWAPI::Broodwar->printf("worker %d burrows for safety", worker->getID());
            }
            else
            {
                the.micro.fleeEnemy(worker, enemy);
                //BWAPI::Broodwar->printf("worker %d flees to %d,%d", worker->getID(), destination.x, destination.y);
            }
            makeBusy(worker);
            return true;
        }
    }

    return false;
}

// Unburrow any burrowed drones in burrowedForSafety[] that it appears safe to.
void WorkerManager::maybeUnburrow()
{
    // First clear any workers that should not be in burrowedForSafety[].
    for (auto it = burrowedForSafety.begin(); it != burrowedForSafety.end(); )
    {
        BWAPI::Unit worker = it->first;
        if (
            // Drop workers that have died or been mind controlled.
            !UnitUtil::IsValidUnit(worker) ||
            // A worker can be unburrowed by force if the enemy detects and attacks it.
            !worker->isBurrowed() && !worker->getOrder() == BWAPI::Orders::Burrowing ||
            worker->getOrder() == BWAPI::Orders::Unburrowing ||
            // If the worker was irradiated after burrowing, it should stay burrowed. Drop it.
            worker->isIrradiated())
        {
            it = burrowedForSafety.erase(it);
        }
        else
        {
            if (the.micro.inWeaponsDanger(worker, weaponsMarginPlus))
            {
                // It's still in danger. Update the time so it doesn't unburrow too soon.
                it->second = the.now();
            }
            else
            {
                // It's safe to unburrow if at least this many frames have passed since the danger left.
                int t = it->second;
                if (the.now() - t > 3 * 24 && !inIrradiateDanger(worker))
                {
                    the.micro.Unburrow(worker);
                    // It will be dropped from burrowedForSafety[] on the next call.
                    //BWAPI::Broodwar->printf("worker %d unburrowed", worker->getID());
                }
            }
            ++it;
        }
    }
}

// Is the worker either irradiated or dangerously near an irradiated unit?
// Used in deciding whether to burrow or unburrow, so ignore the unit's burrow status.
bool WorkerManager::inIrradiateDanger(BWAPI::Unit worker) const
{
    // Only if it is affected by irradiate. Probes are robotic.
    if (worker->getType().isOrganic())      // SCV or drone
    {
        if (worker->isIrradiated())
        {
            return true;
        }

        // In danger from a nearby irradiated unit?
        // Only worry if the worker is already damaged, to prevent overreaction.
        if (worker->getHitPoints() < worker->getType().maxHitPoints())
        {
            BWAPI::Unit irrad = BWAPI::Broodwar->getClosestUnit(
                worker->getPosition(),
                BWAPI::Filter::IsIrradiated && !BWAPI::Filter::IsBurrowed,
                3 * 32     // irradiate splash range is 2 tiles, but be cautious
            );
            if (irrad)
            {
                return true;
            }
        }
    }

    return false;
}

// Used for worker self-defense.
// Only include enemy units within 64 pixels that can be targeted by workers
// and are not moving or are stuck and moving randomly to dislodge themselves.
BWAPI::Unit WorkerManager::findEnemyTargetForWorker(BWAPI::Unit worker) const
{
    UAB_ASSERT(worker, "Worker was null");

	BWAPI::Unit closestUnit = nullptr;
	int closestDist = 65;         // ignore anything farther away

	for (BWAPI::Unit unit : the.enemy()->getUnits())
	{
		int dist;

		if ((!unit->isMoving() || unit->isStuck()) &&
            !unit->isFlying() &&
            unit->getPosition().isValid() &&
            (dist = unit->getDistance(worker)) < closestDist &&
			unit->isCompleted() &&
			unit->isDetected())
		{
			closestUnit = unit;
			closestDist = dist;
		}
	}

	return closestUnit;
}

// The worker is defending itself and wants to mineral walk out of trouble.
// Find a suitable mineral patch, if any.
BWAPI::Unit WorkerManager::findEscapeMinerals(BWAPI::Unit worker) const
{
	BWAPI::Unit farthestMinerals = nullptr;
	int farthestDist = 64;           // ignore anything closer

    // Check all visible mineral patches.
	for (BWAPI::Unit unit : BWAPI::Broodwar->getNeutralUnits())
	{
		int dist;

		if (unit->getType() == BWAPI::UnitTypes::Resource_Mineral_Field &&
			(dist = worker->getDistance(unit)) < 400 &&
			dist > farthestDist)
		{
			farthestMinerals = unit;
			farthestDist = dist;
		}
	}

	return farthestMinerals;
}

// If appropriate, order the worker to defend itself.
// The "resource" is workerData.getWorkerResource(worker), passed in so it needn't be looked up again.
// Return whether self-defense was undertaken.
bool WorkerManager::defendSelf(BWAPI::Unit worker, BWAPI::Unit resource)
{
	// We want to defend ourselves if we are near home and we have a close enemy (the target).
	BWAPI::Unit target = findEnemyTargetForWorker(worker);

	if (resource && worker->getDistance(resource) < 200 && target)
	{
		int enemyWeaponRange = UnitUtil::GetAttackRange(target, worker);
		bool flee =
			enemyWeaponRange > 0 &&          // don't flee if the target can't hurt us
			enemyWeaponRange <= 32 &&        // no use to flee if the target has range
			worker->getHitPoints() <= 16;    // reasonable value for the most common attackers
			// worker->getHitPoints() <= UnitUtil::GetWeaponDamageToWorker(target);

		// TODO It's not helping. Reaction time is too slow.
		flee = false;

		if (flee)
		{
			// 1. We're in danger of dying. Flee by mineral walk.
			BWAPI::Unit escapeMinerals = findEscapeMinerals(worker);
			if (escapeMinerals)
			{
				//BWAPI::Broodwar->printf("%d fleeing to %d", worker->getID(), escapeMinerals->getID());
				workerData.setWorkerJob(worker, WorkerData::Minerals, escapeMinerals);
				return true;
			}
			else
			{
				//BWAPI::Broodwar->printf("%d cannot flee", worker->getID());
			}
		}

		// 2. We do not want to or are not able to run away. Fight.
		the.micro.CatchAndAttackUnit(worker, target);
        makeBusy(worker);
		return true;
	}

	return false;
}

BWAPI::Unit WorkerManager::getClosestMineralWorkerTo(BWAPI::Unit enemyUnit)
{
    UAB_ASSERT(enemyUnit, "Unit was null");

    BWAPI::Unit closestMineralWorker = nullptr;
    int closestDist = 100000;

	// Former closest worker may have died or (if zerg) morphed into a building.
	if (UnitUtil::IsValidUnit(previousClosestWorker) && previousClosestWorker->getType().isWorker())
	{
		return previousClosestWorker;
    }

	for (BWAPI::Unit worker : workerData.getWorkers())
	{
        if (isFree(worker)) 
		{
			int dist = worker->getDistance(enemyUnit);
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
	for (BWAPI::Unit worker : workerData.getWorkers())
	{
        if (workerData.getWorkerJob(worker) == WorkerData::Scout) 
		{
			return worker;
		}
	}

    return nullptr;
}

// Send the worker to mine minerals at the closest resource depot, if any.
void WorkerManager::setMineralWorker(BWAPI::Unit unit)
{
    UAB_ASSERT(unit, "Unit was null");

	BWAPI::Unit depot = getClosestNonFullDepot(unit);

	if (depot)
	{
		workerData.setWorkerJob(unit, WorkerData::Minerals, depot);
	}
	else
	{
		//BWAPI::Broodwar->printf("No depot for mineral worker");
	}
}

// Worker is carrying minerals or gas. Tell it to hand them in.
void WorkerManager::setReturnCargoWorker(BWAPI::Unit unit)
{
	UAB_ASSERT(unit, "Unit was null");

	BWAPI::Unit depot = getAnyClosestDepot(unit);

	if (depot)
	{
		workerData.setWorkerJob(unit, WorkerData::ReturnCargo, depot);
	}
	else
	{
		// BWAPI::Broodwar->printf("No depot to accept return cargo");
	}
}

// Get the closest resource depot with no other consideration.
// TODO iterate through bases, not units
BWAPI::Unit WorkerManager::getAnyClosestDepot(BWAPI::Unit worker)
{
	UAB_ASSERT(worker, "Worker was null");

	BWAPI::Unit closestDepot = nullptr;
	int closestDistance = 0;

	for (BWAPI::Unit unit : the.self()->getUnits())
	{
		UAB_ASSERT(unit, "Unit was null");

		if (UnitUtil::IsCompletedResourceDepot(unit))
		{
			int distance = unit->getDistance(worker);
			if (!closestDepot || distance < closestDistance)
			{
				closestDepot = unit;
				closestDistance = distance;
			}
		}
	}

	return closestDepot;
}

// Get the closest resource depot that can accept another mineral worker.
// The depot at a base under attack is treated as unavailable, unless it is the only base to mine at.
BWAPI::Unit WorkerManager::getClosestNonFullDepot(BWAPI::Unit worker)
{
	UAB_ASSERT(worker && worker->getType().isWorker(), "Worker was null");

	BWAPI::Unit closestDepot = nullptr;
	int closestDistance = INT_MAX;

	int nBases = the.bases.completedBaseCount(the.self());

	for (const Base * base : the.bases.getAll())
	{
		BWAPI::Unit depot = base->getDepot();

        if (depot)
        {
            const int distance = depot->getDistance(worker);                       // air distance
            const int travelTime = int(distance / worker->getType().topSpeed());   // lower limit

            if (base->getOwner() == the.self() &&
                distance < closestDistance &&
                UnitUtil::IsNearlyCompletedResourceDepot(depot, travelTime) &&     // should be complete when workers arrive
                (!base->inWorkerDanger() || nBases == 1) &&
                !workerData.depotIsFull(depot))
            {
                closestDepot = depot;
                closestDistance = distance;
            }
        }
	}

	return closestDepot;
}

// other managers that need workers call this when they're done with a unit
void WorkerManager::finishedWithWorker(BWAPI::Unit unit) 
{
	UAB_ASSERT(unit, "Unit was null");

    if (workerData.getWorkerJob(unit) == WorkerData::PostedBuild)
    {
        workerData.resetWorkerPost(unit, WorkerData::Posted);
    }
    else
    {
        workerData.setWorkerJob(unit, WorkerData::Idle, nullptr);
    }
}

// Find a worker to be reassigned to gas duty.
// Transferring across the map is bad. Try not to do that.
BWAPI::Unit WorkerManager::getGasWorker(BWAPI::Unit refinery)
{
	UAB_ASSERT(refinery, "Refinery was null");

	BWAPI::Unit closestWorker = nullptr;
	int closestDistance = 0;
    bool workerInBase = false;

	for (BWAPI::Unit unit : workerData.getWorkers())
	{
		UAB_ASSERT(unit, "Unit was null");

		if (isFree(unit))
		{
            int distance = unit->getDistance(refinery);
            if (distance <= thisBaseRange)
            {
                workerInBase = true;
            }

            // Don't waste minerals. It's OK (though unlikely) to already be carrying gas.
			if (unit->isCarryingMinerals() ||                       // doesn't have minerals and
				unit->getOrder() == BWAPI::Orders::MiningMinerals)  // isn't about to get them
			{
				continue;
			}

			if (!closestWorker || distance < closestDistance)
			{
				closestWorker = unit;
				closestDistance = distance;
			}
		}
	}

    if (closestWorker && closestDistance <= thisBaseRange)
    {
        // This owrker is in the base and is free.
        return closestWorker;
    }

    if (workerInBase)
    {
        // There is a free worker in the base, but it has minerals
        // We'll wait until it returns the minerals.
        return nullptr;
    }

    // There are no free workers in the base. We may transfer one from elsewhere.
	return closestWorker;
}

// Return the closest free worker in the given range.
BWAPI::Unit	WorkerManager::getAnyWorker(const BWAPI::Position & pos, int range)
{
	BWAPI::Unit closestWorker = nullptr;
	int closestDistance = range;

    for (BWAPI::Unit worker : workerData.getWorkers())
	{
        if (isFree(worker))
		{
            int distance = worker->getDistance(pos);
			if (distance < closestDistance)
			{
                closestWorker = worker;
				closestDistance = distance;
			}
		}
	}
	return closestWorker;
}

// Return the closest free worker in the given range which is not carrying resources.
BWAPI::Unit	WorkerManager::getUnencumberedWorker(const BWAPI::Position & pos, int range)
{
	BWAPI::Unit closestWorker = nullptr;
	int closestDistance = range;

    for (BWAPI::Unit worker : workerData.getWorkers())
	{
        if (isFree(worker) &&
            !worker->isCarryingMinerals() &&
            !worker->isCarryingGas() &&
            worker->getOrder() != BWAPI::Orders::MiningMinerals)
		{
            int distance = worker->getDistance(pos);
			if (distance < closestDistance)
			{
                closestWorker = worker;
				closestDistance = distance;
			}
		}
	}
	return closestWorker;
}

// Return the posted worker whose post is closest to the given position,
// if any is reasonably close.
BWAPI::Unit WorkerManager::getPostedWorker(const BWAPI::Position & pos)
{
    BWAPI::Unit closestWorker = nullptr;
    int closestDistance = 14 * 32;      // be closer than this

    for (BWAPI::Unit worker : workerData.getWorkers())
    {
        if (workerData.getWorkerJob(worker) == WorkerData::Posted)
        {
            BWAPI::Position post = workerData.getWorkerPostPosition(worker);
            int distance = pos.getApproxDistance(post);
            //BWAPI::Broodwar->printf("posted %d post %d,%d dist %d", worker->getID(), post.x, post.y, distance);
            if (distance < closestDistance)
            {
                closestWorker = worker;
                closestDistance = distance;
            }
        }
    }
    return closestWorker;
}

// There is at least one posted worker which is busy building something.
// Call this only after getPostedWorker() fails to find any free posted worker,
// to decide whether to wait for a busy posted worker to become free again.
bool WorkerManager::postedWorkerBusy(const BWAPI::Position & pos)
{
    BWAPI::Unit closestWorker = nullptr;
    int closestDistance = 12 * 32;      // be closer than this

    for (BWAPI::Unit worker : workerData.getWorkers())
    {
        if (workerData.getWorkerJob(worker) == WorkerData::PostedBuild)
        {
            BWAPI::Position post = workerData.getWorkerPostPosition(worker);
            int distance = pos.getApproxDistance(post);
            if (distance < closestDistance)
            {
                return true;
            }
        }
    }
    return false;
}

// Get a builder for BuildingManager, based on the building's final or desired position.
// It is the caller's job to setJobAsBuilder() (or not, as needed).
// Reject workers carrying resources, unless we are protoss. We can wait.
BWAPI::Unit WorkerManager::getBuilder(const Building & b)
{
	// 1. If this is a gas steal, return the scout worker, or null if none.
	if (b.isGasSteal)
	{
		return ScoutManager::Instance().getWorkerScout();
	}

    // 2. Return a free worker which is posted near the target position.
    const BWAPI::Position pos(
        b.finalPosition.isValid() ? b.finalPosition : b.desiredPosition
    );
    UAB_ASSERT(pos.isValid(), "bad position");

    BWAPI::Unit builder = getPostedWorker(pos);
    if (builder)
    {
        //BWAPI::Broodwar->printf("getBuilder gets posted %d for %s @ %d,%d",
        //    builder->getID(), UnitTypeName(b.type).c_str(), pos.x, pos.y);
        return builder;
    }

    // 3. If there is a busy worker posted near the position, wait for now.
    // We'll assign that worker when it becomes free (or move on if it dies).
    if (postedWorkerBusy(pos))
    {
        return nullptr;
    }

	// 4. Return a worker which is close enough to be "at this base".
	builder = getUnencumberedWorker(pos, thisBaseRange);
    if (!builder && the.selfRace() == BWAPI::Races::Protoss)
    {
        builder = getAnyWorker(pos, thisBaseRange);
    }
    if (builder)
    {
        //BWAPI::Broodwar->printf("getBuilder gets nearby %d for %s @ %d,%d",
        //    builder->getID(), UnitTypeName(b.type).c_str(), pos.x, pos.y);
        return builder;
    }

	// 5. If any worker is at this base, we're done for now.
	// We'll wait for the worker to return its cargo and select it on a later frame.
	if (getAnyWorker(pos, thisBaseRange))
	{
		return nullptr;
	}

	// 6. This base seems to be barren of workers. Return a worker which is at any base.
    return getUnencumberedWorker(pos, INT_MAX);
}

// Called by outsiders to assign a worker to a construction job.
void WorkerManager::setBuildWorker(BWAPI::Unit worker)
{
	UAB_ASSERT(worker, "Worker was null");

    if (workerData.getWorkerJob(worker) == WorkerData::Build ||
        workerData.getWorkerJob(worker) == WorkerData::PostedBuild)
    {
        // This is already a build worker. Do nothing.
    }
    else if (workerData.getWorkerJob(worker) == WorkerData::Posted)
    {
        workerData.resetWorkerPost(worker, WorkerData::PostedBuild);
    }
    else
    {
        workerData.setWorkerJob(worker, WorkerData::Build);
    }
}

void WorkerManager::setScoutWorker(BWAPI::Unit worker)
{
	UAB_ASSERT(worker, "Worker was null");

	workerData.setWorkerJob(worker, WorkerData::Scout, nullptr);
}

// Will we have the required resources by the time a worker can travel the given distance?
bool WorkerManager::willHaveResources(int mineralsRequired, int gasRequired, double distance)
{
	// if we don't require anything, we will have it
	if (mineralsRequired <= 0 && gasRequired <= 0)
	{
		return true;
	}

	double speed = the.self()->getRace().getWorker().topSpeed();

	// how many frames it will take us to move to the building location
	// add a little to account for worker getting stuck. better early than late
	double framesToMove = (distance / speed) + 24;

	// magic numbers to predict income rates
	double mineralRate = getNumMineralWorkers() * 0.045;
	double gasRate     = getNumGasWorkers() * 0.07;

	// calculate if we will have enough by the time the worker gets there
	return
		mineralRate * framesToMove >= mineralsRequired &&
		gasRate * framesToMove >= gasRequired;
}

void WorkerManager::setCombatWorker(BWAPI::Unit worker)
{
	UAB_ASSERT(worker, "Worker was null");

	workerData.setWorkerJob(worker, WorkerData::Combat, nullptr);
}

// Post the given worker to the given macro location.
void WorkerManager::postGivenWorker(BWAPI::Unit worker, MacroLocation loc)
{
    //BWAPI::Broodwar->printf("posting worker %d", worker->getID());
    workerData.setWorkerPost(worker, loc);
}

// Post the closest free worker to the given macro location.
// In case of failure (which should be rare), do nothing.
BWAPI::Unit WorkerManager::postWorker(MacroLocation loc)
{
    BWAPI::Position pos = the.placer.getMacroLocationPos(loc);
    BWAPI::Unit worker = getUnencumberedWorker(pos, INT_MAX);
    if (!worker)
    {
        worker = getAnyWorker(pos, INT_MAX);
    }

    if (worker)
    {
        postGivenWorker(worker, loc);
    }
    return worker;
}

// Release all workers at the given macro location from their posts.
// If the location is Anywhere (the default), release all posted workers.
void WorkerManager::unpostWorkers(MacroLocation loc)
{
    for (BWAPI::Unit worker : workerData.getWorkers())
    {
        if (loc == MacroLocation::Anywhere || loc == workerData.getWorkerPostLocation(worker))
        {
            WorkerData::WorkerJob job = workerData.getWorkerJob(worker);
            if (job == WorkerData::Posted)
            {
                //BWAPI::Broodwar->printf("unposting worker %d", worker->getID());
                workerData.setWorkerJob(worker, WorkerData::Idle, nullptr);
            }
            else if (job == WorkerData::PostedBuild)
            {
                //BWAPI::Broodwar->printf("unposting build worker %d", worker->getID());
                workerData.setWorkerJob(worker, WorkerData::Build);
            }
        }
    }
}

void WorkerManager::onUnitMorph(BWAPI::Unit unit)
{
	UAB_ASSERT(unit, "Unit was null");

	// if something morphs into a worker, add it
	if (unit->getType().isWorker() && unit->getPlayer() == the.self() && unit->getHitPoints() > 0)
	{
		workerData.addWorker(unit);
	}

	// if something morphs into a building, was it a drone?
	if (unit->getType().isBuilding() && unit->getPlayer() == the.self() && unit->getPlayer()->getRace() == BWAPI::Races::Zerg)
	{
		workerData.workerDestroyed(unit);
	}
}

void WorkerManager::onUnitShow(BWAPI::Unit unit)
{
	UAB_ASSERT(unit && unit->exists(), "bad unit");

	// if something morphs into a worker, add it
	if (unit->getType().isWorker() && unit->getPlayer() == the.self() && unit->getHitPoints() > 0)
	{
		workerData.addWorker(unit);
	}
}

void WorkerManager::onUnitDestroy(BWAPI::Unit unit)
{
    UAB_ASSERT(unit, "Unit was null");

    if (unit->getType().isResourceDepot() && unit->getPlayer() == the.self())
    {
        workerData.removeDepot(unit);
    }

    if (unit->getType().isWorker() && unit->getPlayer() == the.self())
    {
        workerData.workerDestroyed(unit);
    }

    if (unit->getType() == BWAPI::UnitTypes::Resource_Mineral_Field)
    {
        rebalanceWorkers();
    }
}

// Possibly transfer workers to other bases.
// Well, mark them idle. Idle workers will be put to work if there is a place for them.
void WorkerManager::rebalanceWorkers()
{
    for (BWAPI::Unit worker : workerData.getWorkers())
	{
		if (!workerData.getWorkerJob(worker) == WorkerData::Minerals)
		{
			continue;
		}

		BWAPI::Unit depot = workerData.getWorkerDepot(worker);

		if (depot && workerData.depotIsFull(depot))
		{
			// BWAPI::Broodwar->printf("full rebalance");
			workerData.setWorkerJob(worker, WorkerData::Idle, nullptr);
		}
		else if (!depot)
		{
			// BWAPI::Broodwar->printf("resource depot gone");
			workerData.setWorkerJob(worker, WorkerData::Idle, nullptr);
		}
	}
}

void WorkerManager::drawWorkerInformation() 
{
    if (!Config::Debug::DrawWorkerInfo)
    {
        return;
    }

	for (BWAPI::Unit worker : workerData.getWorkers())
	{
        BWAPI::Position pos = worker->getPosition();
        if (pos.isValid())
        {
            int color = isBusy(worker) ? orange : cyan;
            BWAPI::Broodwar->drawTextMap(pos.x - 4, pos.y - 7, "%c%c", color, workerData.getJobCode(worker));

            BWAPI::Position target = worker->getTargetPosition();
            if (target.isValid())
            {
                BWAPI::Broodwar->drawLineMap(pos, target, BWAPI::Colors::Brown);
            }

            BWAPI::Unit depot = workerData.getWorkerDepot(worker);
            if (depot)
            {
                BWAPI::Broodwar->drawLineMap(pos, depot->getPosition() /*+ BWAPI::Position(32, 24)*/, BWAPI::Colors::Grey);
            }
        }
	}
}

bool WorkerManager::isFree(BWAPI::Unit worker)
{
    UAB_ASSERT(worker, "Worker was null");

	WorkerData::WorkerJob job = workerData.getWorkerJob(worker);
	return
        (job == WorkerData::Minerals || job == WorkerData::Idle && !worker->isBurrowed()) &&
        !isBusy(worker) &&
        worker->isCompleted();
}

bool WorkerManager::isWorkerScout(BWAPI::Unit worker)
{
	UAB_ASSERT(worker, "Worker was null");

	return workerData.getWorkerJob(worker) == WorkerData::Scout;
}

bool WorkerManager::isCombatWorker(BWAPI::Unit worker)
{
    UAB_ASSERT(worker, "Worker was null");

	return workerData.getWorkerJob(worker) == WorkerData::Combat;
}

bool WorkerManager::isBuilder(BWAPI::Unit worker)
{
    UAB_ASSERT(worker, "Worker was null");

	return workerData.getWorkerJob(worker) == WorkerData::Build;
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

int WorkerManager::getNumPostedWorkers() const
{
    return workerData.getNumPostedWorkers();
}

// The largest number of workers that it is efficient to have right now.
// Does not take into account possible preparations for future expansions.
// May not exceed Config::Macro::AbsoluteMaxWorkers.
int WorkerManager::getMaxWorkers() const
{
	int patches = the.bases.mineralPatchCount();
	int refineries, geysers;
	the.bases.gasCounts(refineries, geysers);

	// Never let the max number of workers fall to 0!
	// Set aside 1 for future opportunities.
	return std::min(
			Config::Macro::AbsoluteMaxWorkers,
			1 + int(std::round(Config::Macro::WorkersPerPatch * patches + Config::Macro::WorkersPerRefinery * refineries))
		);
}

// The number of workers assigned to this resource depot to mine minerals, or to this refinery to mine gas.
int WorkerManager::getNumWorkers(BWAPI::Unit jobUnit) const
{
	return workerData.getNumAssignedWorkers(jobUnit);
}
