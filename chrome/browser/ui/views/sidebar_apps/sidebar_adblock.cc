// Copyright 2026 MDJ. Use of this source code is governed by a BSD-style
// license that can be found in the Chromium LICENSE file.

#include "chrome/browser/ui/views/sidebar_apps/sidebar_adblock.h"

#include <algorithm>
#include <atomic>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "base/functional/bind.h"
#include "base/memory/raw_ptr.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_split.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "base/values.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/views/sidebar_apps/sidebar_adblock_engine.h"
#include "components/constrained_window/constrained_window_views.h"
#include "components/pref_registry/pref_registry_syncable.h"
#include "components/prefs/pref_service.h"
#include "net/base/registry_controlled_domains/registry_controlled_domain.h"
#include "ui/base/mojom/dialog_button.mojom.h"
#include "ui/base/mojom/ui_base_types.mojom.h"
#include "ui/color/color_id.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/gfx/geometry/size.h"
#include "ui/gfx/text_constants.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/border.h"
#include "ui/views/controls/label.h"
#include "ui/views/controls/scroll_view.h"
#include "ui/views/controls/textarea/textarea.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/layout/layout_manager.h"
#include "ui/views/layout/layout_types.h"
#include "ui/views/view.h"
#include "ui/views/widget/widget.h"
#include "ui/views/window/dialog_delegate.h"

namespace sidebar_apps {
namespace adblock {

namespace {

constexpr char kScopePref[] = "sidebar_adblock.scope";
constexpr char kAppsPref[] = "sidebar_adblock.apps";
constexpr char kSitesPref[] = "sidebar_adblock.sites";
constexpr char kExceptionsPref[] = "sidebar_adblock.exceptions";
constexpr char kExceptionsOnPref[] = "sidebar_adblock.exceptions_on";
// Registrable domains of the sidebar apps, written by Apply() so the policy is
// right from the first request after a restart (before any sidebar exists).
constexpr char kAppHostsPref[] = "sidebar_adblock.app_hosts";

std::atomic<int> g_blocked_count{0};

bool IsValidHostName(const std::string& host) {
  if (host.empty() || host.size() > 253 || host.front() == '.' ||
      host.back() == '.') {
    return false;
  }
  for (char c : host) {
    const bool ok = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
                    c == '-' || c == '.';
    if (!ok) {
      return false;
    }
  }
  return true;
}

// "https://www.Example.com/path", "*.example.com" or "example.com" -> host.
// Empty for blank lines, comments and unusable hosts.
std::string NormalizeSite(std::string_view raw) {
  std::string text(base::TrimWhitespaceASCII(raw, base::TRIM_ALL));
  if (text.empty() || text.front() == '#') {
    return std::string();
  }
  std::string host;
  if (text.find("://") != std::string::npos) {
    host = GURL(text).host();
  } else {
    while (text.rfind("*.", 0) == 0) {
      text.erase(0, 2);
    }
    while (!text.empty() && text.front() == '.') {
      text.erase(0, 1);
    }
    host = GURL("https://" + text).host();
  }
  return IsValidHostName(host) ? host : std::string();
}

std::vector<std::string> ParseSites(std::string_view text) {
  std::vector<std::string> sites;
  for (const std::string& line : base::SplitString(
           text, "\n", base::TRIM_WHITESPACE, base::SPLIT_WANT_NONEMPTY)) {
    const std::string site = NormalizeSite(line);
    if (!site.empty() &&
        std::find(sites.begin(), sites.end(), site) == sites.end()) {
      sites.push_back(site);
    }
  }
  return sites;
}

PrefService* PrefsFor(Profile* profile) {
  return profile ? profile->GetOriginalProfile()->GetPrefs() : nullptr;
}

base::ListValue ToValueList(const std::vector<std::string>& items) {
  base::ListValue list;
  for (const std::string& item : items) {
    list.Append(item);
  }
  return list;
}

bool ListContains(PrefService* prefs, const char* pref, const std::string& v) {
  for (const base::Value& value : prefs->GetList(pref)) {
    if (value.is_string() && value.GetString() == v) {
      return true;
    }
  }
  return false;
}

// True if |host| is one of the pref list's entries or a subdomain of one, so an
// entry that was typed as "www.example.com" still works.
bool HostInList(PrefService* prefs, const char* pref, std::string_view host) {
  for (const base::Value& value : prefs->GetList(pref)) {
    if (!value.is_string()) {
      continue;
    }
    const std::string_view d(value.GetString());
    if (host == d ||
        (host.size() > d.size() && host.ends_with(d) &&
         host[host.size() - d.size() - 1] == '.')) {
      return true;
    }
  }
  return false;
}

// ---- The edit form --------------------------------------------------------

// One hint, one multi-line box. Deliberately plain: fixed-size Textarea, no
// ScrollView / custom size callbacks (the earlier version, with a holder view
// that asked the Textarea for its height inside Layout, crashed on open).
class EditContents : public views::View {
  METADATA_HEADER(EditContents, views::View)

 public:
  static constexpr int kContentWidth = 460;
  static constexpr int kBoxHeight = 170;

  EditContents(const std::u16string& hint, const std::u16string& initial_text) {
    SetLayoutManager(std::make_unique<views::BoxLayout>(
        views::BoxLayout::Orientation::kVertical,
        gfx::Insets::TLBR(8, 24, 16, 24),
        /*between_child_spacing=*/8));
    auto* hint_label = AddChildView(std::make_unique<views::Label>(hint));
    hint_label->SetMultiLine(true);
    hint_label->SetHorizontalAlignment(gfx::ALIGN_LEFT);
    // Without a maximum width the label asks for its whole text on one line
    // and the dialog grows as wide as the screen.
    hint_label->SetMaximumWidth(kContentWidth);

    textarea_ = AddChildView(std::make_unique<views::Textarea>());
    textarea_->GetViewAccessibility().SetName(u"Sites, one per line");
    textarea_->SetPreferredSize(gfx::Size(kContentWidth, kBoxHeight));
    textarea_->SetText(initial_text);
  }

  views::Textarea* textarea() { return textarea_; }

 private:
  raw_ptr<views::Textarea> textarea_ = nullptr;
};

BEGIN_METADATA(EditContents)
END_METADATA

}  // namespace

void RegisterProfilePrefs(user_prefs::PrefRegistrySyncable* registry) {
  registry->RegisterIntegerPref(kScopePref, static_cast<int>(Scope::kOff));
  registry->RegisterBooleanPref(kAppsPref, false);
  registry->RegisterListPref(kSitesPref);
  registry->RegisterListPref(kExceptionsPref);
  registry->RegisterBooleanPref(kExceptionsOnPref, false);
  registry->RegisterListPref(kAppHostsPref);
}

bool IsEnabled(Profile* profile) {
  return GetScope(profile) != Scope::kOff || IsUsedForApps(profile);
}

Scope GetScope(Profile* profile) {
  PrefService* prefs = PrefsFor(profile);
  const int value = prefs ? prefs->GetInteger(kScopePref)
                          : static_cast<int>(Scope::kOff);
  if (value == static_cast<int>(Scope::kAllTabs)) {
    return Scope::kAllTabs;
  }
  if (value == static_cast<int>(Scope::kSiteList)) {
    return Scope::kSiteList;
  }
  return Scope::kOff;
}

void SetScope(Profile* profile, Scope scope) {
  if (PrefService* prefs = PrefsFor(profile)) {
    prefs->SetInteger(kScopePref, static_cast<int>(scope));
  }
}

bool IsUsedForApps(Profile* profile) {
  PrefService* prefs = PrefsFor(profile);
  return prefs && prefs->GetBoolean(kAppsPref);
}

void SetUsedForApps(Profile* profile, bool used) {
  if (PrefService* prefs = PrefsFor(profile)) {
    prefs->SetBoolean(kAppsPref, used);
  }
}

std::string SiteKeyForUrl(const GURL& url) {
  if (!url.is_valid() || !url.SchemeIsHTTPOrHTTPS()) {
    return std::string();
  }
  std::string key = net::registry_controlled_domains::GetDomainAndRegistry(
      url, net::registry_controlled_domains::INCLUDE_PRIVATE_REGISTRIES);
  if (key.empty()) {
    key = url.host();
  }
  return IsValidHostName(key) ? key : std::string();
}

bool IsSiteListed(Profile* profile, const std::string& site_key) {
  PrefService* prefs = PrefsFor(profile);
  return prefs && !site_key.empty() && ListContains(prefs, kSitesPref, site_key);
}

void AddSite(Profile* profile, const std::string& site_key) {
  PrefService* prefs = PrefsFor(profile);
  if (!prefs || site_key.empty() || IsSiteListed(profile, site_key)) {
    return;
  }
  base::ListValue list = prefs->GetList(kSitesPref).Clone();
  list.Append(site_key);
  prefs->SetList(kSitesPref, std::move(list));
}

void RemoveSite(Profile* profile, const std::string& site_key) {
  PrefService* prefs = PrefsFor(profile);
  if (!prefs) {
    return;
  }
  base::ListValue list;
  for (const base::Value& value : prefs->GetList(kSitesPref)) {
    if (value.is_string() && value.GetString() != site_key) {
      list.Append(value.GetString());
    }
  }
  prefs->SetList(kSitesPref, std::move(list));
}

bool IsExceptionsEnabled(Profile* profile) {
  PrefService* prefs = PrefsFor(profile);
  return prefs && prefs->GetBoolean(kExceptionsOnPref);
}

void SetExceptionsEnabled(Profile* profile, bool enabled) {
  if (PrefService* prefs = PrefsFor(profile)) {
    prefs->SetBoolean(kExceptionsOnPref, enabled);
  }
}

bool IsExcepted(Profile* profile, const std::string& site_key) {
  PrefService* prefs = PrefsFor(profile);
  return prefs && !site_key.empty() &&
         ListContains(prefs, kExceptionsPref, site_key);
}

void AddException(Profile* profile, const std::string& site_key) {
  PrefService* prefs = PrefsFor(profile);
  if (!prefs || site_key.empty() || IsExcepted(profile, site_key)) {
    return;
  }
  base::ListValue list = prefs->GetList(kExceptionsPref).Clone();
  list.Append(site_key);
  prefs->SetList(kExceptionsPref, std::move(list));
}

void RemoveException(Profile* profile, const std::string& site_key) {
  PrefService* prefs = PrefsFor(profile);
  if (!prefs) {
    return;
  }
  base::ListValue list;
  for (const base::Value& value : prefs->GetList(kExceptionsPref)) {
    if (value.is_string() && value.GetString() != site_key) {
      list.Append(value.GetString());
    }
  }
  prefs->SetList(kExceptionsPref, std::move(list));
}

bool IsBlockingOnPage(Profile* profile, const GURL& page_url) {
  PrefService* prefs = PrefsFor(profile);
  if (!prefs) {
    return false;
  }
  const std::string key = SiteKeyForUrl(page_url);
  if (key.empty()) {
    return false;  // chrome://, file:, new tab page ... are never touched.
  }
  // The Exception List beats every other option below.
  if (prefs->GetBoolean(kExceptionsOnPref) &&
      HostInList(prefs, kExceptionsPref, page_url.host())) {
    return false;
  }
  const bool is_app = ListContains(prefs, kAppHostsPref, key);
  const bool apps_on = prefs->GetBoolean(kAppsPref);
  switch (GetScope(profile)) {
    case Scope::kAllTabs:
      return !is_app || apps_on;
    case Scope::kSiteList:
      return ListContains(prefs, kSitesPref, key) || (apps_on && is_app);
    case Scope::kOff:
      break;
  }
  return apps_on && is_app;
}

bool ShouldBlockRequest(Profile* profile,
                        const GURL& url,
                        const GURL& source_url,
                        const GURL& page_url,
                        RequestKind kind) {
  if (!url.SchemeIsHTTPOrHTTPS() || !IsBlockingOnPage(profile, page_url)) {
    return false;
  }
  Engine& engine = Engine::Get();
  engine.EnsureLoaded();
  const rs::BlockResult result = engine.Check(url, source_url, kind);
  if (result.matched && !result.exception) {
    g_blocked_count.fetch_add(1, std::memory_order_relaxed);
    return true;
  }
  return false;
}

void Apply(Profile* profile, const std::vector<GURL>& app_urls) {
  PrefService* prefs = PrefsFor(profile);
  if (!prefs || profile->IsOffTheRecord()) {
    return;
  }
  std::vector<std::string> hosts;
  for (const GURL& url : app_urls) {
    const std::string host = SiteKeyForUrl(url);
    if (!host.empty() &&
        std::find(hosts.begin(), hosts.end(), host) == hosts.end()) {
      hosts.push_back(host);
    }
  }
  base::ListValue list = ToValueList(hosts);
  if (prefs->GetList(kAppHostsPref) != list) {
    prefs->SetList(kAppHostsPref, std::move(list));
  }
  if (IsEnabled(profile)) {
    Engine::Get().EnsureLoaded();
  }
}

int GetBlockedCount() {
  return g_blocked_count.load(std::memory_order_relaxed);
}

std::u16string StatusText(Profile* profile) {
  if (!IsEnabled(profile)) {
    return std::u16string();
  }
  Engine& engine = Engine::Get();
  switch (engine.state()) {
    case EngineState::kNotLoaded:
    case EngineState::kLoading:
      return u"Ad block: loading lists\u2026";
    case EngineState::kMissing:
      return u"Ad block: filter lists not found";
    case EngineState::kReady:
      break;
  }
  return base::NumberToString16(GetBlockedCount()) +
         u" ads blocked";
}

void ShowEditDialog(views::Widget* parent,
                    Profile* profile,
                    EditKind kind,
                    base::RepeatingClosure on_saved) {
  PrefService* prefs = PrefsFor(profile);
  if (!parent || !prefs) {
    return;
  }
  const bool exceptions = kind == EditKind::kExceptions;
  const char* list_pref = exceptions ? kExceptionsPref : kSitesPref;
  std::string initial;
  for (const base::Value& value : prefs->GetList(list_pref)) {
    if (value.is_string()) {
      initial += value.GetString() + "\n";
    }
  }
  auto contents = std::make_unique<EditContents>(
      exceptions
          ? u"Exception List: sites where ads are never blocked, one per "
            u"line, even when the options above are on (needs \"Ignore sites "
            u"in Exception List\" to be checked). \"example.com\" also "
            u"covers its subdomains."
          : u"Specific sites where ads are blocked, one per line. "
            u"\"example.com\" also covers its subdomains.",
      base::UTF8ToUTF16(initial));
  EditContents* form = contents.get();

  auto dialog = std::make_unique<views::DialogDelegate>();
  dialog->SetTitle(exceptions ? u"Ad block Exception List"
                              : u"Ad block specific sites");
  dialog->SetModalType(ui::mojom::ModalType::kWindow);
  dialog->SetButtonLabel(ui::mojom::DialogButton::kOk, u"Save");
  dialog->SetButtonLabel(ui::mojom::DialogButton::kCancel, u"Cancel");
  dialog->SetContentsView(std::move(contents));
  dialog->SetInitiallyFocusedView(form->textarea());
  dialog->SetAcceptCallbackWithClose(base::BindRepeating(
      [](Profile* profile, const char* list_pref,
         base::RepeatingClosure on_saved, EditContents* form) {
        PrefService* prefs = PrefsFor(profile);
        if (!prefs) {
          return true;
        }
        prefs->SetList(list_pref,
                       ToValueList(ParseSites(
                           base::UTF16ToUTF8(form->textarea()->GetText()))));
        if (on_saved) {
          on_saved.Run();
        }
        return true;
      },
      base::Unretained(profile), list_pref, std::move(on_saved),
      base::Unretained(form)));

  views::Widget* widget = constrained_window::CreateBrowserModalDialogViews(
      std::move(dialog), parent->GetNativeWindow());
  if (widget) {
    widget->Show();
  }
}

}  // namespace adblock
}  // namespace sidebar_apps
