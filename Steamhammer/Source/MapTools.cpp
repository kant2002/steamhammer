#include "MapTools.h"

#include "Bases.h"
#include "BuildingPlacer.h"
#include "InformationManager.h"
#include "The.h"
#include "UnitUtil.h"

using namespace UAlbertaBot;

MapTools::MapTools()
{
}

void MapTools::initialize()
{
    // Figure out which tiles are walkable and buildable.
    setBWAPIMapData();
}

// Read the map data from BWAPI and remember which 32x32 build tiles are walkable.
// NOTE The game map is walkable at the resolution of 8x8 walk tiles, so this is an approximation.
//      We're asking "Can big units walk here?" Small units may be able to squeeze into more places.
void MapTools::setBWAPIMapData()
{
	// 1. Mark all tiles walkable and buildable at first.
	_terrainWalkable = std::vector< std::vector<bool> >(BWAPI::Broodwar->mapWidth(), std::vector<bool>(BWAPI::Broodwar->mapHeight(), true));
	_walkable = std::vector< std::vector<bool> >(BWAPI::Broodwar->mapWidth(), std::vector<bool>(BWAPI::Broodwar->mapHeight(), true));
	_buildable = std::vector< std::vector<bool> >(BWAPI::Broodwar->mapWidth(), std::vector<bool>(BWAPI::Broodwar->mapHeight(), true));
	_depotBuildable = std::vector< std::vector<bool> >(BWAPI::Broodwar->mapWidth(), std::vector<bool>(BWAPI::Broodwar->mapHeight(), true));

	// 2. Check terrain: Is it buildable? Is it walkable?
	// This sets _walkable and _terrainWalkable identically.
	for (int x = 0; x < BWAPI::Broodwar->mapWidth(); ++x)
	{
		for (int y = 0; y < BWAPI::Broodwar->mapHeight(); ++y)
		{
			// This initializes all cells of _buildable and _depotBuildable.
			bool buildable = BWAPI::Broodwar->isBuildable(BWAPI::TilePosition(x, y), false);
			_buildable[x][y] = buildable;
			_depotBuildable[x][y] = buildable;

			// Check each 8x8 walk tile within this 32x32 TilePosition.
			for (int i = 0; i < 4; ++i)
			{
				for (int j = 0; j < 4; ++j)
				{
					if (!BWAPI::Broodwar->isWalkable(x * 4 + i, y * 4 + j))
					{
						_terrainWalkable[x][y] = false;
						_walkable[x][y] = false;
                        goto tileExit;
					}
				}
			}
        tileExit:;
		}
	}

	// 3. Check neutral units: Do they block walkability?
	// This affects _walkable but not _terrainWalkable. We don't update buildability here.
	for (BWAPI::Unit unit : BWAPI::Broodwar->getStaticNeutralUnits())
	{
		// The neutral units may include moving critters which do not permanently block tiles.
		// Something immobile blocks tiles it occupies until it is destroyed. (Are there exceptions?)
		if (!unit->getType().canMove() && !unit->getType().isFlyer())
		{
			BWAPI::TilePosition pos = unit->getInitialTilePosition();
			for (int x = pos.x; x < pos.x + unit->getType().tileWidth(); ++x)
			{
				for (int y = pos.y; y < pos.y + unit->getType().tileHeight(); ++y)
				{
					if (BWAPI::TilePosition(x, y).isValid())   // assume it may be partly off the edge
					{
						_walkable[x][y] = false;
					}
				}
			}
		}
	}

	// 4. Check static resources: Do they block buildability?
	for (BWAPI::Unit resource : BWAPI::Broodwar->getStaticNeutralUnits())
	{
		if (!resource->getInitialType().isResourceContainer())
		{
			continue;
		}

		int tileX = resource->getInitialTilePosition().x;
        int tileY = resource->getInitialTilePosition().y;

		for (int x = tileX; x < tileX + resource->getType().tileWidth(); ++x)
		{
			for (int y = tileY; y < tileY + resource->getType().tileHeight(); ++y)
			{
				_buildable[x][y] = false;

				// A resource depot can't be built within 3 tiles of any resource.
				for (int dx = -3; dx <= 3; dx++)
				{
					for (int dy = -3; dy <= 3; dy++)
					{
						if (BWAPI::TilePosition(x + dx, y + dy).isValid())
						{
                            _depotBuildable[x + dx][y + dy] = false;
                        }
					}
				}
			}
		}
	}
}

// Ground distance in tiles, -1 if no path exists.
// This is Manhattan distance, not true walking distance. Still good for finding paths.
int MapTools::getGroundTileDistance(BWAPI::TilePosition origin, BWAPI::TilePosition destination)
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

    // Do we have a distance map to the destination?
	auto it = _allMaps.find(destination);
	if (it != _allMaps.end())
	{
		return (*it).second.at(origin);
	}

	// It's symmetrical. A distance map to the origin is just as good.
	it = _allMaps.find(origin);
	if (it != _allMaps.end())
	{
		return (*it).second.at(destination);
	}

	// Make a new map for this destination.
	_allMaps.insert(std::pair<BWAPI::TilePosition, GridDistances>(destination, GridDistances(destination)));
	return _allMaps[destination].at(origin);
}

int MapTools::getGroundTileDistance(BWAPI::Position origin, BWAPI::Position destination)
{
	return getGroundTileDistance(BWAPI::TilePosition(origin), BWAPI::TilePosition(destination));
}

// Ground distance in pixels (with TilePosition granularity), -1 if no path exists.
// TilePosition granularity means that the distance is a multiple of 32 pixels.
int MapTools::getGroundDistance(BWAPI::Position origin, BWAPI::Position destination)
{
	int tiles = getGroundTileDistance(origin, destination);
	if (tiles > 0)
	{
		return 32 * tiles;
	}
	return tiles;    // 0 or -1
}

const std::vector<BWAPI::TilePosition> & MapTools::getClosestTilesTo(BWAPI::TilePosition pos)
{
	// make sure the distance map is calculated with pos as a destination
	int a = getGroundTileDistance(pos, pos);

	return _allMaps[pos].getSortedTiles();
}

const std::vector<BWAPI::TilePosition> & MapTools::getClosestTilesTo(BWAPI::Position pos)
{
	return getClosestTilesTo(BWAPI::TilePosition(pos));
}

// TODO deprecated method
bool MapTools::isBuildable(BWAPI::TilePosition tile, BWAPI::UnitType type) const
{
	if (!tile.isValid())
	{
		return false;
	}

	int startX = tile.x;
	int endX = tile.x + type.tileWidth();
	int startY = tile.y;
	int endY = tile.y + type.tileHeight();

	for (int x = startX; x<endX; ++x)
	{
		for (int y = startY; y<endY; ++y)
		{
			BWAPI::TilePosition tile(x, y);

			if (!tile.isValid() || !isBuildable(tile) || type.isResourceDepot() && !isDepotBuildable(tile))
			{
				return false;
			}
		}
	}

	return true;
}

void MapTools::drawHomeDistances()
{
	if (Config::Debug::DrawMapDistances)
	{
        the.bases.myMain()->getDistances().draw();
	}
}

// Make the assumption that we are looking for a mineral-only base.
void MapTools::drawExpoScores()
{
    if (Config::Debug::DrawExpoScores)
    {
        (void)nextExpansion(false, true, false);
    }
}

Base * MapTools::nextExpansion(bool hidden, bool wantMinerals, bool wantGas) const
{
	UAB_ASSERT(wantMinerals || wantGas, "unwanted expansion");

	// Abbreviations.
	BWAPI::Player player = BWAPI::Broodwar->self();
	BWAPI::Player enemy = BWAPI::Broodwar->enemy();
    BWAPI::Position offset(-24, -20);

	// We'll go through the bases and pick the one with the best score.
	Base * bestBase = nullptr;
	double bestScore = -999999.0;
	
	BWAPI::TilePosition homeTile = the.bases.myStart()->getTilePosition();
	BWAPI::Position myBasePosition(homeTile);

    for (Base * base : the.bases.getAll())
    {
		// Don't expand to an existing base, or a reserved base.
		if (base->getOwner() != BWAPI::Broodwar->neutral() || base->isReserved())
		{
            if (Config::Debug::DrawExpoScores)
            {
                BWAPI::Broodwar->drawTextMap(base->getCenter()+offset, "%ctaken", red);
            }
			continue;
		}

		// Do we demand a gas base?
		if (wantGas && base->getInitialGas() == 0)
		{
            if (Config::Debug::DrawExpoScores)
            {
                BWAPI::Broodwar->drawTextMap(base->getCenter()+offset+offset, "%cno gas", red);
            }
            continue;
		}

        const int estimatedMinerals = base->getLastKnownMinerals();

		// Do we demand a mineral base?
		// The constant is an arbitrary limit "enough minerals to be worth it".
        if (wantMinerals && estimatedMinerals < 500)
		{
            if (Config::Debug::DrawExpoScores)
            {
                BWAPI::Broodwar->drawTextMap(base->getCenter()+offset, "%cno minerals", red);
            }
            continue;
		}

        // If this is to be a hidden base, and the enemy is not found yet, avoid mains and naturals.
        if (hidden && the.bases.enemyStart() == nullptr)
        {
            if (base->isAStartingBase() || base->getMain())
            {
                continue;
            }
        }

        BWAPI::TilePosition topLeft = base->getTilePosition();
        BWAPI::TilePosition bottomRight = topLeft + BWAPI::TilePosition(4, 3);

        // No good if the building location is known to be in range of enemy static defense.
        if (the.groundAttacks.inRange(player->getRace().getCenter(), topLeft))
        {
            if (Config::Debug::DrawExpoScores)
            {
                BWAPI::Broodwar->drawTextMap(base->getCenter()+offset, "%cendangered", red);
            }
            continue;
        }

        // No good if an enemy building or burrowed unit is in the way (even if out of sight).
        // No good if an enemy combat unit is nearby.
        bool somethingInTheWay = false;
        for (const auto & kv : InformationManager::Instance().getUnitData(BWAPI::Broodwar->enemy()).getUnits())
        {
            const UnitInfo & ui(kv.second);

            if (!ui.goneFromLastPosition)
            {
                if (ui.type.isBuilding() && !ui.lifted || ui.burrowed)
                {
                    BWAPI::TilePosition lastTilePos(ui.lastPosition);
                    if (lastTilePos.x >= topLeft.x &&
                        lastTilePos.x < bottomRight.x &&
                        lastTilePos.y >= topLeft.y &&
                        lastTilePos.y < bottomRight.y)
                    {
                        if (Config::Debug::DrawExpoScores)
                        {
                            BWAPI::Broodwar->drawTextMap(base->getCenter()+offset, "%cblocked (known)", red);
                        }
                        somethingInTheWay = true;
                        goto buildingLoopExit;
                    }
                }
                if (the.now() - ui.updateFrame < 60 * 24 &&
                    UnitUtil::TypeCanAttackGround(ui.type) &&
                    base->getCenter().getApproxDistance(ui.lastPosition) <= 7 * 32 &&
                    !ui.type.isWorker() &&
                    ui.type != BWAPI::UnitTypes::Terran_Vulture_Spider_Mine)
                {
                    if (Config::Debug::DrawExpoScores)
                    {
                        BWAPI::Broodwar->drawTextMap(base->getCenter() + offset, "%cguarded by enemy", red);
                    }
                    somethingInTheWay = true;
                    goto buildingLoopExit;

                }
            }
        }

        // No good if a visible building is in the way.
        for (int x = 0; x < player->getRace().getCenter().tileWidth(); ++x)
        {
			for (int y = 0; y < player->getRace().getCenter().tileHeight(); ++y)
            {
                if (the.placer.isReserved(topLeft.x + x, topLeft.y + y))
				{
                    if (Config::Debug::DrawExpoScores)
                    {
                        BWAPI::Broodwar->drawTextMap(base->getCenter()+offset, "%creserved tiles", yellow);
                    }
                    // This happens if we were already planning to expand here. Try somewhere else.
					somethingInTheWay = true;
                    goto buildingLoopExit;
				}

                // This knows about visible units only.
                for (BWAPI::Unit unit : BWAPI::Broodwar->getUnitsOnTile(BWAPI::TilePosition(topLeft.x + x, topLeft.y + y)))
                {
                    if (unit->getType().isBuilding() &&
                        !unit->isLifted())
                        // Our own buildings are OK if we can lift them out of the way.
                        // TODO This is waiting until the code to lift is written.
                        // !(unit->getPlayer() == BWAPI::Broodwar->self() && unit->canLift()))
                    {
                        if (Config::Debug::DrawExpoScores)
                        {
                            BWAPI::Broodwar->drawTextMap(base->getCenter()+offset, "%cblocked (tile)a", red);
                        }
                        somethingInTheWay = true;
                        goto buildingLoopExit;
                    }
                }
            }
        }
buildingLoopExit:
        if (somethingInTheWay)
        {
            continue;
        }

		double score = 0.0;

		// NOTE Ground distances are computed at tile resolution, which is coarser than the walking
		// resolution. When a map has narrow chokes, two connected points may appear unconnected
		// by ground, meaning their distance is returned as -1. Map partitions are computed at walk
		// resolution, and we use those to decide whether a base is reachable. That can also be wrong
		// if the widest path between the points is too narrow for a worker. But it works for most
		// maps.
		// Anyway, if we get ground distance -1, we check connectivity and switch to air distance
		// as a backup.

		// Want to be close to our own base (unless this is to be a hidden base).
        // Pixel distance.
        int distanceFromUs = the.bases.myStart()->getDistance(topLeft);

        // If it is not connected by ground, skip this potential base.
		if (distanceFromUs < 0)
        {
            if (the.partitions.id(topLeft) == the.partitions.id(myBasePosition))
			{
                // It looks to be connected by a narrow path, use air distance.
                distanceFromUs = myBasePosition.getApproxDistance(BWAPI::Position(topLeft));
			}
			else
			{
				continue;
			}
        }

		// Want to be far from the enemy base.
        Base * enemyBase = the.bases.enemyStart();  // may be null or no longer enemy-owned
        // Pixel distance.
        int distanceFromEnemy = 0;
		if (enemyBase) {
            distanceFromEnemy = enemyBase->getDistance(topLeft);
			if (distanceFromEnemy < 0)
			{
				// No ground distance found, so again substitute air distance.
                BWAPI::TilePosition enemyTile = enemyBase->getTilePosition();
                if (the.partitions.id(topLeft) == the.partitions.id(enemyTile))
				{
                    distanceFromEnemy = enemyTile.getApproxDistance(topLeft);
				}
				else
				{
					distanceFromEnemy = 0;
				}
			}
		}

		// Add up the score.
		score = hidden ? (distanceFromEnemy + distanceFromUs / 2) : (distanceFromEnemy / 2 - distanceFromUs);

		// Far from the edge of the map -> worse.
		// It's a proxy for "how wide open is this base?" Usually a base on the edge is
		// relatively sheltered and a base in the middle is more open (though not always).
        int edgeXdist = std::min(topLeft.x, BWAPI::Broodwar->mapWidth() - topLeft.x);
        int edgeYdist = std::min(topLeft.y, BWAPI::Broodwar->mapHeight() - topLeft.y);
		int edgeDistance = std::min(edgeXdist, edgeYdist);  // tile distance
        edgeDistance = std::max(0, edgeDistance - 10);      // within this distance is always OK
        if (edgeDistance > std::min(BWAPI::Broodwar->mapWidth(), BWAPI::Broodwar->mapHeight()) / 4)
        {
            // If we're very far from the edge, it's worse.
            edgeDistance *= 4;
        }
		score -= 15.0 * edgeDistance;

		// More resources -> better.
        // NOTE The number of mineral patchers/geysers controls how fast we can mine.
        //      The resource amount controls how long we can keep mining.
		if (wantMinerals)
		{
            score += 5.0 * base->getMinerals().size() + 0.005 * estimatedMinerals;
		}
		if (wantGas)
		{
            score += 20.0 * base->getGeysers().size() + 0.01 * base->getLastKnownGas();
		}
        else
        {
            // We didn't ask for gas, but it may be useful anyway.
            score += 5.0 * base->getGeysers().size() + 0.0025 * base->getLastKnownGas();
        }

		/* TODO on a flat map, all mains may be in the same zone
		// Big penalty for enemy buildings in the same region.
		if (InformationManager::Instance().isEnemyBuildingInRegion(base->getRegion()))
		{
			score -= 100.0;
		}
		*/

		// BWAPI::Broodwar->printf("base score %d, %d -> %f",  tile.x, tile.y, score);
        if (Config::Debug::DrawExpoScores)
        {
            BWAPI::Broodwar->drawTextMap(base->getCenter()+offset, "%c%g", green, score);
        }

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

BWAPI::TilePosition MapTools::getNextExpansion(bool hidden, bool wantMinerals, bool wantGas) const
{
	Base * base = nextExpansion(hidden, wantMinerals, wantGas);
	if (base)
	{
		// BWAPI::Broodwar->printf("foresee base @ %d, %d", base->getTilePosition().x, base->getTilePosition().y);
		return base->getTilePosition();
	}
	return BWAPI::TilePositions::None;
}

BWAPI::TilePosition MapTools::reserveNextExpansion(bool hidden, bool wantMinerals, bool wantGas)
{
	Base * base = nextExpansion(hidden, wantMinerals, wantGas);
	if (base)
	{
		// BWAPI::Broodwar->printf("reserve base @ %d, %d", base->getTilePosition().x, base->getTilePosition().y);
		base->reserve();
		return base->getTilePosition();
	}
	return BWAPI::TilePositions::None;
}
