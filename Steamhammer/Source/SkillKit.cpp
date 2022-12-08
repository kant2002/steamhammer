#include "SkillKit.h"

#include "SkillBattles.h"
#include "SkillGasSteal.h"
#include "SkillLurkers.h"
#include "SkillOpeningTiming.h"
#include "SkillUnitTimings.h"
#include "The.h"

using namespace UAlbertaBot;

// Usage: addSkill(new WhateverSkill).
void SkillKit::addSkill(Skill * skill)
{
    if (skill->enabled())
    {
        skills.push_back(skill);
    }
    else
    {
        delete skill;
    }
}

// -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- --

// This happens before skill data is read, necessarily.
void SkillKit::initialize()
{
    addSkill(new SkillBattles);
    addSkill(new SkillGasSteal);
    addSkill(new SkillLurkers);
    addSkill(new SkillOpeningTiming);
    addSkill(new SkillUnitTimings);
}

// Called from GameRecord, where skill data is persisted.
// skill: <data depending on the skill>
void SkillKit::read(GameRecord & r, const std::string & line)
{
    size_t i = line.find(':');
    if (i == std::string::npos || i == 0)
    {
        // Not found, or the skill name is empty. This is not a valid skill string.
        // Nothing can be done about it, so just return.
        return;
    }
    std::string name(line, 0, i);
    Skill * skill = getSkill(name);
    if (skill)
    {
        std::string value(line, i+1, std::string::npos);
        skill->getData(r, value);
    }
    // If we didn't find the skill by name, it may be disabled or even deleted. That's fine.
}

// Called from GameRecordNow, where skill data for the current game is persisted.
void SkillKit::write(std::ostream & out)
{
    for (Skill * skill : skills)
    {
        std::string data = skill->putData();
        if (data != "")
        {
            out << skill->getName() << ':' << ' ' << data << '\n';
        }
    }
}

void SkillKit::update()
{
    for (Skill * skill : skills)
    {
        if (skill->nextUpdate() <= the.now())
        {
            skill->update();
            if (skill->feasible() && skill->good())
            {
                skill->execute();
            }
        }
    }
}

// Draw debugging info. Each skill decides for itself whether to draw its info.
void SkillKit::draw() const
{
    for (Skill * skill : skills)
    {
        skill->draw();
    }
}

Skill * SkillKit::getSkill(const std::string & name) const
{
    for (Skill * skill : skills)
    {
        if (skill->getName() == name)
        {
            return skill;
        }
    }

    return nullptr;
}
