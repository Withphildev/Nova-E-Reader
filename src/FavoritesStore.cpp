#include "FavoritesStore.h"
#include <HalStorage.h>
#include <JsonSettingsIO.h>
#include <Logging.h>
#include <algorithm>

namespace {
constexpr char FAVORITES_FILE_JSON[] = "/.crosspoint/favorites.json";
} // namespace

FavoritesStore FavoritesStore::instance;

bool FavoritesStore::isFavorite(const std::string& path) const {
  for (const auto& b : favorites) {
    if (b.path == path) {
      return true;
    }
  }
  return false;
}

void FavoritesStore::toggleFavorite(const std::string& path, const std::string& title,
                                    const std::string& author, const std::string& coverBmpPath) {
  auto it = std::find_if(favorites.begin(), favorites.end(),
                         [&](const RecentBook& b) { return b.path == path; });
  if (it != favorites.end()) {
    favorites.erase(it);
    LOG_DBG("FAV", "Removed from favorites: %s", path.c_str());
  } else {
    std::string t = title;
    std::string a = author;
    std::string c = coverBmpPath;
    if (t.empty()) {
      RecentBook b = RECENT_BOOKS.getDataFromBook(path);
      t = b.title;
      a = b.author;
      c = b.coverBmpPath;
    }
    favorites.push_back({path, t, a, c, 0});
    LOG_DBG("FAV", "Added to favorites: %s", path.c_str());
  }
  saveToFile();
}

bool FavoritesStore::saveToFile() const {
  Storage.mkdir("/.crosspoint");
  return JsonSettingsIO::saveFavorites(*this, FAVORITES_FILE_JSON);
}

bool FavoritesStore::loadFromFile() {
  if (Storage.exists(FAVORITES_FILE_JSON)) {
    String json = Storage.readFile(FAVORITES_FILE_JSON);
    if (!json.isEmpty()) {
      return JsonSettingsIO::loadFavorites(*this, json.c_str());
    }
  }
  return false;
}
