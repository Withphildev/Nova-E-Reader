#pragma once
#include <cstdint>
#include "I18n.h"

class JourneyManager {
 public:
  enum JourneyStage {
    STAGE_NEW_FRIEND = 1,        // Day 1 / Default
    STAGE_READING_TOGETHER = 2,  // 3-day streak OR 100 pages
    STAGE_GROWING_TOGETHER = 3,  // 7-day streak OR 500 pages
    STAGE_HABIT_BUILDER = 4,     // 14-day streak OR 1,000 pages
    STAGE_EXPLORER = 5,          // 30-day streak OR 2,500 pages
    STAGE_PHOTOGRAPHER = 6,      // 50 photography opens
    STAGE_REFERENCE_KEEPER = 7   // 10 reference opens
  };

  enum PendingMilestone {
    MILESTONE_NONE = 0,
    MILESTONE_FIRST_PAGE,
    MILESTONE_100_PAGES,
    MILESTONE_500_PAGES,
    MILESTONE_1000_PAGES,
    MILESTONE_3_DAY_STREAK,
    MILESTONE_7_DAY_STREAK,
    MILESTONE_14_DAY_STREAK,
    MILESTONE_30_DAY_STREAK
  };

 private:
  JourneyManager() = default;
  static JourneyManager instance;

 public:
  static JourneyManager& getInstance() { return instance; }

  JourneyStage getJourneyStage() const;
  StrId getStageTextId(JourneyStage stage) const;
  int getSleepRoomStage() const; // Returns 1 to 4 based on active reading days
  PendingMilestone checkNewMilestones();
};
