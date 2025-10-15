// Copyright 2025 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_PUBLIC_TEST_TEST_SURFACE_UTILS_H_
#define CONTENT_PUBLIC_TEST_TEST_SURFACE_UTILS_H_

#include "components/viz/common/surfaces/frame_sink_id.h"

namespace viz {
class HostFrameSinkManager;
}

namespace content {

// TODO(secure-embed): This file is here to expose surface utils needed for
// verifying SecureEmbed functionality in browser tests with a test surface.
// Once we have tests that embed attached WebContents, this should be deleted.

viz::FrameSinkId AllocateFrameSinkIdForTesting();

viz::HostFrameSinkManager* GetHostFrameSinkManagerForTesting();

}  // namespace content

#endif  // CONTENT_PUBLIC_TEST_TEST_SURFACE_UTILS_H_
