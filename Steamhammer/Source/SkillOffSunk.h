#pragma once

#include "Skill.h"

namespace UAlbertaBot
{
class SkillOffSunk : public Skill
{
private:
    int executeFrame;
    int successFrame;
    int lifetime;

public:

    SkillOffSunk();

    std::string getOutput() const;
    void parse(const std::string & line);

    bool enabled() const;
    bool feasible() const;
    bool good() const;
    void execute();
    void update();
};

}