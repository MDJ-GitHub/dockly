// Copyright 2026 The Dockium Authors.
// chrome/browser/ui/webui/dockium_welcome/dockium_welcome_ui.cc

#include "chrome/browser/ui/webui/dockium_welcome/dockium_welcome_ui.h"

#include "chrome/browser/profiles/profile.h"
#include "chrome/common/webui_url_constants.h"
#include "chrome/grit/dockium_welcome_resources.h"
#include "chrome/grit/dockium_welcome_resources_map.h"
#include "content/public/browser/web_ui.h"
#include "content/public/browser/web_ui_data_source.h"
#include "services/network/public/mojom/content_security_policy.mojom.h"

DockiumWelcomeUI::DockiumWelcomeUI(content::WebUI* web_ui)
    : content::WebUIController(web_ui) {
  Profile* profile = Profile::FromWebUI(web_ui);
  content::WebUIDataSource* source = content::WebUIDataSource::CreateAndAdd(
      profile, chrome::kChromeUIDockiumWelcomeHost);

  source->AddResourcePaths(kDockiumWelcomeResources);
  source->AddResourcePath("", IDR_DOCKIUM_WELCOME_INDEX_HTML);
  source->SetDefaultResource(IDR_DOCKIUM_WELCOME_INDEX_HTML);

  // WebUI's default CSP blocks images and audio from the page itself.
  source->OverrideContentSecurityPolicy(
      network::mojom::CSPDirectiveName::ImgSrc, "img-src 'self';");
  source->OverrideContentSecurityPolicy(
      network::mojom::CSPDirectiveName::MediaSrc, "media-src 'self';");
}

DockiumWelcomeUI::~DockiumWelcomeUI() = default;

WEB_UI_CONTROLLER_TYPE_IMPL(DockiumWelcomeUI)
