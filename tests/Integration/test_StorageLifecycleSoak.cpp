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

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <iterator>
#include <limits>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#if defined(__linux__)
#include <unistd.h>
#endif // defined(__linux__)

#include <gtest/gtest.h>

#include "PublicTestSupport.h"

namespace extoraTest {

namespace {

constexpr std::size_t defaultSoakIterations = 1000;
constexpr std::size_t keyCount = 64;

std::size_t soakIterations()
{
  const char* value = std::getenv("EXTORA_SOAK_ITERATIONS");
  if (value == nullptr || *value == '\0') return defaultSoakIterations;

  char* end = nullptr;
  const unsigned long long parsed = std::strtoull(value, &end, 10);
  if (end == value || *end != '\0' || parsed == 0 || parsed > std::numeric_limits<std::size_t>::max())
    return defaultSoakIterations;

  return static_cast<std::size_t>(parsed);
}

std::string objectKey(std::size_t index)
{
  return "object-" + std::to_string(index);
}

std::vector<std::byte> makePayload(std::size_t iteration)
{
  constexpr std::size_t sizes[] = {0, 1, 4096, 64 * 1024};
  std::vector<std::byte> payload(sizes[iteration % 4]);
  for (std::size_t index = 0; index < payload.size(); ++index)
    payload[index] = static_cast<std::byte>((iteration + index * 31) % 251);
  return payload;
}

std::uint64_t segmentLogicalBytes(const std::filesystem::path& root)
{
  std::uint64_t bytes = 0;
  const std::filesystem::path segments = root / "segments";
  if (!std::filesystem::exists(segments)) return bytes;

  for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator{segments}) {
    if (entry.is_regular_file()) bytes += static_cast<std::uint64_t>(entry.file_size());
  }
  return bytes;
}

#if defined(__linux__)
std::size_t openFileDescriptorCount()
{
  return static_cast<std::size_t>(
      std::distance(std::filesystem::directory_iterator{"/proc/self/fd"}, std::filesystem::directory_iterator{}));
}

std::uint64_t residentMemoryBytes()
{
  std::FILE* file = std::fopen("/proc/self/statm", "r");
  if (file == nullptr) return 0;

  unsigned long long totalPages = 0;
  unsigned long long residentPages = 0;
  const bool parsed = std::fscanf(file, "%llu %llu", &totalPages, &residentPages) == 2;
  std::fclose(file);
  if (!parsed) return 0;

  const long pageSize = ::sysconf(_SC_PAGESIZE);
  if (pageSize <= 0) return 0;
  return residentPages * static_cast<std::uint64_t>(pageSize);
}
#endif // defined(__linux__)

class StorageLifecycleSoakTest : public testing::Test {
protected:
  void SetUp() override
  {
    m_options.root_directory = makeTempRoot();
    m_options.segment_capacity = 1024 * 1024;
    m_options.max_extent_size = 64 * 1024;
    m_options.durability = extora::storage_durability::balanced;
    const extora::storage_error error = openStore();
    ASSERT_TRUE(succeeded(error)) << error.message;
    ASSERT_TRUE(m_store);
    ASSERT_TRUE(succeeded(m_store->create_bucket(extora::bucket_name{"soak"})));
  }

  extora::storage_error openStore()
  {
    extora::storage_error error;
    m_store = extora::open_object_store(m_options, error);
    if (failed(error)) return error;
    if (!m_store)
      return extora::make_error(extora::storage_error_code::backend_failure, "store factory returned no instance");
    return {};
  }

  extora::storage_error reopenStore()
  {
    m_store.reset();
    return openStore();
  }

  void verifyObject(const std::string& key, const std::vector<std::byte>& expected)
  {
    VectorWriter writer;
    extora::open_object_result result;
    const extora::storage_error error = readObject(*m_store, extora::bucket_name{"soak"}, extora::object_key{key},
                                                   writer, extora::open_object_options{}, result);
    ASSERT_TRUE(succeeded(error)) << error.message;
    EXPECT_EQ(writer.bytes(), expected);
    EXPECT_EQ(result.object.content_length, expected.size());
  }

  extora::object_store_options m_options;
  std::unique_ptr<extora::managed_object_store> m_store;
};

TEST_F(StorageLifecycleSoakTest, SurvivesRepeatedMutationReclamationAndReopen)
{
#if defined(__linux__)
  std::size_t warmedFileDescriptors = 0;
  std::uint64_t warmedResidentMemory = 0;
#endif // defined(__linux__)
  std::unordered_map<std::string, std::vector<std::byte>> expected;
  const std::size_t iterations = soakIterations();

  for (std::size_t iteration = 0; iteration < iterations; ++iteration) {
    const std::string key = objectKey(iteration % keyCount);
    std::vector<std::byte> payload = makePayload(iteration);
    VectorReader reader{payload, 4096};
    extora::object_metadata metadata;
    extora::put_object_options options;
    options.expected_content_length = payload.size();
    extora::put_object_result result;
    const extora::storage_error putError =
        m_store->put_object(extora::bucket_name{"soak"}, extora::object_key{key}, reader, metadata, result, options);
    ASSERT_TRUE(succeeded(putError)) << "iteration " << iteration << ": " << putError.message;

    expected[key] = std::move(payload);
    verifyObject(key, expected.at(key));

    if (iteration % 5 == 0) {
      const std::string deletedKey = objectKey((iteration + keyCount / 2) % keyCount);
      const auto found = expected.find(deletedKey);
      if (found != expected.end()) {
        extora::delete_object_result deleteResult;
        const extora::storage_error deleteError =
            m_store->delete_object(extora::bucket_name{"soak"}, extora::object_key{deletedKey}, deleteResult);
        ASSERT_TRUE(succeeded(deleteError)) << "iteration " << iteration << ": " << deleteError.message;
        expected.erase(found);
      }
    }

    if (iteration % 97 == 0) {
      extora::reclamation_estimate estimate;
      ASSERT_TRUE(succeeded(m_store->get_reclamation_estimate(estimate)));
      ASSERT_TRUE(succeeded(m_store->reclaim_storage(ignoredReclaimResult())));
    }

    if (iteration > 0 && iteration % 250 == 0) {
      const extora::storage_error reopenError = reopenStore();
      ASSERT_TRUE(succeeded(reopenError)) << reopenError.message;
      for (const auto& entry : expected)
        verifyObject(entry.first, entry.second);
#if defined(__linux__)
      if (warmedFileDescriptors == 0) {
        warmedFileDescriptors = openFileDescriptorCount();
        warmedResidentMemory = residentMemoryBytes();
      }
#endif // defined(__linux__)
    }
  }

  const extora::storage_error reopenError = reopenStore();
  ASSERT_TRUE(succeeded(reopenError)) << reopenError.message;
  for (const auto& entry : expected)
    verifyObject(entry.first, entry.second);

  extora::list_objects_options listOptions;
  listOptions.max_keys = keyCount + 1;
  extora::object_list listed;
  ASSERT_TRUE(succeeded(m_store->list_objects(extora::bucket_name{"soak"}, listed, listOptions)));
  ASSERT_FALSE(listed.is_truncated);
  ASSERT_EQ(listed.objects.size(), expected.size());
  for (const extora::object_info& object : listed.objects)
    EXPECT_NE(expected.find(object.key.value), expected.end());

  ASSERT_TRUE(succeeded(m_store->reclaim_storage(ignoredReclaimResult())));
  EXPECT_LE(segmentLogicalBytes(m_options.root_directory), 16 * m_options.segment_capacity);

#if defined(__linux__)
  if (warmedFileDescriptors > 0) {
    EXPECT_LE(openFileDescriptorCount(), warmedFileDescriptors + 2);
  }
  const std::uint64_t finalResidentMemory = residentMemoryBytes();
  if (warmedResidentMemory > 0 && finalResidentMemory > 0) {
    EXPECT_LE(finalResidentMemory, warmedResidentMemory + 64 * 1024 * 1024);
  }
#endif // defined(__linux__)
}

} // namespace

} // namespace extoraTest
