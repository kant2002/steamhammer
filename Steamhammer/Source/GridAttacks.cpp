#include "GridAttacks.h"

#include "InformationManager.h"
#include "UnitUtil.h"

using namespace UAlbertaBot;

GroundAttacks::GroundAttacks()
    : GridAttacks(false)
{
}

AirAttacks::AirAttacks()
    : GridAttacks(true)
{
}

// Count 1 for each tile in range of the given enemy position.
void GridAttacks::addTilesInRange(const BWAPI::Position & enemyPosition, int range)
{
    // Find a bounding box that all affected tiles fit within.
    BWAPI::Position topLeft(enemyPosition.x - range - 1, enemyPosition.y - range - 1);
    BWAPI::Position bottomRight(enemyPosition.x + range + 1, enemyPosition.y + range + 1);
    BWAPI::TilePosition topLeftTile(topLeft);
    BWAPI::TilePosition bottomRightTile(bottomRight);

    // Find the tiles inside the bounding box which are in range.
    // Be conservative: If the corner nearest the enemy is in range, the tile is in range.
    // The 32 is for converting from tiles to pixels.
    for (int x = std::max(0, topLeftTile.x); x <= std::min(width-1, bottomRightTile.x); ++x)
    {
        int nearestX = 32 * ((32 * x + 31 < enemyPosition.x) ? x + 1 : x);
        for (int y = std::max(0, topLeftTile.y); y <= std::min(height-1, bottomRightTile.y); ++y)
        {
            int nearestY = 32 * ((32 * y + 31 <= enemyPosition.y) ? y + 1 : y);
            if (BWAPI::Position(nearestX, nearestY).getApproxDistance(enemyPosition) <= range)
            {
                grid[x][y] += 1;
            }
        }
    }
}

void GridAttacks::computeAir(const std::map<BWAPI::Unit, UnitInfo> & unitsInfo)
{
    for (const auto & kv : unitsInfo)
    {
        const auto & ui = kv.second;

        if (ui.type.isBuilding() &&
            !ui.goneFromLastPosition &&
            UnitUtil::TypeCanAttackAir(ui.type) &&
            ui.isCompleted() &&
            ui.powered)
        {
            int airRange = UnitUtil::GetAttackRangeAssumingUpgrades(ui.type, BWAPI::UnitTypes::Terran_Wraith);
            addTilesInRange(ui.lastPosition, airRange);
        }
    }
}

// Static defense plus sieged tanks and burrowed lurkers.
void GridAttacks::computeGround(const std::map<BWAPI::Unit, UnitInfo> & unitsInfo)
{
    for (const auto & kv : unitsInfo)
    {
        const auto & ui = kv.second;

        if ((ui.type.isBuilding() || ui.type == BWAPI::UnitTypes::Terran_Siege_Tank_Siege_Mode || ui.type == BWAPI::UnitTypes::Zerg_Lurker && ui.burrowed) &&
            !ui.goneFromLastPosition &&
            UnitUtil::TypeCanAttackGround(ui.type) &&
            ui.isCompleted() &&
            ui.powered)
        {
            int groundRange = UnitUtil::GetAttackRangeAssumingUpgrades(ui.type, BWAPI::UnitTypes::Terran_Marine);
            addTilesInRange(ui.lastPosition, groundRange);
        }
    }
}

GridAttacks::GridAttacks(bool air)
    : Grid(BWAPI::Broodwar->mapWidth(), BWAPI::Broodwar->mapHeight(), 0)
    , versusAir(air)
{
}

// Initialize with attacks by the enemy, against either air or ground units.
void GridAttacks::update()
{
    // Zero out the grid.
    for (int x = 0; x < width; ++x)
    {
        std::fill(grid[x].begin(), grid[x].end(), 0);
    }

    // Fill in the grid.
    const std::map<BWAPI::Unit, UnitInfo> & unitsInfo =
        InformationManager::Instance().getUnitData(BWAPI::Broodwar->enemy()).getUnits();
    if (versusAir)
    {
        computeAir(unitsInfo);
    }
    else
    {
        computeGround(unitsInfo);
    }
}

bool GridAttacks::inRange(const BWAPI::TilePosition & pos) const
{
    return pos.isValid() && grid[pos.x][pos.y];
}

bool GridAttacks::inRange(const BWAPI::TilePosition & topLeft, const BWAPI::TilePosition & bottomRight) const
{
    UAB_ASSERT(topLeft.isValid() && bottomRight.isValid(), "bad rectangle");

    if (grid[topLeft.x][topLeft.y])
    {
        return true;
    }

    // If the rectangle covers more than one tile, check each corner.
    if (topLeft != bottomRight)
    {
        if (grid[bottomRight.x][bottomRight.y] ||
            grid[topLeft.x][bottomRight.y] ||
            grid[bottomRight.x][topLeft.y])
        {
            return true;
        }
    }
    return false;
}

// For placing buildings.
// Assume that the building is entirely on the map.
bool GridAttacks::inRange(BWAPI::UnitType buildingType, const BWAPI::TilePosition & topLeftTile) const
{
    UAB_ASSERT(buildingType.isBuilding(), "bad type");

    BWAPI::TilePosition bottomRightTile(
        topLeftTile.x + buildingType.tileWidth() - 1,
        topLeftTile.y + buildingType.tileHeight() - 1);

    return inRange(topLeftTile, bottomRightTile);
}

// For checking the safety of a unit.
// Assume that the unit is entirely on the map.
bool GridAttacks::inRange(BWAPI::Unit unit) const
{
    UAB_ASSERT(unit && unit->isVisible(), "bad unit");

    BWAPI::TilePosition topLeftTile(BWAPI::Position(unit->getLeft(), unit->getTop()));
    BWAPI::TilePosition bottomRightTile(BWAPI::Position(unit->getRight(), unit->getBottom()));

    return inRange(topLeftTile, bottomRightTile);
}

// The unit is a building or resource. Check whether tiles adjacent to the unit are in range.
// This is for finding out whether a resource can be safely mined, or a production building can
// safely produce units.
bool GridAttacks::safeToVisit(BWAPI::Unit unit) const
{
    UAB_ASSERT(unit && unit->isVisible(), "bad unit");

    BWAPI::TilePosition topLeftTile(unit->getTilePosition().x - 1, unit->getTilePosition().y - 1);
    topLeftTile.makeValid();

    BWAPI::TilePosition bottomRightTile(
        unit->getTilePosition().x + unit->getType().tileWidth(),
        unit->getTilePosition().y + unit->getType().tileHeight());
    bottomRightTile.makeValid();

    return !inRange(topLeftTile, bottomRightTile);
}
