#pragma once

#include <BWAPI.h>

namespace UAlbertaBot
{
    class ResourceInfo
    {
        BWAPI::Unit initialUnit;
        BWAPI::Unit currentGasUnit;     // unused for mineral patches
        int amount;
        int updateFrame;
        bool destroyed;                 // a mineral patch that is mined out

        bool isMineralVisible() const;
        bool isGasVisible() const;
        BWAPI::Unit accessibleGasUnit();
        void update(int a);

    public:
        ResourceInfo();
        ResourceInfo(BWAPI::Unit u);

        void updateMinerals();
        void updateGas();

        bool isAccessible()   const;
        bool isMineralPatch() const { return initialUnit->getInitialType().isMineralField(); };
        int getAmount()       const { return amount; };
        int getFrame()        const { return updateFrame; };
        BWAPI::Unit getUnit() const { return initialUnit; };
        bool isDestroyed()    const { return destroyed; };      // is mineral patch mined out?
    };
};
