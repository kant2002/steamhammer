#pragma once

#include <BWAPI.h>

namespace UAlbertaBot
{
    // NOTE Add new tactics at the end of the enum to avoid invalidating saved game records.
    enum class LurkerTactic
    { First = 0
    , Aggressive = 0    // the classic behavior
    , WithSquad         // default: act as regular members of the ground squad
    , Runby
    , Contain
    , DefendBases
    , DefendRamp
    , EggBlock
    , DefendCenter
    , BlockBase
    , DenyBase
    , AmbushBase
    , Last = AmbushBase
    };

    const std::vector< std::pair<LurkerTactic, std::string> > LurkerTacticNames =
    {
        std::pair<LurkerTactic, std::string>(LurkerTactic::Aggressive, "Aggressive"),
        std::pair<LurkerTactic, std::string>(LurkerTactic::WithSquad, "With Squad"),
        std::pair<LurkerTactic, std::string>(LurkerTactic::Runby, "Runby"),
        std::pair<LurkerTactic, std::string>(LurkerTactic::Contain, "Contain"),
        std::pair<LurkerTactic, std::string>(LurkerTactic::DefendBases, "Defend Bases"),
        std::pair<LurkerTactic, std::string>(LurkerTactic::DefendRamp, "Defend Ramp"),
        std::pair<LurkerTactic, std::string>(LurkerTactic::EggBlock, "Egg Block"),
        std::pair<LurkerTactic, std::string>(LurkerTactic::DefendCenter, "Defend Center"),
        std::pair<LurkerTactic, std::string>(LurkerTactic::BlockBase, "Block Base"),
        std::pair<LurkerTactic, std::string>(LurkerTactic::DenyBase, "Deny Base"),
        std::pair<LurkerTactic, std::string>(LurkerTactic::AmbushBase, "Ambush Base"),
    };

    static std::string LurkerTacticString(LurkerTactic tactic)
    {
        for (auto it = LurkerTacticNames.begin(); it != LurkerTacticNames.end(); ++it)
        {
            if ((*it).first == tactic)
            {
                return (*it).second;
            }
        }

        return "Error";
    }

    // Decided by SkillLurkers.
    struct LurkerOrder
    {
        LurkerTactic tactic;
        bool holdLurker;
        int count;
        BWAPI::Position where;
    };

    // Kept by CombatCommander.
    struct LurkerState
    {
        LurkerOrder order;

        int createFrame;
        int goFrame;
        std::vector<BWAPI::Unit> lurkers;               // assigned to this tactic

        LurkerState();
        LurkerState(const LurkerOrder & o);
    };

    struct LurkerOrders
    {
        LurkerTactic generalTactic;
        std::map<LurkerTactic, LurkerState> orders;     // only one order per tactic

        LurkerOrders();
    };
}
