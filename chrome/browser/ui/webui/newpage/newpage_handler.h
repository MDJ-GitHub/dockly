// Copyright 2026. Personal build.
#ifndef CHROME_BROWSER_UI_WEBUI_NEWPAGE_NEWPAGE_HANDLER_H_
#define CHROME_BROWSER_UI_WEBUI_NEWPAGE_NEWPAGE_HANDLER_H_

#include <memory>
#include <optional>
#include <string>

#include "base/memory/weak_ptr.h"
#include "base/values.h"
#include "content/public/browser/web_ui_message_handler.h"

namespace network {
class SimpleURLLoader;
}

// Browser-side half of bridge.js:
//   fetchText(url, maxBytes)   -> string | null
//   transcribe(url, base64)    -> string | null
//   navigate(url, newTab)
//   fetchImage(url)            -> data: URL string | null
class NewPageHandler : public content::WebUIMessageHandler {
 public:
  NewPageHandler();
  NewPageHandler(const NewPageHandler&) = delete;
  NewPageHandler& operator=(const NewPageHandler&) = delete;
  ~NewPageHandler() override;

  // content::WebUIMessageHandler:
  void RegisterMessages() override;
  void OnJavascriptDisallowed() override;

 private:
  void HandleFetchText(const base::ListValue& args);
  void HandleTranscribe(const base::ListValue& args);
  void HandleNavigate(const base::ListValue& args);
  void HandleFetchImage(const base::ListValue& args);

  void OnFetched(base::Value callback_id, std::optional<std::string> body);
  void OnImageFetched(base::Value callback_id, std::optional<std::string> body);
  void OnTranscribed(base::Value callback_id,
                     std::optional<std::string> body);

  std::unique_ptr<network::SimpleURLLoader> stt_loader_;
  base::WeakPtrFactory<NewPageHandler> weak_factory_{this};
};

#endif  // CHROME_BROWSER_UI_WEBUI_NEWPAGE_NEWPAGE_HANDLER_H_
