#include "Skill.h"

using namespace UAlbertaBot;

Skill::Skill(const std::string & name)
    : _name(name)
    , _nextUpdateFrame(1)
{
}

// -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- --

void Skill::read(GameRecord & r, std::istream & instream)
{
    std::string line;
    if (std::getline(instream, line))
    {
        getData(r, line);
    }
}

void Skill::write(std::ostream & outstream) const
{
    outstream << putData() << '\n';
}
