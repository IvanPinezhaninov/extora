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

#ifndef EXTORA_OBJECT_TYPES_H
#define EXTORA_OBJECT_TYPES_H

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <extora/bucket_types.h>
#include <extora/checksum.h>
#include <extora/object_stream.h>

namespace extora {

/** @brief Maximum page size for object, version, part, and multipart upload listings. */
inline constexpr std::size_t max_list_page_size = 1000;

/** @brief Maximum object key size in bytes. */
inline constexpr std::size_t max_object_key_size = 1024;

/**
 * @brief Object key.
 *
 * Extora accepts 1 to @c max_object_key_size bytes without NUL and does
 * not normalize the value. Separators are not filesystem paths.
 */
struct object_key {
  /** @brief Key bytes. */
  std::string value;
};

/** @brief Version ID used by unversioned and suspended buckets. */
inline constexpr char null_version_id[] = "null";

/** @brief ETag wildcard that matches any object. */
inline constexpr char etag_wildcard[] = "*";

/** @brief Object version ID. */
struct object_version_id {
  /**
   * @brief Version ID value.
   *
   * @c null_version_id identifies the null version. Other values are opaque
   * IDs.
   */
  std::string value;
};

/** @brief Custom metadata entry. */
struct metadata_entry {
  /** @brief Metadata name. */
  std::string name;

  /** @brief Metadata value. */
  std::string value;
};

/** @brief Descriptive metadata for a new object; payload expectations belong in operation options. */
struct object_metadata {
  /** @brief Media type. */
  std::optional<std::string> content_type;

  /** @brief Cache-Control response value. */
  std::optional<std::string> cache_control;

  /** @brief Content-Disposition response value. */
  std::optional<std::string> content_disposition;

  /** @brief Content-Encoding response value. */
  std::optional<std::string> content_encoding;

  /** @brief Content-Language response value. */
  std::optional<std::string> content_language;

  /** @brief Expires response time. */
  std::optional<std::chrono::system_clock::time_point> expires_at;

  /** @brief Custom @ref metadata_entry values. */
  std::vector<metadata_entry> custom_metadata;
};

/** @brief Stored object metadata. */
struct object_info {
  /** @brief @ref object_key in the bucket. */
  object_key key;

  /** @brief @ref object_version_id value. */
  object_version_id version_id;

  /** @brief Whether this is a delete marker. */
  bool is_delete_marker = false;

  /** @brief Whether this is the latest version. */
  bool is_latest = false;

  /** @brief Object ETag. */
  std::string etag;

  /** @brief Object size in bytes. */
  std::uint64_t content_length = 0;

  /** @brief Media type. */
  std::optional<std::string> content_type;

  /** @brief Cache-Control response value. */
  std::optional<std::string> cache_control;

  /** @brief Content-Disposition response value. */
  std::optional<std::string> content_disposition;

  /** @brief Content-Encoding response value. */
  std::optional<std::string> content_encoding;

  /** @brief Content-Language response value. */
  std::optional<std::string> content_language;

  /** @brief Expires response time. */
  std::optional<std::chrono::system_clock::time_point> expires_at;

  /** @brief Public @ref object_checksum value. */
  std::optional<object_checksum> checksum;

  /** @brief Creation time. */
  std::chrono::system_clock::time_point created_at;

  /** @brief Last modification time. */
  std::chrono::system_clock::time_point modified_at;

  /** @brief Custom @ref metadata_entry values. */
  std::vector<metadata_entry> custom_metadata;
};

/** @brief Object preconditions. */
struct object_conditions {
  /** @brief Required matching ETag. */
  std::optional<std::string> if_match_etag;

  /** @brief Required non-match; "*" matches any object. */
  std::optional<std::string> if_none_match_etag;

  /** @brief Required newer modification time. */
  std::optional<std::chrono::system_clock::time_point> if_modified_since;

  /** @brief Required modification time that is not newer. */
  std::optional<std::chrono::system_clock::time_point> if_unmodified_since;
};

/** @brief Byte range form. */
enum class byte_range_type : std::uint8_t {
  offset_length, ///< Read length bytes from offset.
  offset_to_end, ///< Read from offset to the end.
  suffix         ///< Read the last length bytes.
};

/** @brief Byte range request. */
struct byte_range {
  /** @brief @ref byte_range_type value. */
  byte_range_type type = byte_range_type::offset_length;

  /** @brief First byte for offset ranges. */
  std::uint64_t offset = 0;

  /** @brief Byte count. */
  std::uint64_t length = 0;
};

/** @brief Resolved byte range. */
struct resolved_byte_range {
  /** @brief First returned byte. */
  std::uint64_t offset = 0;

  /** @brief Returned byte count. */
  std::uint64_t length = 0;
};

/** @brief Options used by @ref object_store::open_object calls. */
struct open_object_options {
  /** @brief Optional @ref object_version_id value; empty selects the latest. */
  std::optional<object_version_id> version_id;

  /** @brief @ref object_conditions for the read. */
  object_conditions conditions;

  /** @brief Optional @ref byte_range value; empty reads the full object. */
  std::optional<byte_range> range;

  /**
   * @brief Verify the stored integrity checksum while streaming a full-object read.
   *
   * Verification completes only when the reader reaches the end of the
   * object. On mismatch, the final read may return the last bytes together
   * with @ref storage_error_code::checksum_mismatch, and the store quarantines
   * every object generation that shares the damaged payload. Later accesses
   * fail with @ref storage_error_code::object_corrupted. Bytes already passed
   * to the caller cannot be revoked and must be discarded. Destroying the
   * reader earlier does not verify integrity. This option cannot be combined
   * with @ref range.
   */
  bool verify_integrity = false;
};

/**
 * @brief Open object snapshot returned by @ref object_store::open_object calls.
 *
 * A successful result always owns a non-null reader, including when the
 * selected body is empty. Once an object version is resolved, @ref object is
 * populated even if a condition, range, integrity setup, or delete-marker
 * check prevents the reader from being returned.
 */
struct open_object_result {
  /** @brief @ref object_info for the immutable snapshot. */
  object_info object;

  /** @brief @ref resolved_byte_range value, or empty for a full read. */
  std::optional<resolved_byte_range> byte_range;

  /** @brief @ref object_reader for the selected bytes. */
  std::unique_ptr<object_reader> reader;
};

/** @brief Per-write deduplication setting. */
enum class dedup_mode : std::uint8_t {
  enabled, ///< Enable deduplication.
  disabled ///< Disable deduplication.
};

/** @brief Options used by @ref object_store::put_object calls. */
struct put_object_options {
  /** @brief Target @ref object_conditions value. */
  object_conditions conditions;

  /** @brief Expected byte count; a mismatch rejects the write. */
  std::optional<std::uint64_t> expected_content_length;

  /** @brief Expected full-object @ref object_checksum; also selects its algorithm. */
  std::optional<object_checksum> expected_checksum;

  /** @brief @ref dedup_mode value. */
  dedup_mode dedup = dedup_mode::enabled;
};

/** @brief Result returned by @ref object_store::put_object calls. */
struct put_object_result {
  /** @brief Stored object ETag. */
  std::string etag;

  /** @brief Stored @ref object_version_id value. */
  object_version_id version_id;

  /** @brief Stored @ref object_checksum value; XXH3-128 by default. */
  object_checksum checksum;
};

/** @brief Options used by @ref object_store::copy_object calls. */
struct copy_object_options {
  /** @brief Optional @ref object_version_id; empty selects the latest source version. */
  std::optional<object_version_id> source_version_id;

  /** @brief Source @ref object_conditions value. */
  object_conditions source_conditions;

  /** @brief Target @ref object_conditions value. */
  object_conditions target_conditions;

  /** @brief Whether to replace source metadata. */
  bool replace_metadata = false;

  /** @brief Replacement @ref object_metadata value. */
  object_metadata metadata;

  /** @brief Target checksum algorithm; a change reads the source body. */
  checksum_algorithm_name target_checksum_algorithm;
};

/** @brief Result returned by @ref object_store::copy_object calls. */
struct copy_object_result {
  /** @brief Resolved source @ref object_version_id value. */
  object_version_id source_version_id;

  /** @brief Target ETag. */
  std::string etag;

  /** @brief Target @ref object_version_id value. */
  object_version_id version_id;

  /** @brief Target @ref object_checksum; XXH3-128 by default. */
  object_checksum checksum;

  /** @brief Target modification time. */
  std::chrono::system_clock::time_point modified_at;
};

/** @brief Options used by @ref object_store::list_objects calls. */
struct list_objects_options {
  /** @brief Required key prefix. */
  std::string prefix;

  /** @brief Prefix grouping delimiter. */
  std::string delimiter;

  /** @brief Opaque token returned by the previous page. */
  std::string continuation_token;

  /**
   * @brief Exclusive key cursor for the first page.
   *
   * Used only when @ref continuation_token is empty. Objects and grouped
   * prefixes not lexicographically greater than this value are skipped.
   */
  std::string start_after;

  /** @brief Maximum object and prefix count; values above 1000 are capped. */
  std::size_t max_keys = max_list_page_size;
};

/** @brief Page of current objects. */
struct object_list {
  /** @brief @ref object_info values. */
  std::vector<object_info> objects;

  /** @brief Grouped key prefixes. */
  std::vector<std::string> common_prefixes;

  /** @brief Opaque token present when another page exists. */
  std::optional<std::string> next_continuation_token;

  /** @brief Whether more objects exist. */
  bool is_truncated = false;
};

/** @brief Page of object versions and delete markers. */
struct object_version_list {
  /** @brief @ref object_info values for versions and delete markers. */
  std::vector<object_info> versions;

  /** @brief Grouped key prefixes. */
  std::vector<std::string> common_prefixes;

  /** @brief Key component of the cursor for the next page. */
  std::optional<std::string> next_key_marker;

  /** @brief @ref object_version_id component paired with @ref next_key_marker. */
  std::optional<object_version_id> next_version_id_marker;

  /** @brief Whether more entries exist. */
  bool is_truncated = false;
};

/** @brief Options used by @ref object_store::head_object calls. */
struct head_object_options {
  /** @brief Optional @ref object_version_id value; empty selects the latest. */
  std::optional<object_version_id> version_id;

  /** @brief Lookup @ref object_conditions value. */
  object_conditions conditions;
};

/** @brief Options used by @ref object_store::list_object_versions calls. */
struct list_object_versions_options {
  /** @brief Required key prefix. */
  std::string prefix;

  /** @brief Prefix grouping delimiter. */
  std::string delimiter;

  /**
   * @brief Key component of the exclusive pagination cursor.
   *
   * Without @ref version_id_marker, all entries for this key are skipped.
   */
  std::string key_marker;

  /**
   * @brief @ref object_version_id component of the exclusive pagination cursor.
   *
   * When set, @ref key_marker must identify the same key. Listing resumes
   * after this exact key and version pair.
   */
  std::optional<object_version_id> version_id_marker;

  /** @brief Maximum version and prefix count; values above 1000 are capped. */
  std::size_t max_keys = max_list_page_size;
};

/** @brief Options used by @ref object_store::delete_object calls. */
struct delete_object_options {
  /**
   * @brief @ref object_version_id to delete.
   *
   * Empty deletes an unversioned object or adds a delete marker to a versioned
   * bucket.
   */
  std::optional<object_version_id> version_id;
};

/** @brief Result returned by @ref object_store::delete_object calls. */
struct delete_object_result {
  /** @brief Deleted @ref object_version_id or new marker ID. */
  object_version_id version_id;

  /** @brief Whether @ref version_id is a delete marker. */
  bool is_delete_marker = false;
};

} // namespace extora

#endif // EXTORA_OBJECT_TYPES_H
