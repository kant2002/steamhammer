#include "ProductionManager.h"
#include "GameCommander.h"
#include "UnitUtil.h"

using namespace UAlbertaBot;

ProductionManager::ProductionManager()
	: _lastProductionFrame				 (0)
	, _assignedWorkerForThisBuilding     (false)
	, _haveLocationForThisBuilding       (false)
	, _delayBuildingPredictionUntilFrame (0)
	, _outOfBook                         (false)
	, _targetGasAmount                   (0)
	, _extractorTrickState			     (ExtractorTrick::None)
	, _extractorTrickBuilding		     (nullptr)
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
}

void ProductionManager::update() 
{
	// TODO move this to worker manager and make it more precise; it often goes a little over
	// If we have reached a target amount of gas, take workers off gas.
	if (_targetGasAmount && BWAPI::Broodwar->self()->gatheredGas() >= _targetGasAmount)  // tends to go over
	{
		WorkerManager::Instance().setCollectGas(false);
		_targetGasAmount = 0;           // clear the target
	}

	// If we're in trouble, adjust the production queue to help.
	// Includes scheduling supply as needed.
	StrategyManager::Instance().handleUrgentProductionIssues(_queue);

	// if nothing is currently building, get a new goal from the strategy manager
	if (_queue.isEmpty())
		{
		if (Config::Debug::DrawBuildOrderSearchInfo)
		{
			BWAPI::Broodwar->drawTextScreen(150, 10, "Nothing left to build, replanning.");
		}

		goOutOfBook();
		StrategyManager::Instance().freshProductionPlan();
	}

	// Build stuff from the production queue.
	manageBuildOrderQueue();
}

void ProductionManager::onUnitDestroy(BWAPI::Unit unit)
{
	// If it's not our unit, we don't care.
	// Also, zerg no longer relies on this.
	if (BWAPI::Broodwar->self()->getRace() == BWAPI::Races::Zerg ||
		!unit ||
		unit->getPlayer() != BWAPI::Broodwar->self())
	{
		return;
	}
	
	// if it's a worker or a building, we need to re-search for the current goal
	if ((unit->getType().isWorker() && !WorkerManager::Instance().isWorkerScout(unit)) ||
		unit->getType().isBuilding())
	{
		goOutOfBook();
	}
}

void ProductionManager::manageBuildOrderQueue() 
{
	// If the extractor trick is in progress, do that.
	if (_extractorTrickState != ExtractorTrick::None)
	{
		doExtractorTrick();
		return;
	}

	// if there is nothing in the _queue, oh well
	if (_queue.isEmpty()) 
	{
		return;
	}

	// while there is still something left in the _queue
	while (!_queue.isEmpty()) 
	{
		BuildOrderItem & currentItem = _queue.getHighestPriorityItem();

		// If this is a command, execute it and keep going.
		if (currentItem.macroAct.isCommand())
		{
			executeCommand(currentItem.macroAct);
			_queue.removeHighestPriorityItem();
			_lastProductionFrame = BWAPI::Broodwar->getFrameCount();
			continue;
		}

		// The unit which can produce the currentItem. May be null.
        BWAPI::Unit producer = getProducer(currentItem.macroAct);

		// Work around a bug: If you say you want 2 comsats total, BOSS may order up 2 comsats more
		// than you already have. So we drop any extra comsat.
		if (!producer && currentItem.macroAct.isUnit() && currentItem.macroAct.getUnitType() == BWAPI::UnitTypes::Terran_Comsat_Station)
		{
			_queue.removeHighestPriorityItem();
			_lastProductionFrame = BWAPI::Broodwar->getFrameCount();
			continue;
		}

		// check to see if we can make it right now
		bool canMake = producer && canMakeNow(producer, currentItem.macroAct);

		// if the next item in the list is a building and we can't yet make it
        if (currentItem.macroAct.isBuilding() &&
			!canMake &&
			currentItem.macroAct.whatBuilds().isWorker() &&
			BWAPI::Broodwar->getFrameCount() >= _delayBuildingPredictionUntilFrame)
		{
			// construct a temporary building object
			Building b(currentItem.macroAct.getUnitType(), InformationManager::Instance().getMyMainBaseLocation()->getTilePosition());
			b.macroLocation = currentItem.macroAct.getMacroLocation();
            b.isGasSteal = currentItem.isGasSteal;

			// set the producer as the closest worker, but do not set its job yet
			producer = WorkerManager::Instance().getBuilder(b, false);

			// predict the worker movement to that building location
			// NOTE If the worker is set moving, this sets flag _movingToThisBuildingLocation = true
			//      so that we don't 
			predictWorkerMovement(b);
		}

		// if we can make the current item
		if (canMake) 
		{
			// create it
			create(producer, currentItem);
			_assignedWorkerForThisBuilding = false;
			_haveLocationForThisBuilding = false;
			_delayBuildingPredictionUntilFrame = 0;

			// and remove it from the _queue
			_queue.removeHighestPriorityItem();
			_lastProductionFrame = BWAPI::Broodwar->getFrameCount();

			// don't actually loop around in here
			// TODO because we don't keep track of resources used,
			//      we wait until the next frame to build the next thing.
			//      Can cause delays in late game!
			break;
		}
		else
		{
			// We didn't make anything. Check for a possible production jam.
			// Jams can happen due to bugs, or due to losing prerequisites for planned items.
			if (BWAPI::Broodwar->getFrameCount() > _lastProductionFrame + Config::Macro::ProductionJamFrameLimit)
			{
				// Looks very like a jam. Clear the queue and hope for better luck next time.
				// BWAPI::Broodwar->printf("breaking a production jam");
				goOutOfBook();
				return;
			}

			// TODO not much of a loop, eh? breaks on all branches
			break;
		}
	}
}

// May return null if no producer is found.
// NOTE closestTo defaults to BWAPI::Positions::None, meaning we don't care.
BWAPI::Unit ProductionManager::getProducer(MacroAct t, BWAPI::Position closestTo)
{
	UAB_ASSERT(!t.isCommand(), "no producer of a command");

    // get the type of unit that builds this
    BWAPI::UnitType producerType = t.whatBuilds();

    // make a set of all candidate producers
    BWAPI::Unitset candidateProducers;
    for (auto & unit : BWAPI::Broodwar->self()->getUnits())
    {
        // Reasons that a unit cannot produce the desired type:

		if (producerType != unit->getType()) { continue; }

		// TODO Due to a BWAPI 4.1.2 bug, lair research can't be done in a hive.
		//      Also spire upgrades can't be done in a greater spire.
		//      The bug is supposedly fixed in the next version.
		//      When a fixed version is available, change the above line to the following:
		// If the producerType is a lair, a hive will do as well.
		// Note: Burrow research in a hatchery can also be done in a lair or hive, but we rarely want to.
		// Ignore the possibility so that we don't accidentally waste lair time.
		//if (!(
		//	producerType == unit->getType() ||
		//	producerType == BWAPI::UnitTypes::Zerg_Lair && unit->getType() == BWAPI::UnitTypes::Zerg_Hive ||
		//  producerType == BWAPI::UnitTypes::Zerg_Spire && unit->getType() == BWAPI::UnitTypes::Zerg_Greater_Spire
		//	))
		//{
		//	continue;
		//}

        if (!unit->isCompleted())  { continue; }
        if (unit->isTraining())    { continue; }
        if (unit->isLifted())      { continue; }
        if (!unit->isPowered())    { continue; }
		if (unit->isUpgrading())   { continue; }
		if (unit->isResearching()) { continue; }

        // if the type is an addon, some special cases
        if (t.isUnit() && t.getUnitType().isAddon())
        {
            // Already has an addon, or is otherwise unable to make one.
			if (!unit->canBuildAddon())
            {
                continue;
            }

            // if we just told this unit to build an addon, then it will not be building another one
            // this deals with the frame-delay of telling a unit to build an addon and it actually starting to build
            if (unit->getLastCommand().getType() == BWAPI::UnitCommandTypes::Build_Addon 
                && (BWAPI::Broodwar->getFrameCount() - unit->getLastCommandFrame() < 10)) 
            { 
                continue; 
            }
        }
        
        // if a unit requires an addon and the producer doesn't have one
		// TODO Addons seem a bit erratic. Bugs are likely.
		// TODO What exactly is requiredUnits()? On the face of it, the story is that
		//      this code is for e.g. making tanks, built in a factory which has a machine shop.
		//      Research that requires an addon is done in the addon, a different case.
		//      Apparently wrong for e.g. ghosts, which require an addon not on the producer.
		if (t.isUnit())
		{
			bool reject = false;   // innocent until proven guilty
			typedef std::pair<BWAPI::UnitType, int> ReqPair;
			for (const ReqPair & pair : t.getUnitType().requiredUnits())
			{
				BWAPI::UnitType requiredType = pair.first;
				if (requiredType.isAddon())
				{
					if (!unit->getAddon() || (unit->getAddon()->getType() != requiredType))
					{
						reject = true;
						break;     // out of inner loop
					}
				}
			}
			if (reject)
			{
				continue;
			}
		}

        // if we haven't rejected it, add it to the set of candidates
        candidateProducers.insert(unit);
    }

	// Trick: If we're producing a worker, choose the producer (command center, nexus,
	// or larva) which is farthest from the main base. That way expansions are preferentially
	// populated with less need for worker transfers.
	if (t.isUnit() && t.getUnitType().isWorker())
	{
		return getFarthestUnitFromPosition(candidateProducers,
			InformationManager::Instance().getMyMainBaseLocation()->getPosition());
	}
	else
	{
		return getClosestUnitToPosition(candidateProducers, closestTo);
	}
}

BWAPI::Unit ProductionManager::getClosestUnitToPosition(const BWAPI::Unitset & units, BWAPI::Position closestTo)
{
    if (units.size() == 0)
    {
        return nullptr;
    }

    // if we don't care where the unit is return the first one we have
    if (closestTo == BWAPI::Positions::None)
    {
        return *(units.begin());
    }

    BWAPI::Unit closestUnit = nullptr;
    double minDist(1000000);

	for (auto & unit : units) 
    {
        UAB_ASSERT(unit != nullptr, "Unit was null");

		double distance = unit->getDistance(closestTo);
		if (distance < minDist) 
        {
			closestUnit = unit;
			minDist = distance;
		}
	}

    return closestUnit;
}

BWAPI::Unit ProductionManager::getFarthestUnitFromPosition(const BWAPI::Unitset & units, BWAPI::Position farthest)
{
	if (units.size() == 0)
	{
		return nullptr;
	}

	// if we don't care where the unit is return the first one we have
	if (farthest == BWAPI::Positions::None)
	{
		return *(units.begin());
	}

	BWAPI::Unit farthestUnit = nullptr;
	double maxDist(-1);

	for (auto & unit : units)
	{
		UAB_ASSERT(unit != nullptr, "Unit was null");

		double distance = unit->getDistance(farthest);
		if (distance > maxDist)
		{
			farthestUnit = unit;
			maxDist = distance;
		}
	}

	return farthestUnit;
}

BWAPI::Unit ProductionManager::getClosestLarvaToPosition(BWAPI::Position closestTo)
{
	BWAPI::Unitset larvas;
	for (auto & unit : BWAPI::Broodwar->self()->getUnits())
	{
		if (unit->getType() == BWAPI::UnitTypes::Zerg_Larva)
		{
			larvas.insert(unit);
		}
	}

	return getClosestUnitToPosition(larvas, closestTo);
}

// check to see if all preconditions are met and then create a unit
void ProductionManager::create(BWAPI::Unit producer, BuildOrderItem & item) 
{
    if (!producer)
    {
        return;
    }

    MacroAct act = item.macroAct;

    // if we're dealing with a building
	if (act.isBuilding()                                        // implies act.isUnit()
		&& !UnitUtil::IsMorphedBuildingType(act.getUnitType())  // morphed from another zerg building, not built
		&& !act.getUnitType().isAddon())                        // terran addon
	{
		// By default, build in the main base.
		// BuildingManager will override the location if it needs to.
		// Otherwise it will find some spot near desiredLocation.
		BWAPI::TilePosition desiredLocation = InformationManager::Instance().getMyMainBaseLocation()->getTilePosition();

		if (act.getMacroLocation() == MacroLocation::Natural)
		{
			BWTA::BaseLocation * natural = InformationManager::Instance().getMyNaturalLocation();
			if (natural)
			{
				desiredLocation = natural->getTilePosition();
			}
		}
		
		BuildingManager::Instance().addBuildingTask(act, desiredLocation, item.isGasSteal);
	}
	else if (act.isUnit() && act.getUnitType().isAddon())
	{
		//BWAPI::TilePosition addonPosition(producer->getTilePosition().x + producer->getType().tileWidth(), producer->getTilePosition().y + producer->getType().tileHeight() - t.unitType.tileHeight());
		producer->buildAddon(act.getUnitType());
	}
	// if we're dealing with a non-building unit, or a morphed zerg building
	else if (act.isUnit())
	{
		if (act.getUnitType().getRace() == BWAPI::Races::Zerg)
		{
			// if the race is zerg, morph the unit
			producer->morph(act.getUnitType());
		}
		else
		{
			// if not, train the unit
			producer->train(act.getUnitType());
		}
	}
	// if we're dealing with a tech research
	else if (act.isTech())
	{
		producer->research(act.getTechType());
	}
	else if (act.isUpgrade())
	{
		producer->upgrade(act.getUpgradeType());
	}
	else
	{
		UAB_ASSERT(false, "don't know how to create that");
	}
}

bool ProductionManager::canMakeNow(BWAPI::Unit producer, MacroAct t)
{
	UAB_ASSERT(producer != nullptr, "producer was null");

	bool canMake = meetsReservedResources(t);
	if (canMake)
	{
		if (t.isUnit())
		{
			canMake = BWAPI::Broodwar->canMake(t.getUnitType(), producer);
		}
		else if (t.isTech())
		{
			canMake = BWAPI::Broodwar->canResearch(t.getTechType(), producer);
		}
		else if (t.isUpgrade())
		{
			canMake = BWAPI::Broodwar->canUpgrade(t.getUpgradeType(), producer);
		}
		else if (t.isCommand())
		{
			canMake = true;
		}
		else
		{
			UAB_ASSERT(false, "Unknown type");
		}
	}

	return canMake;
}

// When the next item in the _queue is a building, this checks to see if we should move to
// its location in preparation for construction. If so, it orders the move.
// This function is here as it needs to access prodction manager's reserved resources info.
void ProductionManager::predictWorkerMovement(const Building & b)
{
    if (b.isGasSteal)
    {
        return;
    }

	// get a possible building location for the building
	if (!_haveLocationForThisBuilding)
	{
		_predictedTilePosition = BuildingManager::Instance().getBuildingLocation(b);
	}

	if (_predictedTilePosition != BWAPI::TilePositions::None)
	{
		_haveLocationForThisBuilding = true;
	}
	else
	{
		// BWAPI::Broodwar->printf("can't place building %s", b.type.getName().c_str());
		// If we can't place the building now, we probably can't place it next frame either.
		// Delay for a while before trying again. We could overstep the time limit.
		_delayBuildingPredictionUntilFrame = 12 + BWAPI::Broodwar->getFrameCount();
		return;
	}
	
	int x1 = _predictedTilePosition.x * 32;
	int y1 = _predictedTilePosition.y * 32;

	// draw a box where the building will be placed
	if (Config::Debug::DrawWorkerInfo)
    {
		int x2 = x1 + (b.type.tileWidth()) * 32;
		int y2 = y1 + (b.type.tileHeight()) * 32;
		BWAPI::Broodwar->drawBoxMap(x1, y1, x2, y2, BWAPI::Colors::Blue, false);
    }

	// where we want the worker to walk to
	BWAPI::Position walkToPosition		= BWAPI::Position(x1 + (b.type.tileWidth()/2)*32, y1 + (b.type.tileHeight()/2)*32);

	// compute how many resources we need to construct this building
	int mineralsRequired				= std::max(0, b.type.mineralPrice() - getFreeMinerals());
	int gasRequired						= std::max(0, b.type.gasPrice() - getFreeGas());

	// get a candidate worker to move to this location
	BWAPI::Unit moveWorker				= WorkerManager::Instance().getMoveWorker(walkToPosition);

	// Conditions under which to move the worker: 
	//		- there's a valid worker to move
	//		- we haven't yet assigned a worker to move to this location
	//		- the build position is valid
	//		- we will have the required resources by the time the worker gets there
	if (moveWorker && _haveLocationForThisBuilding && !_assignedWorkerForThisBuilding && (_predictedTilePosition != BWAPI::TilePositions::None) &&
		WorkerManager::Instance().willHaveResources(mineralsRequired, gasRequired, moveWorker->getDistance(walkToPosition)) )
	{
		// we have assigned a worker
		_assignedWorkerForThisBuilding = true;

		// tell the worker manager to move this worker
		WorkerManager::Instance().setMoveWorker(mineralsRequired, gasRequired, walkToPosition);
	}
}

int ProductionManager::getFreeMinerals() const
{
	return BWAPI::Broodwar->self()->minerals() - BuildingManager::Instance().getReservedMinerals();
}

int ProductionManager::getFreeGas() const
{
	return BWAPI::Broodwar->self()->gas() - BuildingManager::Instance().getReservedGas();
}

void ProductionManager::executeCommand(MacroAct act)
{
	UAB_ASSERT(act.isCommand(), "executing a non-command");

	MacroCommandType cmd = act.getCommandType().getType();
	if (cmd == MacroCommandType::Scout) {
		GameCommander::Instance().goScoutAlways();
	}
	else if (cmd == MacroCommandType::ScoutIfNeeded) {
		GameCommander::Instance().goScoutIfNeeded();
	}
	else if (cmd == MacroCommandType::ScoutLocation) {
		GameCommander::Instance().goScoutIfNeeded();
		ScoutManager::Instance().setScoutLocationOnly();
	}
	else if (cmd == MacroCommandType::StopGas) {
		WorkerManager::Instance().setCollectGas(false);
	}
	else if (cmd == MacroCommandType::StartGas) {
		WorkerManager::Instance().setCollectGas(true);
	}
	else if (cmd == MacroCommandType::GasUntil) {
		WorkerManager::Instance().setCollectGas(true);
		_targetGasAmount = BWAPI::Broodwar->self()->gatheredGas()
			- BWAPI::Broodwar->self()->gas()
			+ act.getCommandType().getAmount();
	}
	else if (cmd == MacroCommandType::StealGas) {
		ScoutManager::Instance().setGasSteal();
	}
	else if (cmd == MacroCommandType::ExtractorTrick) {
		startExtractorTrick();
	}
	else if (cmd == MacroCommandType::Aggressive) {
		CombatCommander::Instance().setAggression(true);
	}
	else if (cmd == MacroCommandType::Defensive) {
		CombatCommander::Instance().setAggression(false);
	}
	else {
		UAB_ASSERT(false, "unknown macro command");
	}
}

// Can we afford it, taking into account reserved resources?
bool ProductionManager::meetsReservedResources(MacroAct act)
{
	return (act.mineralPrice() <= getFreeMinerals()) && (act.gasPrice() <= getFreeGas());
}

// selects a unit of a given type
BWAPI::Unit ProductionManager::selectUnitOfType(BWAPI::UnitType type, BWAPI::Position closestTo) 
{
	// if we have none of the unit type, return nullptr right away
	if (BWAPI::Broodwar->self()->completedUnitCount(type) == 0) 
	{
		return nullptr;
	}

	BWAPI::Unit unit = nullptr;

	// if we are concerned about the position of the unit, that takes priority
    if (closestTo != BWAPI::Positions::None) 
    {
		double minDist(1000000);

		for (auto & u : BWAPI::Broodwar->self()->getUnits()) 
        {
			if (u->getType() == type) 
            {
				double distance = u->getDistance(closestTo);
				if (!unit || distance < minDist) {
					unit = u;
					minDist = distance;
				}
			}
		}

	// if it is a building and we are worried about selecting the unit with the least
	// amount of training time remaining
	} 
    else if (type.isBuilding()) 
    {
		for (auto & u : BWAPI::Broodwar->self()->getUnits()) 
        {
            UAB_ASSERT(u != nullptr, "Unit was null");

			if (u->getType() == type && u->isCompleted() && !u->isTraining() && !u->isLifted() && u->isPowered()) {

				return u;
			}
		}
		// otherwise just return the first unit we come across
	} 
    else 
    {
		for (auto & u : BWAPI::Broodwar->self()->getUnits()) 
		{
            UAB_ASSERT(u != nullptr, "Unit was null");

			if (u->getType() == type && u->isCompleted() && u->getHitPoints() > 0 && !u->isLifted() && u->isPowered()) 
			{
				return u;
			}
		}
	}

	return nullptr;
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
				BWAPI::Broodwar->drawTextScreen(x - 10, y, "\x04 gas %d, target %d", BWAPI::Broodwar->self()->gatheredGas(), _targetGasAmount);
			}
			else
			{
				BWAPI::Broodwar->drawTextScreen(x - 10, y, "\x04 gas %d", BWAPI::Broodwar->self()->gatheredGas());
			}
		}
		else
		{
			BWAPI::Broodwar->drawTextScreen(x - 10, y, "\x04 gas %d, stopped", BWAPI::Broodwar->self()->gatheredGas());
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
	else if (_extractorTrickState == ExtractorTrick::DroneOrdered)
	{
		BWAPI::Broodwar->drawTextScreen(x - 10, y, "\x04 extractor trick: drone ordered");
	}
	y += 2;

	// fill prod with each unit which is under construction
	std::vector<BWAPI::Unit> prod;
	for (auto & unit : BWAPI::Broodwar->self()->getUnits())
	{
        UAB_ASSERT(unit != nullptr, "Unit was null");

		if (unit->isBeingConstructed())
		{
			prod.push_back(unit);
		}
	}
	
	// sort it based on the time it was started
	std::sort(prod.begin(), prod.end(), CompareWhenStarted());

	for (auto & unit : prod) 
    {
		y += 10;

		BWAPI::UnitType t = unit->getType();
        if (t == BWAPI::UnitTypes::Zerg_Egg)
        {
            t = unit->getBuildType();
        }

		BWAPI::Broodwar->drawTextScreen(x, y, " %c%s", green, TrimRaceName(t.getName()).c_str());
		BWAPI::Broodwar->drawTextScreen(x - 35, y, "%c%6d", green, unit->getRemainingBuildTime());
	}

	_queue.drawQueueInformation(x, y+10, _outOfBook);
}

ProductionManager & ProductionManager::Instance()
{
	static ProductionManager instance;
	return instance;
}

void ProductionManager::queueGasSteal()
{
    _queue.queueAsHighestPriority(MacroAct(BWAPI::Broodwar->self()->getRace().getRefinery()), true);
}

// We're zerg and doing the extractor trick to get an extra drone.
// Set a flag to start the procedure.
void ProductionManager::startExtractorTrick()
{
	// Only zerg can do the extractor trick.
	if (BWAPI::Broodwar->self()->getRace() != BWAPI::Races::Zerg)
	{
		return;
	}

	// If we're not supply blocked, then we may have lost units earlier.
	// No need to do the trick, just queue up a drone.
	if (BWAPI::Broodwar->self()->supplyTotal() - BWAPI::Broodwar->self()->supplyUsed() >= 2)
	{
		_queue.queueAsHighestPriority(MacroAct(BWAPI::UnitTypes::Zerg_Drone));
		return;
	}
	
	// We need a free drone to execute the trick.
	if (WorkerManager::Instance().getNumMineralWorkers() <= 0)
	{
		return;
	}

	// And we need a free geyser to do it on.
	if (BuildingPlacer::Instance().getRefineryPosition() == BWAPI::TilePositions::None)
	{
		return;
	}
	
	_extractorTrickState = ExtractorTrick::Start;
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
		else if (getFreeMinerals() >= 100 || (nDrones >= 6 && getFreeMinerals() >= 76))
		{
			// We also need a larva to make the drone.
			if (BWAPI::Broodwar->self()->completedUnitCount(BWAPI::UnitTypes::Zerg_Larva) > 0)
			{
				BWAPI::TilePosition loc = BWAPI::TilePosition(0, 0);     // this gets ignored
				Building & b = BuildingManager::Instance().addTrackedBuildingTask(MacroAct(BWAPI::UnitTypes::Zerg_Extractor), loc, false);
				_extractorTrickState = ExtractorTrick::ExtractorOrdered;
				_extractorTrickBuilding = &b;
			}
		}
	}
	else if (_extractorTrickState == ExtractorTrick::ExtractorOrdered)
	{
		int supplyAvail = BWAPI::Broodwar->self()->supplyTotal() - BWAPI::Broodwar->self()->supplyUsed();
		if (supplyAvail >= 2 && getFreeMinerals() >= 50)
		{
			// We can build a drone now: The extractor finished, or another unit died somewhere.
			// Well, there is one more condition: We need a larva.
			BWAPI::Unit larva = getClosestLarvaToPosition(BWAPI::Position(InformationManager::Instance().getMyMainBaseLocation()->getTilePosition()));
			if (larva)
			{
				larva->morph(BWAPI::UnitTypes::Zerg_Drone);
				_extractorTrickState = ExtractorTrick::DroneOrdered;
			}
		}
		else if (supplyAvail < -2)
		{
			// Uh oh, we must have lost an overlord or a hatchery. Give up by moving on.
			_extractorTrickState = ExtractorTrick::DroneOrdered;
		}
		else if (WorkerManager::Instance().getNumMineralWorkers() <= 0)
		{
			// Yow, there must have been a drone massacre. Give up by moving on.
			_extractorTrickState = ExtractorTrick::DroneOrdered;
		}
	}
	else if (_extractorTrickState == ExtractorTrick::DroneOrdered)
	{
		UAB_ASSERT(_extractorTrickBuilding, "no extractor to cancel");
		BuildingManager::Instance().cancelBuilding(*_extractorTrickBuilding);
		_extractorTrickState = ExtractorTrick::None;
		_extractorTrickBuilding = nullptr;
	}
	else
	{
		UAB_ASSERT(false, "unexpected extractor trick state (possibly None)");
	}
}

// We have finished our book line, or are breaking out of it early.
// Clear the queue, set _outOfBook, go aggressive.
void ProductionManager::goOutOfBook()
{
	_queue.clearAll();
	_outOfBook = true;
	CombatCommander::Instance().setAggression(true);
}