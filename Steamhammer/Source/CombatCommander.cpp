#include "CombatCommander.h"
#include "UnitUtil.h"

using namespace UAlbertaBot;

const size_t IdlePriority = 0;
const size_t AttackPriority = 2;
const size_t BaseDefensePriority = 3;
const size_t ScoutDefensePriority = 4;
const size_t DropPriority = 5;         // don't steal from Drop squad for Defense squad
const size_t SurveyPriority = 10;      // consists of only 1 overlord, no need to steal from it

CombatCommander::CombatCommander() 
    : _initialized(false)
	, _goAggressive(true)
{
}

// Called once at the start of the game.
// You can also create new squads at other times.
void CombatCommander::initializeSquads()
{
	// The idle squad includes workers at work (not idle at all) and unassigned overlords.
    SquadOrder idleOrder(SquadOrderTypes::Idle, BWAPI::Position(BWAPI::Broodwar->self()->getStartLocation()), 100, "Chill out");
	_squadData.addSquad(Squad("Idle", idleOrder, IdlePriority));

    // the main attack squad will pressure an enemy base location
    SquadOrder mainAttackOrder(SquadOrderTypes::Attack, getMainAttackLocation(nullptr), 800, "Attack enemy base");
	_squadData.addSquad(Squad("MainAttack", mainAttackOrder, AttackPriority));

	// The flying squad separates air units so they can act independently.
	// It gets the same order as the attack squad.
	_squadData.addSquad(Squad("Flying", mainAttackOrder, AttackPriority));

	BWAPI::Position ourBasePosition = BWAPI::Position(BWAPI::Broodwar->self()->getStartLocation());

    // the scout defense squad will handle chasing the enemy worker scout
	if (Config::Micro::ScoutDefenseRadius > 0)
	{
		SquadOrder enemyScoutDefense(SquadOrderTypes::Defend, ourBasePosition, Config::Micro::ScoutDefenseRadius, "Get the scout");
		_squadData.addSquad(Squad("ScoutDefense", enemyScoutDefense, ScoutDefensePriority));
	}

	// If we're using a drop opening, create a drop squad.
	// It is initially ordered to hold ground until it can load up and go.
    if (StrategyManager::Instance().getOpeningGroup() == "drop")
    {
		SquadOrder doDrop(SquadOrderTypes::Hold, ourBasePosition, 800, "Wait for transport");
		_squadData.addSquad(Squad("Drop", doDrop, DropPriority));
    }

	// Zerg can put overlords into a simpleminded survey squad.
	// With no evasion skills, it's dangerous to do that against terran.
	if (BWAPI::Broodwar->self()->getRace() == BWAPI::Races::Zerg &&
		BWAPI::Broodwar->enemy()->getRace() != BWAPI::Races::Terran)
	{
		SquadOrder surveyMap(SquadOrderTypes::Survey, getSurveyLocation(), 100, "Get the surveyors");
		_squadData.addSquad(Squad("Survey", surveyMap, SurveyPriority));
	}

    _initialized = true;
}

void CombatCommander::update(const BWAPI::Unitset & combatUnits)
{
    if (!_initialized)
    {
        initializeSquads();
    }

    _combatUnits = combatUnits;

	int frame8 = BWAPI::Broodwar->getFrameCount() % 8;

	if (frame8 == 1)
	{
		updateIdleSquad();
		updateDropSquads();
		updateScoutDefenseSquad();
		updateBaseDefenseSquads();
		updateAttackSquads();
		updateSurveySquad();
	}
	else if (frame8 % 4 == 2)
	{
		doComsatScan();
	}

	loadOrUnloadBunkers();

	_squadData.update();          // update() all the squads

	cancelDyingBuildings();
}

void CombatCommander::updateIdleSquad()
{
    Squad & idleSquad = _squadData.getSquad("Idle");
    for (auto & unit : _combatUnits)
    {
        // if it hasn't been assigned to a squad yet, put it in the low priority idle squad
        if (_squadData.canAssignUnitToSquad(unit, idleSquad))
        {
            idleSquad.addUnit(unit);
        }
    }
}

// Form the main attack squad (on the ground) and the flying squad.
// NOTE Nothing here recognizes arbiters or zerg greater spire units.
//      Therefore they default into the ground squad.
void CombatCommander::updateAttackSquads()
{
    Squad & mainAttackSquad = _squadData.getSquad("MainAttack");
	Squad & flyingSquad = _squadData.getSquad("Flying");

	// Include exactly 1 detector in each squad, for detection.
	bool mainDetector = false;
	bool mainSquadExists = false;
	for (const auto unit : mainAttackSquad.getUnits())
	{
		if (unit->getType().isDetector())
		{
			mainDetector = true;
		}
		else
		{
			mainSquadExists = true;
		}
	}

	bool flyingDetector = false;
	bool flyingSquadExists = false;	    // scourge and carriers to flying squad if any, otherwise main squad
	for (const auto unit : flyingSquad.getUnits())
	{
		if (unit->getType().isDetector())
		{
			flyingDetector = true;
		}
		else if (unit->getType() == BWAPI::UnitTypes::Zerg_Mutalisk ||
			unit->getType() == BWAPI::UnitTypes::Terran_Wraith ||
			unit->getType() == BWAPI::UnitTypes::Terran_Valkyrie ||
			unit->getType() == BWAPI::UnitTypes::Terran_Battlecruiser ||
			unit->getType() == BWAPI::UnitTypes::Protoss_Corsair ||
			unit->getType() == BWAPI::UnitTypes::Protoss_Scout)
		{
			flyingSquadExists = true;
		}
	}

	for (const auto unit : _combatUnits)
    {
        // Scourge and carriers go into the flying squad only if it already exists.
		// Otherwise they go into the ground squad.
		bool isDetector = unit->getType().isDetector();
		if (_squadData.canAssignUnitToSquad(unit, flyingSquad)
			  &&
			(unit->getType() == BWAPI::UnitTypes::Zerg_Mutalisk ||
			unit->getType() == BWAPI::UnitTypes::Zerg_Scourge && flyingSquadExists ||
			unit->getType() == BWAPI::UnitTypes::Terran_Wraith ||
			unit->getType() == BWAPI::UnitTypes::Terran_Valkyrie ||
			unit->getType() == BWAPI::UnitTypes::Terran_Battlecruiser ||
			unit->getType() == BWAPI::UnitTypes::Protoss_Corsair ||
			unit->getType() == BWAPI::UnitTypes::Protoss_Scout ||
			unit->getType() == BWAPI::UnitTypes::Protoss_Carrier && flyingSquadExists ||
			isDetector && !flyingDetector && flyingSquadExists))
		{
			_squadData.assignUnitToSquad(unit, flyingSquad);
			if (isDetector)
			{
				flyingDetector = true;
			}
		}
        else if (!unit->getType().isWorker() &&
			(!isDetector || isDetector && !mainDetector && mainSquadExists) &&
			_squadData.canAssignUnitToSquad(unit, mainAttackSquad))
        {
			_squadData.assignUnitToSquad(unit, mainAttackSquad);
			if (isDetector)
			{
				mainDetector = true;
			}
        }
    }

    SquadOrder mainAttackOrder(SquadOrderTypes::Attack, getMainAttackLocation(&mainAttackSquad), 800, "Attack enemy base");
    mainAttackSquad.setSquadOrder(mainAttackOrder);

	SquadOrder flyingAttackOrder(SquadOrderTypes::Attack, getMainAttackLocation(&flyingSquad), 800, "Attack enemy base");
	flyingSquad.setSquadOrder(flyingAttackOrder);
}

// Despite the name, this supports only 1 drop squad which has 1 transport.
// Furthermore, it can only drop once and doesn't know how to reset itself to try again.
// Still, it's a start and it can be effective.
void CombatCommander::updateDropSquads()
{
	// If we don't have a drop squad, then we don't want to drop.
	// It is created in initializeSquads().
	if (!_squadData.squadExists("Drop"))
    {
		return;
    }

    Squad & dropSquad = _squadData.getSquad("Drop");

	// The squad is initialized with a Hold order.
	// There are 3 phases, and in each phase the squad is given a different order:
	// Collect units (Hold); load the transport (Load); go drop (Drop).
	// If it has already been told to go, we are done.
	if (dropSquad.getSquadOrder().getType() != SquadOrderTypes::Hold &&
		dropSquad.getSquadOrder().getType() != SquadOrderTypes::Load)
	{
		return;
	}

    // What units do we have, what units do we need?
	BWAPI::Unit transportUnit = nullptr;
    int transportSpotsRemaining = 8;      // all transports are the same size
	bool anyUnloadedUnits = false;
	const auto & dropUnits = dropSquad.getUnits();

    for (const auto unit : dropUnits)
    {
		if (unit->exists())
		{
			if (unit->isFlying() && unit->getType().spaceProvided() > 0)
			{
				transportUnit = unit;
			}
			else
			{
				transportSpotsRemaining -= unit->getType().spaceRequired();
				if (!unit->isLoaded())
				{
					anyUnloadedUnits = true;
				}
			}
		}
    }

	if (transportUnit && transportSpotsRemaining == 0)
	{
		if (anyUnloadedUnits)
		{
			// The drop squad is complete. Load up.
			// See Squad::loadTransport().
			SquadOrder loadOrder(SquadOrderTypes::Load, transportUnit->getPosition(), 800, "Load up");
			dropSquad.setSquadOrder(loadOrder);
		}
		else
		{
			// We're full. Change the order to Drop.
			BWAPI::Position target = InformationManager::Instance().getEnemyMainBaseLocation()
				? InformationManager::Instance().getEnemyMainBaseLocation()->getPosition()
				: getMainAttackLocation(&dropSquad);

			SquadOrder dropOrder = SquadOrder(SquadOrderTypes::Drop, target, 300, "Go drop!");
			dropSquad.setSquadOrder(dropOrder);
		}
	}
	else
    {
		// The drop squad is not complete. Look for more units.
        for (const auto unit : _combatUnits)
        {
            // If the squad doesn't have a transport, try to add one.
			if (!transportUnit &&
				unit->getType().spaceProvided() > 0 && unit->isFlying() &&
				_squadData.canAssignUnitToSquad(unit, dropSquad))
            {
                _squadData.assignUnitToSquad(unit, dropSquad);
				transportUnit = unit;
            }

            // If the unit fits and is good to drop, add it to the squad.
			// Rewrite unitIsGoodToDrop() to select the units of your choice to drop.
			// Simplest to stick to units that occupy the same space in a transport, to avoid difficulties
			// like "add zealot, add dragoon, can't add another dragoon--but transport is not full, can't go".
			else if (unit->getType().spaceRequired() <= transportSpotsRemaining &&
				unitIsGoodToDrop(unit) &&
				_squadData.canAssignUnitToSquad(unit, dropSquad))
            {
				_squadData.assignUnitToSquad(unit, dropSquad);
                transportSpotsRemaining -= unit->getType().spaceRequired();
            }
        }
    }
}

void CombatCommander::updateScoutDefenseSquad() 
{
	if (Config::Micro::ScoutDefenseRadius == 0 || _combatUnits.empty())
    { 
        return; 
    }

    // if the current squad has units in it then we can ignore this
    Squad & scoutDefenseSquad = _squadData.getSquad("ScoutDefense");
  
    // get the region that our base is located in
    BWTA::Region * myRegion = BWTA::getRegion(InformationManager::Instance().getMyMainBaseLocation()->getTilePosition());
    if (!myRegion || !myRegion->getCenter().isValid())
    {
        return;
    }

    // get all of the enemy units in this region
	BWAPI::Unitset enemyUnitsInRegion;
    for (auto & unit : BWAPI::Broodwar->enemy()->getUnits())
    {
        if (BWTA::getRegion(BWAPI::TilePosition(unit->getPosition())) == myRegion)
        {
            enemyUnitsInRegion.insert(unit);
        }
    }

    // if there's an enemy worker in our region then assign someone to chase him
    bool assignScoutDefender = enemyUnitsInRegion.size() == 1 && (*enemyUnitsInRegion.begin())->getType().isWorker();

    // if our current squad is empty and we should assign a worker, do it
    if (scoutDefenseSquad.isEmpty() && assignScoutDefender)
    {
        // the enemy worker that is attacking us
        BWAPI::Unit enemyWorker = *enemyUnitsInRegion.begin();

        // get our worker unit that is mining that is closest to it
        BWAPI::Unit workerDefender = findClosestWorkerToTarget(_combatUnits, enemyWorker);

		if (enemyWorker && workerDefender)
		{
			// grab it from the worker manager and put it in the squad
            if (_squadData.canAssignUnitToSquad(workerDefender, scoutDefenseSquad))
            {
			    WorkerManager::Instance().setCombatWorker(workerDefender);
                _squadData.assignUnitToSquad(workerDefender, scoutDefenseSquad);
            }
		}
    }
    // if our squad is not empty and we shouldn't have a worker chasing then take it out of the squad
    else if (!scoutDefenseSquad.isEmpty() && !assignScoutDefender)
    {
        scoutDefenseSquad.clear();     // also releases the worker
    }
}

void CombatCommander::updateBaseDefenseSquads() 
{
	if (_combatUnits.empty()) 
    { 
        return; 
    }
    
    BWTA::BaseLocation * enemyBaseLocation = InformationManager::Instance().getEnemyMainBaseLocation();
    BWTA::Region * enemyRegion = nullptr;
    if (enemyBaseLocation)
    {
        enemyRegion = BWTA::getRegion(enemyBaseLocation->getPosition());
    }

	// for each of our occupied regions
	for (BWTA::Region * myRegion : InformationManager::Instance().getOccupiedRegions(BWAPI::Broodwar->self()))
	{
        // don't defend inside the enemy region, this will end badly when we are stealing gas
        if (myRegion == enemyRegion)
        {
            continue;
        }

		BWAPI::Position regionCenter = myRegion->getCenter();
		if (!regionCenter.isValid())
		{
			continue;
		}

		// start off assuming all enemy units in region are just workers
		int numDefendersPerEnemyUnit = 2;

		// all of the enemy units in this region
		BWAPI::Unitset enemyUnitsInRegion;
        for (auto & unit : BWAPI::Broodwar->enemy()->getUnits())
        {
            // If it's a harmless air unit, don't worry about it for base defense.
			// TODO something more sensible
            if (unit->getType() == BWAPI::UnitTypes::Zerg_Overlord ||
				unit->getType() == BWAPI::UnitTypes::Protoss_Observer ||
				unit->isLifted())  // floating terran building
            {
                continue;
            }

            if (BWTA::getRegion(BWAPI::TilePosition(unit->getPosition())) == myRegion)
            {
                enemyUnitsInRegion.insert(unit);
            }
        }

        // we ignore the first enemy worker in our region since we assume it is a scout
		// This is because we can't catch it early. Should skip this check when we can. 
		// TODO replace with something sensible
        for (auto & unit : enemyUnitsInRegion)
        {
            if (unit->getType().isWorker())
            {
                enemyUnitsInRegion.erase(unit);
                break;
            }
        }

        std::stringstream squadName;
        squadName << "Base Defense " << regionCenter.x << " " << regionCenter.y; 
        
        // if there's nothing in this region to worry about
        if (enemyUnitsInRegion.empty())
        {
            // if a defense squad for this region exists, empty it
            if (_squadData.squadExists(squadName.str()))
            {
				_squadData.getSquad(squadName.str()).clear();
			}
            
            // and return, nothing to defend here
            continue;
        }
        else 
        {
            // if we don't have a squad assigned to this region already, create one
            if (!_squadData.squadExists(squadName.str()))
            {
                SquadOrder defendRegion(SquadOrderTypes::Defend, regionCenter, 32 * 25, "Defend region");
                _squadData.addSquad(Squad(squadName.str(), defendRegion, BaseDefensePriority));
			}
        }

		int numEnemyFlyingInRegion = std::count_if(enemyUnitsInRegion.begin(), enemyUnitsInRegion.end(), [](BWAPI::Unit u) { return u->isFlying(); });
		int numEnemyGroundInRegion = std::count_if(enemyUnitsInRegion.begin(), enemyUnitsInRegion.end(), [](BWAPI::Unit u) { return !u->isFlying(); });

		// assign units to the squad
		UAB_ASSERT(_squadData.squadExists(squadName.str()), "Squad should exist: %s", squadName.str().c_str());
        Squad & defenseSquad = _squadData.getSquad(squadName.str());

        // figure out how many units we need on defense
	    int flyingDefendersNeeded = numDefendersPerEnemyUnit * numEnemyFlyingInRegion;
	    int groundDefendersNeeded = numDefendersPerEnemyUnit * numEnemyGroundInRegion;

		// Count static defense as air defenders.
		for (auto & unit : BWAPI::Broodwar->self()->getUnits()) {
			if ((unit->getType() == BWAPI::UnitTypes::Terran_Missile_Turret ||
				unit->getType() == BWAPI::UnitTypes::Protoss_Photon_Cannon ||
				unit->getType() == BWAPI::UnitTypes::Zerg_Spore_Colony) &&
				unit->isCompleted() && unit->isPowered() &&
				BWTA::getRegion(BWAPI::TilePosition(unit->getPosition())) == myRegion)
			{
				flyingDefendersNeeded -= 3;
			}
		}
		flyingDefendersNeeded = std::max(flyingDefendersNeeded, 0);

		// Count static defense as ground defenders.
		// Cannons get counted as air and ground, which can be a mistake.
		bool sunkenDefender = false;
		for (auto & unit : BWAPI::Broodwar->self()->getUnits()) {
			if ((unit->getType() == BWAPI::UnitTypes::Protoss_Photon_Cannon ||
				unit->getType() == BWAPI::UnitTypes::Zerg_Sunken_Colony) &&
				unit->isCompleted() && unit->isPowered() &&
				BWTA::getRegion(BWAPI::TilePosition(unit->getPosition())) == myRegion)
			{
				sunkenDefender = true;
				groundDefendersNeeded -= 4;
			}
		}
		groundDefendersNeeded = std::max(groundDefendersNeeded, 0);

		// Pull workers only in narrow conditions.
		// Pulling workers (as implemented) can lead to big losses.
		bool pullWorkers =
			Config::Micro::WorkersDefendRush &&
			(!sunkenDefender && numZerglingsInOurBase() > 0 || buildingRush());

		updateDefenseSquadUnits(defenseSquad, flyingDefendersNeeded, groundDefendersNeeded, pullWorkers);
    }

    // for each of our defense squads, if there aren't any enemy units near the position, remove the squad
	// TODO partially overlaps with "is enemy in region check" above
	for (const auto & kv : _squadData.getSquads())
	{
		const Squad & squad = kv.second;
		const SquadOrder & order = squad.getSquadOrder();

		if (order.getType() != SquadOrderTypes::Defend || squad.isEmpty())
		{
			continue;
		}

		bool enemyUnitInRange = false;
		for (const auto unit : BWAPI::Broodwar->enemy()->getUnits())
		{
			if (unit->getPosition().getDistance(order.getPosition()) < order.getRadius())
			{
				enemyUnitInRange = true;
				break;
			}
		}

		if (!enemyUnitInRange)
		{
			_squadData.getSquad(squad.getName()).clear();
		}
	}
}

void CombatCommander::updateDefenseSquadUnits(Squad & defenseSquad, const size_t & flyingDefendersNeeded, const size_t & groundDefendersNeeded, bool pullWorkers)
{
	// if there's nothing left to defend, clear the squad
	if (flyingDefendersNeeded == 0 && groundDefendersNeeded == 0)
	{
		defenseSquad.clear();
		return;
	}

	const BWAPI::Unitset & squadUnits = defenseSquad.getUnits();
	size_t flyingDefendersInSquad = std::count_if(squadUnits.begin(), squadUnits.end(), UnitUtil::CanAttackAir);
	size_t groundDefendersInSquad = std::count_if(squadUnits.begin(), squadUnits.end(), UnitUtil::CanAttackGround);

	// add flying defenders if we still need them
	size_t flyingDefendersAdded = 0;
	while (flyingDefendersNeeded > flyingDefendersInSquad + flyingDefendersAdded)
	{
		BWAPI::Unit defenderToAdd = findClosestDefender(defenseSquad, defenseSquad.getSquadOrder().getPosition(), true, false);

		// if we find a valid flying defender, add it to the squad
		if (defenderToAdd)
		{
			_squadData.assignUnitToSquad(defenderToAdd, defenseSquad);
			++flyingDefendersAdded;
		}
		// otherwise we'll never find another one so break out of this loop
		else
		{
			break;
		}
	}

	// add ground defenders if we still need them
	size_t groundDefendersAdded = 0;
	while (groundDefendersNeeded > groundDefendersInSquad + groundDefendersAdded)
	{
		BWAPI::Unit defenderToAdd = findClosestDefender(defenseSquad, defenseSquad.getSquadOrder().getPosition(), false, pullWorkers);

		// If we find a valid ground defender, add it.
		if (defenderToAdd)
		{
			if (defenderToAdd->getType().isWorker())
			{
				WorkerManager::Instance().setCombatWorker(defenderToAdd);
			}
			_squadData.assignUnitToSquad(defenderToAdd, defenseSquad);
			++groundDefendersAdded;
		}
		// otherwise we'll never find another one so break out of this loop
		else
		{
			break;
		}
	}
}

// Choose a defender to join the base defense squad.
BWAPI::Unit CombatCommander::findClosestDefender(const Squad & defenseSquad, BWAPI::Position pos, bool flyingDefender, bool pullWorkers)
{
	BWAPI::Unit closestDefender = nullptr;
	int minDistance = 99999;

	for (auto & unit : _combatUnits) 
	{
		if ((flyingDefender && !UnitUtil::CanAttackAir(unit)) ||
			(!flyingDefender && !UnitUtil::CanAttackGround(unit)))
        {
            continue;
        }

        if (!_squadData.canAssignUnitToSquad(unit, defenseSquad))
        {
            continue;
        }

		int dist = unit->getDistance(pos);

		// Pull workers only if requested, and not from distant bases.
		if (unit->getType().isWorker() && (!pullWorkers || dist > 1000))
        {
            continue;
        }

		if (dist < minDistance)
        {
            closestDefender = unit;
            minDistance = dist;
        }
	}

	return closestDefender;
}

// If we should, add 1 overlord at the start of the game. Otherwise do nothing.
void CombatCommander::updateSurveySquad()
{
	if (!_squadData.squadExists("Survey"))
	{
		return;
	}

	Squad & surveySquad = _squadData.getSquad("Survey");

	if (BWAPI::Broodwar->getFrameCount() < 10 && surveySquad.isEmpty() && _squadData.squadExists("Idle")) {
		Squad & idleSquad = _squadData.getSquad("Idle");
		const BWAPI::Unit * myOverlord = nullptr;
		for (auto & unit : idleSquad.getUnits())
		{
			if (unit->getType() == BWAPI::UnitTypes::Zerg_Overlord &&
				_squadData.canAssignUnitToSquad(unit, surveySquad))
			{
				myOverlord = &unit;
				break;
			}
		}
		if (myOverlord)
		{
			_squadData.assignUnitToSquad(*myOverlord, surveySquad);
		}
	}
}

// NOTE This implementation is kind of cheesy. Orders ought to be delegated to a squad.
void CombatCommander::loadOrUnloadBunkers()
{
	if (BWAPI::Broodwar->self()->getRace() != BWAPI::Races::Terran)
	{
		return;
	}

	for (auto & bunker : BWAPI::Broodwar->self()->getUnits())
	{
		if (bunker->getType() == BWAPI::UnitTypes::Terran_Bunker)
		{
			// BWAPI::Broodwar->drawCircleMap(bunker->getPosition(), 12 * 32, BWAPI::Colors::Cyan);
			// BWAPI::Broodwar->drawCircleMap(bunker->getPosition(), 18 * 32, BWAPI::Colors::Orange);
			
			// Are there enemies close to the bunker?
			bool enemyIsNear = false;

			// 1. Is any enemy unit within a small radius?
			BWAPI::Unitset enemiesNear = BWAPI::Broodwar->getUnitsInRadius(bunker->getPosition(), 12 * 32,
				BWAPI::Filter::IsEnemy);
			if (enemiesNear.empty())
			{
				// 2. Is a fast enemy unit within a wider radius?
				enemiesNear = BWAPI::Broodwar->getUnitsInRadius(bunker->getPosition(), 18 * 32,
					BWAPI::Filter::IsEnemy &&
						(BWAPI::Filter::GetType == BWAPI::UnitTypes::Terran_Vulture ||
						 BWAPI::Filter::GetType == BWAPI::UnitTypes::Zerg_Mutalisk)
					);
				enemyIsNear = !enemiesNear.empty();
			}
			else
			{
				enemyIsNear = true;
			}

			if (enemyIsNear)
			{
				// Load one marine at a time if there is free space.
				if (bunker->getSpaceRemaining() > 0)
				{
					BWAPI::Unit marine = BWAPI::Broodwar->getClosestUnit(
						bunker->getPosition(),
						BWAPI::Filter::IsOwned && BWAPI::Filter::GetType == BWAPI::UnitTypes::Terran_Marine,
						12 * 32);
					if (marine)
					{
						bunker->load(marine);
					}
				}
			}
			else
			{
				bunker->unloadAll();
			}
		}
	}
}

// Scan enemy cloaked units.
void CombatCommander::doComsatScan()
{
	if (BWAPI::Broodwar->self()->getRace() != BWAPI::Races::Terran)
	{
		return;
	}

	if (UnitUtil::GetCompletedUnitCount(BWAPI::UnitTypes::Terran_Comsat_Station) == 0)
	{
		return;
	}

	// Does the enemy have undetected cloaked units that we may be able to engage?
	for (const auto unit : BWAPI::Broodwar->enemy()->getUnits())
	{
		if (unit->isVisible() &&
			(!unit->isDetected() || unit->getOrder() == BWAPI::Orders::Burrowing) &&
			unit->getPosition().isValid())
		{
			// At most one scan per call. We don't check whether it succeeds.
			(void) Micro::SmartScan(unit->getPosition());
			break;
		}
	}
}

// What units do you want to drop into the enemy base from a transport?
bool CombatCommander::unitIsGoodToDrop(const BWAPI::Unit unit) const
{
	return
		unit->getType() == BWAPI::UnitTypes::Protoss_Dark_Templar ||
		unit->getType() == BWAPI::UnitTypes::Terran_Vulture;
}

// Get our money back at the last moment for stuff that is about to be destroyed.
// It is not ideal: A building which is destined to die only after it is completed
// will be completed and die.
// NOTE See BuildingManager::cancelBuilding() for another way to cancel buildings.
void CombatCommander::cancelDyingBuildings()
{
	for (auto & unit : BWAPI::Broodwar->self()->getUnits())
	{
		if (unit->getType().isBuilding() && !unit->isCompleted() && unit->isUnderAttack() && unit->getHitPoints() < 30)
		{
			if (unit->isMorphing() && unit->canCancelMorph()) {
				unit->cancelMorph();
			}
			else if (unit->isBeingConstructed() && unit->canCancelConstruction()) {
				unit->cancelConstruction();
			}
		}
	}
}

BWAPI::Position CombatCommander::getDefendLocation()
{
	return BWTA::getRegion(InformationManager::Instance().getMyMainBaseLocation()->getTilePosition())->getCenter();
}

void CombatCommander::drawSquadInformation(int x, int y)
{
	_squadData.drawSquadInformation(x, y);
}

// Choose a point of attack for the given squad (which may be null).
BWAPI::Position CombatCommander::getMainAttackLocation(const Squad * squad)
{
	// If we're defensive, try to find a front line to hold.
	if (!_goAggressive)
	{
		// We are guaranteed to always have a main base location, even if it has been destroyed.
		BWTA::BaseLocation * base = InformationManager::Instance().getMyMainBaseLocation();

		// We may have taken our natural. If so, call that the front line.
		BWTA::BaseLocation * natural = InformationManager::Instance().getMyNaturalLocation();
		if (natural && BWAPI::Broodwar->self() == InformationManager::Instance().getBaseOwner(natural))
		{
			base = natural;
		}

		return base->getPosition();
	}

    // What stuff the squad can attack.
	bool canAttackAir = true;
	bool canAttackGround = true;
	if (squad)
	{
		canAttackAir = squad->hasAntiAir();
		canAttackGround = squad->hasAntiGround();
	}

	// Otherwise we are aggressive. Try to find a spot to attack.

	BWTA::BaseLocation * enemyBaseLocation = InformationManager::Instance().getEnemyMainBaseLocation();

	// First choice: Attack the enemy main unless we think it's empty.
	if (enemyBaseLocation)
	{
		BWAPI::Position enemyBasePosition = enemyBaseLocation->getPosition();

		// If the enemy base hasn't been seen yet, go there.
		if (!BWAPI::Broodwar->isExplored(BWAPI::TilePosition(enemyBasePosition)))
		{
			return enemyBasePosition;
		}

		// get all known enemy units in the area
		BWAPI::Unitset enemyUnitsInArea;
		MapGrid::Instance().GetUnits(enemyUnitsInArea, enemyBasePosition, 800, false, true);

		for (const auto unit : enemyUnitsInArea)
		{
			if (unit->getType() != BWAPI::UnitTypes::Zerg_Larva &&
				(unit->isFlying() && canAttackAir || !unit->isFlying() && canAttackGround))
			{
				// Enemy base is not empty: Something interesting is in the enemy base area.
				return enemyBasePosition;
			}
		}
	}

	// Second choice: Attack known enemy buildings.
	// We assume that a terran can lift the buildings; otherwise, the squad must be able to attack ground.
	if (canAttackGround || BWAPI::Broodwar->enemy()->getRace() == BWAPI::Races::Terran)
	{
		for (const auto & kv : InformationManager::Instance().getUnitInfo(BWAPI::Broodwar->enemy()))
		{
			const UnitInfo & ui = kv.second;

			if (ui.type.isBuilding() && ui.lastPosition != BWAPI::Positions::None)
			{
				return ui.lastPosition;
			}
		}
	}

	// Third choice: Attack visible enemy units.
	for (const auto unit : BWAPI::Broodwar->enemy()->getUnits())
	{
		if (unit->getType() == BWAPI::UnitTypes::Zerg_Larva ||
			!UnitUtil::IsValidUnit(unit) ||
			!unit->isVisible())
		{
			continue;
		}

		if (unit->isFlying() && canAttackAir || !unit->isFlying() && canAttackGround)
		{
			return unit->getPosition();
		}
	}

	// Fourth choice: We can't see anything so explore the map attacking along the way
	return MapGrid::Instance().getLeastExplored();
}

BWAPI::Position CombatCommander::getSurveyLocation()
{
	BWTA::BaseLocation * ourBaseLocation = InformationManager::Instance().getMyMainBaseLocation();
	BWTA::BaseLocation * enemyBaseLocation = InformationManager::Instance().getEnemyMainBaseLocation();

	// If it's a 2-player map, or we miraculously know the enemy base location, that's it.
	if (enemyBaseLocation)
	{
		return enemyBaseLocation->getPosition();
	}

	// Otherwise just pick a base, any base that's not ours.
	for (BWTA::BaseLocation * startLocation : BWTA::getStartLocations())
	{
		if (startLocation && startLocation != ourBaseLocation && startLocation->getPosition().isValid())
		{
			return startLocation->getPosition();
		}
	}

	if (ourBaseLocation && ourBaseLocation->getPosition().isValid()) {
		UAB_ASSERT(false, "map seems to have only 1 base");
		return ourBaseLocation->getPosition();
	}
	else {
		UAB_ASSERT(false, "map seems to have no bases");
		return BWAPI::Position(0, 0);
	}
}

// Choose one worker to pull for scout defense.
BWAPI::Unit CombatCommander::findClosestWorkerToTarget(BWAPI::Unitset & unitsToAssign, BWAPI::Unit target)
{
    UAB_ASSERT(target != nullptr, "target was null");

    if (!target)
    {
        return nullptr;
    }

    BWAPI::Unit closestMineralWorker = nullptr;
	int closestDist = Config::Micro::ScoutDefenseRadius + 128;    // more distant workers do not get pulled
    
	for (auto & unit : unitsToAssign)
	{
		if (unit->getType().isWorker() && WorkerManager::Instance().isFree(unit))
		{
			int dist = unit->getDistance(target);
			if (unit->isCarryingMinerals())
			{
				dist += 96;
			}

            if (dist < closestDist)
            {
                closestMineralWorker = unit;
                dist = closestDist;
            }
		}
	}

    return closestMineralWorker;
}

int CombatCommander::numZerglingsInOurBase() const
{
    const int concernRadius = 300;
    int zerglings = 0;
	
	BWTA::BaseLocation * main = InformationManager::Instance().getMyMainBaseLocation();
	BWAPI::Position myBasePosition(main->getPosition());

    for (auto unit : BWAPI::Broodwar->enemy()->getUnits())
    {
        if (unit->getType() == BWAPI::UnitTypes::Zerg_Zergling &&
			unit->getDistance(myBasePosition) < concernRadius)
        {
			++zerglings;
		}
    }

	return zerglings;
}

// Is an enemy building near our base? If so, we may pull workers.
bool CombatCommander::buildingRush() const
{
	// If we have units, there will be no need to pull workers.
	if (InformationManager::Instance().weHaveCombatUnits())
	{
		return false;
	}

	BWTA::BaseLocation * main = InformationManager::Instance().getMyMainBaseLocation();
	BWAPI::Position myBasePosition(main->getPosition());

    for (auto & unit : BWAPI::Broodwar->enemy()->getUnits())
    {
        if (unit->getType().isBuilding() && unit->getDistance(myBasePosition) < 1200)
        {
            return true;
        }
    }

    return false;
}

CombatCommander & CombatCommander::Instance()
{
	static CombatCommander instance;
	return instance;
}
