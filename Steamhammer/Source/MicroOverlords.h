#pragma once

#include "MicroManager.h"

namespace UAlbertaBot
{
class Base;

class MicroOverlords : public MicroManager
{
    bool overlordHunterTech;                    // enemy can fly around to shoot us down?
    bool mobileAntiAirTech;                     // enemy has tech for units that shoot air
    bool mobileAntiAirUnits;                    // we've seen enemy anti-air units
    bool weHaveSpores;                          // do we have defensive spore colonies?

    std::map<BWAPI::Unit, BWAPI::TilePosition> assignments;  // overlord -> location

    bool enemyHasMobileAntiAirUnits() const;
    bool ourOverlord(BWAPI::Unit overlord) const;
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
