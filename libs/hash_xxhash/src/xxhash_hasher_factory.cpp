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

#include "extora/core/xxhash_hasher_factory.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <xxhash.h>

namespace extora::core {

namespace {

constexpr char hexadecimal_digits[] = "0123456789abcdef";

int hexadecimal_value(char value)
{
  if (value >= '0' && value <= '9') return value - '0';
  if (value >= 'a' && value <= 'f') return value - 'a' + 10;
  if (value >= 'A' && value <= 'F') return value - 'A' + 10;
  return -1;
}

class xxh3_128_hasher final : public hasher {
public:
  using state_deleter = XXH_errorcode (*)(XXH3_state_t*);
  using state_pointer = std::unique_ptr<XXH3_state_t, state_deleter>;

  explicit xxh3_128_hasher(state_pointer state)
    : m_state{std::move(state)}
  {}

  storage_error reset()
  {
    if (XXH3_128bits_reset(m_state.get()) == XXH_ERROR)
      return make_error(storage_error_code::backend_failure, "failed to initialize xxh3-128 hasher");
    return {};
  }

  storage_error update(const std::byte* data, std::size_t size) override
  {
    if (m_finished) return make_error(storage_error_code::backend_failure, "xxh3-128 hasher is already finalized");
    if (size == 0) return {};
    if (data == nullptr) return make_error(storage_error_code::backend_failure, "xxh3-128 input is null");

    if (XXH3_128bits_update(m_state.get(), data, size) == XXH_ERROR)
      return make_error(storage_error_code::backend_failure, "failed to update xxh3-128 hasher");
    return {};
  }

  storage_error finish(std::string& value) override
  {
    if (m_finished) return make_error(storage_error_code::backend_failure, "xxh3-128 hasher is already finalized");

    XXH128_canonical_t canonical;
    XXH128_canonicalFromHash(&canonical, XXH3_128bits_digest(m_state.get()));

    value.resize(sizeof(canonical.digest) * 2);
    for (std::size_t index = 0; index < sizeof(canonical.digest); ++index) {
      const std::uint8_t byte = canonical.digest[index];
      value[index * 2] = hexadecimal_digits[byte >> 4];
      value[index * 2 + 1] = hexadecimal_digits[byte & 0x0f];
    }
    m_finished = true;
    return {};
  }

private:
  state_pointer m_state;
  bool m_finished = false;
};

} // namespace

std::unique_ptr<hasher> xxhash_hasher_factory::create_hasher(const checksum_algorithm_name& checksum_algorithm,
                                                             storage_error& error)
{
  error = {};
  if (checksum_algorithm.value != xxh3_128_checksum_algorithm.value) {
    error = make_error(storage_error_code::unsupported_checksum_algorithm, "unsupported built-in hash algorithm");
    return {};
  }

  xxh3_128_hasher::state_pointer state{XXH3_createState(), &XXH3_freeState};
  if (!state) {
    error = make_error(storage_error_code::backend_failure, "failed to allocate xxh3-128 hasher");
    return {};
  }

  std::unique_ptr<xxh3_128_hasher> result = std::make_unique<xxh3_128_hasher>(std::move(state));
  error = result->reset();
  if (failed(error)) return {};
  return result;
}

bool xxhash_hasher_factory::can_combine_checksums(const checksum_algorithm_name& checksum_algorithm) const
{
  return checksum_algorithm.value == xxh3_128_checksum_algorithm.value;
}

storage_error xxhash_hasher_factory::combine_checksums(const checksum_algorithm_name& checksum_algorithm,
                                                       const std::vector<std::string_view>& part_checksums,
                                                       std::string& value)
{
  value.clear();
  storage_error error;
  std::unique_ptr<hasher> composite = create_hasher(checksum_algorithm, error);
  if (failed(error)) return error;
  if (!composite) {
    return make_error(storage_error_code::unsupported_checksum_type,
                      "composite checksum is unsupported for the selected algorithm");
  }

  std::array<std::byte, 16> decoded;
  for (const std::string_view part_checksum : part_checksums) {
    if (part_checksum.size() != decoded.size() * 2)
      return make_error(storage_error_code::checksum_mismatch, "invalid xxh3-128 part checksum encoding");
    for (std::size_t index = 0; index < decoded.size(); ++index) {
      const int high = hexadecimal_value(part_checksum[index * 2]);
      const int low = hexadecimal_value(part_checksum[index * 2 + 1]);
      if (high < 0 || low < 0)
        return make_error(storage_error_code::checksum_mismatch, "invalid xxh3-128 part checksum encoding");
      decoded[index] = static_cast<std::byte>((high << 4) | low);
    }
    error = composite->update(decoded.data(), decoded.size());
    if (failed(error)) return error;
  }
  error = composite->finish(value);
  if (!failed(error)) {
    value += '-';
    value += std::to_string(part_checksums.size());
  }
  return error;
}

composite_hasher_factory::composite_hasher_factory(std::shared_ptr<hasher_factory> extension_factory)
  : m_extension_factory{std::move(extension_factory)}
{}

std::unique_ptr<hasher> composite_hasher_factory::create_hasher(const checksum_algorithm_name& checksum_algorithm,
                                                                storage_error& error)
{
  if (checksum_algorithm.value == xxh3_128_checksum_algorithm.value)
    return m_builtin_factory.create_hasher(checksum_algorithm, error);

  if (m_extension_factory != nullptr) return m_extension_factory->create_hasher(checksum_algorithm, error);

  error = make_error(storage_error_code::unsupported_checksum_algorithm, "unsupported hash algorithm");
  return {};
}

bool composite_hasher_factory::can_combine_checksums(const checksum_algorithm_name& checksum_algorithm) const
{
  if (checksum_algorithm.value == xxh3_128_checksum_algorithm.value) return true;
  return m_extension_factory != nullptr && m_extension_factory->can_combine_checksums(checksum_algorithm);
}

storage_error composite_hasher_factory::combine_checksums(const checksum_algorithm_name& checksum_algorithm,
                                                          const std::vector<std::string_view>& part_checksums,
                                                          std::string& value)
{
  if (checksum_algorithm.value == xxh3_128_checksum_algorithm.value)
    return m_builtin_factory.combine_checksums(checksum_algorithm, part_checksums, value);
  if (m_extension_factory != nullptr)
    return m_extension_factory->combine_checksums(checksum_algorithm, part_checksums, value);
  value.clear();
  return make_error(storage_error_code::unsupported_checksum_type,
                    "composite checksum is unsupported for the selected algorithm");
}

} // namespace extora::core
