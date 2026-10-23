#include "WowDifficultyState.h"
#include <atomic>

namespace
{
    std::atomic<int32> GDungeonDifficulty{0};
    std::atomic<int32> GRaidDifficulty{0};
}

int32 FWowDifficultyState::GetDungeon() { return GDungeonDifficulty.load(std::memory_order_relaxed); }
int32 FWowDifficultyState::GetRaid() { return GRaidDifficulty.load(std::memory_order_relaxed); }
void FWowDifficultyState::SetDungeon(int32 Difficulty) { GDungeonDifficulty.store(FMath::Clamp(Difficulty, 0, 1), std::memory_order_relaxed); }
void FWowDifficultyState::SetRaid(int32 Difficulty) { GRaidDifficulty.store(FMath::Clamp(Difficulty, 0, 3), std::memory_order_relaxed); }
