#include "BuildOrderQueue.h"

using namespace UAlbertaBot;

BuildOrderQueue::BuildOrderQueue() 
	: highestPriority(0), 
	  lowestPriority(0),
	  defaultPrioritySpacing(10)
{
}

void BuildOrderQueue::clearAll() 
{
	// clear the queue
	queue.clear();

	// reset the priorities
	highestPriority = 0;
	lowestPriority = 0;
}

BuildOrderItem & BuildOrderQueue::getHighestPriorityItem()
{
	UAB_ASSERT(!queue.empty(), "taking from empty queue");

	// the queue will be sorted with the highest priority at the back
	return queue.back();
}

void BuildOrderQueue::queueItem(BuildOrderItem b) 
{
	// if the queue is empty, set the highest and lowest priorities
	if (queue.empty()) 
	{
		highestPriority = b.priority;
		lowestPriority = b.priority;
	}

	// push the item into the queue
	if (b.priority <= lowestPriority) 
	{
		queue.push_front(b);
	}
	else
	{
		queue.push_back(b);
	}

	// if the item is somewhere in the middle, we have to sort again
	if ((queue.size() > 1) && (b.priority < highestPriority) && (b.priority > lowestPriority)) 
	{
		// sort the list in ascending order, putting highest priority at the top
		std::sort(queue.begin(), queue.end());
	}

	// update the highest or lowest if it is beaten
	highestPriority = (b.priority > highestPriority) ? b.priority : highestPriority;
	lowestPriority  = (b.priority < lowestPriority)  ? b.priority : lowestPriority;
}

void BuildOrderQueue::queueAsHighestPriority(MacroAct m, bool gasSteal) 
{
	// the new priority will be higher
	int newPriority = highestPriority + defaultPrioritySpacing;

	// queue the item
	queueItem(BuildOrderItem(m, newPriority, gasSteal));
}

void BuildOrderQueue::queueAsLowestPriority(MacroAct m) 
{
	// the new priority will be higher
	int newPriority = lowestPriority - defaultPrioritySpacing;

	// queue the item
	queueItem(BuildOrderItem(m, newPriority));
}

void BuildOrderQueue::removeHighestPriorityItem() 
{
	// remove the back element of the vector
	queue.pop_back();

	// if the list is not empty, set the highest accordingly
	highestPriority = queue.empty() ? 0 : queue.back().priority;
	lowestPriority  = queue.empty() ? 0 : lowestPriority;
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
	
	std::string prefix = "\x04";

	size_t reps = std::min(size_t(12), queue.size());
	int remaining = queue.size() - reps;
	
	// for each item in the queue
	for (size_t i(0); i<reps; i++) {

		prefix = "\x04";

		const BuildOrderItem & item = queue[queue.size() - 1 - i];
        const MacroAct & act = item.macroAct;

        if (act.isUnit())
        {
            if (act.getUnitType().isWorker())
            {
                prefix = "\x1F";
            }
            else if (act.getUnitType().supplyProvided() > 0)
            {
                prefix = "\x03";
            }
            else if (act.getUnitType().isRefinery())
            {
                prefix = "\x1E";
            }
            else if (act.isBuilding())
            {
                prefix = "\x11";
            }
            else if (act.getUnitType().groundWeapon() != BWAPI::WeaponTypes::None || act.getUnitType().airWeapon() != BWAPI::WeaponTypes::None)
            {
                prefix = "\x06";
            }
        }
		else if (act.isCommand())
		{
			prefix = "\x04";
		}

		BWAPI::Broodwar->drawTextScreen(x, y, " %s%s", prefix.c_str(), TrimRaceName(act.getName()).c_str());
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
		BWAPI::Broodwar->drawTextScreen(x, y, " %s%s", "\x04", endMark.str().c_str());
	}
}

BuildOrderItem BuildOrderQueue::operator [] (int i)
{
	return queue[i];
}
