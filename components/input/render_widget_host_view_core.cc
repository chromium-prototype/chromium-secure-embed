// Copyright 2025 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/input/render_widget_host_view_core.h"

namespace input {

content::RenderWidgetHost::InputEventObserver*
RenderWidgetHostViewCore::GetInputTransferHandlerObserver() {
  return nullptr;
}

}  // namespace input
