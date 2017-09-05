#pragma once

#include "Common.h"
#include "PlayerSnapshot.h"

// NOTE
// This class does little checking of its input file format. Feed it no bad files.

namespace UAlbertaBot
{

struct GameSnapshot
{
	const int frame;
	const PlayerSnapshot us;
	const PlayerSnapshot them;

	// For reading a snapshot from a file.
	GameSnapshot(int t, const PlayerSnapshot & me, const PlayerSnapshot & you)
		: frame(t)
		, us(me)
		, them(you)
	{
	}

	// For taking a snapshot during the game.
	GameSnapshot(const PlayerSnapshot & me, const PlayerSnapshot & you)
		: frame(BWAPI::Broodwar->getFrameCount())
		, us(me)
		, them(you)
	{
	}
};

class GameRecord
{
	const int firstSnapshotTime = 2 * 60 * 24;
	const int snapshotInterval = 30 * 24;

	const std::string gameEndMark = "END GAME";

	// Is this a valid record, or broken in reading?
	bool valid;

	// About the game.
	BWAPI::Race ourRace;
	BWAPI::Race enemyRace;
	bool enemyIsRandom;
	//
	bool win;
	int frameEnemyScoutsOurBase;           // TODO written but not updated
	int frameEnemyGetsCombatUnits;
	int frameEnemyGetsAirUnits;
	int frameEnemyGetsStaticAntiAir;
	int frameEnemyGetsMobileAntiAir;
	int frameEnemyGetsCloakedUnits;
	int frameEnemyGetsStaticDetection;     // includes spider mines
	int frameEnemyGetsMobileDetection;
	int frameGameEnds;

	// We allocate the snapshots and never release them.
	std::vector<GameSnapshot *> snapshots;

	void takeSnapshot();

	char raceChar(BWAPI::Race race);
	BWAPI::Race charRace(char ch);

	int readNumber(std::istream & input);
	int readNumber(std::string & s);

	void parseMatchup(const std::string & s);

	void writePlayerSnapshot(std::ostream & output, const PlayerSnapshot & snap);
	void writeGameSnapshot(std::ostream & output, const GameSnapshot * snap);

	bool readPlayerSnapshot(std::istream & input, PlayerSnapshot & snap);
	GameSnapshot * readGameSnapshot(std::istream & input);
	void skipToEnd(std::istream & input);
	void read(std::istream & input);

	int snapDistance(const PlayerSnapshot & a, const PlayerSnapshot & b) const;

public:
	std::string mapName;
	std::string openingName;

	GameRecord();
	GameRecord(std::istream & input);

	bool isValid() { return valid; };
	void setWin(bool isWinner);

	void write(std::ostream & output);

	void update();

	int distance(const GameRecord & record) const;    // similarity distance

	bool findClosestSnapshot(int t, PlayerSnapshot & snap) const;

	void debugLog();
	static GameRecord & Instance();
};

}