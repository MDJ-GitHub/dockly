// Copyright 2026 MDJ. Use of this source code is governed by a BSD-style
// license that can be found in the Chromium LICENSE file.

#ifndef CHROME_BROWSER_UI_VIEWS_SIDEBAR_APPS_SIDEBAR_ADBLOCK_ENGINE_H_
#define CHROME_BROWSER_UI_VIEWS_SIDEBAR_APPS_SIDEBAR_ADBLOCK_ENGINE_H_

#include <optional>
#include <string>
#include <vector>

#include "base/memory/weak_ptr.h"
#include "chrome/browser/ui/views/sidebar_apps/adblock_rs/lib.rs.h"
#include "chrome/browser/ui/views/sidebar_apps/sidebar_adblock.h"

namespace base {
template <typename T>
class NoDestructor;
}  // namespace base

namespace sidebar_apps {
namespace adblock {

enum class EngineState {
  kNotLoaded,
  kLoading,
  kReady,
  kMissing,  // No filter lists next to the browser.
};

// Owns the adblock-rust engine. One per browser process, UI thread only.
//
// Files, shipped next to the browser like the Tor bundle:
//   <folder of chrome.exe>/adblock/lists/*.txt   (EasyList, EasyPrivacy,
//                                                 uBlock filters, ... in
//                                                 Adblock Plus syntax)
//   <folder of chrome.exe>/adblock/resources.json (optional; uBlock Origin
//                                                 "redirect" / scriptlet
//                                                 resources)
// The engine is built once on a thread-pool thread; until it is ready nothing
// is blocked (pages still load).
class Engine {
 public:
  static Engine& Get();

  // Starts loading if it has not started yet. Cheap to call repeatedly.
  void EnsureLoaded();
  EngineState state() const { return state_; }
  int rule_count() const { return rule_count_; }

  // Only valid when state() == kReady, otherwise returns "no match".
  rs::BlockResult Check(const GURL& url,
                        const GURL& source_url,
                        RequestKind kind);
  // Element-hiding selectors that apply to |url| (specific ones).
  rs::CosmeticResult Cosmetic(const GURL& url);
  // Generic selectors for the classes / ids a page actually uses.
  std::vector<std::string> HiddenSelectors(const GURL& url,
                                           std::vector<std::string> classes,
                                           std::vector<std::string> ids);

 private:
  friend class base::NoDestructor<Engine>;
  Engine();
  ~Engine();

  struct Built {
    std::optional<::rust::Box<rs::AdblockEngine>> engine;
    int rule_count = 0;
  };
  void OnBuilt(Built built);

  EngineState state_ = EngineState::kNotLoaded;
  int rule_count_ = 0;
  std::optional<::rust::Box<rs::AdblockEngine>> engine_;
  base::WeakPtrFactory<Engine> weak_factory_{this};
};

}  // namespace adblock
}  // namespace sidebar_apps

#endif  // CHROME_BROWSER_UI_VIEWS_SIDEBAR_APPS_SIDEBAR_ADBLOCK_ENGINE_H_
