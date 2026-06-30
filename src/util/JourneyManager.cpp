#include "JourneyManager.h"
#include "ReadingStatsManager.h"
#include "RecentBooksStore.h"

JourneyManager JourneyManager::instance;

JourneyManager::JourneyStage JourneyManager::getJourneyStage() const {
  const auto& books = RecentBooksStore::getInstance().getBooks();
  if (books.empty()) {
    return STAGE_NEW_FRIEND;
  }

  int progress = books[0].progressPercent;
  if (progress >= 100) {
    return STAGE_EXPLORER;
  }
  if (progress >= 75) {
    return STAGE_HABIT_BUILDER;
  }
  if (progress >= 50) {
    return STAGE_GROWING_TOGETHER;
  }
  if (progress >= 25) {
    return STAGE_READING_TOGETHER;
  }
  return STAGE_NEW_FRIEND;
}

StrId JourneyManager::getStageTextId(JourneyStage stage) const {
  switch (stage) {
    case STAGE_READING_TOGETHER:
      return StrId::STR_STAGE2_QUOTE;
    case STAGE_GROWING_TOGETHER:
      return StrId::STR_STAGE3_QUOTE;
    case STAGE_HABIT_BUILDER:
      return StrId::STR_STAGE4_QUOTE;
    case STAGE_EXPLORER:
      return StrId::STR_STAGE5_QUOTE;
    case STAGE_PHOTOGRAPHER:
      return StrId::STR_STAGE6_QUOTE;
    case STAGE_REFERENCE_KEEPER:
      return StrId::STR_STAGE7_QUOTE;
    case STAGE_NEW_FRIEND:
    default:
      return StrId::STR_STAGE1_QUOTE;
  }
}

int JourneyManager::getSleepRoomStage() const {
  const auto& stats = ReadingStatsManager::getInstance();
  const uint16_t activeDays = stats.getTotalDaysRead();

  if (activeDays >= 365) {
    return 4; // Library
  }
  if (activeDays >= 100) {
    return 3; // Comfortable Room
  }
  if (activeDays >= 30) {
    return 2; // Room with Bookshelf
  }
  return 1; // Empty Room
}

JourneyManager::PendingMilestone JourneyManager::checkNewMilestones() {
  auto& statsMgr = ReadingStatsManager::getInstance();
  const uint32_t pages = statsMgr.getTotalPagesRead();
  const uint32_t streak = statsMgr.getCurrentStreak();

  // 1. First Page
  if (pages >= 1 && !statsMgr.isMilestoneCelebrated(MILESTONE_FIRST_PAGE)) {
    statsMgr.markMilestoneCelebrated(MILESTONE_FIRST_PAGE);
    statsMgr.save();
    return MILESTONE_FIRST_PAGE;
  }
  // 2. 3-Day Streak
  if (streak >= 3 && !statsMgr.isMilestoneCelebrated(MILESTONE_3_DAY_STREAK)) {
    statsMgr.markMilestoneCelebrated(MILESTONE_3_DAY_STREAK);
    statsMgr.save();
    return MILESTONE_3_DAY_STREAK;
  }
  // 3. 100 Pages
  if (pages >= 100 && !statsMgr.isMilestoneCelebrated(MILESTONE_100_PAGES)) {
    statsMgr.markMilestoneCelebrated(MILESTONE_100_PAGES);
    statsMgr.save();
    return MILESTONE_100_PAGES;
  }
  // 4. 7-Day Streak
  if (streak >= 7 && !statsMgr.isMilestoneCelebrated(MILESTONE_7_DAY_STREAK)) {
    statsMgr.markMilestoneCelebrated(MILESTONE_7_DAY_STREAK);
    statsMgr.save();
    return MILESTONE_7_DAY_STREAK;
  }
  // 5. 500 Pages
  if (pages >= 500 && !statsMgr.isMilestoneCelebrated(MILESTONE_500_PAGES)) {
    statsMgr.markMilestoneCelebrated(MILESTONE_500_PAGES);
    statsMgr.save();
    return MILESTONE_500_PAGES;
  }
  // 6. 14-Day Streak
  if (streak >= 14 && !statsMgr.isMilestoneCelebrated(MILESTONE_14_DAY_STREAK)) {
    statsMgr.markMilestoneCelebrated(MILESTONE_14_DAY_STREAK);
    statsMgr.save();
    return MILESTONE_14_DAY_STREAK;
  }
  // 7. 1000 Pages
  if (pages >= 1000 && !statsMgr.isMilestoneCelebrated(MILESTONE_1000_PAGES)) {
    statsMgr.markMilestoneCelebrated(MILESTONE_1000_PAGES);
    statsMgr.save();
    return MILESTONE_1000_PAGES;
  }
  // 8. 30-Day Streak
  if (streak >= 30 && !statsMgr.isMilestoneCelebrated(MILESTONE_30_DAY_STREAK)) {
    statsMgr.markMilestoneCelebrated(MILESTONE_30_DAY_STREAK);
    statsMgr.save();
    return MILESTONE_30_DAY_STREAK;
  }

  return MILESTONE_NONE;
}
