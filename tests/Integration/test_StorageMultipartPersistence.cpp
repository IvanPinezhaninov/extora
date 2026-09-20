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

#include <gtest/gtest.h>

#include "PublicTestSupport.h"
#include "SqliteTestSupport.h"

namespace extoraTest {

TEST(StorageMultipartPersistenceTest, CompletesUploadAfterReopen)
{
  const std::string root = makeTempRoot();
  extora::object_store_options storeOptions;
  storeOptions.root_directory = root;
  extora::multipart_upload_id uploadId;
  extora::upload_part_result partResult;

  {
    extora::storage_error error;
    std::unique_ptr<extora::object_store> store = extora::open_object_store(storeOptions, error);
    ASSERT_TRUE(succeeded(error));
    ASSERT_TRUE(store);
    ASSERT_TRUE(succeeded(store->create_bucket(extora::bucket_name{"photos"})));

    extora::object_metadata metadata;
    metadata.content_type = "text/plain";
    metadata.cache_control = "max-age=120";
    metadata.expires_at = std::chrono::system_clock::time_point{std::chrono::seconds{1893456000}};
    metadata.custom_metadata.push_back(extora::metadata_entry{"origin", "reopen-test"});
    extora::create_multipart_upload_options createOptions;
    createOptions.checksum_type = extora::object_checksum_type::composite;
    extora::create_multipart_upload_result createResult;
    ASSERT_TRUE(succeeded(store->create_multipart_upload(
        extora::bucket_name{"photos"}, extora::object_key{"persistent"}, metadata, createResult, createOptions)));
    uploadId = createResult.upload_id;

    VectorReader reader{bytesFromString("persistent-body"), 3};
    ASSERT_TRUE(succeeded(store->upload_part(extora::bucket_name{"photos"}, extora::object_key{"persistent"}, uploadId,
                                             1, reader, partResult)));
  }

  EXPECT_EQ(countPhysicalExtentsInState(joinPath(root, "index.sqlite3"), sqliteExtentStateReserved), 1);

  extora::storage_error reopenError;
  std::unique_ptr<extora::object_store> reopened = extora::open_object_store(storeOptions, reopenError);
  ASSERT_TRUE(succeeded(reopenError)) << reopenError.message;
  ASSERT_TRUE(reopened);

  extora::multipart_part_list parts;
  ASSERT_TRUE(succeeded(
      reopened->list_parts(extora::bucket_name{"photos"}, extora::object_key{"persistent"}, uploadId, parts)));
  ASSERT_EQ(parts.parts.size(), 1u);
  EXPECT_EQ(parts.parts[0].etag, partResult.etag);
  ASSERT_TRUE(parts.parts[0].checksum.has_value());
  EXPECT_EQ(parts.parts[0].checksum->checksum_algorithm.value, extora::xxh3_128_checksum_algorithm.value);

  extora::complete_multipart_upload_options completeOptions;
  completeOptions.parts.push_back(extora::completed_multipart_part{1, partResult.etag, std::nullopt});
  extora::put_object_result completeResult;
  ASSERT_TRUE(succeeded(reopened->complete_multipart_upload(
      extora::bucket_name{"photos"}, extora::object_key{"persistent"}, uploadId, completeOptions, completeResult)));
  EXPECT_EQ(completeResult.checksum.type, extora::object_checksum_type::composite);
  EXPECT_NE(completeResult.checksum.value.find("-1"), std::string::npos);

  reopened.reset();
  reopened = extora::open_object_store(storeOptions, reopenError);
  ASSERT_TRUE(succeeded(reopenError)) << reopenError.message;
  ASSERT_TRUE(reopened);

  extora::object_part_list completedParts;
  ASSERT_TRUE(succeeded(
      reopened->list_object_parts(extora::bucket_name{"photos"}, extora::object_key{"persistent"}, completedParts)));
  EXPECT_EQ(completedParts.object.version_id.value, completeResult.version_id.value);
  EXPECT_EQ(completedParts.total_parts, 1u);
  ASSERT_EQ(completedParts.parts.size(), 1u);
  EXPECT_EQ(completedParts.parts[0].part_number, 1u);
  EXPECT_EQ(completedParts.parts[0].offset, 0u);
  EXPECT_EQ(completedParts.parts[0].content_length, 15u);
  EXPECT_EQ(completedParts.parts[0].checksum.value, partResult.checksum.value);

  VectorWriter writer;
  extora::open_object_result getResult;
  ASSERT_TRUE(succeeded(readObject(*reopened, extora::bucket_name{"photos"}, extora::object_key{"persistent"}, writer,
                                   extora::open_object_options{}, getResult)));
  EXPECT_EQ(stringFromBytes(writer.bytes()), "persistent-body");
  ASSERT_TRUE(getResult.object.content_type.has_value());
  EXPECT_EQ(*getResult.object.content_type, "text/plain");
  ASSERT_TRUE(getResult.object.cache_control.has_value());
  EXPECT_EQ(*getResult.object.cache_control, "max-age=120");
  ASSERT_TRUE(getResult.object.expires_at.has_value());
  EXPECT_EQ(*getResult.object.expires_at, std::chrono::system_clock::time_point{std::chrono::seconds{1893456000}});
  ASSERT_EQ(getResult.object.custom_metadata.size(), 1u);
  EXPECT_EQ(getResult.object.custom_metadata[0].value, "reopen-test");
}

} // namespace extoraTest
