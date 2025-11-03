// Copyright 2025 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_BROWSER_SECURE_EMBED_CONNECTOR_IMPL_H_
#define CONTENT_BROWSER_SECURE_EMBED_CONNECTOR_IMPL_H_

#include <memory>

#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "components/viz/common/surfaces/frame_sink_id.h"
#include "content/browser/renderer_host/cross_process_frame_connector.h"
#include "content/public/browser/secure_embed_connector.h"
#include "content/public/browser/web_contents.h"
#include "third_party/blink/public/mojom/input/focus_type.mojom.h"

namespace blink {
struct FrameVisualProperties;
class WebKeyboardEvent;
}  // namespace blink

namespace content {

class RenderFrameHost;

class SecureEmbedConnectorImpl : public SecureEmbedConnector {
 public:
  // `embedded_web_contents` will have ownership of this.
  SecureEmbedConnectorImpl(WebContentsImpl* embedder_web_contents,
                           WebContentsImpl* embedded_web_contents);
  ~SecureEmbedConnectorImpl() override;

  WebContents* GetEmbedderWebContents() override;

  // Convenience wrapper for GetDelegate()->FocusInEmbedder that null-checks
  // the delegate.
  void FocusInEmbedder(FocusOperation focus_op);

  // SecureEmbedConnector:
  void SetDelegate(SecureEmbedConnector::Delegate* delegate) override;
  SecureEmbedConnector::Delegate* GetDelegate() override;
  void OnSynchronizeVisualProperties(
      const blink::FrameVisualProperties& visual_properties) override;
  const viz::FrameSinkId& GetFrameSinkId() const override;
  void ForwardKeyboardEvent(
      const blink::WebKeyboardEvent& keyboard_event) override;
  void SetFocus(bool focused, blink::mojom::FocusType focus_type) override;

  void OnRenderViewReady();
  void OnRenderFrameHostChanged(RenderFrameHost* old_host,
                                RenderFrameHost* new_host);

 private:
  // Forward decl for internal observer that tracks WebContents events and
  // forwards them to this class.
  class Observer;

  // Updates the view_ member to track the current RenderWidgetHostView
  // associated with the guest WebContents.
  void UpdateViewForCurrentRenderFrameHost();

  std::unique_ptr<Observer> observer_;
  raw_ptr<SecureEmbedConnector::Delegate> delegate_ = nullptr;

  base::WeakPtr<WebContents> embedder_web_contents_;
  base::WeakPtr<WebContents> guest_web_contents_;
  raw_ptr<RenderWidgetHostViewChildFrame> view_ = nullptr;

  // The owned CrossProcessFrameConnector that handles all the frame connection
  // logic.
  std::unique_ptr<CrossProcessFrameConnector> connector_;

  // The last received FrameSinkId from the guest WebContents's view.
  viz::FrameSinkId frame_sink_id_;
};

}  // namespace content

#endif  // #define CONTENT_BROWSER_SECURE_EMBED_CONNECTOR_IMPL_H_
