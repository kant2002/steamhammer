#pragma once

namespace UAlbertaBot
{
    class OpeningTiming
    {
    private:

    public:
        OpeningTiming();

        void read();

        static OpeningTiming & Instance();
    };

}