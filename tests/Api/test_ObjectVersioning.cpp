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

#include <string_view>

#include "ApiTestSupport.h"
#include "PublicTestSupport.h"

namespace extoraTest {

TEST_F(ObjectStoreApiTest, ReportsAndChangesBucketVersioningStatus)
{
  extora::bucket_versioning_status status = extora::bucket_versioning_status::enabled;
  ASSERT_TRUE(succeeded(m_store->get_bucket_versioning(extora::bucket_name{"photos"}, status)));
  EXPECT_EQ(status, extora::bucket_versioning_status::unversioned);

  ASSERT_TRUE(succeeded(
      m_store->set_bucket_versioning(extora::bucket_name{"photos"}, extora::bucket_versioning_configuration::enabled)));
  ASSERT_TRUE(succeeded(m_store->get_bucket_versioning(extora::bucket_name{"photos"}, status)));
  EXPECT_EQ(status, extora::bucket_versioning_status::enabled);

  ASSERT_TRUE(succeeded(m_store->set_bucket_versioning(extora::bucket_name{"photos"},
                                                       extora::bucket_versioning_configuration::suspended)));
  ASSERT_TRUE(succeeded(m_store->get_bucket_versioning(extora::bucket_name{"photos"}, status)));
  EXPECT_EQ(status, extora::bucket_versioning_status::suspended);
}

TEST_F(ObjectStoreApiTest, PreservesTheNullVersionWhenVersioningIsEnabled)
{
  extora::put_object_result original;
  ASSERT_TRUE(succeeded(putText(*m_store, "photos", "object", "unversioned", {}, {}, &original)));
  EXPECT_EQ(original.version_id.value, extora::null_version_id);
  ASSERT_TRUE(succeeded(
      m_store->set_bucket_versioning(extora::bucket_name{"photos"}, extora::bucket_versioning_configuration::enabled)));
  extora::put_object_result current;
  ASSERT_TRUE(succeeded(putText(*m_store, "photos", "object", "versioned", {}, {}, &current)));
  EXPECT_NE(current.version_id.value, extora::null_version_id);

  extora::open_object_options options;
  options.version_id = extora::object_version_id{extora::null_version_id};
  std::string text;
  ASSERT_TRUE(succeeded(readText(*m_store, "photos", "object", text, options)));
  EXPECT_EQ(text, "unversioned");

  extora::object_version_list versions;
  ASSERT_TRUE(succeeded(m_store->list_object_versions(extora::bucket_name{"photos"}, versions)));
  ASSERT_EQ(versions.versions.size(), 2u);
  EXPECT_EQ(versions.versions[0].version_id.value, current.version_id.value);
  EXPECT_TRUE(versions.versions[0].is_latest);
  EXPECT_EQ(versions.versions[1].version_id.value, extora::null_version_id);
}

TEST_F(ObjectStoreApiTest, IncludesCustomMetadataInObjectVersionListings)
{
  ASSERT_TRUE(succeeded(
      m_store->set_bucket_versioning(extora::bucket_name{"photos"}, extora::bucket_versioning_configuration::enabled)));
  extora::object_metadata metadata;
  metadata.custom_metadata.push_back(extora::metadata_entry{"owner", "api-test"});
  ASSERT_TRUE(succeeded(putText(*m_store, "photos", "object", "body", metadata)));

  extora::object_version_list result;
  ASSERT_TRUE(succeeded(m_store->list_object_versions(extora::bucket_name{"photos"}, result)));
  ASSERT_EQ(result.versions.size(), 1u);
  ASSERT_EQ(result.versions[0].custom_metadata.size(), 1u);
  EXPECT_EQ(result.versions[0].custom_metadata[0].name, metadata.custom_metadata[0].name);
  EXPECT_EQ(result.versions[0].custom_metadata[0].value, metadata.custom_metadata[0].value);
}

TEST_F(ObjectStoreApiTest, UsesDeleteMarkersAndRestoresThePreviousVersion)
{
  ASSERT_TRUE(succeeded(
      m_store->set_bucket_versioning(extora::bucket_name{"photos"}, extora::bucket_versioning_configuration::enabled)));
  extora::put_object_result first;
  extora::put_object_result second;
  ASSERT_TRUE(succeeded(putText(*m_store, "photos", "object", "first", {}, {}, &first)));
  ASSERT_TRUE(succeeded(putText(*m_store, "photos", "object", "second", {}, {}, &second)));

  extora::delete_object_result marker;
  ASSERT_TRUE(succeeded(m_store->delete_object(extora::bucket_name{"photos"}, extora::object_key{"object"}, marker)));
  EXPECT_TRUE(marker.is_delete_marker);

  extora::open_object_result markerResult;
  EXPECT_EQ(m_store->open_object(extora::bucket_name{"photos"}, extora::object_key{"object"}, markerResult).code,
            extora::storage_error_code::object_is_delete_marker);
  EXPECT_TRUE(markerResult.object.is_delete_marker);
  EXPECT_EQ(markerResult.object.version_id.value, marker.version_id.value);
  EXPECT_EQ(markerResult.reader, nullptr);

  extora::head_object_options markerHeadOptions;
  markerHeadOptions.version_id = marker.version_id;
  extora::object_info markerInfo;
  EXPECT_EQ(
      m_store->head_object(extora::bucket_name{"photos"}, extora::object_key{"object"}, markerInfo, markerHeadOptions)
          .code,
      extora::storage_error_code::object_is_delete_marker);
  EXPECT_TRUE(markerInfo.is_delete_marker);
  EXPECT_EQ(markerInfo.version_id.value, marker.version_id.value);

  extora::open_object_options missingVersionOptions;
  missingVersionOptions.version_id = extora::object_version_id{"missing-version"};
  extora::open_object_result missingVersionResult;
  EXPECT_EQ(m_store
                ->open_object(extora::bucket_name{"photos"}, extora::object_key{"object"}, missingVersionResult,
                              missingVersionOptions)
                .code,
            extora::storage_error_code::object_version_not_found);
  EXPECT_TRUE(missingVersionResult.object.key.value.empty());
  EXPECT_EQ(missingVersionResult.reader, nullptr);

  extora::delete_object_options missingDeleteOptions;
  missingDeleteOptions.version_id = extora::object_version_id{"missing-version"};
  extora::delete_object_result missingDeleteResult;
  EXPECT_EQ(m_store
                ->delete_object(extora::bucket_name{"photos"}, extora::object_key{"object"}, missingDeleteResult,
                                missingDeleteOptions)
                .code,
            extora::storage_error_code::object_version_not_found);

  extora::delete_object_options deleteMarker;
  deleteMarker.version_id = marker.version_id;
  ASSERT_TRUE(succeeded(m_store->delete_object(extora::bucket_name{"photos"}, extora::object_key{"object"},
                                               ignoredDeleteResult(), deleteMarker)));
  std::string text;
  ASSERT_TRUE(succeeded(readText(*m_store, "photos", "object", text)));
  EXPECT_EQ(text, "second");

  extora::delete_object_options deleteSecond;
  deleteSecond.version_id = second.version_id;
  ASSERT_TRUE(succeeded(m_store->delete_object(extora::bucket_name{"photos"}, extora::object_key{"object"},
                                               ignoredDeleteResult(), deleteSecond)));
  ASSERT_TRUE(succeeded(readText(*m_store, "photos", "object", text)));
  EXPECT_EQ(text, "first");
}

TEST_F(ObjectStoreApiTest, ReplacesTheNullVersionWhileVersioningIsSuspended)
{
  ASSERT_TRUE(succeeded(
      m_store->set_bucket_versioning(extora::bucket_name{"photos"}, extora::bucket_versioning_configuration::enabled)));
  extora::put_object_result historical;
  ASSERT_TRUE(succeeded(putText(*m_store, "photos", "object", "historical", {}, {}, &historical)));
  ASSERT_TRUE(succeeded(m_store->set_bucket_versioning(extora::bucket_name{"photos"},
                                                       extora::bucket_versioning_configuration::suspended)));
  ASSERT_TRUE(succeeded(putText(*m_store, "photos", "object", "suspended-one")));
  ASSERT_TRUE(succeeded(putText(*m_store, "photos", "object", "suspended-two")));

  extora::object_version_list versions;
  ASSERT_TRUE(succeeded(m_store->list_object_versions(extora::bucket_name{"photos"}, versions)));
  ASSERT_EQ(versions.versions.size(), 2u);
  EXPECT_EQ(versions.versions[0].version_id.value, extora::null_version_id);
  EXPECT_EQ(versions.versions[1].version_id.value, historical.version_id.value);
}

TEST_F(ObjectStoreApiTest, FiltersAndPaginatesObjectVersions)
{
  ASSERT_TRUE(succeeded(
      m_store->set_bucket_versioning(extora::bucket_name{"photos"}, extora::bucket_versioning_configuration::enabled)));
  for (std::string_view key : {"a/one", "a/two", "b/one"}) {
    ASSERT_TRUE(succeeded(putText(*m_store, "photos", key, "first")));
    ASSERT_TRUE(succeeded(putText(*m_store, "photos", key, "second")));
  }

  extora::list_object_versions_options options;
  options.prefix = "a/";
  options.max_keys = 2;
  extora::object_version_list first;
  ASSERT_TRUE(succeeded(m_store->list_object_versions(extora::bucket_name{"photos"}, first, options)));
  ASSERT_EQ(first.versions.size(), 2u);
  EXPECT_TRUE(first.is_truncated);
  ASSERT_TRUE(first.next_key_marker.has_value());
  ASSERT_TRUE(first.next_version_id_marker.has_value());

  options.key_marker = *first.next_key_marker;
  options.version_id_marker = *first.next_version_id_marker;
  extora::object_version_list second;
  ASSERT_TRUE(succeeded(m_store->list_object_versions(extora::bucket_name{"photos"}, second, options)));
  EXPECT_FALSE(second.versions.empty());
  for (const extora::object_info& version : second.versions)
    EXPECT_EQ(version.key.value.compare(0, 2, "a/"), 0);
  EXPECT_FALSE(second.is_truncated);
  EXPECT_FALSE(second.next_key_marker.has_value());
  EXPECT_FALSE(second.next_version_id_marker.has_value());
}

TEST_F(ObjectStoreApiTest, GroupsAndPaginatesObjectVersionsWithDelimiter)
{
  ASSERT_TRUE(succeeded(
      m_store->set_bucket_versioning(extora::bucket_name{"photos"}, extora::bucket_versioning_configuration::enabled)));
  for (std::string_view key : {"a/b/one", "a/b/two", "a/c/one", "a/root", "outside"}) {
    ASSERT_TRUE(succeeded(putText(*m_store, "photos", key, "first")));
    ASSERT_TRUE(succeeded(putText(*m_store, "photos", key, "second")));
  }

  extora::list_object_versions_options options;
  options.prefix = "a/";
  options.delimiter = "/";
  options.max_keys = 2;

  extora::object_version_list first;
  ASSERT_TRUE(succeeded(m_store->list_object_versions(extora::bucket_name{"photos"}, first, options)));
  EXPECT_TRUE(first.versions.empty());
  EXPECT_EQ(first.common_prefixes, (std::vector<std::string>{"a/b/", "a/c/"}));
  EXPECT_TRUE(first.is_truncated);
  ASSERT_TRUE(first.next_key_marker.has_value());
  ASSERT_TRUE(first.next_version_id_marker.has_value());
  EXPECT_FALSE(first.next_key_marker->empty());
  EXPECT_FALSE(first.next_version_id_marker->value.empty());

  options.key_marker = *first.next_key_marker;
  options.version_id_marker = *first.next_version_id_marker;
  extora::object_version_list second;
  ASSERT_TRUE(succeeded(m_store->list_object_versions(extora::bucket_name{"photos"}, second, options)));
  ASSERT_EQ(second.versions.size(), 2u);
  EXPECT_TRUE(second.common_prefixes.empty());
  EXPECT_EQ(second.versions[0].key.value, "a/root");
  EXPECT_EQ(second.versions[1].key.value, "a/root");
  EXPECT_TRUE(second.versions[0].is_latest);
  EXPECT_FALSE(second.versions[1].is_latest);
  EXPECT_FALSE(second.is_truncated);
  EXPECT_FALSE(second.next_key_marker.has_value());
  EXPECT_FALSE(second.next_version_id_marker.has_value());
}

TEST_F(ObjectStoreApiTest, FiltersVersionCommonPrefixesAtKeyMarker)
{
  ASSERT_TRUE(succeeded(
      m_store->set_bucket_versioning(extora::bucket_name{"photos"}, extora::bucket_versioning_configuration::enabled)));
  for (std::string_view key : {"a/b/first", "a/b/zeta", "a/c/first"})
    ASSERT_TRUE(succeeded(putText(*m_store, "photos", key, key)));

  extora::list_object_versions_options options;
  options.prefix = "a/";
  options.delimiter = "/";
  options.key_marker = "a/b/middle";
  extora::object_version_list result;
  ASSERT_TRUE(succeeded(m_store->list_object_versions(extora::bucket_name{"photos"}, result, options)));
  EXPECT_TRUE(result.versions.empty());
  EXPECT_EQ(result.common_prefixes, (std::vector<std::string>{"a/c/"}));
}

} // namespace extoraTest
