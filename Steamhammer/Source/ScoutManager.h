#pragma once

#include "Common.h"
#include "MicroManager.h"
#include "InformationManager.h"

namespace UAlbertaBot
{
class ScoutManager 
{
	BWAPI::Unit						_workerScout;
    std::string                     _scoutStatus;
    std::string                     _gasStealStatus;
	bool							_scoutLocationOnly;
	bool			                _scoutUnderAttack;
	bool							_tryGasSteal;
    bool                            _didGasSteal;
    bool                            _gasStealFinished;
    int                             _currentRegionVertexIndex;
    int                             _previousScoutHP;
	std::vector<BWAPI::Position>    _enemyRegionVertices;

	bool                            enemyWorkerInRadius();
    bool			                immediateThreat();
    void                            gasSteal();
    int                             getClosestVertexIndex(BWAPI::Unit unit);
    BWAPI::Position                 getFleePosition();
	BWAPI::Unit						getEnemyGeyser();
	BWAPI::Unit						enemyWorkerToHarass();
    void                            followPerimeter();
	void                            moveScout();
    void                            drawScoutInformation(int x, int y);
    void                            calculateEnemyRegionVertices();

	ScoutManager();

public:

    static ScoutManager & Instance();

	void update();

    void setWorkerScout(BWAPI::Unit unit);
	void releaseWorkerScout();
	void setGasSteal();
	void setScoutLocationOnly();
};
}