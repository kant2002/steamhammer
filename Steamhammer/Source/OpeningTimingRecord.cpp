#include "OpeningTimingRecord.h"

using namespace UAlbertaBot;

// Record info about the timings of our opening.
// See SkilOpeningTiming.

OpeningTimingRecord::OpeningTimingRecord()
    : lastFrame(0)
    , nWorkers(4)
    , armyMineralCost(0)
    , armyGasCost(0)
    , minerals(0)
    , gas(0)
    , production1(0)
    , production2(0)
    , production3(0)
    , bases(0)
{
}
