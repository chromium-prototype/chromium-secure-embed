// Copyright 2025 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/command_line.h"
#include "base/run_loop.h"
#include "base/test/bind.h"
#include "base/test/run_until.h"
#include "components/guest_contents/browser/guest_contents_handle.h"
#include "components/secure_embed/browser/secure_embed_host.h"
#include "content/browser/renderer_host/render_frame_host_impl.h"
#include "content/public/browser/browser_context.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/render_widget_host.h"
#include "content/public/browser/render_widget_host_observer.h"
#include "content/public/browser/render_widget_host_view.h"
#include "content/public/browser/secure_embed_connector.h"
#include "content/public/browser/visibility.h"
#include "content/public/browser/web_contents.h"
#include "content/public/common/result_codes.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "content/public/test/content_browser_test.h"
#include "content/public/test/content_browser_test_utils.h"
#include "content/public/test/no_renderer_crashes_assertion.h"
#include "content/public/test/text_input_test_utils.h"
#include "content/shell/browser/shell.h"
#include "net/dns/mock_host_resolver.h"
#include "third_party/blink/public/mojom/frame/lifecycle.mojom.h"
#include "third_party/skia/include/core/SkBitmap.h"
#include "third_party/skia/include/core/SkColor.h"
#include "ui/gfx/geometry/rect.h"

namespace secure_embed {

namespace {
constexpr char kAttachHarnessUrl[] =
    "/secure_embed/single_embed_attach_harness.html";
constexpr char kRedBoxUrl[] = "/secure_embed/red_box.html";
constexpr char kBlueBoxUrl[] = "/secure_embed/blue_box.html";
constexpr char kEmptyUrl[] = "/secure_embed/empty.html";

// Observer that waits for a RenderWidgetHost visibility change.
class RenderWidgetHostVisibilityWaiter
    : public content::RenderWidgetHostObserver {
 public:
  RenderWidgetHostVisibilityWaiter(content::RenderWidgetHost* rwh,
                                   bool expected_visibility)
      : expected_visibility_(expected_visibility) {
    rwh->AddObserver(this);
    rwh_ = rwh;
  }

  ~RenderWidgetHostVisibilityWaiter() override { rwh_->RemoveObserver(this); }

  void Wait() {
    if (!satisfied_) {
      run_loop_.Run();
    }
  }

  // RenderWidgetHostObserver:
  void RenderWidgetHostVisibilityChanged(content::RenderWidgetHost* widget_host,
                                         bool became_visible) override {
    if (became_visible == expected_visibility_) {
      satisfied_ = true;
      run_loop_.Quit();
    }
  }

  void RenderWidgetHostDestroyed(
      content::RenderWidgetHost* widget_host) override {
    rwh_->RemoveObserver(this);
    rwh_ = nullptr;
  }

 private:
  bool expected_visibility_;
  bool satisfied_ = false;
  base::RunLoop run_loop_;
  raw_ptr<content::RenderWidgetHost> rwh_ = nullptr;
};
}  // namespace

class SecureEmbedBrowserTest : public content::ContentBrowserTest {
 public:
  // RAII helper for monitoring data-content-id attribute changes.
  class ScopedDataContentIdMonitor {
   public:
    ScopedDataContentIdMonitor(content::WebContents* web_contents,
                               bool expect_zero_change)
        : web_contents_(web_contents), expect_zero_change_(expect_zero_change) {
      EXPECT_TRUE(
          content::ExecJs(web_contents_, "startMonitoringDataContentId();"));
    }

    ~ScopedDataContentIdMonitor() {
      bool zero_detected =
          content::EvalJs(web_contents_, "wasZeroChangeDetected()")
              .ExtractBool();

      if (expect_zero_change_) {
        // Wait for zero change if we expect it but haven't seen it yet.
        if (!zero_detected) {
          EXPECT_TRUE(base::test::RunUntil([&]() {
            return content::EvalJs(web_contents_, "wasZeroChangeDetected()")
                .ExtractBool();
          })) << "data-content-id was not set to 0 as expected";
        }
      } else {
        EXPECT_FALSE(zero_detected)
            << "data-content-id was unexpectedly set to 0 (DetachedByHost "
               "incorrectly triggered)";
      }

      EXPECT_TRUE(
          content::ExecJs(web_contents_, "stopMonitoringDataContentId();"));
    }

    ScopedDataContentIdMonitor(const ScopedDataContentIdMonitor&) = delete;
    ScopedDataContentIdMonitor& operator=(const ScopedDataContentIdMonitor&) =
        delete;

   private:
    raw_ptr<content::WebContents> web_contents_;
    bool expect_zero_change_;
  };

  void SetUpCommandLine(base::CommandLine* command_line) override {
    content::ContentBrowserTest::SetUpCommandLine(command_line);
    // Enable pixel output in tests to allow CopyFromSurface to capture actual
    // rendered content instead of returning empty/black bitmaps. Note that we
    // force a device scale factor of 1.5 to ensure scaling is handled
    // correctly.
    EnablePixelOutput(1.5f);
  }

  void SetUpOnMainThread() override {
    content::ContentBrowserTest::SetUpOnMainThread();
    host_resolver()->AddRule("b.com", "127.0.0.1");
    embedded_test_server()->ServeFilesFromSourceDirectory(
        "components/test/data");
    ASSERT_TRUE(embedded_test_server()->Start());
  }

  content::WebContents* web_contents() { return shell()->web_contents(); }

  void NavigateToTestUrl(const char* url) {
    const GURL test_url = embedded_test_server()->GetURL(url);
    ASSERT_TRUE(NavigateToURL(web_contents(), test_url));
  }

  int CountEmbedElementsInPage() {
    return content::EvalJs(web_contents(), "document.embeds.length")
        .ExtractInt();
  }

  bool WaitForHostCreation() {
    // Host creation is asynchronous because it requires mojo IPC between
    // the renderer process (SecureEmbedWebPlugin) and browser process
    // (SecureEmbedHost). Poll until the expected number of hosts are created.
    return base::test::RunUntil(
        [&]() { return SecureEmbedHost::GetInstanceCountForTesting() >= 1; });
  }

  bool WaitForHostAttachment(size_t expected_count) {
    // After host creation, the attachment happens asychronously when
    // SecureEmbedHost::AttachConnector() is called. Poll until the expected
    // number of hosts are attached.
    return base::test::RunUntil([&]() {
      return SecureEmbedHost::GetAttachedInstanceCountForTesting() >=
             expected_count;
    });
  }

  // Helper to verify pixel colors at specific locations within a capture rect.
  // Polls repeatedly until all expected colors match or timeout occurs.
  // The points in `expected_colors` should be specified in device independent
  // pixels relative to the capture_rect origin. They will be scaled by the
  // device scale factor internally since the bitmap from CopyFromSurface is in
  // physical pixels. Note that CopyFromSurface itself takes the capture_rect in
  // device independent pixels. Returns true if all pixels match expected
  // colors.
  bool VerifyPixelColors(
      content::RenderWidgetHostView* rwhv,
      const gfx::Rect& capture_rect,
      const std::vector<std::pair<gfx::Point, SkColor>>& expected_colors) {
    float device_scale_factor = rwhv->GetDeviceScaleFactor();
    bool all_pixels_correct = false;
    return base::test::RunUntil([&]() {
      base::RunLoop run_loop;

      // Passing in gfx::Size() to avoid scaling the captured rect, specified
      // via capture_rect, to a different size if the underlying device is doing
      // pixel scaling.
      rwhv->CopyFromSurface(
          capture_rect, gfx::Size(),
          base::BindLambdaForTesting(
              [quit_closure = run_loop.QuitClosure(), &all_pixels_correct,
               device_scale_factor, &expected_colors](const SkBitmap& bitmap) {
                all_pixels_correct = false;

                if (bitmap.drawsNothing()) {
                  std::move(quit_closure).Run();
                  return;
                }

                bool all_match = true;
                for (const auto& [point, expected_color] : expected_colors) {
                  // Scale the point by device scale factor to get physical
                  // pixel coordinates in the bitmap.
                  int physical_x =
                      static_cast<int>(point.x() * device_scale_factor);
                  int physical_y =
                      static_cast<int>(point.y() * device_scale_factor);
                  SkColor actual_color =
                      bitmap.getColor(physical_x, physical_y);
                  if (actual_color != expected_color) {
                    all_match = false;
                    break;
                  }
                }

                if (all_match) {
                  all_pixels_correct = true;
                }

                std::move(quit_closure).Run();
              }));

      run_loop.Run();
      return all_pixels_correct;
    });
  }

  // Navigate the main WebContents to the attach harness page.
  void NavigateToAttachHarness() {
    const GURL harness_url = embedded_test_server()->GetURL(kAttachHarnessUrl);
    ASSERT_TRUE(NavigateToURL(web_contents(), harness_url));
  }

  std::unique_ptr<content::WebContents> CreateGuestWebContents() {
    content::WebContents::CreateParams create_params(
        shell()->web_contents()->GetBrowserContext());
    std::unique_ptr<content::WebContents> guest_contents =
        content::WebContents::Create(create_params);
    EXPECT_NE(guest_contents, nullptr);
    return guest_contents;
  }

  // Navigate guest WebContents to a URL. Wait for load to complete.
  void NavigateGuestToUrl(content::WebContents* guest_contents,
                          const std::string& path) {
    const GURL guest_url = embedded_test_server()->GetURL(path);
    ASSERT_TRUE(NavigateToURL(guest_contents, guest_url));
    ASSERT_TRUE(content::WaitForLoadStop(guest_contents));
  }

  // Attach a guest to an embed element and wait for SecureEmbedHost creation.
  void AttachGuestToEmbed(content::WebContents* guest_contents,
                          std::optional<std::string> parent_id = std::nullopt) {
    AttachGuestToEmbedWithId(guest_contents, std::nullopt, parent_id);
  }

  // Attach a guest to an embed element with an optional ID and parent element.
  void AttachGuestToEmbedWithId(
      content::WebContents* guest_contents,
      std::optional<std::string> embed_id,
      std::optional<std::string> parent_id = std::nullopt) {
    guest_contents::GuestContentsHandle* guest_handle =
        guest_contents::GuestContentsHandle::CreateForWebContents(
            guest_contents);
    ASSERT_NE(guest_handle, nullptr);
    std::string script =
        "createEmbed(" + base::NumberToString(guest_handle->id());
    if (embed_id.has_value()) {
      script += ", '" + embed_id.value() + "'";
    } else {
      script += ", null";
    }
    if (parent_id.has_value()) {
      script += ", '" + parent_id.value() + "'";
    }
    script += ");";
    ASSERT_TRUE(content::ExecJs(web_contents(), script));
    ASSERT_TRUE(WaitForHostAttachment(1));
    EXPECT_NE(guest_contents->GetSecureEmbedConnector(), nullptr);
  }

  // Get the guest handle for the provided guest WebContents.
  int GetGuestHandleId(content::WebContents* guest_contents) {
    guest_contents::GuestContentsHandle* guest_handle =
        guest_contents::GuestContentsHandle::CreateForWebContents(
            guest_contents);
    EXPECT_NE(guest_handle, nullptr);
    return guest_handle ? guest_handle->id() : -1;
  }

  // Setup guest with harness navigation and content loading.
  std::unique_ptr<content::WebContents> SetupHarnessAndGuestWithContent(
      const std::string& guest_path) {
    NavigateToAttachHarness();
    auto guest_contents = CreateGuestWebContents();
    NavigateGuestToUrl(guest_contents.get(), guest_path);
    return guest_contents;
  }

  // Verify box rendering at standard location with specified color. The box is
  // 50x50 CSS pixels at position (10, 10).
  void VerifyBoxRendering(SkColor box_color) {
    auto* rwhv = web_contents()->GetRenderWidgetHostView();
    ASSERT_NE(rwhv, nullptr);

    gfx::Rect capture_rect(0, 0, 70, 70);
    std::vector<std::pair<gfx::Point, SkColor>> expected_colors = {
        {gfx::Point(0, 0), SK_ColorWHITE},
        {gfx::Point(10, 10), box_color},
        {gfx::Point(59, 59), box_color},
        {gfx::Point(60, 60), SK_ColorWHITE},
    };
    EXPECT_TRUE(VerifyPixelColors(rwhv, capture_rect, expected_colors));
  }

  // Waits for the element with `element_id` to be focused.
  void WaitForFocus(const content::ToRenderFrameHost& rfh,
                    const std::string& element_id) {
    ASSERT_TRUE(base::test::RunUntil([&]() {
      return element_id == content::EvalJs(rfh, "document.activeElement.id");
    }));
  }

  // Create a container div with optional styling.
  void CreateContainer(const std::string& container_id = "embed-container",
                       const std::string& additional_style = "") {
    std::string script =
        "const container = document.createElement('div');"
        "container.id = '" +
        container_id + "';";
    if (!additional_style.empty()) {
      script += "container.style.cssText = '" + additional_style + "';";
    }
    script += "document.body.appendChild(container);";
    ASSERT_TRUE(content::ExecJs(web_contents(), script));
  }

  // Add a cross-site iframe to guest contents and wait for load.
  // Returns the iframe RenderFrameHost.
  content::RenderFrameHost* AddCrossSiteIframeToGuest(
      content::WebContents* guest_contents,
      const std::string& cross_site_host = "b.com",
      const std::string& iframe_path = kRedBoxUrl) {
    GURL iframe_url =
        embedded_test_server()->GetURL(cross_site_host, iframe_path);
    std::string create_iframe_script =
        "const iframe = document.createElement('iframe');"
        "iframe.src = '" +
        iframe_url.spec() +
        "';"
        "document.body.appendChild(iframe);";
    EXPECT_TRUE(content::ExecJs(guest_contents, create_iframe_script));
    EXPECT_TRUE(content::WaitForLoadStop(guest_contents));

    content::RenderFrameHost* main_frame =
        guest_contents->GetPrimaryMainFrame();
    content::RenderFrameHost* iframe = ChildFrameAt(main_frame, 0);
    EXPECT_NE(iframe, nullptr);
    if (iframe) {
      EXPECT_NE(main_frame->GetProcess(), iframe->GetProcess());
    }
    return iframe;
  }

  // Setup guest with harness, content loading, and cross-site iframe.
  std::unique_ptr<content::WebContents> SetupHarnessAndGuestWithIframe(
      const std::string& guest_path = kEmptyUrl,
      const std::string& cross_site_host = "b.com",
      const std::string& iframe_path = kRedBoxUrl) {
    NavigateToAttachHarness();
    auto guest_contents = CreateGuestWebContents();
    NavigateGuestToUrl(guest_contents.get(), guest_path);
    AddCrossSiteIframeToGuest(guest_contents.get(), cross_site_host,
                              iframe_path);
    return guest_contents;
  }
};

IN_PROC_BROWSER_TEST_F(SecureEmbedBrowserTest,
                       PluginRendersRedSquare_AttachBeforeLoad) {
  NavigateToAttachHarness();
  auto guest_contents = CreateGuestWebContents();

  // Attach before loading content.
  AttachGuestToEmbed(guest_contents.get());

  // Now navigate the guest to the red box page after attachment.
  NavigateGuestToUrl(guest_contents.get(), kRedBoxUrl);

  VerifyBoxRendering(SK_ColorRED);
}

IN_PROC_BROWSER_TEST_F(SecureEmbedBrowserTest,
                       PluginRendersRedSquare_AttachAfterLoad) {
  // Load content before attaching.
  auto guest_contents = SetupHarnessAndGuestWithContent(kRedBoxUrl);
  AttachGuestToEmbed(guest_contents.get());

  VerifyBoxRendering(SK_ColorRED);
}

IN_PROC_BROWSER_TEST_F(SecureEmbedBrowserTest, Crash) {
  auto guest_contents = SetupHarnessAndGuestWithContent(kRedBoxUrl);
  AttachGuestToEmbed(guest_contents.get());

  VerifyBoxRendering(SK_ColorRED);

  // Simulate a crash.
  content::ScopedAllowRendererCrashes testing_crashes_here(
      guest_contents->GetPrimaryMainFrame());
  guest_contents->GetPrimaryMainFrame()->GetProcess()->Shutdown(
      content::RESULT_CODE_KILLED);
  EXPECT_TRUE(
      base::test::RunUntil([&]() { return guest_contents->IsCrashed(); }));
  // The crashed frame gets drawn with a gray background, with an image
  // in the middle (which doesn't seem configured for tests). The gray in
  // question is a bit different than SK_ColorGRAY.
  gfx::Rect capture_rect(0, 0, 10, 10);
  std::vector<std::pair<gfx::Point, SkColor>> expected_colors = {
      {gfx::Point(0, 0), SkColors::kGray.toSkColor()},
      {gfx::Point(5, 5), SkColors::kGray.toSkColor()},
  };
  auto* rwhv = web_contents()->GetRenderWidgetHostView();
  ASSERT_NE(rwhv, nullptr);
  EXPECT_TRUE(VerifyPixelColors(rwhv, capture_rect, expected_colors));
}

IN_PROC_BROWSER_TEST_F(SecureEmbedBrowserTest,
                       PluginRendersRedSquare_MultipleAttachCalls) {
  NavigateToAttachHarness();

  // Create and load both guests.
  auto guest_contents_red = CreateGuestWebContents();
  NavigateGuestToUrl(guest_contents_red.get(), kRedBoxUrl);
  int guest_id_red = GetGuestHandleId(guest_contents_red.get());

  auto guest_contents_blue = CreateGuestWebContents();
  NavigateGuestToUrl(guest_contents_blue.get(), kBlueBoxUrl);
  int guest_id_blue = GetGuestHandleId(guest_contents_blue.get());

  // Attach the red guest first.
  AttachGuestToEmbed(guest_contents_red.get());
  VerifyBoxRendering(SK_ColorRED);

  ScopedDataContentIdMonitor monitor(web_contents(),
                                     /*expect_zero_change=*/false);

  // Swap to the blue guest by changing the data-content-id attribute.
  std::string script_blue =
      "setDataContentId(" + base::NumberToString(guest_id_blue) + ");";
  ASSERT_TRUE(content::ExecJs(web_contents(), script_blue));
  VerifyBoxRendering(SK_ColorBLUE);
  EXPECT_EQ(guest_contents_red->GetSecureEmbedConnector(), nullptr);

  // Swap back to red guest.
  std::string script_red_again =
      "setDataContentId(" + base::NumberToString(guest_id_red) + ");";
  ASSERT_TRUE(content::ExecJs(web_contents(), script_red_again));
  VerifyBoxRendering(SK_ColorRED);
  EXPECT_EQ(guest_contents_blue->GetSecureEmbedConnector(), nullptr);
}

IN_PROC_BROWSER_TEST_F(SecureEmbedBrowserTest, VisibilityHiddenStopsRendering) {
  auto guest_contents = SetupHarnessAndGuestWithContent(kRedBoxUrl);
  AttachGuestToEmbed(guest_contents.get());
  VerifyBoxRendering(SK_ColorRED);

  // Set visibility to hidden - guest should stop rendering.
  ASSERT_TRUE(content::ExecJs(
      web_contents(),
      "document.querySelector('embed').style.visibility = 'hidden';"));
  VerifyBoxRendering(SK_ColorWHITE);

  // Set visibility back to visible - guest should start rendering again.
  ASSERT_TRUE(content::ExecJs(
      web_contents(),
      "document.querySelector('embed').style.visibility = 'visible';"));
  VerifyBoxRendering(SK_ColorRED);
}

IN_PROC_BROWSER_TEST_F(SecureEmbedBrowserTest, TabSwitchingUpdatesVisibility) {
  // This test simulates a tab switch by changing the visibility of the top-
  // level WebContents. The secure embed guest should receive visibility
  // updates accordingly. This is similar to SitePerProcessBrowserTest::
  // TabSwitchingUpdatesVisibility.

  auto guest_contents =
      SetupHarnessAndGuestWithContent("/secure_embed/red_box.html");
  AttachGuestToEmbed(guest_contents.get());

  VerifyBoxRendering(SK_ColorRED);

  content::RenderFrameHost* guest_frame = guest_contents->GetPrimaryMainFrame();
  auto* guest_rwh = guest_frame->GetRenderWidgetHost();
  ASSERT_NE(guest_rwh, nullptr);
  EXPECT_TRUE(guest_rwh->GetView()->IsShowing());

  // Simulate tab switch away (hide) and back (show), verifying guest
  // visibility.
  {
    RenderWidgetHostVisibilityWaiter waiter(guest_rwh, false);
    web_contents()->WasHidden();
    waiter.Wait();
    EXPECT_FALSE(guest_rwh->GetView()->IsShowing());
  }

  // The window/tab switches to a background color here so don't verify that.

  {
    RenderWidgetHostVisibilityWaiter waiter(guest_rwh, true);
    web_contents()->WasShown();
    waiter.Wait();
    EXPECT_TRUE(guest_rwh->GetView()->IsShowing());
  }

  VerifyBoxRendering(SK_ColorRED);
}

IN_PROC_BROWSER_TEST_F(SecureEmbedBrowserTest,
                       TabSwitchingUpdatesVisibilityWithIframe) {
  // Test tab switching with a guest that contains a cross-site iframe.
  // Both guest main frame and iframe should receive visibility updates.
  auto guest_contents = SetupHarnessAndGuestWithIframe();
  AttachGuestToEmbed(guest_contents.get());

  VerifyBoxRendering(SK_ColorRED);

  content::RenderFrameHost* guest_frame = guest_contents->GetPrimaryMainFrame();
  content::RenderFrameHost* iframe = ChildFrameAt(guest_frame, 0);
  EXPECT_NE(guest_frame->GetProcess(), iframe->GetProcess());
  auto* guest_rwh = guest_frame->GetRenderWidgetHost();
  auto* iframe_rwh = iframe->GetRenderWidgetHost();
  EXPECT_TRUE(guest_rwh->GetView()->IsShowing());
  EXPECT_TRUE(iframe_rwh->GetView()->IsShowing());

  // Simulate tab switch away - both guest and iframe should be hidden.
  {
    RenderWidgetHostVisibilityWaiter guest_waiter(guest_rwh, false);
    RenderWidgetHostVisibilityWaiter iframe_waiter(iframe_rwh, false);
    web_contents()->WasHidden();
    guest_waiter.Wait();
    iframe_waiter.Wait();
    EXPECT_FALSE(guest_rwh->GetView()->IsShowing());
    EXPECT_FALSE(iframe_rwh->GetView()->IsShowing());
  }

  // The window/tab switches to a background color here so don't verify that.

  // Simulate tab switch back - both guest and iframe should be visible.
  {
    RenderWidgetHostVisibilityWaiter guest_waiter(guest_rwh, true);
    RenderWidgetHostVisibilityWaiter iframe_waiter(iframe_rwh, true);
    web_contents()->WasShown();
    guest_waiter.Wait();
    iframe_waiter.Wait();
    EXPECT_TRUE(guest_rwh->GetView()->IsShowing());
    EXPECT_TRUE(iframe_rwh->GetView()->IsShowing());
  }

  VerifyBoxRendering(SK_ColorRED);
}

IN_PROC_BROWSER_TEST_F(SecureEmbedBrowserTest,
                       VisibilityCrossesSurfaceEmbedBoundary) {
  // This test creates a nested structure using secure embeds to verify that
  // visibility changes propagate through multiple embedding levels. When
  // visibility of the outermost embed is set to hidden, both the middle and
  // inner guests should report themselves as hidden. This test goes from
  // visible to hidden and back to visible, verifying the visibility state at
  // each step. Additionally, this test adds a cross-site iframe to the
  // innermost guest to verify visibility propagates through the full chain:
  // parent -> middle guest -> inner guest -> iframe.

  // Navigate the parent page to the attach harness.
  GURL parent_url = embedded_test_server()->GetURL(
      "/secure_embed/single_embed_attach_harness.html");
  ASSERT_TRUE(NavigateToURL(web_contents(), parent_url));

  // Create the middle guest and set up its embed for the inner guest.
  auto middle_guest = CreateGuestWebContents();
  GURL middle_url = embedded_test_server()->GetURL(
      "/secure_embed/single_embed_attach_harness.html");
  ASSERT_TRUE(NavigateToURL(middle_guest.get(), middle_url));
  ASSERT_TRUE(content::WaitForLoadStop(middle_guest.get()));

  // Create the innermost guest with a cross-site iframe.
  auto inner_guest = CreateGuestWebContents();
  NavigateGuestToUrl(inner_guest.get(), kEmptyUrl);
  AddCrossSiteIframeToGuest(inner_guest.get());
  int inner_guest_id = GetGuestHandleId(inner_guest.get());
  int middle_guest_id = GetGuestHandleId(middle_guest.get());

  // Attach the middle guest to the parent's embed element.
  std::string attach_middle_script =
      "createEmbed(" + base::NumberToString(middle_guest_id) + ");";
  ASSERT_TRUE(content::ExecJs(web_contents(), attach_middle_script));
  ASSERT_TRUE(WaitForHostAttachment(1));

  // Attach the inner guest to the middle guest's embed element.
  std::string attach_inner_script =
      "createEmbed(" + base::NumberToString(inner_guest_id) + ");";
  ASSERT_TRUE(content::ExecJs(middle_guest.get(), attach_inner_script));
  ASSERT_TRUE(WaitForHostAttachment(2));

  VerifyBoxRendering(SK_ColorRED);

  content::RenderFrameHost* parent_frame =
      web_contents()->GetPrimaryMainFrame();
  content::RenderFrameHost* middle_frame = middle_guest->GetPrimaryMainFrame();
  content::RenderFrameHost* inner_frame = inner_guest->GetPrimaryMainFrame();
  content::RenderFrameHost* iframe = ChildFrameAt(inner_frame, 0);

  auto* parent_rwh = parent_frame->GetRenderWidgetHost();
  auto* middle_rwh = middle_frame->GetRenderWidgetHost();
  auto* inner_rwh = inner_frame->GetRenderWidgetHost();
  auto* iframe_rwh = iframe->GetRenderWidgetHost();

  // Verify all frames are initially visible.
  EXPECT_TRUE(parent_rwh->GetView()->IsShowing());
  EXPECT_TRUE(middle_rwh->GetView()->IsShowing());
  EXPECT_TRUE(inner_rwh->GetView()->IsShowing());
  EXPECT_TRUE(iframe_rwh->GetView()->IsShowing());

  // Transition to hidden.
  {
    // Create visibility observers for the guest frames to detect when they
    // become hidden.
    RenderWidgetHostVisibilityWaiter middle_waiter(middle_rwh, false);
    RenderWidgetHostVisibilityWaiter inner_waiter(inner_rwh, false);
    RenderWidgetHostVisibilityWaiter iframe_waiter(iframe_rwh, false);

    // Hide the embed element in the parent, which should propagate to all
    // nested guests and the iframe.
    ASSERT_TRUE(content::ExecJs(
        web_contents(),
        "document.querySelector('embed').style.visibility = 'hidden';"));

    middle_waiter.Wait();
    inner_waiter.Wait();
    iframe_waiter.Wait();

    // Verify all guest frames and iframe are now hidden.
    EXPECT_TRUE(parent_rwh->GetView()->IsShowing());
    EXPECT_FALSE(middle_rwh->GetView()->IsShowing());
    EXPECT_FALSE(inner_rwh->GetView()->IsShowing());
    EXPECT_FALSE(iframe_rwh->GetView()->IsShowing());
  }

  // Transition to visible.
  {
    // Create visibility observers for the guest frames to detect when they
    // become visible again.
    RenderWidgetHostVisibilityWaiter middle_waiter(middle_rwh, true);
    RenderWidgetHostVisibilityWaiter inner_waiter(inner_rwh, true);
    RenderWidgetHostVisibilityWaiter iframe_waiter(iframe_rwh, true);

    // Show the embed element again.
    ASSERT_TRUE(content::ExecJs(
        web_contents(),
        "document.querySelector('embed').style.visibility = 'visible';"));

    middle_waiter.Wait();
    inner_waiter.Wait();
    iframe_waiter.Wait();

    // Verify all frames are visible again.
    EXPECT_TRUE(parent_rwh->GetView()->IsShowing());
    EXPECT_TRUE(middle_rwh->GetView()->IsShowing());
    EXPECT_TRUE(inner_rwh->GetView()->IsShowing());
    EXPECT_TRUE(iframe_rwh->GetView()->IsShowing());
  }

  VerifyBoxRendering(SK_ColorRED);
}

IN_PROC_BROWSER_TEST_F(SecureEmbedBrowserTest, DisplayNoneStopsRendering) {
  auto guest_contents = SetupHarnessAndGuestWithContent(kRedBoxUrl);
  AttachGuestToEmbed(guest_contents.get());
  VerifyBoxRendering(SK_ColorRED);

  // Set display to none - guest should stop rendering.
  ASSERT_TRUE(content::ExecJs(
      web_contents(),
      "document.querySelector('embed').style.display = 'none';"));
  VerifyBoxRendering(SK_ColorWHITE);

  // Set display back to block - guest should start rendering again.
  ASSERT_TRUE(content::ExecJs(
      web_contents(),
      "document.querySelector('embed').style.display = 'block';"));
  VerifyBoxRendering(SK_ColorRED);
}

IN_PROC_BROWSER_TEST_F(SecureEmbedBrowserTest,
                       VisibilityScrolledOutStopsRendering) {
  auto guest_contents = SetupHarnessAndGuestWithContent(kRedBoxUrl);
  AttachGuestToEmbed(guest_contents.get());
  VerifyBoxRendering(SK_ColorRED);

  auto* guest_rfh = static_cast<content::RenderFrameHostImpl*>(
      guest_contents->GetPrimaryMainFrame());
  auto* guest_rwh = guest_rfh->GetRenderWidgetHost();

  // Keep CSS visibility intact but move the embed outside the viewport.
  ASSERT_TRUE(content::ExecJs(web_contents(),
                              R"JS(
        const embed = document.querySelector('embed');
        embed.style.visibility = 'visible';
        embed.style.display = 'block';
        embed.style.marginTop = '4000px';
        document.body.style.height = '8000px';
        window.scrollTo(0, 0);
      )JS"));

  EXPECT_TRUE(base::test::RunUntil([&]() {
    return guest_rfh->visibility() ==
           blink::mojom::FrameVisibility::kRenderedOutOfViewport;
  }));
  VerifyBoxRendering(SK_ColorWHITE);
  EXPECT_EQ(content::Visibility::VISIBLE, web_contents()->GetVisibility());
  // Widget should still be showing even when scrolled out of viewport
  EXPECT_TRUE(guest_rwh->GetView()->IsShowing());

  ASSERT_TRUE(content::ExecJs(web_contents(), "window.scrollTo(0, 4100);"));
  EXPECT_TRUE(base::test::RunUntil([&]() {
    return guest_rfh->visibility() ==
           blink::mojom::FrameVisibility::kRenderedInViewport;
  }));
  VerifyBoxRendering(SK_ColorRED);
  EXPECT_TRUE(guest_rwh->GetView()->IsShowing());
}

IN_PROC_BROWSER_TEST_F(SecureEmbedBrowserTest,
                       IntermediateElementVisibilityHidden) {
  // Test that hiding a parent element between root and <embed> properly
  // propagates visibility to the guest.
  auto guest_contents = SetupHarnessAndGuestWithContent(kRedBoxUrl);
  CreateContainer();
  AttachGuestToEmbed(guest_contents.get(), "embed-container");

  VerifyBoxRendering(SK_ColorRED);

  content::RenderFrameHost* guest_frame = guest_contents->GetPrimaryMainFrame();
  auto* guest_rwh = guest_frame->GetRenderWidgetHost();
  EXPECT_TRUE(guest_rwh->GetView()->IsShowing());

  // Hide the parent container - guest should stop rendering.
  {
    RenderWidgetHostVisibilityWaiter waiter(guest_rwh, false);
    ASSERT_TRUE(content::ExecJs(web_contents(),
                                "document.getElementById('embed-container')."
                                "style.visibility = 'hidden';"));
    waiter.Wait();
    EXPECT_FALSE(guest_rwh->GetView()->IsShowing());
  }
  VerifyBoxRendering(SK_ColorWHITE);

  // Show the parent container - guest should start rendering again.
  {
    RenderWidgetHostVisibilityWaiter waiter(guest_rwh, true);
    ASSERT_TRUE(content::ExecJs(web_contents(),
                                "document.getElementById('embed-container')."
                                "style.visibility = 'visible';"));
    waiter.Wait();
    EXPECT_TRUE(guest_rwh->GetView()->IsShowing());
  }
  VerifyBoxRendering(SK_ColorRED);
}

IN_PROC_BROWSER_TEST_F(SecureEmbedBrowserTest, IntermediateElementDisplayNone) {
  // Test that display:none on a parent element between root and <embed>
  // properly propagates visibility to the guest.
  auto guest_contents = SetupHarnessAndGuestWithContent(kRedBoxUrl);
  CreateContainer();
  AttachGuestToEmbed(guest_contents.get(), "embed-container");

  VerifyBoxRendering(SK_ColorRED);

  auto* guest_rfh = static_cast<content::RenderFrameHostImpl*>(
      guest_contents->GetPrimaryMainFrame());
  auto* guest_rwh = guest_rfh->GetRenderWidgetHost();
  EXPECT_TRUE(guest_rwh->GetView()->IsShowing());

  // Set display:none on parent container - guest should stop rendering.
  {
    RenderWidgetHostVisibilityWaiter waiter(guest_rwh, false);
    // Check that the embed is a child of embed-container before hiding it.
    int embed_count = content::EvalJs(
        web_contents(),
        "document.getElementById('embed-container').getElementsByTagName('embed')."
        "length")
                          .ExtractInt();
    EXPECT_EQ(embed_count, 1);
    ASSERT_TRUE(content::ExecJs(
        web_contents(),
        "document.getElementById('embed-container').style.display = 'none';"));
    waiter.Wait();
    EXPECT_FALSE(guest_rwh->GetView()->IsShowing());
    EXPECT_TRUE(base::test::RunUntil([&]() {
      return guest_rfh->visibility() ==
             blink::mojom::FrameVisibility::kNotRendered;
    }));
  }
  VerifyBoxRendering(SK_ColorWHITE);

  // Set display:block on parent container - guest should start rendering.
  {
    RenderWidgetHostVisibilityWaiter waiter(guest_rwh, true);
    ASSERT_TRUE(content::ExecJs(
        web_contents(),
        "document.getElementById('embed-container').style.display = 'block';"));
    waiter.Wait();
    EXPECT_TRUE(guest_rwh->GetView()->IsShowing());
    EXPECT_TRUE(base::test::RunUntil([&]() {
      return guest_rfh->visibility() ==
             blink::mojom::FrameVisibility::kRenderedInViewport;
    }));
  }
  VerifyBoxRendering(SK_ColorRED);
}

IN_PROC_BROWSER_TEST_F(SecureEmbedBrowserTest,
                       GuestWebContentsVisibilityToggle) {
  // Test that directly toggling guest WebContents visibility works correctly.
  auto guest_contents = SetupHarnessAndGuestWithContent(kRedBoxUrl);
  AttachGuestToEmbed(guest_contents.get());
  VerifyBoxRendering(SK_ColorRED);

  content::RenderFrameHost* guest_frame = guest_contents->GetPrimaryMainFrame();
  auto* guest_rwh = guest_frame->GetRenderWidgetHost();
  EXPECT_TRUE(guest_rwh->GetView()->IsShowing());

  // Directly hide the guest WebContents.
  {
    RenderWidgetHostVisibilityWaiter waiter(guest_rwh, false);
    guest_contents->WasHidden();
    waiter.Wait();
    EXPECT_FALSE(guest_rwh->GetView()->IsShowing());
  }
  VerifyBoxRendering(SK_ColorWHITE);

  // Directly show the guest WebContents.
  {
    RenderWidgetHostVisibilityWaiter waiter(guest_rwh, true);
    guest_contents->WasShown();
    waiter.Wait();
    EXPECT_TRUE(guest_rwh->GetView()->IsShowing());
  }
  VerifyBoxRendering(SK_ColorRED);
}

IN_PROC_BROWSER_TEST_F(SecureEmbedBrowserTest,
                       GuestWebContentsVisibilityToggleWithIframe) {
  // Test that directly toggling guest WebContents visibility propagates to
  // cross-site iframes within the guest.
  auto guest_contents = SetupHarnessAndGuestWithIframe();
  AttachGuestToEmbed(guest_contents.get());
  VerifyBoxRendering(SK_ColorRED);

  content::RenderFrameHost* guest_frame = guest_contents->GetPrimaryMainFrame();
  content::RenderFrameHost* iframe = ChildFrameAt(guest_frame, 0);
  EXPECT_NE(guest_frame->GetProcess(), iframe->GetProcess());
  auto* guest_rwh = guest_frame->GetRenderWidgetHost();
  auto* iframe_rwh = iframe->GetRenderWidgetHost();
  EXPECT_TRUE(guest_rwh->GetView()->IsShowing());
  EXPECT_TRUE(iframe_rwh->GetView()->IsShowing());

  // Directly hide the guest WebContents - iframe should also be hidden.
  {
    RenderWidgetHostVisibilityWaiter guest_waiter(guest_rwh, false);
    RenderWidgetHostVisibilityWaiter iframe_waiter(iframe_rwh, false);
    guest_contents->WasHidden();
    guest_waiter.Wait();
    iframe_waiter.Wait();
    EXPECT_FALSE(guest_rwh->GetView()->IsShowing());
    EXPECT_FALSE(iframe_rwh->GetView()->IsShowing());
  }
  VerifyBoxRendering(SK_ColorWHITE);

  // Directly show the guest WebContents - iframe should also be visible.
  {
    RenderWidgetHostVisibilityWaiter guest_waiter(guest_rwh, true);
    RenderWidgetHostVisibilityWaiter iframe_waiter(iframe_rwh, true);
    guest_contents->WasShown();
    guest_waiter.Wait();
    iframe_waiter.Wait();
    EXPECT_TRUE(guest_rwh->GetView()->IsShowing());
    EXPECT_TRUE(iframe_rwh->GetView()->IsShowing());
  }
  VerifyBoxRendering(SK_ColorRED);
}

IN_PROC_BROWSER_TEST_F(SecureEmbedBrowserTest, IntermediateElementScrolledOut) {
  // Test that scrolling a parent container out of viewport propagates
  // visibility state to the guest.
  auto guest_contents = SetupHarnessAndGuestWithContent(kRedBoxUrl);
  CreateContainer("embed-container", "margin-top: 4000px");
  AttachGuestToEmbed(guest_contents.get(), "embed-container");
  ASSERT_TRUE(content::ExecJs(web_contents(),
                              "document.body.style.height = '8000px';"));

  auto* guest_rfh = static_cast<content::RenderFrameHostImpl*>(
      guest_contents->GetPrimaryMainFrame());
  auto* guest_rwh = guest_rfh->GetRenderWidgetHost();

  // Start with container scrolled out of view
  ASSERT_TRUE(content::ExecJs(web_contents(), "window.scrollTo(0, 0);"));

  EXPECT_TRUE(base::test::RunUntil([&]() {
    return guest_rfh->visibility() ==
           blink::mojom::FrameVisibility::kRenderedOutOfViewport;
  }));
  VerifyBoxRendering(SK_ColorWHITE);
  // Widget should still be showing even when scrolled out
  EXPECT_TRUE(guest_rwh->GetView()->IsShowing());

  // Scroll to bring container into viewport
  ASSERT_TRUE(content::ExecJs(web_contents(), "window.scrollTo(0, 4100);"));
  EXPECT_TRUE(base::test::RunUntil([&]() {
    return guest_rfh->visibility() ==
           blink::mojom::FrameVisibility::kRenderedInViewport;
  }));
  VerifyBoxRendering(SK_ColorRED);
  EXPECT_TRUE(guest_rwh->GetView()->IsShowing());
}

// TODO(secure-embed): This test fails right now because attribute changes do
// not trigger re-attaches.
IN_PROC_BROWSER_TEST_F(SecureEmbedBrowserTest, VisibilityHiddenSwapGuest) {
  NavigateToAttachHarness();

  // Create and load both guests.
  auto guest_contents_red = CreateGuestWebContents();
  NavigateGuestToUrl(guest_contents_red.get(), kRedBoxUrl);

  auto guest_contents_blue = CreateGuestWebContents();
  NavigateGuestToUrl(guest_contents_blue.get(), kBlueBoxUrl);
  int guest_id_blue = GetGuestHandleId(guest_contents_blue.get());

  // Attach the red guest first.
  AttachGuestToEmbed(guest_contents_red.get());
  VerifyBoxRendering(SK_ColorRED);

  // Set visibility to hidden - guest should stop rendering.
  ASSERT_TRUE(content::ExecJs(
      web_contents(),
      "document.querySelector('embed').style.visibility = 'hidden';"));
  VerifyBoxRendering(SK_ColorWHITE);

  ScopedDataContentIdMonitor monitor(web_contents(),
                                     /*expect_zero_change=*/false);

  // Swap to the blue guest while hidden.
  std::string script_blue =
      "setDataContentId(" + base::NumberToString(guest_id_blue) + ");";
  ASSERT_TRUE(content::ExecJs(web_contents(), script_blue));
  VerifyBoxRendering(SK_ColorWHITE);

  // Set visibility back to visible - blue guest should now be rendering.
  ASSERT_TRUE(content::ExecJs(
      web_contents(),
      "document.querySelector('embed').style.visibility = 'visible';"));
  VerifyBoxRendering(SK_ColorBLUE);
}

IN_PROC_BROWSER_TEST_F(SecureEmbedBrowserTest, DisplayNoneSwapGuest) {
  NavigateToAttachHarness();

  // Create and load both guests.
  auto guest_contents_red = CreateGuestWebContents();
  NavigateGuestToUrl(guest_contents_red.get(), kRedBoxUrl);

  auto guest_contents_blue = CreateGuestWebContents();
  NavigateGuestToUrl(guest_contents_blue.get(), kBlueBoxUrl);
  int guest_id_blue = GetGuestHandleId(guest_contents_blue.get());

  // Attach the red guest first.
  AttachGuestToEmbed(guest_contents_red.get());
  VerifyBoxRendering(SK_ColorRED);

  // Set display to none - guest should stop rendering.
  ASSERT_TRUE(content::ExecJs(
      web_contents(),
      "document.querySelector('embed').style.display = 'none';"));
  VerifyBoxRendering(SK_ColorWHITE);

  ScopedDataContentIdMonitor monitor(web_contents(),
                                     /*expect_zero_change=*/false);

  // Swap to the blue guest while display is none.
  std::string script_blue =
      "setDataContentId(" + base::NumberToString(guest_id_blue) + ");";
  ASSERT_TRUE(content::ExecJs(web_contents(), script_blue));
  VerifyBoxRendering(SK_ColorWHITE);

  // Set display back to block - blue guest should now be rendering.
  ASSERT_TRUE(content::ExecJs(
      web_contents(),
      "document.querySelector('embed').style.display = 'block';"));
  VerifyBoxRendering(SK_ColorBLUE);
}

// Create 2 <embed>s that use the same content id. The 2nd <embed> should show
// and the 1st <embed> should be blank.
IN_PROC_BROWSER_TEST_F(SecureEmbedBrowserTest, TwoEmbedSameContentId) {
  NavigateToAttachHarness();

  // Create and load both guests.
  auto guest_contents_red = CreateGuestWebContents();
  NavigateGuestToUrl(guest_contents_red.get(), kRedBoxUrl);
  int guest_id_red = GetGuestHandleId(guest_contents_red.get());

  // Attach the red guest first.
  AttachGuestToEmbedWithId(guest_contents_red.get(), "embed1");
  VerifyBoxRendering(SK_ColorRED);

  // Cache the first host's delegate pointer before forced detachment.
  auto* connector = guest_contents_red->GetSecureEmbedConnector();
  ASSERT_NE(connector, nullptr);
  auto* first_host_delegate = connector->GetDelegate();
  ASSERT_NE(first_host_delegate, nullptr);
  EXPECT_TRUE(first_host_delegate->IsAttachedForTesting());

  // The first embed's data-content-id should be reset to 0 since it was
  // forcibly detached when the guest was attached to the 2nd embed.
  ScopedDataContentIdMonitor monitor(web_contents(),
                                     /*expect_zero_change=*/true);

  // Add a 2nd <embed> with ID 'embed2' that uses the same content id.
  AttachGuestToEmbedWithId(guest_contents_red.get(), "embed2");

  // Then verify the color of the 1st <embed>, which should become blank.
  VerifyBoxRendering(SK_ColorWHITE);

  // The first host should no longer think it's attached due to the forced
  // detachment by being attached to the 2nd <embed>.
  EXPECT_FALSE(first_host_delegate->IsAttachedForTesting());
  auto* second_connector = guest_contents_red->GetSecureEmbedConnector();
  ASSERT_NE(second_connector, nullptr);
  EXPECT_TRUE(second_connector->GetDelegate()->IsAttachedForTesting());

  // Try to reattach to the first embed by setting its data-content-id back to
  // the original guest ID to ensure that a forced detachment doesn't prevent
  // subsequent re-attachment.
  std::string script_reattach_first =
      "setDataContentId(" + base::NumberToString(guest_id_red) + ", 'embed1');";
  ASSERT_TRUE(content::ExecJs(web_contents(), script_reattach_first));
  VerifyBoxRendering(SK_ColorRED);
  EXPECT_TRUE(first_host_delegate->IsAttachedForTesting());
  EXPECT_TRUE(content::EvalJs(web_contents(), "getDataContentId('embed2')")
                  .ExtractString() == "0");
}

IN_PROC_BROWSER_TEST_F(SecureEmbedBrowserTest, ReattachSameGuestToNewEmbed) {
  auto guest_contents = SetupHarnessAndGuestWithContent(kRedBoxUrl);
  int guest_id = GetGuestHandleId(guest_contents.get());

  AttachGuestToEmbed(guest_contents.get());
  VerifyBoxRendering(SK_ColorRED);

  // Destroy the embed element.
  ASSERT_TRUE(content::ExecJs(web_contents(),
                              "document.querySelector('embed').remove();"));
  VerifyBoxRendering(SK_ColorWHITE);

  // Create a new embed element and attach the same guest.
  std::string script = "createEmbed(" + base::NumberToString(guest_id) + ");";
  ASSERT_TRUE(content::ExecJs(web_contents(), script));
  VerifyBoxRendering(SK_ColorRED);
}

#if defined(USE_AURA)  // BoundingBoxUpdateWaiter is aura-specific.

// Test that the proper (top-level) TextInputManager is used for embedded
// WebContents. This tests triggers a selection change at top-level when the
// embed is focused, which used to crash on querying the selection state.
// (In particular, platforms with a selection clipboard buffer --- e.g. X11 ---
//  query TextInputManager's selection state to potentially update the
//  clipboard; this test exercises a similar code path via
//  BoundingBoxUpdateWaiter querying the selection's bounding box).
IN_PROC_BROWSER_TEST_F(SecureEmbedBrowserTest, TextInputManager) {
  auto guest_contents = SetupHarnessAndGuestWithContent(kRedBoxUrl);
  AttachGuestToEmbed(guest_contents.get());
  content::TextInputManagerTester input_tester(web_contents());
  content::TextInputManagerTester guest_input_tester(guest_contents.get());

  const char kAddTextScript[] = R"(
     let text = document.createTextNode("Some text to select");
     document.body.appendChild(text);
  )";
  ASSERT_TRUE(content::ExecJs(web_contents(), kAddTextScript));
  ASSERT_TRUE(content::ExecJs(guest_contents.get(), kAddTextScript));

  const char kFocusScript[] = R"(
    document.getElementsByTagName("embed")[0].focus();
  )";

  ASSERT_TRUE(content::ExecJs(web_contents(), kFocusScript));

  const char kSelectionChange[] = R"(
     let range = document.createRange();
     range.selectNodeContents(document.body);
     window.getSelection().addRange(range);
  )";

  // BoundingBoxUpdateWaiter asks the top-level for active frame's selection
  // change whenever the top-level's selection changes, so we need to have
  // both frames change their selections.
  content::BoundingBoxUpdateWaiter waiter(web_contents());
  ASSERT_TRUE(content::ExecJs(guest_contents.get(), kSelectionChange));
  ASSERT_TRUE(content::ExecJs(web_contents(), kSelectionChange));

  waiter.Wait();

  // If the same TextInputManager is used for the guest as top-level, they'll
  // have the same option of last updated view.
  EXPECT_EQ(input_tester.GetUpdatedView(), guest_input_tester.GetUpdatedView());
}

#endif

IN_PROC_BROWSER_TEST_F(SecureEmbedBrowserTest, Detach) {
  auto guest_contents = SetupHarnessAndGuestWithContent(kRedBoxUrl);
  AttachGuestToEmbed(guest_contents.get());

  VerifyBoxRendering(SK_ColorRED);

  // Change the data-content-id attribute to 0 to trigger a detach.
  std::string script_cause_detach = "setDataContentId(0);";
  ASSERT_TRUE(content::ExecJs(web_contents(), script_cause_detach));
  EXPECT_EQ(guest_contents->GetSecureEmbedConnector(), nullptr);
  VerifyBoxRendering(SK_ColorWHITE);

  // Re-attach the same guest.
  int guest_id = GetGuestHandleId(guest_contents.get());
  std::string script_reattach =
      "setDataContentId(" + base::NumberToString(guest_id) + ");";
  ASSERT_TRUE(content::ExecJs(web_contents(), script_reattach));
  VerifyBoxRendering(SK_ColorRED);
  EXPECT_NE(guest_contents->GetSecureEmbedConnector(), nullptr);
}

IN_PROC_BROWSER_TEST_F(SecureEmbedBrowserTest,
                       GuestWithCrossSiteIframeVisibilityHidden) {
  // Test that hiding the embed element properly propagates visibility to
  // both the guest main frame and a cross-site iframe within the guest.
  auto guest_contents = SetupHarnessAndGuestWithIframe();
  AttachGuestToEmbed(guest_contents.get());
  VerifyBoxRendering(SK_ColorRED);

  content::RenderFrameHost* guest_frame = guest_contents->GetPrimaryMainFrame();
  content::RenderFrameHost* iframe = ChildFrameAt(guest_frame, 0);
  EXPECT_NE(guest_frame->GetProcess(), iframe->GetProcess());
  auto* guest_rwh = guest_frame->GetRenderWidgetHost();
  auto* iframe_rwh = iframe->GetRenderWidgetHost();
  EXPECT_TRUE(guest_rwh->GetView()->IsShowing());
  EXPECT_TRUE(iframe_rwh->GetView()->IsShowing());

  // Hide the embed - both guest and iframe should be hidden.
  {
    RenderWidgetHostVisibilityWaiter guest_waiter(guest_rwh, false);
    RenderWidgetHostVisibilityWaiter iframe_waiter(iframe_rwh, false);
    ASSERT_TRUE(content::ExecJs(
        web_contents(),
        "document.querySelector('embed').style.visibility = 'hidden';"));
    guest_waiter.Wait();
    iframe_waiter.Wait();
    EXPECT_FALSE(guest_rwh->GetView()->IsShowing());
    EXPECT_FALSE(iframe_rwh->GetView()->IsShowing());
  }
  VerifyBoxRendering(SK_ColorWHITE);

  // Show the embed - both guest and iframe should be visible.
  {
    RenderWidgetHostVisibilityWaiter guest_waiter(guest_rwh, true);
    RenderWidgetHostVisibilityWaiter iframe_waiter(iframe_rwh, true);
    ASSERT_TRUE(content::ExecJs(
        web_contents(),
        "document.querySelector('embed').style.visibility = 'visible';"));
    guest_waiter.Wait();
    iframe_waiter.Wait();
    EXPECT_TRUE(guest_rwh->GetView()->IsShowing());
    EXPECT_TRUE(iframe_rwh->GetView()->IsShowing());
  }
  VerifyBoxRendering(SK_ColorRED);
}

IN_PROC_BROWSER_TEST_F(SecureEmbedBrowserTest,
                       GuestWithCrossSiteIframeScrolledOut) {
  // Test that scrolling the embed out of viewport propagates visibility state
  // to both guest main frame and cross-site iframe. The widget should remain
  // showing but frame visibility should change to kRenderedOutOfViewport.
  auto guest_contents = SetupHarnessAndGuestWithIframe();
  AttachGuestToEmbed(guest_contents.get());
  VerifyBoxRendering(SK_ColorRED);
  auto* guest_rfh = static_cast<content::RenderFrameHostImpl*>(
      guest_contents->GetPrimaryMainFrame());
  content::RenderFrameHost* iframe = ChildFrameAt(guest_rfh, 0);
  EXPECT_NE(guest_rfh->GetProcess(), iframe->GetProcess());

  auto* guest_rwh = guest_rfh->GetRenderWidgetHost();
  auto* iframe_rwh = iframe->GetRenderWidgetHost();

  // Move embed outside viewport.
  ASSERT_TRUE(content::ExecJs(web_contents(),
                              R"JS(
        const embed = document.querySelector('embed');
        embed.style.visibility = 'visible';
        embed.style.display = 'block';
        embed.style.marginTop = '4000px';
        document.body.style.height = '8000px';
        window.scrollTo(0, 0);
      )JS"));

  // Verify guest transitions to kRenderedOutOfViewport.
  EXPECT_TRUE(base::test::RunUntil([&]() {
    return guest_rfh->visibility() ==
           blink::mojom::FrameVisibility::kRenderedOutOfViewport;
  }));
  VerifyBoxRendering(SK_ColorWHITE);
  // Widgets should still be showing even when scrolled out.
  EXPECT_TRUE(guest_rwh->GetView()->IsShowing());
  EXPECT_TRUE(iframe_rwh->GetView()->IsShowing());

  // Scroll to bring embed back into viewport.
  ASSERT_TRUE(content::ExecJs(web_contents(), "window.scrollTo(0, 4100);"));
  EXPECT_TRUE(base::test::RunUntil([&]() {
    return guest_rfh->visibility() ==
           blink::mojom::FrameVisibility::kRenderedInViewport;
  }));
  VerifyBoxRendering(SK_ColorRED);
  EXPECT_TRUE(guest_rwh->GetView()->IsShowing());
  EXPECT_TRUE(iframe_rwh->GetView()->IsShowing());
}

IN_PROC_BROWSER_TEST_F(SecureEmbedBrowserTest, GuestWithCrossSiteIframe) {
  // If a OOPIF is present in the guest and sets up its TextInputManager before
  // the embed is attached, we used to hit a DCHECK failure while attaching as
  // all of the views that are part of the guest's frame tree must share the
  // same TextInputManager as the embed's WebContents.
  NavigateToAttachHarness();
  auto guest_contents = CreateGuestWebContents();

  NavigateGuestToUrl(guest_contents.get(), kEmptyUrl);

  GURL iframe_url = embedded_test_server()->GetURL("b.com", kRedBoxUrl);
  std::string create_iframe_script =
      "const iframe = document.createElement('iframe');"
      "iframe.src = '" +
      iframe_url.spec() +
      "';"
      "document.body.appendChild(iframe);";
  ASSERT_TRUE(content::ExecJs(guest_contents.get(), create_iframe_script));

  ASSERT_TRUE(content::WaitForLoadStop(guest_contents.get()));

  content::RenderFrameHost* main_frame = guest_contents->GetPrimaryMainFrame();
  content::RenderFrameHost* iframe = ChildFrameAt(main_frame, 0);
  ASSERT_NE(iframe, nullptr);
  EXPECT_NE(main_frame->GetProcess(), iframe->GetProcess());

  AttachGuestToEmbed(guest_contents.get());
  VerifyBoxRendering(SK_ColorRED);
}

IN_PROC_BROWSER_TEST_F(SecureEmbedBrowserTest,
                       DetachAndReattachGuestWithCrossSiteIframe) {
  // If a OOPIF is present in the guest and sets up its TextInputManager before
  // the embed is attached, we used to hit a DCHECK failure while attaching as
  // all of the views that are part of the guest's frame tree must share the
  // same TextInputManager as the embed's WebContents. This test is verifying
  // that detach and re-attach also work correctly in this scenario.
  NavigateToAttachHarness();
  auto guest_contents = CreateGuestWebContents();

  NavigateGuestToUrl(guest_contents.get(), kEmptyUrl);

  GURL iframe_url = embedded_test_server()->GetURL("b.com", kRedBoxUrl);
  std::string create_iframe_script =
      "const iframe = document.createElement('iframe');"
      "iframe.src = '" +
      iframe_url.spec() +
      "';"
      "document.body.appendChild(iframe);";
  ASSERT_TRUE(content::ExecJs(guest_contents.get(), create_iframe_script));

  ASSERT_TRUE(content::WaitForLoadStop(guest_contents.get()));

  content::RenderFrameHost* main_frame = guest_contents->GetPrimaryMainFrame();
  content::RenderFrameHost* iframe = ChildFrameAt(main_frame, 0);
  ASSERT_NE(iframe, nullptr);
  EXPECT_NE(main_frame->GetProcess(), iframe->GetProcess());

  AttachGuestToEmbed(guest_contents.get());
  VerifyBoxRendering(SK_ColorRED);

  // Change the data-content-id attribute to 0 to trigger a detach.
  std::string script_cause_detach = "setDataContentId(0);";
  ASSERT_TRUE(content::ExecJs(web_contents(), script_cause_detach));
  EXPECT_EQ(guest_contents->GetSecureEmbedConnector(), nullptr);
  VerifyBoxRendering(SK_ColorWHITE);

  // Reattach the same guest.
  int guest_id = GetGuestHandleId(guest_contents.get());
  std::string script_reattach =
      "setDataContentId(" + base::NumberToString(guest_id) + ");;";
  ASSERT_TRUE(content::ExecJs(web_contents(), script_reattach));
  VerifyBoxRendering(SK_ColorRED);
}

IN_PROC_BROWSER_TEST_F(SecureEmbedBrowserTest, InvalidContentIdNegative) {
  // Test that specifying an invalid (negative) content_id creates a host but
  // doesn't attach any guest, resulting in white rendering.
  NavigateToAttachHarness();

  std::string script = "createEmbed(-1);";
  ASSERT_TRUE(content::ExecJs(web_contents(), script));

  // Host should be created even with invalid content_id but no attach should
  // happen.
  ASSERT_TRUE(WaitForHostCreation());
  EXPECT_EQ(1u, SecureEmbedHost::GetInstanceCountForTesting());
  EXPECT_EQ(0u, SecureEmbedHost::GetAttachedInstanceCountForTesting());
  VerifyBoxRendering(SK_ColorWHITE);
}

IN_PROC_BROWSER_TEST_F(SecureEmbedBrowserTest,
                       AttachValidGuestThenUpdateToNegativeContentId) {
  // Test that attaching a valid guest shows its content, but updating to a
  // negative content_id detaches and results in white rendering.
  auto guest_contents = SetupHarnessAndGuestWithContent(kRedBoxUrl);
  AttachGuestToEmbed(guest_contents.get());

  VerifyBoxRendering(SK_ColorRED);
  EXPECT_NE(guest_contents->GetSecureEmbedConnector(), nullptr);

  // Update the data-content-id to a negative number to trigger detachment.
  std::string script_detach = "setDataContentId(-5);";
  ASSERT_TRUE(content::ExecJs(web_contents(), script_detach));

  VerifyBoxRendering(SK_ColorWHITE);
  EXPECT_EQ(guest_contents->GetSecureEmbedConnector(), nullptr);
}

// Tests that calling focus() on an <embed> makes the guest WebContents focused.
IN_PROC_BROWSER_TEST_F(SecureEmbedBrowserTest, FocusByJS) {
  auto guest_contents = SetupHarnessAndGuestWithContent(kRedBoxUrl);
  AttachGuestToEmbed(guest_contents.get());

  // Focus the embedder (parent).
  ASSERT_TRUE(content::ExecJs(web_contents(),
                              "let btn = document.createElement('button');"
                              "document.body.appendChild(btn);"
                              "btn.focus();"));
  EXPECT_NE(nullptr, web_contents()->GetFocusedFrame());

  // Verify that the guest does not expose the parent's focused frame.
  EXPECT_EQ(nullptr, guest_contents->GetFocusedFrame());

  // Now focus the embed element, which transfers focus to the guest.
  ASSERT_TRUE(content::ExecJs(web_contents(),
                              "document.querySelector('embed').focus();"));

  // Verify that the guest now returns a focused frame (its own).
  ASSERT_TRUE(base::test::RunUntil([&]() {
    return guest_contents.get() ==
           content::GetFocusedWebContents(guest_contents.get());
  }));
  ASSERT_TRUE(base::test::RunUntil([&]() {
    return guest_contents->GetPrimaryMainFrame() ==
           guest_contents->GetFocusedFrame();
  }));
}

// Tests that the focus can traverse in and out of <embed> and <iframe> in
// <embed> by pressing the Tab key.
//
// This test has the following DOM structure:
// <html>
//   <button id='embedder_btn_1' />
//     <embed>
//       <button id='guest_btn' />
//       <iframe>
//         <button id='iframe_btn' />
//       </iframe>
//     </embed>
//   <button id='embedder_btn_2' />
// </html>
IN_PROC_BROWSER_TEST_F(SecureEmbedBrowserTest, FocusByTabTraversal) {
  NavigateToAttachHarness();
  auto guest_contents = CreateGuestWebContents();
  NavigateGuestToUrl(guest_contents.get(), kEmptyUrl);

  // Add a button to the guest.
  ASSERT_TRUE(content::ExecJs(guest_contents.get(),
                              "let btn = document.createElement('button');"
                              "btn.id = 'guest_btn';"
                              "document.body.appendChild(btn);"));

  // Add a cross-site iframe to the guest.
  GURL iframe_url = embedded_test_server()->GetURL("b.com", kEmptyUrl);
  std::string create_iframe_script =
      "const iframe = document.createElement('iframe');"
      "iframe.src = '" +
      iframe_url.spec() +
      "';"
      "document.body.appendChild(iframe);";
  ASSERT_TRUE(content::ExecJs(guest_contents.get(), create_iframe_script));
  ASSERT_TRUE(content::WaitForLoadStop(guest_contents.get()));

  content::RenderFrameHost* main_frame = guest_contents->GetPrimaryMainFrame();
  content::RenderFrameHost* iframe = ChildFrameAt(main_frame, 0);
  ASSERT_NE(iframe, nullptr);

  // Add a button to the iframe.
  ASSERT_TRUE(content::ExecJs(iframe,
                              "let btn = document.createElement('button');"
                              "btn.id = 'iframe_btn';"
                              "document.body.appendChild(btn);"));

  AttachGuestToEmbed(guest_contents.get());

  // Add a button to the embedder before the embed element.
  ASSERT_TRUE(content::ExecJs(
      web_contents(),
      "let btn = document.createElement('button');"
      "btn.id = 'embedder_btn_1';"
      "document.body.insertBefore(btn, document.querySelector('embed'));"
      "btn.focus();"));

  // Add a button to the embedder after the embed element.
  ASSERT_TRUE(content::ExecJs(web_contents(),
                              "let btn = document.createElement('button');"
                              "btn.id = 'embedder_btn_2';"
                              "document.body.appendChild(btn);"));

  // 1. Initial state: Embedder button 1 focused.
  EXPECT_EQ("embedder_btn_1",
            content::EvalJs(web_contents(), "document.activeElement.id"));
  EXPECT_EQ(web_contents(), content::GetFocusedWebContents(web_contents()));
  EXPECT_EQ(web_contents()->GetPrimaryMainFrame(),
            web_contents()->GetFocusedFrame());
  EXPECT_EQ(nullptr, content::GetFocusedWebContents(guest_contents.get()));
  EXPECT_EQ(guest_contents->GetFocusedFrame(), nullptr);

  // 2. Press Tab: Focus enters guest -> guest button.
  // Wait untils the View is composited. The keyboard events are ignored before
  // FCP.
  WaitForCopyableViewInWebContents(web_contents());
  content::SimulateKeyPress(web_contents(), ui::DomKey::TAB, ui::DomCode::TAB,
                            ui::VKEY_TAB, false, false, false, false);
  WaitForFocus(guest_contents.get(), "guest_btn");
  EXPECT_EQ(guest_contents.get(),
            content::GetFocusedWebContents(web_contents()));
  EXPECT_EQ(guest_contents->GetPrimaryMainFrame(),
            web_contents()->GetFocusedFrame());
  EXPECT_EQ(guest_contents.get(),
            content::GetFocusedWebContents(guest_contents.get()));
  EXPECT_EQ(guest_contents->GetPrimaryMainFrame(),
            guest_contents->GetFocusedFrame());

  // 3. Press Tab: Focus moves to iframe -> iframe button.
  WaitForCopyableViewInFrame(iframe);
  content::SimulateKeyPress(web_contents(), ui::DomKey::TAB, ui::DomCode::TAB,
                            ui::VKEY_TAB, false, false, false, false);
  WaitForFocus(iframe, "iframe_btn");
  // EXPECT_EQ(guest_contents->GetFocusedFrame(), iframe);
  EXPECT_EQ(guest_contents.get(),
            content::GetFocusedWebContents(web_contents()));
  EXPECT_EQ(iframe, web_contents()->GetFocusedFrame());
  EXPECT_EQ(guest_contents.get(),
            content::GetFocusedWebContents(guest_contents.get()));
  EXPECT_EQ(iframe, guest_contents->GetFocusedFrame());

  // 4. Press Tab: Focus leaves guest -> back to embedder (embedder_btn_2).
  content::SimulateKeyPress(web_contents(), ui::DomKey::TAB, ui::DomCode::TAB,
                            ui::VKEY_TAB, false, false, false, false);
  WaitForFocus(web_contents(), "embedder_btn_2");
  EXPECT_EQ(web_contents(), content::GetFocusedWebContents(web_contents()));
  EXPECT_EQ(web_contents()->GetPrimaryMainFrame(),
            web_contents()->GetFocusedFrame());
  // Guest should no longer consider itself focused.
  EXPECT_EQ(nullptr, content::GetFocusedWebContents(guest_contents.get()));
  EXPECT_EQ(guest_contents->GetFocusedFrame(), nullptr);
}

// Tests that clicking on <input> makes it focused.
//
// The test has the following DOM structure:
// <html>
//    <input id=“outer-input” />
//    <iframe id=“outer-iframe” >
//       <input id="outer-iframe-input” />
//    </iframe>
//    <embed>
//      <input id=“embed-input” />
//      <iframe id="embed-iframe”>
//         <input id=“embed-iframe-input” />
//      </iframe>
//    </embed>
// </html>
//
// The test enumerates all combinations of (starting_input, target_input). The
// focus is verified by checking the input's value after sending keyboard
// events.
IN_PROC_BROWSER_TEST_F(SecureEmbedBrowserTest, FocusByClick) {
  NavigateToAttachHarness();
  auto guest_contents = CreateGuestWebContents();
  NavigateGuestToUrl(guest_contents.get(), kEmptyUrl);

  // Setup Outer Input
  ASSERT_TRUE(content::ExecJs(web_contents(),
                              "let input = document.createElement('input');"
                              "input.id = 'outer-input';"
                              "document.body.appendChild(input);"));

  // Setup Outer Iframe with Input
  GURL outer_iframe_url = embedded_test_server()->GetURL("b.com", kEmptyUrl);
  ASSERT_TRUE(content::ExecJs(web_contents(),
                              "let iframe = document.createElement('iframe');"
                              "iframe.id = 'outer-iframe';"
                              "iframe.src = '" +
                                  outer_iframe_url.spec() +
                                  "';"
                                  "document.body.appendChild(iframe);"));
  ASSERT_TRUE(content::WaitForLoadStop(web_contents()));
  content::RenderFrameHost* outer_iframe =
      ChildFrameAt(web_contents()->GetPrimaryMainFrame(), 0);
  ASSERT_TRUE(content::ExecJs(outer_iframe,
                              "let input = document.createElement('input');"
                              "input.id = 'outer-iframe-input';"
                              "document.body.appendChild(input);"));

  // Setup Guest Input
  ASSERT_TRUE(content::ExecJs(guest_contents.get(),
                              "let input = document.createElement('input');"
                              "input.id = 'embed-input';"
                              "document.body.appendChild(input);"));

  // Setup Guest Iframe with Input
  GURL guest_iframe_url = embedded_test_server()->GetURL("b.com", kEmptyUrl);
  ASSERT_TRUE(content::ExecJs(guest_contents.get(),
                              "let iframe = document.createElement('iframe');"
                              "iframe.id = 'embed-iframe';"
                              "iframe.src = '" +
                                  guest_iframe_url.spec() +
                                  "';"
                                  "document.body.appendChild(iframe);"));
  ASSERT_TRUE(content::WaitForLoadStop(guest_contents.get()));
  content::RenderFrameHost* guest_iframe =
      ChildFrameAt(guest_contents->GetPrimaryMainFrame(), 0);
  ASSERT_TRUE(content::ExecJs(guest_iframe,
                              "let input = document.createElement('input');"
                              "input.id = 'embed-iframe-input';"
                              "document.body.appendChild(input);"));

  AttachGuestToEmbed(guest_contents.get());

  // Wait for layout and hit test data to propagate.
  WaitForCopyableViewInWebContents(web_contents());
  WaitForCopyableViewInFrame(outer_iframe);
  WaitForCopyableViewInWebContents(guest_contents.get());
  WaitForCopyableViewInFrame(guest_iframe);

  struct InputTarget {
    content::GlobalRenderFrameHostId rfh_id;
    std::string element_id;
    std::string name;
    bool is_in_guest;
  };

  std::vector<InputTarget> targets = {
      {web_contents()->GetPrimaryMainFrame()->GetGlobalId(), "outer-input",
       "Outer Input", false},
      {outer_iframe->GetGlobalId(), "outer-iframe-input", "Outer Iframe Input",
       false},
      {guest_contents->GetPrimaryMainFrame()->GetGlobalId(), "embed-input",
       "Embed Input", true},
      {guest_iframe->GetGlobalId(), "embed-iframe-input", "Embed Iframe Input",
       true}};

  auto get_absolute_click_coordinates =
      [&](content::RenderFrameHost* target_rfh, const std::string& element_id,
          bool is_in_guest) {
        gfx::PointF center = content::GetCenterCoordinatesOfElementWithId(
            target_rfh, element_id);
        gfx::Vector2dF offset;

        auto get_offset = [&](content::RenderFrameHost* rfh,
                              const std::string& js_get_element) {
          auto val =
              content::EvalJs(
                  rfh, "const el = " + js_get_element +
                           ";"
                           "const r = el.getBoundingClientRect();"
                           "[r.left + el.clientLeft, r.top + el.clientTop]")
                  .TakeValue();
          const auto& list = val.GetList();
          return gfx::Vector2dF(list[0].GetDouble(), list[1].GetDouble());
        };

        if (target_rfh->GetGlobalId() == outer_iframe->GetGlobalId()) {
          offset = get_offset(web_contents()->GetPrimaryMainFrame(),
                              "document.getElementById('outer-iframe')");
        } else if (is_in_guest) {
          if (target_rfh->GetGlobalId() == guest_iframe->GetGlobalId()) {
            offset = get_offset(guest_contents->GetPrimaryMainFrame(),
                                "document.getElementById('embed-iframe')");
          }
          offset += get_offset(web_contents()->GetPrimaryMainFrame(),
                               "document.querySelector('embed')");
        }

        return gfx::ToRoundedPoint(center + offset);
      };

  auto click_and_verify_focus = [&](const InputTarget& start,
                                    const InputTarget& end) {
    // Focus start
    content::RenderFrameHost* start_rfh =
        content::RenderFrameHost::FromID(start.rfh_id);
    ASSERT_TRUE(content::ExecJs(
        start_rfh,
        "document.getElementById('" + start.element_id + "').focus();"));
    WaitForFocus(start_rfh, start.element_id);

    // Calculate coordinates for click
    content::RenderFrameHost* end_rfh =
        content::RenderFrameHost::FromID(end.rfh_id);
    gfx::Point click_point = get_absolute_click_coordinates(
        end_rfh, end.element_id, end.is_in_guest);

    content::SimulateMouseClickAt(
        web_contents(), 0, blink::WebMouseEvent::Button::kLeft, click_point);
    WaitForFocus(end_rfh, end.element_id);

    if (end.is_in_guest) {
      EXPECT_EQ(guest_contents.get(),
                content::GetFocusedWebContents(web_contents()));
      EXPECT_EQ(end_rfh, web_contents()->GetFocusedFrame());
      EXPECT_EQ(guest_contents.get(),
                content::GetFocusedWebContents(guest_contents.get()));
      EXPECT_EQ(end_rfh, guest_contents->GetFocusedFrame());
    } else {
      EXPECT_EQ(web_contents(), content::GetFocusedWebContents(web_contents()));
      EXPECT_EQ(end_rfh, web_contents()->GetFocusedFrame());
      EXPECT_EQ(nullptr, content::GetFocusedWebContents(guest_contents.get()));
      EXPECT_EQ(nullptr, guest_contents->GetFocusedFrame());
    }
  };

  for (const auto& start : targets) {
    for (const auto& end : targets) {
      click_and_verify_focus(start, end);
    }
  }
}

}  // namespace secure_embed
