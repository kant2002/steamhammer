#include "GridBuildable.h"

#include "BuildingPlacer.h"
#include "MapTools.h"
#include "The.h"

using namespace UAlbertaBot;

GridBuildable::GridBuildable()
    : Grid(BWAPI::Broodwar->mapWidth(), BWAPI::Broodwar->mapHeight(), 0)
{
}

// 1. Call this once to initialize. It depends on MapTools, so after that.
// 2. Optionally, call again each time some tile's buildability changes.
void GridBuildable::compute()
{
    for (int y = BWAPI::Broodwar->mapHeight() - 1; y >= 0; --y)
    {
        int k = 0;
        for (int x = BWAPI::Broodwar->mapWidth() - 1; x >= 0; --x)
        {
            if (BWAPI::Broodwar->isBuildable(x, y, true) &&
                !the.placer.isReserved(x, y) &&
                the.map.isBuildable(BWAPI::TilePosition(x, y)))
            {
                ++k;
            }
            else
            {
                k = 0;
            }
            grid[x][y] = k;
        }
    }
}
