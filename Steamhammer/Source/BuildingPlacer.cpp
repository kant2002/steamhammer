#include "BuildingPlacer.h"

#include "Bases.h"
#include "MapTools.h"
#include "The.h"

using namespace UAlbertaBot;

// Don't build in a position that blocks mining. Part of initialization.
void BuildingPlacer::reserveSpaceNearResources()
{
    for (Base * base : the.bases.getAll())
    {
        // A tile close to the center of the resource depot building (which is 4x3 tiles).
        BWAPI::TilePosition baseTile = base->getTilePosition() + BWAPI::TilePosition(2, 1);

        // NOTE This still allows the bot to block mineral mining of some patches, but it's relatively rare.
        for (BWAPI::Unit mineral : base->getMinerals())
        {
            BWAPI::TilePosition minTile = mineral->getTilePosition();
            if (minTile.x < baseTile.x)
            {
                reserveTiles(minTile + BWAPI::TilePosition(2, 0), 1, 1);
            }
            else if (minTile.x > baseTile.x)
            {
                reserveTiles(minTile + BWAPI::TilePosition(-1, 0), 1, 1);
            }
            if (minTile.y < baseTile.y)
            {
                reserveTiles(minTile + BWAPI::TilePosition(0, 1), 2, 1);
            }
            else if (minTile.y > baseTile.y)
            {
                reserveTiles(minTile + BWAPI::TilePosition(0, -1), 2, 1);
            }
        }

        for (BWAPI::Unit gas : base->getGeysers())
        {
            // Don't build on the right edge or a right corner of a geyser.
            // It causes workers to take a long path around. Other locations are OK.
            BWAPI::TilePosition gasTile = gas->getTilePosition();
            //reserveTiles(gasTile, 4, 2);
            if (gasTile.x - baseTile.x > 2)
            {
                reserveTiles(gasTile + BWAPI::TilePosition(-1, 1), 3, 2);
            }
            else if (gasTile.x - baseTile.x < -2)
            {
                reserveTiles(gasTile + BWAPI::TilePosition(2, -1), 3, 2);
            }
            if (gasTile.y - baseTile.y > 2)
            {
                reserveTiles(gasTile + BWAPI::TilePosition(-1, -1), 2, 3);
            }
            else if (gasTile.y - baseTile.y < -2)
            {
                reserveTiles(gasTile + BWAPI::TilePosition(3, 0), 2, 3);
            }
        }
    }
}

// Reserve or unreserve tiles.
// Tilepositions off the map are silently ignored; initialization code depends on it.
void BuildingPlacer::setReserve(const BWAPI::TilePosition & position, int width, int height, bool flag)
{
    int rwidth = _reserveMap.size();
    int rheight = _reserveMap[0].size();

    for (int x = std::max(position.x, 0); x < std::min(position.x + width, rwidth); ++x)
    {
        for (int y = std::max(position.y, 0); y < std::min(position.y + height, rheight); ++y)
        {
            _reserveMap[x][y] = flag;
        }
    }
}

// We want to build near the given tile, but it may not be walkable, which makes it awkward
// to find nearby buildable tiles. Find a nearby buildable tile to start with.
// NOTE This can fail in theory, but it should not happen on any reasonable map.
BWAPI::TilePosition BuildingPlacer::connectedWalkableTileNear(const BWAPI::TilePosition & start) const
{
    BWAPI::TilePosition closestTile = BWAPI::TilePositions::None;
    int closestDist = MAX_DISTANCE;

    // We can step by twos because every building is at least 2x2 in size.
    for (int x = 0; x < BWAPI::Broodwar->mapWidth(); x += 2)
    {
        for (int y = 0; y < BWAPI::Broodwar->mapHeight(); y += 2)
        {
            if (_buildable.at(x,y) > 0 && the.bases.connectedToStart(BWAPI::TilePosition(x, y)))
            {
                int dist = abs(x - start.x) + abs(y - start.y);
                if (dist < closestDist)
                {
                    closestTile = BWAPI::TilePosition(x, y);
                    closestDist = dist;
                }
            }
        }
    }

    return closestTile;
}

// If we're building at the enemy base, we don't care if our building overlaps a base location.
bool BuildingPlacer::enemyMacroLocation(MacroLocation loc) const
{
    return
        loc == MacroLocation::Proxy ||
        loc == MacroLocation::EnemyMain ||
        loc == MacroLocation::EnemyNatural;
}

// The rectangle in tile coordinates overlaps with a resource depot location.
bool BuildingPlacer::boxOverlapsBase(int x1, int y1, int x2, int y2) const
{
    for (Base * base : the.bases.getAll())
    {
        // The base location. It's the same size for all races.
        int bx1 = base->getTilePosition().x;
        int by1 = base->getTilePosition().y;
        int bx2 = bx1 + 3;
        int by2 = by1 + 2;

        if (!(x2 < bx1 || x1 > bx2 || y2 < by1 || y1 > by2))
        {
            return true;
        }
    }

    // No base overlaps.
    return false;
}

bool BuildingPlacer::tileBlocksAddon(const BWAPI::TilePosition & position) const
{
    for (int i = 0; i <= 2; ++i)
    {
        for (BWAPI::Unit unit : BWAPI::Broodwar->getUnitsOnTile(position.x - i, position.y))
        {
            if (unit->getType().canBuildAddon())
            {
                return true;
            }
        }
    }

    return false;
}

// Does this rectangle consist entirely of buildable terrain?
// The test is very fast and can be done before other buildability checks.
bool BuildingPlacer::buildableTerrain(int x0, int y0, int width, int height) const
{
    for (int y = y0; y < y0 + height; ++y)
    {
        if (_buildable.at(x0, y) < width)
        {
            return false;
        }
    }
    return true;
}

// The tile is free of permanent obstacles, including future ones from planned buildings.
// There might be a unit passing through, though.
// The caller must ensure that x and y are in range!
bool BuildingPlacer::isFreeTile(int x, int y) const
{
    UAB_ASSERT(BWAPI::TilePosition(x,y).isValid(), "bad tile");

    if (!BWAPI::Broodwar->isBuildable(x, y, true) || _reserveMap[x][y])
    {
        return false;
    }
    if (the.self()->getRace() == BWAPI::Races::Terran && tileBlocksAddon(BWAPI::TilePosition(x, y)))
    {
        return false;
    }

    return true;
}

// Check that nothing obstructs the top of the building, including the corners.
// For example, if the building is o, then nothing must obstruct the tiles marked x:
//
//  x x x x
//    o o
//    o o
//
// Unlike canBuildHere(), the freeOn...() functions do not care if mobile units are on the tiles.
// They only care that the tiles are buildable (which implies walkable) and not reserved for future buildings.
bool BuildingPlacer::freeOnTop(const BWAPI::TilePosition & tile, BWAPI::UnitType buildingType) const
{
    int x1 = tile.x - 1;
    int x2 = tile.x + buildingType.tileWidth();
    int y = tile.y - 1;
    if (y < 0 || x1 < 0 || x2 >= BWAPI::Broodwar->mapWidth())
    {
        return false;
    }

    for (int x = x1; x <= x2; ++x)
    {
        if (!isFreeTile(x,y))
        {
            return false;
        }
    }
    return true;
}

//      x
//  o o x
//  o o x
//      x
bool BuildingPlacer::freeOnRight(const BWAPI::TilePosition & tile, BWAPI::UnitType buildingType) const
{
    int x = tile.x + buildingType.tileWidth();
    int y1 = tile.y - 1;
    int y2 = tile.y + buildingType.tileHeight();
    if (x >= BWAPI::Broodwar->mapWidth() || y1 < 0 || y2 >= BWAPI::Broodwar->mapHeight())
    {
        return false;
    }

    for (int y = y1; y <= y2; ++y)
    {
        if (!isFreeTile(x, y))
        {
            return false;
        }
    }
    return true;
}

//  x
//  x o o
//  x o o
//  x
bool BuildingPlacer::freeOnLeft(const BWAPI::TilePosition & tile, BWAPI::UnitType buildingType) const
{
    int x = tile.x - 1;
    int y1 = tile.y - 1;
    int y2 = tile.y + buildingType.tileHeight();
    if (x < 0 || y1 < 0 || y2 >= BWAPI::Broodwar->mapHeight())
    {
        return false;
    }

    for (int y = y1; y <= y2; ++y)
    {
        if (!isFreeTile(x, y))
        {
            return false;
        }
    }
    return true;
}

//    o o
//    o o
//  x x x x
bool BuildingPlacer::freeOnBottom(const BWAPI::TilePosition & tile, BWAPI::UnitType buildingType) const
{
    int x1 = tile.x - 1;
    int x2 = tile.x + buildingType.tileWidth();
    int y = tile.y + buildingType.tileHeight();
    if (y >= BWAPI::Broodwar->mapHeight() || x1 < 0 || x2 >= BWAPI::Broodwar->mapWidth())
    {
        return false;
    }

    for (int x = x1; x <= x2; ++x)
    {
        if (!isFreeTile(x, y))
        {
            return false;
        }
    }
    return true;
}

bool BuildingPlacer::freeOnAllSides(BWAPI::Unit building) const
{
    return
        freeOnTop(building->getTilePosition(), building->getType()) &&
        freeOnRight(building->getTilePosition(), building->getType()) &&
        freeOnLeft(building->getTilePosition(), building->getType()) &&
        freeOnBottom(building->getTilePosition(), building->getType());
}

// Can a building can be built here?
// This does not check all conditions! Other code must check for possible overlaps
// with planned or future buildings.
bool BuildingPlacer::canBuildHere(const BWAPI::TilePosition & position, const Building & b) const
{
    return
        // BWAPI thinks the space is buildable.
        // This includes looking for units which may be in the way.
        BWAPI::Broodwar->canBuildHere(position, b.type, b.builderUnit) &&

        // A worker can reach the place.
        // NOTE This simplified check disallows building on islands!
        the.bases.connectedToStart(position) &&
        
        // Enemy static defense/immobile units cannot fire on the building location.
        // This is part of the response to e.g. cannon rushes.
        !the.groundAttacks.inRange(b.type, position);
}

// Can we build this building here with the specified amount of space around it?
bool BuildingPlacer::canBuildWithSpace(const BWAPI::TilePosition & position, const Building & b, int extraSpace) const
{
    // Can the building go here? This does not check the extra space or worry about future
    // buildings, but does all necessary checks for current obstructions of the building area itself.
    if (!canBuildHere(position, b))
    {
        return false;
    }

    // Is the entire area, including the extra space, free of obstructions
    // from possible future buildings?

    // Height and width of the building.
    int width(b.type.tileWidth());
    int height(b.type.tileHeight());

    // Allow space for terran addons. The space may be taller than necessary; it's easier that way.
    // All addons are 2x2 tiles.
    if (b.type.canBuildAddon())
    {
        width += 2;
    }

    // A rectangle covering the building spot.
    int x1 = position.x - extraSpace;
    int y1 = position.y - extraSpace;
    int x2 = position.x + width + extraSpace - 1;
    int y2 = position.y + height + extraSpace - 1;

    // The rectangle must fit on the map.
    if (x1 < 0 || x2 >= BWAPI::Broodwar->mapWidth() ||
        y1 < 0 || y2 >= BWAPI::Broodwar->mapHeight())
    {
        return false;
    }
    
    if (!enemyMacroLocation(b.macroLocation) && boxOverlapsBase(x1, y1, x2, y2))
    {
        return false;
    }

    // Every tile must be buildable and unreserved.
    for (int x = x1; x <= x2; ++x)
    {
        for (int y = y1; y <= y2; ++y)
        {
            if (!isFreeTile(x, y))
            {
                return false;
            }
        }
    }

    return true;
}

// Buildings of these types should be grouped together,
// not grouped with buildings of other types.
bool BuildingPlacer::groupTogether(BWAPI::UnitType type) const
{
    return
        type == BWAPI::UnitTypes::Terran_Barracks ||
        type == BWAPI::UnitTypes::Terran_Factory ||
        type == BWAPI::UnitTypes::Protoss_Gateway;
}

// Seek a location on the edge of the map.
// NOTE
//   This is intended for supply depots and tech buildings.
//   It may go wrong if given a building which can take addons.
BWAPI::TilePosition BuildingPlacer::findEdgeLocation(const Building & b) const
{
    // The building's tile position is its upper left corner.
    int rightEdge = BWAPI::Broodwar->mapWidth() - b.type.tileWidth();
    int bottomEdge = BWAPI::Broodwar->mapHeight() - b.type.tileHeight();

    for (const BWAPI::TilePosition & tile : the.map.getClosestTilesTo(the.bases.myMain()->getTilePosition()))
    {
        // If the position is too far away, skip it.
        if (the.zone.at(tile) != the.zone.at(the.bases.myMain()->getTilePosition()) ||
            tile.getApproxDistance(the.bases.myMain()->getTilePosition()) > 18 * 32)
        {
            continue;
        }

        // Left.
        if (tile.x == 0)
        {
            if (canBuildWithSpace(tile, b, 0) && freeOnRight(tile, b.type))
            {
                return tile;
            }
        }

        // Top.
        if (tile.y == 0)
        {
            if (canBuildWithSpace(tile, b, 0) && freeOnBottom(tile, b.type))
            {
                return tile;
            }
        }

        // Right.
        else if (tile.x + b.type.tileWidth() == BWAPI::Broodwar->mapWidth())
        {
            if (canBuildWithSpace(tile, b, 0) && freeOnLeft(tile, b.type))
            {
                return tile;
            }
        }

        // Bottom.
        // NOTE The map's bottom row of tiles is not buildable (though other edges are).
        else if (tile.y + b.type.tileHeight() == BWAPI::Broodwar->mapHeight() - 1)
        {
            if (canBuildWithSpace(tile, b, 0) && freeOnTop(tile, b.type))
            {
                return tile;
            }
        }
    }

    // It's not on an edge.
    return BWAPI::TilePositions::None;
}

// Try to put a pylon at every base.
BWAPI::TilePosition BuildingPlacer::findPylonlessBaseLocation(const Building & b) const
{
    if (b.macroLocation != MacroLocation::Anywhere)
    {
        // The location is specified, don't override it.
        return BWAPI::TilePositions::None;
    }

    for (Base * base : the.bases.getAll())
    {
        // NOTE We won't notice a pylon that is in the building manager but not started yet.
        //      It's not a problem.
        if (base->getOwner() == the.self() && !base->getPylon())
        {
            Building pylon = b;
            pylon.desiredPosition = BWAPI::TilePosition(base->getFrontTile());
            return findAnyLocation(pylon, 0);
        }
    }

    return BWAPI::TilePositions::None;
}

BWAPI::TilePosition BuildingPlacer::findGroupedLocation(const Building & b) const
{
    // Some buildings should not be clustered.
    // That includes resource depots and refineries, which are placed by different code.
    if (b.type == BWAPI::UnitTypes::Terran_Bunker ||
        b.type == BWAPI::UnitTypes::Terran_Missile_Turret ||
        b.type == BWAPI::UnitTypes::Protoss_Pylon ||
        b.type == BWAPI::UnitTypes::Protoss_Photon_Cannon ||
        b.type == BWAPI::UnitTypes::Zerg_Sunken_Colony ||
        b.type == BWAPI::UnitTypes::Zerg_Spore_Colony ||
        b.type == BWAPI::UnitTypes::Zerg_Creep_Colony ||
        b.type == BWAPI::UnitTypes::Zerg_Hatchery)
    {
        return BWAPI::TilePositions::None;
    }

    // Some buildings are best grouped with others of the same type.
    const bool sameType = groupTogether(b.type);

    BWAPI::Unitset choices;
    for (BWAPI::Unit building : the.self()->getUnits())
    {
        if (building->getType().isBuilding() &&
            building->getType() != BWAPI::UnitTypes::Protoss_Pylon &&
            the.zone.at(building->getTilePosition()) == the.zone.at(b.desiredPosition) &&
            (building->getType().tileWidth() == b.type.tileWidth() || building->getType().tileHeight() == b.type.tileHeight()))
        {
            if (sameType)
            {
                if (b.type == building->getType() && freeOnAllSides(building))
                {
                    choices.insert(building);
                }
            }
            else
            {
                if (!groupTogether(building->getType()) && freeOnAllSides(building))
                {
                    choices.insert(building);
                }
            }
        }
    }

    for (BWAPI::Unit choice : choices)
    {
        BWAPI::TilePosition tile;

        if (choice->getType().tileWidth() == b.type.tileWidth())
        {
            // Above.
            tile = choice->getTilePosition() + BWAPI::TilePosition(0, -b.type.tileHeight());
            if (canBuildWithSpace(tile, b, 0) &&
                freeOnTop(tile, b.type) &&
                freeOnLeft(tile, b.type) &&
                freeOnRight(tile, b.type))
            {
                return tile;
            }

            // Below.
            tile = choice->getTilePosition() + BWAPI::TilePosition(0, choice->getType().tileHeight());
            if (canBuildWithSpace(tile, b, 0) &&
                freeOnBottom(tile, b.type) &&
                freeOnLeft(tile, b.type) &&
                freeOnRight(tile, b.type))
            {
                return tile;
            }
        }

        // Buildings that may have addons in the future should be grouped vertically if at all.
        if (choice->getType().tileHeight() == b.type.tileHeight() &&
            !b.type.canBuildAddon() &&
            !choice->getType().canBuildAddon())
        {
            // Left.
            tile = choice->getTilePosition() + BWAPI::TilePosition(-b.type.tileWidth(), 0);
            if (canBuildWithSpace(tile, b, 0) &&
                freeOnTop(tile, b.type) &&
                freeOnLeft(tile, b.type) &&
                freeOnBottom(tile, b.type))
            {
                return tile;
            }

            // Right.
            tile = choice->getTilePosition() + BWAPI::TilePosition(0, choice->getType().tileHeight());
            if (canBuildWithSpace(tile, b, 0) &&
                freeOnBottom(tile, b.type) &&
                freeOnLeft(tile, b.type) &&
                freeOnTop(tile, b.type))
            {
                return tile;
            }
        }
    }
    
    return BWAPI::TilePositions::None;
}

// Some buildings get special-case placement.
BWAPI::TilePosition BuildingPlacer::findSpecialLocation(const Building & b) const
{
    BWAPI::TilePosition tile = BWAPI::TilePositions::None;

    if (b.type == BWAPI::UnitTypes::Terran_Supply_Depot ||
        b.type == BWAPI::UnitTypes::Terran_Academy ||
        b.type == BWAPI::UnitTypes::Terran_Armory)
    {
        // These buildings are all 3x2 and will line up neatly.
        tile = findEdgeLocation(b);
        if (!tile.isValid())
        {
            tile = findGroupedLocation(b);
        }
    }
    else if (b.type == BWAPI::UnitTypes::Protoss_Pylon)
    {
        tile = findPylonlessBaseLocation(b);
    }
    else
    {
        tile = findGroupedLocation(b);
    }

    return tile;
}

BWAPI::TilePosition BuildingPlacer::findAnyLocation(const Building & b, int extraSpace) const
{
    // Tiles sorted in order of closeness to the location.
    const std::vector<BWAPI::TilePosition> & closest = the.map.getClosestTilesTo(b.desiredPosition);

    for (const BWAPI::TilePosition & tile : closest)
    {
        if (canBuildWithSpace(tile, b, extraSpace))
        {
            return tile;
        }
    }

    return BWAPI::TilePositions::None;
}

// Mineral patches in range of the position. Calculations in pixels.
int BuildingPlacer::countInRange(const BWAPI::Unitset & patches, BWAPI::Position xy, int range) const
{
    int count = 0;
    for (BWAPI::Unit patch : patches)
    {
        if (xy.getApproxDistance(patch->getInitialPosition()) <= range)
        {
            ++count;
        }
    }
    return count;
}

// -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- --

BuildingPlacer::BuildingPlacer()
{
}

void BuildingPlacer::initialize()
{
    _reserveMap = std::vector< std::vector<bool> >(BWAPI::Broodwar->mapWidth(), std::vector<bool>(BWAPI::Broodwar->mapHeight(), false));

    reserveSpaceNearResources();
    _buildable.compute();
}

// Place a building other than an expansion.
// The minimum distance between buildings is extraSpace, the extra space we check
// for accessibility around each potential building location.
BWAPI::TilePosition BuildingPlacer::getBuildLocationNear(const Building & b, int extraSpace) const
{
    // BWAPI::Broodwar->printf("Building Placer seeks position near %d, %d", b.desiredPosition.x, b.desiredPosition.y);

    BWAPI::TilePosition tile = BWAPI::TilePositions::None;
    
    tile = findSpecialLocation(b);
    
    if (!tile.isValid())
    {
        tile = findAnyLocation(b, extraSpace);
    }

    // Let Bases decide whether to change which base is the main base.
    the.bases.checkBuildingPosition(b.desiredPosition, tile);

    return tile;		// may be None
}

void BuildingPlacer::reserveTiles(const BWAPI::TilePosition & position, int width, int height)
{
    setReserve(position, width, height, true);
}

void BuildingPlacer::freeTiles(const BWAPI::TilePosition & position, int width, int height)
{
    setReserve(position, width, height, false);
}

void BuildingPlacer::freeTiles(const Building & b)
{
    freeTiles(b.finalPosition, b.type.tileWidth(), b.type.tileHeight());
}

bool BuildingPlacer::isReserved(int x, int y) const
{
    UAB_ASSERT(BWAPI::TilePosition(x, y).isValid(), "bad tile");

    return _reserveMap[x][y];
}

void BuildingPlacer::drawReservedTiles() const
{
    if (!Config::Debug::DrawReservedBuildingTiles)
    {
        return;
    }

    // _buildable.draw();

    int rwidth = _reserveMap.size();
    int rheight = _reserveMap[0].size();

    for (int x = 0; x < rwidth; ++x)
    {
        for (int y = 0; y < rheight; ++y)
        {
            if (_reserveMap[x][y])
            {
                int x1 = x*32 + 3;
                int y1 = y*32 + 3;
                int x2 = (x+1)*32 - 3;
                int y2 = (y+1)*32 - 3;

                BWAPI::Broodwar->drawBoxMap(x1, y1, x2, y2, BWAPI::Colors::Grey);
            }
        }
    }
}

// The building has been placed. Is the placement valid?
// Used when outside callers want to make sure.
bool BuildingPlacer::buildingOK(const Building & b) const
{
    return buildingOK(b, b.finalPosition);
}

// Is the placement valid for the building?
// Used by outside callers.
// TODO does this check for lack of blocks? apparently not
bool BuildingPlacer::buildingOK(const Building & b, const BWAPI::TilePosition & pos) const
{
    return
        pos.isValid() &&
        canBuildHere(pos, b);
}

// Find the desired location for a command center, nexus, or hatchery from its macro location.
// It's usually the same as usual, but the "Anywhere" default is different.
BWAPI::TilePosition BuildingPlacer::getExpoLocationTile(MacroLocation loc) const
{
    if (loc == MacroLocation::Anywhere)
    {
        if (the.bases.myNatural() &&
            the.bases.myNatural()->owner == the.neutral() &&
            the.bases.myStart()->owner == the.self())
        {
            loc = MacroLocation::Natural;
        }
        else
        {
            loc = MacroLocation::Expo;
        }
    }

    return getMacroLocationTile(loc);
}

// Generic macro locations on the map.
// If used to place a building, the expectation in most cases is that the returned tile
// will be a starting point; we'll look for an open area nearby to build.
// NOTE Some cases must be treated as special cases, e.g. refinery building locations.
//      See BuildingManager::getBuildingLocation().
BWAPI::TilePosition BuildingPlacer::getMacroLocationTile(MacroLocation loc) const
{
    if (loc == MacroLocation::Main)
    {
        // A main base building, including macro hatchery.
        // The main base is always set, even if we don't have any building there at the moment.
        return the.bases.myMain()->getTilePosition();
    }
    else if (loc == MacroLocation::Natural)
    {
        Base * natural = the.bases.myNatural();
        if (natural)
        {
            return natural->getTilePosition();
        }
    }
    else if (loc == MacroLocation::Front)
    {
        BWAPI::TilePosition front = the.bases.frontTile();
        if (front.isValid())
        {
            return front;
        }
    }
    else if (loc == MacroLocation::Expo)
    {
        // Mineral and gas base.
        BWAPI::TilePosition pos = the.map.getNextExpansion(false, true, true);
        if (pos.isValid())
        {
            return pos;
        }
    }
    else if (loc == MacroLocation::MinOnly)
    {
        // Mineral base with or without gas geyser.
        BWAPI::TilePosition pos = the.map.getNextExpansion(false, true, false);
        if (pos.isValid())
        {
            return pos;
        }
    }
    else if (loc == MacroLocation::GasOnly)
    {
        // Gas base with or without minerals.
        BWAPI::TilePosition pos = the.map.getNextExpansion(false, false, true);
        if (pos.isValid())
        {
            return pos;
        }
    }
    else if (loc == MacroLocation::Hidden)
    {
        // "Hidden" mineral and gas base.
        BWAPI::TilePosition pos = the.map.getNextExpansion(true, true, true);
        if (pos.isValid())
        {
            return pos;
        }
    }
    else if (loc == MacroLocation::Center)
    {
        // Near the center of the map. Find a walkable tile connected to the start.
        return connectedWalkableTileNear(BWAPI::TilePosition(BWAPI::Broodwar->mapWidth() / 2, BWAPI::Broodwar->mapHeight() / 2));
    }
    else if (loc == MacroLocation::Proxy)
    {
        if (the.bases.enemyStart())
        {
            // We know where the enemy is. We can proxy in or close to the enemy base.
            // Other code should try to find the enemy base first!
            BWAPI::TilePosition proxy = getProxyPosition(the.bases.enemyStart());
            if (proxy.isValid())
            {
                return proxy;
            }
        }
        // We don't know where the enemy is, or can't find a close proxy position,
        // but we can at least proxy to the center of the map.
        return getMacroLocationTile(MacroLocation::Center);
    }
    else if (loc == MacroLocation::EnemyMain)
    {
        if (the.bases.enemyStart())
        {
            return the.bases.enemyStart()->getTilePosition();
        }
        return getMacroLocationTile(MacroLocation::Center);
    }
    else if (loc == MacroLocation::EnemyNatural)
    {
        if (the.bases.enemyStart() && the.bases.enemyStart()->getNatural())
        {
            return the.bases.enemyStart()->getNatural()->getTilePosition();
        }
        return getMacroLocationTile(MacroLocation::Center);
    }

    // Default: Build in the current main base, which is guaranteed to exist (though it may be empty or in enemy hands).
    // MacroLocation::Anywhere falls through to here. So does MacroLocation::Tile.
    return the.bases.myMain()->getTilePosition();
}

BWAPI::Position BuildingPlacer::getMacroLocationPos(MacroLocation loc) const
{
    return TileCenter(getMacroLocationTile(loc));
}

// NOTE This allows building only on visible geysers.
BWAPI::TilePosition BuildingPlacer::getRefineryPosition() const
{
    BWAPI::TilePosition closestGeyser = BWAPI::TilePositions::None;
    int minGeyserDistanceFromHome = 100000;
    BWAPI::Position homePosition = the.bases.myMain()->getPosition();

    for (BWAPI::Unit geyser : BWAPI::Broodwar->getGeysers())
    {
        // Check to see if the geyser is near one of our depots.
        for (BWAPI::Unit unit : the.self()->getUnits())
        {
            if (unit->getType().isResourceDepot() && unit->getDistance(geyser) < 300)
            {
                // Don't take a geyser which is in enemy static defense range. It'll just die.
                // This is rare so we check it only after other checks succeed.
                if (the.groundAttacks.inRange(geyser->getType(), geyser->getTilePosition()))
                {
                    break;
                }

                int homeDistance = geyser->getDistance(homePosition);

                if (homeDistance < minGeyserDistanceFromHome)
                {
                    minGeyserDistanceFromHome = homeDistance;
                    closestGeyser = geyser->getTilePosition();      // BWAPI bug workaround by Arrak
                }
                break;
            }
        }
    }
    
    return closestGeyser;
}

// Near the given base, protected by terrain, in range of the minerals.
// Only possible on a few maps, but a potential game-winner when possible.
// Return an invalid tile on failure.
// TODO in progress
BWAPI::TilePosition BuildingPlacer::getTerrainProxyPosition(const Base * base) const
{
    const int range = 9;

    // A fictitious large building to place.
    Building b(BWAPI::UnitTypes::Protoss_Nexus, BWAPI::TilePositions::None);

    // Compute the bounding box of the minerals, in tiles.
    int left = MAX_DISTANCE;
    int right = -1;
    int top = MAX_DISTANCE;
    int bottom = -1;
    for (BWAPI::Unit patch : base->getMinerals())
    {
        left   = std::min(left,   patch->getInitialTilePosition().x);
        right  = std::max(right,  patch->getInitialTilePosition().x + 1);    // patch size is 2x1
        top    = std::min(top,    patch->getInitialTilePosition().y);
        bottom = std::max(bottom, patch->getInitialTilePosition().y);
    }

    // Find the position which is in range and hits the most mineral patches,
    // with greater ground distance as the tiebreaker.
    BWAPI::TilePosition bestTile = BWAPI::TilePositions::Invalid;
    int bestHits = 2;
    int bestDist = 0;
    for (int x = left - range; x <= right + range; ++x)
    {
        for (int y = top - range; y <= bottom + range; ++y)
        {
            BWAPI::TilePosition xy(x, y);
            if (xy.isValid())
            {
                int groundDist = base->getTileDistance(xy);
                if (groundDist > 20 && canBuildHere(xy, b))
                {
                    int hits = countInRange(base->getMinerals(), TileCenter(xy), 32 * range);
                    if (hits > bestHits || hits == bestHits && groundDist > bestDist)
                    {
                        bestTile = xy;
                        bestHits = hits;
                        bestDist = groundDist;
                        /*
                        BWAPI::Broodwar->printf("xy %d,%d ground dist %d hits %d",
                            xy.x, xy.y, groundDist, hits);
                        BWAPI::Broodwar->printf("   best %d,%d, ground dist %d hits %d",
                            bestTile.x, bestTile.y, bestDist, bestHits);
                        */
                    }
                }
            }
        }
    }

    return bestTile;
}

// In the given base, as far away as possible from the enemy's position
// and from the closest entrance.
// Return an invalid tile on failure.
BWAPI::TilePosition BuildingPlacer::getInBaseProxyPosition(const Base * base) const
{
    // Tiles sorted in order of closeness to the enemy main resource depot.
    const BWAPI::TilePosition enemyCenter = base->getCenterTile();
    const std::vector<BWAPI::TilePosition> & closest = the.map.getClosestTilesTo(enemyCenter);

    // A fictitious large building to place.
    Building b(BWAPI::UnitTypes::Protoss_Nexus, BWAPI::TilePositions::None);

    const int myMinDist = the.bases.myMain()->getTileDistance(base->getTilePosition());
    int bestScore = 16;
    BWAPI::TilePosition bestTile = BWAPI::TilePositions::None;
    for (const BWAPI::TilePosition & tile : closest)
    {
        const int enemyDistX = abs(tile.x - enemyCenter.x);
        const int enemyDistY = abs(tile.y - enemyCenter.y);
        const int myDist = the.bases.myMain()->getTileDistance(tile) - myMinDist;
        const int score = enemyDistX + enemyDistY + myDist;
        if (myDist >= 0 && enemyDistX > 8 && enemyDistY > 8 && score > bestScore)
        {
            if (the.zone.at(tile) == the.zone.at(enemyCenter) && canBuildHere(tile, b))
            {
                bestScore = score;
                bestTile = tile;
            }
        }
        else if (enemyDistX > 24)
        {
            // Too far away. Stop looking.
            break;
        }
    }
    return bestTile;
}

// Return an invalid tile on failure.
BWAPI::TilePosition BuildingPlacer::getProxyPosition(const Base * base) const
{
    UAB_ASSERT(base, "bad base");

    BWAPI::TilePosition tile = BWAPI::TilePositions::Invalid;

    Base * natural = base->getNatural();
    if (natural)
    {
        // In the enemy natural, hidden by terrain. Only possible on a few maps.
        // TODO disabled for now, needs improved building placement skills + pathing
        // tile = getTerrainProxyPosition(natural);
    }
    if (!tile.isValid())
    {
        // In a far corner of the enemy main.
        tile = getInBaseProxyPosition(base);
    }

    return tile;
}

// A zerg base has been bunkered by a terran opponent.
// Figure out where a sunk can be placed out of bunker range but in range to hit the bunker.
// Return an invalid tile on failure.
BWAPI::TilePosition BuildingPlacer::getAntiBunkerSunkenPosition(const Base * base, BWAPI::Unit bunker) const
{
    UAB_ASSERT(base, "bad base");
    UAB_ASSERT(bunker, "bad unit");

    Building creep(BWAPI::UnitTypes::Zerg_Creep_Colony, BWAPI::TilePositions::None);

    int closestToMain = MAX_DISTANCE;

    for (int x = base->getCenterTile().x - 7; x <= base->getCenterTile().x + 7; ++x)
    {
        for (int y = base->getCenterTile().y - 7; y <= base->getCenterTile().y + 7; ++y)
        {
            BWAPI::TilePosition xy(x, y);
            if (xy.isValid())
            {
                int dist = bunker->getDistance(BWAPI::Position(TileCenter(xy)));
                if (dist > 5 * 32 &&        // sunken is out of bunker range
                    dist < 7 * 32 &&        // bunker is in sunken range
                    canBuildHere(xy, creep))
                {
                    return xy;
                }
            }
        }
    }

    return BWAPI::TilePositions::Invalid;
}

// A zerg base has cannons nearby.
// 1. Find where to place a sunk to hit a pylon, exploiting the sunken range bug.
// 2. Failing that, where to place it to prevent cannons from creeping closer.
// Return an invalid tile on failure.
BWAPI::TilePosition BuildingPlacer::getAntiCannonSunkenPosition(const Base * base, BWAPI::Unit cannon) const
{
    UAB_ASSERT(base, "bad base");
    UAB_ASSERT(cannon, "bad unit");

    Building creep(BWAPI::UnitTypes::Zerg_Creep_Colony, BWAPI::TilePositions::None);

    int totalDist = cannon->getDistance(base->getCenter());
    UAB_ASSERT(totalDist <= 16 * 32, "too distant");
    int bestCannonDist = totalDist;
    int bestPylonDist = totalDist;
    int closestToMainVsCannon = MAX_DISTANCE;
    int closestToMainVsPylon = MAX_DISTANCE;

    BWAPI::TilePosition vsCannonTile = BWAPI::TilePositions::Invalid;
    BWAPI::TilePosition vsPylonTile = BWAPI::TilePositions::Invalid;

    // As close to the target as possible without being in cannon range.
    // As a second consideration, as close as possible to the main base, to protect the entrance.
    int offset = std::min(9, (totalDist + 31)/32);
    for (int x = base->getCenterTile().x - offset; x <= base->getCenterTile().x + offset; ++x)
    {
        for (int y = base->getCenterTile().y - offset; y <= base->getCenterTile().y + offset; ++y)
        {
            BWAPI::TilePosition xy(x, y);
            if (xy.isValid() && BWAPI::Broodwar->hasCreep(xy))
            {
                // The approximate center of where the cannon will be if placed at tile xy.
                const BWAPI::Position xyCenter = BWAPI::Position(xy) + BWAPI::Position(32, 32);

                const int cannonDist = cannon->getDistance(xyCenter);       // in pixels
                if (cannonDist >= 7 * 32 + 31  &&
                    canBuildHere(xy, creep) &&
                    nullptr == BWAPI::Broodwar->getClosestUnit(
                        xyCenter,
                        (BWAPI::Filter::GetType == BWAPI::UnitTypes::Zerg_Sunken_Colony || BWAPI::Filter::GetType == BWAPI::UnitTypes::Zerg_Creep_Colony) && BWAPI::Filter::IsOwned,
                        6 * 32))
                {
                    const int mainDist = base->getMain() ? base->getMain()->getTileDistance(xy) : MAX_DISTANCE;     // in tiles

                    // Place a sunken out of range of the cannon but as close as possible, to prevent cannons from creeping closer.
                    if (cannonDist < bestCannonDist || cannonDist == bestCannonDist && mainDist < closestToMainVsCannon)
                    {
                        bestCannonDist = cannonDist;
                        vsCannonTile = xy;
                        closestToMainVsCannon = mainDist;
                    }

                    if (Config::Skills::UseSunkenRangeBug)
                    {
                        // Place a sunken close enough to hit a pylon. Then when the cannon fires, the sunken will target the
                        // cannon even though the cannon is out of range.
                        BWAPI::Unit pylon = BWAPI::Broodwar->getClosestUnit(
                            xyCenter,
                            BWAPI::Filter::GetType == BWAPI::UnitTypes::Protoss_Pylon && BWAPI::Filter::IsEnemy,
                            7 * 32
                        );
                        if (pylon)
                        {
                            const int pylonDist = pylon->getDistance(xyCenter);     // in pixels
                            if (pylonDist < bestPylonDist || pylonDist == bestPylonDist && mainDist < closestToMainVsPylon)
                            {
                                bestPylonDist = pylonDist;
                                vsPylonTile = xy;
                                closestToMainVsPylon = mainDist;
                            }
                        }
                    }
                }
            }
        }
    }

    return vsPylonTile.isValid() ? vsPylonTile : vsCannonTile;
}
