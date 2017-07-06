#pragma once

#include "Common.h"
#include "CombatCommander.h"
#include "InformationManager.h"
#include "MapGrid.h"
#include "WorkerManager.h"
#include "ProductionManager.h"
#include "BuildingManager.h"
#include "ScoutManager.h"
#include "StrategyManager.h"
#include "TimerManager.h"

namespace UAlbertaBot
{

class UnitToAssign
{
public:

	BWAPI::Unit unit;
	bool isAssigned;

	UnitToAssign(BWAPI::Unit u)
	{
		unit = u;
		isAssigned = false;
	}
};

class GameCommander 
{
	CombatCommander &		_combatCommander;
	TimerManager		    _timerManager;

	BWAPI::Unitset          _validUnits;
	BWAPI::Unitset          _combatUnits;
	BWAPI::Unitset          _scoutUnits;

	int						_surrenderTime;    // for giving up early

    bool                    _initialScoutSet;
	bool					_scoutAlways;
	bool					_scoutIfNeeded;

    void                    assignUnit(BWAPI::Unit unit, BWAPI::Unitset & set);
	bool                    isAssigned(BWAPI::Unit unit) const;

	bool					surrenderMonkey();

public:

	GameCommander();
	~GameCommander() {};

	void update();

	void handleUnitAssignments();
	void setValidUnits();
	void setScoutUnits();
	void setCombatUnits();

	void goScoutAlways();
	void goScoutIfNeeded();

	void drawDebugInterface();
    void drawGameInformation(int x, int y);
	void drawUnitOrders();

	BWAPI::Unit getFirstSupplyProvider();
	// BWAPI::Unit getClosestUnitToTarget(BWAPI::UnitType type, BWAPI::Position target);
	BWAPI::Unit getAnyFreeWorker();

	void onUnitShow(BWAPI::Unit unit);
	void onUnitHide(BWAPI::Unit unit);
	void onUnitCreate(BWAPI::Unit unit);
	void onUnitComplete(BWAPI::Unit unit);
	void onUnitRenegade(BWAPI::Unit unit);
	void onUnitDestroy(BWAPI::Unit unit);
	void onUnitMorph(BWAPI::Unit unit);

	static GameCommander & Instance();
};

}