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
#include "SqliteTestSupport.h"

namespace extoraTest {

TEST_F(StoreCoreTest, OverwritePublishesNewGenerationAndMarksOldExtentGarbage)
{
  VectorReader firstReader{bytesFromString("first"), 16};
  extora::object_metadata firstMetadata;
  extora::put_object_options firstOptions;
  firstOptions.expected_content_length = 5;
  ASSERT_TRUE(succeeded(m_core->put_object(extora::bucket_name{"photos"}, extora::object_key{"overwrite.txt"},
                                           firstReader, firstMetadata, ignoredPutResult(), firstOptions)));

  extora::object_info cachedInfo;
  ASSERT_TRUE(
      succeeded(m_core->head_object(extora::bucket_name{"photos"}, extora::object_key{"overwrite.txt"}, cachedInfo)));
  ASSERT_EQ(cachedInfo.content_length, 5);

  VectorReader secondReader{bytesFromString("second"), 16};
  extora::object_metadata secondMetadata;
  extora::put_object_options secondOptions;
  secondOptions.expected_content_length = 6;
  ASSERT_TRUE(succeeded(m_core->put_object(extora::bucket_name{"photos"}, extora::object_key{"overwrite.txt"},
                                           secondReader, secondMetadata, ignoredPutResult(), secondOptions)));

  VectorWriter writer;
  ASSERT_TRUE(succeeded(readObject(*m_core, extora::bucket_name{"photos"}, extora::object_key{"overwrite.txt"}, writer,
                                   extora::open_object_options{}, ignoredOpenObjectResult())));
  EXPECT_EQ(stringFromBytes(writer.bytes()), "second");

  extora::object_info info;
  ASSERT_TRUE(succeeded(m_core->head_object(extora::bucket_name{"photos"}, extora::object_key{"overwrite.txt"}, info)));
  EXPECT_EQ(info.content_length, 6);

  EXPECT_EQ(countPhysicalExtentsInState(joinPath(m_root, "index.sqlite3"), sqliteExtentStateGarbage), 1);
  EXPECT_EQ(countPhysicalExtentsInState(joinPath(m_root, "index.sqlite3"), sqliteExtentStateCommitted), 1);
}

TEST_F(StoreCoreTest, RejectsPutWhenIfNoneMatchWildcardFindsVisibleObject)
{
  VectorReader firstReader{bytesFromString("first"), 16};
  ASSERT_TRUE(succeeded(m_core->put_object(extora::bucket_name{"photos"}, extora::object_key{"create-only.txt"},
                                           firstReader, extora::object_metadata{}, ignoredPutResult())));

  VectorReader secondReader{bytesFromString("second"), 16};
  extora::put_object_options options;
  options.conditions.if_none_match_etag = extora::etag_wildcard;
  const extora::storage_error error =
      m_core->put_object(extora::bucket_name{"photos"}, extora::object_key{"create-only.txt"}, secondReader,
                         extora::object_metadata{}, ignoredPutResult(), options);
  EXPECT_EQ(error.code, extora::storage_error_code::precondition_failed);

  VectorWriter writer;
  ASSERT_TRUE(succeeded(readObject(*m_core, extora::bucket_name{"photos"}, extora::object_key{"create-only.txt"},
                                   writer, extora::open_object_options{}, ignoredOpenObjectResult())));
  EXPECT_EQ(stringFromBytes(writer.bytes()), "first");
}

TEST_F(StoreCoreTest, DeleteObjectHidesObjectAndMarksExtentGarbage)
{
  VectorReader reader{bytesFromString("deleted"), 16};
  ASSERT_TRUE(succeeded(m_core->put_object(extora::bucket_name{"photos"}, extora::object_key{"delete-me.txt"}, reader,
                                           extora::object_metadata{}, ignoredPutResult())));

  extora::object_info cachedInfo;
  ASSERT_TRUE(
      succeeded(m_core->head_object(extora::bucket_name{"photos"}, extora::object_key{"delete-me.txt"}, cachedInfo)));

  ASSERT_TRUE(succeeded(m_core->delete_object(extora::bucket_name{"photos"}, extora::object_key{"delete-me.txt"},
                                              ignoredDeleteResult())));

  VectorWriter writer;
  const extora::storage_error getError =
      readObject(*m_core, extora::bucket_name{"photos"}, extora::object_key{"delete-me.txt"}, writer,
                 extora::open_object_options{}, ignoredOpenObjectResult());
  EXPECT_EQ(getError.code, extora::storage_error_code::object_not_found);

  extora::object_info info;
  const extora::storage_error statError =
      m_core->head_object(extora::bucket_name{"photos"}, extora::object_key{"delete-me.txt"}, info);
  EXPECT_EQ(statError.code, extora::storage_error_code::object_not_found);

  EXPECT_EQ(countPhysicalExtentsInState(joinPath(m_root, "index.sqlite3"), sqliteExtentStateGarbage), 1);
  EXPECT_EQ(countPhysicalExtentsInState(joinPath(m_root, "index.sqlite3"), sqliteExtentStateCommitted), 0);
}

TEST_F(StoreCoreTest, DeleteMissingUnversionedObjectIsIdempotent)
{
  const extora::storage_error error = m_core->delete_object(
      extora::bucket_name{"photos"}, extora::object_key{"missing-delete.txt"}, ignoredDeleteResult());
  EXPECT_TRUE(succeeded(error));
}

TEST_F(StoreCoreTest, RepeatedUnversionedDeleteIsIdempotent)
{
  VectorReader reader{bytesFromString("deleted"), 16};
  ASSERT_TRUE(succeeded(m_core->put_object(extora::bucket_name{"photos"}, extora::object_key{"delete-twice.txt"},
                                           reader, extora::object_metadata{}, ignoredPutResult())));

  ASSERT_TRUE(succeeded(m_core->delete_object(extora::bucket_name{"photos"}, extora::object_key{"delete-twice.txt"},
                                              ignoredDeleteResult())));

  const extora::storage_error error = m_core->delete_object(
      extora::bucket_name{"photos"}, extora::object_key{"delete-twice.txt"}, ignoredDeleteResult());
  EXPECT_TRUE(succeeded(error));
}

TEST_F(StoreCoreTest, ContentLengthMismatchDoesNotPublishObject)
{
  VectorReader reader{bytesFromString("short"), 16};
  extora::object_metadata metadata;
  extora::put_object_options options;
  options.expected_content_length = 100;

  const extora::storage_error putError = m_core->put_object(extora::bucket_name{"photos"}, extora::object_key{"bad"},
                                                            reader, metadata, ignoredPutResult(), options);
  EXPECT_EQ(putError.code, extora::storage_error_code::source_failure);

  VectorWriter writer;
  const extora::storage_error getError = readObject(*m_core, extora::bucket_name{"photos"}, extora::object_key{"bad"},
                                                    writer, extora::open_object_options{}, ignoredOpenObjectResult());
  EXPECT_EQ(getError.code, extora::storage_error_code::object_not_found);
}

} // namespace extoraTest
