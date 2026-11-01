// Copyright 2026 MDJ. Use of this source code is governed by a BSD-style
// license that can be found in the Chromium LICENSE file.

#ifndef CHROME_BROWSER_UI_VIEWS_SIDEBAR_APPS_SIDEBAR_VPN_H_
#define CHROME_BROWSER_UI_VIEWS_SIDEBAR_APPS_SIDEBAR_VPN_H_

#include <array>
#include <string>
#include <vector>

#include "base/functional/callback.h"
#include "url/gurl.h"

class Profile;

namespace user_prefs {
class PrefRegistrySyncable;
}  // namespace user_prefs

namespace content {
class WebContents;
}  // namespace content

namespace views {
class Widget;
}  // namespace views

namespace sidebar_apps {
namespace vpn {

// A simple "VPN" for the browser. Underneath it is a proxy: the profile's
// proxy setting is pointed at a PAC script that sends the chosen sites (or
// everything) through one proxy server (SOCKS5 / SOCKS4 / HTTP / HTTPS).
// Only the traffic of this browser is affected, not the rest of the computer.
//
// All state lives in profile prefs, so every window of the profile agrees.

// There is no master switch: the VPN is active when "all sites" is on, or
// "specific sites" is on, or "sidebar apps" is on (see IsEnabled()).
//
// Who uses the VPN (the proxy AND WebRTC, which always go together):
//  - "Sidebar apps" off: the sidebar apps are not touched at all.
//  - "Sidebar apps" on: the sidebar apps use the VPN, whatever the scope is.
//  - "All sites": every site except the sidebar apps.
//  - "Specific sites": only the listed sites (not all sites, not the apps).
//  - Exception List on ("Ignore sites in Exception List"): the sites in it
//    never use the VPN, whatever the other options say. The toggle alone does
//    not turn the VPN on.
//
// WebRTC: a page that uses the VPN gets "disable_non_proxied_udp" (Tor cannot
// carry UDP, so calls on such pages do not connect). A page that does not use
// the VPN keeps the profile's normal WebRTC policy. This is decided per tab
// (see AttachWebRtcTabHelper), not with the profile-wide pref.
enum class Scope {
  kAllTabs = 0,   // Every site, except the sidebar apps unless |UsedForApps|.
  kSiteList = 1,  // Only the specific sites (plus the apps if asked).
  kOff = 2,       // No regular site (the apps may still use it).
};

// How many servers the "Server" menu can show.
constexpr int kMaxServers = 16;

// The built-in Tor (see sidebar_tor.h) shows up as the first server when the tor
// files are installed next to the browser. Selecting it starts Tor whenever the
// VPN is on and stops it when the VPN is off.
constexpr char kBuiltinTorUrl[] = "tor://builtin";

struct TorCountry {
  const char* code;      // "de"; empty = any country.
  const char16_t* name;
};
constexpr std::array<TorCountry, 15> kTorCountries = {
    TorCountry{"", u"Automatic"},        TorCountry{"de", u"Germany"},
    TorCountry{"nl", u"Netherlands"},    TorCountry{"us", u"United States"},
    TorCountry{"gb", u"United Kingdom"}, TorCountry{"fr", u"France"},
    TorCountry{"se", u"Sweden"},         TorCountry{"ch", u"Switzerland"},
    TorCountry{"ca", u"Canada"},         TorCountry{"jp", u"Japan"},
    TorCountry{"es", u"Spain"},          TorCountry{"it", u"Italy"},
    TorCountry{"fi", u"Finland"},        TorCountry{"ro", u"Romania"},
    TorCountry{"at", u"Austria"}};

struct Server {
  std::string name;           // "Tor (local)"
  std::string url;            // "socks5://127.0.0.1:9050"
  std::string pac_directive;  // "SOCKS5 127.0.0.1:9050"
};

// Everything BuildPacScript() needs. Kept separate from prefs so it can be
// unit tested.
struct PacConfig {
  std::string pac_directive;
  Scope scope = Scope::kOff;
  bool use_for_apps = false;
  std::vector<std::string> sites;      // Normalized hosts.
  bool use_exceptions = false;         // "Ignore sites in Exception List".
  std::vector<std::string> exceptions;  // Normalized hosts; always DIRECT.
  std::vector<std::string> app_hosts;  // Normalized hosts of the sidebar apps.
};

// Call from the profile pref registration (browser_prefs.cc).
void RegisterProfilePrefs(user_prefs::PrefRegistrySyncable* registry);

// True when any of the three options is on.
bool IsEnabled(Profile* profile);
Scope GetScope(Profile* profile);
void SetScope(Profile* profile, Scope scope);
bool IsUsedForApps(Profile* profile);
void SetUsedForApps(Profile* profile, bool used);

// The key stored in the site list for |url|: its registrable domain
// ("example.com", which also covers "www.example.com"), or the host itself for
// IP addresses and the like. Empty if |url| is not a web page.
std::string SiteKeyForUrl(const GURL& url);
bool IsSiteListed(Profile* profile, const std::string& site_key);
void AddSite(Profile* profile, const std::string& site_key);
void RemoveSite(Profile* profile, const std::string& site_key);

// The valid servers (at most kMaxServers) and the selected one.
std::vector<Server> GetServers(Profile* profile);
int GetSelectedServerIndex(Profile* profile);
void SetSelectedServerIndex(Profile* profile, int index);

// The Exception List. Same site keys as the specific sites above.
bool IsExceptionsEnabled(Profile* profile);
void SetExceptionsEnabled(Profile* profile, bool enabled);
bool IsExcepted(Profile* profile, const std::string& site_key);
void AddException(Profile* profile, const std::string& site_key);
void RemoveException(Profile* profile, const std::string& site_key);

// Exit country of the built-in Tor ("" = automatic).
std::string GetTorCountry(Profile* profile);
void SetTorCountry(Profile* profile, const std::string& country);
// True if the selected server is the built-in Tor.
bool IsBuiltinTorSelected(Profile* profile);
// "Tor: connecting..." / "Tor: connected" / "Tor: could not start", or empty
// when the built-in Tor is not in use.
std::u16string TorStatusText(Profile* profile);
// Both do nothing unless the VPN is on and the built-in Tor is the selected
// server.
// Relaunch: closes the tor program and starts it again from scratch (fresh
// round of connection attempts).
void RelaunchTor(Profile* profile);
// Reconnect: keeps the tor program, but drops every open connection of the
// browser so pages reconnect through Tor. If Tor is stopped or gave up, it is
// started again.
void ReconnectTor(Profile* profile);

// Writes the profile's proxy setting to match the saved state. |app_urls| are
// the sidebar apps (the PAC script cannot tell which tab a request comes from,
// so apps are recognised by their site). Safe to call as often as you like;
// does nothing when nothing changed. Never touches a proxy the user set up
// themselves unless the VPN is on.
void Apply(Profile* profile, const std::vector<GURL>& app_urls);

// Keeps the WebRTC policy of one tab in line with whether its page uses the
// VPN. Call for every tab (chrome/browser/ui/tab_helpers.cc). Safe to call more
// than once. UI thread only.
void AttachWebRtcTabHelper(content::WebContents* contents);
// Re-evaluates the WebRTC policy of every open tab of the profile. Apply() calls
// it, so you normally do not need to.
void RefreshWebRtcPolicies(Profile* profile);

std::string BuildPacScript(const PacConfig& config);

// "Edit specific sites" / "Edit Exception List" / "Edit servers" form: one
// entry per line.
enum class EditKind { kSites, kExceptions, kServers };
void ShowEditDialog(views::Widget* parent,
                    Profile* profile,
                    EditKind kind,
                    base::RepeatingClosure on_saved);

}  // namespace vpn
}  // namespace sidebar_apps

#endif  // CHROME_BROWSER_UI_VIEWS_SIDEBAR_APPS_SIDEBAR_VPN_H_
