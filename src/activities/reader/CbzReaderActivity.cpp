#include "CbzReaderActivity.h"
#include <FsHelpers.h>
#include <HalStorage.h>
#include <Logging.h>
#include <I18n.h>
#include "ProgressFile.h"
#include "RecentBooksStore.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include <ZipFile.h>

CbzReaderActivity* CbzReaderActivity::activeInstance = nullptr;

// Bayer 8x8 threshold matrix for ordered dithering
static const uint8_t B8[8][8] = {
    { 0, 48, 12, 60,  3, 51, 15, 63},
    {32, 16, 44, 28, 35, 19, 47, 31},
    { 8, 56,  4, 52, 11, 59,  7, 55},
    {40, 24, 36, 20, 43, 27, 39, 23},
    { 2, 50, 14, 62,  1, 49, 13, 61},
    {34, 18, 46, 30, 33, 17, 45, 29},
    {10, 58,  6, 54,  9, 57,  5, 53},
    {42, 26, 38, 22, 41, 25, 37, 21}
};

CbzReaderActivity::CbzReaderActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string path)
    : Activity("CbzReader", renderer, mappedInput), filePath(std::move(path)) {}

std::string CbzReaderActivity::getCachePath() const {
  uint64_t hash = ZipFile::fnvHash64(filePath.c_str(), filePath.length());
  char buf[64];
  snprintf(buf, sizeof(buf), "/.crosspoint/cbz_%llx", hash);
  return std::string(buf);
}

void CbzReaderActivity::saveProgress() const {
  uint8_t data[4];
  data[0] = currentFileIndex & 0xFF;
  data[1] = (currentFileIndex >> 8) & 0xFF;
  data[2] = showRightHalf ? 1 : 0;
  data[3] = 0;
  
  if (!ProgressFile::writeAtomic(getCachePath(), data, sizeof(data))) {
    LOG_ERR("CBZ", "Failed to save progress: page %d", currentFileIndex);
  }

  int total = archive.getPageCount();
  int progressPercent = total > 0 ? static_cast<int>((currentFileIndex + 1) * 100.0f / total + 0.5f) : 0;
  if (progressPercent > 100) progressPercent = 100;
  RecentBooksStore::getInstance().updateProgress(filePath, progressPercent);
}

void CbzReaderActivity::loadProgress() {
  HalFile f;
  if (Storage.openFileForRead("CBZ", getCachePath() + "/progress.bin", f)) {
    uint8_t data[4];
    if (f.read(data, 4) == 4) {
      currentFileIndex = data[0] + (data[1] << 8);
      showRightHalf = (data[2] == 1);
    }
    f.close();
  }
}

void CbzReaderActivity::onEnter() {
  Activity::onEnter();

  if (!archive.open(filePath)) {
    LOG_ERR("CBZ", "Failed to load CBZ archive: %s", filePath.c_str());
    finish();
    return;
  }

  loadProgress();

  if (currentFileIndex >= static_cast<int>(archive.getPageCount())) {
    currentFileIndex = 0;
    showRightHalf = false;
  }

  preparePage();
  renderPage();
}

void CbzReaderActivity::onExit() {
  Activity::onExit();
  archive.close();
  // Clear screen on exit
  renderer.clearScreen();
  renderer.displayBuffer(HalDisplay::HALF_REFRESH);
}

void CbzReaderActivity::preparePage(bool goingBackward) {
  std::string tempPath = "/.crosspoint/cbz_temp.tmp";
  
  // Extract to SD card
  if (!archive.extractPageToTempFile(currentFileIndex, tempPath)) {
    LOG_ERR("CBZ", "Failed to extract page index %d", currentFileIndex);
    return;
  }

  // Open the temp file to parse image headers (JPEG or PNG)
  HalFile file;
  if (!Storage.openFileForRead("CBZ", tempPath, file)) {
    LOG_ERR("CBZ", "Failed to open extracted temp file for size query");
    return;
  }

  size_t fileSize = file.size();
  uint8_t* buffer = static_cast<uint8_t*>(malloc(fileSize));
  if (!buffer) {
    LOG_ERR("CBZ", "OOM when allocating buffer for image size query");
    file.close();
    return;
  }

  size_t bytesRead = file.read(buffer, fileSize);
  file.close();

  if (bytesRead < fileSize) {
    LOG_ERR("CBZ", "Short read on temp file: read %d of %d", (int)bytesRead, (int)fileSize);
    free(buffer);
    return;
  }

  // Identify type and get dimensions
  isLandscape = false;
  imgWidth = 0;
  imgHeight = 0;

  if (bytesRead >= 8 && buffer[0] == 0x89 && buffer[1] == 0x50 && buffer[2] == 0x4E && buffer[3] == 0x47) {
    // PNG file
    PNG png;
    if (png.openRAM(buffer, fileSize, pngDrawCallback) == PNG_SUCCESS) {
      imgWidth = png.getWidth();
      imgHeight = png.getHeight();
      png.close();
    }
  } else {
    // Treat as JPEG
    JPEGDEC jpeg;
    if (jpeg.openRAM(buffer, fileSize, jpegDrawCallback)) {
      imgWidth = jpeg.getWidth();
      imgHeight = jpeg.getHeight();
      jpeg.close();
    }
  }

  free(buffer);

  if (imgWidth > 0 && imgHeight > 0) {
    isLandscape = (imgWidth > imgHeight);
  }

  if (goingBackward) {
    showRightHalf = isLandscape;
  }
}

void CbzReaderActivity::renderPage() {
  std::string tempPath = "/.crosspoint/cbz_temp.tmp";
  
  HalFile file;
  if (!Storage.openFileForRead("CBZ", tempPath, file)) {
    LOG_ERR("CBZ", "Failed to open temp file for rendering");
    return;
  }

  size_t fileSize = file.size();
  uint8_t* buffer = static_cast<uint8_t*>(malloc(fileSize));
  if (!buffer) {
    LOG_ERR("CBZ", "OOM allocating image render buffer");
    file.close();
    return;
  }

  file.read(buffer, fileSize);
  file.close();

  // Clear screen before drawing
  renderer.clearScreen();

  activeInstance = this;

  if (fileSize >= 8 && buffer[0] == 0x89 && buffer[1] == 0x50 && buffer[2] == 0x4E && buffer[3] == 0x47) {
    // PNG file
    PNG png;
    if (png.openRAM(buffer, fileSize, pngDrawCallback) == PNG_SUCCESS) {
      png.decode(reinterpret_cast<void*>(&png), 0);
      png.close();
    }
  } else {
    // JPEG file
    JPEGDEC jpeg;
    if (jpeg.openRAM(buffer, fileSize, jpegDrawCallback)) {
      jpeg.decode(0, 0, 0);
      jpeg.close();
    }
  }

  activeInstance = nullptr;
  free(buffer);

  // Draw UI hints/statusBar
  int total = archive.getPageCount();
  char pageStr[32];
  if (isLandscape) {
    snprintf(pageStr, sizeof(pageStr), "%d [%s] / %d", currentFileIndex + 1, showRightHalf ? "R" : "L", total);
  } else {
    snprintf(pageStr, sizeof(pageStr), "%d / %d", currentFileIndex + 1, total);
  }

  // Draw bottom bar
  const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", "<", ">");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  
  // Render page number centered at the top/bottom
  renderer.drawCenteredText(UI_10_FONT_ID, renderer.getScreenHeight() - 32, pageStr);

  renderer.displayBuffer(HalDisplay::HALF_REFRESH);
}

void CbzReaderActivity::pageTurn(bool isForward) {
  if (isForward) {
    if (isLandscape && !showRightHalf) {
      showRightHalf = true;
      renderPage();
    } else {
      if (currentFileIndex + 1 < static_cast<int>(archive.getPageCount())) {
        currentFileIndex++;
        showRightHalf = false;
        preparePage();
        renderPage();
      }
    }
  } else {
    if (isLandscape && showRightHalf) {
      showRightHalf = false;
      renderPage();
    } else {
      if (currentFileIndex > 0) {
        currentFileIndex--;
        preparePage(true); // Tells preparePage to set showRightHalf = true if landscape
        renderPage();
      }
    }
  }
}

void CbzReaderActivity::loop() {
  Activity::loop();

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    saveProgress();
    activityManager.goToFileBrowser(filePath);
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Up) ||
      mappedInput.wasReleased(MappedInputManager::Button::Left)) {
    pageTurn(false);
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Down) ||
      mappedInput.wasReleased(MappedInputManager::Button::Right)) {
    pageTurn(true);
    return;
  }
}

// static callbacks
int CbzReaderActivity::jpegDrawCallback(JPEGDRAW *pDraw) {
  if (activeInstance) {
    activeInstance->handleJpegDraw(pDraw);
  }
  return 1; // Continue decoding
}

int CbzReaderActivity::pngDrawCallback(PNGDRAW *pDraw) {
  if (activeInstance) {
    activeInstance->handlePngDraw(pDraw);
  }
  return 1;
}

void CbzReaderActivity::handleJpegDraw(JPEGDRAW *pDraw) {
  int screenW = renderer.getScreenWidth();
  int screenH = renderer.getScreenHeight();

  // If landscape, we are splitting it into two logical pages
  int effectiveSrcW = isLandscape ? (imgWidth / 2) : imgWidth;
  int srcOffset = (isLandscape && showRightHalf) ? (imgWidth / 2) : 0;

  for (int y = 0; y < pDraw->iHeight; y++) {
    int srcY = pDraw->y + y;
    if (srcY >= imgHeight) continue;

    int destY = srcY * screenH / imgHeight;
    if (destY < 0 || destY >= screenH) continue;

    for (int x = 0; x < pDraw->iWidth; x++) {
      int srcX = pDraw->x + x;
      if (srcX >= imgWidth) continue;

      // Crop checks for split screen
      if (isLandscape) {
        if (!showRightHalf && srcX >= imgWidth / 2) continue;
        if (showRightHalf && srcX < imgWidth / 2) continue;
      }

      int effectiveSrcX = srcX - srcOffset;
      int destX = effectiveSrcX * screenW / effectiveSrcW;
      if (destX < 0 || destX >= screenW) continue;

      uint16_t pixel = pDraw->pPixels[y * pDraw->iWidth + x];

      // Convert RGB565 to Grayscale
      uint8_t r = ((pixel >> 11) & 0x1F) * 255 / 31;
      uint8_t g = ((pixel >> 5) & 0x3F) * 255 / 63;
      uint8_t b = (pixel & 0x1F) * 255 / 31;
      uint8_t gray = static_cast<uint8_t>(0.299f * r + 0.587f * g + 0.114f * b);

      // Bayer ordered dither
      uint8_t threshold = B8[destY % 8][destX % 8] / 64.0f * 255.0f;
      bool pixelState = (gray < threshold);

      renderer.drawPixel(destX, destY, pixelState);
    }
  }
}

void CbzReaderActivity::handlePngDraw(PNGDRAW *pDraw) {
  int screenW = renderer.getScreenWidth();
  int screenH = renderer.getScreenHeight();

  // PNGdec draws line-by-line
  int srcY = pDraw->y;
  if (srcY >= imgHeight) return;

  int destY = srcY * screenH / imgHeight;
  if (destY < 0 || destY >= screenH) return;

  // We decode the row to an RGB565 line buffer
  uint16_t lineBuffer[2048];
  int lineW = pDraw->iWidth < 2048 ? pDraw->iWidth : 2048;
  
  PNG* png = reinterpret_cast<PNG*>(pDraw->pUser);
  png->getLineAsRGB565(pDraw, lineBuffer, PNG_RGB565_LITTLE_ENDIAN, 0xffffffff);

  int effectiveSrcW = isLandscape ? (imgWidth / 2) : imgWidth;
  int srcOffset = (isLandscape && showRightHalf) ? (imgWidth / 2) : 0;

  for (int srcX = 0; srcX < lineW; srcX++) {
    // Crop checks for split screen
    if (isLandscape) {
      if (!showRightHalf && srcX >= imgWidth / 2) continue;
      if (showRightHalf && srcX < imgWidth / 2) continue;
    }

    int effectiveSrcX = srcX - srcOffset;
    int destX = effectiveSrcX * screenW / effectiveSrcW;
    if (destX < 0 || destX >= screenW) continue;

    uint16_t pixel = lineBuffer[srcX];

    // Convert RGB565 to Grayscale
    uint8_t r = ((pixel >> 11) & 0x1F) * 255 / 31;
    uint8_t g = ((pixel >> 5) & 0x3F) * 255 / 63;
    uint8_t b = (pixel & 0x1F) * 255 / 31;
    uint8_t gray = static_cast<uint8_t>(0.299f * r + 0.587f * g + 0.114f * b);

    // Bayer ordered dither
    uint8_t threshold = B8[destY % 8][destX % 8] / 64.0f * 255.0f;
    bool pixelState = (gray < threshold);

    renderer.drawPixel(destX, destY, pixelState);
  }
}
