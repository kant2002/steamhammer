#pragma once

#include "BuildingData.h"
#include "GridBuildable.h"

namespace UAlbertaBot
{
class The;
class Base;

class BuildingPlacer
{
    std::vector< std::vector<bool> > _reserveMap;
    GridBuildable _buildable;

    void	reserveSpaceNearResources();

    void	setReserve(const BWAPI::TilePosition & position, int width, int height, bool flag);

    BWAPI::TilePosition connectedWalkableTileNear(const BWAPI::TilePosition & start) const;

    bool    enemyMacroLocation(MacroLocation loc) const;
    bool	boxOverlapsBase(int x1, int y1, int x2, int y2) const;
    bool	tileBlocksAddon(const BWAPI::TilePosition & position) const;

    bool    buildableTerrain(int x1, int y1, int width, int height) const;

    bool	isFreeTile(int x, int y) const;
    bool	freeOnTop(const BWAPI::TilePosition & tile, BWAPI::UnitType buildingType) const;
    bool	freeOnRight(const BWAPI::TilePosition & tile, BWAPI::UnitType buildingType) const;
    bool	freeOnLeft(const BWAPI::TilePosition & tile, BWAPI::UnitType buildingType) const;
    bool	freeOnBottom(const BWAPI::TilePosition & tile, BWAPI::UnitType buildingType) const;
    bool	freeOnAllSides(BWAPI::Unit building) const;

    bool	canBuildHere(const BWAPI::TilePosition & position, const Building & b) const;
    bool	canBuildWithSpace(const BWAPI::TilePosition & position, const Building & b, int extraSpace) const;

    bool	groupTogether(BWAPI::UnitType type) const;

    BWAPI::TilePosition findEdgeLocation(const Building & b) const;
    BWAPI::TilePosition findPylonlessBaseLocation(const Building & b) const;
    BWAPI::TilePosition findGroupedLocation(const Building & b) const;
    BWAPI::TilePosition findSpecialLocation(const Building & b) const;
    BWAPI::TilePosition findAnyLocation(const Building & b, int extraSpace) const;

    int     countInRange(const BWAPI::Unitset & units, BWAPI::Position xy, int range) const;

public:

    BuildingPlacer();
    void initialize();

    // Return a build location near a building's desired location, with the given margin of space.
    BWAPI::TilePosition	getBuildLocationNear(const Building & b, int extraSpace) const;

    void				reserveTiles(const BWAPI::TilePosition & position, int width, int height);
    void				freeTiles(const BWAPI::TilePosition & position, int width, int height);
    void                freeTiles(const Building & b);
    bool				isReserved(int x, int y) const;
    bool                isReserved(const BWAPI::TilePosition & tile) const { return isReserved(tile.x, tile.y); };
    void				drawReservedTiles() const;

    bool                buildingOK(const Building & b) const;
    bool                buildingOK(const Building & b, const BWAPI::TilePosition & pos) const;

    BWAPI::TilePosition getExpoLocationTile(MacroLocation loc) const;
    BWAPI::TilePosition getMacroLocationTile(MacroLocation loc) const;
    BWAPI::Position     getMacroLocationPos(MacroLocation loc) const;
    BWAPI::TilePosition	getRefineryPosition() const;

    BWAPI::TilePosition getTerrainProxyPosition(const Base * base) const;
    BWAPI::TilePosition getInBaseProxyPosition(const Base * base) const;
    BWAPI::TilePosition getProxyPosition(const Base * base) const;

    BWAPI::TilePosition getAntiBunkerSunkenPosition(const Base * base, BWAPI::Unit bunker) const;
    BWAPI::TilePosition getAntiCannonSunkenPosition(const Base * base, BWAPI::Unit cannon) const;
};
}