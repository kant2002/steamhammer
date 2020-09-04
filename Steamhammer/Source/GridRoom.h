#pragma once

#include "GridWalk.h"

// The accessible room, measured in walk tiles, around each walk tile.

namespace UAlbertaBot
{
class GridRoom : public GridWalk
{
public:
	GridRoom();

	void initialize();
	
	void draw() const;
};
}