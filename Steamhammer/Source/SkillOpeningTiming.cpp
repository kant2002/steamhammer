#include "SkillOpeningTiming.h"

#include "Bases.h"
#include "GameRecord.h"
#include "ProductionManager.h"
#include "The.h"

using namespace UAlbertaBot;

// Record info about the timings of our opening.

// -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- --

// The type is a tech building for purposes of opening timing.
// Compare UnitUtil::IsTechBuildingType().
bool SkillOpeningTiming::isTechBuildingType(BWAPI::UnitType type) const
{
    if (type.isBuilding())
    {
        if (type == BWAPI::UnitTypes::Zerg_Hatchery)
        {
            return false;
        }

        if (!type.upgradesWhat().empty() ||
            !type.researchesWhat().empty() ||
            type == BWAPI::UnitTypes::Terran_Barracks ||
            type == BWAPI::UnitTypes::Terran_Factory ||
            type == BWAPI::UnitTypes::Terran_Starport ||
            type == BWAPI::UnitTypes::Protoss_Gateway ||
            type == BWAPI::UnitTypes::Protoss_Robotics_Facility ||
            type == BWAPI::UnitTypes::Protoss_Stargate)
        {
            return true;
        }
    }

    return false;
}

// -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- --

SkillOpeningTiming::SkillOpeningTiming()
    : Skill("opening timing")
{
    // Build time for the fastest first tech building (excluding time to gather minerals).
    // There is no point in running before then (and the value is conserative).
    _nextUpdateFrame = 25 * 24;
}

std::string SkillOpeningTiming::putData() const
{
    std::stringstream s;

    s << record.lastFrame << ' ' << record.nWorkers << ' ' << record.armyMineralCost << ' ' << record.armyGasCost;
    s << ' ' << record.minerals << ' ' << record.gas;
    s << ' ' << record.production1 << ' ' << record.production2 << ' ' << record.production3;
    s << ' ' << record.bases;

    for (const std::pair<BWAPI::UnitType, int> & building : record.techBuildingsDone)
    {
        s << ' ' << int(building.first) << ' ' << building.second;
    }
    s << ' ' << sentinel;

    for (const std::pair<BWAPI::UpgradeType, int> & upgrade : record.upgradesDone)
    {
        s << ' ' << int(upgrade.first) << ' ' << upgrade.second;
    }
    s << ' ' << sentinel;

    for (const std::pair<BWAPI::TechType, int> & research : record.researchDone)
    {
        s << ' ' << int(research.first) << ' ' << research.second;
    }

    return s.str();
}

void SkillOpeningTiming::getData(GameRecord & grecord, const std::string & line)
{
    std::stringstream s(line);

    OpeningTimingRecord & r = grecord.openingTimingRecord;

    s >> r.lastFrame >> r.nWorkers >> r.armyMineralCost >> r.armyGasCost;
    s >> r.minerals >> r.gas;
    s >> r.production1 >> r.production2 >> r.production3;
    s >> r.bases;

    // Tech buildings.
    while (1)
    {
        int item, frame;
        if (!(s >> item) || item == sentinel)
        {
            break;
        }
        if (!(s >> frame))
        {
            break;
        }
        r.techBuildingsDone[BWAPI::UnitType(item)] = frame;
    }

    // Upgrades.
    while (1)
    {
        int item, frame;
        if (!(s >> item) || item == sentinel)
        {
            break;
        }
        if (!(s >> frame))
        {
            break;
        }
        r.upgradesDone[BWAPI::UpgradeType(item)] = frame;
    }

    // Research.
    while (1)
    {
        int item, frame;
        if (!(s >> item) || item == sentinel)
        {
            break;
        }
        if (!(s >> frame))
        {
            break;
        }
        r.researchDone[BWAPI::TechType(item)] = frame;
    }
}

void SkillOpeningTiming::update()
{
    if (record.lastFrame && the.now() >= record.lastFrame + 1500)
    {
        // We continue a while after we're out of book, to see ongoing stuff finish and other near-term events.
        _nextUpdateFrame = INT_MAX;
        return;
    }
    if (!record.lastFrame && ProductionManager::Instance().isOutOfBook())
    {
        // We just went out of book. Record summary info about the opening.
        record.lastFrame = the.now();
        record.nWorkers = the.my.completed.count(the.selfRace().getWorker());
        record.armyMineralCost = ProductionManager::Instance().getCombatMineralsSpent();
        record.armyGasCost = ProductionManager::Instance().getCombatGasSpent();
        record.minerals = the.self()->minerals();
        record.gas = the.self()->gas();

        for (BWAPI::Unit u : the.self()->getUnits())
        {
            const BWAPI::UnitType type = u->getType();
            if (type.isBuilding() && u->isCompleted())
            {
                if (type == BWAPI::UnitTypes::Terran_Barracks ||
                    type == BWAPI::UnitTypes::Protoss_Gateway ||
                    type == BWAPI::UnitTypes::Zerg_Hatchery ||
                    type == BWAPI::UnitTypes::Zerg_Lair ||
                    type == BWAPI::UnitTypes::Zerg_Hive)
                {
                    ++record.production1;
                }
            }
            else if (type == BWAPI::UnitTypes::Terran_Factory ||
                     type == BWAPI::UnitTypes::Protoss_Robotics_Facility)
            {
                ++record.production2;
            }
            else if (type == BWAPI::UnitTypes::Terran_Starport ||
                     type == BWAPI::UnitTypes::Protoss_Stargate)
            {
                ++record.production3;
            }
        }

        record.bases = the.bases.baseCount(the.self());
    }
    _nextUpdateFrame = the.now() + 24;

    // Initialize the set of research that we get for free at the start of the game.
    if (freeResearch.empty())
    {
        for (BWAPI::TechType type : BWAPI::TechTypes::allTechTypes())
        {
            if (type.getRace() == the.selfRace() && the.self()->hasResearched(type))
            {
                freeResearch.insert(type);
                //BWAPI::Broodwar->printf("free research %s", type.getName().c_str());
            }
        }
    }

    // Tech buildings.
    for (BWAPI::Unit u : the.self()->getUnits())
    {
        const BWAPI::UnitType type = u->getType();
        if (u->isCompleted() && isTechBuildingType(type))
        {
            auto it = record.techBuildingsDone.find(type);
            if (it == record.techBuildingsDone.end())
            {
                record.techBuildingsDone[type] = the.now();
                //BWAPI::Broodwar->printf("building %s @ %d", type.getName().c_str(), the.now());
            }
        }
    }

    // Upgrades.
    for (BWAPI::UpgradeType type : BWAPI::UpgradeTypes::allUpgradeTypes())
    {
        if (the.self()->getUpgradeLevel(type) > 0)
        {
            auto it = record.upgradesDone.find(type);
            if (it == record.upgradesDone.end())
            {
                record.upgradesDone[type] = the.now();
                //BWAPI::Broodwar->printf("upgrade %s @ %d", type.getName().c_str(), the.now());
            }
        }
    }

    // Research.
    for (BWAPI::TechType type : BWAPI::TechTypes::allTechTypes())
    {
        if (type.getRace() == the.selfRace() && the.self()->hasResearched(type))
        {
            auto freeIt = freeResearch.find(type);
            if (freeIt == freeResearch.end())
            {
                auto it = record.researchDone.find(type);
                if (it == record.researchDone.end())
                {
                    record.researchDone[type] = the.now();
                    //BWAPI::Broodwar->printf("research %s @ %d", type.getName().c_str(), the.now());
                }
            }
        }
    }
}
