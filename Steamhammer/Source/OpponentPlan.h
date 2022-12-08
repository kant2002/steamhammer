#pragma once

#include <string>
#include <vector>
#include "BWAPI.h"

namespace UAlbertaBot
{

enum class OpeningPlan
    { Unknown		// enemy plan not known yet or not recognized as one of the below
    , Contain       // early-game contain with bunker/cannons/sunkens
    , Proxy			// proxy building
    , WorkerRush	// early like Stone, late like one Tscmoo version
    , FastRush		// a cheese rush faster than 9 pool/8 rax/9 gate
    , HeavyRush		// 2 hatcheries pool only, 2 barracks no gas, 2 gates no gas
    , Factory		// terran fast factory
    , Wraith        // terran fast wraiths (typically 2 starport)
    , SafeExpand	// defended fast expansion, with bunker or cannons
    , NakedExpand	// undefended fast expansion (usual for zerg, bold for others)
    , Turtle		// cannons/bunker/sunkens thought to be on 1 base
    , Size
    };

const std::vector< std::pair<OpeningPlan, std::string> > PlanNames =
{
    std::pair<OpeningPlan, std::string>(OpeningPlan::Unknown, "Unknown"),
    std::pair<OpeningPlan, std::string>(OpeningPlan::Contain, "Contain"),
    std::pair<OpeningPlan, std::string>(OpeningPlan::Proxy, "Proxy"),
    std::pair<OpeningPlan, std::string>(OpeningPlan::WorkerRush, "Worker rush"),
    std::pair<OpeningPlan, std::string>(OpeningPlan::FastRush, "Fast rush"),
    std::pair<OpeningPlan, std::string>(OpeningPlan::HeavyRush, "Heavy rush"),
    std::pair<OpeningPlan, std::string>(OpeningPlan::Factory, "Factory"),
    std::pair<OpeningPlan, std::string>(OpeningPlan::Wraith, "Wraith"),
    std::pair<OpeningPlan, std::string>(OpeningPlan::SafeExpand, "Safe expand"),
    std::pair<OpeningPlan, std::string>(OpeningPlan::NakedExpand, "Naked expand"),
    std::pair<OpeningPlan, std::string>(OpeningPlan::Turtle, "Turtle")
};

// Turn an opening plan into a string.
static std::string OpeningPlanString(OpeningPlan plan)
{
    for (auto it = PlanNames.begin(); it != PlanNames.end(); ++it)
    {
        if ((*it).first == plan)
        {
            return (*it).second;
        }
    }

    return "Error";
}

// Turn a string into an opening plan.
static OpeningPlan OpeningPlanFromString(const std::string & planString)
{
    for (auto it = PlanNames.begin(); it != PlanNames.end(); ++it)
    {
        if ((*it).second == planString)
        {
            return (*it).first;
        }
    }

    return OpeningPlan::Unknown;
}

// Forward declarations.
class Base;
struct UnitInfo;

class OpponentPlan
{
private:

    OpeningPlan _openingPlan;		// estimated enemy plan
    bool _planIsFixed;				// estimate will no longer change

    // Utility functions. Time in frames.
    int travelTime(BWAPI::UnitType unitType, const BWAPI::Position & pos, const Base * base) const;
    Base * closestEnemyBase(const BWAPI::Position & pos) const;
    bool rushBuilding(const UnitInfo & ui) const;

    OpeningPlan recognizeProxy() const;
    OpeningPlan recognizeRush() const;
    OpeningPlan recognizeTerranTech() const;

    void recognize();

public:
    OpponentPlan();

    void update();

    OpeningPlan getPlan() const { return _openingPlan; };
};

}