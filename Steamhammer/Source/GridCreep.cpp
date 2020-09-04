#include "GridCreep.h"

using namespace UAlbertaBot;

void GridCreep::addKnownCreep()
{

}

// -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- --

GridCreep::GridCreep()
	: Grid(BWAPI::Broodwar->mapWidth(), BWAPI::Broodwar->mapHeight(), 0)
{
    addKnownCreep();
}

void GridCreep::update()
{
}
