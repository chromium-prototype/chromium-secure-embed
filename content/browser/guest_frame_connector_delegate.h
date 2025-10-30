// Copyright 2025 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_BROWSER_GUEST_FRAME_CONNECTOR_DELEGATE_H_
#define CONTENT_BROWSER_GUEST_FRAME_CONNECTOR_DELEGATE_H_

#include "base/memory/raw_ptr.h"
#include "content/browser/renderer_host/cross_process_frame_connector.h"
#include "content/public/browser/guest_frame.h"
#include "content/public/browser/visibility.h"

namespace blink::mojom {
class IntrinsicSizingInfo;
using IntrinsicSizingInfoPtr = mojo::StructPtr<IntrinsicSizingInfo>;
}  // namespace blink::mojom

namespace viz {
class FrameSinkId;
}

namespace content {

class RenderFrameHostImpl;
class RenderWidgetHostViewBase;
class RenderWidgetHostViewChildFrame;
class WebContents;

// Delegate implementation for secure embed/guest frame scenarios.
// This delegate handles communication through the guest WebContents and
// GuestFrame::Delegate for embedded guest content.
class GuestFrameConnectorDelegate final
    : public CrossProcessFrameConnector::Delegate {
 public:
  GuestFrameConnectorDelegate(WebContents* guest_web_contents,
                              GuestFrame::Delegate* guest_delegate);

  ~GuestFrameConnectorDelegate() override;

  GuestFrameConnectorDelegate(const GuestFrameConnectorDelegate&) = delete;
  GuestFrameConnectorDelegate& operator=(const GuestFrameConnectorDelegate&) =
      delete;

  // CrossProcessFrameConnector::Delegate implementation:
  void DidUpdateVisualProperties(
      const cc::RenderFrameMetadata& metadata) override;
  void DisableAutoResize() override;
  void EnableAutoResize(const gfx::Size& min_size,
                        const gfx::Size& max_size) override;
  RenderFrameHostImpl* GetChildRenderFrameHost() const override;
  RenderWidgetHostViewBase* GetParentView() override;
  RenderWidgetHostViewBase* GetRootView() override;
  RenderProcessHost* GetParentProcess() const override;
  Visibility GetEmbedderVisibility() override;
  void OnChildProcessGone() override;
  void OnNeedsReload() override;
  bool OnVisibilityChanged(RenderWidgetHostViewChildFrame* view,
                           blink::mojom::FrameVisibility visibility) override;
  void SendIntrinsicSizingInfoToParent(
      blink::mojom::IntrinsicSizingInfoPtr info) override;
  void SendScreenRects() override;
  void SetFrameSinkId(const viz::FrameSinkId& id,
                      bool allow_paint_holding) override;

 private:
  // The guest WebContents being embedded.
  raw_ptr<WebContents> guest_web_contents_;

  // Delegate for communicating with the embedder.
  raw_ptr<GuestFrame::Delegate> guest_delegate_;
};

}  // namespace content

#endif  // CONTENT_BROWSER_GUEST_FRAME_CONNECTOR_DELEGATE_H_
