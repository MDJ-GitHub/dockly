// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_VIEWS_TABS_COMMON_TAB_VIEW_VERTICAL_LAYOUT_H_
#define CHROME_BROWSER_UI_VIEWS_TABS_COMMON_TAB_VIEW_VERTICAL_LAYOUT_H_

#include "chrome/browser/ui/views/tabs/common/tab_view.h"

enum class TabStripOrientation;

namespace views {
struct ProposedLayout;
}

class TabViewVerticalLayout : public TabView::LayoutManager {
 public:
  // Dockium: width of the strip while it is collapsed, as measured by the top
  // container (0 until it has been seen). The tab icon keeps the x it has in
  // that width while the strip is hover-expanded, so it never moves.
  static void NoteCollapsedColumnWidth(int width);
  static int GetCollapsedColumnWidth();

  TabViewVerticalLayout();
  TabViewVerticalLayout(const TabViewVerticalLayout&) = delete;
  TabViewVerticalLayout& operator=(const TabViewVerticalLayout&) = delete;
  ~TabViewVerticalLayout() override;

 protected:
  // views::LayoutManagerBase:
  void OnInstalled(views::View* host) override;
  views::ProposedLayout CalculateProposedLayout(
      const views::SizeBounds& size_bounds) const override;

 private:
  struct TabChildConfig {
    raw_ptr<views::View> view = nullptr;
    int min_width = 0;
    int padding = 0;
    bool align_leading = false;
    bool expand = false;
    // Some alert indicators need to decorate the close button when the tab
    // strip is collapsed. In that case, center the child and set a size of (0,
    // 0).
    bool decorate_on_collapse = false;
  };

  gfx::Rect GetChildBounds(const gfx::Rect& container,
                           const TabChildConfig& config,
                           const bool center) const;

  // Calculates the visibility of child view based on various states.
  bool IsChildVisible(const views::View* child, const int width) const;

  // Ordered vector of children to be rendered in the tab.
  std::vector<TabChildConfig> tab_children_configs_;

  // The icon's x (in tab coordinates) and the tab width it was recorded at,
  // taken while the tab is collapsed (centered). While the strip is
  // hover-expanded the icon keeps this x, so it doesn't slide to the right.
  mutable int collapsed_icon_x_ = -1;
  mutable int collapsed_icon_width_ = 0;
};

#endif  // CHROME_BROWSER_UI_VIEWS_TABS_COMMON_TAB_VIEW_VERTICAL_LAYOUT_H_
