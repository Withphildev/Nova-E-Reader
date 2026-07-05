#include "CbzReaderActivity.h"
#include <FsHelpers.h>
#include <HalStorage.h>
#include <Logging.h>
#include <I18n.h>
#include <Memory.h>
#include "ProgressFile.h"
#include "ReaderUtils.h"
#include "RecentBooksStore.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include <ZipFile.h>
#include <memory>
#include <new>

CbzReaderActivity* CbzReaderActivity::activeInstance = nullptr;

namespace {
// Extraction scratch file. Lives in the cache dir (not SD root) and is removed in onExit.
constexpr const char* kCbzTempPath = "/.crosspoint/cbz_temp.tmp";
// Save progress every N page turns so a dead battery or auto-sleep doesn't lose the reading position; onExit saves the remainder.
constexpr int kSaveEveryNPages = 10;
// Pre-dithered page cache format version. Bump when the header or dithering output changes; stale files are ignored and overwritten.
constexpr uint8_t kPageCacheVersion = 1;
constexpr size_t kPageCacheHeaderSize = 10;
}  // namespace

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

  const std::string fileName = filePath.substr(filePath.rfind('/') + 1);
  RECENT_BOOKS.addBook(filePath, fileName, "", "");

  showCurrentView(false);
}

void CbzReaderActivity::onExit() {
  Activity::onExit();
  if (archive.getPageCount() > 0) {
    saveProgress();
  }
  archive.close();
  Storage.remove(kCbzTempPath);
  renderer.clearScreen();
  renderer.displayBuffer(HalDisplay::HALF_REFRESH);
}

// File system callbacks for streaming decoding from SD card
void* CbzReaderActivity::cbzFileOpen(const char *szFilename, int32_t *pFileSize) {
  HalFile* file = new (std::nothrow) HalFile();
  if (!file) {
    LOG_ERR("CBZ", "OOM: HalFile");
    return nullptr;
  }
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

bool CbzReaderActivity::ensureTempPage(int fileIndex) {
  if (tempInfo.index == fileIndex && tempInfo.width > 0) {
    return true;
  }

  tempInfo = TempPageInfo{};

  if (!archive.extractPageToTempFile(fileIndex, kCbzTempPath)) {
    LOG_ERR("CBZ", "Failed to extract page index %d", fileIndex);
    return false;
  }

  bool isPng = false;
  {
    HalFile file;
    if (Storage.openFileForRead("CBZ", kCbzTempPath, file)) {
      uint8_t header[4];
      if (file.read(header, 4) == 4) {
        isPng = (header[0] == 0x89 && header[1] == 0x50 && header[2] == 0x4E && header[3] == 0x47);
      }
    }
  }

  int w = 0;
  int h = 0;
  if (isPng) {
    auto png = makeUniqueNoThrow<PNG>();
    if (!png) {
      LOG_ERR("CBZ", "OOM: PNG decoder");
      return false;
    }
    if (png->open(kCbzTempPath, cbzFileOpen, cbzFileClose, cbzPngRead, cbzPngSeek, pngDrawCallback) == PNG_SUCCESS) {
      w = png->getWidth();
      h = png->getHeight();
      png->close();
    }
  } else {
    auto jpeg = makeUniqueNoThrow<JPEGDEC>();
    if (!jpeg) {
      LOG_ERR("CBZ", "OOM: JPEG decoder");
      return false;
    }
    if (jpeg->open(kCbzTempPath, cbzFileOpen, cbzFileClose, cbzJpegRead, cbzJpegSeek, jpegDrawCallback)) {
      w = jpeg->getWidth();
      h = jpeg->getHeight();
      jpeg->close();
    }
  }

  if (w <= 0 || h <= 0) {
    LOG_ERR("CBZ", "Failed to parse image dimensions for page %d", fileIndex);
    return false;
  }

  tempInfo.index = fileIndex;
  tempInfo.width = w;
  tempInfo.height = h;
  tempInfo.png = isPng;
  tempInfo.landscape = w > h;
  return true;
}

bool CbzReaderActivity::decodeViewToFramebuffer(int fileIndex, bool rightHalf) {
  if (!ensureTempPage(fileIndex)) {
    return false;
  }

  int shift = 0;
  if (!tempInfo.png) {
    const int screenW = renderer.getScreenWidth();
    const int screenH = renderer.getScreenHeight();
    const int needW = tempInfo.landscape ? screenW * 2 : screenW;
    while (shift < 3 && (tempInfo.width >> (shift + 1)) >= needW && (tempInfo.height >> (shift + 1)) >= screenH) {
      shift++;
    }
  }

  decSrcW = tempInfo.width >> shift;
  decSrcH = tempInfo.height >> shift;
  decSplit = tempInfo.landscape;
  decRight = rightHalf && tempInfo.landscape;
  if (decSrcW <= 0 || decSrcH <= 0) {
    return false;
  }

  if (tempInfo.png) {
    pngLineBuffer = makeUniqueNoThrow<uint16_t[]>(decSrcW);
    if (!pngLineBuffer) {
      LOG_ERR("CBZ", "OOM: PNG line buffer (%d px)", decSrcW);
      return false;
    }
  }

  renderer.clearScreen();
  activeInstance = this;
  bool ok = false;

  if (tempInfo.png) {
    auto png = makeUniqueNoThrow<PNG>();
    if (!png) {
      LOG_ERR("CBZ", "OOM: PNG decoder");
    } else if (png->open(kCbzTempPath, cbzFileOpen, cbzFileClose, cbzPngRead, cbzPngSeek, pngDrawCallback) ==
               PNG_SUCCESS) {
      ok = png->decode(reinterpret_cast<void*>(png.get()), 0) == PNG_SUCCESS;
      png->close();
    }
  } else {
    auto jpeg = makeUniqueNoThrow<JPEGDEC>();
    if (!jpeg) {
      LOG_ERR("CBZ", "OOM: JPEG decoder");
    } else if (jpeg->open(kCbzTempPath, cbzFileOpen, cbzFileClose, cbzJpegRead, cbzJpegSeek, jpegDrawCallback)) {
      int options = 0;
      if (shift == 1) {
        options = JPEG_SCALE_HALF;
      } else if (shift == 2) {
        options = JPEG_SCALE_QUARTER;
      } else if (shift == 3) {
        options = JPEG_SCALE_EIGHTH;
      }
      ok = jpeg->decode(0, 0, options) == 1;
      jpeg->close();
    }
  }

  activeInstance = nullptr;
  pngLineBuffer.reset();

  if (!ok) {
    LOG_ERR("CBZ", "Failed to decode page %d", fileIndex);
  }
  return ok;
}

std::string CbzReaderActivity::cachedViewPath(int fileIndex, bool rightHalf) const {
  char buf[48];
  snprintf(buf, sizeof(buf), "/pages/p%05d_%c_%dx%d.bin", fileIndex, rightHalf ? 'R' : 'L',
           renderer.getScreenWidth(), renderer.getScreenHeight());
  return getCachePath() + buf;
}

bool CbzReaderActivity::tryLoadCachedView(int fileIndex, bool rightHalf) {
  const std::string path = cachedViewPath(fileIndex, rightHalf);
  bool ok = false;
  bool existed = false;

  {
    HalFile f;
    if (Storage.openFileForRead("CBZ", path, f)) {
      existed = true;
      uint8_t header[kPageCacheHeaderSize];
      if (f.read(header, sizeof(header)) == static_cast<int>(sizeof(header)) && header[0] == 'C' &&
          header[1] == 'B' && header[2] == 'Z' && header[3] == kPageCacheVersion) {
        const int w = header[4] | (header[5] << 8);
        const int h = header[6] | (header[7] << 8);
        if (w == renderer.getScreenWidth() && h == renderer.getScreenHeight()) {
          if (f.read(renderer.getFrameBuffer(), HalDisplay::BUFFER_SIZE) ==
              static_cast<int>(HalDisplay::BUFFER_SIZE)) {
            isLandscape = (header[8] & 1) != 0;
            ok = true;
          }
        }
      }
    }
  }

  if (existed && !ok) {
    Storage.remove(path.c_str());
    LOG_ERR("CBZ", "Dropped invalid page cache entry: %s", path.c_str());
  }
  return ok;
}

bool CbzReaderActivity::saveCachedView(int fileIndex, bool rightHalf) {
  const std::string cacheDir = getCachePath();
  if (!Storage.exists(cacheDir.c_str())) {
    Storage.mkdir(cacheDir.c_str());
  }
  const std::string pagesDir = cacheDir + "/pages";
  if (!Storage.exists(pagesDir.c_str())) {
    Storage.mkdir(pagesDir.c_str());
  }

  const std::string finalPath = cachedViewPath(fileIndex, rightHalf);
  const std::string tmpPath = finalPath + ".tmp";

  bool ok = false;
  {
    HalFile f;
    if (!Storage.openFileForWrite("CBZ", tmpPath, f)) {
      LOG_ERR("CBZ", "Failed to open page cache file for writing: %s", tmpPath.c_str());
      return false;
    }
    const uint16_t w = renderer.getScreenWidth();
    const uint16_t h = renderer.getScreenHeight();
    const uint8_t header[kPageCacheHeaderSize] = {'C',
                                                   'B',
                                                   'Z',
                                                   kPageCacheVersion,
                                                   static_cast<uint8_t>(w & 0xFF),
                                                   static_cast<uint8_t>(w >> 8),
                                                   static_cast<uint8_t>(h & 0xFF),
                                                   static_cast<uint8_t>(h >> 8),
                                                   static_cast<uint8_t>(tempInfo.landscape ? 1 : 0),
                                                   0};
    ok = f.write(header, sizeof(header)) == sizeof(header) &&
         f.write(renderer.getFrameBuffer(), HalDisplay::BUFFER_SIZE) == HalDisplay::BUFFER_SIZE;
  }

  if (!ok) {
    LOG_ERR("CBZ", "Failed to write page cache: %s", tmpPath.c_str());
    Storage.remove(tmpPath.c_str());
    return false;
  }

  Storage.remove(finalPath.c_str());
  if (!Storage.rename(tmpPath.c_str(), finalPath.c_str())) {
    Storage.remove(tmpPath.c_str());
    return false;
  }
  return true;
}

void CbzReaderActivity::finishRender() {
  const int total = static_cast<int>(archive.getPageCount());
  char pageStr[32];
  if (isLandscape) {
    snprintf(pageStr, sizeof(pageStr), "%d [%s] / %d", currentFileIndex + 1, showRightHalf ? "R" : "L", total);
  } else {
    snprintf(pageStr, sizeof(pageStr), "%d / %d", currentFileIndex + 1, total);
  }

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", "<", ">");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.drawCenteredText(UI_10_FONT_ID, renderer.getScreenHeight() - 32, pageStr);

  renderer.displayBuffer(HalDisplay::FAST_REFRESH);
}

void CbzReaderActivity::renderAndCacheCurrentView() {
  if (!decodeViewToFramebuffer(currentFileIndex, showRightHalf)) {
    renderer.clearScreen();
    finishRender();
    return;
  }
  isLandscape = tempInfo.landscape;
  showRightHalf = showRightHalf && isLandscape;
  saveCachedView(currentFileIndex, showRightHalf);
  finishRender();
}

void CbzReaderActivity::showCurrentView(bool enterFromEnd) {
  if (enterFromEnd) {
    if (tryLoadCachedView(currentFileIndex, true)) {
      showRightHalf = true;
      finishRender();
      return;
    }
    if (tryLoadCachedView(currentFileIndex, false)) {
      if (!isLandscape) {
        showRightHalf = false;
        finishRender();
        return;
      }
      showRightHalf = true;
    } else if (ensureTempPage(currentFileIndex)) {
      showRightHalf = tempInfo.landscape;
    } else {
      showRightHalf = false;
    }
    renderAndCacheCurrentView();
    return;
  }

  if (tryLoadCachedView(currentFileIndex, showRightHalf)) {
    finishRender();
    return;
  }
  renderAndCacheCurrentView();
}

void CbzReaderActivity::pageTurn(bool isForward) {
  bool changed = false;
  bool enterFromEnd = false;

  if (isForward) {
    if (isLandscape && !showRightHalf) {
      showRightHalf = true;
      changed = true;
    } else if (currentFileIndex + 1 < static_cast<int>(archive.getPageCount())) {
      currentFileIndex++;
      showRightHalf = false;
      changed = true;
    }
  } else {
    if (isLandscape && showRightHalf) {
      showRightHalf = false;
      changed = true;
    } else if (currentFileIndex > 0) {
      currentFileIndex--;
      showRightHalf = false;
      changed = true;
      enterFromEnd = true;
    }
  }

  if (!changed) {
    return;
  }
  showCurrentView(enterFromEnd);

  if (++pagesSinceSave >= kSaveEveryNPages) {
    saveProgress();
    pagesSinceSave = 0;
  }
}

void CbzReaderActivity::loop() {
  Activity::loop();

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    activityManager.goToFileBrowser(filePath);
    return;
  }

  const auto turn = ReaderUtils::detectPageTurn(mappedInput);
  if (turn.prev) {
    pageTurn(false);
    return;
  }
  if (turn.next) {
    pageTurn(true);
    return;
  }
}

// Decoder draw callbacks
int CbzReaderActivity::jpegDrawCallback(JPEGDRAW *pDraw) {
  if (activeInstance) {
    activeInstance->handleJpegDraw(pDraw);
  }
  return 1;
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

  int effectiveSrcW = decSplit ? (decSrcW / 2) : decSrcW;
  int srcOffset = decRight ? (decSrcW / 2) : 0;
  if (effectiveSrcW <= 0) return;

  uint32_t scaleX = (screenW << 16) / effectiveSrcW;
  uint32_t scaleY = (screenH << 16) / decSrcH;

  for (int y = 0; y < pDraw->iHeight; y++) {
    int srcY = pDraw->y + y;
    if (srcY >= decSrcH) continue;

    int destY = (srcY * scaleY) >> 16;
    if (destY < 0 || destY >= screenH) continue;

    int destY_mod8 = destY & 7;

    for (int x = 0; x < pDraw->iWidth; x++) {
      int srcX = pDraw->x + x;
      if (srcX >= decSrcW) continue;

      if (decSplit) {
        if (!decRight && srcX >= decSrcW / 2) continue;
        if (decRight && srcX < decSrcW / 2) continue;
      }

      int effectiveSrcX = srcX - srcOffset;
      int destX = (effectiveSrcX * scaleX) >> 16;
      if (destX < 0 || destX >= screenW) continue;

      uint16_t pixel = pDraw->pPixels[y * pDraw->iWidth + x];

      uint8_t r5 = (pixel >> 11) & 0x1F;
      uint8_t g6 = (pixel >> 5) & 0x3F;
      uint8_t b5 = pixel & 0x1F;
      uint8_t r = (r5 << 3) | (r5 >> 2);
      uint8_t g = (g6 << 2) | (g6 >> 4);
      uint8_t b = (b5 << 3) | (b5 >> 2);
      uint8_t gray = (r * 77 + g * 150 + b * 29) >> 8;

      gray = (gray + ((gray * gray) >> 8)) >> 1;

      bool pixelState = (gray < B8_255[destY_mod8][destX & 7]);

      renderer.drawPixel(destX, destY, pixelState);
    }
  }
}

void CbzReaderActivity::handlePngDraw(PNGDRAW *pDraw) {
  if (!pngLineBuffer) return;

  int screenW = renderer.getScreenWidth();
  int screenH = renderer.getScreenHeight();

  int srcY = pDraw->y;
  if (srcY >= decSrcH) return;

  uint32_t scaleY = (screenH << 16) / decSrcH;
  int destY = (srcY * scaleY) >> 16;
  if (destY < 0 || destY >= screenH) return;

  int lineW = pDraw->iWidth < decSrcW ? pDraw->iWidth : decSrcW;

  PNG* png = reinterpret_cast<PNG*>(pDraw->pUser);
  png->getLineAsRGB565(pDraw, pngLineBuffer.get(), PNG_RGB565_LITTLE_ENDIAN, 0xffffffff);

  int effectiveSrcW = decSplit ? (decSrcW / 2) : decSrcW;
  int srcOffset = decRight ? (decSrcW / 2) : 0;
  if (effectiveSrcW <= 0) return;

  uint32_t scaleX = (screenW << 16) / effectiveSrcW;
  int destY_mod8 = destY & 7;

  for (int srcX = 0; srcX < lineW; srcX++) {
    if (decSplit) {
      if (!decRight && srcX >= decSrcW / 2) continue;
      if (decRight && srcX < decSrcW / 2) continue;
    }

    int effectiveSrcX = srcX - srcOffset;
    int destX = (effectiveSrcX * scaleX) >> 16;
    if (destX < 0 || destX >= screenW) continue;

    uint16_t pixel = pngLineBuffer[srcX];

    uint8_t r5 = (pixel >> 11) & 0x1F;
    uint8_t g6 = (pixel >> 5) & 0x3F;
    uint8_t b5 = pixel & 0x1F;
    uint8_t r = (r5 << 3) | (r5 >> 2);
    uint8_t g = (g6 << 2) | (g6 >> 4);
    uint8_t b = (b5 << 3) | (b5 >> 2);
    uint8_t gray = (r * 77 + g * 150 + b * 29) >> 8;

    gray = (gray + ((gray * gray) >> 8)) >> 1;

    bool pixelState = (gray < B8_255[destY_mod8][destX & 7]);

    renderer.drawPixel(destX, destY, pixelState);
  }
}
