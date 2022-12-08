#include "Squad.h"

#include "Bases.h"
#include "CombatSimulation.h"
#include "MapTools.h"
#include "SquadOrder.h"
#include "StrategyManager.h"
#include "The.h"
#include "UnitUtil.h"

using namespace UAlbertaBot;

Squad::Squad()
    : _name("Default")
    , _combatSquad(false)
    , _combatSimRadius(Config::Micro::CombatSimRadius)
    , _fightVisibleOnly(false)
    , _meatgrinder(false)
    , _hasAir(false)
    , _hasGround(false)
    , _canAttackAir(false)
    , _canAttackGround(false)
    , _attackAtMax(false)
    , _priority(0)
    , _timeMark(MAX_FRAME)
    , _lastAttack(the.now())
    , _lastRetreat(the.now())
    , _order(SquadOrder("None"))
    , _orderFrame(the.now())
    , _lurkerTactic(LurkerTactic::WithSquad)
    , _regroupPosition(BWAPI::Positions::Invalid)
{
    setOrderForMicroManagers();
}

// A "combat" squad is any squad except the Idle squad and Overlord squad.
// The usual work of workers is managed by WorkerManager. If we put workers into
// another squad, we have to notify WorkerManager.
Squad::Squad(const std::string & name, size_t priority)
    : _name(name)
    , _combatSquad(name != "Idle" && name != "Overlord")
    , _combatSimRadius(Config::Micro::CombatSimRadius)
    , _fightVisibleOnly(false)
    , _meatgrinder(false)
    , _hasAir(false)
    , _hasGround(false)
    , _canAttackAir(false)
    , _canAttackGround(false)
    , _attackAtMax(false)
    , _priority(priority)
    , _timeMark(MAX_FRAME)
    , _lastAttack(the.now())
    , _lastRetreat(the.now())
    , _order(SquadOrder("None"))
    , _orderFrame(the.now())
    , _lurkerTactic(LurkerTactic::WithSquad)
    , _regroupPosition(BWAPI::Positions::Invalid)
{
    setOrderForMicroManagers();
}

Squad::~Squad()
{
    clear();
}

void Squad::update()
{
    updateUnits();

    // The Irradiated squad.
    if (_name == "Irradiated")
    {
        _microIrradiated.update();
        return;
    }

    // The Overlord squad.
    // Overlords as combat squad detectors are managed by _microDetectors.
    if (_name == "Overlord")
    {
        _microOverlords.update();
        return;
    }

    // The vanguard (squad unit farthest forward) will be updated below if appropriate.
    _vanguard = nullptr;

    if (_units.empty())
    {
        return;
    }
    
    // If this is a worker squad, there is nothing more to do.
    if (!_combatSquad)
    {
        return;
    }

    if (_order.getType() == SquadOrderTypes::Watch)
    {
        if (maybeWatch())
        {
            return;
        }
        // Else fall through and act as usual.
    }

    // This is a non-empty combat squad, so it may have a meaningful vanguard unit.
    _vanguard = unitClosestToTarget(_units);

    if (_order.getType() == SquadOrderTypes::Load)
    {
        loadTransport();
        return;
    }

    if (_order.getType() == SquadOrderTypes::Drop)
    {
        _microTransports.update();
        // And fall through to let the rest of the drop squad fight.
    }

    // Maybe stim marines and firebats.
    stimIfNeeded();

    // Detectors.
    _microDetectors.setUnitClosestToTarget(_vanguard);
    _microDetectors.setSquadSize(_units.size());
    _microDetectors.go(_units);

    // High templar stay home until they merge to archons, all that's supported so far.
    _microHighTemplar.update();

    // Queens don't go into clusters, but act independently.
    _microQueens.update(_vanguard);

    // Finish choosing the units.
    BWAPI::Unitset unitsToCluster;
    for (BWAPI::Unit unit : _units)
    {
        if (unit->getType().isDetector() ||
            unit->getType().spaceProvided() > 0 ||
            unit->getType() == BWAPI::UnitTypes::Protoss_High_Templar ||
            unit->getType() == BWAPI::UnitTypes::Zerg_Queen)
        {
            // Don't cluster detectors, transports, high templar, queens.
            // They are handled separately above.
        }
        else if (unreadyUnit(unit))
        {
            // Unit needs prep. E.g., a carrier wants a minimum number of interceptors before it moves out.
            // unreadyUnit() itself does the prep.
        }
        else
        {
            unitsToCluster.insert(unit);
        }
    }

    // First pass to set cluster status.
    the.ops.cluster(the.self(), unitsToCluster, _clusters);
    for (UnitCluster & cluster : _clusters)
    {
        setClusterStatus(cluster);
        microSpecialUnits(cluster);
    }

    // Second pass to reconsider cluster status in light of the status of nearby clusters.
    bool statusReset;
    do
    {
        statusReset = false;
        for (UnitCluster & cluster : _clusters)
        {
            statusReset = statusReset || resetClusterStatus(cluster);
        }
    } while (statusReset);

    // Update _lastAttack and _lastRetreat based on the cluster status.
    setLastAttackRetreat();

    // It can get slow in late game when there are many clusters, so cut down the update frequency.
    const int nPhases = std::max(1, std::min(4, int(_clusters.size() / 12)));
    int phase = BWAPI::Broodwar->getFrameCount() % nPhases;
    for (const UnitCluster & cluster : _clusters)
    {
        if (phase == 0)
        {
            clusterCombat(cluster);
        }
        phase = (phase + 1) % nPhases;
    }
}

// Set cluster status and take non-combat cluster actions.
void Squad::setClusterStatus(UnitCluster & cluster)
{
    // Cases where the cluster can't get into a fight.
    if (noCombatUnits(cluster))
    {
        if (joinUp(cluster))
        {
            cluster.status = ClusterStatus::Advance;
            _regroupStatus = yellow + std::string("Join up");
        }
        else
        {
            // Can't join another cluster. Move back to base.
            cluster.status = ClusterStatus::FallBack;
            moveCluster(cluster, finalRegroupPosition());
            _regroupStatus = red + std::string("Fall back");
        }
    }
    else if (notNearEnemy(cluster))
    {
        cluster.status = ClusterStatus::Advance;
        if (joinUp(cluster))
        {
            _regroupStatus = yellow + std::string("Join up");
        }
        else
        {
            // Move toward the order position.
            moveCluster(cluster, _order.getPosition(), false);
            _regroupStatus = yellow + std::string("Advance");
        }
    }
    else
    {
        // Cases where the cluster might get into a fight.
        if (needsToRegroup(cluster))
        {
            cluster.status = ClusterStatus::Regroup;
        }
        else
        {
            cluster.status = ClusterStatus::Attack;
        }
    }

    drawCluster(cluster);
}

// Update _lastAttack and _lastRetreat based on the cluster status.
void Squad::setLastAttackRetreat()
{
    for (UnitCluster & cluster : _clusters)
    {
        if (cluster.status == ClusterStatus::Attack)
        {
            _lastAttack = the.now();
        }
        if (cluster.status == ClusterStatus::Regroup)
        {
            _lastRetreat = the.now();
        }
    }
}

// Reconsider cluster status in light of the status of nearby clusters.
// Some clusters may be set to retreat, but they're near a fight they should join.
// Tell them to attack after all.
// Return true if a change was made.
bool Squad::resetClusterStatus(UnitCluster & cluster)
{
    if (cluster.status == ClusterStatus::Regroup)
    {
        for (const UnitCluster & cluster2 : _clusters)
        {
            if (cluster2.status == ClusterStatus::Attack &&
                cluster.center.getApproxDistance(cluster2.center) < 10 * 32)
            {
                cluster.status = ClusterStatus::Attack;
                return true;
            }
        }
    }

    return false;
}

// Special-case units which are clustered, but arrange their own movement
// instead of accepting the cluster's movement commands.
// Currently, this is medics and defilers.
// Queens are not clustered.
void Squad::microSpecialUnits(const UnitCluster & cluster)
{
    // Medics and defilers try to get near the front line.
    static int spellPhase = 0;
    spellPhase = (spellPhase + 1) % 6;
    if (spellPhase == 0)
    {
        // The vanguard is chosen among combat units only, so a non-combat unit sent toward
        // the vanguard may either advance or retreat--either way, that's probably what we want.
        BWAPI::Unit vanguard = unitClosestToTarget(cluster.units);	// cluster vanguard
        if (!vanguard)
        {
            vanguard = _vanguard;									// squad vanguard
        }
        
        _microDefilers.updateMovement(cluster, vanguard);
        _microMedics.update(cluster, vanguard);
    }
    else if (spellPhase == 2)
    {
        _microDefilers.updateSwarm(cluster);
    }
    else if (spellPhase == 4)
    {
        _microDefilers.updatePlague(cluster);
    }
}

// Take cluster combat actions.
// This handles cluster status of Attack and Regroup. Others are handled by setClusterStatus().
// This takes no action for special units; see microSpecialUnits().
void Squad::clusterCombat(const UnitCluster & cluster)
{
    if (cluster.status == ClusterStatus::Attack)
    {
        _microAirToAir.execute(cluster);
        _microMelee.execute(cluster);
        //_microMutas.execute(cluster);
        _microRanged.execute(cluster);
        _microScourge.execute(cluster);
        _microTanks.execute(cluster);

        _microLurkers.setTactic(_lurkerTactic);
        _microLurkers.execute(cluster);
    }
    else if (cluster.status == ClusterStatus::Regroup)
    {
        // Regroup, aka retreat. Only fighting units care about regrouping.
        if (!_regroupPosition.isValid())
        {
            _regroupPosition = calcRegroupPosition(cluster);
        }

        if (Config::Debug::DrawClusters)
        {
            BWAPI::Broodwar->drawLineMap(cluster.center, _regroupPosition, BWAPI::Colors::Purple);
        }

        _microAirToAir.regroup(_regroupPosition, cluster);
        _microMelee.regroup(_regroupPosition, cluster);
        //_microMutas.regroup(_regroupPosition);
        _microRanged.regroup(_regroupPosition, cluster);
        _microScourge.regroup(_regroupPosition, cluster);
        _microTanks.regroup(_regroupPosition, cluster);

        // Aggressive lurkers do not regroup, but always execute their order.
        if (_lurkerTactic == LurkerTactic::Aggressive)
        {
            _microLurkers.setTactic(_lurkerTactic);
            _microLurkers.execute(cluster);
        }
        else
        {
            _microLurkers.regroup(_regroupPosition, cluster);
        }

        _regroupPosition = BWAPI::Positions::Invalid;       // don't reuse it by mistake
    }
}

// The cluster has no units which can fight.
// It should try to join another cluster, or else retreat to base.
bool Squad::noCombatUnits(const UnitCluster & cluster) const
{
    for (BWAPI::Unit unit : cluster.units)
    {
        if (UnitUtil::TypeCanAttack(unit->getType()))
        {
            return false;
        }
    }
    return true;
}

// The cluster has no enemies nearby.
// It will try to join another cluster, or to advance toward the goal.
bool Squad::notNearEnemy(const UnitCluster & cluster)
{
    for (BWAPI::Unit unit : cluster.units)
    {
        if (_nearEnemy[unit])
        {
            return false;
        }
    }
    return true;
}

// Try to merge this cluster with one ahead of it. Return true for success.
bool Squad::joinUp(const UnitCluster & cluster)
{
    if (_clusters.size() < 2)
    {
        // Nobody to join up with.
        return false;
    }

    // Move toward the closest other cluster which is closer to the goal.
    // Distance to goal is by squad order distance. Distance to the other cluster is air distance.
    int bestDistance = MAX_DISTANCE;
    const UnitCluster * bestCluster = nullptr;

    for (const UnitCluster & otherCluster : _clusters)
    {
        if (cluster.center != otherCluster.center &&
            getDistance(cluster.center) >= getDistance(otherCluster.center))
            // cluster.center.getApproxDistance(_order.getPosition()) >= otherCluster.center.getApproxDistance(_order.getPosition()))
        {
            int dist = cluster.center.getApproxDistance(otherCluster.center);
            if (dist < bestDistance)
            {
                bestDistance = dist;
                bestCluster = &otherCluster;
            }
        }
    }

    if (bestCluster)
    {
        moveCluster(cluster, bestCluster->center, true);
        return true;
    }

    return false;
}

// Move toward the given position.
// Parameter lazy defaults to false. If true, use MoveNear() which reduces APM and accuracy.
void Squad::moveCluster(const UnitCluster & cluster, const BWAPI::Position & destination, bool lazy)
{
    for (BWAPI::Unit unit : cluster.units)
    {
        // Only move units which don't arrange their own movement.
        // Queens do their own movement, but are not clustered and won't turn up here.
        if (unit->getType() != BWAPI::UnitTypes::Terran_Medic &&
            !_microDefilers.getUnits().contains(unit))      // defilers plus defiler food
        {
            if (!UnitUtil::MobilizeUnit(unit))
            {
                if (lazy)
                {
                    the.micro.MoveNear(unit, destination, _order.distances());
                }
                else
                {
                    the.micro.Move(unit, destination, _order.distances());
                }
            }
        }
    }
}

// If the unit needs to do some preparatory work before it can be put into a cluster,
// do a step of the prep work and return true.
bool Squad::unreadyUnit(BWAPI::Unit u)
{
    if (u->getType() == BWAPI::UnitTypes::Protoss_Reaver)
    {
        if (u->canTrain(BWAPI::UnitTypes::Protoss_Scarab) && !u->isTraining())
        {
            return the.micro.Make(u, BWAPI::UnitTypes::Protoss_Scarab);
        }
    }
    else if (u->getType() == BWAPI::UnitTypes::Protoss_Carrier)
    {
        if (u->canTrain(BWAPI::UnitTypes::Protoss_Interceptor) && !u->isTraining())
        {
            return the.micro.Make(u, BWAPI::UnitTypes::Protoss_Interceptor);
        }
    }

    return false;
}

bool Squad::isEmpty() const
{
    return _units.empty();
}

size_t Squad::getPriority() const
{
    return _priority;
}

void Squad::setPriority(const size_t & priority)
{
    _priority = priority;
}

void Squad::updateUnits()
{
    setAllUnits();
    setNearEnemyUnits();
    addUnitsToMicroManagers();
}

// Clean up the _units vector.
// Also notice and remember a few facts about the members of the squad.
// NOTE Some units may be loaded in a bunker or transport and cannot accept orders.
//      Check unit->isLoaded() before issuing orders.
void Squad::setAllUnits()
{
    _hasAir = false;
    _hasGround = false;
    _canAttackAir = false;
    _canAttackGround = false;

    BWAPI::Unitset goodUnits;
    for (BWAPI::Unit unit : _units)
    {
        if (UnitUtil::IsValidUnit(unit))
        {
            goodUnits.insert(unit);

            if (unit->isFlying())
            {
                if (!unit->getType().isDetector())    // mobile detectors don't count
                {
                    _hasAir = true;
                }
            }
            else
            {
                _hasGround = true;
            }
            if (UnitUtil::CanAttackAir(unit))
            {
                _canAttackAir = true;
            }
            if (UnitUtil::CanAttackGround(unit))
            {
                _canAttackGround = true;
            }
        }
    }
    _units = goodUnits;
}

void Squad::setNearEnemyUnits()
{
    _nearEnemy.clear();

    for (BWAPI::Unit unit : _units)
    {
        if (!unit->getPosition().isValid())   // excludes loaded units
        {
            continue;
        }

        _nearEnemy[unit] = unitNearEnemy(unit);

        if (Config::Debug::DrawSquadInfo)
        {
            if (_nearEnemy[unit])
            {
                int left = unit->getType().dimensionLeft();
                int right = unit->getType().dimensionRight();
                int top = unit->getType().dimensionUp();
                int bottom = unit->getType().dimensionDown();

                int x = unit->getPosition().x;
                int y = unit->getPosition().y;

                BWAPI::Broodwar->drawBoxMap(x - left, y - top, x + right, y + bottom,
                    Config::Debug::ColorUnitNearEnemy);
            }
        }
    }
}

// Pass the order on to all micromanagers that care about the order. (Some don't.)
void Squad::setOrderForMicroManagers()
{
    _microAirToAir.setOrder(_order);
    _microMelee.setOrder(_order);
    _microRanged.setOrder(_order);
    _microDetectors.setOrder(_order);
    _microHighTemplar.setOrder(_order);
    _microLurkers.setOrder(_order);
    _microMedics.setOrder(_order);
    //_microMutas.setOrder(_order);
    _microScourge.setOrder(_order);
    _microTanks.setOrder(_order);
    _microTransports.setOrder(_order);
}

void Squad::addUnitsToMicroManagers()
{
    BWAPI::Unitset irradiatedUnits;
    BWAPI::Unitset overlordUnits;
    BWAPI::Unitset airToAirUnits;
    BWAPI::Unitset meleeUnits;
    BWAPI::Unitset rangedUnits;
    BWAPI::Unitset defilerUnits;
    BWAPI::Unitset detectorUnits;
    BWAPI::Unitset highTemplarUnits;
    BWAPI::Unitset scourgeUnits;
    BWAPI::Unitset transportUnits;
    BWAPI::Unitset lurkerUnits;
    //BWAPI::Unitset mutaUnits;
    BWAPI::Unitset queenUnits;
    BWAPI::Unitset tankUnits;
    BWAPI::Unitset medicUnits;

    // We will assign zerglings as defiler food. The defiler micro manager will control them.
    int defilerFoodWanted = 0;

    // First grab the defilers, so we know how many there are.
    // Assign the minimum number of zerglings as food--check each defiler's energy level.
    // Remember where one of the defilers is, so we can assign nearby zerglings as food.
    BWAPI::Position defilerPos = BWAPI::Positions::None;
    for (BWAPI::Unit unit : _units)
    {
        if (unit->getType() == BWAPI::UnitTypes::Zerg_Defiler &&
            unit->isCompleted() && unit->exists() && unit->getHitPoints() > 0 && unit->getPosition().isValid())
        {
            defilerUnits.insert(unit);
            defilerPos = unit->getPosition();
            if (BWAPI::Broodwar->self()->hasResearched(BWAPI::TechTypes::Consume))
            {
                defilerFoodWanted += std::max(0, (199 - unit->getEnergy()) / 50);
            }
        }
    }

    for (BWAPI::Unit unit : _units)
    {
        if (unit->isCompleted() && unit->exists() && unit->getHitPoints() > 0 && unit->getPosition().isValid())
        {
            if (_name == "Irradiated")
            {
                irradiatedUnits.insert(unit);
            }
            else if (_name == "Overlord" && unit->getType() == BWAPI::UnitTypes::Zerg_Overlord)
            {
                // Special case for the Overlord squad: All overlords under control of MicroOverlords.
                overlordUnits.insert(unit);
            }
            else if (unit->getType() == BWAPI::UnitTypes::Terran_Valkyrie ||
                unit->getType() == BWAPI::UnitTypes::Protoss_Corsair ||
                unit->getType() == BWAPI::UnitTypes::Zerg_Devourer)
            {
                airToAirUnits.insert(unit);
            }
            else if (unit->getType() == BWAPI::UnitTypes::Protoss_High_Templar)
            {
                highTemplarUnits.insert(unit);
            }
            else if (unit->getType() == BWAPI::UnitTypes::Zerg_Lurker)
            {
                lurkerUnits.insert(unit);
            }
            //else if (unit->getType() == BWAPI::UnitTypes::Zerg_Mutalisk)
            //{
            //	mutaUnits.insert(unit);
            //}
            else if (unit->getType() == BWAPI::UnitTypes::Zerg_Scourge)
            {
                scourgeUnits.insert(unit);
            }
            else if (unit->getType() == BWAPI::UnitTypes::Zerg_Queen)
            {
                queenUnits.insert(unit);
            }
            else if (unit->getType() == BWAPI::UnitTypes::Terran_Medic)
            {
                medicUnits.insert(unit);
            }
            else if (unit->getType() == BWAPI::UnitTypes::Terran_Siege_Tank_Siege_Mode ||
                unit->getType() == BWAPI::UnitTypes::Terran_Siege_Tank_Tank_Mode)
            {
                tankUnits.insert(unit);
            }   
            else if (unit->getType().isDetector() && unit->getType().isFlyer())   // not a building
            {
                detectorUnits.insert(unit);
            }
            // NOTE This excludes overlords as transports (they are also detectors, a confusing case).
            else if (unit->getType() == BWAPI::UnitTypes::Protoss_Shuttle ||
                unit->getType() == BWAPI::UnitTypes::Terran_Dropship)
            {
                transportUnits.insert(unit);
            }
            // NOTE This excludes spellcasters (except arbiters, which have a regular weapon too).
            else if (unit->getType().groundWeapon().maxRange() > 32 ||
                unit->getType() == BWAPI::UnitTypes::Protoss_Reaver ||
                unit->getType() == BWAPI::UnitTypes::Protoss_Carrier)
            {
                rangedUnits.insert(unit);
            }
            else if (unit->getType().isWorker() && _combatSquad)
            {
                // If this is a combat squad, then workers are melee units like any other,
                // but we have to tell WorkerManager about them.
                // If it's not a combat squad, WorkerManager owns them; don't add them to a micromanager.
                WorkerManager::Instance().setCombatWorker(unit);
                meleeUnits.insert(unit);
            }
            // Melee units include firebats, which have range 32.
            else if (unit->getType().groundWeapon().maxRange() <= 32 &&     // melee range
                unit->getType().groundWeapon().maxRange() > 0)              // but can attack: not a spellcaster
            {
                meleeUnits.insert(unit);
            }
            // NOTE Some units may fall through and not be assigned. It's intentional.
        }
    }

    // If we want defiler food, find the nearest zerglings and pull them out of meleeUnits.
    while (defilerFoodWanted > 0)
    {
        BWAPI::Unit food = NearestOf(defilerPos, meleeUnits, BWAPI::UnitTypes::Zerg_Zergling);
        if (food)
        {
            defilerUnits.insert(food);
            meleeUnits.erase(food);
            --defilerFoodWanted;
        }
        else
        {
            // No zerglings left in meleeUnits (though there may be other unit types).
            break;
        }
    }

    _microIrradiated.setUnits(irradiatedUnits);
    _microOverlords.setUnits(overlordUnits);
    _microAirToAir.setUnits(airToAirUnits);
    _microMelee.setUnits(meleeUnits);
    _microRanged.setUnits(rangedUnits);
    _microDefilers.setUnits(defilerUnits);
    _microDetectors.setUnits(detectorUnits);
    _microHighTemplar.setUnits(highTemplarUnits);
    _microLurkers.setUnits(lurkerUnits);
    _microMedics.setUnits(medicUnits);
    //_microMutas.setUnits(mutaUnits);
    _microScourge.setUnits(scourgeUnits);
    _microQueens.setUnits(queenUnits);
    _microTanks.setUnits(tankUnits);
    _microTransports.setUnits(transportUnits);
}

// Calculates whether to regroup, aka retreat. Does combat sim if necessary.
// If we don't regroup, we attack.
bool Squad::needsToRegroup(UnitCluster & cluster)
{
    cluster.setExtraText("");

    // Our order may not allow us to regroup.
    if (!_order.isRegroupableOrder())
    {
        _regroupStatus = yellow + std::string("Never retreat!");
        return false;
    }

    // If we're nearly maxed and have good income or cash, don't retreat.
    if (the.self()->supplyUsed() >= 390 &&
        (the.self()->minerals() > 1000 || WorkerManager::Instance().getNumMineralWorkers() > 12))
    {
        _attackAtMax = true;
    }

    if (_attackAtMax)
    {
        if (BWAPI::Broodwar->self()->supplyUsed() < 320)
        {
            _attackAtMax = false;
        }
        else
        {
            _regroupStatus = green + std::string("Banzai!");
            return false;
        }
    }

    BWAPI::Unit vanguard = unitClosestToTarget(cluster.units);  // cluster vanguard (not squad vanguard)

    if (!vanguard)
    {
        _regroupStatus = yellow + std::string("No vanguard");
        return true;
    }

    const BWAPI::Position lastStand = finalRegroupPosition();

    // Is there static defense nearby that we should take into account?
    // The vanguard is known to be set thanks to the test immediately above.
    BWAPI::Unit defense = nearbyStaticDefense(vanguard->getPosition());
    if (defense)
    {
        // Don't retreat if we are in range of defense that is attacking.
        if (defense->getOrder() == BWAPI::Orders::AttackUnit)
        {
            _regroupStatus = green + std::string("Go defense!");
            return false;
        }

        // If there is defense to retreat to, try to get behind it wrt the enemy.
        const UnitCluster * enemyCluster = the.ops.getNearestEnemyClusterVs(cluster.center, !cluster.air, cluster.air);
        if (!enemyCluster)
        {
            _regroupStatus = green + std::string("Nothing to fear");
            return false;
        }

        if (defense->getDistance(cluster.center) < 128 &&
            cluster.center.getApproxDistance(enemyCluster->center) - defense->getDistance(enemyCluster->center) >= 32)
        {
            _regroupStatus = green + std::string("Behind defense");
            return false;
        }
    }
    else
    {
        // There is no defense to retreat to.
        // Have we retreated as far as we can?
        if (vanguard->getDistance(lastStand) < 224)
        {
            _regroupStatus = green + std::string("Back to the wall");
            return false;
        }
    }

    // -- --
    // All other checks are done. Finally do the expensive combat simulation.

    CombatSimulation & sim = the.combatSim;

    sim.setCombatUnits(cluster.units, vanguard->getPosition(), _combatSimRadius, _fightVisibleOnly);
    double score = sim.simulateCombat(_meatgrinder);
    bool attack = score >= 0.0;

    std::stringstream clusterText;
    if (Config::Debug::DrawClusters)
    {
        clusterText << white << "sim: " << (attack ? green : red) << score;
    }

    // Use the smoothing mechanism to average out recent results.
    the.micro.setAttack(cluster.units, attack);
    attack = the.micro.getSmoothedAttack(cluster.units);

    if (Config::Debug::DrawClusters)
    {
        clusterText << (attack ? green : red) << (attack ? " go" : "  back");
        cluster.setExtraText(clusterText.str());
    }

    if (attack)
    {
        _regroupStatus = green + std::string("Attack");
    }
    else
    {
        _regroupStatus = red + std::string("Retreat");

        /* Disabled because it works poorly in some important situations.
        // The combat sim says to retreat, but... can we?
        if (cluster.size() < 40)
        {
            _regroupPosition = calcRegroupPosition(cluster);
            sim.setCombatUnits(cluster.units, vanguard->getPosition(), _combatSimRadius, true);
            double retreatScore = sim.simulateRetreat(_regroupPosition);
            attack = retreatScore > 0.50;   // if losing more than this proportion, fight after all
        }
        if (attack)
        {
            _regroupStatus = green + std::string("Last stand");
        }
        */
    }

    return !attack;
}

BWAPI::Position Squad::calcRegroupPosition(const UnitCluster & cluster) const
{
    // 1. Retreat toward immobile defense, if any is near.
    BWAPI::Unit vanguard = unitClosestToTarget(cluster.units);  // cluster vanguard (not squad vanguard)

    if (vanguard)
    {
        BWAPI::Unit nearest = nearbyImmobileDefense(vanguard->getPosition());
        if (nearest)
        {
            BWAPI::Position behind = DistanceAndDirection(nearest->getPosition(), vanguard->getPosition(), -128);
            return behind;
        }
    }

    // 2. Regroup toward another cluster.
    // Look for a cluster nearby, and preferably closer to the enemy.
    const int riskRadius = (!cluster.air && the.info.enemyHasSiegeMode()) ? 12 * 32 : 8 * 32;
    BWAPI::Unit closestEnemy = BWAPI::Broodwar->getClosestUnit(cluster.center, BWAPI::Filter::IsEnemy && BWAPI::Filter::CanAttack, riskRadius);
    if (!closestEnemy)
    {
        const int distToOrder = getDistance(cluster.center);        // pixels by air or ground
        const UnitCluster * bestCluster = nullptr;
        int bestScore = INT_MIN;
        for (const UnitCluster & neighbor : _clusters)
        {
            int distToNeighbor = cluster.center.getApproxDistance(neighbor.center);
            // An air cluster may join a ground cluster, but not vice versa.
            if (distToNeighbor > 0 && cluster.air >= neighbor.air)
            {
                int score = distToOrder - getDistance(neighbor.center);
                if (neighbor.status == ClusterStatus::Attack)
                {
                    score += 3 * 32;
                }
                else if (neighbor.status == ClusterStatus::Regroup)
                {
                    score -= 32;
                }
                if (score > bestScore)
                {
                    bestCluster = &neighbor;
                    bestScore = score;
                }
            }
        }
        if (bestCluster)
        {
            return bestCluster->center;
        }
    }

    // 3. Retreat to the location of the cluster unit not near the enemy which is
    // closest to the order position. This tries to stay close while still out of enemy range.
    // Units in the cluster are all air or all ground and exclude mobile detectors.
    BWAPI::Position regroup(BWAPI::Positions::None);
    int minDist = MAX_DISTANCE;
    for (BWAPI::Unit unit : cluster.units)
    {
        // Count combat units only. Bug fix originally thanks to AIL, it's been rewritten since then.
        if (unit->exists() &&
            !_nearEnemy.at(unit) &&
            unit->getType() != BWAPI::UnitTypes::Terran_Medic &&
            unit->getPosition().isValid())      // excludes loaded units
        {
            int dist = unit->getDistance(_order.getPosition());
            if (dist < minDist)
            {
                // If the squad has any ground units, don't try to retreat to the position of a unit
                // which is in a place that we cannot reach.
                if (!_hasGround || -1 != the.map.getGroundTileDistance(unit->getPosition(), _order.getPosition()))
                {
                    minDist = dist;
                    regroup = unit->getPosition();
                }
            }
        }
    }
    if (regroup.isValid())
    {
        return regroup;
    }

    // 4. Retreat to a base we own.
    return finalRegroupPosition();
}

// Return the rearmost position we should retreat to, which puts our "back to the wall".
BWAPI::Position Squad::finalRegroupPosition() const
{
    // Retreat to the main base, unless we change our mind below.
    Base * base = the.bases.myMain();

    // If the natural has been taken, retreat there instead.
    Base * natural = the.bases.myNatural();
    if (natural && natural->getOwner() == the.self())
    {
        base = natural;
    }

    return base->getPosition();
}

bool Squad::containsUnit(BWAPI::Unit u) const
{
    return _units.contains(u);
}

BWAPI::Unit Squad::nearbyImmobileGroundDefense(const BWAPI::Position & pos) const
{
    if (the.selfRace() == BWAPI::Races::Terran)
    {
        return BWAPI::Broodwar->getClosestUnit(
            pos,
            (BWAPI::Filter::GetType == BWAPI::UnitTypes::Terran_Bunker || BWAPI::Filter::GetType == BWAPI::UnitTypes::Terran_Siege_Tank_Siege_Mode) && BWAPI::Filter::IsOwned && BWAPI::Filter::IsCompleted,
            ImmobileDefenseRadius
        );
    }

    if (the.selfRace() == BWAPI::Races::Protoss)
    {
        return BWAPI::Broodwar->getClosestUnit(
            pos,
            BWAPI::Filter::GetType == BWAPI::UnitTypes::Protoss_Photon_Cannon && BWAPI::Filter::IsOwned && BWAPI::Filter::IsCompleted,
            ImmobileDefenseRadius
        );
    }
    
    // Zerg.
    return BWAPI::Broodwar->getClosestUnit(
        pos,
        (BWAPI::Filter::GetType == BWAPI::UnitTypes::Zerg_Sunken_Colony || BWAPI::Filter::GetType == BWAPI::UnitTypes::Zerg_Lurker && BWAPI::Filter::IsBurrowed) && BWAPI::Filter::IsOwned && BWAPI::Filter::IsCompleted,
        ImmobileDefenseRadius
    );
}

// Immobile defense means static defense, sieged tank, or burrowed lurker.
BWAPI::Unit Squad::nearbyImmobileDefense(const BWAPI::Position & pos) const
{
    // NOTE What matters is whether the enemy has ground or air units.
    //      We are checking the wrong thing here. But it's usually correct anyway.

    if (hasGround())
    {
        return nearbyImmobileGroundDefense(pos);
    }

    BWAPI::Unit nearest = InformationManager::Instance().nearestAirStaticDefense(pos);
    if (nearest && nearest->getDistance(pos) < ImmobileDefenseRadius)
    {
        return nearest;
    }
    return nullptr;
}

BWAPI::Unit Squad::nearbyStaticDefense(const BWAPI::Position & pos) const
{
    BWAPI::Unit nearest = nullptr;

    // NOTE What matters is whether the enemy has ground or air units.
    //      We are checking the wrong thing here. But it's usually correct anyway.
    if (hasGround())
    {
        nearest = InformationManager::Instance().nearestGroundStaticDefense(pos);
    }
    else
    {
        nearest = InformationManager::Instance().nearestAirStaticDefense(pos);
    }
    if (nearest && nearest->getDistance(pos) < ImmobileDefenseRadius)
    {
        return nearest;
    }
    return nullptr;
}

bool Squad::containsUnitType(BWAPI::UnitType t) const
{
    for (BWAPI::Unit u : _units)
    {
        if (u->getType() == t)
        {
            return true;
        }
    }
    return false;
}

void Squad::clear()
{
    for (BWAPI::Unit unit : _units)
    {
        if (unit->getType().isWorker())
        {
            WorkerManager::Instance().finishedWithWorker(unit);
        }
    }

    _units.clear();
}

bool Squad::unitNearEnemy(BWAPI::Unit unit)
{
    UAB_ASSERT(unit, "missing unit");

    // A unit is near the enemy if it can be fired on.
    if (the.attacks(unit) > 0)
    {
        return true;
    }

    // NOTE The numbers match with CombatSimulation::getClosestEnemyCombatUnit().
    int safeDistance = (!unit->isFlying() && InformationManager::Instance().enemyHasSiegeMode()) ? 15*32 : 11*32;

    // For each enemy unit, visible or not.
    for (const auto & kv : InformationManager::Instance().getUnitData(BWAPI::Broodwar->enemy()).getUnits())
    {
        const UnitInfo & ui(kv.second);

        if (!ui.goneFromLastPosition)
        {
            if (unit->getDistance(ui.lastPosition) <= safeDistance)
            {
                return true;
            }
        }
    }
    return false;
}

// What map partition is the squad on?
// Not an easy question. The different units might be on different partitions.
// We simply pick a unit, any unit, and assume that that gives the partition.
int Squad::mapPartition() const
{
    // Default to our starting position.
    BWAPI::Position pos = the.bases.myStart()->getPosition();

    // Pick any unit with a position on the map (not, for example, in a bunker).
    for (BWAPI::Unit unit : _units)
    {
        if (unit->getPosition().isValid())
        {
            pos = unit->getPosition();
            break;
        }
    }

    return the.partitions.id(pos);
}

// NOTE The squad center is a geometric center. It ignores terrain.
// The center might be on unwalkable ground, or even on a different island.
BWAPI::Position Squad::calcCenter() const
{
    if (_units.empty())
    {
        return the.bases.myStart()->getPosition();
    }

    BWAPI::Position accum(0,0);
    for (BWAPI::Unit unit : _units)
    {
        if (unit->getPosition().isValid())
        {
            accum += unit->getPosition();
        }
    }
    return BWAPI::Position(accum.x / _units.size(), accum.y / _units.size());
}

BWAPI::Unit Squad::unitClosestToPosition(const BWAPI::Position & pos, const BWAPI::Unitset & units) const
{
    UAB_ASSERT(pos.isValid(), "bad order position");

    BWAPI::Unit closest = nullptr;
    int closestDist = MAX_DISTANCE;

    for (BWAPI::Unit unit : units)
    {
        // Non-combat units should be ignored for this calculation.
        // If the cluster contains only these units, we'll return null.
        if (unit->getType().isDetector() ||
            !unit->getPosition().isValid() ||       // includes units loaded into bunkers or transports
            unit->getType() == BWAPI::UnitTypes::Terran_Medic ||
            unit->getType() == BWAPI::UnitTypes::Protoss_High_Templar ||
            unit->getType() == BWAPI::UnitTypes::Protoss_Dark_Archon ||
            unit->getType() == BWAPI::UnitTypes::Zerg_Defiler ||
            unit->getType() == BWAPI::UnitTypes::Zerg_Queen)
        {
            continue;
        }

        int dist;
        if (_hasGround)
        {
            // A ground or air-ground group. Use ground distance.
            // It is -1 if no ground path exists.
            dist = the.map.getGroundDistance(unit->getPosition(), pos);
        }
        else
        {
            // An all-air group. Use air distance (which is what unit->getDistance() gives).
            dist = unit->getDistance(pos);
        }

        if (dist < closestDist && dist != -1)
        {
            closest = unit;
            closestDist = dist;
        }
    }

    return closest;
}

// Return the unit closest to the order position.
// The order position might be an enemy position, or a defense position in our base.
BWAPI::Unit Squad::unitClosestToTarget(const BWAPI::Unitset & units) const
{
    return unitClosestToPosition(_order.getPosition(), units);
}

const BWAPI::Unitset & Squad::getUnits() const	
{ 
    return _units; 
} 

void Squad::setOrder(SquadOrder & so)
{
    if (_order != so)
    {
        _orderFrame = the.now();
    }

    // Always move the order, to avoid memory leaks.
    _order = std::move(so);     // `so` should not be reused
    setOrderForMicroManagers();
}

SquadOrder & Squad::getOrder()
{ 
    return _order; 
}

void Squad::setLurkerTactic(LurkerTactic tactic)
{
    _lurkerTactic = tactic;
}

const std::string Squad::getRegroupStatus() const
{
    return _regroupStatus;
}

void Squad::addUnit(BWAPI::Unit u)
{
    _units.insert(u);
}

void Squad::removeUnit(BWAPI::Unit u)
{
    if (_combatSquad && u->getType().isWorker())
    {
        WorkerManager::Instance().finishedWithWorker(u);
    }
    _units.erase(u);
}

// Remove all workers from the squad, releasing them back to WorkerManager.
void Squad::releaseWorkers()
{
    for (auto it = _units.begin(); it != _units.end(); )
    {
        if (_combatSquad && (*it)->getType().isWorker())
        {
            WorkerManager::Instance().finishedWithWorker(*it);
            it = _units.erase(it);
        }
        else
        {
            ++it;
        }
    }
}

const std::string & Squad::getName() const
{
    return _name;
}

// This is a watch squad. Handle special cases.
// 1. If a unit is at its assigned watch position, burrow it if possible.
// 2. If a unit is burrowed and no detector is in view, leave it there.
// If there's an enemy detector, stay above ground to fight (or run away).
bool Squad::maybeWatch()
{
    for (BWAPI::Unit u : _units)
    {
        if (!u->isBurrowed())
        {
            if (u->canBurrow() &&
                u->getDistance(_order.getPosition()) < 8 &&
                !UnitUtil::EnemyDetectorInRange(u->getPosition()))
            {
                the.micro.Burrow(u);
                return true;
            }
        }
        else
        {
            // It's burrowed.
            if (!UnitUtil::EnemyDetectorInRange(u->getPosition()) ||
                nullptr == BWAPI::Broodwar->getClosestUnit(
                    u->getPosition(),
                    BWAPI::Filter::IsEnemy && BWAPI::Filter::GroundWeapon != BWAPI::WeaponTypes::None,
                    6 * 32
                ))
            {
                // Return true to indicate that no action is to be taken.
                // In particular, don't unburrow to retreat.
                return true;
            }
        }
    }

    return false;
}

// The drop squad has been given a Load order. Load up the transports for a drop.
// Unlike other code in the drop system, this supports any number of transports, including zero.
// Called once per frame while a Load order is in effect.
void Squad::loadTransport()
{
    for (BWAPI::Unit trooper : _units)
    {
        // If it's not the transport itself, send it toward the order location,
        // which is set to the transport's initial location.
        if (trooper->exists() && !trooper->isLoaded() && trooper->getType().spaceProvided() == 0)
        {
            the.micro.Move(trooper, _order.getPosition());
        }
    }

    for (const auto transport : _microTransports.getUnits())
    {
        if (!transport->exists())
        {
            continue;
        }
        
        for (BWAPI::Unit unit : _units)
        {
            if (transport->getSpaceRemaining() == 0)
            {
                break;
            }

            if (the.micro.Load(transport, unit))
            {
                break;
            }
        }
    }
}

// Stim marines and firebats if possible and appropriate.
// This stims for combat. It doesn't consider stim to travel faster.
// We bypass the micro managers because it simplifies the bookkeeping, but a disadvantage
// is that we don't have access to the target list. Should refactor a little to get that,
// because it can help us figure out how important it is to stim.
void Squad::stimIfNeeded()
{
    // Do we have stim?
    if (BWAPI::Broodwar->self()->getRace() != BWAPI::Races::Terran ||
        !BWAPI::Broodwar->self()->hasResearched(BWAPI::TechTypes::Stim_Packs))
    {
        return;
    }

    // Are there enemies nearby that we may want to fight?
    if (_nearEnemy.empty())
    {
        return;
    }

    // So far so good. Time to get into details.

    // Stim can be used more freely if we have medics with lots of energy.
    int totalMedicEnergy = _microMedics.getTotalEnergy();

    // Stim costs 10 HP, which requires 5 energy for a medic to heal.
    // To reduce overstim, pretend it costs more than that.
    const int stimEnergyCost = 10;

    // Firebats first, because they are likely to be right up against the enemy.
    for (auto firebat : _microMelee.getUnits())
    {
        // Invalid position means the firebat is probably in a bunker or transport.
        if (firebat->getType() != BWAPI::UnitTypes::Terran_Firebat || !firebat->getPosition().isValid())
        {
            continue;
        }
        // Don't overstim and lose too many HP.
        if (firebat->isStimmed() || firebat->getHitPoints() < 35 || totalMedicEnergy <= 0 && firebat->getHitPoints() < 45)
        {
            continue;
        }

        BWAPI::Unitset nearbyEnemies;
        MapGrid::Instance().getUnits(nearbyEnemies, firebat->getPosition(), 64, false, true);

        // NOTE We don't check whether the enemy is attackable or worth attacking.
        if (!nearbyEnemies.empty())
        {
            the.micro.Stim(firebat);
            totalMedicEnergy -= stimEnergyCost;
        }
    }

    // Next marines, treated the same except for range and hit points.
    for (auto marine : _microRanged.getUnits())
    {
        // Invalid position means the marine is probably in a bunker or transport.
        if (marine->getType() != BWAPI::UnitTypes::Terran_Marine || !marine->getPosition().isValid())
        {
            continue;
        }
        // Don't overstim and lose too many HP.
        if (marine->isStimmed() || marine->getHitPoints() <= 30 || totalMedicEnergy <= 0 && marine->getHitPoints() < 40)
        {
            continue;
        }

        BWAPI::Unitset nearbyEnemies;
        MapGrid::Instance().getUnits(nearbyEnemies, marine->getPosition(), 5 * 32, false, true);

        if (!nearbyEnemies.empty())
        {
            the.micro.Stim(marine);
            totalMedicEnergy -= stimEnergyCost;
        }
    }
}

bool Squad::hasCombatUnits() const
{
    // If the only units we have are detectors, then we have no combat units.
    return !(_units.empty() || _units.size() == _microDetectors.getUnits().size());
}

// Is every unit in the squad an overlord hunter (or a detector)?
// An overlord hunter is a fast air unit that is strong against overlords.
bool Squad::isOverlordHunterSquad() const
{
    if (!hasCombatUnits())
    {
        return false;
    }

    for (BWAPI::Unit unit : _units)
    {
        const BWAPI::UnitType type = unit->getType();
        if (!type.isFlyer())
        {
            return false;
        }
        if (!type.isDetector() &&
            type != BWAPI::UnitTypes::Terran_Wraith &&
            type != BWAPI::UnitTypes::Terran_Valkyrie &&
            type != BWAPI::UnitTypes::Zerg_Mutalisk &&
            type != BWAPI::UnitTypes::Zerg_Scourge &&      // questionable, but the squad may have both
            type != BWAPI::UnitTypes::Protoss_Corsair &&
            type != BWAPI::UnitTypes::Protoss_Scout)
        {
            return false;
        }
    }
    return true;
}

// Pixel distance from the given tile to the order position.
int Squad::getDistance(const BWAPI::TilePosition & tile) const
{
    if (_order.distances())
    {
        return 32 * _order.distances()->at(tile);
    }

    return _order.getPosition().getApproxDistance(TileCenter(tile));
}

// Pixel distance from the given position to the order position.
int Squad::getDistance(const BWAPI::Position & pos) const
{
    if (_order.distances())
    {
        return 32 * _order.distances()->at(pos);
    }

    return _order.getPosition().getApproxDistance(pos);
}

void Squad::drawCluster(const UnitCluster & cluster) const
{
    if (Config::Debug::DrawClusters)
    {
        cluster.draw(BWAPI::Colors::Grey, white + _name + ' ' + _regroupStatus);
    }
}
