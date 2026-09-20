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

#include <optional>
#include <string>

#include <gtest/gtest.h>

#include "HttpConstants.h"
#include "HttpHandlerSupport.h"

namespace extoraHttpExample {

TEST(HttpHandlerSupportTest, EnablesPersistentHttp11Responses)
{
  boost::beast::http::response<boost::beast::http::empty_body> http11Response{boost::beast::http::status::ok, 11};
  setCommonHeaders(http11Response);
  EXPECT_TRUE(http11Response.keep_alive());

  boost::beast::http::response<boost::beast::http::empty_body> http10Response{boost::beast::http::status::ok, 10};
  setCommonHeaders(http10Response);
  EXPECT_FALSE(http10Response.keep_alive());
}

TEST(HttpHandlerSupportTest, IdentifiesAndRemovesQuerySelectors)
{
  EXPECT_TRUE(hasQuerySelector("versions", versionsQuery));
  EXPECT_TRUE(hasQuerySelector("versions&prefix=2026", versionsQuery));
  EXPECT_FALSE(hasQuerySelector("version-id=value", versionsQuery));
  EXPECT_EQ(queryAfterSelector("versions", versionsQuery), "");
  EXPECT_EQ(queryAfterSelector("versions&prefix=2026", versionsQuery), "prefix=2026");
}

TEST(HttpHandlerSupportTest, ParsesCopySourceAndVersion)
{
  boost::beast::http::fields fields;
  fields.set(copySourceHeader, "/source/folder%2Fphoto.jpg?version-id=v%2F1");
  Route route;
  std::optional<extora::object_version_id> versionId;
  std::string errorMessage;
  ASSERT_TRUE(parseCopySource(fields, route, versionId, errorMessage));
  EXPECT_EQ(route.bucket.value, "source");
  EXPECT_EQ(route.key.value, "folder/photo.jpg");
  ASSERT_TRUE(versionId.has_value());
  EXPECT_EQ(versionId->value, "v/1");
}

TEST(HttpHandlerSupportTest, RejectsMissingAndNonObjectCopySources)
{
  boost::beast::http::fields fields;
  Route route;
  std::optional<extora::object_version_id> versionId;
  std::string errorMessage;
  EXPECT_FALSE(parseCopySource(fields, route, versionId, errorMessage));
  fields.set(copySourceHeader, "/source");
  EXPECT_FALSE(parseCopySource(fields, route, versionId, errorMessage));
  fields.set(copySourceHeader, "/source/photo.jpg?unknown=value");
  EXPECT_FALSE(parseCopySource(fields, route, versionId, errorMessage));
}

TEST(HttpHandlerSupportTest, ValidatesCopyMetadataDirectiveAndChecksumSelection)
{
  boost::beast::http::fields fields;
  extora::copy_object_options options;
  std::string errorMessage;
  ASSERT_TRUE(parseCopyOptions(fields, options, errorMessage));
  EXPECT_FALSE(options.replace_metadata);

  fields.set(metadataDirectiveHeader, "Replace");
  fields.set(checksumAlgorithmHeader, "xxh3-128");
  fields.set(checksumHeader, "checksum-value");
  fields.set(boost::beast::http::field::content_type, "image/jpeg");
  EXPECT_FALSE(parseCopyOptions(fields, options, errorMessage));

  fields.erase(checksumHeader);
  ASSERT_TRUE(parseCopyOptions(fields, options, errorMessage));
  EXPECT_TRUE(options.replace_metadata);
  EXPECT_EQ(options.metadata.content_type, "image/jpeg");
  EXPECT_EQ(options.target_checksum_algorithm.value, "xxh3-128");

  fields.erase(checksumAlgorithmHeader);
  fields.set(metadataDirectiveHeader, "Invalid");
  extora::copy_object_options invalidDirectiveOptions;
  EXPECT_FALSE(parseCopyOptions(fields, invalidDirectiveOptions, errorMessage));
}

} // namespace extoraHttpExample
