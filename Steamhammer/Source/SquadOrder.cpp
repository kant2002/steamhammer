#include "SquadOrder.h"

#include "Base.h"
#include "GridDistances.h"

using namespace UAlbertaBot;

// -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- --

SquadOrder::SquadOrder() 
    : _type(SquadOrderTypes::None)
    , _position(BWAPI::Positions::None)
    , _base(nullptr)
    , _radius(0)
    , _status("Default")
    , _distances(nullptr)
{
}

// The copy constructor can be expensive if _distances is set. Try to minimize use.
SquadOrder::SquadOrder(const SquadOrder & source) 
    : _type(source._type)
    , _position(source._position)
    , _base(source._base)
    , _radius(source._radius)
    , _key(source._key)
    , _status(source._status)
    , _distances(source._distances == nullptr ? nullptr : new GridDistances(*_distances))
{
}

// Copy assignment operator.
SquadOrder & SquadOrder::operator=(const SquadOrder & source)
{
    // Self-assignment is a special case, since C++ is a low-level language.
    if (&source != this)
    {
        _type = source._type;
        _position = source._position;
        _base = source._base;
        _radius = source._radius;
        _key = source._key;
        _status = source._status;
        _distances = source._distances == nullptr ? nullptr : new GridDistances(*_distances);
    }
    return *this;
}

// The move constructor must leave the source in an "unspecified but valid" state.
SquadOrder::SquadOrder(SquadOrder && source) 
    : _type(source._type)
    , _position(source._position)
    , _base(source._base)
    , _radius(source._radius)
    , _key(source._key)
    , _status(source._status)
    , _distances(source._distances)
{
    // Make the source valid, don't double-release the distances.
    source._distances = nullptr;
}

// Move assignment operator.
SquadOrder & SquadOrder::operator=(SquadOrder && source)
{
    // Self-assignment is a special case, since C++ is a low-level language.
    if (&source != this)
    {
        delete _distances;

        _type = source._type;
        _position = source._position;
        _base = source._base;
        _radius = source._radius;
        _key = source._key;
        _status = source._status;
        _distances = source._distances;

        source._distances = nullptr;
    }
    return *this;
}

// For squads whose behavior is decided by code rather than by the squad order,
// or which have not yet been given their first real order.
SquadOrder::SquadOrder(const std::string & status) 
    : _type(SquadOrderTypes::Idle)
    , _position(BWAPI::Positions::Origin)
    , _base(nullptr)
    , _radius(0)
    , _status(status)
    , _distances(nullptr)
{
}

// Optionally use or ignore distances.
SquadOrder::SquadOrder(SquadOrderTypes type, BWAPI::Position position, int radius, bool useDistances, const std::string & status) 
    : _type(type)
    , _position(position)
    , _base(nullptr)
    , _radius(radius)
    , _status(status)
    , _distances(useDistances && position.isValid() ? new GridDistances(BWAPI::TilePosition(position)) : nullptr)
{
}

// Calculate distances on the spot.
SquadOrder::SquadOrder(SquadOrderTypes type, BWAPI::Position position, int radius, const std::string & status) 
    : _type(type)
    , _position(position)
    , _base(nullptr)
    , _radius(radius)
    , _status(status)
    , _distances(position.isValid() ? new GridDistances(BWAPI::TilePosition(position)) : nullptr)
{
}

// `base` must not be null.
// If useDistances, use the distances of _base. Don't fill in _distances.
// If !useDistances, use the position of the base but don't set _base. We don't want any distances.
SquadOrder::SquadOrder(SquadOrderTypes type, Base * base, int radius, bool useDistances, const std::string & status) 
    : _type(type)
    , _position(base->getCenter())
    , _base(useDistances ? base : nullptr)
    , _radius(radius)
    , _status(status)
    , _distances(nullptr)
{
    UAB_ASSERT(base, "baseless");
}

SquadOrder::~SquadOrder()
{
    delete _distances;      // not always set
}

// Most stuff is irrelevant in deciding whether two orders are equivalent.
bool SquadOrder::operator==(const SquadOrder& o)
{
    return
        _type == o._type &&
        _position == o._position;
}

bool SquadOrder::operator!=(const SquadOrder& o)
{
    return !(*this == o);
}

// The distances that we want to use. May be null to use none, otherwise must have been
// allocated with `new GridDistances`.
// Clear _base, since we don't want to use its distances.
void SquadOrder::setDistances(GridDistances * distances)
{
    _base = nullptr;
    delete _distances;
    _distances = distances;
}

SquadOrderTypes SquadOrder::getType() const
{
    return _type;
}

const BWAPI::Position & SquadOrder::getPosition() const
{
    return _position;
}

int SquadOrder::getRadius() const
{
    return _radius;
}

const std::string & SquadOrder::getStatus() const 
{
    return _status;
}

void SquadOrder::setKey(const std::string & key)
{
    _key = key;
}

const std::string & SquadOrder::getKey() const
{
    return _key;
}

void SquadOrder::setStatus(const std::string & status)
{
    _status = status;
}

const char SquadOrder::getCharCode() const
{
    switch (_type)
    {
    case SquadOrderTypes::None:				return '-';
    case SquadOrderTypes::Idle:				return 'I';
    case SquadOrderTypes::Watch:			return 'W';
    case SquadOrderTypes::Attack:			return 'a';
    case SquadOrderTypes::OmniAttack:		return 'A';
    case SquadOrderTypes::Defend:			return 'd';
    case SquadOrderTypes::Hold:				return 'H';
    case SquadOrderTypes::Load:				return 'L';
    case SquadOrderTypes::Drop:				return 'D';
    case SquadOrderTypes::DestroyNeutral:	return 'N';
    }
    return '?';
}

// These orders are considered combat orders and are linked to combat-related micro.
bool SquadOrder::isCombatOrder() const
{
    return
        _type == SquadOrderTypes::Watch ||
        _type == SquadOrderTypes::Attack ||
        _type == SquadOrderTypes::OmniAttack ||
        _type == SquadOrderTypes::Defend ||
        _type == SquadOrderTypes::Hold ||
        _type == SquadOrderTypes::Drop ||
        _type == SquadOrderTypes::DestroyNeutral;
}

// These orders use the regrouping mechanism to retreat when facing superior enemies.
// Combat orders not in this group fight on against any odds.
bool SquadOrder::isRegroupableOrder() const
{
    return
        _type == SquadOrderTypes::Watch ||
        _type == SquadOrderTypes::Attack ||
        _type == SquadOrderTypes::OmniAttack ||
        _type == SquadOrderTypes::Defend ||
        _type == SquadOrderTypes::DestroyNeutral;
}

const GridDistances * SquadOrder::distances() const
{
    if (_distances)
    {
        return _distances;
    }

    if (_base)
    {
        return &_base->getDistances();
    }

    return nullptr;
}
