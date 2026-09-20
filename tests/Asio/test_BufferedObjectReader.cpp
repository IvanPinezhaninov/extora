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
#include <chrono>
#include <cstddef>
#include <future>
#include <string>

#include <boost/asio/thread_pool.hpp>
#include <boost/asio/use_future.hpp>
#include <boost/system/system_error.hpp>

#include <gtest/gtest.h>

#include "PublicTestSupport.h"
#include "extora/asio/buffered_object_reader.h"
#include "extora/storage_error.h"

namespace extoraTest {
namespace {

TEST(BufferedObjectReaderTest, StreamsOwnedChunksAndSuccessfulEndOfStream)
{
  boost::asio::thread_pool executor{1};
  extora::asio::buffered_object_reader reader{executor.get_executor(), 2};

  std::future<void> writeFuture = reader.async_write(bytesFromString("abcdef"), boost::asio::use_future);
  writeFuture.get();

  std::array<std::byte, 4> buffer;
  extora::object_read_result result = reader.read(buffer.data(), buffer.size());
  ASSERT_TRUE(extora::succeeded(result.error)) << result.error.message;
  EXPECT_EQ(std::string(reinterpret_cast<const char*>(buffer.data()), result.bytes_read), "abcd");
  EXPECT_FALSE(result.end_of_stream);

  result = reader.read(buffer.data(), buffer.size());
  ASSERT_TRUE(extora::succeeded(result.error)) << result.error.message;
  EXPECT_EQ(std::string(reinterpret_cast<const char*>(buffer.data()), result.bytes_read), "ef");
  EXPECT_FALSE(result.end_of_stream);

  std::future<void> finishFuture = reader.async_finish({}, boost::asio::use_future);
  finishFuture.get();
  result = reader.read(buffer.data(), buffer.size());
  EXPECT_TRUE(extora::succeeded(result.error)) << result.error.message;
  EXPECT_TRUE(result.end_of_stream);
  EXPECT_EQ(result.bytes_read, 0U);
  executor.join();
}

TEST(BufferedObjectReaderTest, AppliesBackpressureUntilTheConsumerMakesSpace)
{
  boost::asio::thread_pool executor{1};
  extora::asio::buffered_object_reader reader{executor.get_executor(), 1};

  reader.async_write(bytesFromString("first"), boost::asio::use_future).get();
  std::future<void> pendingWrite = reader.async_write(bytesFromString("second"), boost::asio::use_future);
  EXPECT_EQ(pendingWrite.wait_for(std::chrono::milliseconds{0}), std::future_status::timeout);

  std::array<std::byte, 16> buffer;
  extora::object_read_result result = reader.read(buffer.data(), buffer.size());
  EXPECT_EQ(std::string(reinterpret_cast<const char*>(buffer.data()), result.bytes_read), "first");
  pendingWrite.get();

  result = reader.read(buffer.data(), buffer.size());
  EXPECT_EQ(std::string(reinterpret_cast<const char*>(buffer.data()), result.bytes_read), "second");
  reader.async_finish({}, boost::asio::use_future).get();
  EXPECT_TRUE(reader.read(buffer.data(), buffer.size()).end_of_stream);
  executor.join();
}

TEST(BufferedObjectReaderTest, PreservesATerminalSourceError)
{
  boost::asio::thread_pool executor{1};
  extora::asio::buffered_object_reader reader{executor.get_executor(), 1};
  const extora::storage_error sourceError =
      extora::make_error(extora::storage_error_code::source_failure, "network body failed");

  reader.async_finish(sourceError, boost::asio::use_future).get();
  std::array<std::byte, 1> buffer;
  const extora::object_read_result first = reader.read(buffer.data(), buffer.size());
  const extora::object_read_result second = reader.read(buffer.data(), buffer.size());

  EXPECT_EQ(first.error.code, extora::storage_error_code::source_failure);
  EXPECT_EQ(first.error.message, "network body failed");
  EXPECT_FALSE(first.end_of_stream);
  EXPECT_EQ(second.error.code, first.error.code);
  EXPECT_EQ(second.error.message, first.error.message);
  executor.join();
}

TEST(BufferedObjectReaderTest, CancelsAProducerWaitingForBufferSpace)
{
  boost::asio::thread_pool executor{1};
  extora::asio::buffered_object_reader reader{executor.get_executor(), 1};

  reader.async_write(bytesFromString("buffered"), boost::asio::use_future).get();
  std::future<void> pendingWrite = reader.async_write(bytesFromString("waiting"), boost::asio::use_future);
  ASSERT_EQ(pendingWrite.wait_for(std::chrono::milliseconds{0}), std::future_status::timeout);

  reader.cancel();
  EXPECT_THROW(pendingWrite.get(), boost::system::system_error);
  executor.join();
}

} // namespace
} // namespace extoraTest
