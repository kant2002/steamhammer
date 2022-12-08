#pragma once

#include <BWAPI.h>
#include <map>

namespace UAlbertaBot
{

// Info recorded at the end of the opening.
class OpeningTimingRecord
{
public:
    OpeningTimingRecord();

    int lastFrame;          // first frame out of book
    int nWorkers;           // workers alive then
    int armyMineralCost;    // of all combat units produced
    int armyGasCost;        // of all combat units produced
    int minerals;           // unspent resources
    int gas;
    int production1;        // count barracks, gateway, hatchery/lair/hive
    int production2;        // count factory, robo facility, 0
    int production3;        // count starport, stargate, 0
    int bases;              // number of bases, including incomplete bases

    // item -> completed at frame
    std::map<BWAPI::UnitType, int> techBuildingsDone;
    std::map<BWAPI::UpgradeType, int> upgradesDone;
    std::map<BWAPI::TechType, int> researchDone;
};

}