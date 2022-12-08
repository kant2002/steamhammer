#pragma once

// Plan and construct static defense buildings for any race.

#include <BWAPI.h>
#include <vector>

namespace UAlbertaBot
{

// Forward declaration.
class Base;

// NOTE The plan doesn't include shield batteries. Not supported yet.
struct StaticDefensePlan
{
    // Count bunkers, cannons, or sunken colonies at different sets of places.
    // An "inner base" is one the enemy cannot reach by ground without passing an outer base.
    int atInnerBases;
    int atOuterBases;
    int atFront;           // front line only (a specific outer base)
    //int atProxy;           // proxy location NOT IMPLEMENTED

    // Count turrets or spore colonies. Not used for protoss.
    // If airPerBase is true, then antiAir gives the count wanted for each base.
    // If false, the total count, one at each of this many bases. It can be only 0 to 3.
    bool airIsPerBase;
    int antiAir;

    StaticDefensePlan();
};

class StaticDefense
{
    StaticDefensePlan _plan;
    BWAPI::UnitType _ground;
    BWAPI::UnitType _groundPrereq;
    BWAPI::UnitType _air;
    BWAPI::UnitType _airPrereq;
    std::vector<Base *> _airBases;
    const int _MinDroneLimit;       // replace zerg drones below this limit

    void limitZergDefenses();

    void planTP();

    int analyzeGroundDefensesZvT() const;
    int analyzeGroundDefensesZvP() const;
    int analyzeGroundDefensesZvZ() const;
    void planZ();

    void plan();

    void startBuilding(BWAPI::UnitType building, const BWAPI::TilePosition & tile);
    bool morphStrandedCreeps(BWAPI::UnitType type);
    bool startFrontGroundBuildings();
    bool startOtherGroundBuildings();
    bool buildGround();

    void chooseAirBases();
    void startAirBuilding();
    void buildAir();

    bool alreadyBuilding(BWAPI::UnitType type);
    bool needPrerequisite(BWAPI::UnitType prereq);
    void build();

    void drawGroundCount(Base * base, int count) const;
    void drawAirCount(Base * base, int count) const;
    void draw() const;

public:

    StaticDefense();

    void update();
};

};
