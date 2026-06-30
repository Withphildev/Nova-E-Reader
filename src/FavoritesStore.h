#pragma once
#include <string>
#include <vector>
#include "RecentBooksStore.h" // Reuses the RecentBook struct

class FavoritesStore;
namespace JsonSettingsIO {
bool loadFavorites(FavoritesStore& store, const char* json);
bool saveFavorites(const FavoritesStore& store, const char* path);
}  // namespace JsonSettingsIO

class FavoritesStore {
  // Static instance
  static FavoritesStore instance;

  std::vector<RecentBook> favorites;

  friend bool JsonSettingsIO::loadFavorites(FavoritesStore&, const char*);
  friend bool JsonSettingsIO::saveFavorites(const FavoritesStore&, const char*);

 public:
  ~FavoritesStore() = default;

  // Get singleton instance
  static FavoritesStore& getInstance() { return instance; }

  // Check if a path is favorited
  bool isFavorite(const std::string& path) const;

  // Toggle favorite status
  void toggleFavorite(const std::string& path, const std::string& title = "",
                      const std::string& author = "", const std::string& coverBmpPath = "");

  // Save to file
  bool saveToFile() const;

  // Load from file
  bool loadFromFile();

  // Get list of favorites
  const std::vector<RecentBook>& getFavorites() const { return favorites; }

  // Get count
  int getCount() const { return static_cast<int>(favorites.size()); }
};
