/******************************************************************************
**
** Copyright (C) 2026 Ivan Pinezhaninov <ivan.pinezhaninov@gmail.com>
**
** This file is part of the extora which can be found at
** https://github.com/IvanPinezhaninov/extora/.
**
** THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
** IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
** FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
** IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
** DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
** OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR
** THE USE OR OTHER DEALINGS IN THE SOFTWARE.
**
******************************************************************************/

#include <array>
#include <cstddef>
#include <future>
#include <memory>
#include <string>
#include <thread>
#include <utility>

#include <boost/asio/io_context.hpp>
#include <boost/asio/thread_pool.hpp>
#include <boost/asio/use_future.hpp>

#include <gtest/gtest.h>

#include "PublicTestSupport.h"
#include "extora/asio/async_object_store.h"
#include "extora/extora.h"

namespace extoraTest {
namespace {

std::shared_ptr<extora::managed_object_store> openStore()
{
  extora::object_store_options options;
  options.root_directory = makeTempRoot();
  extora::storage_error error;
  std::shared_ptr<extora::managed_object_store> store = extora::open_object_store(options, error);
  EXPECT_TRUE(extora::succeeded(error)) << error.message;
  return store;
}

TEST(AsyncObjectStoreTest, DefersCompletionToTheConfiguredExecutor)
{
  std::shared_ptr<extora::managed_object_store> store = openStore();
  ASSERT_TRUE(store);
  boost::asio::io_context io;
  boost::asio::thread_pool workers{1};
  extora::asio::async_object_store adapter{store, io.get_executor(), workers.get_executor()};

  const std::thread::id initiatingThread = std::this_thread::get_id();
  bool initiatingFunctionReturned = false;
  bool completed = false;
  adapter.async_create_bucket(extora::bucket_name{"bucket"}, [&](extora::asio::operation_result<void> result) {
    EXPECT_TRUE(initiatingFunctionReturned);
    EXPECT_EQ(std::this_thread::get_id(), initiatingThread);
    EXPECT_TRUE(extora::succeeded(result.error)) << result.error.message;
    completed = true;
  });
  initiatingFunctionReturned = true;

  EXPECT_FALSE(completed);
  io.run();
  EXPECT_TRUE(completed);
  workers.join();
}

TEST(AsyncObjectStoreTest, RetainsStoreUntilOutstandingOperationCompletes)
{
  std::shared_ptr<extora::managed_object_store> store = openStore();
  ASSERT_TRUE(store);
  boost::asio::io_context io;
  boost::asio::thread_pool workers{1};
  std::future<extora::asio::operation_result<void>> future;
  {
    extora::asio::async_object_store adapter{store, io.get_executor(), workers.get_executor()};
    future = adapter.async_create_bucket(extora::bucket_name{"bucket"}, boost::asio::use_future);
  }
  store.reset();

  io.run();
  const extora::asio::operation_result<void> result = future.get();
  EXPECT_TRUE(extora::succeeded(result.error)) << result.error.message;
  workers.join();
}

TEST(AsyncObjectStoreTest, RejectsANullStore)
{
  boost::asio::io_context io;
  boost::asio::thread_pool workers{1};
  EXPECT_THROW((extora::asio::async_object_store{nullptr, io.get_executor(), workers.get_executor()}),
               std::invalid_argument);
  workers.join();
}

TEST(AsyncObjectStoreTest, SupportsUseFutureAndPreservesStructuredErrors)
{
  std::shared_ptr<extora::managed_object_store> store = openStore();
  ASSERT_TRUE(store);
  boost::asio::io_context io;
  boost::asio::thread_pool workers{1};
  extora::asio::async_object_store adapter{store, io.get_executor(), workers.get_executor()};

  std::future<extora::asio::operation_result<void>> future =
      adapter.async_delete_bucket(extora::bucket_name{"missing"}, boost::asio::use_future);
  io.run();
  const extora::asio::operation_result<void> result = future.get();

  EXPECT_EQ(result.error.code, extora::storage_error_code::bucket_not_found);
  EXPECT_FALSE(result.error.message.empty());
  workers.join();
}

TEST(AsyncObjectStoreTest, RunsStorageCompactionOnTheBlockingExecutor)
{
  std::shared_ptr<extora::managed_object_store> store = openStore();
  ASSERT_TRUE(store);
  boost::asio::io_context io;
  boost::asio::thread_pool workers{1};
  extora::asio::async_object_store adapter{store, io.get_executor(), workers.get_executor()};

  extora::compact_storage_options options;
  options.delete_empty_segments = false;
  std::future<extora::asio::operation_result<extora::compact_storage_result>> future =
      adapter.async_compact_storage(options, boost::asio::use_future);
  io.run();
  const extora::asio::operation_result<extora::compact_storage_result> result = future.get();

  EXPECT_TRUE(succeeded(result.error)) << result.error.message;
  EXPECT_EQ(result.value.compacted_payload_count, 0);
  workers.join();
}

TEST(AsyncObjectStoreTest, ReturnsStorageReclamationResult)
{
  std::shared_ptr<extora::managed_object_store> store = openStore();
  ASSERT_TRUE(store);
  boost::asio::io_context io;
  boost::asio::thread_pool workers{1};
  extora::asio::async_object_store adapter{store, io.get_executor(), workers.get_executor()};

  std::future<extora::asio::operation_result<extora::reclaim_storage_result>> future =
      adapter.async_reclaim_storage(boost::asio::use_future);
  io.run();
  const extora::asio::operation_result<extora::reclaim_storage_result> result = future.get();

  EXPECT_TRUE(succeeded(result.error)) << result.error.message;
  EXPECT_EQ(result.value.reclaimed_bytes, 0u);
  EXPECT_EQ(result.value.reclaimed_extent_count, 0u);
  EXPECT_EQ(result.value.remaining_reclaimable_bytes, 0u);
  EXPECT_EQ(result.value.remaining_reclaimable_extent_count, 0u);
  workers.join();
}

TEST(AsyncObjectStoreTest, StreamsObjectDataOnTheBlockingExecutor)
{
  std::shared_ptr<extora::managed_object_store> store = openStore();
  ASSERT_TRUE(store);
  ASSERT_TRUE(extora::succeeded(store->create_bucket(extora::bucket_name{"bucket"})));
  VectorReader source{bytesFromString("asynchronous data"), 3};
  extora::object_metadata metadata;
  extora::put_object_options options;
  options.expected_content_length = 17;

  boost::asio::io_context io;
  boost::asio::thread_pool workers{1};
  extora::asio::async_object_store adapter{store, io.get_executor(), workers.get_executor()};
  std::future<extora::asio::operation_result<extora::put_object_result>> putFuture = adapter.async_put_object(
      extora::bucket_name{"bucket"}, extora::object_key{"key"}, source, metadata, options, boost::asio::use_future);
  io.run();
  EXPECT_TRUE(extora::succeeded(putFuture.get().error));

  io.restart();
  std::future<extora::asio::operation_result<extora::open_object_result>> openFuture =
      adapter.async_open_object(extora::bucket_name{"bucket"}, extora::object_key{"key"}, boost::asio::use_future);
  io.run();
  extora::asio::operation_result<extora::open_object_result> openResult = openFuture.get();
  ASSERT_TRUE(extora::succeeded(openResult.error)) << openResult.error.message;
  ASSERT_TRUE(openResult.value.reader);

  io.restart();
  std::array<std::byte, 32> buffer;
  std::future<extora::asio::operation_result<extora::object_read_result>> readFuture =
      adapter.async_read_some(*openResult.value.reader, buffer.data(), buffer.size(), boost::asio::use_future);
  io.run();
  const extora::asio::operation_result<extora::object_read_result> readResult = readFuture.get();
  ASSERT_TRUE(extora::succeeded(readResult.error)) << readResult.error.message;
  ASSERT_TRUE(extora::succeeded(readResult.value.error)) << readResult.value.error.message;
  EXPECT_TRUE(readResult.value.end_of_stream);
  EXPECT_EQ(std::string(reinterpret_cast<const char*>(buffer.data()), readResult.value.bytes_read),
            "asynchronous data");
  workers.join();
}

} // namespace
} // namespace extoraTest
