#pragma once

#include <BWAPI.h>

#include "Skill.h"

#include "OpponentModel.h"

namespace UAlbertaBot
{
struct GasStealRecord
{
    GasStealRecord()
        : executeFrame(0)
        , successFrame(0)
        , lifetime(0)
    {
    };

    GasStealRecord(const std::vector<int> * data)
    {
        UAB_ASSERT(data->size() == 3, "bad data");

        executeFrame = (*data)[0];
        successFrame = (*data)[1];
        lifetime = (*data)[2];
    };

    int executeFrame;
    int successFrame;
    int lifetime;
};

class SkillGasSteal : public Skill
{
private:
    GasStealRecord record;

    bool _initialized;
    bool _failed;

    OpponentModel::OpeningInfoType tryInfo;
    OpponentModel::OpeningInfoType successInfo;
    int successLifetime;        // mean lifetime (destruction frame) of refinery when made
    int stealGasTiming;         // mean enemy gas timing after successful steal
    int otherGasTiming;         // mean enemy gas timing in other games

    void analyzeRecords();

    bool useless() const;

public:
    SkillGasSteal();

    std::string putData() const;
    void getData(GameRecord & record, const std::string & line);

    bool enabled() const;
    void initialize();
    void update();
    bool feasible() const;
    bool good() const;
    void execute();
};

}