#include "ProductionManager.h"

#include "Bases.h"
#include "BuildingPlacer.h"
#include "CombatCommander.h"
#include "Random.h"
#include "ScoutManager.h"
#include "StrategyBossZerg.h"
#include "The.h"
#include "UnitUtil.h"

using namespace UAlbertaBot;

ProductionManager::ProductionManager()
	: _lastProductionFrame(0)
	, _assignedWorkerForThisBuilding     (nullptr)
	, _typeOfUpcomingBuilding			 (BWAPI::UnitTypes::None)
	, _haveLocationForThisBuilding       (false)
	, _delayBuildingPredictionUntilFrame (0)
	, _outOfBook                         (false)
	, _targetGasAmount                   (0)
	, _extractorTrickState			     (ExtractorTrick::None)
	, _extractorTrickUnitType			 (BWAPI::UnitTypes::None)
	, _extractorTrickBuilding			 (nullptr)
{
    setBuildOrder(StrategyManager::Instance().getOpeningBookBuildOrder());
}

void ProductionManager::setBuildOrder(const BuildOrder & buildOrder)
{
	_queue.clearAll();

	for (size_t i(0); i<buildOrder.size(); ++i)
	{
		_queue.queueAsLowestPriority(buildOrder[i]);
	}
	_queue.resetModified();
}

void ProductionManager::update() 
{
	// TODO move this to worker manager and make it more precise; it normally goes a little over
	// If we have reached a target amount of gas, take workers off gas.
	if (_targetGasAmount && the.self()->gatheredGas() >= _targetGasAmount)
	{
		WorkerManager::Instance().setCollectGas(false);
		_targetGasAmount = 0;           // clear the target
	}

	// If we're in trouble, adjust the production queue to help.
	// Includes scheduling supply as needed.
	StrategyManager::Instance().handleUrgentProductionIssues(_queue);

	// Drop any initial queue items which can't be produced next because they are missing
	// prerequisites. This prevents most queue deadlocks.
	// Zerg does this separately (and more elaborately) in handleUrgentProductionIssues() above.
	if (the.selfRace() != BWAPI::Races::Zerg)
	{
		dropJammedItemsFromQueue();
	}

	// Carry out production goals, plus any other needed goal housekeeping.
	updateGoals();

	// if nothing is currently building, get a new goal from the strategy manager
	if (_queue.isEmpty())
	{
		if (Config::Debug::DrawBuildOrderSearchInfo)
		{
			BWAPI::Broodwar->drawTextScreen(150, 10, "Nothing left to build, replanning.");
		}

		if (!_outOfBook)
		{
			//BWAPI::Broodwar->printf("build finished %d, minerals and gas %d %d",
			//	the.now(), the.self()->minerals(), the.self()->gas()
			//);
		}

		goOutOfBookAndClearQueue();
		StrategyManager::Instance().freshProductionPlan();
	}

	// Build stuff from the production queue.
	manageBuildOrderQueue();
}

// If something important was destroyed, we may want to react.
void ProductionManager::onUnitDestroy(BWAPI::Unit unit)
{
	// If it's not our unit, we don't care.
	if (!unit || unit->getPlayer() != the.self())
	{
		return;
	}
	
	// If we're zerg, we break out of the opening in narrow cases.
	if (the.selfRace() == BWAPI::Races::Zerg && !isOutOfBook())
	{
        if (unit->getType() == BWAPI::UnitTypes::Zerg_Hatchery ||
            unit->getType() == BWAPI::UnitTypes::Zerg_Lair ||
			unit->getType() == BWAPI::UnitTypes::Zerg_Spire ||
			unit->getType() == BWAPI::UnitTypes::Zerg_Hydralisk_Den)
		{
            // We lost a key tech or production building.
            goOutOfBookAndClearQueue();
		}
        else if (!_queue.isEmpty() &&
            _queue.getHighestPriorityItem().macroAct.gasPrice() > the.self()->gas() &&
            the.my.all.count(BWAPI::UnitTypes::Zerg_Extractor) == 0)
        {
            // We lost an extractor and need gas.
            goOutOfBookAndClearQueue();
        }
		return;
	}

	// If it's a worker or a building, it affects the production plan.
	if (unit->getType().isWorker() && !_outOfBook)
	{
		// We lost a worker in the opening. Replace it.
		// This helps if a small number of workers are killed. If many are killed, you're toast anyway.
		// Still, it's better than breaking out of the opening altogether.
		_queue.queueAsHighestPriority(unit->getType());

		// If we have a gateway and no zealots, or a barracks and no marines,
		// consider making a military unit first. To, you know, stay alive and stuff.
        if (the.selfRace() == BWAPI::Races::Protoss)
		{
            if (the.my.completed.count(BWAPI::UnitTypes::Protoss_Gateway) > 0 &&
                the.my.all.count(BWAPI::UnitTypes::Protoss_Zealot) == 0 &&
				!_queue.anyInNextN(BWAPI::UnitTypes::Protoss_Zealot, 2) &&
                (the.self()->minerals() >= 150 || the.my.completed.count(BWAPI::UnitTypes::Protoss_Probe) > 3))
			{
				_queue.queueAsHighestPriority(BWAPI::UnitTypes::Protoss_Zealot);
			}
		}
        else if (the.selfRace() == BWAPI::Races::Terran)
		{
            if (the.my.completed.count(BWAPI::UnitTypes::Terran_Barracks) > 0 &&
                the.my.all.count(BWAPI::UnitTypes::Terran_Marine) == 0 &&
				!_queue.anyInNextN(BWAPI::UnitTypes::Terran_Marine, 2) &&
                (the.self()->minerals() >= 100 || the.my.completed.count(BWAPI::UnitTypes::Terran_SCV) > 3))
			{
				_queue.queueAsHighestPriority(BWAPI::UnitTypes::Terran_Marine);
			}
		}
		else // zerg
		{
            if (the.my.all.count(BWAPI::UnitTypes::Zerg_Drone) <= 9 &&
                the.self()->deadUnitCount(BWAPI::UnitTypes::Zerg_Drone) >= 4)
            {
                goOutOfBook();      // too many drones lost, go out of book anyway
            }
			if (the.my.completed.count(BWAPI::UnitTypes::Zerg_Spawning_Pool) > 0 &&
				the.my.all.count(BWAPI::UnitTypes::Zerg_Zergling) == 0 &&
				!_queue.anyInNextN(BWAPI::UnitTypes::Zerg_Zergling, 2) &&
                (the.self()->minerals() >= 100 || the.my.completed.count(BWAPI::UnitTypes::Zerg_Drone) > 3))
			{
				_queue.queueAsHighestPriority(BWAPI::UnitTypes::Zerg_Zergling);
			}
		}
	}
	else if (unit->getType().isBuilding() &&
		!(UnitUtil::CanAttackAir(unit) || UnitUtil::CanAttackGround(unit)) &&
		unit->getType().supplyProvided() == 0)
	{
		// We lost a building other than static defense or supply. It may be serious. Replan from scratch.
        // BWAPI::Broodwar->printf("critical building loss");
		goOutOfBookAndClearQueue();
	}
}

// Drop any initial items from the queue that will demonstrably cause a production jam.
void ProductionManager::dropJammedItemsFromQueue()
{
	while (!_queue.isEmpty() &&
		!_queue.getHighestPriorityItem().isGasSteal &&
		!itemCanBeProduced(_queue.getHighestPriorityItem().macroAct))
	{
		if (Config::Debug::DrawQueueFixInfo)
		{
			BWAPI::Broodwar->printf("queue: drop jammed %s", _queue.getHighestPriorityItem().macroAct.getName().c_str());
		}
		_queue.removeHighestPriorityItem();
	}
}

// Return false if the item definitely can't be made next.
// This doesn't yet try to handle all cases, so it can return false when it shouldn't.
bool ProductionManager::itemCanBeProduced(const MacroAct & act) const
{
	// A command can always be executed.
	// An addon is a goal that can always be posted (though it may fail).
	if (act.isCommand() || act.isAddon())
	{
		return true;
	}

	return act.hasPotentialProducer() && act.hasTech();
}

void ProductionManager::manageBuildOrderQueue()
{
	// If the extractor trick is in progress, do that.
	if (_extractorTrickState != ExtractorTrick::None)
	{
		doExtractorTrick();
		return;
	}

	// If we were planning to build and assigned a worker, but the queue was then
	// changed behind our back: Release the worker and continue.
	if (_queue.isModified() && _assignedWorkerForThisBuilding &&
		(_queue.isEmpty() || !_queue.getHighestPriorityItem().macroAct.isBuilding() || !_queue.getHighestPriorityItem().macroAct.getUnitType() != _typeOfUpcomingBuilding))
	{
		WorkerManager::Instance().finishedWithWorker(_assignedWorkerForThisBuilding);
		_assignedWorkerForThisBuilding = nullptr;
	}

	// We do nothing if the queue is empty (obviously).
	while (!_queue.isEmpty()) 
	{
		// We may be able to produce faster if we pull a later item to the front.
		maybeReorderQueue();

		const BuildOrderItem & currentItem = _queue.getHighestPriorityItem();

		// WORKAROUND for BOSS bug of making too many gateways: Limit the count to 10.
		// Idea borrowed from Locutus by Bruce Nielsen.
		if (currentItem.macroAct.isUnit() &&
			currentItem.macroAct.getUnitType() == BWAPI::UnitTypes::Protoss_Gateway &&
			the.my.all.count(BWAPI::UnitTypes::Protoss_Gateway) >= 10)
		{
			_queue.doneWithHighestPriorityItem();
			_lastProductionFrame = the.now();
			continue;
		}

		// If this is a command, execute it and keep going.
		if (currentItem.macroAct.isCommand())
		{
			executeCommand(currentItem.macroAct);
			_queue.doneWithHighestPriorityItem();
			_lastProductionFrame = the.now();
			continue;
		}

		// Turn some items into production goals.
        // if (currentItem.macroAct.isAddon() || currentItem.macroAct.isUpgrade() || currentItem.macroAct.isTech())
        if (currentItem.macroAct.isAddon())
		{
            _goals.push_front(ProductionGoal(currentItem.macroAct));
            _queue.doneWithHighestPriorityItem();
            _lastProductionFrame = the.now();
            continue;
		}

		// The unit which can produce the currentItem. May be null.
        BWAPI::Unit producer = getProducer(currentItem.macroAct);

		// check to see if we can make it right now
		bool canMake = producer && canMakeNow(producer, currentItem.macroAct);

        /*
        if (currentItem.macroAct.isUnit() && currentItem.macroAct.getUnitType() == BWAPI::UnitTypes::Zerg_Infested_Terran)
        {
            BWAPI::Broodwar->printf("%s - producer %s canMake %d",
                currentItem.macroAct.getName().c_str(),
                producer ? UnitTypeName(producer->getType()).c_str() : "none",
                int(canMake));
        }
        */

        // if the next item in the list is a building and we can't yet make it
		if (!canMake &&
			nextIsBuilding() &&
			the.now() >= _delayBuildingPredictionUntilFrame &&
			!BuildingManager::Instance().typeIsStalled(currentItem.macroAct.getUnitType()))
		{
			// construct a temporary building object
            Building b(currentItem.macroAct.getUnitType(), the.placer.getMacroLocationTile(currentItem.macroAct.getMacroLocation()));
			b.macroLocation = currentItem.macroAct.getMacroLocation();
            b.isGasSteal = currentItem.isGasSteal;

			// predict the worker movement to that building location
			// NOTE If the worker is set moving, this sets flag _movingToThisBuildingLocation = true
			//      so that we don't 
			predictWorkerMovement(b);
			break;
		}

		// if we can make the current item
		if (canMake)
		{
			// create it
			create(producer, currentItem);
			_assignedWorkerForThisBuilding = nullptr;
			_typeOfUpcomingBuilding = BWAPI::UnitTypes::None;
			_haveLocationForThisBuilding = false;
			_delayBuildingPredictionUntilFrame = 0;

			// and remove it from the _queue
			_queue.doneWithHighestPriorityItem();
			_lastProductionFrame = the.now();

			// don't actually loop around in here
			// TODO because we don't keep track of resources used,
			//      we wait until the next frame to build the next thing.
			//      Can cause delays, especially in late game!
			break;
		}

		// We didn't make anything. Check for a possible production jam.
		// Jams can happen due to bugs, or due to losing prerequisites for planned items.
        // If the queue is empty, we didn't intend to make anything--likely we can't.
        if (!_queue.isEmpty() &&
            the.now() > _lastProductionFrame + Config::Macro::ProductionJamFrameLimit &&
            (the.self()->minerals() >= 100 || the.bases.mineralPatchCount() > 0))
		{
            if (_queue.getHighestPriorityItem().macroAct.isUnit() &&
                _queue.getHighestPriorityItem().macroAct.getUnitType() == BWAPI::UnitTypes::Zerg_Mutalisk &&
                the.my.completed.count(BWAPI::UnitTypes::Zerg_Spire) == 0 &&
                (the.my.all.count(BWAPI::UnitTypes::Zerg_Spire) > 0 || BuildingManager::Instance().isBeingBuilt(BWAPI::UnitTypes::Zerg_Spire)))
            {
                // Exception: Sometimes the opening book can intentionally pause production for over a minute
                // while saving up for mutalisks. So if a mutalisk is next and a spire is building, do nothing.
                // BWAPI::Broodwar->printf("saving for mutas");
            }
            else if (the.self()->supplyUsed() > 400 - 12)
            {
                // We can't produce most likely because we're maxed. Great news!
            }
            else
            {
                // Looks very like a jam. Clear the queue and hope for better luck next time.
                if (Config::Debug::DrawQueueFixInfo)
                {
                    BWAPI::Broodwar->printf("queue: production jam timed out");
                }
                goOutOfBookAndClearQueue();
            }
		}

		// TODO not much of a loop, eh? breaks on all branches
		//      only commands and bug workarounds continue to the next item
		break;
	}
}

// If we can't immediately produce the top item in the queue but we can produce a
// later item, we may want to move the later item to the front.
void ProductionManager::maybeReorderQueue()
{
	if (_queue.size() < 2)
	{
		return;
	}

	// If we're in a severe emergency situation, don't try to reorder the queue.
	// We need a resource depot and a few workers.
	if (the.bases.baseCount(the.self()) == 0 ||
		WorkerManager::Instance().getNumMineralWorkers() <= 3)
	{
		return;
	}

	MacroAct top = _queue.getHighestPriorityItem().macroAct;

	// Don't move anything in front of a command.
	if (top.isCommand())
	{
		return;
	}

	// If next up is supply, don't reorder it.
	// Supply is usually made automatically. If we move something above supply, code below
	// will sometimes have to check whether we have supply to make a unit.
	if (top.isUnit() && top.getUnitType() == the.self()->getRace().getSupplyProvider())
	{
		return;
	}

	int minerals = getFreeMinerals();
	int gas = getFreeGas();

	// We can reorder the queue if: Case 1:
	// We are waiting for gas and have excess minerals,
	// and we can move a later no-gas item to the front,
	// and we have the minerals to cover both
	// and the moved item doesn't require more supply.
	if (top.gasPrice() > 0 && top.gasPrice() > gas && top.mineralPrice() < minerals)
	{
		for (int i = _queue.size() - 2; i >= std::max(0, int(_queue.size()) - 5); --i)
		{
			const MacroAct & act = _queue[i].macroAct;
			// Don't reorder a command or anything after it.
			if (act.isCommand())
			{
				break;
			}
			BWAPI::Unit producer;
			if (act.isUnit() &&
				act.gasPrice() == 0 &&
				act.mineralPrice() + top.mineralPrice() <= minerals &&
				act.supplyRequired() <= top.supplyRequired() &&
				(producer = getProducer(act)) &&
				canMakeNow(producer, act))
			{
				if (Config::Debug::DrawQueueFixInfo)
				{
					BWAPI::Broodwar->printf("queue: pull to front gas-free %s @ %d", act.getName().c_str(), _queue.size() - i);
				}
				_queue.pullToTop(i);
				return;
			}
		}
	}

	// We can reorder the queue if: Case 2:
	// We can't produce the next item
	// and a later item can be produced
	// and it does not require more supply than this item
	// and we have the resources for both.
	// This is where it starts to make a difference.
	BWAPI::Unit topProducer = getProducer(top);
	if (top.gasPrice() < gas &&
		top.mineralPrice() < minerals &&
		(!topProducer || !canMakeNow(topProducer,top)))
	{
		for (int i = _queue.size() - 2; i >= std::max(0, int(_queue.size()) - 5); --i)
		{
			const MacroAct & act = _queue[i].macroAct;
			// Don't reorder a command or anything after it.
			if (act.isCommand())
			{
				break;
			}
			BWAPI::Unit producer;
			if (act.supplyRequired() <= top.supplyRequired() &&
				act.gasPrice() + top.gasPrice() <= gas &&
				act.mineralPrice() + top.mineralPrice() <= minerals &&
				(producer = getProducer(act)) &&
				canMakeNow(producer, act))
			{
				if (Config::Debug::DrawQueueFixInfo)
				{
					BWAPI::Broodwar->printf("queue: pull to front %s @ %d", act.getName().c_str(), _queue.size() - i);
				}
				_queue.pullToTop(i);
				return;
			}
		}
	}
}

// Return null if no producer is found.
// NOTE closestTo defaults to BWAPI::Positions::None, meaning we don't care.
BWAPI::Unit ProductionManager::getProducer(MacroAct act) const
{
    if (act.isBuilding() && !UnitUtil::IsMorphedBuildingType(act.getUnitType()))
    {
        if (_assignedWorkerForThisBuilding)
        {
            return _assignedWorkerForThisBuilding;
        }
        MacroLocation loc = act.getMacroLocation();
        Building b(act.getUnitType(), the.placer.getMacroLocationTile(loc));
        b.macroLocation = loc;
        b.isGasSteal = act.isGasSteal();
        return WorkerManager::Instance().getBuilder(b);
    }

	std::vector<BWAPI::Unit> candidateProducers;
	act.getCandidateProducers(candidateProducers);
	if (candidateProducers.empty())
	{
		return nullptr;
	}

    // We may want to choose the producer by location.
    BWAPI::Position closestTo = BWAPI::Positions::None;
    if (act.getMacroLocation() != MacroLocation::Anywhere)
    {
        closestTo = the.placer.getMacroLocationPos(act.getMacroLocation());
    }
    else
    {
        // We can produce from anywhere. But try the best places first when we can tell.

        // If we're producing from a larva, seek an appropriate one.
        if (act.isUnit() &&
            act.getUnitType().whatBuilds().first == BWAPI::UnitTypes::Zerg_Larva)
        {
            return getBestLarva(act, candidateProducers);
        }

        // Trick: If we're producing a worker, choose the producer (command center or nexus)
        // which is farthest from the main base. That way expansions are preferentially
        // populated with less need to transfer workers.
        // This affects terran and protoss; zerg uses getBestLarva() above.
        if (act.isWorker())
        {
            return getFarthestUnitFromPosition(candidateProducers,
                the.bases.myMain()->getPosition());
        }

        // If we're morphing a lair, do it out of enemy vision if possible.
        if (act.isUnit() && act.getUnitType() == BWAPI::UnitTypes::Zerg_Lair)
        {
            return getBestHatcheryForLair(candidateProducers);
        }
    }

	return getClosestUnitToPosition(candidateProducers, closestTo);
}

// We're producing a zerg unit from a larva.
// The list of units (candidate producers) is guaranteed not empty.
BWAPI::Unit ProductionManager::getBestLarva(const MacroAct & act, const std::vector<BWAPI::Unit> & units) const
{
	// 1. If it's a worker, seek the least saturated base.
	// For equally saturated bases, take the one with the highest larva count.
	// Count only the base's resource depot, not any possible macro hatcheries there.
	if (act.getUnitType().isWorker())
	{
		Base * bestBase = nullptr;
		int maxShortfall = -1;
		size_t maxLarvas = 0;
		for (Base * base : the.bases.getAll())
		{
			if (base->getOwner() == the.self() && UnitUtil::IsCompletedResourceDepot(base->getDepot()))
			{
				auto larvaSet = base->getDepot()->getLarva();
				if (!larvaSet.empty())
				{
					int shortfall = std::max(0, base->getMaxWorkers() - base->getNumWorkers());
					if (shortfall > maxShortfall || shortfall == maxShortfall && larvaSet.size() > maxLarvas)
					{
						bestBase = base;
						maxShortfall = shortfall;
						maxLarvas = larvaSet.size();
					}
				}
			}
		}

		if (bestBase)
		{
			return *bestBase->getDepot()->getLarva().begin();
		}
	}

    // 2. Otherwise, pick a hatchery that has the most larvas.
	// This reduces wasted larvas; a hatchery won't make another if it has three.
	BWAPI::Unit bestHatchery = nullptr;
	size_t maxLarvas = 0;
	for (BWAPI::Unit hatchery : the.self()->getUnits())
	{
		if (UnitUtil::IsCompletedResourceDepot(hatchery))
		{
			auto larvaSet = hatchery->getLarva();
			if (larvaSet.size() > maxLarvas)
			{
				bestHatchery = hatchery;
				maxLarvas = larvaSet.size();
				if (maxLarvas >= 3)
				{
					break;
				}
			}
		}
	}

	if (bestHatchery)
	{
		return *bestHatchery->getLarva().begin();
	}

    // 3. There might be a larva not attached to any hatchery.
    for (BWAPI::Unit larva : the.self()->getUnits())
    {
        if (larva->getType() == BWAPI::UnitTypes::Zerg_Larva)
        {
            return larva;
        }
    }

	return nullptr;
}

BWAPI::Unit ProductionManager::getClosestUnitToPosition(const std::vector<BWAPI::Unit> & units, BWAPI::Position closestTo) const
{
	if (units.empty())
	{
		return nullptr;
	}

	// if we don't care where the unit is return the first one we have
	if (closestTo == BWAPI::Positions::None)
	{
		return *(units.begin());
	}

	BWAPI::Unit closestUnit = nullptr;
	int minDist = INT_MAX;

	for (BWAPI::Unit unit : units)
	{
		UAB_ASSERT(unit, "Unit was null");

		int distance = unit->getDistance(closestTo);
		if (distance < minDist)
		{
			closestUnit = unit;
			minDist = distance;
		}
	}

	return closestUnit;
}

BWAPI::Unit ProductionManager::getFarthestUnitFromPosition(const std::vector<BWAPI::Unit> & units, BWAPI::Position farthest) const
{
	if (units.empty())
	{
		return nullptr;
	}

	// if we don't care where the unit is return the first one we have
	if (farthest == BWAPI::Positions::None)
	{
		return *(units.begin());
	}

	BWAPI::Unit farthestUnit = nullptr;
	int maxDist(-1);

	for (BWAPI::Unit unit : units)
	{
		UAB_ASSERT(unit != nullptr, "Unit was null");

		int distance = unit->getDistance(farthest);
		if (distance > maxDist)
		{
			farthestUnit = unit;
			maxDist = distance;
		}
	}

	return farthestUnit;
}

BWAPI::Unit ProductionManager::getClosestLarvaToPosition(BWAPI::Position closestTo) const
{
	std::vector<BWAPI::Unit> larvas;
	for (BWAPI::Unit unit : the.self()->getUnits())
	{
		if (unit->getType() == BWAPI::UnitTypes::Zerg_Larva)
		{
			larvas.push_back(unit);
		}
	}

	return getClosestUnitToPosition(larvas, closestTo);
}

// We want to morph a lair. Try to do it in a safe place out of enemy vision.
BWAPI::Unit ProductionManager::getBestHatcheryForLair(const std::vector<BWAPI::Unit> & hatcheries) const
{
    int bestScore = INT_MIN;      // higher is better
    BWAPI::Unit bestHatch = nullptr;

    for (BWAPI::Unit hatchery : hatcheries)
    {
        BWAPI::Unit nearestEnemy = BWAPI::Broodwar->getClosestUnit(
            hatchery->getPosition(),
            BWAPI::Filter::IsEnemy,
            14 * 32);
        int score = 16 * 32;
        if (nearestEnemy)
        {
           score = hatchery->getDistance(nearestEnemy);
        }
        if (the.bases.myStart()->getOwner() == the.self() &&
            the.zone.at(hatchery) == the.zone.at(the.bases.myStart()->getTilePosition()))
        {
            score += 8 * 32 + int(the.random.range(5.0));   // a little jitter for variety
        }
        if (score > bestScore)
        {
            bestScore = score;
            bestHatch = hatchery;
        }
    }

    return bestHatch;
}

// Create a unit, start research, etc.
void ProductionManager::create(BWAPI::Unit producer, const BuildOrderItem & item) 
{
    item.macroAct.produce(producer);
}

bool ProductionManager::canMakeNow(BWAPI::Unit producer, MacroAct act)
{
    return act.canProduce(producer);
}

// When the next item in the _queue is a building, this checks to see if we should move to
// its location in preparation for construction. If so, it takes ownership of the worker
// and orders the move.
// The function is here as it needs to access production manager's reserved resources info.
// TODO A better plan is to move the work to BuildingManager: Have ProductionManager create
//      a preliminary building and let all other steps be done in one place, and tied to
//      a specific building instance.
void ProductionManager::predictWorkerMovement(Building & b)
{
    if (b.isGasSteal)
    {
        return;
    }

	_typeOfUpcomingBuilding = b.type;

	// get a possible building location for the building
	if (!_haveLocationForThisBuilding)
	{
		_predictedTilePosition = b.finalPosition = BuildingManager::Instance().getBuildingLocation(b);
	}

	if (_predictedTilePosition.isValid())
	{
		_haveLocationForThisBuilding = true;
	}
	else
	{
		// BWAPI::Broodwar->printf("can't place building %s", UnitTypeName(b.type).c_str());

		// If we can't place the building now, we probably can't place it next frame either.
		// Delay for a while before trying again. We could overstep the time limit.
		_delayBuildingPredictionUntilFrame = 13 + the.now();
		return;
	}
	
	int x1 = _predictedTilePosition.x * 32;
	int y1 = _predictedTilePosition.y * 32;

	if (Config::Debug::DrawWorkerInfo)
    {
		int x2 = x1 + (b.type.tileWidth()) * 32;
		int y2 = y1 + (b.type.tileHeight()) * 32;
		BWAPI::Broodwar->drawBoxMap(x1, y1, x2, y2, BWAPI::Colors::Blue, false);
    }

	// If we assigned a worker and it's not available any more, forget the assignment.
	if (_assignedWorkerForThisBuilding)
	{
		if (!_assignedWorkerForThisBuilding->exists() ||					// it's dead
			_assignedWorkerForThisBuilding->getPlayer() != the.self() ||	// it was mind controlled
            _assignedWorkerForThisBuilding->isLockedDown() ||
            _assignedWorkerForThisBuilding->isStasised() ||
            _assignedWorkerForThisBuilding->isMaelstrommed() ||
            _assignedWorkerForThisBuilding->isBurrowed())
		{
			// BWAPI::Broodwar->printf("missing assigned worker cleared");
			_assignedWorkerForThisBuilding = nullptr;
		}
	}

	// Conditions under which to assign the worker: 
	//		- the build position is valid (verified above)
	//		- we haven't yet assigned a worker to move to this location
	//		- there's a valid worker to move
	//		- we expect to have the required resources by the time the worker gets there
	if (!_assignedWorkerForThisBuilding)
	{
		// Candidate worker to move into position to build. Don't assign the worker yet.
		BWAPI::Unit builder = WorkerManager::Instance().getBuilder(b);

		// Where we want position the worker.
		BWAPI::Position walkToPosition = BWAPI::Position(x1 + (b.type.tileWidth() / 2) * 32, y1 + (b.type.tileHeight() / 2) * 32);

		// What more do we need to construct this building?
		int mineralsRequired = std::max(0, b.type.mineralPrice() - getFreeMinerals());
		int gasRequired = std::max(0, b.type.gasPrice() - getFreeGas());

		if (builder &&
			WorkerManager::Instance().willHaveResources(mineralsRequired, gasRequired, builder->getDistance(walkToPosition)))
		{
			// Assign the worker.
			_assignedWorkerForThisBuilding = builder;
			WorkerManager::Instance().setBuildWorker(builder);

			// Forget about any queue modification that happened. We're beyond it.
			_queue.resetModified();

			// Move the worker.
			the.micro.Move(builder, walkToPosition);
		}
	}
}

int ProductionManager::getFreeMinerals() const
{
	return the.self()->minerals() - BuildingManager::Instance().getReservedMinerals();
}

int ProductionManager::getFreeGas() const
{
	return the.self()->gas() - BuildingManager::Instance().getReservedGas();
}

void ProductionManager::executeCommand(const MacroAct & act)
{
    UAB_ASSERT(act.isCommand(), "not a command");

    MacroCommandType cmd = act.getCommandType().getType();

	if (cmd == MacroCommandType::Scout ||
		cmd == MacroCommandType::ScoutIfNeeded||
		cmd == MacroCommandType::ScoutLocation ||
		cmd == MacroCommandType::ScoutOnceOnly ||
		cmd == MacroCommandType::ScoutWhileSafe)
	{
		ScoutManager::Instance().setScoutCommand(cmd);
	}
	else if (cmd == MacroCommandType::StealGas)
	{
		ScoutManager::Instance().setGasSteal();
	}
	else if (cmd == MacroCommandType::StopGas)
	{
		WorkerManager::Instance().setCollectGas(false);
	}
	else if (cmd == MacroCommandType::StartGas)
	{
		WorkerManager::Instance().setCollectGas(true);
	}
	else if (cmd == MacroCommandType::GasUntil)
	{
		WorkerManager::Instance().setCollectGas(true);
		// NOTE This normally works correctly, but can be wrong if we turn gas on and off too quickly.
		//      It's wrong if e.g. we collect 100, then ask to collect 100 more before the first 100 is spent.
		_targetGasAmount = the.self()->gatheredGas()
			- the.self()->gas()
			+ act.getCommandType().getAmount();
	}
	else if (cmd == MacroCommandType::ExtractorTrickDrone)
	{
		startExtractorTrick(BWAPI::UnitTypes::Zerg_Drone);
	}
	else if (cmd == MacroCommandType::ExtractorTrickZergling)
	{
		startExtractorTrick(BWAPI::UnitTypes::Zerg_Zergling);
	}
	else if (cmd == MacroCommandType::Aggressive)
	{
		CombatCommander::Instance().setAggression(true);
	}
	else if (cmd == MacroCommandType::Defensive)
	{
		CombatCommander::Instance().setAggression(false);
	}
	else if (cmd == MacroCommandType::PullWorkers)
	{
        CombatCommander::Instance().pullWorkers(act.getCommandType().getAmount());
	}
	else if (cmd == MacroCommandType::PullWorkersLeaving)
	{
		int nWorkers = WorkerManager::Instance().getNumMineralWorkers() + WorkerManager::Instance().getNumGasWorkers();
        CombatCommander::Instance().pullWorkers(nWorkers - act.getCommandType().getAmount());
	}
	else if (cmd == MacroCommandType::ReleaseWorkers)
	{
		CombatCommander::Instance().releaseWorkers();
	}
    else if (cmd == MacroCommandType::PostWorker)
    {
        WorkerManager::Instance().postWorker(act.getMacroLocation());
    }
    else if (cmd == MacroCommandType::UnpostWorkers)
    {
        WorkerManager::Instance().unpostWorkers(act.getMacroLocation());
    }
    else if (cmd == MacroCommandType::Nonadaptive)
	{
		StrategyBossZerg::Instance().setNonadaptive(true);
	}
    else if (cmd == MacroCommandType::Lift)
    {
        liftBuildings(act.getCommandType().getUnitType());
    }
	else if (cmd == MacroCommandType::QueueBarrier)
	{
		// It does nothing! Every command is a queue barrier.
	}
	else
	{
		UAB_ASSERT(false, "unknown MacroCommand");
	}
}

void ProductionManager::updateGoals()
{
	// 1. Drop any goals which have been achieved.
	_goals.remove_if([](ProductionGoal & g) { return g.done(); });

	// 2. Attempt to carry out goals.
	for (ProductionGoal & goal : _goals)
	{
		goal.update();
	}
}

// Can we afford it, taking into account reserved resources?
bool ProductionManager::meetsReservedResources(const MacroAct & act) const
{
	return
        act.mineralPrice() <= getFreeMinerals() &&
        act.gasPrice() <= getFreeGas();
}

void ProductionManager::drawProductionInformation(int x, int y)
{
    if (!Config::Debug::DrawProductionInfo)
    {
        return;
    }

	y += 10;
	if (_extractorTrickState == ExtractorTrick::None)
	{
		if (WorkerManager::Instance().isCollectingGas())
		{
			if (_targetGasAmount)
			{
				BWAPI::Broodwar->drawTextScreen(x - 10, y, "\x04 gas %d, target %d", the.self()->gatheredGas(), _targetGasAmount);
			}
			else
			{
				BWAPI::Broodwar->drawTextScreen(x - 10, y, "\x04 gas %d", the.self()->gatheredGas());
			}
		}
		else
		{
			BWAPI::Broodwar->drawTextScreen(x - 10, y, "\x04 gas %d, stopped", the.self()->gatheredGas());
		}
	}
	else if (_extractorTrickState == ExtractorTrick::Start)
	{
		BWAPI::Broodwar->drawTextScreen(x - 10, y, "\x04 extractor trick: start");
	}
	else if (_extractorTrickState == ExtractorTrick::ExtractorOrdered)
	{
		BWAPI::Broodwar->drawTextScreen(x - 10, y, "\x04 extractor trick: extractor ordered");
	}
	else if (_extractorTrickState == ExtractorTrick::UnitOrdered)
	{
		BWAPI::Broodwar->drawTextScreen(x - 10, y, "\x04 extractor trick: unit ordered");
	}
	y += 2;

	// fill prod with each unit which is under construction
	std::vector<BWAPI::Unit> prod;
	for (BWAPI::Unit unit : the.self()->getUnits())
	{
        UAB_ASSERT(unit != nullptr, "Unit was null");

		if (unit->isBeingConstructed())
		{
			prod.push_back(unit);
		}
	}
	
	// sort it based on the time it was started
	std::sort(prod.begin(), prod.end(), CompareWhenStarted());

	for (const ProductionGoal & goal : _goals)
	{
		y += 10;
		BWAPI::Broodwar->drawTextScreen(x, y, " %cgoal %c%s", white, orange, NiceMacroActName(goal.act.getName()).c_str());
	}

	for (const auto & unit : prod)
    {
		y += 10;

		BWAPI::UnitType t = unit->getType();
        if (t == BWAPI::UnitTypes::Zerg_Egg)
        {
            t = unit->getBuildType();
        }

		BWAPI::Broodwar->drawTextScreen(x, y, " %c%s", green, NiceMacroActName(t.getName()).c_str());
		BWAPI::Broodwar->drawTextScreen(x - 35, y, "%c%6d", green, unit->getRemainingBuildTime());
	}
	
	_queue.drawQueueInformation(x, y+10, _outOfBook);
}

ProductionManager & ProductionManager::Instance()
{
	static ProductionManager instance;
	return instance;
}

// We're zerg and doing the extractor trick to get an extra drone or pair of zerglings,
// as specified in the argument.
// Set a flag to start the procedure, and handle various error cases.
void ProductionManager::startExtractorTrick(BWAPI::UnitType type)
{
	// Only zerg can do the extractor trick.
	if (the.self()->getRace() != BWAPI::Races::Zerg)
	{
		return;
	}

	// If we're not supply blocked, then we may have lost units earlier.
	// We may or may not have a larva available now, so instead of finding a larva and
	// morphing the unit here, we set a special case extractor trick state to do it
	// when a larva becomes available.
	// We can't queue a unit, because when we return the caller will delete the front queue
	// item--Steamhammer used to do that, but Arrak found the bug.
	if (the.self()->supplyTotal() - the.self()->supplyUsed() >= 2)
	{
		if (_extractorTrickUnitType != BWAPI::UnitTypes::None)
		{
			_extractorTrickState = ExtractorTrick::MakeUnitBypass;
			_extractorTrickUnitType = type;
		}
		return;
	}
	
	// We need a free drone to execute the trick.
	if (WorkerManager::Instance().getNumMineralWorkers() <= 0)
	{
		return;
	}

	// And we need a free geyser to do it on.
	if (the.placer.getRefineryPosition() == BWAPI::TilePositions::None)
	{
		return;
	}
	
	_extractorTrickState = ExtractorTrick::Start;
	_extractorTrickUnitType = type;
}

// The extractor trick is in progress. Take the next step, when possible.
// At most one step occurs per frame.
void ProductionManager::doExtractorTrick()
{
	if (_extractorTrickState == ExtractorTrick::Start)
	{
		UAB_ASSERT(!_extractorTrickBuilding, "already have an extractor trick building");
		int nDrones = WorkerManager::Instance().getNumMineralWorkers();
		if (nDrones <= 0)
		{
			// Oops, we can't do it without a free drone. Give up.
			_extractorTrickState = ExtractorTrick::None;
		}
		// If there are "many" drones mining, assume we'll get resources to finish the trick.
		// Otherwise wait for the full 100 before we start.
		// NOTE 100 assumes we are making a drone or a pair of zerglings.
		else if (getFreeMinerals() >= 100 || (nDrones >= 6 && getFreeMinerals() >= 76))
		{
			// We also need a larva to make the drone.
			if (the.self()->completedUnitCount(BWAPI::UnitTypes::Zerg_Larva) > 0)
			{
				BWAPI::TilePosition loc = BWAPI::TilePosition(0, 0);     // this gets ignored
				Building & b = BuildingManager::Instance().addTrackedBuildingTask(MacroAct(BWAPI::UnitTypes::Zerg_Extractor), loc, nullptr, false);
				_extractorTrickState = ExtractorTrick::ExtractorOrdered;
				_extractorTrickBuilding = &b;    // points into building manager's queue of buildings
			}
		}
	}
	else if (_extractorTrickState == ExtractorTrick::ExtractorOrdered)
	{
		if (_extractorTrickUnitType == BWAPI::UnitTypes::None)
		{
			_extractorTrickState = ExtractorTrick::UnitOrdered;
		}
		else
		{
			int supplyAvail = the.self()->supplyTotal() - the.self()->supplyUsed();
			if (supplyAvail >= 2 &&
				getFreeMinerals() >= _extractorTrickUnitType.mineralPrice() &&
				getFreeGas() >= _extractorTrickUnitType.gasPrice())
			{
				// We can build a unit now: The extractor started, or another unit died somewhere.
				// Well, there is one more condition: We need a larva.
				BWAPI::Unit larva = getClosestLarvaToPosition(the.bases.myMain()->getPosition());
				if (larva && _extractorTrickUnitType != BWAPI::UnitTypes::None)
				{
					if (_extractorTrickUnitType == BWAPI::UnitTypes::Zerg_Zergling &&
                        the.my.completed.count(BWAPI::UnitTypes::Zerg_Spawning_Pool) == 0)
					{
						// We want a zergling but don't have the tech.
						// Give up by doing nothing and moving on.
					}
					else
					{
						the.micro.Make(larva, _extractorTrickUnitType);
					}
					_extractorTrickState = ExtractorTrick::UnitOrdered;
				}
			}
			else if (supplyAvail < -2)
			{
				// Uh oh, we must have lost an overlord or a hatchery. Give up by moving on.
				_extractorTrickState = ExtractorTrick::UnitOrdered;
			}
			else if (WorkerManager::Instance().getNumMineralWorkers() <= 0)
			{
				// Drone massacre, or all drones pulled to fight. Give up by moving on.
				_extractorTrickState = ExtractorTrick::UnitOrdered;
			}
		}
	}
	else if (_extractorTrickState == ExtractorTrick::UnitOrdered)
	{
		UAB_ASSERT(_extractorTrickBuilding, "no extractor to cancel");
		BuildingManager::Instance().cancelBuilding(*_extractorTrickBuilding);
		_extractorTrickState = ExtractorTrick::None;
		_extractorTrickUnitType = BWAPI::UnitTypes::None;
		_extractorTrickBuilding = nullptr;
	}
	else if (_extractorTrickState == ExtractorTrick::MakeUnitBypass)
	{
		// We did the extractor trick when we didn't need to, whether because the opening was
		// miswritten or because units were lost before we got here.
		// This special state lets us construct the unit we want anyway, bypassing the extractor.
		BWAPI::Unit larva = getClosestLarvaToPosition(the.bases.myMain()->getPosition());
		if (larva &&
			getFreeMinerals() >= _extractorTrickUnitType.mineralPrice() &&
			getFreeGas() >= _extractorTrickUnitType.gasPrice())
		{
			the.micro.Make(larva, _extractorTrickUnitType);
			_extractorTrickState = ExtractorTrick::None;
		}
	}
	else
	{
		UAB_ASSERT(false, "unexpected extractor trick state (possibly None)");
	}
}

void ProductionManager::liftBuildings(BWAPI::UnitType type) const
{
    for (BWAPI::Unit u : the.self()->getUnits())
    {
        if (u->getType() == type && u->canLift())
        {
            (void)the.micro.Lift(u);
        }
    }
}

void ProductionManager::queueGasSteal()
{
	_queue.queueAsHighestPriority(MacroAct(the.self()->getRace().getRefinery(), MacroLocation::GasSteal), true);
}

// Has a gas steal has been queued?
bool ProductionManager::isGasStealInQueue() const
{
	return _queue.isGasStealInQueue() || BuildingManager::Instance().isGasStealInQueue();
}

// The next item in the queue is a building that requires a worker to construct.
// Addons and morphed buildings (e.g. lair) do not need a worker.
bool ProductionManager::nextIsBuilding() const
{
	if (_queue.isEmpty())
	{
		return false;
	}

	const MacroAct & next = _queue.getHighestPriorityItem().macroAct;

	return
		next.isBuilding() &&
		!next.getUnitType().isAddon() &&
		!UnitUtil::IsMorphedBuildingType(next.getUnitType());
}

// We have finished our book line, or are breaking out of it early.
// Clear the queue, set _outOfBook, go aggressive.
// NOTE This clears the queue even if we are already out of book.
void ProductionManager::goOutOfBookAndClearQueue()
{
    if (Config::Debug::DrawQueueFixInfo && !_queue.isEmpty())
    {
        BWAPI::Broodwar->printf("queue: go out of book and clear queue");
    }
	_queue.clearAll();
	_outOfBook = true;
    _lastProductionFrame = the.now();       // don't immediately clear the "jam" again
	CombatCommander::Instance().setAggression(true);
}

// If we're in book, leave it and clear the queue.
// Otherwise do nothing.
void ProductionManager::goOutOfBook()
{
	if (!_outOfBook)
	{
        if (Config::Debug::DrawQueueFixInfo && _queue.isEmpty())
        {
            BWAPI::Broodwar->printf("queue: go out of book");
        }
        goOutOfBookAndClearQueue();
	}
}
