#pragma once

#include "MacroCommand.h"

namespace UAlbertaBot
{
enum class MacroLocation
    { Anywhere     // default location
    , Main         // macro hatchery or main base building
    , Natural      // "natural" first expansion base
    , Front        // front line base (main or natural, as available)
    , Macro        // where production is needed
    , Expo         // mineral and gas expansion
    , MinOnly      // mineral expansion, gas optional
    , GasOnly      // gas expansion, minerals optional
    , Hidden       // mineral and gas expansion far from both sides' main bases
    , Center       // middle of the map
    , Proxy        // hidden in or in range of the enemy main or natural
    , EnemyMain    // in plain sight
    , EnemyNatural // in plain sight
    , GasSteal     // this is a gas steal, a unit type must be a refinery type
    , Tile         // on or near a specific tile
    };

namespace MacroActs
{
    enum {Unit, Tech, Upgrade, Command, Default};
}

class MacroAct 
{
    size_t				_type;

    BWAPI::UnitType		_unitType;
    BWAPI::TechType		_techType;
    BWAPI::UpgradeType	_upgradeType;
    MacroCommand		_macroCommandType;
    BWAPI::Unit         _parent;

    MacroLocation		_macroLocation;
    BWAPI::TilePosition _tileLocation;      // only set when macroLocation is Tile

    void                initializeUnitTypesByName();
    MacroLocation		getMacroLocationFromString(const std::string & s) const;
    BWAPI::UnitType     getUnitTypeFromString(const std::string & s) const;

public:

    MacroAct();
    MacroAct(const std::string & name);
    MacroAct(BWAPI::UnitType t);
    MacroAct(BWAPI::UnitType t, MacroLocation loc);
    MacroAct(BWAPI::UnitType t, const BWAPI::TilePosition & tile);
    MacroAct(BWAPI::UnitType t, BWAPI::Unit parent);
    MacroAct(BWAPI::TechType t);
    MacroAct(BWAPI::UpgradeType t);
    MacroAct(MacroCommandType t);
    MacroAct(MacroCommandType t, int amount);
    MacroAct(MacroCommandType t, BWAPI::UnitType type);

    bool    isUnit()			const;
    bool	isWorker()			const;
    bool    isCombatUnit()      const;
    bool    isTech()			const;
    bool    isUpgrade()			const;
    bool    isCommand()			const;
    bool    isBuilding()		const;
    bool	isAddon()			const;
    bool	isMorphedBuilding()	const;
    bool    isRefinery()		const;
    bool	isSupply()			const;
    bool    isGasSteal()        const;
    
    size_t type() const;

    BWAPI::UnitType getUnitType() const;
    BWAPI::TechType getTechType() const;
    BWAPI::UpgradeType getUpgradeType() const;
    MacroCommand getCommandType() const;
    MacroLocation getMacroLocation() const;
    BWAPI::TilePosition getTileLocation() const;

    int supplyRequired() const;
    int mineralPrice()   const;
    int gasPrice()       const;

    BWAPI::UnitType whatBuilds() const;
    std::string getName() const;

    bool isProducer(BWAPI::Unit unit) const;
    void getCandidateProducers(std::vector<BWAPI::Unit> & candidates) const;
    bool hasEventualProducer() const;
    bool hasPotentialProducer() const;
    bool hasTech() const;

    bool canProduce(BWAPI::Unit producer) const;
    void produce(BWAPI::Unit producer) const;
};
}