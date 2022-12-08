#include "MicroManager.h"
#include "MicroLurkers.h"

#include "Bases.h"
#include "InformationManager.h"
#include "The.h"
#include "UnitUtil.h"

using namespace UAlbertaBot;

// Prefer the nearest target, as most units do.
BWAPI::Unit MicroLurkers::getNearestTarget(BWAPI::Unit lurker, const BWAPI::Unitset & targets) const
{
    int highPriority = 0;
    int closestDist = MAX_DISTANCE;
    BWAPI::Unit bestTarget = nullptr;

    for (BWAPI::Unit target : targets)
    {
        int distance = lurker->getDistance(target);
        int priority = getAttackPriority(target);

        // BWAPI::Broodwar->drawTextMap(target->getPosition() + BWAPI::Position(20, -10), "%c%d", yellow, priority);

        if (priority > highPriority || priority == highPriority && distance < closestDist)
        {
            closestDist = distance;
            highPriority = priority;
            bestTarget = target;
        }
    }

    return bestTarget;
}

// Prefer the farthest target with the highest priority.
// The caller promises that all targets are in range, so all targets can be attacked.
// Choosing a distant target gives better odds of accidentally also hitting nearer targets,
// since nearby targets subtend a larger angle from the point of view of the lurker.
// It's a way to slightly improve lurker targeting without doing a full analysis.
BWAPI::Unit MicroLurkers::getFarthestTarget(BWAPI::Unit lurker, const BWAPI::Unitset & targets) const
{
    int highPriority = 0;
    int farthestDist = -1;
    BWAPI::Unit bestTarget = nullptr;

    for (BWAPI::Unit target : targets)
    {
        int distance = lurker->getDistance(target);
        int priority = getAttackPriority(target);

        // BWAPI::Broodwar->drawTextMap(target->getPosition() + BWAPI::Position(20, -10), "%c%d", yellow, priority);

        if (priority > highPriority || priority == highPriority && distance > farthestDist)
        {
            farthestDist = distance;
            highPriority = priority;
            bestTarget = target;
        }
    }

    return bestTarget;
}

// An unseen enemy could kill the lurker if it unburrows now. That means
// 1. A hard-hitting unit: Tank, battlecruiser, reaver, dragoon, lurker, guardian.
// 2. An undetected cloaked unit like a dark templar.
// 3. A known enemy in range but out of sight on high ground.
bool MicroLurkers::dangerousEnemyInRange(BWAPI::Unit lurker) const
{
    if (UnitUtil::EnemyDetectorInRange(lurker))
    {
        // We're detected anyway. Unburrowing won't make us more detected.
        return false;
    }

    for (const auto & kv : the.info.getUnitData(the.enemy()).getUnits())
    {
        const UnitInfo & ui(kv.second);

        if (!ui.goneFromLastPosition && !ui.lifted && ui.unit)
        {
            if (ui.unit->isVisible())
            {
                // The enemy unit is in sight.
                if (!ui.unit->isDetected() &&
                    lurker->getDistance(ui.lastPosition) < 8 * 32)
                {
                    // 2. Undetected cloaked enemy.
                    //BWAPI::Broodwar->printf("lurker %d near cloaked %s", lurker->getID(), UnitTypeName(ui.type).c_str());
                    return true;
                }
                BWAPI::WeaponType enemyWeapon = UnitUtil::GetGroundWeapon(ui.unit->getType());
                if (enemyWeapon.damageAmount() >= 20)
                {
                    int enemyRange = UnitUtil::GetAttackRangeAssumingUpgrades(ui.type, BWAPI::UnitTypes::Zerg_Lurker);
                    if (lurker->getDistance(ui.unit) <= enemyRange)
                    {
                        // 1. Hard-hitting enemy in range.
                        return  true;
                    }
                }
            }
            else
            {
                // The enemy unit is out of sight.
                int enemyRange = UnitUtil::GetAttackRangeAssumingUpgrades(ui.type, BWAPI::UnitTypes::Zerg_Lurker);
                if (enemyRange > 0 && lurker->getDistance(ui.lastPosition) <= enemyRange + 31)
                {
                    // 3. Enemy out of sight on high ground.
                    //BWAPI::Broodwar->printf("lurker %d near unseen %s", lurker->getID(), UnitTypeName(ui.type).c_str());
                    return true;
                }
            }
        }
    }

    return false;
}

// The lurker can probably unburrow without dying immediately.
// The answer will be wrong if we're surrounded by dragoons....
// Unburrow only at set intervals, to reduce the burrow-unburrow frenzy.
bool MicroLurkers::okToUnburrow(BWAPI::Unit lurker) const
{
    return
        BWAPI::Broodwar->getFrameCount() % 25 == 0 &&
        lurker->getHitPoints() > 31 &&
        !dangerousEnemyInRange(lurker);
}

// A lurker can burrow if it is outside tank and cannon range, or in a large enough group.
// Or undetected.
bool MicroLurkers::safeToBurrow(const BWAPI::Unitset & lurkers, BWAPI::Unit lurker) const
{
    return
        lurkers.size() >= 4 ||
        the.groundAttacks.at(lurker->getTilePosition()) == 0 ||
        !UnitUtil::EnemyDetectorInRange(lurker);
}

// A lurker should try to burrow spaced away from other lurkers, if the enemy has splash damage.
bool MicroLurkers::correctlySpacedToBurrow(const BWAPI::Unitset & lurkers, BWAPI::Unit lurker) const
{
    if (_tileSpacing == 0)
    {
        return true;
    }

    for (BWAPI::Unit el : lurkers)
    {
        if (el != lurker &&
            (el->isBurrowed() || el->getOrder() == BWAPI::Orders::Burrowing) &&
            lurker->getDistance(el) < 32 * _tileSpacing)
        {
            return false;
        }
    }

    return true;
}

// Retreat the lurker toward our main base (because it's easy).
// We expect it to burrow when it reaches a safe position.
void MicroLurkers::retreatToSafety(BWAPI::Unit lurker)
{
    the.micro.Move(lurker, the.bases.myMain()->getPosition(), &the.bases.myMain()->getDistances());
}

// If none, a distance "at infinity".
int MicroLurkers::closestBurrowedLurker(const BWAPI::Unitset & lurkers, const BWAPI::Position & xy) const
{
    int bestDist = MAX_DISTANCE;

    for (BWAPI::Unit el : lurkers)
    {
        if (el->isBurrowed() || el->getOrder() == BWAPI::Orders::Burrowing)
        {
            int dist = el->getDistance(xy);
            if (dist < bestDist)
            {
                bestDist = dist;
            }
        }
    }

    return bestDist;
}

void MicroLurkers::moveToSpacedPosition(const BWAPI::Unitset & lurkers, BWAPI::Unit lurker)
{
    int closest = closestBurrowedLurker(lurkers, lurker->getPosition());

    // Look two tiles away in every direction for anywhere better (not necessarily good enough).
    for (int dx = -1; dx <= 1; ++dx)
    {
        for (int dy = -1; dy <= 1; ++dy)
        {
            if (dx != 0 || dy !=0)
            {
                BWAPI::TilePosition xy = lurker->getTilePosition() + BWAPI::TilePosition(dx * 2, dy * 2);
                if (xy.isValid() &&
                    the.map.isWalkable(xy) &&
                    the.partitions.id(xy) == the.partitions.id(lurker->getTilePosition()) &&
                    closestBurrowedLurker(lurkers, BWAPI::Position(xy)) > closest)
                {
                    the.micro.Move(lurker, BWAPI::Position(xy));
                    return;
                }
            }
        }
    }

    // We found no better place. Retreat until we can find one.
    retreatToSafety(lurker);
}

// Burrow if all looks good, or move to a better position to burrow.
void MicroLurkers::seekToBurrow(const BWAPI::Unitset & lurkers, BWAPI::Unit lurker)
{
    //BWAPI::Broodwar->printf("lurker %d seeks to burrow", lurker->getID());

    if (!safeToBurrow(lurkers, lurker))
    {
        //BWAPI::Broodwar->printf("  retreating to burrow safely");
        retreatToSafety(lurker);
    }
    /* TODO works poorly because lurkers are slow and uncoordinated in spacing themselves out
    else if (!correctlySpacedToBurrow(lurkers, lurker))
    {
        BWAPI::Broodwar->printf("  moving to burrow with spacing");
        moveToSpacedPosition(lurkers, lurker);
    }
    */
    else
    {
        //BWAPI::Broodwar->printf("  burrowing", lurker->getID());
        the.micro.Burrow(lurker);
    }
}

// Space burrowed lurkers at least this far apart to avoid splash damage.
int MicroLurkers::findTileSpacing() const
{
    if (the.info.enemyHasStorm())
    {
        return 3;
    }
    if (the.info.enemyHasSiegeMode() || the.your.seen.count(BWAPI::UnitTypes::Protoss_Reaver) > 0)
    {
        return 2;
    }
    return 0;
}

// -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- --

MicroLurkers::MicroLurkers()
    : _tileSpacing(0)
    , _tactic(LurkerTactic::WithSquad)
{
}

// We're not retreating. Advance or attack.
void MicroLurkers::executeMicro(const BWAPI::Unitset & targets, const UnitCluster & cluster)
{
    const BWAPI::Unitset & lurkers = Intersection (getUnits(), cluster.units);
    if (lurkers.empty())
    {
        return;
    }

    _tileSpacing = findTileSpacing();

    // Potential targets.
    BWAPI::Unitset lurkerTargets;
    std::copy_if(targets.begin(), targets.end(), std::inserter(lurkerTargets, lurkerTargets.end()),
        [=](BWAPI::Unit u){
            return
                !u->isFlying() &&
                !infestable(u);
    });
    
    for (BWAPI::Unit lurker : lurkers)
    {
        const bool inOrderRange = lurker->getDistance(order->getPosition()) <= 3 * 32;
        BWAPI::Unit target = getTarget(lurker, lurkerTargets);

        if (target)
        {
            // If our target is a threat, burrow at max range.
            // If our target is safe, get close first.
            // Count a turret as a threat, but not a spore colony. Zerg has overlords.
            const bool isThreat =
                target->getType() == BWAPI::UnitTypes::Terran_Missile_Turret ||
                UnitUtil::CanAttackGround(target) && !target->getType().isWorker();

            const int dist = lurker->getDistance(target);

            const int lurkerRange = BWAPI::UnitTypes::Zerg_Lurker.groundWeapon().maxRange();

            if (Config::Debug::DrawUnitTargets)
            {
                BWAPI::Broodwar->drawLineMap(lurker->getPosition(), target->getPosition(), BWAPI::Colors::Blue);
                if (lurker->getTarget() && lurker->getTarget()->getPosition().isValid())
                {
                    BWAPI::Broodwar->drawLineMap(lurker->getPosition(), lurker->getTarget()->getPosition(), BWAPI::Colors::Orange);
                }
                BWAPI::Broodwar->drawCircleMap(lurker->getPosition(), 12, BWAPI::Colors::Red);
                BWAPI::Broodwar->drawTextMap(lurker->getPosition() + BWAPI::Position(20, -10), "%c%d/192", white, dist);
            }

            if (dist <= 65 || isThreat && dist <= lurkerRange)
            {
                // Burrow or stay burrowed.
                if (lurker->isBurrowed())
                {
                    if (dist <= lurkerRange)
                    {
                        the.micro.AttackUnit(lurker, target);
                    }
                }
                else if (lurker->canBurrow())
                {
                    seekToBurrow(lurkers, lurker);
                }
            }
            else if (!isThreat && dist > lurkerRange ||
                isThreat && dist > std::max(lurkerRange, 31 + UnitUtil::GetAttackRange(target, lurker)))
            {
                // Possibly unburrow and move.
                if (lurker->canUnburrow() &&
                    !inOrderRange &&
                    okToUnburrow(lurker))
                {
                    //BWAPI::Broodwar->printf("unburrow lurker %d with target", lurker->getID());
                    the.micro.Unburrow(lurker);
                }
                else if (lurker->isBurrowed())
                {
                    if (dist <= lurkerRange)
                    {
                        the.micro.AttackUnit(lurker, target);
                    }
                }
                else
                {
                    the.micro.Move(lurker, target->getPosition());
                }
            }
            else
            {
                // In between "close enough to burrow" and "far enough to unburrow".
                // Keep doing whatever we were doing.
                if (lurker->isBurrowed())
                {
                    if (dist <= lurkerRange)
                    {
                        the.micro.AttackUnit(lurker, target);
                    }
                }
                else
                {
                    the.micro.Move(lurker, target->getPosition());
                }
            }
        }
        else
        {
            // No target assigned.
            // Move toward the order position and burrow there.
            // NOTE This happens even if the order is to go to a floating building. It's kinda funny.

            if (Config::Debug::DrawUnitTargets)
            {
                BWAPI::Broodwar->drawLineMap(lurker->getPosition(), order->getPosition(), BWAPI::Colors::White);
            }

            if (inOrderRange)
            {
                if (lurker->canBurrow())
                {
                    seekToBurrow(lurkers, lurker);
                }
            }
            else
            {
                if (lurker->canUnburrow())
                {
                    if (okToUnburrow(lurker))
                    {
                        //BWAPI::Broodwar->printf("unburrow lurker %d without target", lurker->getID());
                        the.micro.Unburrow(lurker);
                    }
                }
                else
                {
                    the.micro.Move(lurker, order->getPosition());
                }
            }
        }
    }
}

// Return a target in range if any exists.
// Only ground targets are passed in.
BWAPI::Unit MicroLurkers::getTarget(BWAPI::Unit lurker, const BWAPI::Unitset & targets)
{
    if (targets.empty())
    {
        return nullptr;
    }

    const int lurkerRange = BWAPI::UnitTypes::Zerg_Lurker.groundWeapon().maxRange();

    BWAPI::Unitset targetsInRange;
    for (BWAPI::Unit target : targets)
    {
        if (lurker->getDistance(target) <= lurkerRange)
        {
            targetsInRange.insert(target);
        }
    }

    // If we can shoot, choose the most distant target because it's more effective.
    if (lurker->isBurrowed() && !targetsInRange.empty())
    {
        return getFarthestTarget(lurker, targetsInRange);
    }

    // If any targets are in lurker range, then always return one of the targets in range.
    // The nearest one is the one we should approach.
    const BWAPI::Unitset & newTargets = targetsInRange.empty() ? targets : targetsInRange;
    return getNearestTarget(lurker, newTargets);
}

//  Only ground units are passed in as potential targets.
int MicroLurkers::getAttackPriority(BWAPI::Unit target) const
{
    BWAPI::UnitType targetType = target->getType();

    // A ghost which is nuking is the highest priority by a mile.
    if (targetType == BWAPI::UnitTypes::Terran_Ghost &&
        target->getOrder() == BWAPI::Orders::NukePaint ||
        target->getOrder() == BWAPI::Orders::NukeTrack)
    {
        return 15;
    }

    // A spider mine on the attack is a time-critical target.
    if (targetType == BWAPI::UnitTypes::Terran_Vulture_Spider_Mine && !target->isBurrowed())
    {
        return 10;
    }
    if (targetType == BWAPI::UnitTypes::Protoss_High_Templar ||
        targetType == BWAPI::UnitTypes::Protoss_Reaver ||
        targetType == BWAPI::UnitTypes::Zerg_Defiler)
    {
        return 10;
    }

    // Something that can attack us or aid in combat
    if (UnitUtil::CanAttackGround(target) && !target->getType().isWorker())
    {
        return 9;
    }
    // turrets are just as bad (cannons are threats, zerg has overlords so spores are meh)
    if (targetType == BWAPI::UnitTypes::Terran_Missile_Turret)
    {
        return 9;
    }
    if (targetType.isWorker())
    {
        if (target->isConstructing() || target->isRepairing())
        {
            return 9;
        }
        return 8;
    }
    if (targetType == BWAPI::UnitTypes::Terran_Medic)
    {
        return 8;
    }
    if (targetType == BWAPI::UnitTypes::Terran_Comsat_Station)      // even if unfinished
    {
        return 7;
    }
    if (targetType == BWAPI::UnitTypes::Protoss_Observatory || targetType == BWAPI::UnitTypes::Protoss_Robotics_Facility)
    {
        return 7;
    }

    return getBackstopAttackPriority(target);
}