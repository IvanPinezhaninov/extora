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
#include <atomic>
#include <chrono>
#include <cstddef>
#include <string>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

#include "PublicTestSupport.h"
#include "extora/core/sqlite_object_index.h"

namespace extoraTest {

TEST(SqliteIndexConcurrencyTest, ServesManyConcurrentMetadataReaders)
{
  constexpr std::size_t threadCount = 16;
  constexpr std::size_t readsPerThread = 100;
  const extora::bucket_name bucket{"photos"};
  const extora::object_key key{"object"};

  const std::string root = makeTempRoot();
  extora::core::sqlite_object_index index{joinPath(root, "index.sqlite3"), 1024};
  ASSERT_TRUE(succeeded(index.open()));
  ASSERT_TRUE(succeeded(index.create_bucket(bucket)));

  extora::core::indexed_object object;
  object.bucket = bucket;
  object.key = key;
  object.metadata.content_length = 0;
  object.metadata.custom_metadata.push_back(extora::metadata_entry{"owner", "Alice"});
  object.payload.internal_checksum =
      extora::object_checksum{extora::checksum_algorithm_name{"test-integrity"}, "empty"};
  object.etag = "empty-etag";
  object.version_id = extora::object_version_id{extora::null_version_id};
  object.created_at = std::chrono::system_clock::now();
  object.modified_at = object.created_at;
  ASSERT_TRUE(succeeded(index.publish_object(object)));

  std::atomic<std::size_t> readyReaders{0};
  std::atomic<bool> start{false};
  std::array<extora::storage_error, threadCount> errors;
  std::array<bool, threadCount> validResults{};
  std::vector<std::thread> readers;
  readers.reserve(threadCount);

  for (std::size_t readerIndex = 0; readerIndex < threadCount; ++readerIndex) {
    readers.emplace_back([&, readerIndex]() {
      readyReaders.fetch_add(1, std::memory_order_release);
      while (!start.load(std::memory_order_acquire))
        std::this_thread::yield();

      validResults[readerIndex] = true;
      for (std::size_t readIndex = 0; readIndex < readsPerThread; ++readIndex) {
        extora::core::indexed_object found;
        errors[readerIndex] = index.find_object_metadata(bucket, key, found);
        if (failed(errors[readerIndex])) break;
        if (found.etag != object.etag || found.metadata.custom_metadata.size() != 1 ||
            found.metadata.custom_metadata[0].name != "owner" || found.metadata.custom_metadata[0].value != "Alice") {
          validResults[readerIndex] = false;
          break;
        }
      }
    });
  }

  while (readyReaders.load(std::memory_order_acquire) != threadCount)
    std::this_thread::yield();
  start.store(true, std::memory_order_release);

  for (std::thread& reader : readers)
    reader.join();

  for (std::size_t readerIndex = 0; readerIndex < threadCount; ++readerIndex) {
    EXPECT_TRUE(succeeded(errors[readerIndex])) << errors[readerIndex].message;
    EXPECT_TRUE(validResults[readerIndex]);
  }
}

} // namespace extoraTest
