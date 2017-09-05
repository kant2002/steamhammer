#include "GameRecord.h"

#include "InformationManager.h"
#include "Logger.h"
#include "UnitUtil.h"

using namespace UAlbertaBot;

// Take a digest snapshot of the game situation.
void GameRecord::takeSnapshot()
{
	snapshots.push_back(new GameSnapshot(PlayerSnapshot (BWAPI::Broodwar->self()), PlayerSnapshot (BWAPI::Broodwar->enemy())));
}

char GameRecord::raceChar(BWAPI::Race race)
{
	if (race == BWAPI::Races::Zerg)
	{
		return 'Z';
	}
	if (race == BWAPI::Races::Protoss)
	{
		return 'P';
	}
	if (race == BWAPI::Races::Terran)
	{
		return 'T';
	}
	return 'U';
}

BWAPI::Race GameRecord::charRace(char ch)
{
	if (ch == 'Z')
	{
		return BWAPI::Races::Zerg;
	}
	if (ch == 'P')
	{
		return BWAPI::Races::Protoss;
	}
	if (ch == 'T')
	{
		return BWAPI::Races::Terran;
	}
	return BWAPI::Races::Unknown;
}

// Read a number that is on a line by itself.
int GameRecord::readNumber(std::istream & input)
{
	std::string line;
	int n;

	if (std::getline(input, line))
	{
		n = readNumber(line);
	}
	else
	{
		valid = false;
	}

	if (valid)
	{
		return n;
	}
	else
	{
		skipToEnd(input);
		return -1;
	}
}

// Read a number from a string.
int GameRecord::readNumber(std::string & s)
{
	std::istringstream lineStream(s);
	int n;
	if (lineStream >> n)
	{
		return n;
	}

	valid = false;
	return -1;
}

void GameRecord::parseMatchup(const std::string & s)
{
	if (s.length() == 3)        // "ZvT"
	{
		ourRace = charRace(s[0]);
		enemyRace = charRace(s[2]);
		enemyIsRandom = false;
	}
	else if (s.length() == 4)   // "ZvRT"
	{
		ourRace = charRace(s[0]);
		enemyRace = charRace(s[3]);
		enemyIsRandom = true;            // we don't even check for the R character
	}
	else
	{
		valid = false;
	}

	// Validity check. Did we change our default race?
	if (ourRace == BWAPI::Races::Unknown)
	{
		valid = false;
	}
}

void GameRecord::writePlayerSnapshot(std::ostream & output, const PlayerSnapshot & snap)
{
	output << snap.numBases;
	for (auto unitCount : snap.unitCounts)
	{
		output << ' ' << unitCount.first.getID() << ' ' << unitCount.second;
	}
	output << '\n';
}

void GameRecord::writeGameSnapshot(std::ostream & output, const GameSnapshot * snap)
{
	output << snap->frame << '\n';
	writePlayerSnapshot(output, snap->us);
	writePlayerSnapshot(output, snap->them);
}

// Return true if the snapshot is valid and we should continue reading, otherwise false.
bool GameRecord::readPlayerSnapshot(std::istream & input, PlayerSnapshot & snap)
{
	std::string line;

	if (std::getline(input, line))
	{
		if (line == gameEndMark)
		{
			return false;
		}

		std::istringstream lineStream(line);
		int bases, id, n;
		
		if (lineStream >> bases)
		{
			snap.numBases = bases;
		}
		else
		{
			skipToEnd(input);
			return false;
		}
		while (lineStream >> id >> n)
		{
			snap.unitCounts[BWAPI::UnitType(id)] = n;
		}
		return true;
	}
	skipToEnd(input);
	return false;
}

// Allocate and return the next snapshot, or null if none.
GameSnapshot * GameRecord::readGameSnapshot(std::istream & input)
{
	int t;
	PlayerSnapshot me;
	PlayerSnapshot you;

	std::string line;

	if (std::getline(input, line))
	{
		if (line == gameEndMark)
		{
			return nullptr;
		}
		t = readNumber(line);
	}

	if (valid && readPlayerSnapshot(input, me) && valid && readPlayerSnapshot(input, you) && valid)
	{
		return new GameSnapshot(t, me, you);
	}

	return nullptr;
}

// Reading a game record, we hit an error before the end of the record.
// Mark it invalid and skip to the end of game mark so we don't break the rest of the records.
void GameRecord::skipToEnd(std::istream & input)
{
	valid = false;

	std::string line;
	while (std::getline(input, line))
	{
		if (line == gameEndMark)
		{
			break;
		}
	}
}

// Read the game record from the given stream.
// NOTE Reading is line-oriented. We read each line with getline() before parsing it.
// In case of error, we try to read ahead to the end-of-game mark so that the next record
// will be read correctly. But there is not much error checking.
void GameRecord::read(std::istream & input)
{
	std::string matchupStr;

	if (std::getline(input, matchupStr))
	{
		parseMatchup(matchupStr);
		if (!valid)
		{
			skipToEnd(input);
			return;
		}
	}
	else
	{
		skipToEnd(input);
		return;
	}

	if (!std::getline(input, mapName))     { skipToEnd(input); return; }
	if (!std::getline(input, openingName)) { skipToEnd(input); return; }
	win = readNumber(input) != 0;
	frameEnemyScoutsOurBase = readNumber(input);
	frameEnemyGetsCombatUnits = readNumber(input);
	frameEnemyGetsAirUnits = readNumber(input);
	frameEnemyGetsStaticAntiAir = readNumber(input);
	frameEnemyGetsMobileAntiAir = readNumber(input);
	frameEnemyGetsCloakedUnits = readNumber(input);
	frameEnemyGetsStaticDetection = readNumber(input);
	frameEnemyGetsMobileDetection = readNumber(input);
	frameGameEnds = readNumber(input);

	GameSnapshot * snap;
	while (valid && (snap = readGameSnapshot(input)))
	{
		snapshots.push_back(snap);
	}
}

// Calculate a similarity distance between 2 snapshots.
// This version is a simple first try. Some unit types should matter more than others.
// 12 vs. 10 zerglings should count less than 2 vs. 0 lurkers.
// Buildings and mobile units are hard to compare. Probably should weight by cost in some way.
// Part of distance().
int GameRecord::snapDistance(const PlayerSnapshot & a, const PlayerSnapshot & b) const
{
	int distance = 0;

	// From a to b, count all differences.
	for (std::pair<BWAPI::UnitType, int> unitCountA : a.unitCounts)
	{
		auto unitCountBIt = b.unitCounts.find(unitCountA.first);
		if (unitCountBIt == b.unitCounts.end())
		{
			distance += unitCountA.second;
		}
		else
		{
			distance += abs(unitCountA.second - (*unitCountBIt).second);
		}
	}

	// From b to a, count differences where a is missing the type.
	for (std::pair<BWAPI::UnitType, int> unitCountB : b.unitCounts)
	{
		if (a.unitCounts.find(unitCountB.first) == a.unitCounts.end())
		{
			distance += unitCountB.second;
		}
	}

	return distance;
}

// -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- --

// Constructor for the record of the current game.
GameRecord::GameRecord()
	: valid(true)                  // never invalid, since it is recorded live
	, ourRace(BWAPI::Broodwar->self()->getRace())
	, enemyRace(BWAPI::Broodwar->enemy()->getRace())
	, enemyIsRandom(BWAPI::Broodwar->enemy()->getRace() == BWAPI::Races::Unknown)
	, mapName(BWAPI::Broodwar->mapFileName())
	, openingName(Config::Strategy::StrategyName)
	, win(false)                   // until proven otherwise
	, frameEnemyScoutsOurBase(0)
	, frameEnemyGetsCombatUnits(0)
	, frameEnemyGetsAirUnits(0)
	, frameEnemyGetsStaticAntiAir(0)
	, frameEnemyGetsMobileAntiAir(0)
	, frameEnemyGetsCloakedUnits(0)
	, frameEnemyGetsStaticDetection(0)
	, frameEnemyGetsMobileDetection(0)
	, frameGameEnds(0)
{
}

// Constructor for the record of a past game.
GameRecord::GameRecord(std::istream & input)
	: valid(true)                  // until proven otherwise
	, ourRace(BWAPI::Races::Unknown)
	, enemyRace(BWAPI::Races::Unknown)
	, enemyIsRandom(false)
	, win(false)                   // until proven otherwise
	, frameEnemyScoutsOurBase(0)
	, frameEnemyGetsCombatUnits(0)
	, frameEnemyGetsAirUnits(0)
	, frameEnemyGetsStaticAntiAir(0)
	, frameEnemyGetsMobileAntiAir(0)
	, frameEnemyGetsCloakedUnits(0)
	, frameEnemyGetsStaticDetection(0)
	, frameEnemyGetsMobileDetection(0)
	, frameGameEnds(0)
{
	read(input);
}

// Called when the game is over.
void GameRecord::setWin(bool isWinner)
{
	win = isWinner;
	frameGameEnds = BWAPI::Broodwar->getFrameCount();
}

// Write the game record to the given stream. File format:
// matchup   e.g. ZvP, ZvRP
// map
// opening
// result    1 or 0
// frame enemy first scouts our base
// frame enemy first gets combat units
// frame enemy first gets air units
// frame enemy first gets static anti-air
// frame enemy first gets mobile anti-air
// frame enemy first gets cloaked units
// frame enemy first gets static detection
// frame enemy first gets mobile detection
// game duration in frames
// snapshots
void GameRecord::write(std::ostream & output)
{
	output <<
		raceChar(ourRace) <<
		'v' <<
		(enemyIsRandom ? "R" : "") << raceChar(enemyRace) << '\n';
	output << mapName << '\n';
	output << openingName << '\n';
	output << (win ? '1' : '0') << '\n';
	output << frameEnemyScoutsOurBase << '\n';
	output << frameEnemyGetsCombatUnits << '\n';
	output << frameEnemyGetsAirUnits << '\n';
	output << frameEnemyGetsStaticAntiAir << '\n';
	output << frameEnemyGetsMobileAntiAir << '\n';
	output << frameEnemyGetsCloakedUnits << '\n';
	output << frameEnemyGetsStaticDetection << '\n';
	output << frameEnemyGetsMobileDetection << '\n';
	output << frameGameEnds << '\n';

	for (const auto & snap : snapshots)
	{
		writeGameSnapshot(output, snap);
	}
	output << gameEndMark << '\n';
}

void GameRecord::update()
{
	int now = BWAPI::Broodwar->getFrameCount();

	// Update the when-it-happens frame counters. We don't actually need to check often.
	if (now % 32 == 31)
	{
		if (enemyRace == BWAPI::Races::Unknown)
		{
			enemyRace = BWAPI::Broodwar->enemy()->getRace();
		}
		if (!frameEnemyScoutsOurBase)
		{
			// TODO unimplemented
		}
		if (!frameEnemyGetsCombatUnits && InformationManager::Instance().enemyHasCombatUnits())
		{
			frameEnemyGetsCombatUnits = now;
		}
		if (!frameEnemyGetsAirUnits && InformationManager::Instance().enemyHasAirTech())
		{
			frameEnemyGetsAirUnits = now;
		}
		if (!frameEnemyGetsStaticAntiAir && InformationManager::Instance().enemyHasStaticAntiAir())
		{
			frameEnemyGetsStaticAntiAir = now;
		}
		if (!frameEnemyGetsMobileAntiAir && InformationManager::Instance().enemyHasAntiAir())
		{
			frameEnemyGetsMobileAntiAir = now;
		}
		if (!frameEnemyGetsCloakedUnits && InformationManager::Instance().enemyHasCloakTech())
		{
			frameEnemyGetsCloakedUnits = now;
		}
		if (!frameEnemyGetsStaticDetection && InformationManager::Instance().enemyHasStaticDetection())
		{
			frameEnemyGetsStaticDetection = now;
		}
		if (!frameEnemyGetsMobileDetection && InformationManager::Instance().enemyHasMobileDetection())
		{
			frameEnemyGetsMobileDetection = now;
		}
	}

	// If it's time, take a snapshot.
	int sinceFirst = now - firstSnapshotTime;
	if (sinceFirst >= 0 && sinceFirst % snapshotInterval == 0)
	{
		takeSnapshot();
	}
}

// Calculate a similarity distance between two game records; -1 if they cannot be compared.
// The more similar they are, the less the distance.
int GameRecord::distance(const GameRecord & record) const
{
	// Return -1 if the records are for different matchups.
	if (ourRace != record.ourRace || enemyRace != record.enemyRace)
	{
		return -1;
	}

	// Also return -1 for any record which has no snapshots. It conveys no info.
	if (record.snapshots.size() == 0)
	{
		return -1;
	}

	int distance = 0;

	if (mapName != record.mapName)
	{
		distance += 20;
	}

	if (openingName != record.openingName)
	{
		distance += 200;
	}

	// Differences in enemy play count 5 times more than differences in our play.
	auto here = snapshots.begin();
	auto there = record.snapshots.begin();
	int latest = 0;
	while (here != snapshots.end() && there != record.snapshots.end())     // until one record runs out
	{
		distance +=     snapDistance((*here)->us,   (*there)->us);
		distance += 5 * snapDistance((*here)->them, (*there)->them);
		latest = (*there)->frame;

		++here;
		++there;
	}

	// If the 'there' record ends too early, the comparison is no good after all.
	// The game we're trying to compare to ended before this game and has no information for us.
	if (BWAPI::Broodwar->getFrameCount() - latest > snapshotInterval)
	{
		return -1;
	}

	return distance;
}

// Find the enemy snapshot closest in time to time t.
// The caller promises that there is one, but we check anyway.
bool GameRecord::findClosestSnapshot(int t, PlayerSnapshot & snap) const
{
	for (const auto & ourSnap : snapshots)
	{
		if (abs(ourSnap->frame - t) < snapshotInterval)
		{
			snap = ourSnap->them;
			return true;
		}
	}
	UAB_ASSERT(false, "opponent model - no snapshot @ t");
	return false;
}

void GameRecord::debugLog()
{
	BWAPI::Broodwar->printf("best %s %s", mapName, openingName);

	std::stringstream msg;

	msg << "best match, t = " << BWAPI::Broodwar->getFrameCount() << '\n'
		<< mapName << ' ' << openingName << ' ' << (win ? "win" : "loss") << '\n'
		<< "scout " << frameEnemyScoutsOurBase << '\n'
		<< "combat " << frameEnemyGetsCombatUnits << '\n'
		<< "air " << frameEnemyGetsAirUnits << '\n'
		<< "turrets " << frameEnemyGetsStaticAntiAir << '\n'
		<< "marines " << frameEnemyGetsMobileAntiAir << '\n'
		<< "wraiths " << frameEnemyGetsCloakedUnits << '\n'
		<< "turrets " << frameEnemyGetsStaticDetection << '\n'
		<< "vessels " << frameEnemyGetsMobileDetection << '\n'
		<< "end of game " << frameGameEnds << '\n';

	for (auto snap : snapshots)
	{
		msg << snap->frame << '\n'
			<< snap->us.debugString()
			<< snap->them.debugString();
	}
	msg  << '\n';

	Logger::LogAppendToFile(Config::Debug::ErrorLogFilename, msg.str());
}

GameRecord & GameRecord::Instance()
{
	static GameRecord instance;
	return instance;
}
