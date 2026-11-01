// Copyright 2026. Personal build.
#include "chrome/browser/ui/webui/newpage/newpage_handler.h"

#include <algorithm>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "base/base64.h"
#include "base/containers/span.h"
#include "base/functional/bind.h"
#include "base/json/json_reader.h"
#include "base/location.h"
#include "base/rand_util.h"
#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/single_thread_task_runner.h"
#include "base/time/time.h"
#include "chrome/browser/profiles/profile.h"
#include "content/public/browser/page_navigator.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_ui.h"
#include "net/http/http_request_headers.h"
#include "net/traffic_annotation/network_traffic_annotation.h"
#include "services/network/public/cpp/resource_request.h"
#include "services/network/public/cpp/shared_url_loader_factory.h"
#include "services/network/public/cpp/simple_url_loader.h"
#include "services/network/public/cpp/simple_url_loader_stream_consumer.h"
#include "services/network/public/mojom/fetch_api.mojom.h"
#include "ui/base/page_transition_types.h"
#include "ui/base/window_open_disposition.h"
#include "url/gurl.h"
#include "url/origin.h"
#include "url/url_constants.h"

namespace {

constexpr size_t kMaxBytes = 2 * 1024 * 1024;       // same as server.js
constexpr size_t kMaxSttResponse = 1024 * 1024;
constexpr size_t kMaxImageBytes = 8 * 1024 * 1024;

// Page bodies can be cut mid-character or be non-UTF-8; base::Value strings
// must be valid UTF-8, so replace bad sequences with U+FFFD.
std::string ToValidUtf8(const std::string& in) {
  return base::UTF16ToUTF8(base::UTF8ToUTF16(in));
}

bool StartsWith(const std::string& b, std::string_view prefix) {
  return b.size() >= prefix.size() && b.compare(0, prefix.size(), prefix) == 0;
}

// Image type from the first bytes; empty for anything that is not an image.
std::string SniffImageMime(const std::string& b) {
  if (StartsWith(b, "\xff\xd8\xff"))
    return "image/jpeg";
  if (StartsWith(b, "\x89PNG"))
    return "image/png";
  if (StartsWith(b, "GIF8"))
    return "image/gif";
  if (StartsWith(b, std::string_view("\x00\x00\x01\x00", 4)))
    return "image/x-icon";
  if (StartsWith(b, "BM"))
    return "image/bmp";
  if (b.size() > 12 && StartsWith(b, "RIFF") && b.compare(8, 4, "WEBP") == 0)
    return "image/webp";
  if (b.size() > 12 && b.compare(4, 8, "ftypavif") == 0)
    return "image/avif";
  if (b.substr(0, 512).find("<svg") != std::string::npos)
    return "image/svg+xml";
  return std::string();
}

int ToInt(const base::Value& v) {
  if (v.is_int())
    return v.GetInt();
  if (v.is_double())
    return static_cast<int>(v.GetDouble());
  return 0;
}

// Downloads a URL as a stream so that, when |truncate| is set, we can stop
// at |limit| bytes and still succeed (only a page's <head> is wanted).
// Self-owned: deletes itself after running the callback.
class StreamFetcher : public network::SimpleURLLoaderStreamConsumer {
 public:
  using Callback = base::OnceCallback<void(std::optional<std::string>)>;

  static void Start(scoped_refptr<network::SharedURLLoaderFactory> factory,
                    const GURL& url,
                    size_t limit,
                    bool truncate,
                    Callback callback) {
    (new StreamFetcher(url, limit, truncate, std::move(callback)))
        ->Run(std::move(factory));
  }

 private:
  StreamFetcher(const GURL& url,
                size_t limit,
                bool truncate,
                Callback callback)
      : url_(url),
        limit_(limit),
        truncate_(truncate),
        callback_(std::move(callback)) {}
  ~StreamFetcher() override = default;

  void Run(scoped_refptr<network::SharedURLLoaderFactory> factory) {
    net::NetworkTrafficAnnotationTag annotation =
        net::DefineNetworkTrafficAnnotation("newpage_fetch_text", R"(
        semantics {
          sender: "Custom new tab page"
          description:
            "Downloads RSS/Atom feeds, public-holiday data and article "
            "<head> metadata that the custom new tab page shows."
          trigger: "Opening the custom new tab page."
          data: "Only the URL. No cookies or credentials are sent."
          destination: WEBSITE
        }
        policy {
          cookies_allowed: NO
          setting: "Personal build; no setting."
          policy_exception_justification: "Not implemented."
        })");

    auto request = std::make_unique<network::ResourceRequest>();
    request->url = url_;
    request->credentials_mode = network::mojom::CredentialsMode::kOmit;
    request->headers.SetHeader(net::HttpRequestHeaders::kAcceptLanguage,
                               "en-US,en;q=0.9");
    request->referrer = url::Origin::Create(url_).GetURL();

    loader_ = network::SimpleURLLoader::Create(std::move(request), annotation);
    loader_->SetTimeoutDuration(base::Seconds(10));
    loader_->DownloadAsStream(factory.get(), this);
  }

  // network::SimpleURLLoaderStreamConsumer:
  void OnDataReceived(std::string_view data,
                      base::OnceClosure resume) override {
    if (finishing_)
      return;
    if (body_.size() + data.size() > limit_) {
      finishing_ = true;
      if (truncate_)
        body_.append(data.substr(0, limit_ - body_.size()));
      // Don't destroy the loader from inside its own callback; post instead.
      base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
          FROM_HERE, base::BindOnce(&StreamFetcher::Finish,
                                    weak_factory_.GetWeakPtr(), truncate_));
      return;
    }
    body_.append(data);
    std::move(resume).Run();
  }

  void OnComplete(bool success) override {
    if (finishing_)
      return;
    finishing_ = true;
    Finish(success);
  }

  void OnRetry(base::OnceClosure start_retry) override {}  // not enabled

  void Finish(bool ok) {
    std::move(callback_).Run(
        ok ? std::optional<std::string>(std::move(body_)) : std::nullopt);
    delete this;
  }

  const GURL url_;
  const size_t limit_;
  const bool truncate_;
  Callback callback_;
  std::string body_;
  bool finishing_ = false;
  std::unique_ptr<network::SimpleURLLoader> loader_;
  base::WeakPtrFactory<StreamFetcher> weak_factory_{this};
};

}  // namespace

NewPageHandler::NewPageHandler() = default;
NewPageHandler::~NewPageHandler() = default;

void NewPageHandler::RegisterMessages() {
  web_ui()->RegisterMessageCallback(
      "fetchText", base::BindRepeating(&NewPageHandler::HandleFetchText,
                                       base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "transcribe", base::BindRepeating(&NewPageHandler::HandleTranscribe,
                                        base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "fetchImage", base::BindRepeating(&NewPageHandler::HandleFetchImage,
                                        base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "navigate", base::BindRepeating(&NewPageHandler::HandleNavigate,
                                      base::Unretained(this)));
}

void NewPageHandler::OnJavascriptDisallowed() {
  // Page reloaded/closed: drop pending work so nothing resolves into a dead page.
  weak_factory_.InvalidateWeakPtrs();
  stt_loader_.reset();
}

// args: [callbackId, url, maxBytes]
void NewPageHandler::HandleFetchText(const base::ListValue& args) {
  AllowJavascript();
  if (args.size() < 3 || !args[1].is_string()) {
    if (!args.empty())
      RejectJavascriptCallback(args[0], base::Value());
    return;
  }
  const base::Value& callback_id = args[0];
  GURL url(args[1].GetString());
  if (!url.is_valid() || !url.SchemeIs(url::kHttpsScheme)) {  // https only
    ResolveJavascriptCallback(callback_id, base::Value());
    return;
  }

  const int requested = ToInt(args[2]);
  const bool truncate = requested > 0;
  const size_t limit =
      truncate ? std::min(static_cast<size_t>(requested), kMaxBytes)
               : kMaxBytes;

  StreamFetcher::Start(
      Profile::FromWebUI(web_ui())->GetURLLoaderFactory(), url, limit,
      truncate,
      base::BindOnce(&NewPageHandler::OnFetched, weak_factory_.GetWeakPtr(),
                     callback_id.Clone()));
}

void NewPageHandler::OnFetched(base::Value callback_id,
                               std::optional<std::string> body) {
  if (!body) {
    ResolveJavascriptCallback(callback_id, base::Value());
    return;
  }
  ResolveJavascriptCallback(callback_id, base::Value(ToValidUtf8(*body)));
}

// args: [callbackId, url]  ->  "data:<mime>;base64,..." or null
void NewPageHandler::HandleFetchImage(const base::ListValue& args) {
  AllowJavascript();
  if (args.size() < 2 || !args[1].is_string()) {
    if (!args.empty())
      RejectJavascriptCallback(args[0], base::Value());
    return;
  }
  const base::Value& callback_id = args[0];
  GURL url(args[1].GetString());
  if (!url.is_valid() || !url.SchemeIs(url::kHttpsScheme)) {
    ResolveJavascriptCallback(callback_id, base::Value());
    return;
  }
  StreamFetcher::Start(
      Profile::FromWebUI(web_ui())->GetURLLoaderFactory(), url,
      kMaxImageBytes, /*truncate=*/false,
      base::BindOnce(&NewPageHandler::OnImageFetched,
                     weak_factory_.GetWeakPtr(), callback_id.Clone()));
}

void NewPageHandler::OnImageFetched(base::Value callback_id,
                                    std::optional<std::string> body) {
  if (!body) {
    ResolveJavascriptCallback(callback_id, base::Value());
    return;
  }
  const std::string mime = SniffImageMime(*body);
  if (mime.empty()) {
    ResolveJavascriptCallback(callback_id, base::Value());
    return;
  }
  ResolveJavascriptCallback(
      callback_id,
      base::Value(base::StrCat({"data:", mime, ";base64,",
                                base::Base64Encode(base::as_byte_span(*body))})));
}

// args: [callbackId, sttUrl, audioBase64]
void NewPageHandler::HandleTranscribe(const base::ListValue& args) {
  AllowJavascript();
  if (args.size() < 3 || !args[1].is_string() || !args[2].is_string()) {
    if (!args.empty())
      RejectJavascriptCallback(args[0], base::Value());
    return;
  }
  const base::Value& callback_id = args[0];
  GURL url(args[1].GetString());
  std::optional<std::vector<uint8_t>> audio =
      base::Base64Decode(args[2].GetString());
  if (!url.is_valid() || !url.SchemeIsHTTPOrHTTPS() || !audio ||
      audio->empty()) {
    ResolveJavascriptCallback(callback_id, base::Value());
    return;
  }

  const std::string boundary =
      base::StrCat({"----newpage", base::NumberToString(base::RandUint64())});
  std::string form;
  form += "--" + boundary +
          "\r\nContent-Disposition: form-data; name=\"file\"; "
          "filename=\"speech.webm\"\r\nContent-Type: audio/webm\r\n\r\n";
  form.append(reinterpret_cast<const char*>(audio->data()), audio->size());
  form += "\r\n--" + boundary +
          "\r\nContent-Disposition: form-data; name=\"model\"\r\n\r\n"
          "whisper-1\r\n";
  form += "--" + boundary +
          "\r\nContent-Disposition: form-data; "
          "name=\"response_format\"\r\n\r\njson\r\n";
  form += "--" + boundary + "--\r\n";

  net::NetworkTrafficAnnotationTag annotation =
      net::DefineNetworkTrafficAnnotation("newpage_transcribe", R"(
      semantics {
        sender: "Custom new tab page"
        description:
          "Sends recorded voice-search audio to the transcription server the "
          "user configured (for example a local Whisper server)."
        trigger: "The user presses the microphone button."
        data: "The recorded audio."
        destination: OTHER
      }
      policy {
        cookies_allowed: NO
        setting: "Only used when a server address is set on the page."
        policy_exception_justification: "Not implemented."
      })");

  auto request = std::make_unique<network::ResourceRequest>();
  request->url = url;
  request->method = "POST";
  request->credentials_mode = network::mojom::CredentialsMode::kOmit;

  stt_loader_ = network::SimpleURLLoader::Create(std::move(request), annotation);
  stt_loader_->SetTimeoutDuration(base::Seconds(60));
  stt_loader_->AttachStringForUpload(
      form, base::StrCat({"multipart/form-data; boundary=", boundary}));
  stt_loader_->DownloadToString(
      Profile::FromWebUI(web_ui())->GetURLLoaderFactory().get(),
      base::BindOnce(&NewPageHandler::OnTranscribed,
                     weak_factory_.GetWeakPtr(), callback_id.Clone()),
      kMaxSttResponse);
}

void NewPageHandler::OnTranscribed(base::Value callback_id,
                                   std::optional<std::string> body) {
  stt_loader_.reset();
  if (!body) {
    ResolveJavascriptCallback(callback_id, base::Value());
    return;
  }
  std::optional<base::Value> json =
      base::JSONReader::Read(*body, base::JSON_PARSE_RFC);
  const std::string* text = (json && json->is_dict())
                                ? json->GetDict().FindString("text")
                                : nullptr;
  if (!text) {
    ResolveJavascriptCallback(callback_id, base::Value());
    return;
  }
  ResolveJavascriptCallback(callback_id, base::Value(ToValidUtf8(*text)));
}

// args: [url, newTab]   (fire-and-forget, via chrome.send)
void NewPageHandler::HandleNavigate(const base::ListValue& args) {
  if (args.size() < 2 || !args[0].is_string() || !args[1].is_bool())
    return;
  GURL url(args[0].GetString());
  if (!url.is_valid() || !url.SchemeIsHTTPOrHTTPS())
    return;

  content::OpenURLParams params(
      url, content::Referrer(),
      args[1].GetBool() ? WindowOpenDisposition::NEW_FOREGROUND_TAB
                        : WindowOpenDisposition::CURRENT_TAB,
      ui::PAGE_TRANSITION_LINK, /*is_renderer_initiated=*/false);
  // Older Chromium: OpenURL(params) takes no second argument.
  web_ui()->GetWebContents()->OpenURL(params,
                                      /*navigation_handle_callback=*/{});
}
