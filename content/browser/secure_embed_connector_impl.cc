// Copyright 2025 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/browser/secure_embed_connector_impl.h"

#include "components/input/native_web_keyboard_event.h"
#include "content/browser/renderer_host/render_view_host_impl.h"
#include "content/browser/renderer_host/render_widget_host_delegate.h"
#include "content/browser/renderer_host/render_widget_host_impl.h"
#include "content/browser/renderer_host/render_widget_host_view_base.h"
#include "content/browser/renderer_host/render_widget_host_view_child_frame.h"
#include "content/browser/secure_embed_connector_delegate.h"
#include "content/browser/web_contents/web_contents_impl.h"
#include "content/public/browser/web_contents_observer.h"
#include "third_party/blink/public/common/frame/frame_visual_properties.h"
#include "third_party/blink/public/common/input/web_keyboard_event.h"

namespace content {

// Nested observer class that forwards relevant events to
// SecureEmbedConnectorImpl. This avoids function name collisions with
// CrossProcessFrameConnectorBase.
class SecureEmbedConnectorImpl::Observer : public WebContentsObserver {
 public:
  explicit Observer(SecureEmbedConnectorImpl* connector,
                    WebContents* web_contents)
      : WebContentsObserver(web_contents), connector_(connector) {}

  ~Observer() override = default;

  // WebContentsObserver:
  void RenderViewReady() override { connector_->OnRenderViewReady(); }

  void RenderFrameHostChanged(RenderFrameHost* old_host,
                              RenderFrameHost* new_host) override {
    connector_->OnRenderFrameHostChanged(old_host, new_host);
  }

 private:
  raw_ptr<SecureEmbedConnectorImpl> connector_;
};

SecureEmbedConnectorImpl::SecureEmbedConnectorImpl(
    WebContentsImpl* embedder_web_contents,
    WebContentsImpl* embedded_web_contents)
    : embedder_web_contents_(embedder_web_contents->GetWeakPtr()),
      guest_web_contents_(embedded_web_contents->GetWeakPtr()) {
  observer_ = std::make_unique<Observer>(this, embedded_web_contents);

  auto connector_delegate = std::make_unique<SecureEmbedConnectorDelegate>(
      embedded_web_contents, this);
  connector_ = std::make_unique<CrossProcessFrameConnector>(
      std::move(connector_delegate));

  UpdateViewForCurrentRenderFrameHost();
}

SecureEmbedConnectorImpl::~SecureEmbedConnectorImpl() = default;

WebContents* SecureEmbedConnectorImpl::GetEmbedderWebContents() {
  return embedder_web_contents_.get();
}

void SecureEmbedConnectorImpl::FocusInEmbedder(FocusOperation focus_op) {
  if (delegate_) {
    delegate_->FocusInEmbedder(focus_op);
  }
}

void SecureEmbedConnectorImpl::SetDelegate(
    SecureEmbedConnector::Delegate* delegate) {
  CHECK(!(delegate && delegate_));
  delegate_ = delegate;
}

SecureEmbedConnector::Delegate* SecureEmbedConnectorImpl::GetDelegate() {
  return delegate_;
}

void SecureEmbedConnectorImpl::OnSynchronizeVisualProperties(
    const blink::FrameVisualProperties& visual_properties) {
  connector_->SynchronizeVisualProperties(visual_properties);
}

const viz::FrameSinkId& SecureEmbedConnectorImpl::GetFrameSinkId() const {
  return frame_sink_id_;
}

void SecureEmbedConnectorImpl::ForwardKeyboardEvent(
    const blink::WebKeyboardEvent& keyboard_event) {
  if (!guest_web_contents_) {
    return;
  }

  RenderWidgetHostViewBase* parent_view =
      connector_->GetParentRenderWidgetHostView();
  if (!parent_view) {
    return;
  }

  auto* child_view = static_cast<RenderWidgetHostViewBase*>(
      guest_web_contents_->GetRenderWidgetHostView());
  if (!child_view) {
    return;
  }

  input::NativeWebKeyboardEvent native_event(keyboard_event,
                                             parent_view->GetNativeView());

  RenderWidgetHostImpl* target_host = child_view->host();

  // If there are multiple widgets on the page (such as when there are
  // out-of-process iframes), pick the one that should process this event.
  if (target_host->delegate()) {
    target_host =
        target_host->delegate()->GetFocusedRenderWidgetHost(target_host);
  }
  if (!target_host) {
    return;
  }

  target_host->ForwardKeyboardEvent(native_event);
}

void SecureEmbedConnectorImpl::SetFocus(bool focused,
                                        blink::mojom::FocusType focus_type) {
  if (!guest_web_contents_) {
    return;
  }

  auto* child_view = static_cast<RenderWidgetHostViewBase*>(
      guest_web_contents_->GetRenderWidgetHostView());
  if (!child_view) {
    return;
  }

  child_view->host()->SetPageFocus(focused);
  if (focused && (focus_type == blink::mojom::FocusType::kForward ||
                  focus_type == blink::mojom::FocusType::kBackward)) {
    static_cast<RenderViewHostImpl*>(guest_web_contents_->GetRenderViewHost())
        ->SetInitialFocus(
            /*reverse=*/focus_type == blink::mojom::FocusType::kBackward);
  }
}

void SecureEmbedConnectorImpl::OnRenderViewReady() {
  // When the RenderView is ready, update the view in case it has changed.
  UpdateViewForCurrentRenderFrameHost();
}

void SecureEmbedConnectorImpl::OnRenderFrameHostChanged(
    RenderFrameHost* old_host,
    RenderFrameHost* new_host) {
  // When the RenderFrameHost changes, we need to update the view to track
  // the new RenderWidgetHostView associated with the new RenderFrameHost.
  UpdateViewForCurrentRenderFrameHost();
}

void SecureEmbedConnectorImpl::UpdateViewForCurrentRenderFrameHost() {
  CHECK(guest_web_contents_);  // Should not get here w/o WebContents.

  // Get the current RenderWidgetHostView for the guest WebContents.
  auto* base_view = static_cast<RenderWidgetHostViewBase*>(
      guest_web_contents_->GetRenderWidgetHostView());

  if (!base_view) {
    connector_->SetView(nullptr, /*allow_paint_holding=*/false);
    return;
  }

  if (!base_view->IsRenderWidgetHostViewChildFrame()) {
    return;
  }

  auto* child_view = static_cast<RenderWidgetHostViewChildFrame*>(base_view);

  connector_->SetView(child_view, /*allow_paint_holding=*/false);
}

}  // namespace content
