#include <BWAPI.h>
#include "Random.h"

// Simple random number utility class.
// It keeps the state and makes random numbers on demand.

using namespace UAlbertaBot;

Random::Random()
{
	std::random_device seed;
	_rng = std::minstd_rand(seed());
}

// Random number in the range [0,n-1], such as an array index.
int Random::index(int n)
{
	std::uniform_int_distribution<int> uniform_dist(0, n-1);
	return uniform_dist(_rng);
}

Random & Random::Instance()
{
	static Random instance;
	return instance;
}
