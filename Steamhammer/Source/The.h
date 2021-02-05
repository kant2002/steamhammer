#pragma once

#include "BuildingPlacer.h"
#include "GridAttacks.h"
#include "GridInset.h"
#include "GridRoom.h"
#include "GridTileRoom.h"
#include "GridZone.h"
#include "MapPartitions.h"
#include "MapTools.h"
#include "Micro.h"
#include "OpsBoss.h"
#include "PlayerSnapshot.h"
#include "SkillKit.h"

// Central singleton to provide access to many components.
#define the (The::Root())

namespace UAlbertaBot
{
    class Bases;
    class MapGrid;
    class InformationManager;
    class Random;

    struct My
    {
        PlayerSnapshot completed;
        PlayerSnapshot all;
    };

    struct Your
    {
        PlayerSnapshot seen;
        PlayerSnapshot ever;
        PlayerSnapshot inferred;
    };

    class The
	{
    private:
        BWAPI::Race _selfRace;

    public:
		The();
        // Initialize The. Call this once per game in onStart().
		void initialize();

		// Map information.

		GridRoom vWalkRoom;
        // How much room is there around this tile? A rough estimate of how much stuff fits there.
		GridTileRoom tileRoom;
        // How far is this walk tile from the nearest wall?
		GridInset inset;
        // What zone is this tile in?
		GridZone zone;
        // What map partition is this walk tile in? You can walk between places in the same partition.
		MapPartitions partitions;
        // Map information and calculations.
        MapTools map;

		// Managers.

        // Information about bases and resources.
        Bases & bases;
        // Large cells laid over the map.
        MapGrid & grid;
        // Game state information, especially stored information about the enemy.
        InformationManager & info;
        // Perform unit control actions ("unit micro").
		Micro micro;
        // Place buildings. Find macro locations.
        BuildingPlacer placer;
        // Operations boss. Unfinished.
		OpsBoss ops;
        // Extensible set of skills using the opponent model.
        SkillKit skillkit;

		// Varying during the game.

        // My current unit counts.
        My my;
        // Your current unit counts.
        Your your;
        // What tiles does enemy static defense hit on the ground?
		GroundAttacks groundAttacks;
        // What tiles does enemy static defense hit in the air?
        AirAttacks airAttacks;
        // What tiles does enemy static defense hit for this unit?
        int attacks(BWAPI::Unit unit, const BWAPI::TilePosition & tile) const;
        int attacks(BWAPI::Unit unit) const;

		// Update the varying values.
		void update();

        // Utility.

        // The player (this bot).
        BWAPI::Player self()      const { return BWAPI::Broodwar->self(); };
        // The enemy player.
        BWAPI::Player enemy()     const { return BWAPI::Broodwar->enemy(); };
        // The neutral player, which owns things like resources and critters.
        BWAPI::Player neutral()   const { return BWAPI::Broodwar->neutral(); };

        // The bot's race, terran protoss zerg.
        BWAPI::Race selfRace()    const { return _selfRace; };
        // The enemy's race, terran protoss zerg.
        BWAPI::Race enemyRace()   const { return BWAPI::Broodwar->enemy()->getRace(); };

        // Current frame count, same as Broodwar->getFrameCount().
        int now() const { return BWAPI::Broodwar->getFrameCount(); };

        // Generate random numbers.
        Random & random;

        static inline The & Root() {
            static The TheRoot;
            return TheRoot;
        };
    };
}
