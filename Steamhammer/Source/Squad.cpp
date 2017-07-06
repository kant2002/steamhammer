#include "Squad.h"
#include "UnitUtil.h"

using namespace UAlbertaBot;

Squad::Squad()
	: _hasAir(false)
	, _hasGround(false)
	, _hasAntiAir(false)
	, _hasAntiGround(false)
	, _name("Default")
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

// TODO make a proper dispatch system for different orders
void Squad::update()
{
	// update all necessary unit information within this squad
	updateUnits();

	if (_units.empty())
	{
		return;
	}

	_microHighTemplar.update();

	// TODO This is a crude stand-in for a real survey squad controller.
	if (_order.getType() == SquadOrderTypes::Survey && BWAPI::Broodwar->getFrameCount() < 24)
	{
		BWAPI::Unit surveyor = *(_units.begin());
		if (surveyor && surveyor->exists())
		{
			Micro::SmartMove(surveyor, _order.getPosition());
		}
		return;
	}

	if (_order.getType() == SquadOrderTypes::Load)
	{
		loadTransport();
		return;
	}

	if (_order.getType() == SquadOrderTypes::Drop)
	{
		_microTransports.update();
		// And fall through to let the rest of the drop squad attack.
	}

	bool needToRegroup = needsToRegroup();
    
	if (Config::Debug::DrawSquadInfo && _order.isRegroupableOrder()) 
	{
		BWAPI::Broodwar->drawTextScreen(200, 350, "%c%s", white, _regroupStatus.c_str());
	}

	if (needToRegroup)
	{
		// Regroup, aka retreat. Only fighting units care about regrouping.
		BWAPI::Position regroupPosition = calcRegroupPosition();

        if (Config::Debug::DrawCombatSimulationInfo)
        {
		    BWAPI::Broodwar->drawTextScreen(200, 150, "REGROUP");
        }

		if (Config::Debug::DrawSquadInfo)
		{
			BWAPI::Broodwar->drawCircleMap(regroupPosition.x, regroupPosition.y, 30, BWAPI::Colors::Purple, true);
		}
        
		_microMelee.regroup(regroupPosition);
		_microRanged.regroup(regroupPosition);
		_microLurkers.regroup(regroupPosition);
		_microTanks.regroup(regroupPosition);
	}
	else
	{
		// No need to regroup. Execute micro.
		_microMelee.execute(_order);
		_microRanged.execute(_order);
		_microLurkers.execute(_order);
		_microTanks.execute(_order);
	}

	// Maybe stim marines and firebats.
	stimIfNeeded();

	// The remaining non-combat micro managers try to keep units near the front line.
	if (BWAPI::Broodwar->getFrameCount() % 8 == 3)    // deliberately lag a little behind reality
	{
		BWAPI::Unit vanguard = unitClosestToEnemy();

		// Medics.
		BWAPI::Position medicGoal = vanguard && vanguard->getPosition().isValid() ? vanguard->getPosition() : calcCenter();
		_microMedics.update(medicGoal);

		// Detectors.
		_microDetectors.setUnitClosestToEnemy(vanguard);
		_microDetectors.execute(_order);
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

// Clean up the _units vector.
// Note: Some units may be loaded in a bunker or transport and cannot accept orders.
//       Check unit->isLoaded() before issuing orders.
void Squad::setAllUnits()
{
	_hasAir = false;
	_hasGround = false;
	_hasAntiAir = false;
	_hasAntiGround = false;

	BWAPI::Unitset goodUnits;
	for (const auto unit : _units)
	{
		if (UnitUtil::IsValidUnit(unit))
		{
			goodUnits.insert(unit);

			if (unit->isFlying())
			{
				_hasAir = true;
			}
			else
			{
				_hasGround = true;
			}
			if (UnitUtil::CanAttackAir(unit))
			{
				_hasAntiAir = true;
			}
			if (UnitUtil::CanAttackGround(unit))
			{
				_hasAntiGround = true;
			}
		}
	}
	_units = goodUnits;
}

void Squad::setNearEnemyUnits()
{
	_nearEnemy.clear();

	for (const auto unit : _units)
	{
		if (!unit->exists() || unit->isLoaded())
		{
			continue;
		}

		int x = unit->getPosition().x;
		int y = unit->getPosition().y;

		_nearEnemy[unit] = unitNearEnemy(unit);

		if (Config::Debug::DrawSquadInfo) {
			int left = unit->getType().dimensionLeft();
			int right = unit->getType().dimensionRight();
			int top = unit->getType().dimensionUp();
			int bottom = unit->getType().dimensionDown();

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
	BWAPI::Unitset highTemplarUnits;
	BWAPI::Unitset transportUnits;
	BWAPI::Unitset lurkerUnits;
    BWAPI::Unitset tankUnits;
    BWAPI::Unitset medicUnits;

	for (const auto unit : _units)
	{
		if (unit->isCompleted() && unit->getHitPoints() > 0 && unit->exists() && !unit->isLoaded())
		{
			if (unit->getType().isWorker())
			{
				// We accept workers into the squad, but do not give them orders.
				// WorkerManager is responsible for that.
				// The squad creator (in CombatCommander) should be sure to give each worker
				// an appropriate job using the WorkerManager.
				// squad.clear() releases the worker jobs, so you don't have to worry about that.
			}
			else if (unit->getType() == BWAPI::UnitTypes::Protoss_High_Templar)
			{
				highTemplarUnits.insert(unit);
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
			// NOTE Excludes overlords as transports (they are also detectors, a confusing case).
			else if (unit->getType() == BWAPI::UnitTypes::Protoss_Shuttle ||
				unit->getType() == BWAPI::UnitTypes::Terran_Dropship)
			{
				transportUnits.insert(unit);
			}
			// NOTE Excludes some units: spellcasters, valkyries, corsairs, devourers.
			else if ((unit->getType().groundWeapon().maxRange() > 32) ||
				unit->getType() == BWAPI::UnitTypes::Zerg_Scourge ||
				unit->getType() == BWAPI::UnitTypes::Protoss_Reaver ||
				unit->getType() == BWAPI::UnitTypes::Protoss_Carrier)
			{
				rangedUnits.insert(unit);
			}
			else if (unit->getType().groundWeapon().maxRange() <= 32)
			{
				meleeUnits.insert(unit);
			}
			// NOTE Some units may fall through and not be assigned.
		}
	}

	_microMelee.setUnits(meleeUnits);
	_microRanged.setUnits(rangedUnits);
	_microDetectors.setUnits(detectorUnits);
	_microHighTemplar.setUnits(highTemplarUnits);
	_microLurkers.setUnits(lurkerUnits);
	_microMedics.setUnits(medicUnits);
	_microTanks.setUnits(tankUnits);
	_microTransports.setUnits(transportUnits);
}

// Calculates whether to regroup, aka retreat. Does combat sim if necessary.
bool Squad::needsToRegroup()
{
	if (_units.empty())
	{
		_regroupStatus = std::string("No attackers available");
		return false;
	}

	// If we are not attacking, never regroup.
	// This includes the Defend and Drop orders (among others).
	if (!_order.isRegroupableOrder())
	{
		_regroupStatus = std::string("No attack order");
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

	// if we are DT rushing and we haven't lost a DT yet, no retreat!
	if (StrategyManager::Instance().getOpeningGroup() == "dark templar" &&
		BWAPI::Broodwar->self()->deadUnitCount(BWAPI::UnitTypes::Protoss_Dark_Templar) == 0)
	{
		_regroupStatus = std::string("Go dark templar!");
		return false;
	}

	BWAPI::Unit unitClosest = unitClosestToEnemy();

	if (!unitClosest)
	{
		_regroupStatus = std::string("No closest unit");
		return false;
	}

	std::vector<UnitInfo> enemyCombatUnits;
    const auto & enemyUnitInfo = InformationManager::Instance().getUnitInfo(BWAPI::Broodwar->enemy());

	// if none of our units are in range of any enemy units, don't retreat
	bool anyInRange = false;
    for (const auto & eui : enemyUnitInfo)
    {
		for (const auto u : _units)
        {
			if (!u->exists() || u->isLoaded())
			{
				continue;
			}

			// Max of weapon range and vision range. Vision range is as long or longer, except for tanks.
			// We assume that the tanks may siege, and check the siege range of unsieged tanks.
			int range = 0;     // range of enemy unit, zero if it cannot hit us
			if (UnitUtil::CanAttack(eui.second.type, u->getType()))
			{
				if (eui.second.type == BWAPI::UnitTypes::Terran_Siege_Tank_Tank_Mode ||
					eui.second.type == BWAPI::UnitTypes::Terran_Siege_Tank_Siege_Mode)
				{
					range = (BWAPI::UnitTypes::Terran_Siege_Tank_Siege_Mode).groundWeapon().maxRange() + 64;  // plus safety fudge
				}
				else
				{
					// This is always >= weapon range, so we can stop here.
					range = eui.second.type.sightRange();
				}
				range += 128;    // plus some to account for our squad spreading out

				if (range >= eui.second.lastPosition.getDistance(u->getPosition()))
				{
					anyInRange = true;
					break;   // break out of inner loop
				}
			}
        }

		if (anyInRange)
        {
            break;       // break out of outer loop
        }
    }

    if (!anyInRange)
    {
        _regroupStatus = std::string("No enemy units in range");
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
    for (const auto unit : _units)
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
            BWAPI::Broodwar->printf("Squad::calcCenter() of empty squad");
        }
        return BWAPI::Position(0,0);
    }

	BWAPI::Position accum(0,0);
	for (const auto unit : _units)
	{
		if (unit->getPosition().isValid())
		{
			accum += unit->getPosition();
		}
	}
	return BWAPI::Position(accum.x / _units.size(), accum.y / _units.size());
}

BWAPI::Position Squad::calcRegroupPosition()
{
	BWAPI::Position regroup(0,0);

	int minDist = 100000;

	// Retreat to the location of the unit not near the enemy which is
	// closest to the order's target destination.
	// NOTE May retreat somewhere silly if the chosen unit was newly produced.
	//      Zerg sometimes retreats back and forth through the enemy when new
	//      zerg units are produced in bases on opposite sides.
	for (const auto unit : _units)
	{
		// Don't return the position of a detector, which may be in a weird place.
		// That means science vessel, protoss observer, or overlord.
		// Bug fix thanks to AIL!
		if (!_nearEnemy[unit] && !unit->getType().isDetector() && !unit->isLoaded())
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
		BWTA::BaseLocation * natural = InformationManager::Instance().getMyNaturalLocation();
		if (natural && InformationManager::Instance().getBaseOwner(natural) == BWAPI::Broodwar->self())
		{
			base = natural;
		}
		return BWTA::getRegion(base->getTilePosition())->getCenter();
	}
	return regroup;
}

// Actually the unit closest to the order position by ground distance.
// This is usually OK for air units, but may sometimes be a little silly.
BWAPI::Unit Squad::unitClosestToEnemy()
{
	BWAPI::Unit closest = nullptr;
	int closestDist = 100000;

	UAB_ASSERT(_order.getPosition().isValid(), "bad order position");

	for (const auto unit : _units)
	{
		if (unit->getType().isDetector() || unit->isLoaded())
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

const BWAPI::Unitset & Squad::getUnits() const	
{ 
	return _units; 
} 

const SquadOrder & Squad::getSquadOrder() const			
{ 
	return _order; 
}

void Squad::addUnit(BWAPI::Unit u)
{
	_units.insert(u);
}

// NOTE If the unit is a worker, you may have to release it before calling this.
void Squad::removeUnit(BWAPI::Unit u)
{
	_units.erase(u);
}

const std::string & Squad::getName() const
{
    return _name;
}

// The drop squad has been given a Load order. Load up the transports for a drop.
// Unlike other code in the drop system, this supports any number of transports, including zero.
// Called once per frame while a Load order is in effect.
void Squad::loadTransport()
{
	for (const auto trooper : _units)
	{
		// If it's not the transport itself, send it toward the order location,
		// which is set to the transport's initial location.
		if (trooper->exists() && !trooper->isLoaded() && trooper->getType().spaceProvided() == 0)
		{
			Micro::SmartMove(trooper, _order.getPosition());
		}
	}

	for (const auto transport : _microTransports.getUnits())
	{
		if (!transport->exists())
		{
			continue;
		}
		
		for (const auto unit : _units)
		{
			if (transport->getSpaceRemaining() == 0)
			{
				break;
			}

			if (transport->load(unit))
			{
				break;
			}
		}
	}
}

// Stim marines and firebats if possible and appropriate.
// This stims for combat. It doesn't consider stim to travel faster.
// We bypass the micro managers because it simplifies the bookkeeping, but a disadvantage
// is that we don't have access to the target list. Should refactor a little to get that,
// because it can help us figure out how important it is to stim.
void Squad::stimIfNeeded()
{
	// Do we have stim?
	if (BWAPI::Broodwar->self()->getRace() != BWAPI::Races::Terran ||
		!BWAPI::Broodwar->self()->hasResearched(BWAPI::TechTypes::Stim_Packs))
	{
		return;
	}

	// Are there enemies nearby that we may want to fight?
	if (_nearEnemy.empty())
	{
		return;
	}

	// So far so good. Time to get into details.

	// Stim can be used more freely if we have medics with lots of energy.
	int totalMedicEnergy = _microMedics.getTotalEnergy();

	// Stim costs 10 HP, which requires 5 energy for a medic to heal.
	const int stimEnergyCost = 5;

	// Firebats first, because they are likely to be right up against the enemy.
	for (const auto firebat : _microMelee.getUnits())
	{
		// Invalid position means the firebat is probably in a bunker or transport.
		if (firebat->getType() != BWAPI::UnitTypes::Terran_Firebat || !firebat->getPosition().isValid())
		{
			continue;
		}
		// Don't overstim and lose too many HP.
		if (firebat->getHitPoints() < 35 || totalMedicEnergy <= 0 && firebat->getHitPoints() < 45)
		{
			continue;
		}

		BWAPI::Unitset nearbyEnemies;
		MapGrid::Instance().GetUnits(nearbyEnemies, firebat->getPosition(), 64, false, true);

		// NOTE We don't check whether the enemy is attackable or worth attacking.
		if (!nearbyEnemies.empty())
		{
			Micro::SmartStim(firebat);
			totalMedicEnergy -= stimEnergyCost;
		}
	}

	// Next marines, treated the same except for range and hit points.
	for (const auto marine : _microRanged.getUnits())
	{
		// Invalid position means the marine is probably in a bunker or transport.
		if (marine->getType() != BWAPI::UnitTypes::Terran_Marine || !marine->getPosition().isValid())
		{
			continue;
		}
		// Don't overstim and lose too many HP.
		if (marine->getHitPoints() <= 30 || totalMedicEnergy <= 0 && marine->getHitPoints() < 40)
		{
			continue;
		}

		BWAPI::Unitset nearbyEnemies;
		MapGrid::Instance().GetUnits(nearbyEnemies, marine->getPosition(), 5 * 32, false, true);

		if (!nearbyEnemies.empty())
		{
			Micro::SmartStim(marine);
			totalMedicEnergy -= stimEnergyCost;
		}
	}
}
