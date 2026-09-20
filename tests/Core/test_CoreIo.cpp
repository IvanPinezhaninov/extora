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

#include "CoreTestSupport.h"
#include "PublicTestSupport.h"

namespace extoraTest {

TEST_F(StoreCoreTest, PutsAndGetsEmptyObject)
{
  VectorReader reader{{}, 16};
  extora::object_metadata metadata;
  extora::put_object_options putOptions;
  putOptions.expected_content_length = 0;

  ASSERT_TRUE(succeeded(m_core->put_object(extora::bucket_name{"photos"}, extora::object_key{"empty"}, reader, metadata,
                                           ignoredPutResult(), putOptions)));

  VectorWriter writer;
  ASSERT_TRUE(succeeded(readObject(*m_core, extora::bucket_name{"photos"}, extora::object_key{"empty"}, writer,
                                   extora::open_object_options{}, ignoredOpenObjectResult())));

  EXPECT_TRUE(writer.bytes().empty());
}

TEST_F(StoreCoreTest, PutsAndGetsSmallObject)
{
  const std::string source = "hello extora";
  VectorReader reader{bytesFromString(source), 64};
  extora::object_metadata metadata;
  metadata.content_type = "text/plain";
  extora::put_object_options putOptions;
  putOptions.expected_content_length = source.size();

  ASSERT_TRUE(succeeded(m_core->put_object(extora::bucket_name{"photos"}, extora::object_key{"hello.txt"}, reader,
                                           metadata, ignoredPutResult(), putOptions)));

  VectorWriter writer;
  ASSERT_TRUE(succeeded(readObject(*m_core, extora::bucket_name{"photos"}, extora::object_key{"hello.txt"}, writer,
                                   extora::open_object_options{}, ignoredOpenObjectResult())));

  EXPECT_EQ(stringFromBytes(writer.bytes()), source);
}

TEST_F(StoreCoreTest, ReadsObjectRange)
{
  const std::string source = "hello extora";
  VectorReader reader{bytesFromString(source), 64};
  extora::object_metadata metadata;
  extora::put_object_options putOptions;
  putOptions.expected_content_length = source.size();

  ASSERT_TRUE(succeeded(m_core->put_object(extora::bucket_name{"photos"}, extora::object_key{"range.txt"}, reader,
                                           metadata, ignoredPutResult(), putOptions)));

  extora::open_object_options options;
  options.range = extora::byte_range{extora::byte_range_type::offset_length, 6, 6};

  VectorWriter writer;
  ASSERT_TRUE(succeeded(readObject(*m_core, extora::bucket_name{"photos"}, extora::object_key{"range.txt"}, writer,
                                   options, ignoredOpenObjectResult())));

  EXPECT_EQ(stringFromBytes(writer.bytes()), "extora");
}

TEST_F(StoreCoreTest, ReadsZeroLengthRange)
{
  VectorReader reader{bytesFromString("hello"), 64};
  ASSERT_TRUE(succeeded(m_core->put_object(extora::bucket_name{"photos"}, extora::object_key{"zero-range.txt"}, reader,
                                           extora::object_metadata{}, ignoredPutResult())));

  extora::open_object_options options;
  options.range = extora::byte_range{extora::byte_range_type::offset_length, 2, 0};

  VectorWriter writer;
  ASSERT_TRUE(succeeded(readObject(*m_core, extora::bucket_name{"photos"}, extora::object_key{"zero-range.txt"}, writer,
                                   options, ignoredOpenObjectResult())));

  EXPECT_TRUE(writer.bytes().empty());
}

TEST_F(StoreCoreTest, RejectsOutOfRangeReads)
{
  VectorReader reader{bytesFromString("hello"), 64};
  ASSERT_TRUE(succeeded(m_core->put_object(extora::bucket_name{"photos"}, extora::object_key{"invalid-range.txt"},
                                           reader, extora::object_metadata{}, ignoredPutResult())));

  extora::open_object_options offsetOptions;
  offsetOptions.range = extora::byte_range{extora::byte_range_type::offset_length, 5, 1};
  VectorWriter offsetWriter;
  extora::storage_error error =
      readObject(*m_core, extora::bucket_name{"photos"}, extora::object_key{"invalid-range.txt"}, offsetWriter,
                 offsetOptions, ignoredOpenObjectResult());
  EXPECT_EQ(error.code, extora::storage_error_code::invalid_range);

  extora::open_object_options lengthOptions;
  lengthOptions.range = extora::byte_range{extora::byte_range_type::offset_length, 3, 3};
  VectorWriter lengthWriter;
  error = readObject(*m_core, extora::bucket_name{"photos"}, extora::object_key{"invalid-range.txt"}, lengthWriter,
                     lengthOptions, ignoredOpenObjectResult());
  EXPECT_EQ(error.code, extora::storage_error_code::invalid_range);
}

TEST_F(StoreCoreTest, ReadsZeroLengthRangeFromEmptyObject)
{
  VectorReader reader{{}, 16};
  extora::object_metadata metadata;
  extora::put_object_options putOptions;
  putOptions.expected_content_length = 0;
  ASSERT_TRUE(succeeded(m_core->put_object(extora::bucket_name{"photos"}, extora::object_key{"empty-range"}, reader,
                                           metadata, ignoredPutResult(), putOptions)));

  extora::open_object_options options;
  options.range = extora::byte_range{extora::byte_range_type::offset_length, 0, 0};

  VectorWriter writer;
  ASSERT_TRUE(succeeded(readObject(*m_core, extora::bucket_name{"photos"}, extora::object_key{"empty-range"}, writer,
                                   options, ignoredOpenObjectResult())));
  EXPECT_TRUE(writer.bytes().empty());
}

TEST_F(StoreCoreTest, RejectsNonEmptyRangeFromEmptyObject)
{
  VectorReader reader{{}, 16};
  extora::object_metadata metadata;
  extora::put_object_options putOptions;
  putOptions.expected_content_length = 0;
  ASSERT_TRUE(succeeded(m_core->put_object(extora::bucket_name{"photos"}, extora::object_key{"empty-invalid-range"},
                                           reader, metadata, ignoredPutResult(), putOptions)));

  extora::open_object_options options;
  options.range = extora::byte_range{extora::byte_range_type::offset_length, 0, 1};

  VectorWriter writer;
  const extora::storage_error error =
      readObject(*m_core, extora::bucket_name{"photos"}, extora::object_key{"empty-invalid-range"}, writer, options,
                 ignoredOpenObjectResult());
  EXPECT_EQ(error.code, extora::storage_error_code::invalid_range);
}

TEST_F(StoreCoreTest, StatObjectReturnsMetadata)
{
  const std::string source = "metadata object";
  VectorReader reader{bytesFromString(source), 5};
  extora::object_metadata metadata;
  metadata.content_type = "text/plain";
  extora::put_object_options putOptions;
  putOptions.expected_content_length = source.size();
  putOptions.expected_checksum =
      extora::object_checksum{extora::checksum_algorithm_name{"test-sum"}, testChecksumValue(source)};

  ASSERT_TRUE(succeeded(m_core->put_object(extora::bucket_name{"photos"}, extora::object_key{"metadata.txt"}, reader,
                                           metadata, ignoredPutResult(), putOptions)));

  extora::object_info info;
  ASSERT_TRUE(succeeded(m_core->head_object(extora::bucket_name{"photos"}, extora::object_key{"metadata.txt"}, info)));

  EXPECT_EQ(info.key.value, "metadata.txt");
  EXPECT_EQ(info.content_length, source.size());
  ASSERT_TRUE(info.content_type.has_value());
  EXPECT_EQ(*info.content_type, "text/plain");
  ASSERT_TRUE(info.checksum.has_value());
  EXPECT_EQ(info.checksum->checksum_algorithm.value, "test-sum");
  EXPECT_EQ(info.checksum->value, testChecksumValue(source));
  EXPECT_NE(info.created_at, std::chrono::system_clock::time_point{});
  EXPECT_NE(info.modified_at, std::chrono::system_clock::time_point{});
}

TEST_F(StoreCoreTest, StatMissingObjectReturnsStructuredError)
{
  extora::object_info info;
  const extora::storage_error error =
      m_core->head_object(extora::bucket_name{"photos"}, extora::object_key{"missing"}, info);

  EXPECT_EQ(error.code, extora::storage_error_code::object_not_found);
}

TEST_F(StoreCoreTest, PutsAndGetsMultiChunkObject)
{
  std::string source;
  for (int i = 0; i < 20000; ++i) {
    source.push_back(static_cast<char>('a' + (i % 26)));
  }

  VectorReader reader{bytesFromString(source), 17};
  extora::object_metadata metadata;

  ASSERT_TRUE(succeeded(m_core->put_object(extora::bucket_name{"photos"}, extora::object_key{"large.bin"}, reader,
                                           metadata, ignoredPutResult())));

  VectorWriter writer;
  ASSERT_TRUE(succeeded(readObject(*m_core, extora::bucket_name{"photos"}, extora::object_key{"large.bin"}, writer,
                                   extora::open_object_options{}, ignoredOpenObjectResult())));

  EXPECT_EQ(stringFromBytes(writer.bytes()), source);
}

TEST_F(StoreCoreTest, MissingObjectReturnsStructuredError)
{
  VectorWriter writer;
  const extora::storage_error error = readObject(*m_core, extora::bucket_name{"photos"}, extora::object_key{"missing"},
                                                 writer, extora::open_object_options{}, ignoredOpenObjectResult());

  EXPECT_EQ(error.code, extora::storage_error_code::object_not_found);
}

} // namespace extoraTest
