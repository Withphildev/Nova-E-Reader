#include "FavoritesActivity.h"

#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>

#include <algorithm>
#include <memory>

#include "MappedInputManager.h"
#include "FavoritesStore.h"
#include "components/UITheme.h"
#include "fontIds.h"

void FavoritesActivity::loadFavorites() {
  favoriteBooks = FavoritesStore::getInstance().getFavorites();
}

void FavoritesActivity::onEnter() {
  Activity::onEnter();

  // Load data
  loadFavorites();

  selectorIndex = 0;
  requestUpdate();
}

void FavoritesActivity::onExit() {
  Activity::onExit();
  favoriteBooks.clear();
}

void FavoritesActivity::onSelectBook(const std::string& path) {
  activityManager.goToReader(path);
}

void FavoritesActivity::loop() {
  const int pageItems = UITheme::getInstance().getNumberOfItemsPerPage(renderer, true, false, true, true);

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    if (!favoriteBooks.empty() && selectorIndex < favoriteBooks.size()) {
      LOG_DBG("FAV", "Selected favorite book: %s", favoriteBooks[selectorIndex].path.c_str());
      onSelectBook(favoriteBooks[selectorIndex].path);
      return;
    }
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    onGoHome();
    return;
  }

  // Left or Right or Power button toggles favorite status (removes it from favorites)
  if (mappedInput.wasReleased(MappedInputManager::Button::Left) ||
      mappedInput.wasReleased(MappedInputManager::Button::Right) ||
      mappedInput.wasReleased(MappedInputManager::Button::Power)) {
    if (!favoriteBooks.empty() && selectorIndex < favoriteBooks.size()) {
      FavoritesStore::getInstance().toggleFavorite(favoriteBooks[selectorIndex].path);
      loadFavorites();
      if (favoriteBooks.empty()) {
        selectorIndex = 0;
      } else if (selectorIndex >= favoriteBooks.size()) {
        selectorIndex = favoriteBooks.size() - 1;
      }
      requestUpdate(true);
      return;
    }
  }

  int listSize = static_cast<int>(favoriteBooks.size());

  buttonNavigator.onNextRelease([this, listSize] {
    selectorIndex = ButtonNavigator::nextIndex(static_cast<int>(selectorIndex), listSize);
    requestUpdate();
  });

  buttonNavigator.onPreviousRelease([this, listSize] {
    selectorIndex = ButtonNavigator::previousIndex(static_cast<int>(selectorIndex), listSize);
    requestUpdate();
  });

  buttonNavigator.onNextContinuous([this, listSize, pageItems] {
    selectorIndex = ButtonNavigator::nextPageIndex(static_cast<int>(selectorIndex), listSize, pageItems);
    requestUpdate();
  });

  buttonNavigator.onPreviousContinuous([this, listSize, pageItems] {
    selectorIndex = ButtonNavigator::previousPageIndex(static_cast<int>(selectorIndex), listSize, pageItems);
    requestUpdate();
  });
}

void FavoritesActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();
  const auto& metrics = UITheme::getInstance().getMetrics();

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, tr(STR_MENU_FAVORITES));

  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int contentHeight = pageHeight - contentTop - metrics.buttonHintsHeight - metrics.verticalSpacing;

  if (favoriteBooks.empty()) {
    renderer.drawText(UI_10_FONT_ID, metrics.contentSidePadding, contentTop + 20, tr(STR_NO_FAVORITES));
  } else {
    GUI.drawList(
        renderer, Rect{0, contentTop, pageWidth, contentHeight}, favoriteBooks.size(), selectorIndex,
        [this](int index) { return favoriteBooks[index].title; },
        [this](int index) { return favoriteBooks[index].author; },
        [this](int index) { return UIIcon::Bookmark; });
  }

  // Help text
  const auto labels = mappedInput.mapLabels(tr(STR_HOME), favoriteBooks.empty() ? "" : tr(STR_OPEN), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}
