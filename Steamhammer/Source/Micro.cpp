
#include "Base.h"
#include "InformationManager.h"
#include "MapGrid.h"
#include "The.h"
#include "UnitUtil.h"

using namespace UAlbertaBot;

size_t TotalCommands = 0;  // not all commands are counted

// -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- --

// Complain if there is an "obvious" problem.
// So far, the only problem detected is issuing two orders during the same frame.
void MicroState::check(BWAPI::Unit u, BWAPI::Order o) const
{
    return;	// TODO only continue when debugging

    if (orderFrame == the.now())
    {
        BWAPI::Broodwar->printf(">1 order for %s %d frame %d, %s -> %s",
            UnitTypeName(u).c_str(), u->getID(), the.now(),
            order.getName().c_str(), o.getName().c_str());
    }
}

// Execute the unit's order.
void MicroState::execute(BWAPI::Unit u)
{
    if (order == BWAPI::Orders::Move)
    {
        if (u->getPosition() != targetPosition && the.now() >= lastActionFrame + framesBetweenActions)
        {
            if (u->move(targetPosition))
            {
                lastCheckFrame = executeFrame = the.now();
                needsMonitoring = true;
            }
            lastActionFrame = executeFrame;
        }
    }
    else
    {
        // It's an order we don't support, but pretend to execute it to save time later.
        executeFrame = 0;
        needsMonitoring = false;
    }
}

// Monitor the order: Check for and try to correct failure to execute.
void MicroState::monitor(BWAPI::Unit u)
{
    if (order == BWAPI::Orders::Move)
    {
        if (u->isFlying() || u->getType() == BWAPI::UnitTypes::Protoss_Reaver)
        {
            // Units that don't benefit.
            needsMonitoring = false;
        }
        else if (u->getPosition() == targetPosition)
        {
            // We're there. All done.
            needsMonitoring = false;
        }
        // Some ways to fail:
        // 1. The command might not have been accepted despite the return value.
        else if (u->getOrder() != BWAPI::Orders::Move ||
            u->getLastCommand().getType() != BWAPI::UnitCommandTypes::Move ||
            u->getLastCommand().getTargetPosition() != targetPosition)
        {
            /*
            BWAPI::Broodwar->printf("move command missing for %d (%s %s %d,%d->%d,%d)", u->getID(),
                u->getOrder().getName().c_str(),
                u->getLastCommand().getType().getName().c_str(),
                targetPosition.x, targetPosition.y,
                u->getLastCommand().getTargetPosition().x, u->getLastCommand().getTargetPosition().y);
            */
            u->move(targetPosition);
            lastCheckFrame = the.now();
            lastActionFrame = lastCheckFrame;
        }
        // 2. The unit could be "stuck", moving randomly to escape overlapping with other units.
        else if (u->isStuck())
        {
            // Wait for the trouble to pass.
            lastCheckFrame = the.now();
            // NOTE It's said that spamming move can help. It's worth a try.
        }
        // 3. The unit could be frozen in place "for no reason".
        // NOTE The velocity is calculated using pixels and is never near zero without being equal to zero.
        else if (u->getPosition() == startPosition || u->getVelocityX() == 0.0 && u->getVelocityY() == 0.0)
        {
            // BWAPI::Broodwar->printf("moving unit %d froze velocity %g,%g", u->getID(), u->getVelocityX(), u->getVelocityY());
            u->stop();
            lastCheckFrame = the.now();
            lastActionFrame = lastCheckFrame;
            // On the next retry, the order will not be Move and the Move order will be reissued.
        }
        // 4. The unit could be blocked and unable to make progress. UNIMPLEMENTED
        // OTHERWISE it may be time to switch to the next waypoint.

    }
    else
    {
        // It's an order we don't support. Turn off monitoring to save effort.
        needsMonitoring = false;
    }
    // TODO other commands are not implemented yet
}

// The two positions are so close together that we can treat them as the same
// for purposes of orders which take a position.
bool MicroState::positionsNearlyEqual(BWAPI::Unit u, const BWAPI::Position & pos1, const BWAPI::Position & pos2) const
{
    UAB_ASSERT(pos1.isValid() && pos2.isValid(), "bad position");

    int dist = u->getDistance(pos2);
    int tolerance =
        dist <= 6 * 32
          ? 3                              // tightest tolerance
          : (dist <= 12 * 32               // within tank range?
              ? 6                          // yes, close tolerance
              : std::min(96, dist / 32));  // no, be looser

    return
        abs(pos1.x - pos2.x) <= tolerance &&
        abs(pos1.y - pos2.y) <= tolerance;
}

// -- --

// Create a blank MicroState. Values will be filled in later.
MicroState::MicroState()
    : order(BWAPI::Orders::None)
    , targetUnit(nullptr)
    , targetPosition(BWAPI::Positions::None)
    , attack(0.5)
    , attackUpdateFrame(-1)
    , orderFrame(-1)
    , executeFrame(-1)
    , needsMonitoring(false)
    , lastCheckFrame(-1)
    , lastActionFrame(-framesBetweenActions)       // an action at time 0 must execute
    , startPosition(BWAPI::Positions::None)
{
}

// No-argument order.
void MicroState::setOrder(BWAPI::Unit u, BWAPI::Order o)
{
    check(u, o);

    if (order != o)
    {
        order = o;
        targetPosition = BWAPI::Positions::None;
        targetUnit = nullptr;

        orderFrame = the.now();
        executeFrame = -1;

        startPosition = u->getPosition();
    }
}

// Order that targets a unit.
void MicroState::setOrder(BWAPI::Unit u, BWAPI::Order o, BWAPI::Unit t)
{
    check(u, o);

    if (order != o || targetUnit != t)
    {
        order = o;
        targetPosition = BWAPI::Positions::None;
        targetUnit = t;

        orderFrame = the.now();
        executeFrame = -1;

        startPosition = u->getPosition();
    }
}

// Order that targets a position.
void MicroState::setOrder(BWAPI::Unit u, BWAPI::Order o, BWAPI::Position p)
{
    check(u, o);

    // We check whether positions are "nearly equal" rather than equal
    // to reduce unnecessary orders. Don't spam Starcraft!
    if (order != o || !positionsNearlyEqual(u, targetPosition, p))
    {
        order = o;
        targetPosition = p;
        targetUnit = nullptr;

        orderFrame = the.now();
        executeFrame = -1;

        startPosition = u->getPosition();
    }
}

void MicroState::update(BWAPI::Unit u)
{
    if (executeFrame < 0)
    {
        // Not executed yet. Do that.
        execute(u);
    }
    else if (needsMonitoring && the.now() > lastCheckFrame + 2 * BWAPI::Broodwar->getLatencyFrames())
    {
        // Executed and should be working. Make sure it is.
        monitor(u);
    }
    else if (u->getType().isBuilding() &&
             (order == BWAPI::Orders::ConstructingBuilding || order == BWAPI::Orders::ZergBuildingMorph) &&
             u->isCompleted())
    {
        // Leaving an old morph order around can prevent future morphs from happening.
        // The only case is morphing a lair into a hive (but it's a critical one).
        order = BWAPI::Orders::None;
        targetPosition = BWAPI::Positions::None;
        targetUnit = nullptr;

        orderFrame = the.now();
        executeFrame = 0;
    }
}

// `a` is true if the combat sim tells the unit to participate in an attack this frame, false if to regroup.
// Maintain floating point `attack` which smooths the attack/retreat values over time using an
// exponential moving average.
void MicroState::setAttack(bool a)
{
    // Don't try to update twice per frame.
    UAB_ASSERT(attackUpdateFrame < the.now(), "double attack update");

    // Decay the previous attack value depending on how much time has passed.
    if (attackUpdateFrame != -1)
    {
        attack *= std::pow(1.0 - attackDecay, the.now() - attackUpdateFrame);
    }
    attackUpdateFrame = the.now();

    // And add in the new value.
    attack += a ? attackDecay : 0.0;
}

// Return the smoothed attack/retreat value.
// It will be combined with values from other units to make decisions.
double MicroState::getSmoothedAttack() const
{
    return attack;
}

void MicroState::draw(BWAPI::Unit u) const
{
    int x = u->getRight() + 2;
    int y = u->getTop();

    if (order == BWAPI::Orders::Move)
    {
        BWAPI::Broodwar->drawTextMap(x, y, "move %d,%d", targetPosition.x, targetPosition.y);
    }
    else
    {
        BWAPI::Broodwar->drawTextMap(x, y, "%s", order.getName().c_str());
    }
}

// -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- --

// For drawing debug info on the screen.
const int dotRadius = 2;

// Execute any micro commands stored up this frame, and generally try to make sure
// that units do what they have been ordered to.
void Micro::update()
{
    for (auto it = orders.begin(); it != orders.end(); )
    {
        BWAPI::Unit u = (*it).first;
        MicroState & o = (*it).second;

        if (u->exists())
        {
            if (o.getOrder() == BWAPI::Orders::Move)
            {
                int x = u->getRight() + 2;
                int y = u->getTop() - 10;
            }

            o.update(u);

            // NOTE Execute orders before drawing the state.
            if (Config::Debug::DrawMicroState)
            {
                o.draw(u);
            }

            ++it;
        }
        else
        {
            // Delete records for units which are gone.
            it = orders.erase(it);
        }
    }
}

bool Micro::alwaysKite(BWAPI::UnitType type) const
{
    return
        type == BWAPI::UnitTypes::Terran_Vulture ||
        type == BWAPI::UnitTypes::Terran_Wraith ||
        type == BWAPI::UnitTypes::Zerg_Mutalisk ||
        type == BWAPI::UnitTypes::Zerg_Guardian;
}

// Is the distance inside the target distance?
bool Micro::distanceIs(int x, int y, const GridDistances * distances, int target) const
{
    BWAPI::TilePosition xy(x, y);
    return xy.isValid() && distances->at(xy) < target && distances->at(xy) >= 0;
}

// If the distances are given and we can get there from here, return a nearby position on a good path.
// In any other case, return the ultimate destination.
BWAPI::Position Micro::nextGroundDestination(BWAPI::Unit unit, const BWAPI::Position & destination, const GridDistances * distances) const
{
    if (!distances || !unit->getTilePosition().isValid())
    {
        return destination;
    }

    const int StepSize = 8;
    int here = distances->at(unit->getTilePosition());
    if (here < StepSize)
    {
        // Either we're already very near, or we can't get there. Same answer for both.
        // NOTE This lets distances be slightluy offset from the true distances without error.
        return destination;
    }

    // Use the distances map to find our next waypoint.
    // One tile of every StepSize tiles along the path is a waypoint.
    // When we are halfway between waypoints, we switch to the following one.
    // In other words, if we're going to X and we're halfway between B and C, we switch from waypoint C to D.
    const int phase = here % StepSize;
    const int target = std::max(0, here - phase - (phase > StepSize / 2 ? 0 : StepSize));

    int x = unit->getTilePosition().x;
    int y = unit->getTilePosition().y;
    while (here > target)
    {
        UAB_ASSERT(BWAPI::TilePosition(x, y).isValid(), "bad tile %d,%d", x, y);
        UAB_ASSERT(distances->at(BWAPI::TilePosition(x, y)) >= 0, "inaccessible tile %d,%d", x, y);

        // Unroll the loop by hand.
        // Check diagonals first, so that we prefer to move diagonally when it's shortest.
             if (distanceIs(x-1, y-1, distances, here)) { x = x-1; y = y-1; }
        else if (distanceIs(x+1, y-1, distances, here)) { x = x+1; y = y-1; }
        else if (distanceIs(x+1, y+1, distances, here)) { x = x+1; y = y+1; }
        else if (distanceIs(x-1, y+1, distances, here)) { x = x-1; y = y+1; }
        // Then check the orthogonal directions.
        else if (distanceIs(x-1, y  , distances, here)) { x = x-1; y = y  ; }
        else if (distanceIs(x+1, y  , distances, here)) { x = x+1; y = y  ; }
        else if (distanceIs(x  , y-1, distances, here)) { x = x  ; y = y-1; }
        else if (distanceIs(x  , y+1, distances, here)) { x = x  ; y = y+1; }
        else
        {
            // We failed to find a way to advance. That should not happen.
            UAB_ASSERT(false, "can't go from %d,%d", x, y);
            break;
        }

        here = distances->at(BWAPI::TilePosition(x, y));        // closer by 1 or 2 tiles
    }

    UAB_ASSERT(BWAPI::TilePosition(x, y).isValid(), "bad tile %d,%d", x, y);
    UAB_ASSERT(distances->at(BWAPI::TilePosition(x, y)) >= 0, "inaccessible tile %d,%d", x, y);

    return TileCenter(BWAPI::TilePosition(x, y));
}

// -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- --

Micro::Micro()
{
}

const MicroState & Micro::getMicroState(BWAPI::Unit unit) const
{
    auto it = orders.find(unit);

    if (it == orders.end())
    {
        return noOrder;
    }

    return (*it).second;
}

// Was the unit already given a command this frame?
bool Micro::alreadyCommanded(BWAPI::Unit unit) const
{
    auto it = orders.find(unit);

    if (it == orders.end())
    {
        return false;
    }

    return (*it).second.getOrderFrame() >= the.now();
}

// Is the given unit in danger of enemy weapons fire, with the given safety margin (in pixels)?
// If so, return the nearest enemy that puts the unit in danger.
// NOTE The check is quick and dirty, not exact. It will make mistakes.
BWAPI::Unit Micro::inWeaponsDanger(BWAPI::Unit unit, int margin) const
{
    BWAPI::Unit enemy;
    if (unit->isFlying())
    {
        // Flying unit.
        enemy = BWAPI::Broodwar->getClosestUnit(
            unit->getPosition(),
            BWAPI::Filter::IsEnemy &&
            (BWAPI::Filter::AirWeapon != BWAPI::WeaponTypes::None || BWAPI::Filter::GetType == BWAPI::UnitTypes::Terran_Bunker || BWAPI::Filter::GetType == BWAPI::UnitTypes::Protoss_Carrier) &&
            BWAPI::Filter::GetType != BWAPI::UnitTypes::Protoss_Interceptor &&
            BWAPI::Filter::IsCompleted,
            margin + 8 * 32
        );
    }
    else
    {
        // Ground unit.
        enemy = BWAPI::Broodwar->getClosestUnit(
            unit->getPosition(),
            BWAPI::Filter::IsEnemy &&
            !BWAPI::Filter::IsWorker &&
            (BWAPI::Filter::GroundWeapon != BWAPI::WeaponTypes::None || BWAPI::Filter::GetType == BWAPI::UnitTypes::Terran_Bunker || BWAPI::Filter::GetType == BWAPI::UnitTypes::Protoss_Reaver || BWAPI::Filter::GetType == BWAPI::UnitTypes::Protoss_Carrier) &&
            BWAPI::Filter::GetType != BWAPI::UnitTypes::Protoss_Interceptor &&
            BWAPI::Filter::IsCompleted,
            margin + (the.info.enemyHasSiegeMode() ? 12 * 32 : 8 * 32)
        );
    }

    if (enemy && unit->getDistance(enemy) < margin + UnitUtil::GetAttackRangeAssumingUpgrades(enemy->getType(), unit->getType()))
    {
        return enemy;
    }
    return nullptr;
}

// Is this a safe spot for the given unit to flee to? Useful for nearby spots only.
// "Safe" mostly means that we should not get stuck on terrain, etc.
bool Micro::canFleeTo(BWAPI::Unit unit, const BWAPI::Position & destination) const
{
    // Don't flee off the map.
    if (!destination.isValid())
    {
        return false;
    }

    // Don't flee into enemy static defense, unless we're already in its range.
    BWAPI::TilePosition tile(destination);
    if (the.attacks(unit, tile) > the.attacks(unit))
    {
        return false;
    }

    // For a flying unit, that's all we want to check.
    if (unit->isFlying())
    {
        return true;
    }

    // Don't flee somewhere we can't walk on the terrain.
    if (!the.map.isWalkable(tile))
    {
        return false;
    }

    // Don't flee to a tile blocked by a building or unit.
    // The check conservatively assumes that any other ground unit may block us.
    BWAPI::Unitset blocks = BWAPI::Broodwar->getUnitsOnTile(tile, !BWAPI::Filter::IsFlying && !BWAPI::Filter::IsBurrowed);
    for (BWAPI::Unit u : blocks)
    {
        if (u != unit)      // no way to check this with a filter, apparently
        {
            return false;
        }
    }

    return true;
}

// Kite backward a short distance, in a direction away from the danger point.
// If that's impossible (something is in the way), return false instead.
bool Micro::kiteBack(BWAPI::Unit unit, BWAPI::Unit enemy)
{
    BWAPI::Position destination = RawDistanceAndDirection(unit->getPosition(), enemy->getPosition(), -48);

    //BWAPI::Broodwar->drawLineMap(unit->getPosition(), destination, BWAPI::Colors::Grey);

    if (canFleeTo(unit, destination))
    {
        Move(unit, destination);
        //BWAPI::Broodwar->drawLineMap(unit->getPosition(), destination, BWAPI::Colors::Green);
        return true;
    }
    else
    {
        // The direct retreat does not work. Let's see if we can sidestep.
        if (!unit->isFlying() && !enemy->isFlying())
        {
            int range = UnitUtil::GetAttackRange(enemy, unit);
            if (range > 0 && range <= 32)
            {
                int enemyDist = unit->getDistance(enemy);
                for (int dx = -32; dx <= 32; dx += 32)
                {
                    for (int dy = -32; dy <= 32; dy += 32)
                    {
                        if (dx != 0 || dy != 0)
                        {
                            destination = unit->getPosition() + BWAPI::Position(dx, dy);
                            if (canFleeTo(unit, destination) && enemy->getDistance(destination) > enemyDist)
                            {
                                Move(unit, destination);
                                //BWAPI::Broodwar->drawLineMap(unit->getPosition(), destination, BWAPI::Colors::Blue);
                                return true;
                            }
                            else
                            {
                                //BWAPI::Broodwar->drawLineMap(unit->getPosition(), destination, BWAPI::Colors::Purple);
                            }
                        }
                    }
                }
            }
        }
        //BWAPI::Broodwar->drawCircleMap(unit->getPosition(), 3, BWAPI::Colors::Green);
    }

    return false;
}

BWAPI::Position Micro::fleeTo(BWAPI::Unit unit, const BWAPI::Position & danger, int distance) const
{
    return DistanceAndDirection(unit->getPosition(), danger, -distance);
}

void Micro::fleePosition(BWAPI::Unit unit, const BWAPI::Position & danger, int distance)
{
    BWAPI::Position destination = fleeTo(unit, danger, distance);
    the.micro.MoveNear(unit, destination);
    if (Config::Debug::DrawUnitTargets)
    {
        BWAPI::Broodwar->drawLineMap(unit->getPosition(), destination, BWAPI::Colors::Green);
    }
}

void Micro::fleeEnemy(BWAPI::Unit unit, BWAPI::Unit enemy, int distance)
{
    fleePosition(unit, enemy->getPosition(), distance);
}

// If our ground unit is next to an undetected dark templar, run it away and return true.
// Otherwise return false.
bool Micro::fleeDT(BWAPI::Unit unit)
{
    if (!unit || !unit->exists() || unit->getPlayer() != the.self() || !unit->getPosition().isValid())
    {
        UAB_ASSERT(false, "bad arg");
        return false;
    }

    if (!unit->isFlying() && unit->canMove())
    {
        BWAPI::Unit dt = BWAPI::Broodwar->getClosestUnit(
            unit->getPosition(),
            BWAPI::Filter::GetType == BWAPI::UnitTypes::Protoss_Dark_Templar &&
            BWAPI::Filter::IsEnemy &&
            !BWAPI::Filter::IsDetected,
            64);

        if (dt)
        {
            fleeEnemy(unit, dt);
            return true;
        }
    }

    return false;
}

void Micro::Stop(BWAPI::Unit unit)
{
    if (!unit || !unit->exists() || unit->getPlayer() != the.self())
    {
        UAB_ASSERT(false, "bad arg");
        return;
    }

    // if we have issued a command to this unit already this frame, ignore this one
    if (unit->getLastCommandFrame() >= the.now() || unit->isAttackFrame())
    {
        return;
    }

    // If we already gave this command, don't repeat it.
    BWAPI::UnitCommand currentCommand(unit->getLastCommand());
    if (currentCommand.getType() == BWAPI::UnitCommandTypes::Stop)
    {
        return;
    }

    orders[unit].setOrder(unit, BWAPI::Orders::Stop);

    unit->stop();
    TotalCommands++;
}

void Micro::HoldPosition(BWAPI::Unit unit)
{
    if (!unit || !unit->exists() || unit->getPlayer() != the.self())
    {
        UAB_ASSERT(false, "bad arg");
        return;
    }

    // if we have issued a command to this unit already this frame, ignore this one
    if (unit->getLastCommandFrame() >= the.now() || unit->isAttackFrame())
    {
        return;
    }

    // If we already gave this command, don't repeat it.
    BWAPI::UnitCommand currentCommand(unit->getLastCommand());
    if (currentCommand.getType() == BWAPI::UnitCommandTypes::Hold_Position)
    {
        return;
    }

    orders[unit].setOrder(unit, BWAPI::Orders::HoldPosition);

    unit->holdPosition();
    TotalCommands++;
}

// If the target is moving, chase it.
// If it's not moving or we're in range and ready, attack it.
void Micro::CatchAndAttackUnit(BWAPI::Unit attacker, BWAPI::Unit target)
{
    if (!attacker || !attacker->exists() || attacker->getPlayer() != the.self() ||
        !target || !target->exists())
    {
        UAB_ASSERT(false, "bad arg");
        return;
    }

    if (!target->isMoving() || !attacker->canMove() || attacker->isInWeaponRange(target))
    {
        //BWAPI::Broodwar->drawLineMap(attacker->getPosition(), target->getPosition(), BWAPI::Colors::Orange);
        AttackUnit(attacker, target);
    }
    else
    {
        if (attacker->getDistance(target) > 8 * 32)
        {
            // The target is far away, so be lazy.
            BWAPI::Position destination = PredictMovement(target, 12);
            //BWAPI::Broodwar->drawLineMap(attacker->getPosition(), destination, BWAPI::Colors::Red);
            MoveNear(attacker, destination);
        }
        else if (attacker->getDistance(target) > 4 * 32)
        {
            BWAPI::Position destination = PredictMovement(target, 8);
            //BWAPI::Broodwar->drawLineMap(attacker->getPosition(), destination, BWAPI::Colors::Orange);
            Move(attacker, destination);
        }
        else
        {
            // The target is near. Aim for its predicted position.
            // NOTE The caller should have already decided that we can catch the target.
            int frames = UnitUtil::FramesToReachAttackRange(attacker, target);
            BWAPI::Position destination = PredictMovement(target, std::min(frames, 8));
            //BWAPI::Broodwar->drawLineMap(target->getPosition(), destination, BWAPI::Colors::Blue);
            //BWAPI::Broodwar->drawLineMap(attacker->getPosition(), destination, BWAPI::Colors::Yellow);
            Move(attacker, destination);
        }
    }
}

void Micro::AttackUnit(BWAPI::Unit attacker, BWAPI::Unit target)
{
    if (!attacker || !attacker->exists() || attacker->getPlayer() != the.self() ||
        !target || !target->exists())
    {
        UAB_ASSERT(false, "bad arg");
        return;
    }

    // Do nothing if we've already issued a command this frame, or the unit is busy attacking.
    // NOTE A lurker attacking a fixed target is ALWAYS on an attack frame.
    //      According to Arrak, sunken colonies behave the same.
    if (attacker->getLastCommandFrame() >= the.now() ||
        (attacker->isAttackFrame() && attacker->getType() != BWAPI::UnitTypes::Zerg_Lurker))
    {
        return;
    }
    
    // get the unit's current command
    BWAPI::UnitCommand currentCommand(attacker->getLastCommand());

    // if we've already told this unit to attack this target, ignore this command
    if (currentCommand.getType() == BWAPI::UnitCommandTypes::Attack_Unit &&	currentCommand.getTarget() == target)
    {
        return;
    }
    
    orders[attacker].setOrder(attacker, BWAPI::Orders::AttackUnit, target);

    // if nothing prevents it, attack the target
    attacker->attack(target);
    TotalCommands++;

    if (Config::Debug::DrawUnitTargets) 
    {
        BWAPI::Broodwar->drawCircleMap(attacker->getPosition(), dotRadius, BWAPI::Colors::Red, true);
        BWAPI::Broodwar->drawCircleMap(target->getPosition(), dotRadius, BWAPI::Colors::Red, true);
        BWAPI::Broodwar->drawLineMap(attacker->getPosition(), target->getPosition(), BWAPI::Colors::Red);
    }
}

void Micro::AttackMove(BWAPI::Unit attacker, const BWAPI::Position & targetPosition)
{
    if (!attacker || !attacker->exists() || attacker->getPlayer() != the.self() || !targetPosition.isValid())
    {
        UAB_ASSERT(false, "bad arg");
        return;
    }

    // if we have issued a command to this unit already this frame, ignore this one
    if (attacker->getLastCommandFrame() >= the.now() || attacker->isAttackFrame())
    {
        return;
    }

    // get the unit's current command
    BWAPI::UnitCommand currentCommand(attacker->getLastCommand());

    // if we've already told this unit to attack this target, ignore this command
    if (currentCommand.getType() == BWAPI::UnitCommandTypes::Attack_Move &&	currentCommand.getTargetPosition() == targetPosition)
    {
        return;
    }

    orders[attacker].setOrder(attacker, BWAPI::Orders::AttackMove, targetPosition);

    // if nothing prevents it, attack the target
    attacker->attack(targetPosition);
    TotalCommands++;

    if (Config::Debug::DrawUnitTargets) 
    {
        BWAPI::Broodwar->drawCircleMap(attacker->getPosition(), dotRadius, BWAPI::Colors::Orange, true);
        BWAPI::Broodwar->drawCircleMap(targetPosition, dotRadius, BWAPI::Colors::Orange, true);
        BWAPI::Broodwar->drawLineMap(attacker->getPosition(), targetPosition, BWAPI::Colors::Orange);
    }
}

void Micro::Move(BWAPI::Unit unit, const BWAPI::Position & targetPosition, const GridDistances * distances)
{
    if (!unit || !unit->exists() || unit->getPlayer() != the.self() || !targetPosition.isValid())
    {
        UAB_ASSERT(false, "bad arg");
        return;
    }
    
    BWAPI::Position destination = nextGroundDestination(unit, targetPosition, distances);

    orders[unit].setOrder(unit, BWAPI::Orders::Move, destination);

    TotalCommands++;

    if (Config::Debug::DrawUnitTargets)
    {
        BWAPI::Broodwar->drawCircleMap(unit->getPosition(), dotRadius, BWAPI::Colors::White, true);
        BWAPI::Broodwar->drawCircleMap(destination, dotRadius, BWAPI::Colors::White, true);
        BWAPI::Broodwar->drawLineMap(unit->getPosition(), destination, BWAPI::Colors::White);
        BWAPI::Broodwar->drawLineMap(destination, targetPosition, BWAPI::Colors::Orange);
   }
}

// Like Move, but lazier. If the new target position is close to the previous one,
// then don't be in a hurry to update the order.
// Suitable for approaching a moving target that is distant, or slow, or needs little accuracy.
// This reduces unnecessary orders. It's up to the caller to decide that they may be unnecessary.
// With fewer orders, units are less likely to get stuck.
void Micro::MoveNear(BWAPI::Unit unit, const BWAPI::Position & targetPosition, const GridDistances * distances)
{
    auto it = orders.find(unit);
    if (it == orders.end())
    {
        // The unit doesn't have an existing order.
        Move(unit, targetPosition, distances);
    }
    else
    {
        // The unit has an existing order. Check it.
        MicroState & state = it->second;

        if (state.getOrder() != BWAPI::Orders::Move ||
            the.now() - state.getOrderFrame() >= 18 ||
            state.getTargetPosition().getApproxDistance(targetPosition) > 2 * 32)
        {
            Move(unit, targetPosition, distances);
        }
        // Otherwise do nothing. It's close enough.
    }
}

// Move toward the given destination, unless danger is visible, in which case move
// away from the danger. Call this every frame (or every n frames) to attempt to
// follow a safe path toward the destination.
// Suitable for units which never want to come under fire, such as scouting units.
// Return true if we fled danger, otherwise false.
bool Micro::MoveSafely(BWAPI::Unit unit, const BWAPI::Position & targetPosition, const GridDistances * distances)
{
    if (!unit || !unit->exists() || unit->getPlayer() != the.self() || !unit->getPosition().isValid() ||
        !targetPosition.isValid())
    {
        UAB_ASSERT(false, "bad arg");
        return false;
    }

    int margin = 32;
    if (unit->getType().isWorker() || !UnitUtil::TypeCanAttack(unit->getType()))
    {
        margin = 2 * 32;
    }

    BWAPI::Unit enemy = inWeaponsDanger(unit, margin);
    if (enemy)
    {
        if (!enemy->getType().isBuilding())
        {
            // Look for friendly nearby units that we can flee toward.
            const std::vector<UnitCluster> & defenders =
                enemy->isFlying() ? the.ops.getAirDefenseClusters() : the.ops.getGroundDefenseClusters();
            const UnitCluster * bestCluster = nullptr;
            int lowestScore = 5 * 24;
            for (const UnitCluster & cluster : defenders)
            {
                // Time in frames for each unit to reach the cluster.
                // The times may be negative if the unit is already inside the cluster's radius.
                int friendTime = int((unit->getDistance(cluster.center) - cluster.radius) / (the.self()->topSpeed(unit->getType())));
                int enemyTime = int((enemy->getDistance(cluster.center) - cluster.radius) / (the.enemy()->topSpeed(enemy->getType())));
                int enemyTimeClipped = std::max(0, enemyTime);  // OK if enemy is deeper inside

                int score = friendTime - enemyTimeClipped;
                if (score < lowestScore)
                {
                    bestCluster = &cluster;
                    lowestScore = score;
                }
            }
            if (bestCluster)
            {
                MoveNear(unit, bestCluster->center);
                return true;
            }
        }

        // No nearby units to flee toward. Flee away from the enemy instead.
        fleeEnemy(unit, enemy);
        return true;
    }

    MoveNear(unit, targetPosition, distances);
    return false;
}

void Micro::RightClick(BWAPI::Unit unit, BWAPI::Unit target)
{
    if (!unit || !unit->exists() || unit->getPlayer() != the.self() ||
        !target || !target->exists())
    {
        UAB_ASSERT(false, "bad arg");
        return;
    }

    // if we have issued a command to this unit already this frame, ignore this one
    if (unit->getLastCommandFrame() >= the.now() || unit->isAttackFrame())
    {
        return;
    }

    // get the unit's current command
    BWAPI::UnitCommand currentCommand(unit->getLastCommand());

    // if we've already told this unit to right-click this target, ignore this command
    if ((currentCommand.getType() == BWAPI::UnitCommandTypes::Right_Click_Unit) && (currentCommand.getTargetPosition() == target->getPosition()))
    {
        return;
    }

    // NOTE This treats a right-click on one of our own units as an order to attack it.
    BWAPI::Order order = BWAPI::Orders::AttackUnit;
    if (target->getType() == BWAPI::UnitTypes::Resource_Mineral_Field)
    {
        order = BWAPI::Orders::MoveToMinerals;
    }
    else if (target->getType().isRefinery())
    {
        order = BWAPI::Orders::MoveToGas;
    }
    orders[unit].setOrder(unit, order, target);

    // if nothing prevents it, attack the target
    unit->rightClick(target);
    TotalCommands++;

    if (Config::Debug::DrawUnitTargets) 
    {
        BWAPI::Broodwar->drawCircleMap(unit->getPosition(), dotRadius, BWAPI::Colors::Cyan, true);
        BWAPI::Broodwar->drawCircleMap(target->getPosition(), dotRadius, BWAPI::Colors::Cyan, true);
        BWAPI::Broodwar->drawLineMap(unit->getPosition(), target->getPosition(), BWAPI::Colors::Cyan);
    }
}

// This is part of the support for mineral locking. It works for gathering resources.
// To mine out blocking minerals with zero resources, or to mineral walk, use RightClick() instead.
void Micro::MineMinerals(BWAPI::Unit unit, BWAPI::Unit mineralPatch)
{
    if (!unit || !unit->exists() || unit->getPlayer() != the.self() ||
        !mineralPatch || !mineralPatch->exists() || !mineralPatch->getType().isMineralField())
    {
        UAB_ASSERT(false, "bad arg");
        return;
    }

    // if we have issued a command to this unit already this frame, ignore this one
    if (unit->getLastCommandFrame() >= the.now() || unit->isAttackFrame())
    {
        return;
    }

    orders[unit].setOrder(unit, BWAPI::Orders::MoveToMinerals, mineralPatch);

    unit->rightClick(mineralPatch);
    TotalCommands++;

    if (Config::Debug::DrawUnitTargets)
    {
        BWAPI::Broodwar->drawLineMap(unit->getPosition(), mineralPatch->getPosition(), BWAPI::Colors::Cyan);
    }
}

void Micro::LaySpiderMine(BWAPI::Unit unit, const BWAPI::Position & pos)
{
    if (!unit || !unit->exists() || unit->getPlayer() != the.self() || !pos.isValid())
    {
        UAB_ASSERT(false, "bad arg");
        return;
    }

    if (!unit->canUseTech(BWAPI::TechTypes::Spider_Mines, pos))
    {
        return;
    }

    BWAPI::UnitCommand currentCommand(unit->getLastCommand());

    // if we've already told this unit to move to this position, ignore this command
    if ((currentCommand.getType() == BWAPI::UnitCommandTypes::Use_Tech_Position) && (currentCommand.getTargetPosition() == pos))
    {
        return;
    }

    orders[unit].setOrder(unit, BWAPI::Orders::PlaceMine, pos);

    unit->canUseTechPosition(BWAPI::TechTypes::Spider_Mines, pos);
}

void Micro::Repair(BWAPI::Unit unit, BWAPI::Unit target)
{
    if (!unit || !unit->exists() || unit->getPlayer() != the.self() ||
        !target || !target->exists())
    {
        UAB_ASSERT(false, "bad arg");
        return;
    }

    // if we have issued a command to this unit already this frame, ignore this one
    if (unit->getLastCommandFrame() >= the.now() || unit->isAttackFrame())
    {
        return;
    }

    // get the unit's current command
    BWAPI::UnitCommand currentCommand(unit->getLastCommand());

    // if we've already told this unit to move to this position, ignore this command
    if ((currentCommand.getType() == BWAPI::UnitCommandTypes::Repair) && (currentCommand.getTarget() == target))
    {
        return;
    }

    orders[unit].setOrder(unit, BWAPI::Orders::Repair, target);

    // Nothing prevents it, so attack the target.
    unit->repair(target);
    TotalCommands++;

    if (Config::Debug::DrawUnitTargets) 
    {
        BWAPI::Broodwar->drawCircleMap(unit->getPosition(), dotRadius, BWAPI::Colors::Cyan, true);
        BWAPI::Broodwar->drawCircleMap(target->getPosition(), dotRadius, BWAPI::Colors::Cyan, true);
        BWAPI::Broodwar->drawLineMap(unit->getPosition(), target->getPosition(), BWAPI::Colors::Cyan);
    }
}

void Micro::ReturnCargo(BWAPI::Unit worker)
{
    if (!worker || !worker->exists() || worker->getPlayer() != the.self() ||
        !worker->getType().isWorker())
    {
        UAB_ASSERT(false, "bad arg");
        return;
    }

    // If the worker has no cargo, ignore this command.
    if (!worker->isCarryingMinerals() && !worker->isCarryingGas())
    {
        return;
    }

    // if we have issued a command to this unit already this frame, ignore this one
    if (worker->getLastCommandFrame() >= the.now() || worker->isAttackFrame())
    {
        return;
    }

    // If we've already issued this command, don't issue it again.
    BWAPI::UnitCommand currentCommand(worker->getLastCommand());
    if (currentCommand.getType() == BWAPI::UnitCommandTypes::Return_Cargo)
    {
        return;
    }

    orders[worker].setOrder(worker, worker->isCarryingMinerals() ? BWAPI::Orders::ReturnMinerals : BWAPI::Orders::ReturnGas);

    // Nothing prevents it, so return cargo.
    worker->returnCargo();
    TotalCommands++;
}

// Order construction of a building.
bool Micro::Build(BWAPI::Unit builder, BWAPI::UnitType building, const BWAPI::TilePosition & location)
{
    if (!builder || !builder->exists() || !builder->getType().isWorker() ||
        !builder->getPosition().isValid() || builder->isBurrowed() ||
        builder->getPlayer() != the.self() ||
        !building.isBuilding() || !location.isValid())
    {
        UAB_ASSERT(false, "bad building");
        return false;
    }

    // NOTE This order does not set the type or location.
    //      The purpose is only to remind MicroState that the unit has this order.
    orders[builder].setOrder(builder, BWAPI::Orders::ConstructingBuilding);

    return builder->build(building, location);
}

// Order production of a unit. Includes terran addon and zerg morphed building or unit.
bool Micro::Make(BWAPI::Unit producer, BWAPI::UnitType type)
{
    if (!producer || !producer->exists() || !producer->getPosition().isValid() || producer->getType().getRace() != type.getRace())
    {
        UAB_ASSERT(false, "bad unit");
        return false;
    }

    // NOTE We do not set the type.
    //      The purpose is only to remind MicroState that the unit has this order.

    if (type.isAddon())
    {
        orders[producer].setOrder(producer, BWAPI::Orders::BuildAddon);
        return producer->buildAddon(type);
    }

    // Zerg morphs units, except for the infested terran, which is trained. Tricky!
    if (type.getRace() == BWAPI::Races::Zerg && type != BWAPI::UnitTypes::Zerg_Infested_Terran)
    {
        orders[producer].setOrder(producer, type.isBuilding() ? BWAPI::Orders::ZergBuildingMorph : BWAPI::Orders::ZergUnitMorph);
        return producer->morph(type);
    }

    orders[producer].setOrder(producer, BWAPI::Orders::Train);
    return producer->train(type);
}

// Cancel a building under construction or a morphing unit.
bool Micro::Cancel(BWAPI::Unit unit)
{
    if (!unit || !unit->exists() || !unit->getPosition().isValid())
    {
        UAB_ASSERT(false, "bad unit");
        return false;
    }

    orders[unit].setOrder(unit, BWAPI::Orders::Stop);

    return unit->cancelMorph() || unit->cancelConstruction();
}

bool Micro::Lift(BWAPI::Unit terranBuilding)
{
    if (!terranBuilding || !terranBuilding->exists() ||
        !terranBuilding->getType().isBuilding() ||
        terranBuilding->getPlayer() != the.self())
    {
        UAB_ASSERT(false, "bad unit");
        return false;
    }

    orders[terranBuilding].setOrder(terranBuilding, BWAPI::Orders::LiftingOff);

    return terranBuilding->lift();
}

// Burrow a zerg unit.
bool Micro::Burrow(BWAPI::Unit unit)
{
    if (!unit || !unit->exists() || !unit->getPosition().isValid())
    {
        UAB_ASSERT(false, "bad unit");
        return false;
    }

    // The Orders are Burrowing and Burrowed. Also lurkers can do stuff while burrowed.
    orders[unit].setOrder(unit, BWAPI::Orders::Burrowing);

    if (unit->isBurrowed() || unit->getOrder() == BWAPI::Orders::Burrowing)
    {
        // Already done, so don't issue the order.
        return true;
    }

    return unit->burrow();
}

// Unburrow a zerg unit.
bool Micro::Unburrow(BWAPI::Unit unit)
{
    if (!unit || !unit->exists() || !unit->getPosition().isValid())
    {
        UAB_ASSERT(false, "bad unit");
        return false;
    }

    orders[unit].setOrder(unit, BWAPI::Orders::Unburrowing);

    return unit->unburrow();
}

// Load a unit into a bunker or transport.
bool Micro::Load(BWAPI::Unit container, BWAPI::Unit content)
{
    if (!container || !container->exists() || !content || !content->exists() || !content->getPosition().isValid())
    {
        UAB_ASSERT(false, "bad unit");
        return false;
    }

    if (container->getType() == BWAPI::UnitTypes::Terran_Bunker)
    {
        orders[container].setOrder(container, BWAPI::Orders::PickupBunker, content);
    }
    else
    {
        orders[container].setOrder(container, BWAPI::Orders::PickupTransport, content);
    }

    return container->load(content);
}

// Move to the given position and unload all units from a transport.
bool Micro::UnloadAt(BWAPI::Unit container, const BWAPI::Position & targetPosition)
{
    if (!container || !container->exists() || !container->getPosition().isValid() ||
        !targetPosition.isValid())
    {
        UAB_ASSERT(false, "bad unit");
        return false;
    }

    orders[container].setOrder(container, BWAPI::Orders::MoveUnload);

    return container->unloadAll(targetPosition);
}

// Unload all units from a bunker or transport.
bool Micro::UnloadAll(BWAPI::Unit container)
{
    if (!container || !container->exists() || !container->getPosition().isValid())
    {
        UAB_ASSERT(false, "bad unit");
        return false;
    }

    orders[container].setOrder(container, BWAPI::Orders::Unload);

    return container->unloadAll();
}

// Siege a tank.
bool Micro::Siege(BWAPI::Unit tank)
{
    if (!tank || !tank->exists() || !tank->getPosition().isValid())
    {
        UAB_ASSERT(false, "bad unit");
        return false;
    }

    // The Orders are Burrowing and Burrowed. Also lurkers can do stuff while burrowed.
    orders[tank].setOrder(tank, BWAPI::Orders::Sieging);

    return tank->siege();
}

// Unsiege a tank.
bool Micro::Unsiege(BWAPI::Unit tank)
{
    if (!tank || !tank->exists() || !tank->getPosition().isValid())
    {
        UAB_ASSERT(false, "bad unit");
        return false;
    }

    orders[tank].setOrder(tank, BWAPI::Orders::Unsieging);

    return tank->unsiege();
}

// Perform a comsat scan at the given position if possible and necessary.
// If it's not possible, or we already scanned there, do nothing.
// Return whether the scan occurred.
// NOTE Comsat scan does not use the orders[] mechanism.
bool Micro::Scan(const BWAPI::Position & targetPosition)
{
    UAB_ASSERT(targetPosition.isValid(), "bad position");

    // If a scan of this position is still active, don't scan it again.
    if (MapGrid::Instance().scanIsActiveAt(targetPosition))
    {
        return false;
    }

    // Choose the comsat with the highest energy.
    // If we're not terran, we're unlikely to have any comsats....
    int maxEnergy = 49;      // anything greater is enough energy for a scan
    BWAPI::Unit comsat = nullptr;
    for (BWAPI::Unit unit : the.self()->getUnits())
    {
        if (unit->getType() == BWAPI::UnitTypes::Terran_Comsat_Station &&
            unit->getEnergy() > maxEnergy &&
            unit->canUseTech(BWAPI::TechTypes::Scanner_Sweep, targetPosition))
        {
            maxEnergy = unit->getEnergy();
            comsat = unit;
        }
    }

    if (comsat)
    {
        MapGrid::Instance().scanAtPosition(targetPosition);
        return comsat->useTech(BWAPI::TechTypes::Scanner_Sweep, targetPosition);
    }

    return false;
}

// Stim the given marine or firebat, if possible; otherwise, do nothing.
// Return whether the stim occurred.
// NOTE Stim does not use the orders[] mechanism. Apparently no order corresponds to stim.
bool Micro::Stim(BWAPI::Unit unit)
{
    if (!unit ||
        unit->getType() != BWAPI::UnitTypes::Terran_Marine && unit->getType() != BWAPI::UnitTypes::Terran_Firebat ||
        unit->getPlayer() != the.self())
    {
        UAB_ASSERT(false, "bad unit");
        return false;
    }

    if (unit->isStimmed())
    {
        return false;
    }

    // if we have issued a command to this unit already this frame, ignore this one
    if (unit->getLastCommandFrame() >= the.now())
    {
        return false;
    }

    // Allow a small latency for a previous stim command to take effect.
    // Marines and firebats have only 1 tech to use, so we don't need to check which.
    if (unit->getLastCommand().getType() == BWAPI::UnitCommandTypes::Use_Tech &&
        the.now() - unit->getLastCommandFrame() < 8)
    {
        return false;
    }

    // useTech() checks whether stim is researched and any other conditions.
    return unit->useTech(BWAPI::TechTypes::Stim_Packs);
}

// Merge the given high templar into an archon, or dark templar into a dark archon.
// Archon merger failures are handled in MicroHighTemplar.
bool Micro::MergeArchon(BWAPI::Unit templar1, BWAPI::Unit templar2)
{
    if (!templar1 || !templar2 ||
        templar1->getPlayer() != the.self() ||
        templar2->getPlayer() != the.self() ||
        templar1->getType() != BWAPI::UnitTypes::Protoss_High_Templar && templar1->getType() != BWAPI::UnitTypes::Protoss_Dark_Templar ||
        templar2->getType() != templar1->getType() ||
        templar1 == templar2)
    {
        UAB_ASSERT(false, "bad unit");
        return false;
    }

    // If we have issued a command already this frame, ignore this one.
    if (templar1->getLastCommandFrame() >= the.now() ||
        templar2->getLastCommandFrame() >= the.now())
    {
        return false;
    }

    BWAPI::Order order = BWAPI::Orders::ArchonWarp;
    BWAPI::TechType techType = BWAPI::TechTypes::Archon_Warp;
    if (templar1->getType() == BWAPI::UnitTypes::Protoss_Dark_Templar)
    {
        order = BWAPI::Orders::DarkArchonMeld;
        techType = BWAPI::TechTypes::Dark_Archon_Meld;
    }

    orders[templar1].setOrder(templar1, order, templar2);

    // useTech() checks any other conditions.
    return templar1->useTech(techType, templar2);
}

// Move these larvas to the left with the larva trick.
// NOTE The argument and other conditions are not checked.
bool Micro::LarvaTrick(const BWAPI::Unitset & larvas)
{
    return larvas.stop();
}

// Use a tech on a target unit.
// NOTE The order is set correctly only for techs that Steamhammer already implements.
bool Micro::UseTech(BWAPI::Unit unit, BWAPI::TechType tech, BWAPI::Unit target)
{
    if (!unit || !unit->exists() || !unit->getPosition().isValid() || unit->getPlayer() != the.self() ||
        !target || !target->exists() || !target->getPosition().isValid())
    {
        UAB_ASSERT(false, "bad unit");
        return false;
    }

    BWAPI::Order o;
    if (tech == BWAPI::TechTypes::Consume)
    {
        o = BWAPI::Orders::CastConsume;
    }
    else if (tech == BWAPI::TechTypes::Parasite)
    {
        o = BWAPI::Orders::CastParasite;
    }
    else if (tech == BWAPI::TechTypes::Spawn_Broodlings)
    {
        o = BWAPI::Orders::CastSpawnBroodlings;
    }
    else
    {
        o = BWAPI::Orders::UnusedNothing;
        UAB_ASSERT(false, "unsupported tech");
    }
    orders[unit].setOrder(unit, o);

    return unit->useTech(tech, target);
}

// Use a tech on a target position.
// NOTE The order is set correctly only for techs that Steamhammer already implements.
bool Micro::UseTech(BWAPI::Unit unit, BWAPI::TechType tech, const BWAPI::Position & target)
{
    if (!unit || !unit->exists() || !unit->getPosition().isValid() || unit->getPlayer() != the.self() ||
        !target.isValid())
    {
        UAB_ASSERT(false, "bad unit");
        return false;
    }

    BWAPI::Order o;
    if (tech == BWAPI::TechTypes::Ensnare)
    {
        o = BWAPI::Orders::CastEnsnare;
    }
    else if (tech == BWAPI::TechTypes::Dark_Swarm)
    {
        o = BWAPI::Orders::CastDarkSwarm;
    }
    else if (tech == BWAPI::TechTypes::Plague)
    {
        o = BWAPI::Orders::CastPlague;
    }
    else
    {
        o = BWAPI::Orders::UnusedNothing;
        UAB_ASSERT(false, "unsupported tech");
    }
    orders[unit].setOrder(unit, o);

    return unit->useTech(tech, target);
}

void Micro::KiteTarget(BWAPI::Unit rangedUnit, BWAPI::Unit target)
{
    // The always-kite units have their own micro.
    if (alwaysKite(rangedUnit->getType()))
    {
        MutaDanceTarget(rangedUnit, target);
        return;
    }

    if (!rangedUnit || !rangedUnit->exists() || rangedUnit->getPlayer() != the.self() ||
        !target || !target->exists())
    {
        UAB_ASSERT(false, "bad arg");
        return;
    }

    BWAPI::WeaponType weapon = UnitUtil::GetWeapon(rangedUnit, target);
    double range = rangedUnit->getPlayer()->weaponMaxRange(weapon);

    bool kite = true;

    // Only kite if somebody wants to shoot us.
    if (InformationManager::Instance().getEnemyFireteam(rangedUnit).empty())
    {
        kite = false;
    }

    // Kite if we're not ready yet: Wait for the weapon.
    double dist(rangedUnit->getDistance(target));
    double speed(rangedUnit->getPlayer()->topSpeed(rangedUnit->getType()));
    double timeToEnter = 0.0;                      // time to reach firing range
    if (speed > .00001)                            // don't even visit the same city as division by zero
    {
        timeToEnter = std::max(0.0, dist - range) / speed;
    }
    if (timeToEnter >= weapon.damageCooldown() + BWAPI::Broodwar->getRemainingLatencyFrames() ||
        target->getType().isBuilding())
    {
        kite = false;
    }

    if (kite)
    {
        // Run away if we can. If we can't, then kite becomes false.
        kite = kiteBack(rangedUnit, target);
    }
    
    if (!kite)
    {
        // Shoot.
        Micro::CatchAndAttackUnit(rangedUnit, target);
        //BWAPI::Broodwar->drawLineMap(rangedUnit->getPosition(), target->getPosition(), BWAPI::Colors::Orange);
    }
}

// Used for fast units with no delay in making turns--not necessarily mutalisks.
// See alwaysKite() for the set of units this is used for.
void Micro::MutaDanceTarget(BWAPI::Unit muta, BWAPI::Unit target)
{
    if (!muta || !muta->exists() || muta->getPlayer() != the.self() ||
        !target || !target->exists())
    {
        UAB_ASSERT(false, "bad arg");
        return;
    }

    const int latency					= BWAPI::Broodwar->getRemainingLatencyFrames();

    const int framesToEnterFiringRange	= UnitUtil::FramesToReachAttackRange(muta, target);
    const int framesToAttack			= framesToEnterFiringRange + 2 * latency;

    // How many frames are left before we should order the attack?
    const int cooldownNow =
        target->isFlying() ? muta->getAirWeaponCooldown() : muta->getGroundWeaponCooldown();
    const int staticCooldown =
        target->isFlying() ? muta->getType().airWeapon().damageCooldown() : muta->getType().groundWeapon().damageCooldown();
    const int cooldown =
        muta->isStartingAttack() ? staticCooldown : cooldownNow;

    if (cooldown <= framesToAttack)
    {
        // Attack.
        // This is functionally equivalent to Micro::CatchAndAttackUnit(muta, target) .

        if (!target->isMoving() || muta->isInWeaponRange(target))
        {
            //BWAPI::Broodwar->drawLineMap(muta->getPosition(), target->getPosition(), BWAPI::Colors::Orange);
            AttackUnit(muta, target);
        }
        else
        {
            BWAPI::Position destination = PredictMovement(target, std::min(framesToEnterFiringRange, 12));
            //BWAPI::Broodwar->drawLineMap(target->getPosition(), destination, BWAPI::Colors::Grey);
            //BWAPI::Broodwar->drawLineMap(muta->getPosition(), destination, BWAPI::Colors::Yellow);
            MoveNear(muta, destination);
        }
    }
    else
    {
        // Kite backward.
        fleeEnemy(muta, target, 64);
    }
}

void Micro::setAttack(const BWAPI::Unitset & units, bool a)
{
    for (BWAPI::Unit u : units)
    {
        orders[u].setAttack(a);
    }
}

bool Micro::getSmoothedAttack(const BWAPI::Unitset & units)
{
    if (units.size() == 0)
    {
        return false;
    }

    double total = 0.0;
    for (BWAPI::Unit u : units)
    {
        total += orders[u].getSmoothedAttack();
    }
    return total / units.size() >= 0.5;
}
