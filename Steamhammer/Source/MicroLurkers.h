#pragma once

#include "Common.h"
#include "TacticsOrders.h"

namespace UAlbertaBot
{
    class MicroManager;

    class MicroLurkers : public MicroManager
    {
    private:

        int _tileSpacing;
        LurkerTactic _tactic;

        BWAPI::Unit getNearestTarget(BWAPI::Unit lurker, const BWAPI::Unitset & targets) const;
        BWAPI::Unit getFarthestTarget(BWAPI::Unit lurker, const BWAPI::Unitset & targets) const;

        bool dangerousEnemyInRange(BWAPI::Unit lurker) const;
        bool okToUnburrow(BWAPI::Unit lurker) const;
        bool safeToBurrow(const BWAPI::Unitset & lurkers, BWAPI::Unit lurker) const;
        bool correctlySpacedToBurrow(const BWAPI::Unitset & lurkers, BWAPI::Unit lurker) const;
        void retreatToSafety(BWAPI::Unit lurker);
        int closestBurrowedLurker(const BWAPI::Unitset & lurkers, const BWAPI::Position & xy) const;
        void moveToSpacedPosition(const BWAPI::Unitset & lurkers, BWAPI::Unit lurker);
        void seekToBurrow(const BWAPI::Unitset & lurkers, BWAPI::Unit lurker);

        int findTileSpacing() const;

    public:

        MicroLurkers();

        void setTactic(LurkerTactic tactic) { _tactic = tactic; };
        void executeMicro(const BWAPI::Unitset & targets, const UnitCluster & cluster);

        BWAPI::Unit getTarget(BWAPI::Unit lurker, const BWAPI::Unitset & targets);
        int getAttackPriority(BWAPI::Unit target) const;
    };
}