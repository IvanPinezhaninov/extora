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

#include "extora/core/segment_data_store.h"

#include <cstdint>
#include <filesystem>
#include <limits>
#include <mutex>
#include <utility>

#include "segment_file_io.h"
#include "segment_layout.h"

namespace extora::core {

namespace {

std::uint64_t make_handle_value(std::uint32_t& next_generation, std::size_t slot)
{
  std::uint32_t generation = next_generation++;
  if (generation == 0) {
    generation = next_generation++;
  }
  return (static_cast<std::uint64_t>(generation) << 32U) | (static_cast<std::uint64_t>(slot) + 1U);
}

std::uint64_t make_read_handle_value(std::uint32_t& next_generation, std::size_t shard_index, std::size_t slot,
                                     std::size_t shard_count)
{
  const std::uint64_t encoded_slot = static_cast<std::uint64_t>(slot) * shard_count + shard_index + 1U;
  std::uint32_t generation = next_generation++;
  if (generation == 0) {
    generation = next_generation++;
  }
  return (static_cast<std::uint64_t>(generation) << 32U) | encoded_slot;
}

bool decode_read_handle(std::uint64_t handle_value, std::size_t shard_count, std::size_t& shard_index,
                        std::size_t& slot)
{
  const std::uint32_t encoded_slot = static_cast<std::uint32_t>(handle_value);
  if (encoded_slot == 0) return false;

  const std::size_t combined_index = static_cast<std::size_t>(encoded_slot - 1U);
  shard_index = combined_index % shard_count;
  slot = combined_index / shard_count;
  return true;
}

} // namespace

using segment_detail::ensure_directory;
using segment_detail::make_errno_error;
using segment_detail::read_at;
using segment_detail::sync_directory;
using segment_detail::sync_file;
using segment_detail::write_at;

segment_data_store::segment_data_store(std::filesystem::path root_directory, std::uint64_t segment_capacity,
                                       storage_durability durability, segment_allocation_strategy allocation_strategy)
  : m_root_directory{std::move(root_directory)}
  , m_segments_directory{m_root_directory / "segments"}
  , m_segment_capacity{segment_capacity}
  , m_durability{durability}
  , m_handle_pool{m_segments_directory, segment_capacity, allocation_strategy, max_cached_segments}
{}

storage_error segment_data_store::open()
{
  if (m_segment_capacity == 0) return make_error(storage_error_code::backend_failure, "segment capacity is zero");

  storage_error error = ensure_directory(m_root_directory);
  if (failed(error)) return error;

  error = ensure_directory(m_segments_directory);
  if (failed(error)) return error;

  error = validate_segment_files();
  if (failed(error)) return error;

  if (m_durability != storage_durability::relaxed) {
    if (sync_directory(m_root_directory) != 0)
      return make_errno_error(storage_error_code::backend_failure, "failed to flush storage directory");
    if (sync_directory(m_segments_directory) != 0)
      return make_errno_error(storage_error_code::backend_failure, "failed to flush segment directory");
  }

  m_is_open = true;
  return {};
}

std::uint64_t segment_data_store::max_extent_size() const
{
  return m_segment_capacity;
}

storage_error segment_data_store::get_segment_storage_usage(segment_storage_usage& usage)
{
  usage = {};
  storage_error error = ensure_open();
  if (failed(error)) return error;

  std::error_code error_code;
  for (const std::filesystem::directory_entry& entry :
       std::filesystem::directory_iterator{m_segments_directory, error_code}) {
    if (error_code) return make_error(storage_error_code::backend_failure, "failed to inspect segments directory");
    if (entry.path().extension() != ".dat") continue;
    if (!entry.is_regular_file(error_code) || error_code)
      return make_error(storage_error_code::backend_failure, "segment path is not a regular file");
    const std::uint64_t file_size = entry.file_size(error_code);
    if (error_code) return make_error(storage_error_code::backend_failure, "failed to inspect segment file");
    if (usage.segment_count == (std::numeric_limits<std::uint64_t>::max)() ||
        file_size > (std::numeric_limits<std::uint64_t>::max)() - usage.total_bytes)
      return make_error(storage_error_code::backend_failure, "segment storage usage overflow");
    ++usage.segment_count;
    usage.total_bytes += file_size;
  }
  if (error_code) return make_error(storage_error_code::backend_failure, "failed to inspect segments directory");
  return {};
}

storage_error segment_data_store::begin_write(const physical_extent& extent, data_write_handle& handle)
{
  storage_error error = ensure_open();
  if (failed(error)) return error;

  error = validate_write_request(extent, 0, 0);
  if (failed(error)) return error;

  segment_handle_pool::lease lease;
  error = m_handle_pool.acquire_for_write(extent.segment_id, m_durability != storage_durability::relaxed, lease);
  if (failed(error)) return error;

  std::lock_guard<std::mutex> lock{m_write_handles_mutex};
  std::size_t slot = 0;
  while (slot < m_active_write_handles.size() && m_active_write_handles[slot].value != 0)
    ++slot;
  if (slot == m_active_write_handles.size()) m_active_write_handles.emplace_back();
  if (slot >= static_cast<std::size_t>((std::numeric_limits<std::uint32_t>::max)()))
    return make_error(storage_error_code::backend_failure, "too many active write handles");

  active_handle& active = m_active_write_handles[slot];
  active.value = make_handle_value(m_next_write_generation, slot);
  active.extent = extent;
  active.lease = std::move(lease);
  handle.value = active.value;
  return {};
}

storage_error segment_data_store::write(data_write_handle handle, std::uint64_t storage_offset, const std::byte* data,
                                        std::size_t size)
{
  storage_error error = ensure_open();
  if (failed(error)) return error;

  physical_extent extent;
  int fd = -1;
  error = find_handle(handle.value, m_active_write_handles, m_write_handles_mutex, extent, fd);
  if (failed(error)) return error;

  error = validate_write_request(extent, storage_offset, size);
  if (failed(error)) return error;

  return write_at(fd, data, size, extent.offset + storage_offset);
}

void segment_data_store::finish_write(data_write_handle handle) noexcept
{
  finish_handle(handle.value, m_active_write_handles, m_write_handles_mutex);
}

storage_error segment_data_store::flush(std::uint64_t segment_id)
{
  storage_error error = ensure_open();
  if (failed(error)) return error;

  if (segment_id == 0) return make_error(storage_error_code::backend_failure, "invalid object storage segment");
  if (m_durability == storage_durability::relaxed) return {};

  return sync_segment(segment_id);
}

storage_error segment_data_store::validate_extent(const physical_extent& extent)
{
  storage_error error = ensure_open();
  if (failed(error)) return error;

  error = validate_extent_bounds(extent, extent.length);
  if (failed(error)) return error;

  const std::filesystem::path path = segment_detail::segment_path(m_segments_directory, extent.segment_id);
  std::error_code error_code;
  if (!std::filesystem::is_regular_file(path, error_code))
    return make_error(storage_error_code::backend_failure, "segment file is missing");

  const std::uint64_t file_size = static_cast<std::uint64_t>(std::filesystem::file_size(path, error_code));
  if (error_code) return make_error(storage_error_code::backend_failure, "failed to inspect segment file");

  if (file_size != m_segment_capacity)
    return make_error(storage_error_code::backend_failure, "segment capacity does not match store");

  return {};
}

storage_error segment_data_store::remove_segment(std::uint64_t segment_id, segment_removal_result& result)
{
  result = {};
  storage_error error = ensure_open();
  if (failed(error)) return error;
  if (segment_id == 0) return make_error(storage_error_code::backend_failure, "invalid object storage segment");

  error =
      m_handle_pool.remove_inactive(segment_id, result.segment_absent, result.segment_removed, result.released_bytes);
  if (failed(error) || !result.segment_absent || m_durability == storage_durability::relaxed) return error;
  if (segment_detail::sync_directory(m_segments_directory) != 0)
    return segment_detail::make_errno_error(storage_error_code::backend_failure,
                                            "failed to flush released segment storage");
  return {};
}

storage_error segment_data_store::begin_read(const physical_extent& extent, data_read_handle& handle)
{
  storage_error error = ensure_open();
  if (failed(error)) return error;

  error = validate_read_request(extent, 0);
  if (failed(error)) return error;

  segment_handle_pool::lease lease;
  error = m_handle_pool.acquire_existing(extent.segment_id, lease);
  if (failed(error)) return error;

  const std::size_t shard_index =
      m_next_read_handle_shard.fetch_add(1, std::memory_order_relaxed) % read_handle_shard_count;
  read_handle_shard& shard = m_read_handle_shards[shard_index];
  {
    std::lock_guard<std::mutex> lock{shard.handle_mutex};
    std::size_t slot = 0;
    while (slot < shard.handles.size() && shard.handles[slot].value != 0)
      ++slot;
    if (slot == shard.handles.size()) shard.handles.emplace_back();
    const std::size_t maximum_slot =
        (static_cast<std::size_t>((std::numeric_limits<std::uint32_t>::max)()) - shard_index - 1U) /
        read_handle_shard_count;
    if (slot > maximum_slot) return make_error(storage_error_code::backend_failure, "too many active read handles");

    active_handle& active = shard.handles[slot];
    active.value = make_read_handle_value(shard.next_generation, shard_index, slot, read_handle_shard_count);
    active.extent = extent;
    active.lease = std::move(lease);
    handle.value = active.value;
  }
  return {};
}

storage_error segment_data_store::read(data_read_handle handle, std::uint64_t object_offset, std::byte* data,
                                       std::size_t size, std::size_t& bytes_read)
{
  bytes_read = 0;

  physical_extent extent;
  int fd = -1;
  storage_error error = find_read_handle(handle.value, extent, fd);
  if (failed(error)) return error;

  error = validate_read_request(extent, object_offset);
  if (failed(error)) return error;

  const std::uint64_t remaining = extent.length - object_offset;
  const std::size_t bounded_size = static_cast<std::size_t>(
      remaining < static_cast<std::uint64_t>(size) ? remaining : static_cast<std::uint64_t>(size));
  if (bounded_size == 0) return {};

  return read_at(fd, data, bounded_size, extent.offset + object_offset, bytes_read);
}

void segment_data_store::finish_read(data_read_handle handle) noexcept
{
  finish_read_handle(handle.value);
}

storage_error segment_data_store::ensure_open() const
{
  if (m_is_open) return {};

  return make_error(storage_error_code::backend_failure, "filesystem segment data store is not open");
}

storage_error segment_data_store::validate_segment_files() const
{
  std::error_code error_code;
  for (const std::filesystem::directory_entry& entry :
       std::filesystem::directory_iterator{m_segments_directory, error_code}) {
    if (error_code) return make_error(storage_error_code::backend_failure, "failed to inspect segments directory");
    if (entry.path().extension() != ".dat") continue;
    if (!entry.is_regular_file(error_code) || error_code)
      return make_error(storage_error_code::backend_failure, "segment path is not a regular file");
    if (entry.file_size(error_code) != m_segment_capacity || error_code)
      return make_error(storage_error_code::backend_failure, "segment capacity does not match store");
  }
  if (error_code) return make_error(storage_error_code::backend_failure, "failed to inspect segments directory");

  return {};
}

storage_error segment_data_store::sync_segment(std::uint64_t segment_id)
{
  segment_handle_pool::lease lease;
  storage_error error = m_handle_pool.acquire_existing(segment_id, lease);
  if (failed(error)) return error;

  if (sync_file(lease.fd()) != 0)
    return make_errno_error(storage_error_code::backend_failure, "failed to flush segment");

  return {};
}

storage_error segment_data_store::validate_write_request(const physical_extent& extent, std::uint64_t storage_offset,
                                                         std::size_t size) const
{
  if (extent.length == 0) return make_error(storage_error_code::backend_failure, "extent length is zero");

  storage_error error = validate_extent_bounds(extent, extent.length);
  if (failed(error)) return error;

  if (storage_offset > extent.length || static_cast<std::uint64_t>(size) > extent.length - storage_offset)
    return make_error(storage_error_code::invalid_range, "write exceeds reserved extent");

  return {};
}

storage_error segment_data_store::validate_extent_bounds(const physical_extent& extent, std::uint64_t length) const
{
  if (extent.segment_id == 0) return make_error(storage_error_code::backend_failure, "invalid object storage segment");
  if (extent.offset > m_segment_capacity || length > m_segment_capacity - extent.offset)
    return make_error(storage_error_code::backend_failure, "object storage exceeds segment capacity");

  return {};
}

storage_error segment_data_store::validate_read_request(const physical_extent& extent,
                                                        std::uint64_t object_offset) const
{
  storage_error error = validate_extent_bounds(extent, extent.length);
  if (failed(error)) return error;

  if (object_offset > extent.length)
    return make_error(storage_error_code::invalid_range, "object offset is past object length");

  return {};
}

storage_error segment_data_store::find_handle(std::uint64_t handle_value, const std::vector<active_handle>& handles,
                                              std::mutex& handles_mutex, physical_extent& extent, int& fd)
{
  if (handle_value == 0) return make_error(storage_error_code::backend_failure, "invalid segment handle");

  std::lock_guard<std::mutex> lock{handles_mutex};
  const std::uint32_t encoded_slot = static_cast<std::uint32_t>(handle_value);
  if (encoded_slot == 0 || static_cast<std::size_t>(encoded_slot) > handles.size())
    return make_error(storage_error_code::backend_failure, "invalid segment handle");

  const active_handle& active = handles[static_cast<std::size_t>(encoded_slot - 1U)];
  if (active.value != handle_value) return make_error(storage_error_code::backend_failure, "invalid segment handle");

  extent = active.extent;
  fd = active.lease.fd();
  return {};
}

void segment_data_store::finish_handle(std::uint64_t handle_value, std::vector<active_handle>& handles,
                                       std::mutex& handles_mutex) noexcept
{
  if (handle_value == 0) return;

  std::lock_guard<std::mutex> lock{handles_mutex};
  const std::uint32_t encoded_slot = static_cast<std::uint32_t>(handle_value);
  if (encoded_slot == 0 || static_cast<std::size_t>(encoded_slot) > handles.size()) return;

  active_handle& active = handles[static_cast<std::size_t>(encoded_slot - 1U)];
  if (active.value != handle_value) return;

  active.value = 0;
  active.extent = {};
  active.lease.reset();
}

storage_error segment_data_store::find_read_handle(std::uint64_t handle_value, physical_extent& extent, int& fd)
{
  std::size_t shard_index = 0;
  std::size_t slot = 0;
  if (!decode_read_handle(handle_value, read_handle_shard_count, shard_index, slot))
    return make_error(storage_error_code::backend_failure, "invalid segment handle");

  read_handle_shard& shard = m_read_handle_shards[shard_index];
  std::lock_guard<std::mutex> lock{shard.handle_mutex};
  if (slot >= shard.handles.size()) return make_error(storage_error_code::backend_failure, "invalid segment handle");

  const active_handle& active = shard.handles[slot];
  if (active.value != handle_value) return make_error(storage_error_code::backend_failure, "invalid segment handle");

  extent = active.extent;
  fd = active.lease.fd();
  return {};
}

void segment_data_store::finish_read_handle(std::uint64_t handle_value) noexcept
{
  std::size_t shard_index = 0;
  std::size_t slot = 0;
  if (!decode_read_handle(handle_value, read_handle_shard_count, shard_index, slot)) return;

  read_handle_shard& shard = m_read_handle_shards[shard_index];
  std::lock_guard<std::mutex> lock{shard.handle_mutex};
  if (slot >= shard.handles.size()) return;

  active_handle& active = shard.handles[slot];
  if (active.value != handle_value) return;

  active.value = 0;
  active.extent = {};
  active.lease.reset();
}

} // namespace extora::core
