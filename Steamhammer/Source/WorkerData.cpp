#include "WorkerData.h"
#include "MacroAct.h"
#include "Micro.h"
#include "The.h"

using namespace UAlbertaBot;

WorkerData::WorkerPost::WorkerPost()
    : location(MacroLocation::Anywhere)
    , position(the.placer.getMacroLocationPos(MacroLocation::Anywhere))
{
}

WorkerData::WorkerPost::WorkerPost(MacroLocation loc)
    : location(loc)
    , position(the.placer.getMacroLocationPos(loc))
{
}

// -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- --

WorkerData::WorkerData()
{
    for (BWAPI::Unit unit : BWAPI::Broodwar->getAllUnits())
	{
		if (unit->getType() == BWAPI::UnitTypes::Resource_Mineral_Field)
		{
            workersOnMineralPatch[unit] = 0;
		}
	}
}

void WorkerData::workerDestroyed(BWAPI::Unit unit)
{
	if (!unit) { return; }

	clearPreviousJob(unit);
	workers.erase(unit);
}

void WorkerData::addWorker(BWAPI::Unit unit)
{
	if (!unit || !unit->exists()) { return; }

	workers.insert(unit);
	workerJobMap[unit] = Default;
}

void WorkerData::addWorker(BWAPI::Unit unit, WorkerJob job, BWAPI::Unit jobUnit)
{
	if (!unit || !unit->exists() || !jobUnit || !jobUnit->exists()) { return; }

	assert(workers.find(unit) == workers.end());

	workers.insert(unit);
	setWorkerJob(unit, job, jobUnit);
}

void WorkerData::addWorker(BWAPI::Unit unit, WorkerJob job, BWAPI::UnitType jobUnitType)
{
	if (!unit || !unit->exists()) { return; }

	assert(workers.find(unit) == workers.end());
	workers.insert(unit);
	setWorkerJob(unit, job);
}

void WorkerData::removeDepot(BWAPI::Unit unit)
{	
	if (!unit) { return; }

	depotWorkerCount.erase(unit);

	// re-balance workers in here
	for (BWAPI::Unit worker : workers)
	{
		// if a worker was working at this depot
		if (workerDepotMap[worker] == unit)
		{
			setWorkerJob(worker, Idle, nullptr);
		}
	}
}

void WorkerData::addToMineralPatch(BWAPI::Unit unit, int num)
{
    if (workersOnMineralPatch.find(unit) == workersOnMineralPatch.end())
    {
        workersOnMineralPatch[unit] = num;
    }
    else
    {
        workersOnMineralPatch[unit] += num;
    }
}

void WorkerData::setWorkerJob(BWAPI::Unit unit, WorkerJob job, BWAPI::Unit jobUnit)
{
	if (!unit) { return; }

	//BWAPI::Broodwar->printf("set worker job (%d %c)", unit->getID(), getJobCode(job));

	clearPreviousJob(unit);
	workerJobMap[unit] = job;

	if (job == Minerals)
	{
        depotWorkerCount[jobUnit] += 1;

		workerDepotMap[unit] = jobUnit;

        BWAPI::Unit mineralToMine = getMineralToMine(unit);
        if (!mineralToMine)
        {
            // The worker cannot be assigned. Give up and do nothing more.
            BWAPI::Broodwar->printf("no mineral to mine for worker %d", unit->getID());
            return;
        }
        workerMineralAssignment[unit] = mineralToMine;
        addToMineralPatch(mineralToMine, 1);

        if (mineralToMine->isVisible())
        {
            // Start mining.
            the.micro.RightClick(unit, mineralToMine);
        }
        else
        {
            // First we have to move close enough to see it.
            the.micro.MoveNear(unit, mineralToMine->getInitialPosition());
        }
	}
	else if (job == Gas)
	{
		refineryWorkerCount[jobUnit] += 1;

		workerRefineryMap[unit] = jobUnit;

		// Start harvesting.
		the.micro.RightClick(unit, jobUnit);
	}
    else if (job == Repair)
    {
        UAB_ASSERT(unit->getType() == BWAPI::UnitTypes::Terran_SCV, "bad job");

        workerRepairMap[unit] = jobUnit;

        if (!unit->isRepairing())
        {
            the.micro.Repair(unit, jobUnit);
        }
    }
}

// Give the worker a Build job.
void WorkerData::setWorkerJob(BWAPI::Unit unit, WorkerJob job)
{
	if (!unit) { return; }
    UAB_ASSERT(job == Build, "bad job");

    // BWAPI::Broodwar->printf("Setting worker job to build");

    clearPreviousJob(unit);
	workerJobMap[unit] = job;
}

// Give the worker an Unblock job to mine out blocking minerals.
void WorkerData::setWorkerJob(BWAPI::Unit unit, const BWAPI::TilePosition & tile)
{
    if (!unit) { return; }

    //BWAPI::Broodwar->printf("assigning worker to unblock");

    clearPreviousJob(unit);
    workerJobMap[unit] = Unblock;
    workerUnblockMap[unit] = tile;
}

// Post the worker: Give it a Posted job.
// Calculate the map position from the macro location and remember it.
void WorkerData::setWorkerPost(BWAPI::Unit unit, MacroLocation loc)
{
    if (!unit) { return; }

    // BWAPI::Broodwar->printf("Posting worker to location");

    clearPreviousJob(unit);
    workerJobMap[unit] = Posted;
    workerPostMap[unit] = WorkerData::WorkerPost(loc);
}

// Give it a Posted or BuildPosted job without updating the map position.
void WorkerData::resetWorkerPost(BWAPI::Unit unit, WorkerJob job)
{
    if (!unit) { return; }

    UAB_ASSERT(job == Posted || job == PostedBuild, "bad job");
    UAB_ASSERT(workerJobMap[unit] == Posted || workerJobMap[unit] == PostedBuild, "bad job");

    workerJobMap[unit] = job;
    // Do not update the worker post map. It stays the same.
}

void WorkerData::clearPreviousJob(BWAPI::Unit unit)
{
	if (!unit) { return; }

	WorkerJob previousJob = getWorkerJob(unit);

	if (previousJob == Minerals)
	{
		depotWorkerCount[workerDepotMap[unit]] -= 1;

		workerDepotMap.erase(unit);

        // remove a worker from this unit's assigned mineral patch
        addToMineralPatch(workerMineralAssignment[unit], -1);

        // erase the association from the map
        workerMineralAssignment.erase(unit);
	}
	else if (previousJob == Gas)
	{
		refineryWorkerCount[workerRefineryMap[unit]] -= 1;
		workerRefineryMap.erase(unit);
	}
    else if (previousJob == Repair)
	{
		workerRepairMap.erase(unit);
	}
    else if (previousJob == Unblock)
    {
        workerUnblockMap.erase(unit);
    }
    else if (previousJob == Posted || previousJob == PostedBuild)
    {
        workerPostMap.erase(unit);
    }

	workerJobMap.erase(unit);
}

int WorkerData::getNumWorkers() const
{
	return workers.size();
}

int WorkerData::getNumMineralWorkers() const
{
	size_t num = 0;
	for (BWAPI::Unit unit : workers)
	{
		if (workerJobMap.at(unit) == WorkerData::Minerals)
		{
			num++;
		}
	}
	return num;
}

int WorkerData::getNumGasWorkers() const
{
	size_t num = 0;
    for (BWAPI::Unit unit : workers)
	{
		if (workerJobMap.at(unit) == WorkerData::Gas)
		{
			num++;
		}
	}
	return num;
}

int WorkerData::getNumReturnCargoWorkers() const
{
	size_t num = 0;
    for (BWAPI::Unit unit : workers)
	{
		if (workerJobMap.at(unit) == WorkerData::ReturnCargo)
		{
			num++;
		}
	}
	return num;
}

int WorkerData::getNumCombatWorkers() const
{
	size_t num = 0;
    for (BWAPI::Unit unit : workers)
	{
		if (workerJobMap.at(unit) == WorkerData::Combat)
		{
			num++;
		}
	}
	return num;
}

int WorkerData::getNumRepairWorkers() const
{
    size_t num = 0;
    for (BWAPI::Unit unit : workers)
    {
        if (workerJobMap.at(unit) == WorkerData::Repair)
        {
            num++;
        }
    }
    return num;
}

int WorkerData::getNumIdleWorkers() const
{
	size_t num = 0;
    for (BWAPI::Unit unit : workers)
	{
		if (workerJobMap.at(unit) == WorkerData::Idle)
		{
			num++;
		}
	}
	return num;
}

int WorkerData::getNumPostedWorkers() const
{
    return workerPostMap.size();
}

bool WorkerData::anyUnblocker() const
{
    for (BWAPI::Unit unit : workers)
    {
        if (workerJobMap.at(unit) == WorkerData::Unblock)
        {
            return true;
        }
    }
    return false;
}

enum WorkerData::WorkerJob WorkerData::getWorkerJob(BWAPI::Unit unit)
{
	if (!unit) { return Default; }

	std::map<BWAPI::Unit, enum WorkerJob>::iterator it = workerJobMap.find(unit);

	if (it != workerJobMap.end())
	{
		return it->second;
	}

	return Default;
}

// No more workers are needed for full mineral mining.
bool WorkerData::depotIsFull(BWAPI::Unit depot)
{
	if (!depot) { return false; }

	int assignedWorkers = getNumAssignedWorkers(depot);
	int mineralsNearDepot = getMineralsNearDepot(depot);

	// Mineral locking ensures that 2 workers per mineral patch are always enough (on normal maps).
	// The configuration file may specify a different limit.
	return
		assignedWorkers >= 2 * mineralsNearDepot ||
		assignedWorkers >= int (Config::Macro::WorkersPerPatch * mineralsNearDepot + 0.5);
}

BWAPI::Unitset WorkerData::getMineralPatchesNearDepot(BWAPI::Unit depot)
{
    BWAPI::Unitset mineralsNearDepot;

    int radius = 300;

    // NOTE getNeutralUnits() returns visible units only. Visibility is needed for the distance check to work.
    for (BWAPI::Unit unit : BWAPI::Broodwar->getNeutralUnits())
	{
		if (unit->getType() == BWAPI::UnitTypes::Resource_Mineral_Field && unit->getDistance(depot) < radius)
		{
            mineralsNearDepot.insert(unit);
		}
	}

    // If we didn't find any, then include visible mineral patches everywhere on the map.
    if (mineralsNearDepot.empty())
    {
        for (BWAPI::Unit unit : BWAPI::Broodwar->getNeutralUnits())
	    {
		    if (unit->getType() == BWAPI::UnitTypes::Resource_Mineral_Field)
		    {
                mineralsNearDepot.insert(unit);
		    }
	    }
    }

    return mineralsNearDepot;
}

int WorkerData::getMineralsNearDepot(BWAPI::Unit depot)
{
	if (!depot) { return 0; }

	int mineralsNearDepot = 0;

    for (BWAPI::Unit unit : BWAPI::Broodwar->getAllUnits())
	{
		if ((unit->getType() == BWAPI::UnitTypes::Resource_Mineral_Field) && unit->getDistance(depot) < 200)
		{
			mineralsNearDepot++;
		}
	}

	return mineralsNearDepot;
}

BWAPI::Unit WorkerData::getWorkerResource(BWAPI::Unit unit)
{
	if (!unit) { return nullptr; }

	// create the iterator
	std::map<BWAPI::Unit, BWAPI::Unit>::iterator it;
	
	// if the worker is mining, set the iterator to the mineral map
	if (getWorkerJob(unit) == Minerals)
	{
		it = workerMineralAssignment.find(unit);
		if (it != workerMineralAssignment.end())
		{
			return it->second;
		}	
	}
	else if (getWorkerJob(unit) == Gas)
	{
		it = workerRefineryMap.find(unit);
		if (it != workerRefineryMap.end())
		{
			return it->second;
		}	
	}

	return nullptr;
}

BWAPI::Unit WorkerData::getMineralToMine(BWAPI::Unit worker)
{
	if (!worker) { return nullptr; }

	// get the depot associated with this unit
	BWAPI::Unit depot = getWorkerDepot(worker);
	BWAPI::Unit bestMineral = nullptr;
	int bestDist = INT_MAX;
    int bestNumAssigned = 1;		// mineral locking implies <= 2 workers per patch

	if (depot)
	{
        BWAPI::Unitset mineralPatches = getMineralPatchesNearDepot(depot);

        for (BWAPI::Unit mineral : mineralPatches)
		{
			int dist = mineral->getDistance(depot);
			int numAssigned = workersOnMineralPatch[mineral];

			if (numAssigned < bestNumAssigned ||
				numAssigned == bestNumAssigned && dist < bestDist)
			{
				bestMineral = mineral;
				bestDist = dist;
				bestNumAssigned = numAssigned;
			}
		}
	}

	return bestMineral;
}

BWAPI::Unit WorkerData::getWorkerRepairUnit(BWAPI::Unit unit)
{
	std::map<BWAPI::Unit, BWAPI::Unit>::iterator it = workerRepairMap.find(unit);

	if (it != workerRepairMap.end())
	{
		return it->second;
	}	

	return nullptr;
}

BWAPI::TilePosition WorkerData::getWorkerTile(BWAPI::Unit unit)
{
    std::map<BWAPI::Unit, BWAPI::TilePosition>::iterator it = workerUnblockMap.find(unit);

    if (it != workerUnblockMap.end())
    {
        return it->second;
    }

    return BWAPI::TilePositions::None;
}

MacroLocation WorkerData::getWorkerPostLocation(BWAPI::Unit unit)
{
    std::map<BWAPI::Unit, WorkerData::WorkerPost>::iterator it = workerPostMap.find(unit);

    if (it != workerPostMap.end())
    {
        return it->second.location;
    }

    return MacroLocation::Anywhere;
}

BWAPI::Position WorkerData::getWorkerPostPosition(BWAPI::Unit unit)
{
    std::map<BWAPI::Unit, WorkerData::WorkerPost>::iterator it = workerPostMap.find(unit);

    if (it != workerPostMap.end())
    {
        return it->second.position;
    }

    return BWAPI::Positions::None;
}

BWAPI::Unit WorkerData::getWorkerDepot(BWAPI::Unit unit)
{
	if (!unit) { return nullptr; }

	std::map<BWAPI::Unit, BWAPI::Unit>::iterator it = workerDepotMap.find(unit);

	if (it != workerDepotMap.end())
	{
		return it->second;
	}	

	return nullptr;
}

int WorkerData::getNumAssignedWorkers(BWAPI::Unit unit) const
{
	if (!unit) { return 0; }

	if (unit->getType().isResourceDepot())
	{
		auto it = depotWorkerCount.find(unit);

		// if there is an entry, return it
		if (it != depotWorkerCount.end())
		{
			return it->second;
		}
	}
	else if (unit->getType().isRefinery())
	{
		auto it = refineryWorkerCount.find(unit);

		// if there is an entry, return it
		if (it != refineryWorkerCount.end())
		{
			return it->second;
		}
	}

	return 0;
}

// Add all gas workers to the given set.
void WorkerData::getGasWorkers(std::set<BWAPI::Unit> & mw)
{
	for (const auto & kv : workerRefineryMap)
	{
		mw.insert(kv.first);
	}
}

char WorkerData::getJobCode(BWAPI::Unit unit)
{
	if (!unit) { return 'X'; }

	return getJobCode(getWorkerJob(unit));
}

char WorkerData::getJobCode(WorkerJob j) const
{
	if (j == WorkerData::Minerals)    return 'M';
	if (j == WorkerData::Gas)         return 'G';
	if (j == WorkerData::Build)       return 'B';
	if (j == WorkerData::Combat)      return 'C';
	if (j == WorkerData::Idle)        return 'I';
	if (j == WorkerData::Repair)      return 'R';
	if (j == WorkerData::Scout)       return 'S';
    if (j == WorkerData::ReturnCargo) return '$';
    if (j == WorkerData::Unblock)     return 'U';
    if (j == WorkerData::Posted)      return 'P';
    if (j == WorkerData::PostedBuild) return 'Q';
    if (j == WorkerData::Default)     return '?';       // e.g. incomplete SCV
	return 'X';
}

void WorkerData::drawDepotDebugInfo()
{
    if (!Config::Debug::DrawWorkerInfo)
    {
        return;
    }

    for (const auto & depotCount : depotWorkerCount)
	{
        BWAPI::Unit depot = depotCount.first;
        int nDepotWorkers = depotCount.second;

		int x = depot->getPosition().x - 64;
		int y = depot->getPosition().y - 32;

		BWAPI::Broodwar->drawBoxMap(x-2, y-1, x+75, y+14, BWAPI::Colors::Black, true);
        BWAPI::Broodwar->drawTextMap(x, y, "%c Workers: %d", white, nDepotWorkers);

        for (BWAPI::Unit mineral : getMineralPatchesNearDepot(depot))
        {
            BWAPI::Position xy = mineral->getPosition() + BWAPI::Position(-16, -16);

            if (workersOnMineralPatch.find(mineral) != workersOnMineralPatch.end())
            {
                 BWAPI::Broodwar->drawBoxMap(xy, xy + BWAPI::Position(18, 16), BWAPI::Colors::Black, true);
                 BWAPI::Broodwar->drawTextMap(xy.x+2, xy.y+1, "%c %d", white, workersOnMineralPatch[mineral]);
            }
        }
    }

    for (const auto & gasCount : refineryWorkerCount)
    {
        BWAPI::Unit gas = gasCount.first;
        int n = gasCount.second;

        BWAPI::Position xy = gas->getPosition() + BWAPI::Position(-8, -16);

        BWAPI::Broodwar->drawBoxMap(xy, xy + BWAPI::Position(18, 16), BWAPI::Colors::Black, true);
        BWAPI::Broodwar->drawTextMap(xy.x + 2, xy.y + 1, "%c %d", white, n);
    }
}