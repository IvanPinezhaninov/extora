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

#include "payload_write.h"

#include <array>
#include <utility>

#include "extent_writer.h"
#include "extora/core/object_data_store.h"
#include "extora/core/object_index.h"
#include "extora/core/object_store_core.h"
#include "operation_tracker.h"
#include "payload_hashes.h"

namespace extora::core {

namespace {

constexpr std::size_t io_buffer_size = 64 * 1024;

class payload_write_operation {
public:
  payload_write_operation(object_reader& reader, object_index& index, std::shared_mutex& index_mutex,
                          object_data_store& data_store, hasher_factory& hash_factory, std::uint64_t max_extent_size,
                          payload_write_options options)
    : m_reader{reader}
    , m_options{std::move(options)}
    , m_hashes{hash_factory}
    , m_extent_writer{
          index, index_mutex, data_store, max_extent_size, m_options.expected_content_length, m_options.allocation_mode}
  {}

  storage_error run(payload_write_result& result)
  {
    result = {};

    storage_error error = configure_hashes();
    if (!failed(error)) error = stream_payload();
    if (!failed(error)) error = finish_payload(result);
    if (!failed(error)) return {};

    m_extent_writer.discard();
    return error;
  }

private:
  storage_error configure_hashes()
  {
    return m_hashes.configure(m_options.expected_checksum, m_options.public_checksum_algorithm);
  }

  storage_error stream_payload()
  {
    std::array<std::byte, io_buffer_size> buffer;
    while (true) {
      const object_read_result read_result = m_reader.read(buffer.data(), buffer.size());
      if (read_result.bytes_read > buffer.size())
        return make_error(storage_error_code::source_failure, "object reader returned too many bytes");

      if (read_result.bytes_read > 0) {
        storage_error error = validate_chunk_size(read_result.bytes_read);
        if (!failed(error)) error = m_hashes.update(buffer.data(), read_result.bytes_read);
        if (!failed(error)) error = m_extent_writer.write(buffer.data(), read_result.bytes_read);
        if (failed(error)) return error;
        if (m_options.operation != nullptr)
          m_options.operation->advance(static_cast<std::uint64_t>(read_result.bytes_read));
      }

      if (failed(read_result.error)) return read_result.error;
      if (read_result.end_of_stream) return {};
      if (read_result.bytes_read == 0)
        return make_error(storage_error_code::source_failure, "object reader made no progress");
    }
  }

  storage_error finish_payload(payload_write_result& result)
  {
    if (m_options.expected_content_length.has_value() &&
        *m_options.expected_content_length != m_extent_writer.content_length())
      return content_length_error();

    payload_hash_result hash_result;
    storage_error error = m_hashes.finish(hash_result);
    if (!failed(error)) error = m_extent_writer.finish();
    if (failed(error)) return error;

    result.content_length = m_extent_writer.content_length();
    result.extents = m_extent_writer.take_extents();
    result.etag = std::move(hash_result.etag);
    result.internal_checksum = std::move(hash_result.internal_checksum);
    result.checksum = std::move(hash_result.checksum);
    result.deduplicate = m_options.deduplicate;
    return {};
  }

  storage_error validate_chunk_size(std::size_t size) const
  {
    if (!m_options.expected_content_length.has_value()) return {};
    const std::uint64_t content_length = m_extent_writer.content_length();
    if (content_length > *m_options.expected_content_length ||
        static_cast<std::uint64_t>(size) > *m_options.expected_content_length - content_length)
      return content_length_error();
    return {};
  }

  static storage_error content_length_error()
  {
    return storage_error{storage_error_code::source_failure, "content length does not match bytes read"};
  }

  object_reader& m_reader;
  payload_write_options m_options;
  payload_hashes m_hashes;
  extent_writer m_extent_writer;
};

} // namespace

storage_error object_store_core::write_payload(object_reader& reader, payload_write_options options,
                                               payload_write_result& result)
{
  const storage_error reclamation_error = reclaim_storage_if_needed();
  if (failed(reclamation_error)) return reclamation_error;

  payload_write_operation operation{reader,         m_index,           m_index_mutex,     m_data_store,
                                    m_hash_factory, m_max_extent_size, std::move(options)};
  return operation.run(result);
}

} // namespace extora::core
