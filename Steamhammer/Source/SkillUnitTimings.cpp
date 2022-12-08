#include "SkillUnitTimings.h"

#include "InformationManager.h"
#include "The.h"

using namespace UAlbertaBot;

// Record the first time each enemy unit type is sighted, in frames.
// For unfinished building types, record the building's predicted completion time instead,
// marking it by negating the frame count.

// -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- --

SkillUnitTimings::SkillUnitTimings()
    : Skill("unit timings")
{
    // We should not see any enemy units before this. (Maybe on a tiny map.)
    _nextUpdateFrame = 20 * 24;
}

std::string SkillUnitTimings::putData() const
{
    std::stringstream s;

    for (const std::pair<BWAPI::UnitType, int> & typeTime : timings)
    {
        s << ' ' << int(typeTime.first) << ' ' << typeTime.second;
    }

    return s.str();
}

// (<type> <frame>)*
void SkillUnitTimings::getData(const std::string & line)
{
    std::map<BWAPI::UnitType, int> timingRecord;
    std::stringstream s(line);

    int typeIndex;
    int frame;
    while (s >> typeIndex >> frame)
    {
        timingRecord[BWAPI::UnitType(typeIndex)] = frame;
    }

    pastTimings.push_back(timingRecord);
}

// Positive frame values are scouting times.
// Negative frame values are building completion times, negated. Negation is just a flag.
void SkillUnitTimings::update()
{
    for (const auto & kv : the.info.getUnitData(the.enemy()).getUnits())
    {
        const UnitInfo & ui(kv.second);

        auto it = timings.find(ui.type);
        if (it == timings.end())
        {
            int frame = ui.updateFrame;
            if (ui.type.isBuilding() && !ui.completed)
            {
                frame = -ui.completeBy;
            }
            timings.insert(std::pair<BWAPI::UnitType, int>(ui.type, frame));
        }
    }

    _nextUpdateFrame = the.now() + 23;
}
