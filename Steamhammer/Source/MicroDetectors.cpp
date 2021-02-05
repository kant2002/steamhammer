#include "MicroManager.h"
#include "MicroDetectors.h"

#include "The.h"
#include "Bases.h"
#include "UnitUtil.h"

using namespace UAlbertaBot;

MicroDetectors::MicroDetectors()
	: squadSize(0)
	, unitClosestToEnemy(nullptr)
{
}

void MicroDetectors::executeMicro(const BWAPI::Unitset & targets, const UnitCluster & cluster)
{
}

void MicroDetectors::go(const BWAPI::Unitset & squadUnits)
{
    const BWAPI::Unitset & detectorUnits = getUnits();

	if (detectorUnits.empty())
	{
		return;
	}

	// Look through enemy units to find those which we want to seek or to avoid.
	BWAPI::Unitset cloakedTargets;
	BWAPI::Unitset enemies;
    
	for (const BWAPI::Unit target : BWAPI::Broodwar->enemy()->getUnits())
	{
        if (!target->getPosition().isValid() || !target->isCompleted())
        {
            continue;
        }

		// 1. Find cloaked units. We want to keep them in detection range.
		if (target->getType().hasPermanentCloak() ||     // dark templar, observer
			target->getType().isCloakable() ||           // wraith, ghost
			target->getType() == BWAPI::UnitTypes::Terran_Vulture_Spider_Mine ||
			target->getType() == BWAPI::UnitTypes::Zerg_Lurker ||
			target->isBurrowed() ||
            target->getOrder() == BWAPI::Orders::Burrowing)
		{
            cloakedTargets.insert(target);
        }

		if (UnitUtil::CanAttackAir(target))
		{
			// 2. Find threats. Keep away from them.
			enemies.insert(target);
		}
	}

	// Units that may be able to fire on attackers, defending the detector.
    BWAPI::Unitset groundDefenders;
    BWAPI::Unitset airDefenders;
    for (BWAPI::Unit unit : squadUnits)
    {
        if (unit->getPosition().isValid())
        {
            if (UnitUtil::CanAttackGround(unit) && !unit->getType().isWorker())
            {
                groundDefenders.insert(unit);
            }
            if (UnitUtil::CanAttackAir(unit))
            {
                airDefenders.insert(unit);
            }
        }
    }

	// For each detector.
	// In Steamhammer, detectors in the squad are normally zero or one.
    // (If zero, we never got this far.)
	for (const BWAPI::Unit detectorUnit : detectorUnits)
	{
		if (squadSize == 1)
		{
			// The detector is alone in the squad. Move to the order position.
			// This allows the Recon squad to scout with a detector on island maps.
            if (order.getPosition().isValid())
            {
                the.micro.MoveSafely(detectorUnit, order.getPosition());
            }
            else
            {
                the.micro.MoveSafely(detectorUnit, BWAPI::Positions::Origin);
            }
			return;
		}

		BWAPI::Position destination;
        BWAPI::Unit nearestEnemy = NearestOf(detectorUnit->getPosition(), enemies);
        BWAPI::Unit nearestGroundDefender = NearestOf(detectorUnit->getPosition(), groundDefenders);
        BWAPI::Unit nearestAirDefender = NearestOf(detectorUnit->getPosition(), airDefenders);
        BWAPI::Unit nearestCloaked = NearestOf(detectorUnit->getPosition(), cloakedTargets);

        if (nearestEnemy &&
            detectorUnit->getDistance(nearestEnemy) <= 2 * 32 + UnitUtil::GetAttackRange(nearestEnemy, detectorUnit))
        {
            if (nearestEnemy->isFlying() &&
                nearestAirDefender &&
                detectorUnit->getDistance(nearestAirDefender) <= 12 * 32)
            {
                // Move toward the defender, our only hope to escape a flying attacker.
                destination = nearestAirDefender->getPosition();
            }
            else if (!nearestEnemy->isFlying() &&
                nearestGroundDefender &&
                detectorUnit->getDistance(nearestGroundDefender) <= 8 * 32)
            {
                // Move toward the defender, hoping to escape a ground attacker.
                destination = nearestGroundDefender->getPosition();
            }
            else
            {
                // There is no appropriate defender near. Move away from the attacker.
                destination = DistanceAndDirection(detectorUnit->getPosition(), nearestEnemy->getPosition(), -8 * 32);
            }
        }
        else if (nearestCloaked &&
            detectorUnit->getDistance(nearestCloaked) > 9 * 32)      // detection range is 11 tiles
        {
            destination = nearestCloaked->getPosition();
        }
		else if (unitClosestToEnemy &&
            unitClosestToEnemy->getPosition().isValid() &&
            !the.airAttacks.at(unitClosestToEnemy->getTilePosition()))
		{
			destination = unitClosestToEnemy->getPosition();
		}
        else if (the.bases.myMain()->getPosition().isValid())
		{
			destination = the.bases.myMain()->getPosition();
		}
        else
        {
            destination = BWAPI::Positions::Origin;
        }
		the.micro.MoveNear(detectorUnit, destination);
	}
}
