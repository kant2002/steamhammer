#pragma once

#include "Grid.h"

// This is GridRoom reduced to 32x32 build tile resolution.
// It is intended for path computations because computing with it is much faster.

namespace UAlbertaBot
{
class GridTileRoom : public Grid
{
public:
    GridTileRoom();

    void initialize();
    
    void draw() const;
};
}