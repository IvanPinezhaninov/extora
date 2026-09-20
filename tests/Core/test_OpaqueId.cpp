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
#include <cctype>
#include <chrono>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_set>
#include <vector>

#include <gtest/gtest.h>

#include "extora/core/opaque_id.h"

TEST(OpaqueIdTest, UsesStableOpaqueFormat)
{
  const std::string id = extora::core::generate_opaque_id(extora::core::object_version_id_prefix);

  ASSERT_EQ(id.size(), 34u);
  EXPECT_EQ(id.substr(0, 2), "v_");
  EXPECT_TRUE(std::all_of(id.begin() + 2, id.end(), [](const char value) {
    return std::isdigit(static_cast<unsigned char>(value)) != 0 || (value >= 'a' && value <= 'f');
  }));
}

TEST(OpaqueIdTest, GeneratesUniqueIdsConcurrently)
{
  constexpr std::size_t threadCount = 8;
  static constexpr std::size_t idsPerThread = 1000;
  std::vector<std::string> ids;
  ids.reserve(threadCount * idsPerThread);
  std::mutex idsMutex;
  std::vector<std::thread> threads;
  threads.reserve(threadCount);

  for (std::size_t thread = 0; thread < threadCount; ++thread) {
    threads.emplace_back([&ids, &idsMutex]() {
      std::vector<std::string> localIds;
      localIds.reserve(idsPerThread);
      for (std::size_t index = 0; index < idsPerThread; ++index) {
        localIds.push_back(extora::core::generate_opaque_id(extora::core::write_operation_id_prefix));
      }

      const std::lock_guard<std::mutex> lock{idsMutex};
      ids.insert(ids.end(), localIds.begin(), localIds.end());
    });
  }
  for (std::thread& thread : threads)
    thread.join();

  const std::unordered_set<std::string> uniqueIds{ids.begin(), ids.end()};
  EXPECT_EQ(uniqueIds.size(), threadCount * idsPerThread);
}

TEST(OpaqueIdTest, GeneratesIdsOrderedByTimestamp)
{
  const auto firstTime = std::chrono::system_clock::time_point{std::chrono::milliseconds{100}};
  const auto secondTime = std::chrono::system_clock::time_point{std::chrono::milliseconds{101}};
  const std::string first =
      extora::core::generate_ordered_opaque_id(extora::core::multipart_upload_id_prefix, firstTime);
  const std::string second =
      extora::core::generate_ordered_opaque_id(extora::core::multipart_upload_id_prefix, secondTime);

  EXPECT_LT(first, second);
}

TEST(OpaqueIdTest, EncodesAndValidatesListingTokens)
{
  const std::string marker = "photos/2026/\xD1\x84\xD0\xBE\xD1\x82\xD0\xBE";
  const std::string token = extora::core::encode_listing_token(extora::core::object_listing_token_prefix, marker);
  std::string decoded;

  EXPECT_NE(token, marker);
  EXPECT_TRUE(extora::core::decode_listing_token(token, extora::core::object_listing_token_prefix, decoded));
  EXPECT_EQ(decoded, marker);
  EXPECT_FALSE(extora::core::decode_listing_token(token, extora::core::bucket_listing_token_prefix, decoded));
  EXPECT_FALSE(extora::core::decode_listing_token("o_not-hex", extora::core::object_listing_token_prefix, decoded));
  EXPECT_FALSE(extora::core::decode_listing_token("o_gg", extora::core::object_listing_token_prefix, decoded));
  EXPECT_TRUE(decoded.empty());
}
