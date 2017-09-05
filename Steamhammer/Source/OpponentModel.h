#pragma once

#include "Common.h"
#include "GameRecord.h"

namespace UAlbertaBot
{

	class OpponentModel
	{
	private:

		std::string _filename;
		GameRecord _gameRecord;
		std::vector<GameRecord *> _pastGameRecords;

		GameRecord * _bestMatch;

		void setBestMatch();

	public:
		OpponentModel();

		void setWin(bool isWinner) { _gameRecord.setWin(isWinner); };

		void read();
		void write();

		void update();

		void predictEnemy(int lookaheadFrames, PlayerSnapshot & snap) const;

		static OpponentModel & Instance();
	};

}