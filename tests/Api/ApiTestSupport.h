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

#ifndef EXTORA_TEST_API_TEST_SUPPORT_H
#define EXTORA_TEST_API_TEST_SUPPORT_H

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <gtest/gtest.h>

#include <extora/extora.h>

namespace extoraTest {

std::string apiChecksumValue(std::string_view text);

class ApiHasher final : public extora::hasher {
public:
  extora::storage_error update(const std::byte* data, std::size_t size) override;
  extora::storage_error finish(std::string& value) override;

private:
  std::uint64_t m_value = 0;
};

class ApiHasherFactory final : public extora::hasher_factory {
public:
  std::unique_ptr<extora::hasher> create_hasher(const extora::checksum_algorithm_name& checksumAlgorithm,
                                                extora::storage_error& error) override;
  bool can_combine_checksums(const extora::checksum_algorithm_name& checksumAlgorithm) const override;
  extora::storage_error combine_checksums(const extora::checksum_algorithm_name& checksumAlgorithm,
                                          const std::vector<std::string_view>& partChecksums,
                                          std::string& value) override;
};

class FailingReader final : public extora::object_reader {
public:
  extora::object_read_result read(std::byte*, std::size_t) override;
};

class FailingWriter final : public extora::object_writer {
public:
  extora::storage_error write(const std::byte*, std::size_t) override;
};

extora::storage_error putText(extora::object_store& store, std::string_view bucket, std::string_view key,
                              std::string_view text, const extora::object_metadata& metadata = {},
                              const extora::put_object_options& options = {},
                              extora::put_object_result* result = nullptr);

extora::storage_error readText(extora::object_store& store, std::string_view bucket, std::string_view key,
                               std::string& text, const extora::open_object_options& options = {},
                               extora::open_object_result* result = nullptr);

class ObjectStoreApiTest : public testing::Test {
protected:
  void SetUp() override;

  std::shared_ptr<ApiHasherFactory> m_hasherFactory = std::make_shared<ApiHasherFactory>();
  std::unique_ptr<extora::managed_object_store> m_store;
  std::string m_root;
};

} // namespace extoraTest

#endif // EXTORA_TEST_API_TEST_SUPPORT_H
