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
#include <condition_variable>
#include <cstddef>
#include <filesystem>
#include <future>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>

#include <gtest/gtest.h>

#include "PublicTestSupport.h"
#include "SqliteTestSupport.h"

namespace extoraTest {

namespace {

extora::storage_error putKnownText(extora::object_store& store, std::string_view key, std::string_view text)
{
  VectorReader reader{bytesFromString(text), 3};
  extora::put_object_options options;
  options.expected_content_length = text.size();
  return store.put_object(extora::bucket_name{"bucket"}, extora::object_key{std::string{key}}, reader,
                          extora::object_metadata{}, ignoredPutResult(), options);
}

void createEvacuationScenario(extora::managed_object_store& store)
{
  ASSERT_TRUE(succeeded(putKnownText(store, "small", "aaaa")));
  ASSERT_TRUE(succeeded(putKnownText(store, "large", "0123456789abcdefghijklmn")));
  ASSERT_TRUE(succeeded(
      store.delete_object(extora::bucket_name{"bucket"}, extora::object_key{"small"}, ignoredDeleteResult())));
  ASSERT_TRUE(succeeded(store.reclaim_storage(ignoredReclaimResult())));
}

std::unique_ptr<extora::managed_object_store>
openCompactionStore(const std::string& root, std::shared_ptr<extora::operation_observer> observer = {})
{
  extora::object_store_options options;
  options.root_directory = root;
  options.segment_capacity = 8;
  options.max_extent_size = 8;
  options.observer = observer;
  options.operation_progress_interval_bytes = 1;

  extora::storage_error error;
  std::unique_ptr<extora::managed_object_store> store = extora::open_object_store(options, error);
  EXPECT_TRUE(succeeded(error)) << error.message;
  return store;
}

class BlockingCompactionObserver final : public extora::operation_observer {
public:
  void on_operation_event(const extora::operation_event& event) override
  {
    if (event.type != extora::operation_type::compact_storage || event.stage != extora::operation_stage::progress)
      return;

    std::unique_lock<std::mutex> lock{m_mutex};
    if (m_blocked) return;
    m_blocked = true;
    m_condition.notify_all();
    m_condition.wait(lock, [&]() { return m_released; });
  }

  bool waitUntilBlocked()
  {
    std::unique_lock<std::mutex> lock{m_mutex};
    return m_condition.wait_for(lock, std::chrono::seconds{5}, [&]() { return m_blocked; });
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
  bool m_blocked = false;
  bool m_released = false;
};

std::string readOpenedObject(extora::open_object_result& opened)
{
  std::string text;
  std::array<std::byte, 8> buffer;
  while (true) {
    const extora::object_read_result result = opened.reader->read(buffer.data(), buffer.size());
    EXPECT_TRUE(succeeded(result.error)) << result.error.message;
    for (std::size_t index = 0; index < result.bytes_read; ++index)
      text.push_back(static_cast<char>(buffer[index]));
    if (result.end_of_stream) break;
    if (result.bytes_read == 0) break;
  }
  return text;
}

} // namespace

TEST(StorageCompactionTest, CompactsFragmentedPayloadAndRetainsUnmovedExtents)
{
  const std::string root = makeTempRoot();
  std::unique_ptr<extora::managed_object_store> store = openCompactionStore(root);
  ASSERT_TRUE(store);
  ASSERT_TRUE(succeeded(store->create_bucket(extora::bucket_name{"bucket"})));
  createEvacuationScenario(*store);

  const std::string databasePath = joinPath(root, "index.sqlite3");
  ASSERT_EQ(scalarQuery(databasePath, R"sql(
    SELECT COUNT(*)
    FROM payload_extents AS extent
    JOIN objects AS object ON object.payload_id = extent.payload_id
    WHERE object.key = 'large' AND object.state = 1
    )sql"),
            4);

  extora::compact_storage_result result;
  ASSERT_TRUE(succeeded(store->compact_storage(result)));
  EXPECT_EQ(result.examined_payload_count, 1);
  EXPECT_EQ(result.compacted_payload_count, 1);
  EXPECT_EQ(result.compacted_bytes, 4);
  EXPECT_EQ(result.replaced_extent_count, 1);
  EXPECT_EQ(result.compacted_extent_count, 1);
  EXPECT_EQ(scalarQuery(databasePath, R"sql(
    SELECT COUNT(*)
    FROM payload_extents AS extent
    JOIN objects AS object ON object.payload_id = extent.payload_id
    WHERE object.key = 'large' AND object.state = 1
    )sql"),
            4);
  EXPECT_EQ(scalarQuery(databasePath, R"sql(
    SELECT COUNT(*)
    FROM payload_extents AS link
    JOIN objects AS object ON object.payload_id = link.payload_id
    JOIN physical_extents AS physical ON physical.id = link.physical_extent_id
    WHERE object.key = 'large'
      AND object.state = 1
      AND physical.segment_id IN (2, 3)
    )sql"),
            2);
  EXPECT_EQ(scalarQuery(databasePath, "SELECT COUNT(DISTINCT segment_id) FROM physical_extents"), 4);

  VectorWriter writer;
  const extora::storage_error readError = readObject(*store, extora::bucket_name{"bucket"}, extora::object_key{"large"},
                                                     writer, {}, ignoredOpenObjectResult());
  ASSERT_TRUE(succeeded(readError)) << readError.message;
  EXPECT_EQ(stringFromBytes(writer.bytes()), "0123456789abcdefghijklmn");

  extora::compact_storage_result second;
  ASSERT_TRUE(succeeded(store->compact_storage(second)));
  EXPECT_EQ(second.examined_payload_count, 1);
  EXPECT_EQ(second.compacted_payload_count, 0);

  store.reset();
  store = openCompactionStore(root);
  ASSERT_TRUE(store);
  VectorWriter reopenedWriter;
  ASSERT_TRUE(succeeded(readObject(*store, extora::bucket_name{"bucket"}, extora::object_key{"large"}, reopenedWriter,
                                   {}, ignoredOpenObjectResult())));
  EXPECT_EQ(stringFromBytes(reopenedWriter.bytes()), "0123456789abcdefghijklmn");
}

TEST(StorageCompactionTest, MovesDataFromFullerHigherSegmentIntoLowerSegment)
{
  const std::string root = makeTempRoot();
  extora::object_store_options options;
  options.root_directory = root;
  options.segment_capacity = 8;
  options.max_extent_size = 8;
  options.durability = extora::storage_durability::relaxed;

  extora::storage_error error;
  std::unique_ptr<extora::managed_object_store> store = extora::open_object_store(options, error);
  ASSERT_TRUE(succeeded(error));
  ASSERT_TRUE(store);
  ASSERT_TRUE(succeeded(store->create_bucket(extora::bucket_name{"bucket"})));
  ASSERT_TRUE(succeeded(putKnownText(*store, "lower", "L")));
  ASSERT_TRUE(succeeded(putKnownText(*store, "lower-filler", "1234567")));
  ASSERT_TRUE(succeeded(putKnownText(*store, "higher", "HHHHHH")));
  ASSERT_TRUE(succeeded(putKnownText(*store, "higher-filler", "12")));

  ASSERT_TRUE(succeeded(
      store->delete_object(extora::bucket_name{"bucket"}, extora::object_key{"lower-filler"}, ignoredDeleteResult())));
  ASSERT_TRUE(succeeded(
      store->delete_object(extora::bucket_name{"bucket"}, extora::object_key{"higher-filler"}, ignoredDeleteResult())));
  ASSERT_TRUE(succeeded(store->reclaim_storage(ignoredReclaimResult())));

  extora::compact_storage_result result;
  ASSERT_TRUE(succeeded(store->compact_storage(result)));
  EXPECT_EQ(result.compacted_payload_count, 1);
  EXPECT_EQ(result.compacted_bytes, 6);

  const std::filesystem::path segments = std::filesystem::path{root} / "segments";
  EXPECT_TRUE(std::filesystem::is_regular_file(segments / "0000000000000001.dat"));
  EXPECT_FALSE(std::filesystem::exists(segments / "0000000000000002.dat"));

  const std::string databasePath = joinPath(root, "index.sqlite3");
  EXPECT_EQ(scalarQuery(databasePath, R"sql(
    SELECT COUNT(DISTINCT physical.segment_id)
    FROM objects AS object
    JOIN payload_extents AS link ON link.payload_id = object.payload_id
    JOIN physical_extents AS physical ON physical.id = link.physical_extent_id
    WHERE object.state = 1
    )sql"),
            1);
  EXPECT_EQ(scalarQuery(databasePath, R"sql(
    SELECT MAX(physical.segment_id)
    FROM objects AS object
    JOIN payload_extents AS link ON link.payload_id = object.payload_id
    JOIN physical_extents AS physical ON physical.id = link.physical_extent_id
    WHERE object.state = 1
    )sql"),
            1);

  VectorWriter writer;
  ASSERT_TRUE(succeeded(readObject(*store, extora::bucket_name{"bucket"}, extora::object_key{"higher"}, writer, {},
                                   ignoredOpenObjectResult())));
  EXPECT_EQ(stringFromBytes(writer.bytes()), "HHHHHH");

  ASSERT_TRUE(succeeded(putKnownText(*store, "tail", "T")));
  EXPECT_TRUE(std::filesystem::is_regular_file(segments / "0000000000000001.dat"));
  EXPECT_FALSE(std::filesystem::exists(segments / "0000000000000002.dat"));
  EXPECT_EQ(scalarQuery(databasePath, R"sql(
    SELECT MAX(physical.segment_id)
    FROM objects AS object
    JOIN payload_extents AS link ON link.payload_id = object.payload_id
    JOIN physical_extents AS physical ON physical.id = link.physical_extent_id
    WHERE object.state = 1
    )sql"),
            1);
}

TEST(StorageCompactionTest, AllowsIoAndProtectsReadersDuringCompaction)
{
  const std::string root = makeTempRoot();
  const std::shared_ptr<BlockingCompactionObserver> observer = std::make_shared<BlockingCompactionObserver>();
  std::unique_ptr<extora::managed_object_store> store = openCompactionStore(root, observer);
  ASSERT_TRUE(store);
  ASSERT_TRUE(succeeded(store->create_bucket(extora::bucket_name{"bucket"})));
  createEvacuationScenario(*store);

  extora::compact_storage_result compacted;
  std::future<extora::storage_error> compaction =
      std::async(std::launch::async, [&]() { return store->compact_storage(compacted); });
  ASSERT_TRUE(observer->waitUntilBlocked());

  extora::open_object_result opened;
  ASSERT_TRUE(succeeded(store->open_object(extora::bucket_name{"bucket"}, extora::object_key{"large"}, opened)));
  ASSERT_TRUE(opened.reader);
  ASSERT_TRUE(succeeded(putKnownText(*store, "concurrent", "write")));

  extora::compact_storage_result rejected;
  EXPECT_EQ(store->compact_storage(rejected).code, extora::storage_error_code::concurrency_limit_exceeded);

  observer->release();
  ASSERT_TRUE(succeeded(compaction.get()));
  EXPECT_EQ(compacted.compacted_payload_count, 1);

  const std::string databasePath = joinPath(root, "index.sqlite3");
  EXPECT_EQ(countPhysicalExtentsInState(databasePath, sqliteExtentStateGarbage), 1);
  EXPECT_EQ(readOpenedObject(opened), "0123456789abcdefghijklmn");
  opened.reader.reset();

  ASSERT_TRUE(succeeded(store->reclaim_storage(ignoredReclaimResult())));
  EXPECT_EQ(countPhysicalExtentsInState(databasePath, sqliteExtentStateGarbage), 0);

  VectorWriter concurrentWriter;
  ASSERT_TRUE(succeeded(readObject(*store, extora::bucket_name{"bucket"}, extora::object_key{"concurrent"},
                                   concurrentWriter, {}, ignoredOpenObjectResult())));
  EXPECT_EQ(stringFromBytes(concurrentWriter.bytes()), "write");
}

TEST(StorageCompactionTest, QuarantinesPayloadDamagedBeforeCompaction)
{
  const std::string root = makeTempRoot();
  std::unique_ptr<extora::managed_object_store> store = openCompactionStore(root);
  ASSERT_TRUE(store);
  ASSERT_TRUE(succeeded(store->create_bucket(extora::bucket_name{"bucket"})));
  createEvacuationScenario(*store);

  const std::string segmentPath = joinPath(joinPath(root, "segments"), "0000000000000001.dat");
  ASSERT_TRUE(overwriteFileByte(segmentPath, 4, std::byte{'X'}));

  extora::compact_storage_result result;
  EXPECT_EQ(store->compact_storage(result).code, extora::storage_error_code::checksum_mismatch);
  EXPECT_EQ(result.compacted_payload_count, 0);

  extora::object_info info;
  EXPECT_EQ(store->head_object(extora::bucket_name{"bucket"}, extora::object_key{"large"}, info).code,
            extora::storage_error_code::object_corrupted);
  EXPECT_EQ(countObjectsInState(joinPath(root, "index.sqlite3"), sqliteObjectStateCorrupted), 1);
}

TEST(StorageCompactionTest, DeletesEmptySegmentsAndRestartsNumbering)
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
  ASSERT_TRUE(succeeded(putKnownText(*store, "large", "0123456789abcdefghij")));

  const std::filesystem::path segments = std::filesystem::path{root} / "segments";
  const std::filesystem::path firstSegment = segments / "0000000000000001.dat";
  const std::filesystem::path secondSegment = segments / "0000000000000002.dat";
  const std::filesystem::path thirdSegment = segments / "0000000000000003.dat";
  const std::filesystem::path fourthSegment = segments / "0000000000000004.dat";
  ASSERT_TRUE(std::filesystem::is_regular_file(firstSegment));
  ASSERT_TRUE(std::filesystem::is_regular_file(secondSegment));
  ASSERT_TRUE(std::filesystem::is_regular_file(thirdSegment));
  ASSERT_FALSE(std::filesystem::exists(fourthSegment));

  ASSERT_TRUE(succeeded(
      store->delete_object(extora::bucket_name{"bucket"}, extora::object_key{"large"}, ignoredDeleteResult())));
  ASSERT_TRUE(succeeded(store->reclaim_storage(ignoredReclaimResult())));
  EXPECT_TRUE(std::filesystem::is_regular_file(firstSegment));
  EXPECT_TRUE(std::filesystem::is_regular_file(secondSegment));
  EXPECT_TRUE(std::filesystem::is_regular_file(thirdSegment));

  extora::compact_storage_options compactOptions;
  compactOptions.delete_empty_segments = false;
  extora::compact_storage_result retained;
  ASSERT_TRUE(succeeded(store->compact_storage(retained, compactOptions)));
  EXPECT_EQ(retained.segment_count_before, 3);
  EXPECT_EQ(retained.segment_count_after, 3);
  EXPECT_EQ(retained.removed_segment_count, 0);
  EXPECT_EQ(retained.released_bytes, 0);
  EXPECT_TRUE(std::filesystem::is_regular_file(firstSegment));
  EXPECT_TRUE(std::filesystem::is_regular_file(secondSegment));
  EXPECT_TRUE(std::filesystem::is_regular_file(thirdSegment));

  extora::compact_storage_result removed;
  ASSERT_TRUE(succeeded(store->compact_storage(removed)));
  EXPECT_EQ(removed.segment_count_before, 3);
  EXPECT_EQ(removed.segment_count_after, 0);
  EXPECT_EQ(removed.removed_segment_count, 3);
  EXPECT_EQ(removed.released_bytes, 24);
  EXPECT_FALSE(std::filesystem::exists(firstSegment));
  EXPECT_FALSE(std::filesystem::exists(secondSegment));
  EXPECT_FALSE(std::filesystem::exists(thirdSegment));

  const std::string databasePath = joinPath(root, "index.sqlite3");
  EXPECT_EQ(scalarQuery(databasePath, "SELECT COUNT(*) FROM physical_extents"), 3);
  EXPECT_EQ(countPhysicalExtentsInState(databasePath, sqliteExtentStateFree), 3);
  EXPECT_EQ(scalarQuery(databasePath, "SELECT SUM(length) FROM physical_extents"), 24);

  ASSERT_TRUE(succeeded(putKnownText(*store, "replacement", "0123456789ab")));
  EXPECT_TRUE(std::filesystem::is_regular_file(firstSegment));
  EXPECT_TRUE(std::filesystem::is_regular_file(secondSegment));
  EXPECT_FALSE(std::filesystem::exists(thirdSegment));
  EXPECT_EQ(scalarQuery(databasePath, "SELECT MIN(segment_id) FROM physical_extents"), 1);
  EXPECT_EQ(scalarQuery(databasePath, "SELECT MAX(segment_id) FROM physical_extents"), 3);

  ASSERT_TRUE(succeeded(putKnownText(*store, "overflow", "ABCDEFGHIJKLM")));
  EXPECT_TRUE(std::filesystem::is_regular_file(thirdSegment));
  EXPECT_TRUE(std::filesystem::is_regular_file(fourthSegment));
  EXPECT_EQ(scalarQuery(databasePath, "SELECT MAX(segment_id) FROM physical_extents"), 4);

  VectorWriter writer;
  ASSERT_TRUE(succeeded(readObject(*store, extora::bucket_name{"bucket"}, extora::object_key{"overflow"}, writer, {},
                                   ignoredOpenObjectResult())));
  EXPECT_EQ(stringFromBytes(writer.bytes()), "ABCDEFGHIJKLM");
}

TEST(StorageCompactionTest, CompactsIntoLowerSegmentAndContinuesAfterReopen)
{
  const std::string root = makeTempRoot();
  extora::object_store_options options;
  options.root_directory = root;
  options.segment_capacity = 8;
  options.max_extent_size = 8;
  options.durability = extora::storage_durability::relaxed;

  extora::storage_error error;
  std::unique_ptr<extora::managed_object_store> store = extora::open_object_store(options, error);
  ASSERT_TRUE(succeeded(error));
  ASSERT_TRUE(store);
  ASSERT_TRUE(succeeded(store->create_bucket(extora::bucket_name{"bucket"})));
  ASSERT_TRUE(succeeded(putKnownText(*store, "lower", "11111111")));
  ASSERT_TRUE(succeeded(putKnownText(*store, "upper", "22222222")));

  ASSERT_TRUE(succeeded(
      store->delete_object(extora::bucket_name{"bucket"}, extora::object_key{"lower"}, ignoredDeleteResult())));
  ASSERT_TRUE(succeeded(store->reclaim_storage(ignoredReclaimResult())));
  extora::compact_storage_result compacted;
  ASSERT_TRUE(succeeded(store->compact_storage(compacted)));

  const std::filesystem::path segments = std::filesystem::path{root} / "segments";
  const std::filesystem::path firstSegment = segments / "0000000000000001.dat";
  const std::filesystem::path secondSegment = segments / "0000000000000002.dat";
  EXPECT_TRUE(std::filesystem::is_regular_file(firstSegment));
  EXPECT_FALSE(std::filesystem::exists(secondSegment));

  const std::string databasePath = joinPath(root, "index.sqlite3");
  EXPECT_EQ(scalarQuery(databasePath, R"sql(
    SELECT COUNT(*)
    FROM physical_extents
    WHERE segment_id = 2
      AND offset = 0
      AND length = 8
      AND state = 6
    )sql"),
            1);

  store.reset();
  store = extora::open_object_store(options, error);
  ASSERT_TRUE(succeeded(error));
  ASSERT_TRUE(store);
  ASSERT_TRUE(succeeded(putKnownText(*store, "replacement", "33333333")));

  EXPECT_TRUE(std::filesystem::is_regular_file(firstSegment));
  EXPECT_TRUE(std::filesystem::is_regular_file(secondSegment));
  EXPECT_EQ(scalarQuery(databasePath, R"sql(
    SELECT physical.segment_id
    FROM objects AS object
    JOIN payload_extents AS link ON link.payload_id = object.payload_id
    JOIN physical_extents AS physical ON physical.id = link.physical_extent_id
    WHERE object.key = 'replacement'
      AND object.state = 1
    )sql"),
            2);
}

TEST(StorageCompactionTest, PacksSingleExtentPayloadsIntoTheMinimumSegmentCount)
{
  const std::string root = makeTempRoot();
  extora::object_store_options options;
  options.root_directory = root;
  options.segment_capacity = 8;
  options.max_extent_size = 8;
  options.durability = extora::storage_durability::relaxed;

  extora::storage_error error;
  std::unique_ptr<extora::managed_object_store> store = extora::open_object_store(options, error);
  ASSERT_TRUE(succeeded(error));
  ASSERT_TRUE(store);
  ASSERT_TRUE(succeeded(store->create_bucket(extora::bucket_name{"bucket"})));
  ASSERT_TRUE(succeeded(putKnownText(*store, "file-1", "aa")));
  ASSERT_TRUE(succeeded(putKnownText(*store, "file-2", "0123456789abcdefghijklmnopqr")));
  ASSERT_TRUE(succeeded(putKnownText(*store, "file-3", "bb")));

  ASSERT_TRUE(succeeded(
      store->delete_object(extora::bucket_name{"bucket"}, extora::object_key{"file-2"}, ignoredDeleteResult())));
  ASSERT_TRUE(succeeded(store->reclaim_storage(ignoredReclaimResult())));

  extora::compact_storage_result result;
  ASSERT_TRUE(succeeded(store->compact_storage(result)));
  EXPECT_EQ(result.segment_count_before, 4);
  EXPECT_EQ(result.segment_count_after, 1);
  EXPECT_EQ(result.examined_payload_count, 2);
  EXPECT_EQ(result.compacted_payload_count, 1);
  EXPECT_EQ(result.compacted_bytes, 2);
  EXPECT_EQ(result.removed_segment_count, 3);
  EXPECT_EQ(result.released_bytes, 24);

  const std::string databasePath = joinPath(root, "index.sqlite3");
  EXPECT_EQ(scalarQuery(databasePath, "SELECT COUNT(DISTINCT segment_id) FROM physical_extents"), 4);
  EXPECT_EQ(scalarQuery(databasePath, R"sql(
    SELECT COUNT(DISTINCT physical.segment_id)
    FROM payload_extents AS link
    JOIN physical_extents AS physical ON physical.id = link.physical_extent_id
    )sql"),
            1);

  std::size_t segmentCount = 0;
  for (const std::filesystem::directory_entry& entry :
       std::filesystem::directory_iterator{std::filesystem::path{root} / "segments"}) {
    if (entry.is_regular_file() && entry.path().extension() == ".dat") ++segmentCount;
  }
  EXPECT_EQ(segmentCount, 1);

  VectorWriter firstWriter;
  ASSERT_TRUE(succeeded(readObject(*store, extora::bucket_name{"bucket"}, extora::object_key{"file-1"}, firstWriter, {},
                                   ignoredOpenObjectResult())));
  EXPECT_EQ(stringFromBytes(firstWriter.bytes()), "aa");
  VectorWriter thirdWriter;
  ASSERT_TRUE(succeeded(readObject(*store, extora::bucket_name{"bucket"}, extora::object_key{"file-3"}, thirdWriter, {},
                                   ignoredOpenObjectResult())));
  EXPECT_EQ(stringFromBytes(thirdWriter.bytes()), "bb");

  extora::compact_storage_result second;
  ASSERT_TRUE(succeeded(store->compact_storage(second)));
  EXPECT_EQ(second.segment_count_before, 1);
  EXPECT_EQ(second.segment_count_after, 1);
  EXPECT_EQ(second.compacted_payload_count, 0);
  EXPECT_EQ(second.removed_segment_count, 0);
  EXPECT_EQ(second.released_bytes, 0);
}

} // namespace extoraTest
