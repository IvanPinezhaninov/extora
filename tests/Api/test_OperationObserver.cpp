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
#include <cstdint>
#include <cstring>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "ApiTestSupport.h"
#include "PublicTestSupport.h"

namespace extoraTest {

namespace {

struct OperationErrorSnapshot {
  extora::storage_error_code code = extora::storage_error_code::none;
  std::string message;
};

struct OperationEventSnapshot {
  std::uint64_t operationId = 0;
  extora::operation_type type = extora::operation_type::put_object;
  extora::operation_stage stage = extora::operation_stage::started;
  std::string bucket;
  std::string key;
  std::optional<std::string> uploadId;
  std::optional<std::uint32_t> partNumber;
  std::optional<std::uint64_t> readOffset;
  std::uint64_t bytesProcessed = 0;
  std::optional<std::uint64_t> totalBytes;
  std::optional<OperationErrorSnapshot> error;
};

class RecordingOperationObserver final : public extora::operation_observer {
public:
  void on_operation_event(const extora::operation_event& event) override
  {
    OperationEventSnapshot snapshot;
    snapshot.operationId = event.operation_id;
    snapshot.type = event.type;
    snapshot.stage = event.stage;
    snapshot.bucket = event.bucket;
    snapshot.key = event.key;
    if (event.upload_id.has_value()) snapshot.uploadId = std::string{*event.upload_id};
    snapshot.partNumber = event.part_number;
    snapshot.readOffset = event.read_offset;
    snapshot.bytesProcessed = event.bytes_processed;
    snapshot.totalBytes = event.total_bytes;
    if (event.error.has_value()) {
      snapshot.error = OperationErrorSnapshot{event.error->code, std::string{event.error->message}};
    }
    m_events.push_back(snapshot);
  }

  const std::vector<OperationEventSnapshot>& events() const
  {
    return m_events;
  }

  void clear()
  {
    m_events.clear();
  }

private:
  std::vector<OperationEventSnapshot> m_events;
};

class ThrowingOperationObserver final : public extora::operation_observer {
public:
  void on_operation_event(const extora::operation_event&) override
  {
    throw std::runtime_error{"observer failed"};
  }
};

class PartiallyFailingReader final : public extora::object_reader {
public:
  extora::object_read_result read(std::byte* data, std::size_t size) override
  {
    if (m_delivered) {
      return extora::object_read_result{
          0, false, extora::make_error(extora::storage_error_code::source_failure, "reader failed after data")};
    }

    constexpr char content[] = "abc";
    const std::size_t bytesRead = std::min(size, sizeof(content) - 1);
    std::memcpy(data, content, bytesRead);
    m_delivered = true;
    return extora::object_read_result{bytesRead, false, {}};
  }

private:
  bool m_delivered = false;
};

class OperationObserverApiTest : public testing::Test {
protected:
  void SetUp() override
  {
    m_root = makeTempRoot();
    extora::object_store_options options;
    options.root_directory = m_root;
    options.segment_capacity = 1024 * 1024;
    options.observer = m_observer;
    options.operation_progress_interval_bytes = 4;

    extora::storage_error error;
    m_store = extora::open_object_store(options, error);
    ASSERT_TRUE(succeeded(error)) << error.message;
    ASSERT_TRUE(m_store);
    ASSERT_TRUE(succeeded(m_store->create_bucket(extora::bucket_name{"photos"})));
  }

  std::shared_ptr<RecordingOperationObserver> m_observer = std::make_shared<RecordingOperationObserver>();
  std::unique_ptr<extora::managed_object_store> m_store;
  std::string m_root;
};

TEST_F(OperationObserverApiTest, ReportsKnownLengthPutLifecycleAndProgress)
{
  const std::string content = "0123456789";
  VectorReader reader{bytesFromString(content), 3};
  extora::object_metadata metadata;
  extora::put_object_options options;
  options.expected_content_length = content.size();

  ASSERT_TRUE(succeeded(m_store->put_object(extora::bucket_name{"photos"}, extora::object_key{"large"}, reader,
                                            metadata, ignoredPutResult(), options)));

  const std::vector<OperationEventSnapshot>& events = m_observer->events();
  ASSERT_EQ(events.size(), 4);
  EXPECT_EQ(events[0].stage, extora::operation_stage::started);
  EXPECT_EQ(events[1].stage, extora::operation_stage::progress);
  EXPECT_EQ(events[1].bytesProcessed, 6);
  EXPECT_EQ(events[2].stage, extora::operation_stage::progress);
  EXPECT_EQ(events[2].bytesProcessed, content.size());
  EXPECT_EQ(events[3].stage, extora::operation_stage::completed);
  EXPECT_EQ(events[3].bytesProcessed, content.size());

  for (const OperationEventSnapshot& event : events) {
    EXPECT_EQ(event.operationId, events[0].operationId);
    EXPECT_EQ(event.type, extora::operation_type::put_object);
    EXPECT_EQ(event.bucket, "photos");
    EXPECT_EQ(event.key, "large");
    EXPECT_EQ(event.totalBytes, content.size());
    EXPECT_FALSE(event.uploadId.has_value());
    EXPECT_FALSE(event.error.has_value());
  }
}

TEST_F(OperationObserverApiTest, ResolvesReadOffsetAndTotalDuringOpenObject)
{
  ASSERT_TRUE(extora::succeeded(putText(*m_store, "photos", "range", "0123456789")));
  m_observer->clear();

  extora::open_object_options options;
  options.range = extora::byte_range{extora::byte_range_type::offset_length, 2, 5};
  VectorWriter writer;
  ASSERT_TRUE(succeeded(readObject(*m_store, extora::bucket_name{"photos"}, extora::object_key{"range"}, writer,
                                   options, ignoredOpenObjectResult())));
  EXPECT_EQ(stringFromBytes(writer.bytes()), "23456");

  const std::vector<OperationEventSnapshot>& events = m_observer->events();
  ASSERT_EQ(events.size(), 3);
  EXPECT_EQ(events[0].stage, extora::operation_stage::started);
  EXPECT_FALSE(events[0].totalBytes.has_value());
  EXPECT_FALSE(events[0].readOffset.has_value());
  EXPECT_EQ(events[1].stage, extora::operation_stage::progress);
  EXPECT_EQ(events[1].bytesProcessed, 5);
  EXPECT_EQ(events[1].totalBytes, 5);
  EXPECT_EQ(events[1].readOffset, 2);
  EXPECT_EQ(events[2].stage, extora::operation_stage::completed);
  EXPECT_EQ(events[2].bytesProcessed, 5);
  EXPECT_EQ(events[2].totalBytes, 5);
  EXPECT_EQ(events[2].readOffset, 2);
  EXPECT_EQ(events[2].type, extora::operation_type::open_object);
}

TEST_F(OperationObserverApiTest, KeepsOpenObjectActiveUntilTheReaderFinishesOrIsDestroyed)
{
  ASSERT_TRUE(extora::succeeded(putText(*m_store, "photos", "opened", "body")));
  m_observer->clear();

  extora::open_object_result result;
  ASSERT_TRUE(succeeded(m_store->open_object(extora::bucket_name{"photos"}, extora::object_key{"opened"}, result)));
  ASSERT_EQ(m_observer->events().size(), 1u);
  EXPECT_EQ(m_observer->events()[0].stage, extora::operation_stage::started);

  result.reader.reset();
  const std::vector<OperationEventSnapshot>& events = m_observer->events();
  ASSERT_EQ(events.size(), 2u);
  EXPECT_EQ(events[1].stage, extora::operation_stage::failed);
  ASSERT_TRUE(events[1].error.has_value());
  EXPECT_EQ(events[1].error->code, extora::storage_error_code::operation_cancelled);
}

TEST_F(OperationObserverApiTest, ReportsDiscoveredTotalForUnknownLengthPut)
{
  ASSERT_TRUE(extora::succeeded(putText(*m_store, "photos", "unknown", "abcdefghij")));

  const std::vector<OperationEventSnapshot>& events = m_observer->events();
  ASSERT_GE(events.size(), 2);
  EXPECT_FALSE(events.front().totalBytes.has_value());
  EXPECT_EQ(events.back().stage, extora::operation_stage::completed);
  EXPECT_EQ(events.back().bytesProcessed, 10);
  EXPECT_EQ(events.back().totalBytes, 10);
}

TEST_F(OperationObserverApiTest, ReportsFailureAndTransferredByteCount)
{
  PartiallyFailingReader reader;
  extora::object_metadata metadata;
  extora::put_object_options options;
  options.expected_content_length = 10;

  const extora::storage_error error = m_store->put_object(extora::bucket_name{"photos"}, extora::object_key{"failed"},
                                                          reader, metadata, ignoredPutResult(), options);
  ASSERT_EQ(error.code, extora::storage_error_code::source_failure);

  const std::vector<OperationEventSnapshot>& events = m_observer->events();
  ASSERT_EQ(events.size(), 2);
  EXPECT_FALSE(events[0].error.has_value());
  EXPECT_EQ(events[1].stage, extora::operation_stage::failed);
  EXPECT_EQ(events[1].bytesProcessed, 3);
  EXPECT_EQ(events[1].totalBytes, 10);
  ASSERT_TRUE(events[1].error.has_value());
  EXPECT_EQ(events[1].error->code, error.code);
  EXPECT_EQ(events[1].error->message, error.message);
}

TEST_F(OperationObserverApiTest, IncludesMultipartPartContext)
{
  extora::create_multipart_upload_result upload;
  ASSERT_TRUE(succeeded(m_store->create_multipart_upload(extora::bucket_name{"photos"}, extora::object_key{"archive"},
                                                         extora::object_metadata{}, upload)));

  VectorReader reader{bytesFromString("part-1"), 3};
  extora::upload_part_options options;
  options.expected_content_length = 6;
  ASSERT_TRUE(succeeded(m_store->upload_part(extora::bucket_name{"photos"}, extora::object_key{"archive"},
                                             upload.upload_id, 7, reader, ignoredUploadPartResult(), options)));

  const std::vector<OperationEventSnapshot>& events = m_observer->events();
  ASSERT_GE(events.size(), 2);
  for (const OperationEventSnapshot& event : events) {
    EXPECT_EQ(event.type, extora::operation_type::upload_part);
    ASSERT_TRUE(event.uploadId.has_value());
    EXPECT_EQ(*event.uploadId, upload.upload_id.value);
    EXPECT_EQ(event.partNumber, 7);
    EXPECT_FALSE(event.error.has_value());
  }
  EXPECT_EQ(events.back().stage, extora::operation_stage::completed);
  EXPECT_EQ(events.back().bytesProcessed, 6);
}

TEST_F(OperationObserverApiTest, ReportsStorageCompactionLifecycle)
{
  m_observer->clear();
  extora::compact_storage_result result;
  ASSERT_TRUE(succeeded(m_store->compact_storage(result)));

  const std::vector<OperationEventSnapshot>& events = m_observer->events();
  ASSERT_EQ(events.size(), 2);
  EXPECT_EQ(events[0].stage, extora::operation_stage::started);
  EXPECT_FALSE(events[0].totalBytes.has_value());
  EXPECT_EQ(events[1].stage, extora::operation_stage::completed);
  EXPECT_EQ(events[1].totalBytes, 0);
  for (const OperationEventSnapshot& event : events) {
    EXPECT_EQ(event.operationId, events[0].operationId);
    EXPECT_EQ(event.type, extora::operation_type::compact_storage);
    EXPECT_TRUE(event.bucket.empty());
    EXPECT_TRUE(event.key.empty());
    EXPECT_FALSE(event.error.has_value());
  }
}

TEST(OperationObserverStandaloneApiTest, ObserverExceptionsDoNotChangeStorageResult)
{
  const std::shared_ptr<ThrowingOperationObserver> observer = std::make_shared<ThrowingOperationObserver>();
  extora::object_store_options options;
  options.root_directory = makeTempRoot();
  options.observer = observer;
  options.operation_progress_interval_bytes = 0;

  extora::storage_error error;
  std::unique_ptr<extora::managed_object_store> store = extora::open_object_store(options, error);
  ASSERT_TRUE(succeeded(error)) << error.message;
  ASSERT_TRUE(store);
  ASSERT_TRUE(succeeded(store->create_bucket(extora::bucket_name{"photos"})));

  EXPECT_TRUE(extora::succeeded(putText(*store, "photos", "object", "payload")));
}

TEST(OperationObserverStandaloneApiTest, StoreRetainsObserver)
{
  extora::object_store_options options;
  options.root_directory = makeTempRoot();
  std::shared_ptr<RecordingOperationObserver> observer = std::make_shared<RecordingOperationObserver>();
  const std::weak_ptr<RecordingOperationObserver> weakObserver = observer;
  options.observer = std::move(observer);

  extora::storage_error error;
  std::unique_ptr<extora::managed_object_store> store = extora::open_object_store(options, error);
  ASSERT_TRUE(succeeded(error)) << error.message;
  ASSERT_TRUE(store);
  options.observer.reset();
  ASSERT_FALSE(weakObserver.expired());

  ASSERT_TRUE(succeeded(store->create_bucket(extora::bucket_name{"photos"})));
  ASSERT_TRUE(succeeded(putText(*store, "photos", "object", "payload")));
  {
    const std::shared_ptr<RecordingOperationObserver> retainedObserver = weakObserver.lock();
    ASSERT_TRUE(retainedObserver);
    EXPECT_FALSE(retainedObserver->events().empty());
  }

  store.reset();
  EXPECT_TRUE(weakObserver.expired());
}

} // namespace

} // namespace extoraTest
