#include "SkillGasSteal.h"

#include "Bases.h"
#include "Config.h"
#include "InformationManager.h"
#include "OpponentModel.h"
#include "Random.h"
#include "ScoutManager.h"
#include "The.h"

using namespace UAlbertaBot;

void SkillGasSteal::analyzeRecords()
{
    int totalStealGasTiming = 0;
    int totalOtherGasTiming = 0;
    int totalLifetime = 0;
    int totalStealGasUseGames = 0;
    int totalOtherGasUseGames = 0;

    for (const GameRecord * record : OpponentModel::Instance().getRecords())
    {
        if (OpponentModel::Instance().sameMatchup(*record))
        {
            const std::vector<int> * info = record->getSkillInfo(this, 0);
            if (info)
            {
                GasStealRecord r(info);
                if (r.executeFrame)
                {
                    tryInfo.sameWins += int(record->getWin());
                    tryInfo.sameGames += 1;
                }
                else
                {
                    tryInfo.otherWins += int(record->getWin());
                    tryInfo.otherGames += 1;
                }
                if (r.successFrame)
                {
                    successInfo.sameWins += int(record->getWin());
                    successInfo.sameGames += 1;

                    totalLifetime += r.lifetime;
                    if (record->getFrameEnemyUsesGas())     // zero means never
                    {
                        ++totalStealGasUseGames;
                        totalStealGasTiming += record->getFrameEnemyUsesGas();
                    }
                }
                else
                {
                    successInfo.otherWins += int(record->getWin());
                    successInfo.otherGames += 1;

                    if (record->getFrameEnemyUsesGas())     // zero means never
                    {
                        ++totalOtherGasUseGames;
                        totalOtherGasTiming += record->getFrameEnemyUsesGas();
                    }

                }
            }
        }
    }

    successLifetime = successInfo.sameGames ? totalLifetime / successInfo.sameGames : 0;
    stealGasTiming = totalStealGasUseGames ? totalStealGasTiming / totalStealGasUseGames : 0;
    otherGasTiming = totalOtherGasUseGames ? totalOtherGasTiming / totalOtherGasUseGames : 0;
}

// Something indicates that a gas steal will never occur.
bool SkillGasSteal::useless() const
{
    return
        _failed ||

        // We have lost few games, so as far as we know we may win anyway. Don't try yet.
        tryInfo.otherGames - tryInfo.otherWins <= 8 ||

        // We tried a lot of times and rarely succeeded.
        tryInfo.sameGames >= 10 && tryInfo.sameGames > 10 * successInfo.sameGames ||

        // Not if the enemy has already used gas.
        the.info.enemyGasTiming() ||

        // This enemy doesn't get gas anyway.
        !otherGasTiming;
}

// -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- --

SkillGasSteal::SkillGasSteal()
    : Skill("gas steal")
    , _initialized(false)
    , _failed(false)
{
}

// gas steal: <execute frame> <success frame> <lifetime>
std::string SkillGasSteal::putData() const
{
    std::stringstream s;

    s << record.executeFrame << ' ' << record.successFrame << ' ' << record.lifetime;
    return s.str();
}

void SkillGasSteal::getData(GameRecord & record, const std::string & line)
{
    std::stringstream s(line);

    GasStealRecord r;
    if (s >> r.executeFrame >> r.successFrame >> r.lifetime)
    {
        record.setSkillInfo(this, 0, std::vector<int>({ r.executeFrame, r.successFrame, r.lifetime }));
    }
    // Else fail silently.
}

bool SkillGasSteal::enabled() const
{
    return
        Config::Skills::GasSteal &&
        !the.bases.isIslandStart();
}

void SkillGasSteal::update()
{
    if (!_initialized)
    {
        analyzeRecords();
        _initialized = true;
    }

    if (useless())
    {
        //BWAPI::Broodwar->printf("gas steal useless @ %d", the.now());
        _failed = true;
        _nextUpdateFrame = MAX_FRAME;
        return;
    }

    const Base * enemyStart = the.bases.enemyStart();
    BWAPI::Unit geyser =
        enemyStart && enemyStart->getGeysers().size() == 1 ? *(enemyStart->getGeysers().begin()) : nullptr;

    if (record.executeFrame)
    {
        if (!record.successFrame && geyser && geyser->isVisible() && geyser->getPlayer() == the.self())
        {
            //BWAPI::Broodwar->printf("gas steal success @ %d", the.now());
            record.successFrame = the.now();
        }
        else if (record.successFrame && !record.lifetime && geyser && (!geyser->isVisible() || geyser->getPlayer() != the.self()))
        {
            //BWAPI::Broodwar->printf("gas steal lifetime = %d", the.now());
            record.lifetime = the.now();
            _nextUpdateFrame = MAX_FRAME;
            return;
        }

        // Gas steal attempt is over without success.
        if (!record.successFrame &&
            (ScoutManager::Instance().gasStealOver() || !ScoutManager::Instance().getWorkerScout()))
        {
            //BWAPI::Broodwar->printf("gas steal failed to make refinery @ %d", the.now());
            _failed = true;
            _nextUpdateFrame = MAX_FRAME;
            return;
        }
    }

    // If the enemy has already used gas, give up.
    if (!record.successFrame && the.info.enemyGasTiming() && the.info.enemyGasTiming() <= the.now())
    {
        //BWAPI::Broodwar->printf("no gas steal, enemy used gas");
        _failed = true;
        _nextUpdateFrame = MAX_FRAME;
        return;
    }

    // Did the enemy take it before we could?
    if (!record.successFrame && geyser && geyser->isVisible() && geyser->getPlayer() != the.neutral())
    {
        //BWAPI::Broodwar->printf("no gas steal, enemy got it first @ %d", the.now());
        _failed = true;
        _nextUpdateFrame = MAX_FRAME;
        return;
    }

    int delay = (ScoutManager::Instance().getWorkerScout() || record.executeFrame) ? 15 : 60;
    _nextUpdateFrame = the.now() + delay;
}

// Only steal gas once, after we've located the enemy base, and if it has exactly 1 geyser to steal.
bool SkillGasSteal::feasible() const
{
    const Base * enemyStart = the.bases.enemyStart();
    return
        !record.executeFrame &&
        !_failed &&
        enemyStart &&
        enemyStart->getInitialGeysers().size() == 1 &&
        !the.info.isGeyserTaken(*enemyStart->getInitialGeysers().begin());
}

bool SkillGasSteal::good() const
{
   // Cases where we probably don't want to steal gas.
    if (
        // Only once a worker scout is assigned. We don't send a worker, we wait for one.
        !ScoutManager::Instance().getWorkerScout() ||

        // The scout worker is not at the enemy base yet. Decide later.
        the.zone.at(ScoutManager::Instance().getWorkerScout()->getTilePosition()) != the.zone.at(the.bases.enemyStart()->getTilePosition()) ||

        // Not if the enemy is playing a strategy that doesn't need gas.
        OpponentModel::Instance().getDarnLikelyEnemyPlan() == OpeningPlan::WorkerRush ||
        OpponentModel::Instance().getDarnLikelyEnemyPlan() == OpeningPlan::FastRush ||

        // The enemy is expected to get gas far in the future, with ample time to clear the steal.
        tryInfo.otherGames > 0 && otherGasTiming >= the.now() + 4 * 60 * 24 ||

        // The enemy can get gas elsewhere.
        the.bases.geyserCount(the.enemy()) >= 2
    )
    {
        return false;
    }

    // Try gas steal randomly up to a limit.
    if (tryInfo.sameGames < 5)
    {
        //BWAPI::Broodwar->printf("randomly steal gas 0.5 of the time");
        return the.random.flag(0.5);
    }

    // Otherwise, actually weigh the evidence. We have >= 5 attempted gas steals.
    OpponentModel::OpeningInfoType info = tryInfo;

    // Adjust the data to reflect preconceptions.
    // With experience, the preconceptions will be washed out and stop mattering.
    if (the.your.seen.count(BWAPI::UnitTypes::Terran_Barracks) >= 2 ||
        the.your.seen.count(BWAPI::UnitTypes::Protoss_Gateway) >= 2)
    {
        //BWAPI::Broodwar->printf("gas steal weak versus 2 rax or 2 gate");
        info.sameGames += 2;
    }
    else if (the.enemyRace() == BWAPI::Races::Protoss &&
        (OpponentModel::Instance().getDarnLikelyEnemyPlan() == OpeningPlan::Turtle ||
        OpponentModel::Instance().getDarnLikelyEnemyPlan() == OpeningPlan::SafeExpand))
    {
        //BWAPI::Broodwar->printf("gas steal weak versus cannons");
        info.sameGames += 3;
    }
    else if (the.selfRace() == BWAPI::Races::Protoss && the.enemyRace() == BWAPI::Races::Terran)
    {
        //BWAPI::Broodwar->printf("gas steal PvT strong");
        info.sameWins += 2;
        info.sameGames += 2;
    }

    double stealWinRate = info.sameGames ? info.sameWins / double(info.sameGames) : 0.0;
    double otherWinRate = info.otherGames ? info.otherWins / double(info.otherGames) : 1.0;

    // Probability of steal at 0.5 with no evidence, moving toward 0 or 1 as games increase.
    double r = std::min(15, std::min(info.sameGames, info.otherGames)) / 15.0;
    if (stealWinRate <= otherWinRate)
    {
        r = -r;
    }
    double p = 0.5 + 0.5 * r;

    //BWAPI::Broodwar->printf("gas steal with p = %g", p);
    return the.random.flag(p);
}

void SkillGasSteal::execute()
{
    // If we have a scout worker, and it is still alive, it will be given the task.
    // If no worker has been sent to scout, then setGasSteal() sends one to steal gas.
    ScoutManager::Instance().setGasSteal();
    record.executeFrame = the.now();
    //BWAPI::Broodwar->printf("gas steal execute @ %d", record.executeFrame);
}
