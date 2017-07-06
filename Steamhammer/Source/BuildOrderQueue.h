#pragma once

#include "Common.h"

#include "MacroAct.h"

namespace UAlbertaBot
{
struct BuildOrderItem
{
    MacroAct			macroAct;	// the thing we want to 'build'
    int					priority;	// the priority at which to place it in the queue
    bool                isGasSteal;

    BuildOrderItem(MacroAct m,int p,bool gasSteal = false)
        : macroAct(m)
        , priority(p)
        , isGasSteal(gasSteal) 
    {
    }

    bool operator<(const BuildOrderItem &x) const
    {
        return priority < x.priority;
    }
};

class BuildOrderQueue
{
    std::deque< BuildOrderItem > queue;

    int lowestPriority;
    int highestPriority;
    int defaultPrioritySpacing;

	void queueItem(BuildOrderItem b);							// queues something with a given priority

public:

    BuildOrderQueue();

    void clearAll();											// clears the entire build order queue
    void queueAsHighestPriority(MacroAct m,bool gasSteal = false);		// queues something at the highest priority
    void queueAsLowestPriority(MacroAct m);						// queues something at the lowest priority
    void removeHighestPriorityItem();							// removes the highest priority item

    size_t size() const;										// returns the size of the queue
	bool isEmpty() const;
	
    void removeAll(MacroAct m);									// removes all matching meta types from queue

    BuildOrderItem & getHighestPriorityItem();					// returns the highest priority item
	
	bool anyInQueue(BWAPI::UpgradeType type);
	bool anyInQueue(BWAPI::UnitType type);
	size_t numInQueue(BWAPI::UnitType type);
	void totalCosts(int & minerals, int & gas);

	void drawQueueInformation(int x, int y, bool outOfBook);

    // overload the bracket operator for ease of use
    BuildOrderItem operator [] (int i);
};
}