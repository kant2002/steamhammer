#pragma once

#include "UnitData.h"

namespace UAlbertaBot {

    class FastAPproximation {
        struct FAPUnit {
            FAPUnit(BWAPI::Unit u);
            FAPUnit(const UnitInfo & ui);

            const FAPUnit &operator= (const FAPUnit & other) const;
            double unitSpeed(const UnitInfo & ui) const;

            int id = 0;

            mutable int x = 0, y = 0;

            mutable int health = 0;
            mutable int maxHealth = 0;
            mutable int armor = 0;

            mutable int shields = 0;
            mutable int shieldArmor = 0;
            mutable int maxShields = 0;

            mutable double speed = 0;
            mutable bool flying = false;
            mutable int elevation = -1;
            mutable bool underSwarm = false;

            mutable BWAPI::UnitSizeType unitSize;

            mutable int groundDamage = 0;
            mutable int groundCooldown = 0;
            mutable int groundMaxRange = 0;                 // square of the true range
            mutable int groundMinRange = 0;                 // square of the true range
            mutable BWAPI::DamageType groundDamageType;

            mutable int airDamage = 0;
            mutable int airCooldown = 0;
            mutable int airMaxRange = 0;                    // square of the true range
            mutable BWAPI::DamageType airDamageType;

            mutable BWAPI::UnitType unitType;
            mutable BWAPI::Player player = nullptr;
            mutable int healTimer = 0;
            mutable bool isOrganic = false;
            mutable bool didHealThisFrame = false;
            mutable int score = 0;

            mutable int attackCooldownRemaining = 0;

#ifdef _DEBUG

            mutable int damageTaken = 0;

#endif

            bool operator< (const FAPUnit &other) const;

            int unitScore(BWAPI::UnitType type) const;
        };

        public:

            FastAPproximation();

            void addUnitPlayer1(FAPUnit fu);
            void addIfCombatUnitPlayer1(FAPUnit fu);
            void addUnitPlayer2(FAPUnit fu);
            void addIfCombatUnitPlayer2(FAPUnit fu);

            void simulate(int nFrames = 4 * 24); // 4 seconds on fastest
            void simulateRetreat(const BWAPI::Position & retreatTo, int nFrames = 2 * 24);

            std::pair <int, int> playerScores() const;
            std::pair <int, int> playerScoresUnits() const;
            std::pair <int, int> playerScoresBuildings() const;
            std::pair <std::vector <FAPUnit> *, std::vector <FAPUnit> *> getState();
            void clearState();

        private:
            std::vector <FAPUnit> player1, player2;

            // A distance greater than the largest squared distance that FAP will use.
            static const int InfiniteDistanceSquared = 8192 * 8192 + 1;

            bool didSomething;
            BWAPI::Position targetPosition;     // when doing movesim()

            void dealDamage(const FastAPproximation::FAPUnit & fu, int damage, BWAPI::DamageType damageType) const;
            int distSquared(const FastAPproximation::FAPUnit & u1, const BWAPI::Position & xy) const;
            int distSquared(const FastAPproximation::FAPUnit & u1, const FastAPproximation::FAPUnit & u2) const;
            bool isSuicideUnit(BWAPI::UnitType ut);
            void unitsim(const FAPUnit & fu, std::vector <FAPUnit> &enemyUnits);
            void movesim(const FAPUnit & fu, std::vector <FAPUnit> &enemyUnits);
            void medicsim(const FAPUnit & fu, std::vector <FAPUnit> &friendlyUnits);
            bool suicideSim(const FAPUnit & fu, std::vector <FAPUnit> &enemyUnits);
            void isimulate(bool retreat);
            void unitDeath(const FAPUnit & fu, std::vector <FAPUnit> &itsFriendlies);
            void convertToUnitType(const FAPUnit &fu, BWAPI::UnitType ut);
    };

}

extern UAlbertaBot::FastAPproximation fap;
