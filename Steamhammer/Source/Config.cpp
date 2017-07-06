#include "Config.h"
#include "UABAssert.h"

namespace Config
{
    namespace ConfigFile
    {
        bool ConfigFileFound                = false;
        bool ConfigFileParsed               = false;
        std::string ConfigFileLocation      = "bwapi-data/AI/Steamhammer_1.2.3.json";
    }

    namespace Strategy
    {
        std::string ProtossStrategyName     = "1ZealotCore";			// default
        std::string TerranStrategyName      = "11Rax";					// default
        std::string ZergStrategyName        = "9PoolSpeed";				// default
        std::string StrategyName            = "9PoolSpeed";
        std::string ReadDir                 = "bwapi-data/read/";
        std::string WriteDir                = "bwapi-data/write/";
        bool ScoutHarassEnemy               = true;
		bool SurrenderWhenHopeIsLost        = true;
        bool UseEnemySpecificStrategy       = true;
        bool FoundEnemySpecificStrategy     = false;
    }

    namespace Modules							    
    {
        // the default tournament bot modules
        bool UsingStrategyIO                = false;    // toggle the use of file io for strategy
		
        // extra things, don't enable unless you know what they are
        bool UsingBuildOrderDemo            = false;
    }

    namespace BotInfo
    {
        std::string BotName                 = "Steamhammer";
        std::string Authors                 = "Jay Scott";
        bool PrintInfoOnStart               = false;
    }

    namespace BWAPIOptions
    {
        int SetLocalSpeed                   = 42;
        int SetFrameSkip                    = 0;
        bool EnableUserInput                = true;
        bool EnableCompleteMapInformation   = false;
    }
    
    namespace Tournament						
    {
        int GameEndFrame                    = 86400;	
    }
    
    namespace Debug								
    {
        bool DrawGameInfo                   = true;
        bool DrawUnitHealthBars             = false;
        bool DrawProductionInfo             = true;
        bool DrawBuildOrderSearchInfo       = false;
        bool DrawScoutInfo                  = false;
        bool DrawResourceInfo               = false;
        bool DrawWorkerInfo                 = false;
        bool DrawModuleTimers               = false;
        bool DrawReservedBuildingTiles      = false;
        bool DrawCombatSimulationInfo       = false;
        bool DrawBuildingInfo               = false;
        bool DrawMouseCursorInfo            = false;
        bool DrawEnemyUnitInfo              = false;
        bool DrawBWTAInfo                   = false;
        bool DrawMapGrid                    = false;
		bool DrawMapDistances				= false;
		bool DrawBaseInfo					= false;
		bool DrawStrategyBossInfo			= false;
		bool DrawUnitTargetInfo				= false;
		bool DrawUnitOrders					= false;
        bool DrawSquadInfo                  = false;
        bool DrawBOSSStateInfo              = false;

        std::string ErrorLogFilename        = "Steamhammer_ErrorLog.txt";
        bool LogAssertToErrorFile           = false;

        BWAPI::Color ColorLineTarget        = BWAPI::Colors::White;
        BWAPI::Color ColorLineMineral       = BWAPI::Colors::Cyan;
        BWAPI::Color ColorUnitNearEnemy     = BWAPI::Colors::Red;
        BWAPI::Color ColorUnitNotNearEnemy  = BWAPI::Colors::Green;
    }

    namespace Micro								
    {
        bool KiteWithRangedUnits            = true;
        std::set<BWAPI::UnitType> KiteLongerRangedUnits;
        bool WorkersDefendRush              = false; 
		int RetreatMeleeUnitShields         = 0;
        int RetreatMeleeUnitHP              = 0;
        int CombatRegroupRadius             = 300;      // radius of units around frontmost unit for combat sim
        int UnitNearEnemyRadius             = 600;      // radius to consider a unit 'near' to an enemy unit
		int ScoutDefenseRadius				= 600;		// radius to chase enemy scout worker
    }

    namespace Macro
    {
        int BOSSFrameLimit                  = 160;
        int WorkersPerRefinery              = 3;
		double WorkersPerPatch              = 3.0;
		int AbsoluteMaxWorkers				= 75;
        int BuildingSpacing                 = 1;
        int PylonSpacing                    = 3;
		int ProductionJamFrameLimit			= 360;
    }

    namespace Tools								
    {
        extern int MAP_GRID_SIZE            = 320;      // size of grid spacing in MapGrid
    }
}