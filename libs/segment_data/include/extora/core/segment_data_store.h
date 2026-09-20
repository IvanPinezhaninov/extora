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

#ifndef EXTORA_CORE_SEGMENT_DATA_STORE_H
#define EXTORA_CORE_SEGMENT_DATA_STORE_H

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <vector>

#include <extora/object_store_options.h>

#include <extora/core/object_data_store.h>
#include <extora/core/segment_handle_pool.h>

namespace extora::core {

class segment_data_store final : public object_data_store {
public:
  segment_data_store(std::filesystem::path root_directory, std::uint64_t segment_capacity,
                     storage_durability durability = storage_durability::balanced,
                     segment_allocation_strategy allocation_strategy = segment_allocation_strategy::sparse);
  ~segment_data_store() override = default;

  segment_data_store(const segment_data_store&) = delete;
  segment_data_store& operator=(const segment_data_store&) = delete;

  storage_error open();

  std::uint64_t max_extent_size() const override;

  storage_error get_segment_storage_usage(segment_storage_usage& usage) override;

  storage_error begin_write(const physical_extent& extent, data_write_handle& handle) override;

  storage_error write(data_write_handle handle, std::uint64_t storage_offset, const std::byte* data,
                      std::size_t size) override;

  void finish_write(data_write_handle handle) noexcept override;

  storage_error flush(std::uint64_t segment_id) override;

  storage_error validate_extent(const physical_extent& extent) override;

  storage_error remove_segment(std::uint64_t segment_id, segment_removal_result& result) override;

  storage_error begin_read(const physical_extent& extent, data_read_handle& handle) override;

  storage_error read(data_read_handle handle, std::uint64_t object_offset, std::byte* data, std::size_t size,
                     std::size_t& bytes_read) override;

  void finish_read(data_read_handle handle) noexcept override;

private:
  struct active_handle {
    std::uint64_t value = 0;
    physical_extent extent;
    segment_handle_pool::lease lease;
  };

  struct read_handle_shard {
    std::uint32_t next_generation = 1;
    std::vector<active_handle> handles;
    std::mutex handle_mutex;
  };

  static constexpr std::size_t read_handle_shard_count = 64;

  storage_error ensure_open() const;

  storage_error validate_segment_files() const;

  storage_error sync_segment(std::uint64_t segment_id);

  storage_error validate_extent_bounds(const physical_extent& extent, std::uint64_t length) const;

  storage_error validate_write_request(const physical_extent& extent, std::uint64_t storage_offset,
                                       std::size_t size) const;

  storage_error validate_read_request(const physical_extent& extent, std::uint64_t object_offset) const;

  static storage_error find_handle(std::uint64_t handle_value, const std::vector<active_handle>& handles,
                                   std::mutex& handles_mutex, physical_extent& extent, int& fd);

  static void finish_handle(std::uint64_t handle_value, std::vector<active_handle>& handles,
                            std::mutex& handles_mutex) noexcept;

  storage_error find_read_handle(std::uint64_t handle_value, physical_extent& extent, int& fd);

  void finish_read_handle(std::uint64_t handle_value) noexcept;

  std::filesystem::path m_root_directory;
  std::filesystem::path m_segments_directory;
  std::uint64_t m_segment_capacity = 0;
  storage_durability m_durability = storage_durability::balanced;
  segment_handle_pool m_handle_pool;
  bool m_is_open = false;
  std::uint32_t m_next_write_generation = 1;
  std::vector<active_handle> m_active_write_handles;
  std::mutex m_write_handles_mutex;
  std::array<read_handle_shard, read_handle_shard_count> m_read_handle_shards;
  std::atomic<std::size_t> m_next_read_handle_shard{0};

  static constexpr std::size_t max_cached_segments = 64;
};

} // namespace extora::core

#endif // EXTORA_CORE_SEGMENT_DATA_STORE_H
