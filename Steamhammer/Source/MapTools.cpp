#include "MapTools.h"
#include "BuildingPlacer.h"
#include "InformationManager.h"

using namespace UAlbertaBot;

MapTools & MapTools::Instance()
{
    static MapTools instance;
    return instance;
}

MapTools::MapTools()
    : _rows(BWAPI::Broodwar->mapHeight())
    , _cols(BWAPI::Broodwar->mapWidth())
{
	UAB_ASSERT(_rows > 0 && _cols > 0, "empty map");

    _map    = std::vector<bool>(_rows*_cols,false);
    _fringe = std::vector<short int>(_rows*_cols,0);

    setBWAPIMapData();

	_hasIslandBases = false;
	for (BWTA::BaseLocation * base : BWTA::getBaseLocations())
	{
		if (base->isIsland())
		{
			_hasIslandBases = true;
			break;
		}
	}
}

// return the index of the 1D array from (row,col)
inline short MapTools::getIndex(int row,int col) const
{
    return row * _cols + col;
}

// Is the tile unexplored?
bool MapTools::unexplored(DistanceMap & dmap, short index) const
{
    return index != -1 &&            // index is valid
		dmap[index] == -1 &&         // tile doesn't have a known distance
		_map[index];                 // the map says it's walkable
}

// Read the map data from BWAPI and remember which 32x32 build tiles are walkable.
// NOTE The game map is walkable at the resolution of 8x8 walk tiles, so this is an approximation.
//      We're asking "Can big units walk here?" Small units may be able to squeeze into more places.
void MapTools::setBWAPIMapData()
{
	// 1. Check terrain: Is it walkable?
    for (int r(0); r < _rows; ++r)
    {
        for (int c(0); c < _cols; ++c)
        {
            bool walkable = true;

            // check each walk tile within this TilePosition
            for (int i=0; i<4 && walkable; ++i)
            {
                for (int j=0; j<4 && walkable; ++j)
                {
                    if (!BWAPI::Broodwar->isWalkable(c*4 + i,r*4 + j))
                    {
                        walkable = false;
                    }
                }
			}

            _map[getIndex(r,c)] = walkable;
        }
    }

	// 2. Check static units: Do they block tiles?
	for (const auto unit : BWAPI::Broodwar->getStaticNeutralUnits())
	{
		// The neutral units may include moving critters which do not permanently block tiles.
		// Something immobile blocks tiles it occupies until it is destroyed (exceptions may be possible).
		if (!unit->getType().canMove() && !unit->isFlying())
		{
			BWAPI::TilePosition pos = unit->getTilePosition();
			for (int r = pos.y; r < pos.y + unit->getType().tileHeight(); ++r)
			{
				for (int c = pos.x; c < pos.x + unit->getType().tileWidth(); ++c)
				{
					_map[getIndex(r, c)] = false;
				}
			}
		}
	}
}

void MapTools::resetFringe()
{
    std::fill(_fringe.begin(),_fringe.end(),0);
}

// Ground distance in tiles, -1 if no path exists.
int MapTools::getGroundTileDistance(BWAPI::Position origin, BWAPI::Position destination)
{
    // if we have too many maps, reset our stored maps in case we run out of memory
	if (_allMaps.size() > allMapsSize)
    {
        _allMaps.clear();

		if (Config::Debug::DrawMapDistances)
		{
			BWAPI::Broodwar->printf("Cleared distance map cache");
		}
    }

    // if we have already computed the distance map to the destination
	if (_allMaps.find(destination) != _allMaps.end())
	{
		return _allMaps[destination][origin];
	}

	// if we have computed the opposite direction, we can use that too
	if (_allMaps.find(origin) != _allMaps.end())
	{
		return _allMaps[origin][destination];
	}

	// add the map and compute it
	_allMaps.insert(std::pair<BWAPI::Position,DistanceMap>(destination,DistanceMap()));
	computeDistance(_allMaps[destination],destination);
	return _allMaps[destination][origin];
}

// Ground distance in pixels (with build tile granularity), -1 if no path exists.
// Build tile granularity means that the distance is a multiple of 32 pixels.
int MapTools::getGroundDistance(BWAPI::Position origin, BWAPI::Position destination)
{
	int tiles = getGroundTileDistance(origin, destination);
	if (tiles > 0)
	{
		return 32 * tiles;
	}
	return tiles;    // 0 or -1
}

// computes walk distance from Position P to all other points on the map
void MapTools::computeDistance(DistanceMap & dmap,const BWAPI::Position p)
{
    search(dmap,p.y / 32,p.x / 32);
}

// does the dynamic programming search
void MapTools::search(DistanceMap & dmap,const int sR,const int sC)
{
    // reset the internal variables
    resetFringe();

    // set the starting position for this search
    // dmap.setStartPosition(sR,sC);

    // set the distance of the start cell to zero
    dmap[getIndex(sR,sC)] = 0;

    // set the fringe variables accordingly
    int fringeSize(1);
    int fringeIndex(0);
    _fringe[0] = getIndex(sR,sC);
    dmap.addSorted(getTilePosition(_fringe[0]));

    // temporary variables used in search loop
    int currentIndex,nextIndex;
    int newDist;

    // the size of the map
    int size = _rows*_cols;

    // while we still have things left to expand
    while (fringeIndex < fringeSize)
    {
        // grab the current index to expand from the fringe
        currentIndex = _fringe[fringeIndex++];
        newDist = dmap[currentIndex] + 1;

        // search up
        nextIndex = (currentIndex > _cols) ? (currentIndex - _cols) : -1;
        if (unexplored(dmap,nextIndex))
        {
            // set the distance based on distance to current cell
            dmap.setDistance(nextIndex,newDist);
            dmap.addSorted(getTilePosition(nextIndex));

            // put it in the fringe
            _fringe[fringeSize++] = nextIndex;
        }

        // search down
        nextIndex = (currentIndex + _cols < size) ? (currentIndex + _cols) : -1;
        if (unexplored(dmap,nextIndex))
        {
            // set the distance based on distance to current cell
            dmap.setDistance(nextIndex,newDist);
            dmap.addSorted(getTilePosition(nextIndex));

            // put it in the fringe
            _fringe[fringeSize++] = nextIndex;
        }

        // search left
        nextIndex = (currentIndex % _cols > 0) ? (currentIndex - 1) : -1;
        if (unexplored(dmap,nextIndex))
        {
            // set the distance based on distance to current cell
            dmap.setDistance(nextIndex,newDist);
            dmap.addSorted(getTilePosition(nextIndex));

            // put it in the fringe
            _fringe[fringeSize++] = nextIndex;
        }

        // search right
        nextIndex = (currentIndex % _cols < _cols - 1) ? (currentIndex + 1) : -1;
        if (unexplored(dmap,nextIndex))
        {
            // set the distance based on distance to current cell
            dmap.setDistance(nextIndex,newDist);
            dmap.addSorted(getTilePosition(nextIndex));

            // put it in the fringe
            _fringe[fringeSize++] = nextIndex;
        }
    }
}

const std::vector<BWAPI::TilePosition> & MapTools::getClosestTilesTo(BWAPI::Position pos)
{
    // make sure the distance map is calculated with pos as a destination
//    int a = getGroundTileDistance(BWAPI::Position(BWAPI::Broodwar->self()->getStartLocation()),pos);
	int a = getGroundTileDistance(pos, pos);

    return _allMaps[pos].getSortedTiles();
}

BWAPI::TilePosition MapTools::getTilePosition(short index)
{
    return BWAPI::TilePosition(index % _cols,index / _cols);
}

void MapTools::drawHomeDistanceMap()
{
	if (!Config::Debug::DrawMapDistances)
	{
		return;
	}

    BWAPI::Position homePosition = BWAPI::Position(BWAPI::Broodwar->self()->getStartLocation());

    for (int x = 0; x < BWAPI::Broodwar->mapWidth(); ++x)
    {
        for (int y = 0; y < BWAPI::Broodwar->mapHeight(); ++y)
        {
            BWAPI::Position pos(x*32, y*32);

            int dist = getGroundTileDistance(pos, homePosition);

            BWAPI::Broodwar->drawTextMap(pos + BWAPI::Position(16,16), "%d", dist);
        }
    }
}

BWTA::BaseLocation * MapTools::nextExpansion(bool hidden, bool wantMinerals, bool wantGas)
{
	UAB_ASSERT(wantMinerals || wantGas, "unwanted expansion");

	// Abbreviations.
	BWAPI::Player player = BWAPI::Broodwar->self();
	BWAPI::Player enemy = BWAPI::Broodwar->enemy();

	// We'll go through the bases and pick the one with the best score.
	BWTA::BaseLocation * bestBase = nullptr;
	double bestScore = -999999.0;
	
	BWAPI::TilePosition homeTile = InformationManager::Instance().getMyMainBaseLocation()->getTilePosition();
	BWAPI::Position myBasePosition(homeTile);
	BWTA::BaseLocation * enemyBase = InformationManager::Instance().getEnemyMainBaseLocation();  // may be null

    for (BWTA::BaseLocation * base : BWTA::getBaseLocations())
    {
		double score = 0.0;

        // Do we demand a gas base?
		if (wantGas && (base->isMineralOnly() || base->gas() == 0))
		{
			continue;
		}

		// Do we demand a mineral base?
		// The constant is an arbitrary limit "enough minerals to be worth it".
		if (wantMinerals && base->minerals() < 500)
		{
			continue;
		}

		// Don't expand to an existing base.
		if (InformationManager::Instance().getBaseOwner(base) != BWAPI::Broodwar->neutral())
		{
			continue;
		}
        
		// Don't expand to a base already reserved for another expansion.
		if (InformationManager::Instance().isBaseReserved(base))
		{
			continue;
		}

		BWAPI::TilePosition tile = base->getTilePosition();
        bool buildingInTheWay = false;

        for (int x = 0; x < player->getRace().getCenter().tileWidth(); ++x)
        {
			for (int y = 0; y < player->getRace().getCenter().tileHeight(); ++y)
            {
				if (BuildingPlacer::Instance().isReserved(x,y))
				{
					// This happens if we were already planning to expand here. Try somewhere else.
					buildingInTheWay = true;
					break;
				}

				// TODO bug: this doesn't include enemy buildings which are known but out of sight
				for (const auto unit : BWAPI::Broodwar->getUnitsOnTile(BWAPI::TilePosition (tile.x + x, tile.y + y)))
                {
                    if (unit->getType().isBuilding() && !unit->isLifted())
                    {
                        buildingInTheWay = true;
                        break;
                    }
                }
            }
        }
            
        if (buildingInTheWay)
        {
            continue;
        }

        // Want to be close to our own base (unless this is to be a hidden base).
		double distanceFromUs = MapTools::Instance().getGroundTileDistance(BWAPI::Position(tile), myBasePosition);

        // if it is not connected, continue
		if (distanceFromUs < 0)
        {
            continue;
        }

		// Want to be far from the enemy base.
		double distanceFromEnemy = 0.0;
		if (enemyBase) {
			BWAPI::TilePosition enemyTile = enemyBase->getTilePosition();
			distanceFromEnemy = MapTools::Instance().getGroundTileDistance(BWAPI::Position(tile), BWAPI::Position(enemyTile));
			if (distanceFromEnemy < 0)        // no ground connection between the locations
			{
				distanceFromEnemy = 0.0;
			}
		}

		// Add up the score.
		score = hidden ? (distanceFromEnemy + distanceFromUs / 2.0) : (distanceFromEnemy / 2.0 - distanceFromUs);

		// More resources -> better.
		if (wantMinerals)
		{
			score += 0.01 * base->minerals();
		}
		if (wantGas)
		{
			score += 0.02 * base->gas();
		}
		// Big penalty for enemy buildings in the same region.
		if (InformationManager::Instance().isEnemyBuildingInRegion(base->getRegion()))
		{
			score -= 100.0;
		}

		// BWAPI::Broodwar->printf("base score %d, %d -> %f",  tile.x, tile.y, score);
		if (score > bestScore)
        {
            bestBase = base;
			bestScore = score;
		}
    }

    if (bestBase)
    {
        return bestBase;
	}
	if (wantMinerals && wantGas)
	{
		// We wanted a gas base and there isn't one. Try for a mineral-only base.
		return nextExpansion(hidden, true, false);
	}
	return nullptr;
}

BWAPI::TilePosition MapTools::getNextExpansion(bool hidden, bool wantMinerals, bool wantGas)
{
	BWTA::BaseLocation * base = nextExpansion(hidden, wantMinerals, wantGas);
	if (base)
	{
		// BWAPI::Broodwar->printf("foresee base @ %d, %d", base->getTilePosition().x, base->getTilePosition().y);
		return base->getTilePosition();
	}
	return BWAPI::TilePositions::None;
}

BWAPI::TilePosition MapTools::reserveNextExpansion(bool hidden, bool wantMinerals, bool wantGas)
{
	BWTA::BaseLocation * base = nextExpansion(hidden, wantMinerals, wantGas);
	if (base)
	{
		// BWAPI::Broodwar->printf("reserve base @ %d, %d", base->getTilePosition().x, base->getTilePosition().y);
		InformationManager::Instance().reserveBase(base);
		return base->getTilePosition();
	}
	return BWAPI::TilePositions::None;
}
