#include "SkillBattles.h"

#include "Bases.h"
#include "GameRecord.h"
#include "The.h"

using namespace UAlbertaBot;

// Record the time and unit counts of the earliest two "significant" battles.

// Record this many battles.
static const int MaxBattles = 2;

// The next "significant battle" is one with enemy units taking this much supply or more.
// Supply 8 is 4 marines, 2 zealots, etc.
static const int BattleSupplyThreshold[MaxBattles] = { 8, 20 };

// But we don't record the significant battle as soon as it has occurred.
// We wait one update cycle in case it grows larger as more units come into view.
static const int UpdateCycle = 3 * 24 + 1;

// -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- --

SkillBattles::BattleRecord::BattleRecord()
{
    battleStartFrame = the.combatSim.getBiggestBattleFrame();

    BWAPI::Position pos = the.combatSim.getBiggestBattleCenter();
    distanceFromMe = the.bases.myStart()->getTileDistance(pos);
    if (the.bases.enemyStart())
    {
        distanceFromYou = the.bases.enemyStart()->getTileDistance(pos);
    }
    else
    {
        distanceFromYou = -1;
    }

    enemies = the.combatSim.getBiggestBattleEnemies();
}

void SkillBattles::BattleRecord::render(std::stringstream & s) const
{
    s << battleStartFrame << ' ' << distanceFromMe << ' ' << distanceFromYou;

    for (const std::pair<BWAPI::UnitType, int> & unitCount : enemies.getCounts())
    {
        s << ' ' << int(unitCount.first) << ' ' << unitCount.second;
    }
}

// -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- --

SkillBattles::SkillBattles()
    : Skill("battles")
    , gotOne(false)
    , nBattles(0)
{
    // We expect no battle earlier than this.
    _nextUpdateFrame = 25 * 24;
}

// Use -1 as an end mark after each battle.
std::string SkillBattles::putData() const
{
    std::stringstream s;

    std::string separator = "";
    for (const BattleRecord & battle : battles)
    {
        s << separator;
        separator = " ";
        battle.render(s);
        s << " -1";
    }

    return s.str();
}

// Transfer recorded battles into the given game record. -1 ends each battle record.
void SkillBattles::getData(GameRecord & r, const std::string & line)
{
    std::stringstream s(line);

    int i = 0;
    while (1)
    {
        // battleStartFrame distanceFromMe distanceFromYou
        int b1, b2, b3;
        if (!(s >> b1 >> b2 >> b3))
        {
            return;
        }
        std::vector<int> timeAndPlace = { b1, b2, b3 };
        r.setSkillInfo(this, i, timeAndPlace);
        ++i;

        // The snapshot of enemy unit types and counts.
        std::vector<int> snapshot;
        int type;
        int count;
        while ((s >> type) && type != -1)
        {
            s >> count;
            snapshot.push_back(type);
            snapshot.push_back(count);
        }
        r.setSkillInfo(this, i, snapshot);
        ++i;
    }
}

void SkillBattles::update()
{
    if (gotOne)
    {
        battles.push_back(BattleRecord());
        gotOne = false;
        ++nBattles;
    }
    else if (the.combatSim.getBiggestBattleEnemies().getSupply() >= BattleSupplyThreshold[nBattles])
    {
        gotOne = true;
    }

    if (nBattles >= MaxBattles)
    {
        _nextUpdateFrame = INT_MAX;     // never run again
        return;
    }

    _nextUpdateFrame = the.now() + UpdateCycle;
}
