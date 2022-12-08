#include "MicroIrradiated.h"

#include "Bases.h"
#include "The.h"

using namespace UAlbertaBot;

// -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- --

// If our lurker burrows here, can it hit an enemy?
bool MicroIrradiated::enemyInLurkerRange(BWAPI::Unit lurker) const
{
    BWAPI::Unit enemy = BWAPI::Broodwar->getClosestUnit(
        lurker->getPosition(),
        BWAPI::Filter::IsEnemy && !BWAPI::Filter::IsFlying,
        6 * 32
    );

    return enemy != nullptr;
}

// The nearest enemy that we can splash with our radiation.
// Visible enemies only. Unseen enemies may have moved.
BWAPI::Unit MicroIrradiated::nearestEnemy(BWAPI::Unit unit) const
{
    // Look farther if we are flying. Flyers are more likely to be able to get there.
    int dist = unit->isFlying() ? 14 * 32 : 10 * 32;

    // NOTE The enemy is terran, so we don't have to check all details.
    //      A zerg building is organic but is unaffected by radiation. But if the enemy
    //      has a zerg building, then the enemy can't irradiate us.
    BWAPI::Unit enemy = BWAPI::Broodwar->getClosestUnit(
        unit->getPosition(),
        BWAPI::Filter::IsEnemy && BWAPI::Filter::IsOrganic,
        dist
    );

    return enemy;           // may be null
}

// Are we putting a nearby friendly unit at risk?
BWAPI::Unit MicroIrradiated::friendNearby(BWAPI::Unit unit) const
{
    BWAPI::Unit friendly = BWAPI::Broodwar->getClosestUnit(
        unit->getPosition(),
        BWAPI::Filter::IsOwned && BWAPI::Filter::IsOrganic && !BWAPI::Filter::IsBuilding,
        6 * 32              // range of irradiate splash is 2; allow a wide safety margin
    );

    return friendly;        // may be null
}

void MicroIrradiated::burrow(BWAPI::Unit unit)
{
    (void) the.micro.Burrow(unit);
}

// Try to expose enemy units to our irradiation splash. And also attack them, if possible.
// The enemy unit is never null.
// NOTE Better would be to stay near as many vulnerable enemies as possible.
void MicroIrradiated::runToEnemy(BWAPI::Unit unit, BWAPI::Unit enemy)
{
    if (unit->getDistance(enemy) < 2 * 32 && unit->canAttack(enemy))
    {
        // We're close enough to cause damage by irradiate splash. Attack.
        the.micro.AttackMove(unit, enemy->getPosition());
    }
    else
    {
        the.micro.MoveNear(unit, enemy->getPosition());
    }
}

// Run away from any vulnerable friendly units. Burrow if possible.
// The friendly unit may be null.
void MicroIrradiated::runAway(BWAPI::Unit unit, BWAPI::Unit friendly)
{
    if (friendly)
    {
        if (unit->canBurrow())
        {
            // The fastest and most reliable escape, when available.
            burrow(unit);
        }
        else
        {
            // We're in danger range of a friendly unit. Treat the friend as an enemy and flee.
            the.micro.fleeEnemy(unit, friendly, 6 * 32);
        }
    }
    else if (the.bases.enemyStart())
    {
        // There is neither opportunity nor danger, so try to scout a little.
        the.micro.AttackMove(unit, the.bases.enemyStart()->getPosition());
    }
    else
    {
        // We got nothing. Just hide in the corner. It's rare.
        the.micro.AttackMove(unit, BWAPI::Positions::Origin);
    }
}

// -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- --

MicroIrradiated::MicroIrradiated()
{
}

void MicroIrradiated::update()
{
    for (BWAPI::Unit unit : getUnits())
    {
        if (unit->isBurrowed() || unit->getOrder() == BWAPI::Orders::Burrowing)
        {
            // Units that can burrow will die from irradiate. Stay underground.
            // NOTE This is safe, but microscopically better play is possible.
        }
        else if (unit->getType() == BWAPI::UnitTypes::Zerg_Lurker && enemyInLurkerRange(unit))
        {
            burrow(unit);
        }
        else if (BWAPI::Unit friendly = friendNearby(unit))
        {
            if (unit->canBurrow())
            {
                burrow(unit);
            }
            else if (BWAPI::Unit enemy = nearestEnemy(unit))
            {
                runToEnemy(unit, enemy);
            }
            else
            {
                runAway(unit, friendly);
            }
        }
        else if (BWAPI::Unit enemy = nearestEnemy(unit))
        {
            runToEnemy(unit, enemy);
        }
        else
        {
            runAway(unit, nullptr);
        }
    }
}
