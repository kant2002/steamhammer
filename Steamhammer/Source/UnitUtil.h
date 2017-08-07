#pragma once

#include <Common.h>
#include <BWAPI.h>

namespace UAlbertaBot
{
namespace UnitUtil
{      
	bool IsMorphedBuildingType(BWAPI::UnitType unitType);
	
	bool IsCombatSimUnit(BWAPI::UnitType type);
	bool IsCombatUnit(BWAPI::UnitType type);
	bool IsCombatUnit(BWAPI::Unit unit);
    bool IsValidUnit(BWAPI::Unit unit);
    
	bool CanAttack(BWAPI::Unit attacker, BWAPI::Unit target);
	bool CanAttack(BWAPI::UnitType attacker, BWAPI::UnitType target);
	bool CanAttackAir(BWAPI::Unit attacker);
	bool TypeCanAttackAir(BWAPI::UnitType attacker);
	bool CanAttackGround(BWAPI::Unit attacker);
	bool TypeCanAttackGround(BWAPI::UnitType attacker);
	double CalculateLTD(BWAPI::Unit attacker, BWAPI::Unit target);
	BWAPI::WeaponType GetWeapon(BWAPI::Unit attacker, BWAPI::Unit target);
	BWAPI::WeaponType GetWeapon(BWAPI::UnitType attacker, BWAPI::Unit target);
	BWAPI::WeaponType GetWeapon(BWAPI::UnitType attacker, BWAPI::UnitType target);
	int GetAttackRange(BWAPI::Unit attacker, BWAPI::Unit target);
	int GetAttackRangeAssumingUpgrades(BWAPI::UnitType attacker, BWAPI::UnitType target);
	
	int GetAllUnitCount(BWAPI::UnitType type);
	int GetCompletedUnitCount(BWAPI::UnitType type);
};
}