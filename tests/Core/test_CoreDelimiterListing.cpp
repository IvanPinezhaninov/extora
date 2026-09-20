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

#include <string>
#include <string_view>

#include "CoreTestSupport.h"
#include "PublicTestSupport.h"

namespace extoraTest {

namespace {

extora::storage_error putText(extora::object_store& store, std::string_view bucket, std::string_view key,
                              std::string_view text, const extora::put_object_options& options = {})
{
  VectorReader reader{bytesFromString(text), 7};
  extora::object_metadata metadata;
  metadata.content_type = "text/plain";
  extora::put_object_options effectiveOptions = options;
  effectiveOptions.expected_content_length = text.size();
  return store.put_object(extora::bucket_name{std::string{bucket}}, extora::object_key{std::string{key}}, reader,
                          metadata, ignoredPutResult(), effectiveOptions);
}

TEST_F(StoreCoreTest, ListsObjectsAndCommonPrefixesWithDelimiter)
{
  for (std::string_view key : {"a/1.txt", "a/2.txt", "a/b/3.txt", "a/b/4.txt", "a/c/5.txt", "z.txt"})
    ASSERT_TRUE(succeeded(putText(*m_core, "photos", key, key)));

  extora::list_objects_options options;
  options.prefix = "a/";
  options.delimiter = "/";
  extora::object_list result;
  ASSERT_TRUE(succeeded(m_core->list_objects(extora::bucket_name{"photos"}, result, options)));
  ASSERT_EQ(result.objects.size(), 2);
  EXPECT_EQ(result.objects[0].key.value, "a/1.txt");
  EXPECT_EQ(result.objects[1].key.value, "a/2.txt");
  EXPECT_EQ(result.common_prefixes, (std::vector<std::string>{"a/b/", "a/c/"}));
}

TEST_F(StoreCoreTest, PaginatesObjectsAndCommonPrefixesTogether)
{
  for (std::string_view key : {"a/1", "a/b/1", "a/b/2", "a/c/1"})
    ASSERT_TRUE(succeeded(putText(*m_core, "photos", key, key)));

  extora::list_objects_options firstOptions;
  firstOptions.prefix = "a/";
  firstOptions.delimiter = "/";
  firstOptions.max_keys = 2;
  extora::object_list first;
  ASSERT_TRUE(succeeded(m_core->list_objects(extora::bucket_name{"photos"}, first, firstOptions)));
  ASSERT_EQ(first.objects.size(), 1);
  EXPECT_EQ(first.common_prefixes, (std::vector<std::string>{"a/b/"}));
  ASSERT_TRUE(first.is_truncated);
  ASSERT_TRUE(first.next_continuation_token.has_value());

  extora::list_objects_options secondOptions = firstOptions;
  secondOptions.continuation_token = *first.next_continuation_token;
  extora::object_list second;
  ASSERT_TRUE(succeeded(m_core->list_objects(extora::bucket_name{"photos"}, second, secondOptions)));
  EXPECT_TRUE(second.objects.empty());
  EXPECT_EQ(second.common_prefixes, (std::vector<std::string>{"a/c/"}));
  EXPECT_FALSE(second.is_truncated);
  EXPECT_FALSE(second.next_continuation_token.has_value());
}

} // namespace

} // namespace extoraTest
