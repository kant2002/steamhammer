#pragma once

#include "MicroManager.h"

namespace UAlbertaBot
{
class Base;

class MicroOverlords : public MicroManager
{
    bool overlordHunters;                   // enemy will fly around to shoot us down?
    bool mobileAntiAir;                     // enemy has units that shoot air
    bool weHaveSpores;                      // do we have defensive spore colonies?

    std::map<BWAPI::Unit, BWAPI::TilePosition> assignments;  // overlord -> location

    BWAPI::Unit nearestOverlord(const BWAPI::Unitset & overlords, const BWAPI::TilePosition & tile) const;
	BWAPI::Unit nearestSpore(BWAPI::Unit overlord) const;
    void assignOverlordsToSpores(const BWAPI::Unitset & overlords);
	void assignOverlords();

public:
	MicroOverlords();
	void executeMicro(const BWAPI::Unitset & targets, const UnitCluster & cluster) {};

	void update();
};
};
