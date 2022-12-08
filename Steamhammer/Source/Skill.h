#pragma once

#include <string>
#include <iostream>

namespace UAlbertaBot
{
class GameRecord;

class Skill
{
protected:

    std::string _name;
    int _nextUpdateFrame;

public:

    Skill(const std::string & name);

    const std::string & getName() const { return _name; };

    // To save this game's data in the current game record, override this.
    // Any return other than the empty string will be recorded.
    virtual std::string putData() const { return ""; };

    // To read data back from the game records, override this.
    // It is called once for each record that has data for the skill.
    virtual void getData(GameRecord & record, const std::string & line) {};

    // Called once at startup to decide whether to run the skill at all.
    // Available are the map, config settings, and the opponent's race.
    // Opponent model information has not been read in yet.
    virtual bool enabled() const = 0;

    // Update any info that feasible(), good(), execute() may want to look at.
    // Also possibly take actions. Not all skills use execute().
    virtual void update() = 0;

    // Fast check for whether to execute the skill.
    // Called once per update.
    virtual bool feasible() const = 0;

    // Careful, slower check for whether to execute the skill.
    // Called once per update when feasible() is true.
    virtual bool good() const = 0;

    // Execute the skill, for those skills that make execute/don't decisions.
    // Called once per update when feasible() and good() are true.
    virtual void execute() = 0;

    // Skills that want to draw debugging info should override this.
    // It will be called once per frame.
    virtual void draw() const {};

    int nextUpdate() const { return _nextUpdateFrame; };

    void read(GameRecord & r, std::istream & instream);
    void write(std::ostream & outstream) const;
};

}