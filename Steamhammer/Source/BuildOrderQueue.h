#pragma once

#include "Common.h"

#include "MacroAct.h"

namespace UAlbertaBot
{
struct BuildOrderItem
{
    MacroAct macroAct;	   // the thing we want to produce
    bool     isGasSteal;

    BuildOrderItem(MacroAct m, bool gasSteal = false)
        : macroAct(m)
        , isGasSteal(gasSteal) 
    {
    }
};

class BuildOrderQueue
{
    std::deque< BuildOrderItem > queue;
	bool modified;                                              // so ProductionManager can detect changes made behind its back

	void queueItem(BuildOrderItem b);							// queues something with a given priority

public:

    BuildOrderQueue();

	bool isModified() const { return modified; };
	void resetModified() { modified = false; };

    void clearAll();											// clears the entire build order queue
    void queueAsHighestPriority(MacroAct m,bool gasSteal = false);		// queues something at the highest priority
    void queueAsLowestPriority(MacroAct m);						// queues something at the lowest priority
    void removeHighestPriorityItem();							// removes the highest priority item
	void doneWithHighestPriorityItem();							// removes highest priority item without setting `modified`

    size_t size() const;										// number of items in the queue
	bool isEmpty() const;
	
    const BuildOrderItem & getHighestPriorityItem() const;		// returns the highest priority item
	BWAPI::UnitType getNextUnit() const;						// skip commands and return item if it's a unit
	int getNextGasCost(int n) const;							// look n ahead, return next nonzero gas cost
	
	bool anyInQueue(BWAPI::UpgradeType type) const;
	bool anyInQueue(BWAPI::UnitType type) const;
	size_t numInQueue(BWAPI::UnitType type) const;
	void totalCosts(int & minerals, int & gas) const;

	void drawQueueInformation(int x, int y, bool outOfBook);

    // overload the bracket operator for ease of use
    BuildOrderItem operator [] (int i);
};
}