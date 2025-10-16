// Copyright 2025 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/input/render_widget_host_view_core.h"

#include "base/notreached.h"
#include "content/public/browser/cross_process_frame_connector_base.h"

namespace input {

void RenderWidgetHostViewCore::SetFrameConnector(
    content::CrossProcessFrameConnectorBase* frame_connector) {
  NOTREACHED();
}

content::RenderWidgetHost::InputEventObserver*
RenderWidgetHostViewCore::GetInputTransferHandlerObserver() {
  return nullptr;
}

bool RenderWidgetHostViewCore::IsChildView() const {
  return false;
}

}  // namespace input
