#include "BuildOrderQueue.h"

using namespace UAlbertaBot;

BuildOrderQueue::BuildOrderQueue() 
{
}

void BuildOrderQueue::clearAll() 
{
	queue.clear();
}

BuildOrderItem & BuildOrderQueue::getHighestPriorityItem()
{
	UAB_ASSERT(!queue.empty(), "taking from empty queue");

	// the queue will be sorted with the highest priority at the back
	return queue.back();
}

// Return the next unit type in the queue, or None, skipping over commands.
BWAPI::UnitType BuildOrderQueue::getNextUnit()
{
	for (int i = queue.size() - 1; i >= 0; --i)
	{
		MacroAct & act = queue[i].macroAct;
		if (act.isUnit())
		{
			return act.getUnitType();
		}
		if (!act.isCommand())
		{
			return BWAPI::UnitTypes::None;
		}
	}

	return BWAPI::UnitTypes::None;
}

// Return the gas cost of the next item in the queue that has a nonzero gas cost.
int BuildOrderQueue::getNextGasCost(int n)
{
	for (int i = queue.size() - 1; i >= std::max(0, int(queue.size()) - n); --i)
	{
		MacroAct & act = queue[i].macroAct;
		int price = act.gasPrice();
		if (price > 0)
		{
			return price;
		}
	}

	return 0;
}

void BuildOrderQueue::queueAsHighestPriority(MacroAct m, bool gasSteal)
{
	queue.push_back(BuildOrderItem(m, gasSteal));
}

void BuildOrderQueue::queueAsLowestPriority(MacroAct m) 
{
	queue.push_front(BuildOrderItem(m));
}

void BuildOrderQueue::removeHighestPriorityItem() 
{
	// remove the back element of the vector
	queue.pop_back();
}

size_t BuildOrderQueue::size() const
{
	return queue.size();
}

bool BuildOrderQueue::isEmpty() const
{
	return queue.empty();
}

bool BuildOrderQueue::anyInQueue(BWAPI::UpgradeType type)
{
	for (auto & item : queue)
	{
		if (item.macroAct.isUpgrade() && item.macroAct.getUpgradeType() == type)
		{
			return true;
		}
	}
	return false;
}

bool BuildOrderQueue::anyInQueue(BWAPI::UnitType type)
{
	for (auto & item : queue)
	{
		if (item.macroAct.isUnit() && item.macroAct.getUnitType() == type)
		{
			return true;
		}
	}
	return false;
}

size_t BuildOrderQueue::numInQueue(BWAPI::UnitType type)
{
	size_t count = 0;
	for (auto & item : queue)
	{
		if (item.macroAct.isUnit() && item.macroAct.getUnitType() == type)
		{
			++count;
		}
	}
	return count;
}

void BuildOrderQueue::totalCosts(int & minerals, int & gas)
{
	minerals = 0;
	gas = 0;
	for (auto & item : queue)
	{
		minerals += item.macroAct.mineralPrice();
		gas += item.macroAct.gasPrice();
	}
}

void BuildOrderQueue::drawQueueInformation(int x, int y, bool outOfBook) 
{
    if (!Config::Debug::DrawProductionInfo)
    {
        return;
    }
	
	char prefix = white;

	size_t reps = std::min(size_t(12), queue.size());
	int remaining = queue.size() - reps;
	
	// for each item in the queue
	for (size_t i(0); i<reps; i++) {

		prefix = white;

		const BuildOrderItem & item = queue[queue.size() - 1 - i];
        const MacroAct & act = item.macroAct;

        if (act.isUnit())
        {
            if (act.getUnitType().isWorker())
            {
                prefix = cyan;
            }
            else if (act.getUnitType().supplyProvided() > 0)
            {
                prefix = yellow;
            }
            else if (act.getUnitType().isRefinery())
            {
                prefix = gray;
            }
            else if (act.isBuilding())
            {
                prefix = orange;
            }
            else if (act.getUnitType().groundWeapon() != BWAPI::WeaponTypes::None || act.getUnitType().airWeapon() != BWAPI::WeaponTypes::None)
            {
                prefix = red;
            }
        }
		else if (act.isCommand())
		{
			prefix = white;
		}

		BWAPI::Broodwar->drawTextScreen(x, y, " %c%s", prefix, TrimRaceName(act.getName()).c_str());
		y += 10;
	}

	std::stringstream endMark;
	if (remaining > 0)
	{
		endMark << '+' << remaining << " more ";
	}
	if (!outOfBook)
	{
		endMark << "[book]";
	}
	if (!endMark.str().empty())
	{
		BWAPI::Broodwar->drawTextScreen(x, y, " %c%s", white, endMark.str().c_str());
	}
}

BuildOrderItem BuildOrderQueue::operator [] (int i)
{
	return queue[i];
}
