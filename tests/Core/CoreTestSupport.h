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

#ifndef EXTORA_TEST_CORE_TEST_SUPPORT_H
#define EXTORA_TEST_CORE_TEST_SUPPORT_H

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include <gtest/gtest.h>

#include <extora/core/object_store_core.h>
#include <extora/core/segment_data_store.h>
#include <extora/core/sqlite_object_index.h>
#include <extora/core/xxhash_hasher_factory.h>

namespace extoraTest {

std::string testChecksumValue(std::string_view text);
extora::hasher_factory& defaultCoreHasherFactory();

class TestHasher final : public extora::hasher {
public:
  extora::storage_error update(const std::byte* data, std::size_t size) override;
  extora::storage_error finish(std::string& value) override;

private:
  std::uint64_t m_value = 0;
};

class TestHasherFactory final : public extora::hasher_factory {
public:
  std::unique_ptr<extora::hasher> create_hasher(const extora::checksum_algorithm_name& checksumAlgorithm,
                                                extora::storage_error& error) override;
};

class StoreCoreTest : public testing::Test {
protected:
  void SetUp() override;

  std::optional<extora::core::sqlite_object_index> m_index;
  std::optional<extora::core::segment_data_store> m_dataStore;
  std::shared_ptr<TestHasherFactory> m_extensionHasherFactory = std::make_shared<TestHasherFactory>();
  extora::core::composite_hasher_factory m_hasherFactory{m_extensionHasherFactory};
  std::optional<extora::core::object_store_core> m_core;
  std::string m_root;
};

} // namespace extoraTest

#endif // EXTORA_TEST_CORE_TEST_SUPPORT_H
