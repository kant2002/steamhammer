
#pragma once

#include <BWAPI.h>

namespace UAlbertaBot
{

enum class SquadOrderTypes
{
    None,
    Idle,			// workers, overlords with no other job
    Watch,			// stand watch over a location
    Attack,			// go attack
    OmniAttack,		// attack any visible enemy, not only a nearby one
    Defend,			// defend a base (automatically disbanded when enemy is gone)
    Hold,			// hold ground, stand ready to defend until needed
    Load,			// load into a transport (Drop squad)
    Drop,			// go drop on the enemy (Drop squad)
    DestroyNeutral,	// destroy neutral units by attack (e.g. destroy blocking buildings)
};

// Forward declarations.
class Base;
class GridDistances;

class SquadOrder
{
    SquadOrderTypes     _type;
    BWAPI::Position     _position;      // always set, not always valid
    Base *              _base;          // not always set
    int                 _radius;
    std::string         _key;           // semantically related orders get matching keys
    std::string         _status;

    // Ground distances, set for ground squads when _base is not set.
    // distance() uses _base if set, else _distances if it is set.
    GridDistances *     _distances;

public:

    SquadOrder();
    SquadOrder(const SquadOrder & source);              // no copy constructor, expensive if _distances is set
    SquadOrder & operator=(const SquadOrder & source);  // no copy assignment
    SquadOrder(SquadOrder && source);                   // move constructor
    SquadOrder & operator=(SquadOrder && source);       // move assignment
    SquadOrder(const std::string & status);
    SquadOrder(SquadOrderTypes type, BWAPI::Position position, int radius, const std::string & status = "Default");
    SquadOrder(SquadOrderTypes type, BWAPI::Position position, int radius, bool useDistances, const std::string & status = "Default");
    SquadOrder(SquadOrderTypes type, Base * base, int radius, bool useDistances, const std::string & status = "Default");
    ~SquadOrder();

    bool operator==(const SquadOrder & o);
    bool operator!=(const SquadOrder & o);

    // And clear _base, since we don't want to use its distances.
    void setDistances(GridDistances * distances);

    SquadOrderTypes getType() const;
    const BWAPI::Position & getPosition() const;
    Base * getBase() const { return _base; };
    int getRadius() const;
    const std::string & getStatus() const;
    void setKey(const std::string & key);
    const std::string & getKey() const;
    void setStatus(const std::string & status);

    const char getCharCode() const;

    // These orders are considered combat orders and are linked to combat-related micro.
    bool isCombatOrder() const;

    // These orders use the regrouping mechanism to retreat when facing superior enemies.
    // Combat orders not in this group fight on against any odds.
    bool isRegroupableOrder() const;

    // Map of ground distance in tiles from the order position, when available.
    const GridDistances * distances() const;

};
}