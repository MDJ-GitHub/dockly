// Copyright 2026 The Dockium Authors.
// chrome/browser/ui/webui/dockium_welcome/dockium_welcome_launch.h

#ifndef CHROME_BROWSER_UI_WEBUI_DOCKIUM_WELCOME_DOCKIUM_WELCOME_LAUNCH_H_
#define CHROME_BROWSER_UI_WEBUI_DOCKIUM_WELCOME_DOCKIUM_WELCOME_LAUNCH_H_

class PrefRegistrySimple;
class PrefService;

namespace base {
class CommandLine;
}

namespace dockium_welcome {

// Testing switch: always show the welcome page on startup, without
// remembering that it was shown.
inline constexpr char kForceWelcomeSwitch[] = "force-dockium-welcome";

// Registers the "already shown" flag. Call from RegisterLocalStatePrefs().
void RegisterLocalStatePrefs(PrefRegistrySimple* registry);

// The switch the "Start with Windows" entry passes (see sidebar_autostart.cc).
inline constexpr char kStartInTraySwitch[] = "start-in-tray";

// Returns true when the welcome page should open with this launch, and
// remembers that it was shown. Returns false (and remembers nothing) when the
// browser was started with --start-in-tray, so a silent Windows autostart
// does not use up the welcome page.
bool ShouldShowWelcomeOnLaunch(PrefService* local_state,
                               const base::CommandLine& command_line);

}  // namespace dockium_welcome

#endif  // CHROME_BROWSER_UI_WEBUI_DOCKIUM_WELCOME_DOCKIUM_WELCOME_LAUNCH_H_
