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

#ifndef EXTORA_CORE_PAYLOAD_HASHES_H
#define EXTORA_CORE_PAYLOAD_HASHES_H

#include <array>
#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include <extora/checksum.h>
#include <extora/storage_error.h>

namespace extora::core {

struct payload_hash_result {
  std::string etag;
  object_checksum internal_checksum;
  object_checksum checksum;
};

class payload_hashes {
public:
  explicit payload_hashes(hasher_factory& hash_factory);

  storage_error configure(std::optional<object_checksum> expected_checksum,
                          const checksum_algorithm_name& public_checksum_algorithm = {});
  storage_error update(const std::byte* data, std::size_t size);
  storage_error finish(payload_hash_result& result);

private:
  struct hash_entry {
    checksum_algorithm_name checksum_algorithm;
    std::unique_ptr<hasher> instance;
    std::string value;
  };

  storage_error add_hash(const checksum_algorithm_name& checksum_algorithm, storage_error_code unavailable_code,
                         const char* unavailable_message, hash_entry*& entry);
  hash_entry* find_hash(const checksum_algorithm_name& checksum_algorithm);

  hasher_factory& m_hash_factory;
  std::optional<object_checksum> m_expected_checksum;
  std::array<hash_entry, 2> m_entries;
  std::size_t m_entry_count = 0;
  hash_entry* m_internal_checksum = nullptr;
  hash_entry* m_checksum = nullptr;
};

} // namespace extora::core

#endif // EXTORA_CORE_PAYLOAD_HASHES_H
