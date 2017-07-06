#include "Squad.h"
#include "UnitUtil.h"

using namespace UAlbertaBot;

Squad::Squad()
    : _name("Default")
	, _attackAtMax(false)
    , _lastRetreatSwitch(0)
    , _lastRetreatSwitchVal(false)
    , _priority(0)
{
    int a = 10;   // only you can prevent linker errors
}

Squad::Squad(const std::string & name, SquadOrder order, size_t priority) 
	: _name(name)
	, _attackAtMax(false)
	, _lastRetreatSwitch(0)
    , _lastRetreatSwitchVal(false)
    , _priority(priority)
	, _order(order)
{
}

Squad::~Squad()
{
    clear();
}

void Squad::update()
{
	// update all necessary unit information within this squad
	updateUnits();

	// TODO This is a crude temporary stand-in for a real survey squad controller.
	if (_order.getType() == SquadOrderTypes::Survey && BWAPI::Broodwar->getFrameCount() < 24)
	{
		if (_units.empty())
		{
			return;
		}

		BWAPI::Unit surveyor = *(_units.begin());
		if (surveyor && surveyor->exists())
		{
			Micro::SmartMove(surveyor, _order.getPosition());
		}
		return;
	}

	// determine whether or not we should regroup
	bool needToRegroup = needsToRegroup();
    
	// draw some debug info
	if (Config::Debug::DrawSquadInfo && _order.getType() == SquadOrderTypes::Attack) 
	{
		BWAPI::Broodwar->drawTextScreen(200, 350, "%c%s", white, _regroupStatus.c_str());
	}

	// if we do need to regroup, do it
	if (needToRegroup)
	{
		BWAPI::Position regroupPosition = calcRegroupPosition();

        if (Config::Debug::DrawCombatSimulationInfo)
        {
		    BWAPI::Broodwar->drawTextScreen(200, 150, "REGROUP");
        }

		if (Config::Debug::DrawSquadInfo)
		{
			BWAPI::Broodwar->drawCircleMap(regroupPosition.x, regroupPosition.y, 30, BWAPI::Colors::Purple, true);
		}
        
		_meleeManager.regroup(regroupPosition);
		_rangedManager.regroup(regroupPosition);
		_lurkerManager.regroup(regroupPosition);
        _tankManager.regroup(regroupPosition);
        _medicManager.regroup(regroupPosition);
		// NOTE Detectors and transports do not regroup.
	}
	else // otherwise, execute micro
	{
		_meleeManager.execute(_order);
		_rangedManager.execute(_order);
		_lurkerManager.execute(_order);
		_tankManager.execute(_order);
        _medicManager.execute(_order);

		_transportManager.update();
		_detectorManager.setUnitClosestToEnemy(unitClosestToEnemy());
		_detectorManager.execute(_order);
	}
}

bool Squad::isEmpty() const
{
    return _units.empty();
}

size_t Squad::getPriority() const
{
    return _priority;
}

void Squad::setPriority(const size_t & priority)
{
    _priority = priority;
}

void Squad::updateUnits()
{
	setAllUnits();
	setNearEnemyUnits();
	addUnitsToMicroManagers();
}

void Squad::setAllUnits()
{
	// Clean up the _units vector in case one of them died or went into a bunker or transport.
	// If the unit is unloaded later, it will be added back to a squad.
	BWAPI::Unitset goodUnits;
	for (auto & unit : _units)
	{
		if (UnitUtil::IsValidUnit(unit) && !unit->isLoaded())
		{
			goodUnits.insert(unit);
		}
	}
	_units = goodUnits;
}

void Squad::setNearEnemyUnits()
{
	_nearEnemy.clear();
	for (auto & unit : _units)
	{
		int x = unit->getPosition().x;
		int y = unit->getPosition().y;

		int left = unit->getType().dimensionLeft();
		int right = unit->getType().dimensionRight();
		int top = unit->getType().dimensionUp();
		int bottom = unit->getType().dimensionDown();

		_nearEnemy[unit] = unitNearEnemy(unit);

		if (Config::Debug::DrawSquadInfo) {
			BWAPI::Broodwar->drawBoxMap(x - left, y - top, x + right, y + bottom,
				(_nearEnemy[unit]) ? Config::Debug::ColorUnitNearEnemy : Config::Debug::ColorUnitNotNearEnemy);
		}
	}
}

void Squad::addUnitsToMicroManagers()
{
	BWAPI::Unitset meleeUnits;
	BWAPI::Unitset rangedUnits;
	BWAPI::Unitset detectorUnits;
	BWAPI::Unitset transportUnits;
	BWAPI::Unitset lurkerUnits;
    BWAPI::Unitset tankUnits;
    BWAPI::Unitset medicUnits;

	for (auto & unit : _units)
	{
		UAB_ASSERT(unit, "missing unit");
		if (unit->isCompleted() && unit->getHitPoints() > 0 && unit->exists())
		{
			if (unit->getType().isWorker())
			{
				// We accept workers into the squad, but do not give them orders.
				// WorkerManager is responsible for that.
				// The squad creator (in CombatCommander) should be sure to give each worker
				// an appropriate job using the WorkerManager.
				// squad.clear() releases the worker jobs, so you don't have to worry about that.
			}
            else if (unit->getType() == BWAPI::UnitTypes::Terran_Medic)
            {
                medicUnits.insert(unit);
            }
			else if (unit->getType() == BWAPI::UnitTypes::Zerg_Lurker)
			{
				lurkerUnits.insert(unit);
			}
			else if (unit->getType() == BWAPI::UnitTypes::Terran_Siege_Tank_Siege_Mode ||
				unit->getType() == BWAPI::UnitTypes::Terran_Siege_Tank_Tank_Mode)
            {
                tankUnits.insert(unit);
            }   
			else if (unit->getType().isDetector() && !unit->getType().isBuilding())
			{
				detectorUnits.insert(unit);
			}
			// TOOO excludes overlords (which are also detectors, a confusing case)
			else if (unit->getType() == BWAPI::UnitTypes::Protoss_Shuttle ||
				unit->getType() == BWAPI::UnitTypes::Terran_Dropship)
			{
				transportUnits.insert(unit);
			}
			// TODO excludes some units: spellcasters, carriers, reavers; also valkyries, corsairs, devourers
			else if ((unit->getType().groundWeapon().maxRange() > 32) ||
				unit->getType() == BWAPI::UnitTypes::Zerg_Scourge)
			{
				rangedUnits.insert(unit);
			}
			else if (unit->getType().groundWeapon().maxRange() <= 32)
			{
				meleeUnits.insert(unit);
			}
			// Note: Some units may fall through and not be assigned.
		}
	}

	_meleeManager.setUnits(meleeUnits);
	_rangedManager.setUnits(rangedUnits);
	_detectorManager.setUnits(detectorUnits);
	_transportManager.setUnits(transportUnits);
	_lurkerManager.setUnits(lurkerUnits);
	_tankManager.setUnits(tankUnits);
    _medicManager.setUnits(medicUnits);
}

// Calculates whether to regroup, aka retreat. Does combat sim if necessary.
bool Squad::needsToRegroup()
{
	// if we are not attacking, never regroup
	if (_units.empty() || (_order.getType() != SquadOrderTypes::Attack))
	{
		_regroupStatus = std::string("No attackers available");
		return false;
	}

	// If we're nearly maxed and have good income or cash, don't retreat.
	if (BWAPI::Broodwar->self()->supplyUsed() >= 390 &&
		(BWAPI::Broodwar->self()->minerals() > 1000 || WorkerManager::Instance().getNumMineralWorkers() > 12))
	{
		_attackAtMax = true;
	}

	if (_attackAtMax)
	{
		if (BWAPI::Broodwar->self()->supplyUsed() < 320)
		{
			_attackAtMax = false;
		}
		else
		{
			_regroupStatus = std::string("Maxed. Banzai!");
			return false;
		}
	}

    BWAPI::Unit unitClosest = unitClosestToEnemy();

	if (!unitClosest)
	{
		_regroupStatus = std::string("No closest unit");
		return false;
	}

    std::vector<UnitInfo> enemyCombatUnits;
    const auto & enemyUnitInfo = InformationManager::Instance().getUnitInfo(BWAPI::Broodwar->enemy());

	// if none of our units are in attack range of any enemy units, don't retreat
	bool anyInRange = false;
    for (const auto & eui : enemyUnitInfo)
    {
        for (const auto & u : _units)
        {
            int range = UnitUtil::GetAttackRangeAssumingUpgrades(eui.second.type, u->getType());

            if (range + 128 >= eui.second.lastPosition.getDistance(u->getPosition()))
            {
				anyInRange = true;
                break;   // break out of inner loop
            }
        }

		if (anyInRange)
        {
            break;       // break out of outer loop
        }
    }

    if (!anyInRange)
    {
        _regroupStatus = std::string("No enemy units in attack range");
        return false;
    }

	// if we are DT rushing and we haven't lost a DT yet, no retreat!
	if (StrategyManager::Instance().getOpeningGroup() == "dark templar" && (BWAPI::Broodwar->self()->deadUnitCount(BWAPI::UnitTypes::Protoss_Dark_Templar) == 0))
	{
		_regroupStatus = std::string("GO DARK TEMPLAR!");
		return false;
	}

	SparCraft::ScoreType score = 0;

	//do the SparCraft Simulation!
	CombatSimulation sim;
    
	sim.setCombatUnits(unitClosest->getPosition(), Config::Micro::CombatRegroupRadius);
	score = sim.simulateCombat();

    bool retreat = score < 0;
    int switchTime = 100;
    bool waiting = false;

    // we should not attack unless 5 seconds have passed since a retreat
    if (retreat != _lastRetreatSwitchVal)
    {
        if (!retreat && (BWAPI::Broodwar->getFrameCount() - _lastRetreatSwitch < switchTime))
        {
            waiting = true;
            retreat = _lastRetreatSwitchVal;
        }
        else
        {
            waiting = false;
            _lastRetreatSwitch = BWAPI::Broodwar->getFrameCount();
            _lastRetreatSwitchVal = retreat;
        }
    }
	
	if (retreat)
	{
		_regroupStatus = std::string("Retreat");
	}
	else
	{
		_regroupStatus = std::string("Attack");
	}

	return retreat;
}

void Squad::setSquadOrder(const SquadOrder & so)
{
	_order = so;
}

bool Squad::containsUnit(BWAPI::Unit u) const
{
    return _units.contains(u);
}

void Squad::clear()
{
    for (auto & unit : getUnits())
    {
        if (unit->getType().isWorker())
        {
            WorkerManager::Instance().finishedWithWorker(unit);
        }
    }

    _units.clear();
}

bool Squad::unitNearEnemy(BWAPI::Unit unit)
{
	UAB_ASSERT(unit, "missing unit");

	BWAPI::Unitset enemyNear;

	MapGrid::Instance().GetUnits(enemyNear, unit->getPosition(), 400, false, true);

	return enemyNear.size() > 0;
}

BWAPI::Position Squad::calcCenter()
{
    if (_units.empty())
    {
        if (Config::Debug::DrawSquadInfo)
        {
            BWAPI::Broodwar->printf("Squad::calcCenter() called on empty squad");
        }
        return BWAPI::Position(0,0);
    }

	BWAPI::Position accum(0,0);
	for (auto & unit : _units)
	{
		accum += unit->getPosition();
	}
	return BWAPI::Position(accum.x / _units.size(), accum.y / _units.size());
}

BWAPI::Position Squad::calcRegroupPosition()
{
	BWAPI::Position regroup(0,0);

	int minDist = 100000;

	// Retreat to the location of whatever unit not near the enemy which is
	// closest to the order's target destination.
	// NOTE May retreat somewhere silly if the chosen unit was newly produced.
	//      Zerg sometimes retreats back and forth through the enemy when new
	//      zerg units are produced in bases on opposite sides.
	for (auto & unit : _units)
	{
		// Don't return the position of an overlord, which may be in a weird place.
		// Bug fix thanks to AIL!
		if (!_nearEnemy[unit] &&
			unit->getType() != BWAPI::UnitTypes::Zerg_Overlord &&
			unit->getType() != BWAPI::UnitTypes::Protoss_Observer)
		{
			int dist = unit->getDistance(_order.getPosition());
			if (dist < minDist)
			{
				minDist = dist;
				regroup = unit->getPosition();
			}
		}
	}

	// Failing that, retreat to a base we own.
	if (regroup == BWAPI::Position(0,0))
	{
		// Retreat to the main base (guaranteed not null, even if the buildings were destroyed).
		BWTA::BaseLocation * base = InformationManager::Instance().getMyMainBaseLocation();

		// If the natural has been taken, retreat there instead.
		if (InformationManager::Instance().getMyNaturalLocation() &&
			InformationManager::Instance().getBaseOwner(InformationManager::Instance().getMyNaturalLocation()) == BWAPI::Broodwar->self())
		{
			base = InformationManager::Instance().getMyNaturalLocation();
		}
		return BWTA::getRegion(base->getTilePosition())->getCenter();
	}
	return regroup;
}

BWAPI::Unit Squad::unitClosestToEnemy()
{
	BWAPI::Unit closest = nullptr;
	int closestDist = 100000;

	UAB_ASSERT(_order.getPosition().isValid(), "bad order position");

	for (auto & unit : _units)
	{
		if (unit->getType() == BWAPI::UnitTypes::Zerg_Overlord ||
			unit->getType() == BWAPI::UnitTypes::Protoss_Observer)
		{
			continue;
		}

		// the distance to the order position
		int dist = MapTools::Instance().getGroundDistance(unit->getPosition(), _order.getPosition());

		if (dist != -1 && dist < closestDist)
		{
			closest = unit;
			closestDist = dist;
		}
	}

	return closest;
}

int Squad::squadUnitsNear(BWAPI::Position p)
{
	int numUnits = 0;

	for (auto & unit : _units)
	{
		if (unit->getDistance(p) < 600)
		{
			numUnits++;
		}
	}

	return numUnits;
}

const BWAPI::Unitset & Squad::getUnits() const	
{ 
	return _units; 
} 

const SquadOrder & Squad::getSquadOrder()	const			
{ 
	return _order; 
}

void Squad::addUnit(BWAPI::Unit u)
{
	_units.insert(u);
}

// NOTE: If the unit is a worker, you may have to release it before calling this.
void Squad::removeUnit(BWAPI::Unit u)
{
    _units.erase(u);
}

const std::string & Squad::getName() const
{
    return _name;
}