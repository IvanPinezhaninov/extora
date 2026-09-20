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

#ifndef EXTORA_CORE_XXHASH_HASHER_FACTORY_H
#define EXTORA_CORE_XXHASH_HASHER_FACTORY_H

#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <extora/checksum.h>
#include <extora/storage_error.h>

namespace extora::core {

class xxhash_hasher_factory final : public hasher_factory {
public:
  std::unique_ptr<hasher> create_hasher(const checksum_algorithm_name& checksum_algorithm,
                                        storage_error& error) override;
  bool can_combine_checksums(const checksum_algorithm_name& checksum_algorithm) const override;
  storage_error combine_checksums(const checksum_algorithm_name& checksum_algorithm,
                                  const std::vector<std::string_view>& part_checksums, std::string& value) override;
};

// Uses built-in algorithms first, then the optional extension factory.
class composite_hasher_factory final : public hasher_factory {
public:
  explicit composite_hasher_factory(std::shared_ptr<hasher_factory> extension_factory);

  std::unique_ptr<hasher> create_hasher(const checksum_algorithm_name& checksum_algorithm,
                                        storage_error& error) override;
  bool can_combine_checksums(const checksum_algorithm_name& checksum_algorithm) const override;
  storage_error combine_checksums(const checksum_algorithm_name& checksum_algorithm,
                                  const std::vector<std::string_view>& part_checksums, std::string& value) override;

private:
  xxhash_hasher_factory m_builtin_factory;
  std::shared_ptr<hasher_factory> m_extension_factory;
};

} // namespace extora::core

#endif // EXTORA_CORE_XXHASH_HASHER_FACTORY_H
