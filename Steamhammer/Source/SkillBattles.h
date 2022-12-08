#pragma once

#include "Skill.h"

#include <BWAPI.h>
#include "PlayerSnapshot.h"

namespace UAlbertaBot
{

class SkillBattles : public Skill
{
private:
    // Enemies, time, and location of a battle.
    struct BattleRecord
    {
        BattleRecord();

        int battleStartFrame;
        int distanceFromMe;     // in tiles from start
        int distanceFromYou;    // in tiles, -1 if enemy not found
        PlayerSnapshot enemies;

        void render(std::stringstream & s) const;
    };

    bool gotOne;
    int nBattles;
    std::vector<BattleRecord> battles;

public:

    SkillBattles();

    std::string putData() const;
    void getData(GameRecord & r, const std::string & line);

    bool enabled() const   { return true;  };
    bool feasible() const  { return false; };
    bool good() const      { return false; };
    void execute()         { };
    void update();
};

}