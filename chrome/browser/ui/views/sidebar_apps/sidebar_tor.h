// Copyright 2026 MDJ. Use of this source code is governed by a BSD-style
// license that can be found in the Chromium LICENSE file.

#ifndef CHROME_BROWSER_UI_VIEWS_SIDEBAR_APPS_SIDEBAR_TOR_H_
#define CHROME_BROWSER_UI_VIEWS_SIDEBAR_APPS_SIDEBAR_TOR_H_

#include <string>
#include <vector>

#include "base/files/file_path.h"
#include "base/functional/callback.h"
#include "base/memory/weak_ptr.h"
#include "base/process/process.h"
#include "base/time/time.h"
#include "base/timer/timer.h"

namespace base {
template <typename T>
class NoDestructor;
}  // namespace base

namespace sidebar_apps {
namespace vpn {

// Port of the built-in Tor. Not 9050 / 9150 so it never clashes with a Tor the
// user runs themselves.
constexpr int kTorSocksPort = 19050;

// How many times in a row the built-in Tor is (re)started before giving up.
constexpr int kMaxStartAttempts = 5;

enum class TorState {
  kStopped,
  kStarting,  // Running, still connecting to the Tor network (or about to be
              // relaunched after a failed attempt).
  kReady,
  kFailed,    // Gave up after kMaxStartAttempts attempts.
};

// Runs the "tor" program shipped next to the browser:
//   <folder of chrome.exe>/tor/tor.exe          (tor on Linux / macOS)
//   <folder of chrome.exe>/tor/data/geoip, geoip6
// (both come from the "Tor Expert Bundle"). One copy per browser process.
// Tor ends by itself if the browser dies (--__OwningControllerProcess).
//
// Every launch first kills any leftover tor program that was started from that
// exact file (for example one that survived a browser that was killed). A tor
// started from anywhere else (Tor Browser, "Tor (local)") is never touched.
//
// If a launch fails (cannot start, exits, or does not connect in time) it is
// retried automatically, up to kMaxStartAttempts times in a row.
// UI thread only.
class TorManager {
 public:
  static TorManager& Get();
  // Whether the tor files are there is checked once, off the UI thread (the UI
  // thread may not touch the disk). Until the check is done:
  //  - IsInstallKnown() is false (and the first call starts the check),
  //  - IsInstalled() is false.
  static bool IsInstallKnown();
  static bool IsInstalled();
  // Runs |callback| once the check is done (right away if it already is).
  static void WhenInstallKnown(base::OnceClosure callback);

  // Starts Tor with exits in |country| ("de"; empty = any country). Does
  // nothing if it already runs (or is starting / retrying) with the same
  // settings; restarts it if the country changed. After a failure that used
  // up all attempts, calling this again starts a fresh round of attempts.
  void Start(const base::FilePath& data_dir, const std::string& country);
  // Ends Tor, then starts it again from scratch (fresh round of attempts).
  void Restart(const base::FilePath& data_dir, const std::string& country);
  // Ends Tor. Does nothing if it is not running.
  void Stop();
  TorState state() const { return state_; }
  // 1-based number of the current launch attempt (0 when nothing started).
  int attempt() const { return attempt_; }

 private:
  friend class base::NoDestructor<TorManager>;
  TorManager();
  ~TorManager();

  void OnInstallChecked(bool installed);
  // Launches tor now (counts as the next attempt).
  void BeginLaunch();
  void OnLaunched(int generation, base::Process process);
  void Poll();
  void OnPolled(int generation, bool ready, bool exited);
  // The current attempt failed: clean up, then retry or give up.
  void HandleFailure();

  enum class InstallState { kUnknown, kChecking, kInstalled, kMissing };
  InstallState install_state_ = InstallState::kUnknown;
  std::vector<base::OnceClosure> install_waiters_;

  TorState state_ = TorState::kStopped;
  base::Process process_;
  base::FilePath data_dir_;
  std::string country_;
  base::TimeTicks started_;
  bool launching_ = false;
  bool poll_in_flight_ = false;
  int attempt_ = 0;
  // Replies that belong to an older launch / Stop() are ignored.
  int generation_ = 0;
  base::RepeatingTimer poll_timer_;
  // Pause between a failed attempt and the next one.
  base::OneShotTimer retry_timer_;
  base::WeakPtrFactory<TorManager> weak_factory_{this};
};

}  // namespace vpn
}  // namespace sidebar_apps

#endif  // CHROME_BROWSER_UI_VIEWS_SIDEBAR_APPS_SIDEBAR_TOR_H_
