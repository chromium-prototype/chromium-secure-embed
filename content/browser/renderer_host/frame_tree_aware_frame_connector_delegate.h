// Copyright 2025 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_BROWSER_RENDERER_HOST_FRAME_TREE_AWARE_FRAME_CONNECTOR_DELEGATE_H_
#define CONTENT_BROWSER_RENDERER_HOST_FRAME_TREE_AWARE_FRAME_CONNECTOR_DELEGATE_H_

#include "base/memory/raw_ptr.h"
#include "content/browser/renderer_host/cross_process_frame_connector.h"
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
class RenderFrameProxyHost;
class RenderWidgetHostViewBase;
class RenderWidgetHostViewChildFrame;

// Delegate implementation for cross-process iframe scenarios.
// This delegate handles communication through the RenderFrameProxyHost in the
// parent renderer process for out-of-process child frames.
class FrameTreeAwareFrameConnectorDelegate final
    : public CrossProcessFrameConnector::Delegate {
 public:
  // |frame_proxy_in_parent_renderer| corresponds to the RenderFrameProxyHost
  // that routes messages to the parent frame's renderer process.
  explicit FrameTreeAwareFrameConnectorDelegate(
      RenderFrameProxyHost* frame_proxy_in_parent_renderer);

  ~FrameTreeAwareFrameConnectorDelegate() override;

  FrameTreeAwareFrameConnectorDelegate(
      const FrameTreeAwareFrameConnectorDelegate&) = delete;
  FrameTreeAwareFrameConnectorDelegate& operator=(
      const FrameTreeAwareFrameConnectorDelegate&) = delete;

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
  // The RenderFrameProxyHost that routes messages to the parent frame's
  // renderer process. Can be nullptr in tests.
  raw_ptr<RenderFrameProxyHost> frame_proxy_in_parent_renderer_;
};

}  // namespace content

#endif  // CONTENT_BROWSER_RENDERER_HOST_FRAME_TREE_AWARE_FRAME_CONNECTOR_DELEGATE_H_
