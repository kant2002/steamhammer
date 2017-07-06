#include "RangedManager.h"
#include "UnitUtil.h"

using namespace UAlbertaBot;

RangedManager::RangedManager() 
{ 
}

void RangedManager::executeMicro(const BWAPI::Unitset & targets) 
{
	assignTargetsOld(targets);
}

void RangedManager::assignTargetsOld(const BWAPI::Unitset & targets)
{
    const BWAPI::Unitset & rangedUnits = getUnits();

	// figure out targets
	BWAPI::Unitset rangedUnitTargets;
    std::copy_if(targets.begin(), targets.end(), std::inserter(rangedUnitTargets, rangedUnitTargets.end()),
		[](BWAPI::Unit u){ return u->isVisible() && u->getType() != BWAPI::UnitTypes::Zerg_Larva &&
		u->getType() != BWAPI::UnitTypes::Zerg_Egg; });

    for (auto & rangedUnit : rangedUnits)
	{
		// train sub units such as scarabs or interceptors
		//trainSubUnits(rangedUnit);

		// if the order is to attack or defend
		if (order.getType() == SquadOrderTypes::Attack || order.getType() == SquadOrderTypes::Defend) 
        {
			// if there are targets
			if (!rangedUnitTargets.empty())
			{
				// find the best target for this zealot
				BWAPI::Unit target = getTarget(rangedUnit, rangedUnitTargets);
                
                if (target && Config::Debug::DrawUnitTargetInfo) 
	            {
		            BWAPI::Broodwar->drawLineMap(rangedUnit->getPosition(), rangedUnit->getTargetPosition(), BWAPI::Colors::Purple);
	            }


				// attack it
                if (Config::Micro::KiteWithRangedUnits)
                {
                    if (rangedUnit->getType() == BWAPI::UnitTypes::Zerg_Mutalisk || rangedUnit->getType() == BWAPI::UnitTypes::Terran_Vulture)
                    {
				        Micro::MutaDanceTarget(rangedUnit, target);
                    }
                    else
                    {
                        Micro::SmartKiteTarget(rangedUnit, target);
                    }
                }
                else
                {
                    Micro::SmartAttackUnit(rangedUnit, target);
                }
			}
			// if there are no targets
			else
			{
				// if we're not near the order position
				if (rangedUnit->getDistance(order.getPosition()) > 100)
				{
					// move to it
					Micro::SmartAttackMove(rangedUnit, order.getPosition());
				}
			}
		}
	}
}

std::pair<BWAPI::Unit, BWAPI::Unit> RangedManager::findClosestUnitPair(const BWAPI::Unitset & attackers, const BWAPI::Unitset & targets)
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

// get a target to attack
BWAPI::Unit RangedManager::getTarget(BWAPI::Unit rangedUnit, const BWAPI::Unitset & targets)
{
	int bestPriorityDistance = 1000000;
    int bestPriority = 0;
    
    double bestLTD = 0;

    int highPriority = 0;
	double closestDist = std::numeric_limits<double>::infinity();
	BWAPI::Unit closestTarget = nullptr;

    for (const auto & target : targets)
    {
        double distance         = rangedUnit->getDistance(target);
        double LTD              = UnitUtil::CalculateLTD(target, rangedUnit);
        int priority            = getAttackPriority(rangedUnit, target);
        bool targetIsThreat     = LTD > 0;
        
		if (!closestTarget || (priority > highPriority) || (priority == highPriority && distance < closestDist))
		{
			closestDist = distance;
			highPriority = priority;
			closestTarget = target;
		}       
    }

    return closestTarget;
}

// get the attack priority of a target unit
int RangedManager::getAttackPriority(BWAPI::Unit rangedUnit, BWAPI::Unit target) 
{
	BWAPI::UnitType rangedType = rangedUnit->getType();
	BWAPI::UnitType targetType = target->getType();

	if (rangedType == BWAPI::UnitTypes::Zerg_Scourge)
    {
		if (targetType == BWAPI::UnitTypes::Terran_Valkyrie ||
			targetType == BWAPI::UnitTypes::Terran_Battlecruiser ||
			targetType == BWAPI::UnitTypes::Terran_Science_Vessel)
		{
			return 100;
		}
		if (targetType == BWAPI::UnitTypes::Terran_Wraith)
		{
			return 90;
		}
		if (targetType == BWAPI::UnitTypes::Terran_Dropship)
		{
			return 80;
		}
		if (targetType == BWAPI::UnitTypes::Protoss_Carrier)
        {
            return 100;
        }
		if (targetType == BWAPI::UnitTypes::Protoss_Corsair || targetType == BWAPI::UnitTypes::Protoss_Scout)
        {
            return 90;
        }
		if (targetType == BWAPI::UnitTypes::Protoss_Shuttle)
		{
			return 80;
		}
		if (targetType == BWAPI::UnitTypes::Protoss_Observer)
		{
			return 60;
		}
		if (targetType == BWAPI::UnitTypes::Zerg_Devourer)
		{
			return 90;
		}
		if (targetType == BWAPI::UnitTypes::Zerg_Mutalisk)
		{
			return 80;
		}
		if (targetType == BWAPI::UnitTypes::Zerg_Guardian)
		{
			return 70;
		}
		if (targetType == BWAPI::UnitTypes::Zerg_Overlord || targetType == BWAPI::UnitTypes::Zerg_Scourge)
		{
			// Usually not worth scourge at all.
			return 0;
		}
	}

	// An addon other than a completed comsat is boring.
	if (targetType.isAddon() && !(targetType == BWAPI::UnitTypes::Terran_Comsat_Station && target->isCompleted()))
	{
		return 1;
	}

    // if the target is building something near our base something is fishy
    BWAPI::Position ourBasePosition = BWAPI::Position(BWAPI::Broodwar->self()->getStartLocation());
	if (target->getDistance(ourBasePosition) < 1200) {
		if (target->getType().isWorker() && (target->isConstructing() || target->isRepairing()))
		{
			return 101;
		}
		if (target->getType().isBuilding() && (target->isCompleted() || target->isBeingConstructed()))
		{
			return 90;
		}
	}
    
	bool isThreat = rangedType.isFlyer() ? targetType.airWeapon() != BWAPI::WeaponTypes::None : targetType.groundWeapon() != BWAPI::WeaponTypes::None;
	// Exception: Workers are not threats after all.
	if (target->getType().isWorker())
	{
		isThreat = false;
	}

	// Highest priority is the most dangerous stuff.
	if (rangedType.isFlyer()) {
		// Exceptions if we're a flyer.
		if (targetType == BWAPI::UnitTypes::Protoss_Carrier ||
			targetType == BWAPI::UnitTypes::Zerg_Scourge)
		{
			return 20;
		}
	}
	else
	{
		// Exceptions if we're a ground unit.
		if (targetType == BWAPI::UnitTypes::Terran_Vulture_Spider_Mine)
		{
			return 20;
		}
		if (targetType == BWAPI::UnitTypes::Terran_Siege_Tank_Siege_Mode ||
			targetType == BWAPI::UnitTypes::Terran_Siege_Tank_Tank_Mode ||
			targetType == BWAPI::UnitTypes::Protoss_Reaver ||
			targetType == BWAPI::UnitTypes::Zerg_Lurker)
		{
			return 19;
		}
	}

	if (targetType == BWAPI::UnitTypes::Protoss_High_Templar)
	{
		return 20;
	}

	if (targetType == BWAPI::UnitTypes::Protoss_Photon_Cannon && target->isPowered())
	{
		return 19;
	}

	// Next is droppers. They may be loaded. Also scourge and vessels.
	if (targetType == BWAPI::UnitTypes::Terran_Dropship ||
		targetType == BWAPI::UnitTypes::Terran_Science_Vessel ||
		targetType == BWAPI::UnitTypes::Protoss_Shuttle ||
		targetType == BWAPI::UnitTypes::Zerg_Scourge)
	{
		return 14;
	}
	// Next is ordinary dangerous stuff.
	if (isThreat)
	{
		return 11;
	}
	// Bunkers next.
	if (targetType == BWAPI::UnitTypes::Terran_Bunker)
	{
		return 10;
	}
	// next priority is worker
	if (targetType.isWorker()) 
	{
        if (rangedUnit->getType() == BWAPI::UnitTypes::Terran_Vulture)
        {
            return 11;
        }
		// SCVs repairing or constructing.
		if (target->isRepairing() || target->isConstructing() || unitNearChokepoint(target))
		{
			return 11;
		}

  		return 9;
	}
	// Carriers don't shoot (by themselves) but are more important than buildings.
	if (targetType == BWAPI::UnitTypes::Protoss_Carrier)
	{
		return 8;
	}
	// Turrets should be cleared out on principle.
	if (targetType == BWAPI::UnitTypes::Terran_Missile_Turret)
	{
		return 7;
	}
	// Ditto for templar tech.
	if (targetType == BWAPI::UnitTypes::Protoss_Templar_Archives)
	{
		return 7;
	}
	if (targetType == BWAPI::UnitTypes::Zerg_Spawning_Pool)
	{
		return 7;
	}
	// Don't forget the nexus/cc/hatchery.
	if (targetType.isResourceDepot())
	{
		return 6;
	}
	if (targetType == BWAPI::UnitTypes::Protoss_Pylon)
	{
		return 5;
	}
	if (targetType == BWAPI::UnitTypes::Terran_Factory || targetType == BWAPI::UnitTypes::Terran_Armory)
	{
		return 5;
	}
	// Downgrade unfinished/unpowered buildings, with exceptions.
	if (targetType.isBuilding() &&
		(!target->isCompleted() || !target->isPowered()) &&
		!(	targetType.isResourceDepot() ||
			targetType.groundWeapon() != BWAPI::WeaponTypes::None ||
			targetType.airWeapon() != BWAPI::WeaponTypes::None ||
			targetType == BWAPI::UnitTypes::Terran_Bunker))
	{
		return 2;
	}
	if (targetType.gasPrice() > 0)
	{
		return 4;
	}
	if (targetType.mineralPrice() > 0)
	{
		return 3;
	}
	// Finally everything else.
	return 1;
}

BWAPI::Unit RangedManager::closestrangedUnit(BWAPI::Unit target, std::set<BWAPI::Unit> & rangedUnitsToAssign)
{
	double minDistance = 0;
	BWAPI::Unit closest = nullptr;

	for (auto & rangedUnit : rangedUnitsToAssign)
	{
		double distance = rangedUnit->getDistance(target);
		if (!closest || distance < minDistance)
		{
			minDistance = distance;
			closest = rangedUnit;
		}
	}
	
	return closest;
}


// still has bug in it somewhere, use Old version
void RangedManager::assignTargetsNew(const BWAPI::Unitset & targets)
{
    const BWAPI::Unitset & rangedUnits = getUnits();

	// figure out targets
	BWAPI::Unitset rangedUnitTargets;
    std::copy_if(targets.begin(), targets.end(), std::inserter(rangedUnitTargets, rangedUnitTargets.end()), [](BWAPI::Unit u){ return u->isVisible(); });

    BWAPI::Unitset rangedUnitsToAssign(rangedUnits);
    std::map<BWAPI::Unit, int> attackersAssigned;

    for (auto & unit : rangedUnitTargets)
    {
        attackersAssigned[unit] = 0;
    }

    // keep assigning targets while we have attackers and targets remaining
    while (!rangedUnitsToAssign.empty() && !rangedUnitTargets.empty())
    {
        auto attackerAssignment = findClosestUnitPair(rangedUnitsToAssign, rangedUnitTargets);
        BWAPI::Unit & attacker = attackerAssignment.first;
        BWAPI::Unit & target = attackerAssignment.second;

        UAB_ASSERT_WARNING(attacker, "We should have chosen an attacker!");

        if (!attacker)
        {
            break;
        }

        if (!target)
        {
            Micro::SmartAttackMove(attacker, order.getPosition());
            continue;
        }

        if (Config::Micro::KiteWithRangedUnits)
        {
            if (attacker->getType() == BWAPI::UnitTypes::Zerg_Mutalisk || attacker->getType() == BWAPI::UnitTypes::Terran_Vulture)
            {
			    Micro::MutaDanceTarget(attacker, target);
            }
            else
            {
                Micro::SmartKiteTarget(attacker, target);
            }
        }
        else
        {
            Micro::SmartAttackUnit(attacker, target);
        }

        // update the number of units assigned to attack the target we found
        int & assigned = attackersAssigned[attackerAssignment.second];
        assigned++;

        // if it's a small / fast unit and there's more than 2 things attacking it already, don't assign more
        if ((target->getType().isWorker() || target->getType() == BWAPI::UnitTypes::Zerg_Zergling) && (assigned > 2))
        {
            rangedUnitTargets.erase(target);
        }
        // if it's a building and there's more than 10 things assigned to it already, don't assign more
        else if (target->getType().isBuilding() && (assigned > 10))
        {
            rangedUnitTargets.erase(target);
        }

        rangedUnitsToAssign.erase(attacker);
    }

    // if there's no targets left, attack move to the order destination
    if (rangedUnitTargets.empty())
    {
        for (auto & unit : rangedUnitsToAssign)    
        {
			if (unit->getDistance(order.getPosition()) > 100)
			{
				// move to it
				Micro::SmartAttackMove(unit, order.getPosition());
			}
        }
    }
}