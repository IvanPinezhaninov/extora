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

#ifndef EXTORA_BUCKET_TYPES_H
#define EXTORA_BUCKET_TYPES_H

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace extora {

/** @brief Maximum number of buckets returned in one list page. */
inline constexpr std::size_t max_bucket_list_page_size = 10000;

/** @brief Maximum bucket name size in bytes. */
inline constexpr std::size_t max_bucket_name_size = 255;

/**
 * @brief Bucket name.
 *
 * Extora accepts 1 to @c max_bucket_name_size bytes without NUL and does not
 * normalize the value. Frontends may use stricter rules.
 */
struct bucket_name {
  /** @brief Name bytes. */
  std::string value;
};

/** @brief Current bucket versioning state. */
enum class bucket_versioning_status : std::uint8_t {
  unversioned, ///< Versioning was never enabled.
  enabled,     ///< Writes create retained versions.
  suspended    ///< Old versions remain; new writes replace the null version.
};

/** @brief Bucket versioning setting. */
enum class bucket_versioning_configuration : std::uint8_t {
  enabled,  ///< Enable versioning.
  suspended ///< Suspend versioning.
};

/** @brief Bucket metadata. */
struct bucket_info {
  /** @brief @ref bucket_name value. */
  bucket_name name;

  /** @brief Creation time. */
  std::chrono::system_clock::time_point created_at;

  /** @brief @ref bucket_versioning_status value. */
  bucket_versioning_status versioning = bucket_versioning_status::unversioned;
};

/**
 * @brief Logical storage usage of one bucket.
 *
 * Object bytes are counted once per retained object version, even when the
 * physical payload is deduplicated. Delete markers do not contribute to the
 * byte or object counts. Multipart fields describe incomplete uploads.
 */
struct bucket_usage {
  /** @brief Total bytes in current object versions. */
  std::uint64_t current_object_bytes = 0;

  /** @brief Total bytes in retained noncurrent object versions. */
  std::uint64_t noncurrent_version_bytes = 0;

  /** @brief Total bytes in parts of incomplete multipart uploads. */
  std::uint64_t multipart_bytes = 0;

  /** @brief Number of current object versions, excluding delete markers. */
  std::uint64_t current_object_count = 0;

  /** @brief Number of retained noncurrent versions, excluding delete markers. */
  std::uint64_t noncurrent_version_count = 0;

  /** @brief Number of parts in incomplete multipart uploads. */
  std::uint64_t multipart_part_count = 0;
};

/** @brief Options used by @ref object_store::list_buckets calls. */
struct list_buckets_options {
  /** @brief Required bucket-name prefix. */
  std::string prefix;

  /** @brief Opaque token from the previous page. */
  std::string continuation_token;

  /** @brief Maximum bucket count; values above 10000 are capped. */
  std::size_t max_buckets = max_bucket_list_page_size;
};

/** @brief Bucket list page. */
struct bucket_list {
  /** @brief @ref bucket_info values in lexicographic name order. */
  std::vector<bucket_info> buckets;

  /** @brief Opaque token present when another page exists. */
  std::optional<std::string> next_continuation_token;
};

} // namespace extora

#endif // EXTORA_BUCKET_TYPES_H
