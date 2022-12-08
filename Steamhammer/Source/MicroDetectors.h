#pragma once

namespace UAlbertaBot
{
class MicroManager;
class Squad;

class MicroDetectors : public MicroManager
{

    int squadSize;
    BWAPI::Unit unitClosestToTarget;

public:

    MicroDetectors();
    ~MicroDetectors() {}

    void setSquadSize(int n) { squadSize = n; };
    void setUnitClosestToTarget(BWAPI::Unit unit) { unitClosestToTarget = unit; }
    void executeMicro(const BWAPI::Unitset & targets, const UnitCluster & cluster);
    void go(const BWAPI::Unitset & squadUnits);
};
}