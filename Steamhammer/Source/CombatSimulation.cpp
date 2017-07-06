#include "CombatSimulation.h"

using namespace UAlbertaBot;

CombatSimulation::CombatSimulation()
{
}

// sets the starting states based on the combat units within a radius of a given position
// this center will most likely be the position of the forwardmost combat unit we control
void CombatSimulation::setCombatUnits(const BWAPI::Position & center, const int radius)
{
	SparCraft::GameState s;

	if (Config::Debug::DrawCombatSimulationInfo)
	{
		BWAPI::Broodwar->drawCircleMap(center.x, center.y, 6, BWAPI::Colors::Red, true);
		BWAPI::Broodwar->drawCircleMap(center.x, center.y, radius, BWAPI::Colors::Red);
	}

	BWAPI::Unitset ourCombatUnits;
	std::vector<UnitInfo> enemyCombatUnits;

	MapGrid::Instance().GetUnits(ourCombatUnits, center, Config::Micro::CombatRegroupRadius, true, false);
	InformationManager::Instance().getNearbyForce(enemyCombatUnits, center, BWAPI::Broodwar->enemy(), Config::Micro::CombatRegroupRadius);

	for (auto & unit : ourCombatUnits)
	{
        if (unit->getType().isWorker())
        {
            continue;
        }

        if (InformationManager::Instance().isCombatUnit(unit->getType()) && SparCraft::System::isSupportedUnitType(unit->getType()))
		{
            try
            {
			    s.addUnit(getSparCraftUnit(unit));
            }
            catch (int e)
            {
				// Ignore the exception and the unit.
				e = 0;    // use the variable to avoid a pointless warning
				//BWAPI::Broodwar->printf("Problem Adding Self Unit with ID: %d", unit->getID());
            }
		}
	}

	for (UnitInfo & ui : enemyCombatUnits)
	{
        if (ui.type.isWorker())
        {
            continue;
        }

		// Pretend that a bunker is 5 marines with prorated hit points.
		// TODO account for repair--we can pretend that the pretend marines have more hit points
        if (ui.type == BWAPI::UnitTypes::Terran_Bunker)
        {
            double hpRatio = static_cast<double>(ui.lastHealth) / ui.type.maxHitPoints();

            SparCraft::Unit marine( BWAPI::UnitTypes::Terran_Marine,
                            SparCraft::Position(ui.lastPosition), 
                            ui.unitID, 
                            getSparCraftPlayerID(ui.player), 
                            static_cast<int>(BWAPI::UnitTypes::Terran_Marine.maxHitPoints() * hpRatio), 
                            0,
		                    BWAPI::Broodwar->getFrameCount(), 
                            BWAPI::Broodwar->getFrameCount());	

            for (size_t i(0); i < 5; ++i)
            {
                s.addUnit(marine);
            }
            
            continue;
        }

		// Pretend that a spore colony is 2 stacked turrets with prorated hit points.
		/* doesn't help at all
		if (ui.type == BWAPI::UnitTypes::Zerg_Spore_Colony)
		{
			double hpRatio = static_cast<double>(ui.lastHealth) / ui.type.maxHitPoints();

			SparCraft::Unit turret(BWAPI::UnitTypes::Terran_Missile_Turret,
				SparCraft::Position(ui.lastPosition),
				ui.unitID,
				getSparCraftPlayerID(ui.player),
				static_cast<int>(BWAPI::UnitTypes::Terran_Missile_Turret.maxHitPoints() * hpRatio),
				0,
				BWAPI::Broodwar->getFrameCount(),
				BWAPI::Broodwar->getFrameCount());

			BWAPI::Broodwar->printf("adding turret");
			s.addUnit(turret);
			s.addUnit(turret);

			continue;
		}
		*/

		// I think it excludes enemy air units so they don't scare our zerglings away--
		// only a good idea in certain circumstances.
		// SparCraft claims to support mutas, wraiths, BCs, scouts.
        if (!ui.type.isFlyer() && SparCraft::System::isSupportedUnitType(ui.type) && ui.completed)
		{
            try
            {
			    s.addUnit(getSparCraftUnit(ui));
            }
            catch (int e)
            {
				// Ignore the exception and the unit.
				e = 0;    // use the variable to avoid a pointless warning
                //BWAPI::Broodwar->printf("Problem Adding Enemy Unit with ID: %d %d", ui.unitID, e);
            }
		}
	}

	s.finishedMoving();

	state = s;
}

// Gets a SparCraft unit from a BWAPI::Unit, used for our own units since we have all their info
const SparCraft::Unit CombatSimulation::getSparCraftUnit(BWAPI::Unit unit) const
{
    return SparCraft::Unit( unit->getType(),
                            SparCraft::Position(unit->getPosition()), 
                            unit->getID(), 
                            getSparCraftPlayerID(unit->getPlayer()), 
                            unit->getHitPoints() + unit->getShields(), 
                            0,
		                    BWAPI::Broodwar->getFrameCount(), 
                            BWAPI::Broodwar->getFrameCount());	
}

// Gets a SparCraft unit from a UnitInfo struct, needed to get units of enemy behind FoW
const SparCraft::Unit CombatSimulation::getSparCraftUnit(const UnitInfo & ui) const
{
	BWAPI::UnitType type = ui.type;

    // this is a hack, treat medics as a marine for now
	// TODO this is weird: SparCraft appears to support healing
	if (type == BWAPI::UnitTypes::Terran_Medic)
	{
		type = BWAPI::UnitTypes::Terran_Marine;
	}

    return SparCraft::Unit( ui.type, 
                            SparCraft::Position(ui.lastPosition), 
                            ui.unitID, 
                            getSparCraftPlayerID(ui.player), 
                            ui.lastHealth, 
                            0,
		                    BWAPI::Broodwar->getFrameCount(), 
                            BWAPI::Broodwar->getFrameCount());	
}

SparCraft::ScoreType CombatSimulation::simulateCombat()
{
    try
    {
	    SparCraft::GameState s1(state);

        SparCraft::PlayerPtr selfNOK(new SparCraft::Player_NOKDPS(getSparCraftPlayerID(BWAPI::Broodwar->self())));

	    SparCraft::PlayerPtr enemyNOK(new SparCraft::Player_NOKDPS(getSparCraftPlayerID(BWAPI::Broodwar->enemy())));

	    SparCraft::Game g (s1, selfNOK, enemyNOK, 2000);

	    g.play();
	
	    SparCraft::ScoreType eval =  g.getState().eval(SparCraft::Players::Player_One, SparCraft::EvaluationMethods::LTD2).val();

        if (Config::Debug::DrawCombatSimulationInfo)
        {
            std::stringstream ss1;
            ss1 << "Initial State:\n";
            ss1 << s1.toStringCompact() << "\n\n";

            std::stringstream ss2;

            ss2 << "Predicted Outcome: " << eval << "\n";
            ss2 << g.getState().toStringCompact() << "\n";

            BWAPI::Broodwar->drawTextScreen(150,200,"%s", ss1.str().c_str());
            BWAPI::Broodwar->drawTextScreen(300,200,"%s", ss2.str().c_str());

			std::string prefix = (eval < 0) ? "\x06" : "\x07";
	        BWAPI::Broodwar->drawTextScreen(240, 280, "Combat Sim : %s%d", prefix.c_str(), eval);
        }
        
	    return eval;
    }
    catch (int e)
    {
        //BWAPI::Broodwar->printf("SparCraft FatalError, simulateCombat() threw");

        return e;
    }
}

const SparCraft::GameState & CombatSimulation::getSparCraftState() const
{
	return state;
}

const SparCraft::IDType CombatSimulation::getSparCraftPlayerID(BWAPI::Player player) const
{
	if (player == BWAPI::Broodwar->self())
	{
		return SparCraft::Players::Player_One;
	}
	if (player == BWAPI::Broodwar->enemy())
	{
		return SparCraft::Players::Player_Two;
	}

	return SparCraft::Players::Player_None;
}
