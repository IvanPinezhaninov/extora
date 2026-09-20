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

#include <string>
#include <string_view>

#include <gtest/gtest.h>

#include "PublicTestSupport.h"
#include "SqliteTestSupport.h"

namespace extoraTest {

namespace {

extora::storage_error putText(extora::object_store& store, std::string_view bucket, std::string_view key,
                              std::string_view text, const extora::put_object_options& options = {})
{
  VectorReader reader{bytesFromString(text), 7};
  extora::object_metadata metadata;
  metadata.content_type = "text/plain";
  extora::put_object_options effectiveOptions = options;
  effectiveOptions.expected_content_length = text.size();
  return store.put_object(extora::bucket_name{std::string{bucket}}, extora::object_key{std::string{key}}, reader,
                          metadata, ignoredPutResult(), effectiveOptions);
}

TEST(StorageDeduplicationTest, ReusesBlobAndKeepsItUntilLastReferenceIsDeleted)
{
  const std::string root = makeTempRoot();
  extora::object_store_options options;
  options.root_directory = root;
  options.dedup_min_object_size = 1;
  extora::storage_error error;
  std::unique_ptr<extora::managed_object_store> store = extora::open_object_store(options, error);
  ASSERT_TRUE(succeeded(error));
  ASSERT_TRUE(succeeded(store->create_bucket(extora::bucket_name{"bucket"})));

  ASSERT_TRUE(succeeded(putText(*store, "bucket", "one", "same body")));
  ASSERT_TRUE(succeeded(putText(*store, "bucket", "two", "same body")));
  const std::string databasePath = joinPath(root, "index.sqlite3");
  EXPECT_EQ(scalarQuery(databasePath, "SELECT COUNT(*) FROM physical_extents WHERE state = 2"), 1);
  EXPECT_EQ(scalarQuery(databasePath, "SELECT COUNT(*) FROM object_payloads"), 1);
  EXPECT_EQ(scalarQuery(databasePath, "SELECT COUNT(*) FROM payload_extents"), 1);

  ASSERT_TRUE(
      succeeded(store->delete_object(extora::bucket_name{"bucket"}, extora::object_key{"one"}, ignoredDeleteResult())));
  EXPECT_EQ(scalarQuery(databasePath, "SELECT COUNT(*) FROM physical_extents WHERE state = 2"), 1);
  ASSERT_TRUE(
      succeeded(store->delete_object(extora::bucket_name{"bucket"}, extora::object_key{"two"}, ignoredDeleteResult())));
  EXPECT_EQ(scalarQuery(databasePath, "SELECT COUNT(*) FROM physical_extents WHERE state = 3"), 1);

  ASSERT_TRUE(succeeded(store->reclaim_storage(ignoredReclaimResult())));
  EXPECT_EQ(scalarQuery(databasePath, "SELECT COUNT(*) FROM object_payloads"), 0);
  EXPECT_EQ(scalarQuery(databasePath, "SELECT COUNT(*) FROM payload_extents"), 0);
}

TEST(StorageDeduplicationTest, BucketDeleteKeepsBlobReferencedByAnotherBucket)
{
  const std::string root = makeTempRoot();
  extora::object_store_options options;
  options.root_directory = root;
  options.dedup_min_object_size = 1;
  extora::storage_error error;
  std::unique_ptr<extora::object_store> store = extora::open_object_store(options, error);
  ASSERT_TRUE(succeeded(error));
  ASSERT_TRUE(succeeded(store->create_bucket(extora::bucket_name{"source"})));
  ASSERT_TRUE(succeeded(store->create_bucket(extora::bucket_name{"target"})));
  ASSERT_TRUE(succeeded(putText(*store, "source", "object", "shared")));
  ASSERT_TRUE(succeeded(putText(*store, "target", "object", "shared")));

  ASSERT_TRUE(succeeded(
      store->delete_object(extora::bucket_name{"source"}, extora::object_key{"object"}, ignoredDeleteResult())));
  ASSERT_TRUE(succeeded(store->delete_bucket(extora::bucket_name{"source"})));
  EXPECT_EQ(scalarQuery(joinPath(root, "index.sqlite3"), "SELECT COUNT(*) FROM physical_extents WHERE state = 2"), 1);

  VectorWriter writer;
  ASSERT_TRUE(succeeded(readObject(*store, extora::bucket_name{"target"}, extora::object_key{"object"}, writer,
                                   extora::open_object_options{}, ignoredOpenObjectResult())));
  EXPECT_EQ(stringFromBytes(writer.bytes()), "shared");
}

TEST(StorageDeduplicationTest, UsesBuiltInHasherWithoutExtensionFactory)
{
  const std::string root = makeTempRoot();
  extora::object_store_options options;
  options.root_directory = root;
  options.dedup_min_object_size = 1;

  extora::storage_error error;
  std::unique_ptr<extora::object_store> store = extora::open_object_store(options, error);
  ASSERT_TRUE(succeeded(error)) << error.message;
  ASSERT_TRUE(store);
  ASSERT_TRUE(succeeded(store->create_bucket(extora::bucket_name{"bucket"})));

  ASSERT_TRUE(succeeded(putText(*store, "bucket", "first", "same", extora::put_object_options{})));
  ASSERT_TRUE(succeeded(putText(*store, "bucket", "second", "same", extora::put_object_options{})));

  const std::string databasePath = joinPath(root, "index.sqlite3");
  EXPECT_EQ(scalarQuery(databasePath, "SELECT COUNT(*) FROM object_payloads"), 1);
  EXPECT_EQ(scalarQuery(databasePath, "SELECT COUNT(*) FROM physical_extents"), 2);
}

TEST(StorageDeduplicationTest, SharesContentAcrossMetadataOverwriteAndCopy)
{
  const std::string root = makeTempRoot();
  extora::object_store_options options;
  options.root_directory = root;
  options.dedup_min_object_size = 1;
  extora::storage_error error;
  std::unique_ptr<extora::object_store> store = extora::open_object_store(options, error);
  ASSERT_TRUE(succeeded(error));
  ASSERT_TRUE(succeeded(store->create_bucket(extora::bucket_name{"bucket"})));

  const std::string content = "shared content";
  VectorReader firstReader{bytesFromString(content), 4};
  extora::object_metadata firstMetadata;
  firstMetadata.content_type = "type/one";
  ASSERT_TRUE(succeeded(store->put_object(extora::bucket_name{"bucket"}, extora::object_key{"one"}, firstReader,
                                          firstMetadata, ignoredPutResult())));
  VectorReader secondReader{bytesFromString(content), 5};
  extora::object_metadata secondMetadata;
  secondMetadata.content_type = "type/two";
  ASSERT_TRUE(succeeded(store->put_object(extora::bucket_name{"bucket"}, extora::object_key{"two"}, secondReader,
                                          secondMetadata, ignoredPutResult())));

  extora::copy_object_result copyResult;
  ASSERT_TRUE(succeeded(store->copy_object(extora::bucket_name{"bucket"}, extora::object_key{"one"},
                                           extora::bucket_name{"bucket"}, extora::object_key{"copy"}, copyResult)));

  VectorReader overwriteReader{bytesFromString(content), 6};
  ASSERT_TRUE(succeeded(store->put_object(extora::bucket_name{"bucket"}, extora::object_key{"one"}, overwriteReader,
                                          firstMetadata, ignoredPutResult())));

  const std::string databasePath = joinPath(root, "index.sqlite3");
  EXPECT_EQ(scalarQuery(databasePath, "SELECT COUNT(*) FROM physical_extents WHERE state = 2"), 1);
  EXPECT_EQ(scalarQuery(databasePath, "SELECT COUNT(DISTINCT payload_id) FROM objects WHERE state = 1"), 1);
  EXPECT_EQ(scalarQuery(databasePath, "SELECT COUNT(*) FROM payload_extents"), 1);
}

TEST(StorageDeduplicationTest, DisabledModeCreatesIndependentBlob)
{
  const std::string root = makeTempRoot();
  extora::object_store_options options;
  options.root_directory = root;
  options.dedup_min_object_size = 1;
  extora::storage_error error;
  std::unique_ptr<extora::object_store> store = extora::open_object_store(options, error);
  ASSERT_TRUE(succeeded(error));
  ASSERT_TRUE(succeeded(store->create_bucket(extora::bucket_name{"bucket"})));

  extora::put_object_options putOptions;
  putOptions.dedup = extora::dedup_mode::disabled;
  ASSERT_TRUE(succeeded(putText(*store, "bucket", "one", "same", putOptions)));
  ASSERT_TRUE(succeeded(putText(*store, "bucket", "two", "same", putOptions)));
  EXPECT_EQ(scalarQuery(joinPath(root, "index.sqlite3"), "SELECT COUNT(*) FROM physical_extents WHERE state = 2"), 2);
}

TEST(StorageDeduplicationTest, MultipartEnabledModeReusesOrdinaryPayload)
{
  const std::string root = makeTempRoot();
  extora::object_store_options options;
  options.root_directory = root;
  options.dedup_min_object_size = 1;
  extora::storage_error error;
  std::unique_ptr<extora::object_store> store = extora::open_object_store(options, error);
  ASSERT_TRUE(succeeded(error));
  ASSERT_TRUE(succeeded(store->create_bucket(extora::bucket_name{"bucket"})));

  const std::string content = "same multipart body";
  ASSERT_TRUE(succeeded(putText(*store, "bucket", "ordinary", content)));

  extora::create_multipart_upload_options createOptions;
  createOptions.dedup = extora::dedup_mode::enabled;
  extora::create_multipart_upload_result upload;
  ASSERT_TRUE(succeeded(store->create_multipart_upload(extora::bucket_name{"bucket"}, extora::object_key{"multipart"},
                                                       extora::object_metadata{}, upload, createOptions)));

  VectorReader reader{bytesFromString(content), 5};
  extora::upload_part_result part;
  ASSERT_TRUE(succeeded(store->upload_part(extora::bucket_name{"bucket"}, extora::object_key{"multipart"},
                                           upload.upload_id, 1, reader, part)));

  extora::complete_multipart_upload_options completeOptions;
  completeOptions.parts.push_back(extora::completed_multipart_part{1, part.etag, std::nullopt});
  ASSERT_TRUE(succeeded(store->complete_multipart_upload(extora::bucket_name{"bucket"}, extora::object_key{"multipart"},
                                                         upload.upload_id, completeOptions, ignoredPutResult())));

  const std::string databasePath = joinPath(root, "index.sqlite3");
  EXPECT_EQ(scalarQuery(databasePath, "SELECT COUNT(*) FROM physical_extents WHERE state = 2"), 1);
  EXPECT_EQ(scalarQuery(databasePath, "SELECT COUNT(*) FROM object_payloads"), 1);
  EXPECT_EQ(scalarQuery(databasePath, "SELECT COUNT(DISTINCT payload_id) FROM objects WHERE state = 1"), 1);

  VectorWriter writer;
  ASSERT_TRUE(succeeded(readObject(*store, extora::bucket_name{"bucket"}, extora::object_key{"multipart"}, writer,
                                   extora::open_object_options{}, ignoredOpenObjectResult())));
  EXPECT_EQ(stringFromBytes(writer.bytes()), content);
}

} // namespace

} // namespace extoraTest
