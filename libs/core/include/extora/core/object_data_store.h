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

#ifndef EXTORA_CORE_OBJECT_DATA_STORE_H
#define EXTORA_CORE_OBJECT_DATA_STORE_H

#include <cstddef>
#include <cstdint>

#include <extora/core/storage_types.h>
#include <extora/storage_error.h>

namespace extora::core {

/** @brief Physical usage reported by a segment-backed data store. */
struct segment_storage_usage {
  /** @brief Number of segment files currently present. */
  std::uint64_t segment_count = 0;

  /** @brief Total logical size of the current segment files. */
  std::uint64_t total_bytes = 0;
};

/** @brief Outcome of an attempt to remove a fully free segment. */
struct segment_removal_result {
  /** @brief Whether the segment is absent after the attempt. */
  bool segment_absent = false;

  /** @brief Whether this attempt removed the segment. */
  bool segment_removed = false;

  /** @brief Logical size of the segment removed by this attempt. */
  std::uint64_t released_bytes = 0;
};

// Reads and writes physical extents. Independent handles may be used
// concurrently. The caller serializes access to each handle.
class object_data_store {
public:
  virtual ~object_data_store() = default;

  virtual std::uint64_t max_extent_size() const = 0;

  virtual storage_error get_segment_storage_usage(segment_storage_usage& usage) = 0;

  // Creates storage if needed and opens the extent.
  virtual storage_error begin_write(const physical_extent& extent, data_write_handle& handle) = 0;

  // A handle stays bound to the extent passed to begin_*.
  virtual storage_error write(data_write_handle handle, std::uint64_t storage_offset, const std::byte* data,
                              std::size_t size) = 0;

  virtual void finish_write(data_write_handle handle) noexcept = 0;

  virtual storage_error flush(std::uint64_t segment_id) = 0;

  virtual storage_error validate_extent(const physical_extent& extent) = 0;

  // Removes a fully free segment. An active handle leaves the segment present
  // and reports a successful no-op through result.
  virtual storage_error remove_segment(std::uint64_t segment_id, segment_removal_result& result) = 0;

  virtual storage_error begin_read(const physical_extent& extent, data_read_handle& handle) = 0;

  virtual storage_error read(data_read_handle handle, std::uint64_t object_offset, std::byte* data, std::size_t size,
                             std::size_t& bytes_read) = 0;

  virtual void finish_read(data_read_handle handle) noexcept = 0;
};

} // namespace extora::core

#endif // EXTORA_CORE_OBJECT_DATA_STORE_H
