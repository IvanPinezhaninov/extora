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

#ifndef EXTORA_CORE_SEGMENT_HANDLE_POOL_H
#define EXTORA_CORE_SEGMENT_HANDLE_POOL_H

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <vector>

#include <extora/object_store_options.h>
#include <extora/storage_error.h>

namespace extora::core {

// Thread-safe cache of open segment files. A lease prevents eviction.
class segment_handle_pool {
public:
  class lease {
  public:
    lease() = default;
    ~lease();

    lease(const lease&) = delete;
    lease& operator=(const lease&) = delete;

    lease(lease&& other) noexcept;
    lease& operator=(lease&& other) noexcept;

    int fd() const;
    void reset() noexcept;

  private:
    friend class segment_handle_pool;

    lease(segment_handle_pool& pool, std::uint64_t segment_id, int fd);

    segment_handle_pool* m_pool = nullptr;
    std::uint64_t m_segment_id = 0;
    int m_fd = -1;
  };

  segment_handle_pool(std::filesystem::path segments_directory, std::uint64_t segment_capacity,
                      segment_allocation_strategy allocation_strategy, std::size_t capacity);
  ~segment_handle_pool();

  segment_handle_pool(const segment_handle_pool&) = delete;
  segment_handle_pool& operator=(const segment_handle_pool&) = delete;

  storage_error acquire_for_write(std::uint64_t segment_id, bool sync_directory_on_creation, lease& result);
  storage_error acquire_existing(std::uint64_t segment_id, lease& result);
  storage_error remove_inactive(std::uint64_t segment_id, bool& segment_absent, bool& segment_removed,
                                std::uint64_t& released_bytes);

private:
  struct entry {
    std::uint64_t segment_id = 0;
    int fd = -1;
    std::size_t lease_count = 0;
    std::uint64_t last_access = 0;
  };

  void release(std::uint64_t segment_id) noexcept;
  void evict_inactive();
  storage_error acquire(std::uint64_t segment_id, bool create_if_missing, bool sync_directory_on_creation,
                        lease& result);

  std::filesystem::path m_segments_directory;
  std::uint64_t m_segment_capacity = 0;
  segment_allocation_strategy m_allocation_strategy = segment_allocation_strategy::sparse;
  std::size_t m_capacity = 0;
  std::uint64_t m_access_counter = 0;
  std::vector<entry> m_entries;
  std::mutex m_mutex;
};

} // namespace extora::core

#endif // EXTORA_CORE_SEGMENT_HANDLE_POOL_H
