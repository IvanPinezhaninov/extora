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

#include <chrono>
#include <condition_variable>
#include <future>
#include <mutex>
#include <thread>

#include "CoreTestSupport.h"
#include "PublicTestSupport.h"
#include "SqliteTestSupport.h"

namespace extoraTest {

namespace {

class OperationGate {
public:
  void enterAndWait()
  {
    std::unique_lock<std::mutex> lock{m_mutex};
    m_entered = true;
    m_condition.notify_all();
    m_condition.wait(lock, [this]() { return m_released; });
  }

  bool waitUntilEntered()
  {
    std::unique_lock<std::mutex> lock{m_mutex};
    return m_condition.wait_for(lock, std::chrono::seconds{5}, [this]() { return m_entered; });
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
  bool m_entered = false;
  bool m_released = false;
};

class BlockingReader final : public extora::object_reader {
public:
  explicit BlockingReader(OperationGate& gate, std::byte value = std::byte{0x2a})
    : m_gate{gate}
    , m_value{value}
  {}

  extora::object_read_result read(std::byte* data, std::size_t size) override
  {
    m_gate.enterAndWait();
    if (size > 0) data[0] = m_value;

    extora::object_read_result result;
    result.bytes_read = size > 0 ? 1 : 0;
    result.end_of_stream = true;
    return result;
  }

private:
  OperationGate& m_gate;
  std::byte m_value;
};

class BlockingWriter final : public extora::object_writer {
public:
  explicit BlockingWriter(OperationGate& gate)
    : m_gate{gate}
  {}

  extora::storage_error write(const std::byte*, std::size_t) override
  {
    m_gate.enterAndWait();
    return {};
  }

private:
  OperationGate& m_gate;
};

class BlockingBeginWriteDataStore final : public extora::core::object_data_store {
public:
  BlockingBeginWriteDataStore(extora::core::object_data_store& delegate, OperationGate& gate)
    : m_delegate{delegate}
    , m_gate{gate}
  {}

  std::uint64_t max_extent_size() const override
  {
    return m_delegate.max_extent_size();
  }

  extora::storage_error get_segment_storage_usage(extora::core::segment_storage_usage& usage) override
  {
    return m_delegate.get_segment_storage_usage(usage);
  }

  extora::storage_error begin_write(const extora::core::physical_extent& extent,
                                    extora::core::data_write_handle& handle) override
  {
    m_gate.enterAndWait();
    return m_delegate.begin_write(extent, handle);
  }

  extora::storage_error write(extora::core::data_write_handle handle, std::uint64_t offset, const std::byte* data,
                              std::size_t size) override
  {
    return m_delegate.write(handle, offset, data, size);
  }

  void finish_write(extora::core::data_write_handle handle) noexcept override
  {
    m_delegate.finish_write(handle);
  }

  extora::storage_error flush(std::uint64_t segmentId) override
  {
    return m_delegate.flush(segmentId);
  }

  extora::storage_error validate_extent(const extora::core::physical_extent& extent) override
  {
    return m_delegate.validate_extent(extent);
  }

  extora::storage_error remove_segment(std::uint64_t segmentId, extora::core::segment_removal_result& result) override
  {
    return m_delegate.remove_segment(segmentId, result);
  }

  extora::storage_error begin_read(const extora::core::physical_extent& extent,
                                   extora::core::data_read_handle& handle) override
  {
    return m_delegate.begin_read(extent, handle);
  }

  extora::storage_error read(extora::core::data_read_handle handle, std::uint64_t offset, std::byte* data,
                             std::size_t size, std::size_t& bytesRead) override
  {
    return m_delegate.read(handle, offset, data, size, bytesRead);
  }

  void finish_read(extora::core::data_read_handle handle) noexcept override
  {
    m_delegate.finish_read(handle);
  }

private:
  extora::core::object_data_store& m_delegate;
  OperationGate& m_gate;
};

class PausingWriter final : public extora::object_writer {
public:
  explicit PausingWriter(OperationGate& gate)
    : m_gate{gate}
  {}

  extora::storage_error write(const std::byte* data, std::size_t size) override
  {
    if (!m_paused) {
      m_paused = true;
      m_gate.enterAndWait();
    }

    m_bytes.insert(m_bytes.end(), data, data + size);
    return {};
  }

  const std::vector<std::byte>& bytes() const
  {
    return m_bytes;
  }

private:
  OperationGate& m_gate;
  std::vector<std::byte> m_bytes;
  bool m_paused = false;
};

class FailingReader final : public extora::object_reader {
public:
  extora::object_read_result read(std::byte*, std::size_t) override
  {
    extora::object_read_result result;
    result.error.code = extora::storage_error_code::source_failure;
    result.error.message = "intentional reader failure";
    return result;
  }
};

class FailingWriter final : public extora::object_writer {
public:
  extora::storage_error write(const std::byte*, std::size_t) override
  {
    return extora::storage_error{extora::storage_error_code::sink_failure, "intentional writer failure"};
  }
};

} // namespace

TEST_F(StoreCoreTest, ReadsObjectFromMultipleThreads)
{
  const std::string source = "concurrent object data";
  VectorReader reader{bytesFromString(source), 4};
  extora::object_metadata metadata;
  extora::put_object_options options;
  options.expected_content_length = source.size();

  ASSERT_TRUE(succeeded(m_core->put_object(extora::bucket_name{"photos"}, extora::object_key{"shared.txt"}, reader,
                                           metadata, ignoredPutResult(), options)));

  std::vector<std::thread> threads;
  std::vector<std::string> results(10);
  std::vector<extora::storage_error> errors(10);

  for (std::size_t i = 0; i < results.size(); ++i) {
    threads.emplace_back([this, &errors, &results, i]() {
      VectorWriter writer;
      errors[i] = readObject(*m_core, extora::bucket_name{"photos"}, extora::object_key{"shared.txt"}, writer,
                             extora::open_object_options{}, ignoredOpenObjectResult());
      results[i] = stringFromBytes(writer.bytes());
    });
  }

  for (std::thread& thread : threads)
    thread.join();

  for (std::size_t i = 0; i < results.size(); ++i) {
    EXPECT_TRUE(succeeded(errors[i]));
    EXPECT_EQ(results[i], source);
  }
}

TEST_F(StoreCoreTest, WritesObjectsFromMultipleThreads)
{
  std::vector<std::thread> threads;
  std::vector<extora::storage_error> errors(10);

  for (std::size_t i = 0; i < errors.size(); ++i) {
    threads.emplace_back([this, &errors, i]() {
      const std::string text = "object-" + std::to_string(i);
      VectorReader reader{bytesFromString(text), 3};
      extora::object_metadata metadata;
      extora::put_object_options options;
      options.expected_content_length = text.size();
      errors[i] = m_core->put_object(extora::bucket_name{"photos"}, extora::object_key{"object-" + std::to_string(i)},
                                     reader, metadata, ignoredPutResult(), options);
    });
  }

  for (std::thread& thread : threads)
    thread.join();

  for (std::size_t i = 0; i < errors.size(); ++i) {
    ASSERT_TRUE(succeeded(errors[i]));

    VectorWriter writer;
    ASSERT_TRUE(
        succeeded(readObject(*m_core, extora::bucket_name{"photos"}, extora::object_key{"object-" + std::to_string(i)},
                             writer, extora::open_object_options{}, ignoredOpenObjectResult())));
    EXPECT_EQ(stringFromBytes(writer.bytes()), "object-" + std::to_string(i));
  }
}

TEST(StoreCoreConcurrencyTest, DataPreparationDoesNotHoldIndexLock)
{
  const std::string root = makeTempRoot();
  extora::core::sqlite_object_index index{joinPath(root, "index.sqlite3"), 1024};
  ASSERT_TRUE(succeeded(index.open()));

  extora::core::segment_data_store dataStore{root, 1024};
  ASSERT_TRUE(succeeded(dataStore.open()));
  OperationGate gate;
  BlockingBeginWriteDataStore blockingDataStore{dataStore, gate};
  extora::core::object_store_core core{index, blockingDataStore, defaultCoreHasherFactory()};
  ASSERT_TRUE(succeeded(core.create_bucket(extora::bucket_name{"photos"})));

  VectorReader reader{bytesFromString("data"), 4};
  extora::storage_error putError;
  std::thread putThread{[&]() {
    putError = core.put_object(extora::bucket_name{"photos"}, extora::object_key{"object"}, reader,
                               extora::object_metadata{}, ignoredPutResult());
  }};

  EXPECT_TRUE(gate.waitUntilEntered());
  extora::bucket_info info;
  std::future<extora::storage_error> headFuture =
      std::async(std::launch::async, [&]() { return core.head_bucket(extora::bucket_name{"photos"}, info); });
  const std::future_status headStatus = headFuture.wait_for(std::chrono::seconds{5});
  EXPECT_EQ(headStatus, std::future_status::ready);

  gate.release();
  putThread.join();
  EXPECT_TRUE(succeeded(putError));
  if (headStatus == std::future_status::ready) {
    EXPECT_TRUE(succeeded(headFuture.get()));
  }
}

TEST(StoreCoreConcurrencyLimitTest, RejectsReadAboveConfiguredLimit)
{
  const std::string root = makeTempRoot();
  extora::core::sqlite_object_index index{joinPath(root, "index.sqlite3"), 1024 * 1024};
  ASSERT_TRUE(succeeded(index.open()));

  extora::core::segment_data_store dataStore{root, 1024 * 1024};
  ASSERT_TRUE(succeeded(dataStore.open()));

  extora::core::object_store_core_options options;
  options.max_concurrent_reads = 1;
  extora::core::object_store_core core{index, dataStore, defaultCoreHasherFactory(), options};
  ASSERT_TRUE(succeeded(core.create_bucket(extora::bucket_name{"photos"})));

  VectorReader reader{bytesFromString("data"), 4};
  ASSERT_TRUE(succeeded(core.put_object(extora::bucket_name{"photos"}, extora::object_key{"limited"}, reader,
                                        extora::object_metadata{}, ignoredPutResult())));

  extora::open_object_result firstResult;
  ASSERT_TRUE(succeeded(core.open_object(extora::bucket_name{"photos"}, extora::object_key{"limited"}, firstResult)));

  extora::open_object_result secondResult;
  const extora::storage_error secondError =
      core.open_object(extora::bucket_name{"photos"}, extora::object_key{"limited"}, secondResult);
  EXPECT_EQ(secondError.code, extora::storage_error_code::concurrency_limit_exceeded);

  firstResult.reader.reset();
  EXPECT_TRUE(succeeded(core.open_object(extora::bucket_name{"photos"}, extora::object_key{"limited"}, secondResult)));
}

TEST(StoreCoreConcurrencyLimitTest, RejectsWriteAboveConfiguredLimit)
{
  const std::string root = makeTempRoot();
  extora::core::sqlite_object_index index{joinPath(root, "index.sqlite3"), 1024 * 1024};
  ASSERT_TRUE(succeeded(index.open()));

  extora::core::segment_data_store dataStore{root, 1024 * 1024};
  ASSERT_TRUE(succeeded(dataStore.open()));

  extora::core::object_store_core_options options;
  options.max_concurrent_writes = 1;
  extora::core::object_store_core core{index, dataStore, defaultCoreHasherFactory(), options};
  ASSERT_TRUE(succeeded(core.create_bucket(extora::bucket_name{"photos"})));

  OperationGate gate;
  BlockingReader blockingReader{gate};
  extora::storage_error firstError;
  std::thread firstWrite{[&]() {
    firstError = core.put_object(extora::bucket_name{"photos"}, extora::object_key{"first"}, blockingReader,
                                 extora::object_metadata{}, ignoredPutResult());
  }};

  EXPECT_TRUE(gate.waitUntilEntered());
  VectorReader secondReader{bytesFromString("data"), 4};
  const extora::storage_error secondError =
      core.put_object(extora::bucket_name{"photos"}, extora::object_key{"second"}, secondReader,
                      extora::object_metadata{}, ignoredPutResult());
  EXPECT_EQ(secondError.code, extora::storage_error_code::concurrency_limit_exceeded);

  gate.release();
  firstWrite.join();
  EXPECT_TRUE(succeeded(firstError));
}

TEST_F(StoreCoreTest, LastPublishedConcurrentPutRemainsVisible)
{
  OperationGate firstGate;
  OperationGate secondGate;
  BlockingReader firstReader{firstGate, std::byte{0x11}};
  BlockingReader secondReader{secondGate, std::byte{0x22}};
  extora::storage_error firstError;
  extora::storage_error secondError;

  std::thread firstPut{[&]() {
    firstError = m_core->put_object(extora::bucket_name{"photos"}, extora::object_key{"shared"}, firstReader,
                                    extora::object_metadata{}, ignoredPutResult());
  }};
  EXPECT_TRUE(firstGate.waitUntilEntered());

  std::thread secondPut{[&]() {
    secondError = m_core->put_object(extora::bucket_name{"photos"}, extora::object_key{"shared"}, secondReader,
                                     extora::object_metadata{}, ignoredPutResult());
  }};
  EXPECT_TRUE(secondGate.waitUntilEntered());

  firstGate.release();
  firstPut.join();
  ASSERT_TRUE(succeeded(firstError));

  secondGate.release();
  secondPut.join();
  ASSERT_TRUE(succeeded(secondError));

  VectorWriter writer;
  ASSERT_TRUE(succeeded(readObject(*m_core, extora::bucket_name{"photos"}, extora::object_key{"shared"}, writer,
                                   extora::open_object_options{}, ignoredOpenObjectResult())));
  ASSERT_EQ(writer.bytes().size(), 1);
  EXPECT_EQ(writer.bytes()[0], std::byte{0x22});
}

TEST_F(StoreCoreTest, ActiveReadCompletesAfterDelete)
{
  const std::vector<std::byte> source(128 * 1024, std::byte{0x5a});
  VectorReader reader{source, 16 * 1024};
  ASSERT_TRUE(succeeded(m_core->put_object(extora::bucket_name{"photos"}, extora::object_key{"delete-during-get"},
                                           reader, extora::object_metadata{}, ignoredPutResult())));

  OperationGate gate;
  PausingWriter writer{gate};
  extora::storage_error getError;
  std::thread getThread{[&]() {
    getError = readObject(*m_core, extora::bucket_name{"photos"}, extora::object_key{"delete-during-get"}, writer,
                          extora::open_object_options{}, ignoredOpenObjectResult());
  }};

  EXPECT_TRUE(gate.waitUntilEntered());
  const extora::storage_error deleteError = m_core->delete_object(
      extora::bucket_name{"photos"}, extora::object_key{"delete-during-get"}, ignoredDeleteResult());
  EXPECT_TRUE(succeeded(deleteError));
  gate.release();
  getThread.join();

  ASSERT_TRUE(succeeded(getError));
  EXPECT_EQ(writer.bytes(), source);
}

TEST_F(StoreCoreTest, ReclamationSkipsExtentUsedByActiveRead)
{
  const std::vector<std::byte> source(128 * 1024, std::byte{0x5a});
  VectorReader reader{source, 16 * 1024};
  ASSERT_TRUE(succeeded(m_core->put_object(extora::bucket_name{"photos"}, extora::object_key{"reclaim-during-read"},
                                           reader, extora::object_metadata{}, ignoredPutResult())));

  OperationGate gate;
  PausingWriter writer{gate};
  extora::storage_error getError;
  std::thread getThread{[&]() {
    getError = readObject(*m_core, extora::bucket_name{"photos"}, extora::object_key{"reclaim-during-read"}, writer,
                          extora::open_object_options{}, ignoredOpenObjectResult());
  }};

  ASSERT_TRUE(gate.waitUntilEntered());
  ASSERT_TRUE(succeeded(m_core->delete_object(extora::bucket_name{"photos"}, extora::object_key{"reclaim-during-read"},
                                              ignoredDeleteResult())));

  extora::reclaim_storage_result protectedResult;
  std::future<extora::storage_error> cleanupFuture =
      std::async(std::launch::async, [this, &protectedResult]() { return m_core->reclaim_storage(protectedResult); });
  ASSERT_EQ(cleanupFuture.wait_for(std::chrono::seconds{5}), std::future_status::ready);
  EXPECT_TRUE(succeeded(cleanupFuture.get()));
  EXPECT_EQ(protectedResult.reclaimed_bytes, 0u);
  EXPECT_EQ(protectedResult.reclaimed_extent_count, 0u);
  EXPECT_EQ(protectedResult.remaining_reclaimable_bytes, source.size());
  EXPECT_EQ(protectedResult.remaining_reclaimable_extent_count, 1u);
  EXPECT_EQ(countPhysicalExtentsInState(joinPath(m_root, "index.sqlite3"), sqliteExtentStateGarbage), 1);
  EXPECT_EQ(countPhysicalExtentsInState(joinPath(m_root, "index.sqlite3"), sqliteExtentStateFree), 1);

  gate.release();
  getThread.join();
  EXPECT_TRUE(succeeded(getError));
  EXPECT_EQ(writer.bytes(), source);

  extora::reclaim_storage_result releasedResult;
  EXPECT_TRUE(succeeded(m_core->reclaim_storage(releasedResult)));
  EXPECT_EQ(releasedResult.reclaimed_bytes, source.size());
  EXPECT_EQ(releasedResult.reclaimed_extent_count, 1u);
  EXPECT_EQ(releasedResult.remaining_reclaimable_bytes, 0u);
  EXPECT_EQ(releasedResult.remaining_reclaimable_extent_count, 0u);
  EXPECT_EQ(countPhysicalExtentsInState(joinPath(m_root, "index.sqlite3"), sqliteExtentStateGarbage), 0);
  EXPECT_EQ(countPhysicalExtentsInState(joinPath(m_root, "index.sqlite3"), sqliteExtentStateFree), 1);
}

TEST_F(StoreCoreTest, ListDoesNotExposeUnpublishedPut)
{
  OperationGate gate;
  BlockingReader reader{gate};
  extora::storage_error putError;
  std::thread putThread{[&]() {
    putError = m_core->put_object(extora::bucket_name{"photos"}, extora::object_key{"pending"}, reader,
                                  extora::object_metadata{}, ignoredPutResult());
  }};

  EXPECT_TRUE(gate.waitUntilEntered());
  extora::object_list beforePublish;
  const extora::storage_error listError = m_core->list_objects(extora::bucket_name{"photos"}, beforePublish);
  EXPECT_TRUE(succeeded(listError));
  EXPECT_TRUE(beforePublish.objects.empty());

  gate.release();
  putThread.join();
  ASSERT_TRUE(succeeded(putError));

  extora::object_list afterPublish;
  ASSERT_TRUE(succeeded(m_core->list_objects(extora::bucket_name{"photos"}, afterPublish)));
  ASSERT_EQ(afterPublish.objects.size(), 1);
  EXPECT_EQ(afterPublish.objects[0].key.value, "pending");
}

TEST(StoreCoreConcurrencyLimitTest, ReadAndWriteLimitsAreIndependent)
{
  const std::string root = makeTempRoot();
  extora::core::sqlite_object_index index{joinPath(root, "index.sqlite3"), 1024 * 1024};
  ASSERT_TRUE(succeeded(index.open()));

  extora::core::segment_data_store dataStore{root, 1024 * 1024};
  ASSERT_TRUE(succeeded(dataStore.open()));

  extora::core::object_store_core_options options;
  options.max_concurrent_reads = 1;
  options.max_concurrent_writes = 1;
  extora::core::object_store_core core{index, dataStore, defaultCoreHasherFactory(), options};
  ASSERT_TRUE(succeeded(core.create_bucket(extora::bucket_name{"photos"})));

  VectorReader initialReader{bytesFromString("initial"), 7};
  ASSERT_TRUE(succeeded(core.put_object(extora::bucket_name{"photos"}, extora::object_key{"initial"}, initialReader,
                                        extora::object_metadata{}, ignoredPutResult())));

  OperationGate gate;
  BlockingWriter blockingWriter{gate};
  extora::storage_error getError;
  std::thread getThread{[&]() {
    getError = readObject(core, extora::bucket_name{"photos"}, extora::object_key{"initial"}, blockingWriter,
                          extora::open_object_options{}, ignoredOpenObjectResult());
  }};

  EXPECT_TRUE(gate.waitUntilEntered());
  VectorReader putReader{bytesFromString("new"), 3};
  EXPECT_TRUE(succeeded(core.put_object(extora::bucket_name{"photos"}, extora::object_key{"new"}, putReader,
                                        extora::object_metadata{}, ignoredPutResult())));

  gate.release();
  getThread.join();
  EXPECT_TRUE(succeeded(getError));
}

TEST(StoreCoreConcurrencyLimitTest, ReleasesPermitsAfterStreamFailures)
{
  const std::string root = makeTempRoot();
  extora::core::sqlite_object_index index{joinPath(root, "index.sqlite3"), 1024 * 1024};
  ASSERT_TRUE(succeeded(index.open()));

  extora::core::segment_data_store dataStore{root, 1024 * 1024};
  ASSERT_TRUE(succeeded(dataStore.open()));

  extora::core::object_store_core_options options;
  options.max_concurrent_reads = 1;
  options.max_concurrent_writes = 1;
  extora::core::object_store_core core{index, dataStore, defaultCoreHasherFactory(), options};
  ASSERT_TRUE(succeeded(core.create_bucket(extora::bucket_name{"photos"})));

  FailingReader failingReader;
  const extora::storage_error failedPut = core.put_object(extora::bucket_name{"photos"}, extora::object_key{"failed"},
                                                          failingReader, extora::object_metadata{}, ignoredPutResult());
  ASSERT_EQ(failedPut.code, extora::storage_error_code::source_failure);

  VectorReader reader{bytesFromString("data"), 4};
  ASSERT_TRUE(succeeded(core.put_object(extora::bucket_name{"photos"}, extora::object_key{"valid"}, reader,
                                        extora::object_metadata{}, ignoredPutResult())));

  FailingWriter failingWriter;
  const extora::storage_error failedGet =
      readObject(core, extora::bucket_name{"photos"}, extora::object_key{"valid"}, failingWriter,
                 extora::open_object_options{}, ignoredOpenObjectResult());
  ASSERT_EQ(failedGet.code, extora::storage_error_code::sink_failure);

  VectorWriter writer;
  EXPECT_TRUE(succeeded(readObject(core, extora::bucket_name{"photos"}, extora::object_key{"valid"}, writer,
                                   extora::open_object_options{}, ignoredOpenObjectResult())));
}

} // namespace extoraTest
