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
** OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
** USE OR OTHER DEALINGS IN THE SOFTWARE.
**
******************************************************************************/

#include "CoreTestSupport.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

#include "PublicTestSupport.h"

namespace extoraTest {

extora::hasher_factory& defaultCoreHasherFactory()
{
  static extora::core::composite_hasher_factory factory{nullptr};
  return factory;
}

std::string testChecksumValue(std::string_view text)
{
  std::uint64_t value = 0;
  for (const char ch : text)
    value += static_cast<unsigned char>(ch);

  return std::to_string(value);
}

extora::storage_error TestHasher::update(const std::byte* data, std::size_t size)
{
  for (std::size_t i = 0; i < size; ++i)
    m_value += static_cast<unsigned char>(data[i]);

  return {};
}

extora::storage_error TestHasher::finish(std::string& value)
{
  value = std::to_string(m_value);
  return {};
}

std::unique_ptr<extora::hasher>
TestHasherFactory::create_hasher(const extora::checksum_algorithm_name& checksumAlgorithm, extora::storage_error& error)
{
  error = {};
  if (checksumAlgorithm.value != "test-sum") return {};

  return std::make_unique<TestHasher>();
}

void StoreCoreTest::SetUp()
{
  m_root = makeTempRoot();
  ASSERT_FALSE(m_root.empty());

  m_index.emplace(joinPath(m_root, "index.sqlite3"), 1024 * 1024);
  ASSERT_TRUE(succeeded(m_index->open()));

  m_dataStore.emplace(m_root, 1024 * 1024);
  ASSERT_TRUE(succeeded(m_dataStore->open()));

  m_core.emplace(*m_index, *m_dataStore, m_hasherFactory);
  ASSERT_TRUE(succeeded(m_core->create_bucket(extora::bucket_name{"photos"})));
}

} // namespace extoraTest
