#pragma once

#include <BWAPI.h>
#include "MacroCommand.h"

namespace UAlbertaBot
{
class Base;
class The;

class ScoutManager 
{
    BWAPI::Unit			_overlordScout;
    BWAPI::Unit			_workerScout;
    std::string         _scoutStatus;
    std::string         _gasStealStatus;
    MacroCommandType	_scoutCommand;
    Base *              _overlordScoutTarget;   // only while still seeking the enemy base
    Base *              _workerScoutTarget;     // only while still seeking the enemy base
    bool				_overlordAtEnemyBase;
    BWAPI::Position     _overlordAtBaseTarget;  // look around inside/near the enemy base
    bool			    _scoutUnderAttack;
    bool				_tryGasSteal;
    BWAPI::Unit			_enemyGeyser;
    bool                _startedGasSteal;
    bool				_queuedGasSteal;
    bool				_gasStealOver;
    int                 _previousScoutHP;
    BWAPI::Position		_nextDestination;

    ScoutManager();

    void				setScoutTargets();

    bool                releaseScoutEarly(BWAPI::Unit worker) const;
    bool                enemyWorkerInRadius();
    bool                gasSteal();
    BWAPI::Unit			getAnyEnemyGeyser() const;
    BWAPI::Unit			getTheEnemyGeyser() const;
    BWAPI::Unit			enemyWorkerToHarass() const;
    void                moveGroundScout();
    void                followGroundPath();
    void                moveAirScout();
    void                moveAirScoutAroundEnemyBase();
    void                drawScoutInformation(int x, int y);

    bool                overlordBlockedByAirDefense() const;
    void                releaseOverlordScout();

public:

    static ScoutManager & Instance();

    void update();

    bool shouldScout();
    
    void setOverlordScout(BWAPI::Unit unit);
    void setWorkerScout(BWAPI::Unit unit);
    BWAPI::Unit getWorkerScout() const { return _workerScout; };
    void releaseWorkerScout();

    void setGasSteal() { _tryGasSteal = true; };
    bool tryGasSteal() const { return _tryGasSteal; };
    bool wantGasSteal() const { return _tryGasSteal && !_gasStealOver; };
    bool gasStealQueued() const { return _queuedGasSteal; };
    bool gasStealOver() const { return _gasStealOver; };
    void setGasStealOver() { _gasStealOver = true; };    // called by BuildingManager when releasing the worker

    void setScoutCommand(MacroCommandType cmd);
};
}