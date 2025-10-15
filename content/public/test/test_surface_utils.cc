// Copyright 2025 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/browser/compositor/surface_utils.h"

#include "build/build_config.h"
#include "components/viz/host/host_frame_sink_manager.h"
#include "content/browser/compositor/image_transport_factory.h"
#include "ui/compositor/compositor.h"  // nogncheck

namespace content {

viz::FrameSinkId AllocateFrameSinkIdForTesting() {
  return content::AllocateFrameSinkId();
}

viz::HostFrameSinkManager* GetHostFrameSinkManagerForTesting() {
  return content::GetHostFrameSinkManager();
}

}  // namespace content
