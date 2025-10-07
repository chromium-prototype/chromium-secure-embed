// Copyright 2025 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/memory/raw_ptr.h"
#include "base/run_loop.h"
#include "base/strings/stringprintf.h"
#include "base/strings/utf_string_conversions.h"
#include "base/test/bind.h"
#include "components/secure_embed/browser/secure_embed_host.h"
#include "components/secure_embed/common/secure_embed.mojom.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "content/public/test/content_browser_test.h"
#include "content/public/test/content_browser_test_content_browser_client.h"
#include "content/shell/browser/shell.h"
#include "mojo/public/cpp/bindings/associated_receiver.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "third_party/blink/public/common/associated_interfaces/associated_interface_registry.h"

namespace secure_embed {

namespace {

constexpr char kTestUrl[] = "/secure_embed/embed_tag.html";
constexpr char kMultipleEmbedsUrl[] = "/secure_embed/multiple_embeds.html";

class SecureEmbedHostTracker;

class MockSecureEmbedHost : public mojom::SecureEmbedHost {
 public:
  explicit MockSecureEmbedHost(SecureEmbedHostTracker* tracker)
      : tracker_(tracker) {}
  ~MockSecureEmbedHost() override = default;

  void Bind(mojo::PendingAssociatedReceiver<mojom::SecureEmbedHost> receiver);

  // mojom::SecureEmbedHost implementation
  void Attach(int64_t content_id) override {
    attach_call_count_++;
    last_content_id_ = content_id;
    if (attach_callback_) {
      std::move(attach_callback_).Run(content_id);
    }
  }

  void SetAttachCallback(base::OnceCallback<void(int64_t)> callback) {
    attach_callback_ = std::move(callback);
  }

  void SetDisconnectCallback(base::OnceClosure callback) {
    disconnect_callback_ = std::move(callback);
  }

  int attach_call_count() const { return attach_call_count_; }
  int64_t last_content_id() const { return last_content_id_; }
  bool is_connected() const { return receiver_.is_bound(); }

 private:
  raw_ptr<SecureEmbedHostTracker> tracker_;
  mojo::AssociatedReceiver<mojom::SecureEmbedHost> receiver_{this};
  int attach_call_count_ = 0;
  int64_t last_content_id_ = -1;
  base::OnceCallback<void(int64_t)> attach_callback_;
  base::OnceClosure disconnect_callback_;
};

// Helper class to track MockSecureEmbedHost instances.
// Note: MockSecureEmbedHost instances delete themselves on disconnect,
// so this tracker only holds raw pointers for observation purposes.
class SecureEmbedHostTracker {
 public:
  SecureEmbedHostTracker() = default;
  ~SecureEmbedHostTracker() = default;

  void AddMockHost(MockSecureEmbedHost* host) { mock_hosts_.push_back(host); }

  void RemoveMockHost(MockSecureEmbedHost* host) {
    auto it = std::find(mock_hosts_.begin(), mock_hosts_.end(), host);
    CHECK(it != mock_hosts_.end());
    mock_hosts_.erase(it);
  }

  const std::vector<MockSecureEmbedHost*>& mock_hosts() const {
    return mock_hosts_;
  }

  MockSecureEmbedHost* GetMockHost(size_t index) const {
    if (index < mock_hosts_.size()) {
      return mock_hosts_[index];
    }
    return nullptr;
  }

  size_t GetMockHostCount() const { return mock_hosts_.size(); }

 private:
  std::vector<MockSecureEmbedHost*> mock_hosts_;
};

void MockSecureEmbedHost::Bind(
    mojo::PendingAssociatedReceiver<mojom::SecureEmbedHost> receiver) {
  receiver_.Bind(std::move(receiver));
  receiver_.set_disconnect_handler(base::BindOnce(
      [](MockSecureEmbedHost* host) {
        if (host->disconnect_callback_) {
          std::move(host->disconnect_callback_).Run();
        }
        host->tracker_->RemoveMockHost(host);
        delete host;
      },
      base::Unretained(this)));
}

class SecureEmbedTestContentBrowserClient
    : public content::ContentBrowserTestContentBrowserClient {
 public:
  explicit SecureEmbedTestContentBrowserClient(SecureEmbedHostTracker* tracker)
      : tracker_(tracker) {}
  ~SecureEmbedTestContentBrowserClient() override = default;

  void RegisterAssociatedInterfaceBindersForRenderFrameHost(
      content::RenderFrameHost& render_frame_host,
      blink::AssociatedInterfaceRegistry& associated_registry) override {
    associated_registry.AddInterface<mojom::SecureEmbedHost>(
        base::BindRepeating(
            [](SecureEmbedHostTracker* tracker,
               content::RenderFrameHost* render_frame_host,
               mojo::PendingAssociatedReceiver<mojom::SecureEmbedHost>
                   receiver) {
              auto* mock_host = new MockSecureEmbedHost(tracker);
              mock_host->Bind(std::move(receiver));
              tracker->AddMockHost(mock_host);
            },
            base::Unretained(tracker_), &render_frame_host));
  }

 private:
  raw_ptr<SecureEmbedHostTracker> tracker_;
};

}  // namespace

class SecureEmbedRendererTest : public content::ContentBrowserTest {
 public:
  SecureEmbedRendererTest() = default;

  void SetUpOnMainThread() override {
    content::ContentBrowserTest::SetUpOnMainThread();

    embedded_test_server()->ServeFilesFromSourceDirectory(
        "components/test/data");
    ASSERT_TRUE(embedded_test_server()->Start());
  }

  void CreatedBrowserMainParts(
      content::BrowserMainParts* browser_main_parts) override {
    content::ContentBrowserTest::CreatedBrowserMainParts(browser_main_parts);
    test_browser_client_ =
        std::make_unique<SecureEmbedTestContentBrowserClient>(&tracker_);
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

  bool WaitForAttachCall(MockSecureEmbedHost* host) {
    if (host->attach_call_count() > 0) {
      return true;
    }

    base::RunLoop run_loop;
    host->SetAttachCallback(base::BindLambdaForTesting(
        [&](int64_t content_id) { run_loop.Quit(); }));
    run_loop.Run();
    return true;
  }

  MockSecureEmbedHost* GetMockHost(size_t index) {
    return tracker_.GetMockHost(index);
  }

  size_t GetMockHostCount() const { return tracker_.GetMockHostCount(); }

 protected:
  SecureEmbedHostTracker tracker_;
  std::unique_ptr<SecureEmbedTestContentBrowserClient> test_browser_client_;
};

IN_PROC_BROWSER_TEST_F(SecureEmbedRendererTest, EmbedTagCreatesPlugin) {
  NavigateToTestUrl(kTestUrl);

  EXPECT_EQ(1, CountEmbedElementsInPage());

  ASSERT_EQ(1u, GetMockHostCount());

  MockSecureEmbedHost* host = GetMockHost(0);
  ASSERT_NE(nullptr, host);

  EXPECT_TRUE(host->is_connected());

  ASSERT_TRUE(WaitForAttachCall(host));
  EXPECT_EQ(1, host->attach_call_count());

  // TODO(secure-embed): Update this expectation when content_id is implemented.
  EXPECT_EQ(0, host->last_content_id());
}

IN_PROC_BROWSER_TEST_F(SecureEmbedRendererTest, MultipleEmbedTags) {
  NavigateToTestUrl(kMultipleEmbedsUrl);

  ASSERT_EQ(3, CountEmbedElementsInPage());
  ASSERT_EQ(3u, GetMockHostCount());

  // Verify each host has an independent Mojo connection and receives Attach()
  for (size_t i = 0; i < 3; i++) {
    MockSecureEmbedHost* host = GetMockHost(i);
    ASSERT_NE(nullptr, host);
    ASSERT_TRUE(host->is_connected());
    ASSERT_TRUE(WaitForAttachCall(host));
    ASSERT_EQ(1, host->attach_call_count());
    // TODO(secure-embed): Update this expectation when content_id is
    // implemented.
    EXPECT_EQ(0, host->last_content_id());
  }
}

IN_PROC_BROWSER_TEST_F(SecureEmbedRendererTest, PluginDestruction) {
  // Verifies that navigating away from a page with a plugin properly destroys
  // the SecureEmbedWebPlugin and SecureEmbedHost, and that the Mojo connection
  // is cleanly closed.
  NavigateToTestUrl(kTestUrl);
  ASSERT_EQ(1, CountEmbedElementsInPage());
  ASSERT_EQ(1u, GetMockHostCount());
  MockSecureEmbedHost* first_host = GetMockHost(0);
  ASSERT_NE(nullptr, first_host);

  ASSERT_TRUE(WaitForAttachCall(first_host));
  ASSERT_TRUE(first_host->is_connected());

  base::RunLoop disconnect_loop;
  bool first_host_disconnected = false;
  first_host->SetDisconnectCallback(base::BindLambdaForTesting([&]() {
    first_host_disconnected = true;
    first_host = nullptr;
    disconnect_loop.Quit();
  }));

  NavigateToTestUrl(kMultipleEmbedsUrl);
  ASSERT_EQ(3, CountEmbedElementsInPage());

  // Navigation should have disconnected the first host
  disconnect_loop.Run();
  EXPECT_TRUE(first_host_disconnected);

  ASSERT_EQ(3u, GetMockHostCount());

  for (size_t i = 0; i < 3; i++) {
    MockSecureEmbedHost* host = GetMockHost(i);
    ASSERT_NE(nullptr, host);
    ASSERT_TRUE(WaitForAttachCall(host));
    EXPECT_TRUE(host->is_connected());
  }

  std::vector<std::unique_ptr<base::RunLoop>> disconnect_loops;
  std::vector<bool> hosts_disconnected(3, false);
  for (size_t i = 0; i < 3; i++) {
    MockSecureEmbedHost* host = GetMockHost(i);
    auto loop = std::make_unique<base::RunLoop>();
    host->SetDisconnectCallback(
        base::BindLambdaForTesting([&, i, loop_ptr = loop.get()]() {
          hosts_disconnected[i] = true;
          loop_ptr->Quit();
        }));
    disconnect_loops.push_back(std::move(loop));
  }

  ASSERT_TRUE(NavigateToURL(web_contents(), GURL("about:blank")));
  EXPECT_EQ(0, CountEmbedElementsInPage());

  for (size_t i = 0; i < disconnect_loops.size(); i++) {
    disconnect_loops[i]->Run();
    EXPECT_TRUE(hosts_disconnected[i]);
  }

  ASSERT_EQ(0u, GetMockHostCount());
}

IN_PROC_BROWSER_TEST_F(SecureEmbedRendererTest, RemoveEmbedFromDOM) {
  // Verifies that removing an embed element from the DOM destroys the
  // SecureEmbedHost connection.
  NavigateToTestUrl(kTestUrl);

  ASSERT_EQ(1, CountEmbedElementsInPage());
  ASSERT_EQ(1u, GetMockHostCount());

  MockSecureEmbedHost* host = GetMockHost(0);
  ASSERT_NE(nullptr, host);
  ASSERT_TRUE(WaitForAttachCall(host));

  base::RunLoop disconnect_loop;
  bool host_disconnected = false;
  host->SetDisconnectCallback(base::BindLambdaForTesting([&]() {
    host_disconnected = true;
    host = nullptr;
    disconnect_loop.Quit();
  }));

  // Remove the embed element from the DOM using JavaScript
  ASSERT_TRUE(content::ExecJs(web_contents(), "document.embeds[0].remove();"));

  // Wait for the disconnect callback to be triggered
  disconnect_loop.Run();
  EXPECT_TRUE(host_disconnected);

  // Verify there are now no embed elements in the page
  EXPECT_EQ(0, CountEmbedElementsInPage());

  // Verify no hosts remain
  EXPECT_EQ(0u, GetMockHostCount());
}

}  // namespace secure_embed
