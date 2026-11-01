// Copyright 2026 The Dockium Authors.
// Place at: chrome/browser/ui/webui/dockium_welcome/dockium_welcome_ui.h

#ifndef CHROME_BROWSER_UI_WEBUI_DOCKIUM_WELCOME_DOCKIUM_WELCOME_UI_H_
#define CHROME_BROWSER_UI_WEBUI_DOCKIUM_WELCOME_DOCKIUM_WELCOME_UI_H_

#include "chrome/common/webui_url_constants.h"
#include "content/public/browser/web_ui_controller.h"
#include "content/public/browser/webui_config.h"

class DockiumWelcomeUI;

// Registers chrome://dockium-welcome.
class DockiumWelcomeUIConfig
    : public content::DefaultWebUIConfig<DockiumWelcomeUI> {
 public:
  DockiumWelcomeUIConfig()
      : DefaultWebUIConfig(content::kChromeUIScheme,
                           chrome::kChromeUIDockiumWelcomeHost) {}
};

// The first-run welcome page (slides + Start Dockium button).
class DockiumWelcomeUI : public content::WebUIController {
 public:
  explicit DockiumWelcomeUI(content::WebUI* web_ui);
  DockiumWelcomeUI(const DockiumWelcomeUI&) = delete;
  DockiumWelcomeUI& operator=(const DockiumWelcomeUI&) = delete;
  ~DockiumWelcomeUI() override;

 private:
  WEB_UI_CONTROLLER_TYPE_DECL();
};

#endif  // CHROME_BROWSER_UI_WEBUI_DOCKIUM_WELCOME_DOCKIUM_WELCOME_UI_H_
