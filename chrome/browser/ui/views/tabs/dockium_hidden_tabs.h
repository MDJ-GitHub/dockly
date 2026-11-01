// Copyright 2026 MDJ. Use of this source code is governed by a BSD-style
// license that can be found in the Chromium LICENSE file.

#ifndef CHROME_BROWSER_UI_VIEWS_TABS_DOCKIUM_HIDDEN_TABS_H_
#define CHROME_BROWSER_UI_VIEWS_TABS_DOCKIUM_HIDDEN_TABS_H_

#include "base/callback_list.h"
#include "base/functional/callback.h"

namespace content {
class WebContents;
}  // namespace content

namespace tabs {
class TabInterface;
}  // namespace tabs

// Lets the sidebar mark some tabs as "app tabs" that the tab strip must not
// show. The tab stays a normal tab of the window (so permission prompts,
// uploads, downloads, etc. all work); only the strip hides it.
//
// This lives next to the tab strip code on purpose: the strip needs to call it,
// and the sidebar (in a different build target) calls it too.
namespace sidebar_apps {

// Marks or unmarks the tab showing |contents|. Notifies the tab strips when
// the state actually changes.
void SetHiddenAppTab(content::WebContents* contents, bool hidden);

bool IsHiddenAppTab(const content::WebContents* contents);

// Convenience for the tab strip. |tab| may be null (returns false).
bool IsHiddenAppTab(tabs::TabInterface* tab);

// While a hidden app tab is shown, the window normally has no toolbar and no
// bookmark bar. The sidebar has a button that sets this to true to bring them
// back (and to false to hide them again). Notifies like the calls above.
void SetToolbarShownForApps(bool shown);
bool IsToolbarShownForApps();

// Runs |callback| whenever any tab becomes hidden or visible again.
base::CallbackListSubscription AddHiddenAppTabsChangedCallback(
    base::RepeatingClosure callback);

}  // namespace sidebar_apps

#endif  // CHROME_BROWSER_UI_VIEWS_TABS_DOCKIUM_HIDDEN_TABS_H_
