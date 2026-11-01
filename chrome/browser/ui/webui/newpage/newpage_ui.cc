// Copyright 2026. Personal build.
#include "chrome/browser/ui/webui/newpage/newpage_ui.h"

#include <memory>

#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/webui/newpage/newpage_constants.h"
#include "chrome/browser/ui/webui/newpage/newpage_handler.h"
#include "chrome/grit/browser_resources.h"
#include "content/public/browser/web_ui.h"
#include "content/public/browser/web_ui_data_source.h"
#include "services/network/public/mojom/content_security_policy.mojom.h"

NewPageUI::NewPageUI(content::WebUI* web_ui)
    : content::WebUIController(web_ui) {
  Profile* profile = Profile::FromWebUI(web_ui);

  content::WebUIDataSource* source =
      content::WebUIDataSource::CreateAndAdd(profile, newpage::kHost);

  source->AddResourcePath("index.html", IDR_NEWPAGE_INDEX_HTML);
  source->AddResourcePath("newpage.js", IDR_NEWPAGE_JS);
  source->AddResourcePath("newpage.css", IDR_NEWPAGE_CSS);
  source->AddResourcePath("bridge.js", IDR_NEWPAGE_BRIDGE_JS);
  source->SetDefaultResource(IDR_NEWPAGE_INDEX_HTML);

  // Backgrounds, news thumbnails and site icons are remote https images.
  source->OverrideContentSecurityPolicy(
      network::mojom::CSPDirectiveName::ImgSrc,
      "img-src 'self' https: data: blob:;");
  source->OverrideContentSecurityPolicy(
      network::mojom::CSPDirectiveName::StyleSrc,
      "style-src 'self' 'unsafe-inline';");

  // newpage.js uses DOMParser.parseFromString(), which is a Trusted Types sink.
  source->DisableTrustedTypesCSP();

  web_ui->AddMessageHandler(std::make_unique<NewPageHandler>());
}

NewPageUI::~NewPageUI() = default;
