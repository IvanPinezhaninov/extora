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

#ifndef EXTORA_CORE_STORAGE_TYPES_H
#define EXTORA_CORE_STORAGE_TYPES_H

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <extora/multipart_types.h>

namespace extora::core {

enum class extent_allocation_mode : std::uint8_t {
  reuse,
  contiguous,
};

// One segment range. reserved_length is the allocated size before publication.
struct physical_extent {
  std::uint64_t segment_id = 0;
  std::uint64_t offset = 0;
  std::uint64_t length = 0;
  std::uint64_t reserved_length = 0;
};

// Free physical ranges claimed by one reclamation pass. Claimed ranges cannot
// be reserved again until the index finalizes the plan.
struct storage_reclamation_plan {
  /** @brief Ranges claimed by the index for this reclamation pass. */
  std::vector<physical_extent> extents;
  /** @brief Segments that contained no ranges outside extents when claimed. */
  std::vector<std::uint64_t> fully_free_segment_ids;
  /** @brief Fully free segments confirmed physically absent by the data store. */
  std::vector<std::uint64_t> absent_segment_ids;
};

enum class compaction_extent_kind : std::uint8_t {
  movable,
  reusable,
  fixed,
};

// One indexed physical range considered by storage compaction.
struct compaction_extent {
  physical_extent extent;
  compaction_extent_kind kind = compaction_extent_kind::fixed;
};

// Stable indexed layout snapshot used while planning one compaction pass.
struct storage_compaction_layout {
  std::vector<compaction_extent> extents;
};

// Immutable body shared by object generations.
struct object_payload {
  std::uint64_t id = 0;
  std::vector<physical_extent> extents;
  object_checksum internal_checksum;
  checksum_algorithm_name dedup_algorithm;
  std::string dedup_value;
};

// Metadata persisted for a committed object generation.
struct stored_object_metadata {
  std::optional<std::string> content_type;
  std::optional<std::uint64_t> content_length;
  std::optional<std::string> cache_control;
  std::optional<std::string> content_disposition;
  std::optional<std::string> content_encoding;
  std::optional<std::string> content_language;
  std::optional<std::chrono::system_clock::time_point> expires_at;
  std::optional<object_checksum> checksum;
  std::vector<metadata_entry> custom_metadata;
};

// Committed object generation.
struct indexed_object {
  bucket_name bucket;
  object_key key;
  object_payload payload;
  stored_object_metadata metadata;
  std::vector<object_part_info> parts;
  std::string etag;
  object_version_id version_id;
  bool is_delete_marker = false;
  bool is_latest = false;
  bool is_corrupted = false;
  std::chrono::system_clock::time_point created_at;
  std::chrono::system_clock::time_point modified_at;
  std::uint64_t generation = 0;
};

struct indexed_object_page {
  std::vector<indexed_object> objects;
  bool is_truncated = false;
  std::optional<std::string> next_continuation_token;
};

struct indexed_object_version_page {
  std::vector<indexed_object> objects;
  bool is_truncated = false;
  std::optional<std::string> next_key_marker;
  std::optional<object_version_id> next_version_id_marker;
};

struct indexed_object_part_page {
  std::vector<object_part_info> parts;
  std::uint32_t total_parts = 0;
  std::optional<std::uint32_t> next_part_number_marker;
  bool is_truncated = false;
};

struct indexed_multipart_part {
  multipart_part_info info;
  std::vector<physical_extent> extents;
  object_checksum internal_checksum;
};

struct indexed_multipart_upload {
  multipart_upload_id upload_id;
  bucket_name bucket;
  object_key key;
  object_metadata metadata;
  checksum_algorithm_name checksum_algorithm;
  object_checksum_type checksum_type = object_checksum_type::full_object;
  dedup_mode dedup = dedup_mode::enabled;
  std::chrono::system_clock::time_point initiated_at;
  std::vector<indexed_multipart_part> parts;
};

struct data_write_handle {
  std::uint64_t value = 0;
};

struct data_read_handle {
  std::uint64_t value = 0;
};

} // namespace extora::core

#endif // EXTORA_CORE_STORAGE_TYPES_H
