#pragma once

#include "BuildOrderQueue.h"

namespace UAlbertaBot
{

class StrategyBossZerg
{
	StrategyBossZerg::StrategyBossZerg();

	BWAPI::Player _self;
	int _existingSupply;
	int _pendingSupply;

	void figureSupply();
	void figure();

public:
	static StrategyBossZerg & Instance();

	// Called once per frame for emergencies and other urgent needs.
	void handleUrgentProductionIssues(BuildOrderQueue & queue);

};

};
