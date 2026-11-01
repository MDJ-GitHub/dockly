// Copyright 2026 The Dockium Authors.
// chrome/browser/ui/webui/dockium_welcome/dockium_welcome_launch.cc

#include "chrome/browser/ui/webui/dockium_welcome/dockium_welcome_launch.h"

#include "base/command_line.h"
#include "components/prefs/pref_registry_simple.h"
#include "components/prefs/pref_service.h"

namespace dockium_welcome {

namespace {
constexpr char kWelcomeShownPref[] = "dockium.welcome_shown";
}  // namespace

void RegisterLocalStatePrefs(PrefRegistrySimple* registry) {
  registry->RegisterBooleanPref(kWelcomeShownPref, false);
}

bool ShouldShowWelcomeOnLaunch(PrefService* local_state,
                               const base::CommandLine& command_line) {
  if (command_line.HasSwitch(kForceWelcomeSwitch)) {
    return true;
  }
  if (!local_state || command_line.HasSwitch(kStartInTraySwitch)) {
    return false;
  }
  if (local_state->GetBoolean(kWelcomeShownPref)) {
    return false;
  }
  local_state->SetBoolean(kWelcomeShownPref, true);
  return true;
}

}  // namespace dockium_welcome
