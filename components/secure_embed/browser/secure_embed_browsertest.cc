// Copyright 2025 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/command_line.h"
#include "base/run_loop.h"
#include "base/test/bind.h"
#include "base/test/run_until.h"
#include "components/guest_contents/browser/guest_contents_handle.h"
#include "components/secure_embed/browser/secure_embed_host.h"
#include "content/public/browser/browser_context.h"
#include "content/public/browser/render_widget_host.h"
#include "content/public/browser/render_widget_host_observer.h"
#include "content/public/browser/render_widget_host_view.h"
#include "content/public/browser/secure_embed_connector.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "content/public/test/content_browser_test.h"
#include "content/shell/browser/shell.h"
#include "third_party/skia/include/core/SkBitmap.h"
#include "third_party/skia/include/core/SkColor.h"
#include "ui/gfx/geometry/rect.h"

namespace secure_embed {

namespace {
constexpr char kEmbedTagTestUrl[] = "/secure_embed/embed_tag.html";
constexpr char kAttachHarnessUrl[] =
    "/secure_embed/single_embed_attach_harness.html";

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

  bool WaitForHostCreation(size_t expected_count) {
    // Host creation is asynchronous because it requires mojo IPC between
    // the renderer process (SecureEmbedWebPlugin) and browser process
    // (SecureEmbedHost). Poll until the expected number of hosts are created.
    return base::test::RunUntil([&]() {
      return SecureEmbedHost::GetInstanceCountForTesting() >= expected_count;
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

  // Create a guest WebContents configured for secure embed.
  std::unique_ptr<content::WebContents> CreateGuestWebContents(
      content::WebContents* embedder = nullptr) {
    content::WebContents::CreateParams create_params(
        shell()->web_contents()->GetBrowserContext());
    create_params.secure_embed_embedder =
        embedder ? embedder : shell()->web_contents();
    std::unique_ptr<content::WebContents> guest_contents =
        content::WebContents::Create(create_params);
    EXPECT_NE(guest_contents, nullptr);
    EXPECT_NE(guest_contents->GetSecureEmbedConnector(), nullptr);
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
                          size_t expected_host_count = 1) {
    guest_contents::GuestContentsHandle* guest_handle =
        guest_contents::GuestContentsHandle::CreateForWebContents(
            guest_contents);
    ASSERT_NE(guest_handle, nullptr);
    std::string script =
        "createEmbed(" + base::NumberToString(guest_handle->id()) + ");";
    ASSERT_TRUE(content::ExecJs(web_contents(), script));
    ASSERT_TRUE(WaitForHostCreation(expected_host_count));
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
};

IN_PROC_BROWSER_TEST_F(SecureEmbedBrowserTest, EmbedTagCreatesPlugin) {
  NavigateToTestUrl(kEmbedTagTestUrl);

  EXPECT_EQ(1, CountEmbedElementsInPage());

  ASSERT_TRUE(WaitForHostCreation(1));
  EXPECT_EQ(1u, SecureEmbedHost::GetInstanceCountForTesting());
}

IN_PROC_BROWSER_TEST_F(SecureEmbedBrowserTest,
                       PluginRendersRedSquare_AttachBeforeLoad) {
  NavigateToAttachHarness();
  auto guest_contents = CreateGuestWebContents();

  // Attach before loading content.
  AttachGuestToEmbed(guest_contents.get());

  // Now navigate the guest to the red box page after attachment.
  NavigateGuestToUrl(guest_contents.get(), "/secure_embed/red_box.html");

  VerifyBoxRendering(SK_ColorRED);
}

IN_PROC_BROWSER_TEST_F(SecureEmbedBrowserTest,
                       PluginRendersRedSquare_AttachAfterLoad) {
  // Load content before attaching.
  auto guest_contents =
      SetupHarnessAndGuestWithContent("/secure_embed/red_box.html");
  AttachGuestToEmbed(guest_contents.get());

  VerifyBoxRendering(SK_ColorRED);
}

// TODO(secure-embed): This test fails right now because attribute changes do
// not trigger re-attaches.
IN_PROC_BROWSER_TEST_F(SecureEmbedBrowserTest,
                       DISABLED_PluginRendersRedSquare_MultipleAttachCalls) {
  NavigateToAttachHarness();

  // Create and load both guests.
  auto guest_contents_red = CreateGuestWebContents();
  NavigateGuestToUrl(guest_contents_red.get(), "/secure_embed/red_box.html");
  int guest_id_red = GetGuestHandleId(guest_contents_red.get());

  auto guest_contents_blue = CreateGuestWebContents();
  NavigateGuestToUrl(guest_contents_blue.get(), "/secure_embed/blue_box.html");
  int guest_id_blue = GetGuestHandleId(guest_contents_blue.get());

  // Attach the red guest first.
  AttachGuestToEmbed(guest_contents_red.get());
  VerifyBoxRendering(SK_ColorRED);

  // Swap to the blue guest by changing the data-content-id attribute.
  std::string script_blue =
      "setDataContentId(" + base::NumberToString(guest_id_blue) + ");";
  ASSERT_TRUE(content::ExecJs(web_contents(), script_blue));
  VerifyBoxRendering(SK_ColorBLUE);

  // Swap back to red guest.
  std::string script_red_again =
      "setDataContentId(" + base::NumberToString(guest_id_red) + ");";
  ASSERT_TRUE(content::ExecJs(web_contents(), script_red_again));
  VerifyBoxRendering(SK_ColorRED);
}

IN_PROC_BROWSER_TEST_F(SecureEmbedBrowserTest, VisibilityHiddenStopsRendering) {
  auto guest_contents =
      SetupHarnessAndGuestWithContent("/secure_embed/red_box.html");
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

  {
    RenderWidgetHostVisibilityWaiter waiter(guest_rwh, true);
    web_contents()->WasShown();
    waiter.Wait();
    EXPECT_TRUE(guest_rwh->GetView()->IsShowing());
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
  // each step.

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

  // Create the innermost guest.
  auto inner_guest = CreateGuestWebContents(middle_guest.get());
  GURL inner_url = embedded_test_server()->GetURL("/secure_embed/red_box.html");
  ASSERT_TRUE(NavigateToURL(inner_guest.get(), inner_url));
  ASSERT_TRUE(content::WaitForLoadStop(inner_guest.get()));

  int inner_guest_id = GetGuestHandleId(inner_guest.get());
  int middle_guest_id = GetGuestHandleId(middle_guest.get());

  // Attach the middle guest to the parent's embed element.
  std::string attach_middle_script =
      "createEmbed(" + base::NumberToString(middle_guest_id) + ");";
  ASSERT_TRUE(content::ExecJs(web_contents(), attach_middle_script));
  ASSERT_TRUE(WaitForHostCreation(1));

  // Attach the inner guest to the middle guest's embed element.
  std::string attach_inner_script =
      "createEmbed(" + base::NumberToString(inner_guest_id) + ");";
  ASSERT_TRUE(content::ExecJs(middle_guest.get(), attach_inner_script));
  ASSERT_TRUE(WaitForHostCreation(2));

  content::RenderFrameHost* parent_frame =
      web_contents()->GetPrimaryMainFrame();
  content::RenderFrameHost* middle_frame = middle_guest->GetPrimaryMainFrame();
  content::RenderFrameHost* inner_frame = inner_guest->GetPrimaryMainFrame();

  auto* parent_rwh = parent_frame->GetRenderWidgetHost();
  auto* middle_rwh = middle_frame->GetRenderWidgetHost();
  auto* inner_rwh = inner_frame->GetRenderWidgetHost();

  // Verify all frames are initially visible.
  EXPECT_TRUE(parent_rwh->GetView()->IsShowing());
  EXPECT_TRUE(middle_rwh->GetView()->IsShowing());
  EXPECT_TRUE(inner_rwh->GetView()->IsShowing());

  VerifyBoxRendering(SK_ColorRED);

  // Transition to hidden.
  {
    // Create visibility observers for the guest frames to detect when they
    // become hidden.
    RenderWidgetHostVisibilityWaiter middle_waiter(middle_rwh, false);
    RenderWidgetHostVisibilityWaiter inner_waiter(inner_rwh, false);

    // Hide the embed element in the parent, which should propagate to all
    // nested guests.
    ASSERT_TRUE(content::ExecJs(
        web_contents(),
        "document.querySelector('embed').style.visibility = 'hidden';"));

    middle_waiter.Wait();
    inner_waiter.Wait();

    // Verify all guest frames are now hidden.
    EXPECT_TRUE(parent_rwh->GetView()->IsShowing());
    EXPECT_FALSE(middle_rwh->GetView()->IsShowing());
    EXPECT_FALSE(inner_rwh->GetView()->IsShowing());
  }

  // Transition to visible.
  {
    // Create visibility observers for the guest frames to detect when they
    // become visible again.
    RenderWidgetHostVisibilityWaiter middle_waiter(middle_rwh, true);
    RenderWidgetHostVisibilityWaiter inner_waiter(inner_rwh, true);

    // Show the embed element again.
    ASSERT_TRUE(content::ExecJs(
        web_contents(),
        "document.querySelector('embed').style.visibility = 'visible';"));

    middle_waiter.Wait();
    inner_waiter.Wait();

    // Verify all frames are visible again.
    EXPECT_TRUE(parent_rwh->GetView()->IsShowing());
    EXPECT_TRUE(middle_rwh->GetView()->IsShowing());
    EXPECT_TRUE(inner_rwh->GetView()->IsShowing());
  }
}

IN_PROC_BROWSER_TEST_F(SecureEmbedBrowserTest, DisplayNoneStopsRendering) {
  auto guest_contents =
      SetupHarnessAndGuestWithContent("/secure_embed/red_box.html");
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

// TODO(secure-embed): This test fails right now because attribute changes do
// not trigger re-attaches.
IN_PROC_BROWSER_TEST_F(SecureEmbedBrowserTest,
                       DISABLED_VisibilityHiddenSwapGuest) {
  NavigateToAttachHarness();

  // Create and load both guests.
  auto guest_contents_red = CreateGuestWebContents();
  NavigateGuestToUrl(guest_contents_red.get(), "/secure_embed/red_box.html");

  auto guest_contents_blue = CreateGuestWebContents();
  NavigateGuestToUrl(guest_contents_blue.get(), "/secure_embed/blue_box.html");
  int guest_id_blue = GetGuestHandleId(guest_contents_blue.get());

  // Attach the red guest first.
  AttachGuestToEmbed(guest_contents_red.get());
  VerifyBoxRendering(SK_ColorRED);

  // Set visibility to hidden - guest should stop rendering.
  ASSERT_TRUE(content::ExecJs(
      web_contents(),
      "document.querySelector('embed').style.visibility = 'hidden';"));
  VerifyBoxRendering(SK_ColorWHITE);

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
  NavigateGuestToUrl(guest_contents_red.get(), "/secure_embed/red_box.html");

  auto guest_contents_blue = CreateGuestWebContents();
  NavigateGuestToUrl(guest_contents_blue.get(), "/secure_embed/blue_box.html");
  int guest_id_blue = GetGuestHandleId(guest_contents_blue.get());

  // Attach the red guest first.
  AttachGuestToEmbed(guest_contents_red.get());
  VerifyBoxRendering(SK_ColorRED);

  // Set display to none - guest should stop rendering.
  ASSERT_TRUE(content::ExecJs(
      web_contents(),
      "document.querySelector('embed').style.display = 'none';"));
  VerifyBoxRendering(SK_ColorWHITE);

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

IN_PROC_BROWSER_TEST_F(SecureEmbedBrowserTest, ReattachSameGuestToNewEmbed) {
  auto guest_contents =
      SetupHarnessAndGuestWithContent("/secure_embed/red_box.html");
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

}  // namespace secure_embed
