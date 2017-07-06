#pragma once

#include "Common.h"
#include "InformationManager.h"
#include "WorkerManager.h"
#include "BuildOrder.h"
#include "BuildOrderQueue.h"

namespace UAlbertaBot
{
typedef std::pair<MacroAct, size_t> MetaPair;
typedef std::vector<MetaPair> MetaPairVector;

struct Strategy
{
    std::string _name;
    BWAPI::Race _race;
    int         _wins;
    int         _losses;
	std::string _openingGroup;
    BuildOrder  _buildOrder;

    Strategy()
        : _name("None")
        , _race(BWAPI::Races::None)
        , _wins(0)
        , _losses(0)
		, _openingGroup("")
    {
    }

	Strategy(const std::string & name, const BWAPI::Race & race, const std::string & openingGroup, const BuildOrder & buildOrder)
        : _name(name)
        , _race(race)
        , _wins(0)
        , _losses(0)
		, _openingGroup(openingGroup)
		, _buildOrder(buildOrder)
	{
    }
};

class StrategyManager 
{
	StrategyManager();

	BWAPI::Race					    _selfRace;
	BWAPI::Race					    _enemyRace;
    std::map<std::string, Strategy> _strategies;
    int                             _totalGamesPlayed;
    const BuildOrder                _emptyBuildOrder;
	std::string						_openingGroup;

	        void	                writeResults();
	const	int					    getScore(BWAPI::Player player) const;
	const	double				    getUCBValue(const size_t & strategy) const;
	const	bool				    shouldExpandNow() const;
    const	MetaPairVector		    getProtossBuildOrderGoal();
	const	MetaPairVector		    getTerranBuildOrderGoal();
	const	MetaPairVector		    getZergBuildOrderGoal() const;

	bool							detectSupplyBlock(BuildOrderQueue & queue);
	bool							canPlanBuildOrderNow() const;
	void							performBuildOrderSearch();

public:
    
	static	StrategyManager &	    Instance();

			void				    onEnd(const bool isWinner);

            void                    addStrategy(const std::string & name, Strategy & strategy);
			void					setOpeningGroup();
	const	std::string &			getOpeningGroup() const;
            void                    setLearnedStrategy();
            void	                readResults();
	const	MetaPairVector		    getBuildOrderGoal();
	const	BuildOrder &            getOpeningBookBuildOrder() const;

			void					handleUrgentProductionIssues(BuildOrderQueue & queue);
			void					freshProductionPlan();
};

}