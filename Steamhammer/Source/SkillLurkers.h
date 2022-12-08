#pragma once

#include <BWAPI.h>

#include "Skill.h"
#include "TacticsOrders.h"

namespace UAlbertaBot
{
class SkillLurkers : public Skill
{
private:

    struct LurkerRecord
    {
        LurkerRecord()
            : tactic(LurkerTactic::Aggressive)
            , birth(0)
            , kills(0)
        {
        };

        LurkerRecord(LurkerTactic t, int b, int k)
            : tactic(t)
            , birth(b)
            , kills(k)
        {
        };

        LurkerTactic tactic;
        int birth;
        int kills;
    };

    LurkerTactic _generalTactic;

    std::map<BWAPI::Unit, LurkerRecord> lurkers;    // info about each lurker
    std::map<LurkerTactic, int> kills;              // how valuable has each tactic been?
    std::map<LurkerTactic, int> duration;           // how long has the tactic been in effect, in lurker-frames?

    bool tooFewLurkers() const;                     // not enough lurkers to use except in defense

public:
    SkillLurkers();

    std::string putData() const;
    void getData(GameRecord & record, const std::string & line);

    bool enabled() const;
    void initialize();
    void update();
    bool feasible() const;
    bool good() const;
    void execute();
    void draw() const;
};

}