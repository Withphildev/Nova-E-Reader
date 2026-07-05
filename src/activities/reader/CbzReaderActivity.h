#pragma once
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
  void preparePage(bool goingBackward = false);
  void renderPage();
  void pageTurn(bool isForward);
  void saveProgress() const;
  void loadProgress();
  std::string getCachePath() const;

  std::string filePath;
  CbzArchive archive;
  int currentFileIndex = 0;
  bool showRightHalf = false;
  bool isLandscape = false;
  
  // Cache page dimensions
  int imgWidth = 0;
  int imgHeight = 0;

  // Heap-allocated line buffer to prevent stack overflow
  uint16_t* pngLineBuffer = nullptr;

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
