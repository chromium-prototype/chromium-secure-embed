// Copyright 2014 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/browser/renderer_host/cross_process_frame_connector.h"

#include "base/functional/bind.h"
#include "base/metrics/histogram_macros.h"
#include "base/trace_event/optional_trace_event.h"
#include "components/input/cursor_manager.h"
#include "components/input/render_widget_host_input_event_router.h"
#include "components/viz/common/features.h"
#include "content/browser/renderer_host/frame_tree.h"
#include "content/browser/renderer_host/frame_tree_node.h"
#include "content/browser/renderer_host/render_frame_host_delegate.h"
#include "content/browser/renderer_host/render_frame_host_manager.h"
#include "content/browser/renderer_host/render_frame_proxy_host.h"
#include "content/browser/renderer_host/render_view_host_impl.h"
#include "content/browser/renderer_host/render_widget_host_delegate.h"
#include "content/browser/renderer_host/render_widget_host_impl.h"
#include "content/browser/renderer_host/render_widget_host_view_base.h"
#include "content/browser/renderer_host/render_widget_host_view_child_frame.h"
#include "content/common/features.h"
#include "third_party/blink/public/common/frame/frame_visual_properties.h"
#include "third_party/blink/public/common/input/web_input_event.h"
#include "third_party/blink/public/mojom/frame/intrinsic_sizing_info.mojom.h"
#include "ui/base/cursor/cursor.h"
#include "ui/gfx/geometry/dip_util.h"

namespace content {

CrossProcessFrameConnector::CrossProcessFrameConnector(
    RenderFrameProxyHost* frame_proxy_in_parent_renderer)
    : frame_proxy_in_parent_renderer_(frame_proxy_in_parent_renderer) {
  // May be null  for tests.
  if (frame_proxy_in_parent_renderer_) {
    // At this point, SetView() has not been called and so the associated
    // RenderWidgetHost doesn't have a view yet. That means calling
    // GetScreenInfos() on the associated RenderWidgetHost will just default to
    // the primary display, which may not be appropriate. So instead we call
    // GetScreenInfos() on the root RenderWidgetHost, which will be guaranteed
    // to be on the correct display. All subsequent updates to |screen_infos_|
    // ultimately come from the root, so it makes sense to do it here as well.
    screen_infos_ = current_child_frame_host()
                        ->GetOutermostMainFrameOrEmbedder()
                        ->GetRenderWidgetHost()
                        ->GetScreenInfos();
  }
}

CrossProcessFrameConnector::~CrossProcessFrameConnector() = default;

void CrossProcessFrameConnector::ReportFrameSinkId(
    const viz::FrameSinkId& frame_sink_id,
    bool allow_paint_holding) {
  if (frame_proxy_in_parent_renderer_ &&
      frame_proxy_in_parent_renderer_->is_render_frame_proxy_live()) {
    frame_proxy_in_parent_renderer_->GetAssociatedRemoteFrame()->SetFrameSinkId(
        view_->GetFrameSinkId(), allow_paint_holding);
  }
}

void CrossProcessFrameConnector::ReportChildProcessGone() {
  frame_proxy_in_parent_renderer_->ChildProcessGone();
}

void CrossProcessFrameConnector::ReportBadVisualProperties() {
  bad_message::ReceivedBadMessage(
      frame_proxy_in_parent_renderer_->GetProcess(),
      bad_message::CPFC_RESIZE_PARAMS_CHANGED_LOCAL_SURFACE_ID_UNCHANGED);
}

bool CrossProcessFrameConnector::NotifyInnerWebContentsVisibilityChanged() {
  return view_->host()
      ->frame_tree()
      ->delegate()
      ->OnRenderFrameProxyVisibilityChanged(frame_proxy_in_parent_renderer_,
                                            visibility_);
}

void CrossProcessFrameConnector::RequestTabReload() {
  frame_proxy_in_parent_renderer_->frame_tree_node()
      ->frame_tree()
      .controller()
      .SetNeedsReload(
          NavigationControllerImpl::NeedsReloadType::kCrashedSubframe);
}

void CrossProcessFrameConnector::SendIntrinsicSizingInfoToParent(
    blink::mojom::IntrinsicSizingInfoPtr sizing_info) {
  // The width/height should not be negative since gfx::SizeF will clamp
  // negative values to zero.
  DCHECK((sizing_info->size.width() >= 0.f) &&
         (sizing_info->size.height() >= 0.f));
  DCHECK((sizing_info->aspect_ratio.width() >= 0.f) &&
         (sizing_info->aspect_ratio.height() >= 0.f));
  if (!frame_proxy_in_parent_renderer_->is_render_frame_proxy_live())
    return;
  frame_proxy_in_parent_renderer_->GetAssociatedRemoteFrame()
      ->IntrinsicSizingInfoOfChildChanged(std::move(sizing_info));
}

RenderWidgetHostViewBase*
CrossProcessFrameConnector::GetRootRenderWidgetHostView() {
  // Tests may not have frame_proxy_in_parent_renderer_ set.
  if (!frame_proxy_in_parent_renderer_)
    return nullptr;

  RenderFrameHostImpl* root =
      current_child_frame_host()
          ->GetOutermostMainFrameOrEmbedderExcludingProspectiveOwners();
  return static_cast<RenderWidgetHostViewBase*>(root->GetView());
}

RenderWidgetHostViewBase*
CrossProcessFrameConnector::GetParentRenderWidgetHostView() {
  // Input always hits the parent view if there is one so we should
  // escape to an embedder.
  RenderFrameHostImpl* parent =
      current_child_frame_host()
          ? current_child_frame_host()
                ->GetParentOrOuterDocumentOrEmbedderExcludingProspectiveOwners()
          : nullptr;
  return parent ? static_cast<RenderWidgetHostViewBase*>(parent->GetView())
                : nullptr;
}

void CrossProcessFrameConnector::EnableAutoResize(const gfx::Size& min_size,
                                                  const gfx::Size& max_size) {
  frame_proxy_in_parent_renderer_->EnableAutoResize(min_size, max_size);
}

void CrossProcessFrameConnector::DisableAutoResize() {
  frame_proxy_in_parent_renderer_->DisableAutoResize();
}

void CrossProcessFrameConnector::DidUpdateVisualProperties(
    const cc::RenderFrameMetadata& metadata) {
  frame_proxy_in_parent_renderer_->DidUpdateVisualProperties(metadata);
}

RenderFrameHostImpl* CrossProcessFrameConnector::current_child_frame_host()
    const {
  return frame_proxy_in_parent_renderer_
             ? frame_proxy_in_parent_renderer_->frame_tree_node()
                   ->current_frame_host()
             : nullptr;
}

}  // namespace content
