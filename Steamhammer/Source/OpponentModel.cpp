#include "OpponentModel.h"

using namespace UAlbertaBot;

// Find the past game record which best matches the current game and remember it.
void OpponentModel::setBestMatch()
{
	int bestScore = -1;
	GameRecord * bestRecord = nullptr;

	for (GameRecord * record : _pastGameRecords)
	{
		int score = _gameRecord.distance(*record);
		if (score != -1 && (!bestRecord || score < bestScore))
		{
			bestScore = score;
			bestRecord = record;
		}
	}

	_bestMatch = bestRecord;
}

// -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- --

OpponentModel::OpponentModel()
	: _bestMatch(nullptr)
{
	std::string name = BWAPI::Broodwar->enemy()->getName();

	// Replace characters that the filesystem may not like with '_'.
	// TODO Obviously not a thorough job.
	std::replace(name.begin(), name.end(), ' ', '_');

	_filename = "om_" + name + ".txt";
}

// Read past game records from the opponent model file.
void OpponentModel::read()
{
	if (Config::IO::UseOpponentModel)
	{
		std::ifstream inFile(Config::IO::ReadDir + _filename);

		// There may not be a file to read. That's OK.
		if (inFile.bad())
		{
			return;
		}

		while (inFile.good())
		{
			// NOTE We allocate records here and never free them if valid.
			//      Their lifetime is the whole game.
			GameRecord * record = new GameRecord(inFile);
			if (record->isValid())
			{
				_pastGameRecords.push_back(record);
			}
			else
			{
				delete record;
			}
		}

		inFile.close();
	}
}

// Write the current game record to the opponent model file.
void OpponentModel::write()
{
	if (Config::IO::UseOpponentModel)
	{
		std::ofstream outFile(Config::IO::WriteDir + _filename, std::ios::app);

		// If it fails, there's not much we can do about it.
		if (outFile.bad())
		{
			return;
		}

		_gameRecord.write(outFile);

		// This was used for testing.
		//for (GameRecord * record : _pastGameRecords)
		//{
		//	record->write(outFile);
		//}

		outFile.close();
	}
}

void OpponentModel::update()
{
	if (Config::IO::UseOpponentModel)
	{
		_gameRecord.update();

		if (BWAPI::Broodwar->getFrameCount() % 32 == 31)
		{
			setBestMatch();
		}

		if (_bestMatch)
		{
			//_bestMatch->debugLog();
			//BWAPI::Broodwar->drawTextScreen(200, 10, "%cmatch %s %s", white, _bestMatch->mapName, _bestMatch->openingName);
			BWAPI::Broodwar->drawTextScreen(220, 6, "%cmatch", white);
		}
		else
		{
			BWAPI::Broodwar->drawTextScreen(220, 6, "%cno best match", white);
		}
	}
}

// Fill in the snapshot with a prediction of what the opponent may have at a given time.
void OpponentModel::predictEnemy(int lookaheadFrames, PlayerSnapshot & snap) const
{
	const int t = BWAPI::Broodwar->getFrameCount() + lookaheadFrames;

	// Use the best-match past game record if possible.
	// Otherwise, take a current snapshot and call it the prediction.
	if (_bestMatch && _bestMatch->findClosestSnapshot(t, snap))
	{
		// All done.
	}
	else
	{
		snap.takeEnemy();
	}
}

OpponentModel & OpponentModel::Instance()
{
	static OpponentModel instance;
	return instance;
}
