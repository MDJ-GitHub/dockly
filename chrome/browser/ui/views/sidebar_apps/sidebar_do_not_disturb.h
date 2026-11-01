// Copyright 2026 MDJ. Use of this source code is governed by a BSD-style
// license that can be found in the Chromium LICENSE file.

#ifndef CHROME_BROWSER_UI_VIEWS_SIDEBAR_APPS_SIDEBAR_DO_NOT_DISTURB_H_
#define CHROME_BROWSER_UI_VIEWS_SIDEBAR_APPS_SIDEBAR_DO_NOT_DISTURB_H_

#include <array>
#include <string>

#include "base/callback_list.h"
#include "base/functional/callback.h"
#include "base/time/time.h"

namespace sidebar_apps {

// Do Not Disturb: for a while, web notifications are not shown. One switch for
// the whole browser process; it is not saved, so it never outlasts a restart.
//
// The notifications are held back by PlatformNotificationServiceImpl, which
// asks IsDoNotDisturbActive() before showing one (see the two lines added to
// DisplayNotification() and DisplayPersistentNotification()).

struct DoNotDisturbDuration {
  const char16_t* label;
  int minutes;
};

// The choices of the menus, shortest first.
// 0 minutes means "until I turn it off".
inline constexpr std::array<DoNotDisturbDuration, 7> kDoNotDisturbDurations = {
    DoNotDisturbDuration{u"5 minutes", 5},
    DoNotDisturbDuration{u"10 minutes", 10},
    DoNotDisturbDuration{u"30 minutes", 30},
    DoNotDisturbDuration{u"1 hour", 60},
    DoNotDisturbDuration{u"2 hours", 120},
    DoNotDisturbDuration{u"3 hours", 180},
    DoNotDisturbDuration{u"Until I turn it off", 0}};

bool IsDoNotDisturbActive();
base::TimeDelta DoNotDisturbTimeLeft();

// Turns it on for |duration| (or changes the time left if it is already on).
void StartDoNotDisturb(base::TimeDelta duration);
// Turns it on until it is turned off again.
void StartDoNotDisturbIndefinitely();
// |minutes| of a kDoNotDisturbDurations entry: 0 means indefinitely.
void StartDoNotDisturbForMinutes(int minutes);
bool IsDoNotDisturbIndefinite();
// Turns it off now.
void StopDoNotDisturb();

// "Do Not Disturb until 14:35" ("... until you turn it off"), or an empty
// string when it is off.
std::u16string DoNotDisturbStatusText();

// |callback| runs whenever it is turned on or off (also when the time runs
// out). Keep the subscription for as long as the callback should run.
base::CallbackListSubscription AddDoNotDisturbObserver(
    base::RepeatingClosure callback);

}  // namespace sidebar_apps

#endif  // CHROME_BROWSER_UI_VIEWS_SIDEBAR_APPS_SIDEBAR_DO_NOT_DISTURB_H_
