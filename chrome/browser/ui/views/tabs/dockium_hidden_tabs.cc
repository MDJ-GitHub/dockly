// Copyright 2026 MDJ. Use of this source code is governed by a BSD-style
// license that can be found in the Chromium LICENSE file.

#include "chrome/browser/ui/views/tabs/dockium_hidden_tabs.h"

#include <memory>
#include <utility>

#include "base/callback_list.h"
#include "base/functional/bind.h"
#include "base/memory/weak_ptr.h"
#include "base/no_destructor.h"
#include "base/process/kill.h"
#include "base/supports_user_data.h"
#include "base/task/sequenced_task_runner.h"
#include "chrome/browser/ui/views/sidebar_apps/power_sleep.h"
#include "components/tabs/public/tab_interface.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/reload_type.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_contents_observer.h"

namespace sidebar_apps {

namespace {

const char kHiddenAppTabKey[] = "DockiumHiddenAppTab";

// Presence of this user data on a WebContents means "hidden app tab".
class HiddenAppTabMarker : public base::SupportsUserData::Data {};

bool g_toolbar_shown_for_apps = false;

// ---- Power Sleep (see chrome/browser/ui/views/sidebar_apps/power_sleep.h) ----

const char kPowerSleepKey[] = "DockiumPowerSleep";

// Owned by the WebContents (user data). Watches a Power Sleep tab: when it is
// shown it loads it, when its page is back it removes itself.
class PowerSleepMarker : public base::SupportsUserData::Data,
                         public content::WebContentsObserver {
 public:
  // Safety limit so a page that crashes while loading cannot reload forever.
  static constexpr int kMaxLoadAttempts = 5;

  explicit PowerSleepMarker(content::WebContents* contents)
      : content::WebContentsObserver(contents) {}
  ~PowerSleepMarker() override = default;

  // True once per "death" of the page: the caller then starts the load. A new
  // death of the process (PrimaryMainFrameRenderProcessGone) gives a new turn,
  // so a wake that came too early (process not reported gone yet) can never
  // use up the only chance.
  bool TakeLoadTurn() {
    if (!load_turn_available_ || load_attempts_ >= kMaxLoadAttempts) {
      return false;
    }
    load_turn_available_ = false;
    ++load_attempts_;
    return true;
  }

  // content::WebContentsObserver:
  void OnVisibilityChanged(content::Visibility visibility) override {
    if (visibility != content::Visibility::VISIBLE) {
      return;
    }
    if (!web_contents()->IsCrashed() &&
        !web_contents()->GetController().NeedsReload()) {
      return;
    }
    // Not from inside the visibility change itself.
    base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE, base::BindOnce(&PowerSleepMarker::WakeIfShown,
                                  web_contents()->GetWeakPtr()));
  }
  // The process is gone. This is a new death, so a new load turn. If the tab
  // is already on screen (the user came back before the end of the process was
  // reported, so OnVisibilityChanged() saw a live page and did nothing) it is
  // loaded now; otherwise OnVisibilityChanged() does it when it is shown.
  void PrimaryMainFrameRenderProcessGone(
      base::TerminationStatus status) override {
    load_turn_available_ = true;
    if (web_contents()->GetVisibility() != content::Visibility::VISIBLE) {
      return;
    }
    base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE, base::BindOnce(&PowerSleepMarker::WakeIfShown,
                                  web_contents()->GetWeakPtr()));
  }
  // The page is back: the tab is a normal tab again.
  void RenderViewReady() override {
    base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE, base::BindOnce(&PowerSleepMarker::Done,
                                  web_contents()->GetWeakPtr()));
  }

 private:
  static void WakeIfShown(base::WeakPtr<content::WebContents> contents) {
    if (contents && contents->GetVisibility() == content::Visibility::VISIBLE) {
      WakePowerSleptTab(contents.get());
    }
  }

  static void Done(base::WeakPtr<content::WebContents> contents) {
    if (!contents) {
      return;
    }
    // Only when the page really is alive. A RenderViewReady that was reported
    // for a process that has since been ended must not remove the mark (the
    // tab would then show "Aw, Snap!").
    content::RenderFrameHost* main_frame = contents->GetPrimaryMainFrame();
    if (contents->IsCrashed() || contents->GetController().NeedsReload() ||
        !main_frame || !main_frame->IsRenderFrameLive()) {
      return;
    }
    ClearPowerSlept(contents.get());
  }

  bool load_turn_available_ = true;
  int load_attempts_ = 0;
};

PowerSleepMarker* GetPowerSleepMarker(content::WebContents* contents) {
  return contents ? static_cast<PowerSleepMarker*>(
                        contents->GetUserData(kPowerSleepKey))
                  : nullptr;
}

base::RepeatingClosureList& GetCallbacks() {
  static base::NoDestructor<base::RepeatingClosureList> callbacks;
  return *callbacks;
}

}  // namespace

void SetHiddenAppTab(content::WebContents* contents, bool hidden) {
  if (!contents || IsHiddenAppTab(contents) == hidden) {
    return;
  }
  if (hidden) {
    contents->SetUserData(kHiddenAppTabKey,
                          std::make_unique<HiddenAppTabMarker>());
  } else {
    contents->RemoveUserData(kHiddenAppTabKey);
  }
  GetCallbacks().Notify();
}

void SetToolbarShownForApps(bool shown) {
  if (g_toolbar_shown_for_apps == shown) {
    return;
  }
  g_toolbar_shown_for_apps = shown;
  GetCallbacks().Notify();
}

bool IsToolbarShownForApps() {
  return g_toolbar_shown_for_apps;
}

bool IsPowerSlept(content::WebContents* contents) {
  return GetPowerSleepMarker(contents) != nullptr;
}

void MarkPowerSlept(content::WebContents* contents) {
  if (contents && !IsPowerSlept(contents)) {
    contents->SetUserData(kPowerSleepKey,
                          std::make_unique<PowerSleepMarker>(contents));
  }
}

void ClearPowerSlept(content::WebContents* contents) {
  if (contents) {
    contents->RemoveUserData(kPowerSleepKey);
  }
}

void WakePowerSleptTab(content::WebContents* contents) {
  PowerSleepMarker* marker = GetPowerSleepMarker(contents);
  if (!marker) {
    return;
  }
  const bool crashed = contents->IsCrashed();
  const bool needs_reload = contents->GetController().NeedsReload();
  // Nothing to load yet: the process has not been reported gone (it is still
  // dying) or the page is already loading. Do NOT use up the load turn here;
  // PrimaryMainFrameRenderProcessGone() will wake the tab when it is reported.
  if (!crashed && !needs_reload) {
    return;
  }
  if (!marker->TakeLoadTurn()) {
    return;
  }
  if (crashed) {
    contents->GetController().Reload(content::ReloadType::NORMAL,
                                     /*check_for_repost=*/false);
  } else {
    contents->GetController().LoadIfNecessary();
  }
}

bool IsHiddenAppTab(const content::WebContents* contents) {
  return contents && contents->GetUserData(kHiddenAppTabKey) != nullptr;
}

bool IsHiddenAppTab(tabs::TabInterface* tab) {
  return tab && IsHiddenAppTab(tab->GetContents());
}

base::CallbackListSubscription AddHiddenAppTabsChangedCallback(
    base::RepeatingClosure callback) {
  return GetCallbacks().Add(std::move(callback));
}

}  // namespace sidebar_apps
