#pragma once
#include <memory>
#include <string>
#include "activities/Activity.h"
#include "CbzArchive.h"
#include <JPEGDEC.h>
#include <PNGdec.h>

class CbzReaderActivity final : public Activity {
 public:
  CbzReaderActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string filePath);
  ~CbzReaderActivity() = default;

  void onEnter() override;
  void onExit() override;
  void loop() override;

 private:
  // --- Page display flow ---
  // Render the view described by (currentFileIndex, showRightHalf).
  // enterFromEnd: arriving from the following page, land on the page's LAST
  // view (right half for landscape pages).
  void showCurrentView(bool enterFromEnd);
  void renderAndCacheCurrentView();
  void pageTurn(bool isForward);
  void finishRender();  // overlays (hints + page number) + display

  // --- Temp file & decoding ---
  bool ensureTempPage(int fileIndex);
  bool decodeViewToFramebuffer(int fileIndex, bool rightHalf);

  // --- Pre-dithered page cache (.crosspoint/cbz_<hash>/pages/) ---
  std::string cachedViewPath(int fileIndex, bool rightHalf) const;
  bool tryLoadCachedView(int fileIndex, bool rightHalf);
  bool saveCachedView(int fileIndex, bool rightHalf);

  // --- Progress ---
  void saveProgress() const;
  void loadProgress();
  std::string getCachePath() const;

  std::string filePath;
  CbzArchive archive;
  int currentFileIndex = 0;
  bool showRightHalf = false;
  bool isLandscape = false;  // layout of the page currently DISPLAYED
  int pagesSinceSave = 0;    // debounce counter for periodic progress saves

  // Describes the image currently extracted to the temp file.
  struct TempPageInfo {
    int index = -1;  // page index the temp file holds (-1 = none)
    int width = 0;
    int height = 0;
    bool png = false;
    bool landscape = false;
  } tempInfo;

  // Decode context for the draw callbacks. Set immediately before each decode
  // — the callbacks must never read display state (isLandscape/showRightHalf) directly.
  int decSrcW = 0;   // decoder output width (after JPEG DCT scaling)
  int decSrcH = 0;   // decoder output height
  bool decSplit = false;  // landscape page shown as two halves
  bool decRight = false;  // decoding the right half

  // Heap-allocated line buffer to prevent stack overflow (PNG decode only)
  std::unique_ptr<uint16_t[]> pngLineBuffer;

  // Static pointer for decoder callbacks
  static CbzReaderActivity* activeInstance;

  // JPEG / PNG decoder callbacks
  static int jpegDrawCallback(JPEGDRAW *pDraw);
  static int pngDrawCallback(PNGDRAW *pDraw);

  void handleJpegDraw(JPEGDRAW *pDraw);
  void handlePngDraw(PNGDRAW *pDraw);

  // File system callbacks for streaming decoding from SD card
  static void* cbzFileOpen(const char *szFilename, int32_t *pFileSize);
  static void cbzFileClose(void *pHandle);
  static int32_t cbzJpegRead(JPEGFILE *pFile, uint8_t *pBuf, int32_t iLen);
  static int32_t cbzJpegSeek(JPEGFILE *pFile, int32_t iPosition);
  static int32_t cbzPngRead(PNGFILE *pFile, uint8_t *pBuf, int32_t iLen);
  static int32_t cbzPngSeek(PNGFILE *pFile, int32_t iPosition);
};
