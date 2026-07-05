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

  // Static pointer for decoder callbacks
  static CbzReaderActivity* activeInstance;

  // JPEG / PNG decoder callbacks
  static int jpegDrawCallback(JPEGDRAW *pDraw);
  static int pngDrawCallback(PNGDRAW *pDraw);

  void handleJpegDraw(JPEGDRAW *pDraw);
  void handlePngDraw(PNGDRAW *pDraw);
};
