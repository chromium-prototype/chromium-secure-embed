// Copyright 2025 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_SECURE_EMBED_BROWSER_SECURE_EMBED_HOST_H_
#define COMPONENTS_SECURE_EMBED_BROWSER_SECURE_EMBED_HOST_H_

#include "base/component_export.h"
#include "components/secure_embed/common/secure_embed.mojom.h"
#include "mojo/public/cpp/bindings/associated_receiver.h"
#include "mojo/public/cpp/bindings/pending_associated_receiver.h"

namespace content {
class RenderFrameHost;
}  // namespace content

namespace secure_embed {

class COMPONENT_EXPORT(SECURE_EMBED) SecureEmbedHost
    : public mojom::SecureEmbedHost {
 public:
  SecureEmbedHost(
      content::RenderFrameHost* render_frame_host,
      mojo::PendingAssociatedReceiver<mojom::SecureEmbedHost> receiver);
  ~SecureEmbedHost() override;

  SecureEmbedHost(const SecureEmbedHost&) = delete;
  SecureEmbedHost& operator=(const SecureEmbedHost&) = delete;

  static void BindSecureEmbedHost(
      content::RenderFrameHost* render_frame_host,
      mojo::PendingAssociatedReceiver<secure_embed::mojom::SecureEmbedHost>
          receiver);

  // mojom::SecureEmbedHost implementation:
  void Attach(int64_t content_id) override;

  static size_t GetInstanceCountForTesting();

 private:
  mojo::AssociatedReceiver<mojom::SecureEmbedHost> receiver_;

  // Count of all alive instances for testing.
  static size_t instance_count_for_testing_;
};

}  // namespace secure_embed

#endif  // COMPONENTS_SECURE_EMBED_BROWSER_SECURE_EMBED_HOST_H_
