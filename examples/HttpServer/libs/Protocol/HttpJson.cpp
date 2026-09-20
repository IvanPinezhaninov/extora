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

#include "HttpJson.h"

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <boost/json.hpp>

#include "extora/multipart_types.h"

namespace extoraHttpExample {

const char* bucketVersioningName(extora::bucket_versioning_status status)
{
  switch (status) {
  case extora::bucket_versioning_status::unversioned:
    return "unversioned";
  case extora::bucket_versioning_status::enabled:
    return "enabled";
  case extora::bucket_versioning_status::suspended:
    return "suspended";
  }
  return "unknown";
}

const char* checksumTypeName(extora::object_checksum_type type)
{
  switch (type) {
  case extora::object_checksum_type::full_object:
    return "full_object";
  case extora::object_checksum_type::composite:
    return "composite";
  }
  return "unknown";
}

namespace {

namespace json = boost::json;

std::int64_t unixMilliseconds(std::chrono::system_clock::time_point time)
{
  return std::chrono::duration_cast<std::chrono::milliseconds>(time.time_since_epoch()).count();
}

json::value checksumJson(const extora::object_checksum& checksum)
{
  return json::object{{"algorithm", checksum.checksum_algorithm.value},
                      {"value", checksum.value},
                      {"type", checksumTypeName(checksum.type)}};
}

json::value checksumJson(const std::optional<extora::object_checksum>& checksum)
{
  if (!checksum.has_value()) return nullptr;
  return checksumJson(*checksum);
}

json::value optionalStringJson(const std::optional<std::string>& value)
{
  if (!value.has_value()) return nullptr;
  return json::value(*value);
}

json::object objectJson(const extora::object_info& object)
{
  json::object result{
      {"key", object.key.value},
      {"version_id", object.version_id.value},
      {"is_delete_marker", object.is_delete_marker},
      {"is_latest", object.is_latest},
      {"etag", object.etag},
      {"content_length", object.content_length},
      {"content_type", optionalStringJson(object.content_type)},
      {"checksum", checksumJson(object.checksum)},
      {"created_at_ms", unixMilliseconds(object.created_at)},
      {"modified_at_ms", unixMilliseconds(object.modified_at)},
  };
  return result;
}

json::array prefixesJson(const std::vector<std::string>& prefixes)
{
  json::array result;
  result.reserve(prefixes.size());
  for (const std::string& prefix : prefixes)
    result.emplace_back(prefix);
  return result;
}

json::object reclamationEstimateValue(const extora::reclamation_estimate& estimate)
{
  return {{"reclaimable_bytes", estimate.reclaimable_bytes},
          {"reclaimable_extent_count", estimate.reclaimable_extent_count}};
}

std::string serializeJson(const json::value& value)
{
  std::string result = json::serialize(value);
  result.push_back('\n');
  return result;
}

} // namespace

std::string bucketsJson(const extora::bucket_list& buckets)
{
  json::array values;
  values.reserve(buckets.buckets.size());
  for (const extora::bucket_info& bucket : buckets.buckets) {
    values.emplace_back(json::object{{"name", bucket.name.value},
                                     {"created_at_ms", unixMilliseconds(bucket.created_at)},
                                     {"versioning", bucketVersioningName(bucket.versioning)}});
  }
  json::object result{{"buckets", std::move(values)}};
  if (buckets.next_continuation_token.has_value())
    result.emplace("next_continuation_token", *buckets.next_continuation_token);
  return serializeJson(result);
}

std::string bucketUsageJson(const extora::bucket_usage& usage)
{
  return serializeJson(json::object{{"current_object_bytes", usage.current_object_bytes},
                                    {"noncurrent_version_bytes", usage.noncurrent_version_bytes},
                                    {"multipart_bytes", usage.multipart_bytes},
                                    {"current_object_count", usage.current_object_count},
                                    {"noncurrent_version_count", usage.noncurrent_version_count},
                                    {"multipart_part_count", usage.multipart_part_count}});
}

std::string errorJson(std::string_view code, std::string_view message)
{
  return serializeJson(json::object{{"error", json::object{{"code", code}, {"message", message}}}});
}

std::string bucketVersioningJson(extora::bucket_versioning_status status)
{
  return serializeJson(json::object{{"status", bucketVersioningName(status)}});
}

std::string objectsJson(const extora::object_list& objects)
{
  json::array values;
  values.reserve(objects.objects.size());
  for (const extora::object_info& object : objects.objects)
    values.emplace_back(objectJson(object));

  json::object result{{"objects", std::move(values)}, {"common_prefixes", prefixesJson(objects.common_prefixes)}};
  if (objects.next_continuation_token.has_value())
    result.emplace("next_continuation_token", *objects.next_continuation_token);
  result.emplace("is_truncated", objects.is_truncated);
  return serializeJson(result);
}

std::string multipartUploadsJson(const extora::multipart_upload_list& uploads)
{
  json::array values;
  values.reserve(uploads.uploads.size());
  for (const extora::multipart_upload_info& upload : uploads.uploads) {
    values.emplace_back(json::object{{"key", upload.key.value},
                                     {"upload_id", upload.upload_id.value},
                                     {"initiated_at_ms", unixMilliseconds(upload.initiated_at)}});
  }
  json::object result{{"uploads", std::move(values)}, {"common_prefixes", prefixesJson(uploads.common_prefixes)}};
  if (uploads.next_key_marker.has_value()) result.emplace("next_key_marker", *uploads.next_key_marker);
  if (uploads.next_upload_id_marker.has_value())
    result.emplace("next_upload_id_marker", uploads.next_upload_id_marker->value);
  result.emplace("is_truncated", uploads.is_truncated);
  return serializeJson(result);
}

std::string multipartPartsJson(const extora::multipart_part_list& parts)
{
  json::array values;
  values.reserve(parts.parts.size());
  for (const extora::multipart_part_info& part : parts.parts) {
    values.emplace_back(json::object{{"part_number", part.part_number},
                                     {"etag", part.etag},
                                     {"content_length", part.content_length},
                                     {"checksum", checksumJson(part.checksum)},
                                     {"created_at_ms", unixMilliseconds(part.created_at)}});
  }
  json::object result{{"parts", std::move(values)}};
  if (parts.next_part_number_marker.has_value())
    result.emplace("next_part_number_marker", *parts.next_part_number_marker);
  result.emplace("is_truncated", parts.is_truncated);
  return serializeJson(result);
}

std::string objectPartsJson(const extora::object_part_list& parts)
{
  json::array values;
  values.reserve(parts.parts.size());
  for (const extora::object_part_info& part : parts.parts) {
    values.emplace_back(json::object{{"part_number", part.part_number},
                                     {"offset", part.offset},
                                     {"content_length", part.content_length},
                                     {"checksum", checksumJson(part.checksum)}});
  }
  json::object result{
      {"object", objectJson(parts.object)}, {"parts", std::move(values)}, {"total_parts", parts.total_parts}};
  if (parts.next_part_number_marker.has_value())
    result.emplace("next_part_number_marker", *parts.next_part_number_marker);
  result.emplace("is_truncated", parts.is_truncated);
  return serializeJson(result);
}

std::string multipartUploadJson(const extora::create_multipart_upload_result& upload)
{
  return serializeJson(json::object{{"upload_id", upload.upload_id.value},
                                    {"checksum_algorithm", upload.checksum_algorithm.value},
                                    {"checksum_type", checksumTypeName(upload.checksum_type)}});
}

std::string objectVersionsJson(const extora::object_version_list& versions)
{
  json::array values;
  values.reserve(versions.versions.size());
  for (const extora::object_info& version : versions.versions)
    values.emplace_back(objectJson(version));

  json::object result{{"versions", std::move(values)}, {"common_prefixes", prefixesJson(versions.common_prefixes)}};
  if (versions.next_key_marker.has_value()) result.emplace("next_key_marker", *versions.next_key_marker);
  if (versions.next_version_id_marker.has_value())
    result.emplace("next_version_id_marker", versions.next_version_id_marker->value);
  result.emplace("is_truncated", versions.is_truncated);
  return serializeJson(result);
}

std::string reclamationEstimateJson(const extora::reclamation_estimate& estimate)
{
  return serializeJson(reclamationEstimateValue(estimate));
}

std::string reclaimResultJson(const extora::reclaim_storage_result& result)
{
  return serializeJson(json::object{{"reclaimed_bytes", result.reclaimed_bytes},
                                    {"reclaimed_extent_count", result.reclaimed_extent_count},
                                    {"remaining_reclaimable_bytes", result.remaining_reclaimable_bytes},
                                    {"remaining_reclaimable_extent_count", result.remaining_reclaimable_extent_count}});
}

std::string compactionResultJson(const extora::compact_storage_result& result)
{
  return serializeJson(json::object{{"segment_count_before", result.segment_count_before},
                                    {"segment_count_after", result.segment_count_after},
                                    {"examined_payload_count", result.examined_payload_count},
                                    {"compacted_payload_count", result.compacted_payload_count},
                                    {"compacted_bytes", result.compacted_bytes},
                                    {"replaced_extent_count", result.replaced_extent_count},
                                    {"compacted_extent_count", result.compacted_extent_count},
                                    {"removed_segment_count", result.removed_segment_count},
                                    {"released_bytes", result.released_bytes}});
}

} // namespace extoraHttpExample
