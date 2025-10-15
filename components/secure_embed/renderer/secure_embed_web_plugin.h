// Copyright 2025 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_SECURE_EMBED_RENDERER_SECURE_EMBED_WEB_PLUGIN_H_
#define COMPONENTS_SECURE_EMBED_RENDERER_SECURE_EMBED_WEB_PLUGIN_H_

#include "base/memory/raw_ptr.h"
#include "base/memory/scoped_refptr.h"
#include "cc/layers/solid_color_layer.h"
#include "cc/layers/surface_layer.h"
#include "components/secure_embed/common/secure_embed.mojom.h"
#include "components/viz/common/surfaces/surface_id.h"
#include "mojo/public/cpp/bindings/associated_receiver.h"
#include "mojo/public/cpp/bindings/associated_remote.h"
#include "third_party/blink/public/web/web_plugin.h"

namespace cc {
class SolidColorLayer;
}

namespace blink {
struct WebPluginParams;
class WebPluginContainer;
}  // namespace blink

namespace content {
class RenderFrame;
}  // namespace content

namespace secure_embed {

class SecureEmbedWebPlugin : public blink::WebPlugin,
                             public mojom::SecureEmbed {
 public:
  static SecureEmbedWebPlugin* Create(content::RenderFrame* render_frame,
                                      const blink::WebPluginParams& params);

  SecureEmbedWebPlugin(const SecureEmbedWebPlugin&) = delete;
  SecureEmbedWebPlugin& operator=(const SecureEmbedWebPlugin&) = delete;
  ~SecureEmbedWebPlugin() override;

  // blink::WebPlugin:
  bool Initialize(blink::WebPluginContainer* container) override;
  void Destroy() override;
  blink::WebPluginContainer* Container() const override;
  v8::Local<v8::Object> V8ScriptableObject(v8::Isolate* isolate) override;
  void UpdateAllLifecyclePhases(blink::DocumentUpdateReason reason) override;
  void Paint(cc::PaintCanvas* canvas, const gfx::Rect& rect) override;
  void UpdateGeometry(const gfx::Rect& window_rect,
                      const gfx::Rect& clip_rect,
                      const gfx::Rect& unobscured_rect,
                      bool is_visible) override;
  void UpdateFocus(bool focused, blink::mojom::FocusType focus_type) override;
  void UpdateVisibility(bool is_visible) override;
  blink::WebInputEventResult HandleInputEvent(
      const blink::WebCoalescedInputEvent& event,
      ui::Cursor* cursor) override;
  void DidReceiveResponse(const blink::WebURLResponse& response) override;
  void DidReceiveData(base::span<const char> data) override;
  void DidFinishLoading() override;
  void DidFailLoading(const blink::WebURLError& error) override;

  // mojom::SecureEmbed:
  void OnAttached() override;
  void SetSurfaceId(const viz::SurfaceId& surface_id) override;

 private:
  explicit SecureEmbedWebPlugin(
      mojo::AssociatedRemote<mojom::SecureEmbedHost> host,
      int contents_id);

  void OnSecureEmbedHostDisconnected();

  void EnsurePlaceholderLayer();
  void EnsureSurfaceLayer();

  // The guest contents ID parsed from the `data-content-id` attribute.
  int contents_id_ = -1;

  raw_ptr<blink::WebPluginContainer> container_ = nullptr;

  // Placeholder layer shown when there is no valid SurfaceId.
  scoped_refptr<cc::SolidColorLayer> placeholder_layer_;

  // The SurfaceId to embed, if set. Provided via SetSurfaceId.
  viz::SurfaceId surface_id_;

  // Surface layer for embedding the attached compositor content. If there is no
  // valid `surface_id_` then this layer must not exist.
  scoped_refptr<cc::SurfaceLayer> surface_layer_;

  mojo::AssociatedRemote<mojom::SecureEmbedHost> host_;
  mojo::AssociatedReceiver<mojom::SecureEmbed> receiver_{this};
};

}  // namespace secure_embed

#endif  // COMPONENTS_SECURE_EMBED_RENDERER_SECURE_EMBED_WEB_PLUGIN_H_
