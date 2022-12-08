#pragma once

#include <BWAPI.h>
#include <map>
#include <set>

#include "OpeningTimingRecord.h"
#include "Skill.h"

namespace UAlbertaBot
{

class SkillOpeningTiming : public Skill
{
private:
    OpeningTimingRecord record;

    std::set<BWAPI::TechType> freeResearch;     // you get this at the start of the game

    // Separator between groups of data.
    static const int sentinel = -1;

    bool isTechBuildingType(BWAPI::UnitType type) const;

public:
    SkillOpeningTiming();

    std::string putData() const;
    void getData(GameRecord & r, const std::string & line);

    bool enabled() const   { return true;  };
    bool feasible() const  { return false; };
    bool good() const      { return false; };
    void execute()         { };
    void update();
};

}