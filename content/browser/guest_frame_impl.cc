// Copyright 2025 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/browser/guest_frame_impl.h"

#include "base/notimplemented.h"
#include "components/viz/common/surfaces/surface_info.h"
#include "content/browser/renderer_host/render_frame_host_impl.h"
#include "content/browser/renderer_host/render_widget_host_view_child_frame.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/secure_embed_delegate.h"
#include "content/public/browser/web_contents.h"
#include "third_party/blink/public/common/frame/frame_visual_properties.h"
#include "third_party/blink/public/mojom/frame/intrinsic_sizing_info.mojom.h"
#include "third_party/blink/public/mojom/frame/viewport_intersection_state.mojom.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/gfx/geometry/size.h"

namespace content {

// static
std::unique_ptr<GuestFrame> GuestFrame::Create(WebContents* guest_web_contents,
                                               RenderFrameHost* embedder_rfh) {
  return std::make_unique<GuestFrameImpl>(guest_web_contents, embedder_rfh);
}

GuestFrameImpl::GuestFrameImpl(WebContents* guest_web_contents,
                               RenderFrameHost* embedder_rfh)
    : guest_web_contents_(guest_web_contents),
      view_(static_cast<RenderWidgetHostViewChildFrame*>(
          guest_web_contents->GetRenderWidgetHostView())) {
  auto* base_view = static_cast<RenderWidgetHostViewBase*>(
    guest_web_contents->GetRenderWidgetHostView());
  CHECK(base_view->IsRenderWidgetHostViewChildFrame());
  view_ = static_cast<RenderWidgetHostViewChildFrame*>(base_view);
  SetView(view_, /*allow_paint_holding=*/false);
}

GuestFrameImpl::~GuestFrameImpl() = default;

void GuestFrameImpl::SetLocalSurfaceId(
    const viz::LocalSurfaceId& local_surface_id) {
  local_surface_id_ = local_surface_id;
  LOG(INFO) << "GuestFrameImpl::SetLocalSurfaceId:"
            << local_surface_id_.ToString();

  // TODO(secure-embed): The visual properties should not be hardcoded.
  blink::FrameVisualProperties visual_properties;
  visual_properties.visible_viewport_size = gfx::Size(1000, 1000);
  visual_properties.compositor_viewport = gfx::Rect(10, 10, 1000, 1000);
  visual_properties.rect_in_local_root = gfx::Rect(10, 10, 1000, 1000);
  visual_properties.local_frame_size = gfx::Size(1000, 1000);
  visual_properties.local_surface_id = local_surface_id_;
  visual_properties.screen_infos =
      GetRootRenderWidgetHostView()->GetScreenInfos();
  OnSynchronizeVisualProperties(visual_properties);
}

const viz::FrameSinkId& GuestFrameImpl::GetFrameSinkId() const {
  return frame_sink_id_;
}

RenderFrameHostImpl* GuestFrameImpl::current_child_frame_host() const {
  return static_cast<RenderFrameHostImpl*>(
      const_cast<WebContents*>(guest_web_contents_.get())
          ->GetPrimaryMainFrame());
}

RenderWidgetHostViewBase* GuestFrameImpl::GetParentRenderWidgetHostView() {
  return static_cast<RenderWidgetHostViewBase*>(
      guest_web_contents_->GetSecureEmbedDelegate()
          ->GetEmbedderWebContents()
          ->GetRenderWidgetHostView());
}

RenderWidgetHostViewBase*
GuestFrameImpl::GetRootRenderWidgetHostView() {
  // TODO(secure-embed): Do we support multiple levels of embedding?
  // Mixed kinds?
  return GetParentRenderWidgetHostView();
}
void GuestFrameImpl::SendIntrinsicSizingInfoToParent(
    blink::mojom::IntrinsicSizingInfoPtr) {
  NOTIMPLEMENTED();
}
void GuestFrameImpl::EnableAutoResize(const gfx::Size& min_size,
                                      const gfx::Size& max_size) {
  NOTIMPLEMENTED();
}
void GuestFrameImpl::DisableAutoResize() {
  NOTIMPLEMENTED();
}
void GuestFrameImpl::DidUpdateVisualProperties(
    const cc::RenderFrameMetadata& metadata) {
  NOTIMPLEMENTED();
}

void GuestFrameImpl::ReportFrameSinkId(const viz::FrameSinkId& frame_sink_id,
                                       bool allow_paint_holding) {
  // TODO(secure-embed): This should actively push.
  frame_sink_id_ = frame_sink_id;
}
void GuestFrameImpl::ReportChildProcessGone() {
  NOTIMPLEMENTED();
}
void GuestFrameImpl::ReportBadVisualProperties() {
  NOTIMPLEMENTED();
}
bool GuestFrameImpl::NotifyInnerWebContentsVisibilityChanged() {
  NOTIMPLEMENTED();
  return false;
}
void GuestFrameImpl::RequestTabReload() {
  NOTIMPLEMENTED();
}

}  // namespace content
