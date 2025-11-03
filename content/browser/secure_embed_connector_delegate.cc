// Copyright 2025 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/browser/secure_embed_connector_delegate.h"

#include "base/notimplemented.h"
#include "content/browser/renderer_host/render_frame_host_impl.h"
#include "content/browser/renderer_host/render_widget_host_view_base.h"
#include "content/browser/web_contents/web_contents_impl.h"
#include "content/public/browser/render_process_host.h"
#include "content/public/browser/secure_embed_connector.h"
#include "content/public/browser/web_contents.h"

namespace content {

SecureEmbedConnectorDelegate::SecureEmbedConnectorDelegate(
    WebContents* guest_web_contents,
    SecureEmbedConnector* connector)
    : guest_web_contents_(guest_web_contents->GetWeakPtr()),
      secure_embed_connector_(connector) {}

SecureEmbedConnectorDelegate::~SecureEmbedConnectorDelegate() = default;

void SecureEmbedConnectorDelegate::DidUpdateVisualProperties(
    const cc::RenderFrameMetadata& metadata) {
  NOTIMPLEMENTED();
}

void SecureEmbedConnectorDelegate::DisableAutoResize() {
  NOTIMPLEMENTED();
}

void SecureEmbedConnectorDelegate::EnableAutoResize(const gfx::Size& min_size,
                                                   const gfx::Size& max_size) {
  NOTIMPLEMENTED();
}

RenderFrameHostImpl* SecureEmbedConnectorDelegate::GetChildRenderFrameHost()
    const {
  return static_cast<WebContentsImpl*>(guest_web_contents_.get())
      ->GetPrimaryMainFrame();
}

RenderWidgetHostViewBase* SecureEmbedConnectorDelegate::GetParentView() {
  // The guest WebContents may have already been destroyed during shutdown when
  // clearing the view on CPFC.
  WebContents* embedder_web_contents = secure_embed_connector_->GetEmbedderWebContents();
  if (!embedder_web_contents) {
    return nullptr;
  }

  return static_cast<RenderWidgetHostViewBase*>(
      embedder_web_contents->GetRenderWidgetHostView());
}

RenderWidgetHostViewBase* SecureEmbedConnectorDelegate::GetRootView() {
  // TODO(secure-embed): This is returning the parent instead of the root right
  // now. Need to look through the callers and determine what the right behavior
  // is.
  return GetParentView();
}

RenderProcessHost* SecureEmbedConnectorDelegate::GetParentProcess() const {
  WebContents* embedder_web_contents = secure_embed_connector_->GetEmbedderWebContents();
  if (!embedder_web_contents) {
    return nullptr;
  }

  return embedder_web_contents->GetPrimaryMainFrame()->GetProcess();
}

Visibility SecureEmbedConnectorDelegate::GetEmbedderVisibility() {
  if (!guest_web_contents_) {
    return Visibility::HIDDEN;
  }

  WebContents* embedder_web_contents = secure_embed_connector_->GetEmbedderWebContents();
  if (!embedder_web_contents) {
    return Visibility::HIDDEN;
  }

  return embedder_web_contents->GetVisibility();
}

void SecureEmbedConnectorDelegate::ChildProcessGone() {
  NOTIMPLEMENTED();
}

void SecureEmbedConnectorDelegate::NeedsReload() {
  NOTIMPLEMENTED();
}

bool SecureEmbedConnectorDelegate::VisibilityChanged(
    RenderWidgetHostViewChildFrame* view,
    blink::mojom::FrameVisibility visibility) {
  NOTIMPLEMENTED();
  return false;
}

void SecureEmbedConnectorDelegate::SendIntrinsicSizingInfoToParent(
    blink::mojom::IntrinsicSizingInfoPtr info) {
  NOTREACHED();
}

void SecureEmbedConnectorDelegate::SendScreenRects() {
  NOTIMPLEMENTED();
}

void SecureEmbedConnectorDelegate::SetFrameSinkId(const viz::FrameSinkId& id,
                                                 bool allow_paint_holding) {
  if (secure_embed_connector_->GetDelegate()) {
    // TODO(secure-embed): Support paint holding?
    secure_embed_connector_->GetDelegate()->SetFrameSinkId(id);
  }
}

}  // namespace content
