#pragma once

#include "Common.h"

namespace UAlbertaBot
{
struct UnitInfo
{
    // we need to store all of this data because if the unit is not visible, we
    // can't reference it from the unit pointer

    int             unitID;
	int				updateFrame;
    int             lastHealth;
    int             lastShields;
    BWAPI::Player   player;
    BWAPI::Unit     unit;
    BWAPI::Position lastPosition;
	bool			goneFromLastPosition;    // last position was seen, and it wasn't there
    BWAPI::UnitType type;
    bool            completed;

    UnitInfo()
        : unitID(0)
		, updateFrame(0)
		, lastHealth(0)
		, lastShields(0)
		, player(nullptr)
        , unit(nullptr)
        , lastPosition(BWAPI::Positions::None)
		, goneFromLastPosition(false)
        , type(BWAPI::UnitTypes::None)
        , completed(false)
	{
    }

	UnitInfo(BWAPI::Unit unit)
		: unitID(unit->getID())
		, updateFrame(BWAPI::Broodwar->getFrameCount())
		, lastHealth(unit->getHitPoints())
		, lastShields(unit->getShields())
		, player(unit->getPlayer())
		, unit(unit)
		, lastPosition(unit->getPosition())
		, goneFromLastPosition(false)
		, type(unit->getType())
		, completed(unit->isCompleted())
	{
	}

    const bool operator == (BWAPI::Unit unit) const
    {
        return unitID == unit->getID();
    }

    const bool operator == (const UnitInfo & rhs) const
    {
        return unitID == rhs.unitID;
    }

    const bool operator < (const UnitInfo & rhs) const
    {
        return unitID < rhs.unitID;
    }

	int estimateHealth() const;
};

typedef std::vector<UnitInfo> UnitInfoVector;
typedef std::map<BWAPI::Unit,UnitInfo> UIMap;

class UnitData
{
    UIMap unitMap;

    const bool			badUnitInfo(const UnitInfo & ui) const;

    std::vector<int>	numUnits;       // how many now
	std::vector<int>	numDeadUnits;   // how many lost

    int					mineralsLost;
    int					gasLost;

public:

    UnitData();

	void	updateGoneFromLastPosition();

    void	updateUnit(BWAPI::Unit unit);
    void	removeUnit(BWAPI::Unit unit);
    void	removeBadUnits();

    int		getGasLost()                                const;
    int		getMineralsLost()                           const;
    int		getNumUnits(BWAPI::UnitType t)              const;
    int		getNumDeadUnits(BWAPI::UnitType t)          const;
    const	std::map<BWAPI::Unit,UnitInfo> & getUnits() const;
};
}