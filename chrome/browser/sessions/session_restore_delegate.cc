// Copyright 2015 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/sessions/session_restore_delegate.h"

#include <stddef.h>

#include <array>
#include <utility>

#include "base/compiler_specific.h"
#include "base/functional/bind.h"
#include "base/logging.h"
#include "base/memory/weak_ptr.h"
#include "base/metrics/field_trial.h"
#include "base/strings/string_util.h"
#include "base/task/sequenced_task_runner.h"
#include "chrome/browser/performance_manager/public/background_tab_loading_policy.h"
#include "chrome/browser/sessions/session_restore_stats_collector.h"
#include "chrome/browser/ui/views/sidebar_apps/power_sleep.h"
#include "chrome/browser/ui/views/tabs/dockium_hidden_tabs.h"
#include "chrome/common/url_constants.h"
#include "components/favicon/content/content_favicon_driver.h"
#include "components/tab_groups/tab_group_id.h"
#include "components/tab_groups/tab_group_visual_data.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/navigation_entry.h"
#include "content/public/browser/web_contents.h"

namespace {

bool IsInternalPage(const GURL& url) {
  // There are many chrome:// UI URLs, but only look for the ones that users
  // are likely to have open. Most of the benefit is from the NTP URL.
  const auto kReloadableUrlPrefixes = std::to_array<const char*>({
      chrome::kChromeUIDownloadsURL,
      chrome::kChromeUIHistoryURL,
      chrome::kChromeUINewTabURL,
      chrome::kChromeUISettingsURL,
  });
  // Prefix-match against the table above.
  for (const char* prefix : kReloadableUrlPrefixes) {
    if (base::StartsWith(url.spec(), prefix)) {
      return true;
    }
  }
  return false;
}

// The address of a restored tab. A restored tab that has not started has no
// committed page yet (GetLastCommittedURL() is empty for it), so this reads the
// address from the restored navigation entry instead.
GURL RestoredTabUrl(content::WebContents* contents) {
  content::NavigationController& controller = contents->GetController();
  // Try every place the restored address can be, the first one that has it
  // wins.
  content::NavigationEntry* candidates[] = {
      controller.GetLastCommittedEntry(),
      controller.GetVisibleEntry(),
      controller.GetEntryAtIndex(controller.GetLastCommittedEntryIndex()),
      controller.GetEntryAtIndex(controller.GetCurrentEntryIndex()),
  };
  for (content::NavigationEntry* entry : candidates) {
    if (entry && entry->GetURL().is_valid() && !entry->IsInitialEntry()) {
      return entry->GetURL();
    }
  }
  return contents->GetVisibleURL();
}

// Asks the favicon service for the saved icon of |url| for a tab that is not
// started (it is only a lookup in the saved icons, nothing is loaded from the
// web). The tab shows it, in grey, instead of the generic globe.
void FetchRestoredFavicon(base::WeakPtr<content::WebContents> contents,
                          GURL url) {
  if (!contents) {
    return;
  }
  if (favicon::ContentFaviconDriver* driver =
          favicon::ContentFaviconDriver::FromWebContents(contents.get())) {
    driver->FetchFavicon(url, /*is_same_document=*/false);
  }
}

}  // namespace

SessionRestoreDelegate::RestoredTab::RestoredTab(
    content::WebContents* contents,
    bool is_active,
    bool is_app,
    bool is_pinned,
    const std::optional<tab_groups::TabGroupId>& group,
    const std::optional<split_tabs::SplitTabId>& split)
    : contents_(contents->GetWeakPtr()),
      is_active_(is_active),
      is_app_(is_app),
      is_internal_page_(IsInternalPage(contents->GetLastCommittedURL())),
      is_pinned_(is_pinned),
      group_(group),
      split_(split) {}

SessionRestoreDelegate::RestoredTab::RestoredTab(const RestoredTab&) = default;

SessionRestoreDelegate::RestoredTab&
SessionRestoreDelegate::RestoredTab::operator=(const RestoredTab&) = default;

SessionRestoreDelegate::RestoredTab::~RestoredTab() = default;

bool SessionRestoreDelegate::RestoredTab::operator<(
    const RestoredTab& right) const {
  // Tab with internal web UI like NTP or Settings are good choices to
  // defer loading.
  if (is_internal_page_ != right.is_internal_page_)
    return !is_internal_page_;
  // Pinned tabs should be loaded first.
  if (is_pinned_ != right.is_pinned_)
    return is_pinned_;
  // Apps should be loaded before normal tabs.
  if (is_app_ != right.is_app_)
    return is_app_;
  // Finally, older tabs should be deferred first.
  return contents_->GetLastActiveTimeTicks() >
         right.contents_->GetLastActiveTimeTicks();
}

// static
void SessionRestoreDelegate::RestoreTabs(
    const std::vector<RestoredTab>& tabs,
    const base::TimeTicks& restore_started) {
  if (tabs.empty())
    return;

  // Power Sleep: every restored web page that is not on screen starts as a
  // sleeping husk. It is never started here, so it uses no memory and no CPU;
  // it shows its title and its icon in grey, and it loads the moment it is
  // opened. Only the tab on screen of each window (and tabs that are not web
  // pages, like the New Tab page) are loaded now.
  size_t sleeping_tabs = 0;
  std::vector<RestoredTab> tabs_to_load;
  std::vector<content::WebContents*> web_contents_vector;
  tabs_to_load.reserve(tabs.size());
  web_contents_vector.reserve(tabs.size());
  for (const auto& tab : tabs) {
    CHECK(tab.contents());
    content::WebContents* contents = tab.contents();
    const GURL url = RestoredTabUrl(contents);
    const bool hidden_app = sidebar_apps::IsHiddenAppTab(contents);
    // (is_app() is set for every tab in this browser, so it is not used.)
    if (!tab.is_active() && !hidden_app && url.SchemeIsHTTPOrHTTPS()) {
      sidebar_apps::MarkPowerSlept(contents);
      // A moment later, when the tab's helpers are surely there.
      base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
          FROM_HERE, base::BindOnce(&FetchRestoredFavicon,
                                    contents->GetWeakPtr(), url));
      ++sleeping_tabs;
      continue;
    }
    tabs_to_load.push_back(tab);
    web_contents_vector.push_back(contents);
  }
  LOG(INFO) << "Power Sleep restore: " << sleeping_tabs
            << " tab(s) started asleep, " << tabs_to_load.size()
            << " tab(s) loaded.";
  if (tabs_to_load.empty())
    return;

  SessionRestoreStatsCollector::GetOrCreateInstance(
      restore_started,
      std::make_unique<
          SessionRestoreStatsCollector::UmaStatsReportingDelegate>())
      ->TrackTabs(tabs_to_load);

  performance_manager::policies::ScheduleLoadForRestoredTabs(
      std::move(web_contents_vector));
}
