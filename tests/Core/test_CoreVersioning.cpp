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

TEST_F(StoreCoreTest, UsesNullVersionBeforeVersioningAndPreservesItAfterEnabling)
{
  VectorReader unversionedReader{bytesFromString("unversioned"), 3};
  extora::put_object_result unversionedResult;
  ASSERT_TRUE(succeeded(m_core->put_object(extora::bucket_name{"photos"}, extora::object_key{"object"},
                                           unversionedReader, extora::object_metadata{}, unversionedResult)));
  EXPECT_EQ(unversionedResult.version_id.value, extora::null_version_id);

  extora::object_info unversionedInfo;
  ASSERT_TRUE(
      succeeded(m_core->head_object(extora::bucket_name{"photos"}, extora::object_key{"object"}, unversionedInfo)));
  EXPECT_EQ(unversionedInfo.version_id.value, extora::null_version_id);

  extora::object_version_list unversionedVersions;
  ASSERT_TRUE(succeeded(m_core->list_object_versions(extora::bucket_name{"photos"}, unversionedVersions)));
  ASSERT_EQ(unversionedVersions.versions.size(), 1);
  EXPECT_EQ(unversionedVersions.versions[0].version_id.value, extora::null_version_id);
  EXPECT_TRUE(unversionedVersions.versions[0].is_latest);

  ASSERT_TRUE(succeeded(
      m_core->set_bucket_versioning(extora::bucket_name{"photos"}, extora::bucket_versioning_configuration::enabled)));
  VectorReader versionedReader{bytesFromString("versioned"), 3};
  extora::put_object_result versionedResult;
  ASSERT_TRUE(succeeded(m_core->put_object(extora::bucket_name{"photos"}, extora::object_key{"object"}, versionedReader,
                                           extora::object_metadata{}, versionedResult)));
  EXPECT_EQ(versionedResult.version_id.value.compare(0, 2, "v_"), 0);
  EXPECT_NE(versionedResult.version_id.value, extora::null_version_id);

  extora::object_version_list enabledVersions;
  ASSERT_TRUE(succeeded(m_core->list_object_versions(extora::bucket_name{"photos"}, enabledVersions)));
  ASSERT_EQ(enabledVersions.versions.size(), 2);
  EXPECT_EQ(enabledVersions.versions[0].version_id.value, versionedResult.version_id.value);
  EXPECT_TRUE(enabledVersions.versions[0].is_latest);
  EXPECT_EQ(enabledVersions.versions[1].version_id.value, extora::null_version_id);
  EXPECT_FALSE(enabledVersions.versions[1].is_latest);

  VectorWriter latestWriter;
  ASSERT_TRUE(succeeded(readObject(*m_core, extora::bucket_name{"photos"}, extora::object_key{"object"}, latestWriter,
                                   extora::open_object_options{}, ignoredOpenObjectResult())));
  EXPECT_EQ(stringFromBytes(latestWriter.bytes()), "versioned");

  extora::open_object_options nullOptions;
  nullOptions.version_id = extora::object_version_id{extora::null_version_id};
  VectorWriter nullWriter;
  ASSERT_TRUE(succeeded(readObject(*m_core, extora::bucket_name{"photos"}, extora::object_key{"object"}, nullWriter,
                                   nullOptions, ignoredOpenObjectResult())));
  EXPECT_EQ(stringFromBytes(nullWriter.bytes()), "unversioned");

  extora::head_object_options nullStatOptions;
  nullStatOptions.version_id = extora::object_version_id{extora::null_version_id};
  extora::object_info nullInfo;
  ASSERT_TRUE(succeeded(
      m_core->head_object(extora::bucket_name{"photos"}, extora::object_key{"object"}, nullInfo, nullStatOptions)));
  EXPECT_EQ(nullInfo.version_id.value, extora::null_version_id);

  extora::open_object_options emptyVersionOptions;
  emptyVersionOptions.version_id = extora::object_version_id{};
  VectorWriter emptyVersionWriter;
  EXPECT_EQ(readObject(*m_core, extora::bucket_name{"photos"}, extora::object_key{"object"}, emptyVersionWriter,
                       emptyVersionOptions, ignoredOpenObjectResult())
                .code,
            extora::storage_error_code::object_version_not_found);

  extora::delete_object_options emptyDeleteOptions;
  emptyDeleteOptions.version_id = extora::object_version_id{};
  extora::delete_object_result emptyDeleteResult;
  EXPECT_EQ(m_core
                ->delete_object(extora::bucket_name{"photos"}, extora::object_key{"object"}, emptyDeleteResult,
                                emptyDeleteOptions)
                .code,
            extora::storage_error_code::object_version_not_found);

  extora::delete_object_options deleteNullOptions;
  deleteNullOptions.version_id = extora::object_version_id{extora::null_version_id};
  extora::delete_object_result deleteNullResult;
  ASSERT_TRUE(succeeded(m_core->delete_object(extora::bucket_name{"photos"}, extora::object_key{"object"},
                                              deleteNullResult, deleteNullOptions)));
  EXPECT_EQ(deleteNullResult.version_id.value, extora::null_version_id);
  EXPECT_FALSE(deleteNullResult.is_delete_marker);

  VectorWriter deletedNullWriter;
  EXPECT_EQ(readObject(*m_core, extora::bucket_name{"photos"}, extora::object_key{"object"}, deletedNullWriter,
                       nullOptions, ignoredOpenObjectResult())
                .code,
            extora::storage_error_code::object_version_not_found);
}

TEST_F(StoreCoreTest, UnversionedDeletePermanentlyRemovesNullVersion)
{
  VectorReader reader{bytesFromString("object"), 3};
  extora::put_object_result putResult;
  ASSERT_TRUE(succeeded(m_core->put_object(extora::bucket_name{"photos"}, extora::object_key{"object"}, reader,
                                           extora::object_metadata{}, putResult)));
  ASSERT_EQ(putResult.version_id.value, extora::null_version_id);

  VectorReader replacementReader{bytesFromString("replacement"), 3};
  extora::put_object_result replacementResult;
  ASSERT_TRUE(succeeded(m_core->put_object(extora::bucket_name{"photos"}, extora::object_key{"object"},
                                           replacementReader, extora::object_metadata{}, replacementResult)));
  EXPECT_EQ(replacementResult.version_id.value, extora::null_version_id);

  extora::object_version_list replacedVersions;
  ASSERT_TRUE(succeeded(m_core->list_object_versions(extora::bucket_name{"photos"}, replacedVersions)));
  ASSERT_EQ(replacedVersions.versions.size(), 1);
  EXPECT_EQ(replacedVersions.versions[0].version_id.value, extora::null_version_id);

  extora::delete_object_result deleteResult;
  ASSERT_TRUE(
      succeeded(m_core->delete_object(extora::bucket_name{"photos"}, extora::object_key{"object"}, deleteResult)));
  EXPECT_EQ(deleteResult.version_id.value, extora::null_version_id);
  EXPECT_FALSE(deleteResult.is_delete_marker);

  extora::object_version_list versions;
  ASSERT_TRUE(succeeded(m_core->list_object_versions(extora::bucket_name{"photos"}, versions)));
  EXPECT_TRUE(versions.versions.empty());
}

TEST_F(StoreCoreTest, CopyUsesTargetBucketVersioningSemantics)
{
  VectorReader sourceReader{bytesFromString("source"), 3};
  ASSERT_TRUE(succeeded(m_core->put_object(extora::bucket_name{"photos"}, extora::object_key{"source"}, sourceReader,
                                           extora::object_metadata{}, ignoredPutResult())));

  extora::copy_object_result unversionedResult;
  ASSERT_TRUE(succeeded(m_core->copy_object(extora::bucket_name{"photos"}, extora::object_key{"source"},
                                            extora::bucket_name{"photos"}, extora::object_key{"unversioned-target"},
                                            unversionedResult)));
  EXPECT_EQ(unversionedResult.version_id.value, extora::null_version_id);

  ASSERT_TRUE(succeeded(
      m_core->set_bucket_versioning(extora::bucket_name{"photos"}, extora::bucket_versioning_configuration::enabled)));
  extora::copy_object_result enabledResult;
  ASSERT_TRUE(succeeded(m_core->copy_object(extora::bucket_name{"photos"}, extora::object_key{"source"},
                                            extora::bucket_name{"photos"}, extora::object_key{"enabled-target"},
                                            enabledResult)));
  EXPECT_FALSE(enabledResult.version_id.value.empty());
  EXPECT_NE(enabledResult.version_id.value, extora::null_version_id);
  EXPECT_EQ(enabledResult.version_id.value.compare(0, 2, "v_"), 0);

  ASSERT_TRUE(succeeded(m_core->set_bucket_versioning(extora::bucket_name{"photos"},
                                                      extora::bucket_versioning_configuration::suspended)));
  extora::copy_object_result suspendedResult;
  ASSERT_TRUE(succeeded(m_core->copy_object(extora::bucket_name{"photos"}, extora::object_key{"source"},
                                            extora::bucket_name{"photos"}, extora::object_key{"suspended-target"},
                                            suspendedResult)));
  EXPECT_EQ(suspendedResult.version_id.value, extora::null_version_id);
}

TEST_F(StoreCoreTest, SuspendedPutDirectlyReplacesPreEnableNullVersion)
{
  VectorReader originalReader{bytesFromString("original"), 3};
  ASSERT_TRUE(succeeded(m_core->put_object(extora::bucket_name{"photos"}, extora::object_key{"object"}, originalReader,
                                           extora::object_metadata{}, ignoredPutResult())));

  ASSERT_TRUE(succeeded(
      m_core->set_bucket_versioning(extora::bucket_name{"photos"}, extora::bucket_versioning_configuration::enabled)));
  ASSERT_TRUE(succeeded(m_core->set_bucket_versioning(extora::bucket_name{"photos"},
                                                      extora::bucket_versioning_configuration::suspended)));

  VectorReader replacementReader{bytesFromString("replacement"), 3};
  extora::put_object_result replacementResult;
  ASSERT_TRUE(succeeded(m_core->put_object(extora::bucket_name{"photos"}, extora::object_key{"object"},
                                           replacementReader, extora::object_metadata{}, replacementResult)));
  EXPECT_EQ(replacementResult.version_id.value, extora::null_version_id);

  extora::object_version_list versions;
  ASSERT_TRUE(succeeded(m_core->list_object_versions(extora::bucket_name{"photos"}, versions)));
  ASSERT_EQ(versions.versions.size(), 1);
  EXPECT_EQ(versions.versions[0].version_id.value, extora::null_version_id);
  EXPECT_TRUE(versions.versions[0].is_latest);

  extora::open_object_options options;
  options.version_id = extora::object_version_id{extora::null_version_id};
  VectorWriter writer;
  ASSERT_TRUE(succeeded(readObject(*m_core, extora::bucket_name{"photos"}, extora::object_key{"object"}, writer,
                                   options, ignoredOpenObjectResult())));
  EXPECT_EQ(stringFromBytes(writer.bytes()), "replacement");
}

TEST_F(StoreCoreTest, PreservesVersionsAndUsesDeleteMarkers)
{
  ASSERT_TRUE(succeeded(
      m_core->set_bucket_versioning(extora::bucket_name{"photos"}, extora::bucket_versioning_configuration::enabled)));
  extora::bucket_versioning_status bucketStatus = extora::bucket_versioning_status::unversioned;
  ASSERT_TRUE(succeeded(m_core->get_bucket_versioning(extora::bucket_name{"photos"}, bucketStatus)));
  EXPECT_EQ(bucketStatus, extora::bucket_versioning_status::enabled);
  EXPECT_TRUE(succeeded(
      m_core->set_bucket_versioning(extora::bucket_name{"photos"}, extora::bucket_versioning_configuration::enabled)));

  VectorReader firstReader{bytesFromString("first"), 2};
  extora::put_object_result firstResult;
  ASSERT_TRUE(succeeded(m_core->put_object(extora::bucket_name{"photos"}, extora::object_key{"versioned"}, firstReader,
                                           extora::object_metadata{}, firstResult)));

  VectorReader secondReader{bytesFromString("second"), 2};
  extora::put_object_result secondResult;
  ASSERT_TRUE(succeeded(m_core->put_object(extora::bucket_name{"photos"}, extora::object_key{"versioned"}, secondReader,
                                           extora::object_metadata{}, secondResult)));
  EXPECT_NE(firstResult.version_id.value, secondResult.version_id.value);

  extora::open_object_options firstOptions;
  firstOptions.version_id = firstResult.version_id;
  VectorWriter firstWriter;
  ASSERT_TRUE(succeeded(readObject(*m_core, extora::bucket_name{"photos"}, extora::object_key{"versioned"}, firstWriter,
                                   firstOptions, ignoredOpenObjectResult())));
  EXPECT_EQ(stringFromBytes(firstWriter.bytes()), "first");

  extora::delete_object_result markerResult;
  ASSERT_TRUE(
      succeeded(m_core->delete_object(extora::bucket_name{"photos"}, extora::object_key{"versioned"}, markerResult)));
  EXPECT_TRUE(markerResult.is_delete_marker);
  EXPECT_FALSE(markerResult.version_id.value.empty());

  VectorWriter latestWriter;
  EXPECT_EQ(readObject(*m_core, extora::bucket_name{"photos"}, extora::object_key{"versioned"}, latestWriter,
                       extora::open_object_options{}, ignoredOpenObjectResult())
                .code,
            extora::storage_error_code::object_is_delete_marker);

  extora::object_version_list versions;
  ASSERT_TRUE(succeeded(m_core->list_object_versions(extora::bucket_name{"photos"}, versions)));
  ASSERT_EQ(versions.versions.size(), 3);
  EXPECT_TRUE(versions.versions[0].is_delete_marker);
  EXPECT_TRUE(versions.versions[0].is_latest);

  extora::delete_object_options deleteMarker;
  deleteMarker.version_id = markerResult.version_id;
  extora::delete_object_result deletedMarker;
  ASSERT_TRUE(succeeded(m_core->delete_object(extora::bucket_name{"photos"}, extora::object_key{"versioned"},
                                              deletedMarker, deleteMarker)));
  EXPECT_TRUE(deletedMarker.is_delete_marker);

  VectorWriter restoredWriter;
  ASSERT_TRUE(succeeded(readObject(*m_core, extora::bucket_name{"photos"}, extora::object_key{"versioned"},
                                   restoredWriter, extora::open_object_options{}, ignoredOpenObjectResult())));
  EXPECT_EQ(stringFromBytes(restoredWriter.bytes()), "second");

  extora::delete_object_options deleteSecond;
  deleteSecond.version_id = secondResult.version_id;
  ASSERT_TRUE(succeeded(m_core->delete_object(extora::bucket_name{"photos"}, extora::object_key{"versioned"},
                                              ignoredDeleteResult(), deleteSecond)));

  VectorWriter firstLatestWriter;
  ASSERT_TRUE(succeeded(readObject(*m_core, extora::bucket_name{"photos"}, extora::object_key{"versioned"},
                                   firstLatestWriter, extora::open_object_options{}, ignoredOpenObjectResult())));
  EXPECT_EQ(stringFromBytes(firstLatestWriter.bytes()), "first");

  extora::open_object_options deletedSecondOptions;
  deletedSecondOptions.version_id = secondResult.version_id;
  VectorWriter deletedSecondWriter;
  EXPECT_EQ(readObject(*m_core, extora::bucket_name{"photos"}, extora::object_key{"versioned"}, deletedSecondWriter,
                       deletedSecondOptions, ignoredOpenObjectResult())
                .code,
            extora::storage_error_code::object_version_not_found);
}

TEST_F(StoreCoreTest, VersionedDeleteOfMissingKeyCreatesDeleteMarker)
{
  ASSERT_TRUE(succeeded(
      m_core->set_bucket_versioning(extora::bucket_name{"photos"}, extora::bucket_versioning_configuration::enabled)));
  extora::delete_object_result result;
  ASSERT_TRUE(succeeded(m_core->delete_object(extora::bucket_name{"photos"}, extora::object_key{"missing"}, result)));
  EXPECT_TRUE(result.is_delete_marker);

  extora::object_version_list versions;
  ASSERT_TRUE(succeeded(m_core->list_object_versions(extora::bucket_name{"photos"}, versions)));
  ASSERT_EQ(versions.versions.size(), 1);
  EXPECT_TRUE(versions.versions[0].is_delete_marker);
}

TEST_F(StoreCoreTest, SupportsSuspendedVersioningTransitionsAndPreservesHistory)
{
  EXPECT_TRUE(succeeded(m_core->set_bucket_versioning(extora::bucket_name{"photos"},
                                                      extora::bucket_versioning_configuration::suspended)));

  VectorReader originalNullReader{bytesFromString("original-null"), 3};
  extora::put_object_result originalNullResult;
  ASSERT_TRUE(succeeded(m_core->put_object(extora::bucket_name{"photos"}, extora::object_key{"object"},
                                           originalNullReader, extora::object_metadata{}, originalNullResult)));
  ASSERT_EQ(originalNullResult.version_id.value, extora::null_version_id);

  ASSERT_TRUE(succeeded(
      m_core->set_bucket_versioning(extora::bucket_name{"photos"}, extora::bucket_versioning_configuration::enabled)));

  VectorReader historicalReader{bytesFromString("historical"), 3};
  extora::put_object_result historicalResult;
  ASSERT_TRUE(succeeded(m_core->put_object(extora::bucket_name{"photos"}, extora::object_key{"object"},
                                           historicalReader, extora::object_metadata{}, historicalResult)));

  ASSERT_TRUE(succeeded(m_core->set_bucket_versioning(extora::bucket_name{"photos"},
                                                      extora::bucket_versioning_configuration::suspended)));
  ASSERT_TRUE(succeeded(m_core->set_bucket_versioning(extora::bucket_name{"photos"},
                                                      extora::bucket_versioning_configuration::suspended)));
  extora::bucket_versioning_status bucketStatus = extora::bucket_versioning_status::unversioned;
  ASSERT_TRUE(succeeded(m_core->get_bucket_versioning(extora::bucket_name{"photos"}, bucketStatus)));
  EXPECT_EQ(bucketStatus, extora::bucket_versioning_status::suspended);

  VectorReader firstReader{bytesFromString("suspended-one"), 3};
  extora::put_object_result firstSuspendedResult;
  ASSERT_TRUE(succeeded(m_core->put_object(extora::bucket_name{"photos"}, extora::object_key{"object"}, firstReader,
                                           extora::object_metadata{}, firstSuspendedResult)));
  EXPECT_EQ(firstSuspendedResult.version_id.value, extora::null_version_id);
  VectorReader secondReader{bytesFromString("suspended-two"), 3};
  extora::put_object_result secondSuspendedResult;
  ASSERT_TRUE(succeeded(m_core->put_object(extora::bucket_name{"photos"}, extora::object_key{"object"}, secondReader,
                                           extora::object_metadata{}, secondSuspendedResult)));
  EXPECT_EQ(secondSuspendedResult.version_id.value, extora::null_version_id);

  extora::object_version_list versions;
  ASSERT_TRUE(succeeded(m_core->list_object_versions(extora::bucket_name{"photos"}, versions)));
  ASSERT_EQ(versions.versions.size(), 2);
  EXPECT_EQ(versions.versions[0].version_id.value, extora::null_version_id);
  EXPECT_TRUE(versions.versions[0].is_latest);
  EXPECT_EQ(versions.versions[1].version_id.value, historicalResult.version_id.value);

  extora::open_object_options historicalOptions;
  historicalOptions.version_id = historicalResult.version_id;
  VectorWriter historicalWriter;
  ASSERT_TRUE(succeeded(readObject(*m_core, extora::bucket_name{"photos"}, extora::object_key{"object"},
                                   historicalWriter, historicalOptions, ignoredOpenObjectResult())));
  EXPECT_EQ(stringFromBytes(historicalWriter.bytes()), "historical");

  extora::open_object_options nullOptions;
  nullOptions.version_id = extora::object_version_id{extora::null_version_id};
  VectorWriter nullWriter;
  ASSERT_TRUE(succeeded(readObject(*m_core, extora::bucket_name{"photos"}, extora::object_key{"object"}, nullWriter,
                                   nullOptions, ignoredOpenObjectResult())));
  EXPECT_EQ(stringFromBytes(nullWriter.bytes()), "suspended-two");

  extora::delete_object_result markerResult;
  ASSERT_TRUE(
      succeeded(m_core->delete_object(extora::bucket_name{"photos"}, extora::object_key{"object"}, markerResult)));
  EXPECT_TRUE(markerResult.is_delete_marker);
  EXPECT_EQ(markerResult.version_id.value, extora::null_version_id);

  VectorWriter deletedLatestWriter;
  EXPECT_EQ(readObject(*m_core, extora::bucket_name{"photos"}, extora::object_key{"object"}, deletedLatestWriter,
                       extora::open_object_options{}, ignoredOpenObjectResult())
                .code,
            extora::storage_error_code::object_is_delete_marker);

  VectorWriter nullBehindMarkerWriter;
  EXPECT_EQ(readObject(*m_core, extora::bucket_name{"photos"}, extora::object_key{"object"}, nullBehindMarkerWriter,
                       nullOptions, ignoredOpenObjectResult())
                .code,
            extora::storage_error_code::object_is_delete_marker);

  extora::delete_object_options deleteNullOptions;
  deleteNullOptions.version_id = extora::object_version_id{extora::null_version_id};
  extora::delete_object_result deleteNullResult;
  ASSERT_TRUE(succeeded(m_core->delete_object(extora::bucket_name{"photos"}, extora::object_key{"object"},
                                              deleteNullResult, deleteNullOptions)));
  EXPECT_EQ(deleteNullResult.version_id.value, extora::null_version_id);

  VectorWriter removedNullWriter;
  EXPECT_EQ(readObject(*m_core, extora::bucket_name{"photos"}, extora::object_key{"object"}, removedNullWriter,
                       nullOptions, ignoredOpenObjectResult())
                .code,
            extora::storage_error_code::object_version_not_found);

  VectorWriter retainedHistoricalWriter;
  ASSERT_TRUE(succeeded(readObject(*m_core, extora::bucket_name{"photos"}, extora::object_key{"object"},
                                   retainedHistoricalWriter, historicalOptions, ignoredOpenObjectResult())));
  EXPECT_EQ(stringFromBytes(retainedHistoricalWriter.bytes()), "historical");

  ASSERT_TRUE(succeeded(
      m_core->set_bucket_versioning(extora::bucket_name{"photos"}, extora::bucket_versioning_configuration::enabled)));
  ASSERT_TRUE(succeeded(
      m_core->set_bucket_versioning(extora::bucket_name{"photos"}, extora::bucket_versioning_configuration::enabled)));
  bucketStatus = extora::bucket_versioning_status::unversioned;
  ASSERT_TRUE(succeeded(m_core->get_bucket_versioning(extora::bucket_name{"photos"}, bucketStatus)));
  EXPECT_EQ(bucketStatus, extora::bucket_versioning_status::enabled);
}

TEST_F(StoreCoreTest, CopiesAnExplicitSourceVersion)
{
  ASSERT_TRUE(succeeded(
      m_core->set_bucket_versioning(extora::bucket_name{"photos"}, extora::bucket_versioning_configuration::enabled)));
  VectorReader firstReader{bytesFromString("first"), 2};
  extora::put_object_result firstResult;
  ASSERT_TRUE(succeeded(m_core->put_object(extora::bucket_name{"photos"}, extora::object_key{"source"}, firstReader,
                                           extora::object_metadata{}, firstResult)));
  VectorReader secondReader{bytesFromString("second"), 2};
  ASSERT_TRUE(succeeded(m_core->put_object(extora::bucket_name{"photos"}, extora::object_key{"source"}, secondReader,
                                           extora::object_metadata{}, ignoredPutResult())));

  extora::copy_object_options options;
  options.source_version_id = firstResult.version_id;
  extora::copy_object_result result;
  ASSERT_TRUE(
      succeeded(m_core->copy_object(extora::bucket_name{"photos"}, extora::object_key{"source"},
                                    extora::bucket_name{"photos"}, extora::object_key{"target"}, result, options)));
  VectorWriter writer;
  ASSERT_TRUE(succeeded(readObject(*m_core, extora::bucket_name{"photos"}, extora::object_key{"target"}, writer,
                                   extora::open_object_options{}, ignoredOpenObjectResult())));
  EXPECT_EQ(stringFromBytes(writer.bytes()), "first");
}

} // namespace extoraTest
