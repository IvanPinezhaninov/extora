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

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <future>
#include <mutex>
#include <string>
#include <string_view>

#include <gtest/gtest.h>

#include "PublicTestSupport.h"
#include "SqliteTestSupport.h"

namespace extoraTest {

namespace {

extora::storage_error putText(extora::object_store& store, std::string_view key, std::string_view text)
{
  VectorReader reader{bytesFromString(text), 7};
  extora::object_metadata metadata;
  extora::put_object_options options;
  options.expected_content_length = text.size();
  return store.put_object(extora::bucket_name{"bucket"}, extora::object_key{std::string{key}}, reader, metadata,
                          ignoredPutResult(), options);
}

class PausingTwoChunkReader final : public extora::object_reader {
public:
  extora::object_read_result read(std::byte* data, std::size_t size) override
  {
    if (m_chunk == 1) {
      std::unique_lock<std::mutex> lock{m_mutex};
      m_paused = true;
      m_condition.notify_all();
      m_condition.wait(lock, [this]() { return m_released; });
    }

    const char* chunk = m_chunk == 0 ? "ABCD" : "EFGH";
    const std::size_t bytesRead = std::min<std::size_t>(size, 4);
    for (std::size_t index = 0; index < bytesRead; ++index)
      data[index] = static_cast<std::byte>(chunk[index]);

    ++m_chunk;
    return extora::object_read_result{bytesRead, m_chunk == 2, {}};
  }

  bool waitUntilPaused()
  {
    std::unique_lock<std::mutex> lock{m_mutex};
    return m_condition.wait_for(lock, std::chrono::seconds{5}, [this]() { return m_paused; });
  }

  void release()
  {
    const std::lock_guard<std::mutex> lock{m_mutex};
    m_released = true;
    m_condition.notify_all();
  }

private:
  std::mutex m_mutex;
  std::condition_variable m_condition;
  std::size_t m_chunk = 0;
  bool m_paused = false;
  bool m_released = false;
};

} // namespace

TEST(StorageReclamationTest, DoesNotReuseBytesReservedByAParallelUpload)
{
  const std::string root = makeTempRoot();
  extora::object_store_options options;
  options.root_directory = root;
  options.segment_capacity = 8;
  options.max_extent_size = 4;
  options.durability = extora::storage_durability::relaxed;

  extora::storage_error error;
  std::unique_ptr<extora::managed_object_store> store = extora::open_object_store(options, error);
  ASSERT_TRUE(succeeded(error));
  ASSERT_TRUE(store);
  ASSERT_TRUE(succeeded(store->create_bucket(extora::bucket_name{"bucket"})));
  ASSERT_TRUE(succeeded(putText(*store, "deleted", "old!")));
  ASSERT_TRUE(succeeded(
      store->delete_object(extora::bucket_name{"bucket"}, extora::object_key{"deleted"}, ignoredDeleteResult())));

  PausingTwoChunkReader reader;
  extora::put_object_options putOptions;
  putOptions.expected_content_length = 8;
  std::future<extora::storage_error> upload = std::async(std::launch::async, [&]() {
    return store->put_object(extora::bucket_name{"bucket"}, extora::object_key{"parallel"}, reader,
                             extora::object_metadata{}, ignoredPutResult(), putOptions);
  });

  if (!reader.waitUntilPaused()) {
    reader.release();
    static_cast<void>(upload.get());
    FAIL() << "parallel upload did not reach its second chunk";
    return;
  }

  const extora::storage_error reclaimError = store->reclaim_storage(ignoredReclaimResult());
  reader.release();
  const extora::storage_error uploadError = upload.get();
  ASSERT_TRUE(succeeded(reclaimError)) << reclaimError.message;
  ASSERT_TRUE(succeeded(uploadError)) << uploadError.message;

  VectorWriter writer;
  ASSERT_TRUE(succeeded(readObject(*store, extora::bucket_name{"bucket"}, extora::object_key{"parallel"}, writer, {},
                                   ignoredOpenObjectResult())));
  EXPECT_EQ(stringFromBytes(writer.bytes()), "ABCDEFGH");
}

TEST(StorageReclamationTest, AutomaticallyReusesAbandonedDataAtTheConfiguredThreshold)
{
  const std::string root = makeTempRoot();
  extora::object_store_options options;
  options.root_directory = root;
  options.segment_capacity = 1024;
  options.max_extent_size = 1024;
  options.dedup_min_object_size = 1;
  options.automatic_reclamation_threshold_bytes = 1;

  extora::storage_error error;
  std::unique_ptr<extora::object_store> store = extora::open_object_store(options, error);
  ASSERT_TRUE(succeeded(error));
  ASSERT_TRUE(store);
  ASSERT_TRUE(succeeded(store->create_bucket(extora::bucket_name{"bucket"})));

  ASSERT_TRUE(succeeded(putText(*store, "first", "same body")));
  ASSERT_TRUE(succeeded(putText(*store, "duplicate", "same body")));

  const std::string databasePath = joinPath(root, "index.sqlite3");
  ASSERT_EQ(countPhysicalExtentsInState(databasePath, sqliteExtentStateAbandoned), 1);
  EXPECT_EQ(scalarQuery(databasePath, "SELECT COALESCE(SUM(length), 0) FROM physical_extents WHERE state = 4"), 9);

  ASSERT_TRUE(succeeded(putText(*store, "different", "new value")));

  EXPECT_EQ(countPhysicalExtentsInState(databasePath, sqliteExtentStateAbandoned), 0);
  EXPECT_EQ(countPhysicalExtentsInState(databasePath, sqliteExtentStateFree), 0);
  EXPECT_EQ(storageExtentLength(databasePath, 1, 9), 9);
  EXPECT_EQ(scalarQuery(databasePath, "SELECT MAX(segment_id) FROM physical_extents"), 1);
}

TEST(StorageReclamationTest, DisablesAutomaticReclamationByDefault)
{
  const std::string root = makeTempRoot();
  extora::object_store_options options;
  options.root_directory = root;
  options.segment_capacity = 1024;
  options.max_extent_size = 1024;
  options.dedup_min_object_size = 1;

  extora::storage_error error;
  std::unique_ptr<extora::object_store> store = extora::open_object_store(options, error);
  ASSERT_TRUE(succeeded(error));
  ASSERT_TRUE(store);
  ASSERT_TRUE(succeeded(store->create_bucket(extora::bucket_name{"bucket"})));

  ASSERT_TRUE(succeeded(putText(*store, "first", "same body")));
  ASSERT_TRUE(succeeded(putText(*store, "duplicate", "same body")));
  ASSERT_TRUE(succeeded(putText(*store, "different", "new value")));

  const std::string databasePath = joinPath(root, "index.sqlite3");
  EXPECT_EQ(countPhysicalExtentsInState(databasePath, sqliteExtentStateAbandoned), 1);
  EXPECT_EQ(storageExtentLength(databasePath, 1, 18), 9);
}

TEST(StorageReclamationTest, ZeroAutomaticReclamationThresholdReusesAnyAbandonedData)
{
  const std::string root = makeTempRoot();
  extora::object_store_options options;
  options.root_directory = root;
  options.segment_capacity = 1024;
  options.max_extent_size = 1024;
  options.dedup_min_object_size = 1;
  options.automatic_reclamation_threshold_bytes = 0;

  extora::storage_error error;
  std::unique_ptr<extora::object_store> store = extora::open_object_store(options, error);
  ASSERT_TRUE(succeeded(error));
  ASSERT_TRUE(store);
  ASSERT_TRUE(succeeded(store->create_bucket(extora::bucket_name{"bucket"})));

  ASSERT_TRUE(succeeded(putText(*store, "first", "same body")));
  ASSERT_TRUE(succeeded(putText(*store, "duplicate", "same body")));

  const std::string databasePath = joinPath(root, "index.sqlite3");
  ASSERT_EQ(countPhysicalExtentsInState(databasePath, sqliteExtentStateAbandoned), 1);

  ASSERT_TRUE(succeeded(putText(*store, "different", "new value")));

  EXPECT_EQ(countPhysicalExtentsInState(databasePath, sqliteExtentStateAbandoned), 0);
  EXPECT_EQ(countPhysicalExtentsInState(databasePath, sqliteExtentStateFree), 0);
  EXPECT_EQ(storageExtentLength(databasePath, 1, 9), 9);
  EXPECT_EQ(scalarQuery(databasePath, "SELECT MAX(segment_id) FROM physical_extents"), 1);
}

TEST(StorageReclamationTest, PreservesCorruptedObjectsUntilExplicitlyDeleted)
{
  const std::string root = makeTempRoot();
  extora::object_store_options options;
  options.root_directory = root;
  options.segment_capacity = 1024;
  options.max_extent_size = 1024;

  extora::storage_error error;
  std::unique_ptr<extora::managed_object_store> store = extora::open_object_store(options, error);
  ASSERT_TRUE(succeeded(error));
  ASSERT_TRUE(store);
  ASSERT_TRUE(succeeded(store->create_bucket(extora::bucket_name{"bucket"})));
  ASSERT_TRUE(succeeded(putText(*store, "damaged", "object body")));

  const std::string segmentPath = joinPath(joinPath(root, "segments"), "0000000000000001.dat");
  ASSERT_TRUE(overwriteFileByte(segmentPath, 0, std::byte{'X'}));

  extora::open_object_options openOptions;
  openOptions.verify_integrity = true;
  VectorWriter writer;
  EXPECT_EQ(readObject(*store, extora::bucket_name{"bucket"}, extora::object_key{"damaged"}, writer, openOptions,
                       ignoredOpenObjectResult())
                .code,
            extora::storage_error_code::checksum_mismatch);

  const std::string databasePath = joinPath(root, "index.sqlite3");
  EXPECT_EQ(countObjectsInState(databasePath, sqliteObjectStateCorrupted), 1);
  EXPECT_EQ(countPhysicalExtentsInState(databasePath, sqliteExtentStateCommitted), 1);

  extora::reclaim_storage_result preservedResult;
  ASSERT_TRUE(succeeded(store->reclaim_storage(preservedResult)));
  EXPECT_EQ(preservedResult.reclaimed_bytes, 0u);
  EXPECT_EQ(preservedResult.reclaimed_extent_count, 0u);
  EXPECT_EQ(countObjectsInState(databasePath, sqliteObjectStateCorrupted), 1);
  EXPECT_EQ(countPhysicalExtentsInState(databasePath, sqliteExtentStateCommitted), 1);

  store.reset();
  store = extora::open_object_store(options, error);
  ASSERT_TRUE(succeeded(error));
  ASSERT_TRUE(store);
  extora::object_info info;
  EXPECT_EQ(store->head_object(extora::bucket_name{"bucket"}, extora::object_key{"damaged"}, info).code,
            extora::storage_error_code::object_corrupted);

  extora::reclaim_storage_options reclaimOptions;
  reclaimOptions.delete_corrupted_objects = true;
  extora::reclaim_storage_result deletedResult;
  ASSERT_TRUE(succeeded(store->reclaim_storage(deletedResult, reclaimOptions)));
  EXPECT_EQ(deletedResult.reclaimed_bytes, 11u);
  EXPECT_EQ(deletedResult.reclaimed_extent_count, 1u);
  EXPECT_EQ(deletedResult.remaining_reclaimable_bytes, 0u);
  EXPECT_EQ(deletedResult.remaining_reclaimable_extent_count, 0u);
  EXPECT_EQ(store->head_object(extora::bucket_name{"bucket"}, extora::object_key{"damaged"}, info).code,
            extora::storage_error_code::object_not_found);
  EXPECT_EQ(countObjectsInState(databasePath, sqliteObjectStateCorrupted), 0);
  EXPECT_EQ(countObjectsInState(databasePath, sqliteObjectStateDeleted), 1);
  EXPECT_EQ(countPhysicalExtentsInState(databasePath, sqliteExtentStateCommitted), 0);
  EXPECT_EQ(countPhysicalExtentsInState(databasePath, sqliteExtentStateFree), 1);
}

} // namespace extoraTest
