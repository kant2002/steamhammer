#pragma once

#include "Common.h"

namespace UAlbertaBot
{
class PlayerSnapshot
{
protected:
    bool excludeType(BWAPI::UnitType type) const;

    void reset(BWAPI::Player side);
    void inferUnseenRequirements(const PlayerSnapshot & ever, BWAPI::UnitType t);

public:
    BWAPI::Player player;
    int numBases;
    std::map<BWAPI::UnitType, int> unitCounts;

    const std::map<BWAPI::UnitType, int> & getCounts() const { return unitCounts; };

    PlayerSnapshot();
    PlayerSnapshot(BWAPI::Player);
    PlayerSnapshot(const BWAPI::Unitset & units);

    void takeSelf();
    void takeSelfAll();
    void takeEnemy();
    void takeEnemyEver(const PlayerSnapshot & seen);
    int initialEverTypeCount() const;
    void takeEnemyInferred(const PlayerSnapshot & ever);

    int count(BWAPI::UnitType type) const;
    int countWorkers() const;
    int getSupply() const;

    std::string debugString() const;
};

}