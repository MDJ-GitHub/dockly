// Copyright 2026 MDJ. Use of this source code is governed by a BSD-style
// license that can be found in the Chromium LICENSE file.

#include "chrome/browser/ui/views/sidebar_apps/sidebar_view.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <set>
#include <cstdint>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "base/files/file_util.h"
#include "base/functional/bind.h"
#include "base/functional/callback.h"
#include "base/functional/callback_helpers.h"
#include "base/logging.h"
#include "base/no_destructor.h"
#include "base/strings/escape.h"
#include "base/timer/timer.h"
#include "base/values.h"
#include "content/public/browser/render_frame_host.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_split.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "ui/base/base_window.h"
#include "chrome/browser/ui/views/sidebar_apps/sidebar_do_not_disturb.h"
#include "content/public/browser/devtools_agent_host.h"
#include "content/public/browser/render_process_host.h"
#include "content/public/browser/web_contents_user_data.h"
#include "content/public/common/result_codes.h"
#include "content/public/common/url_constants.h"
#include "content/public/browser/visibility.h"
#include "url/url_constants.h"
#include "ui/gfx/color_utils.h"
#include "base/task/sequenced_task_runner.h"
#include "base/task/single_thread_task_runner.h"
#include "base/task/task_traits.h"
#include "base/task/thread_pool.h"
#include "base/time/time.h"
#include "cc/paint/paint_flags.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/common/chrome_isolated_world_ids.h"
#include "chrome/browser/sessions/session_restore.h"
#include "chrome/browser/ui/toolbar/app_menu_model.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/toolbar/app_menu.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/color/chrome_color_id.h"
#include "chrome/browser/favicon/favicon_utils.h"
#include "chrome/browser/ui/tab_ui_helper.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/views/sidebar_apps/power_sleep.h"
#include "chrome/browser/ui/views/sidebar_apps/sidebar_apps_constants.h"
#include "chrome/browser/ui/views/sidebar_apps/sidebar_autostart.h"
#include "chrome/browser/ui/views/sidebar_apps/sidebar_tray.h"
#include "chrome/browser/ui/views/sidebar_apps/sidebar_adblock.h"
#include "chrome/browser/ui/views/sidebar_apps/sidebar_vpn.h"
#include "chrome/browser/ui/views/tabs/dockium_hidden_tabs.h"
#include "components/tabs/public/tab_interface.h"
#include "components/constrained_window/constrained_window_views.h"
#include "content/public/browser/browser_context.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/navigation_handle.h"
#include "content/public/browser/page_navigator.h"
#include "content/public/browser/reload_type.h"
#include "content/public/browser/site_instance.h"
#include "content/public/browser/storage_partition_config.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_contents_observer.h"
#include "net/base/registry_controlled_domains/registry_controlled_domain.h"
#include "skia/ext/image_operations.h"
#include "third_party/skia/include/core/SkColor.h"
#include "third_party/skia/include/core/SkClipOp.h"
#include "third_party/skia/include/core/SkPath.h"
#include "third_party/skia/include/core/SkPathBuilder.h"
#include "third_party/skia/include/core/SkRRect.h"
#include "ui/gfx/geometry/rect_f.h"
#include "ui/gfx/geometry/skia_conversions.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/color/color_id.h"
#include "ui/color/color_provider.h"
#include "ui/base/models/image_model.h"
#include "ui/base/mojom/menu_source_type.mojom.h"
#include "ui/compositor/layer.h"
#include "ui/base/page_transition_types.h"
#include "ui/base/ui_base_types.h"
#include "ui/base/window_open_disposition.h"
#include "ui/events/event.h"
#include "ui/shell_dialogs/select_file_dialog.h"
#include "ui/shell_dialogs/select_file_policy.h"
#include "ui/shell_dialogs/selected_file_info.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/codec/png_codec.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/gfx/geometry/point_f.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/gfx/geometry/size.h"
#include "ui/gfx/font_list.h"
#include "ui/gfx/text_constants.h"
#include "ui/gfx/image/image_skia.h"
#include "ui/gfx/image/image_skia_operations.h"
#include "ui/gfx/image/image_skia_rep.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/background.h"
#include "ui/views/border.h"
#include "ui/views/controls/button/button.h"
#include "ui/views/controls/button/checkbox.h"
#include "ui/views/controls/button/label_button.h"
#include "ui/views/controls/button/md_text_button.h"
#include "ui/views/controls/label.h"
#include "ui/views/controls/textarea/textarea.h"
#include "ui/views/controls/textfield/textfield.h"
#include "ui/views/controls/textfield/textfield_controller.h"
#include "ui/views/controls/menu/menu_runner.h"
#include "ui/views/controls/menu/menu_types.h"
#include "ui/views/layout/flex_layout.h"
#include "ui/views/layout/flex_layout_types.h"
#include "ui/views/layout/layout_types.h"
#include "ui/views/style/typography.h"
#include "ui/views/view_class_properties.h"
#include "ui/views/window/dialog_delegate.h"
#include "url/gurl.h"
#include "url/origin.h"

namespace sidebar_apps {

namespace {

// The sizes of the buttons and icons depend on the saved settings; see
// SidebarView::ApplyMetrics().
constexpr int kBadgeHeight = 16;
// How long the mouse must have left an auto-hidden strip before it collapses.
constexpr base::TimeDelta kAutoHideDelay = base::Milliseconds(500);
// The sidebar's window buttons are never smaller than this along the strip (the
// size of the window's own buttons is used when it is larger): the height of a
// button in a column, the width of a button in a row.
constexpr int kMinWindowButtonHeight = 40;
constexpr int kMinWindowButtonWidth = 46;
// How often apps and tabs are checked for going to sleep.
constexpr base::TimeDelta kSleepCheckInterval = base::Seconds(30);

// Renderer processes used by |contents|, including cross-site iframes.
std::set<int> CollectRendererProcessIds(content::WebContents* contents) {
  std::set<int> ids;
  contents->ForEachRenderFrameHost([&ids](content::RenderFrameHost* rfh) {
    if (content::RenderProcessHost* host = rfh->GetProcess()) {
      ids.insert(host->GetDeprecatedID());
    }
  });
  return ids;
}

// True if |contents| is already in Power Sleep (its process was ended, it never
// started, or it crashed). Such a tab has nothing left to free.
bool IsPowerSleeping(content::WebContents* contents) {
  return contents->IsCrashed() || IsPowerSlept(contents);
}

// True if every frame hosted by |host| belongs to |contents|. A process that
// also hosts another tab or sidebar app must not be ended: that other page is
// not marked as Power Sleep, so it would crash with "Aw, Snap!".
bool ProcessIsExclusiveTo(content::RenderProcessHost* host,
                          content::WebContents* contents) {
  bool exclusive = true;
  host->ForEachRenderFrameHost([&](content::RenderFrameHost* rfh) {
    content::WebContents* other =
        content::WebContents::FromRenderFrameHost(rfh);
    if (other && other != contents) {
      exclusive = false;
    }
  });
  return exclusive;
}

// Pages that may go to Power Sleep: the web, and the new tab page (it has
// nothing to lose, it just loads again when it is shown).
bool IsSleepableUrl(const GURL& url) {
  if (url.SchemeIsHTTPOrHTTPS()) {
    return true;
  }
  if (!url.SchemeIs(content::kChromeUIScheme)) {
    return false;
  }
  const std::string host(url.host());
  return host == "new-tab-page" || host == "newtab" ||
         host == "new-tab-page-third-party";
}

// Power Sleep for a regular tab or a sidebar app: ends the renderer process of
// the page, exactly like "End process" in the Task Manager. The tab is marked
// (power_sleep.h), so no "Aw, Snap!" page and no dead-tab icon show up, and it
// loads again the moment it is shown. The tab stays where it is (title, icon,
// history, pinned state, group).
// - If the process of the page itself is shared with another tab or app,
//   nothing is done (returns false) and the next check tries again: ending it
//   would crash a page that is not marked, possibly the one in use.
// - Processes that only hold frames of other sites (ads, sign-in, embeds) are
//   often shared with other tabs. Those are left running; this tab's frames in
//   them go away with the page.
bool PowerSleepTab(content::WebContents* contents) {
  if (!contents || IsPowerSleeping(contents)) {
    return false;
  }
  content::RenderFrameHost* main_frame = contents->GetPrimaryMainFrame();
  content::RenderProcessHost* main_host =
      main_frame ? main_frame->GetProcess() : nullptr;
  if (!main_host) {
    return false;
  }
  if (!ProcessIsExclusiveTo(main_host, contents)) {
    LOG(INFO) << "[PowerSleep] not sleeping " << contents->GetLastCommittedURL()
              << ": its process is shared with another tab or app";
    return false;
  }
  std::vector<content::RenderProcessHost*> hosts = {main_host};
  for (int id : CollectRendererProcessIds(contents)) {
    content::RenderProcessHost* host = content::RenderProcessHost::FromID(id);
    if (host && host != main_host && ProcessIsExclusiveTo(host, contents)) {
      hosts.push_back(host);
    }
  }
  MarkPowerSlept(contents);
  for (content::RenderProcessHost* host : hosts) {
    host->Shutdown(content::RESULT_CODE_KILLED);
  }
  return true;
}

// True if |site| is in |sites|.
bool ListHasSite(const std::vector<std::string>& sites,
                 const std::string& site) {
  return std::find(sites.begin(), sites.end(), site) != sites.end();
}

// Custom icons larger than this are refused.
constexpr size_t kMaxIconFileBytes = 5 * 1024 * 1024;

// How long after the app list changes (or loads) the host window waits before
// starting the apps. This gives session restore time to bring back tabs, which
// are then reused instead of duplicated.
constexpr base::TimeDelta kAppLaunchDelay = base::Seconds(2);

constexpr int kCommandReload = 1;
constexpr int kCommandRemove = 2;
constexpr int kCommandEdit = 3;
constexpr int kCommandAddCurrent = 4;
constexpr int kCommandAddByUrl = 5;
constexpr int kCommandBack = 6;
constexpr int kCommandForward = 7;
constexpr int kCommandSiteSettings = 8;
constexpr int kCommandKeepIcon = 9;
constexpr int kCommandUseSiteIcon = 10;
constexpr int kCommandSetOrigin = 11;
constexpr int kCommandToggleDisabled = 12;
// Options menu.
constexpr int kCommandToggleToolbar = 20;
constexpr int kCommandToggleAutohide = 21;
constexpr int kCommandToggleBadges = 22;
constexpr int kCommandResetSettings = 23;
constexpr int kCommandWidthMenu = 24;
constexpr int kCommandIconMenu = 25;
constexpr int kCommandAppsMenu = 26;
constexpr int kCommandPositionMenu = 27;
constexpr int kCommandSleepMenu = 63;
constexpr int kCommandSleepAllApps = 64;
constexpr int kCommandSleepAllTabs = 65;
constexpr int kCommandDoNotDisturbMenu = 66;
constexpr int kCommandDoNotDisturbOff = 67;
constexpr int kCommandDoNotDisturbBase = 70;  // + index in kDoNotDisturbDurations
constexpr int kCommandTabSleepTime = 60;
constexpr int kCommandToggleSleepApps = 61;
constexpr int kCommandToggleSleepTabs = 62;
// Power Sleep extras (80..85).
constexpr int kCommandToggleGlobalAppSleep = 80;
constexpr int kCommandToggleIgnorePinned = 81;
constexpr int kCommandToggleSleepExceptions = 82;
constexpr int kCommandAppSleepTime = 83;
constexpr int kCommandSleepToggleException = 84;
constexpr int kCommandSleepEditExceptions = 85;
constexpr int kCommandReloadAllApps = 90;
constexpr int kCommandToggleAutostart = 91;
constexpr int kCommandPositionBase = 50;  // + SidebarPosition
constexpr int kPositionRadioGroup = 3;
constexpr int kCommandWidthBase = 30;     // + index in kWidthPresets
constexpr int kCommandIconSizeBase = 40;  // + index in kIconSizePresets
constexpr int kCommandAppToggleBase = 300;  // + index in options_app_ids_
// VPN submenu (200..299, see sidebar_vpn.h).
constexpr int kCommandVpnFirst = 200;
constexpr int kCommandVpnMenu = 200;
constexpr int kCommandVpnScopeAll = 202;
constexpr int kCommandVpnScopeList = 203;
constexpr int kCommandVpnToggleSite = 204;
constexpr int kCommandVpnEditSites = 205;
constexpr int kCommandVpnApps = 206;
constexpr int kCommandVpnServerMenu = 207;
constexpr int kCommandVpnEditServers = 208;
constexpr int kCommandVpnCountryMenu = 209;
constexpr int kCommandVpnServerBase = 210;  // + index of the server
constexpr int kCommandVpnTorStatus = 230;   // Disabled text item.
constexpr int kCommandVpnCountryBase = 240;  // + index in vpn::kTorCountries
constexpr int kCommandVpnExceptionsOn = 260;     // "Ignore sites in Exception List"
constexpr int kCommandVpnToggleException = 261;  // Add / Remove current site.
constexpr int kCommandVpnEditExceptions = 262;
constexpr int kCommandVpnRelaunchTor = 263;   // Close and reopen built-in Tor.
constexpr int kCommandVpnReconnectTor = 264;  // Drop connections, reconnect.
constexpr int kCommandVpnLast = 299;
constexpr int kVpnCountryRadioGroup = 6;
constexpr int kVpnServerRadioGroup = 5;
// Ad block submenu (100..149, see sidebar_adblock.h).
constexpr int kCommandAdblockFirst = 100;
constexpr int kCommandAdblockMenu = 100;
constexpr int kCommandAdblockScopeAll = 102;
constexpr int kCommandAdblockScopeList = 103;
constexpr int kCommandAdblockToggleSite = 104;
constexpr int kCommandAdblockEditSites = 105;
constexpr int kCommandAdblockApps = 106;
constexpr int kCommandAdblockStatus = 107;  // Disabled text item.
constexpr int kCommandAdblockExceptionsOn = 108;
constexpr int kCommandAdblockToggleException = 109;
constexpr int kCommandAdblockEditExceptions = 110;
constexpr int kCommandAdblockLast = 149;
constexpr int kWidthRadioGroup = 1;
constexpr int kIconRadioGroup = 2;

struct Preset {
  const char16_t* label;
  int value;
};
constexpr std::array<Preset, 4> kWidthPresets = {
    Preset{u"Narrow", 44}, Preset{u"Default", 52}, Preset{u"Wide", 64},
    Preset{u"Extra wide", 80}};
constexpr std::array<Preset, 4> kIconSizePresets = {
    Preset{u"Small", 20}, Preset{u"Default", 24}, Preset{u"Large", 32},
    Preset{u"Extra large", 40}};

constexpr std::array<const char16_t*, 4> kPositionNames = {u"Left", u"Right",
                                                           u"Top", u"Bottom"};

// Space around an app button. In a column (|side| is the space left and right
// of it) the strip has no margin of its own, so that the window buttons can
// use the full width.
gfx::Insets EntryMargins(bool horizontal, int side) {
  return horizontal ? gfx::Insets::VH(side, 4) : gfx::Insets::VH(4, side);
}

// Space around the add / options / menu buttons; |size_difference| is how much
// smaller they are than an app button.
gfx::Insets BottomButtonMargins(bool horizontal,
                                int size_difference,
                                int side) {
  const int own = size_difference / 2;
  return horizontal ? gfx::Insets::VH(side + own, 4)
                    : gfx::Insets::VH(4, side + own);
}

std::u16string PresetLabel(const Preset& preset) {
  return std::u16string(preset.label) + u" (" +
         base::NumberToString16(preset.value) + u")";
}

// Main-menu label: a symbol, then the text. U+FE0E asks for the text (single
// color) form of the symbol, so it is drawn in the menu's own text color
// (white on a dark menu) instead of as a colored emoji.
std::u16string MenuLabel(std::u16string_view symbol, std::u16string_view text) {
  std::u16string label(symbol);
  label += u"\uFE0E  ";
  label += text;
  return label;
}

// A readable default name for a site: "https://web.whatsapp.com/" becomes
// "Whatsapp". Uses the registrable domain's first label.
std::string NameForUrl(const GURL& url) {
  std::string name = net::registry_controlled_domains::GetDomainAndRegistry(
      url, net::registry_controlled_domains::INCLUDE_PRIVATE_REGISTRIES);
  if (name.empty()) {
    name = url.host();
  }
  const size_t dot = name.find('.');
  if (dot != std::string::npos) {
    name.resize(dot);
  }
  if (!name.empty()) {
    name[0] = base::ToUpperASCII(name[0]);
  }
  return name;
}

bool IsDigit(char16_t c) {
  return c >= u'0' && c <= u'9';
}

// Reads the digits that start at |pos| in |title| and are followed by
// |closing|. Returns -1 if there is no such number (1 to 5 digits).
int ReadNumberAt(std::u16string_view title, size_t pos, char16_t closing) {
  int value = 0;
  size_t digits = 0;
  while (pos < title.size() && IsDigit(title[pos]) && digits < 5) {
    value = value * 10 + (title[pos] - u'0');
    ++pos;
    ++digits;
  }
  if (digits == 0 || pos >= title.size() || title[pos] != closing) {
    return -1;
  }
  return value;
}

// Most chat and mail sites put their unread count in the page title:
// "(3) Inbox", "[2] Chat", or "Inbox (12) - me@example.com". Returns 0 when
// the title has no such count.
int ParseUnreadCount(std::u16string_view title) {
  size_t start = 0;
  while (start < title.size() && title[start] == u' ') {
    ++start;
  }
  if (start < title.size() && title[start] == u'(') {
    return std::max(0, ReadNumberAt(title, start + 1, u')'));
  }
  if (start < title.size() && title[start] == u'[') {
    return std::max(0, ReadNumberAt(title, start + 1, u']'));
  }
  constexpr std::u16string_view kInbox = u"Inbox (";
  const size_t inbox = title.find(kInbox);
  if (inbox != std::u16string_view::npos) {
    return std::max(0, ReadNumberAt(title, inbox + kInbox.size(), u')'));
  }
  return 0;
}

SkColor ForegroundColor(const views::View* view) {
  const ui::ColorProvider* provider = view->GetColorProvider();
  return provider ? provider->GetColor(ui::kColorPrimaryForeground)
                  : SK_ColorGRAY;
}

// Ids are made by the model, but a file name is only ever built from plain
// characters.
bool IsSafeFileName(const std::string& id) {
  if (id.empty()) {
    return false;
  }
  for (char c : id) {
    const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                    (c >= '0' && c <= '9') || c == '-' || c == '_';
    if (!ok) {
      return false;
    }
  }
  return true;
}

// Every app keeps its cookies, local storage, service workers and so on in its
// own storage partition, so two apps for the same site can be logged in to
// different accounts. The partition domain may only contain [a-z], so the
// app's UUID is written with the letters a-p (one letter per hex digit). One
// domain per app also lets us delete exactly that app's data later.
std::string PartitionDomainForApp(const std::string& app_id) {
  std::string domain = "dockium";
  for (char c : app_id) {
    if (c >= '0' && c <= '9') {
      domain.push_back('a' + (c - '0'));
    } else if (c >= 'a' && c <= 'f') {
      domain.push_back('a' + 10 + (c - 'a'));
    }  // Hyphens (and anything else) are skipped.
  }
  return domain;
}

// Icon files are read, written and deleted one after the other on this runner,
// so a load can never overtake a save.
const scoped_refptr<base::SequencedTaskRunner>& IconTaskRunner() {
  static base::NoDestructor<scoped_refptr<base::SequencedTaskRunner>> runner(
      base::ThreadPool::CreateSequencedTaskRunner(
          {base::MayBlock(), base::TaskPriority::USER_VISIBLE,
           base::TaskShutdownBehavior::SKIP_ON_SHUTDOWN}));
  return *runner;
}

// Runs on the icon runner. Returns a null bitmap if |path| is not a usable PNG.
SkBitmap DecodeIconFile(const base::FilePath& path) {
  std::optional<std::vector<uint8_t>> bytes = base::ReadFileToBytes(path);
  if (!bytes || bytes->empty() || bytes->size() > kMaxIconFileBytes) {
    return SkBitmap();
  }
  SkBitmap bitmap = gfx::PNGCodec::Decode(*bytes);
  if (bitmap.drawsNothing()) {
    return SkBitmap();
  }
  bitmap.setImmutable();
  return bitmap;
}

// Runs on the icon runner. Checks that |source| is a good PNG, then keeps a
// copy of it at |dest|. Returns a null bitmap on any failure.
SkBitmap ImportIconFile(const base::FilePath& source,
                        const base::FilePath& dest) {
  SkBitmap bitmap = DecodeIconFile(source);
  if (bitmap.isNull()) {
    return bitmap;
  }
  if (!base::CreateDirectory(dest.DirName()) ||
      !base::CopyFile(source, dest)) {
    return SkBitmap();
  }
  return bitmap;
}

// Runs on the icon runner. Saves |data| (a PNG) at |dest|.
void WriteIconFile(const base::FilePath& dest, std::vector<uint8_t> data) {
  if (!base::CreateDirectory(dest.DirName()) || !base::WriteFile(dest, data)) {
    LOG(WARNING) << "Could not save the sidebar icon " << dest;
  }
}

// Soft rounded highlight drawn behind a button.
void PaintRoundedHighlight(gfx::Canvas* canvas,
                           const views::View* view,
                           SkAlpha alpha,
                           int radius) {
  if (alpha == 0) {
    return;
  }
  cc::PaintFlags flags;
  flags.setAntiAlias(true);
  flags.setStyle(cc::PaintFlags::kFill_Style);
  flags.setColor(SkColorSetA(ForegroundColor(view), alpha));
  canvas->DrawRoundRect(view->GetLocalBounds(), radius, flags);
}

}  // namespace

// The button of one app: the site's icon (or the first letter of its name)
// with a soft rounded rectangle behind it while the mouse is over it, while it
// is pressed, and while the app is the one being shown.
class SidebarButton : public views::LabelButton {
  METADATA_HEADER(SidebarButton, views::LabelButton)

 public:
  SidebarButton(views::Button::PressedCallback callback,
                const std::u16string& text)
      : views::LabelButton(std::move(callback), text) {}

  // Called with this button and the mouse position in its own coordinates.
  using DragCallback =
      base::RepeatingCallback<void(views::View*, const gfx::Point&)>;
  void SetDragCallbacks(DragCallback start,
                        DragCallback move,
                        DragCallback end) {
    drag_start_ = std::move(start);
    drag_move_ = std::move(move);
    drag_end_ = std::move(end);
  }

  void SetActive(bool active) {
    if (active_ == active) {
      return;
    }
    active_ = active;
    SchedulePaint();
  }

  // The icon is painted by the button itself, exactly in the middle, instead
  // of being placed by the label button's image-and-text layout.
  void SetAppIcon(const gfx::ImageSkia& icon) {
    icon_ = icon;
    SchedulePaint();
  }

 protected:
  // Dragging the button up or down rearranges the apps; a short press is
  // still a click.
  bool OnMousePressed(const ui::MouseEvent& event) override {
    press_point_ = event.location();
    drag_started_ = false;
    return views::LabelButton::OnMousePressed(event);
  }

  bool OnMouseDragged(const ui::MouseEvent& event) override {
    if (!drag_started_) {
      if (!event.IsLeftMouseButton() ||
          std::abs(event.y() - press_point_.y()) < kDragThreshold) {
        return views::LabelButton::OnMouseDragged(event);
      }
      drag_started_ = true;
      if (drag_start_) {
        drag_start_.Run(this, press_point_);
      }
    }
    if (drag_move_) {
      drag_move_.Run(this, event.location());
    }
    return true;
  }

  void OnMouseReleased(const ui::MouseEvent& event) override {
    const bool was_dragging = drag_started_;
    drag_started_ = false;
    if (was_dragging) {
      // The button is let go of by the mouse button, not clicked.
      suppress_click_ = true;
    }
    views::LabelButton::OnMouseReleased(event);
    suppress_click_ = false;
    if (was_dragging && drag_end_) {
      drag_end_.Run(this, event.location());
    }
  }

  void NotifyClick(const ui::Event& event) override {
    if (suppress_click_) {
      return;
    }
    views::LabelButton::NotifyClick(event);
  }

  // views::Button (extension point of Button::OnPaint()):
  void PaintButtonContents(gfx::Canvas* canvas) override {
    if (icon_.isNull()) {
      return;
    }
    canvas->DrawImageInt(icon_, (width() - icon_.width()) / 2,
                         (height() - icon_.height()) / 2);
  }

  // views::View:
  void OnPaintBackground(gfx::Canvas* canvas) override {
    SkAlpha alpha = active_ ? 0x2E : 0x00;
    if (GetState() == STATE_PRESSED) {
      alpha = 0x4A;
    } else if (GetState() == STATE_HOVERED) {
      alpha = active_ ? 0x3C : 0x24;
    }
    PaintRoundedHighlight(canvas, this, alpha, std::clamp(width() / 4, 6, 16));
  }

 private:
  // How far (in DIPs) the mouse must move before a press becomes a drag.
  static constexpr int kDragThreshold = 6;

  bool active_ = false;
  gfx::ImageSkia icon_;
  gfx::Point press_point_;
  bool drag_started_ = false;
  bool suppress_click_ = false;
  DragCallback drag_start_;
  DragCallback drag_move_;
  DragCallback drag_end_;
};

BEGIN_METADATA(SidebarButton)
END_METADATA

// The "add app" button: a round button with a big plus. The circle is faint
// at rest and gets stronger when hovered or pressed.
class AddButton : public views::LabelButton {
  METADATA_HEADER(AddButton, views::LabelButton)

 public:
  explicit AddButton(views::Button::PressedCallback callback)
      : views::LabelButton(std::move(callback), std::u16string()) {
    GetViewAccessibility().SetName(u"Add app");
  }

 protected:
  // views::Button (Button::OnPaint() is final; this is its extension point):
  void PaintButtonContents(gfx::Canvas* canvas) override {
    SkAlpha alpha = 0x14;
    if (GetState() == STATE_PRESSED) {
      alpha = 0x48;
    } else if (GetState() == STATE_HOVERED) {
      alpha = 0x30;
    }
    const SkColor color = ForegroundColor(this);
    const gfx::Rect bounds = GetLocalBounds();
    const gfx::PointF center(bounds.CenterPoint());
    const float radius =
        std::min(bounds.width(), bounds.height()) / 2.0f - 2.0f;

    cc::PaintFlags fill;
    fill.setAntiAlias(true);
    fill.setStyle(cc::PaintFlags::kFill_Style);
    fill.setColor(SkColorSetA(color, alpha));
    canvas->DrawCircle(center, radius, fill);

    const float scale = std::min(bounds.width(), bounds.height()) / 40.0f;
    const float kArm = 8.0f * scale;  // Half the length of each stroke.
    cc::PaintFlags stroke;
    stroke.setAntiAlias(true);
    stroke.setStyle(cc::PaintFlags::kStroke_Style);
    stroke.setStrokeWidth(2.5f * scale);
    stroke.setStrokeCap(cc::PaintFlags::kRound_Cap);
    stroke.setColor(color);
    canvas->DrawLine(gfx::PointF(center.x() - kArm, center.y()),
                     gfx::PointF(center.x() + kArm, center.y()), stroke);
    canvas->DrawLine(gfx::PointF(center.x(), center.y() - kArm),
                     gfx::PointF(center.x(), center.y() + kArm), stroke);
  }
};

BEGIN_METADATA(AddButton)
END_METADATA

// The "settings" button: the same round button as "add", with a gear. It opens
// the browser settings, which are otherwise out of reach while a sidebar app
// is shown (the toolbar is hidden then).
class SettingsButton : public views::LabelButton {
  METADATA_HEADER(SettingsButton, views::LabelButton)

 public:
  explicit SettingsButton(views::Button::PressedCallback callback)
      : views::LabelButton(std::move(callback), std::u16string()) {
    GetViewAccessibility().SetName(u"Menu");
  }

 protected:
  // views::Button (extension point of Button::OnPaint()):
  void PaintButtonContents(gfx::Canvas* canvas) override {
    SkAlpha alpha = 0x14;
    if (GetState() == STATE_PRESSED) {
      alpha = 0x48;
    } else if (GetState() == STATE_HOVERED) {
      alpha = 0x30;
    }
    const SkColor color = ForegroundColor(this);
    const gfx::Rect bounds = GetLocalBounds();
    const gfx::PointF center(bounds.CenterPoint());
    const float radius =
        std::min(bounds.width(), bounds.height()) / 2.0f - 2.0f;

    cc::PaintFlags fill;
    fill.setAntiAlias(true);
    fill.setStyle(cc::PaintFlags::kFill_Style);
    fill.setColor(SkColorSetA(color, alpha));
    canvas->DrawCircle(center, radius, fill);

    // The gear: a thick ring with eight teeth around it.
    const float scale = std::min(bounds.width(), bounds.height()) / 40.0f;
    cc::PaintFlags ring;
    ring.setAntiAlias(true);
    ring.setStyle(cc::PaintFlags::kStroke_Style);
    ring.setStrokeWidth(3.0f * scale);
    ring.setColor(color);
    canvas->DrawCircle(center, 5.5f * scale, ring);

    cc::PaintFlags tooth;
    tooth.setAntiAlias(true);
    tooth.setStyle(cc::PaintFlags::kStroke_Style);
    tooth.setStrokeWidth(3.0f * scale);
    tooth.setColor(color);
    constexpr float kPi = 3.14159265f;
    for (int i = 0; i < 8; ++i) {
      const float angle = i * kPi / 4.0f;
      const float dx = std::cos(angle);
      const float dy = std::sin(angle);
      canvas->DrawLine(
          gfx::PointF(center.x() + dx * 6.5f * scale,
                      center.y() + dy * 6.5f * scale),
          gfx::PointF(center.x() + dx * 9.5f * scale,
                      center.y() + dy * 9.5f * scale),
          tooth);
    }
  }
};

BEGIN_METADATA(SettingsButton)
END_METADATA

// The "options" button: the same round button as "add", with three sliders.
// It opens the options menu (toolbar, width, icon size, auto-hide, ...).
class OptionsButton : public views::LabelButton {
  METADATA_HEADER(OptionsButton, views::LabelButton)

 public:
  explicit OptionsButton(views::Button::PressedCallback callback)
      : views::LabelButton(std::move(callback), std::u16string()) {
    GetViewAccessibility().SetName(u"Sidebar options");
  }

 protected:
  // views::Button (extension point of Button::OnPaint()):
  void PaintButtonContents(gfx::Canvas* canvas) override {
    SkAlpha alpha = 0x14;
    if (GetState() == STATE_PRESSED) {
      alpha = 0x48;
    } else if (GetState() == STATE_HOVERED) {
      alpha = 0x30;
    }
    const SkColor color = ForegroundColor(this);
    const gfx::Rect bounds = GetLocalBounds();
    const gfx::PointF center(bounds.CenterPoint());
    const float radius =
        std::min(bounds.width(), bounds.height()) / 2.0f - 2.0f;

    cc::PaintFlags fill;
    fill.setAntiAlias(true);
    fill.setStyle(cc::PaintFlags::kFill_Style);
    fill.setColor(SkColorSetA(color, alpha));
    canvas->DrawCircle(center, radius, fill);

    // Three horizontal lines, each with a knob at a different position.
    const float scale = std::min(bounds.width(), bounds.height()) / 40.0f;
    const float half = 8.0f * scale;
    constexpr std::array<float, 3> kRows = {-6.0f, 0.0f, 6.0f};
    constexpr std::array<float, 3> kKnobs = {-3.0f, 3.5f, -1.0f};

    cc::PaintFlags line;
    line.setAntiAlias(true);
    line.setStyle(cc::PaintFlags::kStroke_Style);
    line.setStrokeWidth(2.0f * scale);
    line.setStrokeCap(cc::PaintFlags::kRound_Cap);
    line.setColor(color);

    cc::PaintFlags knob;
    knob.setAntiAlias(true);
    knob.setStyle(cc::PaintFlags::kFill_Style);
    knob.setColor(color);

    for (int i = 0; i < 3; ++i) {
      const float y = center.y() + kRows[i] * scale;
      canvas->DrawLine(gfx::PointF(center.x() - half, y),
                       gfx::PointF(center.x() + half, y), line);
      canvas->DrawCircle(gfx::PointF(center.x() + kKnobs[i] * scale, y),
                         3.0f * scale, knob);
    }
  }
};

BEGIN_METADATA(OptionsButton)
END_METADATA

// A window button (minimize, maximize / restore, close) at the top of the
// strip, drawn like the other round buttons.
class WindowButton : public views::LabelButton {
  METADATA_HEADER(WindowButton, views::LabelButton)

 public:
  enum class Kind { kMinimize, kMaximize, kClose };

  WindowButton(views::Button::PressedCallback callback,
               Kind kind,
               const std::u16string& name,
               base::RepeatingCallback<bool()> is_maximized,
               bool fill_bounds,
               int glyph_shift_x)
      : views::LabelButton(std::move(callback), std::u16string()),
        kind_(kind),
        fill_bounds_(fill_bounds),
        glyph_shift_x_(glyph_shift_x),
        is_maximized_(std::move(is_maximized)) {
    GetViewAccessibility().SetName(name);
    SetTooltipText(name);
  }

 protected:
  // views::Button (extension point of Button::OnPaint()):
  void PaintButtonContents(gfx::Canvas* canvas) override {
    const bool pressed = GetState() == STATE_PRESSED;
    const bool hovered = GetState() == STATE_HOVERED;
    const SkColor color = ForegroundColor(this);
    SkColor glyph_color = color;
    if (fill_bounds_) {
      // Pure white on a dark frame (black on a light one), like the window's
      // own buttons.
      if (const ui::ColorProvider* provider = GetColorProvider()) {
        const bool active = !GetWidget() || GetWidget()->ShouldPaintAsActive();
        glyph_color = color_utils::GetColorWithMaxContrast(provider->GetColor(
            active ? ui::kColorFrameActive : ui::kColorFrameInactive));
      }
    }
    const gfx::Rect bounds = GetLocalBounds();
    const float cx = bounds.CenterPoint().x() + glyph_shift_x_;
    const float cy = bounds.CenterPoint().y();
    const float s = std::min(bounds.width(), bounds.height()) / 32.0f;

    if (fill_bounds_) {
      // Like the window's own buttons: the whole rectangle lights up (red for
      // close), wherever the mouse is on it.
      if (pressed || hovered) {
        if (kind_ == Kind::kClose) {
          canvas->FillRect(bounds, pressed ? SkColorSetRGB(0xF1, 0x70, 0x7A)
                                           : SkColorSetRGB(0xE8, 0x11, 0x23));
          glyph_color = SK_ColorWHITE;
        } else {
          canvas->FillRect(bounds, SkColorSetA(color, pressed ? 0x40 : 0x26));
        }
      }
    } else if (pressed || hovered) {
      cc::PaintFlags fill;
      fill.setAntiAlias(true);
      fill.setStyle(cc::PaintFlags::kFill_Style);
      fill.setColor(SkColorSetA(color, pressed ? 0x48 : 0x30));
      canvas->DrawCircle(gfx::PointF(cx, cy),
                         std::min(bounds.width(), bounds.height()) / 2.0f - 2.0f,
                         fill);
    }

    if (fill_bounds_) {
      DrawCrispGlyph(canvas, cx, cy, glyph_color);
      return;
    }

    cc::PaintFlags line;
    line.setAntiAlias(true);
    line.setStyle(cc::PaintFlags::kStroke_Style);
    line.setStrokeWidth(1.6f * s);
    line.setStrokeCap(cc::PaintFlags::kRound_Cap);
    line.setColor(glyph_color);

    switch (kind_) {
      case Kind::kMinimize:
        canvas->DrawLine(gfx::PointF(cx - 5.0f * s, cy),
                         gfx::PointF(cx + 5.0f * s, cy), line);
        break;
      case Kind::kMaximize:
        if (is_maximized_ && is_maximized_.Run()) {
          // Restore: a square with a second one peeking out behind it.
          const float x0 = cx - 5.0f * s;
          const float y0 = cy - 3.0f * s;
          canvas->DrawRect(gfx::RectF(x0, y0, 8.0f * s, 8.0f * s), line);
          canvas->DrawLine(gfx::PointF(x0 + 2.0f * s, y0 - 2.0f * s),
                           gfx::PointF(x0 + 10.0f * s, y0 - 2.0f * s), line);
          canvas->DrawLine(gfx::PointF(x0 + 10.0f * s, y0 - 2.0f * s),
                           gfx::PointF(x0 + 10.0f * s, y0 + 6.0f * s), line);
          canvas->DrawLine(gfx::PointF(x0 + 2.0f * s, y0 - 2.0f * s),
                           gfx::PointF(x0 + 2.0f * s, y0), line);
          canvas->DrawLine(gfx::PointF(x0 + 10.0f * s, y0 + 6.0f * s),
                           gfx::PointF(x0 + 8.0f * s, y0 + 6.0f * s), line);
        } else {
          canvas->DrawRect(
              gfx::RectF(cx - 5.0f * s, cy - 5.0f * s, 10.0f * s, 10.0f * s),
              line);
        }
        break;
      case Kind::kClose:
        canvas->DrawLine(gfx::PointF(cx - 4.5f * s, cy - 4.5f * s),
                         gfx::PointF(cx + 4.5f * s, cy + 4.5f * s), line);
        canvas->DrawLine(gfx::PointF(cx + 4.5f * s, cy - 4.5f * s),
                         gfx::PointF(cx - 4.5f * s, cy + 4.5f * s), line);
        break;
    }
  }

 private:
  // Draws the symbol the way Windows does, for the rectangular buttons: 10 DIPs
  // across whatever the size of the rectangle, lines one device pixel thick on
  // whole pixels, so it is as sharp and as bright as the window's own buttons.
  void DrawCrispGlyph(gfx::Canvas* canvas,
                      float cx,
                      float cy,
                      SkColor color) const {
    const float scale = canvas->image_scale();
    const float unit = 1.0f / scale;
    // The middle of the device pixel that contains |v|.
    auto pixel_middle = [scale](float v) {
      return (std::floor(v * scale) + 0.5f) / scale;
    };
    const float left = pixel_middle(cx - 5.0f);
    const float top = pixel_middle(cy - 5.0f);
    const float span = 10.0f - unit;
    const float right = left + span;
    const float bottom = top + span;

    cc::PaintFlags line;
    line.setAntiAlias(true);
    line.setStyle(cc::PaintFlags::kStroke_Style);
    line.setStrokeWidth(unit);
    line.setStrokeCap(cc::PaintFlags::kButt_Cap);
    line.setColor(color);

    switch (kind_) {
      case Kind::kMinimize: {
        const float y = pixel_middle(cy);
        canvas->DrawLine(gfx::PointF(left - unit / 2, y),
                         gfx::PointF(right + unit / 2, y), line);
        break;
      }
      case Kind::kMaximize:
        if (is_maximized_ && is_maximized_.Run()) {
          // Restore: a square with a second one peeking out behind it.
          const float front = 8.0f - unit;
          canvas->DrawRect(gfx::RectF(left, top + 2.0f, front, front), line);
          canvas->DrawLine(gfx::PointF(left + 2.0f - unit / 2, top),
                           gfx::PointF(right + unit / 2, top), line);
          canvas->DrawLine(gfx::PointF(left + 2.0f, top),
                           gfx::PointF(left + 2.0f, top + 2.0f), line);
          canvas->DrawLine(gfx::PointF(right, top),
                           gfx::PointF(right, top + front + unit / 2), line);
          canvas->DrawLine(gfx::PointF(left + front + unit / 2, top + front),
                           gfx::PointF(right, top + front), line);
        } else {
          canvas->DrawRect(gfx::RectF(left, top, span, span), line);
        }
        break;
      case Kind::kClose:
        canvas->DrawLine(gfx::PointF(left, top), gfx::PointF(right, bottom),
                         line);
        canvas->DrawLine(gfx::PointF(right, top), gfx::PointF(left, bottom),
                         line);
        break;
    }
  }

  const Kind kind_;
  const bool fill_bounds_;
  // How far the symbol is moved sideways from the center of the button.
  const int glyph_shift_x_;
  base::RepeatingCallback<bool()> is_maximized_;
};

BEGIN_METADATA(WindowButton)
END_METADATA

// One sidebar entry: the app button plus a small round badge in its top-right
// corner.
class AppEntryView : public views::View {
  METADATA_HEADER(AppEntryView, views::View)

 public:
  AppEntryView(views::Button::PressedCallback callback,
               const std::u16string& name,
               int button_size,
               int icon_size)
      : icon_size_(icon_size) {
    SetPreferredSize(gfx::Size(button_size, button_size));
    name_ = name;
    letter_ = name.substr(0, 1);
    button_ = AddChildView(
        std::make_unique<SidebarButton>(std::move(callback), letter_));
    button_->SetTooltipText(name);
    button_->SetHorizontalAlignment(gfx::ALIGN_CENTER);
    badge_ = AddChildView(std::make_unique<views::Label>());
    badge_->SetEnabledColor(SK_ColorWHITE);
    badge_->SetHorizontalAlignment(gfx::ALIGN_CENTER);
    badge_->SetBackground(
        views::CreateRoundedRectBackground(SkColorSetRGB(0xE5, 0x39, 0x35),
                                           kBadgeHeight / 2));
    // Clicks and right-clicks on the badge go to the button below it.
    badge_->SetCanProcessEventsWithinSubtree(false);
    // Its own layer, so it is drawn above the button (which may have layers of
    // its own for hover effects).
    badge_->SetPaintToLayer();
    badge_->layer()->SetFillsBoundsOpaquely(false);
    badge_->SetVisible(false);
  }

  views::LabelButton* button() { return button_; }
  void SetDragCallbacks(SidebarButton::DragCallback start,
                        SidebarButton::DragCallback move,
                        SidebarButton::DragCallback end) {
    button_->SetDragCallbacks(std::move(start), std::move(move),
                              std::move(end));
  }

  // Highlights the entry of the app that is currently shown.
  void SetActive(bool active) { button_->SetActive(active); }

  // A disabled app is dimmed; it is not started and cannot be opened.
  void SetDisabled(bool disabled) {
    button_->SetTooltipText(disabled ? name_ + u" (disabled)" : name_);
    if (!disabled && !layer()) {
      return;
    }
    if (!layer()) {
      SetPaintToLayer();
      layer()->SetFillsBoundsOpaquely(false);
    }
    layer()->SetOpacity(disabled ? 0.4f : 1.0f);
  }

  // Right-clicking the entry or its button opens the controller's menu.
  void SetContextMenuController(views::ContextMenuController* controller) {
    set_context_menu_controller(controller);
    button_->set_context_menu_controller(controller);
  }

  // Replaces the letter with the site's icon.
  void SetIcon(const gfx::ImageSkia& icon) {
    if (icon.isNull()) {
      return;
    }
    button_->SetAppIcon(gfx::ImageSkiaOperations::CreateResizedImage(
        icon, skia::ImageOperations::RESIZE_BEST,
        gfx::Size(icon_size_, icon_size_)));
    button_->SetText(std::u16string());
  }

  // Goes back to the letter (used when a custom icon is removed).
  void ClearIcon() {
    button_->SetAppIcon(gfx::ImageSkia());
    button_->SetText(letter_);
  }

  void SetBadgeCount(int count) {
    if (count <= 0) {
      badge_->SetVisible(false);
      return;
    }
    badge_->SetText(count > 99 ? u"99+" : base::NumberToString16(count));
    badge_->SetVisible(true);
    LayoutChildren();
  }

 protected:
  // views::View:
  void OnBoundsChanged(const gfx::Rect& previous_bounds) override {
    LayoutChildren();
  }
  void OnThemeChanged() override {
    views::View::OnThemeChanged();
    if (const ui::ColorProvider* provider = GetColorProvider()) {
      // The letter shown until the site's icon loads follows the theme.
      button_->SetEnabledTextColors(
          provider->GetColor(ui::kColorPrimaryForeground));
    }
  }

 private:
  void LayoutChildren() {
    button_->SetBoundsRect(gfx::Rect(size()));
    const int badge_width = badge_->GetText().size() > 2 ? 28 : 18;
    // Top-right corner, drawn on top of the icon.
    badge_->SetBoundsRect(
        gfx::Rect(width() - badge_width, 0, badge_width, kBadgeHeight));
  }

  std::u16string name_;
  std::u16string letter_;
  const int icon_size_;
  raw_ptr<SidebarButton> button_ = nullptr;
  raw_ptr<views::Label> badge_ = nullptr;
};

BEGIN_METADATA(AppEntryView)
END_METADATA


namespace {

// Sites that do not put an unread count like "(3)" in the page title. For
// each of them, a script that is run inside the page (in an isolated world, so
// the page's own scripts and security rules do not matter) and returns the
// number of unread items, read from the page's own HTML. To add a site, add a
// script below and a line to kDomCountSites.

// Messenger: the "Chats" button of the left bar has an aria-label like
// "Chats · 4 unread".
constexpr char16_t kMessengerScript[] =
    uR"JS((function () {
  var best = 0;
  var links = document.querySelectorAll('a[aria-label]');
  for (var i = 0; i < links.length; i++) {
    var label = links[i].getAttribute('aria-label') || '';
    var m = /^Chats\b.*?(\d+)\s+unread/i.exec(label);
    if (m) {
      best = Math.max(best, parseInt(m[1], 10));
    }
  }
  return best;
})())JS";

// Google Messages for web: every chat in the conversation list carries
// data-e2e-is-unread="true" while it has new messages. This counts the unread
// chats (an element and its parent chat are counted once), like the Messenger
// script above.
constexpr char16_t kGoogleMessagesScript[] =
    uR"JS((function () {
  var marked = document.querySelectorAll('[data-e2e-is-unread="true"]');
  var chats = new Set();
  for (var i = 0; i < marked.length; i++) {
    chats.add(marked[i].closest('mws-conversation-list-item') || marked[i]);
  }
  return chats.size;
})())JS";

struct DomCountSite {
  const char* host;
  const char16_t* script;
};

constexpr DomCountSite kDomCountSites[] = {
    {"www.messenger.com", kMessengerScript},
    {"messenger.com", kMessengerScript},
    {"messages.google.com", kGoogleMessagesScript},
};

// The script that reads the unread count of the app at |url|, or an empty
// string if that site counts in its title like most sites.
std::u16string DomCountScriptFor(const GURL& url) {
  for (const DomCountSite& site : kDomCountSites) {
    if (url.host() == site.host) {
      return site.script;
    }
  }
  return std::u16string();
}

}  // namespace

// Watches one app tab and reports its unread count. Most sites show it in the
// title ("(3) Inbox"). For the sites in kDomCountSites it is read from the
// page itself, every few seconds, because their title has no count.
class SidebarView::TitleObserver : public content::WebContentsObserver {
 public:
  TitleObserver(content::WebContents* contents,
                std::u16string dom_script,
                base::RepeatingCallback<void(int)> callback)
      : content::WebContentsObserver(contents),
        dom_script_(std::move(dom_script)),
        callback_(std::move(callback)) {
    if (dom_script_.empty()) {
      Report();
    } else {
      poll_timer_.Start(FROM_HERE, base::Seconds(3),
                        base::BindRepeating(&TitleObserver::PollPage,
                                            base::Unretained(this)));
    }
  }

  // content::WebContentsObserver:
  void TitleWasSet(content::NavigationEntry* entry) override {
    if (dom_script_.empty()) {
      Report();
    } else {
      PollPage();
    }
  }
  void WebContentsDestroyed() override { callback_.Run(0); }

 private:
  void Report() {
    if (web_contents()) {
      callback_.Run(ParseUnreadCount(web_contents()->GetTitle()));
    }
  }

  // Runs the site's script in the page and reports what it returns.
  void PollPage() {
    if (!web_contents()) {
      return;
    }
    content::RenderFrameHost* frame = web_contents()->GetPrimaryMainFrame();
    if (!frame || !frame->IsRenderFrameLive()) {
      return;
    }
    frame->ExecuteJavaScriptInIsolatedWorld(
        dom_script_,
        base::BindOnce(&TitleObserver::OnPageResult,
                       weak_factory_.GetWeakPtr()),
        ISOLATED_WORLD_ID_CHROME_INTERNAL);
  }

  void OnPageResult(base::Value result) {
    // Anything that is not a number (the page is still loading, the script
    // failed) leaves the last count as it was.
    if (std::optional<int> count = result.GetIfInt()) {
      callback_.Run(std::max(0, *count));
    } else if (std::optional<double> number = result.GetIfDouble()) {
      callback_.Run(std::max(0, static_cast<int>(*number)));
    }
  }

  const std::u16string dom_script_;
  base::RepeatingCallback<void(int)> callback_;
  base::RepeatingTimer poll_timer_;
  base::WeakPtrFactory<TitleObserver> weak_factory_{this};
};

// Tells the strip when the page in the active tab enters or leaves HTML
// fullscreen (a video, a game, a presentation).
class SidebarView::FullscreenObserver : public content::WebContentsObserver {
 public:
  FullscreenObserver(content::WebContents* contents,
                     base::RepeatingClosure callback)
      : content::WebContentsObserver(contents),
        callback_(std::move(callback)) {}

  // content::WebContentsObserver:
  void DidToggleFullscreenModeForTab(bool entered_fullscreen,
                                     bool will_cause_resize) override {
    callback_.Run();
  }

 private:
  base::RepeatingClosure callback_;
};

SidebarView::SidebarView(Profile* profile, BrowserWindowInterface* browser)
    : browser_(browser),
      profile_(profile),
      model_(SidebarAppModel::GetForProfile(profile)) {
  SetID(kSidebarViewId);
  // Right-clicking the strip itself (not an app button) shows the options.
  set_context_menu_controller(this);
  sleep_timer_.Start(FROM_HERE, kSleepCheckInterval, this,
                     &SidebarView::OnSleepTick);
  wake_timer_.Start(FROM_HERE, base::Seconds(1), this,
                    &SidebarView::WakeVisibleApps);
  sleep_all_subscription_ = SidebarTray::Get().AddSleepAllCallback(
      base::BindRepeating(&SidebarView::SleepAll, base::Unretained(this)));

  layout_ = SetLayoutManager(std::make_unique<views::FlexLayout>());
  layout_->SetOrientation(views::LayoutOrientation::kVertical);
  layout_->SetCrossAxisAlignment(views::LayoutAlignment::kStretch);
  ApplyMetrics();

  if (browser_) {
    active_tab_subscription_ = browser_->RegisterActiveTabDidChange(
        base::BindRepeating(&SidebarView::OnActiveTabChanged,
                            base::Unretained(this)));
    // Stops by itself when this view or the tab strip goes away.
    browser_->GetTabStripModel()->AddObserver(this);
  }

  model_observation_.Observe(model_);
  RebuildButtons();
  ScheduleAppLaunch();

  // One tray icon per process, following the first regular profile.
  if (!profile_->IsOffTheRecord()) {
    SidebarTray::AttachToModel(model_);
    if (browser_) {
      SidebarTray::Get().SetLastActiveWindow(browser_->GetWeakPtr());
    }
  }
}

SidebarView::~SidebarView() {
  model_->ReleaseAppHost(this);
}

// Radius of the page's rounded corners next to the strip, like the page next
// to Chrome's vertical tabs.
constexpr int kPageEdgeRadius = 10;
// Thickness of the thin line the browser draws along the page (the frame line
// at the top of the window, or the line under the toolbar). The rounded edge
// starts at the outer side of that line, and the border is not drawn over it.
constexpr int kEdgeLinePx = 2;
// How much of that line the border still overlaps where it starts, so the two
// meet. Two values: with no toolbar (the page starts at the top of the window)
// and with a toolbar above the page. A smaller number reaches further into the
// line (meets it, but can light its pixels up); a bigger one stops earlier.
constexpr int kBorderSkipPx = 0;
constexpr int kBorderSkipToolbarPx = 1;

// A thin overlay on the page, right next to the strip. It paints the strip's
// color in the corners a rounded page would leave empty, and a faint 1px border
// along the page edge. Which corners are rounded depends on the strip's
// position: top and bottom of the edge for a left or right strip, left and
// right for a top strip, and so on. It never takes a click.
class SidebarPageEdge : public views::View {
  METADATA_HEADER(SidebarPageEdge, views::View)

 public:
  SidebarPageEdge() {
    SetCanProcessEventsWithinSubtree(false);
    SetPaintToLayer();
    layer()->SetFillsBoundsOpaquely(false);
  }

  void SetEdge(SidebarPosition position, SkColor fill, SkColor frame_fill) {
    if (position == position_ && fill == fill_ && frame_fill == frame_fill_) {
      return;
    }
    position_ = position;
    fill_ = fill;
    frame_fill_ = frame_fill;
    SchedulePaint();
  }

  // The part of this view (in its own coordinates) that sits next to the
  // window buttons, where the strip is painted in the window frame's color.
  void SetFrameRect(const gfx::Rect& frame_rect) {
    if (frame_rect == frame_rect_) {
      return;
    }
    frame_rect_ = frame_rect;
    SchedulePaint();
  }

  void SetStartSkip(int skip_px) {
    if (skip_px == start_skip_px_) {
      return;
    }
    start_skip_px_ = skip_px;
    SchedulePaint();
  }

  // Sits on the page, right next to |span|: the part of the strip that has the
  // page beside it (in the parent's coordinates).
  void PlaceNextTo(const gfx::Rect& span) {
    constexpr int r = kPageEdgeRadius;
    switch (position_) {
      case SidebarPosition::kLeft:
        SetBoundsRect(gfx::Rect(span.right(), span.y(), r, span.height()));
        break;
      case SidebarPosition::kRight:
        SetBoundsRect(gfx::Rect(span.x() - r, span.y(), r, span.height()));
        break;
      case SidebarPosition::kTop:
        SetBoundsRect(gfx::Rect(span.x(), span.bottom(), span.width(), r));
        break;
      case SidebarPosition::kBottom:
        SetBoundsRect(gfx::Rect(span.x(), span.y() - r, span.width(), r));
        break;
    }
  }

  void OnPaint(gfx::Canvas* canvas) override {
    constexpr float r = kPageEdgeRadius;
    constexpr float kReach = 2 * r;
    // The page as a rounded rect that reaches past this view on the side away
    // from the strip, so only its corners next to the strip are visible.
    gfx::RectF page{gfx::SizeF(GetLocalBounds().size())};
    SkVector radii[4] = {{0, 0}, {0, 0}, {0, 0}, {0, 0}};  // UL, UR, LR, LL
    switch (position_) {
      case SidebarPosition::kLeft:
        page.set_width(page.width() + kReach);
        radii[0] = radii[3] = {r, r};
        break;
      case SidebarPosition::kRight:
        page.set_x(-kReach);
        page.set_width(page.width() + kReach);
        radii[1] = radii[2] = {r, r};
        break;
      case SidebarPosition::kTop:
        page.set_height(page.height() + kReach);
        radii[0] = radii[1] = {r, r};
        break;
      case SidebarPosition::kBottom:
        page.set_y(-kReach);
        page.set_height(page.height() + kReach);
        radii[2] = radii[3] = {r, r};
        break;
    }
    SkRRect page_rrect;
    page_rrect.setRectRadii(gfx::RectFToSkRect(page), radii);

    // The strip's color everywhere outside the rounded page.
    // It reaches half a pixel into the page, under the border, so the page's
    // own pixels (white on a white page) never show between the two.
    SkRRect fill_rrect;
    page_rrect.inset(0.5f, 0.5f, &fill_rrect);
    canvas->Save();
    canvas->sk_canvas()->clipRRect(fill_rrect, SkClipOp::kDifference,
                                   /*doAntiAlias=*/true);
    canvas->DrawColor(fill_);
    if (!frame_rect_.IsEmpty()) {
      canvas->FillRect(frame_rect_, frame_fill_);
    }
    canvas->Restore();

    // Faint border along the page edge: the straight part next to the strip and
    // the two arcs, but not over the thin line at the ends (it would light up
    // that line's pixels where it joins the arc).
    constexpr float rr = r - 0.5f;
    // Only the arc at the start of the page (the top, where that line is)
    // begins a little late, so the border meets the line without lighting it
    // up; the arc at the other end (the window's edge) is drawn in full.
    const float skip =
        std::acos(std::min(1.0f, (r - start_skip_px_) / rr)) *
        57.29578f;  // In degrees.
    const float w = static_cast<float>(width());
    const float h = static_cast<float>(height());
    const auto oval = [](float cx, float cy) {
      return SkRect::MakeLTRB(cx - rr, cy - rr, cx + rr, cy + rr);
    };
    SkPathBuilder path;
    switch (position_) {
      case SidebarPosition::kRight:  // Page edge at x = w.
        path.arcTo(oval(w - r, r), 270 + skip, 90 - skip, true);
        path.lineTo(SkPoint::Make(w - 0.5f, h - r));
        path.arcTo(oval(w - r, h - r), 0, 90, false);
        break;
      case SidebarPosition::kLeft:  // Page edge at x = 0.
        path.arcTo(oval(r, r), 270 - skip, -(90 - skip), true);
        path.lineTo(SkPoint::Make(0.5f, h - r));
        path.arcTo(oval(r, h - r), 180, -90, false);
        break;
      case SidebarPosition::kTop:  // Page edge at y = 0.
        path.arcTo(oval(r, r), 180 + skip, 90 - skip, true);
        path.lineTo(SkPoint::Make(w - r, 0.5f));
        path.arcTo(oval(w - r, r), 270, 90, false);
        break;
      case SidebarPosition::kBottom:  // Page edge at y = h.
        path.arcTo(oval(r, 0), 180 - skip, -(90 - skip), true);
        path.lineTo(SkPoint::Make(w - r, h - 0.5f));
        path.arcTo(oval(w - r, 0), 90, -90, false);
        break;
    }
    cc::PaintFlags flags;
    flags.setAntiAlias(true);
    flags.setStyle(cc::PaintFlags::kStroke_Style);
    flags.setStrokeWidth(1);
    flags.setColor(SkColorSetA(ForegroundColor(this), 0x26));
    canvas->sk_canvas()->drawPath(path.detach(), flags);
  }

 private:
  SidebarPosition position_ = SidebarPosition::kLeft;
  SkColor fill_ = SK_ColorTRANSPARENT;
  SkColor frame_fill_ = SK_ColorTRANSPARENT;
  gfx::Rect frame_rect_;
  int start_skip_px_ = kBorderSkipPx;
};

BEGIN_METADATA(SidebarPageEdge)
END_METADATA

void SidebarView::UpdatePageEdge() {
  if (!parent() || !GetWidget()) {
    return;
  }
  // The parent may be going through its children right now (this is called
  // from AddedToWidget(), theme and layout changes): adding a child to it here
  // would crash. Do that right after the current event instead.
  if (!page_edge_) {
    if (!page_edge_pending_) {
      page_edge_pending_ = true;
      base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
          FROM_HERE, base::BindOnce(&SidebarView::CreatePageEdge,
                                    weak_factory_.GetWeakPtr()));
    }
    return;
  }
  if (!page_edge_timer_.IsRunning()) {
    page_edge_timer_.Start(
        FROM_HERE, base::Milliseconds(50),
        base::BindRepeating(&SidebarView::UpdatePageEdge,
                            base::Unretained(this)));
  }
  const ui::ColorProvider* provider = GetColorProvider();
  const bool show =
      provider && GetVisible() && !collapsed_ && !bounds().IsEmpty();
  if (!show) {
    page_edge_->SetVisible(false);
    return;
  }

  // The part of the strip that has the page beside it. With the toolbar shown
  // the page starts under it, so the rounded corner does too; with no toolbar
  // it starts at the top of the window.
  gfx::Rect span = bounds();
  bool starts_under_toolbar = false;
  content::WebContents* page_contents =
      browser_ ? browser_->GetTabStripModel()->GetActiveWebContents()
               : nullptr;
  if (page_contents) {
    gfx::Rect page = page_contents->GetContainerBounds();  // On the screen.
    gfx::Rect strip_on_screen = bounds();
    views::View::ConvertRectToScreen(parent(), &strip_on_screen);
    page.Offset(bounds().origin() - strip_on_screen.origin());
    if (!page.IsEmpty()) {
      // A page that starts only a few pixels inside the strip's end (the thin
      // frame line along the top of the window, no toolbar) is treated as
      // starting at the very end: the corner then rounds from the frame line,
      // not under it. A toolbar is much taller, so the corner stays under it.
      const auto snap = [](int value, int limit) {
        return (value - limit <= kPageEdgeRadius &&
                limit - value <= kPageEdgeRadius)
                   ? limit
                   : value;
      };
      // Where the page is not at the strip's end (a toolbar is above it), the
      // line the browser draws along the page belongs to the rounded edge.
      bool toolbar_above = false;
      const auto fit = [&snap, &toolbar_above](int page_begin, int page_end, int span_begin,
                               int span_end, int* begin, int* end) {
        *begin = snap(std::max(span_begin, page_begin), span_begin);
        *end = snap(std::min(span_end, page_end), span_end);
        if (*begin != span_begin) {
          *begin = std::max(span_begin, *begin - kEdgeLinePx);
          toolbar_above = true;
        }
        if (*end != span_end) {
          *end = std::min(span_end, *end + kEdgeLinePx);
        }
      };
      int begin = 0;
      int end = 0;
      if (IsHorizontal()) {
        fit(page.x(), page.right(), span.x(), span.right(), &begin, &end);
        span.set_x(begin);
        span.set_width(std::max(0, end - begin));
      } else {
        fit(page.y(), page.bottom(), span.y(), span.bottom(), &begin, &end);
        span.set_y(begin);
        span.set_height(std::max(0, end - begin));
      }
      starts_under_toolbar = toolbar_above;
    }
  }
  if (span.IsEmpty()) {
    page_edge_->SetVisible(false);
    return;
  }

  // Behind the window buttons the strip is painted in the window frame's color
  // (see OnPaintBackground()); the corner has to match it there.
  const bool paint_active = GetWidget()->ShouldPaintAsActive();
  page_edge_->SetEdge(
      position(), provider->GetColor(kColorToolbar),
      provider->GetColor(paint_active ? ui::kColorFrameActive
                                      : ui::kColorFrameInactive));
  page_edge_->PlaceNextTo(span);
  page_edge_->SetStartSkip(starts_under_toolbar ? kBorderSkipToolbarPx
                                                : kBorderSkipPx);
  const gfx::Rect edge = page_edge_->bounds();
  gfx::Rect frame_rect;
  if (window_buttons_) {
    const gfx::Rect buttons = window_buttons_->bounds();
    if (IsHorizontal()) {
      const int x0 = bounds().x() + buttons.x();
      frame_rect = gfx::Rect(x0 - edge.x(), 0, bounds().right() - x0,
                             edge.height());
    } else {
      const int y0 = bounds().y();
      frame_rect = gfx::Rect(0, y0 - edge.y(), edge.width(), buttons.bottom());
    }
  }
  page_edge_->SetFrameRect(frame_rect);
  page_edge_->SetVisible(true);
}

void SidebarView::CreatePageEdge() {
  page_edge_pending_ = false;
  if (page_edge_ || !parent() || !GetWidget()) {
    return;
  }
  page_edge_ = parent()->AddChildView(std::make_unique<SidebarPageEdge>());
  UpdatePageEdge();
}

void SidebarView::OnThemeChanged() {
  views::View::OnThemeChanged();
  const ui::ColorProvider* provider = GetColorProvider();
  if (!provider) {
    return;
  }
  // Same color as the toolbar, the bookmark bar and the active tab.
  SetBackground(
      views::CreateSolidBackground(provider->GetColor(kColorToolbar)));
  ApplyThemeToAddButton();
  UpdateActiveIndicator();
  UpdatePageEdge();
}

void SidebarView::OnSidebarAppsChanged() {
  // The app list may only just have been read from disk; tabs that session
  // restore brought back earlier can be recognized now.
  SweepRestoredAppTabs();
  PruneRemovedApps();
  CloseDisabledAppTabs();
  RebuildButtons();
  ScheduleAppLaunch();
  // The VPN and the ad blocker recognise the apps by their site.
  ApplyVpn();
  ApplyAdblock();
}

void SidebarView::OnSidebarSettingsChanged() {
  ApplySettings();
}

void SidebarView::OnUnreadCountsChanged() {
  UpdateBadges();
}

void SidebarView::RebuildButtons() {
  entries_.clear();
  add_button_ = nullptr;
  settings_button_ = nullptr;
  options_button_ = nullptr;
  window_buttons_ = nullptr;
  maximize_button_ = nullptr;
  leading_children_ = 0;
  ApplyMetrics();  // The top margin depends on the window buttons.
  RemoveAllChildViews();

  // Minimize / maximize / close, when the window's own buttons are covered by
  // the open sidebar app (see WantsWindowButtons()). They come first: at the
  // top of a column (strip on the left or right), at the left of a row (bottom).
  const bool window_buttons_wanted = WantsWindowButtons();
  if (window_buttons_wanted && !IsHorizontal()) {
    window_buttons_ = AddChildView(CreateWindowButtons());
    leading_children_ = 1;
  }

  for (const SidebarApp& app : model_->apps()) {
    auto entry = std::make_unique<AppEntryView>(
        base::BindRepeating(&SidebarView::OnAppPressed, base::Unretained(this),
                            app.id),
        base::UTF8ToUTF16(app.name), button_size_, icon_size_);
    entry->SetProperty(views::kMarginsKey, EntryMargins(IsHorizontal(), side_margin_));
    entry->SetContextMenuController(this);
    entry->SetDragCallbacks(
        base::BindRepeating(&SidebarView::BeginEntryDrag,
                            base::Unretained(this), app.id),
        base::BindRepeating(&SidebarView::UpdateEntryDrag,
                            base::Unretained(this), app.id),
        base::BindRepeating(&SidebarView::EndEntryDrag, base::Unretained(this),
                            app.id));
    AppEntryView* raw_entry = entry.get();
    AddChildView(std::move(entry));
    entries_[app.id] = raw_entry;
    raw_entry->SetDisabled(app.disabled);
    auto custom = custom_icons_.find(app.id);
    auto icon = app_icons_.find(app.id);
    if (custom != custom_icons_.end()) {
      raw_entry->SetIcon(custom->second);
    } else if (icon != app_icons_.end()) {
      raw_entry->SetIcon(icon->second);
    }
    // Picks up a custom icon saved earlier (or by another window).
    LoadCustomIcon(app.id);
  }

  // An empty, stretchy view pushes the add button to the end of the strip.
  auto spacer = std::make_unique<views::View>();
  spacer->SetProperty(
      views::kFlexBehaviorKey,
      views::FlexSpecification(views::MinimumFlexSizeRule::kScaleToZero,
                               views::MaximumFlexSizeRule::kUnbounded));
  AddChildView(std::move(spacer));

  auto add_button = std::make_unique<AddButton>(
      base::BindRepeating(&SidebarView::OnAddPressed, base::Unretained(this)));
  add_button->SetTooltipText(
      u"Add the current page to the sidebar (right-click for more)");
  add_button->set_context_menu_controller(this);
  add_button->SetPreferredSize(gfx::Size(bottom_button_size_, bottom_button_size_));
  add_button->SetProperty(
      views::kMarginsKey,
      BottomButtonMargins(IsHorizontal(),
                          button_size_ - bottom_button_size_, side_margin_));
  add_button_ = AddChildView(std::move(add_button));

  auto options_button = std::make_unique<OptionsButton>(
      base::BindRepeating(&SidebarView::OnOptionsPressed,
                          base::Unretained(this)));
  options_button->SetTooltipText(u"Sidebar options");
  options_button->SetPreferredSize(
      gfx::Size(bottom_button_size_, bottom_button_size_));
  options_button->SetProperty(
      views::kMarginsKey,
      BottomButtonMargins(IsHorizontal(),
                          button_size_ - bottom_button_size_, side_margin_));
  options_button_ = AddChildView(std::move(options_button));

  auto settings_button = std::make_unique<SettingsButton>(base::BindRepeating(
      &SidebarView::OnSettingsPressed, base::Unretained(this)));
  settings_button->SetTooltipText(u"Menu");
  settings_button->SetPreferredSize(gfx::Size(bottom_button_size_, bottom_button_size_));
  settings_button->SetProperty(
      views::kMarginsKey,
      BottomButtonMargins(IsHorizontal(),
                          button_size_ - bottom_button_size_, side_margin_));
  settings_button_ = AddChildView(std::move(settings_button));
  if (window_buttons_wanted && IsHorizontal()) {
    window_buttons_ = AddChildView(CreateWindowButtons());
  }
  ApplyThemeToAddButton();

  UpdateBadges();
  UpdateActiveApp();
  UpdateWidth();
  SchedulePaint();
  // With the window buttons on the right edge the frame hides its own.
  if (HidesCaptionButtons() != caption_buttons_hidden_) {
    caption_buttons_hidden_ = HidesCaptionButtons();
    NotifyCaptionButtonsObservers();
  }
}

void SidebarView::ApplyThemeToAddButton() {
  // The button paints itself from the theme; just make it repaint.
  if (add_button_) {
    add_button_->SchedulePaint();
  }
  if (settings_button_) {
    settings_button_->SchedulePaint();
  }
  if (options_button_) {
    options_button_->SchedulePaint();
  }
}

void SidebarView::UpdateBadges() {
  for (const auto& [id, entry] : entries_) {
    const int count = model_->GetUnreadCount(id);
    entry->SetBadgeCount(model_->settings().show_badges ? count : 0);
    if (count == 0) {
      // Back to a clean state: take the site's icon again (see
      // OnAppTabUiChanged()).
      OnAppTabUiChanged(id);
    }
  }
}

void SidebarView::SetTopInset(int top_inset) {
  if (top_inset == top_inset_) {
    return;
  }
  top_inset_ = top_inset;
  // Called from the browser layout: change the margin (and the height of the
  // window buttons) right after it.
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, base::BindOnce(&SidebarView::RebuildButtons,
                                weak_factory_.GetWeakPtr()));
}

bool SidebarView::UpdateWindowButtons(bool vertical_tabs) {
  vertical_tabs_ = vertical_tabs;
  SyncWindowButtons();
  return WantsWindowButtons();
}

bool SidebarView::WantsWindowButtons() const {
  return vertical_tabs_ && position() != SidebarPosition::kTop &&
         !active_app_id_.empty() && !sidebar_apps::IsToolbarShownForApps();
}

void SidebarView::SyncWindowButtons() {
  if (window_buttons_sync_pending_ ||
      WantsWindowButtons() == (window_buttons_ != nullptr)) {
    return;
  }
  // This can be called from the browser layout (or from inside a rebuild):
  // change the buttons right after it.
  window_buttons_sync_pending_ = true;
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, base::BindOnce(&SidebarView::ApplyWindowButtons,
                                weak_factory_.GetWeakPtr()));
}

void SidebarView::ApplyWindowButtons() {
  window_buttons_sync_pending_ = false;
  RebuildButtons();
  // The strip starts at the very top when it has the buttons (right side).
  if (parent()) {
    parent()->InvalidateLayout();
  }
}

std::unique_ptr<views::View> SidebarView::CreateWindowButtons() {
  const bool horizontal = IsHorizontal();
  auto buttons = std::make_unique<views::View>();
  auto* layout =
      buttons->SetLayoutManager(std::make_unique<views::FlexLayout>());
  layout->SetOrientation(horizontal ? views::LayoutOrientation::kHorizontal
                                    : views::LayoutOrientation::kVertical);
  // Each button is a rectangle as big as the window's own caption buttons along
  // the strip, and as wide (column) or as tall (row) as the strip across it. The
  // whole rectangle lights up and takes the click.
  layout->SetCrossAxisAlignment(views::LayoutAlignment::kStretch);
  const gfx::Size native = GetNativeCaptionButtonSize();
  const int strip = model_->settings().width;
  const gfx::Size size =
      horizontal
          ? gfx::Size(std::max(native.width(), kMinWindowButtonWidth), strip)
          : gfx::Size(strip, std::max(native.height(), kMinWindowButtonHeight));
  // The window's own close button, right above on the right edge, is a few
  // DIPs to the left of the middle of the strip: the symbols line up with it.
  // Set to 0 by the user himself; keep it 0.
  constexpr int kRightGlyphShift = 0;
  const int glyph_shift_x =
      position() == SidebarPosition::kRight ? kRightGlyphShift : 0;

  // The close button is the one nearest to the corner of the window: close,
  // maximize, minimize from the top in a column; minimize, maximize, close from
  // the left in a row at the right end. On the right the window's own caption
  // buttons are hidden while these are shown (the frame asks
  // SidebarHidesCaptionButtons()).
  std::vector<WindowButton::Kind> kinds;
  if (horizontal) {
    kinds = {WindowButton::Kind::kMinimize, WindowButton::Kind::kMaximize,
             WindowButton::Kind::kClose};
  } else if (position() != SidebarPosition::kTop) {
    kinds = {WindowButton::Kind::kClose, WindowButton::Kind::kMaximize,
             WindowButton::Kind::kMinimize};
  }

  for (const WindowButton::Kind kind : kinds) {
    base::RepeatingCallback<void(const ui::Event&)> callback;
    base::RepeatingCallback<bool()> is_maximized;
    std::u16string name;
    switch (kind) {
      case WindowButton::Kind::kMinimize:
        callback = base::BindRepeating(&SidebarView::OnMinimizePressed,
                                       base::Unretained(this));
        name = u"Minimize";
        break;
      case WindowButton::Kind::kMaximize:
        callback = base::BindRepeating(&SidebarView::OnMaximizePressed,
                                       base::Unretained(this));
        is_maximized = base::BindRepeating(&SidebarView::IsWindowMaximized,
                                           base::Unretained(this));
        name = u"Maximize or restore";
        break;
      case WindowButton::Kind::kClose:
        // For the window that hosts the apps, closing hides it to the tray.
        callback = base::BindRepeating(&SidebarView::OnClosePressed,
                                       base::Unretained(this));
        name = u"Close";
        break;
    }
    auto button = std::make_unique<WindowButton>(
        std::move(callback), kind, name, std::move(is_maximized),
        /*fill_bounds=*/true, glyph_shift_x);
    button->SetPreferredSize(size);
    if (kind == WindowButton::Kind::kMaximize) {
      maximize_button_ = buttons->AddChildView(std::move(button));
    } else {
      buttons->AddChildView(std::move(button));
    }
  }
  // Room between the window buttons and the first app.
  buttons->SetProperty(views::kMarginsKey, horizontal
                                               ? gfx::Insets::TLBR(0, 12, 0, 0)
                                               : gfx::Insets::TLBR(0, 0, 12, 0));
  return buttons;
}

void SidebarView::AddedToWidget() {
  views::View::AddedToWidget();
  ApplyVpn();
  ApplyAdblock();
  // The frame's color is different for an inactive window.
  paint_as_active_subscription_ = GetWidget()->RegisterPaintAsActiveChangedCallback(
      base::BindRepeating(&SidebarView::OnPaintAsActiveChanged,
                          base::Unretained(this)));
  UpdatePageEdge();
}

void SidebarView::RemovedFromWidget() {
  paint_as_active_subscription_ = base::CallbackListSubscription();
  page_edge_timer_.Stop();
  views::View::RemovedFromWidget();
}

void SidebarView::OnPaintAsActiveChanged() {
  SchedulePaint();
  UpdatePageEdge();
}

void SidebarView::OnPaintBackground(gfx::Canvas* canvas) {
  views::View::OnPaintBackground(canvas);
  if (!window_buttons_ || collapsed_) {
    return;
  }
  const ui::ColorProvider* provider = GetColorProvider();
  if (!provider) {
    return;
  }
  // From the top of the strip (behind the window's own close button on the
  // right) down to the end of the window buttons: the color of the window
  // frame, the same as behind the close button.
  const bool active = !GetWidget() || GetWidget()->ShouldPaintAsActive();
  canvas->FillRect(
      IsHorizontal()
          ? gfx::Rect(window_buttons_->bounds().x(), 0,
                      width() - window_buttons_->bounds().x(), height())
          : gfx::Rect(0, 0, width(), window_buttons_->bounds().bottom()),
      provider->GetColor(active ? ui::kColorFrameActive
                                : ui::kColorFrameInactive));
}

void SidebarView::OnBoundsChanged(const gfx::Rect& previous_bounds) {
  // The window may have been maximized or restored: show the right symbol.
  if (maximize_button_) {
    maximize_button_->SchedulePaint();
  }
  UpdatePageEdge();
}

bool SidebarView::IsWindowMaximized() const {
  ui::BaseWindow* window = browser_ ? browser_->GetWindow() : nullptr;
  return window && window->IsMaximized();
}

void SidebarView::OnMinimizePressed(const ui::Event& event) {
  if (ui::BaseWindow* window = browser_ ? browser_->GetWindow() : nullptr) {
    window->Minimize();
  }
}

void SidebarView::OnMaximizePressed(const ui::Event& event) {
  if (ui::BaseWindow* window = browser_ ? browser_->GetWindow() : nullptr) {
    if (window->IsMaximized()) {
      window->Restore();
    } else {
      window->Maximize();
    }
  }
  if (maximize_button_) {
    maximize_button_->SchedulePaint();
  }
}

void SidebarView::OnClosePressed(const ui::Event& event) {
  // For the window that hosts the apps this hides it to the tray.
  if (ui::BaseWindow* window = browser_ ? browser_->GetWindow() : nullptr) {
    window->Close();
  }
}

void SidebarView::OnAddPressed(const ui::Event& event) {
  AddCurrentPageAsApp();
}

void SidebarView::OnOptionsPressed(const ui::Event& event) {
  ShowOptionsMenu(nullptr, ui::mojom::MenuSourceType::kMouse);
}

void SidebarView::OpenAppFromTray(const std::string& app_id) {
  if (const SidebarApp* app = model_->FindApp(app_id)) {
    OpenOrActivateApp(*app);
  }
}

void SidebarView::ShowOptionsMenu(const gfx::Point* screen_point,
                                  ui::mojom::MenuSourceType source_type) {
  if (!GetWidget()) {
    return;
  }
  if (options_menu_runner_ && options_menu_runner_->IsRunning()) {
    return;
  }
  // The runner first, then the models it was showing.
  options_menu_runner_.reset();
  options_menu_model_.reset();

  position_menu_model_ = std::make_unique<ui::SimpleMenuModel>(this);
  for (size_t i = 0; i < std::size(kPositionNames); ++i) {
    position_menu_model_->AddRadioItem(kCommandPositionBase + static_cast<int>(i),
                                       kPositionNames[i], kPositionRadioGroup);
  }
  width_menu_model_ = std::make_unique<ui::SimpleMenuModel>(this);
  for (size_t i = 0; i < std::size(kWidthPresets); ++i) {
    width_menu_model_->AddRadioItem(kCommandWidthBase + static_cast<int>(i),
                                    PresetLabel(kWidthPresets[i]),
                                    kWidthRadioGroup);
  }
  icon_menu_model_ = std::make_unique<ui::SimpleMenuModel>(this);
  for (size_t i = 0; i < std::size(kIconSizePresets); ++i) {
    icon_menu_model_->AddRadioItem(kCommandIconSizeBase + static_cast<int>(i),
                                   PresetLabel(kIconSizePresets[i]),
                                   kIconRadioGroup);
  }
  options_app_ids_.clear();
  apps_menu_model_.reset();
  if (!model_->apps().empty()) {
    apps_menu_model_ = std::make_unique<ui::SimpleMenuModel>(this);
    for (const SidebarApp& app : model_->apps()) {
      apps_menu_model_->AddCheckItem(
          kCommandAppToggleBase + static_cast<int>(options_app_ids_.size()),
          base::UTF8ToUTF16(app.name));
      options_app_ids_.push_back(app.id);
    }
  }

  // Everything about sleeping in one submenu.
  sleep_current_site_.clear();
  if (browser_) {
    if (content::WebContents* active =
            browser_->GetTabStripModel()->GetActiveWebContents()) {
      sleep_current_site_ = adblock::SiteKeyForUrl(active->GetVisibleURL());
    }
  }
  const SidebarSettings& sleep_settings = model_->settings();
  std::u16string sleep_exception_label =
      u"Add this site to exception list";
  if (!sleep_current_site_.empty()) {
    const std::u16string site = base::UTF8ToUTF16(sleep_current_site_);
    sleep_exception_label =
        ListHasSite(sleep_settings.sleep_exception_sites,
                    sleep_current_site_)
            ? u"Remove " + site + u" from the exception list"
            : u"Add " + site + u" to exception list";
  }

  sleep_menu_model_ = std::make_unique<ui::SimpleMenuModel>(this);
  sleep_menu_model_->AddCheckItem(kCommandToggleSleepTabs,
                                  u"Allow power sleeping for inactive tabs");
  sleep_menu_model_->AddCheckItem(kCommandToggleSleepApps,
                                  u"Allow power sleeping for dock apps");
  sleep_menu_model_->AddCheckItem(kCommandToggleGlobalAppSleep,
                                  u"Use global timer for dock apps");
  sleep_menu_model_->AddCheckItem(kCommandToggleIgnorePinned,
                                  u"Ignore pinned tabs");
  sleep_menu_model_->AddCheckItem(kCommandToggleSleepExceptions,
                                  u"Ignore sites in the exception list");
  sleep_menu_model_->AddSeparator(ui::NORMAL_SEPARATOR);
  sleep_menu_model_->AddItem(
      kCommandTabSleepTime,
      u"Change tabs inactivity timeout (" +
          base::NumberToString16(sleep_settings.tab_sleep_minutes) +
          u" min)\u2026");
  sleep_menu_model_->AddItem(
      kCommandAppSleepTime,
      u"Change global dock apps timeout (" +
          base::NumberToString16(sleep_settings.app_sleep_minutes) +
          u" min)\u2026");
  sleep_menu_model_->AddSeparator(ui::NORMAL_SEPARATOR);
  sleep_menu_model_->AddItem(kCommandSleepToggleException,
                             sleep_exception_label);
  sleep_menu_model_->AddItem(kCommandSleepEditExceptions,
                             u"Edit exception list\u2026");
  sleep_menu_model_->AddSeparator(ui::NORMAL_SEPARATOR);
  sleep_menu_model_->AddItem(kCommandSleepAllApps, u"Power sleep all dock apps now");
  sleep_menu_model_->AddItem(kCommandSleepAllTabs, u"Power sleep all tabs now");

  do_not_disturb_menu_model_ = std::make_unique<ui::SimpleMenuModel>(this);
  for (size_t i = 0; i < kDoNotDisturbDurations.size(); ++i) {
    do_not_disturb_menu_model_->AddItem(
        kCommandDoNotDisturbBase + static_cast<int>(i),
        kDoNotDisturbDurations[i].label);
  }
  const bool do_not_disturb = IsDoNotDisturbActive();
  if (do_not_disturb) {
    do_not_disturb_menu_model_->AddSeparator(ui::NORMAL_SEPARATOR);
    do_not_disturb_menu_model_->AddItem(
        kCommandDoNotDisturbOff,
        u"Turn off Do not Disturb");
  }

  BuildVpnMenu();
  BuildAdblockMenu();

  options_menu_model_ = std::make_unique<ui::SimpleMenuModel>(this);
  options_menu_model_->AddCheckItem(
      kCommandToggleToolbar, MenuLabel(u"\U0001F527", u"Show Toolbar on Dock Apps"));
  options_menu_model_->AddCheckItem(
      kCommandToggleAutohide, MenuLabel(u"\U0001F441", u"Auto-hide Dock"));
  options_menu_model_->AddCheckItem(
      kCommandToggleBadges, MenuLabel(u"\U0001F514", u"Show Unread Badges"));
  options_menu_model_->AddItem(
      kCommandReloadAllApps,
      MenuLabel(u"\U0001F501", u"Reload Every Dock App"));
  options_menu_model_->AddSeparator(ui::NORMAL_SEPARATOR);
  options_menu_model_->AddSubMenu(kCommandPositionMenu,
                                  MenuLabel(u"\U0001F4CC", u"Dock Position"),
                                  position_menu_model_.get());
  options_menu_model_->AddSubMenu(kCommandWidthMenu,
                                  MenuLabel(u"\U0001F4CF", u"Dock Size"),
                                  width_menu_model_.get());
  options_menu_model_->AddSubMenu(kCommandIconMenu,
                                  MenuLabel(u"\U0001F5BC", u"Icon Size"),
                                  icon_menu_model_.get());
  if (apps_menu_model_) {
    options_menu_model_->AddSubMenu(kCommandAppsMenu,
                                    MenuLabel(u"\U000023FB", u"Toggle Apps"),
                                    apps_menu_model_.get());
  }
  options_menu_model_->AddSeparator(ui::NORMAL_SEPARATOR);
  // "(on)" while any Power Sleep option is ticked, like VPN and AdBlock.
  const bool power_sleep_on =
      sleep_settings.sleep_tabs_enabled || sleep_settings.sleep_apps_enabled ||
      sleep_settings.use_global_app_sleep || sleep_settings.ignore_pinned_tabs ||
      sleep_settings.sleep_exceptions_enabled;
  options_menu_model_->AddSubMenu(
      kCommandSleepMenu,
      MenuLabel(u"\U0001F4A4",
                power_sleep_on ? u"Power Sleep (on)" : u"Power Sleep"),
      sleep_menu_model_.get());
  options_menu_model_->AddSubMenu(
      kCommandVpnMenu,
      MenuLabel(u"\U0001F512", vpn::IsEnabled(profile_) ? u"VPN (on)" : u"VPN"),
      vpn_menu_model_.get());
  options_menu_model_->AddSubMenu(
      kCommandAdblockMenu,
      MenuLabel(u"\U000026CA",
                adblock::IsEnabled(profile_) ? u"AdBlock (on)" : u"AdBlock"),
      adblock_menu_model_.get());
  options_menu_model_->AddSubMenu(
      kCommandDoNotDisturbMenu,
      MenuLabel(u"\U0001F515",
                do_not_disturb ? u"Do not Disturb (on)" : u"Do not Disturb"),
      do_not_disturb_menu_model_.get());
  options_menu_model_->AddSeparator(ui::NORMAL_SEPARATOR);
  options_menu_model_->AddCheckItem(
      kCommandToggleAutostart,
      MenuLabel(u"\U0001F680", u"Start with Windows"));
  options_menu_model_->AddItem(kCommandResetSettings,
                               MenuLabel(u"\U0001F504", u"Reset Dock Options"));

  options_menu_runner_ = std::make_unique<views::MenuRunner>(
      options_menu_model_.get(),
      screen_point
          ? views::MenuRunner::HAS_MNEMONICS | views::MenuRunner::CONTEXT_MENU
          : views::MenuRunner::HAS_MNEMONICS);
  gfx::Point anchor_point;
  views::MenuAnchorPosition anchor = views::MenuAnchorPosition::kTopLeft;
  if (screen_point) {
    // A right-click: the menu opens at the mouse.
    anchor_point = *screen_point;
  } else {
    // Opens next to the button, towards the page; the menu moves by itself
    // when there is not enough room.
    const gfx::Rect button_bounds = options_button_
                                        ? options_button_->GetBoundsInScreen()
                                        : GetBoundsInScreen();
    anchor_point = gfx::Point(button_bounds.right(), button_bounds.y());
    switch (position()) {
      case SidebarPosition::kLeft:
        break;
      case SidebarPosition::kRight:  // Opens towards the page, on the left.
        anchor_point = gfx::Point(button_bounds.x(), button_bounds.y());
        anchor = views::MenuAnchorPosition::kTopRight;
        break;
      case SidebarPosition::kTop:  // Opens below the strip.
        anchor_point = gfx::Point(button_bounds.x(), button_bounds.bottom());
        break;
      case SidebarPosition::kBottom:  // Opens above the strip.
        anchor_point = gfx::Point(button_bounds.CenterPoint().x(),
                                  button_bounds.y());
        anchor = views::MenuAnchorPosition::kBottomCenter;
        break;
    }
  }
  options_menu_runner_->RunMenuAt(GetWidget(), /*button_controller=*/nullptr,
                                  gfx::Rect(anchor_point, gfx::Size()), anchor,
                                  source_type);
}

bool SidebarView::ExecuteOptionsCommand(int command_id) {
  SidebarSettings settings = model_->settings();
  if (command_id == kCommandToggleToolbar) {
    // Shows (or hides again) the toolbar and bookmark bar on sidebar apps. The
    // window lays itself out again to match.
    sidebar_apps::SetToolbarShownForApps(
        !sidebar_apps::IsToolbarShownForApps());
    SyncWindowButtons();
    if (parent()) {
      parent()->InvalidateLayout();
    }
    return true;
  }
  if (command_id == kCommandToggleAutohide) {
    settings.autohide = !settings.autohide;
    model_->SetSettings(settings);
    return true;
  }
  if (command_id == kCommandToggleBadges) {
    settings.show_badges = !settings.show_badges;
    model_->SetSettings(settings);
    return true;
  }
  if (command_id == kCommandResetSettings) {
    // Keep the Power Sleep exception list: it is the user's own data, not a
    // look-and-feel setting.
    SidebarSettings fresh;
    fresh.sleep_exception_sites = settings.sleep_exception_sites;
    model_->SetSettings(fresh);
    return true;
  }
  if (command_id == kCommandToggleSleepApps) {
    settings.sleep_apps_enabled = !settings.sleep_apps_enabled;
    model_->SetSettings(settings);
    return true;
  }
  if (command_id == kCommandToggleSleepTabs) {
    settings.sleep_tabs_enabled = !settings.sleep_tabs_enabled;
    model_->SetSettings(settings);
    return true;
  }
  if (command_id == kCommandTabSleepTime) {
    ShowTabSleepDialog(/*for_apps=*/false);
    return true;
  }
  if (command_id == kCommandAppSleepTime) {
    ShowTabSleepDialog(/*for_apps=*/true);
    return true;
  }
  if (command_id == kCommandToggleGlobalAppSleep) {
    settings.use_global_app_sleep = !settings.use_global_app_sleep;
    model_->SetSettings(settings);
    return true;
  }
  if (command_id == kCommandToggleIgnorePinned) {
    settings.ignore_pinned_tabs = !settings.ignore_pinned_tabs;
    model_->SetSettings(settings);
    return true;
  }
  if (command_id == kCommandToggleSleepExceptions) {
    settings.sleep_exceptions_enabled = !settings.sleep_exceptions_enabled;
    model_->SetSettings(settings);
    return true;
  }
  if (command_id == kCommandSleepToggleException) {
    if (sleep_current_site_.empty()) {
      return true;
    }
    std::vector<std::string>& sites = settings.sleep_exception_sites;
    auto site_it = std::find(sites.begin(), sites.end(), sleep_current_site_);
    if (site_it != sites.end()) {
      sites.erase(site_it);
    } else {
      sites.push_back(sleep_current_site_);
      settings.sleep_exceptions_enabled = true;
    }
    model_->SetSettings(settings);
    return true;
  }
  if (command_id == kCommandSleepEditExceptions) {
    ShowSleepExceptionsDialog();
    return true;
  }
  if (command_id == kCommandToggleAutostart) {
    sidebar_apps::SetAutostartEnabled(!sidebar_apps::IsAutostartEnabled());
    // The tray menu has the same check item: rebuild it so it matches.
    SidebarTray::Get().RefreshMenu();
    return true;
  }
  if (command_id == kCommandReloadAllApps) {
    for (const SidebarApp& app : model_->apps()) {
      auto it = app_tabs_.find(app.id);
      if (app.disabled || it == app_tabs_.end() || !it->second) {
        continue;
      }
      // Same as the per-app Reload: back to the app's own address.
      content::NavigationController::LoadURLParams reload_params(app.url);
      reload_params.transition_type = ui::PAGE_TRANSITION_AUTO_BOOKMARK;
      it->second->GetController().LoadURLWithParams(reload_params);
    }
    return true;
  }
  if (command_id == kCommandSleepAllApps) {
    SidebarTray::Get().SleepAll(/*sleep_apps=*/true, /*sleep_tabs=*/false);
    return true;
  }
  if (command_id == kCommandSleepAllTabs) {
    SidebarTray::Get().SleepAll(/*sleep_apps=*/false, /*sleep_tabs=*/true);
    return true;
  }
  if (command_id == kCommandDoNotDisturbOff) {
    StopDoNotDisturb();
    return true;
  }
  const int do_not_disturb_index = command_id - kCommandDoNotDisturbBase;
  if (do_not_disturb_index >= 0 &&
      do_not_disturb_index < static_cast<int>(kDoNotDisturbDurations.size())) {
    StartDoNotDisturbForMinutes(
        kDoNotDisturbDurations[do_not_disturb_index].minutes);
    return true;
  }
  if (command_id >= kCommandVpnFirst && command_id <= kCommandVpnLast) {
    ExecuteVpnCommand(command_id);
    return true;
  }
  if (command_id >= kCommandAdblockFirst && command_id <= kCommandAdblockLast) {
    ExecuteAdblockCommand(command_id);
    return true;
  }
  const int position_index = command_id - kCommandPositionBase;
  if (position_index >= 0 &&
      position_index < static_cast<int>(std::size(kPositionNames))) {
    settings.position = static_cast<SidebarPosition>(position_index);
    model_->SetSettings(settings);
    return true;
  }
  const int width_index = command_id - kCommandWidthBase;
  if (width_index >= 0 && width_index < static_cast<int>(std::size(kWidthPresets))) {
    settings.width = kWidthPresets[width_index].value;
    model_->SetSettings(settings);
    return true;
  }
  const int icon_index = command_id - kCommandIconSizeBase;
  if (icon_index >= 0 && icon_index < static_cast<int>(std::size(kIconSizePresets))) {
    settings.icon_size = kIconSizePresets[icon_index].value;
    model_->SetSettings(settings);
    return true;
  }
  if (command_id >= kCommandAppToggleBase) {
    const size_t index = static_cast<size_t>(command_id - kCommandAppToggleBase);
    if (index < options_app_ids_.size()) {
      const std::string id = options_app_ids_[index];
      if (const SidebarApp* app = model_->FindApp(id)) {
        model_->SetAppDisabled(id, !app->disabled);
      }
    }
    return true;
  }
  return false;
}

bool SidebarView::IsCommandIdChecked(int command_id) const {
  const SidebarSettings& settings = model_->settings();
  if (command_id == kCommandToggleAutostart) {
    return sidebar_apps::IsAutostartEnabled();
  }
  if (command_id == kCommandToggleToolbar) {
    return sidebar_apps::IsToolbarShownForApps();
  }
  if (command_id == kCommandToggleAutohide) {
    return settings.autohide;
  }
  if (command_id == kCommandToggleBadges) {
    return settings.show_badges;
  }
  if (command_id == kCommandToggleSleepApps) {
    return settings.sleep_apps_enabled;
  }
  if (command_id == kCommandToggleSleepTabs) {
    return settings.sleep_tabs_enabled;
  }
  if (command_id == kCommandToggleGlobalAppSleep) {
    return settings.use_global_app_sleep;
  }
  if (command_id == kCommandToggleIgnorePinned) {
    return settings.ignore_pinned_tabs;
  }
  if (command_id == kCommandToggleSleepExceptions) {
    return settings.sleep_exceptions_enabled;
  }
  if (command_id >= kCommandVpnFirst && command_id <= kCommandVpnLast) {
    return IsVpnCommandChecked(command_id);
  }
  if (command_id >= kCommandAdblockFirst && command_id <= kCommandAdblockLast) {
    return IsAdblockCommandChecked(command_id);
  }
  const int position_index = command_id - kCommandPositionBase;
  if (position_index >= 0 &&
      position_index < static_cast<int>(std::size(kPositionNames))) {
    return static_cast<int>(settings.position) == position_index;
  }
  const int width_index = command_id - kCommandWidthBase;
  if (width_index >= 0 && width_index < static_cast<int>(std::size(kWidthPresets))) {
    return settings.width == kWidthPresets[width_index].value;
  }
  const int icon_index = command_id - kCommandIconSizeBase;
  if (icon_index >= 0 && icon_index < static_cast<int>(std::size(kIconSizePresets))) {
    return settings.icon_size == kIconSizePresets[icon_index].value;
  }
  if (command_id >= kCommandAppToggleBase) {
    const size_t index = static_cast<size_t>(command_id - kCommandAppToggleBase);
    if (index < options_app_ids_.size()) {
      const SidebarApp* app = model_->FindApp(options_app_ids_[index]);
      return app && !app->disabled;
    }
  }
  return false;
}

void SidebarView::ApplyMetrics() {
  const SidebarSettings& settings = model_->settings();
  // The button has 8 DIPs of room around its icon, but never more than the
  // strip can hold; the icon shrinks if the strip is too narrow for it.
  button_size_ = std::min(settings.icon_size + 16, settings.width - 8);
  icon_size_ = std::min(settings.icon_size, button_size_ - 4);
  // The add / options / menu buttons at the bottom are a bit smaller.
  bottom_button_size_ = std::clamp(button_size_ * 4 / 5, 24, button_size_);
  const int side = std::max(2, (settings.width - button_size_) / 2);
  side_margin_ = side;
  if (IsHorizontal()) {
    layout_->SetOrientation(views::LayoutOrientation::kHorizontal);
    // Like a column (see below), but the window buttons come last, at the right
    // end, and use the full height, so the strip has no margin there of its own.
    const int trailing_margin = WantsWindowButtons() ? 0 : 8;
    layout_->SetInteriorMargin(gfx::Insets::TLBR(0, 8, 0, trailing_margin));
  } else {
    layout_->SetOrientation(views::LayoutOrientation::kVertical);
    // With window buttons they come right after the window's own close button
    // (or the top edge, on the left), with no gap.
    const int top_margin = WantsWindowButtons() ? top_inset_ : 8 + top_inset_;
    layout_->SetInteriorMargin(gfx::Insets::TLBR(top_margin, 0, 8, 0));
  }
  SchedulePaint();
}

void SidebarView::ApplySettings() {
  ApplyMetrics();
  // The mouse may be over the strip right now (the options menu was just
  // used), in which case it must not collapse under it.
  hovered_ = IsMouseHovered();
  RebuildButtons();  // Also updates the width.
  if (model_->settings().autohide) {
    collapse_timer_.Start(FROM_HERE, kAutoHideDelay,
                          base::BindOnce(&SidebarView::OnCollapseTimer,
                                         base::Unretained(this)));
  } else {
    collapse_timer_.Stop();
  }
}

bool SidebarView::IsBusy() const {
  return !drag_app_id_.empty() ||
         (context_menu_runner_ && context_menu_runner_->IsRunning()) ||
         (options_menu_runner_ && options_menu_runner_->IsRunning()) ||
         (app_menu_ && app_menu_->IsShowing());
}

void SidebarView::UpdateWidth() {
  const SidebarSettings& settings = model_->settings();
  const bool collapse = settings.autohide && !hovered_ && !IsBusy();
  const int width = hidden_for_fullscreen_
                        ? 0
                        : (collapse ? kAutoHideStripWidth : settings.width);
  // While something is fullscreen the strip takes no room and is not shown.
  SetVisible(!hidden_for_fullscreen_);
  // Nothing is drawn or clickable on the thin edge.
  for (views::View* child : children()) {
    child->SetVisible(!collapse);
  }
  if (width == current_width_ && collapse == collapsed_ &&
      settings.position == current_position_) {
    return;
  }
  collapsed_ = collapse;
  current_width_ = width;
  current_position_ = settings.position;
  // The browser layout asks for this size: the width of a strip on the left or
  // right, the height of a strip on the top or bottom. The other dimension is
  // up to the layout.
  SetPreferredSize(IsHorizontal() ? gfx::Size(0, width)
                                  : gfx::Size(width, 0));
  if (parent()) {
    parent()->InvalidateLayout();
  }
}

void SidebarView::UpdateFullscreenHidden() {
  content::WebContents* active =
      browser_ ? browser_->GetTabStripModel()->GetActiveWebContents()
               : nullptr;
  // Follow the fullscreen events of whichever tab is active.
  if (!fullscreen_observer_ || fullscreen_observer_->web_contents() != active) {
    fullscreen_observer_ =
        active ? std::make_unique<FullscreenObserver>(
                     active,
                     base::BindRepeating(&SidebarView::UpdateFullscreenHidden,
                                         base::Unretained(this)))
               : nullptr;
  }
  ui::BaseWindow* window = browser_ ? browser_->GetWindow() : nullptr;
  const bool hide = (active && active->IsFullscreen()) ||
                    (window && window->IsFullscreen());
  if (hide == hidden_for_fullscreen_) {
    return;
  }
  hidden_for_fullscreen_ = hide;
  UpdateWidth();
}

void SidebarView::OnMouseEntered(const ui::MouseEvent& event) {
  hovered_ = true;
  UpdateWidth();
  // Keep looking at the mouse while the strip is out.
  if (model_->settings().autohide) {
    collapse_timer_.Start(FROM_HERE, kAutoHideDelay,
                          base::BindOnce(&SidebarView::OnCollapseTimer,
                                         base::Unretained(this)));
  }
}

void SidebarView::OnMouseExited(const ui::MouseEvent& event) {
  hovered_ = false;
  if (model_->settings().autohide) {
    collapse_timer_.Start(FROM_HERE, kAutoHideDelay,
                          base::BindOnce(&SidebarView::OnCollapseTimer,
                                         base::Unretained(this)));
  }
}

void SidebarView::OnCollapseTimer() {
  hovered_ = IsMouseHovered();
  UpdateWidth();
  if (hovered_ || IsBusy()) {
    // The mouse is still on the strip, a menu is open or a drag is going on:
    // look again a bit later. Once the strip has collapsed, the next touch of
    // the edge (OnMouseEntered()) starts the checks again.
    collapse_timer_.Start(FROM_HERE, kAutoHideDelay,
                          base::BindOnce(&SidebarView::OnCollapseTimer,
                                         base::Unretained(this)));
  }
}

void SidebarView::CloseAppTab(const std::string& app_id) {
  // Stop watching the tab before it goes away.
  title_observers_.erase(app_id);
  favicon_subscriptions_.erase(app_id);
  auto it = app_tabs_.find(app_id);
  if (it == app_tabs_.end()) {
    return;
  }
  base::WeakPtr<content::WebContents> tab = it->second;
  app_tabs_.erase(it);
  if (browser_ && tab) {
    TabStripModel* tab_strip = browser_->GetTabStripModel();
    const int index = tab_strip->GetIndexOfWebContents(tab.get());
    if (index != TabStripModel::kNoTab) {
      tab_strip->CloseWebContentsAt(index, /*close_types=*/0);
    }
  }
}

void SidebarView::CloseDisabledAppTabs() {
  std::vector<std::string> ids;
  for (const SidebarApp& app : model_->apps()) {
    if (app.disabled && app_tabs_.contains(app.id)) {
      ids.push_back(app.id);
    }
  }
  for (const std::string& id : ids) {
    CloseAppTab(id);
  }
}

void SidebarView::OnSettingsPressed(const ui::Event& event) {
  if (!browser_ || !settings_button_ || !settings_button_->GetWidget()) {
    return;
  }
  if (app_menu_ && app_menu_->IsShowing()) {
    return;
  }
  BrowserView* browser_view = BrowserView::GetBrowserViewForBrowser(browser_);
  if (!browser_view) {
    return;
  }
  // The same menu as the three dots of the toolbar, which is out of reach
  // while a sidebar app is shown. The menu has to be destroyed before its
  // model.
  app_menu_.reset();
  app_menu_model_ = std::make_unique<AppMenuModel>(browser_view, browser_);
  app_menu_model_->Init();
  app_menu_ = std::make_unique<AppMenu>(browser_, app_menu_model_.get(),
                                        views::MenuRunner::NO_FLAGS,
                                        base::DoNothing());
  // Opens next to the button, on the right of the sidebar; the menu moves up
  // by itself when there is not enough room below.
  const gfx::Rect button_bounds = settings_button_->GetBoundsInScreen();
  app_menu_->RunMenu(
      settings_button_->GetWidget(),
      gfx::Rect(gfx::Point(button_bounds.right(), button_bounds.y()),
                gfx::Size()),
      ui::mojom::MenuSourceType::kMouse);
}

void SidebarView::AddCurrentPageAsApp() {
  if (!browser_) {
    return;
  }
  content::WebContents* contents =
      browser_->GetTabStripModel()->GetActiveWebContents();
  const GURL url = contents ? contents->GetLastCommittedURL() : GURL();
  if (!url.is_valid() || !url.SchemeIsHTTPOrHTTPS()) {
    // A new tab, settings page and so on cannot be an app, but the button
    // still works: show an empty form to type the address into.
    ShowAppDialog(std::string());
    return;
  }

  // Suggest a name that no other app uses yet: "Discord", then "Discord 2",
  // "Discord 3", and so on. The page itself is left alone; the form creates a
  // new, separate app (with its own login) for the same address.
  const std::string base_name = NameForUrl(url);
  auto name_in_use = [this](const std::string& name) {
    for (const SidebarApp& existing : model_->apps()) {
      if (existing.name == name) {
        return true;
      }
    }
    return false;
  };
  std::string name = base_name;
  for (int n = 2; name_in_use(name); ++n) {
    name = base_name + " " + base::NumberToString(n);
  }
  // The page's current icon, so the new app already has its real icon. (Read
  // straight from the page: a sleeping page's icon is grey only when drawn.)
  gfx::ImageSkia current_icon;
  const ui::ImageModel favicon =
      ui::ImageModel::FromImage(favicon::TabFaviconFromWebContents(contents));
  if (favicon.IsImage()) {
    current_icon = favicon.GetImage().AsImageSkia();
  }
  ShowAppDialog(std::string(), url, name, current_icon);
}

void SidebarView::PruneRemovedApps() {
  for (auto it = app_tabs_.begin(); it != app_tabs_.end();) {
    if (model_->FindApp(it->first)) {
      ++it;
      continue;
    }
    const std::string id = it->first;
    if (content::WebContents* contents = it->second.get()) {
      // The tab stays open, now as an ordinary tab.
      SetHiddenAppTab(contents, false);
    }
    title_observers_.erase(id);
    favicon_subscriptions_.erase(id);
    app_icons_.erase(id);
    it = app_tabs_.erase(it);
  }
  // Delete the custom icons of apps that are gone.
  std::vector<std::string> removed_icons;
  for (const auto& [id, icon] : custom_icons_) {
    if (!model_->FindApp(id)) {
      removed_icons.push_back(id);
    }
  }
  for (const std::string& id : removed_icons) {
    RemoveCustomIcon(id);
  }
}

void SidebarView::ShowContextMenuForViewImpl(
    views::View* source,
    const gfx::Point& point,
    ui::mojom::MenuSourceType source_type) {
  if (!source->GetWidget()) {
    return;
  }
  if (source == this) {
    ShowOptionsMenu(&point, source_type);
    return;
  }
  context_menu_model_ = std::make_unique<ui::SimpleMenuModel>(this);
  context_menu_app_id_.clear();

  if (add_button_ && source == add_button_.get()) {
    context_menu_model_->AddItem(kCommandAddCurrent, u"Add the current page");
    context_menu_model_->AddItem(kCommandAddByUrl, u"Add by address...");
  } else {
    for (const auto& [id, entry] : entries_) {
      if (source == entry.get() || source == entry->button()) {
        context_menu_app_id_ = id;
        break;
      }
    }
    if (context_menu_app_id_.empty()) {
      return;
    }
    const SidebarApp* menu_app = model_->FindApp(context_menu_app_id_);
    const bool app_disabled = menu_app && menu_app->disabled;
    if (!app_disabled) {
      context_menu_model_->AddItem(kCommandBack, u"Back");
      context_menu_model_->AddItem(kCommandForward, u"Forward");
      context_menu_model_->AddItem(kCommandReload, u"Reload");
      context_menu_model_->AddItem(kCommandSetOrigin,
                                   u"Make this page the new origin");
      context_menu_model_->AddSeparator(ui::NORMAL_SEPARATOR);
      context_menu_model_->AddItem(kCommandSiteSettings, u"Site settings");
      context_menu_model_->AddSeparator(ui::NORMAL_SEPARATOR);
    }
    // Sites often change their icon when they have notifications. A kept icon
    // stays as it is (also after a restart) until the site's icon is chosen
    // again.
    if (custom_icons_.contains(context_menu_app_id_)) {
      context_menu_model_->AddItem(kCommandUseSiteIcon,
                                   u"Follow the site's icon");
    } else if (app_icons_.contains(context_menu_app_id_)) {
      context_menu_model_->AddItem(kCommandKeepIcon, u"Keep this icon");
    }
    context_menu_model_->AddItem(kCommandToggleDisabled,
                                 app_disabled ? u"Enable app" : u"Disable app");
    context_menu_model_->AddItem(kCommandEdit, u"Edit...");
    context_menu_model_->AddItem(kCommandRemove, u"Remove from sidebar");
  }

  context_menu_runner_ = std::make_unique<views::MenuRunner>(
      context_menu_model_.get(),
      views::MenuRunner::HAS_MNEMONICS | views::MenuRunner::CONTEXT_MENU);
  context_menu_runner_->RunMenuAt(
      source->GetWidget(), /*button_controller=*/nullptr,
      gfx::Rect(point, gfx::Size()), views::MenuAnchorPosition::kTopLeft,
      source_type);
}

bool SidebarView::IsCommandIdEnabled(int command_id) const {
  if (command_id == kCommandSleepToggleException) {
    return !sleep_current_site_.empty();
  }
  if (command_id == kCommandAdblockToggleSite) {
    return !adblock_current_site_.empty();
  }
  if (command_id == kCommandAdblockToggleException) {
    return !adblock_current_site_.empty();
  }
  if (command_id == kCommandAdblockStatus) {
    return false;
  }
  if (command_id == kCommandVpnToggleSite ||
      command_id == kCommandVpnToggleException) {
    return !vpn_current_site_.empty();
  }
  if (command_id == kCommandVpnTorStatus) {
    return false;
  }
  if (command_id == kCommandVpnRelaunchTor ||
      command_id == kCommandVpnReconnectTor) {
    return vpn::IsEnabled(profile_) && vpn::IsBuiltinTorSelected(profile_);
  }
  if (command_id == kCommandSetOrigin) {
    // Only when the app is on an http(s) page other than its current origin.
    const SidebarApp* app = model_->FindApp(context_menu_app_id_);
    auto tab = app_tabs_.find(context_menu_app_id_);
    if (!app || tab == app_tabs_.end() || !tab->second) {
      return false;
    }
    const GURL current = tab->second->GetLastCommittedURL();
    return current.is_valid() && current.SchemeIsHTTPOrHTTPS() &&
           current != app->url;
  }
  if (command_id != kCommandBack && command_id != kCommandForward) {
    return true;
  }
  auto it = app_tabs_.find(context_menu_app_id_);
  if (it == app_tabs_.end() || !it->second) {
    return false;
  }
  content::NavigationController& controller = it->second->GetController();
  return command_id == kCommandBack ? controller.CanGoBack()
                                    : controller.CanGoForward();
}

void SidebarView::BeginEntryDrag(const std::string& app_id,
                                 views::View* source,
                                 const gfx::Point& point) {
  auto it = entries_.find(app_id);
  if (it == entries_.end()) {
    return;
  }
  gfx::Point in_strip = point;
  views::View::ConvertPointToTarget(source, this, &in_strip);
  drag_app_id_ = app_id;
  drag_grab_offset_ = IsHorizontal() ? in_strip.x() - it->second->x()
                                     : in_strip.y() - it->second->y();
}

void SidebarView::UpdateEntryDrag(const std::string& app_id,
                                  views::View* source,
                                  const gfx::Point& point) {
  auto it = entries_.find(app_id);
  if (drag_app_id_ != app_id || it == entries_.end()) {
    return;
  }
  AppEntryView* entry = it->second;
  gfx::Point in_strip = point;
  views::View::ConvertPointToTarget(source, this, &in_strip);
  // The buttons are in a column (strip on the left or right) or in a row
  // (strip on the top or bottom); |start| is the position along it.
  const bool horizontal = IsHorizontal();
  const int extent = horizontal ? width() : height();
  const int entry_extent = horizontal ? entry->width() : entry->height();
  const int start =
      std::clamp((horizontal ? in_strip.x() : in_strip.y()) - drag_grab_offset_,
                 0, std::max(0, extent - entry_extent));

  // The place in the list is decided by how many of the other buttons are
  // above the middle of the dragged one. Those buttons are the first children.
  const int app_count = static_cast<int>(model_->apps().size());
  const int middle = start + entry_extent / 2;
  int target = 0;
  for (int i = 0; i < app_count &&
                  i + leading_children_ < static_cast<int>(children().size());
       ++i) {
    const views::View* other = children()[i + leading_children_];
    const int other_middle = horizontal ? other->x() + other->width() / 2
                                        : other->y() + other->height() / 2;
    if (other != entry && other_middle < middle) {
      ++target;
    }
  }
  const std::optional<size_t> current = GetIndexOf(entry);
  if (current && static_cast<int>(*current) != target + leading_children_) {
    ReorderChildView(entry, target + leading_children_);
    DeprecatedLayoutImmediately();
  }
  entry->SetPosition(horizontal ? gfx::Point(start, entry->y())
                                : gfx::Point(entry->x(), start));
}

void SidebarView::EndEntryDrag(const std::string& app_id,
                               views::View* source,
                               const gfx::Point& point) {
  auto it = entries_.find(app_id);
  if (drag_app_id_ != app_id || it == entries_.end()) {
    return;
  }
  drag_app_id_.clear();

  std::optional<size_t> new_index = GetIndexOf(it->second);
  // Not counting the window buttons before the app buttons.
  if (new_index && *new_index >= static_cast<size_t>(leading_children_)) {
    *new_index -= static_cast<size_t>(leading_children_);
  } else {
    new_index.reset();
  }
  const std::vector<SidebarApp>& apps = model_->apps();
  size_t old_index = apps.size();
  for (size_t i = 0; i < apps.size(); ++i) {
    if (apps[i].id == app_id) {
      old_index = i;
    }
  }
  if (!new_index || *new_index >= apps.size() || *new_index == old_index) {
    InvalidateLayout();  // Back into its place.
    return;
  }
  // Saving rebuilds all buttons, including the one whose mouse event is still
  // being handled; do it right afterwards.
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, base::BindOnce(&SidebarView::ApplyDraggedOrder,
                                weak_factory_.GetWeakPtr(), app_id,
                                *new_index));
}

void SidebarView::ApplyDraggedOrder(const std::string& app_id, size_t index) {
  model_->MoveApp(app_id, index);
}

void SidebarView::ExecuteCommand(int command_id, int event_flags) {
  // Commands that do not belong to one app.
  if (command_id == kCommandAddCurrent) {
    AddCurrentPageAsApp();
    return;
  }
  if (command_id == kCommandAddByUrl) {
    ShowAppDialog(std::string());
    return;
  }
  // Commands of the options menu.
  if (ExecuteOptionsCommand(command_id)) {
    return;
  }

  const SidebarApp* app = model_->FindApp(context_menu_app_id_);
  if (!app) {
    return;
  }
  const std::string id = app->id;  // |app| dies when the app is removed.
  switch (command_id) {
    case kCommandToggleDisabled:
      model_->SetAppDisabled(id, !app->disabled);
      break;
    case kCommandEdit:
      ShowAppDialog(id);
      break;
    case kCommandReload: {
      auto it = app_tabs_.find(id);
      if (it != app_tabs_.end() && it->second) {
        // Back to the app's own address, not the page it has wandered to.
        content::NavigationController::LoadURLParams reload_params(app->url);
        reload_params.transition_type = ui::PAGE_TRANSITION_AUTO_BOOKMARK;
        it->second->GetController().LoadURLWithParams(reload_params);
      }
      break;
    }
    case kCommandSetOrigin: {
      // The page the app is on now becomes its home: Reload and a restart
      // open this address instead of the one the app was added with.
      auto it = app_tabs_.find(id);
      if (it == app_tabs_.end() || !it->second) {
        break;
      }
      const GURL current = it->second->GetLastCommittedURL();
      if (!current.is_valid() || !current.SchemeIsHTTPOrHTTPS()) {
        break;
      }
      SidebarApp updated = *app;  // Keeps the id, name and custom icon.
      updated.url = current;
      model_->UpdateApp(updated);  // |app| may be invalid after this.
      break;
    }
    case kCommandBack:
    case kCommandForward: {
      auto it = app_tabs_.find(id);
      if (it != app_tabs_.end() && it->second) {
        content::NavigationController& controller = it->second->GetController();
        if (command_id == kCommandBack && controller.CanGoBack()) {
          controller.GoBack();
        } else if (command_id == kCommandForward && controller.CanGoForward()) {
          controller.GoForward();
        }
      }
      break;
    }
    case kCommandSiteSettings: {
      if (!browser_) {
        break;
      }
      // The settings of the site the app is on right now.
      GURL page = app->url;
      auto it = app_tabs_.find(id);
      if (it != app_tabs_.end() && it->second) {
        const GURL current = it->second->GetLastCommittedURL();
        if (current.is_valid() && current.SchemeIsHTTPOrHTTPS()) {
          page = current;
        }
      }
      const std::string site = url::Origin::Create(page).Serialize();
      content::OpenURLParams params(
          GURL("chrome://settings/content/siteDetails?site=" +
               base::EscapeQueryParamValue(site, /*use_plus=*/false)),
          content::Referrer(), WindowOpenDisposition::NEW_FOREGROUND_TAB,
          ui::PAGE_TRANSITION_AUTO_BOOKMARK, /*is_renderer_initiated=*/false);
      browser_->OpenURL(params,
                        base::OnceCallback<void(content::NavigationHandle&)>());
      break;
    }
    case kCommandKeepIcon:
      KeepCurrentIcon(id);
      break;
    case kCommandUseSiteIcon:
      RemoveCustomIcon(id);
      break;
    case kCommandRemove: {
      // Close the app's tab first, so its partition is no longer in use when
      // we try to delete the saved login and site data.
      auto tab_it = app_tabs_.find(id);
      if (browser_ && tab_it != app_tabs_.end() && tab_it->second) {
        TabStripModel* tab_strip = browser_->GetTabStripModel();
        const int index = tab_strip->GetIndexOfWebContents(tab_it->second.get());
        if (index != TabStripModel::kNoTab) {
          tab_strip->CloseWebContentsAt(index, /*close_types=*/0);
        }
      }
      model_->RemoveApp(id);
      profile_->AsyncObliterateStoragePartition(
          PartitionDomainForApp(id),
          base::BindOnce([]() {
            LOG(WARNING) << "A sidebar app's site data is still in use and "
                            "could not be deleted yet.";
          }),
          base::DoNothing());
      break;
    }
    default:
      break;
  }
}

namespace {

// Width of the form fields. The dialog is exactly this wide (plus the usual
// dialog margins), because every row is built to fit inside it.
constexpr int kDialogContentWidth = 320;
constexpr int kFieldHeight = 34;
constexpr int kTileSize = 64;
constexpr int kTileRadius = 16;
constexpr int kTileIconSize = 36;

constexpr char16_t kNameHint[] = u"Shown in tooltips. Optional.";
constexpr char16_t kUrlHint[] = u"For example https://web.whatsapp.com";
constexpr char16_t kIconHint[] = u"PNG image, up to 5 MB.";
constexpr char16_t kSleepHint[] = u"0 = never. Power Sleep ends the app's process to save memory.";

std::unique_ptr<views::Label> MakeCaption(const std::u16string& text) {
  auto label = std::make_unique<views::Label>(
      text, views::style::CONTEXT_LABEL, views::style::STYLE_EMPHASIZED);
  label->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  return label;
}

std::unique_ptr<views::Label> MakeHint(const std::u16string& text) {
  auto label = std::make_unique<views::Label>(
      text, views::style::CONTEXT_LABEL, views::style::STYLE_SECONDARY);
  label->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  return label;
}

// A hint line turns red (and shows the problem) when something is wrong.
void SetHintText(views::Label* label,
                 const std::u16string& text,
                 bool is_error) {
  label->SetText(text);
  label->SetEnabledColor(is_error ? ui::kColorAlertHighSeverity
                                    : ui::kColorLabelForegroundSecondary);
}

std::u16string LetterFor(std::u16string_view text) {
  const std::u16string trimmed(base::TrimWhitespace(text, base::TRIM_ALL));
  if (trimmed.empty()) {
    return std::u16string();
  }
  return std::u16string(1, base::ToUpperASCII(trimmed[0]));
}

// "web.whatsapp.com" -> "Whatsapp". Empty while the address is not usable yet.
std::u16string GuessAppName(std::u16string_view address) {
  std::string text(
      base::TrimWhitespaceASCII(base::UTF16ToUTF8(address), base::TRIM_ALL));
  if (text.empty()) {
    return std::u16string();
  }
  if (text.find("://") == std::string::npos) {
    text = "https://" + text;
  }
  const GURL url(text);
  if (!url.is_valid() || !url.SchemeIsHTTPOrHTTPS() ||
      url.host().find('.') == std::string::npos) {
    return std::u16string();
  }
  return base::UTF8ToUTF16(NameForUrl(url));
}

}  // namespace

// The rounded preview tile of the dialog: shows the icon the app will have
// (chosen file, saved custom icon, or the site's icon) or, when there is none,
// the first letter of its name. It updates live while the form is edited.
class IconTile : public views::View {
  METADATA_HEADER(IconTile, views::View)

 public:
  IconTile() {
    SetPreferredSize(gfx::Size(kTileSize, kTileSize));
    GetViewAccessibility().SetIsIgnored(true);
  }

  void SetIcon(const gfx::ImageSkia& icon) {
    icon_ = icon;
    SchedulePaint();
  }

  void SetLetter(const std::u16string& letter) {
    letter_ = letter;
    SchedulePaint();
  }

  // views::View:
  void OnPaint(gfx::Canvas* canvas) override {
    const SkColor color = ForegroundColor(this);
    const gfx::Rect bounds = GetLocalBounds();

    cc::PaintFlags fill;
    fill.setAntiAlias(true);
    fill.setStyle(cc::PaintFlags::kFill_Style);
    fill.setColor(SkColorSetA(color, 0x14));
    canvas->DrawRoundRect(bounds, kTileRadius, fill);

    cc::PaintFlags border;
    border.setAntiAlias(true);
    border.setStyle(cc::PaintFlags::kStroke_Style);
    border.setStrokeWidth(1);
    border.setColor(SkColorSetA(color, 0x33));
    gfx::RectF border_bounds(bounds);
    border_bounds.Inset(0.5f);
    canvas->DrawRoundRect(border_bounds, kTileRadius - 0.5f, border);

    if (!icon_.isNull() && icon_.width() > 0 && icon_.height() > 0) {
      // Fit the icon inside kTileIconSize x kTileIconSize, keeping its shape.
      const float scale =
          std::min(static_cast<float>(kTileIconSize) / icon_.width(),
                   static_cast<float>(kTileIconSize) / icon_.height());
      const int w = std::max(1, static_cast<int>(std::lround(icon_.width() * scale)));
      const int h = std::max(1, static_cast<int>(std::lround(icon_.height() * scale)));
      canvas->DrawImageInt(icon_, 0, 0, icon_.width(), icon_.height(),
                           (bounds.width() - w) / 2, (bounds.height() - h) / 2,
                           w, h, /*filter=*/true);
      return;
    }
    if (!letter_.empty()) {
      const gfx::FontList font =
          gfx::FontList().DeriveWithSizeDelta(14).DeriveWithWeight(
              gfx::Font::Weight::SEMIBOLD);
      canvas->DrawStringRectWithFlags(letter_, font, color, bounds,
                                      gfx::Canvas::TEXT_ALIGN_CENTER);
    }
  }

 private:
  gfx::ImageSkia icon_;
  std::u16string letter_;
};

BEGIN_METADATA(IconTile)
END_METADATA

// The contents of the "Add app" / "Edit app" dialog:
//
//   [ tile ]  Icon
//             PNG image, up to 5 MB.
//             [Browse...] [Use site dynamic icon]
//   Name
//   [______________________]
//   Address
//   [______________________]
//
// The icon is picked with the system file dialog and checked (decoded) right
// away, so a bad file is reported before the form is saved.
class AppDialogContents : public views::View,
                          public views::TextfieldController,
                          public ui::SelectFileDialog::Listener {
  METADATA_HEADER(AppDialogContents, views::View)

 public:
  AppDialogContents(const std::u16string& name,
                    const std::u16string& address,
                    const gfx::ImageSkia& site_icon,
                    const gfx::ImageSkia& custom_icon,
                    bool has_custom_icon,
                    int sleep_minutes)
      : site_icon_(site_icon),
        custom_icon_(custom_icon),
        has_custom_icon_(has_custom_icon) {
    auto* layout = SetLayoutManager(std::make_unique<views::FlexLayout>());
    layout->SetOrientation(views::LayoutOrientation::kVertical);
    layout->SetCrossAxisAlignment(views::LayoutAlignment::kStretch);
    // Breathing room: sides line up with the dialog title, extra space under
    // the title and above the Add / Cancel buttons.
    SetBorder(views::CreateEmptyBorder(gfx::Insets::TLBR(16, 20, 20, 20)));

    // --- Icon section -----------------------------------------------------
    auto* icon_row = AddChildView(std::make_unique<views::View>());
    auto* row_layout =
        icon_row->SetLayoutManager(std::make_unique<views::FlexLayout>());
    row_layout->SetOrientation(views::LayoutOrientation::kHorizontal);
    row_layout->SetCrossAxisAlignment(views::LayoutAlignment::kCenter);

    tile_ = icon_row->AddChildView(std::make_unique<IconTile>());

    auto* column = icon_row->AddChildView(std::make_unique<views::View>());
    column->SetProperty(views::kMarginsKey, gfx::Insets::TLBR(0, 16, 0, 0));
    auto* column_layout =
        column->SetLayoutManager(std::make_unique<views::FlexLayout>());
    column_layout->SetOrientation(views::LayoutOrientation::kVertical);
    column_layout->SetCrossAxisAlignment(views::LayoutAlignment::kStart);

    column->AddChildView(MakeCaption(u"Icon"));
    icon_hint_ = column->AddChildView(MakeHint(kIconHint));
    icon_hint_->SetProperty(views::kMarginsKey, gfx::Insets::TLBR(2, 0, 0, 0));

    auto* buttons = column->AddChildView(std::make_unique<views::View>());
    buttons->SetProperty(views::kMarginsKey, gfx::Insets::TLBR(10, 0, 0, 0));
    auto* buttons_layout =
        buttons->SetLayoutManager(std::make_unique<views::FlexLayout>());
    buttons_layout->SetOrientation(views::LayoutOrientation::kHorizontal);

    choose_button_ = buttons->AddChildView(std::make_unique<views::MdTextButton>(
        base::BindRepeating(&AppDialogContents::OnBrowsePressed,
                            base::Unretained(this)),
        u"Browse\u2026"));
    choose_button_->SetStyle(ui::ButtonStyle::kTonal);
    clear_button_ = buttons->AddChildView(std::make_unique<views::MdTextButton>(
        base::BindRepeating(&AppDialogContents::OnClearPressed,
                            base::Unretained(this)),
        u"Remove"));
    clear_button_->SetStyle(ui::ButtonStyle::kText);
    clear_button_->SetProperty(views::kMarginsKey,
                               gfx::Insets::TLBR(0, 6, 0, 0));

    // --- Name ---------------------------------------------------------------
    AddChildView(MakeCaption(u"Name"))
        ->SetProperty(views::kMarginsKey, gfx::Insets::TLBR(20, 0, 0, 0));
    name_field_ = AddChildView(MakeField(u"Name"));
    name_field_->SetText(name);
    name_hint_ = AddChildView(MakeHint(kNameHint));
    name_hint_->SetProperty(views::kMarginsKey, gfx::Insets::TLBR(4, 0, 0, 0));

    // --- Address ------------------------------------------------------------
    AddChildView(MakeCaption(u"Address"))
        ->SetProperty(views::kMarginsKey, gfx::Insets::TLBR(14, 0, 0, 0));
    url_field_ = AddChildView(MakeField(u"Address"));
    url_field_->SetText(address);
    url_field_->SetPlaceholderText(u"https://");
    url_hint_ = AddChildView(MakeHint(kUrlHint));
    url_hint_->SetProperty(views::kMarginsKey, gfx::Insets::TLBR(4, 0, 0, 0));

    // --- Sleep ----------------------------------------------------------------
    AddChildView(MakeCaption(u"Power Sleep after (minutes)"))
        ->SetProperty(views::kMarginsKey, gfx::Insets::TLBR(14, 0, 0, 0));
    sleep_field_ = AddChildView(MakeField(u"Power Sleep after (minutes)"));
    sleep_field_->SetText(base::NumberToString16(sleep_minutes));
    sleep_hint_ = AddChildView(MakeHint(kSleepHint));
    sleep_hint_->SetProperty(views::kMarginsKey, gfx::Insets::TLBR(4, 0, 0, 0));

    UpdateNamePlaceholder();
    UpdateIconControls();
    UpdateTile();
  }

  ~AppDialogContents() override {
    if (select_dialog_) {
      select_dialog_->ListenerDestroyed();
    }
  }

  views::Textfield* name_field() { return name_field_; }
  views::Textfield* url_field() { return url_field_; }
  views::Textfield* sleep_field() { return sleep_field_; }
  // The PNG picked with the file dialog (empty = none picked).
  const base::FilePath& icon_path() const { return chosen_path_; }
  // True if the user asked to drop the saved custom icon.
  bool reset_icon() const { return reset_icon_; }

  // Called after the form was refused, to say why next to the field.
  void ShowErrors() {
    if (url_field_->GetInvalid()) {
      SetHintText(url_hint_, u"Enter a valid web address", /*is_error=*/true);
    }
    if (sleep_field_->GetInvalid()) {
      SetHintText(sleep_hint_, u"Enter a whole number of minutes (0 = never)",
                  /*is_error=*/true);
    }
  }

  // views::TextfieldController:
  void ContentsChanged(views::Textfield* sender,
                       const std::u16string& new_contents) override {
    if (sender == sleep_field_) {
      sleep_field_->SetInvalid(false);
      SetHintText(sleep_hint_, kSleepHint, /*is_error=*/false);
      return;
    }
    if (sender == url_field_) {
      url_field_->SetInvalid(false);
      SetHintText(url_hint_, kUrlHint, /*is_error=*/false);
      UpdateNamePlaceholder();
    }
    UpdateTile();
  }

  // ui::SelectFileDialog::Listener:
  void FileSelected(const ui::SelectedFileInfo& file, int index) override {
    select_dialog_.reset();
    LoadPreview(file.path());
  }
  void FileSelectionCanceled() override { select_dialog_.reset(); }

 private:
  std::unique_ptr<views::Textfield> MakeField(const std::u16string& label) {
    auto field = std::make_unique<views::Textfield>();
    field->set_controller(this);
    field->GetViewAccessibility().SetName(label);
    field->SetPreferredSize(gfx::Size(kDialogContentWidth, kFieldHeight));
    field->SetProperty(views::kMarginsKey, gfx::Insets::TLBR(6, 0, 0, 0));
    return field;
  }

  void OnBrowsePressed() {
    views::Widget* widget = GetWidget();
    if (!widget) {
      return;
    }
    if (!select_dialog_) {
      select_dialog_ = ui::SelectFileDialog::Create(this, /*policy=*/nullptr);
    }
    ui::SelectFileDialog::FileTypeInfo types;
    types.extensions = {{FILE_PATH_LITERAL("png")}};
    types.extension_description_overrides = {u"PNG images"};
    types.include_all_files = false;
    select_dialog_->SelectFile(ui::SelectFileDialog::SELECT_OPEN_FILE,
                               u"Choose an icon", base::FilePath(), &types,
                               /*file_type_index=*/1,
                               FILE_PATH_LITERAL("png"),
                               widget->GetNativeWindow());
  }

  // "Remove" undoes a picked file; "Use site dynamic icon" drops the saved icon.
  void OnClearPressed() {
    if (!chosen_path_.empty()) {
      chosen_path_.clear();
      chosen_icon_ = gfx::ImageSkia();
    } else if (has_custom_icon_) {
      reset_icon_ = true;
    }
    SetHintText(icon_hint_,
                reset_icon_ ? u"The site's own icon will be used."
                            : std::u16string(kIconHint),
                /*is_error=*/false);
    UpdateIconControls();
    UpdateTile();
  }

  void LoadPreview(const base::FilePath& path) {
    IconTaskRunner()->PostTaskAndReplyWithResult(
        FROM_HERE, base::BindOnce(&DecodeIconFile, path),
        base::BindOnce(&AppDialogContents::OnPreviewDecoded,
                       weak_factory_.GetWeakPtr(), path));
  }

  void OnPreviewDecoded(const base::FilePath& path, const SkBitmap& bitmap) {
    if (bitmap.isNull()) {
      SetHintText(icon_hint_, u"Not a valid PNG (max 5 MB).",
                  /*is_error=*/true);
      return;
    }
    chosen_path_ = path;
    chosen_icon_ = gfx::ImageSkia::CreateFrom1xBitmap(bitmap);
    reset_icon_ = false;

    std::u16string file_name = path.BaseName().LossyDisplayName();
    if (file_name.size() > 28) {
      file_name = file_name.substr(0, 25) + u"\u2026";
    }
    SetHintText(icon_hint_, file_name, /*is_error=*/false);
    UpdateIconControls();
    UpdateTile();
  }

  void UpdateIconControls() {
    if (!chosen_path_.empty()) {
      clear_button_->SetText(u"Remove");
      clear_button_->SetVisible(true);
    } else if (has_custom_icon_ && !reset_icon_) {
      clear_button_->SetText(u"Use site dynamic icon");
      clear_button_->SetVisible(true);
    } else {
      clear_button_->SetVisible(false);
    }
  }

  // While the name is empty, the name that will be used (taken from the
  // address) is shown as the placeholder.
  void UpdateNamePlaceholder() {
    guessed_name_ = GuessAppName(url_field_->GetText());
    name_field_->SetPlaceholderText(guessed_name_.empty() ? u"Name"
                                                          : guessed_name_);
  }

  void UpdateTile() {
    gfx::ImageSkia icon;
    if (!chosen_path_.empty()) {
      icon = chosen_icon_;
    } else if (has_custom_icon_ && !reset_icon_) {
      icon = custom_icon_;
    } else {
      icon = site_icon_;
    }
    tile_->SetIcon(icon);
    const std::u16string name(name_field_->GetText());
    tile_->SetLetter(LetterFor(name.empty() ? guessed_name_ : name));
  }

  const gfx::ImageSkia site_icon_;
  const gfx::ImageSkia custom_icon_;
  const bool has_custom_icon_;
  gfx::ImageSkia chosen_icon_;
  base::FilePath chosen_path_;
  bool reset_icon_ = false;
  std::u16string guessed_name_;

  raw_ptr<IconTile> tile_ = nullptr;
  raw_ptr<views::Label> icon_hint_ = nullptr;
  raw_ptr<views::Label> name_hint_ = nullptr;
  raw_ptr<views::Label> url_hint_ = nullptr;
  raw_ptr<views::Textfield> sleep_field_ = nullptr;
  raw_ptr<views::Label> sleep_hint_ = nullptr;
  raw_ptr<views::MdTextButton> choose_button_ = nullptr;
  raw_ptr<views::MdTextButton> clear_button_ = nullptr;
  raw_ptr<views::Textfield> name_field_ = nullptr;
  raw_ptr<views::Textfield> url_field_ = nullptr;
  scoped_refptr<ui::SelectFileDialog> select_dialog_;
  base::WeakPtrFactory<AppDialogContents> weak_factory_{this};
};

BEGIN_METADATA(AppDialogContents)
END_METADATA

// The form that sets after how many minutes inactive regular tabs (or, with
// the global timer, sidebar apps) go to sleep.
class TabSleepDialogContents : public views::View {
  METADATA_HEADER(TabSleepDialogContents, views::View)

 public:
  TabSleepDialogContents(int minutes,
                         const std::u16string& caption,
                         const std::u16string& hint) {
    auto* layout = SetLayoutManager(std::make_unique<views::FlexLayout>());
    layout->SetOrientation(views::LayoutOrientation::kVertical);
    layout->SetCrossAxisAlignment(views::LayoutAlignment::kStretch);
    SetBorder(views::CreateEmptyBorder(gfx::Insets::TLBR(16, 20, 20, 20)));

    AddChildView(MakeCaption(caption));
    field_ = AddChildView(std::make_unique<views::Textfield>());
    field_->GetViewAccessibility().SetName(u"Minutes");
    field_->SetPreferredSize(gfx::Size(kDialogContentWidth, kFieldHeight));
    field_->SetProperty(views::kMarginsKey, gfx::Insets::TLBR(6, 0, 0, 0));
    field_->SetText(base::NumberToString16(minutes));
    hint_ = AddChildView(MakeHint(hint));
    hint_->SetProperty(views::kMarginsKey, gfx::Insets::TLBR(4, 0, 0, 0));
  }

  views::Textfield* field() { return field_; }

  void ShowError() {
    field_->SetInvalid(true);
    SetHintText(hint_, u"Enter a whole number of minutes, 1 or more.",
                /*is_error=*/true);
  }

 private:
  raw_ptr<views::Textfield> field_ = nullptr;
  raw_ptr<views::Label> hint_ = nullptr;
};

BEGIN_METADATA(TabSleepDialogContents)
END_METADATA

// The form that edits the list of sites that never go to Power Sleep: one site
// per line.
class SleepExceptionsDialogContents : public views::View {
  METADATA_HEADER(SleepExceptionsDialogContents, views::View)

 public:
  explicit SleepExceptionsDialogContents(
      const std::vector<std::string>& sites) {
    auto* layout = SetLayoutManager(std::make_unique<views::FlexLayout>());
    layout->SetOrientation(views::LayoutOrientation::kVertical);
    layout->SetCrossAxisAlignment(views::LayoutAlignment::kStretch);
    SetBorder(views::CreateEmptyBorder(gfx::Insets::TLBR(16, 20, 20, 20)));

    AddChildView(MakeCaption(u"Sites that never go to Power Sleep"));
    area_ = AddChildView(std::make_unique<views::Textarea>());
    area_->GetViewAccessibility().SetName(u"Exception sites");
    area_->SetPreferredSize(gfx::Size(kDialogContentWidth, 160));
    area_->SetProperty(views::kMarginsKey, gfx::Insets::TLBR(6, 0, 0, 0));
    area_->SetText(base::UTF8ToUTF16(base::JoinString(sites, "\n")));
    views::Label* hint =
        AddChildView(MakeHint(u"One site per line, for example example.com."));
    hint->SetProperty(views::kMarginsKey, gfx::Insets::TLBR(4, 0, 0, 0));
  }

  views::Textarea* area() { return area_; }

  // The lines, turned into the same site keys the menu item uses. Empty lines
  // and duplicates are dropped.
  std::vector<std::string> Sites() const {
    std::vector<std::string> result;
    for (const std::string& line : base::SplitString(
             base::UTF16ToUTF8(area_->GetText()), "\n", base::TRIM_WHITESPACE,
             base::SPLIT_WANT_NONEMPTY)) {
      GURL url(line);
      if (!url.has_scheme()) {
        url = GURL("https://" + line);
      }
      const std::string key = adblock::SiteKeyForUrl(url);
      if (!key.empty() && !ListHasSite(result, key)) {
        result.push_back(key);
      }
    }
    return result;
  }

 private:
  raw_ptr<views::Textarea> area_ = nullptr;
};

BEGIN_METADATA(SleepExceptionsDialogContents)
END_METADATA

void SidebarView::ShowAppDialog(const std::string& app_id,
                                const GURL& prefill_url,
                                const std::string& prefill_name,
                                const gfx::ImageSkia& prefill_icon) {
  views::Widget* parent_widget = GetWidget();
  if (!parent_widget) {
    return;
  }
  const SidebarApp* existing =
      app_id.empty() ? nullptr : model_->FindApp(app_id);
  if (!app_id.empty() && !existing) {
    return;
  }

  gfx::ImageSkia site_icon;
  gfx::ImageSkia custom_icon;
  pending_prefill_icon_ = gfx::ImageSkia();
  pending_prefill_url_ = GURL();
  if (!existing && !prefill_icon.isNull()) {
    // Shown in the form's icon tile; kept for the app that gets created.
    site_icon = prefill_icon;
    pending_prefill_icon_ = prefill_icon;
    pending_prefill_url_ = prefill_url;
  }
  if (existing) {
    if (auto it = app_icons_.find(app_id); it != app_icons_.end()) {
      site_icon = it->second;
    }
    if (auto it = custom_icons_.find(app_id); it != custom_icons_.end()) {
      custom_icon = it->second;
    }
  }
  const std::u16string address =
      existing ? base::UTF8ToUTF16(existing->url.spec())
               : (prefill_url.is_valid() ? base::UTF8ToUTF16(prefill_url.spec())
                                         : std::u16string());
  auto contents = std::make_unique<AppDialogContents>(
      existing ? base::UTF8ToUTF16(existing->name)
               : base::UTF8ToUTF16(prefill_name),
      address, site_icon, custom_icon, !custom_icon.isNull(),
      existing ? existing->sleep_minutes : 0);
  AppDialogContents* form = contents.get();

  auto dialog = std::make_unique<views::DialogDelegate>();
  dialog->SetTitle(existing ? u"Edit dock app" : u"Add dock app");
  dialog->SetModalType(ui::mojom::ModalType::kWindow);
  dialog->SetButtonLabel(ui::mojom::DialogButton::kOk,
                         existing ? u"Save" : u"Add app");
  dialog->SetButtonLabel(ui::mojom::DialogButton::kCancel, u"Cancel");
  dialog->SetContentsView(std::move(contents));
  dialog->SetInitiallyFocusedView(existing ? form->url_field()
                                           : form->name_field());
  // The form is owned by the dialog and outlives this callback, which the
  // dialog also owns. The weak pointer guards the sidebar itself.
  dialog->SetAcceptCallbackWithClose(base::BindRepeating(
      [](base::WeakPtr<SidebarView> view, AppDialogContents* form,
         std::string id) {
        if (!view) {
          return true;  // The window is gone; nothing left to save into.
        }
        const bool saved = view->OnAppDialogAccepted(
            form->name_field(), form->url_field(), form->sleep_field(),
            form->icon_path(),
            form->reset_icon(), id);
        if (!saved) {
          form->ShowErrors();
        }
        return saved;
      },
      weak_factory_.GetWeakPtr(), base::Unretained(form), app_id));

  views::Widget* widget = constrained_window::CreateBrowserModalDialogViews(
      std::move(dialog), parent_widget->GetNativeWindow());
  if (widget) {
    widget->Show();
  }
}

bool SidebarView::OnAppDialogAccepted(views::Textfield* name_field,
                                      views::Textfield* url_field,
                                      views::Textfield* sleep_field,
                                      const base::FilePath& icon_source,
                                      bool reset_icon,
                                      const std::string& app_id) {
  std::string url_text = base::UTF16ToUTF8(url_field->GetText());
  url_text = std::string(base::TrimWhitespaceASCII(url_text, base::TRIM_ALL));
  if (url_text.empty()) {
    url_field->SetInvalid(true);
    return false;
  }
  // Be forgiving: "web.whatsapp.com" means "https://web.whatsapp.com".
  if (url_text.find("://") == std::string::npos) {
    url_text = "https://" + url_text;
  }
  const GURL url(url_text);
  if (!url.is_valid() || !url.SchemeIsHTTPOrHTTPS()) {
    url_field->SetInvalid(true);
    return false;
  }

  std::string name = base::UTF16ToUTF8(name_field->GetText());
  name = std::string(base::TrimWhitespaceASCII(name, base::TRIM_ALL));
  if (name.empty()) {
    name = NameForUrl(url);
  }

  // Sleep time in minutes; empty means 0 (never).
  int sleep_minutes = 0;
  const std::string sleep_text = std::string(base::TrimWhitespaceASCII(
      base::UTF16ToUTF8(sleep_field->GetText()), base::TRIM_ALL));
  if (!sleep_text.empty() &&
      (!base::StringToInt(sleep_text, &sleep_minutes) || sleep_minutes < 0 ||
       sleep_minutes > SidebarSettings::kMaxSleepMinutes)) {
    sleep_field->SetInvalid(true);
    return false;
  }

  // |icon_source| was already decoded by the form, so it is known to be a
  // usable PNG; it is copied into the profile folder here.
  if (app_id.empty()) {
    const std::string new_id = model_->AddApp(name, url, std::string(), sleep_minutes);
    if (new_id.empty()) {
      return false;
    }
    if (!icon_source.empty()) {
      SetCustomIconFromFile(new_id, icon_source);
    } else if (!pending_prefill_icon_.isNull()) {
      if (url == pending_prefill_url_) {
        // The address was not changed: keep exactly this icon for good (saved
        // in the profile folder, survives restarts, and the site's later icon
        // changes are not followed). "Follow the site's icon" in the context
        // menu undoes this.
        SaveIconAsCustom(new_id, pending_prefill_icon_);
      } else {
        // The address was edited: the prefill icon belongs to another site.
        // Show it only until the app's own page loads and follow the site's
        // icon from then on.
        app_icons_[new_id] = pending_prefill_icon_;
        auto entry = entries_.find(new_id);
        if (entry != entries_.end()) {
          entry->second->SetIcon(pending_prefill_icon_);
        }
      }
    }
    pending_prefill_icon_ = gfx::ImageSkia();
    pending_prefill_url_ = GURL();
    return true;
  }

  const SidebarApp* existing = model_->FindApp(app_id);
  if (!existing) {
    return true;  // Removed in the meantime; nothing to save.
  }
  SidebarApp updated = *existing;
  const bool url_changed = updated.url != url;
  updated.name = name;
  updated.url = url;
  updated.sleep_minutes = sleep_minutes;
  if (!model_->UpdateApp(updated)) {
    return false;
  }
  if (!icon_source.empty()) {
    SetCustomIconFromFile(app_id, icon_source);
  } else if (reset_icon) {
    // Only "Use site dynamic icon" goes back to following the site; changing
    // the address keeps a fixed icon as it is.
    RemoveCustomIcon(app_id);
  }
  // If the address changed, send the app's page there.
  if (url_changed) {
    auto it = app_tabs_.find(app_id);
    if (it != app_tabs_.end() && it->second) {
      it->second->GetController().LoadURLWithParams(
          content::NavigationController::LoadURLParams(url));
    }
  }
  return true;
}

void SidebarView::OnAppPressed(const std::string& app_id,
                               const ui::Event& event) {
  if (const SidebarApp* app = model_->FindApp(app_id)) {
    OpenOrActivateApp(*app);
  }
}

void SidebarView::OnTabStripModelChanged(
    TabStripModel* tab_strip_model,
    const TabStripModelChange& change,
    const TabStripSelectionChange& selection) {
  // Only tabs that session restore puts back (after a restart, or when
  // "Restore" is pressed after a crash) are of interest here.
  if (change.type() != TabStripModelChange::kInserted ||
      profile_->IsOffTheRecord() || !SessionRestore::IsRestoring(profile_)) {
    return;
  }
  for (const auto& inserted : change.GetInsert()->contents) {
    if (inserted.contents) {
      restored_tabs_.push_back(inserted.contents->GetWeakPtr());
    }
  }
  // Closing tabs from inside a tab strip notification is not allowed; do it
  // right afterwards.
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, base::BindOnce(&SidebarView::SweepRestoredAppTabs,
                                weak_factory_.GetWeakPtr()));
}

void SidebarView::SweepRestoredAppTabs() {
  // Until the app list is loaded nothing can be recognized; the candidates are
  // kept and looked at again when it is.
  if (!browser_ || model_->apps().empty() || restored_tabs_.empty()) {
    return;
  }
  TabStripModel* tab_strip = browser_->GetTabStripModel();
  std::vector<base::WeakPtr<content::WebContents>> candidates;
  candidates.swap(restored_tabs_);
  for (const base::WeakPtr<content::WebContents>& candidate : candidates) {
    content::WebContents* contents = candidate.get();
    if (!contents) {
      continue;
    }
    bool is_current_app_tab = false;
    for (const auto& [id, tab] : app_tabs_) {
      is_current_app_tab = is_current_app_tab || tab.get() == contents;
    }
    if (is_current_app_tab) {
      continue;
    }
    // A restored tab of an app's site is the old copy of that app (in the
    // normal session, so logged out). The apps are started fresh instead.
    const url::Origin origin = url::Origin::Create(contents->GetVisibleURL());
    for (const SidebarApp& app : model_->apps()) {
      if (url::Origin::Create(app.url) != origin) {
        continue;
      }
      const int index = tab_strip->GetIndexOfWebContents(contents);
      if (index != TabStripModel::kNoTab) {
        tab_strip->CloseWebContentsAt(index, /*close_types=*/0);
      }
      break;
    }
  }
}

void SidebarView::OnActiveTabChanged(BrowserWindowInterface* browser) {
  UpdateFullscreenHidden();
  UpdateActiveApp();
  if (!profile_->IsOffTheRecord() && browser_) {
    SidebarTray::Get().SetLastActiveWindow(browser_->GetWeakPtr());
  }
}

void SidebarView::OpenOrActivateApp(const SidebarApp& app) {
  // A disabled app is not started; enable it first (right-click menu).
  if (!browser_ || app.disabled) {
    return;
  }
  TabStripModel* tab_strip = browser_->GetTabStripModel();

  // Already open in this window (normally started in the background at
  // launch): switch to it right away, no loading.
  auto it = app_tabs_.find(app.id);
  if (it != app_tabs_.end() && it->second) {
    const int index = tab_strip->GetIndexOfWebContents(it->second.get());
    if (index != TabStripModel::kNoTab) {
      tab_strip->ActivateTabAt(index);
      return;
    }
  }

  // Not open yet (or its tab was closed): open it in the foreground.
  OpenAppTab(app, /*foreground=*/true);
  UpdateActiveApp();
}

void SidebarView::OpenAppTab(const SidebarApp& app, bool foreground) {
  // A tab in the app's own storage partition. The partition is kept when the
  // page navigates to other sites (sign-in redirects and so on).
  const content::StoragePartitionConfig partition_config =
      content::StoragePartitionConfig::Create(
          profile_, PartitionDomainForApp(app.id), /*partition_name=*/"main",
          /*in_memory=*/false);
  scoped_refptr<content::SiteInstance> site_instance =
      content::SiteInstance::CreateForFixedStoragePartition(
          profile_, app.url, partition_config);
  std::unique_ptr<content::WebContents> new_contents =
      content::WebContents::Create(
          content::WebContents::CreateParams(profile_, site_instance));
  if (!new_contents) {
    LOG(WARNING) << "Could not open a tab for sidebar app " << app.id;
    return;
  }
  content::WebContents* contents = new_contents.get();
  // The tab strip wraps the contents in a TabModel, which attaches the tab
  // helpers itself.
  browser_->GetTabStripModel()->AppendWebContents(std::move(new_contents),
                                                  foreground);
  content::NavigationController::LoadURLParams load_params(app.url);
  load_params.transition_type = ui::PAGE_TRANSITION_AUTO_BOOKMARK;
  contents->GetController().LoadURLWithParams(load_params);
  TrackAppTab(app, contents);
}

void SidebarView::TrackAppTab(const SidebarApp& app,
                              content::WebContents* contents) {
  app_tabs_[app.id] = contents->GetWeakPtr();
  // Keep it a normal tab of the window, but make the tab strip not draw it.
  SetHiddenAppTab(contents, true);
  // Show the site's favicon on the button once the page has one.
  if (tabs::TabInterface* tab = tabs::TabInterface::GetFromContents(contents)) {
    if (TabUIHelper* helper = TabUIHelper::From(tab)) {
      favicon_subscriptions_[app.id] = helper->AddTabUIChangeCallback(
          base::BindRepeating(&SidebarView::OnAppTabUiChanged,
                              base::Unretained(this), app.id));
      OnAppTabUiChanged(app.id);
    }
  }
  // Only the host window reports unread counts, so that two windows showing
  // the same app do not overwrite each other.
  if (is_app_host_) {
    title_observers_[app.id] = std::make_unique<TitleObserver>(
        contents, DomCountScriptFor(app.url),
        base::BindRepeating(&SidebarView::OnUnreadCountParsed,
                            base::Unretained(this), app.id));
  }
}

void SidebarView::OnAppTabUiChanged(const std::string& app_id) {
  // A custom icon always wins.
  if (custom_icons_.contains(app_id)) {
    return;
  }
  // Many sites draw an unread badge into their favicon, which would sit under
  // our own badge. While the app has unread items, keep the icon we already
  // have (it was taken when there were none).
  if (app_icons_.contains(app_id) && model_->GetUnreadCount(app_id) > 0) {
    return;
  }
  auto tab_it = app_tabs_.find(app_id);
  if (tab_it == app_tabs_.end() || !tab_it->second) {
    return;
  }
  tabs::TabInterface* tab =
      tabs::TabInterface::GetFromContents(tab_it->second.get());
  TabUIHelper* helper = tab ? TabUIHelper::From(tab) : nullptr;
  if (!helper) {
    return;
  }
  // Until the site's real icon arrives this is a generic placeholder, which
  // is not an image; keep showing the letter in that case.
  const ui::ImageModel favicon = helper->GetFavicon();
  if (!favicon.IsImage()) {
    return;
  }
  const gfx::ImageSkia icon = favicon.GetImage().AsImageSkia();
  if (icon.isNull()) {
    return;
  }
  auto cached = app_icons_.find(app_id);
  if (cached != app_icons_.end() && cached->second.BackedBySameObjectAs(icon)) {
    return;
  }
  app_icons_[app_id] = icon;
  auto entry = entries_.find(app_id);
  if (entry != entries_.end()) {
    entry->second->SetIcon(icon);
  }
}

bool SidebarView::IsAppTab(const content::WebContents* contents) const {
  for (const auto& [id, tab] : app_tabs_) {
    if (tab.get() == contents) {
      return true;
    }
  }
  return false;
}

void SidebarView::OnSleepTick() {
  if (!browser_) {
    return;
  }
  const SidebarSettings& settings = model_->settings();
  const base::TimeTicks now = base::TimeTicks::Now();

  // --- Sidebar apps ---------------------------------------------------------
  // Forget apps that were removed or whose tab is gone.
  for (auto it = sleeping_apps_.begin(); it != sleeping_apps_.end();) {
    auto tab = app_tabs_.find(*it);
    if (!model_->FindApp(*it) || tab == app_tabs_.end() || !tab->second) {
      sleep_urls_.erase(*it);
      it = sleeping_apps_.erase(it);
    } else {
      ++it;
    }
  }
  for (const SidebarApp& app : model_->apps()) {
    auto tab_it = app_tabs_.find(app.id);
    content::WebContents* tab =
        tab_it == app_tabs_.end() ? nullptr : tab_it->second.get();
    if (!tab) {
      app_last_active_.erase(app.id);
      continue;
    }
    base::TimeTicks& last = app_last_active_[app.id];
    // On screen, playing sound or in a call counts as being in use.
    if (last.is_null() || tab->GetVisibility() == content::Visibility::VISIBLE ||
        tab->IsCurrentlyAudible() || tab->IsBeingCaptured()) {
      last = now;
      continue;
    }
    // The global timer (when on) replaces the app's own time.
    const int app_sleep_minutes = settings.EffectiveAppSleepMinutes(app);
    if (!settings.sleep_apps_enabled || app.disabled || app_sleep_minutes <= 0 ||
        sleeping_apps_.contains(app.id)) {
      continue;
    }
    if (now - last >= base::Minutes(app_sleep_minutes)) {
      SleepApp(app.id);
    }
  }

  // --- Regular tabs -----------------------------------------------------------
  if (!settings.sleep_tabs_enabled) {
    tab_last_visible_.clear();
    return;
  }
  TabStripModel* tab_strip = browser_->GetTabStripModel();
  std::set<uintptr_t> present;
  for (int i = 0; i < tab_strip->count(); ++i) {
    content::WebContents* contents = tab_strip->GetWebContentsAt(i);
    if (!contents || IsAppTab(contents)) {
      continue;
    }
    const uintptr_t key = reinterpret_cast<uintptr_t>(contents);
    present.insert(key);
    base::TimeTicks& last = tab_last_visible_[key];
    if (last.is_null() ||
        contents->GetVisibility() == content::Visibility::VISIBLE ||
        contents->IsCurrentlyAudible() || contents->IsBeingCaptured()) {
      last = now;
      continue;
    }
    // Pinned tabs and exception sites are left alone. Their clock restarts,
    // so removing the exemption later does not put them to sleep at once.
    if (IsSleepExempt(contents, tab_strip->IsTabPinned(i))) {
      last = now;
      continue;
    }
    // Only pages of the web are put to sleep, and only once.
    if (IsPowerSleeping(contents) ||
        !IsSleepableUrl(contents->GetLastCommittedURL())) {
      continue;
    }
    if (now - last >= base::Minutes(settings.tab_sleep_minutes)) {
      PowerSleepTab(contents);
    }
  }
  for (auto it = tab_last_visible_.begin(); it != tab_last_visible_.end();) {
    it = present.contains(it->first) ? std::next(it)
                                     : tab_last_visible_.erase(it);
  }
}

void SidebarView::SleepApp(const std::string& app_id) {
  auto tab_it = app_tabs_.find(app_id);
  if (tab_it == app_tabs_.end() || !tab_it->second ||
      sleeping_apps_.contains(app_id)) {
    return;
  }
  // Power Sleep, same as for regular tabs: the app's process is ended. The tab
  // itself stays, in the app's own storage partition, so the login is not lost,
  // and the app reloads where it was when it is shown again.
  content::WebContents* app_tab = tab_it->second.get();
  if (!IsPowerSleeping(app_tab) && !PowerSleepTab(app_tab)) {
    return;  // Shares a process with another page; tried again later.
  }
  sleeping_apps_.insert(app_id);
  if (is_app_host_) {
    model_->SetUnreadCount(app_id, 0);
  }
}

void SidebarView::WakeApp(const std::string& app_id) {
  app_last_active_[app_id] = base::TimeTicks::Now();
  sleeping_apps_.erase(app_id);
  sleep_urls_.erase(app_id);
  auto tab_it = app_tabs_.find(app_id);
  if (tab_it != app_tabs_.end() && tab_it->second) {
    // Decided by the Power Sleep mark itself, not by |sleeping_apps_|: an app
    // can be Power Slept without being in that set (restored at startup and
    // never started, slept before a reload). Does nothing if the tab is not
    // marked or is already loading.
    WakePowerSleptTab(tab_it->second.get());
  }
}

void SidebarView::SleepAll(bool sleep_apps, bool sleep_tabs) {
  if (!browser_) {
    return;
  }
  if (sleep_apps) {
    for (const SidebarApp& app : model_->apps()) {
      auto it = app_tabs_.find(app.id);
      if (app.disabled || it == app_tabs_.end() || !it->second) {
        continue;
      }
      // What is on screen stays (it would wake up again right away).
      if (it->second->GetVisibility() == content::Visibility::VISIBLE) {
        continue;
      }
      SleepApp(app.id);
    }
  }
  if (sleep_tabs) {
    TabStripModel* tab_strip = browser_->GetTabStripModel();
    for (int i = 0; i < tab_strip->count(); ++i) {
      content::WebContents* contents = tab_strip->GetWebContentsAt(i);
      if (!contents || IsAppTab(contents) ||
          contents->GetVisibility() == content::Visibility::VISIBLE ||
          IsPowerSleeping(contents) ||
          IsSleepExempt(contents, tab_strip->IsTabPinned(i)) ||
          !IsSleepableUrl(contents->GetLastCommittedURL())) {
        continue;
      }
      PowerSleepTab(contents);
    }
  }
}

bool SidebarView::IsSleepExempt(content::WebContents* contents,
                                bool pinned) const {
  const SidebarSettings& settings = model_->settings();
  if (settings.ignore_pinned_tabs && pinned) {
    return true;
  }
  if (settings.sleep_exceptions_enabled &&
      !settings.sleep_exception_sites.empty()) {
    const std::string site =
        adblock::SiteKeyForUrl(contents->GetLastCommittedURL());
    return !site.empty() &&
           ListHasSite(settings.sleep_exception_sites, site);
  }
  return false;
}

void SidebarView::WakeVisibleApps() {
  UpdateFullscreenHidden();  // Also catches F11 (window) fullscreen.

  // Safety net, once a second: anything marked as Power Sleep that is on
  // screen gets loaded, even if the normal wake-up was missed.
  for (const auto& [id, tab] : app_tabs_) {
    if (tab && tab->GetVisibility() == content::Visibility::VISIBLE &&
        IsPowerSlept(tab.get())) {
      WakeApp(id);
    }
  }
  if (browser_) {
    content::WebContents* active =
        browser_->GetTabStripModel()->GetActiveWebContents();
    if (active && active->GetVisibility() == content::Visibility::VISIBLE &&
        IsPowerSlept(active)) {
      WakePowerSleptTab(active);
    }
  }
}

void SidebarView::ShowTabSleepDialog(bool for_apps) {
  views::Widget* parent_widget = GetWidget();
  if (!parent_widget) {
    return;
  }
  const SidebarSettings& current = model_->settings();
  auto contents = std::make_unique<TabSleepDialogContents>(
      for_apps ? current.app_sleep_minutes : current.tab_sleep_minutes,
      for_apps ? u"Power Sleep sidebar apps after (minutes)"
               : u"Power Sleep inactive tabs after (minutes)",
      for_apps
          ? u"Used for every sidebar app while \"Use global timer for sidebar apps\" is on; it replaces each app's own time."
          : u"Tabs you have not looked at for this long are shut down completely and reload when you open them.");
  TabSleepDialogContents* form = contents.get();

  auto dialog = std::make_unique<views::DialogDelegate>();
  dialog->SetTitle(u"Power Sleep time");
  dialog->SetModalType(ui::mojom::ModalType::kWindow);
  dialog->SetButtonLabel(ui::mojom::DialogButton::kOk, u"Save");
  dialog->SetButtonLabel(ui::mojom::DialogButton::kCancel, u"Cancel");
  dialog->SetContentsView(std::move(contents));
  dialog->SetInitiallyFocusedView(form->field());
  dialog->SetAcceptCallbackWithClose(base::BindRepeating(
      [](base::WeakPtr<SidebarView> view, TabSleepDialogContents* form,
         bool for_apps) {
        if (!view) {
          return true;
        }
        int minutes = 0;
        const std::string text = std::string(base::TrimWhitespaceASCII(
            base::UTF16ToUTF8(form->field()->GetText()), base::TRIM_ALL));
        if (!base::StringToInt(text, &minutes) || minutes < 1 ||
            minutes > SidebarSettings::kMaxSleepMinutes) {
          form->ShowError();
          return false;
        }
        SidebarSettings settings = view->model_->settings();
        if (for_apps) {
          settings.app_sleep_minutes = minutes;
        } else {
          settings.tab_sleep_minutes = minutes;
        }
        view->model_->SetSettings(settings);
        return true;
      },
      weak_factory_.GetWeakPtr(), base::Unretained(form), for_apps));

  views::Widget* widget = constrained_window::CreateBrowserModalDialogViews(
      std::move(dialog), parent_widget->GetNativeWindow());
  if (widget) {
    widget->Show();
  }
}

void SidebarView::ShowSleepExceptionsDialog() {
  views::Widget* parent_widget = GetWidget();
  if (!parent_widget) {
    return;
  }
  auto contents = std::make_unique<SleepExceptionsDialogContents>(
      model_->settings().sleep_exception_sites);
  SleepExceptionsDialogContents* form = contents.get();

  auto dialog = std::make_unique<views::DialogDelegate>();
  dialog->SetTitle(u"Power Sleep exception list");
  dialog->SetModalType(ui::mojom::ModalType::kWindow);
  dialog->SetButtonLabel(ui::mojom::DialogButton::kOk, u"Save");
  dialog->SetButtonLabel(ui::mojom::DialogButton::kCancel, u"Cancel");
  dialog->SetContentsView(std::move(contents));
  dialog->SetInitiallyFocusedView(form->area());
  dialog->SetAcceptCallbackWithClose(base::BindRepeating(
      [](base::WeakPtr<SidebarView> view,
         SleepExceptionsDialogContents* form) {
        if (view) {
          SidebarSettings settings = view->model_->settings();
          settings.sleep_exception_sites = form->Sites();
          view->model_->SetSettings(settings);
        }
        return true;
      },
      weak_factory_.GetWeakPtr(), base::Unretained(form)));

  views::Widget* widget = constrained_window::CreateBrowserModalDialogViews(
      std::move(dialog), parent_widget->GetNativeWindow());
  if (widget) {
    widget->Show();
  }
}

void SidebarView::ApplyAdblock() {
  std::vector<GURL> app_urls;
  for (const SidebarApp& app : model_->apps()) {
    if (!app.disabled) {
      app_urls.push_back(app.url);
    }
  }
  adblock::Apply(profile_, app_urls);
}

void SidebarView::BuildAdblockMenu() {
  adblock_current_site_.clear();
  if (browser_) {
    if (content::WebContents* active =
            browser_->GetTabStripModel()->GetActiveWebContents()) {
      adblock_current_site_ = adblock::SiteKeyForUrl(active->GetVisibleURL());
    }
  }
  std::u16string site_label = u"Add this site to specific list";
  std::u16string exception_label = u"Add this site to exception list";
  if (!adblock_current_site_.empty()) {
    const std::u16string site = base::UTF8ToUTF16(adblock_current_site_);
    site_label = adblock::IsSiteListed(profile_, adblock_current_site_)
                     ? u"Remove " + site + u" from specific list"
                     : u"Add " + site + u" to specific list";
    exception_label =
        adblock::IsExcepted(profile_, adblock_current_site_)
            ? u"Remove " + site + u" from exception list"
            : u"Add " + site + u" to exception list";
  }

  adblock_menu_model_ = std::make_unique<ui::SimpleMenuModel>(this);
  const std::u16string status = adblock::StatusText(profile_);
  if (!status.empty()) {
    adblock_menu_model_->AddItem(kCommandAdblockStatus, status);
    adblock_menu_model_->AddSeparator(ui::NORMAL_SEPARATOR);
  }
  adblock_menu_model_->AddCheckItem(kCommandAdblockScopeAll,
                                    u"Block on all sites");
  adblock_menu_model_->AddCheckItem(kCommandAdblockScopeList,
                                    u"Block on sites in specific list only");
  adblock_menu_model_->AddCheckItem(kCommandAdblockApps,
                                    u"Block on dock apps");
  adblock_menu_model_->AddCheckItem(kCommandAdblockExceptionsOn,
                                    u"Ignore sites in exception list");
  adblock_menu_model_->AddSeparator(ui::NORMAL_SEPARATOR);
  adblock_menu_model_->AddItem(kCommandAdblockToggleSite, site_label);
  adblock_menu_model_->AddItem(kCommandAdblockEditSites,
                               u"Edit specific list\u2026");
  adblock_menu_model_->AddSeparator(ui::NORMAL_SEPARATOR);
  adblock_menu_model_->AddItem(kCommandAdblockToggleException,
                               exception_label);
  adblock_menu_model_->AddItem(kCommandAdblockEditExceptions,
                               u"Edit exception list\u2026");
}

bool SidebarView::IsAdblockCommandChecked(int command_id) const {
  if (command_id == kCommandAdblockScopeAll) {
    return adblock::GetScope(profile_) == adblock::Scope::kAllTabs;
  }
  if (command_id == kCommandAdblockScopeList) {
    return adblock::GetScope(profile_) == adblock::Scope::kSiteList;
  }
  if (command_id == kCommandAdblockApps) {
    return adblock::IsUsedForApps(profile_);
  }
  if (command_id == kCommandAdblockExceptionsOn) {
    return adblock::IsExceptionsEnabled(profile_);
  }
  return false;
}

void SidebarView::ExecuteAdblockCommand(int command_id) {
  const base::RepeatingClosure on_saved = base::BindRepeating(
      &SidebarView::ApplyAdblock, weak_factory_.GetWeakPtr());
  if (command_id == kCommandAdblockEditSites) {
    adblock::ShowEditDialog(GetWidget(), profile_, adblock::EditKind::kSites,
                            on_saved);
    return;
  }
  if (command_id == kCommandAdblockEditExceptions) {
    adblock::ShowEditDialog(GetWidget(), profile_,
                            adblock::EditKind::kExceptions, on_saved);
    return;
  }
  bool reload_active_tab = false;
  if (command_id == kCommandAdblockScopeAll) {
    adblock::SetScope(profile_,
                      adblock::GetScope(profile_) == adblock::Scope::kAllTabs
                          ? adblock::Scope::kOff
                          : adblock::Scope::kAllTabs);
  } else if (command_id == kCommandAdblockScopeList) {
    adblock::SetScope(profile_,
                      adblock::GetScope(profile_) == adblock::Scope::kSiteList
                          ? adblock::Scope::kOff
                          : adblock::Scope::kSiteList);
  } else if (command_id == kCommandAdblockApps) {
    adblock::SetUsedForApps(profile_, !adblock::IsUsedForApps(profile_));
  } else if (command_id == kCommandAdblockExceptionsOn) {
    adblock::SetExceptionsEnabled(profile_,
                                  !adblock::IsExceptionsEnabled(profile_));
  } else if (command_id == kCommandAdblockToggleException) {
    if (adblock_current_site_.empty()) {
      return;
    }
    if (adblock::IsExcepted(profile_, adblock_current_site_)) {
      adblock::RemoveException(profile_, adblock_current_site_);
    } else {
      adblock::AddException(profile_, adblock_current_site_);
      // An exception that is not switched on would look like it does nothing.
      adblock::SetExceptionsEnabled(profile_, true);
    }
    reload_active_tab = true;
  } else if (command_id == kCommandAdblockToggleSite) {
    if (adblock_current_site_.empty()) {
      return;
    }
    if (adblock::IsSiteListed(profile_, adblock_current_site_)) {
      adblock::RemoveSite(profile_, adblock_current_site_);
    } else {
      adblock::AddSite(profile_, adblock_current_site_);
      if (adblock::GetScope(profile_) == adblock::Scope::kOff) {
        adblock::SetScope(profile_, adblock::Scope::kSiteList);
      }
    }
    reload_active_tab = true;
  }
  ApplyAdblock();
  if (reload_active_tab && browser_) {
    if (content::WebContents* active =
            browser_->GetTabStripModel()->GetActiveWebContents()) {
      active->GetController().Reload(content::ReloadType::NORMAL,
                                     /*check_for_repost=*/true);
    }
  }
}

void SidebarView::ApplyVpn() {
  std::vector<GURL> app_urls;
  for (const SidebarApp& app : model_->apps()) {
    if (!app.disabled) {
      app_urls.push_back(app.url);
    }
  }
  vpn::Apply(profile_, app_urls);
}

void SidebarView::BuildVpnMenu() {
  // Servers ("regions"): the radio list, then the editor.
  vpn_server_menu_model_ = std::make_unique<ui::SimpleMenuModel>(this);
  const std::vector<vpn::Server> servers = vpn::GetServers(profile_);
  for (size_t i = 0; i < servers.size(); ++i) {
    vpn_server_menu_model_->AddRadioItem(
        kCommandVpnServerBase + static_cast<int>(i),
        base::UTF8ToUTF16(servers[i].name), kVpnServerRadioGroup);
  }
  if (!servers.empty()) {
    vpn_server_menu_model_->AddSeparator(ui::NORMAL_SEPARATOR);
  }
  // Exit country of the built-in Tor (only when it is installed).
  vpn_country_menu_model_ = std::make_unique<ui::SimpleMenuModel>(this);
  const bool has_builtin_tor =
      !servers.empty() && servers[0].url == vpn::kBuiltinTorUrl;
  if (has_builtin_tor) {
    for (size_t i = 0; i < vpn::kTorCountries.size(); ++i) {
      vpn_country_menu_model_->AddRadioItem(
          kCommandVpnCountryBase + static_cast<int>(i),
          vpn::kTorCountries[i].name, kVpnCountryRadioGroup);
    }
    vpn_server_menu_model_->AddSubMenu(kCommandVpnCountryMenu,
                                       u"Tor exit country",
                                       vpn_country_menu_model_.get());
    vpn_server_menu_model_->AddItem(kCommandVpnRelaunchTor,
                                    u"Relaunch Tor");
    vpn_server_menu_model_->AddItem(kCommandVpnReconnectTor,
                                    u"Reconnect to Tor");
    vpn_server_menu_model_->AddSeparator(ui::NORMAL_SEPARATOR);
  }
  vpn_server_menu_model_->AddItem(kCommandVpnEditServers,
                                  u"Edit servers\u2026");

  // The "add / remove this site" item names the site of the active tab.
  vpn_current_site_.clear();
  if (browser_) {
    if (content::WebContents* active =
            browser_->GetTabStripModel()->GetActiveWebContents()) {
      vpn_current_site_ = vpn::SiteKeyForUrl(active->GetVisibleURL());
    }
  }
  std::u16string site_label = u"Add this site to specific list";
  std::u16string exception_label = u"Add this site to exception list";
  if (!vpn_current_site_.empty()) {
    const std::u16string site = base::UTF8ToUTF16(vpn_current_site_);
    site_label = vpn::IsSiteListed(profile_, vpn_current_site_)
                     ? u"Remove " + site + u" from specific list"
                     : u"Add " + site + u" to specific list";
    exception_label = vpn::IsExcepted(profile_, vpn_current_site_)
                          ? u"Remove " + site + u" from exception list"
                          : u"Add " + site + u" to exception list";
  }

  vpn_menu_model_ = std::make_unique<ui::SimpleMenuModel>(this);
  const std::u16string tor_status = vpn::TorStatusText(profile_);
  if (!tor_status.empty()) {
    vpn_menu_model_->AddItem(kCommandVpnTorStatus, tor_status);
    vpn_menu_model_->AddSeparator(ui::NORMAL_SEPARATOR);
  }
  // The VPN is on while any of the first three options is checked. "All
  // sites" and "Specific sites only" exclude each other. The fourth option
  // (the Exception List) beats all of them but does not turn the VPN on.
  vpn_menu_model_->AddCheckItem(kCommandVpnScopeAll, u"Use for all sites");
  vpn_menu_model_->AddCheckItem(kCommandVpnScopeList,
                                u"Use for sites in specific list only");
  vpn_menu_model_->AddCheckItem(kCommandVpnApps, u"Use for dock apps");
  vpn_menu_model_->AddCheckItem(kCommandVpnExceptionsOn,
                                u"Ignore sites in exception list");
  vpn_menu_model_->AddSeparator(ui::NORMAL_SEPARATOR);
  vpn_menu_model_->AddItem(kCommandVpnToggleSite, site_label);
  vpn_menu_model_->AddItem(kCommandVpnEditSites,
                           u"Edit specific list\u2026");
  vpn_menu_model_->AddSeparator(ui::NORMAL_SEPARATOR);
  vpn_menu_model_->AddItem(kCommandVpnToggleException, exception_label);
  vpn_menu_model_->AddItem(kCommandVpnEditExceptions,
                           u"Edit exception list\u2026");
  vpn_menu_model_->AddSeparator(ui::NORMAL_SEPARATOR);
  vpn_menu_model_->AddSubMenu(kCommandVpnServerMenu, u"Server / Region",
                              vpn_server_menu_model_.get());
}

bool SidebarView::IsVpnCommandChecked(int command_id) const {
  if (command_id == kCommandVpnScopeAll) {
    return vpn::GetScope(profile_) == vpn::Scope::kAllTabs;
  }
  if (command_id == kCommandVpnScopeList) {
    return vpn::GetScope(profile_) == vpn::Scope::kSiteList;
  }
  if (command_id == kCommandVpnApps) {
    return vpn::IsUsedForApps(profile_);
  }
  if (command_id == kCommandVpnExceptionsOn) {
    return vpn::IsExceptionsEnabled(profile_);
  }
  const int country_index = command_id - kCommandVpnCountryBase;
  if (country_index >= 0 &&
      country_index < static_cast<int>(vpn::kTorCountries.size())) {
    return vpn::GetTorCountry(profile_) == vpn::kTorCountries[country_index].code;
  }
  const int server_index = command_id - kCommandVpnServerBase;
  if (server_index >= 0 && server_index < vpn::kMaxServers) {
    return server_index == vpn::GetSelectedServerIndex(profile_);
  }
  return false;
}

void SidebarView::ExecuteVpnCommand(int command_id) {
  const base::RepeatingClosure on_saved =
      base::BindRepeating(&SidebarView::ApplyVpn, weak_factory_.GetWeakPtr());
  if (command_id == kCommandVpnEditSites) {
    vpn::ShowEditDialog(GetWidget(), profile_, vpn::EditKind::kSites, on_saved);
    return;
  }
  if (command_id == kCommandVpnEditExceptions) {
    vpn::ShowEditDialog(GetWidget(), profile_, vpn::EditKind::kExceptions,
                        on_saved);
    return;
  }
  if (command_id == kCommandVpnEditServers) {
    vpn::ShowEditDialog(GetWidget(), profile_, vpn::EditKind::kServers,
                        on_saved);
    return;
  }
  bool reload_active_tab = false;
  if (command_id == kCommandVpnScopeAll) {
    vpn::SetScope(profile_, vpn::GetScope(profile_) == vpn::Scope::kAllTabs
                                ? vpn::Scope::kOff
                                : vpn::Scope::kAllTabs);
  } else if (command_id == kCommandVpnScopeList) {
    vpn::SetScope(profile_, vpn::GetScope(profile_) == vpn::Scope::kSiteList
                                ? vpn::Scope::kOff
                                : vpn::Scope::kSiteList);
  } else if (command_id == kCommandVpnApps) {
    vpn::SetUsedForApps(profile_, !vpn::IsUsedForApps(profile_));
  } else if (command_id == kCommandVpnExceptionsOn) {
    vpn::SetExceptionsEnabled(profile_, !vpn::IsExceptionsEnabled(profile_));
  } else if (command_id == kCommandVpnToggleException) {
    if (vpn_current_site_.empty()) {
      return;
    }
    if (vpn::IsExcepted(profile_, vpn_current_site_)) {
      vpn::RemoveException(profile_, vpn_current_site_);
    } else {
      vpn::AddException(profile_, vpn_current_site_);
      // An exception that is not switched on would look like it does nothing.
      vpn::SetExceptionsEnabled(profile_, true);
    }
    // Load the page again through its new route (only matters if the VPN is
    // on for something).
    reload_active_tab = vpn::IsEnabled(profile_);
  } else if (command_id == kCommandVpnToggleSite) {
    if (vpn_current_site_.empty()) {
      return;
    }
    if (vpn::IsSiteListed(profile_, vpn_current_site_)) {
      vpn::RemoveSite(profile_, vpn_current_site_);
    } else {
      vpn::AddSite(profile_, vpn_current_site_);
      // Adding a site while the VPN is off for regular tabs switches the list
      // on (when "all sites" is on the site is just remembered).
      if (vpn::GetScope(profile_) == vpn::Scope::kOff) {
        vpn::SetScope(profile_, vpn::Scope::kSiteList);
      }
    }
    // The page is the reason the site was added or removed: load it again
    // through its new route.
    reload_active_tab = vpn::GetScope(profile_) == vpn::Scope::kSiteList;
  } else if (command_id == kCommandVpnRelaunchTor) {
    vpn::RelaunchTor(profile_);
  } else if (command_id == kCommandVpnReconnectTor) {
    vpn::ReconnectTor(profile_);
    reload_active_tab = true;
  } else if (command_id >= kCommandVpnCountryBase &&
             command_id < kCommandVpnCountryBase +
                              static_cast<int>(vpn::kTorCountries.size())) {
    vpn::SetTorCountry(
        profile_,
        vpn::kTorCountries[command_id - kCommandVpnCountryBase].code);
  } else if (command_id >= kCommandVpnServerBase &&
             command_id < kCommandVpnServerBase + vpn::kMaxServers) {
    vpn::SetSelectedServerIndex(profile_, command_id - kCommandVpnServerBase);
  }
  ApplyVpn();
  if (reload_active_tab && browser_) {
    if (content::WebContents* active =
            browser_->GetTabStripModel()->GetActiveWebContents()) {
      active->GetController().Reload(content::ReloadType::NORMAL,
                                     /*check_for_repost=*/true);
    }
  }
}

void SidebarView::OnUnreadCountParsed(const std::string& app_id, int count) {
  model_->SetUnreadCount(app_id, count);
}

void SidebarView::ScheduleAppLaunch() {
  // Incognito windows never start the apps, and only one window per profile
  // does, so that a second window does not start a second copy of everything.
  if (!browser_ || profile_->IsOffTheRecord() || model_->apps().empty()) {
    return;
  }
  if (!is_app_host_) {
    is_app_host_ = model_->TryClaimAppHost(this);
    if (is_app_host_) {
      // Closing this window will now hide it to the tray.
      SidebarTray::Get().SetHostWindow(browser_->GetWeakPtr());
      SidebarTray::Get().SetOpenAppCallback(base::BindRepeating(
          &SidebarView::OpenAppFromTray, weak_factory_.GetWeakPtr()));
    }
  }
  if (!is_app_host_) {
    return;
  }
  launch_timer_.Start(FROM_HERE, kAppLaunchDelay,
                      base::BindOnce(&SidebarView::LaunchMissingApps,
                                     base::Unretained(this)));
}

void SidebarView::LaunchMissingApps() {
  if (!browser_) {
    return;
  }
  SweepRestoredAppTabs();

  // Once per run, when the apps are first started: tabs left over from the
  // last session that show an app's site are old copies of the apps (they come
  // back in the normal session). Close them; the apps are opened fresh below.
  // This also covers a restore that was not reported as one while it ran.
  static bool startup_sweep_done = false;
  if (!startup_sweep_done) {
    startup_sweep_done = true;
    TabStripModel* tab_strip = browser_->GetTabStripModel();
    for (int i = tab_strip->count() - 1; i >= 0; --i) {
      content::WebContents* contents = tab_strip->GetWebContentsAt(i);
      if (!contents || contents->GetController().GetEntryCount() == 0) {
        continue;
      }
      bool is_current_app_tab = false;
      for (const auto& [id, tab] : app_tabs_) {
        is_current_app_tab = is_current_app_tab || tab.get() == contents;
      }
      if (is_current_app_tab) {
        continue;
      }
      const url::Origin origin = url::Origin::Create(contents->GetVisibleURL());
      for (const SidebarApp& app : model_->apps()) {
        if (url::Origin::Create(app.url) == origin) {
          tab_strip->CloseWebContentsAt(i, /*close_types=*/0);
          break;
        }
      }
    }
  }

  for (const SidebarApp& app : model_->apps()) {
    if (app.disabled) {
      continue;
    }
    auto it = app_tabs_.find(app.id);
    if (it != app_tabs_.end() && it->second) {
      continue;  // Already has a tab in this window.
    }
    OpenAppTab(app, /*foreground=*/false);
  }
  UpdateActiveApp();
}

void SidebarView::UpdateActiveApp() {
  const bool was_app_shown = !active_app_id_.empty();
  active_app_id_.clear();
  if (browser_) {
    content::WebContents* active =
        browser_->GetTabStripModel()->GetActiveWebContents();
    for (const auto& [id, tab] : app_tabs_) {
      if (active && tab.get() == active) {
        active_app_id_ = id;
        break;
      }
    }
  }
  UpdateActiveIndicator();

  // The toolbar and the bookmark bar are hidden while an app is shown, so the
  // window has to lay itself out again whenever that changes.
  if (was_app_shown != !active_app_id_.empty() && parent()) {
    parent()->InvalidateLayout();
  }
  // An app that was asleep wakes up as soon as it is shown.
  if (!active_app_id_.empty()) {
    WakeApp(active_app_id_);
  }
  SyncWindowButtons();
}

void SidebarView::UpdateActiveIndicator() {
  for (const auto& [id, entry] : entries_) {
    entry->SetActive(id == active_app_id_);
  }
}

base::FilePath SidebarView::CustomIconPath(const std::string& app_id) const {
  if (!IsSafeFileName(app_id)) {
    return base::FilePath();
  }
  return profile_->GetPath()
      .AppendASCII("SidebarIcons")
      .AppendASCII(app_id + ".png");
}

void SidebarView::SetCustomIconFromFile(const std::string& app_id,
                                        const base::FilePath& source) {
  const base::FilePath dest = CustomIconPath(app_id);
  if (dest.empty()) {
    return;
  }
  IconTaskRunner()->PostTaskAndReplyWithResult(
      FROM_HERE, base::BindOnce(&ImportIconFile, source, dest),
      base::BindOnce(&SidebarView::OnCustomIconLoaded,
                     weak_factory_.GetWeakPtr(), app_id,
                     /*report_failure=*/true));
}

void SidebarView::LoadCustomIcon(const std::string& app_id) {
  const base::FilePath path = CustomIconPath(app_id);
  if (path.empty()) {
    return;
  }
  IconTaskRunner()->PostTaskAndReplyWithResult(
      FROM_HERE, base::BindOnce(&DecodeIconFile, path),
      base::BindOnce(&SidebarView::OnCustomIconLoaded,
                     weak_factory_.GetWeakPtr(), app_id,
                     /*report_failure=*/false));
}

void SidebarView::OnCustomIconLoaded(const std::string& app_id,
                                     bool report_failure,
                                     const SkBitmap& bitmap) {
  if (bitmap.isNull()) {
    // No custom icon saved for this app is the normal case.
    LOG_IF(WARNING, report_failure)
        << "Could not use the chosen icon file for sidebar app " << app_id
        << " (it must be a PNG of at most 5 MB).";
    return;
  }
  if (!model_->FindApp(app_id)) {
    return;
  }
  const gfx::ImageSkia icon = gfx::ImageSkia::CreateFrom1xBitmap(bitmap);
  custom_icons_[app_id] = icon;
  auto entry = entries_.find(app_id);
  if (entry != entries_.end()) {
    entry->second->SetIcon(icon);
  }
}

void SidebarView::KeepCurrentIcon(const std::string& app_id) {
  auto icon = app_icons_.find(app_id);
  if (icon == app_icons_.end()) {
    return;
  }
  // Copy first: SaveIconAsCustom() does not touch |app_icons_|, but a copy
  // keeps this safe if that ever changes.
  const gfx::ImageSkia current = icon->second;
  SaveIconAsCustom(app_id, current);
}

void SidebarView::SaveIconAsCustom(const std::string& app_id,
                                   const gfx::ImageSkia& icon) {
  const base::FilePath dest = CustomIconPath(app_id);
  if (icon.isNull() || dest.empty()) {
    return;
  }
  const gfx::ImageSkiaRep rep = icon.GetRepresentation(1.0f);
  if (rep.is_null()) {
    return;
  }
  const SkBitmap bitmap = rep.GetBitmap();
  std::optional<std::vector<uint8_t>> png =
      gfx::PNGCodec::EncodeBGRASkBitmap(bitmap, /*discard_transparency=*/false);
  if (!png) {
    LOG(WARNING) << "Could not keep the icon of sidebar app " << app_id;
    return;
  }
  IconTaskRunner()->PostTask(
      FROM_HERE, base::BindOnce(&WriteIconFile, dest, std::move(*png)));
  // From now on the saved icon is the app's custom icon: the site's icon is
  // no longer followed, and it is loaded again after a restart.
  custom_icons_[app_id] = icon;
  auto entry = entries_.find(app_id);
  if (entry != entries_.end()) {
    entry->second->SetIcon(icon);
  }
}

void SidebarView::RemoveCustomIcon(const std::string& app_id) {
  custom_icons_.erase(app_id);
  const base::FilePath path = CustomIconPath(app_id);
  if (!path.empty()) {
    IconTaskRunner()->PostTask(
        FROM_HERE, base::BindOnce(
                       [](const base::FilePath& file) {
                         base::DeleteFile(file);
                       },
                       path));
  }
  // Back to the site's own icon (or the letter until it has one).
  app_icons_.erase(app_id);
  auto entry = entries_.find(app_id);
  if (entry != entries_.end()) {
    entry->second->ClearIcon();
  }
  OnAppTabUiChanged(app_id);
}

BEGIN_METADATA(SidebarView)
END_METADATA

void SetSidebarTopInset(views::View* sidebar_view, int top_inset) {
  static_cast<SidebarView*>(sidebar_view)->SetTopInset(top_inset);
}

namespace {

gfx::Size& NativeCaptionButtonSize() {
  static gfx::Size size(46, 32);
  return size;
}

base::RepeatingClosureList& CaptionButtonsObservers() {
  static base::NoDestructor<base::RepeatingClosureList> observers;
  return *observers;
}

}  // namespace

base::CallbackListSubscription AddCaptionButtonsObserver(
    base::RepeatingClosure callback) {
  return CaptionButtonsObservers().Add(std::move(callback));
}

void SetNativeCaptionButtonSize(const gfx::Size& size) {
  if (size.width() > 0 && size.height() > 0) {
    NativeCaptionButtonSize() = size;
  }
}

gfx::Size GetNativeCaptionButtonSize() {
  return NativeCaptionButtonSize();
}

void NotifyCaptionButtonsObservers() {
  CaptionButtonsObservers().Notify();
}

bool SidebarHidesCaptionButtons(const views::View* sidebar_view) {
  return static_cast<const SidebarView*>(sidebar_view)->HidesCaptionButtons();
}

bool UpdateSidebarWindowButtons(views::View* sidebar_view, bool vertical_tabs) {
  return static_cast<SidebarView*>(sidebar_view)
      ->UpdateWindowButtons(vertical_tabs);
}

SidebarPosition GetSidebarPosition(const views::View* sidebar_view) {
  // Only SidebarView uses kSidebarViewId.
  return static_cast<const SidebarView*>(sidebar_view)->position();
}

}  // namespace sidebar_apps
