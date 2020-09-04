#pragma once

#include "Grid.h"

// On this tile grid, 0 means unbuildable.
// A positive value k means a string of k tiles to the right is buildable, starting here.

namespace UAlbertaBot
{
class GridBuildable : public Grid
{
public:
    GridBuildable();

    void compute();
};
}
