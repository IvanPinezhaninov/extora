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

#include <gtest/gtest.h>

#include "HttpMultipartRequest.h"

namespace extoraHttpExample {

TEST(HttpMultipartRequestTest, ParsesCompletedParts)
{
  extora::complete_multipart_upload_options options;
  std::string errorMessage;
  ASSERT_TRUE(
      parseCompleteMultipartUploadOptions(R"json({"parts":[{"part_number":1,"etag":"first","checksum":"sum-1"},)json"
                                          R"json({"part_number":2,"etag":"second"}],"checksum":"final-sum"})json",
                                          options, errorMessage));
  ASSERT_EQ(options.parts.size(), 2u);
  EXPECT_EQ(options.parts[0].part_number, 1u);
  EXPECT_EQ(options.parts[0].etag, "first");
  EXPECT_EQ(options.parts[0].expected_checksum, "sum-1");
  EXPECT_EQ(options.parts[1].part_number, 2u);
  EXPECT_EQ(options.parts[1].etag, "second");
  EXPECT_FALSE(options.parts[1].expected_checksum.has_value());
  EXPECT_EQ(options.expected_checksum, "final-sum");
}

TEST(HttpMultipartRequestTest, RejectsMalformedRootObjects)
{
  extora::complete_multipart_upload_options options;
  std::string errorMessage;
  EXPECT_FALSE(parseCompleteMultipartUploadOptions("not-json", options, errorMessage));
  EXPECT_FALSE(parseCompleteMultipartUploadOptions("[]", options, errorMessage));
  EXPECT_FALSE(parseCompleteMultipartUploadOptions(R"json({"other":[]})json", options, errorMessage));
  EXPECT_FALSE(parseCompleteMultipartUploadOptions(R"json({"parts":[],"extra":true})json", options, errorMessage));
  EXPECT_FALSE(parseCompleteMultipartUploadOptions(R"json({"parts":[],"checksum":1})json", options, errorMessage));
  EXPECT_FALSE(parseCompleteMultipartUploadOptions(R"json({"parts":[],"checksum":""})json", options, errorMessage));
}

TEST(HttpMultipartRequestTest, RejectsMalformedPartEntries)
{
  extora::complete_multipart_upload_options options;
  std::string errorMessage;
  EXPECT_FALSE(parseCompleteMultipartUploadOptions(R"json({"parts":[1]})json", options, errorMessage));
  EXPECT_FALSE(parseCompleteMultipartUploadOptions(R"json({"parts":[{"part_number":1}]})json", options, errorMessage));
  EXPECT_FALSE(parseCompleteMultipartUploadOptions(
      R"json({"parts":[{"part_number":1,"etag":"value","extra":true}]})json", options, errorMessage));
  EXPECT_FALSE(parseCompleteMultipartUploadOptions(
      R"json({"parts":[{"part_number":1,"etag":"value","checksum":1}]})json", options, errorMessage));
  EXPECT_FALSE(parseCompleteMultipartUploadOptions(
      R"json({"parts":[{"part_number":1,"etag":"value","checksum":""}]})json", options, errorMessage));
}

TEST(HttpMultipartRequestTest, RejectsInvalidPartNumbersAndEtags)
{
  extora::complete_multipart_upload_options options;
  std::string errorMessage;
  EXPECT_FALSE(parseCompleteMultipartUploadOptions(R"json({"parts":[{"part_number":0,"etag":"value"}]})json", options,
                                                   errorMessage));
  EXPECT_FALSE(
      parseCompleteMultipartUploadOptions(R"json({"parts":[{"part_number":1,"etag":""}]})json", options, errorMessage));
  EXPECT_FALSE(parseCompleteMultipartUploadOptions(R"json({"parts":[{"part_number":4294967296,"etag":"value"}]})json",
                                                   options, errorMessage));
}

} // namespace extoraHttpExample
