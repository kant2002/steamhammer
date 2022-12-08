#pragma once

#include "MicroManager.h"

namespace UAlbertaBot
{
class Base;

class MicroIrradiated : public MicroManager
{

    bool enemyInLurkerRange(BWAPI::Unit lurker) const;
    BWAPI::Unit nearestEnemy(BWAPI::Unit unit) const;
    BWAPI::Unit friendNearby(BWAPI::Unit unit) const;

    void burrow(BWAPI::Unit unit);
    void runToEnemy(BWAPI::Unit unit, BWAPI::Unit enemy);
    void runAway(BWAPI::Unit unit, BWAPI::Unit friendly);

public:
    MicroIrradiated();
    void executeMicro(const BWAPI::Unitset & targets, const UnitCluster & cluster) {};

    void update();
};
};
