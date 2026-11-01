// Copyright 2026 MDJ. Use of this source code is governed by a BSD-style
// license that can be found in the Chromium LICENSE file.

#ifndef CHROME_BROWSER_UI_VIEWS_SIDEBAR_APPS_POWER_SLEEP_H_
#define CHROME_BROWSER_UI_VIEWS_SIDEBAR_APPS_POWER_SLEEP_H_

#include "ui/base/models/image_model.h"
#include "ui/gfx/image/image_skia.h"
#include "ui/gfx/image/image_skia_operations.h"

namespace content {
class WebContents;
}  // namespace content

// Power Sleep: a tab (regular tab or sidebar app) that has no renderer process
// and no page loaded. It either had its process ended on purpose, like "End
// process" in the Task Manager, or it was restored at startup and never
// started. Either way the tab is marked (MarkPowerSlept) so the browser knows
// this is NOT a crash:
//  - SadTabHelper does not show "Aw, Snap!" for it,
//  - TabUIHelper does not give it the dead-tab icon (it shows the page's own
//    icon, in greyscale),
//  - the tab loads by itself the moment it is shown, then it is a normal tab
//    again.
//
// The code lives in chrome/browser/ui/views/tabs/dockium_hidden_tabs.cc, which
// is already part of the build and callable from every place that needs this.
namespace sidebar_apps {

// True if |contents| is marked as Power Sleep.
bool IsPowerSlept(content::WebContents* contents);

// Marks |contents| as Power Sleep and starts watching it: it loads the moment
// it is shown, and the mark goes away once its page is back. Does nothing if
// it is already marked.
void MarkPowerSlept(content::WebContents* contents);

void ClearPowerSlept(content::WebContents* contents);

// Loads a Power Sleep tab now (reload after its process was ended, first load
// of a tab that never started). Happens only once per Power Sleep.
void WakePowerSleptTab(content::WebContents* contents);

// The icon of a Power Sleep tab: the page's icon without its colours.
inline ui::ImageModel PowerSleepFavicon(const ui::ImageModel& favicon) {
  if (favicon.IsEmpty() || !favicon.IsImage()) {
    return favicon;
  }
  const gfx::ImageSkia icon = favicon.GetImage().AsImageSkia();
  if (icon.isNull()) {
    return favicon;
  }
  // Saturation 0 = grey; hue and lightness stay as they are (-1).
  return ui::ImageModel::FromImageSkia(
      gfx::ImageSkiaOperations::CreateHSLShiftedImage(icon, {-1, 0, -1}));
}

}  // namespace sidebar_apps

#endif  // CHROME_BROWSER_UI_VIEWS_SIDEBAR_APPS_POWER_SLEEP_H_
