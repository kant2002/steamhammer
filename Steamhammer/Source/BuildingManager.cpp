#include "Common.h"
#include "BuildingManager.h"
#include "Micro.h"
#include "ScoutManager.h"
#include "UnitUtil.h"

using namespace UAlbertaBot;

BuildingManager::BuildingManager()
    : _debugMode(false)
    , _reservedMinerals(0)
    , _reservedGas(0)
{
}

// gets called every frame from GameCommander
void BuildingManager::update()
{
    validateWorkersAndBuildings();          // check to see if assigned workers have died en route or while constructing
    assignWorkersToUnassignedBuildings();   // assign workers to the unassigned buildings and label them 'planned'    
    constructAssignedBuildings();           // for each planned building, if the worker isn't constructing, send the command    
    checkForStartedConstruction();          // check to see if any buildings have started construction and update data structures    
    checkForDeadTerranBuilders();           // if we are terran and a building is under construction without a worker, assign a new one    
    checkForCompletedBuildings();           // check to see if any buildings have completed and update data structures
}

// STEP 1: DO BOOK KEEPING ON BUILDINGS WHICH MAY HAVE DIED
void BuildingManager::validateWorkersAndBuildings()
{
    std::vector<Building> toRemove;
    
    // find any buildings which have become obsolete
    for (auto & b : _buildings)
    {
        if (b.status != BuildingStatus::UnderConstruction)
        {
            continue;
        }

		if (!b.buildingUnit ||
			!b.buildingUnit->exists() ||
			b.buildingUnit->getHitPoints() <= 0 ||
			!b.buildingUnit->getType().isBuilding())
        {
            toRemove.push_back(b);
        }
    }

    undoBuildings(toRemove);
}

// STEP 2: ASSIGN WORKERS TO BUILDINGS WITHOUT THEM
// Also places the building.
void BuildingManager::assignWorkersToUnassignedBuildings()
{
    // for each building that doesn't have a builder, assign one
    for (Building & b : _buildings)
    {
        if (b.status != BuildingStatus::Unassigned)
        {
            continue;
        }

		// BWAPI::Broodwar->printf("Assigning Worker To: %s", b.type.getName().c_str());

        // TODO: special case of terran building whose worker died mid construction
        //       send the right click command to the buildingUnit to resume construction
        //		 skip the buildingsAssigned step and push it back into buildingsUnderConstruction

        BWAPI::TilePosition testLocation = getBuildingLocation(b);
        if (!testLocation.isValid())
        {
			continue;
        }

		b.finalPosition = testLocation;

		// grab the worker unit from WorkerManager which is closest to this final position
		b.builderUnit = WorkerManager::Instance().getBuilder(b);
		if (!b.builderUnit || !b.builderUnit->exists())
		{
			continue;
		}

        // reserve this building's space
        BuildingPlacer::Instance().reserveTiles(b.finalPosition,b.type.tileWidth(),b.type.tileHeight());

        b.status = BuildingStatus::Assigned;
		// BWAPI::Broodwar->printf("assigned and placed building %s", b.type.getName().c_str());
	}
}

// STEP 3: ISSUE CONSTRUCTION ORDERS TO ASSIGNED BUILDINGS AS NEEDED
void BuildingManager::constructAssignedBuildings()
{
    for (auto & b : _buildings)
    {
        if (b.status != BuildingStatus::Assigned)
        {
            continue;
        }

		UAB_ASSERT(b.builderUnit, "bad builder unit");

		/* this seems to cause errors?
		if (!b.builderUnit->exists())
		{
			// The builder unit no longer exists() and must be replaced.

			BWAPI::Broodwar->printf("b.builderUnit gone, b.type = %s", b.type.getName().c_str());

			WorkerManager::Instance().finishedWithWorker(b.builderUnit);    // better still do this
			b.builderUnit = nullptr;
			b.buildCommandGiven = false;
			b.status = BuildingStatus::Unassigned;
			BuildingPlacer::Instance().freeTiles(b.finalPosition, b.type.tileWidth(), b.type.tileHeight());
		}
        else */
		if (!b.builderUnit->isConstructing())
        {
            // if we haven't explored the build position, go there
            if (!isBuildingPositionExplored(b))
            {
				Micro::SmartMove(b.builderUnit, BWAPI::Position(b.finalPosition));
            }
            // if this is not the first time we've sent this guy to build this
            // it must be the case that something was in the way
			else if (b.buildCommandGiven)
            {
                // tell worker manager the unit we had is not needed now, since we might not be able
                // to get a valid location soon enough
                WorkerManager::Instance().finishedWithWorker(b.builderUnit);

                b.builderUnit = nullptr;
                b.buildCommandGiven = false;
                b.status = BuildingStatus::Unassigned;

				// Unreserve the building location. The building will mark its own location.
				BuildingPlacer::Instance().freeTiles(b.finalPosition, b.type.tileWidth(), b.type.tileHeight());
			}
            else
            {
				// Issue the build order!
				// If the builderUnit is zerg, it changes to !exists() when it builds.
				bool success = b.builderUnit->build(b.type, b.finalPosition);

				if (success)
				{
					b.buildCommandGiven = true;
				}
           }
        }
    }
}

// STEP 4: UPDATE DATA STRUCTURES FOR BUILDINGS STARTING CONSTRUCTION
void BuildingManager::checkForStartedConstruction()
{
    // for each building unit which is being constructed
    for (auto & buildingStarted : BWAPI::Broodwar->self()->getUnits())
    {
        // filter out units which aren't buildings under construction
        if (!buildingStarted->getType().isBuilding() || !buildingStarted->isBeingConstructed())
        {
            continue;
        }

        // check all our building status objects to see if we have a match and if we do, update it
        for (auto & b : _buildings)
        {
            if (b.status != BuildingStatus::Assigned)
            {
                continue;
            }
        
            // check if the positions match
            if (b.finalPosition == buildingStarted->getTilePosition())
            {
                // the resources should now be spent, so unreserve them
                _reservedMinerals -= buildingStarted->getType().mineralPrice();
                _reservedGas      -= buildingStarted->getType().gasPrice();

                // flag it as started and set the buildingUnit
                b.underConstruction = true;
                b.buildingUnit = buildingStarted;

                if (BWAPI::Broodwar->self()->getRace() == BWAPI::Races::Zerg)
                {
					// if we are zerg, the builderUnit now becomes nullptr since it's destroyed
					b.builderUnit = nullptr;
                }
                else if (BWAPI::Broodwar->self()->getRace() == BWAPI::Races::Protoss)
                {
					// if we are protoss, give the worker back to worker manager
					// if this was the gas steal unit then it's the scout worker so give it back to the scout manager
                    if (b.isGasSteal)
                    {
                        ScoutManager::Instance().setWorkerScout(b.builderUnit);
                    }
                    // otherwise tell the worker manager we're finished with this unit
                    else
                    {
                        WorkerManager::Instance().finishedWithWorker(b.builderUnit);
                    }

                    b.builderUnit = nullptr;
                }

                b.status = BuildingStatus::UnderConstruction;

                BuildingPlacer::Instance().freeTiles(b.finalPosition,b.type.tileWidth(),b.type.tileHeight());

                // only one building will match
                break;
            }
        }
    }
}

// STEP 5: IF THE SCV DIED DURING CONSTRUCTION, ASSIGN A NEW ONE
void BuildingManager::checkForDeadTerranBuilders()
{
	if (BWAPI::Broodwar->self()->getRace() != BWAPI::Races::Terran)
	{
		return;
	}

	for (auto & b : _buildings)
	{
		if (b.status != BuildingStatus::UnderConstruction)
		{
			continue;
		}

		UAB_ASSERT(b.buildingUnit, "null buildingUnit");

		if (!UnitUtil::IsValidUnit(b.builderUnit))
		{
			b.builderUnit = WorkerManager::Instance().getBuilder(b);
			if (b.builderUnit && b.builderUnit->exists())
			{
				b.builderUnit->rightClick(b.buildingUnit);
			}
		}
	}
}

// STEP 6: CHECK FOR COMPLETED BUILDINGS
void BuildingManager::checkForCompletedBuildings()
{
    std::vector<Building> toRemove;

    // for each of our buildings under construction
    for (auto & b : _buildings)
    {
        if (b.status != BuildingStatus::UnderConstruction)
        {
            continue;       
        }

		UAB_ASSERT(b.buildingUnit, "null buildingUnit");

        // if the unit has completed
        if (b.buildingUnit->isCompleted())
        {
            // if we are terran, give the worker back to worker manager
            if (BWAPI::Broodwar->self()->getRace() == BWAPI::Races::Terran)
            {
                if (b.isGasSteal)
                {
                    ScoutManager::Instance().setWorkerScout(b.builderUnit);
                }
                // otherwise tell the worker manager we're finished with this unit
                else
                {
                    WorkerManager::Instance().finishedWithWorker(b.builderUnit);
                }
            }

            // remove this unit from the under construction vector
            toRemove.push_back(b);
        }
    }

    removeBuildings(toRemove);
}

// Add a new building to be constructed and return it.
Building & BuildingManager::addTrackedBuildingTask(const MacroAct & act, BWAPI::TilePosition desiredLocation, bool isGasSteal)
{
	UAB_ASSERT(act.isBuilding(), "trying to build a non-building");

	BWAPI::UnitType type = act.getUnitType();

	_reservedMinerals += type.mineralPrice();
	_reservedGas += type.gasPrice();

	Building b(type, desiredLocation);
	b.macroLocation = act.getMacroLocation();
	b.isGasSteal = isGasSteal;
	b.status = BuildingStatus::Unassigned;

	_buildings.push_back(b);      // makes a "permanent" copy of the Building object
	return _buildings.back();     // return a reference to the permanent copy
}

// Add a new building to be constructed.
void BuildingManager::addBuildingTask(const MacroAct & act, BWAPI::TilePosition desiredLocation, bool isGasSteal)
{
	(void) addTrackedBuildingTask(act, desiredLocation, isGasSteal);
}

bool BuildingManager::isBuildingPositionExplored(const Building & b) const
{
    BWAPI::TilePosition tile = b.finalPosition;

    // for each tile where the building will be built
    for (int x=0; x<b.type.tileWidth(); ++x)
    {
        for (int y=0; y<b.type.tileHeight(); ++y)
        {
            if (!BWAPI::Broodwar->isExplored(tile.x + x,tile.y + y))
            {
                return false;
            }
        }
    }

    return true;
}

char BuildingManager::getBuildingWorkerCode(const Building & b) const
{
    return b.builderUnit == nullptr ? 'X' : 'W';
}

int BuildingManager::getReservedMinerals() const
{
    return _reservedMinerals;
}

int BuildingManager::getReservedGas() const
{
    return _reservedGas;
}

// In the building queue with any status.
bool BuildingManager::isBeingBuilt(BWAPI::UnitType type)
{
	for (auto & b : _buildings)
	{
		if (b.type == type)
		{
			return true;
		}
	}

	return false;
}

void BuildingManager::drawBuildingInformation(int x, int y)
{
    if (!Config::Debug::DrawBuildingInfo)
    {
        return;
    }

    for (auto & unit : BWAPI::Broodwar->self()->getUnits())
    {
        BWAPI::Broodwar->drawTextMap(unit->getPosition().x,unit->getPosition().y+5,"\x07%d",unit->getID());
    }

	//for (auto geyser : BWAPI::Broodwar->getStaticGeysers())
	for (auto geyser : BWAPI::Broodwar->getAllUnits())
	{
		if (geyser->getType() == BWAPI::UnitTypes::Resource_Vespene_Geyser)
		{
			BWAPI::Broodwar->drawTextMap(geyser->getPosition().x, geyser->getPosition().y + 5, "\x07%d", geyser->getType());
		}
	}
	
	BWAPI::Broodwar->drawTextScreen(x, y, "\x04 Building Information:");
    BWAPI::Broodwar->drawTextScreen(x,y+20,"\x04 Name");
    BWAPI::Broodwar->drawTextScreen(x+150,y+20,"\x04 State");

    int yspace = 0;

	//for (auto geyser : BWAPI::Broodwar->getStaticGeysers())
	//{
	//	char exists = geyser->exists() ? 'e' : '-';
	//	char visible = geyser->isVisible() ? 'v' : '-';
		// BWAPI::Broodwar->drawTextScreen(x, y, "\x07%d @ %d, %d %c%c", geyser->getType(), geyser->getInitialTilePosition().x, geyser->getInitialTilePosition().y, exists, visible);
	//	y += 10;
	//}

	for (const auto & b : _buildings)
    {
        if (b.status == BuildingStatus::Unassigned)
        {
			int x1 = b.desiredPosition.x * 32;
			int y1 = b.desiredPosition.y * 32;
			int x2 = (b.desiredPosition.x + b.type.tileWidth()) * 32;
			int y2 = (b.desiredPosition.y + b.type.tileHeight()) * 32;

			BWAPI::Broodwar->drawBoxMap(x1, y1, x2, y2, BWAPI::Colors::Green, false);
			BWAPI::Broodwar->drawTextScreen(x, y + 40 + ((yspace)* 10), "\x03 %s", b.type.getName().c_str());
			BWAPI::Broodwar->drawTextScreen(x + 150, y + 40 + ((yspace++) * 10), "\x03 Need %c", getBuildingWorkerCode(b));
        }
        else if (b.status == BuildingStatus::Assigned)
        {
            BWAPI::Broodwar->drawTextScreen(x,y+40+((yspace)*10),"\x03 %s %d",b.type.getName().c_str(),b.builderUnit->getID());
            BWAPI::Broodwar->drawTextScreen(x+150,y+40+((yspace++)*10),"\x03 A %c (%d,%d)",getBuildingWorkerCode(b),b.finalPosition.x,b.finalPosition.y);

            int x1 = b.finalPosition.x*32;
            int y1 = b.finalPosition.y*32;
            int x2 = (b.finalPosition.x + b.type.tileWidth())*32;
            int y2 = (b.finalPosition.y + b.type.tileHeight())*32;

            BWAPI::Broodwar->drawLineMap(b.builderUnit->getPosition().x,b.builderUnit->getPosition().y,(x1+x2)/2,(y1+y2)/2,BWAPI::Colors::Orange);
            BWAPI::Broodwar->drawBoxMap(x1,y1,x2,y2,BWAPI::Colors::Red,false);
        }
        else if (b.status == BuildingStatus::UnderConstruction)
        {
            BWAPI::Broodwar->drawTextScreen(x,y+40+((yspace)*10),"\x03 %s %d",b.type.getName().c_str(),b.buildingUnit->getID());
            BWAPI::Broodwar->drawTextScreen(x+150,y+40+((yspace++)*10),"\x03 Const %c",getBuildingWorkerCode(b));
        }
    }
}

BuildingManager & BuildingManager::Instance()
{
    static BuildingManager instance;
    return instance;
}

// The buildings queued and not yet started.
std::vector<BWAPI::UnitType> BuildingManager::buildingsQueued()
{
    std::vector<BWAPI::UnitType> buildingsQueued;

    for (const auto & b : _buildings)
    {
        if (b.status == BuildingStatus::Unassigned || b.status == BuildingStatus::Assigned)
        {
            buildingsQueued.push_back(b.type);
        }
    }

    return buildingsQueued;
}

// Cancel a given building when possible.
// Used as part of the extractor trick or in an emergency.
// NOTE CombatCommander::cancelDyingBuildings() can also cancel buildings, including
//      morphing zerg structures which the BuildingManager does not handle.
void BuildingManager::cancelBuilding(Building & b)
{
	if (b.status == BuildingStatus::Unassigned)
	{
		_reservedMinerals -= b.type.mineralPrice();
		_reservedGas -= b.type.gasPrice();
		undoBuildings({ b });
	}
	else if (b.status == BuildingStatus::Assigned)
	{
		_reservedMinerals -= b.type.mineralPrice();
		_reservedGas -= b.type.gasPrice();
		WorkerManager::Instance().finishedWithWorker(b.builderUnit);
		BuildingPlacer::Instance().freeTiles(b.finalPosition, b.type.tileWidth(), b.type.tileHeight());
		undoBuildings({ b });
	}
	else if (b.status == BuildingStatus::UnderConstruction)
	{
		if (b.buildingUnit && b.buildingUnit->exists() && !b.buildingUnit->isCompleted())
		{
			b.buildingUnit->cancelConstruction();
			BuildingPlacer::Instance().freeTiles(b.finalPosition, b.type.tileWidth(), b.type.tileHeight());
		}
		undoBuildings({ b });
	}
	else
	{
		UAB_ASSERT(false, "unexpected building status");
	}
}

// It's an emergency. Cancel all buildings which are not yet started.
void BuildingManager::cancelQueuedBuildings()
{
	for (Building & b : _buildings)
	{
		if (b.status == BuildingStatus::Unassigned || b.status == BuildingStatus::Assigned)
		{
			cancelBuilding(b);
		}
	}
}

// It's an emergency. Cancel all buildings of a given type.
void BuildingManager::cancelBuildingType(BWAPI::UnitType t)
{
	for (Building & b : _buildings)
	{
		if (b.type == t)
		{
			cancelBuilding(b);
		}
	}
}

// TODO fails in placing a hatchery after all others are destroyed - why?
BWAPI::TilePosition BuildingManager::getBuildingLocation(const Building & b)
{
	if (b.isGasSteal)
    {
        BWTA::BaseLocation * enemyBaseLocation = InformationManager::Instance().getEnemyMainBaseLocation();
        UAB_ASSERT(enemyBaseLocation,"Should find enemy base before gas steal");
        UAB_ASSERT(enemyBaseLocation->getGeysers().size() > 0,"Should have spotted an enemy geyser");

        for (auto & unit : enemyBaseLocation->getGeysers())
        {
            return BWAPI::TilePosition(unit->getInitialTilePosition());
        }
    }

	int numPylons = BWAPI::Broodwar->self()->completedUnitCount(BWAPI::UnitTypes::Protoss_Pylon);
	if (b.type.requiresPsi() && numPylons == 0)
	{
		return BWAPI::TilePositions::None;
	}

	if (b.type.isRefinery())
	{
		return BuildingPlacer::Instance().getRefineryPosition();
	}

	if (b.type.isResourceDepot())
	{
		BWTA::BaseLocation * natural = InformationManager::Instance().getMyNaturalLocation();
		if (b.macroLocation == MacroLocation::Natural && natural)
		{
			return natural->getTilePosition();
		}
		if (b.macroLocation != MacroLocation::Macro)
		{
			return MapTools::Instance().getNextExpansion(b.macroLocation == MacroLocation::Hidden, b.macroLocation == MacroLocation::MinOnly);
		}
		// Else if it's a macro hatchery, treat it like any other building.
	}

    int distance = Config::Macro::BuildingSpacing;
	if (b.type == BWAPI::UnitTypes::Terran_Bunker ||
		b.type == BWAPI::UnitTypes::Protoss_Photon_Cannon ||
		b.type == BWAPI::UnitTypes::Zerg_Creep_Colony)
	{
		// Pack defenses tightly together.
		distance = 0;
	}
	else if (b.type == BWAPI::UnitTypes::Protoss_Pylon)
    {
		if (numPylons < 3)
		{
			// Early pylons may be spaced differently than other buildings.
			distance = Config::Macro::PylonSpacing;
		}
		else
		{
			// Building spacing == 1 is usual. Be more generous with pylons.
			distance = 2;
		}
	}

	// Try to pack protoss buildings more closely together. Space can run out.
	bool noVerticalSpacing = false;
	if (b.type == BWAPI::UnitTypes::Protoss_Gateway ||
		b.type == BWAPI::UnitTypes::Protoss_Forge || 
		b.type == BWAPI::UnitTypes::Protoss_Stargate || 
		b.type == BWAPI::UnitTypes::Protoss_Citadel_of_Adun || 
		b.type == BWAPI::UnitTypes::Protoss_Templar_Archives || 
		b.type == BWAPI::UnitTypes::Protoss_Gateway)
	{
		noVerticalSpacing = true;
	}

	// Get a position within our region.
	return BuildingPlacer::Instance().getBuildLocationNear(b, distance, noVerticalSpacing);
}

// The building failed or is canceled.
// Undo any connections with other data structures, then delete.
void BuildingManager::undoBuildings(const std::vector<Building> & toRemove)
{
	for (auto & b : toRemove)
	{
		// If the building was to establish a base, unreserve the base location.
		if (b.type.isResourceDepot() && b.macroLocation != MacroLocation::Macro && b.finalPosition.isValid())
		{
			InformationManager::Instance().unreserveBase(b.finalPosition);
		}
	}

	removeBuildings(toRemove);
}

// Remove buildings from the list of buildings--nothing more, nothing less.
void BuildingManager::removeBuildings(const std::vector<Building> & toRemove)
{
    for (auto & b : toRemove)
    {
		auto & it = std::find(_buildings.begin(), _buildings.end(), b);

        if (it != _buildings.end())
        {
            _buildings.erase(it);
        }
    }
}
