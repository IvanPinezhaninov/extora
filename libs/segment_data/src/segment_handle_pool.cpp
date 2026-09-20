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

#include "extora/core/segment_handle_pool.h"

#include <cerrno>
#include <utility>

#include "segment_file_io.h"
#include "segment_layout.h"

namespace extora::core {

namespace {

storage_error validate_segment_size(int fd, std::uint64_t expected_size)
{
  std::uint64_t actual_size = 0;
  if (segment_detail::file_size(fd, actual_size) != 0)
    return segment_detail::make_errno_error(storage_error_code::backend_failure, "failed to inspect segment");
  if (actual_size != expected_size)
    return make_error(storage_error_code::backend_failure, "segment capacity does not match store");

  return {};
}

} // namespace

segment_handle_pool::lease::lease(segment_handle_pool& pool, std::uint64_t segment_id, int fd)
  : m_pool{&pool}
  , m_segment_id{segment_id}
  , m_fd{fd}
{}

segment_handle_pool::lease::~lease()
{
  reset();
}

segment_handle_pool::lease::lease(lease&& other) noexcept
  : m_pool{other.m_pool}
  , m_segment_id{other.m_segment_id}
  , m_fd{other.m_fd}
{
  other.m_pool = nullptr;
  other.m_segment_id = 0;
  other.m_fd = -1;
}

segment_handle_pool::lease& segment_handle_pool::lease::operator=(lease&& other) noexcept
{
  if (this == &other) return *this;

  reset();
  m_pool = other.m_pool;
  m_segment_id = other.m_segment_id;
  m_fd = other.m_fd;
  other.m_pool = nullptr;
  other.m_segment_id = 0;
  other.m_fd = -1;
  return *this;
}

int segment_handle_pool::lease::fd() const
{
  return m_fd;
}

void segment_handle_pool::lease::reset() noexcept
{
  if (m_pool != nullptr) m_pool->release(m_segment_id);
  m_pool = nullptr;
  m_segment_id = 0;
  m_fd = -1;
}

segment_handle_pool::segment_handle_pool(std::filesystem::path segments_directory, std::uint64_t segment_capacity,
                                         segment_allocation_strategy allocation_strategy, std::size_t capacity)
  : m_segments_directory{std::move(segments_directory)}
  , m_segment_capacity{segment_capacity}
  , m_allocation_strategy{allocation_strategy}
  , m_capacity{capacity}
{}

segment_handle_pool::~segment_handle_pool()
{
  for (const entry& item : m_entries) {
    if (item.fd >= 0) segment_detail::close_file(item.fd);
  }
}

storage_error segment_handle_pool::acquire_for_write(std::uint64_t segment_id, bool sync_directory_on_creation,
                                                     lease& result)
{
  return acquire(segment_id, true, sync_directory_on_creation, result);
}

storage_error segment_handle_pool::acquire_existing(std::uint64_t segment_id, lease& result)
{
  return acquire(segment_id, false, false, result);
}

storage_error segment_handle_pool::remove_inactive(std::uint64_t segment_id, bool& segment_absent,
                                                   bool& segment_removed, std::uint64_t& released_bytes)
{
  segment_absent = false;
  segment_removed = false;
  released_bytes = 0;
  std::lock_guard<std::mutex> lock{m_mutex};
  for (std::vector<entry>::iterator it = m_entries.begin(); it != m_entries.end(); ++it) {
    if (it->segment_id != segment_id) continue;
    if (it->lease_count != 0) return {};

    if (it->fd >= 0) segment_detail::close_file(it->fd);
    m_entries.erase(it);
    break;
  }

  const std::filesystem::path path = segment_detail::segment_path(m_segments_directory, segment_id);
  std::error_code error;
  const bool exists = std::filesystem::exists(path, error);
  if (error)
    return storage_error{storage_error_code::backend_failure, "failed to inspect free segment: " + error.message()};
  if (!exists) {
    segment_absent = true;
    return {};
  }

  const std::uint64_t file_size = std::filesystem::file_size(path, error);
  if (error)
    return storage_error{storage_error_code::backend_failure, "failed to inspect free segment: " + error.message()};
  segment_removed = std::filesystem::remove(path, error);
  if (error)
    return storage_error{storage_error_code::backend_failure, "failed to remove free segment: " + error.message()};
  if (segment_removed) {
    segment_absent = true;
    released_bytes = file_size;
    return {};
  }

  const bool still_exists = std::filesystem::exists(path, error);
  if (error)
    return storage_error{storage_error_code::backend_failure, "failed to inspect free segment: " + error.message()};
  segment_absent = !still_exists;
  return {};
}

storage_error segment_handle_pool::acquire(std::uint64_t segment_id, bool create_if_missing,
                                           bool sync_directory_on_creation, lease& result)
{
  result.reset();

  std::lock_guard<std::mutex> lock{m_mutex};
  for (entry& item : m_entries) {
    if (item.segment_id != segment_id) continue;

    ++item.lease_count;
    item.last_access = ++m_access_counter;
    result = lease{*this, segment_id, item.fd};
    return {};
  }

  evict_inactive();

  const std::filesystem::path path = segment_detail::segment_path(m_segments_directory, segment_id);
  int fd = segment_detail::open_read_write_existing(path);
  bool created = false;
  std::filesystem::path created_path;
  if (fd < 0 && create_if_missing && errno == ENOENT) {
    created_path = segment_detail::segment_creation_path(m_segments_directory, segment_id);
    std::error_code remove_error;
    std::filesystem::remove(created_path, remove_error);
    if (remove_error)
      return make_error(storage_error_code::backend_failure,
                        "failed to remove incomplete segment creation: " + remove_error.message());

    fd = segment_detail::create_read_write(created_path);
    created = fd >= 0;
  }
  if (fd < 0) return segment_detail::make_errno_error(storage_error_code::backend_failure, "failed to open segment");

  if (created) {
    const auto discard_created_segment = [&](storage_error error) {
      segment_detail::close_file(fd);
      std::error_code remove_error;
      std::filesystem::remove(created_path, remove_error);
      return error;
    };

    const int allocation_result = m_allocation_strategy == segment_allocation_strategy::reserve
                                      ? segment_detail::allocate_file(fd, m_segment_capacity)
                                      : segment_detail::resize_file(fd, m_segment_capacity);
    if (allocation_result != 0)
      return discard_created_segment(
          segment_detail::make_errno_error(storage_error_code::backend_failure, "failed to allocate segment"));

    if (sync_directory_on_creation) {
      if (segment_detail::sync_file(fd) != 0)
        return discard_created_segment(
            segment_detail::make_errno_error(storage_error_code::backend_failure, "failed to persist new segment"));
    }

    std::error_code rename_error;
    std::filesystem::rename(created_path, path, rename_error);
    if (rename_error)
      return discard_created_segment(
          make_error(storage_error_code::backend_failure, "failed to publish new segment: " + rename_error.message()));
    created_path = path;

    if (sync_directory_on_creation && segment_detail::sync_directory(m_segments_directory) != 0)
      return discard_created_segment(
          segment_detail::make_errno_error(storage_error_code::backend_failure, "failed to persist segment creation"));
  } else {
    const storage_error size_error = validate_segment_size(fd, m_segment_capacity);
    if (failed(size_error)) {
      segment_detail::close_file(fd);
      return size_error;
    }
  }

  m_entries.push_back(entry{segment_id, fd, 1, ++m_access_counter});
  result = lease{*this, segment_id, fd};
  return {};
}

void segment_handle_pool::release(std::uint64_t segment_id) noexcept
{
  std::lock_guard<std::mutex> lock{m_mutex};
  for (entry& item : m_entries) {
    if (item.segment_id != segment_id) continue;

    if (item.lease_count != 0) --item.lease_count;
    if (m_entries.size() > m_capacity) evict_inactive();
    return;
  }
}

void segment_handle_pool::evict_inactive()
{
  if (m_entries.size() < m_capacity) return;

  std::vector<entry>::iterator oldest = m_entries.end();
  for (std::vector<entry>::iterator it = m_entries.begin(); it != m_entries.end(); ++it) {
    if (it->lease_count != 0) continue;
    if (oldest == m_entries.end() || it->last_access < oldest->last_access) oldest = it;
  }

  if (oldest == m_entries.end()) return;

  segment_detail::close_file(oldest->fd);
  m_entries.erase(oldest);
}

} // namespace extora::core
