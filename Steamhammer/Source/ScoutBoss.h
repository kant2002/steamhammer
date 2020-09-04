#pragma once

#include <BWAPI.h>

// TODO in progress, still unused

namespace UAlbertaBot
{
struct ScoutGoal
{
    BWAPI::Position position;
    int priority;
};

class ScoutBoss
{

private:
    std::set<ScoutGoal> check;
    std::set<ScoutGoal> monitor;

public:
    
    ScoutBoss();

    void update();

};

}