// Copyright 2026 MDJ. Use of this source code is governed by a BSD-style
// license that can be found in the Chromium LICENSE file.

#include "chrome/browser/ui/views/sidebar_apps/sidebar_app_model.h"

#include <algorithm>
#include <optional>
#include <utility>

#include "base/files/file_util.h"
#include "base/functional/bind.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/logging.h"
#include "base/task/task_traits.h"
#include "base/task/thread_pool.h"
#include "base/uuid.h"
#include "base/values.h"
#include "chrome/browser/profiles/profile.h"

namespace sidebar_apps {

namespace {

const char kUserDataKey[] = "SidebarAppModel";

constexpr base::FilePath::CharType kFileName[] =
    FILE_PATH_LITERAL("SidebarApps.json");

bool IsValidAppUrl(const GURL& url) {
  return url.is_valid() && url.SchemeIsHTTPOrHTTPS();
}

// Runs on the background sequence (blocking is allowed there).
std::optional<std::string> ReadFileContents(const base::FilePath& path) {
  std::string contents;
  if (!base::ReadFileToString(path, &contents)) {
    return std::nullopt;
  }
  return contents;
}

// Runs on the background sequence (blocking is allowed there).
void WriteFileContents(base::FilePath path, std::string data) {
  if (!base::WriteFile(path, data)) {
    LOG(ERROR) << "Failed to write " << path;
  }
}

}  // namespace

void SidebarSettings::Clamp() {
  width = std::clamp(width, kMinWidth, kMaxWidth);
  icon_size = std::clamp(icon_size, kMinIconSize, kMaxIconSize);
  tab_sleep_minutes = std::clamp(tab_sleep_minutes, 1, kMaxSleepMinutes);
  app_sleep_minutes = std::clamp(app_sleep_minutes, 1, kMaxSleepMinutes);
}

// static
SidebarAppModel* SidebarAppModel::GetForProfile(Profile* profile) {
  auto* model =
      static_cast<SidebarAppModel*>(profile->GetUserData(kUserDataKey));
  if (!model) {
    auto owned = std::make_unique<SidebarAppModel>(profile->GetPath());
    model = owned.get();
    model->Load();
    profile->SetUserData(kUserDataKey, std::move(owned));
  }
  return model;
}

SidebarAppModel::SidebarAppModel(const base::FilePath& profile_dir)
    : file_path_(profile_dir.Append(kFileName)),
      io_task_runner_(base::ThreadPool::CreateSequencedTaskRunner(
          {base::MayBlock(), base::TaskPriority::USER_VISIBLE,
           base::TaskShutdownBehavior::BLOCK_SHUTDOWN})) {}

SidebarAppModel::~SidebarAppModel() = default;

const SidebarApp* SidebarAppModel::FindApp(const std::string& id) const {
  for (const SidebarApp& app : apps_) {
    if (app.id == id) {
      return &app;
    }
  }
  return nullptr;
}

std::string SidebarAppModel::AddApp(const std::string& name,
                                    const GURL& url,
                                    const std::string& custom_icon_data_url,
                                    int sleep_minutes) {
  if (!IsValidAppUrl(url)) {
    return std::string();
  }
  SidebarApp app;
  app.id = base::Uuid::GenerateRandomV4().AsLowercaseString();
  app.name = name.empty() ? url.host() : name;
  app.url = url;
  app.custom_icon_data_url = custom_icon_data_url;
  app.sleep_minutes =
      std::clamp(sleep_minutes, 0, SidebarSettings::kMaxSleepMinutes);
  std::string id = app.id;
  apps_.push_back(std::move(app));
  Save();
  NotifyChanged();
  return id;
}

bool SidebarAppModel::UpdateApp(const SidebarApp& updated) {
  if (!IsValidAppUrl(updated.url)) {
    return false;
  }
  for (SidebarApp& app : apps_) {
    if (app.id == updated.id) {
      app.name = updated.name.empty() ? updated.url.host() : updated.name;
      app.url = updated.url;
      app.custom_icon_data_url = updated.custom_icon_data_url;
      app.sleep_minutes = std::clamp(updated.sleep_minutes, 0,
                                     SidebarSettings::kMaxSleepMinutes);
      Save();
      NotifyChanged();
      return true;
    }
  }
  return false;
}

bool SidebarAppModel::RemoveApp(const std::string& id) {
  for (auto it = apps_.begin(); it != apps_.end(); ++it) {
    if (it->id == id) {
      apps_.erase(it);
      const bool had_unread = unread_counts_.erase(id) > 0;
      Save();
      NotifyChanged();
      if (had_unread) {
        NotifyUnreadChanged();
      }
      return true;
    }
  }
  return false;
}

bool SidebarAppModel::SetAppDisabled(const std::string& id, bool disabled) {
  for (SidebarApp& app : apps_) {
    if (app.id != id) {
      continue;
    }
    if (app.disabled == disabled) {
      return true;
    }
    app.disabled = disabled;
    // A disabled app is not running, so it cannot have unread items.
    const bool had_unread = disabled && unread_counts_.erase(id) > 0;
    Save();
    NotifyChanged();
    if (had_unread) {
      NotifyUnreadChanged();
    }
    return true;
  }
  return false;
}

void SidebarAppModel::SetSettings(SidebarSettings settings) {
  settings.Clamp();
  if (settings == settings_) {
    return;
  }
  settings_ = settings;
  Save();
  NotifySettingsChanged();
}

bool SidebarAppModel::MoveApp(const std::string& id, size_t new_index) {
  for (size_t i = 0; i < apps_.size(); ++i) {
    if (apps_[i].id != id) {
      continue;
    }
    SidebarApp app = std::move(apps_[i]);
    apps_.erase(apps_.begin() + i);
    new_index = std::min(new_index, apps_.size());
    apps_.insert(apps_.begin() + new_index, std::move(app));
    Save();
    NotifyChanged();
    return true;
  }
  return false;
}

void SidebarAppModel::SetUnreadCount(const std::string& app_id, int count) {
  if (count < 0) {
    count = 0;
  }
  if (GetUnreadCount(app_id) == count) {
    return;
  }
  if (count == 0) {
    unread_counts_.erase(app_id);
  } else {
    unread_counts_[app_id] = count;
  }
  NotifyUnreadChanged();
}

int SidebarAppModel::GetUnreadCount(const std::string& app_id) const {
  auto it = unread_counts_.find(app_id);
  return it == unread_counts_.end() ? 0 : it->second;
}

int SidebarAppModel::GetTotalUnreadCount() const {
  int total = 0;
  for (const auto& [id, count] : unread_counts_) {
    total += count;
  }
  return total;
}

void SidebarAppModel::AddObserver(Observer* observer) {
  observers_.AddObserver(observer);
}

void SidebarAppModel::RemoveObserver(Observer* observer) {
  observers_.RemoveObserver(observer);
}

bool SidebarAppModel::TryClaimAppHost(const void* host) {
  const uintptr_t id = reinterpret_cast<uintptr_t>(host);
  if (app_host_ == 0) {
    app_host_ = id;
  }
  return app_host_ == id;
}

void SidebarAppModel::ReleaseAppHost(const void* host) {
  if (app_host_ == reinterpret_cast<uintptr_t>(host)) {
    app_host_ = 0;
  }
}

void SidebarAppModel::Load() {
  io_task_runner_->PostTaskAndReplyWithResult(
      FROM_HERE, base::BindOnce(&ReadFileContents, file_path_),
      base::BindOnce(&SidebarAppModel::OnFileRead,
                     weak_factory_.GetWeakPtr()));
}

void SidebarAppModel::OnFileRead(std::optional<std::string> contents) {
  if (!contents || !apps_.empty()) {
    // No file yet, or the list was already changed since the read started;
    // the in-memory list is newer, so keep it.
    return;
  }
  std::optional<base::DictValue> root =
      base::JSONReader::ReadDict(*contents, base::JSON_PARSE_RFC);
  if (!root) {
    LOG(WARNING) << "SidebarApps.json is not valid JSON; ignoring it.";
    return;
  }
  if (const base::DictValue* saved = root->FindDict("settings")) {
    SidebarSettings loaded;
    if (std::optional<int> width = saved->FindInt("width")) {
      loaded.width = *width;
    }
    if (std::optional<int> icon_size = saved->FindInt("icon_size")) {
      loaded.icon_size = *icon_size;
    }
    if (std::optional<int> position = saved->FindInt("position")) {
      if (*position >= static_cast<int>(SidebarPosition::kLeft) &&
          *position <= static_cast<int>(SidebarPosition::kBottom)) {
        loaded.position = static_cast<SidebarPosition>(*position);
      }
    }
    if (std::optional<bool> autohide = saved->FindBool("autohide")) {
      loaded.autohide = *autohide;
    }
    if (std::optional<bool> badges = saved->FindBool("show_badges")) {
      loaded.show_badges = *badges;
    }
    if (std::optional<bool> sleep_apps = saved->FindBool("sleep_apps")) {
      loaded.sleep_apps_enabled = *sleep_apps;
    }
    if (std::optional<bool> sleep_tabs = saved->FindBool("sleep_tabs")) {
      loaded.sleep_tabs_enabled = *sleep_tabs;
    }
    if (std::optional<int> tab_sleep = saved->FindInt("tab_sleep_minutes")) {
      loaded.tab_sleep_minutes = *tab_sleep;
    }
    if (std::optional<bool> use_global =
            saved->FindBool("use_global_app_sleep")) {
      loaded.use_global_app_sleep = *use_global;
    }
    if (std::optional<int> app_sleep = saved->FindInt("app_sleep_minutes")) {
      loaded.app_sleep_minutes = *app_sleep;
    }
    if (std::optional<bool> ignore_pinned =
            saved->FindBool("ignore_pinned_tabs")) {
      loaded.ignore_pinned_tabs = *ignore_pinned;
    }
    if (std::optional<bool> exceptions = saved->FindBool("sleep_exceptions")) {
      loaded.sleep_exceptions_enabled = *exceptions;
    }
    if (const base::ListValue* sites =
            saved->FindList("sleep_exception_sites")) {
      for (const base::Value& site : *sites) {
        const std::string* site_key = site.GetIfString();
        if (site_key && !site_key->empty()) {
          loaded.sleep_exception_sites.push_back(*site_key);
        }
      }
    }
    loaded.Clamp();
    if (!(loaded == settings_)) {
      settings_ = loaded;
      NotifySettingsChanged();
    }
  }
  const base::ListValue* list = root->FindList("apps");
  if (!list) {
    return;
  }
  for (const base::Value& item : *list) {
    const base::DictValue* dict = item.GetIfDict();
    if (!dict) {
      continue;
    }
    const std::string* id = dict->FindString("id");
    const std::string* name = dict->FindString("name");
    const std::string* url_string = dict->FindString("url");
    if (!id || !name || !url_string) {
      continue;
    }
    GURL url(*url_string);
    if (!IsValidAppUrl(url)) {
      continue;
    }
    SidebarApp app;
    app.id = *id;
    app.name = *name;
    app.url = url;
    if (const std::string* icon = dict->FindString("icon")) {
      app.custom_icon_data_url = *icon;
    }
    if (std::optional<bool> disabled = dict->FindBool("disabled")) {
      app.disabled = *disabled;
    }
    if (std::optional<int> sleep = dict->FindInt("sleep_minutes")) {
      app.sleep_minutes =
          std::clamp(*sleep, 0, SidebarSettings::kMaxSleepMinutes);
    }
    apps_.push_back(std::move(app));
  }
  NotifyChanged();
}

void SidebarAppModel::Save() {
  base::ListValue list;
  for (const SidebarApp& app : apps_) {
    base::DictValue dict;
    dict.Set("id", app.id);
    dict.Set("name", app.name);
    dict.Set("url", app.url.spec());
    if (!app.custom_icon_data_url.empty()) {
      dict.Set("icon", app.custom_icon_data_url);
    }
    if (app.disabled) {
      dict.Set("disabled", true);
    }
    if (app.sleep_minutes > 0) {
      dict.Set("sleep_minutes", app.sleep_minutes);
    }
    list.Append(std::move(dict));
  }
  base::DictValue root;
  root.Set("version", 1);
  root.Set("apps", std::move(list));
  base::DictValue settings;
  settings.Set("position", static_cast<int>(settings_.position));
  settings.Set("width", settings_.width);
  settings.Set("icon_size", settings_.icon_size);
  settings.Set("autohide", settings_.autohide);
  settings.Set("show_badges", settings_.show_badges);
  settings.Set("sleep_apps", settings_.sleep_apps_enabled);
  settings.Set("sleep_tabs", settings_.sleep_tabs_enabled);
  settings.Set("tab_sleep_minutes", settings_.tab_sleep_minutes);
  settings.Set("use_global_app_sleep", settings_.use_global_app_sleep);
  settings.Set("app_sleep_minutes", settings_.app_sleep_minutes);
  settings.Set("ignore_pinned_tabs", settings_.ignore_pinned_tabs);
  settings.Set("sleep_exceptions", settings_.sleep_exceptions_enabled);
  base::ListValue exception_sites;
  for (const std::string& site : settings_.sleep_exception_sites) {
    exception_sites.Append(site);
  }
  settings.Set("sleep_exception_sites", std::move(exception_sites));
  root.Set("settings", std::move(settings));

  std::string json;
  if (!base::JSONWriter::WriteWithOptions(
          root, base::JSONWriter::OPTIONS_PRETTY_PRINT, &json)) {
    LOG(ERROR) << "Failed to serialize sidebar apps.";
    return;
  }
  io_task_runner_->PostTask(
      FROM_HERE,
      base::BindOnce(&WriteFileContents, file_path_, std::move(json)));
}

void SidebarAppModel::NotifyChanged() {
  for (Observer& observer : observers_) {
    observer.OnSidebarAppsChanged();
  }
}

void SidebarAppModel::NotifySettingsChanged() {
  for (Observer& observer : observers_) {
    observer.OnSidebarSettingsChanged();
  }
}

void SidebarAppModel::NotifyUnreadChanged() {
  for (Observer& observer : observers_) {
    observer.OnUnreadCountsChanged();
  }
}

}  // namespace sidebar_apps
