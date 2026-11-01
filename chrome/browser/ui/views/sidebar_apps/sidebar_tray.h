// Copyright 2026 MDJ. Use of this source code is governed by a BSD-style
// license that can be found in the Chromium LICENSE file.

#ifndef CHROME_BROWSER_UI_VIEWS_SIDEBAR_APPS_SIDEBAR_TRAY_H_
#define CHROME_BROWSER_UI_VIEWS_SIDEBAR_APPS_SIDEBAR_TRAY_H_

#include <memory>
#include <string>
#include <vector>

#include "base/callback_list.h"
#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/no_destructor.h"
#include "chrome/browser/status_icons/status_icon_menu_model.h"
#include "chrome/browser/status_icons/status_icon_observer.h"
#include "chrome/browser/ui/views/sidebar_apps/sidebar_app_model.h"

class BrowserWindowInterface;
class Profile;
class StatusIcon;

namespace sidebar_apps {

// The system tray icon.
//
// - It shows the total number of unread items across all sidebar apps (a dot
//   and a white number), or a white check mark when nothing is unread.
// - Left-click brings the browser window back. Right-click opens a menu with
//   "Open" and "Quit".
// - It also decides whether closing the window that hosts the sidebar apps
//   should hide it to the tray instead, so the apps keep running.
//
// There is one tray icon per process. It follows the model of the first
// regular (non-incognito) profile that shows a sidebar. The object is created
// on first use and intentionally never destroyed: the tray itself is torn down
// by the browser process at exit.
class SidebarTray : public SidebarAppModel::Observer,
                    public StatusIconObserver,
                    public StatusIconMenuModel::Delegate {
 public:
  // Creates the tray icon (once) and keeps it in sync with |model|. Later
  // calls, for other models, do nothing.
  static void AttachToModel(SidebarAppModel* model);

  static SidebarTray& Get();

  // True if a request to close |browser| should hide its window to the tray
  // instead: it is the window that hosts the sidebar apps and the browser is
  // not being quit.
  static bool ShouldHideInsteadOfClosing(BrowserWindowInterface* browser);

  // The main window is the only window with a sidebar. The first window that
  // asks becomes the main window and keeps being it while it exists; for every
  // other window this returns false. If the main window is gone, the next
  // window that asks takes over.
  static bool ClaimMainWindow(BrowserWindowInterface* browser);

  // True while a browser that was started with --start-in-tray should keep
  // its windows hidden. BrowserView::Show() and friends check this during the
  // first moments after startup; it turns false after about 20 seconds, or as
  // soon as the user opens the window from the tray.
  static bool ShouldSuppressShow();

  // Remembers a window to bring forward when the tray icon is clicked. The
  // sidebar of each regular window calls this when it is created and whenever
  // its active tab changes, so the tray always knows a recent, live window.
  void SetLastActiveWindow(base::WeakPtr<BrowserWindowInterface> window);

  // The window that holds the apps' tabs. Closing it hides it to the tray.
  void SetHostWindow(base::WeakPtr<BrowserWindowInterface> window);

  // Called with an app id when the user picks that app in the tray menu. Set
  // by the sidebar of the host window, which owns the apps' tabs.
  using OpenAppCallback = base::RepeatingCallback<void(const std::string&)>;
  void SetOpenAppCallback(OpenAppCallback callback);

  // "Sleep all": every sidebar app and / or every regular tab is put to sleep
  // right now. The sidebar of each window (see SidebarView::SleepAll()) takes
  // care of its own apps and tabs, so this reaches all windows.
  using SleepAllCallback = base::RepeatingCallback<void(bool, bool)>;
  base::CallbackListSubscription AddSleepAllCallback(SleepAllCallback callback);
  void SleepAll(bool sleep_apps, bool sleep_tabs);

  // Rebuilds the tray menu, so check items (like "Start with Windows") show
  // the current state after something outside the tray changed them.
  void RefreshMenu();

  SidebarTray(const SidebarTray&) = delete;
  SidebarTray& operator=(const SidebarTray&) = delete;
  ~SidebarTray() override;

  // SidebarAppModel::Observer:
  void OnSidebarAppsChanged() override;
  void OnUnreadCountsChanged() override;

  // StatusIconObserver:
  void OnStatusIconClicked() override;

  // StatusIconMenuModel::Delegate:
  void ExecuteCommand(int command_id, int event_flags) override;

 private:
  friend class base::NoDestructor<SidebarTray>;
  SidebarTray();

  void Attach(SidebarAppModel* model);
  void UpdateIcon();
  void CreateContextMenu();
  // Opens a new (incognito) browser window in the profile of the main window.
  void OpenNewWindow(bool incognito);
  Profile* GetProfileForNewWindow();
  // Do Not Disturb was turned on or off: the menu and the tooltip say so.
  void OnDoNotDisturbChanged();
  // Brings the window back and opens the sidebar app |app_id|.
  void OpenApp(const std::string& app_id);
  // Shows the unread count of each app in the menu again (the items stay).
  void UpdateAppMenuLabels();
  // "Dockium: 5 unread", then one line per app with unread items.
  std::u16string BuildToolTip() const;
  // The menu item text of |app|: its name, and its unread count if any.
  std::u16string AppMenuLabel(const SidebarApp& app) const;
  // Shows and activates the host window (or the last active one).
  void ShowWindow();

  // The model may be destroyed with its profile, and the status icon is
  // destroyed by the tray at exit, so neither pointer is owned or assumed to
  // outlive this object. Both are only used from model notifications.
  raw_ptr<SidebarAppModel, DanglingUntriaged> model_ = nullptr;
  raw_ptr<StatusIcon, DanglingUntriaged> icon_ = nullptr;
  base::WeakPtr<BrowserWindowInterface> last_active_window_;
  base::WeakPtr<BrowserWindowInterface> host_window_;
  base::WeakPtr<BrowserWindowInterface> main_window_;
  OpenAppCallback open_app_callback_;
  base::RepeatingCallbackList<void(bool, bool)> sleep_all_callbacks_;
  base::CallbackListSubscription do_not_disturb_subscription_;
  // The submenu of the Do Not Disturb item (the menu points to it).
  std::unique_ptr<StatusIconMenuModel> do_not_disturb_menu_;
  // The submenu of the Sleep item.
  std::unique_ptr<StatusIconMenuModel> sleep_menu_;
  // The tray menu (owned by the status icon, replaced by CreateContextMenu())
  // and the apps in it: they start at menu index |first_app_index_|.
  raw_ptr<StatusIconMenuModel, DanglingUntriaged> menu_model_ = nullptr;
  size_t first_app_index_ = 0;
  std::vector<std::string> menu_app_ids_;
  // Set when the user chose "Quit" in the tray menu.
  bool quitting_ = false;
};

}  // namespace sidebar_apps

#endif  // CHROME_BROWSER_UI_VIEWS_SIDEBAR_APPS_SIDEBAR_TRAY_H_
