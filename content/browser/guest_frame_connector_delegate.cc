// Copyright 2025 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/browser/guest_frame_connector_delegate.h"

#include "base/notimplemented.h"
#include "content/browser/renderer_host/render_frame_host_impl.h"
#include "content/browser/renderer_host/render_widget_host_view_base.h"
#include "content/browser/web_contents/web_contents_impl.h"
#include "content/public/browser/render_process_host.h"
#include "content/public/browser/secure_embed_delegate.h"
#include "content/public/browser/web_contents.h"

namespace content {

GuestFrameConnectorDelegate::GuestFrameConnectorDelegate(
    WebContents* guest_web_contents,
    GuestFrame::Delegate* guest_delegate)
    : guest_web_contents_(guest_web_contents->GetWeakPtr()),
      guest_delegate_(guest_delegate) {}

GuestFrameConnectorDelegate::~GuestFrameConnectorDelegate() = default;

void GuestFrameConnectorDelegate::DidUpdateVisualProperties(
    const cc::RenderFrameMetadata& metadata) {
  NOTIMPLEMENTED();
}

void GuestFrameConnectorDelegate::DisableAutoResize() {
  NOTIMPLEMENTED();
}

void GuestFrameConnectorDelegate::EnableAutoResize(const gfx::Size& min_size,
                                                   const gfx::Size& max_size) {
  NOTIMPLEMENTED();
}

RenderFrameHostImpl* GuestFrameConnectorDelegate::GetChildRenderFrameHost()
    const {
  return static_cast<WebContentsImpl*>(guest_web_contents_.get())
      ->GetPrimaryMainFrame();
}

RenderWidgetHostViewBase* GuestFrameConnectorDelegate::GetParentView() {
  // The guest WebContents may have already been destroyed during shutdown when
  // clearing the view on CPFC.
  if (!guest_web_contents_) {
    return nullptr;
  }

  // TODO(secure-embed): Unclear if we should support parent/root view access.
  return static_cast<RenderWidgetHostViewBase*>(
      guest_web_contents_->GetSecureEmbedDelegate()
          ->GetEmbedderWebContents()
          ->GetRenderWidgetHostView());
}

RenderWidgetHostViewBase* GuestFrameConnectorDelegate::GetRootView() {
  // TODO(secure-embed): Unclear if we should support parent/root view access.
  // For now we'll just return the parent.
  return GetParentView();
}

RenderProcessHost* GuestFrameConnectorDelegate::GetParentProcess() const {
  WebContents* embedder_web_contents =
      guest_web_contents_->GetSecureEmbedDelegate()->GetEmbedderWebContents();
  if (!embedder_web_contents) {
    return nullptr;
  }

  return embedder_web_contents->GetPrimaryMainFrame()->GetProcess();
}

Visibility GuestFrameConnectorDelegate::GetEmbedderVisibility() {
  if (!guest_web_contents_) {
    return Visibility::HIDDEN;
  }

  WebContents* embedder_web_contents =
      guest_web_contents_->GetSecureEmbedDelegate()->GetEmbedderWebContents();
  if (!embedder_web_contents) {
    return Visibility::HIDDEN;
  }

  return embedder_web_contents->GetVisibility();
}

void GuestFrameConnectorDelegate::ChildProcessGone() {
  NOTIMPLEMENTED();
}

void GuestFrameConnectorDelegate::NeedsReload() {
  NOTIMPLEMENTED();
}

bool GuestFrameConnectorDelegate::VisibilityChanged(
    RenderWidgetHostViewChildFrame* view,
    blink::mojom::FrameVisibility visibility) {
  NOTIMPLEMENTED();
  return false;
}

void GuestFrameConnectorDelegate::SendIntrinsicSizingInfoToParent(
    blink::mojom::IntrinsicSizingInfoPtr info) {
  NOTREACHED();
}

void GuestFrameConnectorDelegate::SendScreenRects() {
  NOTIMPLEMENTED();
}

void GuestFrameConnectorDelegate::SetFrameSinkId(const viz::FrameSinkId& id,
                                                 bool allow_paint_holding) {
  guest_delegate_->SetFrameSinkId(id);
}

}  // namespace content
