// Copyright 2026 MDJ. Use of this source code is governed by a BSD-style
// license that can be found in the Chromium LICENSE file.

#ifndef CHROME_BROWSER_UI_VIEWS_SIDEBAR_APPS_SIDEBAR_ADBLOCK_H_
#define CHROME_BROWSER_UI_VIEWS_SIDEBAR_APPS_SIDEBAR_ADBLOCK_H_

#include <string>
#include <vector>

#include "base/functional/callback.h"
#include "url/gurl.h"

class Profile;

namespace user_prefs {
class PrefRegistrySyncable;
}  // namespace user_prefs

namespace views {
class Widget;
}  // namespace views

namespace sidebar_apps {
namespace adblock {

// Ad blocker for the browser, built on Brave's adblock-rust (see
// adblock_rs/ and sidebar_adblock_engine.h). Same three options as the VPN:
//   - "Block on all sites"      (kAllTabs)
//   - "Block on specific sites" (kSiteList)
//   - "Block in sidebar apps"
// There is no master switch: the blocker is on when any option is on.
//
// On top of that there is an Exception List with its own toggle ("Ignore
// sites in Exception List"). Pages of those sites are never blocked, whatever
// the other options say (it beats all three). The toggle alone does not turn
// the blocker on.
//
// All state lives in profile prefs, so every window of the profile agrees.
enum class Scope {
  kAllTabs = 0,   // Every page, except the sidebar apps unless |UsedForApps|.
  kSiteList = 1,  // Only pages of the specific sites (plus apps if asked).
  kOff = 2,       // No regular page (the apps may still be blocked).
};

// What kind of request is being checked (maps 1:1 to adblock-rust request
// types; filters such as "||ads.example^$script" depend on it).
enum class RequestKind {
  kDocument,
  kSubframe,
  kScript,
  kStylesheet,
  kImage,
  kFont,
  kMedia,
  kXhr,
  kOther,
};

// Call from the profile pref registration (browser_prefs.cc).
void RegisterProfilePrefs(user_prefs::PrefRegistrySyncable* registry);

bool IsEnabled(Profile* profile);
Scope GetScope(Profile* profile);
void SetScope(Profile* profile, Scope scope);
bool IsUsedForApps(Profile* profile);
void SetUsedForApps(Profile* profile, bool used);

// Registrable domain of |url| ("example.com"), or the host for IPs. Empty if
// |url| is not a web page. Same rule as vpn::SiteKeyForUrl().
std::string SiteKeyForUrl(const GURL& url);
bool IsSiteListed(Profile* profile, const std::string& site_key);
void AddSite(Profile* profile, const std::string& site_key);
void RemoveSite(Profile* profile, const std::string& site_key);

// The Exception List. Same site keys as the specific sites above.
bool IsExceptionsEnabled(Profile* profile);
void SetExceptionsEnabled(Profile* profile, bool enabled);
bool IsExcepted(Profile* profile, const std::string& site_key);
void AddException(Profile* profile, const std::string& site_key);
void RemoveException(Profile* profile, const std::string& site_key);

// Should ads be blocked on the page |page_url| (the top-level page the request
// belongs to)? This is the scope / list / sidebar-app decision only.
bool IsBlockingOnPage(Profile* profile, const GURL& page_url);

// The full decision for one request: the page policy above AND the filter
// lists. |source_url| is the page (or opener, for a document request) that the
// request comes from. UI thread only.
bool ShouldBlockRequest(Profile* profile,
                        const GURL& url,
                        const GURL& source_url,
                        const GURL& page_url,
                        RequestKind kind);

// Remembers the sidebar apps' sites (the policy cannot tell a sidebar tab from
// a normal tab any other way) and makes sure the filter engine is loading.
// Safe to call as often as you like.
void Apply(Profile* profile, const std::vector<GURL>& app_urls);

// Menu helpers.
int GetBlockedCount();  // Requests blocked since the browser started.
// "Ad block: loading lists..." / "Ad block: N rules" / "Ad block: lists not
// found", or empty when the blocker is off.
std::u16string StatusText(Profile* profile);

// "Edit specific sites" / "Edit Exception List" form: one site per line.
enum class EditKind { kSites, kExceptions };
void ShowEditDialog(views::Widget* parent,
                    Profile* profile,
                    EditKind kind,
                    base::RepeatingClosure on_saved);

}  // namespace adblock
}  // namespace sidebar_apps

#endif  // CHROME_BROWSER_UI_VIEWS_SIDEBAR_APPS_SIDEBAR_ADBLOCK_H_
