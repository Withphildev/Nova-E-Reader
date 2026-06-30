#pragma once
#include <cstdint>

struct ReadingStats {
  uint32_t totalPagesRead = 0;
  uint16_t currentStreak = 0;
  uint32_t lastReadDay = 0;      // Days since epoch
  uint16_t pagesReadToday = 0;
  uint16_t totalDaysRead = 0;
  uint16_t photographyOpensCount = 0;
  uint16_t referenceOpensCount = 0;
  uint32_t celebratedMilestones = 0;
};

struct ReadingStatsFile {
  uint16_t magic = 0x4E43; // "NC"
  uint8_t version = 2;
  uint8_t reserved = 0;
  ReadingStats stats;
};

class ReadingStatsManager {
 private:
  ReadingStats stats;
  uint32_t dirtyTurns = 0;

  ReadingStatsManager();
  static ReadingStatsManager instance;

 public:
  static ReadingStatsManager& getInstance() { return instance; }

  uint32_t getTotalPagesRead() const { return stats.totalPagesRead; }
  uint16_t getCurrentStreak() const { return stats.currentStreak; }
  uint16_t getPagesReadToday() const { return stats.pagesReadToday; }
  uint16_t getTotalDaysRead() const { return stats.totalDaysRead; }
  uint16_t getPhotographyOpensCount() const { return stats.photographyOpensCount; }
  uint16_t getReferenceOpensCount() const { return stats.referenceOpensCount; }
  uint32_t getCelebratedMilestones() const { return stats.celebratedMilestones; }

  void incrementPhotographyOpens() { stats.photographyOpensCount++; dirtyTurns++; }
  void incrementReferenceOpens() { stats.referenceOpensCount++; dirtyTurns++; }
  bool isMilestoneCelebrated(uint8_t milestoneBit) const { return (stats.celebratedMilestones & (1 << milestoneBit)) != 0; }
  void markMilestoneCelebrated(uint8_t milestoneBit) { stats.celebratedMilestones |= (1 << milestoneBit); dirtyTurns++; }

  void load();
  void save();
  void flush();
  void recordPageRead();
};
