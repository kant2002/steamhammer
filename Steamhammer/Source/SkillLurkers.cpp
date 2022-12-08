#include "SkillLurkers.h"

#include "CombatCommander.h"
#include "OpponentModel.h"
#include "ProductionManager.h"
#include "Random.h"
#include "The.h"

using namespace UAlbertaBot;

// -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- --

bool SkillLurkers::tooFewLurkers() const
{
    int enough = the.enemyRace() == BWAPI::Races::Terran ? 4 : 2;

    return the.my.completed.count(BWAPI::UnitTypes::Zerg_Lurker) < enough;
}

// -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- --

SkillLurkers::SkillLurkers()
    : Skill("lurkers")
    , _generalTactic(LurkerTactic::Aggressive)
{
}

// TODO
std::string SkillLurkers::putData() const
{
    std::stringstream s;

    return s.str();
}

// TODO
void SkillLurkers::getData(GameRecord & record, const std::string & line)
{
    std::stringstream s(line);
}

bool SkillLurkers::enabled() const
{
    return the.selfRace() == BWAPI::Races::Zerg;
}

// OLD PLAN:
// In the middle game, follow the steps:
// When there are too few lurkers, DefendBases.
// When there get to be enough, WithSquad.
// When that starts to fail, fall back on Contain.
// Reset to DefendBases whenever lurker count falls to zero.
// In the opening:
// Start at WithSquad.


void SkillLurkers::update()
{
    _generalTactic = LurkerTactic::WithSquad;
    CombatCommander::Instance().setGeneralLurkerTactic(_generalTactic);
    return;

    if (!feasible())
    {
        // No lurkers.
        _generalTactic = LurkerTactic::DefendBases;
        return;
    }
    
    /*
    if (!_initialized)
    {
        analyzeRecords();
        _initialized = true;
    }
    */

    if (tooFewLurkers())
    {
        _generalTactic = LurkerTactic::DefendBases;
    }

    if (_generalTactic == LurkerTactic::DefendBases && !ProductionManager::Instance().isOutOfBook())
    {
        _generalTactic = LurkerTactic::WithSquad;
    }
    else if (_generalTactic == LurkerTactic::DefendBases && true)   // TODO
    {
        _generalTactic = LurkerTactic::WithSquad;
    }
    else
    {
        _generalTactic = LurkerTactic::Contain;
    }

    CombatCommander::Instance().setGeneralLurkerTactic(_generalTactic);
    return;

    // Update lurker kills and tactic kills, a measure of the tactic's effectiveness.
    for (BWAPI::Unit u : the.self()->getUnits())
    {
        if (u->getType() == BWAPI::UnitTypes::Zerg_Lurker)
        {
            // get lurker kills before update

            auto it = lurkers.find(u);
            if (it == lurkers.end())
            {
                lurkers[u] = LurkerRecord(LurkerTactic::Aggressive, u->getKillCount(), the.now());
            }
            else
            {
                lurkers[u].kills = u->getKillCount();
            }

            // net kills = new kills - old kills
            // tactic[lurkerRecord.tactic] += net kills
            // also track tactic duration in lurker cycles
        }
    }

    _nextUpdateFrame = the.now() + 2;
}

bool SkillLurkers::feasible() const
{
    return the.my.completed.count(BWAPI::UnitTypes::Zerg_Lurker) > 0;
}

bool SkillLurkers::good() const
{
   return true;
}

void SkillLurkers::execute()
{
}

void SkillLurkers::draw() const
{
    if (!Config::Debug::DrawLurkerTactics || !feasible())
    {
        return;
    }

    int x = 200;
    int y = 50;

    BWAPI::Broodwar->drawTextScreen(x, y, "%cLurker tactic %c%s", green, yellow, LurkerTacticString(_generalTactic).c_str());
}