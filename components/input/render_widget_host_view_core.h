// Copyright 2025 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_INPUT_RENDER_WIDGET_HOST_VIEW_CORE_H_
#define COMPONENTS_INPUT_RENDER_WIDGET_HOST_VIEW_CORE_H_

#include <string>

#include "base/component_export.h"
#include "components/input/render_widget_host_view_input.h"
#include "content/public/browser/render_widget_host.h"
#include "content/public/browser/render_widget_host_view.h"
#include "ui/gfx/geometry/rect.h"

namespace content {
class CrossProcessFrameConnectorBase;
}  // namespace content

namespace input {

class COMPONENT_EXPORT(INPUT) RenderWidgetHostViewCore
    : public RenderWidgetHostViewInput,
      public content::RenderWidgetHostView {
 public:
  ~RenderWidgetHostViewCore() override = default;

  virtual void SetFrameConnector(
      content::CrossProcessFrameConnectorBase* frame_connector);

  virtual content::RenderWidgetHost::InputEventObserver*
  GetInputTransferHandlerObserver();

  // Updates the tooltip text and its position and displays the requested
  // tooltip on the screen. The |bounds| parameter corresponds to the bounds of
  // the renderer-side element (in widget-relative DIPS) on which the tooltip
  // should appear to be anchored.
  virtual void UpdateTooltipFromKeyboard(const std::u16string& tooltip_text,
                                         const gfx::Rect& bounds) {}

  // Hides tooltips that are still visible and were triggered from a keypress.
  // Doesn't impact tooltips that were triggered from the cursor.
  virtual void ClearKeyboardTriggeredTooltip() {}

  virtual bool IsChildView() const;
};

}  // namespace input

#endif  // COMPONENTS_INPUT_RENDER_WIDGET_HOST_VIEW_CORE_H_
