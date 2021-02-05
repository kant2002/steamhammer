#include "MicroOverlords.h"

#include "Bases.h"
#include "InformationManager.h"
#include "MapGrid.h"
#include "The.h"

using namespace UAlbertaBot;

// The nearest overlord to the given tile.
BWAPI::Unit MicroOverlords::nearestOverlord(const BWAPI::Unitset & overlords, const BWAPI::TilePosition & tile) const
{
    return NearestOf(TileCenter(tile), overlords);
}

// The nearest spore colony, if any.
BWAPI::Unit MicroOverlords::nearestSpore(BWAPI::Unit overlord) const
{
	BWAPI::Unit best = nullptr;
    int bestDistance = INT_MAX;

	for (BWAPI::Unit defense : the.info.getStaticDefense())
	{
		if (defense->exists() && defense->getType() == BWAPI::UnitTypes::Zerg_Spore_Colony)
		{
			int dist = overlord->getDistance(defense);
			if (dist < bestDistance)
			{
				best = defense;
				bestDistance = dist;
			}
		}
	}

	return best;
}

// Assign all overlords in the set to spore colonies.
void MicroOverlords::assignOverlordsToSpores(const BWAPI::Unitset & overlords)
{
    for (BWAPI::Unit overlord : overlords)
    {
        BWAPI::Unit spore = nearestSpore(overlord);
        if (!spore)
        {
            weHaveSpores = false;       // destroyed since last check
            return;
        }
        assignments[overlord] = spore->getTilePosition();
    }
}

// Assign overlords to target destinations.
// Precondition: Assignments are cleared.
void MicroOverlords::assignOverlords()
{
    std::vector<BWAPI::TilePosition> destinations;

    // Add destinations in priority order.
    if (the.now() < 7 * 24 * 60)
    {
        // Early in the game, explore for proxies in our starting base.
        destinations.push_back(BWAPI::TilePosition(the.grid.getLeastExploredNear(the.bases.myStart()->getPosition(), false)));
    }
    Base * enemyNatural = the.bases.enemyStart()
        ? the.bases.enemyStart()->getNatural()      // may be null
        : nullptr;
    if (enemyNatural && !mobileAntiAir)
    {
        // The enemy natural base, if safe, no matter whether the enemy has taken it or not.
        destinations.push_back(enemyNatural->getTilePosition());
    }
    if (the.bases.myNatural() && the.bases.myNatural()->getOwner() == the.self())
    {
        // By the time cloaked units can be out, we have enough overlords for this.
        destinations.push_back(the.bases.myNatural()->getTilePosition());
    }
    if (the.bases.myMain()->getOwner() == the.self())
    {
        destinations.push_back(the.bases.myMain()->getTilePosition());
    }
    if (the.bases.myStart()->getOwner() == the.self() && the.bases.myStart() != the.bases.myMain())
    {
        destinations.push_back(the.bases.myStart()->getTilePosition());
    }
    // Other bases we own.
    for (Base * base : the.bases.getAll())
    {
        if (base->getOwner() == the.self() && base != the.bases.myMain() && base != the.bases.myStart() && base != the.bases.myNatural())
        {
            destinations.push_back(base->getTilePosition());
        }
    }
    if (!overlordHunters && the.info.enemyHasTransport())
    {
        // Keep an eye out for drops.
        Base * base = the.bases.myStart()->getOwner() == the.self() ? the.bases.myStart() : the.bases.myMain();
        destinations.push_back(BWAPI::TilePosition(the.grid.getLeastExploredNear(base->getPosition(), false)));
    }
    // If there are no dangers, near other bases as possible.
    if (!mobileAntiAir)
    {
        for (Base * base : the.bases.getAll())
        {
            if (base->getOwner() != the.self() && base != enemyNatural)
            {
                destinations.push_back(base->getTilePosition());
            }
        }
    }

    // Assign one overlord to each destination.
    BWAPI::Unitset unassigned = getUnits();
    for (BWAPI::TilePosition & dest : destinations)
    {
        if (dest.isValid())     // just in case
        {
            BWAPI::Unit overlord = nearestOverlord(unassigned, dest);
            if (!overlord)
            {
                // We assigned all of them.
                return;
            }
            assignments[overlord] = dest;
            unassigned.erase(overlord);
        }
    }

    // Assign any remaining overlords to default destinations.
    if (overlordHunters && weHaveSpores)
    {
        assignOverlordsToSpores(unassigned);
    }
    else
    {
        // Otherwise send them all to the front base.
        for (BWAPI::Unit overlord : unassigned)
        {
            assignments[overlord] = the.bases.frontTile();
        }
    }
}

MicroOverlords::MicroOverlords()
    : overlordHunters(false)
    , mobileAntiAir(false)
    , weHaveSpores(false)
{
}

// 0. If there are no overlords, do nothing.
// 1. Make assignments (skip many frames).
// 2. Move overlords (skip few frames).
void MicroOverlords::update()
{
    // 0. If there are no overlords, do nothing.
    if (getUnits().empty())         // usually means we're not zerg
    {
        return;
    }

    // 1. Redo assignments at a low rate.
    if (assignments.size() != getUnits().size() || the.now() % 32 == 0)
    {
        assignments.clear();

        // NOTE Could also use the opponent model to predict these values.
        overlordHunters = the.info.enemyHasOverlordHunters();
        mobileAntiAir = overlordHunters || the.info.enemyHasAntiAir();
        weHaveSpores = nearestSpore(*getUnits().begin()) != nullptr;
        const bool cloakedEnemies = the.info.enemyHasMobileCloakTech();
        const bool overlordSpeed = the.self()->getUpgradeLevel(BWAPI::UpgradeTypes::Pneumatized_Carapace) > 0;

        if (overlordHunters && weHaveSpores && (!cloakedEnemies || !overlordSpeed))
        {
            // Send all overlords to safety, if possible.
            assignOverlordsToSpores(getUnits());
        }
        else
        {
            assignOverlords();
        }

        // Every overlord we were given should have an assignment.
        UAB_ASSERT(assignments.size() == getUnits().size(), "bad assignments");
    }

    // 2. Move overlords often, so that they can avoid dangers in time.
    if (BWAPI::Broodwar->getFrameCount() % 4 == 0)
	{
        for (auto it = assignments.begin(); it != assignments.end(); )
        {
            BWAPI::Unit overlord = it->first;
            BWAPI::Position destination = BWAPI::Position(it->second);
            // The overlord may have died or been mind controlled.
            if (overlord->exists() && overlord->getPlayer() == the.self())
            {
                if (!destination.isValid())
                {
                    destination = BWAPI::Positions::Origin;     // likely hidden corner
                }
                if (overlord->getDistance(destination) <= 16)
                {
                    the.micro.Stop(overlord);
                    //BWAPI::Broodwar->drawCircleMap(destination, 4, BWAPI::Colors::Green);
                }
                else
                {
                    the.micro.MoveSafely(overlord, destination);
                    //BWAPI::Broodwar->drawCircleMap(destination, 4, BWAPI::Colors::Yellow);
                    //BWAPI::Broodwar->drawLineMap(overlord->getPosition(), destination, BWAPI::Colors::Grey);
                }
                ++it;
            }
            else
            {
                it = assignments.erase(it);
            }
        }
    }
}
