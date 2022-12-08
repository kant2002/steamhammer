\
#include "The.h"

#include "Bases.h"
#include "InformationManager.h"
#include "MapGrid.h"
#include "OpeningTiming.h"
#include "ParseUtils.h"
#include "ProductionManager.h"
#include "Random.h"
#include "StaticDefense.h"

using namespace UAlbertaBot;

The::The()
    : bases(Bases::Instance())
    , grid(MapGrid::Instance())
    , info(InformationManager::Instance())
    , openingTiming(OpeningTiming::Instance())
    , production(ProductionManager::Instance())
    , random(Random::Instance())
    , staticDefense(*new StaticDefense)
{
}

// -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- --

// NOTE The skill kit is initialized in OpponentModel and updated in GameCommander.

void The::initialize()
{
    _selfRace = BWAPI::Broodwar->self()->getRace();

    // The order of initialization is important because of dependencies.
    partitions.initialize();
    inset.initialize();				// depends on partitions
    vWalkRoom.initialize();			// depends on edgeRange
    tileRoom.initialize();			// depends on vWalkRoom
    zone.initialize();				// depends on tileRoom
    map.initialize();

    bases.initialize();             // depends on map
    info.initialize();              // depends on bases
    placer.initialize();
    ops.initialize();

    // Read the opening timing file, needed to make opening decisions.
    // openingTiming.read();

    // Parse the bot's configuration file.
    // Change this file path (in config.cpp) to point to your config file.
    // Any relative path name will be relative to Starcraft installation folder
    // The config depends on the map and must be read after the map is analyzed.
    // This also reads the opponent model data and decides on the opening.
    ParseUtils::ParseConfigFile(Config::ConfigFile::ConfigFileLocation);

    // Sets the initial queue to the book opening chosen above.
    production.initialize();
}

int The::attacks(BWAPI::Unit unit, const BWAPI::TilePosition & tile) const
{
    return unit->isFlying()
        ? airAttacks.at(tile)
        : groundAttacks.at(tile);
}

int The::attacks(BWAPI::Unit unit) const
{
    return unit->isFlying()
        ? airAttacks.at(unit->getTilePosition())
        : groundAttacks.at(unit->getTilePosition());
}

void The::update()
{
    my.completed.takeSelf();
    my.all.takeSelfAll();
    your.seen.takeEnemy();
    your.ever.takeEnemyEver(your.seen);
    your.inferred.takeEnemyInferred(your.ever);

    if (now() > 45 * 24 && now() % 10 == 0)
    {
        groundAttacks.update();
        airAttacks.update();
    }

    ops.update();
    staticDefense.update();
}
