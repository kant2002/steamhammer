#include "GameRecordNow.h"

#include "Bases.h"
#include "GameCommander.h"
#include "OpponentModel.h"
#include "ScoutManager.h"

using namespace UAlbertaBot;

void GameRecordNow::writeSkills(std::ostream & output) const
{
    the.skillkit.write(output);
}

// Take a digest snapshot of the game situation.
void GameRecordNow::takeSnapshot()
{
    snapshots.push_back(new GameSnapshot(PlayerSnapshot (BWAPI::Broodwar->self()), PlayerSnapshot (BWAPI::Broodwar->enemy())));
}

// Figure out whether the enemy has seen our base yet.
bool GameRecordNow::enemyScoutedUs() const
{
    Base * base = Bases::Instance().myStart();

    for (const auto & kv : InformationManager::Instance().getUnitData(BWAPI::Broodwar->enemy()).getUnits())
    {
        const UnitInfo & ui(kv.second);

        // If a unit was last spotted close to us, assume we've been seen.
        if (ui.lastPosition.getDistance(base->getCenter()) < 800)
        {
            return true;
        }
    }

    return false;
}

// -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- --

// Constructor for the record of the current game.
// When this object is initialized, the opening and some other items are not yet known.
GameRecordNow::GameRecordNow()
{
}

// Called when the game is over.
void GameRecordNow::setWin(bool isWinner)
{
    win = isWinner;
    frameGameEnds = BWAPI::Broodwar->getFrameCount();
}

void GameRecordNow::update()
{
    int now = BWAPI::Broodwar->getFrameCount();

    // Update the when-it-happens frame counters. We don't actually need to check often.
    if (now % 32 == 30)
    {
        if (enemyRace == BWAPI::Races::Unknown)
        {
            enemyRace = BWAPI::Broodwar->enemy()->getRace();
        }
        if (!enemyStartingBaseID && the.bases.enemyStart())
        {
            enemyStartingBaseID = the.bases.enemyStart()->getID();
        }
        enemyPlan = OpponentModel::Instance().getEnemyPlan();
        if (!frameWeMadeFirstCombatUnit && InformationManager::Instance().weHaveCombatUnits())
        {
            frameWeMadeFirstCombatUnit = now;
        }
        if (!frameWeGatheredGas && BWAPI::Broodwar->self()->gatheredGas() > 0)
        {
            frameWeGatheredGas = now;
        }
        if (!frameEnemyScoutsOurBase && enemyScoutedUs())
        {
            frameEnemyScoutsOurBase = now;
        }
        if (!frameEnemyGetsCombatUnits && InformationManager::Instance().enemyHasCombatUnits())
        {
            frameEnemyGetsCombatUnits = now;
        }
        if (!frameEnemyUsesGas && InformationManager::Instance().enemyGasTiming() > 0)
        {
            frameEnemyUsesGas = InformationManager::Instance().enemyGasTiming();
        }
        if (!frameEnemyGetsAirUnits && InformationManager::Instance().enemyHasAirTech())
        {
            frameEnemyGetsAirUnits = now;
        }
        if (!frameEnemyGetsStaticAntiAir && InformationManager::Instance().enemyHasStaticAntiAir())
        {
            frameEnemyGetsStaticAntiAir = now;
        }
        if (!frameEnemyGetsMobileAntiAir && InformationManager::Instance().enemyHasAntiAir())
        {
            frameEnemyGetsMobileAntiAir = now;
        }
        if (!frameEnemyGetsCloakedUnits && InformationManager::Instance().enemyHasCloakTech())
        {
            frameEnemyGetsCloakedUnits = now;
        }
        if (!frameEnemyGetsStaticDetection && InformationManager::Instance().enemyHasStaticDetection())
        {
            frameEnemyGetsStaticDetection = now;
        }
        if (!frameEnemyGetsMobileDetection && InformationManager::Instance().enemyHasMobileDetection())
        {
            frameEnemyGetsMobileDetection = now;
        }
    }

    /* disabled
    // If it's time, take a snapshot.
    int sinceFirst = now - firstSnapshotTime;
    if (sinceFirst >= 0 && sinceFirst % snapshotInterval == 0)
    {
        takeSnapshot();
    }
    */
}
