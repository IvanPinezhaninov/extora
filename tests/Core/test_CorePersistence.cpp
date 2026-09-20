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

#include <gtest/gtest.h>

#include "CoreTestSupport.h"
#include "PublicTestSupport.h"
#include "SqliteTestSupport.h"
#include "extora/core/object_store_core.h"
#include "extora/core/segment_data_store.h"
#include "extora/core/sqlite_object_index.h"

namespace extoraTest {

TEST(StoreCorePersistenceTest, ReadsStrictMultiExtentObjectAfterReopeningSqliteIndex)
{
  const std::string root = makeTempRoot();
  ASSERT_FALSE(root.empty());

  {
    extora::core::sqlite_object_index index{joinPath(root, "index.sqlite3"), 1024 * 1024,
                                            extora::storage_durability::strict};
    ASSERT_TRUE(succeeded(index.open()));

    extora::core::segment_data_store dataStore{root, 1024 * 1024, extora::storage_durability::strict};
    ASSERT_TRUE(succeeded(dataStore.open()));

    extora::core::object_store_core_options options;
    options.max_extent_size = 4;
    extora::core::object_store_core core{index, dataStore, defaultCoreHasherFactory(), options};
    ASSERT_TRUE(succeeded(core.create_bucket(extora::bucket_name{"photos"})));

    VectorReader reader{bytesFromString("persistent object"), 5};
    extora::object_metadata metadata;
    extora::put_object_options putOptions;
    putOptions.expected_content_length = 17;
    ASSERT_TRUE(succeeded(core.put_object(extora::bucket_name{"photos"}, extora::object_key{"persisted"}, reader,
                                          metadata, ignoredPutResult(), putOptions)));
  }

  extora::core::sqlite_object_index index{joinPath(root, "index.sqlite3"), 1024 * 1024,
                                          extora::storage_durability::strict};
  ASSERT_TRUE(succeeded(index.open()));

  extora::core::segment_data_store dataStore{root, 1024 * 1024, extora::storage_durability::strict};
  ASSERT_TRUE(succeeded(dataStore.open()));

  extora::core::object_store_core core{index, dataStore, defaultCoreHasherFactory()};
  VectorWriter writer;
  ASSERT_TRUE(succeeded(readObject(core, extora::bucket_name{"photos"}, extora::object_key{"persisted"}, writer,
                                   extora::open_object_options{}, ignoredOpenObjectResult())));

  EXPECT_EQ(stringFromBytes(writer.bytes()), "persistent object");
}

TEST(StoreCorePersistenceTest, ReadsEmptyObjectWithoutExtentManifestAfterReopen)
{
  const std::string root = makeTempRoot();
  ASSERT_FALSE(root.empty());

  {
    extora::core::sqlite_object_index index{joinPath(root, "index.sqlite3"), 1024 * 1024};
    ASSERT_TRUE(succeeded(index.open()));
    extora::core::segment_data_store dataStore{root, 1024 * 1024};
    ASSERT_TRUE(succeeded(dataStore.open()));
    extora::core::object_store_core core{index, dataStore, defaultCoreHasherFactory()};
    ASSERT_TRUE(succeeded(core.create_bucket(extora::bucket_name{"photos"})));

    VectorReader reader{{}, 1};
    extora::object_metadata metadata;
    extora::put_object_options options;
    options.expected_content_length = 0;
    ASSERT_TRUE(succeeded(core.put_object(extora::bucket_name{"photos"}, extora::object_key{"empty"}, reader, metadata,
                                          ignoredPutResult(), options)));
  }

  extora::core::sqlite_object_index index{joinPath(root, "index.sqlite3"), 1024 * 1024};
  ASSERT_TRUE(succeeded(index.open()));
  extora::core::segment_data_store dataStore{root, 1024 * 1024};
  ASSERT_TRUE(succeeded(dataStore.open()));
  extora::core::object_store_core core{index, dataStore, defaultCoreHasherFactory()};

  VectorWriter writer;
  ASSERT_TRUE(succeeded(readObject(core, extora::bucket_name{"photos"}, extora::object_key{"empty"}, writer,
                                   extora::open_object_options{}, ignoredOpenObjectResult())));
  EXPECT_TRUE(writer.bytes().empty());
}

TEST(StoreCorePersistenceTest, RejectsExtentManifestShorterThanPayloadLength)
{
  const std::string root = makeTempRoot();
  ASSERT_FALSE(root.empty());

  {
    extora::core::sqlite_object_index index{joinPath(root, "index.sqlite3"), 1024 * 1024};
    ASSERT_TRUE(succeeded(index.open()));
    extora::core::segment_data_store dataStore{root, 1024 * 1024};
    ASSERT_TRUE(succeeded(dataStore.open()));
    extora::core::object_store_core core{index, dataStore, defaultCoreHasherFactory()};
    ASSERT_TRUE(succeeded(core.create_bucket(extora::bucket_name{"photos"})));

    VectorReader reader{bytesFromString("data"), 4};
    ASSERT_TRUE(succeeded(core.put_object(extora::bucket_name{"photos"}, extora::object_key{"object"}, reader,
                                          extora::object_metadata{}, ignoredPutResult())));
  }

  ASSERT_TRUE(
      executeSql(joinPath(root, "index.sqlite3"), "UPDATE object_payloads SET content_length = content_length + 1"));

  extora::core::sqlite_object_index index{joinPath(root, "index.sqlite3"), 1024 * 1024};
  ASSERT_TRUE(succeeded(index.open()));
  extora::core::segment_data_store dataStore{root, 1024 * 1024};
  ASSERT_TRUE(succeeded(dataStore.open()));
  extora::core::object_store_core core{index, dataStore, defaultCoreHasherFactory()};

  VectorWriter writer;
  const extora::storage_error error = readObject(core, extora::bucket_name{"photos"}, extora::object_key{"object"},
                                                 writer, extora::open_object_options{}, ignoredOpenObjectResult());
  EXPECT_EQ(error.code, extora::storage_error_code::backend_failure);
}

} // namespace extoraTest
