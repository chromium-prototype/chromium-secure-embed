// Copyright 2025 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/browser/renderer_host/frame_tree_aware_frame_connector_delegate.h"

#include "cc/trees/render_frame_metadata.h"
#include "components/viz/common/surfaces/frame_sink_id.h"
#include "content/browser/renderer_host/frame_tree.h"
#include "content/browser/renderer_host/frame_tree_node.h"
#include "content/browser/renderer_host/navigation_controller_impl.h"
#include "content/browser/renderer_host/render_frame_host_delegate.h"
#include "content/browser/renderer_host/render_frame_host_impl.h"
#include "content/browser/renderer_host/render_frame_proxy_host.h"
#include "content/browser/renderer_host/render_widget_host_view_base.h"
#include "content/browser/renderer_host/render_widget_host_view_child_frame.h"
#include "content/public/browser/render_process_host.h"
#include "ui/gfx/geometry/size.h"

namespace content {

FrameTreeAwareFrameConnectorDelegate::FrameTreeAwareFrameConnectorDelegate(
    RenderFrameProxyHost* frame_proxy_in_parent_renderer)
    : frame_proxy_in_parent_renderer_(frame_proxy_in_parent_renderer) {}

FrameTreeAwareFrameConnectorDelegate::~FrameTreeAwareFrameConnectorDelegate() =
    default;

void FrameTreeAwareFrameConnectorDelegate::DidUpdateVisualProperties(
    const cc::RenderFrameMetadata& metadata) {
  frame_proxy_in_parent_renderer_->DidUpdateVisualProperties(metadata);
}

void FrameTreeAwareFrameConnectorDelegate::DisableAutoResize() {
  frame_proxy_in_parent_renderer_->DisableAutoResize();
}

void FrameTreeAwareFrameConnectorDelegate::EnableAutoResize(
    const gfx::Size& min_size,
    const gfx::Size& max_size) {
  frame_proxy_in_parent_renderer_->EnableAutoResize(min_size, max_size);
}

RenderFrameHostImpl*
FrameTreeAwareFrameConnectorDelegate::GetChildRenderFrameHost() const {
  return frame_proxy_in_parent_renderer_
             ? frame_proxy_in_parent_renderer_->frame_tree_node()
                   ->current_frame_host()
             : nullptr;
}

RenderWidgetHostViewBase*
FrameTreeAwareFrameConnectorDelegate::GetParentView() {
  if (!frame_proxy_in_parent_renderer_) {
    return nullptr;
  }

  // Input always hits the parent view if there is one so we should
  // escape to an embedder.
  RenderFrameHostImpl* parent =
      GetChildRenderFrameHost()
          ->GetParentOrOuterDocumentOrEmbedderExcludingProspectiveOwners();
  return parent ? static_cast<RenderWidgetHostViewBase*>(parent->GetView())
                : nullptr;
}

RenderWidgetHostViewBase* FrameTreeAwareFrameConnectorDelegate::GetRootView() {
  if (!frame_proxy_in_parent_renderer_) {
    return nullptr;
  }

  RenderFrameHostImpl* root =
      GetChildRenderFrameHost()
          ->GetOutermostMainFrameOrEmbedderExcludingProspectiveOwners();
  return static_cast<RenderWidgetHostViewBase*>(root->GetView());
}

RenderProcessHost* FrameTreeAwareFrameConnectorDelegate::GetParentProcess()
    const {
  if (!frame_proxy_in_parent_renderer_) {
    return nullptr;
  }
  return frame_proxy_in_parent_renderer_->GetProcess();
}

Visibility FrameTreeAwareFrameConnectorDelegate::GetEmbedderVisibility() {
  RenderFrameHostImpl* child = GetChildRenderFrameHost();
  if (!child || !child->delegate()) {
    return Visibility::HIDDEN;
  }
  return child->delegate()->GetVisibility();
}

void FrameTreeAwareFrameConnectorDelegate::ChildProcessGone() {
  frame_proxy_in_parent_renderer_->ChildProcessGone();
}

void FrameTreeAwareFrameConnectorDelegate::NeedsReload() {
  frame_proxy_in_parent_renderer_->frame_tree_node()
      ->frame_tree()
      .controller()
      .SetNeedsReload(
          NavigationControllerImpl::NeedsReloadType::kCrashedSubframe);
}

bool FrameTreeAwareFrameConnectorDelegate::VisibilityChanged(
    RenderWidgetHostViewChildFrame* view,
    blink::mojom::FrameVisibility visibility) {
  // If there is an inner WebContents, it should be notified of the change in
  // the visibility. The Show/Hide methods will not be called if an inner
  // WebContents exists since the corresponding WebContents will itself call
  // Show/Hide on all the RenderWidgetHostViews (including this) one.
  if (view->host()
          ->frame_tree()
          ->delegate()
          ->OnRenderFrameProxyVisibilityChanged(frame_proxy_in_parent_renderer_,
                                                visibility)) {
    return true;
  }
  return false;
}

void FrameTreeAwareFrameConnectorDelegate::SendIntrinsicSizingInfoToParent(
    blink::mojom::IntrinsicSizingInfoPtr sizing_info) {
  if (!frame_proxy_in_parent_renderer_->is_render_frame_proxy_live()) {
    return;
  }
  frame_proxy_in_parent_renderer_->GetAssociatedRemoteFrame()
      ->IntrinsicSizingInfoOfChildChanged(std::move(sizing_info));
}

void FrameTreeAwareFrameConnectorDelegate::SendScreenRects() {
  // Other local root frames nested underneath this one implicitly have their
  // view rects changed when their ancestor is repositioned, and therefore
  // need to have their screen rects updated.
  FrameTreeNode* proxy_node =
      frame_proxy_in_parent_renderer_->frame_tree_node();
  for (FrameTreeNode* node :
       proxy_node->frame_tree().SubtreeNodes(proxy_node)) {
    if (node != proxy_node && node->current_frame_host()->is_local_root()) {
      node->current_frame_host()->GetRenderWidgetHost()->SendScreenRects();
    }
  }
}

void FrameTreeAwareFrameConnectorDelegate::SetFrameSinkId(
    const viz::FrameSinkId& id,
    bool allow_paint_holding) {
  if (frame_proxy_in_parent_renderer_ &&
      frame_proxy_in_parent_renderer_->is_render_frame_proxy_live()) {
    frame_proxy_in_parent_renderer_->GetAssociatedRemoteFrame()->SetFrameSinkId(
        id, allow_paint_holding);
  }
}

}  // namespace content
