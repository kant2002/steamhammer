#include "MeleeManager.h"
#include "UnitUtil.h"

using namespace UAlbertaBot;

// Note: Melee units are ground units only. Scourge is a "ranged" unit.

MeleeManager::MeleeManager() 
{ 
}

void MeleeManager::executeMicro(const BWAPI::Unitset & targets) 
{
	assignTargetsOld(targets);
}

void MeleeManager::assignTargetsOld(const BWAPI::Unitset & targets)
{
    const BWAPI::Unitset & meleeUnits = getUnits();

	// figure out targets
	BWAPI::Unitset meleeUnitTargets;
	for (auto & target : targets) 
	{
		// conditions for targeting
		if (target->isVisible() &&
			!target->isFlying() &&
			(target->getType() != BWAPI::UnitTypes::Zerg_Larva) && 
			(target->getType() != BWAPI::UnitTypes::Zerg_Egg) &&
			!target->isStasised() &&
			!target->isUnderDisruptionWeb())             // melee unit can't attack under dweb
		{
			meleeUnitTargets.insert(target);
		}
	}

	for (auto & meleeUnit : meleeUnits)
	{
		// if the order is to attack or defend
		if (order.getType() == SquadOrderTypes::Attack || order.getType() == SquadOrderTypes::Defend) 
        {
            // run away if we meet the retreat criterion
            if (meleeUnitShouldRetreat(meleeUnit, targets))
            {
				// UAB_ASSERT(meleeUnit->exists(), "bad worker");  // TODO temporary debugging - see Micro::SmartMove
				BWAPI::Position fleeTo(BWAPI::Broodwar->self()->getStartLocation());
                Micro::SmartMove(meleeUnit, fleeTo);
            }
			else if (meleeUnitTargets.empty())
			{
				// There are no targets. Move to the order position if not already close.
				if (meleeUnit->getDistance(order.getPosition()) > 96)
				{
					// UAB_ASSERT(meleeUnit->exists(), "bad worker");  // TODO temporary debugging - see Micro::SmartMove
					Micro::SmartMove(meleeUnit, order.getPosition());
				}
			}
			else
			{
				// There are targets. Pick the best one and attack it.
				BWAPI::Unit target = getTarget(meleeUnit, meleeUnitTargets);
				Micro::SmartAttackUnit(meleeUnit, target);
			}
		}

		if (Config::Debug::DrawUnitTargetInfo)
		{
			BWAPI::Broodwar->drawLineMap(meleeUnit->getPosition().x, meleeUnit->getPosition().y, 
				meleeUnit->getTargetPosition().x, meleeUnit->getTargetPosition().y,
				Config::Debug::ColorLineTarget);
		}
	}
}

std::pair<BWAPI::Unit, BWAPI::Unit> MeleeManager::findClosestUnitPair(const BWAPI::Unitset & attackers, const BWAPI::Unitset & targets)
{
    std::pair<BWAPI::Unit, BWAPI::Unit> closestPair(nullptr, nullptr);
    double closestDistance = std::numeric_limits<double>::max();

    for (auto & attacker : attackers)
    {
        BWAPI::Unit target = getTarget(attacker, targets);
        double dist = attacker->getDistance(attacker);

        if (!closestPair.first || (dist < closestDistance))
        {
            closestPair.first = attacker;
            closestPair.second = target;
            closestDistance = dist;
        }
    }

    return closestPair;
}

// get a target for the meleeUnit to attack
BWAPI::Unit MeleeManager::getTarget(BWAPI::Unit meleeUnit, const BWAPI::Unitset & targets)
{
	int highPriority = 0;
	double closestDist = std::numeric_limits<double>::infinity();
	BWAPI::Unit closestTarget = nullptr;

	// for each target possiblity
	for (auto & unit : targets)
	{
		int priority = getAttackPriority(meleeUnit, unit);
		int distance = meleeUnit->getDistance(unit);

		// if it's a higher priority, or it's closer, set it
		if (!closestTarget || (priority > highPriority) || (priority == highPriority && distance < closestDist))
		{
			closestDist = distance;
			highPriority = priority;
			closestTarget = unit;
		}
	}

	return closestTarget;
}

// get the attack priority of a type
int MeleeManager::getAttackPriority(BWAPI::Unit attacker, BWAPI::Unit unit) 
{
	BWAPI::UnitType type = unit->getType();

	// Exceptions for dark templar.
	if (attacker->getType() == BWAPI::UnitTypes::Protoss_Dark_Templar)
	{
		if (unit->getType() == BWAPI::UnitTypes::Terran_Vulture_Spider_Mine)
		{
			return 100;
		}
		if (unit->getType() == BWAPI::UnitTypes::Terran_Missile_Turret
			&& (BWAPI::Broodwar->self()->deadUnitCount(BWAPI::UnitTypes::Protoss_Dark_Templar) == 0))
		{
			return 90;
		}
		if (unit->getType().isWorker())
		{
			return 80;
		}
	}

	// Short circuit: Melee combat unit which is outside some range is lower priority than a worker.
	if (type == BWAPI::UnitTypes::Zerg_Zergling ||
		type == BWAPI::UnitTypes::Zerg_Ultralisk ||
		type == BWAPI::UnitTypes::Zerg_Broodling ||
		type == BWAPI::UnitTypes::Protoss_Zealot ||
		type == BWAPI::UnitTypes::Protoss_Dark_Templar ||
		type == BWAPI::UnitTypes::Terran_Firebat)
	{
		if (attacker->getDistance(unit) > 48) {
			return 8;
		}
	}
	// Short circuit: Ranged unit which is far enough outside its range is lower priority than a worker.
	if (type.groundWeapon() != BWAPI::WeaponTypes::None &&
		!type.isWorker() &&
		attacker->getDistance(unit) > 64 + type.groundWeapon().maxRange())
	{
		return 8;
	}
	// Short circuit: Units before bunkers!
	if (type == BWAPI::UnitTypes::Terran_Bunker)
	{
		return 10;
	}
	// Medics and ordinary combat units. Include workers that are doing stuff.
	if (type == BWAPI::UnitTypes::Terran_Medic || type == BWAPI::UnitTypes::Protoss_High_Templar)
	{
		return 12;
	}
	if (type.groundWeapon() != BWAPI::WeaponTypes::None && !type.isWorker())
	{
		return 12;
	}
	if (type.isWorker() && (unit->isRepairing() || unit->isConstructing() || unitNearChokepoint(unit)))
	{
		return 12;
	}
	// next priority is bored worker
	if (type.isWorker()) 
	{
		return 9;
	}
    // Buildings come under attack during free time, so they can be split into more levels.
	if (type == BWAPI::UnitTypes::Zerg_Spire)
	{
		return 6;
	}
	if (type == BWAPI::UnitTypes::Zerg_Spawning_Pool ||
		type.isResourceDepot() ||
		type == BWAPI::UnitTypes::Protoss_Templar_Archives ||
		type.isSpellcaster())
	{
		return 5;
	}
	// Short circuit: Addons other than a completed comsat are worth almost nothing.
	// TODO should also check that it is attached
	if (type.isAddon() && !(type == BWAPI::UnitTypes::Terran_Comsat_Station && unit->isCompleted()))
	{
		return 1;
	}
	// anything with a cost
	if (type.gasPrice() > 0 || type.mineralPrice() > 0)
	{
		return 3;
	}
	
	// then everything else
	return 1;
}

BWAPI::Unit MeleeManager::closestMeleeUnit(BWAPI::Unit target, const BWAPI::Unitset & meleeUnitsToAssign)
{
	double minDistance = 0;
	BWAPI::Unit closest = nullptr;

	for (auto & meleeUnit : meleeUnitsToAssign)
	{
		double distance = meleeUnit->getDistance(target);
		if (!closest || distance < minDistance)
		{
			minDistance = distance;
			closest = meleeUnit;
		}
	}
	
	return closest;
}

bool MeleeManager::meleeUnitShouldRetreat(BWAPI::Unit meleeUnit, const BWAPI::Unitset & targets)
{
    // terran don't regen so it doesn't make sense to retreat
    if (meleeUnit->getType().getRace() == BWAPI::Races::Terran)
    {
        return false;
    }

    // we don't want to retreat the melee unit if its shields or hit points are above the threshold set in the config file
    // set those values to zero if you never want the unit to retreat from combat individually
    if (meleeUnit->getShields() > Config::Micro::RetreatMeleeUnitShields || meleeUnit->getHitPoints() > Config::Micro::RetreatMeleeUnitHP)
    {
        return false;
    }

    // if there is a ranged enemy unit within attack range of this melee unit then we shouldn't bother retreating since it could fire and kill it anyway
    for (auto & unit : targets)
    {
        int groundWeaponRange = unit->getType().groundWeapon().maxRange();
        if (groundWeaponRange >= 64 && unit->getDistance(meleeUnit) < groundWeaponRange)
        {
            return false;
        }
    }

    return true;
}


// still has bug in it somewhere, use Old version
void MeleeManager::assignTargetsNew(const BWAPI::Unitset & targets)
{
    const BWAPI::Unitset & meleeUnits = getUnits();

	// figure out targets
	BWAPI::Unitset meleeUnitTargets;
	for (auto & target : targets) 
	{
		// conditions for targeting
		if (!(target->getType().isFlyer()) && 
			!(target->isLifted()) &&
			!(target->getType() == BWAPI::UnitTypes::Zerg_Larva) && 
			!(target->getType() == BWAPI::UnitTypes::Zerg_Egg) &&
			target->isVisible()) 
		{
			meleeUnitTargets.insert(target);
		}
	}

    BWAPI::Unitset meleeUnitsToAssign(meleeUnits);
    std::map<BWAPI::Unit, int> attackersAssigned;

    for (auto & unit : meleeUnitTargets)
    {
        attackersAssigned[unit] = 0;
    }

    int smallThreshold = BWAPI::Broodwar->self()->getRace() == BWAPI::Races::Zerg ? 3 : 1;
    int bigThreshold = BWAPI::Broodwar->self()->getRace() == BWAPI::Races::Zerg ? 12 : 3;

    // keep assigning targets while we have attackers and targets remaining
    while (!meleeUnitsToAssign.empty() && !meleeUnitTargets.empty())
    {
        auto attackerAssignment = findClosestUnitPair(meleeUnitsToAssign, meleeUnitTargets);
        BWAPI::Unit & attacker = attackerAssignment.first;
        BWAPI::Unit & target = attackerAssignment.second;

        UAB_ASSERT_WARNING(attacker, "We should have chosen an attacker!");

        if (!attacker)
        {
            break;
        }

        if (!target)
        {
            Micro::SmartMove(attacker, order.getPosition());
            continue;
        }

        Micro::SmartAttackUnit(attacker, target);

        // update the number of units assigned to attack the target we found
        int & assigned = attackersAssigned[attackerAssignment.second];
        assigned++;

        // if it's a small / fast unit and there's more than 2 things attacking it already, don't assign more
        if ((target->getType().isWorker() || target->getType() == BWAPI::UnitTypes::Zerg_Zergling) && (assigned >= smallThreshold))
        {
            meleeUnitTargets.erase(target);
        }
        // if it's a building and there's more than 10 things assigned to it already, don't assign more
        else if (assigned > bigThreshold)
        {
            meleeUnitTargets.erase(target);
        }

        meleeUnitsToAssign.erase(attacker);
    }

    // if there's no targets left, attack move to the order destination
    if (meleeUnitTargets.empty())
    {
        for (auto & unit : meleeUnitsToAssign)    
        {
			if (unit->getDistance(order.getPosition()) > 100)
			{
				// move to it
				Micro::SmartMove(unit, order.getPosition());
                BWAPI::Broodwar->drawLineMap(unit->getPosition(), order.getPosition(), BWAPI::Colors::Yellow);
			}
        }
    }
}
