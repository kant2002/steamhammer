#include "SkillKit.h"

#include "SkillGasSteal.h"
#include "SkillUnitTimings.h"
#include "The.h"

using namespace UAlbertaBot;

// -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- --

// This happens before skill data is read, necessarily.
void SkillKit::initialize()
{
    SkillGasSteal * gasSteal = new SkillGasSteal;
    if (gasSteal->enabled())
    {
        skills.push_back(gasSteal);
    }
    else
    {
        delete gasSteal;
    }

    SkillUnitTimings * unitTimings = new SkillUnitTimings;
    if (unitTimings->enabled())
    {
        skills.push_back(unitTimings);
    }
    else
    {
        delete unitTimings;
    }
}

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
