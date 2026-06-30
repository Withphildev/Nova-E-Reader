#include "BootActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include "fontIds.h"
#include "images/BootNovaWakeUp.h"

void BootActivity::onEnter() {
  Activity::onEnter();

  const auto orig_orientation = renderer.getOrientation();
  renderer.setOrientation(GfxRenderer::Orientation::LandscapeCounterClockwise);

  renderer.clearScreen();
  renderer.drawImage(BootNovaWakeUpImage, 0, 0, 800, 480);

  // Subtle booting info near the bottom center of the landscape screen
  renderer.drawCenteredText(SMALL_FONT_ID, 440, tr(STR_BOOTING));
  renderer.drawCenteredText(SMALL_FONT_ID, 460, CROSSPOINT_VERSION);

  renderer.displayBuffer();
  renderer.setOrientation(orig_orientation);
}
