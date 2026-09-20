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

#ifndef EXTORA_CHECKSUM_H
#define EXTORA_CHECKSUM_H

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <extora/storage_error.h>

namespace extora {

/** @brief Opaque name of a built-in or custom checksum algorithm. */
struct checksum_algorithm_name {
  /** @brief Algorithm identifier passed unchanged to @ref hasher_factory. */
  std::string value;
};

/** @brief Built-in XXH3-128 checksum algorithm name. */
inline const checksum_algorithm_name xxh3_128_checksum_algorithm{"xxh3-128"};

/** @brief How a stored object checksum was calculated. */
enum class object_checksum_type : std::uint8_t {
  full_object = 1, ///< Checksum of all object bytes in order.
  composite = 2    ///< Algorithm-specific composition of ordered part checksums.
};

/** @brief Object checksum. */
struct object_checksum {
  /** @brief Algorithm used to calculate @ref value. */
  checksum_algorithm_name checksum_algorithm;

  /** @brief Algorithm-specific encoded value. */
  std::string value;

  /** @brief @ref object_checksum_type value. */
  object_checksum_type type = object_checksum_type::full_object;
};

/** @brief Incremental object data hasher. */
class hasher {
public:
  /** @brief Destroys the hasher. */
  virtual ~hasher() = default;

  /** @brief Adds bytes to the hash. */
  virtual storage_error update(const std::byte* data, std::size_t size) = 0;

  /** @brief Finishes the hash and returns its encoded value. */
  virtual storage_error finish(std::string& value) = 0;
};

/**
 * @brief Factory for custom checksum hashers.
 *
 * XXH3-128 is built in and takes priority. The factory must be thread-safe.
 * Each returned @ref hasher is used by one operation. Custom algorithms are
 * used for expected upload checksums, multipart contracts, and requested copy
 * checksums. Internal payload integrity always uses built-in XXH3-128.
 */
class hasher_factory {
public:
  /** @brief Destroys the hasher factory. */
  virtual ~hasher_factory() = default;

  /**
   * @brief Creates a hasher.
   *
   * On failure, returns nullptr and sets @p error. An unsupported algorithm
   * uses @ref storage_error_code::unsupported_checksum_algorithm.
   */
  [[nodiscard]] virtual std::unique_ptr<hasher> create_hasher(const checksum_algorithm_name& checksum_algorithm,
                                                              storage_error& error) = 0;

  /** @brief Reports whether @ref combine_checksums supports an algorithm. */
  [[nodiscard]] virtual bool can_combine_checksums(const checksum_algorithm_name& checksum_algorithm) const
  {
    static_cast<void>(checksum_algorithm);
    return false;
  }

  /** @brief Combines ordered part checksum values. */
  virtual storage_error combine_checksums(const checksum_algorithm_name& checksum_algorithm,
                                          const std::vector<std::string_view>& part_checksums, std::string& value)
  {
    static_cast<void>(checksum_algorithm);
    static_cast<void>(part_checksums);
    value.clear();
    return make_error(storage_error_code::unsupported_checksum_type,
                      "composite checksum is unsupported for the selected algorithm");
  }
};

} // namespace extora

#endif // EXTORA_CHECKSUM_H
