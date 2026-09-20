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
#include <string>

#include <gtest/gtest.h>

#include "CoreTestSupport.h"
#include "PublicTestSupport.h"
#include "SqliteTestSupport.h"
#include "extora/core/object_store_core.h"
#include "extora/core/segment_data_store.h"
#include "extora/core/sqlite_object_index.h"

namespace extoraTest {

namespace {

void expectExtent(const extora::core::physical_extent& extent, std::uint64_t segmentId, std::uint64_t offset,
                  std::uint64_t length)
{
  EXPECT_EQ(extent.segment_id, segmentId);
  EXPECT_EQ(extent.offset, offset);
  EXPECT_EQ(extent.length, length);
}

} // namespace

TEST(StorageMultiExtentTest, UsesActiveSegmentTailBeforeContinuingInNextSegment)
{
  const std::string root = makeTempRoot();
  ASSERT_FALSE(root.empty());

  extora::core::sqlite_object_index index{joinPath(root, "index.sqlite3"), 16};
  ASSERT_TRUE(succeeded(index.open()));

  extora::core::segment_data_store dataStore{root, 16};
  ASSERT_TRUE(succeeded(dataStore.open()));

  extora::core::object_store_core core{index, dataStore, defaultCoreHasherFactory()};
  ASSERT_TRUE(succeeded(core.create_bucket(extora::bucket_name{"photos"})));

  VectorReader firstReader{bytesFromString("1234567890"), 32};
  extora::object_metadata firstMetadata;
  extora::put_object_options firstOptions;
  firstOptions.expected_content_length = 10;
  ASSERT_TRUE(succeeded(core.put_object(extora::bucket_name{"photos"}, extora::object_key{"first"}, firstReader,
                                        firstMetadata, ignoredPutResult(), firstOptions)));

  VectorReader secondReader{bytesFromString("abcdefghij"), 32};
  extora::object_metadata secondMetadata;
  extora::put_object_options secondOptions;
  secondOptions.expected_content_length = 10;
  ASSERT_TRUE(succeeded(core.put_object(extora::bucket_name{"photos"}, extora::object_key{"second"}, secondReader,
                                        secondMetadata, ignoredPutResult(), secondOptions)));

  extora::core::indexed_object second;
  ASSERT_TRUE(succeeded(index.find_object(extora::bucket_name{"photos"}, extora::object_key{"second"}, second)));
  ASSERT_EQ(second.payload.extents.size(), 2);
  expectExtent(second.payload.extents[0], 1, 10, 6);
  expectExtent(second.payload.extents[1], 2, 0, 4);

  EXPECT_TRUE(regularFileExists(joinPath(joinPath(root, "segments"), "0000000000000001.dat")));
  EXPECT_TRUE(regularFileExists(joinPath(joinPath(root, "segments"), "0000000000000002.dat")));

  VectorWriter writer;
  ASSERT_TRUE(succeeded(readObject(core, extora::bucket_name{"photos"}, extora::object_key{"second"}, writer,
                                   extora::open_object_options{}, ignoredOpenObjectResult())));

  EXPECT_EQ(stringFromBytes(writer.bytes()), "abcdefghij");
}

TEST(StorageMultiExtentTest, ReopensAndVerifiesObjectLargerThanSegmentCapacity)
{
  const std::string root = makeTempRoot();
  ASSERT_FALSE(root.empty());
  const std::string databasePath = joinPath(root, "index.sqlite3");
  const std::string source = "object-spans-several-segments";

  {
    extora::core::sqlite_object_index index{databasePath, 8};
    ASSERT_TRUE(succeeded(index.open()));

    extora::core::segment_data_store dataStore{root, 8};
    ASSERT_TRUE(succeeded(dataStore.open()));

    extora::core::object_store_core core{index, dataStore, defaultCoreHasherFactory()};
    ASSERT_TRUE(succeeded(core.create_bucket(extora::bucket_name{"photos"})));

    VectorReader reader{bytesFromString(source), 11};
    extora::object_metadata metadata;
    extora::put_object_options options;
    options.expected_content_length = source.size();
    ASSERT_TRUE(succeeded(core.put_object(extora::bucket_name{"photos"}, extora::object_key{"large"}, reader, metadata,
                                          ignoredPutResult(), options)));

    extora::core::indexed_object object;
    ASSERT_TRUE(succeeded(index.find_object(extora::bucket_name{"photos"}, extora::object_key{"large"}, object)));
    ASSERT_EQ(object.payload.extents.size(), 4);
    expectExtent(object.payload.extents[0], 1, 0, 8);
    expectExtent(object.payload.extents[1], 2, 0, 8);
    expectExtent(object.payload.extents[2], 3, 0, 8);
    expectExtent(object.payload.extents[3], 4, 0, 5);
  }

  for (std::uint64_t segmentId = 1; segmentId <= 4; ++segmentId) {
    const std::string segmentName = "000000000000000" + std::to_string(segmentId) + ".dat";
    EXPECT_TRUE(regularFileExists(joinPath(joinPath(root, "segments"), segmentName)));
  }

  extora::core::sqlite_object_index reopenedIndex{databasePath, 8};
  ASSERT_TRUE(succeeded(reopenedIndex.open()));
  extora::core::segment_data_store reopenedDataStore{root, 8};
  ASSERT_TRUE(succeeded(reopenedDataStore.open()));
  extora::core::object_store_core reopenedCore{reopenedIndex, reopenedDataStore, defaultCoreHasherFactory()};

  extora::open_object_options options;
  options.verify_integrity = true;
  VectorWriter writer;
  ASSERT_TRUE(succeeded(readObject(reopenedCore, extora::bucket_name{"photos"}, extora::object_key{"large"}, writer,
                                   options, ignoredOpenObjectResult())));

  EXPECT_EQ(stringFromBytes(writer.bytes()), source);
}

TEST(StorageMultiExtentTest, PutsUnknownLengthObjectAcrossSegments)
{
  const std::string root = makeTempRoot();
  ASSERT_FALSE(root.empty());

  extora::core::sqlite_object_index index{joinPath(root, "index.sqlite3"), 8};
  ASSERT_TRUE(succeeded(index.open()));

  extora::core::segment_data_store dataStore{root, 8};
  ASSERT_TRUE(succeeded(dataStore.open()));

  extora::core::object_store_core core{index, dataStore, defaultCoreHasherFactory()};
  ASSERT_TRUE(succeeded(core.create_bucket(extora::bucket_name{"photos"})));

  const std::string source = "unknown-length-value";
  VectorReader reader{bytesFromString(source), 5};
  ASSERT_TRUE(succeeded(core.put_object(extora::bucket_name{"photos"}, extora::object_key{"unknown"}, reader,
                                        extora::object_metadata{}, ignoredPutResult())));

  extora::core::indexed_object object;
  ASSERT_TRUE(succeeded(index.find_object(extora::bucket_name{"photos"}, extora::object_key{"unknown"}, object)));
  ASSERT_EQ(object.payload.extents.size(), 3);
  expectExtent(object.payload.extents[0], 1, 0, 8);
  expectExtent(object.payload.extents[1], 2, 0, 8);
  expectExtent(object.payload.extents[2], 3, 0, 4);

  extora::open_object_options options;
  options.verify_integrity = true;
  VectorWriter writer;
  ASSERT_TRUE(succeeded(readObject(core, extora::bucket_name{"photos"}, extora::object_key{"unknown"}, writer, options,
                                   ignoredOpenObjectResult())));
  EXPECT_EQ(stringFromBytes(writer.bytes()), source);
}

TEST(StorageMultiExtentTest, ReadsRangeAcrossSegmentExtents)
{
  const std::string root = makeTempRoot();
  ASSERT_FALSE(root.empty());

  extora::core::sqlite_object_index index{joinPath(root, "index.sqlite3"), 4};
  ASSERT_TRUE(succeeded(index.open()));

  extora::core::segment_data_store dataStore{root, 4};
  ASSERT_TRUE(succeeded(dataStore.open()));

  extora::core::object_store_core core{index, dataStore, defaultCoreHasherFactory()};
  ASSERT_TRUE(succeeded(core.create_bucket(extora::bucket_name{"photos"})));

  const std::string source = "abcdefghij";
  VectorReader reader{bytesFromString(source), 10};
  extora::object_metadata metadata;
  extora::put_object_options putOptions;
  putOptions.expected_content_length = source.size();
  ASSERT_TRUE(succeeded(core.put_object(extora::bucket_name{"photos"}, extora::object_key{"range-large"}, reader,
                                        metadata, ignoredPutResult(), putOptions)));

  extora::open_object_options options;
  options.range = extora::byte_range{extora::byte_range_type::offset_length, 2, 7};

  VectorWriter writer;
  extora::open_object_result result;
  ASSERT_TRUE(succeeded(
      readObject(core, extora::bucket_name{"photos"}, extora::object_key{"range-large"}, writer, options, result)));

  EXPECT_EQ(stringFromBytes(writer.bytes()), "cdefghi");
  ASSERT_TRUE(result.byte_range.has_value());
  EXPECT_EQ(result.byte_range->offset, 2);
  EXPECT_EQ(result.byte_range->length, 7);
}

TEST(StorageMultiExtentTest, CompletesMultipartPartAcrossSegments)
{
  const std::string root = makeTempRoot();
  ASSERT_FALSE(root.empty());

  extora::core::sqlite_object_index index{joinPath(root, "index.sqlite3"), 8};
  ASSERT_TRUE(succeeded(index.open()));

  extora::core::segment_data_store dataStore{root, 8};
  ASSERT_TRUE(succeeded(dataStore.open()));

  extora::core::object_store_core core{index, dataStore, defaultCoreHasherFactory()};
  const extora::bucket_name bucket{"photos"};
  const extora::object_key key{"multipart"};
  ASSERT_TRUE(succeeded(core.create_bucket(bucket)));

  extora::create_multipart_upload_result upload;
  ASSERT_TRUE(succeeded(core.create_multipart_upload(bucket, key, extora::object_metadata{}, upload)));

  const std::string source = "multipart-data";
  VectorReader reader{bytesFromString(source), 5};
  extora::upload_part_result part;
  ASSERT_TRUE(succeeded(core.upload_part(bucket, key, upload.upload_id, 1, reader, part)));

  extora::complete_multipart_upload_options completeOptions;
  completeOptions.parts.push_back(extora::completed_multipart_part{1, part.etag, part.checksum.value});
  extora::put_object_result completeResult;
  ASSERT_TRUE(
      succeeded(core.complete_multipart_upload(bucket, key, upload.upload_id, completeOptions, completeResult)));

  extora::core::indexed_object object;
  ASSERT_TRUE(succeeded(index.find_object(bucket, key, object)));
  ASSERT_EQ(object.payload.extents.size(), 2);
  expectExtent(object.payload.extents[0], 1, 0, 8);
  expectExtent(object.payload.extents[1], 2, 0, 6);

  extora::open_object_options options;
  options.verify_integrity = true;
  VectorWriter writer;
  ASSERT_TRUE(succeeded(readObject(core, bucket, key, writer, options, ignoredOpenObjectResult())));
  EXPECT_EQ(stringFromBytes(writer.bytes()), source);
}

TEST(StorageMultiExtentTest, CreatesStorageForRecoveredFreeSegment)
{
  const std::string root = makeTempRoot();
  ASSERT_FALSE(root.empty());
  const std::string databasePath = joinPath(root, "index.sqlite3");

  {
    extora::core::sqlite_object_index index{databasePath, 16};
    ASSERT_TRUE(succeeded(index.open()));

    extora::core::physical_extent interruptedExtent;
    ASSERT_TRUE(succeeded(reserveTestExtent(index, 8, interruptedExtent)));
  }

  extora::core::sqlite_object_index recoveredIndex{databasePath, 16};
  ASSERT_TRUE(succeeded(recoveredIndex.open()));

  extora::core::physical_extent recoveredExtent;
  ASSERT_TRUE(succeeded(reserveTestExtent(recoveredIndex, 8, recoveredExtent)));

  extora::core::segment_data_store dataStore{root, 16};
  ASSERT_TRUE(succeeded(dataStore.open()));
  extora::core::data_write_handle handle;
  ASSERT_TRUE(succeeded(dataStore.begin_write(recoveredExtent, handle)));
  dataStore.finish_write(handle);
  EXPECT_TRUE(regularFileExists(joinPath(joinPath(root, "segments"), "0000000000000001.dat")));
}

} // namespace extoraTest
