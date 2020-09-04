#include "ResourceInfo.h"

using namespace UAlbertaBot;

// Is the location of a mineral patch visible?
// Used only when !initialUnit->isVisible() .
bool ResourceInfo::isMineralVisible() const
{
    BWAPI::TilePosition xy = initialUnit->getInitialTilePosition();    // top left corner

    // Mineral patch is 2x1 in size. Hand-unroll the loop.
    return
        BWAPI::Broodwar->isVisible(xy) &&
        BWAPI::Broodwar->isVisible(xy + BWAPI::TilePosition(1, 0));
}

// Is the location of a gas geyser visible?
// Used only when !currentUnit->isVisible() .
bool ResourceInfo::isGasVisible() const
{
    BWAPI::TilePosition xy = initialUnit->getInitialTilePosition();    // top left corner

    // Geyser is 4x2 in size. Hand-unroll the loop.
    return
        BWAPI::Broodwar->isVisible(xy) &&
        BWAPI::Broodwar->isVisible(xy + BWAPI::TilePosition(1, 0)) &&
        BWAPI::Broodwar->isVisible(xy + BWAPI::TilePosition(2, 0)) &&
        BWAPI::Broodwar->isVisible(xy + BWAPI::TilePosition(3, 0)) &&

        BWAPI::Broodwar->isVisible(xy + BWAPI::TilePosition(0, 1)) &&
        BWAPI::Broodwar->isVisible(xy + BWAPI::TilePosition(1, 1)) &&
        BWAPI::Broodwar->isVisible(xy + BWAPI::TilePosition(2, 1)) &&
        BWAPI::Broodwar->isVisible(xy + BWAPI::TilePosition(3, 1));
}

// For gas geysers only. The associated unit changes when a refinery is destroyed.
// Return it if the gas amount is accessible.
BWAPI::Unit ResourceInfo::accessibleGasUnit()
{
    if (!currentGasUnit || amount == 0)
    {
        // The geyser is exhausted and the amount will never change again.
        // Call it inaccessible so that we remember when it went to zero.
        currentGasUnit = nullptr;
        return nullptr;
    }

    if (currentGasUnit->isVisible())
    {
        // We can see gas in a bare geyser or our own refinery building, not an enemy refinery.
        if (currentGasUnit->getType() == BWAPI::UnitTypes::Resource_Vespene_Geyser ||
            currentGasUnit->getPlayer() == BWAPI::Broodwar->self())
        {
            return currentGasUnit;
        }
        return nullptr;
    }

    if (isGasVisible())
    {
        // This branch comes into effect after a refinery building is destroyed.
        // The unit changes, and we have to find the new unit at the location.
        for (BWAPI::Unit u : BWAPI::Broodwar->getUnitsOnTile(initialUnit->getInitialTilePosition()))
        {
            // We can see gas in a bare geyser or our own refinery building, not an enemy refinery.
            if (u->getType() == BWAPI::UnitTypes::Resource_Vespene_Geyser ||
                u->getType().isRefinery() && u->getPlayer() == BWAPI::Broodwar->self())
            {
                currentGasUnit = u;
                return u;
            }
        }
    }

    return nullptr;
}

void ResourceInfo::update(int a)
{
    amount = a;
    updateFrame = BWAPI::Broodwar->getFrameCount();
}

// -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- --

ResourceInfo::ResourceInfo()
{
}

ResourceInfo::ResourceInfo(BWAPI::Unit u)
    : initialUnit(u)
    , currentGasUnit(u)
    , amount(u->getInitialResources())
    , updateFrame(0)
    , destroyed(false)
{
}

// NOTE A blocking mineral patch may have amount zero from the start.
//      A mined out mineral patch is "destroyed" and no longer blocks paths.
void ResourceInfo::updateMinerals()
{
    if (!destroyed)
    {
        if (initialUnit->isVisible())
        {
            update(initialUnit->getResources());
        }
        else if (isMineralVisible())
        {
            update(0);
            destroyed = true;
        }
    }
}

void ResourceInfo::updateGas()
{
    BWAPI::Unit u = accessibleGasUnit();
    if (u)
    {
        update(u->getResources());
    }
}

// Initially used only in the debug display.
bool ResourceInfo::isAccessible() const
{
    if (isMineralPatch())
    {
        return amount > 0 && (initialUnit->isVisible() || isMineralVisible());
    }
    return currentGasUnit && currentGasUnit->isVisible() && currentGasUnit->getPlayer() != BWAPI::Broodwar->enemy();
}
