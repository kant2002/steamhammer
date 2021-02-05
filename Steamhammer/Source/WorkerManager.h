#pragma once

#include "WorkerData.h"

namespace UAlbertaBot
{
class Building;

class WorkerManager
{
    WorkerData  workerData;
    BWAPI::Unit previousClosestWorker;
	bool		_collectGas;
    BWAPI::Unitset busy;    // units with special orders this frame
    std::map<BWAPI::Unit, int> burrowedForSafety;

    // Used in calls to maybeFleeDanger() and inWeaponsDanger().
    static const int weaponsMargin = 2 * 32;
    static const int weaponsMarginPlus = weaponsMargin + 2 * 32;    // for hysteresis

    bool        isBusy(BWAPI::Unit worker) const;
    void        makeBusy(BWAPI::Unit worker);
    void        burrowForSafety(BWAPI::Unit worker);

	void        setMineralWorker(BWAPI::Unit unit);
	void        setReturnCargoWorker(BWAPI::Unit unit);
	bool		refineryHasDepot(BWAPI::Unit refinery);
	bool        isGasStealRefinery(BWAPI::Unit unit);

    void        clearBlockingMinerals();

	void        handleGasWorkers();
	void        handleIdleWorkers();
	void		handleReturnCargoWorkers();
	void        handleRepairWorkers();
	void		handleMineralWorkers();
    void        handleUnblockWorkers();
    void		handlePostedWorkers();

    bool        maybeFleeDanger(BWAPI::Unit worker, int margin = weaponsMargin);
    void        maybeUnburrow();

    // A worker within this distance of a target location is considered to be
    // "in the same base" as the target. Used by worker selection routines to avoid
    // transferring workers between bases.
    static const int thisBaseRange = 10 * 32;

    bool        inIrradiateDanger(BWAPI::Unit worker) const;

	BWAPI::Unit findEnemyTargetForWorker(BWAPI::Unit worker) const;
	BWAPI::Unit findEscapeMinerals(BWAPI::Unit worker) const;
	bool		defendSelf(BWAPI::Unit worker, BWAPI::Unit resource);

	BWAPI::Unit getAnyClosestDepot(BWAPI::Unit worker);      // don't care whether it's full
	BWAPI::Unit getClosestNonFullDepot(BWAPI::Unit worker);  // only if it can accept more mineral workers

    BWAPI::Unit	getAnyWorker(const BWAPI::Position & pos, int range);
    BWAPI::Unit	getUnencumberedWorker(const BWAPI::Position & pos, int range);
    BWAPI::Unit getPostedWorker(const BWAPI::Position & pos);
    bool        postedWorkerBusy(const BWAPI::Position & pos);

    void        drawWorkerInformation();
    
    WorkerManager();

public:

    void        update();
    void        onUnitDestroy(BWAPI::Unit unit);
    void        onUnitMorph(BWAPI::Unit unit);
    void        onUnitShow(BWAPI::Unit unit);
    void        onUnitRenegade(BWAPI::Unit unit);

    void        finishedWithWorker(BWAPI::Unit unit);

    void        updateWorkerStatus();

    int         getNumMineralWorkers() const;
    int         getNumGasWorkers() const;
	int         getNumReturnCargoWorkers() const;
	int			getNumCombatWorkers() const;
	int         getNumIdleWorkers() const;
    int         getNumPostedWorkers() const;
	int			getMaxWorkers() const;

	int			getNumWorkers(BWAPI::Unit jobUnit) const;

    void        setScoutWorker(BWAPI::Unit worker);

	// NOTE _collectGas == false allows that a little more gas may still be collected.
	bool		isCollectingGas()              { return _collectGas; };
	void		setCollectGas(bool collectGas) { _collectGas = collectGas; };

    bool        isWorkerScout(BWAPI::Unit worker);
	bool		isCombatWorker(BWAPI::Unit worker);
    bool        isFree(BWAPI::Unit worker);
    bool        isBuilder(BWAPI::Unit worker);

    BWAPI::Unit getBuilder(const Building & b);
	void        setBuildWorker(BWAPI::Unit worker);
    BWAPI::Unit getGasWorker(BWAPI::Unit refinery);
    BWAPI::Unit getClosestMineralWorkerTo(BWAPI::Unit enemyUnit);
    BWAPI::Unit getWorkerScout();

    void        setRepairWorker(BWAPI::Unit worker,BWAPI::Unit unitToRepair);
    void        stopRepairing(BWAPI::Unit worker);
    void        setCombatWorker(BWAPI::Unit worker);
    void        postGivenWorker(BWAPI::Unit worker, MacroLocation loc);
    BWAPI::Unit postWorker(MacroLocation loc);
    void        unpostWorkers(MacroLocation loc);

    bool        willHaveResources(int mineralsRequired,int gasRequired,double distance);
    void        rebalanceWorkers();

    static WorkerManager &  Instance();
};
}