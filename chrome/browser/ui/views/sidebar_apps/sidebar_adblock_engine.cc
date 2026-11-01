// Copyright 2026 MDJ. Use of this source code is governed by a BSD-style
// license that can be found in the Chromium LICENSE file.

#include "chrome/browser/ui/views/sidebar_apps/sidebar_adblock_engine.h"

#include <algorithm>
#include <string>
#include <utility>

#include "base/files/file_enumerator.h"
#include "base/files/file_util.h"
#include "base/functional/bind.h"
#include "base/no_destructor.h"
#include "base/path_service.h"
#include "base/task/thread_pool.h"
#include "base/threading/scoped_blocking_call.h"

namespace sidebar_apps {
namespace adblock {

namespace {

const char* RequestTypeName(RequestKind kind) {
  switch (kind) {
    case RequestKind::kDocument:
      return "document";
    case RequestKind::kSubframe:
      return "subdocument";
    case RequestKind::kScript:
      return "script";
    case RequestKind::kStylesheet:
      return "stylesheet";
    case RequestKind::kImage:
      return "image";
    case RequestKind::kFont:
      return "font";
    case RequestKind::kMedia:
      return "media";
    case RequestKind::kXhr:
      return "xmlhttprequest";
    case RequestKind::kOther:
      break;
  }
  return "other";
}

}  // namespace

Engine::Engine() = default;
Engine::~Engine() = default;

// static
Engine& Engine::Get() {
  static base::NoDestructor<Engine> instance;
  return *instance;
}

void Engine::EnsureLoaded() {
  if (state_ != EngineState::kNotLoaded) {
    return;
  }
  state_ = EngineState::kLoading;
  // Thread pool: reads the lists and builds the engine. Slow (a second or two
  // for EasyList + EasyPrivacy), so never on the UI thread.
  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE, {base::MayBlock(), base::TaskPriority::USER_VISIBLE},
      base::BindOnce([]() {
        base::ScopedBlockingCall blocking(FROM_HERE,
                                          base::BlockingType::MAY_BLOCK);
        Built built;
        base::FilePath exe_dir;
        base::PathService::Get(base::DIR_EXE, &exe_dir);
        const base::FilePath root = exe_dir.AppendASCII("adblock");

        ::rust::Vec<::rust::String> lists;
        base::FileEnumerator files(root.AppendASCII("lists"),
                                   /*recursive=*/false,
                                   base::FileEnumerator::FILES,
                                   FILE_PATH_LITERAL("*.txt"));
        for (base::FilePath path = files.Next(); !path.empty();
             path = files.Next()) {
          std::string text;
          if (!base::ReadFileToString(path, &text)) {
            continue;
          }
          built.rule_count += static_cast<int>(std::count_if(
              text.begin(), text.end(), [](char c) { return c == '\n'; }));
          lists.push_back(::rust::String::lossy(text));
        }
        if (lists.empty()) {
          return built;  // engine stays empty: "lists not found".
        }
        std::string resources;
        base::ReadFileToString(root.AppendASCII("resources.json"), &resources);
        built.engine = rs::engine_new(std::move(lists),
                                      ::rust::String::lossy(resources));
        return built;
      }),
      base::BindOnce(&Engine::OnBuilt, weak_factory_.GetWeakPtr()));
}

void Engine::OnBuilt(Built built) {
  if (!built.engine) {
    state_ = EngineState::kMissing;
    return;
  }
  engine_ = std::move(built.engine);
  rule_count_ = built.rule_count;
  state_ = EngineState::kReady;
}

rs::BlockResult Engine::Check(const GURL& url,
                              const GURL& source_url,
                              RequestKind kind) {
  if (state_ != EngineState::kReady) {
    return rs::BlockResult{};
  }
  return rs::engine_check(**engine_, url.spec(),
                          source_url.is_valid() ? source_url.spec()
                                                : url.spec(),
                          RequestTypeName(kind));
}

rs::CosmeticResult Engine::Cosmetic(const GURL& url) {
  if (state_ != EngineState::kReady) {
    return rs::CosmeticResult{};
  }
  return rs::engine_cosmetic(**engine_, url.spec());
}

std::vector<std::string> Engine::HiddenSelectors(
    const GURL& url,
    std::vector<std::string> classes,
    std::vector<std::string> ids) {
  std::vector<std::string> result;
  if (state_ != EngineState::kReady) {
    return result;
  }
  ::rust::Vec<::rust::String> rust_classes;
  for (const std::string& c : classes) {
    rust_classes.push_back(::rust::String::lossy(c));
  }
  ::rust::Vec<::rust::String> rust_ids;
  for (const std::string& i : ids) {
    rust_ids.push_back(::rust::String::lossy(i));
  }
  for (const ::rust::String& s : rs::engine_hidden_selectors(
           **engine_, url.spec(), std::move(rust_classes),
           std::move(rust_ids))) {
    result.emplace_back(std::string(s));
  }
  return result;
}

}  // namespace adblock
}  // namespace sidebar_apps
