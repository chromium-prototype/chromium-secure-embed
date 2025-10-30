// Copyright 2025 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_BROWSER_SECURE_EMBED_CONNECTOR_IMPL_H_
#define CONTENT_BROWSER_SECURE_EMBED_CONNECTOR_IMPL_H_

#include <memory>

#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "cc/input/touch_action.h"
#include "content/public/browser/guest_frame.h"
#include "content/public/browser/secure_embed_connector.h"
#include "content/public/browser/visibility.h"
#include "third_party/blink/public/common/frame/frame_visual_properties.h"
#include "third_party/blink/public/mojom/frame/intrinsic_sizing_info.mojom-forward.h"
#include "third_party/blink/public/mojom/frame/lifecycle.mojom.h"
#include "third_party/blink/public/mojom/frame/viewport_intersection_state.mojom.h"
#include "third_party/blink/public/mojom/input/input_event_result.mojom-shared.h"
#include "third_party/blink/public/mojom/input/pointer_lock_result.mojom-shared.h"
#include "ui/display/screen_infos.h"
#include "ui/gfx/geometry/rect.h"

namespace content {

class RenderFrameHostImpl;
class WebContentsImpl;

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

  // SecureEmbedConnector::
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
  base::WeakPtr<WebContents> guest_web_contents_ = nullptr;
  raw_ptr<RenderWidgetHostViewChildFrame> view_ = nullptr;

  // The owned CrossProcessFrameConnector that handles all the frame connection
  // logic.
  std::unique_ptr<CrossProcessFrameConnector> connector_;

  // The last received FrameSinkId from the guest WebContents's view.
  viz::FrameSinkId frame_sink_id_;
};

}  // namespace content

#endif  // #define CONTENT_BROWSER_SECURE_EMBED_CONNECTOR_IMPL_H_
