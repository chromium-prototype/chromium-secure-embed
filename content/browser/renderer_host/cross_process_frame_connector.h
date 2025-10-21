// Copyright 2014 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_BROWSER_RENDERER_HOST_CROSS_PROCESS_FRAME_CONNECTOR_H_
#define CONTENT_BROWSER_RENDERER_HOST_CROSS_PROCESS_FRAME_CONNECTOR_H_

#include <stdint.h>

#include "base/gtest_prod_util.h"
#include "base/memory/raw_ptr.h"
#include "cc/input/touch_action.h"
#include "components/input/child_frame_input_helper.h"
#include "content/browser/renderer_host/cross_process_frame_connector_base.h"
#include "content/common/content_export.h"
#include "content/public/browser/visibility.h"
#include "third_party/blink/public/common/frame/frame_visual_properties.h"
#include "third_party/blink/public/mojom/frame/intrinsic_sizing_info.mojom-forward.h"
#include "third_party/blink/public/mojom/frame/lifecycle.mojom.h"
#include "third_party/blink/public/mojom/frame/viewport_intersection_state.mojom.h"
#include "third_party/blink/public/mojom/input/input_event_result.mojom-shared.h"
#include "third_party/blink/public/mojom/input/pointer_lock_result.mojom-shared.h"
#include "ui/display/screen_infos.h"
#include "ui/gfx/geometry/rect.h"

namespace blink {
struct FrameVisualProperties;
}  // namespace blink

namespace cc {
class RenderFrameMetadata;
}

namespace input {
class RenderWidgetHostViewInput;
}  // namespace input

namespace ui {
class Cursor;
}

namespace viz {
class SurfaceInfo;
}  // namespace viz

namespace content {
class RenderFrameHostImpl;
class RenderFrameProxyHost;
class RenderWidgetHostViewBase;
class RenderWidgetHostViewChildFrame;

// CrossProcessFrameConnector provides the platform view abstraction for
// RenderWidgetHostViewChildFrame allowing RWHVChildFrame to remain ignorant
// of RenderFrameHost.
//
// The RenderWidgetHostView of an out-of-process child frame needs to
// communicate with the RenderFrameProxyHost representing this frame in the
// process of the parent frame. For example, assume you have this page:
//
//   -----------------
//   | frame 1       |
//   |  -----------  |
//   |  | frame 2 |  |
//   |  -----------  |
//   -----------------
//
// If frames 1 and 2 are in process A and B, there are 4 hosts:
//   A1 - RFH for frame 1 in process A
//   B1 - RFPH for frame 1 in process B
//   A2 - RFPH for frame 2 in process A
//   B2 - RFH for frame 2 in process B
//
// B2, having a parent frame in a different process, will have a
// RenderWidgetHostViewChildFrame. This RenderWidgetHostViewChildFrame needs
// to communicate with A2 because the embedding frame represents the platform
// that the child frame is rendering into -- it needs information necessary for
// compositing child frame textures, and also can pass platform messages such as
// view resizing. CrossProcessFrameConnector bridges between B2's
// RenderWidgetHostViewChildFrame and A2 to allow for this communication.
// (Note: B1 is only mentioned for completeness. It is not needed in this
// example.)
//
// CrossProcessFrameConnector objects are owned by the RenderFrameProxyHost
// in the child frame's RenderFrameHostManager corresponding to the parent's
// SiteInstance, A2 in the picture above. When a child frame navigates in a new
// process, SetView() is called to update to the new view.
//
class CONTENT_EXPORT CrossProcessFrameConnector
    : public CrossProcessFrameConnectorBase {
 public:
  // |frame_proxy_in_parent_renderer| corresponds to A2 in the example above.
  explicit CrossProcessFrameConnector(
      RenderFrameProxyHost* frame_proxy_in_parent_renderer);

  CrossProcessFrameConnector(const CrossProcessFrameConnector&) = delete;
  CrossProcessFrameConnector& operator=(const CrossProcessFrameConnector&) =
      delete;

  ~CrossProcessFrameConnector() override;

  // Returns the parent RenderWidgetHostView or nullptr if it doesn't have one.
  RenderWidgetHostViewBase* GetParentRenderWidgetHostView() override;

  // Returns the view for the top-level frame under the same WebContents.
  RenderWidgetHostViewBase* GetRootRenderWidgetHostView() override;

  void SendIntrinsicSizingInfoToParent(
      blink::mojom::IntrinsicSizingInfoPtr) override;

  void EnableAutoResize(const gfx::Size& min_size,
                        const gfx::Size& max_size) override;

  void DisableAutoResize() override;

  void DidUpdateVisualProperties(
      const cc::RenderFrameMetadata& metadata) override;

 protected:
  // Gets the current RenderFrameHost for the
  // |frame_proxy_in_parent_renderer_|'s (i.e., the child frame's)
  // FrameTreeNode. This corresponds to B2 in the class-level comment
  // above for CrossProcessFrameConnector.
  RenderFrameHostImpl* current_child_frame_host() const override;

  void ReportFrameSinkId(const viz::FrameSinkId& frame_sink_id,
                         bool allow_paint_holding) override;
  void ReportChildProcessGone() override;
  void ReportBadVisualProperties() override;
  bool NotifyInnerWebContentsVisibilityChanged() override;
  void RequestTabReload() override;

  // The RenderFrameProxyHost that routes messages to the parent frame's
  // renderer process.
  // Can be nullptr in tests.
  raw_ptr<RenderFrameProxyHost> frame_proxy_in_parent_renderer_;

};

}  // namespace content

#endif  // CONTENT_BROWSER_RENDERER_HOST_CROSS_PROCESS_FRAME_CONNECTOR_H_
