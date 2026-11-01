// Copyright 2026 MDJ. Use of this source code is governed by a BSD-style
// license that can be found in the Chromium LICENSE file.

#include "chrome/browser/ui/views/sidebar_apps/sidebar_adblock_hooks.h"

#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "base/functional/bind.h"
#include "base/json/json_reader.h"
#include "base/json/string_escape.h"
#include "base/location.h"
#include "base/task/single_thread_task_runner.h"
#include "base/strings/utf_string_conversions.h"
#include "base/values.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/common/chrome_isolated_world_ids.h"
#include "chrome/browser/ui/views/sidebar_apps/sidebar_adblock.h"
#include "chrome/browser/ui/views/sidebar_apps/sidebar_adblock_engine.h"
#include "content/public/browser/browser_context.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/navigation_handle.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_contents_delegate.h"
#include "mojo/public/cpp/bindings/pending_receiver.h"
#include "mojo/public/cpp/bindings/pending_remote.h"
#include "mojo/public/cpp/bindings/receiver_set.h"
#include "mojo/public/cpp/bindings/remote.h"
#include "net/base/isolation_info.h"
#include "net/base/net_errors.h"
#include "net/traffic_annotation/network_traffic_annotation.h"
#include "services/network/public/cpp/resource_request.h"
#include "services/network/public/cpp/url_loader_completion_status.h"
#include "services/network/public/cpp/url_loader_factory_builder.h"
#include "services/network/public/mojom/fetch_api.mojom.h"
#include "services/network/public/mojom/url_loader.mojom.h"
#include "url/origin.h"

namespace sidebar_apps {
namespace adblock {

namespace {

constexpr size_t kMaxSelectors = 20000;

RequestKind KindOf(const network::ResourceRequest& request) {
  using Dest = network::mojom::RequestDestination;
  switch (request.destination) {
    case Dest::kDocument:
      return RequestKind::kDocument;
    case Dest::kIframe:
    case Dest::kFrame:
    case Dest::kFencedframe:
      return RequestKind::kSubframe;
    case Dest::kScript:
    case Dest::kWorker:
    case Dest::kSharedWorker:
    case Dest::kServiceWorker:
      return RequestKind::kScript;
    case Dest::kStyle:
      return RequestKind::kStylesheet;
    case Dest::kImage:
      return RequestKind::kImage;
    case Dest::kFont:
      return RequestKind::kFont;
    case Dest::kAudio:
    case Dest::kVideo:
    case Dest::kTrack:
      return RequestKind::kMedia;
    case Dest::kEmpty:
      return RequestKind::kXhr;  // fetch() / XHR / beacons.
    default:
      return RequestKind::kOther;
  }
}

// Sits in front of the real URLLoaderFactory of one frame / worker / navigation
// and answers blocked requests itself (net::ERR_BLOCKED_BY_CLIENT, which is the
// same error an extension blocker produces). Lives on the UI thread and
// deletes itself when its pipes close.
class FilteringFactory : public network::mojom::URLLoaderFactory {
 public:
  FilteringFactory(
      base::WeakPtr<Profile> profile,
      url::Origin top_origin,
      mojo::PendingReceiver<network::mojom::URLLoaderFactory> receiver,
      mojo::PendingRemote<network::mojom::URLLoaderFactory> target)
      : profile_(std::move(profile)), top_origin_(std::move(top_origin)) {
    target_.Bind(std::move(target));
    target_.set_disconnect_handler(
        base::BindOnce(&FilteringFactory::Destroy, base::Unretained(this)));
    receivers_.Add(this, std::move(receiver));
    receivers_.set_disconnect_handler(base::BindRepeating(
        &FilteringFactory::OnReceiverGone, base::Unretained(this)));
  }

  void CreateLoaderAndStart(
      mojo::PendingReceiver<network::mojom::URLLoader> loader,
      int32_t request_id,
      uint32_t options,
      const network::ResourceRequest& request,
      mojo::PendingRemote<network::mojom::URLLoaderClient> client,
      const net::MutableNetworkTrafficAnnotationTag& traffic_annotation)
      override {
    if (ShouldBlock(request)) {
      mojo::Remote<network::mojom::URLLoaderClient> blocked_client(
          std::move(client));
      blocked_client->OnComplete(
          network::URLLoaderCompletionStatus(net::ERR_BLOCKED_BY_CLIENT));
      return;
    }
    target_->CreateLoaderAndStart(std::move(loader), request_id, options,
                                  request, std::move(client),
                                  traffic_annotation);
  }

  void Clone(
      mojo::PendingReceiver<network::mojom::URLLoaderFactory> receiver)
      override {
    receivers_.Add(this, std::move(receiver));
  }

 private:
  ~FilteringFactory() override = default;

  bool ShouldBlock(const network::ResourceRequest& request) {
    Profile* profile = profile_.get();
    if (!profile || !request.url.SchemeIsHTTPOrHTTPS()) {
      return false;
    }
    const RequestKind kind = KindOf(request);
    GURL page = top_origin_.GetURL();
    GURL source = page;
    if (kind == RequestKind::kDocument) {
      // Only pages / scripts that open a page (pop-ups, "click under" tabs).
      // Typing an address, a bookmark or a link to the same site is never
      // touched.
      if (!request.is_outermost_main_frame || !request.request_initiator) {
        return false;
      }
      page = source = request.request_initiator->GetURL();
      if (SiteKeyForUrl(page) == SiteKeyForUrl(request.url)) {
        return false;
      }
    }
    return ShouldBlockRequest(profile, request.url, source, page, kind);
  }

  void OnReceiverGone() {
    if (receivers_.empty()) {
      Destroy();
    }
  }
  void Destroy() { delete this; }

  base::WeakPtr<Profile> profile_;
  const url::Origin top_origin_;
  mojo::Remote<network::mojom::URLLoaderFactory> target_;
  mojo::ReceiverSet<network::mojom::URLLoaderFactory> receivers_;
};

// ---- Cosmetic filtering ---------------------------------------------------

void InjectHidingCss(content::RenderFrameHost* rfh,
                     const std::vector<std::string>& selectors) {
  if (!rfh || !rfh->IsRenderFrameLive() || selectors.empty()) {
    return;
  }
  // One rule per selector: in a comma list a single bad selector would drop
  // the whole rule.
  std::string css;
  size_t count = 0;
  for (const std::string& selector : selectors) {
    if (++count > kMaxSelectors) {
      break;
    }
    css += selector + "{display:none!important}\n";
  }
  // A constructed stylesheet is not subject to the page's CSP (an inline
  // <style> can be); the <style> path is only the fallback.
  const std::string js =
      "(function(){var c=" + base::GetQuotedJSONString(css) +
      ";try{var s=new CSSStyleSheet();s.replaceSync(c);"
      "document.adoptedStyleSheets=Array.from(document.adoptedStyleSheets)"
      ".concat([s]);}catch(e){var t=document.createElement('style');"
      "t.textContent=c;(document.head||document.documentElement)"
      ".appendChild(t);}})();";
  rfh->ExecuteJavaScriptInIsolatedWorld(base::UTF8ToUTF16(js), {},
                                        ISOLATED_WORLD_ID_SIDEBAR_ADBLOCK);
}

constexpr char kCollectClassesAndIdsJs[] =
    "(function(){var cl=new Set(),id=new Set();"
    "var all=document.querySelectorAll('[class],[id]');"
    "for(var i=0;i<all.length&&i<20000;i++){var e=all[i];"
    "if(e.id)id.add(e.id);var l=e.classList;"
    "for(var j=0;j<l.length;j++)cl.add(l[j]);}"
    "return JSON.stringify({c:Array.from(cl),i:Array.from(id)});})()";

std::vector<std::string> StringsFrom(const base::ListValue* list) {
  std::vector<std::string> out;
  if (list) {
    for (const base::Value& v : *list) {
      if (v.is_string()) {
        out.push_back(v.GetString());
      }
    }
  }
  return out;
}

}  // namespace

void MaybeProxyURLLoaderFactory(
    content::BrowserContext* browser_context,
    content::ContentBrowserClient::URLLoaderFactoryType type,
    const url::Origin& request_initiator,
    const net::IsolationInfo& isolation_info,
    network::URLLoaderFactoryBuilder& factory_builder) {
  using Type = content::ContentBrowserClient::URLLoaderFactoryType;
  if (type != Type::kDocumentSubResource && type != Type::kNavigation &&
      type != Type::kWorkerSubResource) {
    return;
  }
  Profile* profile = Profile::FromBrowserContext(browser_context);
  if (!profile) {
    return;
  }

  // UNVERIFIED: the exact builder call. The builder must hand back (a) the
  // receiver our proxy serves and (b) the remote of the factory it forwards
  // to. Check services/network/public/cpp/url_loader_factory_builder.h (and
  // how WebRequestAPI uses `factory_builder`) and adjust this one line if the
  // method name or return type differs.
  auto [proxied_receiver, target] = factory_builder.Append();

  url::Origin top_origin =
      isolation_info.top_frame_origin().value_or(request_initiator);
  new FilteringFactory(profile->GetWeakPtr(), std::move(top_origin),
                       std::move(proxied_receiver), std::move(target));
}

WEB_CONTENTS_USER_DATA_KEY_IMPL(TabHelper);

TabHelper::TabHelper(content::WebContents* web_contents)
    : content::WebContentsObserver(web_contents),
      content::WebContentsUserData<TabHelper>(*web_contents) {}

TabHelper::~TabHelper() = default;

void TabHelper::DidFinishNavigation(content::NavigationHandle* handle) {
  CloseIfAdPopup(handle);
  if (!handle->HasCommitted() || handle->IsSameDocument() ||
      handle->IsErrorPage()) {
    return;
  }
  Profile* profile =
      Profile::FromBrowserContext(web_contents()->GetBrowserContext());
  const GURL page =
      web_contents()->GetPrimaryMainFrame()->GetLastCommittedURL();
  if (!IsBlockingOnPage(profile, page)) {
    return;
  }
  Engine::Get().EnsureLoaded();
  const rs::CosmeticResult cosmetic = Engine::Get().Cosmetic(handle->GetURL());
  std::vector<std::string> selectors;
  for (const ::rust::String& s : cosmetic.hide_selectors) {
    selectors.emplace_back(std::string(s));
  }
  InjectHidingCss(handle->GetRenderFrameHost(), selectors);
}

void TabHelper::DOMContentLoaded(content::RenderFrameHost* rfh) {
  Profile* profile =
      Profile::FromBrowserContext(web_contents()->GetBrowserContext());
  if (!IsBlockingOnPage(
          profile,
          web_contents()->GetPrimaryMainFrame()->GetLastCommittedURL())) {
    return;
  }
  const content::GlobalRenderFrameHostId id = rfh->GetGlobalId();
  rfh->ExecuteJavaScriptInIsolatedWorld(
      base::UTF8ToUTF16(std::string(kCollectClassesAndIdsJs)),
      base::BindOnce(
          [](content::GlobalRenderFrameHostId id, base::Value result) {
            content::RenderFrameHost* rfh =
                content::RenderFrameHost::FromID(id);
            if (!rfh || !result.is_string()) {
              return;
            }
            std::optional<base::DictValue> parsed =
                base::JSONReader::ReadDict(result.GetString(),
                                           base::JSON_PARSE_RFC);
            if (!parsed) {
              return;
            }
            std::vector<std::string> selectors =
                Engine::Get().HiddenSelectors(
                    rfh->GetLastCommittedURL(),
                    StringsFrom(parsed->FindList("c")),
                    StringsFrom(parsed->FindList("i")));
            InjectHidingCss(rfh, selectors);
          },
          id),
      ISOLATED_WORLD_ID_SIDEBAR_ADBLOCK);
}

// A tab that an ad opened ends up on an error page (the proxy refused the
// navigation). Close it instead of leaving an empty tab behind.
void TabHelper::CloseIfAdPopup(content::NavigationHandle* handle) {
  if (handle->GetNetErrorCode() != net::ERR_BLOCKED_BY_CLIENT ||
      !handle->IsInPrimaryMainFrame() || !web_contents()->HasOpener() ||
      web_contents()->GetController().GetEntryCount() > 1 ||
      !handle->GetInitiatorOrigin()) {
    return;
  }
  Profile* profile =
      Profile::FromBrowserContext(web_contents()->GetBrowserContext());
  const GURL opener = handle->GetInitiatorOrigin()->GetURL();
  if (!IsBlockingOnPage(profile, opener) ||
      !Engine::Get()
           .Check(handle->GetURL(), opener, RequestKind::kDocument)
           .matched) {
    return;  // Blocked by something else (policy, an extension): leave it.
  }
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, base::BindOnce(
                     [](base::WeakPtr<TabHelper> self) {
                       if (self && self->web_contents() &&
                           self->web_contents()->GetDelegate()) {
                         self->web_contents()->GetDelegate()->CloseContents(
                             self->web_contents());
                       }
                     },
                     weak_factory_.GetWeakPtr()));
}

}  // namespace adblock
}  // namespace sidebar_apps
