#pragma once

#include "Common.h"
#include "BWTA.h"

#include "UnitData.h"

#include "..\..\SparCraft\source\SparCraft.h"

namespace UAlbertaBot
{

struct BaseStatus
{
public:
	BWAPI::Unit  	resourceDepot;		// hatchery, etc.; valid iff depotSeen
	BWAPI::Player	owner;              // self, enemy, neutral
	bool			reserved;			// if this is our planned expansion

	// The resourceDepot pointer is set for a base if the depot has been seen.
	// It is possible to infer a base location without seeing the depot.

	BaseStatus()
		: resourceDepot(nullptr)
		, owner(BWAPI::Broodwar->neutral())
		, reserved(false)
	{
	}

	BaseStatus(BWAPI::Unit depot, BWAPI::Player player)
		: resourceDepot(depot)
		, owner(player)
		, reserved(false)
	{
	}

	BaseStatus(BWAPI::Unit depot, BWAPI::Player player, bool res)
		: resourceDepot(depot)
		, owner(player)
		, reserved(res)
	{
	}
};

class InformationManager 
{
	BWAPI::Player	_self;
    BWAPI::Player	_enemy;

	bool			_enemyProxy;

	bool			_weHaveCombatUnits;
	bool			_enemyHasAntiAir;
	bool			_enemyHasAirTech;
	bool			_enemyHasCloakTech;
	bool			_enemyHasMobileCloakTech;
	bool			_enemyHasOverlordHunters;
	bool			_enemyHasMobileDetection;

	std::map<BWAPI::Player, UnitData>                   _unitData;
	std::map<BWAPI::Player, BWTA::BaseLocation *>       _mainBaseLocations;
	BWTA::BaseLocation *								_myNaturalBaseLocation;  // whether taken yet or not
	std::map<BWAPI::Player, std::set<BWTA::Region *> >  _occupiedRegions;        // contains any building
	std::map<BWTA::BaseLocation *, BaseStatus>			_theBases;

	InformationManager();

	void                    initializeRegionInformation();
	void					initializeNaturalBase();

	int                     getIndex(BWAPI::Player player) const;

	void					baseInferred(BWTA::BaseLocation * base);
	void					baseFound(BWAPI::Unit depot);
	void					baseLost(BWAPI::TilePosition basePosition);
	void					maybeAddBase(BWAPI::Unit unit);
	bool					closeEnough(BWAPI::TilePosition a, BWAPI::TilePosition b);
	void					chooseNewMainBase();

	void                    updateUnit(BWAPI::Unit unit);
    void                    updateUnitInfo();
    void                    updateBaseLocationInfo();
    void                    updateOccupiedRegions(BWTA::Region * region,BWAPI::Player player);
    bool                    isValidUnit(BWAPI::Unit unit);

public:

    void                    update();

    // event driven stuff
	void					onUnitShow(BWAPI::Unit unit)        { updateUnit(unit); maybeAddBase(unit); }
    void					onUnitHide(BWAPI::Unit unit)        { updateUnit(unit); }
	void					onUnitCreate(BWAPI::Unit unit)		{ updateUnit(unit); maybeAddBase(unit); }
    void					onUnitComplete(BWAPI::Unit unit)    { updateUnit(unit); }
	void					onUnitMorph(BWAPI::Unit unit)       { updateUnit(unit); maybeAddBase(unit); }
    void					onUnitRenegade(BWAPI::Unit unit)    { updateUnit(unit); }
    void					onUnitDestroy(BWAPI::Unit unit);

	bool					isEnemyBuildingInRegion(BWTA::Region * region);
    int						getNumUnits(BWAPI::UnitType type,BWAPI::Player player);
    bool					nearbyForceHasCloaked(BWAPI::Position p,BWAPI::Player player,int radius);
    bool					isCombatUnit(BWAPI::UnitType type) const;

    void                    getNearbyForce(std::vector<UnitInfo> & unitInfo,BWAPI::Position p,BWAPI::Player player,int radius);

    const UIMap &           getUnitInfo(BWAPI::Player player) const;

    std::set<BWTA::Region *> &  getOccupiedRegions(BWAPI::Player player);
    BWTA::BaseLocation *    getMainBaseLocation(BWAPI::Player player);
	BWTA::BaseLocation *	getMyMainBaseLocation();
	BWTA::BaseLocation *	getEnemyMainBaseLocation();
	BWAPI::Player			getBaseOwner(BWTA::BaseLocation * base);
	BWAPI::Unit 			getBaseDepot(BWTA::BaseLocation * base);
	BWTA::BaseLocation *	getMyNaturalLocation();
	int						getTotalNumBases() const;
	int						getNumBases(BWAPI::Player player);
	int						getNumFreeLandBases();
	int						getMyNumMineralPatches();
	int						getMyNumGeysers();
	int						getMyNumRefineries();
	int						getAir2GroundSupply(BWAPI::Player player) const;

	bool					isBaseReserved(BWTA::BaseLocation * base);
	void					reserveBase(BWTA::BaseLocation * base);
	void					unreserveBase(BWTA::BaseLocation * base);
	void					unreserveBase(BWAPI::TilePosition baseTilePosition);

	bool					weHaveCombatUnits();

	bool					enemyHasAntiAir();
	bool					enemyHasAirTech();
	bool                    enemyHasCloakTech();
	bool                    enemyHasMobileCloakTech();
	bool					enemyHasOverlordHunters();
	bool					enemyHasMobileDetection();

	int						nScourgeNeeded();           // zerg specific

    void                    drawExtendedInterface();
    void                    drawUnitInformation(int x,int y);
    void                    drawMapInformation();
	void					drawBaseInformation(int x, int y);

    const UnitData &        getUnitData(BWAPI::Player player) const;

	// yay for singletons!
	static InformationManager & Instance();
};
}
