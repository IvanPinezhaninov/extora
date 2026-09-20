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

#include "extent_writer.h"

#include <algorithm>
#include <unordered_set>
#include <utility>

#include "extora/core/opaque_id.h"

namespace extora::core {

extent_writer::extent_writer(object_index& index, std::shared_mutex& index_mutex, object_data_store& data_store,
                             std::uint64_t max_extent_size, std::optional<std::uint64_t> expected_content_length,
                             extent_allocation_mode allocation_mode)
  : m_index{index}
  , m_index_mutex{index_mutex}
  , m_data_store{data_store}
  , m_max_extent_size{max_extent_size}
  , m_expected_content_length{expected_content_length}
  , m_allocation_mode{allocation_mode}
  , m_operation_id{generate_opaque_id(write_operation_id_prefix)}
  , m_session{data_store}
{}

storage_error extent_writer::write(const std::byte* data, std::size_t size)
{
  std::size_t bytes_written = 0;
  while (bytes_written < size) {
    if (!m_session.is_open() || m_current_extent_length == m_extents.back().length) {
      m_session.close();
      const storage_error error = open_extent();
      if (failed(error)) return error;
    }

    const physical_extent& extent = m_extents.back();
    const std::uint64_t extent_offset = m_current_extent_length;
    const std::uint64_t available = extent.length - extent_offset;
    const std::size_t chunk_size =
        static_cast<std::size_t>(std::min(available, static_cast<std::uint64_t>(size - bytes_written)));

    const storage_error error = m_data_store.write(m_session.handle(), extent_offset, data + bytes_written, chunk_size);
    if (failed(error)) return error;

    m_current_extent_length += static_cast<std::uint64_t>(chunk_size);
    bytes_written += chunk_size;
    m_content_length += static_cast<std::uint64_t>(chunk_size);
  }
  return {};
}

storage_error extent_writer::finish()
{
  m_session.close();
  if (!m_extents.empty()) m_extents.back().length = m_current_extent_length;
  return flush_segments();
}

void extent_writer::discard()
{
  m_session.close();
  if (m_extents.empty()) return;

  const std::lock_guard<std::shared_mutex> index_lock{m_index_mutex};
  static_cast<void>(m_index.abandon_extents(m_extents));
  m_extents.clear();
}

std::uint64_t extent_writer::content_length() const
{
  return m_content_length;
}

std::vector<physical_extent> extent_writer::take_extents()
{
  return std::move(m_extents);
}

storage_error extent_writer::open_extent()
{
  std::uint64_t reservation_size = std::min(m_max_extent_size, m_data_store.max_extent_size());
  if (reservation_size == 0) return make_error(storage_error_code::backend_failure, "maximum extent size is zero");

  if (m_expected_content_length.has_value()) {
    if (m_content_length >= *m_expected_content_length)
      return make_error(storage_error_code::source_failure, "content length exceeds expected size");
    reservation_size = std::min(reservation_size, *m_expected_content_length - m_content_length);
  }
  physical_extent reserved_extent;
  {
    const std::lock_guard<std::shared_mutex> index_lock{m_index_mutex};
    const storage_error error =
        m_index.reserve_extent(m_operation_id, m_extents.size(), reservation_size, reserved_extent, m_allocation_mode);
    if (failed(error)) return error;
  }

  m_extents.push_back(reserved_extent);
  m_current_extent_length = 0;
  if (reserved_extent.length == 0)
    return make_error(storage_error_code::backend_failure, "extent reservation is empty");

  return m_session.open(reserved_extent);
}

storage_error extent_writer::flush_segments()
{
  std::unordered_set<std::uint64_t> segment_ids;
  segment_ids.reserve(m_extents.size());
  for (const physical_extent& extent : m_extents)
    segment_ids.insert(extent.segment_id);

  for (const std::uint64_t segment_id : segment_ids) {
    const storage_error error = m_data_store.flush(segment_id);
    if (failed(error)) return error;
  }
  return {};
}

} // namespace extora::core
