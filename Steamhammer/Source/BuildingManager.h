#pragma once

#include "WorkerManager.h"
#include "BuildingPlacer.h"
#include "InformationManager.h"
#include "MacroAct.h"
#include "MapTools.h"

namespace UAlbertaBot
{
class BuildingManager
{
    BuildingManager();

    std::vector<Building> _buildings;

    bool            _debugMode;
    int             _reservedMinerals;				// minerals reserved for planned buildings
    int             _reservedGas;					// gas reserved for planned buildings

    bool            isBuildingPositionExplored(const Building & b) const;
	void			undoBuildings(const std::vector<Building> & toRemove);
    void            removeBuildings(const std::vector<Building> & toRemove);

    void            validateWorkersAndBuildings();		    // STEP 1
    void            assignWorkersToUnassignedBuildings();	// STEP 2
    void            constructAssignedBuildings();			// STEP 3
    void            checkForStartedConstruction();			// STEP 4
    void            checkForDeadTerranBuilders();			// STEP 5
    void            checkForCompletedBuildings();			// STEP 6

    char            getBuildingWorkerCode(const Building & b) const;
    
public:
    
    static BuildingManager &	Instance();

    void                update();
    void                onUnitMorph(BWAPI::Unit unit);
    void                onUnitDestroy(BWAPI::Unit unit);
	Building &		    addTrackedBuildingTask(const MacroAct & act, BWAPI::TilePosition desiredLocation, bool isGasSteal);
	void                addBuildingTask(const MacroAct & act, BWAPI::TilePosition desiredLocation, bool isGasSteal);
    void                drawBuildingInformation(int x,int y);
    BWAPI::TilePosition getBuildingLocation(const Building & b);

    int                 getReservedMinerals() const;
    int                 getReservedGas() const;

    bool                isBeingBuilt(BWAPI::UnitType type);

    std::vector<BWAPI::UnitType> buildingsQueued();

	void                cancelBuilding(Building & b);
	void				cancelQueuedBuildings();
	void				cancelBuildingType(BWAPI::UnitType t);
};

}