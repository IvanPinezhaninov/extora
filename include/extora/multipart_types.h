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

#ifndef EXTORA_MULTIPART_TYPES_H
#define EXTORA_MULTIPART_TYPES_H

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <extora/object_types.h>

namespace extora {

/** @brief Incomplete multipart upload ID. */
struct multipart_upload_id {
  /** @brief Opaque ID value. */
  std::string value;
};

/**
 * @brief Options used by @ref object_store::create_multipart_upload calls.
 *
 * The checksum fields define one upload-wide contract. Every part checksum
 * uses @ref checksum_algorithm, while @ref checksum_type selects how the final
 * object checksum is produced.
 */
struct create_multipart_upload_options {
  /** @brief Algorithm used for every public part and object checksum. */
  checksum_algorithm_name checksum_algorithm{xxh3_128_checksum_algorithm};

  /** @brief Final @ref object_checksum_type value. */
  object_checksum_type checksum_type = object_checksum_type::full_object;

  /** @brief @ref dedup_mode value for the final object. */
  dedup_mode dedup = dedup_mode::enabled;
};

/** @brief Result returned by @ref object_store::create_multipart_upload calls. */
struct create_multipart_upload_result {
  /** @brief New @ref multipart_upload_id value. */
  multipart_upload_id upload_id;

  /** @brief Persisted algorithm used for public checksums. */
  checksum_algorithm_name checksum_algorithm;

  /** @brief Persisted final @ref object_checksum_type value. */
  object_checksum_type checksum_type = object_checksum_type::full_object;
};

/** @brief Options used by @ref object_store::upload_part calls. */
struct upload_part_options {
  /** @brief Expected part byte count; a mismatch rejects the part. */
  std::optional<std::uint64_t> expected_content_length;

  /** @brief Expected value under the upload-wide checksum algorithm. */
  std::optional<std::string> expected_checksum;
};

/** @brief Result returned by @ref object_store::upload_part calls. */
struct upload_part_result {
  /** @brief ETag used to select this part during completion. */
  std::string etag;

  /** @brief Stored part size in bytes. */
  std::uint64_t content_length = 0;

  /** @brief Calculated full-part @ref object_checksum. */
  object_checksum checksum;

  /** @brief Part creation time. */
  std::chrono::system_clock::time_point created_at;
};

/** @brief Part selected for multipart completion. */
struct completed_multipart_part {
  /** @brief Uploaded part number. */
  std::uint32_t part_number = 0;

  /** @brief ETag returned by @ref object_store::upload_part. */
  std::string etag;

  /** @brief Optional checksum value returned by @ref object_store::upload_part. */
  std::optional<std::string> expected_checksum;
};

/** @brief Options used by @ref object_store::complete_multipart_upload calls. */
struct complete_multipart_upload_options {
  /** @brief Ordered @ref completed_multipart_part values to publish. */
  std::vector<completed_multipart_part> parts;

  /** @brief Target @ref object_conditions value. */
  object_conditions conditions;

  /** @brief Expected value under the upload-wide checksum contract. */
  std::optional<std::string> expected_checksum;
};

/** @brief Uploaded part metadata. */
struct multipart_part_info {
  /** @brief Uploaded part number. */
  std::uint32_t part_number = 0;

  /** @brief Part ETag. */
  std::string etag;

  /** @brief Stored part size in bytes. */
  std::uint64_t content_length = 0;

  /** @brief Calculated full-part @ref object_checksum, when available. */
  std::optional<object_checksum> checksum;

  /** @brief Part creation time. */
  std::chrono::system_clock::time_point created_at;
};

/** @brief Options used by @ref object_store::list_parts calls. */
struct list_parts_options {
  /** @brief Return part numbers greater than this marker. */
  std::uint32_t part_number_marker = 0;

  /** @brief Maximum returned part count; values above 1000 are capped. */
  std::size_t max_parts = max_list_page_size;
};

/** @brief Page of uploaded parts. */
struct multipart_part_list {
  /** @brief @ref multipart_part_info values in ascending number order. */
  std::vector<multipart_part_info> parts;

  /** @brief Marker present when another page exists. */
  std::optional<std::uint32_t> next_part_number_marker;

  /** @brief Whether another page exists. */
  bool is_truncated = false;
};

/** @brief Part of a completed multipart object. */
struct object_part_info {
  /** @brief Original multipart part number. */
  std::uint32_t part_number = 0;

  /** @brief Byte offset within the completed object. */
  std::uint64_t offset = 0;

  /** @brief Part size in bytes. */
  std::uint64_t content_length = 0;

  /** @brief Full-part @ref object_checksum. */
  object_checksum checksum;
};

/** @brief Options used by @ref object_store::list_object_parts calls. */
struct list_object_parts_options {
  /** @brief Optional @ref object_version_id; empty selects the latest version. */
  std::optional<object_version_id> version_id;

  /** @brief Lookup @ref object_conditions value. */
  object_conditions conditions;

  /** @brief Return part numbers greater than this marker. */
  std::uint32_t part_number_marker = 0;

  /** @brief Maximum returned part count; values above 1000 are capped. */
  std::size_t max_parts = max_list_page_size;
};

/** @brief Page of parts in a completed multipart object. */
struct object_part_list {
  /** @brief Resolved completed @ref object_info value. */
  object_info object;

  /** @brief Selected @ref object_part_info values. */
  std::vector<object_part_info> parts;

  /** @brief Total number of parts in the object. */
  std::uint32_t total_parts = 0;

  /** @brief Marker present when another page exists. */
  std::optional<std::uint32_t> next_part_number_marker;

  /** @brief Whether another page exists. */
  bool is_truncated = false;
};

/** @brief Incomplete multipart upload metadata. */
struct multipart_upload_info {
  /** @brief Target @ref object_key value. */
  object_key key;

  /** @brief @ref multipart_upload_id value. */
  multipart_upload_id upload_id;

  /** @brief Algorithm used for every public part and object checksum. */
  checksum_algorithm_name checksum_algorithm;

  /** @brief Final @ref object_checksum_type value. */
  object_checksum_type checksum_type = object_checksum_type::full_object;

  /** @brief Upload creation time. */
  std::chrono::system_clock::time_point initiated_at;
};

/** @brief Options used by @ref object_store::list_multipart_uploads calls. */
struct list_multipart_uploads_options {
  /** @brief Required object-key prefix. */
  std::string prefix;

  /** @brief Prefix grouping delimiter. */
  std::string delimiter;

  /**
   * @brief Key component of the exclusive pagination cursor.
   *
   * Without @ref upload_id_marker, all uploads for this key are skipped.
   */
  std::string key_marker;

  /**
   * @brief @ref multipart_upload_id component of the exclusive pagination cursor.
   *
   * When non-empty, @ref key_marker must identify the same object key.
   */
  multipart_upload_id upload_id_marker;

  /** @brief Maximum upload and prefix count; values above 1000 are capped. */
  std::size_t max_uploads = max_list_page_size;
};

/** @brief Page of incomplete multipart uploads. */
struct multipart_upload_list {
  /** @brief @ref multipart_upload_info values in page order. */
  std::vector<multipart_upload_info> uploads;

  /** @brief Grouped object-key prefixes. */
  std::vector<std::string> common_prefixes;

  /** @brief Key component of the cursor for the next page. */
  std::optional<std::string> next_key_marker;

  /** @brief @ref multipart_upload_id component paired with @ref next_key_marker. */
  std::optional<multipart_upload_id> next_upload_id_marker;

  /** @brief Whether another page exists. */
  bool is_truncated = false;
};

} // namespace extora

#endif // EXTORA_MULTIPART_TYPES_H
