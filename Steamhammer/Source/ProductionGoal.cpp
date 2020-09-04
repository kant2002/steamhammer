#include "ProductionGoal.h"

#include "BuildingManager.h"
#include "The.h"

using namespace UAlbertaBot;

// An upgrade or research goal may already be done.
bool ProductionGoal::alreadyAchieved() const
{
    if (act.isTech())
    {
        return
            BWAPI::Broodwar->self()->hasResearched(act.getTechType()) ||
            BWAPI::Broodwar->self()->isResearching(act.getTechType());
    }

    if (act.isUpgrade())
    {
        int level = BWAPI::Broodwar->self()->getUpgradeLevel(act.getUpgradeType());
        int max = BWAPI::Broodwar->self()->getMaxUpgradeLevel(act.getUpgradeType());
        return
            level >= max ||
            level == max - 1 && BWAPI::Broodwar->self()->isUpgrading(act.getUpgradeType());
    }

    return false;
}

bool ProductionGoal::failure() const
{
	// The goal fails if no possible unit could become its parent,
	// including buildings not yet started by BuildingManager.

    return
        alreadyAchieved() ||
        !act.hasEventualProducer() && !BuildingManager::Instance().isBeingBuilt(act.whatBuilds());
}

ProductionGoal::ProductionGoal(const MacroAct & macroAct)
	: parent(nullptr)
	, attempted(false)
	, act(macroAct)
{
	UAB_ASSERT(act.isAddon() || act.isUpgrade() || act.isTech(), "unsupported goal");
    //BWAPI::Broodwar->printf("create goal %s", act.getName().c_str());
}

// Meant to be called once per frame to try to carry out the goal.
void ProductionGoal::update()
{
	// Clear any former parent that is lost.
	if (parent && !parent->exists())
	{
		parent = nullptr;
		attempted = false;
	}

	// Add a parent if necessary and possible.
	if (!parent)
	{
		std::vector<BWAPI::Unit> producers;
		act.getCandidateProducers(producers);
		if (!producers.empty())
		{
			parent = *producers.begin();		// we don't care which one
            //BWAPI::Broodwar->printf("%s has parent %s of %d",
            //    act.getName().c_str(), UnitTypeName(parent).c_str(), producers.size());
		}
	}

	// Achieve the goal if possible.
    if (parent && act.canProduce(parent))
    {
        //BWAPI::Broodwar->printf("attempt goal %s", act.getName().c_str());
        attempted = true;
        act.produce(parent);
    }
}

// Meant to be called once per frame to see if the goal is completed and can be dropped.
bool ProductionGoal::done()
{
    // TODO this check is approximate
	bool done = parent && (parent->getAddon() || parent->isUpgrading() || parent->isResearching());

	if (done)
	{
		if (attempted)
		{
			//BWAPI::Broodwar->printf("completed goal %s", act.getName().c_str());
		}
		else
		{
			// The goal was "completed" but not attempted. That means it hit a race condition:
			// It took the same parent as another goal which was attempted and succeeded.
			// The goal went bad and has to be retried with a new parent.
			//BWAPI::Broodwar->printf("retry goal %s", act.getName().c_str());
			parent = nullptr;
			done = false;
		}
	}
	// No need to check every frame whether the goal has failed.
	else if (BWAPI::Broodwar->getFrameCount() % 32 == 22 && failure())
	{
		//BWAPI::Broodwar->printf("failed goal %s", act.getName().c_str());
		done = true;
	}

	return done;
}
