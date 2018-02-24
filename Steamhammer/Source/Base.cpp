#include "Common.h"
#include "Base.h"

using namespace UAlbertaBot;

// Should be called at the beginning of the game and never again.
// It picks out the geysers under that assumption.
// NOTE It's theoretically possible for a geyser to belong to more than one base.
//      That should not happen on a competitive map, though.
Base::Base(BWAPI::Position pos)
	: position(pos)
	, resourceDepot(nullptr)
	, owner(BWAPI::Broodwar->neutral())
	, reserved(false)
{
	findGeysers();
}

void Base::findGeysers()
{
	for (auto unit : BWAPI::Broodwar->getNeutralUnits())
	{
		if ((unit->getType() == BWAPI::UnitTypes::Resource_Vespene_Geyser || unit->getType().isRefinery()) &&
			unit->getPosition().isValid() &&
			unit->getDistance(position) < 400)
		{
			geysers.insert(unit);
		}
	}
}

// The depot may be null. (That's why player is a separate argument, not depot->getPlayer().)
// A null depot for an owned base means that the base is inferred and hasn't been seen.
void Base::setOwner(BWAPI::Unit depot, BWAPI::Player player)
{
	resourceDepot = depot;
	owner = player;
	reserved = false;
}