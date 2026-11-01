// Copyright 2026 MDJ. Use of this source code is governed by a BSD-style
// license that can be found in the Chromium LICENSE file.

#include "chrome/browser/ui/views/sidebar_apps/sidebar_vpn.h"

#include <algorithm>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "base/base64.h"
#include "base/functional/bind.h"
#include "base/functional/callback_helpers.h"
#include "base/json/json_writer.h"
#include "base/memory/raw_ptr.h"
#include "base/no_destructor.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_split.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "base/values.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/views/sidebar_apps/sidebar_tor.h"
#include "chrome/common/pref_names.h"
#include "components/constrained_window/constrained_window_views.h"
#include "components/pref_registry/pref_registry_syncable.h"
#include "components/prefs/pref_service.h"
#include "components/proxy_config/proxy_config_dictionary.h"
#include "components/proxy_config/proxy_config_pref_names.h"
#include "content/public/browser/page.h"
#include "content/public/browser/storage_partition.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_contents_observer.h"
#include "content/public/browser/web_contents_user_data.h"
#include "net/base/registry_controlled_domains/registry_controlled_domain.h"
#include "services/network/public/mojom/network_context.mojom.h"
#include "third_party/blink/public/common/renderer_preferences/renderer_preferences.h"
#include "ui/base/mojom/dialog_button.mojom.h"
#include "ui/base/mojom/ui_base_types.mojom.h"
#include "ui/color/color_id.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/gfx/geometry/size.h"
#include "ui/gfx/text_constants.h"
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
namespace vpn {

namespace {

constexpr char kScopePref[] = "sidebar_vpn.scope";
constexpr char kAppsPref[] = "sidebar_vpn.apps";
constexpr char kSitesPref[] = "sidebar_vpn.sites";
constexpr char kExceptionsPref[] = "sidebar_vpn.exceptions";
constexpr char kExceptionsOnPref[] = "sidebar_vpn.exceptions_on";
constexpr char kServersPref[] = "sidebar_vpn.servers";
constexpr char kSelectedServerPref[] = "sidebar_vpn.selected_server";
// True while the profile's proxy setting was written by us, so that turning the
// VPN off only clears what we set (and never a proxy the user configured).
constexpr char kTorCountryPref[] = "sidebar_vpn.tor_country";
constexpr char kOwnsProxyPref[] = "sidebar_vpn.owns_proxy";
// Set once the leftover profile-wide WebRTC policy of older builds is cleaned up.
constexpr char kWebRtcMigratedPref[] = "sidebar_vpn.webrtc_migrated";

// Tor listens on 9050 (the "tor" program / Expert Bundle) or 9150 (while Tor
// Browser is running). Replace or add your own servers in "Edit servers".
constexpr char kDefaultServers[] =
    "Tor (local) = socks5://127.0.0.1:9050\n"
    "Tor Browser (local) = socks5://127.0.0.1:9150\n";

constexpr char kPacDataUrlPrefix[] =
    "data:application/x-ns-proxy-autoconfig;base64,";

// The PAC script is "variables, then this". It runs inside the network
// service's PAC sandbox for every request URL and must stay cheap. It never
// resolves names (no dnsResolve), so it cannot leak DNS queries.
constexpr char kPacBody[] = R"JS(
function inList(host, list) {
  for (var i = 0; i < list.length; i++) {
    var d = list[i];
    if (host === d) return true;
    if (host.length > d.length &&
        host.substring(host.length - d.length - 1) === "." + d) return true;
  }
  return false;
}
function FindProxyForURL(url, host) {
  host = host.toLowerCase();
  if (isPlainHostName(host) || host.indexOf(":") >= 0 ||
      host === "localhost" ||
      /\.(local|localhost|internal|lan)$/.test(host) ||
      /^(127\.|10\.|192\.168\.|169\.254\.|172\.(1[6-9]|2[0-9]|3[01])\.)/.test(host)) {
    return "DIRECT";
  }
  // The Exception List beats everything below (all sites, specific sites,
  // sidebar apps).
  if (EXC_ON && inList(host, EXCEPTIONS)) return "DIRECT";
  // Sidebar apps: only the sidebar toggle decides, never the scope.
  if (inList(host, APPS)) return APPS_ON ? PROXY : "DIRECT";
  if (SCOPE === "all") return PROXY;
  if (SCOPE === "list") return inList(host, SITES) ? PROXY : "DIRECT";
  return "DIRECT";
}
)JS";

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

// "https://www.Example.com/path", "*.example.com" or "example.com" all become
// the host to store ("www.example.com", "example.com", "example.com"). Empty
// if the line is blank, a comment or not a usable host.
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

bool IsValidProxyHost(const std::string& host) {
  if (host.size() >= 2 && host.front() == '[' && host.back() == ']') {
    for (size_t i = 1; i + 1 < host.size(); ++i) {
      const char c = host[i];
      if (!base::IsHexDigit(c) && c != ':' && c != '.') {
        return false;
      }
    }
    return host.size() > 2;
  }
  if (host.empty()) {
    return false;
  }
  for (char c : host) {
    const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                    (c >= '0' && c <= '9') || c == '-' || c == '.';
    if (!ok) {
      return false;
    }
  }
  return true;
}

// "Name = socks5://host:port" (the name is optional). Schemes: socks5, socks4,
// http, https. Returns false for blank lines, comments and bad entries.
bool ParseServerLine(std::string_view line, Server* out) {
  const std::string text(base::TrimWhitespaceASCII(line, base::TRIM_ALL));
  if (text.empty() || text.front() == '#') {
    return false;
  }
  std::string name;
  std::string address = text;
  const size_t equals = text.find('=');
  if (equals != std::string::npos) {
    name = std::string(
        base::TrimWhitespaceASCII(text.substr(0, equals), base::TRIM_ALL));
    address = std::string(
        base::TrimWhitespaceASCII(text.substr(equals + 1), base::TRIM_ALL));
  }
  const size_t scheme_end = address.find("://");
  if (scheme_end == std::string::npos) {
    return false;
  }
  const std::string scheme = base::ToLowerASCII(address.substr(0, scheme_end));
  std::string host_port = address.substr(scheme_end + 3);
  while (!host_port.empty() && host_port.back() == '/') {
    host_port.pop_back();
  }
  std::string directive;
  if (scheme == "socks5") {
    directive = "SOCKS5";
  } else if (scheme == "socks4") {
    directive = "SOCKS4";
  } else if (scheme == "http") {
    directive = "PROXY";
  } else if (scheme == "https") {
    directive = "HTTPS";
  } else {
    return false;
  }
  const size_t colon = host_port.rfind(':');
  if (colon == std::string::npos || colon == 0) {
    return false;
  }
  const std::string host = host_port.substr(0, colon);
  int port = 0;
  if (!base::StringToInt(host_port.substr(colon + 1), &port) || port < 1 ||
      port > 65535 || !IsValidProxyHost(host)) {
    return false;
  }
  out->name = name.empty() ? host : name;
  out->url = scheme + "://" + host_port;
  out->pac_directive = directive + " " + host_port;
  return true;
}

std::vector<Server> ParseServers(std::string_view text) {
  std::vector<Server> servers;
  for (const std::string& line : base::SplitString(
           text, "\n", base::TRIM_WHITESPACE, base::SPLIT_WANT_NONEMPTY)) {
    Server server;
    if (ParseServerLine(line, &server)) {
      servers.push_back(std::move(server));
      if (servers.size() >= static_cast<size_t>(kMaxServers)) {
        break;
      }
    }
  }
  return servers;
}

PrefService* PrefsFor(Profile* profile) {
  return profile ? profile->GetOriginalProfile()->GetPrefs() : nullptr;
}

std::string JsonOf(base::Value value) {
  return base::WriteJson(value).value_or(std::string());
}

base::ListValue ToValueList(const std::vector<std::string>& items) {
  base::ListValue list;
  for (const std::string& item : items) {
    list.Append(item);
  }
  return list;
}

// Connections that are already open keep their old route; closing the idle ones
// makes the change take effect right away for new page loads.
void CloseIdleConnections(Profile* profile) {
  profile->GetOriginalProfile()->ForEachLoadedStoragePartition(
      [](content::StoragePartition* partition) {
        if (auto* network_context = partition->GetNetworkContext()) {
          network_context->CloseIdleConnections(base::DoNothing());
        }
      });
}

// Like the above, but also ends connections that are in use.
void CloseAllConnections(Profile* profile) {
  profile->GetOriginalProfile()->ForEachLoadedStoragePartition(
      [](content::StoragePartition* partition) {
        if (auto* network_context = partition->GetNetworkContext()) {
          network_context->CloseAllConnections(base::DoNothing());
        }
      });
}

base::FilePath TorDataDir(Profile* profile) {
  return profile->GetOriginalProfile()->GetPath().AppendASCII("SidebarVpnTor");
}

// ---- The edit form --------------------------------------------------------

// Contents of the edit box's ScrollView. A Textarea on its own is only as tall
// as its text, so with a short list the box would be one line high with empty
// space below it. This keeps it at least |kMinHeight| tall and lets it grow
// with the text (the ScrollView then scrolls).
class TextareaHolder : public views::View {
 public:
  static constexpr int kMinHeight = 166;

  explicit TextareaHolder(std::unique_ptr<views::Textarea> textarea) {
    textarea_ = AddChildView(std::move(textarea));
  }

  gfx::Size CalculatePreferredSize(
      const views::SizeBounds& available_size) const override {
    const int width = available_size.width().is_bounded()
                          ? available_size.width().value()
                          : 440;
    return gfx::Size(
        width, std::max(kMinHeight, textarea_->GetHeightForWidth(width)));
  }

  void Layout(PassKey) override { textarea_->SetBoundsRect(GetLocalBounds()); }

  views::Textarea* textarea() { return textarea_; }

 private:
  raw_ptr<views::Textarea> textarea_ = nullptr;
};

class EditContents : public views::View {
 public:
  static constexpr int kContentWidth = 460;
  static constexpr int kSideMargin = 24;

  EditContents(const std::u16string& hint, const std::u16string& initial_text) {
    // A plain DialogDelegate adds no margins around the contents, so the form
    // supplies its own.
    SetLayoutManager(std::make_unique<views::BoxLayout>(
        views::BoxLayout::Orientation::kVertical,
        gfx::Insets::TLBR(8, kSideMargin, 16, kSideMargin),
        /*between_child_spacing=*/8));
    auto* hint_label = AddChildView(std::make_unique<views::Label>(hint));
    hint_label->SetMultiLine(true);
    hint_label->SetHorizontalAlignment(gfx::ALIGN_LEFT);
    // Without a maximum width the label asks for its whole text on one line
    // and the dialog grows as wide as the screen.
    hint_label->SetMaximumWidth(kContentWidth);

    auto scroll = std::make_unique<views::ScrollView>();
    scroll->SetHorizontalScrollBarMode(
        views::ScrollView::ScrollBarMode::kDisabled);
    auto textarea = std::make_unique<views::Textarea>();
    textarea->SetText(initial_text);
    textarea_ = textarea.get();
    scroll->SetContents(std::make_unique<TextareaHolder>(std::move(textarea)));
    // The holder is always at least as tall as the viewport, so the textarea
    // fills the whole box; longer text scrolls.
    scroll->ClipHeightTo(170, 170);
    // Give the input box a visible background and outline.
    scroll->SetBackgroundColor(ui::kColorTextfieldBackground);
    scroll->SetBorder(views::CreateRoundedRectBorder(
        /*thickness=*/1, /*corner_radius=*/4, ui::kColorSeparator));
    AddChildView(std::move(scroll));

    error_label_ = AddChildView(std::make_unique<views::Label>());
    error_label_->SetHorizontalAlignment(gfx::ALIGN_LEFT);
    error_label_->SetEnabledColor(ui::kColorAlertHighSeverity);
  }


  // Fixed dialog width; the height follows the wrapped hint and the textarea.
  gfx::Size CalculatePreferredSize(
      const views::SizeBounds& /*available_size*/) const override {
    const int width = kContentWidth + 2 * kSideMargin;
    return gfx::Size(
        width, GetLayoutManager()->GetPreferredHeightForWidth(this, width));
  }

  views::Textarea* textarea() { return textarea_; }
  void ShowError(const std::u16string& message) {
    error_label_->SetText(message);
  }

 private:
  raw_ptr<views::Textarea> textarea_ = nullptr;
  raw_ptr<views::Label> error_label_ = nullptr;
};

}  // namespace

void RegisterProfilePrefs(user_prefs::PrefRegistrySyncable* registry) {
  registry->RegisterIntegerPref(kScopePref, static_cast<int>(Scope::kOff));
  registry->RegisterBooleanPref(kAppsPref, false);
  registry->RegisterListPref(kSitesPref);
  registry->RegisterListPref(kExceptionsPref);
  registry->RegisterBooleanPref(kExceptionsOnPref, false);
  registry->RegisterStringPref(kServersPref, kDefaultServers);
  registry->RegisterIntegerPref(kSelectedServerPref, 0);
  registry->RegisterStringPref(kTorCountryPref, std::string());
  registry->RegisterBooleanPref(kOwnsProxyPref, false);
  registry->RegisterBooleanPref(kWebRtcMigratedPref, false);
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
  if (!prefs || site_key.empty()) {
    return false;
  }
  for (const base::Value& value : prefs->GetList(kSitesPref)) {
    if (value.is_string() && value.GetString() == site_key) {
      return true;
    }
  }
  return false;
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
  if (!prefs || site_key.empty()) {
    return false;
  }
  for (const base::Value& value : prefs->GetList(kExceptionsPref)) {
    if (value.is_string() && value.GetString() == site_key) {
      return true;
    }
  }
  return false;
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

std::vector<Server> GetServers(Profile* profile) {
  PrefService* prefs = PrefsFor(profile);
  std::vector<Server> servers;
  if (TorManager::IsInstalled()) {
    servers.push_back(Server{
        "Tor (built-in)", kBuiltinTorUrl,
        "SOCKS5 127.0.0.1:" + base::NumberToString(kTorSocksPort)});
  }
  for (Server& server : ParseServers(prefs ? prefs->GetString(kServersPref)
                                           : std::string(kDefaultServers))) {
    if (servers.size() >= static_cast<size_t>(kMaxServers)) {
      break;
    }
    servers.push_back(std::move(server));
  }
  return servers;
}

std::string GetTorCountry(Profile* profile) {
  PrefService* prefs = PrefsFor(profile);
  const std::string country = prefs ? prefs->GetString(kTorCountryPref) : "";
  for (const TorCountry& entry : kTorCountries) {
    if (country == entry.code) {
      return country;
    }
  }
  return std::string();
}

void SetTorCountry(Profile* profile, const std::string& country) {
  if (PrefService* prefs = PrefsFor(profile)) {
    prefs->SetString(kTorCountryPref, country);
  }
}

bool IsBuiltinTorSelected(Profile* profile) {
  const std::vector<Server> servers = GetServers(profile);
  return !servers.empty() &&
         servers[GetSelectedServerIndex(profile)].url == kBuiltinTorUrl;
}

std::u16string TorStatusText(Profile* profile) {
  if (!IsEnabled(profile) || !IsBuiltinTorSelected(profile)) {
    return std::u16string();
  }
  switch (TorManager::Get().state()) {
    case TorState::kStarting: {
      const int attempt = TorManager::Get().attempt();
      if (attempt > 1) {
        return u"Tor: retrying (" + base::NumberToString16(attempt) + u"/" +
               base::NumberToString16(kMaxStartAttempts) + u")\u2026";
      }
      return u"Tor: connecting\u2026";
    }
    case TorState::kReady:
      return u"Tor: connected";
    case TorState::kFailed:
      return u"Tor: could not connect (gave up after 5 tries)";
    case TorState::kStopped:
      break;
  }
  return std::u16string();
}

void RelaunchTor(Profile* profile) {
  if (!profile || !IsEnabled(profile) || !IsBuiltinTorSelected(profile)) {
    return;
  }
  TorManager::Get().Restart(TorDataDir(profile), GetTorCountry(profile));
}

void ReconnectTor(Profile* profile) {
  if (!profile || !IsEnabled(profile) || !IsBuiltinTorSelected(profile)) {
    return;
  }
  const TorState state = TorManager::Get().state();
  if (state == TorState::kFailed || state == TorState::kStopped) {
    // Nothing healthy to reconnect to: start it (fresh round of attempts).
    TorManager::Get().Start(TorDataDir(profile), GetTorCountry(profile));
  }
  CloseAllConnections(profile);
}

int GetSelectedServerIndex(Profile* profile) {
  PrefService* prefs = PrefsFor(profile);
  const int count = static_cast<int>(GetServers(profile).size());
  const int index = prefs ? prefs->GetInteger(kSelectedServerPref) : 0;
  return (index >= 0 && index < count) ? index : 0;
}

void SetSelectedServerIndex(Profile* profile, int index) {
  if (PrefService* prefs = PrefsFor(profile)) {
    prefs->SetInteger(kSelectedServerPref, std::max(0, index));
  }
}

std::string BuildPacScript(const PacConfig& config) {
  std::string script;
  script += "var PROXY = " + JsonOf(base::Value(config.pac_directive)) + ";\n";
  const char* scope = config.scope == Scope::kAllTabs   ? "all"
                      : config.scope == Scope::kSiteList ? "list"
                                                         : "off";
  script += std::string("var SCOPE = \"") + scope + "\";\n";
  script += std::string("var APPS_ON = ") +
            (config.use_for_apps ? "true" : "false") + ";\n";
  script +=
      "var SITES = " + JsonOf(base::Value(ToValueList(config.sites))) + ";\n";
  script += std::string("var EXC_ON = ") +
            (config.use_exceptions ? "true" : "false") + ";\n";
  script += "var EXCEPTIONS = " +
            JsonOf(base::Value(ToValueList(config.exceptions))) + ";\n";
  script += "var APPS = " +
            JsonOf(base::Value(ToValueList(config.app_hosts))) + ";\n";
  script += kPacBody;
  return script;
}

namespace {

// Registrable domains of the sidebar apps, refreshed by every Apply(). The
// per-tab WebRTC policy needs them without having the app list at hand.
// UI thread only.
std::vector<std::string>& AppHosts() {
  static base::NoDestructor<std::vector<std::string>> hosts;
  return *hosts;
}

void SetAppHosts(const std::vector<GURL>& app_urls) {
  std::vector<std::string>& hosts = AppHosts();
  hosts.clear();
  for (const GURL& url : app_urls) {
    // The app's registrable domain, so its other subdomains follow it.
    const std::string host = SiteKeyForUrl(url);
    if (!host.empty() &&
        std::find(hosts.begin(), hosts.end(), host) == hosts.end()) {
      hosts.push_back(host);
    }
  }
}

// Everything except |pac_directive|, read from the saved state.
PacConfig BuildConfig(Profile* profile, PrefService* prefs) {
  PacConfig config;
  config.scope = GetScope(profile);
  config.use_for_apps = IsUsedForApps(profile);
  for (const base::Value& value : prefs->GetList(kSitesPref)) {
    if (!value.is_string()) {
      continue;
    }
    const std::string site = NormalizeSite(value.GetString());
    if (!site.empty() && std::find(config.sites.begin(), config.sites.end(),
                                   site) == config.sites.end()) {
      config.sites.push_back(site);
    }
  }
  config.use_exceptions = IsExceptionsEnabled(profile);
  for (const base::Value& value : prefs->GetList(kExceptionsPref)) {
    if (!value.is_string()) {
      continue;
    }
    const std::string site = NormalizeSite(value.GetString());
    if (!site.empty() &&
        std::find(config.exceptions.begin(), config.exceptions.end(), site) ==
            config.exceptions.end()) {
      config.exceptions.push_back(site);
    }
  }
  config.app_hosts = AppHosts();
  return config;
}

// Same matching as inList() in the PAC script.
bool HostInList(const std::string& host,
                const std::vector<std::string>& list) {
  for (const std::string& d : list) {
    if (host == d) {
      return true;
    }
    if (host.size() > d.size() && base::EndsWith(host, "." + d)) {
      return true;
    }
  }
  return false;
}

// Hosts the PAC script always sends DIRECT.
bool IsLocalHost(const std::string& host) {
  if (host.empty() || host.find('.') == std::string::npos ||
      host.find(':') != std::string::npos || host == "localhost") {
    return true;
  }
  for (const char* suffix : {".local", ".localhost", ".internal", ".lan"}) {
    if (base::EndsWith(host, suffix)) {
      return true;
    }
  }
  for (const char* prefix : {"127.", "10.", "192.168.", "169.254."}) {
    if (base::StartsWith(host, prefix)) {
      return true;
    }
  }
  if (base::StartsWith(host, "172.")) {
    const size_t dot = host.find('.', 4);
    int second = 0;
    if (dot != std::string::npos &&
        base::StringToInt(host.substr(4, dot - 4), &second) && second >= 16 &&
        second <= 31) {
      return true;
    }
  }
  return false;
}

// The one decision for a page: does |host| go through the VPN? It mirrors
// FindProxyForURL() in the PAC script, and the WebRTC policy follows it:
//  1. Exception List (when its toggle is on) wins over everything.
//  2. A sidebar app uses the VPN only if the sidebar toggle is on, whatever
//     the scope says.
//  3. "All sites" -> every other site. "Specific sites" -> only listed ones.
bool UsesVpn(const PacConfig& config, const std::string& host) {
  if (IsLocalHost(host)) {
    return false;
  }
  if (config.use_exceptions && HostInList(host, config.exceptions)) {
    return false;
  }
  if (HostInList(host, config.app_hosts)) {
    return config.use_for_apps;
  }
  if (config.scope == Scope::kAllTabs) {
    return true;
  }
  if (config.scope == Scope::kSiteList) {
    return HostInList(host, config.sites);
  }
  return false;
}

// True if WebRTC must be kept inside the proxy for a page at |url|: the VPN
// is on, our PAC proxy is the active one and the page itself uses the VPN.
// (Pages that do not use the VPN are not touched at all.)
bool WebRtcShouldBlock(Profile* profile, const GURL& url) {
  PrefService* prefs = PrefsFor(profile);
  if (!prefs || !prefs->GetBoolean(kOwnsProxyPref) || !IsEnabled(profile) ||
      !url.SchemeIsHTTPOrHTTPS()) {
    return false;
  }
  return UsesVpn(BuildConfig(profile, prefs), base::ToLowerASCII(url.host()));
}

// The renderer preference is an enum, the profile pref is a string.
blink::mojom::WebRtcIpHandlingPolicy WebRtcPolicyFromString(
    const std::string& value) {
  using Policy = blink::mojom::WebRtcIpHandlingPolicy;
  if (value == "disable_non_proxied_udp") {
    return Policy::kDisableNonProxiedUdp;
  }
  if (value == "default_public_interface_only") {
    return Policy::kDefaultPublicInterfaceOnly;
  }
  if (value == "default_public_and_private_interfaces") {
    return Policy::kDefaultPublicAndPrivateInterfaces;
  }
  return Policy::kDefault;
}

class VpnWebRtcTabHelper;
// Every live helper (one per tab), so all tabs can be refreshed without
// walking the browser windows. UI thread only.
std::vector<raw_ptr<VpnWebRtcTabHelper>>& LiveHelpers();

// Sets WebRTC's IP handling policy for one tab (the profile-wide pref cannot
// tell tabs apart). Tabs that do not use the VPN keep the profile's own value.
class VpnWebRtcTabHelper
    : public content::WebContentsObserver,
      public content::WebContentsUserData<VpnWebRtcTabHelper> {
 public:
  ~VpnWebRtcTabHelper() override {
    std::erase_if(LiveHelpers(), [this](const raw_ptr<VpnWebRtcTabHelper>& p) {
      return p.get() == this;
    });
  }

  bool BelongsTo(Profile* original_profile) {
    return Profile::FromBrowserContext(web_contents()->GetBrowserContext())
               ->GetOriginalProfile() == original_profile;
  }

  void Update() {
    Profile* profile =
        Profile::FromBrowserContext(web_contents()->GetBrowserContext())
            ->GetOriginalProfile();
    PrefService* prefs = PrefsFor(profile);
    if (!prefs) {
      return;
    }
    const std::string policy =
        WebRtcShouldBlock(profile, web_contents()->GetLastCommittedURL())
            ? std::string("disable_non_proxied_udp")
            : prefs->GetString(::prefs::kWebRTCIPHandlingPolicy);
    blink::RendererPreferences* renderer_prefs =
        web_contents()->GetMutableRendererPrefs();
    const blink::mojom::WebRtcIpHandlingPolicy wanted =
        WebRtcPolicyFromString(policy);
    if (renderer_prefs->webrtc_ip_handling_policy == wanted) {
      return;
    }
    renderer_prefs->webrtc_ip_handling_policy = wanted;
    web_contents()->SyncRendererPrefs();
  }

  // content::WebContentsObserver:
  void PrimaryPageChanged(content::Page& page) override { Update(); }
  void RenderViewReady() override { Update(); }

 private:
  friend class content::WebContentsUserData<VpnWebRtcTabHelper>;
  explicit VpnWebRtcTabHelper(content::WebContents* contents)
      : content::WebContentsObserver(contents),
        content::WebContentsUserData<VpnWebRtcTabHelper>(*contents) {
    LiveHelpers().push_back(this);
  }

  WEB_CONTENTS_USER_DATA_KEY_DECL();
};

WEB_CONTENTS_USER_DATA_KEY_IMPL(VpnWebRtcTabHelper);

std::vector<raw_ptr<VpnWebRtcTabHelper>>& LiveHelpers() {
  static base::NoDestructor<std::vector<raw_ptr<VpnWebRtcTabHelper>>> helpers;
  return *helpers;
}

}  // namespace

void AttachWebRtcTabHelper(content::WebContents* contents) {
  if (contents) {
    VpnWebRtcTabHelper::CreateForWebContents(contents);
  }
}

void RefreshWebRtcPolicies(Profile* profile) {
  if (!profile) {
    return;
  }
  Profile* original = profile->GetOriginalProfile();
  for (const raw_ptr<VpnWebRtcTabHelper>& helper : LiveHelpers()) {
    if (helper->BelongsTo(original)) {
      helper->Update();
    }
  }
}

void Apply(Profile* profile, const std::vector<GURL>& app_urls) {
  PrefService* prefs = PrefsFor(profile);
  if (!prefs || profile->IsOffTheRecord()) {
    return;  // Incognito follows the original profile's setting by itself.
  }
  SetAppHosts(app_urls);
  // Older builds wrote the profile-wide WebRTC policy when the VPN turned on
  // and cleared it when it turned off. This build handles WebRTC per tab and
  // never writes it, so a copy left behind in an old profile would block calls
  // on every tab forever. Remove it once.
  if (!prefs->GetBoolean(kWebRtcMigratedPref)) {
    if (!prefs->IsManagedPreference(::prefs::kWebRTCIPHandlingPolicy) &&
        prefs->GetString(::prefs::kWebRTCIPHandlingPolicy) ==
            "disable_non_proxied_udp") {
      prefs->ClearPref(::prefs::kWebRTCIPHandlingPolicy);
    }
    prefs->SetBoolean(kWebRtcMigratedPref, true);
  }
  // Whether the built-in Tor exists decides the server list, and that is only
  // known a moment after startup: come back to this once it is.
  if (!TorManager::IsInstallKnown()) {
    TorManager::WhenInstallKnown(base::BindOnce(
        [](base::WeakPtr<Profile> weak_profile, std::vector<GURL> urls) {
          if (weak_profile) {
            Apply(weak_profile.get(), urls);
          }
        },
        profile->GetWeakPtr(), app_urls));
    return;
  }
  // A proxy forced by policy always wins; do not fight it.
  if (prefs->IsManagedPreference(proxy_config::prefs::kProxy)) {
    return;
  }

  const std::vector<Server> servers = GetServers(profile);
  if (!IsEnabled(profile) || servers.empty()) {
    TorManager::Get().Stop();  // The VPN is off: Tor must not keep running.
    if (prefs->GetBoolean(kOwnsProxyPref)) {
      prefs->ClearPref(proxy_config::prefs::kProxy);
      prefs->SetBoolean(kOwnsProxyPref, false);
      CloseIdleConnections(profile);
    }
    RefreshWebRtcPolicies(profile);  // Tabs go back to the normal policy.
    return;
  }

  const Server& selected = servers[GetSelectedServerIndex(profile)];
  if (selected.url == kBuiltinTorUrl) {
    // Until Tor has connected, proxied requests fail (they never go out
    // directly); a reload works once the menu says "Tor: connected".
    TorManager::Get().Start(profile->GetPath().AppendASCII("SidebarVpnTor"),
                            GetTorCountry(profile));
  } else {
    TorManager::Get().Stop();
  }

  PacConfig config = BuildConfig(profile, prefs);
  config.pac_directive = selected.pac_directive;

  // "mandatory": if the script cannot be loaded, requests fail instead of
  // silently going out directly. The same goes for a dead proxy server: the
  // script never lists DIRECT as a fallback for a proxied site.
  base::DictValue proxy = ProxyConfigDictionary::CreatePacScript(
      std::string(kPacDataUrlPrefix) + base::Base64Encode(BuildPacScript(config)),
      /*pac_mandatory=*/true);
  const bool changed = prefs->GetDict(proxy_config::prefs::kProxy) != proxy ||
                       !prefs->GetBoolean(kOwnsProxyPref);
  prefs->SetDict(proxy_config::prefs::kProxy, std::move(proxy));
  prefs->SetBoolean(kOwnsProxyPref, true);
  if (changed) {
    CloseIdleConnections(profile);
  }
  // WebRTC is handled per tab (see VpnWebRtcTabHelper): a tab that uses the
  // VPN may not reveal the real address around the proxy, so its calls that
  // cannot go through the proxy do not connect. Tabs that do not use the VPN
  // are left alone.
  RefreshWebRtcPolicies(profile);
}

void ShowEditDialog(views::Widget* parent,
                    Profile* profile,
                    EditKind kind,
                    base::RepeatingClosure on_saved) {
  PrefService* prefs = PrefsFor(profile);
  if (!parent || !prefs) {
    return;
  }
  const bool is_list = kind != EditKind::kServers;
  const char* list_pref =
      kind == EditKind::kExceptions ? kExceptionsPref : kSitesPref;

  std::string initial;
  if (is_list) {
    for (const base::Value& value : prefs->GetList(list_pref)) {
      if (value.is_string()) {
        initial += value.GetString() + "\n";
      }
    }
  } else {
    initial = prefs->GetString(kServersPref);
  }

  auto contents = std::make_unique<EditContents>(
      kind == EditKind::kSites
          ? u"Specific sites that use the VPN, one per line. \"example.com\" "
            u"also covers its subdomains. Sites often load files from other "
            u"domains too (for example a video site's CDN): add those as "
            u"well if parts of a page do not load."
      : kind == EditKind::kExceptions
          ? u"Exception List: sites that never use the VPN, one per line, "
            u"even when \"all sites\" or \"specific sites\" is on (needs "
            u"\"Ignore sites in Exception List\" to be checked). "
            u"\"example.com\" also covers its subdomains. Files a page loads "
            u"from other domains (a CDN, for example) are not covered: add "
            u"those as well if parts of a page still use the VPN."
            : u"One server per line:  Name = scheme://host:port\n"
              u"Schemes: socks5, socks4, http, https. The first valid line is "
              u"the default. Example:  Germany = socks5://203.0.113.5:1080",
      base::UTF8ToUTF16(initial));
  EditContents* form = contents.get();

  auto dialog = std::make_unique<views::DialogDelegate>();
  dialog->SetTitle(kind == EditKind::kSites        ? u"VPN specific sites"
                   : kind == EditKind::kExceptions ? u"VPN Exception List"
                                                   : u"VPN servers");
  dialog->SetModalType(ui::mojom::ModalType::kWindow);
  dialog->SetButtonLabel(ui::mojom::DialogButton::kOk, u"Save");
  dialog->SetButtonLabel(ui::mojom::DialogButton::kCancel, u"Cancel");
  dialog->SetContentsView(std::move(contents));
  dialog->SetInitiallyFocusedView(form->textarea());
  dialog->SetAcceptCallbackWithClose(base::BindRepeating(
      [](Profile* profile, bool is_list, const char* list_pref,
         base::RepeatingClosure on_saved, EditContents* form) {
        const std::string text = base::UTF16ToUTF8(form->textarea()->GetText());
        PrefService* prefs = PrefsFor(profile);
        if (!prefs) {
          return true;
        }
        if (is_list) {
          prefs->SetList(list_pref, ToValueList(ParseSites(text)));
        } else {
          // Keep only the valid lines, so the form shows what is really used.
          std::string cleaned;
          std::vector<Server> servers = ParseServers(text);
          if (servers.empty()) {
            form->ShowError(
                u"No valid server found. Use:  Name = socks5://host:port");
            return false;
          }
          for (const Server& server : servers) {
            cleaned += server.name + " = " + server.url + "\n";
          }
          prefs->SetString(kServersPref, cleaned);
          if (prefs->GetInteger(kSelectedServerPref) >=
              static_cast<int>(servers.size())) {
            prefs->SetInteger(kSelectedServerPref, 0);
          }
        }
        if (on_saved) {
          on_saved.Run();
        }
        return true;
      },
      // The profile outlives its dialogs: the dialog is modal to a window of
      // that profile.
      base::Unretained(profile), is_list, list_pref, std::move(on_saved),
      base::Unretained(form)));

  views::Widget* widget = constrained_window::CreateBrowserModalDialogViews(
      std::move(dialog), parent->GetNativeWindow());
  if (widget) {
    widget->Show();
  }
}

}  // namespace vpn
}  // namespace sidebar_apps
