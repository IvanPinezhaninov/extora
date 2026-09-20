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

TEST_F(StoreCoreTest, CopiesAcrossBucketsAndPreservesOrReplacesMetadata)
{
  ASSERT_TRUE(succeeded(m_core->create_bucket(extora::bucket_name{"archive"})));
  VectorReader reader{bytesFromString("copy body"), 3};
  extora::object_metadata metadata;
  metadata.content_type = "text/source";
  metadata.custom_metadata.push_back({"owner", "source"});
  ASSERT_TRUE(succeeded(m_core->put_object(extora::bucket_name{"photos"}, extora::object_key{"source"}, reader,
                                           metadata, ignoredPutResult())));

  extora::copy_object_result preservedResult;
  ASSERT_TRUE(succeeded(m_core->copy_object(extora::bucket_name{"photos"}, extora::object_key{"source"},
                                            extora::bucket_name{"photos"}, extora::object_key{"same-bucket"},
                                            preservedResult)));
  extora::object_info preserved;
  ASSERT_TRUE(
      succeeded(m_core->head_object(extora::bucket_name{"photos"}, extora::object_key{"same-bucket"}, preserved)));
  EXPECT_EQ(preserved.content_type, metadata.content_type);
  EXPECT_EQ(preserved.custom_metadata.size(), 1);

  extora::copy_object_options replaceOptions;
  replaceOptions.replace_metadata = true;
  replaceOptions.metadata.content_type = "text/replaced";
  replaceOptions.metadata.custom_metadata.push_back({"owner", "target"});
  extora::copy_object_result replacedResult;
  ASSERT_TRUE(succeeded(m_core->copy_object(extora::bucket_name{"photos"}, extora::object_key{"source"},
                                            extora::bucket_name{"archive"}, extora::object_key{"cross-bucket"},
                                            replacedResult, replaceOptions)));
  extora::object_info replaced;
  ASSERT_TRUE(
      succeeded(m_core->head_object(extora::bucket_name{"archive"}, extora::object_key{"cross-bucket"}, replaced)));
  EXPECT_EQ(replaced.content_type, replaceOptions.metadata.content_type);
  ASSERT_EQ(replaced.custom_metadata.size(), 1);
  EXPECT_EQ(replaced.custom_metadata[0].value, "target");
  EXPECT_EQ(replacedResult.etag, preservedResult.etag);
}

TEST_F(StoreCoreTest, CopyHonorsOverwriteAndSourceAndTargetConditions)
{
  ASSERT_TRUE(succeeded(putText(*m_core, "photos", "source", "source")));
  ASSERT_TRUE(succeeded(putText(*m_core, "photos", "target", "target")));
  extora::object_info source;
  extora::object_info target;
  ASSERT_TRUE(succeeded(m_core->head_object(extora::bucket_name{"photos"}, extora::object_key{"source"}, source)));
  ASSERT_TRUE(succeeded(m_core->head_object(extora::bucket_name{"photos"}, extora::object_key{"target"}, target)));

  extora::copy_object_options options;
  options.target_conditions.if_none_match_etag = extora::etag_wildcard;
  extora::copy_object_result result;
  EXPECT_EQ(m_core
                ->copy_object(extora::bucket_name{"photos"}, extora::object_key{"source"},
                              extora::bucket_name{"photos"}, extora::object_key{"target"}, result, options)
                .code,
            extora::storage_error_code::precondition_failed);

  options.target_conditions = {};
  options.source_conditions.if_match_etag = "wrong";
  EXPECT_EQ(m_core
                ->copy_object(extora::bucket_name{"photos"}, extora::object_key{"source"},
                              extora::bucket_name{"photos"}, extora::object_key{"target"}, result, options)
                .code,
            extora::storage_error_code::precondition_failed);
  options.source_conditions.if_match_etag = source.etag;
  options.target_conditions.if_match_etag = "wrong";
  EXPECT_EQ(m_core
                ->copy_object(extora::bucket_name{"photos"}, extora::object_key{"source"},
                              extora::bucket_name{"photos"}, extora::object_key{"target"}, result, options)
                .code,
            extora::storage_error_code::precondition_failed);
  options.target_conditions.if_match_etag = target.etag;
  EXPECT_TRUE(
      succeeded(m_core->copy_object(extora::bucket_name{"photos"}, extora::object_key{"source"},
                                    extora::bucket_name{"photos"}, extora::object_key{"target"}, result, options)));
}

} // namespace

} // namespace extoraTest
