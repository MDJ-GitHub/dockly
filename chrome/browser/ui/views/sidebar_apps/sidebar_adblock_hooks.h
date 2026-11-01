// Copyright 2026 MDJ. Use of this source code is governed by a BSD-style
// license that can be found in the Chromium LICENSE file.

#ifndef CHROME_BROWSER_UI_VIEWS_SIDEBAR_APPS_SIDEBAR_ADBLOCK_HOOKS_H_
#define CHROME_BROWSER_UI_VIEWS_SIDEBAR_APPS_SIDEBAR_ADBLOCK_HOOKS_H_

#include "base/memory/weak_ptr.h"
#include "content/public/browser/content_browser_client.h"
#include "content/public/browser/web_contents_observer.h"
#include "content/public/browser/web_contents_user_data.h"
#include "services/network/public/cpp/url_loader_factory_builder.h"

namespace content {
class BrowserContext;
}
namespace net {
class IsolationInfo;
}
namespace url {
class Origin;
}

namespace sidebar_apps {
namespace adblock {

// 1) Network blocking. Call from
// ChromeContentBrowserClient::WillCreateURLLoaderFactory(), passing that
// function's `factory_builder` parameter. Puts a filtering proxy in front of
// the factory, so every request of a document, worker or navigation is
// checked against the filter lists.
void MaybeProxyURLLoaderFactory(
    content::BrowserContext* browser_context,
    content::ContentBrowserClient::URLLoaderFactoryType type,
    const url::Origin& request_initiator,
    const net::IsolationInfo& isolation_info,
    network::URLLoaderFactoryBuilder& factory_builder);

// 2) Per-tab work: hides ad elements (cosmetic filters) and closes tabs that
// an ad opened. Attach from TabHelpers::AttachTabHelpers().
class TabHelper : public content::WebContentsObserver,
                  public content::WebContentsUserData<TabHelper> {
 public:
  ~TabHelper() override;

  // content::WebContentsObserver:
  void DidFinishNavigation(content::NavigationHandle* handle) override;
  void DOMContentLoaded(content::RenderFrameHost* render_frame_host) override;

 private:
  friend class content::WebContentsUserData<TabHelper>;
  explicit TabHelper(content::WebContents* web_contents);

  void CloseIfAdPopup(content::NavigationHandle* handle);

  base::WeakPtrFactory<TabHelper> weak_factory_{this};
  WEB_CONTENTS_USER_DATA_KEY_DECL();
};

}  // namespace adblock
}  // namespace sidebar_apps

#endif  // CHROME_BROWSER_UI_VIEWS_SIDEBAR_APPS_SIDEBAR_ADBLOCK_HOOKS_H_
