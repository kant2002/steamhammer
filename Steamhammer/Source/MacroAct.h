#pragma once

#include "MacroCommand.h"

namespace UAlbertaBot
{
enum class MacroLocation
	{ Anywhere     // default location
	, Main         // macro hatchery or main base building
    , Natural      // "natural" first expansion base
    , Front        // front line base (main or natural, as available)
    , Expo         // mineral and gas expansion
	, MinOnly      // mineral expansion, gas optional
    , GasOnly      // gas expansion, minerals optional
	, Hidden       // mineral and gas expansion far from both sides' main bases
	, Center       // middle of the map
    , Proxy        // hidden in or in range of the enemy main or natural
    , EnemyMain    // in plain sight
    , EnemyNatural // in plain sight
    , GasSteal     // this is a gas steal, a unit type must be a refinery type
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

	MacroLocation		_macroLocation;

	MacroLocation		getMacroLocationFromString(const std::string & s);

public:

	MacroAct();
    MacroAct(const std::string & name);
	MacroAct(BWAPI::UnitType t);
	MacroAct(BWAPI::UnitType t, MacroLocation loc);
	MacroAct(BWAPI::TechType t);
	MacroAct(BWAPI::UpgradeType t);
	MacroAct(MacroCommandType t);
	MacroAct(MacroCommandType t, int amount);

	bool    isUnit()			const;
	bool	isWorker()			const;
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