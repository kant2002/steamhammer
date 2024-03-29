
#pragma once

#include "BWAPI.h"
#include <cassert>

namespace Config
{
    namespace ConfigFile
    {
        extern bool ConfigFileFound;
        extern bool ConfigFileParsed;
        extern std::string ConfigFileLocation;
    }

    namespace BotInfo
    {
        extern std::string BotName;
        extern std::string Authors;
        extern bool PrintInfoOnStart;
    }

    namespace IO
    {
        extern std::string ErrorLogFilename;
        extern bool LogAssertToErrorFile;

        extern std::string StaticDir;
        extern std::string PreparedDataDir;
        extern std::string ReadDir;
        extern std::string WriteDir;
        extern std::string OpeningTimingFile;
        extern int MaxGameRecords;
        extern bool ReadOpponentModel;
        extern bool WriteOpponentModel;
    }

    namespace Skills
    {
        extern bool UnderSCHNAIL;
        extern bool SCHNAILMeansHuman;
        extern bool HumanOpponent;
        extern bool SurrenderWhenHopeIsLost;

        extern bool ScoutHarassEnemy;
        extern bool GasSteal;

        extern bool Burrow;
        extern bool UseSunkenRangeBug;

        extern int MaxQueens;
        extern int MaxInfestedTerrans;
        extern int MaxDefilers;
    }

    namespace Strategy
    {
        extern bool Crazyhammer;
        extern std::string StrategyName;
        extern bool UsePlanRecognizer;
        extern bool UseEnemySpecificStrategy;
        extern bool FoundEnemySpecificStrategy;
    }

    namespace BWAPIOptions
    {
        extern int SetLocalSpeed;
        extern int SetFrameSkip;
        extern bool EnableUserInput;
        extern bool EnableCompleteMapInformation;
    }

    namespace Tournament
    {
        extern int GameEndFrame;	
    }

    namespace Debug
    {
        extern bool DrawGameInfo;
        extern bool DrawUnitHealthBars;
        extern bool DrawProductionInfo;
        extern bool DrawBuildOrderSearchInfo;
        extern bool DrawQueueFixInfo;
        extern bool DrawScoutInfo;
        extern bool DrawWorkerInfo;
        extern bool DrawModuleTimers;
        extern bool DrawReservedBuildingTiles;
        extern bool DrawCombatSimulationInfo;
        extern bool DrawBuildingInfo;
        extern bool DrawStaticDefensePlan;
        extern bool DrawEnemyUnitInfo;
        extern bool DrawUnitCounts;
        extern bool DrawHiddenEnemies;
        extern bool DrawMapInfo;
        extern bool DrawMapGrid;
        extern bool DrawMapDistances;
        extern bool DrawTerrainHeights;
        extern bool DrawBaseInfo;
        extern bool DrawExpoScores;
        extern bool DrawStrategyBossInfo;
        extern bool DrawUnitTargets;
        extern bool DrawUnitOrders;
        extern bool DrawMicroState;
        extern bool DrawLurkerTactics;
        extern bool DrawSquadInfo;
        extern bool DrawClusters;
        extern bool DrawDefenseClusters;
        extern bool DrawResourceAmounts;

        extern BWAPI::Color ColorLineTarget;
        extern BWAPI::Color ColorLineMineral;
        extern BWAPI::Color ColorUnitNearEnemy;
        extern BWAPI::Color ColorUnitNotNearEnemy;
    }

    namespace Micro
    {
        extern bool KiteWithRangedUnits;
        extern bool WorkersDefendRush;
        extern int RetreatMeleeUnitShields;
        extern int RetreatMeleeUnitHP;
        extern int CombatSimRadius;         
        extern int ScoutDefenseRadius;
    }
    
    namespace Macro
    {
        extern int BOSSFrameLimit;
        extern int WorkersPerRefinery;
        extern double WorkersPerPatch;
        extern int AbsoluteMaxWorkers;
        extern int BuildingSpacing;
        extern int PylonSpacing;
        extern int ProductionJamFrameLimit;
        extern bool ExpandToIslands;
    }

    namespace Tools
    {
        extern int MAP_GRID_SIZE;
    }
}