// Copyright 2026 MDJ. Use of this source code is governed by a BSD-style
// license that can be found in the Chromium LICENSE file.

#include "chrome/browser/ui/views/sidebar_apps/sidebar_tray.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <memory>
#include <utility>

#include "base/command_line.h"
#include "base/functional/bind.h"
#include "base/no_destructor.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/single_thread_task_runner.h"
#include "base/time/time.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/lifetime/application_lifetime.h"
#include "chrome/browser/lifetime/browser_shutdown.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/profiles/profile_manager.h"
#include "chrome/browser/status_icons/status_icon.h"
#include "chrome/browser/status_icons/status_tray.h"
#include "chrome/browser/ui/browser_commands.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/views/sidebar_apps/sidebar_autostart.h"
#include "chrome/browser/ui/views/sidebar_apps/sidebar_do_not_disturb.h"
#include "components/prefs/pref_service.h"
#include "third_party/skia/include/core/SkBitmap.h"
#include "third_party/skia/include/core/SkCanvas.h"
#include "third_party/skia/include/core/SkColor.h"
#include "third_party/skia/include/core/SkPaint.h"
#include "third_party/skia/include/core/SkRect.h"
#include "ui/base/base_window.h"
#include "ui/gfx/image/image_skia.h"

namespace sidebar_apps {

namespace {

constexpr int kIconSize = 32;

constexpr int kCommandOpen = 1;
constexpr int kCommandAutostart = 2;
constexpr int kCommandQuit = 3;
constexpr int kCommandSleepAllApps = 4;
constexpr int kCommandSleepAllTabs = 5;
constexpr int kCommandDoNotDisturbMenu = 6;
constexpr int kCommandDoNotDisturbOff = 7;
constexpr int kCommandSleepMenu = 8;
constexpr int kCommandNewWindow = 30;
constexpr int kCommandNewIncognitoWindow = 31;
// The Do Not Disturb durations are kCommandDoNotDisturbBase + their position.
constexpr int kCommandDoNotDisturbBase = 10;
// The apps in the menu are kCommandAppBase + their position in the list.
constexpr int kCommandAppBase = 100;

// Windows keeps at most 127 characters of a tray tooltip.
constexpr size_t kMaxToolTipLength = 127;
// Longer app names are cut in the tooltip so that more apps fit.
constexpr size_t kMaxToolTipNameLength = 22;

// The switch the Windows startup entry passes: start with the window hidden.
constexpr char kStartInTraySwitch[] = "start-in-tray";
// How long after startup windows stay hidden when started in the tray.
constexpr base::TimeDelta kStartInTrayWindow = base::Seconds(20);

// Set when the user opened the window from the tray; ends the hidden start.
bool g_start_in_tray_ended = false;

// Start the browser with this switch to make the close button close the
// window normally (handy while developing, so a rebuild is never blocked by a
// browser that is still running in the tray).
constexpr char kDisableCloseToTraySwitch[] = "disable-close-to-tray";

// A 3x5 pixel font for the digits 0-9. Each row is 3 bits, top row first.
// Drawing the digits by hand keeps the tray icon independent of any font
// setup.
constexpr std::array<std::array<uint8_t, 5>, 10> kDigitRows{{
    {{0b111, 0b101, 0b101, 0b101, 0b111}},  // 0
    {{0b010, 0b110, 0b010, 0b010, 0b111}},  // 1
    {{0b111, 0b001, 0b111, 0b100, 0b111}},  // 2
    {{0b111, 0b001, 0b111, 0b001, 0b111}},  // 3
    {{0b101, 0b101, 0b111, 0b001, 0b001}},  // 4
    {{0b111, 0b100, 0b111, 0b001, 0b111}},  // 5
    {{0b111, 0b100, 0b111, 0b101, 0b111}},  // 6
    {{0b111, 0b001, 0b001, 0b001, 0b001}},  // 7
    {{0b111, 0b101, 0b111, 0b101, 0b111}},  // 8
    {{0b111, 0b101, 0b111, 0b001, 0b111}},  // 9
}};

// The notification dot in the top-right corner.
constexpr SkColor kDotColor = SkColorSetRGB(0xFF, 0x3B, 0x30);
constexpr float kDotRadius = 5.0f;
// The dot's center. It is pushed right up against the right edge of the icon
// (it spans x = 22.5..32.5, so its outermost half pixel is cut off) and the top
// edge (y = 0..10).
constexpr float kDotCenterX = 27.5f;
constexpr float kDotCenterY = 5.0f;

// A pure white check mark (nothing unread).
void DrawCheckMark(SkCanvas& canvas) {
  SkPaint paint;
  paint.setAntiAlias(true);
  paint.setStyle(SkPaint::kStroke_Style);
  paint.setStrokeCap(SkPaint::kRound_Cap);
  paint.setStrokeWidth(6.0f);
  paint.setColor(SK_ColorWHITE);
  canvas.drawLine(4.0f, 17.0f, 12.5f, 25.5f, paint);
  canvas.drawLine(12.5f, 25.5f, 28.0f, 6.0f, paint);
}

// |total| (capped at 99) as a big white two-digit number ("01", "02", ...,
// "99"), in the bottom-left corner of the icon, with a red dot in the top-right
// corner drawn on top of the number so that it hides a little of it and sticks
// out past it. The leading zero keeps
// the icon the same width for every count.
void DrawDotAndNumber(SkCanvas& canvas, int total) {
  const int shown = std::clamp(total, 1, 99);
  const std::array<int, 2> digits = {shown / 10, shown % 10};
  const int count = static_cast<int>(digits.size());

  // The digits sit in the bottom-left corner of the icon, a little smaller
  // than the icon, which leaves room for the dot to stick out above and to the
  // right of them. The columns are 4, 5 and 4px wide (a digit is 13px wide, two
  // digits with a 2px gap are 28px) and the rows 5, 6, 5, 6 and 5px tall (a
  // digit is 27px tall).
  constexpr std::array<int, 3> kColumnWidths = {4, 5, 4};
  constexpr std::array<int, 5> kRowHeights = {5, 6, 5, 6, 5};
  constexpr int kDigitWidth = 13;
  constexpr int kDigitGap = 2;
  constexpr int kTextHeight = 27;
  const int text_left = 0;
  const int top = kIconSize - kTextHeight;

  SkPaint fill;
  fill.setColor(SK_ColorWHITE);
  for (int d = 0; d < count; ++d) {
    const int digit_left = text_left + d * (kDigitWidth + kDigitGap);
    int y = top;
    for (int row = 0; row < 5; ++row) {
      int x = digit_left;
      for (int col = 0; col < 3; ++col) {
        if (kDigitRows[digits[d]][row] & (1 << (2 - col))) {
          canvas.drawRect(
              SkRect::MakeXYWH(static_cast<float>(x), static_cast<float>(y),
                               static_cast<float>(kColumnWidths[col]),
                               static_cast<float>(kRowHeights[row])),
              fill);
        }
        x += kColumnWidths[col];
      }
      y += kRowHeights[row];
    }
  }

  // Drawn last, so it sits on top of the number's top-right corner and sticks
  // out above and to the right of it (the dot spans x = 22.5..32.5,
  // y = 0..10; the digits span x = 0..28, y = 5..32).
  SkPaint dot;
  dot.setAntiAlias(true);
  dot.setColor(kDotColor);
  canvas.drawCircle(kDotCenterX, kDotCenterY, kDotRadius, dot);
}

gfx::ImageSkia RenderTrayIcon(int total) {
  SkBitmap bitmap;
  bitmap.allocN32Pixels(kIconSize, kIconSize);
  bitmap.eraseColor(SK_ColorTRANSPARENT);
  SkCanvas canvas(bitmap);
  if (total > 0) {
    DrawDotAndNumber(canvas, total);
  } else {
    DrawCheckMark(canvas);
  }
  return gfx::ImageSkia::CreateFrom1xBitmap(bitmap);
}

}  // namespace

// static
SidebarTray& SidebarTray::Get() {
  static base::NoDestructor<SidebarTray> tray;
  return *tray;
}

// static
void SidebarTray::AttachToModel(SidebarAppModel* model) {
  Get().Attach(model);
}

// static
bool SidebarTray::ShouldHideInsteadOfClosing(BrowserWindowInterface* browser) {
  SidebarTray& tray = Get();
  if (!browser || tray.quitting_ || browser_shutdown::IsTryingToQuit()) {
    return false;
  }
  if (base::CommandLine::ForCurrentProcess()->HasSwitch(
          kDisableCloseToTraySwitch)) {
    return false;
  }
  return tray.host_window_ && tray.host_window_.get() == browser;
}

// static
bool SidebarTray::ClaimMainWindow(BrowserWindowInterface* browser) {
  if (!browser) {
    return false;
  }
  SidebarTray& tray = Get();
  if (!tray.main_window_) {
    tray.main_window_ = browser->GetWeakPtr();
  }
  return tray.main_window_.get() == browser;
}

// static
bool SidebarTray::ShouldSuppressShow() {
  if (g_start_in_tray_ended ||
      !base::CommandLine::ForCurrentProcess()->HasSwitch(kStartInTraySwitch)) {
    return false;
  }
  // The clock starts the first time a window asks to be shown, which is during
  // startup.
  static const base::TimeTicks deadline =
      base::TimeTicks::Now() + kStartInTrayWindow;
  return base::TimeTicks::Now() < deadline;
}

SidebarTray::SidebarTray() = default;
SidebarTray::~SidebarTray() = default;

void SidebarTray::SetLastActiveWindow(
    base::WeakPtr<BrowserWindowInterface> window) {
  last_active_window_ = std::move(window);
}

void SidebarTray::SetHostWindow(base::WeakPtr<BrowserWindowInterface> window) {
  host_window_ = std::move(window);
}

void SidebarTray::SetOpenAppCallback(OpenAppCallback callback) {
  open_app_callback_ = std::move(callback);
}

base::CallbackListSubscription SidebarTray::AddSleepAllCallback(
    SleepAllCallback callback) {
  return sleep_all_callbacks_.Add(std::move(callback));
}

void SidebarTray::SleepAll(bool sleep_apps, bool sleep_tabs) {
  sleep_all_callbacks_.Notify(sleep_apps, sleep_tabs);
}

Profile* SidebarTray::GetProfileForNewWindow() {
  BrowserWindowInterface* browser =
      host_window_ ? host_window_.get() : last_active_window_.get();
  if (browser) {
    return browser->GetProfile()->GetOriginalProfile();
  }
  ProfileManager* manager =
      g_browser_process ? g_browser_process->profile_manager() : nullptr;
  return manager ? manager->GetLastUsedProfileIfLoaded() : nullptr;
}

void SidebarTray::OpenNewWindow(bool incognito) {
  Profile* profile = GetProfileForNewWindow();
  if (!profile) {
    return;
  }
  if (incognito) {
    chrome::NewIncognitoWindow(profile);
  } else {
    chrome::NewEmptyWindow(profile);
  }
}

void SidebarTray::RefreshMenu() {
  CreateContextMenu();
}

void SidebarTray::OnDoNotDisturbChanged() {
  CreateContextMenu();
  UpdateIcon();
}

void SidebarTray::Attach(SidebarAppModel* model) {
  if (model_ || !model) {
    return;  // Already following a model.
  }
  model_ = model;
  model_->AddObserver(this);
  do_not_disturb_subscription_ = AddDoNotDisturbObserver(base::BindRepeating(
      &SidebarTray::OnDoNotDisturbChanged, base::Unretained(this)));
  UpdateIcon();
}

void SidebarTray::OnSidebarAppsChanged() {
  // The list of apps in the menu changed (added, removed, renamed, enabled);
  // the tooltip shows the names of the apps with unread items.
  CreateContextMenu();
  UpdateIcon();
}

void SidebarTray::OnUnreadCountsChanged() {
  UpdateIcon();
  // Only the counts changed: update the items in place, so a menu that is open
  // right now is not replaced under the user.
  UpdateAppMenuLabels();
}

std::u16string SidebarTray::BuildToolTip() const {
  const int total = model_->GetTotalUnreadCount();
  std::u16string tip =
      total > 0
          ? u"Dockium: " + base::NumberToString16(total) + u" unread"
          : std::u16string(u"Dockium: nothing unread");
  if (IsDoNotDisturbActive()) {
    tip += u"\n" + DoNotDisturbStatusText();
  }
  if (total <= 0) {
    return tip;
  }
  size_t not_shown = 0;
  for (const SidebarApp& app : model_->apps()) {
    const int count = model_->GetUnreadCount(app.id);
    if (count <= 0) {
      continue;
    }
    std::u16string name = base::UTF8ToUTF16(app.name);
    if (name.size() > kMaxToolTipNameLength) {
      name = name.substr(0, kMaxToolTipNameLength - 1) + u"\u2026";
    }
    const std::u16string line =
        u"\n" + name + u": " + base::NumberToString16(count);
    if (tip.size() + line.size() > kMaxToolTipLength) {
      ++not_shown;
      continue;
    }
    tip += line;
  }
  if (not_shown > 0) {
    const std::u16string more =
        u"\n+" + base::NumberToString16(not_shown) + u" more";
    if (tip.size() + more.size() <= kMaxToolTipLength) {
      tip += more;
    }
  }
  return tip;
}

std::u16string SidebarTray::AppMenuLabel(const SidebarApp& app) const {
  std::u16string label = base::UTF8ToUTF16(app.name);
  // A single "&" would mark the next letter as the menu shortcut.
  base::ReplaceSubstringsAfterOffset(&label, 0, u"&", u"&&");
  const int count = model_ ? model_->GetUnreadCount(app.id) : 0;
  if (count > 0) {
    label += u"  (" + base::NumberToString16(count) + u")";
  }
  return label;
}

void SidebarTray::UpdateAppMenuLabels() {
  if (!menu_model_ || !model_) {
    return;
  }
  for (size_t i = 0; i < menu_app_ids_.size(); ++i) {
    if (const SidebarApp* app = model_->FindApp(menu_app_ids_[i])) {
      menu_model_->SetLabel(first_app_index_ + i, AppMenuLabel(*app));
    }
  }
}

void SidebarTray::UpdateIcon() {
  if (!model_ || !g_browser_process || g_browser_process->IsShuttingDown()) {
    return;
  }
  StatusTray* tray = g_browser_process->status_tray();
  if (!tray) {
    return;
  }
  const int total = model_->GetTotalUnreadCount();
  const gfx::ImageSkia image = RenderTrayIcon(total);
  const std::u16string tool_tip = BuildToolTip();
  if (!icon_) {
    icon_ = tray->CreateStatusIcon(StatusTray::OTHER_ICON, image, tool_tip);
    if (icon_) {
      icon_->AddObserver(this);
      CreateContextMenu();
    }
    return;
  }
  icon_->SetImage(image);
  icon_->SetToolTip(tool_tip);
}

void SidebarTray::CreateContextMenu() {
  if (!icon_) {
    return;
  }
  auto menu = std::make_unique<StatusIconMenuModel>(this);
  menu->AddItem(kCommandOpen, u"Open Dockium");
  menu->AddItem(kCommandNewWindow, u"New window");
  menu->AddItem(kCommandNewIncognitoWindow, u"New incognito window");
  menu->AddSeparator(ui::NORMAL_SEPARATOR);
  // One shortcut per enabled app, with its unread count.
  menu_app_ids_.clear();
  first_app_index_ = menu->GetItemCount();
  if (model_) {
    for (const SidebarApp& app : model_->apps()) {
      if (app.disabled) {
        continue;
      }
      menu->AddItem(kCommandAppBase + static_cast<int>(menu_app_ids_.size()),
                    AppMenuLabel(app));
      menu_app_ids_.push_back(app.id);
    }
    if (!menu_app_ids_.empty()) {
      menu->AddSeparator(ui::NORMAL_SEPARATOR);
    }
  }
  sleep_menu_ = std::make_unique<StatusIconMenuModel>(this);
  sleep_menu_->AddItem(kCommandSleepAllApps, u"Power sleep all dock apps now");
  sleep_menu_->AddItem(kCommandSleepAllTabs, u"Power sleep all tabs now");
  menu->AddSubMenu(kCommandSleepMenu, u"Power Sleep", sleep_menu_.get());
  const bool do_not_disturb = IsDoNotDisturbActive();
  do_not_disturb_menu_ = std::make_unique<StatusIconMenuModel>(this);
  for (size_t i = 0; i < kDoNotDisturbDurations.size(); ++i) {
    do_not_disturb_menu_->AddItem(kCommandDoNotDisturbBase + static_cast<int>(i),
                                  kDoNotDisturbDurations[i].label);
  }
  if (do_not_disturb) {
    do_not_disturb_menu_->AddSeparator(ui::NORMAL_SEPARATOR);
    do_not_disturb_menu_->AddItem(kCommandDoNotDisturbOff,
                                  u"Turn off Do Not Disturb");
  }
  menu->AddSubMenu(kCommandDoNotDisturbMenu,
                   do_not_disturb ? u"Do not Disturb (on)" : u"Do not Disturb",
                   do_not_disturb_menu_.get());
  menu->AddSeparator(ui::NORMAL_SEPARATOR);
  menu->AddCheckItem(kCommandAutostart, u"Start with Windows");
  menu->SetCommandIdChecked(kCommandAutostart, IsAutostartEnabled());
  menu->AddSeparator(ui::NORMAL_SEPARATOR);
  menu->AddItem(kCommandQuit, u"Quit Dockium");
  menu_model_ = menu.get();
  icon_->SetContextMenu(std::move(menu));
}

void SidebarTray::ExecuteCommand(int command_id, int event_flags) {
  switch (command_id) {
    case kCommandOpen:
      ShowWindow();
      break;
    case kCommandAutostart:
      SetAutostartEnabled(!IsAutostartEnabled());
      // Rebuild the menu once this one has finished running, so the check
      // box shows the new state. (The tray object lives for the whole
      // process.)
      base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
          FROM_HERE, base::BindOnce(&SidebarTray::CreateContextMenu,
                                    base::Unretained(this)));
      break;
    case kCommandQuit:
      // From here on closing a window really closes it.
      quitting_ = true;
      // A silent exit: the session is always marked as cleanly ended, and no
      // "Leave site?" prompt of a hidden app tab can stop the quit.
      chrome::ExitIgnoreUnloadHandlers();
      // Write the clean-exit mark to disk right away instead of at the very
      // end of shutdown, so a slow or interrupted shutdown does not lose it.
      if (g_browser_process && g_browser_process->profile_manager()) {
        for (Profile* profile :
             g_browser_process->profile_manager()->GetLoadedProfiles()) {
          profile->GetPrefs()->CommitPendingWrite();
        }
      }
      break;
    case kCommandNewWindow:
      OpenNewWindow(/*incognito=*/false);
      break;
    case kCommandNewIncognitoWindow:
      OpenNewWindow(/*incognito=*/true);
      break;
    case kCommandSleepAllApps:
      SleepAll(/*sleep_apps=*/true, /*sleep_tabs=*/false);
      break;
    case kCommandSleepAllTabs:
      SleepAll(/*sleep_apps=*/false, /*sleep_tabs=*/true);
      break;
    case kCommandDoNotDisturbOff:
      StopDoNotDisturb();
      break;
    default:
      if (command_id >= kCommandDoNotDisturbBase &&
          command_id < kCommandDoNotDisturbBase +
                           static_cast<int>(kDoNotDisturbDurations.size())) {
        StartDoNotDisturbForMinutes(
            kDoNotDisturbDurations[command_id - kCommandDoNotDisturbBase]
                .minutes);
        break;
      }
      if (command_id >= kCommandAppBase) {
        const size_t index = static_cast<size_t>(command_id - kCommandAppBase);
        if (index < menu_app_ids_.size()) {
          const std::string app_id = menu_app_ids_[index];
          OpenApp(app_id);
        }
      }
      break;
  }
}

void SidebarTray::OpenApp(const std::string& app_id) {
  ShowWindow();
  if (open_app_callback_) {
    open_app_callback_.Run(app_id);
  }
}

void SidebarTray::OnStatusIconClicked() {
  ShowWindow();
}

void SidebarTray::ShowWindow() {
  // The user asked for the window: stop keeping it hidden.
  g_start_in_tray_ended = true;
  BrowserWindowInterface* browser =
      host_window_ ? host_window_.get() : last_active_window_.get();
  if (!browser) {
    return;
  }
  ui::BaseWindow* window = browser->GetWindow();
  if (!window) {
    return;
  }
  if (window->IsMinimized()) {
    window->Restore();
  }
  window->Show();
  window->Activate();
}

}  // namespace sidebar_apps
