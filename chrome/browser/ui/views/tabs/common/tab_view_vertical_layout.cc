// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/views/tabs/common/tab_view_vertical_layout.h"

#include "chrome/browser/glic/browser_ui/tab_underline_view.h"
#include "chrome/browser/ui/layout_constants.h"
#include "chrome/browser/ui/views/frame/vertical_tab_strip_region_view.h"
#include "chrome/browser/ui/views/tabs/common/tab_view.h"
#include "chrome/browser/ui/views/tabs/shared/tab_strip_types.h"
#include "chrome/browser/ui/views/tabs/tab/alert_indicator_button.h"
#include "chrome/browser/ui/views/tabs/tab/tab_close_button.h"
#include "chrome/browser/ui/views/tabs/tab/tab_icon.h"
#include "chrome/browser/ui/views/tabs/tab/tab_title.h"
#include "chrome/browser/ui/views/tabs/tab_style_views.h"

namespace {
constexpr int kIconDesignWidth = 16;
constexpr int kTitleMinWidth = 10;
constexpr int kHorizontalInset = 8;
constexpr int kDefaultPadding = 4;
// Dockium: nudge the favicon left by this many pixels.
constexpr int kIconLeftNudge = 00;
int g_collapsed_column_width = 0;
}  // namespace

// static
void TabViewVerticalLayout::NoteCollapsedColumnWidth(int width) {
  if (width > 0) {
    g_collapsed_column_width = width;
  }
}

// static
int TabViewVerticalLayout::GetCollapsedColumnWidth() {
  return g_collapsed_column_width;
}

TabViewVerticalLayout::TabViewVerticalLayout() = default;
TabViewVerticalLayout::~TabViewVerticalLayout() = default;

void TabViewVerticalLayout::OnInstalled(views::View* host) {
  TabView::LayoutManager::OnInstalled(host);
  tab_children_configs_ = {
      TabChildConfig(TabView().close_button_, kIconDesignWidth, kDefaultPadding,
                     /*align_leading=*/false,
                     /*expand=*/false),
      TabChildConfig(TabView().alert_indicator_, kIconDesignWidth,
                     kDefaultPadding,
                     /*align_leading=*/false,
                     /*expand=*/false,
                     /*decorate_on_collapse=*/true),
      TabChildConfig(TabView().icon_, kIconDesignWidth, kHorizontalInset,
                     /*align_leading=*/true,
                     /*expand=*/false),
      TabChildConfig(TabView().title_, kTitleMinWidth, kDefaultPadding,
                     /*align_leading=*/true,
                     /*expand=*/true)};
}

views::ProposedLayout TabViewVerticalLayout::CalculateProposedLayout(
    const views::SizeBounds& size_bounds) const {
  const int width = size_bounds.width().value_or(
      VerticalTabStripRegionView::kUncollapsedMaxWidth);
  const int height = GetLayoutConstant(
      TabView().pinned_ ? LayoutConstant::kVerticalTabPinnedHeight
                        : LayoutConstant::kVerticalTabHeight);
  views::ProposedLayout layouts;
  layouts.host_size = gfx::Size(width, height);

  gfx::Rect bounds_remaining = gfx::Rect(layouts.host_size);
  bounds_remaining.Inset(TabView().tab_styling()->GetContentsInsets());

  // If the tab is collapsed but animating with a wider width then we shouldn't
  // center the contents.
  const bool is_centered = (TabView().pinned_ || TabView().collapsed_) &&
                           !TabView().IsInExpandOnHover(width);

  int placed_children = 0;
  for (const auto& child : tab_children_configs_) {
    const bool can_render_child =
        is_centered
            ? (placed_children == 0)
            : (child.min_width + child.padding < bounds_remaining.width() ||
               placed_children < 2);
    const bool is_child_visible = IsChildVisible(child.view, width);
    if (is_child_visible && can_render_child) {
      gfx::Rect child_bounds =
          GetChildBounds(bounds_remaining, child, is_centered);
      if (child.view == TabView().icon_) {
        if (is_centered) {
          // Dockium: shift the collapsed/pinned icon left. The nudged x is
          // what gets remembered below, so the expanded path stays in sync.
          child_bounds.set_x(child_bounds.x() - kIconLeftNudge);
          // Remember where the icon sits in the collapsed tab. Keep the value
          // from the narrowest width seen so animation frames can't skew it.
          if (collapsed_icon_x_ < 0 || width <= collapsed_icon_width_) {
            collapsed_icon_x_ = child_bounds.x();
            collapsed_icon_width_ = width;
          }
        } else {
          // Expanded (e.g. expand-on-hover): keep the collapsed x.
          const int side_padding = GetLayoutConstant(
              LayoutConstant::kVerticalTabStripHorizontalPadding);
          int icon_x = -1;
          const int column_width = GetCollapsedColumnWidth();
          if (column_width > 0) {
            // Dockium: computed from the measured collapsed strip width, so
            // it is exactly the x the icon has when the strip is collapsed
            // (the tab is centered in `column_width - 2 * side_padding`),
            // whatever layouts this tab has seen before.
            icon_x = (column_width - 2 * side_padding -
                      child_bounds.width()) / 2 - kIconLeftNudge;
          } else {
            // Already includes the nudge (stored from the centered path).
            icon_x = collapsed_icon_x_;
          }
          if (icon_x < 0) {
            const int collapsed_tab_width =
                VerticalTabStripRegionView::kCollapsedWidth -
                2 * side_padding;
            icon_x = (collapsed_tab_width - child_bounds.width()) / 2 -
                     kIconLeftNudge;
          }
          child_bounds.set_x(icon_x);
        }
      }
      layouts.child_layouts.emplace_back(child.view.get(), is_child_visible,
                                         child_bounds);

      if (!is_centered) {
        bounds_remaining.Inset(
            child.align_leading
                ? gfx::Insets().set_left(child.padding + child.min_width)
                : gfx::Insets().set_right(child.padding + child.min_width));
      }

      placed_children += 1;
    } else if (child.decorate_on_collapse) {
      layouts.child_layouts.emplace_back(
          child.view.get(), is_child_visible,
          gfx::Rect(width / 2, height / 2, 0, 0));
    } else {
      layouts.child_layouts.emplace_back(
          child.view.get(), is_child_visible,
          gfx::Rect(bounds_remaining.x(), bounds_remaining.y(), 0, 0));
    }
  }

  if (TabView().glic_tab_underline_view_) {
    const gfx::Rect glic_bounds =
        gfx::Rect(0, 0, 2 * glic::TabUnderlineView::kEffectThickness, height);
    layouts.child_layouts.emplace_back(
        TabView().glic_tab_underline_view_.get(),
        TabView().glic_tab_underline_view_->GetVisible(), glic_bounds);
  }

  return layouts;
}

gfx::Rect TabViewVerticalLayout::GetChildBounds(const gfx::Rect& container,
                                                const TabChildConfig& config,
                                                const bool center) const {
  int preferred_width;
  int preferred_height;
  if (config.expand) {
    preferred_width = container.width() - config.padding;
    // The only expandable view is the views::Label. Just get the line height to
    // make calculating bounds cheaper.
    views::Label* label = views::AsViewClass<views::Label>(config.view);
    CHECK(label);
    preferred_height = label->GetLineHeight();
  } else {
    const gfx::Size preferred_size = config.view->GetPreferredSize();
    preferred_width = preferred_size.width();
    preferred_height = preferred_size.height();
  }

  // Some icons have larger sizes to account for decoration. Make a distinction
  // between the design width and the actual width.
  const int design_width =
      config.expand ? container.width() - config.padding : config.min_width;

  int x = container.x();
  if (center) {
    x += 0.5 * (container.width() - preferred_width);
  } else if (config.align_leading) {
    x += 0.5 * (design_width - preferred_width);
  } else {
    x += container.width() - 0.5 * (design_width + preferred_width);
  }
  const int y = container.y() + 0.5 * (container.height() - preferred_height);

  return gfx::Rect(x, y, preferred_width, preferred_height);
}

bool TabViewVerticalLayout::IsChildVisible(const views::View* child_view,
                                           const int width) const {
  if (child_view == TabView().title_) {
    // Pinned titles should be visible in the expand on hover state when the
    // width is sufficient to show the title.
    return !TabView().pinned_ || TabView().IsInExpandOnHover(width);
  }

  if (child_view == TabView().alert_indicator_) {
    if (TabView().glic_tab_underline_view_ &&
        (TabView().alert_indicator_->showing_alert_state() ==
             tabs::TabAlert::kGlicAccessing ||
         TabView().alert_indicator_->showing_alert_state() ==
             tabs::TabAlert::kGlicSharing)) {
      return false;
    }
    return TabView().alert_indicator_->showing_alert_state().has_value();
  }

  if (child_view == TabView().icon_) {
    return !TabView().pinned_ ||
           !IsChildVisible(TabView().alert_indicator_, width);
  }

  if (child_view == TabView().close_button_) {
    if (TabView().pinned_) {
      return false;
    }

    // When uncollapsing the tabstrip, intentionally start showing the close
    // button on active non-hovered tabs a little bit sooner than reaching the
    // uncollapsed min width, because otherwise the close buttons in a grouped
    // split tab will visibly show up at different times due to rounding.
    constexpr int kUncollapsedMinWidthThreshold = 3;

    const bool hovered_or_focused =
        TabView().hovered_ || TabView().HasFocus() ||
        (TabView().close_button_ && TabView().close_button_->HasFocus());
    if (width <
        TabView().UncollapsedMinWidth() - kUncollapsedMinWidthThreshold) {
      return TabView().active_ && hovered_or_focused;
    }

    return TabView().active_ || hovered_or_focused;
  }

  NOTREACHED() << "Unknown tab child view";
}
