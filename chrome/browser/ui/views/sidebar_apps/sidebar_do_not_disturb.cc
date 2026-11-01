// Copyright 2026 MDJ. Use of this source code is governed by a BSD-style
// license that can be found in the Chromium LICENSE file.

#include "chrome/browser/ui/views/sidebar_apps/sidebar_do_not_disturb.h"

#include <utility>

#include "base/functional/bind.h"
#include "base/i18n/time_formatting.h"
#include "base/no_destructor.h"
#include "base/timer/timer.h"

namespace sidebar_apps {

namespace {

// Null while Do Not Disturb is off.
base::TimeTicks& EndTime() {
  // Trivially destructible, so no NoDestructor is needed.
  static base::TimeTicks end_time;
  return end_time;
}

base::OneShotTimer& EndTimer() {
  static base::NoDestructor<base::OneShotTimer> timer;
  return *timer;
}

base::RepeatingClosureList& Observers() {
  static base::NoDestructor<base::RepeatingClosureList> observers;
  return *observers;
}

}  // namespace

bool IsDoNotDisturbActive() {
  return !EndTime().is_null() && base::TimeTicks::Now() < EndTime();
}

bool IsDoNotDisturbIndefinite() {
  return EndTime().is_max();
}

base::TimeDelta DoNotDisturbTimeLeft() {
  if (IsDoNotDisturbIndefinite()) {
    return base::TimeDelta::Max();
  }
  return IsDoNotDisturbActive() ? EndTime() - base::TimeTicks::Now()
                                : base::TimeDelta();
}

void StopDoNotDisturb() {
  EndTimer().Stop();
  const bool was_active = !EndTime().is_null();
  EndTime() = base::TimeTicks();
  if (was_active) {
    Observers().Notify();
  }
}

void StartDoNotDisturb(base::TimeDelta duration) {
  if (duration <= base::TimeDelta()) {
    StopDoNotDisturb();
    return;
  }
  EndTime() = base::TimeTicks::Now() + duration;
  EndTimer().Start(FROM_HERE, duration, base::BindOnce(&StopDoNotDisturb));
  Observers().Notify();
}

void StartDoNotDisturbIndefinitely() {
  EndTimer().Stop();
  EndTime() = base::TimeTicks::Max();
  Observers().Notify();
}

void StartDoNotDisturbForMinutes(int minutes) {
  if (minutes <= 0) {
    StartDoNotDisturbIndefinitely();
  } else {
    StartDoNotDisturb(base::Minutes(minutes));
  }
}

std::u16string DoNotDisturbStatusText() {
  if (!IsDoNotDisturbActive()) {
    return std::u16string();
  }
  if (IsDoNotDisturbIndefinite()) {
    return u"Do Not Disturb until you turn it off";
  }
  return u"Do Not Disturb until " +
         base::TimeFormatTimeOfDay(base::Time::Now() + DoNotDisturbTimeLeft());
}

base::CallbackListSubscription AddDoNotDisturbObserver(
    base::RepeatingClosure callback) {
  return Observers().Add(std::move(callback));
}

}  // namespace sidebar_apps
