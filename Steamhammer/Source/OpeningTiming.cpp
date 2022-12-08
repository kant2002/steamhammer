#include <fstream>

#include "OpeningTiming.h"

#include "The.h"

using namespace UAlbertaBot;

// -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- --

OpeningTiming::OpeningTiming()
{
}

// Read past game records from the opponent model file, and do initial analysis.
// TODO only a stub
void OpeningTiming::read()
{
    std::ifstream inFile;

    inFile.open(Config::IO::StaticDir + Config::IO::OpeningTimingFile);

    // There may not be a file to read.
    if (!inFile.good())
    {
        return;
    }
    // At this point, we have a file to read.

    while (inFile.good())
    {
        // TODO not yet implemented
    }

    inFile.close();
}

OpeningTiming & OpeningTiming::Instance()
{
    static OpeningTiming instance;
    return instance;
}
