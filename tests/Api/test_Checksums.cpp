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

#include "ApiTestSupport.h"
#include "PublicTestSupport.h"

namespace extoraTest {

namespace {

class DefaultCompositeFactory final : public extora::hasher_factory {
public:
  std::unique_ptr<extora::hasher> create_hasher(const extora::checksum_algorithm_name&,
                                                extora::storage_error& error) override
  {
    error = {};
    return {};
  }
};

} // namespace

TEST(ObjectStoreChecksumApiTest, DefaultFactoryRejectsCompositeChecksums)
{
  DefaultCompositeFactory factory;
  const extora::checksum_algorithm_name checksumAlgorithm{"custom"};
  EXPECT_FALSE(factory.can_combine_checksums(checksumAlgorithm));

  const std::vector<std::string_view> partChecksums{"first", "second"};
  std::string value = "stale";
  const extora::storage_error error = factory.combine_checksums(checksumAlgorithm, partChecksums, value);
  EXPECT_EQ(error.code, extora::storage_error_code::unsupported_checksum_type);
  EXPECT_TRUE(value.empty());
}

TEST_F(ObjectStoreApiTest, ValidatesAndStoresAnExpectedChecksum)
{
  const std::string body = "checksummed object";
  extora::put_object_options options;
  options.expected_checksum =
      extora::object_checksum{extora::checksum_algorithm_name{"test-sum"}, apiChecksumValue(body)};
  extora::put_object_result result;
  ASSERT_TRUE(succeeded(putText(*m_store, "photos", "checked", body, extora::object_metadata{}, options, &result)));
  EXPECT_EQ(result.checksum.checksum_algorithm.value, "test-sum");
  EXPECT_EQ(result.checksum.value, apiChecksumValue(body));
}

TEST_F(ObjectStoreApiTest, StoresBuiltInChecksumByDefault)
{
  extora::put_object_result result;
  ASSERT_TRUE(succeeded(putText(*m_store, "photos", "default-checksum", "body", extora::object_metadata{},
                                extora::put_object_options{}, &result)));
  EXPECT_EQ(result.checksum.checksum_algorithm.value, extora::xxh3_128_checksum_algorithm.value);
  EXPECT_EQ(result.etag, result.checksum.checksum_algorithm.value + ":" + result.checksum.value);

  extora::object_info info;
  ASSERT_TRUE(
      succeeded(m_store->head_object(extora::bucket_name{"photos"}, extora::object_key{"default-checksum"}, info)));
  ASSERT_TRUE(info.checksum.has_value());
  EXPECT_EQ(info.checksum->checksum_algorithm.value, result.checksum.checksum_algorithm.value);
  EXPECT_EQ(info.checksum->value, result.checksum.value);
}

TEST_F(ObjectStoreApiTest, RejectsAMismatchingChecksumWithoutPublishing)
{
  extora::put_object_options options;
  options.expected_checksum = extora::object_checksum{extora::checksum_algorithm_name{"test-sum"}, "wrong"};
  EXPECT_EQ(putText(*m_store, "photos", "bad", "body", extora::object_metadata{}, options).code,
            extora::storage_error_code::checksum_mismatch);
  extora::object_info info;
  EXPECT_EQ(m_store->head_object(extora::bucket_name{"photos"}, extora::object_key{"bad"}, info).code,
            extora::storage_error_code::object_not_found);
}

TEST_F(ObjectStoreApiTest, RejectsAChecksumWithoutAlgorithm)
{
  extora::put_object_options options;
  options.expected_checksum = extora::object_checksum{extora::checksum_algorithm_name{}, "value"};
  const extora::storage_error error = putText(*m_store, "photos", "bad", "body", {}, options);
  EXPECT_EQ(error.code, extora::storage_error_code::unsupported_checksum_algorithm);
  EXPECT_EQ(error.message, "checksum algorithm is empty");
}

TEST_F(ObjectStoreApiTest, VerifiesInternalIntegrityDuringFullRead)
{
  extora::put_object_options putOptions;
  putOptions.expected_checksum =
      extora::object_checksum{extora::checksum_algorithm_name{"test-sum"}, apiChecksumValue("body")};
  ASSERT_TRUE(succeeded(putText(*m_store, "photos", "checked", "body", {}, putOptions)));
  extora::open_object_options getOptions;
  getOptions.verify_integrity = true;
  std::string text;
  ASSERT_TRUE(succeeded(readText(*m_store, "photos", "checked", text, getOptions)));
  EXPECT_EQ(text, "body");
}

TEST_F(ObjectStoreApiTest, VerifiesDefaultChecksumAndRejectsRangeVerification)
{
  ASSERT_TRUE(succeeded(putText(*m_store, "photos", "unchecked", "body")));
  extora::open_object_options options;
  options.verify_integrity = true;
  std::string text;
  ASSERT_TRUE(succeeded(readText(*m_store, "photos", "unchecked", text, options)));
  EXPECT_EQ(text, "body");

  options.range = extora::byte_range{extora::byte_range_type::offset_length, 0, 2};
  EXPECT_EQ(readText(*m_store, "photos", "unchecked", text, options).code, extora::storage_error_code::invalid_range);
}

TEST(ObjectStoreChecksumApiTest, ReportsUnsupportedExtensionAlgorithms)
{
  extora::object_store_options options;
  options.root_directory = makeTempRoot();
  extora::storage_error error;
  std::unique_ptr<extora::object_store> withoutExtension = extora::open_object_store(options, error);
  ASSERT_TRUE(succeeded(error));
  ASSERT_NE(withoutExtension, nullptr);
  ASSERT_TRUE(succeeded(withoutExtension->create_bucket(extora::bucket_name{"photos"})));
  extora::put_object_options putOptions;
  putOptions.expected_checksum =
      extora::object_checksum{extora::checksum_algorithm_name{"test-sum"}, apiChecksumValue("body")};
  EXPECT_EQ(putText(*withoutExtension, "photos", "object", "body", {}, putOptions).code,
            extora::storage_error_code::unsupported_checksum_algorithm);

  const std::shared_ptr<ApiHasherFactory> factory = std::make_shared<ApiHasherFactory>();
  options.root_directory = makeTempRoot();
  options.custom_hasher_factory = factory;
  std::unique_ptr<extora::object_store> unsupported = extora::open_object_store(options, error);
  ASSERT_TRUE(succeeded(error));
  ASSERT_NE(unsupported, nullptr);
  ASSERT_TRUE(succeeded(unsupported->create_bucket(extora::bucket_name{"photos"})));
  putOptions.expected_checksum = extora::object_checksum{extora::checksum_algorithm_name{"unsupported"}, "value"};
  EXPECT_EQ(putText(*unsupported, "photos", "object", "body", {}, putOptions).code,
            extora::storage_error_code::unsupported_checksum_algorithm);
}

TEST(ObjectStoreChecksumApiTest, RetainsCustomHasherFactory)
{
  extora::object_store_options options;
  options.root_directory = makeTempRoot();
  std::shared_ptr<ApiHasherFactory> factory = std::make_shared<ApiHasherFactory>();
  const std::weak_ptr<ApiHasherFactory> weakFactory = factory;
  options.custom_hasher_factory = std::move(factory);

  extora::storage_error error;
  std::unique_ptr<extora::object_store> store = extora::open_object_store(options, error);
  ASSERT_TRUE(succeeded(error)) << error.message;
  ASSERT_TRUE(store);
  options.custom_hasher_factory.reset();
  ASSERT_FALSE(weakFactory.expired());

  ASSERT_TRUE(succeeded(store->create_bucket(extora::bucket_name{"photos"})));
  extora::put_object_options putOptions;
  putOptions.expected_checksum =
      extora::object_checksum{extora::checksum_algorithm_name{"test-sum"}, apiChecksumValue("body")};
  EXPECT_TRUE(succeeded(putText(*store, "photos", "object", "body", {}, putOptions)));

  store.reset();
  EXPECT_TRUE(weakFactory.expired());
}

TEST_F(ObjectStoreApiTest, VerifiesMultipartPartChecksums)
{
  extora::create_multipart_upload_result upload;
  extora::create_multipart_upload_options createOptions;
  createOptions.checksum_algorithm = extora::checksum_algorithm_name{"test-sum"};
  ASSERT_TRUE(succeeded(m_store->create_multipart_upload(extora::bucket_name{"photos"}, extora::object_key{"archive"},
                                                         extora::object_metadata{}, upload, createOptions)));
  EXPECT_EQ(upload.checksum_algorithm.value, "test-sum");
  EXPECT_EQ(upload.checksum_type, extora::object_checksum_type::full_object);
  const std::string body = "part-body";
  VectorReader mismatchingReader{bytesFromString(body), 3};
  extora::upload_part_options mismatchingOptions;
  mismatchingOptions.expected_checksum = "wrong";
  EXPECT_EQ(m_store
                ->upload_part(extora::bucket_name{"photos"}, extora::object_key{"archive"}, upload.upload_id, 1,
                              mismatchingReader, ignoredUploadPartResult(), mismatchingOptions)
                .code,
            extora::storage_error_code::checksum_mismatch);

  VectorReader reader{bytesFromString(body), 3};
  extora::upload_part_options options;
  options.expected_checksum = apiChecksumValue(body);
  extora::upload_part_result result;
  ASSERT_TRUE(succeeded(m_store->upload_part(extora::bucket_name{"photos"}, extora::object_key{"archive"},
                                             upload.upload_id, 1, reader, result, options)));
  EXPECT_EQ(result.checksum.value, apiChecksumValue(body));
  EXPECT_EQ(result.checksum.type, extora::object_checksum_type::full_object);

  extora::complete_multipart_upload_options wrongChecksum;
  wrongChecksum.parts.push_back(extora::completed_multipart_part{1, result.etag, "wrong"});
  EXPECT_EQ(m_store
                ->complete_multipart_upload(extora::bucket_name{"photos"}, extora::object_key{"archive"},
                                            upload.upload_id, wrongChecksum, ignoredPutResult())
                .code,
            extora::storage_error_code::invalid_part);

  extora::complete_multipart_upload_options correctChecksum;
  correctChecksum.parts.push_back(extora::completed_multipart_part{1, result.etag, result.checksum.value});
  extora::put_object_result completeResult;
  EXPECT_TRUE(succeeded(m_store->complete_multipart_upload(extora::bucket_name{"photos"}, extora::object_key{"archive"},
                                                           upload.upload_id, correctChecksum, completeResult)));
  EXPECT_EQ(completeResult.checksum.checksum_algorithm.value, "test-sum");
  EXPECT_EQ(completeResult.checksum.value, apiChecksumValue(body));
  EXPECT_EQ(completeResult.checksum.type, extora::object_checksum_type::full_object);
}

TEST_F(ObjectStoreApiTest, CalculatesAndValidatesCompositeMultipartChecksums)
{
  extora::create_multipart_upload_options createOptions;
  createOptions.checksum_algorithm = extora::checksum_algorithm_name{"test-sum"};
  createOptions.checksum_type = extora::object_checksum_type::composite;
  extora::create_multipart_upload_result upload;
  ASSERT_TRUE(succeeded(m_store->create_multipart_upload(extora::bucket_name{"photos"}, extora::object_key{"archive"},
                                                         extora::object_metadata{}, upload, createOptions)));
  EXPECT_EQ(upload.checksum_algorithm.value, createOptions.checksum_algorithm.value);
  EXPECT_EQ(upload.checksum_type, createOptions.checksum_type);

  const std::string body = "part-body";
  VectorReader reader{bytesFromString(body), 3};
  extora::upload_part_result part;
  ASSERT_TRUE(succeeded(m_store->upload_part(extora::bucket_name{"photos"}, extora::object_key{"archive"},
                                             upload.upload_id, 1, reader, part)));
  EXPECT_EQ(part.checksum.checksum_algorithm.value, "test-sum");

  const std::string expectedValue = apiChecksumValue(part.checksum.value) + "-1";
  extora::complete_multipart_upload_options mismatchingOptions;
  mismatchingOptions.parts.push_back(extora::completed_multipart_part{1, part.etag, part.checksum.value});
  mismatchingOptions.expected_checksum = "wrong";
  EXPECT_EQ(m_store
                ->complete_multipart_upload(extora::bucket_name{"photos"}, extora::object_key{"archive"},
                                            upload.upload_id, mismatchingOptions, ignoredPutResult())
                .code,
            extora::storage_error_code::checksum_mismatch);

  extora::complete_multipart_upload_options completeOptions;
  completeOptions.parts.push_back(extora::completed_multipart_part{1, part.etag, part.checksum.value});
  completeOptions.expected_checksum = expectedValue;
  extora::put_object_result completeResult;
  ASSERT_TRUE(succeeded(m_store->complete_multipart_upload(extora::bucket_name{"photos"}, extora::object_key{"archive"},
                                                           upload.upload_id, completeOptions, completeResult)));
  EXPECT_EQ(completeResult.checksum.value, expectedValue);
  EXPECT_EQ(completeResult.checksum.type, extora::object_checksum_type::composite);

  extora::object_info info;
  ASSERT_TRUE(succeeded(m_store->head_object(extora::bucket_name{"photos"}, extora::object_key{"archive"}, info)));
  ASSERT_TRUE(info.checksum.has_value());
  EXPECT_EQ(info.checksum->value, expectedValue);
  EXPECT_EQ(info.checksum->type, extora::object_checksum_type::composite);

  extora::open_object_options getOptions;
  getOptions.verify_integrity = true;
  extora::open_object_result getResult;
  std::string downloaded;
  ASSERT_TRUE(succeeded(readText(*m_store, "photos", "archive", downloaded, getOptions, &getResult)));
  EXPECT_EQ(downloaded, body);
  ASSERT_TRUE(getResult.object.checksum.has_value());
  EXPECT_EQ(getResult.object.checksum->value, expectedValue);
  EXPECT_EQ(getResult.object.checksum->type, extora::object_checksum_type::composite);
}

} // namespace extoraTest
