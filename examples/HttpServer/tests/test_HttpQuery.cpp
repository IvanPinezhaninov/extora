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
#include <optional>
#include <string>

#include <gtest/gtest.h>

#include "HttpQuery.h"

namespace extoraHttpExample {

TEST(HttpQueryTest, ParsesBucketAndObjectListingOptions)
{
  std::string errorMessage;
  extora::list_buckets_options bucketOptions;
  ASSERT_TRUE(
      parseBucketListOptions("prefix=team%2D&continuation-token=next&max-buckets=25", bucketOptions, errorMessage));
  EXPECT_EQ(bucketOptions.prefix, "team-");
  EXPECT_EQ(bucketOptions.continuation_token, "next");
  EXPECT_EQ(bucketOptions.max_buckets, 25u);

  extora::list_objects_options objectOptions;
  ASSERT_TRUE(
      parseObjectListOptions("prefix=photos%2F&delimiter=%2F&max-keys=42&start-after=a", objectOptions, errorMessage));
  EXPECT_EQ(objectOptions.prefix, "photos/");
  EXPECT_EQ(objectOptions.delimiter, "/");
  EXPECT_EQ(objectOptions.max_keys, 42u);
  EXPECT_EQ(objectOptions.start_after, "a");
}

TEST(HttpQueryTest, RejectsMalformedAndUnsupportedListingParameters)
{
  std::string errorMessage;
  extora::list_objects_options options;
  EXPECT_FALSE(parseObjectListOptions("max-keys=invalid", options, errorMessage));
  EXPECT_FALSE(parseObjectListOptions("prefix=a&prefix=b", options, errorMessage));
  EXPECT_FALSE(parseObjectListOptions("unknown=value", options, errorMessage));
  EXPECT_FALSE(parseObjectListOptions("prefix", options, errorMessage));
}

TEST(HttpQueryTest, RequiresKeyMarkerForVersionMarker)
{
  std::string errorMessage;
  extora::list_object_versions_options options;
  EXPECT_FALSE(parseVersionListOptions("version-id-marker=v2", options, errorMessage));
  ASSERT_TRUE(parseVersionListOptions("key-marker=photo.jpg&version-id-marker=v2&max-keys=7", options, errorMessage));
  ASSERT_TRUE(options.version_id_marker.has_value());
  EXPECT_EQ(options.key_marker, "photo.jpg");
  EXPECT_EQ(options.version_id_marker->value, "v2");
  EXPECT_EQ(options.max_keys, 7u);
}

TEST(HttpQueryTest, ParsesMultipartListingOptions)
{
  std::string errorMessage;
  extora::list_multipart_uploads_options uploadOptions;
  ASSERT_TRUE(parseMultipartUploadListOptions(
      "prefix=raw%2F&delimiter=%2F&key-marker=key&upload-id-marker=id&max-uploads=9", uploadOptions, errorMessage));
  EXPECT_EQ(uploadOptions.prefix, "raw/");
  EXPECT_EQ(uploadOptions.delimiter, "/");
  EXPECT_EQ(uploadOptions.key_marker, "key");
  EXPECT_EQ(uploadOptions.upload_id_marker.value, "id");
  EXPECT_EQ(uploadOptions.max_uploads, 9u);

  extora::multipart_upload_id uploadId;
  extora::list_parts_options partOptions;
  ASSERT_TRUE(
      parsePartListOptions("upload-id=id&part-number-marker=4&max-parts=6", uploadId, partOptions, errorMessage));
  EXPECT_EQ(uploadId.value, "id");
  EXPECT_EQ(partOptions.part_number_marker, 4u);
  EXPECT_EQ(partOptions.max_parts, 6u);
  EXPECT_FALSE(parsePartListOptions("max-parts=6", uploadId, partOptions, errorMessage));
}

TEST(HttpQueryTest, ValidatesUploadPartSelectors)
{
  std::string errorMessage;
  extora::multipart_upload_id uploadId;
  std::uint32_t partNumber = 0;
  ASSERT_TRUE(parseUploadPartOptions("upload-id=upload%2D1&part-number=3", uploadId, partNumber, errorMessage));
  EXPECT_EQ(uploadId.value, "upload-1");
  EXPECT_EQ(partNumber, 3u);
  EXPECT_FALSE(parseUploadPartOptions("upload-id=upload-1&part-number=0", uploadId, partNumber, errorMessage));
  EXPECT_FALSE(parseUploadPartOptions("part-number=1", uploadId, partNumber, errorMessage));
}

TEST(HttpQueryTest, ParsesOnlyAnOptionalObjectVersion)
{
  std::string errorMessage;
  std::optional<extora::object_version_id> versionId;
  ASSERT_TRUE(parseObjectVersionId("", versionId, errorMessage));
  EXPECT_FALSE(versionId.has_value());
  ASSERT_TRUE(parseObjectVersionId("version-id=v%2F1", versionId, errorMessage));
  ASSERT_TRUE(versionId.has_value());
  EXPECT_EQ(versionId->value, "v/1");
  EXPECT_FALSE(parseObjectVersionId("version-id=v1&other=x", versionId, errorMessage));
  EXPECT_FALSE(parseObjectVersionId("upload-id=v1", versionId, errorMessage));
}

} // namespace extoraHttpExample
