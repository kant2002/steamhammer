#include "OpponentPlan.h"

#include "Bases.h"
#include "InformationManager.h"
#include "PlayerSnapshot.h"
#include "The.h"

using namespace UAlbertaBot;

// Attempt to recognize what the opponent is doing, so we can cope with it.
// For now, only try to recognize a small number of opening situations that require
// different handling.

// This is part of the OpponentModel module. Access should normally be through the OpponentModel instance.

// -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- --

// Assume that the unit started at the given base (at the location of the resource depot).
// About how long (in frames) did the unit need to reach its current position?
// If the path appears unwalkable, return -1.
int OpponentPlan::travelTime(BWAPI::UnitType unitType, const BWAPI::Position & pos, const Base * base) const
{
    int pixelDistance = base->getDistance(pos);
    if (pixelDistance < 0)
    {
        return -1;
    }

    // NOTE This is called only for units that are known to move!
    //      Otherwise there would be a risk of division by zero.
    UAB_ASSERT(unitType.topSpeed() > 0.0, "immobile unit");
    // NOTE No need to call round(), one frame off is more than good enough.
    return int(pixelDistance / unitType.topSpeed());
}

// The location of the enemy base is not known. Find the closest possibility.
// NOTE If the position is on an unwalkable or partially-walkable tile, this
//      will return null. It can't find the closest base.
Base * OpponentPlan::closestEnemyBase(const BWAPI::Position & pos) const
{
    int bestDist = MAX_DISTANCE;
    Base * bestBase = nullptr;

    for (Base * base : the.bases.getStarting())
    {
        // An explored base can't be the enemy's base, or we'd know it.
        if (base->getOwner() == the.neutral() && !base->isExplored())
        {
            int dist = base->getTileDistance(pos);
            if (dist >= 0 && dist < bestDist)
            {
                bestDist = dist;
                bestBase = base;
            }
        }
    }
    return bestBase;
}

// Does this enemy building imply that we are facing a fast rush?
// Check for an early completion time.
bool OpponentPlan::rushBuilding(const UnitInfo & ui) const
{
    return
        ui.type == BWAPI::UnitTypes::Terran_Barracks && ui.completeBy < 2375 ||  // probably 7 rax or earlier
        ui.type == BWAPI::UnitTypes::Protoss_Gateway && ui.completeBy < 2540 ||  // probably 7 gate or earlier
        ui.type == BWAPI::UnitTypes::Zerg_Spawning_Pool && ui.completeBy < 2675; // probably 8 pool or earlier
}

// Return whether the enemy has a building in our main or natural base.
// Returns the plan as Proxy, Contain for a more distant defense building, or Unknown for none.
OpeningPlan OpponentPlan::recognizeProxy() const
{
    int mainZone = the.zone.at(the.bases.myStart()->getTilePosition());
    int naturalZone = the.bases.myNatural() ? the.zone.at(the.bases.myNatural()->getTilePosition()) : 0;
    bool naturalTaken = the.bases.myNatural() && the.bases.myNatural()->getOwner() == the.self();

    for (const auto & kv : InformationManager::Instance().getUnitData(the.enemy()).getUnits())
    {
        const UnitInfo & ui(kv.second);

        if (ui.type.isBuilding() &&
            !ui.goneFromLastPosition &&
            !ui.type.isRefinery() &&
            ui.type != BWAPI::UnitTypes::Terran_Engineering_Bay &&
            ui.type != BWAPI::UnitTypes::Terran_Supply_Depot &&
            ui.type != BWAPI::UnitTypes::Protoss_Pylon)
        {
            int zone = the.zone.at(ui.lastPosition);
            if (zone > 0)
            {
                const int mainDist = ui.lastPosition.getApproxDistance(the.bases.myStart()->getCenter());
                if (zone == mainZone && mainDist <= 24 * 32 || mainDist <= 13 * 32)
                {
                    return OpeningPlan::Proxy;
                }

                // NOTE A forward forge can only mean cannons.
                const bool possibleContain =
                    ui.type == BWAPI::UnitTypes::Terran_Bunker ||
                    ui.type == BWAPI::UnitTypes::Protoss_Photon_Cannon ||
                    ui.type == BWAPI::UnitTypes::Protoss_Forge ||
                    ui.type == BWAPI::UnitTypes::Zerg_Creep_Colony ||
                    ui.type == BWAPI::UnitTypes::Zerg_Sunken_Colony;
                const int natDist = the.bases.myNatural() ? ui.lastPosition.getApproxDistance(the.bases.myNatural()->getCenter()) : MAX_DISTANCE;
                if (naturalTaken || !possibleContain)
                {
                    if (zone == naturalZone && natDist <= 18 * 32 || natDist <= 12 * 32)
                    {
                        return OpeningPlan::Proxy;
                    }
                }

                // If it's static defense, the enemy may be trying to contain us with it.
                // Check the natural only, and don't worry about whether we've taken the natural.
                if (possibleContain)
                {
                    if (zone == naturalZone && natDist <= 24 * 32 || natDist <= 15 * 32)
                    {
                        return OpeningPlan::Contain;
                    }
                }
            }
        }
    }

    return OpeningPlan::Unknown;
}

// Recognize a fast rush (e.g. 5 pool) or a worker rush.
OpeningPlan OpponentPlan::recognizeRush() const
{
    int frame = the.now();

    Base * myBase = the.bases.myStart();
    BWAPI::Position myOrigin = myBase->getPosition();
    Base * enemyBase = the.bases.enemyStart();

    int enemyWorkerRushCount = 0;
    int nBarracks = 0;              // barracks or gateway count
    int latestBarracks = 0;         // greatest completion time in frames

    if (enemyBase)
    {
        // We know where the enemy started, so we can calculate how far units have traveled.
        // Even in the case of a center proxy, a worker had to travel to build the proxy.
        for (const auto & kv : InformationManager::Instance().getUnitData(the.enemy()).getUnits())
        {
            const UnitInfo & ui(kv.second);

            if (ui.lastPosition.isValid())
            {
                // NOTE If the enemy tile distance == -1 then the comparison still gives the right answer.
                if (ui.type.isWorker() &&
                    myBase->getTileDistance(ui.lastPosition) >= 0 &&
                    myBase->getTileDistance(ui.lastPosition) <= enemyBase->getTileDistance(ui.lastPosition))
                {
                    // This enemy worker is at least half way to my base. Count it.
                    ++enemyWorkerRushCount;
                    if (enemyWorkerRushCount >= 3)
                    {
                        return OpeningPlan::WorkerRush;
                    }
                }
                else if (
                    ui.type == BWAPI::UnitTypes::Terran_Marine ||
                    ui.type == BWAPI::UnitTypes::Protoss_Zealot ||
                    ui.type == BWAPI::UnitTypes::Zerg_Zergling)
                {
                    if (3000 > frame - travelTime(ui.type, ui.lastPosition, enemyBase))
                    {
                        return OpeningPlan::FastRush;
                    }
                }
                else if (rushBuilding(ui))
                {
                    return OpeningPlan::FastRush;
                }
                else if (
                    ui.type == BWAPI::UnitTypes::Terran_Barracks ||
                    ui.type == BWAPI::UnitTypes::Protoss_Gateway)
                {
                    ++nBarracks;
                    latestBarracks = std::max(latestBarracks,  ui.completeBy);
                }
            }
        }
    }
    else
    {
        // Enemy base is not found.
        for (const auto & kv : InformationManager::Instance().getUnitData(the.enemy()).getUnits())
        {
            const UnitInfo & ui(kv.second);

            if (ui.lastPosition.isValid())
            {
                // NOTE If the enemy tile distance == -1 then the comparison still gives the right answer.
                if (ui.type.isWorker() &&
                    myBase->getTileDistance(ui.lastPosition) >= 0)
                {
                    Base * closestEnemy = closestEnemyBase(ui.lastPosition);
                    if (closestEnemy &&
                        myBase->getTileDistance(ui.lastPosition) <= closestEnemy->getTileDistance(ui.lastPosition))
                    {
                        // This enemy worker is at least half way to my base. Count it.
                        ++enemyWorkerRushCount;
                        if (enemyWorkerRushCount >= 3)
                        {
                            return OpeningPlan::WorkerRush;
                        }
                    }
                }
                else if (
                    ui.type == BWAPI::UnitTypes::Terran_Marine ||
                    ui.type == BWAPI::UnitTypes::Protoss_Zealot ||
                    ui.type == BWAPI::UnitTypes::Zerg_Zergling)
                {
                    Base * closestEnemy = closestEnemyBase(ui.lastPosition);
                    if (closestEnemy && closestEnemy->getTileDistance(ui.lastPosition) >= 0)
                    {
                        if (3000 > frame - travelTime(ui.type, ui.lastPosition, closestEnemy))
                        {
                            return OpeningPlan::FastRush;
                        }
                    }
                }
                else if (rushBuilding(ui))
                {
                    return OpeningPlan::FastRush;
                }
                else if (
                    ui.type == BWAPI::UnitTypes::Terran_Barracks ||
                    ui.type == BWAPI::UnitTypes::Protoss_Gateway)
                {
                    ++nBarracks;
                    latestBarracks = std::max(latestBarracks, ui.completeBy);
                }
            }
        }
    }

    if (nBarracks >= 2 && latestBarracks < 3200)
    {
        // This looks like BBS.
        return OpeningPlan::FastRush;
    }

    return OpeningPlan::Unknown;
}

// Factory, possibly with starport, and no sign of many marines intended.
OpeningPlan OpponentPlan::recognizeTerranTech() const
{
    if (the.enemyRace() != BWAPI::Races::Terran)
    {
        return OpeningPlan::Unknown;
    }

    int nMarines = 0;
    int nBarracks = 0;
    int nStarports = 0;
    int nTechProduction = 0;
    bool tech = false;

    for (const auto & kv : InformationManager::Instance().getUnitData(the.enemy()).getUnits())
    {
        const UnitInfo & ui(kv.second);

        if (ui.type == BWAPI::UnitTypes::Terran_Marine)
        {
            ++nMarines;
        }

        else if (ui.type.whatBuilds().first == BWAPI::UnitTypes::Terran_Barracks)
        {
            return OpeningPlan::Unknown;			// academy implied, marines seem to be intended
        }

        else if (ui.type == BWAPI::UnitTypes::Terran_Barracks)
        {
            ++nBarracks;
        }

        else if (ui.type == BWAPI::UnitTypes::Terran_Academy)
        {
            return OpeningPlan::Unknown;			// marines seem to be intended
        }

        else if (ui.type == BWAPI::UnitTypes::Terran_Wraith)
        {
            return OpeningPlan::Wraith;
        }

        else if (ui.type == BWAPI::UnitTypes::Terran_Starport)
        {
            if (ui.unit && ui.unit->isVisible() && ui.unit->isTraining() && !ui.unit->getAddon())
            {
                // The starport is flashing. Without an addon, it can only build a wraith.
                return OpeningPlan::Wraith;
            }
            ++nStarports;
        }

        else if (ui.type == BWAPI::UnitTypes::Terran_Factory)
        {
            ++nTechProduction;
        }

        else if (ui.type.whatBuilds().first == BWAPI::UnitTypes::Terran_Factory ||
            ui.type.whatBuilds().first == BWAPI::UnitTypes::Terran_Starport ||
            ui.type == BWAPI::UnitTypes::Terran_Armory)
        {
            tech = true;			// indicates intention to rely on tech units
        }
    }

    if (nStarports > 1)
    {
        return OpeningPlan::Wraith;
    }

    if ((nTechProduction >= 2 || tech) && nMarines <= 6 && nBarracks <= 1)
    {
        return OpeningPlan::Factory;
    }

    return OpeningPlan::Unknown;

}

void OpponentPlan::recognize()
{
    // Don't recognize island plans.
    // The regular plans and reactions do not make sense for island maps.
    if (the.bases.isIslandStart())
    {
        return;
    }

    // Recognize fast plans first, slow plans below.

    // Recognize in-base proxy buildings and slightly more distant Contain buildings.
    // NOTE It's not required, but still safest to check proxies before rushes, to prevent misrecognition.
    OpeningPlan maybeProxyPlan = recognizeProxy();
    if (maybeProxyPlan != OpeningPlan::Unknown)
    {
        _openingPlan = maybeProxyPlan;
        _planIsFixed = true;
        return;
    }

    // Recognize fast rush and worker rush.
    OpeningPlan maybeRushPlan = recognizeRush();
    if (maybeRushPlan != OpeningPlan::Unknown)
    {
        _openingPlan = maybeRushPlan;
        _planIsFixed = true;
        return;
    }

    int frame = the.now();

    PlayerSnapshot snap;
    snap.takeEnemy();

    // Recognize slower rushes.
    // TODO make sure we've seen the bare geyser in the enemy base!
    // TODO seeing an enemy worker carrying gas also means the enemy has gas
    if (snap.count(BWAPI::UnitTypes::Zerg_Hatchery) >= 2 &&
        snap.count(BWAPI::UnitTypes::Zerg_Spawning_Pool) > 0 &&
        snap.count(BWAPI::UnitTypes::Zerg_Extractor) == 0
        ||
        snap.count(BWAPI::UnitTypes::Terran_Barracks) >= 2 &&
        snap.count(BWAPI::UnitTypes::Terran_Refinery) == 0 &&
        snap.count(BWAPI::UnitTypes::Terran_Command_Center) <= 1
        ||
        snap.count(BWAPI::UnitTypes::Protoss_Gateway) >= 2 &&
        snap.count(BWAPI::UnitTypes::Protoss_Assimilator) == 0 &&
        snap.count(BWAPI::UnitTypes::Protoss_Nexus) <= 1)
    {
        _openingPlan = OpeningPlan::HeavyRush;
        _planIsFixed = true;
        return;
    }

    // Recognize terran factory or starport tech openings.
    OpeningPlan maybeTechPlan = recognizeTerranTech();
    if (maybeTechPlan != OpeningPlan::Unknown)
    {
        _openingPlan = maybeTechPlan;
        if (_openingPlan == OpeningPlan::Wraith)
        {
            // We're cautious about recognizing it, so don't downgrade Wraith to Factory later.
            _planIsFixed = true;
        }
        return;
    }

    // Recognize expansions with pre-placed static defense.
    // Zerg can't do this.
    // NOTE Incomplete test! We don't check the location of the static defense
    if (the.bases.baseCount(the.enemy()) >= 2)
    {
        if (snap.count(BWAPI::UnitTypes::Terran_Bunker) > 0 ||
            snap.count(BWAPI::UnitTypes::Protoss_Photon_Cannon) > 0)
        {
            _openingPlan = OpeningPlan::SafeExpand;
            return;
        }
    }

    // Recognize a naked expansion.
    // This has to run after the SafeExpand check, since it doesn't check for what's missing.
    if (the.bases.baseCount(the.enemy()) >= 2)
    {
        _openingPlan = OpeningPlan::NakedExpand;
        return;
    }

    // Recognize a turtling enemy.
    // NOTE Incomplete test! We don't check where the defenses are placed.
    if (the.bases.baseCount(the.enemy()) < 2)
    {
        if (snap.count(BWAPI::UnitTypes::Terran_Bunker) >= 2 ||
            snap.count(BWAPI::UnitTypes::Protoss_Photon_Cannon) >= 2 ||
            snap.count(BWAPI::UnitTypes::Zerg_Sunken_Colony) >= 2)
        {
            _openingPlan = OpeningPlan::Turtle;
            return;
        }
    }

    // Nothing recognized: Opening plan remains unchanged.
}

// -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- --

OpponentPlan::OpponentPlan()
    : _openingPlan(OpeningPlan::Unknown)
    , _planIsFixed(false)
{
}

// Update the recognized plan.
// Call this every frame. It will take care of throttling itself down to avoid unnecessary work.
void OpponentPlan::update()
{
    if (!Config::Strategy::UsePlanRecognizer)
    {
        return;
    }

    // The plan is decided. Don't change it any more.
    if (_planIsFixed)
    {
        return;
    }

    int frame = BWAPI::Broodwar->getFrameCount();

    if (frame > 100 && frame < 7200 &&       // only try to recognize openings
        frame % 12 == 7)                     // update interval
    {
        recognize();
    }
}
