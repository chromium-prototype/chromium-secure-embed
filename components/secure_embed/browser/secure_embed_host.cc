// Copyright 2025 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/secure_embed/browser/secure_embed_host.h"

#include "base/logging.h"
#include "content/public/browser/render_frame_host.h"

namespace secure_embed {

// static
size_t SecureEmbedHost::instance_count_for_testing_ = 0;

SecureEmbedHost::SecureEmbedHost(
    content::RenderFrameHost* render_frame_host,
    mojo::PendingAssociatedReceiver<mojom::SecureEmbedHost> receiver)
    : receiver_(this, std::move(receiver)) {
  ++instance_count_for_testing_;

  // Set up disconnect handler to delete this when the mojo connection is lost
  // (e.g., when the SecureEmbedWebPlugin is destroyed).
  receiver_.set_disconnect_handler(base::BindOnce(
      [](SecureEmbedHost* host) { delete host; }, base::Unretained(this)));
}

SecureEmbedHost::~SecureEmbedHost() {
  --instance_count_for_testing_;
}

void SecureEmbedHost::BindSecureEmbedHost(
    content::RenderFrameHost* render_frame_host,
    mojo::PendingAssociatedReceiver<secure_embed::mojom::SecureEmbedHost>
        receiver) {
  new SecureEmbedHost(render_frame_host, std::move(receiver));
}

void SecureEmbedHost::Attach(int64_t content_id) {
  // TODO(secure-embed): Implement content_id handling.
}

// static
size_t SecureEmbedHost::GetInstanceCountForTesting() {
  return instance_count_for_testing_;
}

}  // namespace secure_embed
