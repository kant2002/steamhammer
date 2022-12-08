#pragma once

#include "Common.h"
#include "Squad.h"
#include "SquadData.h"
#include "InformationManager.h"
#include "StrategyManager.h"
#include "TacticsOrders.h"
#include "The.h"

namespace UAlbertaBot
{
class CombatCommander
{
    SquadData       _squadData;
    BWAPI::Unitset  _combatUnits;
    bool            _initialized;

    bool			_goAggressive;                  // set by opening book and/or code

    LurkerOrders    _lurkerOrders;

    BWAPI::Position	_scourgeTarget;

    bool            _isWatching;                    // watch squad activated

    bool            _reconSquadAlive;               // recon squad activated
    Base *          _reconTarget;
    int				_lastReconTargetChange;         // frame number

    int				_carrierCount;					// how many carriers?

    void            updateIdleSquad();
    void            updateIrradiatedSquad();
    void            updateOverlordSquad();
    void			updateScourgeSquad();
    void            updateAttackSquads();
    void			updateReconSquad();
    void			updateWatchSquads();
    void            updateBaseDefenseSquads();
    void            updateScoutDefenseSquad();
    void            updateDropSquads();

    bool            wantSquadDetectors() const;
    void			maybeAssignDetector(Squad & squad, bool wantDetector);

    void			loadOrUnloadBunkers();
    void			doComsatScan();
    void			doLarvaTrick();

    int				weighReconUnit(const BWAPI::Unit unit) const;
    int				weighReconUnit(const BWAPI::UnitType type) const;

    bool			isFlyingSquadUnit(const BWAPI::UnitType type) const;
    bool			isOptionalFlyingSquadUnit(const BWAPI::UnitType type) const;
    bool			isGroundSquadUnit(const BWAPI::UnitType type) const;

    bool			unitIsGoodToDrop(const BWAPI::Unit unit) const;

    void			cancelDyingItems();

    BWAPI::Unit     findClosestDefender(const Squad & defenseSquad, BWAPI::Position pos, bool flyingDefender, bool pullWoekers, bool enemyHasAntiAir);
    BWAPI::Unit     findClosestWorkerToTarget(BWAPI::Unitset & unitsToAssign, BWAPI::Unit target);

    void			chooseScourgeTarget(const Squad & squad);
    void			chooseReconTarget(const Squad & squad);
    Base *          getReconLocation() const;
    SquadOrder		getAttackOrder(Squad * squad);
    void            getAttackLocation(Squad * squad, Base * & base, BWAPI::Position & pos, std::string & returnKey);
    bool            defendedTarget(const BWAPI::Position & pos, bool vsGround, bool vsAir) const;
    BWAPI::Position getDropLocation(const Squad & squad);
    Base *          getDefensiveBase();

    void            initializeSquads();

    void            updateDefenseSquadUnits(Squad & defenseSquad, const size_t & flyingDefendersNeeded, const size_t & groundDefendersNeeded, bool pullWorkers, bool enemyHasAntiAir);

    int             numZerglingsInOurBase() const;
    bool            buildingRush() const;

    static int		workerPullScore(BWAPI::Unit worker);

public:

    CombatCommander();

    void update(const BWAPI::Unitset & combatUnits);
    void onEnd();

    void setAggression(bool aggressive) { _goAggressive = aggressive;  }
    bool getAggression() const { return _goAggressive; };

    void setGeneralLurkerTactic(LurkerTactic tactic);
    void addLurkerOrder(LurkerOrder & order);
    void clearLurkerOrder(LurkerTactic tactic);
    
    void pullWorkers(int n);
    void releaseWorkers();
    
    void drawSquadInformation(int x, int y);

    static CombatCommander & Instance();
};
}
