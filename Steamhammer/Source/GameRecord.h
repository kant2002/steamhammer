#pragma once

#include "Common.h"
#include "OpponentPlan.h"
#include "PlayerSnapshot.h"
#include "Skill.h"

#include <exception>

// NOTE
// This class does only a little checking of its input file format. Feed it no bad files.

class game_record_read_error : public std::exception {};

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
protected:
	const int firstSnapshotTime = 2 * 60 * 24;
	const int snapshotInterval = 30 * 24;

    const std::string latestRecordFormat = "3.0";
	const std::string gameEndMark = "END GAME";

    // Each game record is labeled with a version number, to allow backward compatibility
    // when the file format changes.
    // Plan: Set the version number to the last Steamhammer release which changed the file format.
    std::string recordFormat;

    // Is this a valid record, or broken in reading?
	bool valid;

	// Is this the record of the current game, or a saved record?
	bool savedRecord;

	// About the game.
	BWAPI::Race ourRace;
	BWAPI::Race enemyRace;
	bool enemyIsRandom;

	std::string mapName;
	std::string openingName;
    int myStartingBaseID;
    int enemyStartingBaseID;
	OpeningPlan expectedEnemyPlan;
	OpeningPlan enemyPlan;
	bool win;

    // Gas steal data only for v 1.4 game records.
	int frameScoutSentForGasSteal;         // 0 if we didn't try to steal gas
	bool gasStealHappened;                 // true if a refinery building was started

    int frameWeMadeFirstCombatUnit;
    int frameWeGatheredGas;

	int frameEnemyScoutsOurBase;
	int frameEnemyGetsCombatUnits;
    int frameEnemyUsesGas;
	int frameEnemyGetsAirUnits;
	int frameEnemyGetsStaticAntiAir;
	int frameEnemyGetsMobileAntiAir;
	int frameEnemyGetsCloakedUnits;
	int frameEnemyGetsStaticDetection;     // includes spider mines
	int frameEnemyGetsMobileDetection;
	int frameGameEnds;

    // Record skill kit data as raw text so that individual skills don't have to remember it.
    std::vector <std::string> skillKitText;

    // A skill can also store data here for analysis.
    std::map< Skill *, std::map< int, std::vector<int> > > skillData;

	// We allocate the snapshots and never release them.
	std::vector<GameSnapshot *> snapshots;

	BWAPI::Race charRace(char ch);

	int readNumber(std::istream & input);
	int readNumber(std::string & s);

	void parseMatchup(const std::string & s);

	OpeningPlan readOpeningPlan(std::istream & input);

	bool readPlayerSnapshot(std::istream & input, PlayerSnapshot & snap);
	GameSnapshot * readGameSnapshot(std::istream & input);
	void skipToEnd(std::istream & input);

    void read_v3_0(std::istream & input);
    void read_v1_4(std::istream & input);
	void read(std::istream & input);

    void writePlayerSnapshot(std::ostream & output, const PlayerSnapshot & snap);
    void writeGameSnapshot(std::ostream & output, const GameSnapshot * snap);
    virtual void writeSkills(std::ostream & output) const;

    int snapDistance(const PlayerSnapshot & a, const PlayerSnapshot & b) const;

public:
	GameRecord();
	GameRecord(std::istream & input);

    void write(std::ostream & output);

	bool isValid() { return valid; };

	int distance(const GameRecord & record) const;    // similarity distance UNUSED

	bool findClosestSnapshot(int t, PlayerSnapshot & snap) const;

    bool sameMatchup(const GameRecord & record) const;

    bool getEnemyIsRandom() const { return enemyIsRandom; };
	const std::string & getMapName() const { return mapName; };
	const std::string & getOpeningName() const { return openingName; };
	OpeningPlan getExpectedEnemyPlan() const { return expectedEnemyPlan; };
	OpeningPlan getEnemyPlan() const { return enemyPlan; };
	bool getWin() const { return win; };
	int getFrameScoutSentForGasSteal() const { return frameScoutSentForGasSteal; };
	bool getGasStealHappened() const { return gasStealHappened; };
    int getFrameEnemyUsesGas() const { return frameEnemyUsesGas; };

    const std::vector<int> * getSkillInfo(Skill * skill, int i) const;
    void setSkillInfo(Skill * skill, int i, const std::vector<int> & info);

	void debugLog();
};

}