// Copyright 2026 MDJ. Use of this source code is governed by a BSD-style
// license that can be found in the Chromium LICENSE file.

#ifndef CHROME_BROWSER_UI_VIEWS_SIDEBAR_APPS_SIDEBAR_APPS_CONSTANTS_H_
#define CHROME_BROWSER_UI_VIEWS_SIDEBAR_APPS_SIDEBAR_APPS_CONSTANTS_H_

#include "base/callback_list.h"
#include "base/functional/callback.h"
#include "ui/gfx/geometry/size.h"

// Kept deliberately tiny so that the browser layout code can include it
// without pulling in any heavy headers.
namespace views {
class View;
}  // namespace views

namespace sidebar_apps {

// The edge of the window the strip is attached to.
enum class SidebarPosition { kLeft = 0, kRight = 1, kTop = 2, kBottom = 3 };

inline constexpr bool IsHorizontalSidebar(SidebarPosition position) {
  return position == SidebarPosition::kTop ||
         position == SidebarPosition::kBottom;
}

// The position of the sidebar view |sidebar_view| (the view found with
// kSidebarViewId). The layout asks for its size with GetPreferredSize(): the
// width for kLeft / kRight, the height for kTop / kBottom.
SidebarPosition GetSidebarPosition(const views::View* sidebar_view);

// The browser layout tells the sidebar view |sidebar_view| whether vertical
// tabs are on and asks whether the sidebar shows its own minimize / maximize /
// close buttons. It does while a sidebar app is open, the toolbar is hidden and
// vertical tabs are on (the window's own buttons are covered by the page then),
// on every edge except the top. The buttons themselves are added right after
// the layout pass, and the layout runs again.
bool UpdateSidebarWindowButtons(views::View* sidebar_view, bool vertical_tabs);

// True while the sidebar view |sidebar_view| shows its own close / maximize /
// minimize buttons on the right edge of the window: the window frame must not
// show its own caption buttons then (they would be in the same corner).
bool SidebarHidesCaptionButtons(const views::View* sidebar_view);

// |callback| runs whenever SidebarHidesCaptionButtons() may have changed (the
// window frame then shows or hides its caption buttons again). Keep the
// subscription for as long as the callback should run.
base::CallbackListSubscription AddCaptionButtonsObserver(
    base::RepeatingClosure callback);
void NotifyCaptionButtonsObservers();

// The size of the window's own caption buttons, reported by the window frame
// (46 x 32 until it has). The sidebar's close / maximize / minimize buttons
// are as big.
void SetNativeCaptionButtonSize(const gfx::Size& size);
gfx::Size GetNativeCaptionButtonSize();

// How much room the sidebar view |sidebar_view| leaves free at its top, for the
// window's own close button when the strip is on the right. The strip itself
// reaches the top of the window, so its background shows behind that button.
void SetSidebarTopInset(views::View* sidebar_view, int top_inset);

// Thickness of the sidebar strip (its width when it is on the left or right,
// its height when it is on the top or bottom), in DIPs.
inline constexpr int kSidebarWidth = 52;

// Default size of the app icons in the strip, in DIPs.
inline constexpr int kDefaultIconSize = 24;

// Width of the thin strip that stays at the window edge while the sidebar is
// auto-hidden. Moving the mouse onto it brings the sidebar back.
inline constexpr int kAutoHideStripWidth = 6;

// View ids used to find our views among BrowserView's children.
inline constexpr int kSidebarViewId = 0x44434B31;
inline constexpr int kAppViewId = 0x44434B32;

}  // namespace sidebar_apps

#endif  // CHROME_BROWSER_UI_VIEWS_SIDEBAR_APPS_SIDEBAR_APPS_CONSTANTS_H_
