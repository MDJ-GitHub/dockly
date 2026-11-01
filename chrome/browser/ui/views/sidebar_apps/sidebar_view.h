// Copyright 2026 MDJ. Use of this source code is governed by a BSD-style
// license that can be found in the Chromium LICENSE file.

#ifndef CHROME_BROWSER_UI_VIEWS_SIDEBAR_APPS_SIDEBAR_VIEW_H_
#define CHROME_BROWSER_UI_VIEWS_SIDEBAR_APPS_SIDEBAR_VIEW_H_

#include <map>
#include <set>
#include <memory>
#include <string>
#include <vector>

#include "base/callback_list.h"
#include "base/files/file_path.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/scoped_observation.h"
#include "base/timer/timer.h"
#include "chrome/browser/ui/tabs/tab_strip_model_observer.h"
#include "chrome/browser/ui/views/sidebar_apps/sidebar_app_model.h"
#include "third_party/skia/include/core/SkBitmap.h"
#include "third_party/skia/include/core/SkColor.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/base/mojom/menu_source_type.mojom-forward.h"
#include "ui/gfx/image/image_skia.h"
#include "ui/menus/simple_menu_model.h"
#include "ui/views/context_menu_controller.h"
#include "ui/views/view.h"

class AppMenu;
class AppMenuModel;
class BrowserWindowInterface;
class Profile;

namespace content {
class WebContents;
}  // namespace content

namespace gfx {
class Canvas;
}  // namespace gfx

namespace ui {
class Event;
class MouseEvent;
}  // namespace ui

namespace views {
class FlexLayout;
class LabelButton;
class MenuRunner;
class Textfield;
}  // namespace views

namespace sidebar_apps {

class AppEntryView;
class SidebarPageEdge;

// The strip on the left edge of a browser window showing one button per saved
// sidebar app, with a badge for unread notifications.
//
// Each app is a real browser tab of this window that the tab strip does not
// draw (so permission prompts, file uploads, downloads, popups and everything
// else work as in any tab). Clicking an app's button activates its tab.
// Switching to any other tab goes back to normal browsing.
//
// One window per profile (the "host") also opens every app in a background tab
// shortly after startup, so the apps can receive notifications and calls
// before they are clicked. The host reads each app's unread count from its
// page title (for example "(3) Inbox") and publishes it through the model.
class SidebarView : public views::View,
                    public SidebarAppModel::Observer,
                    public views::ContextMenuController,
                    public ui::SimpleMenuModel::Delegate,
                    public TabStripModelObserver {
  METADATA_HEADER(SidebarView, views::View)

 public:
  SidebarView(Profile* profile, BrowserWindowInterface* browser);
  SidebarView(const SidebarView&) = delete;
  SidebarView& operator=(const SidebarView&) = delete;
  ~SidebarView() override;

  // The edge of the window the strip is on.
  SidebarPosition position() const { return model_->settings().position; }
  bool IsHorizontal() const { return IsHorizontalSidebar(position()); }
  // Called by the browser layout; see UpdateSidebarWindowButtons().
  bool UpdateWindowButtons(bool vertical_tabs);
  // See SidebarHidesCaptionButtons().
  bool HidesCaptionButtons() const {
    return window_buttons_ && position() == SidebarPosition::kRight;
  }
  // Room left free at the top of the strip; see SetSidebarTopInset().
  void SetTopInset(int top_inset);

  // views::View:
  void OnThemeChanged() override;
  void OnBoundsChanged(const gfx::Rect& previous_bounds) override;
  void AddedToWidget() override;
  void RemovedFromWidget() override;
  // Paints the window frame's color behind the window buttons (left and right
  // strips), so they look like part of the title bar and separate the apps
  // from it.
  void OnPaintBackground(gfx::Canvas* canvas) override;
  // Auto-hide: touching the thin edge makes the strip grow. While it is out, a
  // timer checks whether the mouse is still over it (buttons included) and
  // shrinks it back to the edge once the mouse has left.
  void OnMouseEntered(const ui::MouseEvent& event) override;
  void OnMouseExited(const ui::MouseEvent& event) override;

  // SidebarAppModel::Observer:
  void OnSidebarAppsChanged() override;
  void OnUnreadCountsChanged() override;
  void OnSidebarSettingsChanged() override;

  // views::ContextMenuController:
  void ShowContextMenuForViewImpl(
      views::View* source,
      const gfx::Point& point,
      ui::mojom::MenuSourceType source_type) override;

  // ui::SimpleMenuModel::Delegate:
  bool IsCommandIdEnabled(int command_id) const override;
  bool IsCommandIdChecked(int command_id) const override;
  void ExecuteCommand(int command_id, int event_flags) override;

  // TabStripModelObserver:
  void OnTabStripModelChanged(
      TabStripModel* tab_strip_model,
      const TabStripModelChange& change,
      const TabStripSelectionChange& selection) override;

 private:
  class TitleObserver;
  class FullscreenObserver;

  void RebuildButtons();
  void OnAppPressed(const std::string& app_id, const ui::Event& event);
  void OnAddPressed(const ui::Event& event);
  void OnMinimizePressed(const ui::Event& event);
  void OnMaximizePressed(const ui::Event& event);
  void OnClosePressed(const ui::Event& event);
  bool IsWindowMaximized() const;
  void OnPaintAsActiveChanged();
  // Keeps the faint rounded border on the page edge next to the strip in
  // place (see SidebarPageEdge in the .cc file).
  void UpdatePageEdge();
  void CreatePageEdge();
  // Minimize / maximize / close buttons are shown while a sidebar app is open
  // with the toolbar hidden and vertical tabs on (not for a strip on the top).
  bool WantsWindowButtons() const;
  // Adds or removes them (right after the current event) when that changed.
  void SyncWindowButtons();
  void ApplyWindowButtons();
  std::unique_ptr<views::View> CreateWindowButtons();
  // Shows the options menu (toolbar, width, icon size, auto-hide, badges,
  // which apps are enabled).
  void OnOptionsPressed(const ui::Event& event);
  // Opens the options menu at |screen_point| (a right-click on the strip), or
  // next to the options button when |screen_point| is null.
  void ShowOptionsMenu(const gfx::Point* screen_point,
                       ui::mojom::MenuSourceType source_type);
  // The user picked |app_id| in the tray menu (host window only).
  void OpenAppFromTray(const std::string& app_id);
  // Opens the form that sets after how many minutes inactive tabs sleep
  // (|for_apps| false) or sidebar apps sleep with the global timer (true).
  void ShowTabSleepDialog(bool for_apps);
  // Opens the form that edits the list of sites that never Power Sleep.
  void ShowSleepExceptionsDialog();
  // True if Power Sleep must leave the regular tab |contents| alone: it is
  // pinned (and "Ignore pinned tabs" is on) or its site is in the exception
  // list (and the list is on). Sidebar apps are never passed in here.
  bool IsSleepExempt(content::WebContents* contents, bool pinned) const;

  // Power Sleep (memory saver). Every 30 seconds apps and tabs that have been
  // off screen for longer than their sleep time are put to Power Sleep.
  void OnSleepTick();
  // An app Power Sleeps by ending its process, like a regular tab (it stays in
  // its own session) and wakes up, reloading where it was, when it is shown
  // again.
  void SleepApp(const std::string& app_id);
  void WakeApp(const std::string& app_id);
  // Puts the apps and / or the regular tabs of this window to sleep right now,
  // except what is on screen. Called for every window by SidebarTray::SleepAll().
  void SleepAll(bool sleep_apps, bool sleep_tabs);
  // Wakes a sleeping app that is on screen again (for example after the
  // window was shown again from the tray, without any tab change).
  void WakeVisibleApps();
  bool IsAppTab(const content::WebContents* contents) const;
  // Runs a command of the options menu. Returns false if |command_id| is not
  // one of them.
  bool ExecuteOptionsCommand(int command_id);
  // VPN (see sidebar_vpn.h). ApplyVpn() writes the saved VPN state to the
  // profile's proxy setting; it runs after every change and whenever the app
  // list changes (the apps are recognised by their site).
  void ApplyVpn();
  void BuildVpnMenu();
  void ExecuteVpnCommand(int command_id);
  bool IsVpnCommandChecked(int command_id) const;
  // Ad block (see sidebar_adblock.h).
  void ApplyAdblock();
  void BuildAdblockMenu();
  void ExecuteAdblockCommand(int command_id);
  bool IsAdblockCommandChecked(int command_id) const;
  // Works out the button, icon and margin sizes from the saved settings.
  void ApplyMetrics();
  // Called when the saved settings changed: new sizes, new buttons.
  void ApplySettings();
  // Sets the width of the strip: the saved width, or the thin edge while
  // auto-hidden. The browser layout reads it from GetPreferredSize().
  void UpdateWidth();
  // Hides the strip while the page (or the window) is fullscreen.
  void UpdateFullscreenHidden();
  void OnCollapseTimer();
  // True while a menu is open or a button is being dragged; the strip must
  // not collapse then.
  bool IsBusy() const;
  // Closes the tab of |app_id| and forgets what was tracked for it, without
  // touching the app's saved login.
  void CloseAppTab(const std::string& app_id);
  void CloseDisabledAppTabs();
  // Drag and drop of the app buttons, to rearrange the apps. |source| is the
  // button and |point| the mouse position in the button's coordinates.
  void BeginEntryDrag(const std::string& app_id,
                      views::View* source,
                      const gfx::Point& point);
  void UpdateEntryDrag(const std::string& app_id,
                       views::View* source,
                       const gfx::Point& point);
  void EndEntryDrag(const std::string& app_id,
                    views::View* source,
                    const gfx::Point& point);
  void ApplyDraggedOrder(const std::string& app_id, size_t index);
  // Shows the browser's three-dot menu next to the button.
  void OnSettingsPressed(const ui::Event& event);
  // Shows the "Add app" form (|app_id| empty) or the "Edit app" form.
  // For a new app, |prefill_url| and |prefill_name| fill in the form, and
  // |prefill_icon| (the icon of the page it was taken from) is shown as the
  // app's icon until the app's own page brings its icon.
  void ShowAppDialog(const std::string& app_id,
                     const GURL& prefill_url = GURL(),
                     const std::string& prefill_name = std::string(),
                     const gfx::ImageSkia& prefill_icon = gfx::ImageSkia());
  // Called when the form's Save button is pressed. Returns false to keep the
  // form open (for example when the address is not valid).
  // |icon_source| is a PNG picked in the form (empty = unchanged) and
  // |reset_icon| asks to go back to the site's own icon.
  bool OnAppDialogAccepted(views::Textfield* name_field,
                           views::Textfield* url_field,
                           views::Textfield* sleep_field,
                           const base::FilePath& icon_source,
                           bool reset_icon,
                           const std::string& app_id);
  // Custom icons: a PNG chosen by the user, copied into the profile folder
  // (SidebarIcons/<app id>.png) so it survives restarts. It always wins over
  // the icon of the site.
  base::FilePath CustomIconPath(const std::string& app_id) const;
  void SetCustomIconFromFile(const std::string& app_id,
                             const base::FilePath& source);
  void LoadCustomIcon(const std::string& app_id);
  void OnCustomIconLoaded(const std::string& app_id,
                          bool report_failure,
                          const SkBitmap& bitmap);
  void RemoveCustomIcon(const std::string& app_id);
  // Saves the icon the app shows right now as its custom icon, so the site's
  // later icon changes (for example a notification dot) are not followed.
  void KeepCurrentIcon(const std::string& app_id);
  // Saves |icon| to disk as the custom icon of |app_id| and shows it. From
  // then on the site's own icon is no longer followed.
  void SaveIconAsCustom(const std::string& app_id, const gfx::ImageSkia& icon);
  void ApplyThemeToAddButton();
  // Shows the "Add app" form filled in with the address of the active tab and
  // a free name (for example "Discord 2"). The tab itself is not touched;
  // saving the form adds a separate app with its own session.
  void AddCurrentPageAsApp();
  // Apps removed from the model: show their tab again as an ordinary tab and
  // forget everything we tracked for them.
  void PruneRemovedApps();
  void OnActiveTabChanged(BrowserWindowInterface* browser);
  // Session restore (after a restart or a crash) brings back the tabs of the
  // apps as ordinary tabs of the normal session. Those old copies are closed,
  // because the apps are started fresh in their own sessions.
  void SweepRestoredAppTabs();

  // Activates the tab of |app|, opening it first if needed.
  void OpenOrActivateApp(const SidebarApp& app);
  // Opens |app| in a new tab in the app's own storage partition (its own
  // login), in the foreground or the background, and hides that tab from the
  // tab strip.
  void OpenAppTab(const SidebarApp& app, bool foreground);
  // Remembers |contents| as the tab of |app|, hides it from the tab strip and
  // starts watching its title for an unread count.
  void TrackAppTab(const SidebarApp& app, content::WebContents* contents);

  // Background startup of the apps (host window only).
  void ScheduleAppLaunch();
  void LaunchMissingApps();

  void OnUnreadCountParsed(const std::string& app_id, int count);
  void UpdateBadges();

  // Shows the site's favicon on the app's button once the page has one.
  void OnAppTabUiChanged(const std::string& app_id);

  // Works out which app (if any) owns the active tab and updates the buttons.
  void UpdateActiveApp();
  // Highlights the button of the app that is currently shown.
  void UpdateActiveIndicator();

  raw_ptr<BrowserWindowInterface> browser_;
  raw_ptr<Profile> profile_;
  raw_ptr<SidebarAppModel> model_;
  std::string active_app_id_;
  // The app whose button is being dragged ("" = none) and where on the button
  // it was grabbed, measured from its top.
  std::string drag_app_id_;
  int drag_grab_offset_ = 0;
  bool is_app_host_ = false;
  // Sizes derived from the settings (see ApplyMetrics()).
  int button_size_ = 40;
  int icon_size_ = 24;
  int bottom_button_size_ = 32;
  // Space left and right of the buttons in a column (strip on the left or
  // right). The window buttons ignore it and use the full width.
  int side_margin_ = 6;
  // Auto-hide state.
  int current_width_ = 0;
  // Window buttons (see WantsWindowButtons()).
  bool vertical_tabs_ = false;
  // Whether the window frame was last told to hide its caption buttons.
  bool caption_buttons_hidden_ = false;
  // Sleep state. |app_last_active_| and |tab_last_visible_| (keyed by the
  // address of the tab's contents, never dereferenced) are the last time the
  // app / tab was seen on screen, |sleeping_apps_| the apps asleep and
  // |sleep_urls_| is no longer used (apps reload where they were).
  base::RepeatingTimer sleep_timer_;
  base::RepeatingTimer wake_timer_;
  base::CallbackListSubscription sleep_all_subscription_;
  std::map<std::string, base::TimeTicks> app_last_active_;
  std::set<std::string> sleeping_apps_;
  std::map<std::string, GURL> sleep_urls_;
  std::map<uintptr_t, base::TimeTicks> tab_last_visible_;
  base::CallbackListSubscription paint_as_active_subscription_;
  int top_inset_ = 0;
  // Owned by the parent view, which may be destroyed before this view.
  raw_ptr<SidebarPageEdge, DanglingUntriaged> page_edge_ = nullptr;
  bool page_edge_pending_ = false;
  // Follows the page's position (the toolbar showing or hiding moves it).
  base::RepeatingTimer page_edge_timer_;
  bool window_buttons_sync_pending_ = false;
  raw_ptr<views::View> window_buttons_ = nullptr;
  raw_ptr<views::LabelButton> maximize_button_ = nullptr;
  // How many children come before the app buttons (the window buttons).
  int leading_children_ = 0;
  SidebarPosition current_position_ = SidebarPosition::kLeft;
  bool collapsed_ = false;
  // True while something is fullscreen: the strip takes no room and is hidden.
  bool hidden_for_fullscreen_ = false;
  std::unique_ptr<FullscreenObserver> fullscreen_observer_;
  bool hovered_ = false;
  base::OneShotTimer collapse_timer_;
  raw_ptr<views::FlexLayout> layout_ = nullptr;
  // The entries currently in the strip, by app id. Children are owned by the
  // view hierarchy; this map is cleared before they are removed.
  std::map<std::string, raw_ptr<AppEntryView>> entries_;
  // The tab opened for each app in this window, by app id.
  std::map<std::string, base::WeakPtr<content::WebContents>> app_tabs_;
  // Favicons seen so far, by app id, so rebuilt buttons can show them again.
  std::map<std::string, gfx::ImageSkia> app_icons_;
  // The icon the open "Add sidebar app" form was prefilled with (null if none).
  gfx::ImageSkia pending_prefill_icon_;
  // The address the form was prefilled with ("Add the current page"). If it is
  // still the same when the app is added, the prefill icon is kept for good.
  GURL pending_prefill_url_;
  // Icons chosen by the user, by app id. These replace the site's icon.
  std::map<std::string, gfx::ImageSkia> custom_icons_;
  // Tells us when an app tab's favicon may have changed.
  std::map<std::string, base::CallbackListSubscription> favicon_subscriptions_;
  // Watches each app tab's title for an unread count (host window only).
  std::map<std::string, std::unique_ptr<TitleObserver>> title_observers_;
  raw_ptr<views::LabelButton> add_button_ = nullptr;
  raw_ptr<views::LabelButton> settings_button_ = nullptr;
  raw_ptr<views::LabelButton> options_button_ = nullptr;
  // The browser menu of the gear button. The menu must be destroyed before the
  // model, so the model is declared first.
  std::unique_ptr<AppMenuModel> app_menu_model_;
  std::unique_ptr<AppMenu> app_menu_;
  // The right-click menu of an app button, and the app it was opened for.
  std::unique_ptr<ui::SimpleMenuModel> context_menu_model_;
  std::unique_ptr<views::MenuRunner> context_menu_runner_;
  std::string context_menu_app_id_;
  // The options menu of the options button. The submenus are declared before
  // the menu that points to them, and the runner after everything it shows,
  // so they are destroyed in the right order.
  std::unique_ptr<ui::SimpleMenuModel> position_menu_model_;
  std::unique_ptr<ui::SimpleMenuModel> width_menu_model_;
  std::unique_ptr<ui::SimpleMenuModel> icon_menu_model_;
  std::unique_ptr<ui::SimpleMenuModel> apps_menu_model_;
  std::unique_ptr<ui::SimpleMenuModel> sleep_menu_model_;
  std::unique_ptr<ui::SimpleMenuModel> do_not_disturb_menu_model_;
  std::unique_ptr<ui::SimpleMenuModel> vpn_country_menu_model_;
  std::unique_ptr<ui::SimpleMenuModel> vpn_server_menu_model_;
  std::unique_ptr<ui::SimpleMenuModel> vpn_menu_model_;
  // The site of the active tab (see vpn::SiteKeyForUrl()), for the "Add /
  // Remove ... VPN list" item; empty if the tab is not a web page.
  std::string vpn_current_site_;
  std::unique_ptr<ui::SimpleMenuModel> adblock_menu_model_;
  // Site of the active tab, for the "Add / Remove ... ad block list" item.
  std::string adblock_current_site_;
  // Site of the active tab, for the "Add / Remove ... Power Sleep exceptions"
  // item; empty if the tab is not a web page.
  std::string sleep_current_site_;
  std::unique_ptr<ui::SimpleMenuModel> options_menu_model_;
  std::unique_ptr<views::MenuRunner> options_menu_runner_;
  // The apps listed in the "Enabled apps" submenu, in menu order.
  std::vector<std::string> options_app_ids_;
  // Tabs put back by session restore, waiting to be looked at once the app
  // list is known.
  std::vector<base::WeakPtr<content::WebContents>> restored_tabs_;
  base::OneShotTimer launch_timer_;
  base::CallbackListSubscription active_tab_subscription_;
  base::ScopedObservation<SidebarAppModel, SidebarAppModel::Observer>
      model_observation_{this};
  base::WeakPtrFactory<SidebarView> weak_factory_{this};
};

}  // namespace sidebar_apps

#endif  // CHROME_BROWSER_UI_VIEWS_SIDEBAR_APPS_SIDEBAR_VIEW_H_
