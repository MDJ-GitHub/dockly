// Copyright 2026. Personal build.
#ifndef CHROME_BROWSER_UI_WEBUI_NEWPAGE_NEWPAGE_UI_H_
#define CHROME_BROWSER_UI_WEBUI_NEWPAGE_NEWPAGE_UI_H_

#include "content/public/browser/web_ui_controller.h"

namespace content {
class WebUI;
}

// Controller for chrome://newpage, the custom new tab page.
class NewPageUI : public content::WebUIController {
 public:
  explicit NewPageUI(content::WebUI* web_ui);
  NewPageUI(const NewPageUI&) = delete;
  NewPageUI& operator=(const NewPageUI&) = delete;
  ~NewPageUI() override;
};

#endif  // CHROME_BROWSER_UI_WEBUI_NEWPAGE_NEWPAGE_UI_H_
