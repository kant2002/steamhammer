#include "MicroLurkers.h"
#include "UnitUtil.h"

using namespace UAlbertaBot;

MicroLurkers::MicroLurkers()
{
}

void MicroLurkers::executeMicro(const BWAPI::Unitset & targets)
{
	const BWAPI::Unitset & lurkers = getUnits();

	// figure out targets
	BWAPI::Unitset LurkerTargets;
	std::copy_if(targets.begin(), targets.end(), std::inserter(LurkerTargets, LurkerTargets.end()),
		[](BWAPI::Unit u){ return u->isVisible() && !u->isFlying(); });

	int lurkerRange = BWAPI::UnitTypes::Zerg_Lurker.groundWeapon().maxRange();

	for (auto & lurker : lurkers)
	{
		bool lurkerNearChokepoint = false;
		for (const auto & choke : BWTA::getChokepoints())
		{
			if (choke->getCenter().getDistance(lurker->getPosition()) < 64)
			{
				lurkerNearChokepoint = true;
				break;
			}
		}

		// if the order is to attack or defend
		if (order.isCombatOrder())
		{
			// if there are targets
			if (!LurkerTargets.empty())
			{
				// find the best target for this zealot
				BWAPI::Unit target = getTarget(lurker, LurkerTargets);

				if (target && Config::Debug::DrawUnitTargetInfo) {
					BWAPI::Broodwar->drawLineMap(lurker->getPosition(), lurker->getTargetPosition(), BWAPI::Colors::Purple);
				}

				// if we are within range of a target, burrow
				if (lurker->getDistance(target) <= lurkerRange) {
					if (lurker->canBurrow()) lurker->burrow();
				} else {
					if (lurker->canUnburrow()) lurker->unburrow();
				}

				// if we're burrowed, attack
				if (lurker->isBurrowed())
				{
					Micro::SmartAttackUnit(lurker, target);
				}
			}
			// if there are no targets
			else
			{
				// if we're not near the order position
				if (lurker->getDistance(order.getPosition()) > 100) {
					if (lurker->canUnburrow()) {
						lurker->unburrow();
					} else {
						Micro::SmartAttackMove(lurker, order.getPosition());
					}
				}
			}
		}
	}
}

BWAPI::Unit MicroLurkers::getTarget(BWAPI::Unit lurker, const BWAPI::Unitset & targets)
{
	int bestPriorityDistance = 1000000;
	int bestPriority = 0;

	double bestLTD = 0;

	BWAPI::Unit bestTargetThreatInRange = nullptr;
	double bestTargetThreatInRangeLTD = 0;

	int lurkerRange = BWAPI::UnitTypes::Zerg_Lurker.groundWeapon().maxRange();

	BWAPI::Unitset targetsInRange;
	for (const auto target : targets)
	{
		if (target->getDistance(lurker) <= lurkerRange && !target->isFlying())
		{
			targetsInRange.insert(target);
		}
	}

	const BWAPI::Unitset & newTargets = targetsInRange.empty() ? targets : targetsInRange;

	int highPriority = 0;
	double closestDist = std::numeric_limits<double>::infinity();
	BWAPI::Unit closestTarget = nullptr;

	// check first for units that are in range of our attack that can cause damage
	// choose the highest priority one from them at the lowest health
	for (const auto & target : newTargets)
	{
		if (!UnitUtil::CanAttack(lurker, target))
		{
			continue;
		}

		double distance = lurker->getDistance(target);
		double LTD = UnitUtil::CalculateLTD(target, lurker);
		int priority = getAttackPriority(lurker, target);
		bool targetIsThreat = LTD > 0;

		BWAPI::Broodwar->drawTextMap(target->getPosition(), "%d", priority);

		if (!closestTarget || (priority > highPriority) || (priority == highPriority && distance < closestDist))
		{
			closestDist = distance;
			highPriority = priority;
			closestTarget = target;
		}
	}

	if (bestTargetThreatInRange)
	{
		return bestTargetThreatInRange;
	}

	return closestTarget;
}

// get the attack priority of a type in relation to a zergling
int MicroLurkers::getAttackPriority(BWAPI::Unit rangedUnit, BWAPI::Unit target)
{
	BWAPI::UnitType rangedType = rangedUnit->getType();
	BWAPI::UnitType targetType = target->getType();

	bool isThreat = rangedType.isFlyer() ? targetType.airWeapon() != BWAPI::WeaponTypes::None : targetType.groundWeapon() != BWAPI::WeaponTypes::None;

	if (target->getType().isWorker())
	{
		isThreat = false;
	}

	if (target->getType() == BWAPI::UnitTypes::Zerg_Larva || target->getType() == BWAPI::UnitTypes::Zerg_Egg || target->getType().isAddon())
	{
		return 0;
	}

	// if the target is building something near our base something is fishy
	BWAPI::Position ourBasePosition = BWAPI::Position(InformationManager::Instance().getMyMainBaseLocation()->getPosition());
	if (target->getType().isWorker() && (target->isConstructing() || target->isRepairing()) && target->getDistance(ourBasePosition) < 1200)
	{
		return 100;
	}

	if (target->getType().isBuilding() && (target->isCompleted() || target->isBeingConstructed()) && target->getDistance(ourBasePosition) < 1200)
	{
		return 90;
	}

	// highest priority is something that can attack us or aid in combat
	if (targetType == BWAPI::UnitTypes::Terran_Bunker || isThreat)
	{
		return 11;
	}
	// next priority is turrets (cannons are threats, zerg has overlords so spores are meh)
	else if (targetType == BWAPI::UnitTypes::Terran_Missile_Turret)
	{
		return 10;
	}
	// next priority is worker
	else if (targetType.isWorker())
	{
		return 9;
	}
	// next is special buildings
	else if (targetType == BWAPI::UnitTypes::Zerg_Spawning_Pool)
	{
		return 5;
	}
	// next is special buildings
	else if (targetType == BWAPI::UnitTypes::Protoss_Pylon)
	{
		return 5;
	}
	// next is buildings that cost gas
	else if (targetType.gasPrice() > 0)
	{
		return 4;
	}
	else if (targetType.mineralPrice() > 0)
	{
		return 3;
	}
	// then everything else
	else
	{
		return 1;
	}
}

BWAPI::Unit MicroLurkers::closestrangedUnit(BWAPI::Unit target, std::set<BWAPI::Unit> & rangedUnitsToAssign)
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