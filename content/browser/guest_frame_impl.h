// Copyright 2025 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_BROWSER_GUEST_FRAME_IMPL_H_
#define CONTENT_BROWSER_GUEST_FRAME_IMPL_H_

#include "content/browser/renderer_host/cross_process_frame_connector_base.h"
#include "content/public/browser/guest_frame.h"

namespace content {

class GuestFrameImpl : public GuestFrame, public CrossProcessFrameConnectorBase {
 public:
  GuestFrameImpl(WebContents* guest_web_contents,
                 RenderFrameHost* embedder_rfh);
  ~GuestFrameImpl() override;

  // GuestFrame:
  void SetLocalSurfaceId(const viz::LocalSurfaceId& local_surface_id) override;
  const viz::FrameSinkId& GetFrameSinkId() const override;

 private:
  // CrossProcessFrameConnectorBase:
  // TODO(secure-embed): Most of these should be delegated to SecureEmbedHost.
  RenderFrameHostImpl* current_child_frame_host() const override;

  RenderWidgetHostViewBase* GetParentRenderWidgetHostView() override;
  RenderWidgetHostViewBase* GetRootRenderWidgetHostView() override;
  void SendIntrinsicSizingInfoToParent(
      blink::mojom::IntrinsicSizingInfoPtr) override;
  void EnableAutoResize(const gfx::Size& min_size,
                        const gfx::Size& max_size) override;
  void DisableAutoResize() override;
  void DidUpdateVisualProperties(
      const cc::RenderFrameMetadata& metadata) override;

  void ReportFrameSinkId(const viz::FrameSinkId& frame_sink_id,
                         bool allow_paint_holding) override;
  void ReportChildProcessGone() override;
  void ReportBadVisualProperties() override;
  bool NotifyInnerWebContentsVisibilityChanged() override;
  void RequestTabReload() override;

  viz::FrameSinkId frame_sink_id_;
  viz::LocalSurfaceId local_surface_id_;
  raw_ptr<WebContents> guest_web_contents_ = nullptr;
  raw_ptr<RenderWidgetHostViewChildFrame> view_ = nullptr;
};

}  // namespace content

#endif // CONTENT_BROWSER_GUEST_FRAME_IMPL_H_
