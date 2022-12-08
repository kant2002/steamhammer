#include "StaticDefense.h"

#include "Bases.h"
#include "BuildingManager.h"
#include "MacroAct.h"
#include "ProductionManager.h"
#include "The.h"
#include "UnitUtil.h"

using namespace UAlbertaBot;

// -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- --

StaticDefensePlan::StaticDefensePlan()
    : atInnerBases(0)
    , atOuterBases(0)
    , atFront(0)
    //, atProxy(0)
    , airIsPerBase(true)
    , antiAir(0)
{
}

// -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- --

// -- -- -- -- -- -- -- -- -- -- --
// Private methods.

// The defense plan has been filled in. Make sure it doesn't call for excess defense.
// This tries to limit defenses to what the economy can support, as a separate task
// to simplify the overall code.
void StaticDefense::limitZergDefenses()
{
    const int nBases = the.bases.completedBaseCount(the.self());
    if (nBases == 0)
    {
        // Save the effort. Also, don't carelessly divide by zero.
        return;
    }

    int nDrones = WorkerManager::Instance().getNumMineralWorkers();
    int limit;

    // Sunkens.
    _plan.atInnerBases = std::min(_plan.atInnerBases, nDrones / 12);

    if (nDrones >= 21)
    {
        limit = nDrones / 6;
    }
    else if (nDrones >= 10)
    {
        limit = 3;
    }
    else if (nDrones >= 7)
    {
        limit = 1;
    }
    else
    {
        limit = 0;
    }

    // If there are a lot of tanks, sunkens won't help much.
    if (the.your.seen.count(BWAPI::UnitTypes::Terran_Siege_Tank_Tank_Mode) +
        the.your.seen.count(BWAPI::UnitTypes::Terran_Siege_Tank_Siege_Mode) > 4 &&
        the.info.enemyHasSiegeMode())
    {
        limit = std::min(limit, 4);
    }

    _plan.atOuterBases = std::min(_plan.atOuterBases, std::max(0, limit-1));
    _plan.atFront = std::min(_plan.atFront, limit);

    // Spores.
    if (_plan.airIsPerBase)
    {
        limit = nDrones / (7 * nBases);     // safe because nBases > 0
        if (the.enemyRace() == BWAPI::Races::Zerg)
        {
            limit = std::min(limit, 3);
        }
        _plan.antiAir = std::min(_plan.antiAir, limit);
    }
}

// Analyze static defense needs for TvX and PvX.
void StaticDefense::planTP()
{
    // -- ground defenses --

    _plan.atInnerBases = 0;
    _plan.atOuterBases = 0;
    _plan.atFront = 0;
    //_plan.atProxy = 0;

    // Terran does not make any ground defenses unless the opening build calls for it.
    // NOTE It doesn't know how to land a floating barracks, so a bunker is not always useful.
    // Protoss defends only against vulture raids.
    // NOTE Air defenses help against some ground attacks, though.
    if (the.selfRace() == BWAPI::Races::Protoss)
    {
        if (the.your.seen.count(BWAPI::UnitTypes::Terran_Vulture) >= 6)
        {
            _plan.atOuterBases = 2;
        }
        else if (the.your.seen.count(BWAPI::UnitTypes::Terran_Vulture) >= 3)
        {
            _plan.atOuterBases = 1;
        }
    }

    // -- air defenses --

    _plan.airIsPerBase = true;

    // NOTE Zerg drop tech is not as easy to detect.
    bool enemyHasDropTech =
        the.your.seen.count(BWAPI::UnitTypes::Terran_Dropship) + the.your.seen.count(BWAPI::UnitTypes::Protoss_Shuttle) > 0;

    int minAir = 0;
    if (enemyHasDropTech)
    {
        minAir = 2;
    }
    else if (the.info.enemyHasCloakTech())
    {
        minAir = 1;
    }

    // Count enemy air to ground power.
    int enemyAir =
        the.your.seen.count(BWAPI::UnitTypes::Terran_Wraith) +
        8 * the.your.seen.count(BWAPI::UnitTypes::Terran_Battlecruiser) +

        2 * the.your.seen.count(BWAPI::UnitTypes::Protoss_Scout) +
        4 * the.your.seen.count(BWAPI::UnitTypes::Protoss_Arbiter) +
        6 * the.your.seen.count(BWAPI::UnitTypes::Protoss_Carrier) +

        2 * the.your.seen.count(BWAPI::UnitTypes::Zerg_Mutalisk) +
        6 * the.your.seen.count(BWAPI::UnitTypes::Zerg_Guardian);

    // Count my mobile anti-air power. Skip spellcasters.
    int myAntiAir =
        the.my.all.count(BWAPI::UnitTypes::Terran_Marine) +
        6 * the.my.all.count(BWAPI::UnitTypes::Terran_Goliath) +
        the.my.all.count(BWAPI::UnitTypes::Terran_Wraith) +
        4 * the.my.all.count(BWAPI::UnitTypes::Terran_Valkyrie) +
        6 * the.my.all.count(BWAPI::UnitTypes::Terran_Battlecruiser) +
        
        2 * the.my.all.count(BWAPI::UnitTypes::Protoss_Dragoon) +
        4 * the.my.all.count(BWAPI::UnitTypes::Protoss_Archon) +
        2 * the.my.all.count(BWAPI::UnitTypes::Protoss_Corsair) +
        3 * the.my.all.count(BWAPI::UnitTypes::Protoss_Scout) +
        3 * the.my.all.count(BWAPI::UnitTypes::Protoss_Carrier);

    int antiAir = std::max(minAir, (enemyAir - myAntiAir) / 4);

    if (the.selfRace() == BWAPI::Races::Protoss)
    {
        _plan.antiAir = 0;

        // Copy needed air defense into the ground plan.
        _plan.atInnerBases = std::max(antiAir, _plan.atInnerBases);
        _plan.atOuterBases = std::max(antiAir, _plan.atOuterBases);
        _plan.atFront = std::max(antiAir, _plan.atFront);
    }
    else // terran
    {
        _plan.antiAir = antiAir;
    }
}

// How many sunks do we need in total?
int StaticDefense::analyzeGroundDefensesZvT() const
{
    const int nLurkers = the.my.completed.count(BWAPI::UnitTypes::Zerg_Lurker);

    // Lurkers multiply, up to a limit.
    const int efficientLurkers = std::min(8, nLurkers);
    int myPower = 2 * (efficientLurkers * efficientLurkers + nLurkers - efficientLurkers);

    // Other units add. Let's pretend.
    for (BWAPI::Unit u : the.self()->getUnits())
    {
        if (!u->getType().isBuilding() && !u->getType().isWorker() &&
            UnitUtil::TypeCanAttackGround(u->getType()) &&
            u->getType() != BWAPI::UnitTypes::Zerg_Lurker)
        {
            myPower += u->getType().supplyRequired();
        }
    }

    // Units that are weak against sunkens. They add.
    int yourPower =
        the.your.seen.count(BWAPI::UnitTypes::Terran_Firebat) +
        the.your.seen.count(BWAPI::UnitTypes::Terran_Vulture) +
        the.your.seen.count(BWAPI::UnitTypes::Terran_Ghost);

    // Other units multiply, up to a limit.

    // No point trying to stop mass tanks with sunkens.
    const int nTanks = std::min(4,
        the.your.seen.count(BWAPI::UnitTypes::Terran_Siege_Tank_Tank_Mode) +
        the.your.seen.count(BWAPI::UnitTypes::Terran_Siege_Tank_Siege_Mode));
    yourPower += 4 * nTanks * nTanks;

    // Goliaths. Pretty strange calculation.
    const int nGoliaths = the.your.seen.count(BWAPI::UnitTypes::Terran_Goliath);
    const int efficientGoliaths = std::min(8, nGoliaths);
    yourPower += efficientGoliaths * efficientGoliaths + nGoliaths - efficientGoliaths;

    // Marines and medics.
    const int nMarines = the.your.seen.count(BWAPI::UnitTypes::Terran_Marine);
    const int nMedics = the.your.seen.count(BWAPI::UnitTypes::Terran_Medic);
    const int efficientMedics = std::min(nMedics, nMarines / 4);
    const int marineLimit = std::min(efficientMedics + 10, 20);
    const int efficientMarines = std::min(marineLimit, nMarines);
    yourPower += efficientMarines * efficientMarines;  // no adjustment for inefficient marines

    return  std::max(0, (yourPower - myPower) / 4);
}

// How many sunks do we need in total?
int StaticDefense::analyzeGroundDefensesZvP() const
{
    int myPower = 0;
    for (BWAPI::Unit u : the.self()->getUnits())
    {
        if (!u->getType().isBuilding() && !u->getType().isWorker() &&
            UnitUtil::TypeCanAttackGround(u->getType()))
        {
            myPower += u->getType().supplyRequired();
        }
    }

    // Protoss ground units with physical attacks.
    int yourPower =
        the.your.seen.count(BWAPI::UnitTypes::Protoss_Zealot) * 4 +
        the.your.seen.count(BWAPI::UnitTypes::Protoss_Dragoon) * 5 +                // can kite out of range
        the.your.seen.count(BWAPI::UnitTypes::Protoss_Archon) * 6 +                 // weak vs sunkens
        the.your.seen.count(BWAPI::UnitTypes::Protoss_Dark_Templar) * 4 +
        std::min(2, the.your.seen.count(BWAPI::UnitTypes::Protoss_Reaver)) * 8;     // sunks don't stop mass reavers

    return std::max(0, (yourPower - myPower) / 10);
}

// How many sunks do we need in total?
// Account for likely midgame units only. Other stuff can take care of itself.
int StaticDefense::analyzeGroundDefensesZvZ() const
{
    const int myPower =
        the.my.all.count(BWAPI::UnitTypes::Zerg_Zergling) +
        2 * (the.my.all.count(BWAPI::UnitTypes::Zerg_Mutalisk) - the.your.seen.count(BWAPI::UnitTypes::Zerg_Mutalisk));

    const int yourPower =
        the.your.seen.count(BWAPI::UnitTypes::Zerg_Zergling) +
        2 * the.your.seen.count(BWAPI::UnitTypes::Zerg_Hydralisk);

    return std::max(0, (yourPower - myPower) / 6);
}

// Analyze static defense needs for ZvT ZvP ZvZ.
void StaticDefense::planZ()
{
    int nBases = the.bases.completedBaseCount(the.self());

    if (the.enemyRace() == BWAPI::Races::Terran)
    {
        const int enemyAirToAir =
            the.your.seen.count(BWAPI::UnitTypes::Terran_Wraith) +
            the.your.seen.count(BWAPI::UnitTypes::Terran_Valkyrie);
        const int enemyAirToGround =
            the.your.seen.count(BWAPI::UnitTypes::Terran_Wraith) +
            4 * the.your.seen.count(BWAPI::UnitTypes::Terran_Battlecruiser);
        // Assume vultures will keep coming.
        const int vultureDefense =
            the.your.ever.count(BWAPI::UnitTypes::Terran_Vulture) > 0 ? 1 : 0;

         // Assume dropships may give up.
        _plan.atInnerBases =
            the.your.seen.count(BWAPI::UnitTypes::Terran_Dropship) > 0 ? 1 : 0;
        if (Config::Skills::HumanOpponent)
        {
            // Humans attack weak bases first, so be strong everywhere.
            _plan.atOuterBases =        // includes the front base
                std::max(vultureDefense, analyzeGroundDefensesZvT());
            _plan.atFront = _plan.atOuterBases;
        }
        else
        {
            // Bots usually attack the front line and ignore other bases.
            _plan.atOuterBases = vultureDefense;
            _plan.atFront = analyzeGroundDefensesZvT();
        }

        _plan.airIsPerBase = false;
        _plan.antiAir = 0;
        if (enemyAirToGround >= 6)
        {
            // Mass wraiths and/or battlecruisers.
            _plan.airIsPerBase = true;
            _plan.antiAir = std::min(3, enemyAirToGround / 5);
        }
        else if (nBases > 1 && (enemyAirToAir >= 3 && the.my.completed.count(BWAPI::UnitTypes::Zerg_Greater_Spire) == 0 || enemyAirToAir >= 6))
        {
            _plan.antiAir = 2;
        }
        else if (enemyAirToAir > 0 && the.my.completed.count(BWAPI::UnitTypes::Zerg_Spire) == 0 || enemyAirToAir >= 3)
        {
            _plan.antiAir = 1;
        }
    }
    else if (the.enemyRace() == BWAPI::Races::Protoss)
    {
        _plan.atInnerBases =
            // Vs human: Shuttle means drop on bases.
            // Vs bot: If there was once a shuttle and reaver and there's still a shuttle, expect another reaver.
            (the.your.seen.count(BWAPI::UnitTypes::Protoss_Shuttle) > 0 &&
            (Config::Skills::HumanOpponent || the.your.ever.count(BWAPI::UnitTypes::Protoss_Reaver) > 0))
            ? 1 : 0;
        if (Config::Skills::HumanOpponent)
        {
            // Humans attack weak bases first, so be strong everywhere.
            _plan.atOuterBases =
                std::max(the.your.ever.count(BWAPI::UnitTypes::Protoss_Dark_Templar) > 0 ? 1 : 0, analyzeGroundDefensesZvP());
            _plan.atFront = _plan.atOuterBases;
        }
        else
        {
            // Bots usually attack the front line and ignore other bases.
            _plan.atFront = analyzeGroundDefensesZvP();
            _plan.atOuterBases =
                (_plan.atFront > 0 || the.your.ever.count(BWAPI::UnitTypes::Protoss_Dark_Templar) > 0) ? 1 : 0;
        }

        int enemyAirToAir =
            the.your.seen.count(BWAPI::UnitTypes::Protoss_Corsair) +
            3 * the.your.seen.count(BWAPI::UnitTypes::Protoss_Scout);
        int enemyAirToGround =      // don't count carriers or arbiters
            the.your.seen.count(BWAPI::UnitTypes::Protoss_Scout);

        _plan.airIsPerBase = false;
        _plan.antiAir = 0;
        if (enemyAirToGround >= 6)
        {
            // Mass scouts. Ouch.
            _plan.airIsPerBase = true;
            _plan.antiAir = enemyAirToGround / 4;
        }
        else if (nBases >= 3 && enemyAirToAir >= 10)
        {
            _plan.antiAir = 3;
        }
        else if (nBases >= 2 && (enemyAirToAir >= 3 && the.my.completed.count(BWAPI::UnitTypes::Zerg_Greater_Spire) == 0 || enemyAirToAir >= 6))
        {
            _plan.antiAir = 2;
        }
        else if (enemyAirToAir > 0 && the.my.completed.count(BWAPI::UnitTypes::Zerg_Spire) == 0 || enemyAirToAir >= 3)
        {
            _plan.antiAir = 1;
        }

    }
    else
    {
        // Enemy is zerg.
        int nMutas = the.my.all.count(BWAPI::UnitTypes::Zerg_Mutalisk);

        _plan.atInnerBases = 0;
        _plan.atOuterBases = analyzeGroundDefensesZvZ();
        _plan.atFront = _plan.atOuterBases;

        _plan.airIsPerBase = true;
        _plan.antiAir = 0;
        if (the.info.enemyGasTiming() > 0)       // enemy has been seen to use gas
        {
            const int enemyMutas = the.your.seen.count(BWAPI::UnitTypes::Zerg_Mutalisk);
            // When did, or will, the enemy spire finish? If before now, pretend it is now.
            const int enemySpireTime = std::max(the.now(), the.info.getEnemyBuildingTiming(BWAPI::UnitTypes::Zerg_Spire));

            if (enemyMutas > nMutas + 6)
            {
                if (the.my.completed.count(BWAPI::UnitTypes::Zerg_Spire) > 0)
                {
                    _plan.antiAir = 2;
                }
                else
                {
                    // Spire destroyed or never made.
                    _plan.antiAir = (enemyMutas - nMutas) / 6;
                }
            }
            else if (enemyMutas > nMutas || the.info.getMySpireTiming() - 30 * 24 > enemySpireTime)
            {
                //BWAPI::Broodwar->printf("our spire %d versus theirs %d", the.info.getMySpireTiming(), enemySpireTime);
                _plan.antiAir = 1;
            }
        }
    }
}

// Analyze the situation and decide what static defense is needed where.
void StaticDefense::plan()
{
    if (the.enemyRace() == BWAPI::Races::Unknown)
    {
        // No defense needed until we've seen some enemy units.
        return;
    }

    if (the.selfRace() == BWAPI::Races::Zerg)
    {
        planZ();
        limitZergDefenses();
    }
    else
    {
        planTP();
    }
}

void StaticDefense::startBuilding(BWAPI::UnitType building, const BWAPI::TilePosition & tile)
{
    BuildOrderQueue & queue = the.production.getQueue();

    // Replace zerg drones if low.
    if (building.getRace() == BWAPI::Races::Zerg && the.my.all.count(BWAPI::UnitTypes::Zerg_Drone) <= _MinDroneLimit)
    {
        queue.queueAsHighestPriority(BWAPI::UnitTypes::Zerg_Drone);
    }

    queue.queueAsHighestPriority(MacroAct(building, tile));
}

// The building manager sometimes forgets about sunkens and spores while building them, and
// leaves unmorphed creep colonies. If we still want to the sunk or spore, morph it.
bool StaticDefense::morphStrandedCreeps(BWAPI::UnitType type)
{
    if (the.selfRace() != BWAPI::Races::Zerg)
    {
        return false;
    }

    // Ensure that no creep colonies are under the building manager's control. We can sieze them.
    if (BuildingManager::Instance().isBeingBuilt(_ground) || BuildingManager::Instance().isBeingBuilt(_air))
    {
        return false;
    }

    bool any = false;

    // We simultaneously morph up to one creep per base.
    for (Base * base : the.bases.getAll())
    {
        int nCreeps = base->getNumUnits(BWAPI::UnitTypes::Zerg_Creep_Colony);
        if (base->isMyCompletedBase() && nCreeps > 0)
        {
            int nUnits = base->getNumUnits(type);
            bool gotOne;
            if (type == _ground)
            {
                gotOne = 
                    base == the.bases.myFront() && nUnits < _plan.atFront ||
                    !base->isInnerBase() && nUnits < _plan.atOuterBases ||
                    base->isInnerBase() && nUnits < _plan.atInnerBases;
            }
            else // type == _air
            {
                gotOne = _plan.airIsPerBase && nUnits < _plan.antiAir;
            }

            if (gotOne)
            {
                // We want to morph a creep. Is there one to morph?
                BWAPI::Unit creep = BWAPI::Broodwar->getClosestUnit(
                    base->getCenter(),
                    BWAPI::Filter::GetType == BWAPI::UnitTypes::Zerg_Creep_Colony && BWAPI::Filter::IsOwned && BWAPI::Filter::IsCompleted,
                    8 * 32
                );
                if (creep)
                {
                    the.micro.Make(creep, type);
                }
            }
        }
    }

    return any;
}

// Urgently start all ground defenses needed at the front base.
bool StaticDefense::startFrontGroundBuildings()
{
    Base * front = the.bases.myFront();

    if (!front || !front->isMyCompletedBase())
    {
        return false;
    }

    int nActual = front->getNumUnits(_ground);
    if (_ground == BWAPI::UnitTypes::Zerg_Sunken_Colony)
    {
        // For sunkens, also count creeps, which will probably turn into sunkens.
        // NOTE We do this only for front base defenses. Others are OK because they are built more slowly.
        nActual += front->getNumUnits(BWAPI::UnitTypes::Zerg_Creep_Colony);
    }
    // Also count queued buildings, without checking where they are set to be built.
    nActual += the.production.getQueue().numInNextN(_ground, 2 * _plan.atFront);

    int nNeeded = _plan.atFront - nActual;      // new buildings to start
    if (nNeeded <= 0)
    {
        return false;
    }

    //BWAPI::Broodwar->printf("%d needed at front: plan %d - existing %d", nNeeded, _plan.atFront, nActual);

    while (nNeeded > 0)
    {
        startBuilding(_ground, front->getFrontTile());
        --nNeeded;
    }

    return true;
}

// Take our time starting all ground defenses needed at bases other than the front base.
// Don't build more at a base than there are workers there, so that new bases don't overdo it.
bool StaticDefense::startOtherGroundBuildings()
{
    for (Base * base : the.bases.getAll())
    {
        if (base->isMyCompletedBase() &&
            (the.selfRace() != BWAPI::Races::Zerg || base->getNumUnits(BWAPI::UnitTypes::Zerg_Creep_Colony) == 0))
        {
            int nUnits = base->getNumUnits(_ground);
            if (base == the.bases.myFront())
            {
                // Handled by startFrontGroundBuildings().
            }
            else if (!base->isInnerBase())  // outer base
            {
                if (nUnits < _plan.atOuterBases && nUnits < base->getNumWorkers())
                {
                    startBuilding(_ground, base->getTilePosition());
                    return true;
                }
            }
            else if (base->isInnerBase())
            {
                if (nUnits < _plan.atInnerBases && nUnits < base->getNumWorkers())
                {
                    startBuilding(_ground, base->getTilePosition());
                    return true;
                }
            }
        }
    }

    return false;
}

bool StaticDefense::buildGround()
{
    if (_plan.atInnerBases + _plan.atInnerBases + _plan.atFront == 0)
    {
        return false;
    }

    // Build barracks/forge/pool if needed.
    // Protoss normally already has a pylon, created by a different mechanism.
    if (needPrerequisite(_groundPrereq)) 
    {
        return true;
    }

    // Morph creep colonies that the building manager forgot about, if needed.
    if (morphStrandedCreeps(_ground))
    {
        return true;
    }

    // Build defenses on the front line as fast as possible.
    if (startFrontGroundBuildings())
    {
        return true;
    }

    if (alreadyBuilding(_ground))
    {
        return true;
    }

    // Build defenses elsewhere one at a time.
    return startOtherGroundBuildings();
}

// We're placing one air defense building at a each of limited number of bases. Choose them.
// In practice, only zerg does this, to provide safe havens for overlords.
// First anti-air: vT and vZ, closest base to enemy; vP, natural.
// Second anti-air in the remaining base of main/natural.
// Any remaining in remaining bases.
void StaticDefense::chooseAirBases()
{
    // Figure out the first two bases.
    const bool hasNatural =
        the.bases.myNatural() &&
        the.bases.myNatural()->isMyCompletedBase();
    Base * base1;
    Base * base2;
    if (the.enemyRace() == BWAPI::Races::Protoss)
    {
        // Natural first vP, to help defend against dark templar.
        base1 = hasNatural ? the.bases.myNatural() : the.bases.myMain();
        base2 =  hasNatural ? the.bases.myMain() : nullptr;
    }
    else // vT or vZ
    {
        base1 = the.bases.myMain();
        base2 = hasNatural ? the.bases.myNatural() : nullptr;

        Base * enemyStart = the.bases.enemyStart();
        if (hasNatural && enemyStart)
        {
            // We know where the enemy is. Put the first in the natural if it is significantly closer.
            int mainDist = the.bases.myMain()->getCenter().getApproxDistance(enemyStart->getPosition());
            int natDist = the.bases.myNatural()->getCenter().getApproxDistance(enemyStart->getPosition());
            if (natDist + 8 * 32 < mainDist)
            {
                base1 = the.bases.myNatural();
                base2 = the.bases.myMain();
            }
        }
    }

    // Fill in _airBases.
    _airBases.clear();
    _airBases.push_back(base1);
    if (base2 && _plan.antiAir >= 2)
    {
        _airBases.push_back(base2);
    }

    if (_plan.antiAir > 2)
    {
        int remaining = _plan.antiAir - 2;
        for (Base * base : the.bases.getAll())
        {
            if (base->isMyCompletedBase() && base != base1 && base != base2)
            {
                _airBases.push_back(base);
                --remaining;
                if (remaining <= 0)
                {
                    break;
                }
            }
        }
    }
}

// All checks passed. Find the next base that needs it, and start the building.
void StaticDefense::startAirBuilding()
{
    const std::vector<Base *> & bases =
        _plan.airIsPerBase ? the.bases.getAll() : _airBases;
    
    for (Base * base : bases)
    {
        if (base->isMyCompletedBase() && base->getNumUnits(_air) < (_plan.airIsPerBase ? _plan.antiAir : 1))
        {
            if (the.selfRace() != BWAPI::Races::Zerg || base->getNumUnits(BWAPI::UnitTypes::Zerg_Creep_Colony) == 0)
            {
                startBuilding(_air, base->getMineralLineTile());
                return;
            }
        }
    }
}

// Order terran turrets or zerg spore colonies.
// Protoss doesn't use this, because cannons are general-purpose.
void StaticDefense::buildAir()
{
    if (the.selfRace() == BWAPI::Races::Protoss || _plan.antiAir <= 0)
    {
        return;
    }

    // Build ebay/evo if needed.
    if (_plan.antiAir > 0 && needPrerequisite(_airPrereq))
    {
        return;
    }

    // Morph creep colonies that the building manager forgot about, if needed.
    if (morphStrandedCreeps(_air))
    {
        return;
    }

    // We're not in a big hurry. Wait for the last one to finish.
    if (alreadyBuilding(_air))
    {
        return;
    }

    if (!_plan.airIsPerBase)
    {
        chooseAirBases();
    }

    startAirBuilding();
}

bool StaticDefense::alreadyBuilding(BWAPI::UnitType type)
{
    return
        UnitUtil::GetUncompletedUnitCount(type) > 0 ||
        BuildingManager::Instance().isBeingBuilt(type) ||
        UnitUtil::GetUncompletedUnitCount(BWAPI::UnitTypes::Zerg_Creep_Colony) > 0 ||
        the.production.getQueue().anyInNextN(type, 2);
}

// Add a preprequisite tech buliding, if necessary.
// Return true iff the prerequisite is not available.
bool StaticDefense::needPrerequisite(BWAPI::UnitType prereq)
{
    if (!prereq.isValid())
    {
        return false;
    }

    if (the.my.completed.count(prereq) > 0)
    {
        return false;
    }

    // If we're very low on workers, defense tech will not save us no matter how much we need it.
    if (the.my.completed.countWorkers() <= 5)
    {
        return true;
    }

    if (the.my.all.count(prereq) > 0 || BuildingManager::Instance().isBeingBuilt(prereq))
    {
        // It's building.
        // Build time of a creep colony is 300 frames. If the prereq will finish by then, it's OK.
        if (the.selfRace() == BWAPI::Races::Zerg && the.info.remainingBuildTime(prereq) <= 300)
        {
            return false;
        }

        return true;
    }

    BuildingManager::Instance().addBuildingTask(prereq, the.bases.myMain()->getTilePosition(), nullptr, false);
    return true;
}

// Possibly add static defenses at various bases.
void StaticDefense::build()
{
    if (!buildGround())
    {
        buildAir();
    }
}

void StaticDefense::drawGroundCount(Base * base, int count) const
{
    if (count == 0)
    {
        return;
    }

    std::string building;
    if (the.selfRace() == BWAPI::Races::Terran)
    {
        building = "bunker";
    }
    else if (the.selfRace() == BWAPI::Races::Protoss)
    {
        building = "cannon";
    }
    else // zerg
    {
        building = "sunken";
    }

    BWAPI::Position xy = base->getFront();
    BWAPI::Broodwar->drawTextMap(xy+BWAPI::Position(-26, -6), "%c%d x %s", yellow, count, building.c_str());
    BWAPI::Broodwar->drawCircleMap(xy, 32, BWAPI::Colors::Orange);
}

void StaticDefense::drawAirCount(Base * base, int count) const
{
    if (count == 0)
    {
        return;
    }

    std::string building;
    if (the.selfRace() == BWAPI::Races::Terran)
    {
        building = "turret";
    }
    else if (the.selfRace() == BWAPI::Races::Protoss)
    {
        building = "(none)";
    }
    else // zerg
    {
        building = "spore";
    }

    BWAPI::Position xy = base->getPosition();
    BWAPI::Broodwar->drawTextMap(xy+BWAPI::Position(-26, -6), "%c%d x %s", yellow, count, building.c_str());
    BWAPI::Broodwar->drawCircleMap(xy, 32, BWAPI::Colors::Orange);
}

void StaticDefense::draw() const
{
    if (!Config::Debug::DrawStaticDefensePlan)
    {
        return;
    }

    for (Base * base : the.bases.getAll())
    {
        if (base->getOwner() == the.self())
        {
            if (base == the.bases.myFront() && _plan.atFront > _plan.atOuterBases)
            {
                drawGroundCount(base, _plan.atFront);
            }
            else if (base->isInnerBase())
            {
                drawGroundCount(base, _plan.atInnerBases);
            }
            else // outer base
            {
                drawGroundCount(base, _plan.atOuterBases);
            }

            if (_plan.airIsPerBase)
            {
                drawAirCount(base, _plan.antiAir);
            }
        }
    }

    if (!_plan.airIsPerBase)
    {
        for (Base * base : _airBases)
        {
            if (base->getOwner() == the.self())
            {
                drawAirCount(base, _plan.antiAir);
            }
        }
    }
}

// -- -- -- -- -- -- -- -- -- -- --
// Public methods.

StaticDefense::StaticDefense()
    : _MinDroneLimit(BWAPI::Broodwar->enemy()->getRace() == BWAPI::Races::Zerg ? 9 : 18)
{
    // NOTE When this runs, the.selfRace() is not yet initialized.
    if (BWAPI::Broodwar->self()->getRace() == BWAPI::Races::Terran)
    {
        _ground = BWAPI::UnitTypes::Terran_Bunker;
        _groundPrereq =  BWAPI::UnitTypes::Terran_Barracks;
        _air = BWAPI::UnitTypes::Terran_Missile_Turret;
        _airPrereq = BWAPI::UnitTypes::Terran_Engineering_Bay;
    }
    else if (BWAPI::Broodwar->self()->getRace() == BWAPI::Races::Protoss)
    {
        _ground = BWAPI::UnitTypes::Protoss_Photon_Cannon;
        _groundPrereq =  BWAPI::UnitTypes::Protoss_Forge;
        _air = BWAPI::UnitTypes::None;
        _airPrereq = BWAPI::UnitTypes::None;
    }
    else // zerg
    {
        _ground = BWAPI::UnitTypes::Zerg_Sunken_Colony;
        _groundPrereq =  BWAPI::UnitTypes::Zerg_Spawning_Pool;
        _air = BWAPI::UnitTypes::Zerg_Spore_Colony;
        _airPrereq = BWAPI::UnitTypes::Zerg_Evolution_Chamber;
    }
}

void StaticDefense::update()
{
    int phase = the.now() % 29;

    if (phase == 1)
    {
        plan();
    }
    else if (phase == 2)
    {
        build();
    }

    draw();
}
