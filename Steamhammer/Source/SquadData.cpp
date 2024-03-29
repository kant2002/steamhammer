#include "SquadData.h"

#include "WorkerManager.h"

using namespace UAlbertaBot;

SquadData::SquadData() 
{
}

void SquadData::update()
{
    updateAllSquads();
    verifySquadUniqueMembership();
}

void SquadData::clearSquadData()
{
    // give back workers who were in squads
    for (auto & kv : _squads)
    {
        Squad & squad = kv.second;

        const BWAPI::Unitset & units = squad.getUnits();

        for (auto unit : units)
        {
            if (unit->getType().isWorker())
            {
                WorkerManager::Instance().finishedWithWorker(unit);
            }
        }
    }

    _squads.clear();
}

void SquadData::removeSquad(const std::string & squadName)
{
    auto & squadPtr = _squads.find(squadName);

    UAB_ASSERT_WARNING(squadPtr != _squads.end(), "Trying to clear a squad that didn't exist: %s", squadName.c_str());
    if (squadPtr == _squads.end())
    {
        return;
    }

    for (BWAPI::Unit unit : squadPtr->second.getUnits())
    {
        if (unit->getType().isWorker())
        {
            WorkerManager::Instance().finishedWithWorker(unit);
        }
    }

    _squads.erase(squadName);
}

const std::map<std::string, Squad> & SquadData::getSquads() const
{
    return _squads;
}

bool SquadData::squadExists(const std::string & squadName)
{
    return _squads.find(squadName) != _squads.end();
}

// Create a new squad. The squad must not already exist.
// The squad is created with a "do nothing" order. Usually you want to provide another order.
Squad & SquadData::createSquad(const std::string & name, size_t priority)
{
    _squads.emplace(name, Squad(name, priority));
    return getSquad(name);
}

void SquadData::updateAllSquads()
{
    for (auto & kv : _squads)
    {
        kv.second.update();
    }
}

void SquadData::drawSquadInformation(int x, int y) 
{
    if (!Config::Debug::DrawSquadInfo)
    {
        return;
    }

    int yspace = 0;

    for (auto & kv : _squads) 
    {
        Squad & squad = kv.second;

        const BWAPI::Unitset & units = squad.getUnits();
        const SquadOrder & order = squad.getOrder();
        const char code = order.getCharCode();                      // a == attack, etc.
        const char useDistances = order.distances() != nullptr ? '#' : ' ';
        const char visibleOnly = squad.getFightVisible() ? 'V' : ' ';
        const char meatgrinder = squad.getMeatgrinder() ? 'M' : ' ';
        const BWAPI::TilePosition orderTile(order.getPosition());   // shorter and easier to read than the pixel position

        BWAPI::Broodwar->drawTextScreen(x      , y + (yspace * 10), "%c%c%c%c", cyan, useDistances, visibleOnly, meatgrinder);
        BWAPI::Broodwar->drawTextScreen(x +  18, y + (yspace * 10), "%c%c", yellow, code);
        BWAPI::Broodwar->drawTextScreen(x +  30, y + (yspace * 10), "%c%s", white, squad.getName().c_str());
        BWAPI::Broodwar->drawTextScreen(x + 100, y + (yspace * 10), "%c%d", yellow, units.size());
        BWAPI::Broodwar->drawTextScreen(x + 125, y + (yspace * 10), "%c%d,%d", yellow, orderTile.x, orderTile.y);
        BWAPI::Broodwar->drawTextScreen(x + 168, y + (yspace * 10), "%c%s", orange, units.size() == 0 ? "" : order.getStatus().c_str());
        ++yspace;

        BWAPI::Broodwar->drawCircleMap(order.getPosition(), 10, BWAPI::Colors::Green, true);
        BWAPI::Broodwar->drawCircleMap(order.getPosition(), order.getRadius(), BWAPI::Colors::Red, false);
        BWAPI::Broodwar->drawTextMap(order.getPosition() + BWAPI::Position(0, 10), "%s", squad.getName().c_str());
    }
}

void SquadData::verifySquadUniqueMembership()
{
    BWAPI::Unitset assigned;

    for (const auto & kv : _squads)
    {
        for (BWAPI::Unit unit : kv.second.getUnits())
        {
            if (assigned.contains(unit))
            {
                BWAPI::Broodwar->printf("Unit is in at least two squads: %s", unit->getType().getName().c_str());
            }

            assigned.insert(unit);
        }
    }
}

bool SquadData::unitIsInSquad(BWAPI::Unit unit) const
{
    return getUnitSquad(unit) != nullptr;
}

const Squad * SquadData::getUnitSquad(BWAPI::Unit unit) const
{
    for (const auto & kv : _squads)
    {
        if (kv.second.getUnits().contains(unit))
        {
            return &kv.second;
        }
    }

    return nullptr;
}

Squad * SquadData::getUnitSquad(BWAPI::Unit unit)
{
    for (auto & kv : _squads)
    {
        if (kv.second.getUnits().contains(unit))
        {
            return &kv.second;
        }
    }

    return nullptr;
}

void SquadData::assignUnitToSquad(BWAPI::Unit unit, Squad & squad)
{
    Squad * previousSquad = getUnitSquad(unit);

    if (previousSquad)
    {
        UAB_ASSERT_WARNING(canAssignUnitToSquad(unit, squad), "re-assigning %s -> %s", previousSquad->getName().c_str(), squad.getName().c_str());
        previousSquad->removeUnit(unit);
    }

    squad.addUnit(unit);
}

bool SquadData::canAssignUnitToSquad(BWAPI::Unit unit, const Squad & squad) const
{
    const Squad * unitSquad = getUnitSquad(unit);

    // make sure strictly less than so we don't reassign to the same squad etc
    return !unitSquad || (unitSquad->getPriority() < squad.getPriority());
}

Squad & SquadData::getSquad(const std::string & squadName)
{
    UAB_ASSERT_WARNING(squadExists(squadName), "Trying to access squad that doesn't exist: %s", squadName);
    if (!squadExists(squadName))
    {
        int a = 10;
    }

    return _squads[squadName];
}