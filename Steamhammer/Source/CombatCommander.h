#pragma once

#include "Common.h"
#include "Squad.h"
#include "SquadData.h"
#include "InformationManager.h"
#include "StrategyManager.h"

namespace UAlbertaBot
{
class CombatCommander
{
	SquadData       _squadData;
    BWAPI::Unitset  _combatUnits;
    bool            _initialized;

	bool			_goAggressive;

    void            updateScoutDefenseSquad();
	void            updateBaseDefenseSquads();
	void            updateAttackSquads();
    void            updateDropSquads();
	void            updateIdleSquad();
	void			updateSurveySquad();

	void			loadOrUnloadBunkers();
	void			doComsatScan();

	bool			unitIsGoodToDrop(const BWAPI::Unit unit) const;

	void			cancelDyingBuildings();

	int             getNumType(BWAPI::Unitset & units, BWAPI::UnitType type);

	BWAPI::Unit     findClosestDefender(const Squad & defenseSquad, BWAPI::Position pos, bool flyingDefender, bool pullWoekers);
    BWAPI::Unit     findClosestWorkerToTarget(BWAPI::Unitset & unitsToAssign, BWAPI::Unit target);

	BWAPI::Position getDefendLocation();
	BWAPI::Position getMainAttackLocation(const Squad * squad);
	BWAPI::Position getSurveyLocation();

    void            initializeSquads();
    void            assignFlyingDefender(Squad & squad);
    void            emptySquad(Squad & squad, BWAPI::Unitset & unitsToAssign);
    int             getNumGroundDefendersInSquad(Squad & squad);
    int             getNumAirDefendersInSquad(Squad & squad);

    void            updateDefenseSquadUnits(Squad & defenseSquad, const size_t & flyingDefendersNeeded, const size_t & groundDefendersNeeded, bool pullWorkers);

    int             numZerglingsInOurBase() const;
    bool            buildingRush() const;

public:

	CombatCommander();

	void update(const BWAPI::Unitset & combatUnits);

	void setAggression(bool aggressive) { _goAggressive = aggressive;  }
	bool getAggression() const { return _goAggressive; };
    
	void drawSquadInformation(int x, int y);

	static CombatCommander & Instance();
};
}
