#include "Base.h"

#include "InformationManager.h"
#include "WorkerManager.h"

using namespace UAlbertaBot;

// Is the base one of the map's starting bases?
// The only purpose of this method is to initialize the startingBase flag.
// NOTE This depends on tilePosition, so the startingBase flag must be declared after tilePosition.
bool Base::findIsStartingBase() const
{
    for (BWAPI::TilePosition tile : BWAPI::Broodwar->getStartLocations())
    {
        if (tile == tilePosition)
        {
            return true;
        }
    }
    return false;
}

// -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- --

// Create a base given its position and a set of resources that may belong to it.
// The caller is responsible for eliminating resources which are too small to be worth it.
Base::Base(BWAPI::TilePosition pos, const BWAPI::Unitset availableResources)
    : id(-1)        // invalid value, will be reset after bases are sorted
    , tilePosition(pos)
    , distances(pos)
    , reserved(false)
    , startingBase(findIsStartingBase())
    , natural(nullptr)
    , workerDanger(false)
    , failedPlacements(0)
    , resourceDepot(nullptr)
    , owner(BWAPI::Broodwar->neutral())
{
    GridDistances resourceDistances(pos, BaseResourceRange, false);

    for (BWAPI::Unit resource : availableResources)
    {
        if (resource->getInitialTilePosition().isValid() && resourceDistances.getStaticUnitDistance(resource) >= 0)
        {
            if (resource->getInitialType().isMineralField())
            {
                minerals.insert(resource);
            }
            else if (resource->getInitialType() == BWAPI::UnitTypes::Resource_Vespene_Geyser)
            {
                initialGeysers.insert(resource);
            }
        }
    }
    geysers = initialGeysers;

    // Fill in the set of blockers, destructible neutral units that are very close to the base
    // and may interfere with its operation.
    // This does not include the minerals to mine!
    for (BWAPI::Unit unit : BWAPI::Broodwar->getStaticNeutralUnits())
    {
        // NOTE Khaydarin crystals are not destructible, and I don't know any way
        //      to find that out other than to check the name explicitly. Is there a way?
        if (!unit->getInitialType().canMove() &&
            !unit->isInvincible() &&
            unit->isTargetable() &&
            !unit->isFlying() &&
            unit->getInitialType().getName().find("Khaydarin") == std::string::npos)
        {
            int dist = resourceDistances.getStaticUnitDistance(unit);
            if (dist >= 0 && dist <= 9)
            {
                blockers.insert(unit);
            }
        }
    }
}

// This is to be called exactly once at startup time.
// With the initial -1 value set in the constructor, the checks here prevent multiple calls.
void Base::setID(int baseID)
{
    UAB_ASSERT(id == -1, "BUG! base ID reset");
    UAB_ASSERT(baseID >= 1, "BUG! bad base ID");
    id = baseID;
}

// The closest non-starting base to this base is its natural.
// This is only called (by Bases) if this is a starting base. Other bases have no natural.
// NOTE Some maps have mains with two naturals. This may give unhelpful results for them.
void Base::initializeNatural(const std::vector<Base *> & bases)
{
    int minDist = INT_MAX;
    for (Base * base : bases)
    {
        if (!base->isAStartingBase())
        {
            int dist = base->getTileDistance(tilePosition);     // -1 if not connected by ground
            if (dist > 0 && dist < minDist)
            {
                minDist = dist;
                natural = base;
            }
        }
    }
}

// The base is on an island, unconnected by ground to any starting base.
bool Base::isIsland() const
{
    for (const BWAPI::TilePosition & tile : BWAPI::Broodwar->getStartLocations())
    {
        if (tile != getTilePosition() && getTileDistance(tile) > 0)
        {
            return false;
        }
    }

    return true;
}

// Recalculate the base's set of geysers, including refineries (completed or not).
// This only works for visible geysers, so it should be called only for bases we own.
// Called to work around BWAPI behavior (maybe not strictly a bug).
void Base::updateGeysers()
{
    geysers.clear();

	for (BWAPI::Unit unit : BWAPI::Broodwar->getAllUnits())
	{
		if ((unit->getType() == BWAPI::UnitTypes::Resource_Vespene_Geyser || unit->getType().isRefinery()) &&
			unit->getPosition().isValid() &&
			unit->getDistance(getCenter()) < 320)
		{
			geysers.insert(unit);
		}
	}
}

// Return a tile near the center of the resource depot location. No tile is at the exact center.
const BWAPI::TilePosition Base::getCenterTile() const
{
    return tilePosition + BWAPI::TilePosition(1, 1);
}

// Return the center of the resource depot location=.
const BWAPI::Position Base::getCenter() const
{
    return BWAPI::Position(tilePosition) + BWAPI::Position(64, 48);
}

// The depot may be null. (That's why player is a separate argument, not depot->getPlayer().)
// A null depot for an owned base means that the base is inferred and hasn't been seen.
void Base::setOwner(BWAPI::Unit depot, BWAPI::Player player)
{
	resourceDepot = depot;
	owner = player;
	reserved = false;
}

// The resource depot of this base has not been seen, but we think it's enemy owned.
void Base::setInferredEnemyBase()
{
	if (owner == BWAPI::Broodwar->neutral())
	{
		resourceDepot = nullptr;
		owner = BWAPI::Broodwar->enemy();
		reserved = false;
	}
}

// The remaining minerals at the base, as of last report.
// For bases we own, the result is up to date.
int Base::getLastKnownMinerals() const
{
    int total = 0;

    for (BWAPI::Unit min : minerals)
    {
        total += InformationManager::Instance().getResourceAmount(min);
    }

    return total;
}

// The remaining gas at the base, as of last report.
// For bases we own, the result is up to date.
int Base::getLastKnownGas() const
{
    int total = 0;

    for (BWAPI::Unit gas : initialGeysers)
    {
        total += InformationManager::Instance().getResourceAmount(gas);
    }

    return total;
}

int Base::getInitialMinerals() const
{
	int total = 0;
	for (const BWAPI::Unit min : minerals)
	{
		total += min->getInitialResources();
	}
	return total;
}

int Base::getInitialGas() const
{
	int total = 0;
	for (const BWAPI::Unit gas : initialGeysers)
	{
		total += gas->getInitialResources();
	}
	return total;
}

// How many workers to saturate the base?
// Two per mineral patch plus three per geyser.
// NOTE This doesn't account for mineral patches mining out, decreasing the maximum.
int Base::getMaxWorkers() const
{
	return 2 * minerals.size() + Config::Macro::WorkersPerRefinery * geysers.size();
}

// How many workers are acually assigned?
int Base::getNumWorkers() const
{
	// The number of assigned mineral workers.
	int nWorkers = WorkerManager::Instance().getNumWorkers(resourceDepot);

	// Add the assigned gas workers.
	for (BWAPI::Unit geyser : geysers)
	{
		nWorkers += WorkerManager::Instance().getNumWorkers(geyser);
	}

	return nWorkers;
}

// The mean offset of the base's mineral patches from the center of the resource depot.
// This is used to tell what direction the minerals are in.
BWAPI::Position Base::getMineralOffset() const
{
	BWAPI::Position center = getCenter();
	BWAPI::Position offset = BWAPI::Positions::Origin;
	for (BWAPI::Unit mineral : getMinerals())
	{
		offset += mineral->getInitialPosition() - center;
	}
	return BWAPI::Position(offset / getMinerals().size());
}

// The "front" of the base, where static defense should go.
// For now, we use a simplified calculation that is only silly half the time.
BWAPI::Position Base::getFrontPoint() const
{
	return (getCenter() - getMineralOffset()).makeValid();
}

// Return a pylon near the nexus, if any. Let's call 256 pixels "near".
// This only makes a difference for protoss, of course.
// NOTE The pylon may not be complete!
BWAPI::Unit Base::getPylon() const
{
	return BWAPI::Broodwar->getClosestUnit(
		getCenter(),
		BWAPI::Filter::IsOwned && BWAPI::Filter::GetType == BWAPI::UnitTypes::Protoss_Pylon,
		256);
}

// We're scouting in the early game. Have we seen whether the enemy base is here?
// To turn the scout around as early as possible if the base is empty, we check
// each corner of the resource depot spot.
bool Base::isExplored() const
{
	return
		BWAPI::Broodwar->isExplored(tilePosition) ||
		BWAPI::Broodwar->isExplored(tilePosition + BWAPI::TilePosition(3, 2)) ||
		BWAPI::Broodwar->isExplored(tilePosition + BWAPI::TilePosition(0, 2)) ||
		BWAPI::Broodwar->isExplored(tilePosition + BWAPI::TilePosition(3, 0));
}

// Should we be able to see the resource depot at this base?
// Yes if we can see any corner of its position.
// This is for checking whether an expected enemy base is missing.
bool Base::isVisible() const
{
	return
		BWAPI::Broodwar->isVisible(tilePosition) ||
		BWAPI::Broodwar->isVisible(tilePosition + BWAPI::TilePosition(3, 2)) ||
		BWAPI::Broodwar->isVisible(tilePosition + BWAPI::TilePosition(0, 2)) ||
		BWAPI::Broodwar->isVisible(tilePosition + BWAPI::TilePosition(3, 0));
}

void Base::clearBlocker(BWAPI::Unit blocker)
{
	blockers.erase(blocker);
}

void Base::drawBaseInfo() const
{
	BWAPI::Position offset(-16, -6);
	for (BWAPI::Unit min : minerals)
	{
		BWAPI::Broodwar->drawTextMap(min->getInitialPosition() + offset, "%c%d", cyan, id);
		// BWAPI::Broodwar->drawTextMap(min->getInitialPosition() + BWAPI::Position(-18, 4), "%c%d", yellow, d.getStaticUnitDistance(min));
	}
	for (BWAPI::Unit gas : geysers)
	{
		BWAPI::Broodwar->drawTextMap(gas->getInitialPosition() + offset, "%cgas %d", cyan, id);
		// BWAPI::Broodwar->drawTextMap(gas->getInitialPosition() + BWAPI::Position(-18, 4), "%cgas %d", yellow, d.getStaticUnitDistance(gas));
	}
	for (BWAPI::Unit blocker : blockers)
	{
		BWAPI::Position pos = blocker->getInitialPosition();
		BWAPI::UnitType type = blocker->getInitialType();
		BWAPI::Broodwar->drawBoxMap(
			pos - BWAPI::Position(type.dimensionLeft(), type.dimensionUp()),
			pos + BWAPI::Position(type.dimensionRight(), type.dimensionDown()),
			BWAPI::Colors::Red);
	}

	BWAPI::Broodwar->drawBoxMap(
		BWAPI::Position(tilePosition),
		BWAPI::Position(tilePosition + BWAPI::TilePosition(4, 3)),
		BWAPI::Colors::Cyan, false);

	int dy = 40;
	BWAPI::Broodwar->drawTextMap(BWAPI::Position(tilePosition) + BWAPI::Position(40, dy),
		"%c%d @ (%d,%d)",
		cyan, id, tilePosition.x, tilePosition.y);

	if (owner != BWAPI::Broodwar->neutral())
	{
		dy += 12;
		char color = green;
		std::string ownerString = "mine";
		if (owner != BWAPI::Broodwar->self())
		{
			color = orange;
			ownerString = "yours";
		}
		BWAPI::Broodwar->drawTextMap(BWAPI::Position(tilePosition) + BWAPI::Position(40, dy),
			"%c%s", color, ownerString.c_str());
	}

	if (blockers.size() > 0)
	{
		dy += 12;
		BWAPI::Broodwar->drawTextMap(BWAPI::Position(tilePosition) + BWAPI::Position(40, dy),
			"%cblockers: %c%d",
			red, cyan, blockers.size());
	}
}
