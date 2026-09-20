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

#include <boost/json.hpp>

#include <gtest/gtest.h>

#include "HttpJson.h"
#include "extora/bucket_types.h"
#include "extora/maintenance_types.h"

namespace extoraHttpExample {

TEST(HttpJsonTest, SerializesStructuredErrors)
{
  const boost::json::value json = boost::json::parse(errorJson("invalid_request", "bad \"value\""));
  const boost::json::object& root = json.as_object();
  const boost::json::object& error = root.at("error").as_object();
  EXPECT_EQ(error.at("code").as_string(), "invalid_request");
  EXPECT_EQ(error.at("message").as_string(), "bad \"value\"");
}

TEST(HttpJsonTest, SerializesBucketPagesAndContinuationTokens)
{
  extora::bucket_list buckets;
  extora::bucket_info bucket;
  bucket.name.value = "photos";
  bucket.created_at = std::chrono::system_clock::time_point{};
  bucket.versioning = extora::bucket_versioning_status::enabled;
  buckets.buckets.push_back(bucket);
  buckets.next_continuation_token = "next";

  const boost::json::value json = boost::json::parse(bucketsJson(buckets));
  const boost::json::object& root = json.as_object();
  const boost::json::object& value = root.at("buckets").as_array().at(0).as_object();
  EXPECT_EQ(value.at("name").as_string(), "photos");
  EXPECT_EQ(value.at("created_at_ms").as_int64(), 0);
  EXPECT_EQ(value.at("versioning").as_string(), "enabled");
  EXPECT_EQ(root.at("next_continuation_token").as_string(), "next");
}

TEST(HttpJsonTest, SerializesEveryBucketUsageCounter)
{
  extora::bucket_usage usage;
  usage.current_object_bytes = 1;
  usage.noncurrent_version_bytes = 2;
  usage.multipart_bytes = 3;
  usage.current_object_count = 4;
  usage.noncurrent_version_count = 5;
  usage.multipart_part_count = 6;

  const boost::json::value json = boost::json::parse(bucketUsageJson(usage));
  const boost::json::object& root = json.as_object();
  EXPECT_EQ(root.at("current_object_bytes").as_int64(), 1);
  EXPECT_EQ(root.at("noncurrent_version_bytes").as_int64(), 2);
  EXPECT_EQ(root.at("multipart_bytes").as_int64(), 3);
  EXPECT_EQ(root.at("current_object_count").as_int64(), 4);
  EXPECT_EQ(root.at("noncurrent_version_count").as_int64(), 5);
  EXPECT_EQ(root.at("multipart_part_count").as_int64(), 6);
}

TEST(HttpJsonTest, SerializesEveryCompactionCounter)
{
  extora::compact_storage_result result;
  result.segment_count_before = 1;
  result.segment_count_after = 2;
  result.examined_payload_count = 3;
  result.compacted_payload_count = 4;
  result.compacted_bytes = 5;
  result.replaced_extent_count = 6;
  result.compacted_extent_count = 7;
  result.removed_segment_count = 8;
  result.released_bytes = 9;

  const boost::json::value json = boost::json::parse(compactionResultJson(result));
  const boost::json::object& root = json.as_object();
  EXPECT_EQ(root.at("segment_count_before").as_int64(), 1);
  EXPECT_EQ(root.at("segment_count_after").as_int64(), 2);
  EXPECT_EQ(root.at("examined_payload_count").as_int64(), 3);
  EXPECT_EQ(root.at("compacted_payload_count").as_int64(), 4);
  EXPECT_EQ(root.at("compacted_bytes").as_int64(), 5);
  EXPECT_EQ(root.at("replaced_extent_count").as_int64(), 6);
  EXPECT_EQ(root.at("compacted_extent_count").as_int64(), 7);
  EXPECT_EQ(root.at("removed_segment_count").as_int64(), 8);
  EXPECT_EQ(root.at("released_bytes").as_int64(), 9);
}

TEST(HttpJsonTest, SerializesEveryReclamationCounter)
{
  extora::reclaim_storage_result result;
  result.reclaimed_bytes = 1;
  result.reclaimed_extent_count = 2;
  result.remaining_reclaimable_bytes = 3;
  result.remaining_reclaimable_extent_count = 4;

  const boost::json::value json = boost::json::parse(reclaimResultJson(result));
  const boost::json::object& root = json.as_object();
  EXPECT_EQ(root.at("reclaimed_bytes").as_int64(), 1);
  EXPECT_EQ(root.at("reclaimed_extent_count").as_int64(), 2);
  EXPECT_EQ(root.at("remaining_reclaimable_bytes").as_int64(), 3);
  EXPECT_EQ(root.at("remaining_reclaimable_extent_count").as_int64(), 4);
}

} // namespace extoraHttpExample
