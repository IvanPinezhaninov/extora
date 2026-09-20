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
#include <filesystem>
#include <limits>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif // WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif // NOMINMAX
#include <windows.h>
#elif defined(__linux__)
#include <sys/stat.h>
#endif // defined(_WIN32)

#include <gtest/gtest.h>

#include "PublicTestSupport.h"
#include "SqliteTestSupport.h"

namespace extoraTest {

#if defined(_WIN32) || defined(__linux__)
namespace {

std::uint64_t allocatedFileSize(const std::filesystem::path& path)
{
#if defined(_WIN32)
  const HANDLE handle = ::CreateFileW(path.c_str(), FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                                      OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (handle == INVALID_HANDLE_VALUE) return 0;

  FILE_STANDARD_INFO info{};
  const BOOL inspected = ::GetFileInformationByHandleEx(handle, FileStandardInfo, &info, sizeof(info));
  ::CloseHandle(handle);
  if (inspected == 0 || info.AllocationSize.QuadPart < 0) return 0;
  return static_cast<std::uint64_t>(info.AllocationSize.QuadPart);
#else
  struct stat info;
  if (::stat(path.c_str(), &info) != 0 || info.st_blocks < 0) return 0;
  return static_cast<std::uint64_t>(info.st_blocks) * 512;
#endif // defined(_WIN32)
}

} // namespace
#endif // defined(_WIN32) || defined(__linux__)

TEST(StorageOptionsTest, RejectsInvalidStorageConfiguration)
{
  extora::storage_error error;
  extora::object_store_options options;

  EXPECT_EQ(extora::open_object_store(options, error), nullptr);
  EXPECT_EQ(error.code, extora::storage_error_code::invalid_configuration);

  options.root_directory = makeTempRoot();
  options.segment_capacity = 0;
  EXPECT_EQ(extora::open_object_store(options, error), nullptr);
  EXPECT_EQ(error.code, extora::storage_error_code::invalid_configuration);

  options.segment_capacity = static_cast<std::uint64_t>((std::numeric_limits<std::int64_t>::max)()) + 1u;
  EXPECT_EQ(extora::open_object_store(options, error), nullptr);
  EXPECT_EQ(error.code, extora::storage_error_code::invalid_configuration);

  options.segment_capacity = extora::default_segment_capacity;
  options.max_extent_size = 0;
  EXPECT_EQ(extora::open_object_store(options, error), nullptr);
  EXPECT_EQ(error.code, extora::storage_error_code::invalid_configuration);

  options.max_extent_size = extora::default_max_extent_size;
  options.durability = static_cast<extora::storage_durability>(255);
  EXPECT_EQ(extora::open_object_store(options, error), nullptr);
  EXPECT_EQ(error.code, extora::storage_error_code::invalid_configuration);

  options.durability = extora::storage_durability::balanced;
  options.segment_allocation = static_cast<extora::segment_allocation_strategy>(255);
  EXPECT_EQ(extora::open_object_store(options, error), nullptr);
  EXPECT_EQ(error.code, extora::storage_error_code::invalid_configuration);
}

TEST(StorageOptionsTest, AppliesMaxExtentSizeToKnownAndUnknownWrites)
{
  const std::string root = makeTempRoot();

  extora::object_store_options options;
  options.root_directory = root;
  options.segment_capacity = 1024 * 1024;
  options.max_extent_size = 128 * 1024;

  extora::storage_error error;
  std::unique_ptr<extora::object_store> store = extora::open_object_store(options, error);
  ASSERT_TRUE(succeeded(error));
  ASSERT_TRUE(store);

  ASSERT_TRUE(succeeded(store->create_bucket(extora::bucket_name{"photos"})));

  std::vector<std::byte> unknownSizeData(70 * 1024, std::byte{0x42});
  VectorReader unknownSizeReader{std::move(unknownSizeData), 8 * 1024};

  ASSERT_TRUE(succeeded(store->put_object(extora::bucket_name{"photos"}, extora::object_key{"unknown-size.bin"},
                                          unknownSizeReader, extora::object_metadata{}, ignoredPutResult())));

  std::vector<std::byte> knownSizeData(300 * 1024, std::byte{0x24});
  VectorReader knownSizeReader{std::move(knownSizeData), 8 * 1024};
  extora::object_metadata knownSizeMetadata;
  extora::put_object_options knownSizeOptions;
  knownSizeOptions.expected_content_length = 300 * 1024;

  ASSERT_TRUE(succeeded(store->put_object(extora::bucket_name{"photos"}, extora::object_key{"known-size.bin"},
                                          knownSizeReader, knownSizeMetadata, ignoredPutResult(), knownSizeOptions)));

  const std::string databasePath = joinPath(root, "index.sqlite3");
  EXPECT_EQ(storageExtentLength(databasePath, 1, 0), 70 * 1024);
  EXPECT_EQ(storageExtentLength(databasePath, 1, 70 * 1024), 58 * 1024);
  EXPECT_EQ(storageExtentLength(databasePath, 1, 128 * 1024), 128 * 1024);
  EXPECT_EQ(storageExtentLength(databasePath, 1, 256 * 1024), 114 * 1024);
}

TEST(StorageOptionsTest, AppliesConfiguredMultipartMinimumPartSize)
{
  EXPECT_EQ(extora::default_multipart_min_part_size, 5u * 1024u * 1024u);

  extora::object_store_options options;
  options.root_directory = makeTempRoot();
  options.segment_capacity = 1024 * 1024;
  options.multipart_min_part_size = 4;

  extora::storage_error error;
  std::unique_ptr<extora::object_store> store = extora::open_object_store(options, error);
  ASSERT_TRUE(succeeded(error));
  ASSERT_TRUE(store);
  ASSERT_TRUE(succeeded(store->create_bucket(extora::bucket_name{"photos"})));

  const extora::bucket_name bucket{"photos"};
  const extora::object_key key{"small-multipart"};
  extora::create_multipart_upload_result upload;
  ASSERT_TRUE(succeeded(store->create_multipart_upload(bucket, key, extora::object_metadata{}, upload)));

  VectorReader firstReader{bytesFromString("four"), 4};
  extora::upload_part_result first;
  ASSERT_TRUE(succeeded(store->upload_part(bucket, key, upload.upload_id, 1, firstReader, first)));
  VectorReader secondReader{bytesFromString("tail"), 4};
  extora::upload_part_result second;
  ASSERT_TRUE(succeeded(store->upload_part(bucket, key, upload.upload_id, 2, secondReader, second)));

  extora::complete_multipart_upload_options completeOptions;
  completeOptions.parts.push_back(extora::completed_multipart_part{1, first.etag, std::nullopt});
  completeOptions.parts.push_back(extora::completed_multipart_part{2, second.etag, std::nullopt});
  ASSERT_TRUE(
      succeeded(store->complete_multipart_upload(bucket, key, upload.upload_id, completeOptions, ignoredPutResult())));

  VectorWriter writer;
  ASSERT_TRUE(
      succeeded(readObject(*store, bucket, key, writer, extora::open_object_options{}, ignoredOpenObjectResult())));
  EXPECT_EQ(stringFromBytes(writer.bytes()), "fourtail");
}

#if defined(_WIN32) || defined(__linux__)
TEST(StorageOptionsTest, AppliesReservedSegmentAllocationThroughFactory)
{
  const std::string root = makeTempRoot();

  extora::object_store_options options;
  options.root_directory = root;
  options.segment_capacity = 1024 * 1024;
  options.segment_allocation = extora::segment_allocation_strategy::reserve;

  extora::storage_error error;
  std::unique_ptr<extora::object_store> store = extora::open_object_store(options, error);
  ASSERT_TRUE(succeeded(error));
  ASSERT_TRUE(store);
  ASSERT_TRUE(succeeded(store->create_bucket(extora::bucket_name{"photos"})));

  VectorReader reader{bytesFromString("x"), 1};
  extora::object_metadata metadata;
  extora::put_object_options putOptions;
  putOptions.expected_content_length = 1;
  ASSERT_TRUE(succeeded(store->put_object(extora::bucket_name{"photos"}, extora::object_key{"object"}, reader, metadata,
                                          ignoredPutResult(), putOptions)));
  store.reset();

  const std::filesystem::path segment = std::filesystem::path{root} / "segments" / "0000000000000001.dat";
  ASSERT_TRUE(regularFileExists(segment));
  EXPECT_GE(allocatedFileSize(segment), options.segment_capacity);
}
#endif // defined(_WIN32) || defined(__linux__)

TEST(StorageOptionsTest, AppliesObjectReadConcurrencyLimit)
{
  extora::object_store_options options;
  options.root_directory = makeTempRoot();
  options.segment_capacity = 1024 * 1024;
  options.max_concurrent_reads = 1;

  extora::storage_error error;
  std::unique_ptr<extora::object_store> store = extora::open_object_store(options, error);
  ASSERT_TRUE(succeeded(error));
  ASSERT_TRUE(store);
  ASSERT_TRUE(succeeded(store->create_bucket(extora::bucket_name{"photos"})));

  VectorReader reader{bytesFromString("data"), 4};
  ASSERT_TRUE(succeeded(store->put_object(extora::bucket_name{"photos"}, extora::object_key{"limited"}, reader,
                                          extora::object_metadata{}, ignoredPutResult())));

  extora::open_object_result first;
  ASSERT_TRUE(succeeded(store->open_object(extora::bucket_name{"photos"}, extora::object_key{"limited"}, first)));

  extora::open_object_result second;
  error = store->open_object(extora::bucket_name{"photos"}, extora::object_key{"limited"}, second);
  EXPECT_EQ(error.code, extora::storage_error_code::concurrency_limit_exceeded);

  first.reader.reset();
  EXPECT_TRUE(succeeded(store->open_object(extora::bucket_name{"photos"}, extora::object_key{"limited"}, second)));
}

TEST(StorageOptionsTest, AppliesObjectWriteConcurrencyLimit)
{
  extora::object_store_options options;
  options.root_directory = makeTempRoot();
  options.segment_capacity = 1024 * 1024;
  options.max_concurrent_writes = 0;

  extora::storage_error error;
  std::unique_ptr<extora::object_store> store = extora::open_object_store(options, error);
  ASSERT_TRUE(succeeded(error));
  ASSERT_TRUE(store);
  ASSERT_TRUE(succeeded(store->create_bucket(extora::bucket_name{"photos"})));

  VectorReader reader{bytesFromString("data"), 4};
  error = store->put_object(extora::bucket_name{"photos"}, extora::object_key{"blocked"}, reader,
                            extora::object_metadata{}, ignoredPutResult());
  EXPECT_EQ(error.code, extora::storage_error_code::concurrency_limit_exceeded);

  extora::create_multipart_upload_result upload;
  ASSERT_TRUE(succeeded(store->create_multipart_upload(extora::bucket_name{"photos"}, extora::object_key{"multipart"},
                                                       extora::object_metadata{}, upload)));
  VectorReader partReader{bytesFromString("part"), 4};
  EXPECT_EQ(store
                ->upload_part(extora::bucket_name{"photos"}, extora::object_key{"multipart"}, upload.upload_id, 1,
                              partReader, ignoredUploadPartResult())
                .code,
            extora::storage_error_code::concurrency_limit_exceeded);
}

} // namespace extoraTest
