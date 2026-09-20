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

#include <algorithm>
#include <array>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "data_store_session.h"
#include "extora/core/object_data_store.h"
#include "extora/core/object_index.h"
#include "extora/core/object_store_core.h"
#include "hashing_object_writer.h"
#include "object_store_core_helpers.h"
#include "operation_tracker.h"

namespace extora::core {

using object_store_detail::check_read_conditions;
using object_store_detail::object_content_length;
using object_store_detail::to_object_info;
using object_store_detail::validate_object_address;

namespace {

constexpr std::size_t io_buffer_size = 64 * 1024;

class payload_read_operation {
public:
  payload_read_operation(object_data_store& data_store, const std::vector<physical_extent>& extents,
                         object_writer& writer, std::uint64_t read_offset, std::uint64_t length,
                         operation_tracker* operation = nullptr)
    : m_data_store{data_store}
    , m_extents{extents}
    , m_writer{writer}
    , m_read_offset{read_offset}
    , m_remaining_to_read{length}
    , m_operation{operation}
  {}

  storage_error run()
  {
    std::uint64_t object_position = 0;
    for (const physical_extent& extent : m_extents) {
      if (m_remaining_to_read == 0) break;

      const std::uint64_t extent_end = object_position + extent.length;
      if (m_read_offset >= extent_end) {
        object_position = extent_end;
        continue;
      }

      const std::uint64_t extent_offset = m_read_offset > object_position ? m_read_offset - object_position : 0;
      const std::uint64_t extent_length = std::min(extent.length - extent_offset, m_remaining_to_read);
      if (extent_length > 0) {
        const storage_error error = read_extent(extent, extent_offset, extent_length);
        if (failed(error)) return error;
      }
      object_position = extent_end;
    }

    if (m_remaining_to_read != 0)
      return make_error(storage_error_code::backend_failure, "object extent manifest ended before expected length");
    return {};
  }

private:
  storage_error read_extent(const physical_extent& extent, std::uint64_t extent_offset,
                            std::uint64_t remaining_in_extent)
  {
    data_read_session read_session{m_data_store};
    storage_error error = read_session.open(extent);
    if (failed(error)) return error;

    while (remaining_in_extent > 0) {
      const std::size_t chunk_size =
          static_cast<std::size_t>(std::min(remaining_in_extent, static_cast<std::uint64_t>(m_buffer.size())));
      std::size_t bytes_read = 0;
      error = m_data_store.read(read_session.handle(), extent_offset, m_buffer.data(), chunk_size, bytes_read);
      if (failed(error)) return error;
      if (bytes_read == 0)
        return make_error(storage_error_code::backend_failure, "object data ended before expected length");

      error = m_writer.write(m_buffer.data(), bytes_read);
      if (failed(error)) return error;
      if (m_operation != nullptr) m_operation->advance(static_cast<std::uint64_t>(bytes_read));

      extent_offset += static_cast<std::uint64_t>(bytes_read);
      m_read_offset += static_cast<std::uint64_t>(bytes_read);
      m_remaining_to_read -= static_cast<std::uint64_t>(bytes_read);
      remaining_in_extent -= static_cast<std::uint64_t>(bytes_read);
    }
    return {};
  }

  object_data_store& m_data_store;
  const std::vector<physical_extent>& m_extents;
  object_writer& m_writer;
  std::uint64_t m_read_offset;
  std::uint64_t m_remaining_to_read;
  std::array<std::byte, io_buffer_size> m_buffer;
  operation_tracker* m_operation = nullptr;
};

class opened_object_reader final : public object_reader {
public:
  opened_object_reader(object_index& index, std::shared_mutex& index_mutex, object_lookup_cache& object_cache,
                       object_data_store& data_store, std::uint64_t payload_id, std::vector<physical_extent> extents,
                       std::uint64_t object_length, std::uint64_t read_offset, std::uint64_t length,
                       active_object_registry::object_registration registration, operation_limiter::permit permit,
                       operation_tracker operation, std::unique_ptr<hasher> checksum_hasher,
                       std::string expected_checksum_value)
    : m_index{index}
    , m_index_mutex{index_mutex}
    , m_object_cache{object_cache}
    , m_data_store{data_store}
    , m_payload_id{payload_id}
    , m_extents{std::move(extents)}
    , m_object_length{object_length}
    , m_read_offset{read_offset}
    , m_remaining_to_read{length}
    , m_registration{std::move(registration)}
    , m_permit{std::move(permit)}
    , m_operation{std::move(operation)}
    , m_read_session{data_store}
    , m_checksum_hasher{std::move(checksum_hasher)}
    , m_expected_checksum_value{std::move(expected_checksum_value)}
  {}

  ~opened_object_reader() override
  {
    if (m_terminal) return;
    static_cast<void>(
        finish(make_error(storage_error_code::operation_cancelled, "object read was abandoned before end of stream")));
  }

  storage_error initialize()
  {
    std::uint64_t manifest_length = 0;
    for (const physical_extent& extent : m_extents) {
      if (extent.length > std::numeric_limits<std::uint64_t>::max() - manifest_length)
        return finish(make_error(storage_error_code::backend_failure, "object extent manifest length overflow"));
      manifest_length += extent.length;
    }
    if (manifest_length != m_object_length) {
      return finish(make_error(storage_error_code::backend_failure,
                               "object extent manifest does not match object content length"));
    }

    if (m_remaining_to_read == 0) return finish_checksum_verification();

    std::uint64_t object_position = 0;
    for (; m_extent_index < m_extents.size(); ++m_extent_index) {
      const physical_extent& extent = m_extents[m_extent_index];
      const std::uint64_t extent_end = object_position + extent.length;
      if (m_read_offset < extent_end) {
        m_extent_offset = m_read_offset - object_position;
        return open_current_extent();
      }
      object_position = extent_end;
    }
    return finish(
        make_error(storage_error_code::backend_failure, "object extent manifest ended before selected range"));
  }

  object_read_result read(std::byte* data, std::size_t size) override
  {
    if (failed(m_terminal_error)) return object_read_result{0, false, m_terminal_error};
    if (m_terminal) return object_read_result{0, true, {}};
    if (size == 0) return {};

    if (!m_read_session.is_open()) {
      const storage_error error = open_current_extent();
      if (failed(error)) return object_read_result{0, false, error};
    }

    const physical_extent& extent = m_extents[m_extent_index];
    const std::uint64_t remaining_in_extent = extent.length - m_extent_offset;
    const std::uint64_t requested_size =
        std::min({m_remaining_to_read, remaining_in_extent, static_cast<std::uint64_t>(size)});
    std::size_t bytes_read = 0;
    storage_error error = m_data_store.read(m_read_session.handle(), m_extent_offset, data,
                                            static_cast<std::size_t>(requested_size), bytes_read);
    if (failed(error)) return object_read_result{0, false, finish(error)};
    if (bytes_read == 0) {
      error = make_error(storage_error_code::backend_failure, "object data ended before expected length");
      return object_read_result{0, false, finish(error)};
    }
    if (bytes_read > requested_size) {
      error = make_error(storage_error_code::backend_failure, "object data backend returned too many bytes");
      return object_read_result{0, false, finish(error)};
    }

    if (m_checksum_hasher) {
      error = m_checksum_hasher->update(data, bytes_read);
      if (failed(error)) return object_read_result{bytes_read, false, finish(error)};
    }

    const std::uint64_t byte_count = static_cast<std::uint64_t>(bytes_read);
    m_extent_offset += byte_count;
    m_remaining_to_read -= byte_count;
    m_operation.advance(byte_count);

    if (m_extent_offset == extent.length) {
      m_read_session.close();
      ++m_extent_index;
      m_extent_offset = 0;
    }

    if (m_remaining_to_read != 0) return object_read_result{bytes_read, false, {}};

    error = finish_checksum_verification();
    return object_read_result{bytes_read, succeeded(error), error};
  }

private:
  storage_error open_current_extent()
  {
    while (m_extent_index < m_extents.size() && m_extent_offset == m_extents[m_extent_index].length) {
      ++m_extent_index;
      m_extent_offset = 0;
    }
    if (m_extent_index >= m_extents.size()) {
      return finish(
          make_error(storage_error_code::backend_failure, "object extent manifest ended before selected range"));
    }

    const storage_error error = m_read_session.open(m_extents[m_extent_index]);
    if (failed(error)) return finish(error);
    return {};
  }

  storage_error finish_checksum_verification()
  {
    if (!m_checksum_hasher) return finish({});

    std::string checksum_value;
    storage_error error = m_checksum_hasher->finish(checksum_value);
    if (succeeded(error) && checksum_value != m_expected_checksum_value) {
      m_registration.reset();
      {
        const std::lock_guard<std::shared_mutex> index_lock{m_index_mutex};
        error = m_index.mark_payload_corrupted(m_payload_id);
        m_object_cache.clear();
      }
      if (succeeded(error))
        error = make_error(storage_error_code::checksum_mismatch, "object checksum does not match stored value");
    }
    return finish(error);
  }

  storage_error finish(storage_error error)
  {
    if (m_terminal) return error;

    m_terminal = true;
    m_terminal_error = error;
    m_read_session.close();
    m_operation.finish(error);
    m_registration.reset();
    m_permit.reset();
    return error;
  }

  object_index& m_index;
  std::shared_mutex& m_index_mutex;
  object_lookup_cache& m_object_cache;
  object_data_store& m_data_store;
  std::uint64_t m_payload_id = 0;
  std::vector<physical_extent> m_extents;
  std::uint64_t m_object_length = 0;
  std::uint64_t m_read_offset = 0;
  std::uint64_t m_remaining_to_read = 0;
  std::size_t m_extent_index = 0;
  std::uint64_t m_extent_offset = 0;
  std::optional<active_object_registry::object_registration> m_registration;
  std::optional<operation_limiter::permit> m_permit;
  operation_tracker m_operation;
  data_read_session m_read_session;
  std::unique_ptr<hasher> m_checksum_hasher;
  std::string m_expected_checksum_value;
  storage_error m_terminal_error;
  bool m_terminal = false;
};

storage_error resolve_byte_range(const std::optional<byte_range>& requested_range, const std::uint64_t object_length,
                                 std::uint64_t& read_offset, std::uint64_t& bytes_to_read,
                                 std::optional<resolved_byte_range>& resolved_range)
{
  read_offset = 0;
  bytes_to_read = object_length;
  resolved_range.reset();

  if (!requested_range.has_value()) return {};

  const byte_range& range = *requested_range;
  if (range.type == byte_range_type::offset_length) {
    read_offset = range.offset;
    bytes_to_read = range.length;
    if (bytes_to_read == 0) {
      if (read_offset > object_length)
        return make_error(storage_error_code::invalid_range, "range offset is outside object");
      resolved_range = resolved_byte_range{read_offset, 0};
      return {};
    }
    if (read_offset >= object_length)
      return make_error(storage_error_code::invalid_range, "range offset is outside object");
    if (bytes_to_read > object_length - read_offset)
      return make_error(storage_error_code::invalid_range, "range exceeds object length");
  } else if (range.type == byte_range_type::offset_to_end) {
    read_offset = range.offset;
    if (read_offset >= object_length)
      return make_error(storage_error_code::invalid_range, "range offset is outside object");
    bytes_to_read = object_length - read_offset;
  } else {
    bytes_to_read = range.length;
    if (bytes_to_read == 0) {
      read_offset = object_length;
      resolved_range = resolved_byte_range{read_offset, 0};
      return {};
    }
    if (bytes_to_read >= object_length) {
      read_offset = 0;
      bytes_to_read = object_length;
    } else {
      read_offset = object_length - bytes_to_read;
    }
  }

  resolved_range = resolved_byte_range{read_offset, bytes_to_read};
  return {};
}

} // namespace
storage_error object_store_core::hash_payload_extents(const std::vector<physical_extent>& extents,
                                                      const checksum_algorithm_name& checksum_algorithm,
                                                      storage_error_code unavailable_code,
                                                      const char* unavailable_message, std::string& value)
{
  value.clear();
  if (checksum_algorithm.value.empty()) return make_error(unavailable_code, unavailable_message);

  storage_error error;
  std::unique_ptr<hasher> payload_hasher = m_hash_factory.create_hasher(checksum_algorithm, error);
  if (failed(error)) return error;
  if (!payload_hasher) return make_error(unavailable_code, unavailable_message);

  std::uint64_t content_length = 0;
  for (const physical_extent& extent : extents)
    content_length += extent.length;

  hashing_object_writer writer{*payload_hasher};
  payload_read_operation operation{m_data_store, extents, writer, 0, content_length};
  error = operation.run();
  if (failed(error)) return error;
  return payload_hasher->finish(value);
}

storage_error object_store_core::verify_payload_integrity(const std::vector<physical_extent>& extents,
                                                          const object_checksum& checksum)
{
  if (checksum.checksum_algorithm.value.empty() || checksum.value.empty())
    return make_error(storage_error_code::index_failure, "payload internal checksum is missing");

  std::string actual_value;
  const storage_error error =
      hash_payload_extents(extents, checksum.checksum_algorithm, storage_error_code::index_failure,
                           "payload internal checksum hasher is unavailable", actual_value);
  if (failed(error)) return error;
  if (actual_value != checksum.value)
    return make_error(storage_error_code::checksum_mismatch, "payload internal checksum does not match stored data");
  return {};
}

storage_error object_store_core::open_object(const bucket_name& bucket, const object_key& key,
                                             open_object_result& result, const open_object_options& options)
{
  result = {};
  operation_tracker operation{m_observer.get(),
                              m_next_operation_id,
                              m_operation_progress_interval_bytes,
                              operation_type::open_object,
                              bucket.value,
                              key.value};
  return open_object_with_tracker(bucket, key, options, result, std::move(operation));
}

storage_error object_store_core::open_object_with_tracker(const bucket_name& bucket, const object_key& key,
                                                          const open_object_options& options,
                                                          open_object_result& result, operation_tracker operation)
{
  const storage_error address_validation_error = validate_object_address(bucket, key);
  if (failed(address_validation_error)) {
    operation.finish(address_validation_error);
    return address_validation_error;
  }
  operation_limiter::permit read_permit = m_read_limiter.try_acquire();
  if (!read_permit.acquired()) {
    const storage_error error =
        make_error(storage_error_code::concurrency_limit_exceeded, "maximum concurrent reads reached");
    operation.finish(error);
    return error;
  }

  // Keep reclamation from reusing extents before registration.
  active_object_registry::object_lookup_guard lookup_guard = m_active_objects.acquire_object_lookup_guard();

  indexed_object object;
  if (options.version_id.has_value() || !m_object_cache.find(bucket, key, object)) {
    const std::shared_lock<std::shared_mutex> index_lock{m_index_mutex};
    const storage_error error = options.version_id.has_value()
                                    ? m_index.find_object_version(bucket, key, *options.version_id, object)
                                    : m_index.find_object(bucket, key, object);
    if (failed(error)) {
      const storage_error lookup_error =
          options.version_id.has_value() && error.code == storage_error_code::object_not_found
              ? make_error(storage_error_code::object_version_not_found, "object version was not found")
              : error;
      operation.finish(lookup_error);
      return lookup_error;
    }
    if (!options.version_id.has_value()) m_object_cache.insert(object);
  }
  result.object = to_object_info(object);
  if (object.is_corrupted) {
    const storage_error error = make_error(storage_error_code::object_corrupted, "object payload is corrupted");
    operation.finish(error);
    return error;
  }
  if (object.is_delete_marker) {
    const storage_error error =
        make_error(storage_error_code::object_is_delete_marker, "requested object version is a delete marker");
    operation.finish(error);
    return error;
  }

  active_object_registry::object_registration active_registration = lookup_guard.register_object(object);

  const storage_error conditions_error = check_read_conditions(object, options.conditions);
  if (failed(conditions_error)) {
    operation.finish(conditions_error);
    return conditions_error;
  }

  const std::uint64_t object_length = object_content_length(object);
  std::uint64_t read_offset = 0;
  std::uint64_t remaining_to_read = object_length;
  std::optional<resolved_byte_range> resolved_range;
  if (options.range.has_value() && options.verify_integrity) {
    const storage_error error =
        make_error(storage_error_code::invalid_range, "integrity verification requires a full-object read");
    operation.finish(error);
    return error;
  }
  const storage_error range_error =
      resolve_byte_range(options.range, object_length, read_offset, remaining_to_read, resolved_range);
  if (failed(range_error)) {
    operation.finish(range_error);
    return range_error;
  }
  operation.set_read_offset(read_offset);
  operation.set_total_bytes(remaining_to_read);

  std::unique_ptr<hasher> checksum_hasher;
  std::string expected_checksum_value;
  if (options.verify_integrity) {
    const object_checksum& verification_checksum = object.payload.internal_checksum;
    storage_error error;
    checksum_hasher = m_hash_factory.create_hasher(verification_checksum.checksum_algorithm, error);
    if (failed(error)) {
      operation.finish(error);
      return error;
    }
    if (!checksum_hasher) {
      error = make_error(storage_error_code::unsupported_checksum_algorithm, "unsupported object checksum algorithm");
      operation.finish(error);
      return error;
    }
    expected_checksum_value = verification_checksum.value;
  }

  std::unique_ptr<opened_object_reader> reader = std::make_unique<opened_object_reader>(
      m_index, m_index_mutex, m_object_cache, m_data_store, object.payload.id, std::move(object.payload.extents),
      object_length, read_offset, remaining_to_read, std::move(active_registration), std::move(read_permit),
      std::move(operation), std::move(checksum_hasher), std::move(expected_checksum_value));
  const storage_error initialization_error = reader->initialize();
  if (failed(initialization_error)) return initialization_error;

  result.byte_range = resolved_range;
  result.reader = std::move(reader);
  return {};
}

storage_error object_store_core::head_object(const bucket_name& bucket, const object_key& key, object_info& info,
                                             const head_object_options& options)
{
  info = {};
  const storage_error address_validation_error = validate_object_address(bucket, key);
  if (failed(address_validation_error)) return address_validation_error;

  indexed_object object;
  if (options.version_id.has_value() || !m_object_cache.find(bucket, key, object)) {
    const std::shared_lock<std::shared_mutex> index_lock{m_index_mutex};
    const storage_error error = options.version_id.has_value()
                                    ? m_index.find_object_version(bucket, key, *options.version_id, object)
                                    : m_index.find_object_metadata(bucket, key, object);
    if (failed(error)) {
      if (options.version_id.has_value() && error.code == storage_error_code::object_not_found)
        return make_error(storage_error_code::object_version_not_found, "object version was not found");
      return error;
    }
  }
  info = to_object_info(object);
  if (object.is_corrupted) return make_error(storage_error_code::object_corrupted, "object payload is corrupted");
  if (object.is_delete_marker)
    return make_error(storage_error_code::object_is_delete_marker, "requested object version is a delete marker");

  const storage_error conditions_error = check_read_conditions(object, options.conditions);
  if (failed(conditions_error)) return conditions_error;
  return {};
}

storage_error object_store_core::list_object_parts(const bucket_name& bucket, const object_key& key,
                                                   object_part_list& result, const list_object_parts_options& options)
{
  result = {};
  const storage_error address_validation_error = validate_object_address(bucket, key);
  if (failed(address_validation_error)) return address_validation_error;

  const std::size_t max_parts = (std::min)(options.max_parts, max_list_page_size);
  const std::shared_lock<std::shared_mutex> index_lock{m_index_mutex};
  indexed_object object;
  storage_error error = options.version_id.has_value()
                            ? m_index.find_object_version(bucket, key, *options.version_id, object)
                            : m_index.find_object_metadata(bucket, key, object);
  if (failed(error)) {
    if (options.version_id.has_value() && error.code == storage_error_code::object_not_found)
      return make_error(storage_error_code::object_version_not_found, "object version was not found");
    return error;
  }

  result.object = to_object_info(object);
  if (object.is_corrupted) return make_error(storage_error_code::object_corrupted, "object payload is corrupted");
  if (object.is_delete_marker)
    return make_error(storage_error_code::object_is_delete_marker, "requested object version is a delete marker");

  error = check_read_conditions(object, options.conditions);
  if (failed(error)) return error;

  indexed_object_part_page page;
  error = m_index.list_object_parts(object, options.part_number_marker, max_parts, page);
  if (failed(error)) return error;

  result.parts = std::move(page.parts);
  result.total_parts = page.total_parts;
  result.next_part_number_marker = page.next_part_number_marker;
  result.is_truncated = page.is_truncated;
  return {};
}

} // namespace extora::core
