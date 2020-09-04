#pragma once

#include "Skill.h"
#include <BWAPI.h>

namespace UAlbertaBot
{

class SkillUnitTimings : public Skill
{
private:
    std::map<BWAPI::UnitType, int> timings;
    std::vector< std::map<BWAPI::UnitType, int> > pastTimings;

public:

    SkillUnitTimings();

    std::string putData() const;
    void getData(const std::string & line);

    bool enabled() const   { return true;  };
    bool feasible() const  { return false; };
    bool good() const      { return false; };
    void execute()         { };
    void update();

    const std::map<BWAPI::UnitType, int> &                getTimings()     const { return timings;     };
    const std::vector< std::map<BWAPI::UnitType, int> > & getPastTimings() const { return pastTimings; };
};

}