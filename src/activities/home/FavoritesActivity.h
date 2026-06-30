#pragma once
#include <I18n.h>

#include <functional>
#include <string>
#include <vector>

#include "RecentBooksStore.h" // For RecentBook struct
#include "FavoritesStore.h"
#include "activities/Activity.h"
#include "util/ButtonNavigator.h"

class FavoritesActivity final : public Activity {
 private:
   ButtonNavigator buttonNavigator;

   size_t selectorIndex = 0;

   // Favorites state
   std::vector<RecentBook> favoriteBooks;

   // Data loading
   void loadFavorites();

   void onSelectBook(const std::string& path);

 public:
   explicit FavoritesActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
       : Activity("Favorites", renderer, mappedInput) {}
   void onEnter() override;
   void onExit() override;
   void loop() override;
   void render(RenderLock&&) override;
};
