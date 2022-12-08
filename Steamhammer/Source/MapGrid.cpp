#include "MapGrid.h"

#include "Bases.h"
#include "The.h"

using namespace UAlbertaBot;

// MapGrid divides the map into large square cells (cells at the right or bottom may be cut off
// by the edge of the map). It keeps track of friendly and visible enemy units in each cell,
// updated each frame, as well as when the cell was last seen and whether there is a comsat scan
// active there.
// Also the last time an enemy was seen in the cell, but that is unused so far.

MapGrid & MapGrid::Instance() 
{
    static MapGrid instance(BWAPI::Broodwar->mapWidth()*32, BWAPI::Broodwar->mapHeight()*32, Config::Tools::MAP_GRID_SIZE);
    return instance;
}

MapGrid::MapGrid()
{
}

MapGrid::MapGrid(int mapWidth, int mapHeight, int cellSize) 
    : mapWidth(mapWidth)
    , mapHeight(mapHeight)
    , cellSize(cellSize)
    , cols((mapWidth + cellSize - 1) / cellSize)
    , rows((mapHeight + cellSize - 1) / cellSize)
    , cells(rows * cols)
    , lastUpdated(0)
{
    calculateCellCenters();
}

// Return the first of:
// 1. Any starting base location that has not been explored.
// 2. The least-recently explored accessible cell (with attention to byGround).
// In either case, choose the cell farthest from our start.
// Item 1 ensures that, if early game scouting failed, we scout with force.
// If byGround, only locations that are accessible by ground (in the given partition).
// If not byGround, the partition is not used.
BWAPI::Position MapGrid::getLeastExplored(bool byGround, int partition) 
{
    // 1. Any starting location that has not been explored.
    for (BWAPI::TilePosition tile : BWAPI::Broodwar->getStartLocations())
    {
        if (!BWAPI::Broodwar->isExplored(tile) &&
            (!byGround || partition == the.partitions.id(tile)))
        {
            return BWAPI::Position(tile);
        }
    }

    // 2. The most distant of the least-recently explored tiles.
    int minSeen = MAX_FRAME;
    double minSeenDist = 0;
    int leastRow(0), leastCol(0);

    for (int r=0; r<rows; ++r)
    {
        for (int c=0; c<cols; ++c)
        {
            BWAPI::Position cellCenter = getCellCenter(r,c);

            // Skip places that we can't get to.
            if (byGround && partition != the.partitions.id(cellCenter))
            {
                continue;
            }

            BWAPI::Position home(the.self()->getStartLocation());
            double dist = home.getDistance(getCellByIndex(r, c).center);
            int lastVisited = getCellByIndex(r, c).timeLastVisited;
            if (lastVisited < minSeen || (lastVisited == minSeen && dist > minSeenDist))
            {
                leastRow = r;
                leastCol = c;
                minSeen = lastVisited;
                minSeenDist = dist;
            }
        }
    }

    return getCellCenter(leastRow, leastCol);
}

// Return the least-recently explored cell "near" the given position.
// This one does not care about starting positions or distance from home.
BWAPI::Position MapGrid::getLeastExploredNear(const BWAPI::Position & center, bool byGround)
{
    int minSeen = MAX_FRAME;
    int leastRow(0), leastCol(0);

    for (int r = 0; r<rows; ++r)
    {
        for (int c = 0; c<cols; ++c)
        {
            BWAPI::Position cellCenter = getCellCenter(r, c);

            // Skip places that we can't get to.
            if (byGround && the.partitions.id(center) != the.partitions.id(cellCenter))
            {
                continue;
            }

            if (center.getApproxDistance(cellCenter) <= 32 * 32 && the.zone.at(center) == the.zone.at(cellCenter))
            {
                int lastVisited = getCellByIndex(r, c).timeLastVisited;
                if (lastVisited < minSeen)
                {
                    leastRow = r;
                    leastCol = c;
                    minSeen = lastVisited;
                }
            }
        }
    }

    return getCellCenter(leastRow, leastCol);
}

// Return the least-recently explored cell "near" the given base, and its natural if any.
BWAPI::Position MapGrid::getLeastExploredNearBase(Base * base, bool byGround)
{
    if (!base->getNatural())
    {
        // No natural. Use the plain method.
        return getLeastExploredNear(base->getPosition(), byGround);
    }

    // Look at the base and its natural.
    BWAPI::Position center1 = base->getPosition();
    BWAPI::Position center2 = base->getNatural()->getPosition();

    int minSeen = MAX_FRAME;
    int leastRow(0), leastCol(0);

    for (int r = 0; r<rows; ++r)
    {
        for (int c = 0; c<cols; ++c)
        {
            BWAPI::Position cellCenter = getCellCenter(r, c);

            // Skip places that we can't get to.
            if (byGround &&
                the.partitions.id(center1) != the.partitions.id(cellCenter) &&
                the.partitions.id(center2) != the.partitions.id(cellCenter))
            {
                continue;
            }

            if (center1.getApproxDistance(cellCenter) <= 32 * 32 && the.zone.at(center1) == the.zone.at(cellCenter) ||
                center2.getApproxDistance(cellCenter) <= 24 * 32 && the.zone.at(center2) == the.zone.at(cellCenter))
            {
                int lastVisited = getCellByIndex(r, c).timeLastVisited;
                if (lastVisited < minSeen)
                {
                    leastRow = r;
                    leastCol = c;
                    minSeen = lastVisited;
                }
            }
        }
    }

    return getCellCenter(leastRow, leastCol);
}

void MapGrid::calculateCellCenters()
{
    for (int r = 0; r < rows; ++r)
    {
        for (int c = 0; c < cols; ++c)
        {
            GridCell & cell = getCellByIndex(r, c);

            int centerX = (c * cellSize) + (cellSize / 2);
            int centerY = (r * cellSize) + (cellSize / 2);

            // if the x position goes past the end of the map
            if (centerX > mapWidth)
            {
                // when did the last cell start
                int lastCellStart = c * cellSize;

                // how wide did we go
                int tooWide = mapWidth - lastCellStart;

                // go half the distance between the last start and how wide we were
                centerX = lastCellStart + (tooWide / 2);
            }
            else if (centerX == mapWidth)
            {
                centerX -= 50;
            }

            if (centerY > mapHeight)
            {
                // when did the last cell start
                int lastCellStart = r * cellSize;

                // how wide did we go
                int tooHigh = mapHeight - lastCellStart;

                // go half the distance between the last start and how wide we were
                centerY = lastCellStart + (tooHigh / 2);
            }
            else if (centerY == mapHeight)
            {
                centerY -= 50;
            }

            cell.center = BWAPI::Position(centerX, centerY);
        }
    }
}

BWAPI::Position MapGrid::getCellCenter(int row, int col)
{
    return getCellByIndex(row, col).center;
}

// clear the vectors in the grid
void MapGrid::clearGrid() { 

    for (size_t i(0); i<cells.size(); ++i) 
    {
        cells[i].ourUnits.clear();
        cells[i].oppUnits.clear();
    }
}

// Populate the grid with units.
// Include all buildings, but other units only if they are completed.
// For the enemy, only include visible units (InformationManager remembers units which are out of sight).
// TODO a crash occurred in here once... somewhere
void MapGrid::update() 
{
    if (Config::Debug::DrawMapGrid) 
    {
        for (int i=0; i<cols; i++) 
        {
            BWAPI::Broodwar->drawLineMap(i*cellSize, 0, i*cellSize, mapHeight, BWAPI::Colors::Blue);
        }

        for (int j=0; j<rows; j++) 
        {
            BWAPI::Broodwar->drawLineMap(0, j*cellSize, mapWidth, j*cellSize, BWAPI::Colors::Blue);
        }

        for (int r=0; r < rows; ++r)
        {
            for (int c=0; c < cols; ++c)
            {
                GridCell & cell = getCellByIndex(r,c);
            
                BWAPI::Broodwar->drawTextMap(cell.center.x, cell.center.y, "Last Seen %d", cell.timeLastVisited);
                BWAPI::Broodwar->drawTextMap(cell.center.x, cell.center.y+10, "Row/Col (%d, %d)", r, c);
            }
        }
    }

    clearGrid();

    //BWAPI::Broodwar->printf("MapGrid info: WH(%d, %d)  CS(%d)  RC(%d, %d)  C(%d)", mapWidth, mapHeight, cellSize, rows, cols, cells.size());

    for (BWAPI::Unit unit : the.self()->getUnits()) 
    {
        if (unit->isCompleted() || unit->getType().isBuilding())
        {
            getCell(unit).ourUnits.insert(unit);
            getCell(unit).timeLastVisited = BWAPI::Broodwar->getFrameCount();
        }
    }

    for (BWAPI::Unit unit : BWAPI::Broodwar->enemy()->getUnits()) 
    {
        if ((unit->isCompleted() || unit->getType().isBuilding()) &&
            unit->getHitPoints() > 0 &&
            unit->getType() != BWAPI::UnitTypes::Unknown) 
        {
            getCell(unit).oppUnits.insert(unit);
            getCell(unit).timeLastOpponentSeen = BWAPI::Broodwar->getFrameCount();
        }
    }
}

// Return the set of units in the given circle.
void MapGrid::getUnits(BWAPI::Unitset & units, BWAPI::Position center, int radius, bool ourUnits, bool oppUnits)
{
    const int x0(std::max( (center.x - radius) / cellSize, 0));
    const int x1(std::min( (center.x + radius) / cellSize, cols-1));
    const int y0(std::max( (center.y - radius) / cellSize, 0));
    const int y1(std::min( (center.y + radius) / cellSize, rows-1));
    const int radiusSq(radius * radius);

    for(int y(y0); y<=y1; ++y)
    {
        for(int x(x0); x<=x1; ++x)
        {
            int row = y;
            int col = x;

            const GridCell & cell(getCellByIndex(row,col));
            if(ourUnits)
            {
                for (BWAPI::Unit unit : cell.ourUnits)
                {
                    BWAPI::Position d(unit->getPosition() - center);
                    if(d.x * d.x + d.y * d.y <= radiusSq)
                    {
                        if (!units.contains(unit)) 
                        {
                            units.insert(unit);
                        }
                    }
                }
            }
            if(oppUnits)
            {
                for (BWAPI::Unit unit : cell.oppUnits) if (unit->getType() != BWAPI::UnitTypes::Unknown)
                {
                    BWAPI::Position d(unit->getPosition() - center);
                    if(d.x * d.x + d.y * d.y <= radiusSq)
                    {
                        if (!units.contains(unit))
                        { 
                            units.insert(unit); 
                        }
                    }
                }
            }
        }
    }
}

// We scanned the given position. Record it so we don't scan the same position
// again before it wears off.
void MapGrid::scanAtPosition(const BWAPI::Position & pos)
{
    GridCell & cell = getCell(pos);
    cell.timeLastScan = BWAPI::Broodwar->getFrameCount();
}

// Is a comsat scan already active at the given position?
// This implementation is a rough appromimation; it only checks whether an ongoing scan
// is in the same grid cell as the given position.
bool MapGrid::scanIsActiveAt(const BWAPI::Position & pos)
{
    const GridCell & cell = getCell(pos);

    return cell.timeLastScan + GridCell::ScanDuration > BWAPI::Broodwar->getFrameCount();
}
