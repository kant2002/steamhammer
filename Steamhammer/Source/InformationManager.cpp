#include "InformationManager.h"

#include "The.h"

#include "Bases.h"
#include "MapTools.h"
#include "ProductionManager.h"
#include "Random.h"
#include "UnitUtil.h"

using namespace UAlbertaBot;

InformationManager::InformationManager()
    : _self(BWAPI::Broodwar->self())
    , _enemy(BWAPI::Broodwar->enemy())
    
    , _weHaveCombatUnits(false)
    , _enemyHasCombatUnits(false)
    , _enemyHasStaticAntiAir(false)
    , _enemyHasAntiAir(false)
    , _enemyHasAirTech(false)
    , _enemyHasCloakTech(false)
    , _enemyCloakedUnitsSeen(false)
    , _enemyHasMobileCloakTech(false)
    , _enemyHasOverlordHunters(false)
    , _enemyHasStaticDetection(false)
    , _enemyHasMobileDetection(_enemy->getRace() == BWAPI::Races::Zerg)
    , _enemyHasSiegeMode(false)
    , _enemyHasStorm(false)
    , _enemyGasTiming(0)
{
}

void InformationManager::initialize()
{
    initializeRegionInformation();
    initializeResources();
}

// Set up _occupiedLocations.
void InformationManager::initializeRegionInformation()
{
    updateOccupiedRegions(the.zone.ptr(the.bases.myStart()->getTilePosition()), _self);
}

// Initial mineral and gas values.
void InformationManager::initializeResources()
{
    for (BWAPI::Unit patch : BWAPI::Broodwar->getStaticMinerals())
    {
        _resources.insert(std::pair<BWAPI::Unit, ResourceInfo>(patch, ResourceInfo(patch)));
    }
    for (BWAPI::Unit geyser : BWAPI::Broodwar->getStaticGeysers())
    {
        _resources.insert(std::pair<BWAPI::Unit, ResourceInfo>(geyser, ResourceInfo(geyser)));
    }
}

void InformationManager::update()
{
    updateUnitInfo();
    updateGoneFromLastPosition();
    updateBaseLocationInfo();
    updateTheirTargets();
    updateBullets();
    updateResources();
    updateEnemyGasTiming();
    updateEnemyScans();
}

void InformationManager::updateUnitInfo() 
{
    _unitData[_enemy].removeBadUnits();
    _unitData[_self].removeBadUnits();

    for (BWAPI::Unit unit : _enemy->getUnits())
    {
        updateUnit(unit);
    }

    // Remove destroyed pylons from _ourPylons.
    for (auto pylonIt = _ourPylons.begin(); pylonIt != _ourPylons.end(); )
    {
        if (!(*pylonIt)->exists())
        {
            pylonIt = _ourPylons.erase(pylonIt);
        }
        else
        {
            ++pylonIt;
        }
    }

    bool anyNewPylons = false;

    for (BWAPI::Unit unit : _self->getUnits())
    {
        updateUnit(unit);

        // Add newly-completed pylons to _ourPylons, and notify BuildingManager.
        if (unit->getType() == BWAPI::UnitTypes::Protoss_Pylon &&
            unit->isCompleted() &&
            !_ourPylons.contains(unit))
        {
            _ourPylons.insert(unit);
            anyNewPylons = true;
        }
    }

    if (anyNewPylons)
    {
        BuildingManager::Instance().unstall();
    }
}

void InformationManager::updateBaseLocationInfo() 
{
    _occupiedRegions[_self].clear();
    _occupiedRegions[_enemy].clear();
    
    // The enemy occupies a region if it has a building there.
    for (const auto & kv : _unitData[_enemy].getUnits())
    {
        const UnitInfo & ui(kv.second);

        if (ui.type.isBuilding() && !ui.goneFromLastPosition && !ui.lifted)
        {
            updateOccupiedRegions(the.zone.ptr(BWAPI::TilePosition(ui.lastPosition)), _enemy);
        }
    }

    // We occupy a region if we have a building there.
    for (const BWAPI::Unit unit : _self->getUnits())
    {
        if (unit->getType().isBuilding() && unit->getPosition().isValid())
        {
            updateOccupiedRegions(the.zone.ptr(unit->getTilePosition()), _self);
        }
    }
}

void InformationManager::updateOccupiedRegions(const Zone * zone, BWAPI::Player player) 
{
    // if the region is valid (flying buildings may be in nullptr regions)
    if (zone)
    {
        // add it to the list of occupied regions
        _occupiedRegions[player].insert(zone);
    }
}

// If we can see the last known location of a remembered unit and the unit is not there,
// set the unit's goneFromLastPosition flag (unless it is burrowed or burrowing).
void InformationManager::updateGoneFromLastPosition()
{
    // We don't need to check every frame.
    // 1. The game supposedly only resets visible tiles when frame % 100 == 99.
    // 2. If the unit has only been gone from its location for a short time, it probably
    //    didn't go far (though it might have been recalled or gone through a nydus).
    // On the other hand, burrowed units can disappear from view more quickly.
    // 3. Detection is updated immediately, so we might overlook having detected
    //    a burrowed unit if we don't update often enough.
    // 4. We also might miss a unit in the process of burrowing.
    // All in all, we should check fairly often.
    if (the.now() % 6 == 5)
    {
        _unitData[_enemy].updateGoneFromLastPosition();
    }

    if (Config::Debug::DrawHiddenEnemies)
    {
        for (const auto & kv : _unitData[_enemy].getUnits())
        {
            const UnitInfo & ui(kv.second);

            // Units that are out of sight range.
            if (ui.unit && !ui.unit->isVisible())
            {
                if (ui.goneFromLastPosition)
                {
                    // Draw a small X.
                    BWAPI::Broodwar->drawLineMap(
                        ui.lastPosition + BWAPI::Position(-2, -2),
                        ui.lastPosition + BWAPI::Position(2, 2),
                        BWAPI::Colors::Red);
                    BWAPI::Broodwar->drawLineMap(
                        ui.lastPosition + BWAPI::Position(2, -2),
                        ui.lastPosition + BWAPI::Position(-2, 2),
                        BWAPI::Colors::Red);
                }
                else
                {
                    // Draw a small circle.
                    BWAPI::Color color = ui.burrowed ? BWAPI::Colors::Yellow : BWAPI::Colors::Green;
                    BWAPI::Broodwar->drawCircleMap(ui.lastPosition, 4, color);
                }
            }

            // Units that are in sight range but undetected.
            if (ui.unit && ui.unit->isVisible() && !ui.unit->isDetected())
            {
                // Draw a larger circle.
                BWAPI::Broodwar->drawCircleMap(ui.lastPosition, 8, BWAPI::Colors::Purple);

                BWAPI::Broodwar->drawTextMap(ui.lastPosition + BWAPI::Position(10, 6),
                    "%c%s", white, UnitTypeName(ui.type).c_str());
            }
        }
    }
}

// For each of our units, keep track of which enemies are targeting it.
// It changes frequently, so this is updated every frame.
void InformationManager::updateTheirTargets()
{
    _theirTargets.clear();

    // We only know the targets for visible enemy units.
    for (BWAPI::Unit enemy : _enemy->getUnits())
    {
        BWAPI::Unit target = enemy->getOrderTarget();
        if (target && target->getPlayer() == _self && (target->getType() == BWAPI::UnitTypes::Terran_Vulture_Spider_Mine || UnitUtil::AttackOrder(enemy)))
        {
            _theirTargets[target].insert(enemy);
            //BWAPI::Broodwar->drawLineMap(enemy->getPosition(), target->getPosition(), BWAPI::Colors::Yellow);
        }
    }
}

// Detect whether the enemy has cast psionic storm.
// NOTE It's easy because Steamhammer doesn't use storm itself.
// TODO Also an unfinished experiment to locate undetected lurkers by their spines.
void InformationManager::updateBullets()
{
    BWAPI::Bulletset bullets = BWAPI::Broodwar->getBullets();

    int x = 150;    // for lurker spine debugging
    int y = 30;

    for (BWAPI::Bullet bullet : bullets)
    {
        if (bullet->getType() == BWAPI::BulletTypes::Psionic_Storm)
        {
            // Latch: Once they have storm, they always have it.
            _enemyHasStorm = true;
        }
        // TODO disabled
        else if (false && bullet->getType() == BWAPI::BulletTypes::Subterranean_Spines)
        {
            char color = gray;
            if (bullet->getPlayer() == the.self())
            {
                color = green;
            }
            else if (bullet->getPlayer() == the.enemy())
            {
                color = orange;
            }

            BWAPI::Broodwar->drawTextScreen(x, y, "%c%d %s %d,%d -> %d,%d @ %g remaining %d",
                color,
                bullet->getID(),
                bullet->getType().getName().c_str(),
                bullet->getPosition().x, bullet->getPosition().y,
                bullet->getTargetPosition().x, bullet->getTargetPosition().y,
                bullet->getAngle(),
                bullet->getRemoveTimer());
            y += 10;
        }
    }
}

// Update any visible mineral patches or vespene geysers with their remaining amounts.
void InformationManager::updateResources()
{
    for (BWAPI::Unit patch : BWAPI::Broodwar->getStaticMinerals())
    {
        auto it = _resources.find(patch);
        UAB_ASSERT(it != _resources.end(), "bad static minerals");
        it->second.updateMinerals();
    }
    for (BWAPI::Unit geyser : BWAPI::Broodwar->getStaticGeysers())
    {
        auto it = _resources.find(geyser);
        UAB_ASSERT(it != _resources.end(), "bad static gas");
        it->second.updateGas();
    }
}

// Find the earliest sign of the enemy spending gas (not merely mining gas).
// For now, notice units that require gas. We don't try to detect enemy research.
// Zero means the enemy has not visibly used gas yet.
void InformationManager::updateEnemyGasTiming()
{
    if (_enemyGasTiming)
    {
        // Already know it.
        return;
    }

    for (const std::pair<BWAPI::UnitType, int> & unitCount : the.your.seen.getCounts())
    {
        BWAPI::UnitType t = unitCount.first;

        // A larva or egg says it costs 1 gas.
        // So does a spider mine, but it does cost gas to research.
        // I think other units tell the truth about their gas costs.
        // The vulture and shuttle are the only non-gas units that need gas to make (for the factory or robo),
        // so we don't need to look through all of the.your.inferred.getCounts().
        if (t.gasPrice() > 0 && t != BWAPI::UnitTypes::Zerg_Larva && t != BWAPI::UnitTypes::Zerg_Egg ||
            t == BWAPI::UnitTypes::Terran_Vulture ||
            t == BWAPI::UnitTypes::Protoss_Shuttle)
       {
            _enemyGasTiming = the.now();
            return;
        }
    }
}

// Keep track of active enemy scans every frame (if the enemy is terran).
void InformationManager::updateEnemyScans()
{
    if (the.enemyRace() == BWAPI::Races::Terran)
    {
        _enemyScans.clear();
        for (BWAPI::Unit u : the.enemy()->getUnits())
        {
            if (u->getType() == BWAPI::UnitTypes::Spell_Scanner_Sweep)
            {
                // NOTE getRemoveTimer() returns 0 for enemy scans.
                _enemyScans.insert(u);
            }
        }
    }
}

bool InformationManager::isEnemyBuildingInRegion(const Zone * zone) 
{
    // Invalid zones aren't considered the same.
    if (!zone)
    {
        return false;
    }

    for (const auto & kv : _unitData[_enemy].getUnits())
    {
        const UnitInfo & ui(kv.second);
        if (ui.type.isBuilding() && !ui.goneFromLastPosition && !ui.lifted)
        {
            if (zone->id() == the.zone.at(ui.lastPosition)) 
            {
                return true;
            }
        }
    }

    return false;
}

const UIMap & InformationManager::getUnitInfo(BWAPI::Player player) const
{
    return getUnitData(player).getUnits();
}

std::set<const Zone *> & InformationManager::getOccupiedRegions(BWAPI::Player player)
{
    return _occupiedRegions[player];
}

int InformationManager::getAir2GroundSupply(BWAPI::Player player) const
{
    int supply = 0;

    for (const auto & kv : getUnitData(player).getUnits())
    {
        const UnitInfo & ui(kv.second);

        if (ui.type.isFlyer() && UnitUtil::TypeCanAttackGround(ui.type))
        {
            supply += ui.type.supplyRequired();
        }
    }

    return supply;
}

void InformationManager::drawExtendedInterface()
{
    if (!Config::Debug::DrawUnitHealthBars)
    {
        return;
    }

    int verticalOffset = -10;

    // draw enemy units
    for (const auto & kv : getUnitData(_enemy).getUnits())
    {
        const UnitInfo & ui(kv.second);

        BWAPI::UnitType type(ui.type);
        int hitPoints = ui.lastHP;
        int shields = ui.lastShields;

        const BWAPI::Position & pos = ui.lastPosition;

        int left    = pos.x - type.dimensionLeft();
        int right   = pos.x + type.dimensionRight();
        int top     = pos.y - type.dimensionUp();
        int bottom  = pos.y + type.dimensionDown();

        if (!BWAPI::Broodwar->isVisible(BWAPI::TilePosition(ui.lastPosition)))
        {
            BWAPI::Broodwar->drawBoxMap(BWAPI::Position(left, top), BWAPI::Position(right, bottom), BWAPI::Colors::Grey, false);
            BWAPI::Broodwar->drawTextMap(BWAPI::Position(left + 3, top + 4), "%s %c",
                ui.type.getName().c_str(),
                ui.goneFromLastPosition ? 'X' : ' ');
        }
        
        if (!type.isResourceContainer() && type.maxHitPoints() > 0)
        {
            double hpRatio = (double)hitPoints / (double)type.maxHitPoints();
        
            BWAPI::Color hpColor = BWAPI::Colors::Green;
            if (hpRatio < 0.66) hpColor = BWAPI::Colors::Orange;
            if (hpRatio < 0.33) hpColor = BWAPI::Colors::Red;

            int ratioRight = left + (int)((right-left) * hpRatio);
            int hpTop = top + verticalOffset;
            int hpBottom = top + 4 + verticalOffset;

            BWAPI::Broodwar->drawBoxMap(BWAPI::Position(left, hpTop), BWAPI::Position(right, hpBottom), BWAPI::Colors::Grey, true);
            BWAPI::Broodwar->drawBoxMap(BWAPI::Position(left, hpTop), BWAPI::Position(ratioRight, hpBottom), hpColor, true);
            BWAPI::Broodwar->drawBoxMap(BWAPI::Position(left, hpTop), BWAPI::Position(right, hpBottom), BWAPI::Colors::Black, false);

            int ticWidth = 3;

            for (int i(left); i < right-1; i+=ticWidth)
            {
                BWAPI::Broodwar->drawLineMap(BWAPI::Position(i, hpTop), BWAPI::Position(i, hpBottom), BWAPI::Colors::Black);
            }
        }

        if (!type.isResourceContainer() && type.maxShields() > 0)
        {
            double shieldRatio = (double)shields / (double)type.maxShields();
        
            int ratioRight = left + (int)((right-left) * shieldRatio);
            int hpTop = top - 3 + verticalOffset;
            int hpBottom = top + 1 + verticalOffset;

            BWAPI::Broodwar->drawBoxMap(BWAPI::Position(left, hpTop), BWAPI::Position(right, hpBottom), BWAPI::Colors::Grey, true);
            BWAPI::Broodwar->drawBoxMap(BWAPI::Position(left, hpTop), BWAPI::Position(ratioRight, hpBottom), BWAPI::Colors::Blue, true);
            BWAPI::Broodwar->drawBoxMap(BWAPI::Position(left, hpTop), BWAPI::Position(right, hpBottom), BWAPI::Colors::Black, false);

            int ticWidth = 3;

            for (int i(left); i < right-1; i+=ticWidth)
            {
                BWAPI::Broodwar->drawLineMap(BWAPI::Position(i, hpTop), BWAPI::Position(i, hpBottom), BWAPI::Colors::Black);
            }
        }
    }

    // draw neutral units and our units
    for (BWAPI::Unit unit : BWAPI::Broodwar->getAllUnits())
    {
        if (unit->getPlayer() == _enemy)
        {
            continue;
        }

        const BWAPI::Position & pos = unit->getPosition();

        int left    = pos.x - unit->getType().dimensionLeft();
        int right   = pos.x + unit->getType().dimensionRight();
        int top     = pos.y - unit->getType().dimensionUp();
        int bottom  = pos.y + unit->getType().dimensionDown();

        //BWAPI::Broodwar->drawBoxMap(BWAPI::Position(left, top), BWAPI::Position(right, bottom), BWAPI::Colors::Grey, false);

        if (!unit->getType().isResourceContainer() && unit->getType().maxHitPoints() > 0)
        {
            double hpRatio = (double)unit->getHitPoints() / (double)unit->getType().maxHitPoints();
        
            BWAPI::Color hpColor = BWAPI::Colors::Green;
            if (hpRatio < 0.66) hpColor = BWAPI::Colors::Orange;
            if (hpRatio < 0.33) hpColor = BWAPI::Colors::Red;

            int ratioRight = left + (int)((right-left) * hpRatio);
            int hpTop = top + verticalOffset;
            int hpBottom = top + 4 + verticalOffset;

            BWAPI::Broodwar->drawBoxMap(BWAPI::Position(left, hpTop), BWAPI::Position(right, hpBottom), BWAPI::Colors::Grey, true);
            BWAPI::Broodwar->drawBoxMap(BWAPI::Position(left, hpTop), BWAPI::Position(ratioRight, hpBottom), hpColor, true);
            BWAPI::Broodwar->drawBoxMap(BWAPI::Position(left, hpTop), BWAPI::Position(right, hpBottom), BWAPI::Colors::Black, false);

            int ticWidth = 3;

            for (int i(left); i < right-1; i+=ticWidth)
            {
                BWAPI::Broodwar->drawLineMap(BWAPI::Position(i, hpTop), BWAPI::Position(i, hpBottom), BWAPI::Colors::Black);
            }
        }

        if (!unit->getType().isResourceContainer() && unit->getType().maxShields() > 0)
        {
            double shieldRatio = (double)unit->getShields() / (double)unit->getType().maxShields();
        
            int ratioRight = left + (int)((right-left) * shieldRatio);
            int hpTop = top - 3 + verticalOffset;
            int hpBottom = top + 1 + verticalOffset;

            BWAPI::Broodwar->drawBoxMap(BWAPI::Position(left, hpTop), BWAPI::Position(right, hpBottom), BWAPI::Colors::Grey, true);
            BWAPI::Broodwar->drawBoxMap(BWAPI::Position(left, hpTop), BWAPI::Position(ratioRight, hpBottom), BWAPI::Colors::Blue, true);
            BWAPI::Broodwar->drawBoxMap(BWAPI::Position(left, hpTop), BWAPI::Position(right, hpBottom), BWAPI::Colors::Black, false);

            int ticWidth = 3;

            for (int i(left); i < right-1; i+=ticWidth)
            {
                BWAPI::Broodwar->drawLineMap(BWAPI::Position(i, hpTop), BWAPI::Position(i, hpBottom), BWAPI::Colors::Black);
            }
        }

        if (unit->getType().isResourceContainer() && unit->getInitialResources() > 0)
        {
            
            double mineralRatio = (double)unit->getResources() / (double)unit->getInitialResources();
        
            int ratioRight = left + (int)((right-left) * mineralRatio);
            int hpTop = top + verticalOffset;
            int hpBottom = top + 4 + verticalOffset;

            BWAPI::Broodwar->drawBoxMap(BWAPI::Position(left, hpTop), BWAPI::Position(right, hpBottom), BWAPI::Colors::Grey, true);
            BWAPI::Broodwar->drawBoxMap(BWAPI::Position(left, hpTop), BWAPI::Position(ratioRight, hpBottom), BWAPI::Colors::Cyan, true);
            BWAPI::Broodwar->drawBoxMap(BWAPI::Position(left, hpTop), BWAPI::Position(right, hpBottom), BWAPI::Colors::Black, false);

            int ticWidth = 3;

            for (int i(left); i < right-1; i+=ticWidth)
            {
                BWAPI::Broodwar->drawLineMap(BWAPI::Position(i, hpTop), BWAPI::Position(i, hpBottom), BWAPI::Colors::Black);
            }
        }
    }
}

void InformationManager::drawUnitInformation(int x, int y) 
{
    if (!Config::Debug::DrawEnemyUnitInfo)
    {
        return;
    }

    BWAPI::Broodwar->drawTextScreen(x, y-10, "\x03 Self Loss:\x04 Minerals: \x1f%d \x04Gas: \x07%d", _unitData[_self].getMineralsLost(), _unitData[_self].getGasLost());
    BWAPI::Broodwar->drawTextScreen(x, y, "\x03 Enemy Loss:\x04 Minerals: \x1f%d \x04Gas: \x07%d", _unitData[_enemy].getMineralsLost(), _unitData[_enemy].getGasLost());
    BWAPI::Broodwar->drawTextScreen(x, y+20, "\x04 UNIT NAME");
    BWAPI::Broodwar->drawTextScreen(x+140, y+20, "\x04#");
    BWAPI::Broodwar->drawTextScreen(x+160, y+20, "\x04X");

    int yspace = 0;

    for (BWAPI::UnitType t : BWAPI::UnitTypes::allUnitTypes()) 
    {
        int numUnits = _unitData[_enemy].getNumUnits(t);
        int numDeadUnits = _unitData[_enemy].getNumDeadUnits(t);

        if (numUnits || numDeadUnits) 
        {
            char color = white;

            if (t.isDetector())			{ color = purple; }
            else if (t.canAttack())		{ color = red; }		
            else if (t.isBuilding())	{ color = yellow; }

            BWAPI::Broodwar->drawTextScreen(x, y+40+((yspace)*10), " %c%s", color, t.getName().c_str());
            BWAPI::Broodwar->drawTextScreen(x+140, y+40+((yspace)*10), "%c%d", color, numUnits);
            BWAPI::Broodwar->drawTextScreen(x+160, y+40+((yspace++)*10), "%c%d", color, numDeadUnits);
        }
    }

    for (const auto & kv : getUnitData(_enemy).getUnits())
    {
        const UnitInfo & ui(kv.second);

        if (ui.type.isBuilding())
        {
            char color = ui.completed ? cyan : orange;
            BWAPI::Broodwar->drawTextMap(ui.lastPosition.x, ui.lastPosition.y - 20, "%c%d", color, ui.completeBy);
        }
    }
}

void InformationManager::drawResourceAmounts() const
{
    if (!Config::Debug::DrawResourceAmounts)
    {
        return;
    }

    const BWAPI::Position offset(-20, -16);
    const BWAPI::Position nextLineOffset(-24, -6);

    for (const std::pair<BWAPI::Unit, ResourceInfo> & r : _resources)
    {
        BWAPI::Position xy = r.first->getInitialPosition();
        if (r.second.isAccessible())
        {
            BWAPI::Broodwar->drawTextMap(xy + offset, "%c%d", white, r.second.getAmount());
        }
        else
        {
            char color = r.second.isMineralPatch() ? (r.second.isDestroyed() ? darkRed : cyan) : green;
            BWAPI::Broodwar->drawTextMap(xy + offset, "%c%d", color, r.second.getAmount());
            BWAPI::Broodwar->drawTextMap(xy + nextLineOffset, "%c@ %d", color, r.second.getFrame());
        }
    }
}

void InformationManager::maybeClearNeutral(BWAPI::Unit unit)
{
    if (unit && unit->getPlayer() == the.neutral() && unit->getType().isBuilding())
    {
        the.bases.clearNeutral(unit);
    }
}

// NOTE This includes both completed and uncompleted static defense buildings.
//      Also shield batteries.
void InformationManager::maybeAddStaticDefense(BWAPI::Unit unit)
{
    if (unit && unit->getPlayer() == _self && UnitUtil::IsStaticDefense(unit->getType()))
    {
        _staticDefense.insert(unit);
    }
}

void InformationManager::updateUnit(BWAPI::Unit unit)
{
    if (unit->getPlayer() == _self || unit->getPlayer() == _enemy)
    {
        _unitData[unit->getPlayer()].updateUnit(unit);
    }
}

void InformationManager::onUnitDestroy(BWAPI::Unit unit) 
{ 
    if (unit->getPlayer() == _self || unit->getPlayer() == _enemy)
    {
        _unitData[unit->getPlayer()].removeUnit(unit);

        // If it is our static defense, remove it.
        if (unit->getPlayer() == _self && UnitUtil::IsStaticDefense(unit->getType()))
        {
            _staticDefense.erase(unit);
        }
    }
    else
    {
        // Should be neutral.
        the.bases.clearNeutral(unit);
    }
}

// Only returns units expected to be completed.
void InformationManager::getNearbyForce(std::vector<UnitInfo> & unitsOut, BWAPI::Position p, BWAPI::Player player, int radius) 
{
    for (const auto & kv : getUnitData(player).getUnits())
    {
        const UnitInfo & ui(kv.second);

        if (UnitUtil::IsCombatSimUnit(ui) &&
            !ui.goneFromLastPosition &&
            ui.isCompleted() &&
            ui.powered)
        {
            if (ui.type == BWAPI::UnitTypes::Terran_Medic)
            {
                // Spellcasters that the combat simulator is able to simulate.
                if (ui.lastPosition.getDistance(p) <= radius + 64)
                {
                    unitsOut.push_back(ui);
                }
            }
            else
            {
                // Non-spellcasters, aka units with weapons that have a range.

                // Determine its attack range, in the worst case.
                int range = UnitUtil::GetMaxAttackRange(ui.type);

                // Include it if it can attack into the radius we care about (with fudge factor).
                if (range && ui.lastPosition.getDistance(p) <= radius + range + 32)
                {
                    unitsOut.push_back(ui);
                }
            }
            // NOTE FAP does not support detectors.
            // else if (ui.type.isDetector() && ui.lastPosition.getDistance(p) <= (radius + 250))
            // {
            //	unitsOut.push_back(ui);
            // }
        }
    }
}

int InformationManager::getNumUnits(BWAPI::UnitType t, BWAPI::Player player) const
{
    int count = 0;

    for (const auto & kv : getUnitData(player).getUnits())
    {
        const UnitInfo & ui(kv.second);

        if (t == ui.type)
        {
            ++count;
        }
    }

    return count;

    // Buggy original! The original method can be extremely wrong, even giving negative counts.
    // return getUnitData(player).getNumUnits(t);
}

// We have completed combat units (excluding workers).
// This is a latch, initially false and set true forever when we get our first combat units.
bool InformationManager::weHaveCombatUnits()
{
    // Latch: Once we have combat units, pretend we always have them.
    if (_weHaveCombatUnits)
    {
        return true;
    }

    for (BWAPI::Unit u : _self->getUnits())
    {
        if (!u->getType().isWorker() &&
            !u->getType().isBuilding() &&
            u->isCompleted() &&
            !u->getType().isSpell() &&
            u->getType() != BWAPI::UnitTypes::Zerg_Larva &&
            u->getType() != BWAPI::UnitTypes::Zerg_Overlord)
        {
            _weHaveCombatUnits = true;
            return true;
        }
    }

    return false;
}

// Enemy has completed combat units (excluding workers).
bool InformationManager::enemyHasCombatUnits()
{
    // Latch: Once they're known to have the tech, they always have it.
    if (_enemyHasCombatUnits)
    {
        return true;
    }

    for (const auto & kv : getUnitData(_enemy).getUnits())
    {
        const UnitInfo & ui(kv.second);

        if (!ui.type.isWorker() &&
            !ui.type.isBuilding() &&
            ui.isCompleted() &&
            ui.type != BWAPI::UnitTypes::Zerg_Larva &&
            ui.type != BWAPI::UnitTypes::Zerg_Overlord)
        {
            _enemyHasCombatUnits = true;
            return true;
        }
    }

    return false;
}

// Enemy has spore colonies, photon cannons, or turrets.
bool InformationManager::enemyHasStaticAntiAir()
{
    // Latch: Once they're known to have the tech, they always have it.
    if (_enemyHasStaticAntiAir)
    {
        return true;
    }

    for (const auto & kv : getUnitData(_enemy).getUnits())
    {
        const UnitInfo & ui(kv.second);

        if (ui.type == BWAPI::UnitTypes::Terran_Missile_Turret ||
            ui.type == BWAPI::UnitTypes::Protoss_Photon_Cannon ||
            ui.type == BWAPI::UnitTypes::Zerg_Spore_Colony)
        {
            _enemyHasStaticAntiAir = true;
            return true;
        }
    }

    return false;
}

// Enemy has mobile units that can shoot up, or the tech to produce them.
bool InformationManager::enemyHasAntiAir()
{
    // Latch: Once they're known to have the tech, they always have it.
    if (_enemyHasAntiAir)
    {
        return true;
    }

    for (const auto & kv : getUnitData(_enemy).getUnits())
    {
        const UnitInfo & ui(kv.second);

        if (
            // For terran, anything other than SCV, command center, depot is a hit.
            // Surely nobody makes ebay before barracks!
            (_enemy->getRace() == BWAPI::Races::Terran &&
            ui.type != BWAPI::UnitTypes::Terran_SCV &&
            ui.type != BWAPI::UnitTypes::Terran_Command_Center &&
            ui.type != BWAPI::UnitTypes::Terran_Supply_Depot)

            ||

            // Otherwise, any mobile unit that has an air weapon.
            (!ui.type.isBuilding() && UnitUtil::TypeCanAttackAir(ui.type))

            ||

            // Or a building for making such a unit.
            // The cyber core only counts once it is finished, other buildings earlier.
            ui.type == BWAPI::UnitTypes::Protoss_Cybernetics_Core && ui.isCompleted() ||
            ui.type == BWAPI::UnitTypes::Protoss_Stargate ||
            ui.type == BWAPI::UnitTypes::Protoss_Fleet_Beacon ||
            ui.type == BWAPI::UnitTypes::Protoss_Arbiter_Tribunal ||
            ui.type == BWAPI::UnitTypes::Zerg_Hydralisk_Den ||
            ui.type == BWAPI::UnitTypes::Zerg_Spire ||
            ui.type == BWAPI::UnitTypes::Zerg_Greater_Spire

            )
        {
            _enemyHasAntiAir = true;
            return true;
        }
    }

    return false;
}

// Enemy has air units or air-producing tech.
// Overlords and lifted buildings are excluded.
// A queen's nest is not air tech--it's usually a prerequisite for hive
// rather than to make queens. So we have to see a queen for it to count.
// Protoss robo fac and terran starport are taken to imply air units.
bool InformationManager::enemyHasAirTech()
{
    // Latch: Once they're known to have the tech, they always have it.
    if (_enemyHasAirTech)
    {
        return true;
    }

    for (const auto & kv : getUnitData(_enemy).getUnits())
    {
        const UnitInfo & ui(kv.second);

        if (ui.type.isFlyer() && ui.type != BWAPI::UnitTypes::Zerg_Overlord && !ui.type.isSpell() ||
            ui.type == BWAPI::UnitTypes::Terran_Starport ||
            ui.type == BWAPI::UnitTypes::Terran_Control_Tower ||
            ui.type == BWAPI::UnitTypes::Terran_Science_Facility ||
            ui.type == BWAPI::UnitTypes::Terran_Covert_Ops ||
            ui.type == BWAPI::UnitTypes::Terran_Physics_Lab ||
            ui.type == BWAPI::UnitTypes::Protoss_Stargate ||
            ui.type == BWAPI::UnitTypes::Protoss_Arbiter_Tribunal ||
            ui.type == BWAPI::UnitTypes::Protoss_Fleet_Beacon ||
            ui.type == BWAPI::UnitTypes::Protoss_Robotics_Facility ||
            ui.type == BWAPI::UnitTypes::Protoss_Robotics_Support_Bay ||
            ui.type == BWAPI::UnitTypes::Protoss_Observatory ||
            ui.type == BWAPI::UnitTypes::Zerg_Spire ||
            ui.type == BWAPI::UnitTypes::Zerg_Greater_Spire)
        {
            _enemyHasAirTech = true;
            return true;
        }
    }

    return false;
}

// This test is good for "can I benefit from detection?"
// NOTE The enemySeenBurrowing() call also sets _enemyHasCloakTech .
bool InformationManager::enemyHasCloakTech()
{
    // Latch: Once they're known to have the tech, they always have it.
    if (_enemyHasCloakTech)
    {
        return true;
    }

    for (const auto & kv : getUnitData(_enemy).getUnits())
    {
        const UnitInfo & ui(kv.second);

        if (ui.type.hasPermanentCloak() ||                             // DT, observer
            ui.type.isCloakable() ||                                   // wraith, ghost
            ui.type == BWAPI::UnitTypes::Terran_Vulture_Spider_Mine ||
            ui.type == BWAPI::UnitTypes::Protoss_Citadel_of_Adun ||    // assume DT
            ui.type == BWAPI::UnitTypes::Protoss_Templar_Archives ||   // assume DT
            ui.type == BWAPI::UnitTypes::Protoss_Observatory ||
            ui.type == BWAPI::UnitTypes::Protoss_Arbiter_Tribunal ||
            ui.type == BWAPI::UnitTypes::Protoss_Arbiter ||
            ui.type == BWAPI::UnitTypes::Zerg_Lurker ||
            ui.type == BWAPI::UnitTypes::Zerg_Lurker_Egg ||
            ui.unit->isBurrowed())
        {
            _enemyHasCloakTech = true;
            return true;
        }
    }

    return false;
}

// This test means more "can I be SURE that I will benefit from detection?"
// It only counts actual cloaked units, not merely the tech for them,
// and does not worry about observers.
// NOTE The enemySeenBurrowing() call also sets _enemyCloakedUnitsSeen.
// NOTE If they have cloaked units, they have cloak tech. Set all appropriate flags.
bool InformationManager::enemyCloakedUnitsSeen()
{
    // Latch: Once they're known to have the tech, they always have it.
    if (_enemyCloakedUnitsSeen)
    {
        return true;
    }

    for (const auto & kv : getUnitData(_enemy).getUnits())
    {
        const UnitInfo & ui(kv.second);

        if (ui.type.isCloakable() ||                                    // wraith, ghost
            ui.type == BWAPI::UnitTypes::Terran_Vulture_Spider_Mine ||
            ui.type == BWAPI::UnitTypes::Protoss_Dark_Templar ||
            ui.type == BWAPI::UnitTypes::Protoss_Arbiter ||
            ui.type == BWAPI::UnitTypes::Zerg_Lurker ||
            ui.type == BWAPI::UnitTypes::Zerg_Lurker_Egg ||
            ui.unit->isBurrowed())
        {
            _enemyHasCloakTech = true;
            _enemyCloakedUnitsSeen = true;
            _enemyHasMobileCloakTech = true;
            return true;
        }
    }

    return false;
}

// This test is better for "do I need detection to live?"
// It doesn't worry about spider mines, observers, or burrowed units except lurkers.
// NOTE If they have cloaked units, they have cloak tech. Set all appropriate flags.
bool InformationManager::enemyHasMobileCloakTech()
{
    // Latch: Once they're known to have the tech, they always have it.
    if (_enemyHasMobileCloakTech)
    {
        return true;
    }

    for (const auto & kv : getUnitData(_enemy).getUnits())
    {
        const UnitInfo & ui(kv.second);

        if (ui.type.isCloakable() ||                                   // wraith, ghost
            ui.type == BWAPI::UnitTypes::Protoss_Dark_Templar ||
            ui.type == BWAPI::UnitTypes::Protoss_Citadel_of_Adun ||    // assume DT
            ui.type == BWAPI::UnitTypes::Protoss_Templar_Archives ||   // assume DT
            ui.type == BWAPI::UnitTypes::Protoss_Arbiter_Tribunal ||
            ui.type == BWAPI::UnitTypes::Protoss_Arbiter ||
            ui.type == BWAPI::UnitTypes::Zerg_Lurker ||
            ui.type == BWAPI::UnitTypes::Zerg_Lurker_Egg)
        {
            _enemyHasCloakTech = true;
            _enemyHasMobileCloakTech = true;
            return true;
        }
    }

    return false;
}

// Enemy has cloaked wraiths or arbiters.
bool InformationManager::enemyHasAirCloakTech()
{
    for (const auto & kv : getUnitData(_enemy).getUnits())
    {
        const UnitInfo & ui(kv.second);

        // We have to see a wraith that is cloaked to be sure.
        if (ui.type == BWAPI::UnitTypes::Terran_Wraith &&
            ui.unit->isVisible() && !ui.unit->isDetected())
        {
            _enemyHasCloakTech = true;
            _enemyHasMobileCloakTech = true;
            _enemyHasAirCloakTech = true;
            return true;
        }

        if (ui.type == BWAPI::UnitTypes::Protoss_Arbiter_Tribunal ||
            ui.type == BWAPI::UnitTypes::Protoss_Arbiter)
        {
            _enemyHasCloakTech = true;
            _enemyHasMobileCloakTech = true;
            _enemyHasAirCloakTech = true;
            return true;
        }
    }

    return false;
}

// Enemy has air units good for hunting down overlords.
// A stargate counts, but not a fleet beacon or arbiter tribunal.
// A starport does not count; it may well be for something else.
bool InformationManager::enemyHasOverlordHunters()
{
    // Latch: Once they're known to have the tech, they always have it.
    if (_enemyHasOverlordHunters)
    {
        return true;
    }

    for (const auto & kv : getUnitData(_enemy).getUnits())
    {
        const UnitInfo & ui(kv.second);

        if (ui.type == BWAPI::UnitTypes::Terran_Wraith ||
            ui.type == BWAPI::UnitTypes::Terran_Valkyrie ||
            ui.type == BWAPI::UnitTypes::Terran_Battlecruiser ||
            ui.type == BWAPI::UnitTypes::Protoss_Corsair ||
            ui.type == BWAPI::UnitTypes::Protoss_Scout ||
            ui.type == BWAPI::UnitTypes::Protoss_Carrier ||
            ui.type == BWAPI::UnitTypes::Protoss_Stargate ||
            ui.type == BWAPI::UnitTypes::Zerg_Spire ||
            ui.type == BWAPI::UnitTypes::Zerg_Greater_Spire ||
            ui.type == BWAPI::UnitTypes::Zerg_Mutalisk ||
            ui.type == BWAPI::UnitTypes::Zerg_Scourge)
        {
            _enemyHasOverlordHunters = true;
            _enemyHasAirTech = true;
            return true;
        }
    }

    return false;
}

void InformationManager::enemySeenBurrowing()
{
    _enemyHasCloakTech = true;
    _enemyCloakedUnitsSeen = true;
}

// Look up when an enemy building finished, or is predicted to finish.
// If none, give a time far in the future.
// This is for checking the timing of enemy tech buildings.
int InformationManager::getEnemyBuildingTiming(BWAPI::UnitType type) const
{
    for (const auto & kv : getUnitData(_enemy).getUnits())
    {
        const UnitInfo & ui(kv.second);

        if (ui.type == type)
        {
            return ui.completeBy;
        }
    }

    // "Infinite" time in the future.
    return MAX_FRAME;
}

// Return the remaining build time for one of our buildings, given only its type.
// This is for checking the timing of our tech buildings.
// If already completed, return 0. If not under construction, return "never".
int InformationManager::remainingBuildTime(BWAPI::UnitType type) const
{
    for (BWAPI::Unit unit : the.self()->getUnits())
    {
        if (unit->getType() == type)
        {
            return unit->getRemainingBuildTime();
        }
    }

    return MAX_FRAME;
}

// If we are making more than one spire, return the earliest.
int InformationManager::getMySpireTiming() const
{
    if (the.my.completed.count(BWAPI::UnitTypes::Zerg_Spire) > 0 ||
        the.my.all.count(BWAPI::UnitTypes::Zerg_Greater_Spire) > 0)
    {
        // The spire is complete, return an early time.
        return 1;
    }

    int frame = MAX_FRAME;
    if (the.my.all.count(BWAPI::UnitTypes::Zerg_Spire) > 0)
    {
        for (BWAPI::Unit unit : _self->getUnits())
        {
            if (unit->getType() == BWAPI::UnitTypes::Zerg_Spire)
            {
                int f = the.now() + unit->getRemainingBuildTime();
                if (f < frame)
                {
                    frame = f;
                }
            }
        }
    }

    return frame;
}

// Enemy has spore colonies, photon cannons, turrets, or spider mines.
// It's the same as enemyHasStaticAntiAir() except for spider mines.
// Spider mines only catch cloaked ground units, so this routine is not for countering wraiths.
bool InformationManager::enemyHasStaticDetection()
{
    // Latch: Once they're known to have the tech, they always have it.
    if (_enemyHasStaticDetection)
    {
        return true;
    }

    if (enemyHasStaticAntiAir())
    {
        _enemyHasStaticDetection = true;
        return true;
    }

    for (const auto & kv : getUnitData(_enemy).getUnits())
    {
        const UnitInfo & ui(kv.second);

        if (ui.type == BWAPI::UnitTypes::Terran_Vulture_Spider_Mine)
        {
            _enemyHasStaticDetection = true;
            return true;
        }
    }

    return false;
}

// Enemy has overlords, observers, comsat, or science vessels.
bool InformationManager::enemyHasMobileDetection()
{
    // Latch: Once they're known to have the tech, they always have it.
    if (_enemyHasMobileDetection)
    {
        return true;
    }

    // If the enemy is zerg, they have overlords.
    // If they went random, we may not have known until now.
    if (_enemy->getRace() == BWAPI::Races::Zerg)
    {
        _enemyHasMobileDetection = true;
        return true;
    }

    for (const auto & kv : getUnitData(_enemy).getUnits())
    {
        const UnitInfo & ui(kv.second);

        if (ui.type == BWAPI::UnitTypes::Terran_Comsat_Station ||
            ui.type == BWAPI::UnitTypes::Spell_Scanner_Sweep ||
            ui.type == BWAPI::UnitTypes::Terran_Science_Facility ||
            ui.type == BWAPI::UnitTypes::Terran_Science_Vessel ||
            ui.type == BWAPI::UnitTypes::Protoss_Observatory ||
            ui.type == BWAPI::UnitTypes::Protoss_Observer)
        {
            _enemyHasMobileDetection = true;
            return true;
        }
    }

    return false;
}

bool InformationManager::enemyHasSiegeMode()
{
    // Only terran can get siege mode. Ignore the possibility of protoss mind control.
    if (_enemy->getRace() != BWAPI::Races::Terran)
    {
        return false;
    }

    // Latch: Once they're known to have the tech, they always have it.
    if (_enemyHasSiegeMode)
    {
        return true;
    }

    for (const auto & kv : getUnitData(_enemy).getUnits())
    {
        const UnitInfo & ui(kv.second);

        // If the tank is in the process of sieging, it is still in tank mode.
        // If it is unsieging, it is still in siege mode. So this condition catches everything.
        if (ui.type == BWAPI::UnitTypes::Terran_Siege_Tank_Siege_Mode ||
            ui.unit->isVisible() && ui.unit->getOrder() == BWAPI::Orders::Sieging)
        {
            _enemyHasSiegeMode = true;
            return true;
        }
    }

    return false;
}

// The enemy may use drop.
// NOTE We ignore zerg drop tech, which is rare and harder to detect.
bool InformationManager::enemyHasTransport() const
{
    return
        the.your.ever.count(BWAPI::UnitTypes::Terran_Dropship) > 0 ||
        the.your.ever.count(BWAPI::UnitTypes::Protoss_Shuttle) > 0;
}

bool InformationManager::weHaveCloakTech() const
{
    return
        BWAPI::Broodwar->self()->hasResearched(BWAPI::TechTypes::Cloaking_Field) ||
        BWAPI::Broodwar->self()->hasResearched(BWAPI::TechTypes::Personnel_Cloaking) ||
        the.my.completed.count(BWAPI::UnitTypes::Protoss_Dark_Templar) > 0 ||
        the.my.completed.count(BWAPI::UnitTypes::Protoss_Arbiter) > 0 ||
        BWAPI::Broodwar->self()->hasResearched(BWAPI::TechTypes::Burrowing) ||
        BWAPI::Broodwar->self()->hasResearched(BWAPI::TechTypes::Lurker_Aspect);
}

// Our nearest static defense building that can hit ground, by air distance.
// Null if none.
// NOTE This assumes that we never put medics or SCVs into a bunker.
BWAPI::Unit InformationManager::nearestGroundStaticDefense(BWAPI::Position pos) const
{
    int closestDist = MAX_DISTANCE;
    BWAPI::Unit closest = nullptr;
    for (BWAPI::Unit building : _staticDefense)
    {
        if (building->getType() == BWAPI::UnitTypes::Terran_Bunker && !building->getLoadedUnits().empty() ||
            building->getType() == BWAPI::UnitTypes::Protoss_Photon_Cannon ||
            building->getType() == BWAPI::UnitTypes::Zerg_Sunken_Colony)
        {
            int dist = building->getDistance(pos);
            if (dist < closestDist)
            {
                closestDist = dist;
                closest = building;
            }
        }
    }
    return closest;
}

// Our nearest static defense building that can hit air, by air distance.
// Null if none.
// NOTE This assumes that we only put marines into a bunker: If it is loaded, it can shoot air.
// If we ever put firebats or SCVs or medics into a bunker, we'll have to do a fancier check.
BWAPI::Unit InformationManager::nearestAirStaticDefense(BWAPI::Position pos) const
{
    int closestDist = MAX_DISTANCE;
    BWAPI::Unit closest = nullptr;
    for (BWAPI::Unit building : _staticDefense)
    {
        if (building->getType() == BWAPI::UnitTypes::Terran_Missile_Turret ||
            building->getType() == BWAPI::UnitTypes::Terran_Bunker && !building->getLoadedUnits().empty() ||
            building->getType() == BWAPI::UnitTypes::Protoss_Photon_Cannon || 
            building->getType() == BWAPI::UnitTypes::Zerg_Spore_Colony)
        {
            int dist = building->getDistance(pos);
            if (dist < closestDist)
            {
                closestDist = dist;
                closest = building;
            }
        }
    }
    return closest;
}

// Our nearest shield battery, by air distance.
// Null if none.
BWAPI::Unit InformationManager::nearestShieldBattery(BWAPI::Position pos) const
{
    if (_self->getRace() == BWAPI::Races::Protoss)
    {
        int closestDist = MAX_DISTANCE;
        BWAPI::Unit closest = nullptr;
        for (BWAPI::Unit building : _staticDefense)
        {
            if (building->getType() == BWAPI::UnitTypes::Protoss_Shield_Battery)
            {
                int dist = building->getDistance(pos);
                if (dist < closestDist)
                {
                    closestDist = dist;
                    closest = building;
                }
            }
        }
        return closest;
    }
    return nullptr;
}

// Zerg specific calculation: How many scourge hits are needed
// to kill the enemy's known air fleet?
// This counts individual units--you get 2 scourge per egg.
// One scourge does 110 normal damage.
// NOTE This ignores air armor, which might make a difference in rare cases.
int InformationManager::nScourgeNeeded()
{
    int count = 0;

    for (const auto & kv : getUnitData(_enemy).getUnits())
    {
        const UnitInfo & ui(kv.second);

        // A few unit types should not usually be scourged. Skip them.
        if (ui.type.isFlyer() &&
            ui.type != BWAPI::UnitTypes::Spell_Scanner_Sweep &&
            ui.type != BWAPI::UnitTypes::Zerg_Overlord &&
            ui.type != BWAPI::UnitTypes::Zerg_Scourge &&
            ui.type != BWAPI::UnitTypes::Protoss_Interceptor)
        {
            int hp = ui.type.maxHitPoints() + ui.type.maxShields();      // assume the worst
            count += (hp + 109) / 110;
        }
    }

    return count;
}

const UnitData & InformationManager::getUnitData(BWAPI::Player player) const
{
    return _unitData.find(player)->second;
}

// Enemy units only. Return null if the enemy unit is not found.
const UnitInfo * InformationManager::getUnitInfo(BWAPI::Unit unit) const
{
    const auto & enemies = getUnitData(_enemy).getUnits();
    const auto it = enemies.find(unit);
    if (it != enemies.end())
    {
        return & (*it).second;
    }
    return nullptr;
}

// Return the set of enemy units targeting a given one of our units.
const BWAPI::Unitset & InformationManager::getEnemyFireteam(BWAPI::Unit ourUnit) const
{
    auto it = _theirTargets.find(ourUnit);
    if (it != _theirTargets.end())
    {
        return (*it).second;
    }
    return EmptyUnitSet;
}

// Return the last seen resource amount of a mineral patch or vespene geyser.
// NOTE Pass in the static unit of the resource container, or it won't work.
int InformationManager::getResourceAmount(BWAPI::Unit resource) const
{
    auto it = _resources.find(resource);
    if (it == _resources.end())
    {
        return 0;
    }
    return it->second.getAmount();
}

// Return whether a mineral patch has been destroyed by being mined out.
// NOTE Pass in the static unit of the resource container, or it won't work.
bool InformationManager::isMineralDestroyed(BWAPI::Unit resource) const
{
    auto it = _resources.find(resource);
    if (it == _resources.end())
    {
        return false;
    }
    return it->second.isDestroyed();
}

// Is this geyser, possibly out of sight, covered with a refinery (at last report)?
// NOTE Pass in a static initial geyser unit, or else getInitialPosition() will not work.
bool InformationManager::isGeyserTaken(BWAPI::Unit resource) const
{
    if (resource->isVisible())
    {
        // Our own refineries are always visible.
        return resource->getType().isRefinery();
    }

    // The geyser is either neutral or enemy, but in any case out of sight.
    // Check enemy units to see if it is known to be owned by the enemy.
    for (const auto & kv : getUnitData(_enemy).getUnits())
    {
        const UnitInfo & ui(kv.second);

        // If an enemy unit is at the same position, it can only be a refinery.
        if (ui.lastPosition == resource->getInitialPosition())
        {
            return true;
        }
    }

    return false;
}

InformationManager & InformationManager::Instance()
{
    static InformationManager instance;
    return instance;
}
