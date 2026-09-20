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

#include <chrono>

#include "CoreTestSupport.h"
#include "PublicTestSupport.h"

namespace extoraTest {

TEST_F(StoreCoreTest, VerifiesExpectedChecksumOnPut)
{
  const std::string source = "checksummed object";
  VectorReader reader{bytesFromString(source), 3};
  extora::object_metadata metadata;
  extora::put_object_options options;
  options.expected_content_length = source.size();
  options.expected_checksum =
      extora::object_checksum{extora::checksum_algorithm_name{"test-sum"}, testChecksumValue(source)};

  ASSERT_TRUE(succeeded(m_core->put_object(extora::bucket_name{"photos"}, extora::object_key{"checked"}, reader,
                                           metadata, ignoredPutResult(), options)));

  extora::object_info info;
  ASSERT_TRUE(succeeded(m_core->head_object(extora::bucket_name{"photos"}, extora::object_key{"checked"}, info)));
  ASSERT_TRUE(info.checksum.has_value());
  EXPECT_EQ(info.checksum->checksum_algorithm.value, "test-sum");
  EXPECT_EQ(info.checksum->value, testChecksumValue(source));
}

TEST_F(StoreCoreTest, RejectsChecksumMismatchAndDoesNotPublishObject)
{
  VectorReader reader{bytesFromString("checksummed object"), 4};
  extora::object_metadata metadata;
  extora::put_object_options options;
  options.expected_checksum = extora::object_checksum{extora::checksum_algorithm_name{"test-sum"}, "wrong"};

  const extora::storage_error putError = m_core->put_object(
      extora::bucket_name{"photos"}, extora::object_key{"bad-checksum"}, reader, metadata, ignoredPutResult(), options);
  EXPECT_EQ(putError.code, extora::storage_error_code::checksum_mismatch);

  VectorWriter writer;
  const extora::storage_error getError =
      readObject(*m_core, extora::bucket_name{"photos"}, extora::object_key{"bad-checksum"}, writer,
                 extora::open_object_options{}, ignoredOpenObjectResult());
  EXPECT_EQ(getError.code, extora::storage_error_code::object_not_found);
}

TEST_F(StoreCoreTest, CopyWithReplacementMetadataPreservesSourceChecksumWhenOmitted)
{
  const std::string source = "checksummed copy";
  VectorReader reader{bytesFromString(source), 4};
  extora::object_metadata metadata;
  extora::put_object_options putOptions;
  putOptions.expected_checksum =
      extora::object_checksum{extora::checksum_algorithm_name{"test-sum"}, testChecksumValue(source)};
  ASSERT_TRUE(succeeded(m_core->put_object(extora::bucket_name{"photos"}, extora::object_key{"copy-source"}, reader,
                                           metadata, ignoredPutResult(), putOptions)));

  extora::copy_object_options copyOptions;
  copyOptions.replace_metadata = true;
  copyOptions.metadata.content_type = "text/copied";
  extora::copy_object_result copyResult;
  ASSERT_TRUE(succeeded(m_core->copy_object(extora::bucket_name{"photos"}, extora::object_key{"copy-source"},
                                            extora::bucket_name{"photos"}, extora::object_key{"copy-target"},
                                            copyResult, copyOptions)));

  EXPECT_EQ(copyResult.checksum.checksum_algorithm.value, "test-sum");
  EXPECT_EQ(copyResult.checksum.value, testChecksumValue(source));
}

TEST_F(StoreCoreTest, CopyCalculatesRequestedTargetChecksum)
{
  const std::string source = "unchecked copy source";
  VectorReader reader{bytesFromString(source), 4};
  ASSERT_TRUE(succeeded(m_core->put_object(extora::bucket_name{"photos"}, extora::object_key{"copy-source"}, reader,
                                           extora::object_metadata{}, ignoredPutResult())));

  extora::copy_object_options copyOptions;
  copyOptions.target_checksum_algorithm = extora::checksum_algorithm_name{"test-sum"};
  extora::copy_object_result copyResult;
  ASSERT_TRUE(succeeded(m_core->copy_object(extora::bucket_name{"photos"}, extora::object_key{"copy-source"},
                                            extora::bucket_name{"photos"}, extora::object_key{"copy-target"},
                                            copyResult, copyOptions)));

  EXPECT_EQ(copyResult.checksum.checksum_algorithm.value, "test-sum");
  EXPECT_EQ(copyResult.checksum.value, testChecksumValue(source));
}

TEST_F(StoreCoreTest, VerifiesInternalIntegrityOnGet)
{
  const std::string source = "checksummed read object";
  VectorReader reader{bytesFromString(source), 4};
  extora::object_metadata metadata;
  extora::put_object_options putOptions;
  putOptions.expected_checksum =
      extora::object_checksum{extora::checksum_algorithm_name{"test-sum"}, testChecksumValue(source)};

  ASSERT_TRUE(succeeded(m_core->put_object(extora::bucket_name{"photos"}, extora::object_key{"read-checked"}, reader,
                                           metadata, ignoredPutResult(), putOptions)));

  extora::open_object_options readOptions;
  readOptions.verify_integrity = true;

  VectorWriter writer;
  ASSERT_TRUE(succeeded(readObject(*m_core, extora::bucket_name{"photos"}, extora::object_key{"read-checked"}, writer,
                                   readOptions, ignoredOpenObjectResult())));
  EXPECT_EQ(stringFromBytes(writer.bytes()), source);
}

TEST_F(StoreCoreTest, VerifiesBuiltInChecksumWhenCallerOmitsOne)
{
  VectorReader reader{bytesFromString("unchecked"), 4};
  extora::put_object_result putResult;
  ASSERT_TRUE(succeeded(m_core->put_object(extora::bucket_name{"photos"}, extora::object_key{"unchecked"}, reader,
                                           extora::object_metadata{}, putResult)));
  EXPECT_EQ(putResult.checksum.checksum_algorithm.value, extora::xxh3_128_checksum_algorithm.value);
  EXPECT_EQ(putResult.etag, putResult.checksum.checksum_algorithm.value + ":" + putResult.checksum.value);

  extora::open_object_options options;
  options.verify_integrity = true;

  VectorWriter writer;
  ASSERT_TRUE(succeeded(readObject(*m_core, extora::bucket_name{"photos"}, extora::object_key{"unchecked"}, writer,
                                   options, ignoredOpenObjectResult())));
  EXPECT_EQ(stringFromBytes(writer.bytes()), "unchecked");
}

TEST_F(StoreCoreTest, RejectsRangeIntegrityVerification)
{
  const std::string source = "checksummed range object";
  VectorReader reader{bytesFromString(source), 4};
  extora::object_metadata metadata;
  ASSERT_TRUE(succeeded(m_core->put_object(extora::bucket_name{"photos"}, extora::object_key{"range-checked"}, reader,
                                           metadata, ignoredPutResult())));

  extora::open_object_options options;
  options.range = extora::byte_range{extora::byte_range_type::offset_length, 0, 4};
  options.verify_integrity = true;

  VectorWriter writer;
  const extora::storage_error error =
      readObject(*m_core, extora::bucket_name{"photos"}, extora::object_key{"range-checked"}, writer, options,
                 ignoredOpenObjectResult());
  EXPECT_EQ(error.code, extora::storage_error_code::invalid_range);
}

TEST_F(StoreCoreTest, DetectsIntegrityMismatchOnGet)
{
  const std::string source = "checksummed read object";
  VectorReader reader{bytesFromString(source), 4};
  extora::object_metadata metadata;

  ASSERT_TRUE(succeeded(m_core->put_object(extora::bucket_name{"photos"}, extora::object_key{"corrupt"}, reader,
                                           metadata, ignoredPutResult())));

  const std::string segmentPath = joinPath(joinPath(m_root, "segments"), "0000000000000001.dat");
  ASSERT_TRUE(overwriteFileByte(segmentPath, 0, std::byte{'X'}));

  extora::open_object_options readOptions;
  readOptions.verify_integrity = true;

  VectorWriter writer;
  const extora::storage_error error = readObject(*m_core, extora::bucket_name{"photos"}, extora::object_key{"corrupt"},
                                                 writer, readOptions, ignoredOpenObjectResult());
  EXPECT_EQ(error.code, extora::storage_error_code::checksum_mismatch);

  extora::object_info info;
  EXPECT_EQ(m_core->head_object(extora::bucket_name{"photos"}, extora::object_key{"corrupt"}, info).code,
            extora::storage_error_code::object_corrupted);

  VectorWriter secondWriter;
  EXPECT_EQ(readObject(*m_core, extora::bucket_name{"photos"}, extora::object_key{"corrupt"}, secondWriter,
                       extora::open_object_options{}, ignoredOpenObjectResult())
                .code,
            extora::storage_error_code::object_corrupted);
}

TEST_F(StoreCoreTest, VerifiesCompositeMultipartPayloadWithInternalChecksum)
{
  const extora::bucket_name bucket{"photos"};
  const extora::object_key key{"composite"};
  extora::create_multipart_upload_options createOptions;
  createOptions.checksum_type = extora::object_checksum_type::composite;
  extora::create_multipart_upload_result upload;
  ASSERT_TRUE(
      succeeded(m_core->create_multipart_upload(bucket, key, extora::object_metadata{}, upload, createOptions)));

  const std::string source = "composite multipart object";
  VectorReader reader{bytesFromString(source), 4};
  extora::upload_part_result part;
  ASSERT_TRUE(succeeded(m_core->upload_part(bucket, key, upload.upload_id, 1, reader, part)));

  extora::complete_multipart_upload_options completeOptions;
  completeOptions.parts.push_back(extora::completed_multipart_part{1, part.etag, part.checksum.value});
  extora::put_object_result completeResult;
  ASSERT_TRUE(
      succeeded(m_core->complete_multipart_upload(bucket, key, upload.upload_id, completeOptions, completeResult)));
  EXPECT_EQ(completeResult.checksum.type, extora::object_checksum_type::composite);

  extora::open_object_options readOptions;
  readOptions.verify_integrity = true;
  VectorWriter validWriter;
  extora::open_object_result validResult;
  ASSERT_TRUE(succeeded(readObject(*m_core, bucket, key, validWriter, readOptions, validResult)));
  EXPECT_EQ(stringFromBytes(validWriter.bytes()), source);
  ASSERT_TRUE(validResult.object.checksum.has_value());
  EXPECT_EQ(validResult.object.checksum->value, completeResult.checksum.value);
  EXPECT_EQ(validResult.object.checksum->type, extora::object_checksum_type::composite);

  const std::string segmentPath = joinPath(joinPath(m_root, "segments"), "0000000000000001.dat");
  ASSERT_TRUE(overwriteFileByte(segmentPath, 0, std::byte{'X'}));

  VectorWriter corruptWriter;
  const extora::storage_error error =
      readObject(*m_core, bucket, key, corruptWriter, readOptions, ignoredOpenObjectResult());
  EXPECT_EQ(error.code, extora::storage_error_code::checksum_mismatch);
}

TEST_F(StoreCoreTest, RejectsCorruptedMultipartPartBeforePublishingObject)
{
  const extora::bucket_name bucket{"photos"};
  const extora::object_key key{"corrupt-before-complete"};
  extora::create_multipart_upload_options createOptions;
  createOptions.checksum_type = extora::object_checksum_type::composite;
  extora::create_multipart_upload_result upload;
  ASSERT_TRUE(
      succeeded(m_core->create_multipart_upload(bucket, key, extora::object_metadata{}, upload, createOptions)));

  VectorReader reader{bytesFromString("multipart body"), 4};
  extora::upload_part_result part;
  ASSERT_TRUE(succeeded(m_core->upload_part(bucket, key, upload.upload_id, 1, reader, part)));

  const std::string segmentPath = joinPath(joinPath(m_root, "segments"), "0000000000000001.dat");
  ASSERT_TRUE(overwriteFileByte(segmentPath, 0, std::byte{'X'}));

  extora::complete_multipart_upload_options options;
  options.parts.push_back(extora::completed_multipart_part{1, part.etag, part.checksum.value});
  extora::put_object_result result;
  EXPECT_EQ(m_core->complete_multipart_upload(bucket, key, upload.upload_id, options, result).code,
            extora::storage_error_code::checksum_mismatch);

  extora::object_info info;
  EXPECT_EQ(m_core->head_object(bucket, key, info).code, extora::storage_error_code::object_not_found);
}

TEST_F(StoreCoreTest, RejectsExpectedChecksumWithoutAlgorithm)
{
  const std::string source = "checksummed object";
  VectorReader reader{bytesFromString(source), 4};
  extora::object_metadata metadata;
  extora::put_object_options options;
  options.expected_checksum = extora::object_checksum{extora::checksum_algorithm_name{}, testChecksumValue(source)};

  const extora::storage_error error =
      m_core->put_object(extora::bucket_name{"photos"}, extora::object_key{"empty-checksum-algorithm"}, reader,
                         metadata, ignoredPutResult(), options);
  EXPECT_EQ(error.code, extora::storage_error_code::unsupported_checksum_algorithm);
  EXPECT_EQ(error.message, "checksum algorithm is empty");
}

TEST_F(StoreCoreTest, RejectsUnsupportedChecksumAlgorithm)
{
  VectorReader reader{bytesFromString("checksummed object"), 4};
  extora::object_metadata metadata;
  extora::put_object_options options;
  options.expected_checksum = extora::object_checksum{extora::checksum_algorithm_name{"unknown"}, "value"};

  const extora::storage_error error =
      m_core->put_object(extora::bucket_name{"photos"}, extora::object_key{"unknown-checksum"}, reader, metadata,
                         ignoredPutResult(), options);
  EXPECT_EQ(error.code, extora::storage_error_code::unsupported_checksum_algorithm);
}

TEST_F(StoreCoreTest, IntegrityVerificationDoesNotRequirePublicChecksum)
{
  extora::core::indexed_object object;
  object.bucket = extora::bucket_name{"photos"};
  object.key = extora::object_key{"missing-checksum"};
  object.metadata.content_length = 0;
  object.payload.internal_checksum =
      extora::object_checksum{extora::checksum_algorithm_name{"test-sum"}, testChecksumValue("")};
  object.etag = "etag";
  object.version_id = extora::object_version_id{extora::null_version_id};
  object.created_at = std::chrono::system_clock::now();
  object.modified_at = object.created_at;
  ASSERT_TRUE(succeeded(m_index->publish_object(object)));

  extora::open_object_options options;
  options.verify_integrity = true;
  extora::open_object_result result;
  EXPECT_TRUE(succeeded(m_core->open_object(object.bucket, object.key, result, options)));
  ASSERT_NE(result.reader, nullptr);
  std::byte data;
  const extora::object_read_result readResult = result.reader->read(&data, 1);
  EXPECT_TRUE(succeeded(readResult.error));
  EXPECT_TRUE(readResult.end_of_stream);
}

TEST_F(StoreCoreTest, IntegrityVerificationIgnoresUnsupportedPublicChecksum)
{
  extora::core::indexed_object object;
  object.bucket = extora::bucket_name{"photos"};
  object.key = extora::object_key{"unsupported-checksum"};
  object.metadata.content_length = 0;
  object.metadata.checksum = extora::object_checksum{extora::checksum_algorithm_name{"unsupported"}, "value"};
  object.payload.internal_checksum =
      extora::object_checksum{extora::checksum_algorithm_name{"test-sum"}, testChecksumValue("")};
  object.etag = "etag";
  object.version_id = extora::object_version_id{extora::null_version_id};
  object.created_at = std::chrono::system_clock::now();
  object.modified_at = object.created_at;
  ASSERT_TRUE(succeeded(m_index->publish_object(object)));

  extora::open_object_options options;
  options.verify_integrity = true;
  extora::open_object_result result;
  EXPECT_TRUE(succeeded(m_core->open_object(object.bucket, object.key, result, options)));
  ASSERT_NE(result.reader, nullptr);
  std::byte data;
  const extora::object_read_result readResult = result.reader->read(&data, 1);
  EXPECT_TRUE(succeeded(readResult.error));
  EXPECT_TRUE(readResult.end_of_stream);
}

} // namespace extoraTest
