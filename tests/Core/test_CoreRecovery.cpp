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

#include <cstdint>

#include <gtest/gtest.h>

#include "CoreTestSupport.h"
#include "PublicTestSupport.h"
#include "SqliteTestSupport.h"
#include "extora/core/object_store_core.h"
#include "extora/core/segment_data_store.h"
#include "extora/core/sqlite_object_index.h"

namespace extoraTest {

namespace {

enum class DataStoreFailure : std::uint8_t { write, flush, remove };

class FailingDataStore final : public extora::core::object_data_store {
public:
  FailingDataStore(extora::core::object_data_store& delegate, DataStoreFailure failure)
    : m_delegate{delegate}
    , m_failure{failure}
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
    return m_delegate.begin_write(extent, handle);
  }

  extora::storage_error write(extora::core::data_write_handle handle, std::uint64_t offset, const std::byte* data,
                              std::size_t size) override
  {
    if (m_failure == DataStoreFailure::write)
      return extora::make_error(extora::storage_error_code::insufficient_space, "injected data write failure");
    return m_delegate.write(handle, offset, data, size);
  }

  void finish_write(extora::core::data_write_handle handle) noexcept override
  {
    m_delegate.finish_write(handle);
  }

  extora::storage_error flush(std::uint64_t segmentId) override
  {
    if (m_failure == DataStoreFailure::flush)
      return extora::make_error(extora::storage_error_code::backend_failure, "injected data flush failure");
    return m_delegate.flush(segmentId);
  }

  extora::storage_error validate_extent(const extora::core::physical_extent& extent) override
  {
    return m_delegate.validate_extent(extent);
  }

  extora::storage_error remove_segment(std::uint64_t segmentId, extora::core::segment_removal_result& result) override
  {
    if (m_failure == DataStoreFailure::remove)
      return extora::make_error(extora::storage_error_code::backend_failure, "injected segment removal failure");
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
  DataStoreFailure m_failure;
};

void expectFailedDataOperationIsAbandoned(DataStoreFailure failure, extora::storage_error_code expectedCode)
{
  const std::string root = makeTempRoot();
  const std::string databasePath = joinPath(root, "index.sqlite3");

  extora::core::sqlite_object_index index{databasePath, 16};
  ASSERT_TRUE(succeeded(index.open()));
  extora::core::segment_data_store delegate{root, 16};
  ASSERT_TRUE(succeeded(delegate.open()));
  FailingDataStore dataStore{delegate, failure};
  extora::core::object_store_core core{index, dataStore, defaultCoreHasherFactory()};
  ASSERT_TRUE(succeeded(core.create_bucket(extora::bucket_name{"photos"})));

  VectorReader reader{bytesFromString("payload"), 7};
  const extora::storage_error error = core.put_object(extora::bucket_name{"photos"}, extora::object_key{"failed"},
                                                      reader, extora::object_metadata{}, ignoredPutResult());

  EXPECT_EQ(error.code, expectedCode);
  EXPECT_EQ(countPhysicalExtentsInState(databasePath, sqliteExtentStateReserved), 0);
  EXPECT_EQ(countPhysicalExtentsInState(databasePath, sqliteExtentStateAbandoned), 1);
  EXPECT_EQ(countPhysicalExtentsInState(databasePath, sqliteExtentStateCommitted), 0);

  extora::object_info info;
  EXPECT_EQ(core.head_object(extora::bucket_name{"photos"}, extora::object_key{"failed"}, info).code,
            extora::storage_error_code::object_not_found);
}

} // namespace

TEST(StoreCoreFailureTest, AbandonsReservationAfterDataWriteFailure)
{
  expectFailedDataOperationIsAbandoned(DataStoreFailure::write, extora::storage_error_code::insufficient_space);
}

TEST(StoreCoreFailureTest, AbandonsReservationAfterDataFlushFailure)
{
  expectFailedDataOperationIsAbandoned(DataStoreFailure::flush, extora::storage_error_code::backend_failure);
}

TEST(StoreCoreFailureTest, MakesClaimedExtentsReusableAfterSegmentRemovalFailure)
{
  const std::string root = makeTempRoot();
  const std::string databasePath = joinPath(root, "index.sqlite3");

  extora::core::sqlite_object_index index{databasePath, 4};
  ASSERT_TRUE(succeeded(index.open()));
  extora::core::segment_data_store delegate{root, 4, extora::storage_durability::relaxed};
  ASSERT_TRUE(succeeded(delegate.open()));
  FailingDataStore dataStore{delegate, DataStoreFailure::remove};
  extora::core::object_store_core core{index, dataStore, defaultCoreHasherFactory()};
  ASSERT_TRUE(succeeded(core.create_bucket(extora::bucket_name{"photos"})));

  extora::put_object_options putOptions;
  putOptions.expected_content_length = 4;
  VectorReader deletedReader{bytesFromString("gone"), 4};
  ASSERT_TRUE(succeeded(core.put_object(extora::bucket_name{"photos"}, extora::object_key{"deleted"}, deletedReader,
                                        extora::object_metadata{}, ignoredPutResult(), putOptions)));
  VectorReader retainedReader{bytesFromString("live"), 4};
  ASSERT_TRUE(succeeded(core.put_object(extora::bucket_name{"photos"}, extora::object_key{"retained"}, retainedReader,
                                        extora::object_metadata{}, ignoredPutResult(), putOptions)));
  ASSERT_TRUE(succeeded(
      core.delete_object(extora::bucket_name{"photos"}, extora::object_key{"deleted"}, ignoredDeleteResult())));

  extora::compact_storage_result result;
  EXPECT_EQ(core.compact_storage(result).code, extora::storage_error_code::backend_failure);
  EXPECT_EQ(countPhysicalExtentsInState(databasePath, sqliteExtentStateReleasing), 0);
  EXPECT_EQ(countPhysicalExtentsInState(databasePath, sqliteExtentStateFree), 1);
  EXPECT_EQ(countPhysicalExtentsInState(databasePath, sqliteExtentStateCommitted), 1);

  VectorWriter writer;
  ASSERT_TRUE(succeeded(readObject(core, extora::bucket_name{"photos"}, extora::object_key{"retained"}, writer, {},
                                   ignoredOpenObjectResult())));
  EXPECT_EQ(stringFromBytes(writer.bytes()), "live");
}

TEST(StoreCoreFailureTest, AbandonsReservedExtentsWhenMultiExtentWriteFails)
{
  const std::string root = makeTempRoot();
  const std::string databasePath = joinPath(root, "index.sqlite3");

  extora::core::sqlite_object_index index{databasePath, 4};
  ASSERT_TRUE(succeeded(index.open()));

  extora::core::segment_data_store dataStore{root, 4};
  ASSERT_TRUE(succeeded(dataStore.open()));

  extora::core::object_store_core core{index, dataStore, defaultCoreHasherFactory()};
  ASSERT_TRUE(succeeded(core.create_bucket(extora::bucket_name{"photos"})));

  VectorReader reader{bytesFromString("abcdefghij"), 10};
  extora::object_metadata metadata;
  extora::put_object_options options;
  options.expected_content_length = 100;

  const extora::storage_error putError = core.put_object(extora::bucket_name{"photos"}, extora::object_key{"bad"},
                                                         reader, metadata, ignoredPutResult(), options);
  EXPECT_EQ(putError.code, extora::storage_error_code::source_failure);

  EXPECT_EQ(countPhysicalExtentsInState(databasePath, sqliteExtentStateAbandoned), 3);
  EXPECT_EQ(countPhysicalExtentsInState(databasePath, sqliteExtentStateReserved), 0);
  EXPECT_EQ(countPhysicalExtentsInState(databasePath, sqliteExtentStateCommitted), 0);

  extora::reclamation_estimate estimate;
  ASSERT_TRUE(succeeded(core.get_reclamation_estimate(estimate)));
  EXPECT_EQ(estimate.reclaimable_bytes, 12);
  EXPECT_EQ(estimate.reclaimable_extent_count, 3);

  VectorWriter writer;
  const extora::storage_error getError = readObject(core, extora::bucket_name{"photos"}, extora::object_key{"bad"},
                                                    writer, extora::open_object_options{}, ignoredOpenObjectResult());
  EXPECT_EQ(getError.code, extora::storage_error_code::object_not_found);
}

TEST_F(StoreCoreTest, AbandonsWrittenExtentWhenPublicationConditionFails)
{
  VectorReader initialReader{bytesFromString("initial"), 7};
  ASSERT_TRUE(succeeded(m_core->put_object(extora::bucket_name{"photos"}, extora::object_key{"conditional"},
                                           initialReader, extora::object_metadata{}, ignoredPutResult())));

  extora::put_object_options options;
  options.dedup = extora::dedup_mode::disabled;
  options.conditions.if_none_match_etag = extora::etag_wildcard;
  VectorReader replacementReader{bytesFromString("replacement"), 11};
  const extora::storage_error error =
      m_core->put_object(extora::bucket_name{"photos"}, extora::object_key{"conditional"}, replacementReader,
                         extora::object_metadata{}, ignoredPutResult(), options);

  EXPECT_EQ(error.code, extora::storage_error_code::precondition_failed);
  EXPECT_EQ(countPhysicalExtentsInState(joinPath(m_root, "index.sqlite3"), sqliteExtentStateReserved), 0);
  EXPECT_EQ(countPhysicalExtentsInState(joinPath(m_root, "index.sqlite3"), sqliteExtentStateAbandoned), 1);
  EXPECT_EQ(countPhysicalExtentsInState(joinPath(m_root, "index.sqlite3"), sqliteExtentStateCommitted), 1);

  VectorWriter writer;
  ASSERT_TRUE(succeeded(readObject(*m_core, extora::bucket_name{"photos"}, extora::object_key{"conditional"}, writer,
                                   extora::open_object_options{}, ignoredOpenObjectResult())));
  EXPECT_EQ(stringFromBytes(writer.bytes()), "initial");
}

TEST(StoreCoreAllocationTest, ReleasesAndReusesUnusedUnknownLengthTail)
{
  const std::string root = makeTempRoot();
  const std::string databasePath = joinPath(root, "index.sqlite3");

  extora::core::sqlite_object_index index{databasePath, 16};
  ASSERT_TRUE(succeeded(index.open()));
  extora::core::segment_data_store dataStore{root, 16};
  ASSERT_TRUE(succeeded(dataStore.open()));
  extora::core::object_store_core core{index, dataStore, defaultCoreHasherFactory()};
  ASSERT_TRUE(succeeded(core.create_bucket(extora::bucket_name{"photos"})));

  VectorReader unknownReader{bytesFromString("unknown"), 7};
  ASSERT_TRUE(succeeded(core.put_object(extora::bucket_name{"photos"}, extora::object_key{"unknown"}, unknownReader,
                                        extora::object_metadata{}, ignoredPutResult())));
  EXPECT_EQ(storageExtentLength(databasePath, 1, 0), 7);
  EXPECT_EQ(storageExtentLength(databasePath, 1, 7), 9);
  EXPECT_EQ(countPhysicalExtentsInState(databasePath, sqliteExtentStateFree), 1);

  VectorReader knownReader{bytesFromString("123456789"), 9};
  extora::object_metadata metadata;
  extora::put_object_options options;
  options.expected_content_length = 9;
  ASSERT_TRUE(succeeded(core.put_object(extora::bucket_name{"photos"}, extora::object_key{"known"}, knownReader,
                                        metadata, ignoredPutResult(), options)));
  EXPECT_EQ(storageExtentLength(databasePath, 1, 7), 9);
  EXPECT_EQ(countPhysicalExtentsInState(databasePath, sqliteExtentStateFree), 0);
  EXPECT_EQ(countPhysicalExtentsInState(databasePath, sqliteExtentStateCommitted), 2);
}

TEST(StoreCoreRecoveryTest, ReclaimsAndReusesExtentsWithoutReopening)
{
  const std::string root = makeTempRoot();
  const std::string databasePath = joinPath(root, "index.sqlite3");

  extora::core::sqlite_object_index index{databasePath, 16};
  ASSERT_TRUE(succeeded(index.open()));
  extora::core::segment_data_store dataStore{root, 16};
  ASSERT_TRUE(succeeded(dataStore.open()));
  extora::core::object_store_core core{index, dataStore, defaultCoreHasherFactory()};
  ASSERT_TRUE(succeeded(core.create_bucket(extora::bucket_name{"photos"})));

  VectorReader firstReader{bytesFromString("deleted"), 7};
  ASSERT_TRUE(succeeded(core.put_object(extora::bucket_name{"photos"}, extora::object_key{"first"}, firstReader,
                                        extora::object_metadata{}, ignoredPutResult())));
  ASSERT_TRUE(
      succeeded(core.delete_object(extora::bucket_name{"photos"}, extora::object_key{"first"}, ignoredDeleteResult())));
  ASSERT_EQ(countPhysicalExtentsInState(databasePath, sqliteExtentStateGarbage), 1);

  extora::reclamation_estimate beforeReclamation;
  ASSERT_TRUE(succeeded(core.get_reclamation_estimate(beforeReclamation)));
  EXPECT_EQ(beforeReclamation.reclaimable_bytes, 7);
  EXPECT_EQ(beforeReclamation.reclaimable_extent_count, 1);

  extora::reclaim_storage_result reclaimed;
  ASSERT_TRUE(succeeded(core.reclaim_storage(reclaimed)));
  EXPECT_EQ(reclaimed.reclaimed_bytes, 7u);
  EXPECT_EQ(reclaimed.reclaimed_extent_count, 1u);
  EXPECT_EQ(reclaimed.remaining_reclaimable_bytes, 0u);
  EXPECT_EQ(reclaimed.remaining_reclaimable_extent_count, 0u);
  EXPECT_EQ(countPhysicalExtentsInState(databasePath, sqliteExtentStateGarbage), 0);
  EXPECT_EQ(countPhysicalExtentsInState(databasePath, sqliteExtentStateFree), 1);

  extora::reclamation_estimate afterReclamation;
  ASSERT_TRUE(succeeded(core.get_reclamation_estimate(afterReclamation)));
  EXPECT_EQ(afterReclamation.reclaimable_bytes, 0);
  EXPECT_EQ(afterReclamation.reclaimable_extent_count, 0);

  VectorReader secondReader{bytesFromString("reused!"), 7};
  ASSERT_TRUE(succeeded(core.put_object(extora::bucket_name{"photos"}, extora::object_key{"second"}, secondReader,
                                        extora::object_metadata{}, ignoredPutResult())));
  EXPECT_EQ(countPhysicalExtentsInState(databasePath, sqliteExtentStateFree), 1);
  EXPECT_EQ(storageExtentLength(databasePath, 1, 0), 7);
  EXPECT_EQ(storageExtentLength(databasePath, 1, 7), 9);

  VectorWriter writer;
  ASSERT_TRUE(succeeded(readObject(core, extora::bucket_name{"photos"}, extora::object_key{"second"}, writer,
                                   extora::open_object_options{}, ignoredOpenObjectResult())));
  EXPECT_EQ(stringFromBytes(writer.bytes()), "reused!");
}

TEST(StoreCoreRecoveryTest, WritesAndReadsObjectThroughReusedFreeExtent)
{
  const std::string root = makeTempRoot();
  const std::string databasePath = joinPath(root, "index.sqlite3");

  {
    extora::core::sqlite_object_index index{databasePath, 16};
    ASSERT_TRUE(succeeded(index.open()));
    extora::core::segment_data_store dataStore{root, 16};
    ASSERT_TRUE(succeeded(dataStore.open()));
    extora::core::object_store_core core{index, dataStore, defaultCoreHasherFactory()};
    ASSERT_TRUE(succeeded(core.create_bucket(extora::bucket_name{"photos"})));

    VectorReader reader{bytesFromString("old-data"), 8};
    ASSERT_TRUE(succeeded(core.put_object(extora::bucket_name{"photos"}, extora::object_key{"old"}, reader,
                                          extora::object_metadata{}, ignoredPutResult())));
    ASSERT_TRUE(
        succeeded(core.delete_object(extora::bucket_name{"photos"}, extora::object_key{"old"}, ignoredDeleteResult())));
  }

  extora::core::sqlite_object_index recoveredIndex{databasePath, 16};
  ASSERT_TRUE(succeeded(recoveredIndex.open()));
  ASSERT_EQ(countPhysicalExtentsInState(databasePath, sqliteExtentStateFree), 1);

  extora::core::segment_data_store recoveredDataStore{root, 16};
  ASSERT_TRUE(succeeded(recoveredDataStore.open()));
  extora::core::object_store_core recoveredCore{recoveredIndex, recoveredDataStore, defaultCoreHasherFactory()};

  VectorReader reader{bytesFromString("new-data"), 8};
  ASSERT_TRUE(succeeded(recoveredCore.put_object(extora::bucket_name{"photos"}, extora::object_key{"new"}, reader,
                                                 extora::object_metadata{}, ignoredPutResult())));

  VectorWriter writer;
  ASSERT_TRUE(succeeded(readObject(recoveredCore, extora::bucket_name{"photos"}, extora::object_key{"new"}, writer,
                                   extora::open_object_options{}, ignoredOpenObjectResult())));
  EXPECT_EQ(stringFromBytes(writer.bytes()), "new-data");
  EXPECT_EQ(countPhysicalExtentsInState(databasePath, sqliteExtentStateFree), 1);
  EXPECT_EQ(countPhysicalExtentsInState(databasePath, sqliteExtentStateCommitted), 1);
  EXPECT_EQ(storageExtentLength(databasePath, 1, 0), 8);
  EXPECT_EQ(storageExtentLength(databasePath, 1, 8), 8);
}

TEST(StoreCoreRecoveryTest, WritesObjectAcrossMultipleSmallerFreeExtents)
{
  const std::string root = makeTempRoot();
  const std::string databasePath = joinPath(root, "index.sqlite3");

  {
    extora::core::sqlite_object_index index{databasePath, 16};
    ASSERT_TRUE(succeeded(index.open()));
    extora::core::segment_data_store dataStore{root, 16};
    ASSERT_TRUE(succeeded(dataStore.open()));
    extora::core::object_store_core core{index, dataStore, defaultCoreHasherFactory()};
    ASSERT_TRUE(succeeded(core.create_bucket(extora::bucket_name{"photos"})));

    extora::object_metadata firstMetadata;
    extora::put_object_options firstOptions;
    firstOptions.expected_content_length = 4;
    VectorReader firstReader{bytesFromString("aaaa"), 4};
    ASSERT_TRUE(succeeded(core.put_object(extora::bucket_name{"photos"}, extora::object_key{"first"}, firstReader,
                                          firstMetadata, ignoredPutResult(), firstOptions)));

    extora::object_metadata gapMetadata;
    extora::put_object_options gapOptions;
    gapOptions.expected_content_length = 2;
    VectorReader gapReader{bytesFromString("xx"), 2};
    ASSERT_TRUE(succeeded(core.put_object(extora::bucket_name{"photos"}, extora::object_key{"gap"}, gapReader,
                                          gapMetadata, ignoredPutResult(), gapOptions)));

    extora::object_metadata secondMetadata;
    extora::put_object_options secondOptions;
    secondOptions.expected_content_length = 3;
    VectorReader secondReader{bytesFromString("bbb"), 3};
    ASSERT_TRUE(succeeded(core.put_object(extora::bucket_name{"photos"}, extora::object_key{"second"}, secondReader,
                                          secondMetadata, ignoredPutResult(), secondOptions)));

    ASSERT_TRUE(succeeded(
        core.delete_object(extora::bucket_name{"photos"}, extora::object_key{"first"}, ignoredDeleteResult())));
    ASSERT_TRUE(succeeded(
        core.delete_object(extora::bucket_name{"photos"}, extora::object_key{"second"}, ignoredDeleteResult())));
  }

  extora::core::sqlite_object_index recoveredIndex{databasePath, 16};
  ASSERT_TRUE(succeeded(recoveredIndex.open()));
  ASSERT_EQ(countPhysicalExtentsInState(databasePath, sqliteExtentStateFree), 2);

  extora::core::segment_data_store recoveredDataStore{root, 16};
  ASSERT_TRUE(succeeded(recoveredDataStore.open()));
  extora::core::object_store_core recoveredCore{recoveredIndex, recoveredDataStore, defaultCoreHasherFactory()};

  extora::object_metadata metadata;
  extora::put_object_options options;
  options.expected_content_length = 7;
  VectorReader reader{bytesFromString("newdata"), 7};
  ASSERT_TRUE(succeeded(recoveredCore.put_object(extora::bucket_name{"photos"}, extora::object_key{"combined"}, reader,
                                                 metadata, ignoredPutResult(), options)));

  VectorWriter writer;
  ASSERT_TRUE(succeeded(readObject(recoveredCore, extora::bucket_name{"photos"}, extora::object_key{"combined"}, writer,
                                   extora::open_object_options{}, ignoredOpenObjectResult())));
  EXPECT_EQ(stringFromBytes(writer.bytes()), "newdata");
  EXPECT_EQ(countPhysicalExtentsInState(databasePath, sqliteExtentStateFree), 0);
  EXPECT_EQ(countPhysicalExtentsInState(databasePath, sqliteExtentStateCommitted), 3);
}

} // namespace extoraTest
