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

#include "payload_hashes.h"

#include <utility>

namespace extora::core {

payload_hashes::payload_hashes(hasher_factory& hash_factory)
  : m_hash_factory{hash_factory}
{}

storage_error payload_hashes::configure(std::optional<object_checksum> expected_checksum,
                                        const checksum_algorithm_name& public_checksum_algorithm)
{
  m_expected_checksum = std::move(expected_checksum);
  m_entries = {};
  m_entry_count = 0;
  m_internal_checksum = nullptr;
  m_checksum = nullptr;

  storage_error error = add_hash(xxh3_128_checksum_algorithm, storage_error_code::backend_failure,
                                 "internal checksum hasher is unavailable", m_internal_checksum);
  if (failed(error)) return error;

  if (m_expected_checksum.has_value()) {
    if (m_expected_checksum->type != object_checksum_type::full_object) {
      return make_error(storage_error_code::unsupported_checksum_type,
                        "streamed payload checksum must be a full-object checksum");
    }
    if (m_expected_checksum->checksum_algorithm.value.empty())
      return make_error(storage_error_code::unsupported_checksum_algorithm, "checksum algorithm is empty");
    if (!public_checksum_algorithm.value.empty() &&
        m_expected_checksum->checksum_algorithm.value != public_checksum_algorithm.value) {
      return make_error(storage_error_code::unsupported_checksum_algorithm,
                        "checksum algorithm does not match the requested algorithm");
    }
    error = add_hash(m_expected_checksum->checksum_algorithm, storage_error_code::unsupported_checksum_algorithm,
                     "unsupported checksum algorithm", m_checksum);
    if (failed(error)) return error;
  } else if (!public_checksum_algorithm.value.empty()) {
    error = add_hash(public_checksum_algorithm, storage_error_code::unsupported_checksum_algorithm,
                     "unsupported checksum algorithm", m_checksum);
    if (failed(error)) return error;
  }

  return {};
}

storage_error payload_hashes::update(const std::byte* data, std::size_t size)
{
  for (std::size_t index = 0; index < m_entry_count; ++index) {
    const storage_error error = m_entries[index].instance->update(data, size);
    if (failed(error)) return error;
  }
  return {};
}

storage_error payload_hashes::finish(payload_hash_result& result)
{
  result = {};
  for (std::size_t index = 0; index < m_entry_count; ++index) {
    const storage_error error = m_entries[index].instance->finish(m_entries[index].value);
    if (failed(error)) return error;
  }

  if (m_expected_checksum.has_value() && m_checksum->value != m_expected_checksum->value)
    return make_error(storage_error_code::checksum_mismatch, "checksum does not match bytes read");

  result.etag = m_internal_checksum->checksum_algorithm.value + ":" + m_internal_checksum->value;
  result.internal_checksum = object_checksum{m_internal_checksum->checksum_algorithm, m_internal_checksum->value};
  const hash_entry& public_checksum = m_checksum == nullptr ? *m_internal_checksum : *m_checksum;
  result.checksum = object_checksum{public_checksum.checksum_algorithm, public_checksum.value};
  return {};
}

storage_error payload_hashes::add_hash(const checksum_algorithm_name& checksum_algorithm,
                                       storage_error_code unavailable_code, const char* unavailable_message,
                                       hash_entry*& entry)
{
  entry = find_hash(checksum_algorithm);
  if (entry != nullptr) return {};

  storage_error error;
  std::unique_ptr<hasher> instance = m_hash_factory.create_hasher(checksum_algorithm, error);
  if (failed(error)) return error;
  if (!instance) return make_error(unavailable_code, unavailable_message);

  entry = &m_entries[m_entry_count++];
  entry->checksum_algorithm = checksum_algorithm;
  entry->instance = std::move(instance);
  return {};
}

payload_hashes::hash_entry* payload_hashes::find_hash(const checksum_algorithm_name& checksum_algorithm)
{
  for (std::size_t index = 0; index < m_entry_count; ++index)
    if (m_entries[index].checksum_algorithm.value == checksum_algorithm.value) return &m_entries[index];
  return nullptr;
}

} // namespace extora::core
