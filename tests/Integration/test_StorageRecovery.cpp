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

#include <cstdio>
#include <filesystem>

#include <gtest/gtest.h>

#include "PublicTestSupport.h"
#include "SqliteTestSupport.h"
#include "extora/core/sqlite_object_index.h"

namespace extoraTest {

TEST(StorageRecoveryTest, ValidatesSegmentsBeforeNormalizingIndex)
{
  const std::string root = makeTempRoot();
  const std::string databasePath = joinPath(root, "index.sqlite3");
  {
    extora::core::sqlite_object_index index{databasePath, 16};
    ASSERT_TRUE(succeeded(index.open()));

    extora::core::physical_extent extent;
    ASSERT_TRUE(succeeded(reserveTestExtent(index, 8, extent)));
  }
  ASSERT_EQ(countPhysicalExtentsInState(databasePath, sqliteExtentStateReserved), 1);

  const std::filesystem::path segments = joinPath(root, "segments");
  ASSERT_TRUE(std::filesystem::create_directories(segments));
  const std::filesystem::path segment = segments / "0000000000000001.dat";
#if defined(_MSC_VER)
  std::FILE* file = nullptr;
  ::_wfopen_s(&file, segment.c_str(), L"wb");
#elif defined(_WIN32)
  std::FILE* file = ::_wfopen(segment.c_str(), L"wb");
#else
  std::FILE* file = std::fopen(segment.c_str(), "wb");
#endif // defined(_MSC_VER)
  ASSERT_NE(file, nullptr);
  ASSERT_EQ(std::fputc(0, file), 0);
  ASSERT_EQ(std::fclose(file), 0);

  extora::object_store_options options;
  options.root_directory = root;
  options.segment_capacity = 16;
  extora::storage_error error;
  std::unique_ptr<extora::object_store> store = extora::open_object_store(options, error);

  EXPECT_FALSE(store);
  EXPECT_EQ(error.code, extora::storage_error_code::backend_failure);
  EXPECT_EQ(countPhysicalExtentsInState(databasePath, sqliteExtentStateReserved), 1);
}

TEST(StorageRecoveryTest, QuarantinesCurrentObjectWhenSegmentExtentIsMissing)
{
  const std::string root = makeTempRoot();

  extora::object_store_options options;
  options.root_directory = root;
  options.segment_capacity = 4;

  {
    extora::storage_error error;
    std::unique_ptr<extora::object_store> store = extora::open_object_store(options, error);
    ASSERT_TRUE(succeeded(error));
    ASSERT_TRUE(store);

    ASSERT_TRUE(succeeded(store->create_bucket(extora::bucket_name{"photos"})));

    VectorReader reader{bytesFromString("abcdefgh"), 8};
    extora::object_metadata metadata;
    extora::put_object_options putOptions;
    putOptions.expected_content_length = 8;
    ASSERT_TRUE(succeeded(store->put_object(extora::bucket_name{"photos"}, extora::object_key{"broken"}, reader,
                                            metadata, ignoredPutResult(), putOptions)));
  }

  const std::string secondSegment = joinPath(joinPath(root, "segments"), "0000000000000002.dat");
  ASSERT_EQ(std::remove(secondSegment.c_str()), 0);

  extora::storage_error openError;
  std::unique_ptr<extora::object_store> recoveredStore = extora::open_object_store(options, openError);
  ASSERT_TRUE(succeeded(openError)) << openError.message;
  ASSERT_TRUE(recoveredStore);

  VectorWriter writer;
  const extora::storage_error readError =
      readObject(*recoveredStore, extora::bucket_name{"photos"}, extora::object_key{"broken"}, writer,
                 extora::open_object_options{}, ignoredOpenObjectResult());
  EXPECT_EQ(readError.code, extora::storage_error_code::object_corrupted);
}

TEST(StorageRecoveryTest, QuarantinesObjectWhenStoredBytesFailInternalChecksum)
{
  const std::string root = makeTempRoot();
  extora::object_store_options options;
  options.root_directory = root;
  options.segment_capacity = 16;

  {
    extora::storage_error error;
    std::unique_ptr<extora::object_store> store = extora::open_object_store(options, error);
    ASSERT_TRUE(succeeded(error)) << error.message;
    ASSERT_TRUE(store);
    ASSERT_TRUE(succeeded(store->create_bucket(extora::bucket_name{"photos"})));

    VectorReader reader{bytesFromString("unchecked"), 3};
    ASSERT_TRUE(succeeded(store->put_object(extora::bucket_name{"photos"}, extora::object_key{"damaged"}, reader,
                                            extora::object_metadata{}, ignoredPutResult())));

    extora::object_info info;
    ASSERT_TRUE(succeeded(store->head_object(extora::bucket_name{"photos"}, extora::object_key{"damaged"}, info)));
    ASSERT_TRUE(info.checksum.has_value());
    EXPECT_EQ(info.checksum->checksum_algorithm.value, extora::xxh3_128_checksum_algorithm.value);
  }

  const std::string segment = joinPath(joinPath(root, "segments"), "0000000000000001.dat");
  ASSERT_TRUE(overwriteFileByte(segment, 0, std::byte{'X'}));

  extora::storage_error error;
  std::unique_ptr<extora::object_store> recovered = extora::open_object_store(options, error);
  ASSERT_TRUE(succeeded(error)) << error.message;
  ASSERT_TRUE(recovered);

  extora::object_info info;
  EXPECT_EQ(recovered->head_object(extora::bucket_name{"photos"}, extora::object_key{"damaged"}, info).code,
            extora::storage_error_code::object_corrupted);
}

TEST(StorageRecoveryTest, QuarantinesEveryObjectSharingACorruptedPayload)
{
  const std::string root = makeTempRoot();
  extora::object_store_options options;
  options.root_directory = root;
  options.segment_capacity = 16;
  options.dedup_min_object_size = 1;

  {
    extora::storage_error error;
    std::unique_ptr<extora::object_store> store = extora::open_object_store(options, error);
    ASSERT_TRUE(succeeded(error)) << error.message;
    ASSERT_TRUE(store);
    ASSERT_TRUE(succeeded(store->create_bucket(extora::bucket_name{"photos"})));

    VectorReader first{bytesFromString("shared"), 2};
    ASSERT_TRUE(succeeded(store->put_object(extora::bucket_name{"photos"}, extora::object_key{"first"}, first,
                                            extora::object_metadata{}, ignoredPutResult())));
    VectorReader second{bytesFromString("shared"), 3};
    ASSERT_TRUE(succeeded(store->put_object(extora::bucket_name{"photos"}, extora::object_key{"second"}, second,
                                            extora::object_metadata{}, ignoredPutResult())));
  }

  const std::string segment = joinPath(joinPath(root, "segments"), "0000000000000001.dat");
  ASSERT_TRUE(overwriteFileByte(segment, 0, std::byte{'X'}));

  extora::storage_error error;
  std::unique_ptr<extora::object_store> recovered = extora::open_object_store(options, error);
  ASSERT_TRUE(succeeded(error)) << error.message;
  ASSERT_TRUE(recovered);

  extora::object_info info;
  EXPECT_EQ(recovered->head_object(extora::bucket_name{"photos"}, extora::object_key{"first"}, info).code,
            extora::storage_error_code::object_corrupted);
  EXPECT_EQ(recovered->head_object(extora::bucket_name{"photos"}, extora::object_key{"second"}, info).code,
            extora::storage_error_code::object_corrupted);
}

TEST(StorageRecoveryTest, QuarantinesDamagedHistoricalVersion)
{
  const std::string root = makeTempRoot();
  extora::object_store_options options;
  options.root_directory = root;
  options.segment_capacity = 4;
  extora::put_object_result firstResult;
  extora::put_object_result secondResult;

  {
    extora::storage_error error;
    std::unique_ptr<extora::object_store> store = extora::open_object_store(options, error);
    ASSERT_TRUE(succeeded(error));
    ASSERT_TRUE(store);
    ASSERT_TRUE(succeeded(store->create_bucket(extora::bucket_name{"photos"})));
    ASSERT_TRUE(succeeded(
        store->set_bucket_versioning(extora::bucket_name{"photos"}, extora::bucket_versioning_configuration::enabled)));

    VectorReader firstReader{bytesFromString("aaaa"), 4};
    ASSERT_TRUE(succeeded(store->put_object(extora::bucket_name{"photos"}, extora::object_key{"object"}, firstReader,
                                            extora::object_metadata{}, firstResult)));
    VectorReader secondReader{bytesFromString("bbbb"), 4};
    ASSERT_TRUE(succeeded(store->put_object(extora::bucket_name{"photos"}, extora::object_key{"object"}, secondReader,
                                            extora::object_metadata{}, secondResult)));
  }

  const std::string firstSegment = joinPath(joinPath(root, "segments"), "0000000000000001.dat");
  ASSERT_EQ(std::remove(firstSegment.c_str()), 0);

  extora::storage_error openError;
  std::unique_ptr<extora::object_store> recoveredStore = extora::open_object_store(options, openError);
  ASSERT_TRUE(succeeded(openError)) << openError.message;
  ASSERT_TRUE(recoveredStore);

  extora::head_object_options historicalOptions;
  historicalOptions.version_id = firstResult.version_id;
  extora::object_info historicalInfo;
  const extora::storage_error historicalError = recoveredStore->head_object(
      extora::bucket_name{"photos"}, extora::object_key{"object"}, historicalInfo, historicalOptions);
  EXPECT_EQ(historicalError.code, extora::storage_error_code::object_corrupted);

  VectorWriter currentWriter;
  ASSERT_TRUE(succeeded(readObject(*recoveredStore, extora::bucket_name{"photos"}, extora::object_key{"object"},
                                   currentWriter, extora::open_object_options{}, ignoredOpenObjectResult())));
  EXPECT_EQ(stringFromBytes(currentWriter.bytes()), "bbbb");
}

TEST(StorageRecoveryTest, KeepsDamagedCurrentVersionQuarantined)
{
  const std::string root = makeTempRoot();
  extora::object_store_options options;
  options.root_directory = root;
  options.segment_capacity = 4;
  extora::put_object_result firstResult;
  extora::put_object_result secondResult;

  {
    extora::storage_error error;
    std::unique_ptr<extora::object_store> store = extora::open_object_store(options, error);
    ASSERT_TRUE(succeeded(error));
    ASSERT_TRUE(store);
    ASSERT_TRUE(succeeded(store->create_bucket(extora::bucket_name{"photos"})));
    ASSERT_TRUE(succeeded(
        store->set_bucket_versioning(extora::bucket_name{"photos"}, extora::bucket_versioning_configuration::enabled)));

    VectorReader firstReader{bytesFromString("aaaa"), 4};
    ASSERT_TRUE(succeeded(store->put_object(extora::bucket_name{"photos"}, extora::object_key{"object"}, firstReader,
                                            extora::object_metadata{}, firstResult)));
    VectorReader secondReader{bytesFromString("bbbb"), 4};
    ASSERT_TRUE(succeeded(store->put_object(extora::bucket_name{"photos"}, extora::object_key{"object"}, secondReader,
                                            extora::object_metadata{}, secondResult)));
  }

  const std::string secondSegment = joinPath(joinPath(root, "segments"), "0000000000000002.dat");
  ASSERT_EQ(std::remove(secondSegment.c_str()), 0);

  extora::storage_error openError;
  std::unique_ptr<extora::managed_object_store> recoveredStore = extora::open_object_store(options, openError);
  ASSERT_TRUE(succeeded(openError));
  ASSERT_TRUE(recoveredStore);

  VectorWriter currentWriter;
  extora::open_object_result currentResult;
  EXPECT_EQ(readObject(*recoveredStore, extora::bucket_name{"photos"}, extora::object_key{"object"}, currentWriter,
                       extora::open_object_options{}, currentResult)
                .code,
            extora::storage_error_code::object_corrupted);
  EXPECT_EQ(currentResult.object.version_id.value, secondResult.version_id.value);

  extora::head_object_options damagedOptions;
  damagedOptions.version_id = secondResult.version_id;
  extora::object_info damagedInfo;
  const extora::storage_error damagedError = recoveredStore->head_object(
      extora::bucket_name{"photos"}, extora::object_key{"object"}, damagedInfo, damagedOptions);
  EXPECT_EQ(damagedError.code, extora::storage_error_code::object_corrupted);

  extora::open_object_options previousOptions;
  previousOptions.version_id = firstResult.version_id;
  VectorWriter previousWriter;
  ASSERT_TRUE(succeeded(readObject(*recoveredStore, extora::bucket_name{"photos"}, extora::object_key{"object"},
                                   previousWriter, previousOptions, ignoredOpenObjectResult())));
  EXPECT_EQ(stringFromBytes(previousWriter.bytes()), "aaaa");

  extora::reclaim_storage_options reclaimOptions;
  reclaimOptions.delete_corrupted_objects = true;
  ASSERT_TRUE(succeeded(recoveredStore->reclaim_storage(ignoredReclaimResult(), reclaimOptions)));

  VectorWriter promotedWriter;
  ASSERT_TRUE(succeeded(readObject(*recoveredStore, extora::bucket_name{"photos"}, extora::object_key{"object"},
                                   promotedWriter, extora::open_object_options{}, ignoredOpenObjectResult())));
  EXPECT_EQ(stringFromBytes(promotedWriter.bytes()), "aaaa");
}

TEST(StorageRecoveryTest, AbortsMultipartUploadWhenAPartExtentIsMissing)
{
  const std::string root = makeTempRoot();
  extora::object_store_options options;
  options.root_directory = root;
  options.segment_capacity = 4;
  extora::create_multipart_upload_result upload;

  {
    extora::storage_error error;
    std::unique_ptr<extora::object_store> store = extora::open_object_store(options, error);
    ASSERT_TRUE(succeeded(error));
    ASSERT_TRUE(store);
    ASSERT_TRUE(succeeded(store->create_bucket(extora::bucket_name{"photos"})));
    ASSERT_TRUE(succeeded(store->create_multipart_upload(extora::bucket_name{"photos"}, extora::object_key{"archive"},
                                                         extora::object_metadata{}, upload)));

    VectorReader reader{bytesFromString("part"), 4};
    ASSERT_TRUE(succeeded(store->upload_part(extora::bucket_name{"photos"}, extora::object_key{"archive"},
                                             upload.upload_id, 1, reader, ignoredUploadPartResult())));
  }

  const std::string segment = joinPath(joinPath(root, "segments"), "0000000000000001.dat");
  ASSERT_EQ(std::remove(segment.c_str()), 0);

  extora::storage_error openError;
  std::unique_ptr<extora::object_store> recoveredStore = extora::open_object_store(options, openError);
  ASSERT_TRUE(succeeded(openError)) << openError.message;
  ASSERT_TRUE(recoveredStore);

  extora::multipart_part_list parts;
  EXPECT_EQ(
      recoveredStore->list_parts(extora::bucket_name{"photos"}, extora::object_key{"archive"}, upload.upload_id, parts)
          .code,
      extora::storage_error_code::multipart_upload_not_found);
  EXPECT_TRUE(succeeded(recoveredStore->delete_bucket(extora::bucket_name{"photos"})));
}

TEST(StorageRecoveryTest, AbortsMultipartUploadWhenPartBytesFailInternalChecksum)
{
  const std::string root = makeTempRoot();
  extora::object_store_options options;
  options.root_directory = root;
  options.segment_capacity = 16;
  extora::create_multipart_upload_result upload;

  {
    extora::storage_error error;
    std::unique_ptr<extora::object_store> store = extora::open_object_store(options, error);
    ASSERT_TRUE(succeeded(error)) << error.message;
    ASSERT_TRUE(store);
    ASSERT_TRUE(succeeded(store->create_bucket(extora::bucket_name{"photos"})));
    ASSERT_TRUE(succeeded(store->create_multipart_upload(extora::bucket_name{"photos"}, extora::object_key{"archive"},
                                                         extora::object_metadata{}, upload)));

    VectorReader reader{bytesFromString("part"), 2};
    ASSERT_TRUE(succeeded(store->upload_part(extora::bucket_name{"photos"}, extora::object_key{"archive"},
                                             upload.upload_id, 1, reader, ignoredUploadPartResult())));
  }

  const std::string segment = joinPath(joinPath(root, "segments"), "0000000000000001.dat");
  ASSERT_TRUE(overwriteFileByte(segment, 0, std::byte{'X'}));

  extora::storage_error error;
  std::unique_ptr<extora::object_store> recovered = extora::open_object_store(options, error);
  ASSERT_TRUE(succeeded(error)) << error.message;
  ASSERT_TRUE(recovered);

  extora::multipart_part_list parts;
  EXPECT_EQ(
      recovered->list_parts(extora::bucket_name{"photos"}, extora::object_key{"archive"}, upload.upload_id, parts).code,
      extora::storage_error_code::multipart_upload_not_found);
}

} // namespace extoraTest
