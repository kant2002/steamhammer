#include "MicroManager.h"
#include "MapTools.h"

using namespace UAlbertaBot;

MicroManager::MicroManager() 
{
}

void MicroManager::setUnits(const BWAPI::Unitset & u) 
{ 
	_units = u; 
}

BWAPI::Position MicroManager::calcCenter() const
{
    if (_units.empty())
    {
        if (Config::Debug::DrawSquadInfo)
        {
            BWAPI::Broodwar->printf("calcCenter() called on empty squad");
        }
        return BWAPI::Position(0,0);
    }

	BWAPI::Position accum(0,0);
	for (const auto unit : _units)
	{
		accum += unit->getPosition();
	}
	return BWAPI::Position(accum.x / _units.size(), accum.y / _units.size());
}

void MicroManager::execute(const SquadOrder & inputOrder)
{
	// Nothing to do if we have no units.
	if (_units.empty())
	{
		return;
	}

	order = inputOrder;             // remember our order
	drawOrderText();

	// If we have no combat order (attack or defend), we're done.
	if (!order.isCombatOrder())
	{
		return;
	}

	// Discover enemies within region of interest.
	BWAPI::Unitset nearbyEnemies;

	// Always include enemies in the radius of the order.
	MapGrid::Instance().GetUnits(nearbyEnemies, order.getPosition(), order.getRadius(), false, true);

	// For attack but not defense, also include enemies near our units.
	if (order.getType() == SquadOrderTypes::Attack)
	{
		for (const auto unit : _units) 
		{
			MapGrid::Instance().GetUnits(nearbyEnemies, unit->getPosition(), unit->getType().sightRange(), false, true);
		}
	}

	executeMicro(nearbyEnemies);
}

const BWAPI::Unitset & MicroManager::getUnits() const
{ 
    return _units; 
}

// Unused but potentially useful.
bool MicroManager::containsType(BWAPI::UnitType type) const
{
	for (const auto unit : _units)
	{
		if (unit->getType() == type)
		{
			return true;
		}
	}
	return false;
}

void MicroManager::regroup(const BWAPI::Position & regroupPosition) const
{
    BWAPI::Position ourBasePosition = BWAPI::Position(InformationManager::Instance().getMyMainBaseLocation()->getPosition());
    int regroupDistanceFromBase = MapTools::Instance().getGroundDistance(regroupPosition, ourBasePosition);

	for (const auto unit : _units)
	{
        int unitDistanceFromBase = MapTools::Instance().getGroundDistance(unit->getPosition(), ourBasePosition);

		// 1. A broodling should never retreat, but attack as long as it lives.
		// 2. A unit next to a sieged tank should not move away.
		// TODO 3. A unit in stay-home mode should stay home, not "regroup" away from home.
		// TODO 4. A unit whose retreat path is blocked by enemies should do something else, at least attack-move.
		if (buildScarabOrInterceptor(unit))
		{
			// We're done for this frame.
		}
		else if (unit->getType() == BWAPI::UnitTypes::Zerg_Broodling ||
			(BWAPI::Broodwar->enemy()->getRace() == BWAPI::Races::Terran &&
			!unit->isFlying() &&
			 BWAPI::Broodwar->getClosestUnit(unit->getPosition(),
				BWAPI::Filter::IsEnemy && BWAPI::Filter::GetType == BWAPI::UnitTypes::Terran_Siege_Tank_Siege_Mode,
				64)))
		{
			Micro::SmartAttackMove(unit, unit->getPosition());
		}
		else if (unitDistanceFromBase > regroupDistanceFromBase)
        {
            Micro::SmartMove(unit, ourBasePosition);
        }
		else if (unit->getDistance(regroupPosition) > 96)
		{
			Micro::SmartMove(unit, regroupPosition);
		}
		else
		{
			Micro::SmartAttackMove(unit, unit->getPosition());
		}
	}
}

// Return true if we started to build a new scarab or interceptor.
bool MicroManager::buildScarabOrInterceptor(BWAPI::Unit u) const
{
	if (u->getType() == BWAPI::UnitTypes::Protoss_Reaver)
	{
		if (!u->isTraining() && u->canTrain(BWAPI::UnitTypes::Protoss_Scarab))
		{
			return u->train(BWAPI::UnitTypes::Protoss_Scarab);
		}
	}
	else if (u->getType() == BWAPI::UnitTypes::Protoss_Carrier)
	{
		if (!u->isTraining() && u->canTrain(BWAPI::UnitTypes::Protoss_Interceptor))
		{
			return u->train(BWAPI::UnitTypes::Protoss_Interceptor);
		}
	}

	return false;
}

bool MicroManager::unitNearEnemy(BWAPI::Unit unit)
{
	assert(unit);

	BWAPI::Unitset enemyNear;

	MapGrid::Instance().GetUnits(enemyNear, unit->getPosition(), 800, false, true);

	return enemyNear.size() > 0;
}

// returns true if position:
// a) is walkable
// b) doesn't have buildings on it
// c) doesn't have a unit on it that can attack ground
// NOTE Unused code, a candidate for throwing out.
bool MicroManager::checkPositionWalkable(BWAPI::Position pos) 
{
	// get x and y from the position
	int x(pos.x), y(pos.y);

	// If it's not walkable, throw it out.
	if (!BWAPI::Broodwar->isWalkable(x / 8, y / 8))
	{
		return false;
	}

	// for each of those units, if it's a building or an attacking enemy unit we don't want to go there
	for (const auto unit : BWAPI::Broodwar->getUnitsOnTile(x/32, y/32)) 
	{
		if	(unit->getType().isBuilding() || unit->getType().isResourceContainer() || 
			(unit->getPlayer() != BWAPI::Broodwar->self() && unit->getType().groundWeapon() != BWAPI::WeaponTypes::None)) 
		{		
				return false;
		}
	}

	// otherwise it's okay
	return true;
}

bool MicroManager::unitNearChokepoint(BWAPI::Unit unit) const
{
	for (BWTA::Chokepoint * choke : BWTA::getChokepoints())
	{
		if (unit->getDistance(choke->getCenter()) < 80)
		{
			return true;
		}
	}

	return false;
}

void MicroManager::drawOrderText() 
{
	if (Config::Debug::DrawUnitTargetInfo)
    {
		for (const auto unit : _units)
		{
			BWAPI::Broodwar->drawTextMap(unit->getPosition().x, unit->getPosition().y, "%s", order.getStatus().c_str());
		}
	}
}
