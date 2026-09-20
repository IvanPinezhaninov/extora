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

TEST_F(StoreCoreTest, AppliesEtagAndTimePreconditions)
{
  ASSERT_TRUE(succeeded(putText(*m_core, "photos", "object", "value")));
  extora::object_info info;
  ASSERT_TRUE(succeeded(m_core->head_object(extora::bucket_name{"photos"}, extora::object_key{"object"}, info)));

  extora::head_object_options statOptions;
  statOptions.conditions.if_match_etag = info.etag;
  extora::object_info conditionalInfo;
  EXPECT_TRUE(succeeded(
      m_core->head_object(extora::bucket_name{"photos"}, extora::object_key{"object"}, conditionalInfo, statOptions)));
  statOptions.conditions.if_match_etag = "wrong";
  EXPECT_EQ(
      m_core->head_object(extora::bucket_name{"photos"}, extora::object_key{"object"}, conditionalInfo, statOptions)
          .code,
      extora::storage_error_code::precondition_failed);
  statOptions.conditions = {};
  statOptions.conditions.if_none_match_etag = info.etag;
  EXPECT_EQ(
      m_core->head_object(extora::bucket_name{"photos"}, extora::object_key{"object"}, conditionalInfo, statOptions)
          .code,
      extora::storage_error_code::not_modified);

  extora::open_object_options readOptions;
  readOptions.conditions.if_match_etag = info.etag;
  VectorWriter writer;
  EXPECT_TRUE(succeeded(readObject(*m_core, extora::bucket_name{"photos"}, extora::object_key{"object"}, writer,
                                   readOptions, ignoredOpenObjectResult())));
  readOptions.conditions.if_match_etag = "wrong";
  VectorWriter failedWriter;
  EXPECT_EQ(readObject(*m_core, extora::bucket_name{"photos"}, extora::object_key{"object"}, failedWriter, readOptions,
                       ignoredOpenObjectResult())
                .code,
            extora::storage_error_code::precondition_failed);

  extora::put_object_options putOptions;
  putOptions.conditions.if_match_etag = "wrong";
  EXPECT_EQ(putText(*m_core, "photos", "object", "new", putOptions).code,
            extora::storage_error_code::precondition_failed);
  putOptions.conditions.if_match_etag = info.etag;
  EXPECT_TRUE(succeeded(putText(*m_core, "photos", "object", "matched", putOptions)));

  ASSERT_TRUE(succeeded(m_core->head_object(extora::bucket_name{"photos"}, extora::object_key{"object"}, info)));
  putOptions.conditions = {};
  putOptions.conditions.if_none_match_etag = extora::etag_wildcard;
  EXPECT_EQ(putText(*m_core, "photos", "object", "new", putOptions).code,
            extora::storage_error_code::precondition_failed);
  EXPECT_TRUE(succeeded(putText(*m_core, "photos", "absent", "new", putOptions)));

  putOptions.conditions = {};
  putOptions.conditions.if_unmodified_since = info.modified_at;
  EXPECT_TRUE(succeeded(putText(*m_core, "photos", "object", "new", putOptions)));

  extora::open_object_options modifiedOptions;
  modifiedOptions.conditions.if_modified_since = info.modified_at - std::chrono::seconds{1};
  VectorWriter recentWriter;
  EXPECT_TRUE(succeeded(readObject(*m_core, extora::bucket_name{"photos"}, extora::object_key{"object"}, recentWriter,
                                   modifiedOptions, ignoredOpenObjectResult())));
  modifiedOptions.conditions.if_modified_since = std::chrono::system_clock::now();
  VectorWriter modifiedWriter;
  EXPECT_EQ(readObject(*m_core, extora::bucket_name{"photos"}, extora::object_key{"object"}, modifiedWriter,
                       modifiedOptions, ignoredOpenObjectResult())
                .code,
            extora::storage_error_code::not_modified);

  extora::open_object_options unmodifiedOptions;
  unmodifiedOptions.conditions.if_unmodified_since = info.modified_at - std::chrono::seconds{1};
  VectorWriter unmodifiedWriter;
  EXPECT_EQ(readObject(*m_core, extora::bucket_name{"photos"}, extora::object_key{"object"}, unmodifiedWriter,
                       unmodifiedOptions, ignoredOpenObjectResult())
                .code,
            extora::storage_error_code::precondition_failed);
}

} // namespace

} // namespace extoraTest
