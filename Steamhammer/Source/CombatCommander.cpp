#include "CombatCommander.h"
#include "UnitUtil.h"

using namespace UAlbertaBot;

const size_t IdlePriority = 0;
const size_t AttackPriority = 2;
const size_t BaseDefensePriority = 3;
const size_t ScoutDefensePriority = 4;
const size_t DropPriority = 5;
const size_t SurveyPriority = 10;    // consists of only 1 overlord, no need to steal from it

CombatCommander::CombatCommander() 
    : _initialized(false)
{
}

void CombatCommander::initializeSquads()
{
	// The idle squad includes workers at work (not idle at all) and unassigned overlords.
    SquadOrder idleOrder(SquadOrderTypes::Idle, BWAPI::Position(BWAPI::Broodwar->self()->getStartLocation()), 100, "Chill Out");
	_squadData.addSquad("Idle", Squad("Idle", idleOrder, IdlePriority));

    // the main attack squad will pressure the enemy's closest base location
    SquadOrder mainAttackOrder(SquadOrderTypes::Attack, getMainAttackLocation(), 800, "Attack Enemy Base");
	_squadData.addSquad("MainAttack", Squad("MainAttack", mainAttackOrder, AttackPriority));

	// The flying squad separates air units so they can act independently.
	// It gets the same order as the attack squad.
	_squadData.addSquad("Flying", Squad("Flying", mainAttackOrder, AttackPriority));

	BWAPI::Position ourBasePosition = BWAPI::Position(BWAPI::Broodwar->self()->getStartLocation());

    // the scout defense squad will handle chasing the enemy worker scout
    SquadOrder enemyScoutDefense(SquadOrderTypes::Defend, ourBasePosition, 900, "Get the scout");
    _squadData.addSquad("ScoutDefense", Squad("ScoutDefense", enemyScoutDefense, ScoutDefensePriority));

	// add a drop squad if we are using a drop strategy
    if (Config::Strategy::StrategyName == "Protoss_Drop")
    {
        SquadOrder zealotDrop(SquadOrderTypes::Drop, ourBasePosition, 900, "Wait for transport");
        _squadData.addSquad("Drop", Squad("Drop", zealotDrop, DropPriority));
    }

	// Zerg can put overlords into a survey squad.
	// With no evasion skills, it's dangerous to do that against terran.
	if (BWAPI::Broodwar->self()->getRace() == BWAPI::Races::Zerg &&
		BWAPI::Broodwar->enemy()->getRace() != BWAPI::Races::Terran)
	{
		SquadOrder surveyMap(SquadOrderTypes::Survey, getSurveyLocation(), 100, "Get the surveyors");
		_squadData.addSquad("Survey", Squad("Survey", surveyMap, SurveyPriority));
	}

    _initialized = true;
}

bool CombatCommander::isSquadUpdateFrame()
{
	return BWAPI::Broodwar->getFrameCount() % 11 == 0;
}

void CombatCommander::update(const BWAPI::Unitset & combatUnits)
{
    if (!Config::Modules::UsingCombatCommander)
    {
        return;
    }

    if (!_initialized)
    {
        initializeSquads();
    }

    _combatUnits = combatUnits;


	if (isSquadUpdateFrame())
	{
        updateIdleSquad();
        updateDropSquads();
        updateScoutDefenseSquad();
		updateDefenseSquads();
		updateAttackSquads();
		updateSurveySquad();
	}

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

void CombatCommander::updateAttackSquads()
{
    Squad & mainAttackSquad = _squadData.getSquad("MainAttack");
	Squad & flyingSquad = _squadData.getSquad("Flying");

	// Include exactly 1 overlord in each squad, for detection.
	bool mainOverlord = false;
	bool mainSquadExists = false;
	for (auto & unit : mainAttackSquad.getUnits())
	{
		if (unit->getType() == BWAPI::UnitTypes::Zerg_Overlord)
		{
			mainOverlord = true;
		}
		else
		{
			mainSquadExists = true;
		}
	}

	bool flyingOverlord = false;
	bool flyingSquadExists = false;	    // scourge goes to flying squad if any, otherwise main squad
	for (auto & unit : flyingSquad.getUnits())
	{
		if (unit->getType() == BWAPI::UnitTypes::Zerg_Overlord)
		{
			flyingOverlord = true;
		}
		else if (unit->getType() == BWAPI::UnitTypes::Zerg_Mutalisk)
		{
			flyingSquadExists = true;
		}
	}

	for (auto & unit : _combatUnits)
    {
		// Here's a weird bit of strategy left over from UAlbertaBot.
		// What is the effect?
        //if (unit->getType() == BWAPI::UnitTypes::Zerg_Scourge && UnitUtil::GetAllUnitCount(BWAPI::UnitTypes::Zerg_Hydralisk) < 30)
        //{
        //    continue;
        //}
		
        // get every unit of a lower priority and put it into an attack squad
		bool isOverlord = unit->getType() == BWAPI::UnitTypes::Zerg_Overlord;
		if ((unit->getType() == BWAPI::UnitTypes::Zerg_Mutalisk ||
			(unit->getType() == BWAPI::UnitTypes::Zerg_Scourge && flyingSquadExists) ||
			(isOverlord && !flyingOverlord && flyingSquadExists)) &&
			_squadData.canAssignUnitToSquad(unit, flyingSquad))
		{
			_squadData.assignUnitToSquad(unit, flyingSquad);
			if (isOverlord)
			{
				flyingOverlord = true;
			}
		}
        else if (!unit->getType().isWorker() &&
			(!isOverlord || isOverlord && !mainOverlord && mainSquadExists) &&
			_squadData.canAssignUnitToSquad(unit, mainAttackSquad))
        {
			_squadData.assignUnitToSquad(unit, mainAttackSquad);
			if (isOverlord)
			{
				mainOverlord = true;
			}
        }
    }

    SquadOrder mainAttackOrder(SquadOrderTypes::Attack, getMainAttackLocation(), 800, "Attack Enemy Base");
    mainAttackSquad.setSquadOrder(mainAttackOrder);
	flyingSquad.setSquadOrder(mainAttackOrder);
}

void CombatCommander::updateDropSquads()
{
    if (Config::Strategy::StrategyName != "Protoss_Drop")
    {
        return;
    }

    Squad & dropSquad = _squadData.getSquad("Drop");

    // figure out how many units the drop squad needs
    bool dropSquadHasTransport = false;
    int transportSpotsRemaining = 8;
    auto & dropUnits = dropSquad.getUnits();

    for (auto & unit : dropUnits)
    {
        if (unit->isFlying() && unit->getType().spaceProvided() > 0)
        {
            dropSquadHasTransport = true;
        }
        else
        {
            transportSpotsRemaining -= unit->getType().spaceRequired();
        }
    }

    // if there are still units to be added to the drop squad, do it
    if (transportSpotsRemaining > 0 || !dropSquadHasTransport)
    {
        // take our first amount of combat units that fill up a transport and add them to the drop squad
        for (auto & unit : _combatUnits)
        {
            // if this is a transport unit and we don't have one in the squad yet, add it
            if (!dropSquadHasTransport && (unit->getType().spaceProvided() > 0 && unit->isFlying()))
            {
                _squadData.assignUnitToSquad(unit, dropSquad);
                dropSquadHasTransport = true;
                continue;
            }

            if (unit->getType().spaceRequired() > transportSpotsRemaining)
            {
                continue;
            }

            // get every unit of a lower priority and put it into the attack squad
            if (!unit->getType().isWorker() && _squadData.canAssignUnitToSquad(unit, dropSquad))
            {
                _squadData.assignUnitToSquad(unit, dropSquad);
                transportSpotsRemaining -= unit->getType().spaceRequired();
            }
        }
    }
    // otherwise the drop squad is full, so execute the order
    else
    {
        SquadOrder dropOrder(SquadOrderTypes::Drop, getMainAttackLocation(), 800, "Attack Enemy Base");
        dropSquad.setSquadOrder(dropOrder);
    }
}

void CombatCommander::updateScoutDefenseSquad() 
{
    if (_combatUnits.empty()) 
    { 
        return; 
    }

    // if the current squad has units in it then we can ignore this
    Squad & scoutDefenseSquad = _squadData.getSquad("ScoutDefense");
  
    // get the region that our base is located in
    BWTA::Region * myRegion = BWTA::getRegion(BWAPI::Broodwar->self()->getStartLocation());
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
    // if our squad is not empty and we shouldn't have a worker chasing then take him out of the squad
    else if (!scoutDefenseSquad.isEmpty() && !assignScoutDefender)
    {
        for (auto & unit : scoutDefenseSquad.getUnits())
        {
            unit->stop();
            if (unit->getType().isWorker())
            {
                WorkerManager::Instance().finishedWithWorker(unit);
            }
        }

        scoutDefenseSquad.clear();
    }
}

void CombatCommander::updateDefenseSquads() 
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
            // if it's an overlord, don't worry about it for defense
            if (unit->getType() == BWAPI::UnitTypes::Zerg_Overlord)
            {
                continue;
            }

            if (BWTA::getRegion(BWAPI::TilePosition(unit->getPosition())) == myRegion)
            {
                enemyUnitsInRegion.insert(unit);
            }
        }

        // we can ignore the first enemy worker in our region since we assume it is a scout
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

        int numEnemyFlyingInRegion = std::count_if(enemyUnitsInRegion.begin(), enemyUnitsInRegion.end(), [](BWAPI::Unit u) { return u->isFlying(); });
        int numEnemyGroundInRegion = std::count_if(enemyUnitsInRegion.begin(), enemyUnitsInRegion.end(), [](BWAPI::Unit u) { return !u->isFlying(); });

        std::stringstream squadName;
        squadName << "Base Defense " << regionCenter.x << " " << regionCenter.y; 
        
        // if there's nothing in this region to worry about
        if (enemyUnitsInRegion.empty())
        {
            // if a defense squad for this region exists, remove it
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
                SquadOrder defendRegion(SquadOrderTypes::Defend, regionCenter, 32 * 25, "Defend Region!");
                _squadData.addSquad(squadName.str(), Squad(squadName.str(), defendRegion, BaseDefensePriority));
            }
        }

        // assign units to the squad
        if (_squadData.squadExists(squadName.str()))
        {
            Squad & defenseSquad = _squadData.getSquad(squadName.str());

            // figure out how many units we need on defense
	        int flyingDefendersNeeded = numDefendersPerEnemyUnit * numEnemyFlyingInRegion;
	        int groundDefendersNeeded = numDefendersPerEnemyUnit * numEnemyGroundInRegion;

			// Count static defense as ground defenders.
			for (auto & unit : BWAPI::Broodwar->self()->getUnits()) {
				if ((unit->getType() == BWAPI::UnitTypes::Protoss_Photon_Cannon ||
					unit->getType() == BWAPI::UnitTypes::Zerg_Sunken_Colony) &&
					unit->isCompleted() && unit->isPowered() &&
					BWTA::getRegion(BWAPI::TilePosition(unit->getPosition())) == myRegion)
				{
					groundDefendersNeeded -= 4;   // pretend a sunken can hold 2 zerglings
				}
			}
			groundDefendersNeeded = std::max(groundDefendersNeeded, 0);

			updateDefenseSquadUnits(defenseSquad, flyingDefendersNeeded, groundDefendersNeeded);
        }
        else
        {
            UAB_ASSERT_WARNING(false, "Squad should have existed: %s", squadName.str().c_str());
        }
	}

    // for each of our defense squads, if there aren't any enemy units near the position, remove the squad
	for (const auto & kv : _squadData.getSquads())
	{
		const Squad & squad = kv.second;
		const SquadOrder & order = squad.getSquadOrder();

		if (order.getType() != SquadOrderTypes::Defend)
		{
			continue;
		}

		bool enemyUnitInRange = false;
		for (auto & unit : BWAPI::Broodwar->enemy()->getUnits())
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

void CombatCommander::updateDefenseSquadUnits(Squad & defenseSquad, const size_t & flyingDefendersNeeded, const size_t & groundDefendersNeeded)
{
	const BWAPI::Unitset & squadUnits = defenseSquad.getUnits();
	size_t flyingDefendersInSquad = std::count_if(squadUnits.begin(), squadUnits.end(), UnitUtil::CanAttackAir);
	size_t groundDefendersInSquad = std::count_if(squadUnits.begin(), squadUnits.end(), UnitUtil::CanAttackGround);

	// if there's nothing left to defend, clear the squad
	if (flyingDefendersNeeded == 0 && groundDefendersNeeded == 0)
	{
		defenseSquad.clear();
		return;
	}

	// Add an overlord if we don't have one.
	// TODO
	
	// add flying defenders if we still need them
	size_t flyingDefendersAdded = 0;
	while (flyingDefendersNeeded > flyingDefendersInSquad + flyingDefendersAdded)
	{
		BWAPI::Unit defenderToAdd = findClosestDefender(defenseSquad, defenseSquad.getSquadOrder().getPosition(), true);

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
		BWAPI::Unit defenderToAdd = findClosestDefender(defenseSquad, defenseSquad.getSquadOrder().getPosition(), false);

		// if we find a valid ground defender add it
		if (defenderToAdd)
		{
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
BWAPI::Unit CombatCommander::findClosestDefender(const Squad & defenseSquad, BWAPI::Position pos, bool flyingDefender)
{
	BWAPI::Unit closestDefender = nullptr;
	double minDistance = std::numeric_limits<double>::max();

	int nLings = numZerglingsInOurBase();
	bool zerglingRush = nLings > 0 && nLings < 10 && BWAPI::Broodwar->getFrameCount() < 6000;
	bool pullWorkers = Config::Micro::WorkersDefendRush && (zerglingRush || beingBuildingRushed());

	for (auto & unit : _combatUnits) 
	{
		if ((flyingDefender && !UnitUtil::CanAttackAir(unit)) || (!flyingDefender && !UnitUtil::CanAttackGround(unit)))
        {
            continue;
        }

        if (!_squadData.canAssignUnitToSquad(unit, defenseSquad))
        {
            continue;
        }


		// Pull workers only a short distance, if at all.
        if (unit->getType().isWorker() && !pullWorkers)
        {
            continue;
        }

		double dist = unit->getDistance(pos);
		if (!closestDefender || (dist < minDistance))
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

// Get our money back for stuff that is about to be destroyed.
// TODO does this work for protoss buildings?
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

// TODO does this have any failure conditions?
BWAPI::Position CombatCommander::getDefendLocation()
{
	return BWTA::getRegion(BWTA::getStartLocation(BWAPI::Broodwar->self())->getTilePosition())->getCenter();
}

void CombatCommander::drawSquadInformation(int x, int y)
{
	_squadData.drawSquadInformation(x, y);
}

BWAPI::Position CombatCommander::getMainAttackLocation()
{
	BWTA::BaseLocation * enemyBaseLocation = InformationManager::Instance().getMainBaseLocation(BWAPI::Broodwar->enemy());

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

		for (auto & unit : enemyUnitsInArea)
		{
			if (unit->getType() != BWAPI::UnitTypes::Zerg_Overlord)
			{
				// Enemy base is not empty: It's not only overlords in the enemy base area.
				return enemyBasePosition;
			}
		}
	}

	// Second choice: Attack known enemy buildings
	for (const auto & kv : InformationManager::Instance().getUnitInfo(BWAPI::Broodwar->enemy()))
	{
		const UnitInfo & ui = kv.second;

		if (ui.type.isBuilding() && ui.lastPosition != BWAPI::Positions::None)
		{
			return ui.lastPosition;
		}
	}

	// Third choice: Attack visible enemy units that aren't overlords
	// TODO should distinguish depending on the squad's air and ground weapons
	for (auto & unit : BWAPI::Broodwar->enemy()->getUnits())
	{
		if (unit->getType() == BWAPI::UnitTypes::Zerg_Overlord)
		{
			continue;
		}

		if (UnitUtil::IsValidUnit(unit) && unit->isVisible())
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
    double closestDist = 128;    // more distant workers do not get pulled
    
    // for each of our workers
	for (auto & unit : unitsToAssign)
	{
		if (unit->getType().isWorker() && WorkerManager::Instance().isFree(unit))
		{
			double dist = unit->getDistance(target);

            if (dist < closestDist)
            {
                closestMineralWorker = unit;
                dist = closestDist;
            }
		}
	}

    return closestMineralWorker;
}

// when do we want to defend with our workers?
// this function can only be called if we have no fighters to defend with
int CombatCommander::defendWithWorkers()
{
	// our home nexus position
	BWAPI::Position homePosition = BWTA::getStartLocation(BWAPI::Broodwar->self())->getPosition();;

	// enemy units near our workers
	int enemyUnitsNearWorkers = 0;

	// defense radius of nexus
	int defenseRadius = 300;

	// fill the set with the types of units we're concerned about
	for (auto & unit : BWAPI::Broodwar->enemy()->getUnits())
	{
		// if it's a zergling or a worker we want to defend
		if (unit->getType() == BWAPI::UnitTypes::Zerg_Zergling)
		{
			if (unit->getDistance(homePosition) < defenseRadius)
			{
				enemyUnitsNearWorkers++;
			}
		}
	}

	// if there are enemy units near our workers, we want to defend
	return enemyUnitsNearWorkers;
}

int CombatCommander::numZerglingsInOurBase()
{
    int concernRadius = 600;
    int zerglings = 0;
    BWAPI::Position ourBasePosition = BWAPI::Position(BWAPI::Broodwar->self()->getStartLocation());
    
    // check to see if the enemy has zerglings as the only attackers in our base
    for (auto & unit : BWAPI::Broodwar->enemy()->getUnits())
    {
        if (unit->getType() != BWAPI::UnitTypes::Zerg_Zergling)
        {
            continue;
        }

        if (unit->getDistance(ourBasePosition) < concernRadius)
        {
            zerglings++;
        }
    }

    return zerglings;
}

bool CombatCommander::beingBuildingRushed()
{
	BWAPI::Position myBasePosition(BWAPI::Broodwar->self()->getStartLocation());

    // check to see if the enemy has buildings near our base
    for (auto & unit : BWAPI::Broodwar->enemy()->getUnits())
    {
        if (unit->getType().isBuilding() && unit->getDistance(myBasePosition) < 1200)
        {
            return true;
        }
    }

    return false;
}