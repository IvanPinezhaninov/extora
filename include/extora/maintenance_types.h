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

#ifndef EXTORA_MAINTENANCE_TYPES_H
#define EXTORA_MAINTENANCE_TYPES_H

#include <cstdint>

namespace extora {

/** @brief Options used by @ref managed_object_store::reclaim_storage calls. */
struct reclaim_storage_options {
  /**
   * @brief Delete quarantined corrupted objects before reclaiming storage.
   *
   * Disabled by default to preserve damaged objects for investigation. In a
   * versioned bucket, deletion may expose the newest healthy previous version.
   */
  bool delete_corrupted_objects = false;
};

/** @brief Current estimate of storage that can be reclaimed. */
struct reclamation_estimate {
  /** @brief Bytes currently eligible for reclamation. */
  std::uint64_t reclaimable_bytes = 0;

  /** @brief Storage extents currently eligible for reclamation. */
  std::uint64_t reclaimable_extent_count = 0;
};

/** @brief Result returned by @ref managed_object_store::reclaim_storage. */
struct reclaim_storage_result {
  /** @brief Bytes made reusable by this call. */
  std::uint64_t reclaimed_bytes = 0;

  /** @brief Storage extents made reusable by this call. */
  std::uint64_t reclaimed_extent_count = 0;

  /** @brief Bytes still awaiting reclamation, including those held by active reads. */
  std::uint64_t remaining_reclaimable_bytes = 0;

  /** @brief Storage extents still awaiting reclamation, including those held by active reads. */
  std::uint64_t remaining_reclaimable_extent_count = 0;
};

/** @brief Options used by @ref managed_object_store::compact_storage calls. */
struct compact_storage_options {
  /** @brief Delete empty segment files after compaction when possible. */
  bool delete_empty_segments = true;
};

/** @brief Result returned by @ref managed_object_store::compact_storage. */
struct compact_storage_result {
  /** @brief Number of segment files observed before compaction. */
  std::uint64_t segment_count_before = 0;

  /** @brief Number of segment files observed after compaction. */
  std::uint64_t segment_count_after = 0;

  /** @brief Number of healthy payloads considered for compaction. */
  std::uint64_t examined_payload_count = 0;

  /** @brief Number of payloads changed by compaction. */
  std::uint64_t compacted_payload_count = 0;

  /** @brief Total number of payload bytes moved. */
  std::uint64_t compacted_bytes = 0;

  /** @brief Number of source extents replaced. */
  std::uint64_t replaced_extent_count = 0;

  /** @brief Number of replacement extents written. */
  std::uint64_t compacted_extent_count = 0;

  /** @brief Number of segment files removed by this call. */
  std::uint64_t removed_segment_count = 0;

  /** @brief Logical size of segment files removed by this call. */
  std::uint64_t released_bytes = 0;
};

} // namespace extora

#endif // EXTORA_MAINTENANCE_TYPES_H
