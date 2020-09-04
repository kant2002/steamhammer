#include "SkillOffSunk.h"

#include "Bases.h"
#include "Config.h"
#include "The.h"

using namespace UAlbertaBot;

// -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- --

SkillOffSunk::SkillOffSunk()
    : Skill("off sunk")
    , executeFrame(0)
    , successFrame(0)
    , lifetime(0)
{
}

// off sunk: <execute frame> <success frame> <lifetime>
std::string SkillOffSunk::getOutput() const
{
    std::stringstream s;

    s << executeFrame << ' ' << successFrame << ' ' << lifetime;
    return s.str();
}

void SkillOffSunk::parse(const std::string & line)
{
    std::stringstream s(line);

    int exec;
    int success;
    int life;
    if (s >> exec >> success >> life)
    {
        // TODO
    }
}

bool SkillOffSunk::enabled() const
{
    return
        the.selfRace() == BWAPI::Races::Zerg &&
        (the.enemyRace() == BWAPI::Races::Zerg || the.enemyRace() == BWAPI::Races::Unknown);
}

bool SkillOffSunk::feasible() const
{
    return
        Bases::Instance().enemyStart() != nullptr &&
        Bases::Instance().baseCount(the.enemy()) > 0;
}

void SkillOffSunk::execute()
{
    executeFrame = the.now();
}

void SkillOffSunk::update()
{
    if (executeFrame)
    {
        // TODO update successFrame and lifetime
    }
}
