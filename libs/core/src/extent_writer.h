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

#ifndef EXTORA_CORE_EXTENT_WRITER_H
#define EXTORA_CORE_EXTENT_WRITER_H

#include <cstddef>
#include <cstdint>
#include <optional>
#include <shared_mutex>
#include <string>
#include <vector>

#include <data_store_session.h>

#include <extora/core/object_index.h>

namespace extora::core {

class extent_writer final {
public:
  extent_writer(object_index& index, std::shared_mutex& index_mutex, object_data_store& data_store,
                std::uint64_t max_extent_size, std::optional<std::uint64_t> expected_content_length,
                extent_allocation_mode allocation_mode = extent_allocation_mode::reuse);

  storage_error write(const std::byte* data, std::size_t size);

  storage_error finish();
  void discard();

  std::uint64_t content_length() const;
  std::vector<physical_extent> take_extents();

private:
  storage_error open_extent();
  storage_error flush_segments();

  object_index& m_index;
  std::shared_mutex& m_index_mutex;
  object_data_store& m_data_store;
  std::uint64_t m_max_extent_size;
  std::optional<std::uint64_t> m_expected_content_length;
  extent_allocation_mode m_allocation_mode = extent_allocation_mode::reuse;
  std::uint64_t m_content_length = 0;
  std::uint64_t m_current_extent_length = 0;
  std::string m_operation_id;
  std::vector<physical_extent> m_extents;
  data_write_session m_session;
};

} // namespace extora::core

#endif // EXTORA_CORE_EXTENT_WRITER_H
