// Copyright 2026 MDJ. Use of this source code is governed by a BSD-style
// license that can be found in the Chromium LICENSE file.

#ifndef CHROME_BROWSER_UI_VIEWS_SIDEBAR_APPS_SIDEBAR_APP_MODEL_H_
#define CHROME_BROWSER_UI_VIEWS_SIDEBAR_APPS_SIDEBAR_APP_MODEL_H_

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "base/files/file_path.h"
#include "base/memory/scoped_refptr.h"
#include "base/memory/weak_ptr.h"
#include "base/observer_list.h"
#include "base/observer_list_types.h"
#include "base/supports_user_data.h"
#include "base/task/sequenced_task_runner.h"
#include "chrome/browser/ui/views/sidebar_apps/sidebar_apps_constants.h"
#include "url/gurl.h"

class Profile;

namespace sidebar_apps {

// One web app shown in the sidebar.
struct SidebarApp {
  // Stable unique id (random UUID). Never changes after creation.
  std::string id;
  std::string name;
  GURL url;
  // Optional custom icon as a data: URL. Empty means "use the site favicon".
  std::string custom_icon_data_url;
  // A disabled app stays in the list (and keeps its login) but is not started
  // and shows no notifications.
  bool disabled = false;
  // Minutes off screen after which the app is put to sleep (its page is
  // unloaded to save memory and loads again when the app is opened). 0 = never.
  int sleep_minutes = 0;
};

// Look and behaviour of the strip, saved per profile with the app list.
struct SidebarSettings {
  static constexpr int kMinWidth = 40;
  static constexpr int kMaxWidth = 120;
  static constexpr int kMinIconSize = 16;
  static constexpr int kMaxIconSize = 48;
  // Longest sleep time that can be set, in minutes (one day).
  static constexpr int kMaxSleepMinutes = 1440;
  static constexpr int kDefaultTabSleepMinutes = 20;
  static constexpr int kDefaultAppSleepMinutes = 20;

  // Edge of the window the strip is attached to.
  SidebarPosition position = SidebarPosition::kLeft;
  // Thickness of the strip in DIPs: its width on the left or right, its
  // height on the top or bottom.
  int width = kSidebarWidth;
  // Size of the app icons in DIPs.
  int icon_size = kDefaultIconSize;
  // Collapse the strip to a thin edge until the mouse touches it.
  bool autohide = false;
  // Show the red unread-count badges.
  bool show_badges = true;
  // Put sidebar apps to sleep (each app has its own sleep time, 0 = never).
  bool sleep_apps_enabled = true;
  // Power Sleep inactive regular tabs after |tab_sleep_minutes| (on by
  // default: a new profile starts with automatic Power Sleep at 20 minutes).
  bool sleep_tabs_enabled = true;
  int tab_sleep_minutes = kDefaultTabSleepMinutes;
  // Global sleep time for sidebar apps. While |use_global_app_sleep| is on it
  // replaces the own sleep time of every app (even apps set to 0 = never).
  bool use_global_app_sleep = false;
  int app_sleep_minutes = kDefaultAppSleepMinutes;
  // Pinned regular tabs never go to Power Sleep (sidebar apps are not pinned
  // tabs and are not affected by this).
  bool ignore_pinned_tabs = false;
  // While |sleep_exceptions_enabled| is on, tabs on the sites in
  // |sleep_exception_sites| (keys from adblock::SiteKeyForUrl) never go to
  // Power Sleep.
  bool sleep_exceptions_enabled = false;
  std::vector<std::string> sleep_exception_sites;

  // The minutes after which |app| goes to sleep (0 = never).
  int EffectiveAppSleepMinutes(const SidebarApp& app) const {
    return use_global_app_sleep ? app_sleep_minutes : app.sleep_minutes;
  }

  // Brings out-of-range values (for example from a hand-edited file) back into
  // the allowed range.
  void Clamp();
  bool operator==(const SidebarSettings&) const = default;
};

// The saved list of sidebar apps for one profile. Stored as JSON in
// <profile dir>/SidebarApps.json. Shared by all browser windows of the profile.
// Lives as user data on the Profile, so it is destroyed with the profile.
//
// All file access happens on a background sequence: the UI thread is not
// allowed to block. The list is therefore empty right after creation and
// observers are notified (OnSidebarAppsChanged) once the file has been read.
class SidebarAppModel : public base::SupportsUserData::Data {
 public:
  class Observer : public base::CheckedObserver {
   public:
    virtual void OnSidebarAppsChanged() = 0;
    // Called when the unread count of any app changes.
    virtual void OnUnreadCountsChanged() {}
    // Called when the width, icon size, auto-hide or badge setting changes.
    virtual void OnSidebarSettingsChanged() {}
  };

  // Returns the model for |profile|, creating it and starting the load on
  // first use.
  static SidebarAppModel* GetForProfile(Profile* profile);

  explicit SidebarAppModel(const base::FilePath& profile_dir);
  SidebarAppModel(const SidebarAppModel&) = delete;
  SidebarAppModel& operator=(const SidebarAppModel&) = delete;
  ~SidebarAppModel() override;

  const std::vector<SidebarApp>& apps() const { return apps_; }
  const SidebarApp* FindApp(const std::string& id) const;

  // Adds an app and returns its new id, or an empty string if |url| is not a
  // valid http(s) URL.
  std::string AddApp(const std::string& name,
                     const GURL& url,
                     const std::string& custom_icon_data_url,
                     int sleep_minutes = 0);

  // Replaces the name, url and icon of the app with |app.id|. Returns false if
  // there is no such app or the url is invalid.
  bool UpdateApp(const SidebarApp& app);

  // Returns false if there is no app with |id|.
  bool RemoveApp(const std::string& id);

  // Enables or disables the app with |id|. Disabling also clears its unread
  // count. Returns false if there is no such app.
  bool SetAppDisabled(const std::string& id, bool disabled);

  const SidebarSettings& settings() const { return settings_; }
  // Clamps and saves |settings| and tells the observers if anything changed.
  void SetSettings(SidebarSettings settings);

  // Moves the app with |id| to position |new_index| (counted in the list as it
  // is after the move; too large values mean "last"). Returns false if there
  // is no such app.
  bool MoveApp(const std::string& id, size_t new_index);

  void AddObserver(Observer* observer);
  void RemoveObserver(Observer* observer);

  // Unread notification counts, reported by the window that hosts the apps and
  // shown as badges. Not saved to disk. Counts are never negative.
  void SetUnreadCount(const std::string& app_id, int count);
  int GetUnreadCount(const std::string& app_id) const;
  int GetTotalUnreadCount() const;

  // Only one window per profile starts the apps in the background at launch.
  // Returns true if |host| is (or has just become) that window.
  bool TryClaimAppHost(const void* host);
  void ReleaseAppHost(const void* host);

 private:
  // Starts reading the file on the background sequence.
  void Load();
  // Runs on the UI thread with the file contents (nullopt if unreadable).
  void OnFileRead(std::optional<std::string> contents);
  // Serializes the list and writes it on the background sequence.
  void Save();
  void NotifyChanged();
  void NotifyUnreadChanged();
  void NotifySettingsChanged();

  const base::FilePath file_path_;
  std::vector<SidebarApp> apps_;
  SidebarSettings settings_;
  base::ObserverList<Observer> observers_;
  // App id -> unread count. Apps without an entry have no unread items.
  std::map<std::string, int> unread_counts_;
  // All file reads and writes run on this sequence, in order.
  scoped_refptr<base::SequencedTaskRunner> io_task_runner_;
  // Identity of the window that hosts the background apps (0 = none). Only
  // compared, never dereferenced.
  uintptr_t app_host_ = 0;

  // Must be the last member.
  base::WeakPtrFactory<SidebarAppModel> weak_factory_{this};
};

}  // namespace sidebar_apps

#endif  // CHROME_BROWSER_UI_VIEWS_SIDEBAR_APPS_SIDEBAR_APP_MODEL_H_
