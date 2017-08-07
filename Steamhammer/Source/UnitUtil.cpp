#include "UnitUtil.h"

using namespace UAlbertaBot;

// Building morphed from another, not constructed.
bool UnitUtil::IsMorphedBuildingType(BWAPI::UnitType type)
{
	return
		type == BWAPI::UnitTypes::Zerg_Sunken_Colony ||
		type == BWAPI::UnitTypes::Zerg_Spore_Colony ||
		type == BWAPI::UnitTypes::Zerg_Lair ||
		type == BWAPI::UnitTypes::Zerg_Hive ||
		type == BWAPI::UnitTypes::Zerg_Greater_Spire;
}

// This is a combat unit for purposes of combat simulation.
// The combat simulation does not support spellcasters other than medics.
// It does not support bunkers, reavers, or carriers--but we fake bunkers.
bool UnitUtil::IsCombatSimUnit(BWAPI::UnitType type)
{
	if (type.isWorker())
	{
		return false;
	}

	return
		type.canAttack() ||            // includes static defense except bunkers, excludes spellcasters
		type == BWAPI::UnitTypes::Terran_Medic ||
		type.isDetector();
}

bool UnitUtil::IsCombatUnit(BWAPI::UnitType type)
{
    // No workers, buildings, or carrier interceptors (which are not controllable).
	// Buildings include static defense buildings; they are not put into squads.
	if (type.isWorker() ||
		type.isBuilding() ||
		type == BWAPI::UnitTypes::Protoss_Interceptor)  // apparently, they canAttack()
    {
        return false;
    }

	if (type.canAttack() ||                             // includes carriers and reavers
		type.isDetector() ||
		type == BWAPI::UnitTypes::Terran_Medic ||
		type == BWAPI::UnitTypes::Protoss_High_Templar ||
		type.isFlyer() && type.spaceProvided() > 0)     // transports
    {
        return true;
	}

	return false;
}

bool UnitUtil::IsCombatUnit(BWAPI::Unit unit)
{
	UAB_ASSERT(unit != nullptr, "Unit was null");
	if (!unit)
	{
		return false;
	}

	return IsCombatUnit(unit->getType());
}

bool UnitUtil::IsValidUnit(BWAPI::Unit unit)
{
	return unit
		&& unit->exists()
		&& (unit->isCompleted() || IsMorphedBuildingType(unit->getType()))
		&& unit->getHitPoints() > 0
		&& unit->getType() != BWAPI::UnitTypes::Unknown
		&& unit->getPosition().isValid();
}

bool UnitUtil::CanAttack(BWAPI::Unit attacker, BWAPI::Unit target)
{
	return target->isFlying() ? TypeCanAttackAir(attacker->getType()) : TypeCanAttackGround(attacker->getType());
}

// Accounts for cases where units can attack without a weapon of their own.
// Ignores spellcasters, which have limitations on their attacks.
// For example, high templar can attack air or ground mobile units, but can't attack buildings.
bool UnitUtil::CanAttack(BWAPI::UnitType attacker, BWAPI::UnitType target)
{
	return target.isFlyer() ? TypeCanAttackAir(attacker) : TypeCanAttackGround(attacker);
}

bool UnitUtil::CanAttackAir(BWAPI::Unit attacker)
{
	return TypeCanAttackAir(attacker->getType());
}

// Assume that a bunker is loaded and can shoot at air.
bool UnitUtil::TypeCanAttackAir(BWAPI::UnitType attacker)
{
	return attacker.airWeapon() != BWAPI::WeaponTypes::None ||
		attacker == BWAPI::UnitTypes::Terran_Bunker ||
		attacker == BWAPI::UnitTypes::Protoss_Carrier;
}

// NOTE surrenderMonkey() checks CanAttackGround() to see whether the enemy can destroy buildings.
//      Adding spellcasters to it would break that.
//      If you do that, make CanAttackBuildings() and have surrenderMonkey() call that.
bool UnitUtil::CanAttackGround(BWAPI::Unit attacker)
{
	return TypeCanAttackGround(attacker->getType());
}

// Assume that a bunker is loaded and can shoot at ground.
bool UnitUtil::TypeCanAttackGround(BWAPI::UnitType attacker)
{
	return attacker.groundWeapon() != BWAPI::WeaponTypes::None ||
		attacker == BWAPI::UnitTypes::Terran_Bunker ||
		attacker == BWAPI::UnitTypes::Protoss_Carrier ||
		attacker == BWAPI::UnitTypes::Protoss_Reaver;
}

// NOTE Unused but potentially useful.
double UnitUtil::CalculateLTD(BWAPI::Unit attacker, BWAPI::Unit target)
{
	BWAPI::WeaponType weapon = GetWeapon(attacker, target);

	if (weapon == BWAPI::WeaponTypes::None || weapon.damageCooldown() <= 0)
	{
		return 0;
	}

	return double(weapon.damageAmount()) / weapon.damageCooldown();
}

// There is no way to handle bunkers here. You have to know what's in it.
BWAPI::WeaponType UnitUtil::GetWeapon(BWAPI::Unit attacker, BWAPI::Unit target)
{
	return GetWeapon(attacker->getType(), target);
}

// Handle carriers and reavers correctly in the case of floating buildings.
// We have to check unit->isFlying() because unitType->isFlyer() is not correct
// for a lifted terran building.
// There is no way to handle bunkers here. You have to know what's in it.
BWAPI::WeaponType UnitUtil::GetWeapon(BWAPI::UnitType attacker, BWAPI::Unit target)
{
	if (attacker == BWAPI::UnitTypes::Protoss_Carrier)
	{
		return GetWeapon(BWAPI::UnitTypes::Protoss_Interceptor, target);
	}
	if (attacker == BWAPI::UnitTypes::Protoss_Reaver)
	{
		return GetWeapon(BWAPI::UnitTypes::Protoss_Scarab, target);
	}
	return target->isFlying() ? attacker.airWeapon() : attacker.groundWeapon();
}

// There is no way to handle bunkers here. You have to know what's in it.
BWAPI::WeaponType UnitUtil::GetWeapon(BWAPI::UnitType attacker, BWAPI::UnitType target)
{
	if (attacker == BWAPI::UnitTypes::Protoss_Carrier)
	{
		return GetWeapon(BWAPI::UnitTypes::Protoss_Interceptor, target);
	}
	if (attacker == BWAPI::UnitTypes::Protoss_Reaver)
	{
		return GetWeapon(BWAPI::UnitTypes::Protoss_Scarab, target);
	}
	return target.isFlyer() ? attacker.airWeapon() : attacker.groundWeapon();
}

// Tries to take possible range upgrades into account, making pessimistic assumptions about the enemy.
// Returns 0 if the attacker does not have a way to attack the target.
// NOTE Does not check whether our reaver, carrier, or bunker has units inside that can attack.
int UnitUtil::GetAttackRange(BWAPI::Unit attacker, BWAPI::Unit target)
{
	// Reavers, carriers, and bunkers have "no weapon" but still have an attack range.
	if (attacker->getType() == BWAPI::UnitTypes::Protoss_Reaver && !target->isFlying())
	{
		return 8;
	}
	if (attacker->getType() == BWAPI::UnitTypes::Protoss_Carrier)
	{
		return 8;
	}
	if (attacker->getType() == BWAPI::UnitTypes::Terran_Bunker)
	{
		if (attacker->getPlayer() == BWAPI::Broodwar->enemy() ||
			BWAPI::Broodwar->self()->getUpgradeLevel(BWAPI::UpgradeTypes::U_238_Shells))
		{
			return 6 * 32;
		}
		return 5 * 32;
	}

	const BWAPI::WeaponType weapon = GetWeapon(attacker, target);

	if (weapon == BWAPI::WeaponTypes::None)
	{
		return 0;
	}

	int range = weapon.maxRange();

	// Count range upgrades,
	// for ourselves if we have researched it,
	// for the enemy always (by pessimistic assumption).
	if (attacker->getType() == BWAPI::UnitTypes::Protoss_Dragoon)
	{
		if (attacker->getPlayer() == BWAPI::Broodwar->enemy() ||
			BWAPI::Broodwar->self()->getUpgradeLevel(BWAPI::UpgradeTypes::Singularity_Charge))
		{
			range = 6 * 32;
		}
	}
	else if (attacker->getType() == BWAPI::UnitTypes::Terran_Marine)
	{
		if (attacker->getPlayer() == BWAPI::Broodwar->enemy() ||
			BWAPI::Broodwar->self()->getUpgradeLevel(BWAPI::UpgradeTypes::U_238_Shells))
		{
			range = 5 * 32;
		}
	}
	else if (attacker->getType() == BWAPI::UnitTypes::Terran_Goliath && target->isFlying())
	{
		if (attacker->getPlayer() == BWAPI::Broodwar->enemy() ||
			BWAPI::Broodwar->self()->getUpgradeLevel(BWAPI::UpgradeTypes::Charon_Boosters))
		{
			range = 8 * 32;
		}
	}
	else if (attacker->getType() == BWAPI::UnitTypes::Zerg_Hydralisk)
	{
		if (attacker->getPlayer() == BWAPI::Broodwar->enemy() ||
			BWAPI::Broodwar->self()->getUpgradeLevel(BWAPI::UpgradeTypes::Grooved_Spines))
		{
			range = 5 * 32;
		}
	}

    return range;
}

// Range is zero if the attacker cannot attack the target at all.
// NOTE Currently unused but potentially useful.
int UnitUtil::GetAttackRangeAssumingUpgrades(BWAPI::UnitType attacker, BWAPI::UnitType target)
{
	// Reavers, carriers, and bunkers have "no weapon" but still have an attack range.
	if (attacker == BWAPI::UnitTypes::Protoss_Reaver && !target.isFlyer())
	{
		return 8;
	}
	if (attacker == BWAPI::UnitTypes::Protoss_Carrier)
	{
		return 8;
	}
	if (attacker == BWAPI::UnitTypes::Terran_Bunker)
	{
		return 6 * 32;
	}

	BWAPI::WeaponType weapon = GetWeapon(attacker, target);
	if (weapon == BWAPI::WeaponTypes::None)
    {
        return 0;
    }

    int range = weapon.maxRange();

	// Assume that any upgrades are researched.
	if (attacker == BWAPI::UnitTypes::Protoss_Dragoon)
	{
		range = 6 * 32;
	}
	else if (attacker == BWAPI::UnitTypes::Terran_Marine)
	{
		range = 5 * 32;
	}
	else if (attacker == BWAPI::UnitTypes::Terran_Goliath && target.isFlyer())
	{
		range = 8 * 32;
	}
	else if (attacker == BWAPI::UnitTypes::Zerg_Hydralisk)
	{
		range = 5 * 32;
	}

	return range;
}

// All our units, whether completed or not.
int UnitUtil::GetAllUnitCount(BWAPI::UnitType type)
{
    int count = 0;
    for (const auto unit : BWAPI::Broodwar->self()->getUnits())
    {
        // trivial case: unit which exists matches the type
        if (unit->getType() == type)
        {
            count++;
        }

        // case where a zerg egg contains the unit type
        if (unit->getType() == BWAPI::UnitTypes::Zerg_Egg && unit->getBuildType() == type)
        {
            count += type.isTwoUnitsInOneEgg() ? 2 : 1;
        }

        // case where a building has started constructing a unit but it doesn't yet have a unit associated with it
        if (unit->getRemainingTrainTime() > 0)
        {
            BWAPI::UnitType trainType = unit->getLastCommand().getUnitType();

            if (trainType == type && unit->getRemainingTrainTime() == trainType.buildTime())
            {
                count++;
            }
        }
    }

    return count;
}

// Only our completed units.
int UnitUtil::GetCompletedUnitCount(BWAPI::UnitType type)
{
	int count = 0;
	for (const auto unit : BWAPI::Broodwar->self()->getUnits())
	{
		if (unit->getType() == type && unit->isCompleted())
		{
			count++;
		}
	}

	return count;
}