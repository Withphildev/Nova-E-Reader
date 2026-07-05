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
#include <memory>

CbzReaderActivity* CbzReaderActivity::activeInstance = nullptr;

// Pre-scaled Bayer 8x8 threshold matrix for ordered dithering (values scaled from 0..63 to 0..255)
static const uint8_t B8_255[8][8] = {
    {  0, 191,  47, 239,  11, 203,  59, 251},
    {127,  63, 175, 111, 139,  75, 187, 123},
    { 31, 223,  15, 207,  43, 235,  27, 219},
    {159,  95, 143,  79, 171, 107, 155,  91},
    {  7, 199,  55, 247,   3, 195,  51, 243},
    {135,  71, 179, 119, 131,  67, 171, 115},
    { 39, 231,  23, 215,  35, 227,  19, 211},
    {167, 103, 151,  87, 163,  99, 147,  83}
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
  std::string cachePath = getCachePath();
  if (!Storage.exists(cachePath.c_str())) {
    Storage.mkdir(cachePath.c_str());
  }

  uint8_t data[4];
  data[0] = currentFileIndex & 0xFF;
  data[1] = (currentFileIndex >> 8) & 0xFF;
  data[2] = showRightHalf ? 1 : 0;
  data[3] = 0;
  
  if (!ProgressFile::writeAtomic(cachePath, data, sizeof(data))) {
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
  // Clear screen on exit with HALF_REFRESH to remove ghosting
  renderer.clearScreen();
  renderer.displayBuffer(HalDisplay::HALF_REFRESH);
}

// File system callbacks for streaming decoding from SD card
void* CbzReaderActivity::cbzFileOpen(const char *szFilename, int32_t *pFileSize) {
  HalFile* file = new HalFile();
  if (Storage.openFileForRead("CBZ", szFilename, *file)) {
    *pFileSize = file->size();
    return reinterpret_cast<void*>(file);
  }
  delete file;
  return nullptr;
}

void CbzReaderActivity::cbzFileClose(void *pHandle) {
  HalFile* file = reinterpret_cast<HalFile*>(pHandle);
  if (file) {
    file->close();
    delete file;
  }
}

int32_t CbzReaderActivity::cbzJpegRead(JPEGFILE *pFile, uint8_t *pBuf, int32_t iLen) {
  HalFile* file = reinterpret_cast<HalFile*>(pFile->fHandle);
  return file ? file->read(pBuf, iLen) : 0;
}

int32_t CbzReaderActivity::cbzJpegSeek(JPEGFILE *pFile, int32_t iPosition) {
  HalFile* file = reinterpret_cast<HalFile*>(pFile->fHandle);
  return (file && file->seek(iPosition)) ? file->position() : -1;
}

int32_t CbzReaderActivity::cbzPngRead(PNGFILE *pFile, uint8_t *pBuf, int32_t iLen) {
  HalFile* file = reinterpret_cast<HalFile*>(pFile->fHandle);
  return file ? file->read(pBuf, iLen) : 0;
}

int32_t CbzReaderActivity::cbzPngSeek(PNGFILE *pFile, int32_t iPosition) {
  HalFile* file = reinterpret_cast<HalFile*>(pFile->fHandle);
  return (file && file->seek(iPosition)) ? file->position() : -1;
}

void CbzReaderActivity::preparePage(bool goingBackward) {
  std::string tempPath = "/cbz_temp.tmp";
  
  // Extract to SD card
  if (!archive.extractPageToTempFile(currentFileIndex, tempPath)) {
    LOG_ERR("CBZ", "Failed to extract page index %d", currentFileIndex);
    return;
  }

  // Open the temp file to check signature
  bool isPng = false;
  HalFile file;
  if (Storage.openFileForRead("CBZ", tempPath, file)) {
    uint8_t header[4];
    if (file.read(header, 4) == 4) {
      isPng = (header[0] == 0x89 && header[1] == 0x50 && header[2] == 0x4E && header[3] == 0x47);
    }
    file.close();
  }

  // Parse dimensions directly from headers (allocating decoders on heap to prevent stack overflow)
  isLandscape = false;
  imgWidth = 0;
  imgHeight = 0;

  if (isPng) {
    auto png = std::make_unique<PNG>();
    if (png->open(tempPath.c_str(), cbzFileOpen, cbzFileClose, cbzPngRead, cbzPngSeek, pngDrawCallback) == PNG_SUCCESS) {
      imgWidth = png->getWidth();
      imgHeight = png->getHeight();
      png->close();
    }
  } else {
    auto jpeg = std::make_unique<JPEGDEC>();
    if (jpeg->open(tempPath.c_str(), cbzFileOpen, cbzFileClose, cbzJpegRead, cbzJpegSeek, jpegDrawCallback)) {
      imgWidth = jpeg->getWidth();
      imgHeight = jpeg->getHeight();
      jpeg->close();
    }
  }

  if (imgWidth > 0 && imgHeight > 0) {
    isLandscape = (imgWidth > imgHeight);
  }

  if (goingBackward) {
    showRightHalf = isLandscape;
  }
}

void CbzReaderActivity::renderPage() {
  std::string tempPath = "/cbz_temp.tmp";
  
  // Verify dimensions are known
  if (imgWidth <= 0 || imgHeight <= 0) {
    preparePage();
  }

  if (imgWidth <= 0 || imgHeight <= 0) {
    LOG_ERR("CBZ", "Failed to resolve image dimensions");
    return;
  }

  // Check signature
  bool isPng = false;
  HalFile file;
  if (Storage.openFileForRead("CBZ", tempPath, file)) {
    uint8_t header[4];
    if (file.read(header, 4) == 4) {
      isPng = (header[0] == 0x89 && header[1] == 0x50 && header[2] == 0x4E && header[3] == 0x47);
    }
    file.close();
  }

  // Allocate heap buffer for PNG lines to prevent stack overflow
  pngLineBuffer = static_cast<uint16_t*>(malloc(imgWidth * sizeof(uint16_t)));
  if (!pngLineBuffer) {
    LOG_ERR("CBZ", "OOM when allocating heap line buffer for PNG (size %d)", imgWidth);
  }

  // Clear screen before drawing
  renderer.clearScreen();

  activeInstance = this;

  if (isPng) {
    auto png = std::make_unique<PNG>();
    if (png->open(tempPath.c_str(), cbzFileOpen, cbzFileClose, cbzPngRead, cbzPngSeek, pngDrawCallback) == PNG_SUCCESS) {
      png->decode(reinterpret_cast<void*>(png.get()), 0);
      png->close();
    }
  } else {
    auto jpeg = std::make_unique<JPEGDEC>();
    if (jpeg->open(tempPath.c_str(), cbzFileOpen, cbzFileClose, cbzJpegRead, cbzJpegSeek, jpegDrawCallback)) {
      jpeg->decode(0, 0, 0);
      jpeg->close();
    }
  }

  activeInstance = nullptr;

  // Free heap line buffer
  if (pngLineBuffer) {
    free(pngLineBuffer);
    pngLineBuffer = nullptr;
  }

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

  // Using FAST_REFRESH for instant page response
  renderer.displayBuffer(HalDisplay::FAST_REFRESH);
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

  int effectiveSrcW = isLandscape ? (imgWidth / 2) : imgWidth;
  int srcOffset = (isLandscape && showRightHalf) ? (imgWidth / 2) : 0;

  // Fixed point 16.16 multipliers to avoid integer division inside loops
  uint32_t scaleX = (screenW << 16) / effectiveSrcW;
  uint32_t scaleY = (screenH << 16) / imgHeight;

  for (int y = 0; y < pDraw->iHeight; y++) {
    int srcY = pDraw->y + y;
    if (srcY >= imgHeight) continue;

    int destY = (srcY * scaleY) >> 16;
    if (destY < 0 || destY >= screenH) continue;

    int destY_mod8 = destY & 7;

    for (int x = 0; x < pDraw->iWidth; x++) {
      int srcX = pDraw->x + x;
      if (srcX >= imgWidth) continue;

      // Crop checks for split screen
      if (isLandscape) {
        if (!showRightHalf && srcX >= imgWidth / 2) continue;
        if (showRightHalf && srcX < imgWidth / 2) continue;
      }

      int effectiveSrcX = srcX - srcOffset;
      int destX = (effectiveSrcX * scaleX) >> 16;
      if (destX < 0 || destX >= screenW) continue;

      uint16_t pixel = pDraw->pPixels[y * pDraw->iWidth + x];

      // Convert RGB565 to Grayscale using bitwise operations
      uint8_t r5 = (pixel >> 11) & 0x1F;
      uint8_t g6 = (pixel >> 5) & 0x3F;
      uint8_t b5 = pixel & 0x1F;
      uint8_t r = (r5 << 3) | (r5 >> 2);
      uint8_t g = (g6 << 2) | (g6 >> 4);
      uint8_t b = (b5 << 3) | (b5 >> 2);
      uint8_t gray = (r * 77 + g * 150 + b * 29) >> 8;

      // Darken midtones to boost contrast and prevent washout (gamma ~1.5)
      gray = (gray + ((gray * gray) >> 8)) >> 1;

      // Bayer ordered dither using pre-scaled matrix B8_255
      bool pixelState = (gray < B8_255[destY_mod8][destX & 7]);

      renderer.drawPixel(destX, destY, pixelState);
    }
  }
}

void CbzReaderActivity::handlePngDraw(PNGDRAW *pDraw) {
  if (!pngLineBuffer) return;

  int screenW = renderer.getScreenWidth();
  int screenH = renderer.getScreenHeight();

  // PNGdec draws line-by-line
  int srcY = pDraw->y;
  if (srcY >= imgHeight) return;

  uint32_t scaleY = (screenH << 16) / imgHeight;
  int destY = (srcY * scaleY) >> 16;
  if (destY < 0 || destY >= screenH) return;

  // We decode the row to the heap line buffer
  int lineW = pDraw->iWidth < imgWidth ? pDraw->iWidth : imgWidth;
  
  PNG* png = reinterpret_cast<PNG*>(pDraw->pUser);
  png->getLineAsRGB565(pDraw, pngLineBuffer, PNG_RGB565_LITTLE_ENDIAN, 0xffffffff);

  int effectiveSrcW = isLandscape ? (imgWidth / 2) : imgWidth;
  int srcOffset = (isLandscape && showRightHalf) ? (imgWidth / 2) : 0;

  uint32_t scaleX = (screenW << 16) / effectiveSrcW;
  int destY_mod8 = destY & 7;

  for (int srcX = 0; srcX < lineW; srcX++) {
    // Crop checks for split screen
    if (isLandscape) {
      if (!showRightHalf && srcX >= imgWidth / 2) continue;
      if (showRightHalf && srcX < imgWidth / 2) continue;
    }

    int effectiveSrcX = srcX - srcOffset;
    int destX = (effectiveSrcX * scaleX) >> 16;
    if (destX < 0 || destX >= screenW) continue;

    uint16_t pixel = pngLineBuffer[srcX];

    // Convert RGB565 to Grayscale using bitwise operations
    uint8_t r5 = (pixel >> 11) & 0x1F;
    uint8_t g6 = (pixel >> 5) & 0x3F;
    uint8_t b5 = pixel & 0x1F;
    uint8_t r = (r5 << 3) | (r5 >> 2);
    uint8_t g = (g6 << 2) | (g6 >> 4);
    uint8_t b = (b5 << 3) | (b5 >> 2);
    uint8_t gray = (r * 77 + g * 150 + b * 29) >> 8;

    // Darken midtones to boost contrast and prevent washout (gamma ~1.5)
    gray = (gray + ((gray * gray) >> 8)) >> 1;

    // Bayer ordered dither using pre-scaled matrix B8_255
    bool pixelState = (gray < B8_255[destY_mod8][destX & 7]);

    renderer.drawPixel(destX, destY, pixelState);
  }
}
