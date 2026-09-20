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

#ifndef EXTORA_OBJECT_STORE_OPTIONS_H
#define EXTORA_OBJECT_STORE_OPTIONS_H

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>

namespace extora {

class hasher_factory;
class operation_observer;

/** @brief Default segment size limit: 1 GiB. */
inline constexpr std::uint64_t default_segment_capacity = 1024 * 1024 * 1024;

/** @brief Default extent size limit: 4 MiB. */
inline constexpr std::uint64_t default_max_extent_size = 4 * 1024 * 1024;

/** @brief Default object lookup cache capacity. */
inline constexpr std::size_t default_object_lookup_cache_capacity = 1024;

/** @brief Default minimum object size for deduplication: 4 KiB. */
inline constexpr std::uint64_t default_dedup_min_object_size = 4 * 1024;

/** @brief Default minimum non-final multipart part size: 5 MiB. */
inline constexpr std::uint64_t default_multipart_min_part_size = 5 * 1024 * 1024;

/** @brief Default progress event interval: 256 KiB. */
inline constexpr std::uint64_t default_operation_progress_interval_bytes = 256 * 1024;

/** @brief Durability policy. */
enum class storage_durability : std::uint8_t {
  strict,   ///< Sync data and index updates.
  balanced, ///< Sync data with lighter index synchronization.
  relaxed,  ///< Skip explicit data synchronization.
};

/** @brief Segment allocation policy. */
enum class segment_allocation_strategy : std::uint8_t {
  sparse,  ///< Do not reserve disk blocks.
  reserve, ///< Reserve disk blocks at creation.
};

/** @brief Object store settings. */
struct object_store_options {
  /**
   * @brief Storage root.
   *
   * It must not be empty or used by another store, including one in another
   * process.
   */
  std::filesystem::path root_directory;

  /** @brief Maximum segment size; must be non-zero and fit a signed 64-bit file offset. */
  std::uint64_t segment_capacity = default_segment_capacity;

  /** @brief @ref segment_allocation_strategy value. */
  segment_allocation_strategy segment_allocation = segment_allocation_strategy::sparse;

  /** @brief @ref storage_durability value. */
  storage_durability durability = storage_durability::balanced;

  /** @brief Maximum extent size; must be non-zero. */
  std::uint64_t max_extent_size = default_max_extent_size;

  /** @brief Maximum cached object records; zero disables the cache. */
  std::size_t object_lookup_cache_capacity = default_object_lookup_cache_capacity;

  /**
   * @brief @ref object_store::open_object concurrency limit.
   *
   * An empty value means unlimited. Zero rejects all reads. A permit remains
   * held until the returned reader reaches EOF, fails, or is destroyed.
   */
  std::optional<std::size_t> max_concurrent_reads;

  /**
   * @brief Concurrent object write limit.
   *
   * Shared by @ref object_store::put_object and @ref object_store::upload_part.
   * An empty value means unlimited. Zero rejects all writes.
   */
  std::optional<std::size_t> max_concurrent_writes;

  /**
   * @brief Optional custom @ref hasher_factory instance.
   *
   * The returned store shares ownership of the factory. Extora handles
   * XXH3-128 itself and uses this factory for other public checksum algorithms.
   * Reopened incomplete multipart uploads need the same algorithm support to
   * accept parts or complete the upload.
   */
  std::shared_ptr<hasher_factory> custom_hasher_factory;

  /** @brief Objects below this size are not deduplicated. */
  std::uint64_t dedup_min_object_size = default_dedup_min_object_size;

  /** @brief Minimum size of every non-final multipart part; zero disables the minimum. */
  std::uint64_t multipart_min_part_size = default_multipart_min_part_size;

  /**
   * @brief Reclaimable bytes that trigger reclamation before a data write.
   *
   * An empty value disables automatic reclamation. Zero triggers reclamation
   * before a data write whenever reclaimable extents exist.
   */
  std::optional<std::uint64_t> automatic_reclamation_threshold_bytes;

  /**
   * @brief Optional @ref operation_observer instance.
   *
   * The returned store shares ownership of the observer. Callbacks may run
   * concurrently.
   */
  std::shared_ptr<operation_observer> observer;

  /**
   * @brief Minimum byte distance between progress events.
   *
   * Zero reports every chunk.
   */
  std::uint64_t operation_progress_interval_bytes = default_operation_progress_interval_bytes;
};

} // namespace extora

#endif // EXTORA_OBJECT_STORE_OPTIONS_H
