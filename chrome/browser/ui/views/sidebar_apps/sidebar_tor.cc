// Copyright 2026 MDJ. Use of this source code is governed by a BSD-style
// license that can be found in the Chromium LICENSE file.

#include "chrome/browser/ui/views/sidebar_apps/sidebar_tor.h"

#include <iterator>
#include <string>
#include <string_view>
#include <utility>

#include "base/command_line.h"
#include "base/files/file_util.h"
#include "base/functional/bind.h"
#include "base/no_destructor.h"
#include "base/path_service.h"
#include "base/process/launch.h"
#include "base/process/process.h"
#include "base/process/process_handle.h"
#include "base/process/process_iterator.h"
#include "base/strings/string_number_conversions.h"
#include "base/task/thread_pool.h"
#include "base/threading/scoped_blocking_call.h"
#include "build/build_config.h"

#if BUILDFLAG(IS_WIN)
#include <windows.h>
#endif

namespace sidebar_apps {
namespace vpn {

namespace {

constexpr base::TimeDelta kStartPollInterval = base::Milliseconds(500);
constexpr base::TimeDelta kReadyPollInterval = base::Seconds(5);
constexpr base::TimeDelta kStartTimeout = base::Seconds(120);
// Pause before the next attempt after a failed one.
constexpr base::TimeDelta kRetryDelay = base::Seconds(2);
// Tor runs with this as its working folder, so its log can be given as a plain
// file name (Tor cannot take a log path that contains spaces).
constexpr char kLogFileName[] = "tor.log";

base::FilePath TorDir() {
  base::FilePath exe_dir;
  base::PathService::Get(base::DIR_EXE, &exe_dir);
  return exe_dir.AppendASCII("tor");
}

base::FilePath TorExe() {
#if BUILDFLAG(IS_WIN)
  return TorDir().AppendASCII("tor.exe");
#else
  return TorDir().AppendASCII("tor");
#endif
}

base::FilePath GeoIpFile(const char* name) {
  return TorDir().AppendASCII("data").AppendASCII(name);
}

// Thread pool: are the tor program and its geoip files there?
bool CheckInstalled() {
  base::ScopedBlockingCall blocking(FROM_HERE, base::BlockingType::MAY_BLOCK);
  return base::PathExists(TorExe()) && base::PathExists(GeoIpFile("geoip"));
}

bool IsValidCountry(const std::string& country) {
  return country.size() == 2 && country[0] >= 'a' && country[0] <= 'z' &&
         country[1] >= 'a' && country[1] <= 'z';
}

// Thread pool: is |entry| the tor program that ships with this browser (the
// exact same file), as opposed to Tor Browser or a tor the user runs?
bool IsOurTor(const base::ProcessEntry& entry) {
#if BUILDFLAG(IS_WIN)
  base::Process process =
      base::Process::OpenWithAccess(entry.pid(), PROCESS_QUERY_LIMITED_INFORMATION);
  if (!process.IsValid()) {
    return false;
  }
  wchar_t buffer[MAX_PATH * 4];
  DWORD size = static_cast<DWORD>(std::size(buffer));
  if (!::QueryFullProcessImageNameW(process.Handle(), 0, buffer, &size)) {
    return false;
  }
  return base::FilePath::CompareEqualIgnoreCase(
      base::FilePath(std::wstring_view(buffer, size)).value(),
      TorExe().value());
#else
  // No cheap way to get another process's full path here: our tor is the one
  // listening on our private port.
  const std::string port_arg = "127.0.0.1:" + base::NumberToString(kTorSocksPort);
  for (const std::string& arg : entry.cmd_line_args()) {
    if (arg == port_arg) {
      return true;
    }
  }
  return false;
#endif
}

// Thread pool: ends every tor program started from this browser's own tor
// file that is still running (left over from a browser that was killed, from
// a previous attempt, or from before a refresh).
void KillLeftoverTor() {
  base::ScopedBlockingCall blocking(FROM_HERE, base::BlockingType::MAY_BLOCK);
  base::NamedProcessIterator iterator(TorExe().BaseName().value(), nullptr);
  while (const base::ProcessEntry* entry = iterator.NextProcessEntry()) {
    if (!IsOurTor(*entry)) {
      continue;
    }
    base::Process old = base::Process::Open(entry->pid());
    if (old.IsValid()) {
      old.Terminate(/*exit_code=*/0, /*wait=*/true);
    }
  }
}

// Thread pool: closes leftover tors, creates the data folder and starts tor.
base::Process LaunchTor(base::FilePath data_dir, std::string country) {
  base::ScopedBlockingCall blocking(FROM_HERE, base::BlockingType::MAY_BLOCK);
  KillLeftoverTor();
  if (!base::CreateDirectory(data_dir)) {
    return base::Process();
  }
  base::DeleteFile(data_dir.AppendASCII(kLogFileName));

  base::CommandLine command(TorExe());
  command.AppendArg("--SocksPort");
  command.AppendArg("127.0.0.1:" + base::NumberToString(kTorSocksPort));
  command.AppendArg("--DataDirectory");
  command.AppendArgPath(data_dir);
  command.AppendArg("--GeoIPFile");
  command.AppendArgPath(GeoIpFile("geoip"));
  command.AppendArg("--GeoIPv6File");
  command.AppendArgPath(GeoIpFile("geoip6"));
  command.AppendArg("--ClientOnly");
  command.AppendArg("1");
  command.AppendArg("--SafeLogging");
  command.AppendArg("1");
  command.AppendArg("--Log");
  command.AppendArg(std::string("notice file ") + kLogFileName);
  // Tor quits on its own when this process disappears, even after a crash.
  command.AppendArg("--__OwningControllerProcess");
  command.AppendArg(base::NumberToString(base::GetCurrentProcId()));
  if (IsValidCountry(country)) {
    command.AppendArg("--ExitNodes");
    command.AppendArg("{" + country + "}");
    command.AppendArg("--StrictNodes");
    command.AppendArg("1");
  }

  base::LaunchOptions options;
  options.current_directory = data_dir;
#if BUILDFLAG(IS_WIN)
  options.start_hidden = true;
#endif
  return base::LaunchProcess(command, options);
}

struct PollResult {
  bool ready = false;
  bool exited = false;
};

// Thread pool: has tor finished connecting, and is it still running?
PollResult CheckTor(base::Process process, base::FilePath log_path) {
  base::ScopedBlockingCall blocking(FROM_HERE, base::BlockingType::MAY_BLOCK);
  PollResult result;
  int exit_code = 0;
  result.exited =
      process.WaitForExitWithTimeout(base::TimeDelta(), &exit_code);
  std::string log;
  if (base::ReadFileToString(log_path, &log)) {
    result.ready = log.find("Bootstrapped 100%") != std::string::npos;
  }
  return result;
}

void TerminateInBackground(base::Process process) {
  if (!process.IsValid()) {
    return;
  }
  // Ending a process can block, so it happens off the UI thread.
  base::ThreadPool::PostTask(
      FROM_HERE,
      {base::MayBlock(), base::TaskShutdownBehavior::CONTINUE_ON_SHUTDOWN},
      base::BindOnce(
          [](base::Process p) {
            p.Terminate(/*exit_code=*/0, /*wait=*/true);
          },
          std::move(process)));
}

}  // namespace

TorManager::TorManager() = default;
TorManager::~TorManager() = default;

// static
TorManager& TorManager::Get() {
  static base::NoDestructor<TorManager> instance;
  return *instance;
}

// static
bool TorManager::IsInstallKnown() {
  TorManager& self = Get();
  if (self.install_state_ == InstallState::kUnknown) {
    self.install_state_ = InstallState::kChecking;
    base::ThreadPool::PostTaskAndReplyWithResult(
        FROM_HERE, {base::MayBlock(), base::TaskPriority::USER_VISIBLE},
        base::BindOnce(&CheckInstalled),
        base::BindOnce(&TorManager::OnInstallChecked,
                       self.weak_factory_.GetWeakPtr()));
  }
  return self.install_state_ == InstallState::kInstalled ||
         self.install_state_ == InstallState::kMissing;
}

// static
bool TorManager::IsInstalled() {
  return IsInstallKnown() && Get().install_state_ == InstallState::kInstalled;
}

// static
void TorManager::WhenInstallKnown(base::OnceClosure callback) {
  if (IsInstallKnown()) {
    std::move(callback).Run();
    return;
  }
  Get().install_waiters_.push_back(std::move(callback));
}

void TorManager::OnInstallChecked(bool installed) {
  install_state_ =
      installed ? InstallState::kInstalled : InstallState::kMissing;
  std::vector<base::OnceClosure> waiters = std::move(install_waiters_);
  install_waiters_.clear();
  for (base::OnceClosure& waiter : waiters) {
    std::move(waiter).Run();
  }
}

void TorManager::Start(const base::FilePath& data_dir,
                       const std::string& country) {
  if (!IsInstalled()) {
    state_ = TorState::kFailed;
    return;
  }
  const bool same_settings = data_dir_ == data_dir && country_ == country;
  const bool busy =
      launching_ || retry_timer_.IsRunning() ||
      (process_.IsValid() && state_ != TorState::kFailed);
  if (same_settings && busy) {
    return;  // Already running, starting or retrying as asked.
  }
  Stop();
  data_dir_ = data_dir;
  country_ = country;
  attempt_ = 0;
  BeginLaunch();
}

void TorManager::Restart(const base::FilePath& data_dir,
                         const std::string& country) {
  Stop();
  Start(data_dir, country);
}

void TorManager::Stop() {
  ++generation_;
  launching_ = false;
  poll_in_flight_ = false;
  poll_timer_.Stop();
  retry_timer_.Stop();
  TerminateInBackground(std::move(process_));
  process_ = base::Process();
  state_ = TorState::kStopped;
}

void TorManager::BeginLaunch() {
  ++attempt_;
  state_ = TorState::kStarting;
  started_ = base::TimeTicks::Now();
  launching_ = true;
  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE, {base::MayBlock(), base::TaskPriority::USER_VISIBLE},
      base::BindOnce(&LaunchTor, data_dir_, country_),
      base::BindOnce(&TorManager::OnLaunched, weak_factory_.GetWeakPtr(),
                     generation_));
}

void TorManager::OnLaunched(int generation, base::Process process) {
  if (generation != generation_) {
    TerminateInBackground(std::move(process));  // Stopped while launching.
    return;
  }
  launching_ = false;
  if (!process.IsValid()) {
    HandleFailure();
    return;
  }
  process_ = std::move(process);
  poll_timer_.Start(FROM_HERE, kStartPollInterval, this, &TorManager::Poll);
}

void TorManager::Poll() {
  if (poll_in_flight_ || !process_.IsValid()) {
    return;
  }
  poll_in_flight_ = true;
  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE, {base::MayBlock(), base::TaskPriority::USER_VISIBLE},
      base::BindOnce(&CheckTor, process_.Duplicate(),
                     data_dir_.AppendASCII(kLogFileName)),
      base::BindOnce(
          [](base::WeakPtr<TorManager> self, int generation,
             PollResult result) {
            if (self) {
              self->OnPolled(generation, result.ready, result.exited);
            }
          },
          weak_factory_.GetWeakPtr(), generation_));
}

void TorManager::OnPolled(int generation, bool ready, bool exited) {
  if (generation != generation_) {
    return;
  }
  poll_in_flight_ = false;
  if (exited) {
    // Tor died. If it had been connected, that is a new problem: give it a
    // full fresh round of attempts.
    if (state_ == TorState::kReady) {
      attempt_ = 0;
    }
    HandleFailure();
    return;
  }
  if (state_ == TorState::kStarting) {
    if (ready) {
      state_ = TorState::kReady;
      attempt_ = 1;  // Connected: a later failure starts over.
      poll_timer_.Start(FROM_HERE, kReadyPollInterval, this,
                        &TorManager::Poll);
    } else if (base::TimeTicks::Now() - started_ > kStartTimeout) {
      HandleFailure();  // Did not connect in time.
    }
  }
}

void TorManager::HandleFailure() {
  // Anything still in flight for the failed attempt is ignored from now on.
  ++generation_;
  launching_ = false;
  poll_in_flight_ = false;
  poll_timer_.Stop();
  TerminateInBackground(std::move(process_));
  process_ = base::Process();
  if (attempt_ < kMaxStartAttempts) {
    // Close, wait a moment, open again. (LaunchTor also kills any tor that is
    // still hanging around from the failed attempt.)
    state_ = TorState::kStarting;
    retry_timer_.Start(FROM_HERE, kRetryDelay, this,
                       &TorManager::BeginLaunch);
  } else {
    state_ = TorState::kFailed;  // The next Start() begins a new round.
  }
}

}  // namespace vpn
}  // namespace sidebar_apps
