#pragma once

#include "Skill.h"

#include <vector>

namespace UAlbertaBot
{
class GameRecord;

class SkillKit
{
private:
    std::vector<Skill *> skills;

public:

    void initialize();
    
    void read(GameRecord & r, const std::string & line);
    void write(std::ostream & out);

    void update();

    size_t nSkills() const { return skills.size(); };
    Skill * getSkill(const std::string & name) const;
};

}