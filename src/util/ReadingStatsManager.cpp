#include "ReadingStatsManager.h"
#include <HalStorage.h>
#include <Logging.h>
#include <time.h>

ReadingStatsManager ReadingStatsManager::instance;

ReadingStatsManager::ReadingStatsManager() {
  stats.totalPagesRead = 0;
  stats.currentStreak = 0;
  stats.lastReadDay = 0;
  stats.pagesReadToday = 0;
  stats.totalDaysRead = 0;
  stats.photographyOpensCount = 0;
  stats.referenceOpensCount = 0;
  stats.celebratedMilestones = 0;
  dirtyTurns = 0;
}

struct StatsPayload {
  uint32_t magic;
  uint32_t version;
  uint32_t totalPagesRead;
  uint32_t currentStreak;
  uint32_t lastReadDay;
  uint32_t pagesReadToday;
};

void ReadingStatsManager::load() {
  HalFile f;
  if (!Storage.openFileForRead("STATS", "/.crosspoint/stats.bin", f)) {
    LOG_INF("STATS", "No stats file found, starting fresh");
    return;
  }

  // Try loading version 2 first
  ReadingStatsFile filePayload;
  int readBytes = f.read(&filePayload, sizeof(filePayload));
  if (readBytes == sizeof(filePayload)) {
    if (filePayload.magic == 0x4E43 && filePayload.version == 2) { // "NC"
      stats = filePayload.stats;
      LOG_INF("STATS", "Loaded v2 stats: total=%u, streak=%u, day=%u, today=%u, photo=%u, ref=%u, celebrated=0x%X",
              stats.totalPagesRead, stats.currentStreak, stats.lastReadDay, stats.pagesReadToday,
              stats.photographyOpensCount, stats.referenceOpensCount, stats.celebratedMilestones);
      f.close();
      return;
    }
  }

  // Fallback: try loading legacy version 1
  f.seek(0);
  StatsPayload legacyPayload;
  readBytes = f.read(&legacyPayload, sizeof(legacyPayload));
  if (readBytes == sizeof(legacyPayload)) {
    if (legacyPayload.magic == 0x4E535453 && legacyPayload.version == 1) { // "NSTS"
      stats.totalPagesRead = legacyPayload.totalPagesRead;
      stats.currentStreak = static_cast<uint16_t>(legacyPayload.currentStreak);
      stats.lastReadDay = legacyPayload.lastReadDay;
      stats.pagesReadToday = static_cast<uint16_t>(legacyPayload.pagesReadToday);
      stats.totalDaysRead = static_cast<uint16_t>(legacyPayload.currentStreak);
      stats.photographyOpensCount = 0;
      stats.referenceOpensCount = 0;
      stats.celebratedMilestones = 0;
      LOG_INF("STATS", "Migrated v1 stats: total=%u, streak=%u, day=%u, today=%u, totalDays=%u",
              stats.totalPagesRead, stats.currentStreak, stats.lastReadDay, stats.pagesReadToday, stats.totalDaysRead);
      f.close();
      save(); // Save immediately in v2 format
      return;
    }
  }

  LOG_ERR("STATS", "Failed to parse stats.bin or magic mismatch");
  f.close();
}

void ReadingStatsManager::save() {
  Storage.mkdir("/.crosspoint");
  HalFile f;
  if (!Storage.openFileForWrite("STATS", "/.crosspoint/stats.bin", f)) {
    LOG_ERR("STATS", "Failed to open stats file for writing");
    return;
  }
  ReadingStatsFile filePayload;
  filePayload.stats = stats;

  size_t written = f.write(&filePayload, sizeof(filePayload));
  if (written == sizeof(filePayload)) {
    LOG_DBG("STATS", "Saved v2 stats successfully");
  } else {
    LOG_ERR("STATS", "Failed to write stats: wrote %d bytes", (int)written);
  }
  f.close();
}

void ReadingStatsManager::flush() {
  if (dirtyTurns > 0) {
    save();
    dirtyTurns = 0;
  }
}

void ReadingStatsManager::recordPageRead() {
  stats.totalPagesRead++;
  stats.pagesReadToday++;

  time_t now = time(nullptr);
  if (now > 1600000000) { // Valid calendar time
    uint32_t dayIndex = now / 86400;
    if (stats.lastReadDay == 0) {
      // First time tracking with valid clock
      stats.lastReadDay = dayIndex;
      stats.currentStreak = 1;
      stats.pagesReadToday = 1;
      stats.totalDaysRead = 1;
    } else if (dayIndex == stats.lastReadDay + 1) {
      // Next day
      stats.lastReadDay = dayIndex;
      stats.currentStreak++;
      stats.pagesReadToday = 1;
      stats.totalDaysRead++;
    } else if (dayIndex > stats.lastReadDay + 1) {
      // Missed days
      stats.lastReadDay = dayIndex;
      stats.currentStreak = 1;
      stats.pagesReadToday = 1;
      stats.totalDaysRead++;
    } else if (dayIndex < stats.lastReadDay) {
      // Clock jump backwards? Don't break streak, just align day index
      stats.lastReadDay = dayIndex;
    }
  }

  dirtyTurns++;
  if (dirtyTurns >= 15) {
    save();
    dirtyTurns = 0;
  }
}
