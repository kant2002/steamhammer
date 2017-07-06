#pragma once

#include "Common.h"

namespace UAlbertaBot
{

enum class MacroCommandType
	{ None
	, Scout
	, ScoutIfNeeded
	, ScoutLocation
	, StartGas
	, StopGas
	, GasUntil
	, StealGas
	, ExtractorTrick    // NOTE implemented, but tickles a bug in BWAPI; don't use it
	, Aggressive
	, Defensive
	};

class MacroCommand
{
	MacroCommandType	_type;
    int                 _amount;

public:

	static const std::list<MacroCommandType> allCommandTypes()
	{
		return std::list<MacroCommandType>
			{ MacroCommandType::Scout
			, MacroCommandType::ScoutIfNeeded
			, MacroCommandType::ScoutLocation
			, MacroCommandType::StartGas
			, MacroCommandType::StopGas
			, MacroCommandType::GasUntil
			, MacroCommandType::StealGas
			, MacroCommandType::ExtractorTrick
			, MacroCommandType::Aggressive
			, MacroCommandType::Defensive
		};
	}

	// Default constructor for when the value doesn't matter.
	MacroCommand()
		: _type(MacroCommandType::None)
		, _amount(0)
	{
	}

	MacroCommand(MacroCommandType type)
		: _type(type)
        , _amount(0)
	{
		UAB_ASSERT(!hasArgument(type),"missing MacroCommand argument");
	}

	MacroCommand(MacroCommandType type, int amount)
		: _type(type)
		, _amount(amount)
	{
		UAB_ASSERT(hasArgument(type), "extra MacroCommand argument");
	}

    const int getAmount() const
    {
        return _amount;
    }

	const MacroCommandType & getType() const
    {
        return _type;
    }

	// Only GasUntil has an argument, the amount of gas to gather.
	static const bool hasArgument(MacroCommandType t)
	{
		return t == MacroCommandType::GasUntil;
	}

	static const std::string getName(MacroCommandType t)
	{
		if (t == MacroCommandType::Scout)
		{
			return "go scout";
		}
		if (t == MacroCommandType::ScoutIfNeeded)
		{
			return "go scout if needed";
		}
		if (t == MacroCommandType::ScoutLocation)
		{
			return "go scout location";
		}
		if (t == MacroCommandType::StartGas)
		{
			return "go start gas";
		}
		if (t == MacroCommandType::StopGas)
		{
			return "go stop gas";
		}
		if (t == MacroCommandType::GasUntil)
		{
			return "go gas until";
		}
		if (t == MacroCommandType::StealGas)
		{
			return "go steal gas";
		}
		if (t == MacroCommandType::ExtractorTrick)
		{
			return "go extractor trick";
		}
		if (t == MacroCommandType::Aggressive)
		{
			return "go aggressive";
		}
		if (t == MacroCommandType::Defensive)
		{
			return "go defensive";
		}

		UAB_ASSERT(t == MacroCommandType::None, "unrecognized MacroCommandType");
		return "go none";
	}

	const std::string getName() const
	{
		if (hasArgument(_type))
		{
			// Include the amount.
			std::stringstream name;
			name << getName(_type) << " " << _amount;
			return name.str();
		}
		else {
			return getName(_type);
		}
	}

};
}