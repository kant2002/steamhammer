#include "The.h"

#include "Bases.h"
#include "InformationManager.h"
#include "Random.h"

using namespace UAlbertaBot;

The::The()
    : bases(Bases::Instance())
    , info(InformationManager::Instance())
    , random(Random::Instance())
{
}

// -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- --

// NOTE The skill kit is initialized in UAlbertaBotModule and updated in GameCommander.

void The::initialize()
{
    _selfRace = BWAPI::Broodwar->self()->getRace();

	// The order of initialization is important because of dependencies.
	partitions.initialize();
	inset.initialize();				// depends on partitions
	vWalkRoom.initialize();			// depends on edgeRange
	tileRoom.initialize();			// depends on vWalkRoom
	zone.initialize();				// depends on tileRoom
    map.initialize();

    bases.initialize();             // depends on map
    info.initialize();              // depends on bases
    placer.initialize();
	ops.initialize();
}

void The::update()
{
    my.completed.takeSelf();
    my.all.takeSelfAll();
    your.seen.takeEnemy();
    your.ever.takeEnemyEver(your.seen);
    your.inferred.takeEnemyInferred(your.ever);

	if (now() > 45 * 24 && now() % 10 == 0)
	{
		groundAttacks.update();
		airAttacks.update();
	}
}
