#pragma once

// Operations boss.
// NOTE Unfinished. Intended to replace CombatCommander.

#include <BWAPI.h>
#include "UnitData.h"

namespace UAlbertaBot
{
    struct UnitInfo;

    enum class ClusterStatus
    {
        None          // enemy cluster or not updated yet
        , Advance     // no enemy near, moving forward
        , Attack      // enemy nearby, attacking
        , Regroup     // regrouping (usually retreating)
        , FallBack    // returning to base
    };

    class UnitCluster
    {
    public:
        BWAPI::Position center;
        int radius;
        ClusterStatus status;

        bool air;               // air units
        double speed;           // minimum speed, ignoring static defense, pixels/frame

        size_t count;           // number of units
        int hp;                 // total HP + shields
        double groundDPF;		// DPF = damage per frame
        double airDPF;

        std::string extraText;  // for draw()

        BWAPI::Unitset units;   // not necessarily visible

        UnitCluster();

        void clear();
        void add(const UnitInfo & ui);
        size_t size() const { return count; };
        void setExtraText(const std::string & s) { extraText = s;};
        void draw(BWAPI::Color color, const std::string & label = "") const;
    };

    class OpsBoss
    {
        const int clusterStart = 5 * 32;
        const int clusterRange = 3 * 32;

        std::vector<UnitCluster> enemyClusters;

        int defenderUpdateFrame;
        std::vector<UnitCluster> groundDefenseClusters;
        std::vector<UnitCluster> airDefenseClusters;

        void locateCluster(const std::vector<BWAPI::Position> & points, UnitCluster & cluster);
        void formCluster(const UnitInfo & seed, const UIMap & theUI, BWAPI::Unitset & units, UnitCluster & cluster);
        void clusterUnits(BWAPI::Player player, BWAPI::Unitset & units, std::vector<UnitCluster> & clusters);

        void updateDefenders();

    public:
        OpsBoss();
        void initialize();

        void cluster(BWAPI::Player player, std::vector<UnitCluster> & clusters);
        void cluster(BWAPI::Player player, const BWAPI::Unitset & units, std::vector<UnitCluster> & clusters);

        void update();

        const std::vector<UnitCluster> & getGroundDefenseClusters();
        const std::vector<UnitCluster> & getAirDefenseClusters();

        const UnitCluster * getNearestEnemyClusterVs(const BWAPI::Position & pos, bool vsGround, bool vsAir);

        void drawClusters() const;
    };
}