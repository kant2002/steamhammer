#pragma once

#include "BWAPI.h"
#include "Grid.h"

// TODO unfinished and unused

// Keep track of zerg creep across the map.
// 1. Scout enemy zerg bases more quickly--if we see unexplained new creep on a tile
// where it was formerly missing, it's enemy. (If it was formerly unseen, it may be static creep.)
// 2. Know where zerg can build beyond its own bases (locate static or enemy creep).

namespace UAlbertaBot
{
class GridCreep : public Grid
{

    // To run once at the start of the game.
    void addKnownCreep();

public:

    GridCreep();

    void update();
};

}