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

#include <memory>
#include <string>
#include <string_view>

#include <gtest/gtest.h>

#include "PublicTestSupport.h"
#include "SqliteTestSupport.h"

namespace extoraTest {

namespace {

constexpr std::string_view abcXxh3 = "06b05ab6733a618578af5f94892f3950";
constexpr std::string_view emptyXxh3 = "99aa06d3014798d86001c324468d497f";

class RejectingExtensionFactory final : public extora::hasher_factory {
public:
  std::unique_ptr<extora::hasher> create_hasher(const extora::checksum_algorithm_name&,
                                                extora::storage_error& error) override
  {
    ++m_calls;
    error =
        extora::make_error(extora::storage_error_code::backend_failure, "extension factory must not handle xxh3-128");
    return {};
  }

  std::size_t calls() const
  {
    return m_calls;
  }

private:
  std::size_t m_calls = 0;
};

} // namespace

TEST(StorageHashingTest, ProvidesBuiltInChecksumWithoutExtensionFactory)
{
  extora::object_store_options options;
  options.root_directory = makeTempRoot();

  extora::storage_error error;
  std::unique_ptr<extora::object_store> store = extora::open_object_store(options, error);
  ASSERT_TRUE(succeeded(error)) << error.message;
  ASSERT_TRUE(store);
  ASSERT_TRUE(succeeded(store->create_bucket(extora::bucket_name{"photos"})));

  VectorReader reader{bytesFromString("abc"), 1};
  extora::object_metadata metadata;
  extora::put_object_options putOptions;
  putOptions.expected_checksum = extora::object_checksum{extora::xxh3_128_checksum_algorithm, std::string{abcXxh3}};
  extora::put_object_result putResult;
  ASSERT_TRUE(succeeded(store->put_object(extora::bucket_name{"photos"}, extora::object_key{"object"}, reader, metadata,
                                          putResult, putOptions)));

  EXPECT_EQ(putResult.checksum.checksum_algorithm.value, extora::xxh3_128_checksum_algorithm.value);
  EXPECT_EQ(putResult.checksum.value, abcXxh3);

  VectorWriter writer;
  extora::open_object_options getOptions;
  getOptions.verify_integrity = true;
  ASSERT_TRUE(succeeded(readObject(*store, extora::bucket_name{"photos"}, extora::object_key{"object"}, writer,
                                   getOptions, ignoredOpenObjectResult())));
  EXPECT_EQ(stringFromBytes(writer.bytes()), "abc");
}

TEST(StorageHashingTest, UsesBuiltInHasherForContentEtags)
{
  extora::object_store_options options;
  options.root_directory = makeTempRoot();

  extora::storage_error error;
  std::unique_ptr<extora::object_store> store = extora::open_object_store(options, error);
  ASSERT_TRUE(succeeded(error)) << error.message;
  ASSERT_TRUE(store);
  ASSERT_TRUE(succeeded(store->create_bucket(extora::bucket_name{"photos"})));

  VectorReader firstReader{bytesFromString("abc"), 2};
  extora::put_object_result first;
  ASSERT_TRUE(succeeded(store->put_object(extora::bucket_name{"photos"}, extora::object_key{"first"}, firstReader,
                                          extora::object_metadata{}, first)));

  VectorReader secondReader{bytesFromString("abc"), 1};
  extora::put_object_result second;
  ASSERT_TRUE(succeeded(store->put_object(extora::bucket_name{"photos"}, extora::object_key{"second"}, secondReader,
                                          extora::object_metadata{}, second)));

  EXPECT_EQ(first.etag, "xxh3-128:" + std::string{abcXxh3});
  EXPECT_EQ(second.etag, first.etag);
}

TEST(StorageHashingTest, HashesEmptyPayloadWithCanonicalXxh3)
{
  const std::string root = makeTempRoot();
  extora::object_store_options options;
  options.root_directory = root;

  extora::storage_error error;
  std::unique_ptr<extora::object_store> store = extora::open_object_store(options, error);
  ASSERT_TRUE(succeeded(error)) << error.message;
  ASSERT_TRUE(store);
  ASSERT_TRUE(succeeded(store->create_bucket(extora::bucket_name{"photos"})));

  VectorReader reader{{}, 1};
  extora::put_object_result result;
  ASSERT_TRUE(succeeded(store->put_object(extora::bucket_name{"photos"}, extora::object_key{"empty"}, reader,
                                          extora::object_metadata{}, result)));

  EXPECT_EQ(result.checksum.checksum_algorithm.value, extora::xxh3_128_checksum_algorithm.value);
  EXPECT_EQ(result.checksum.value, emptyXxh3);
  EXPECT_EQ(result.etag, "xxh3-128:" + std::string{emptyXxh3});
  EXPECT_EQ(scalarTextQuery(joinPath(root, "index.sqlite3"), "SELECT internal_checksum_value FROM object_payloads"),
            emptyXxh3);
}

TEST(StorageHashingTest, BuiltInAlgorithmPrecedesExtensionFactory)
{
  const std::string root = makeTempRoot();
  const std::shared_ptr<RejectingExtensionFactory> extensionFactory = std::make_shared<RejectingExtensionFactory>();
  extora::object_store_options options;
  options.root_directory = root;
  options.custom_hasher_factory = extensionFactory;
  options.dedup_min_object_size = 1;

  extora::storage_error error;
  std::unique_ptr<extora::object_store> store = extora::open_object_store(options, error);
  ASSERT_TRUE(succeeded(error)) << error.message;
  ASSERT_TRUE(store);
  ASSERT_TRUE(succeeded(store->create_bucket(extora::bucket_name{"photos"})));

  extora::object_metadata metadata;
  extora::put_object_options putOptions;
  putOptions.expected_checksum = extora::object_checksum{extora::xxh3_128_checksum_algorithm, std::string{abcXxh3}};
  extora::put_object_result firstResult;
  VectorReader firstReader{bytesFromString("abc"), 1};
  ASSERT_TRUE(succeeded(store->put_object(extora::bucket_name{"photos"}, extora::object_key{"first"}, firstReader,
                                          metadata, firstResult, putOptions)));
  EXPECT_EQ(firstResult.checksum.value, abcXxh3);
  EXPECT_EQ(firstResult.etag, "xxh3-128:" + std::string{abcXxh3});

  VectorReader secondReader{bytesFromString("abc"), 2};
  ASSERT_TRUE(succeeded(store->put_object(extora::bucket_name{"photos"}, extora::object_key{"second"}, secondReader,
                                          metadata, ignoredPutResult())));
  EXPECT_EQ(extensionFactory->calls(), 0u);
  const std::string databasePath = joinPath(root, "index.sqlite3");
  EXPECT_EQ(scalarQuery(databasePath, "SELECT COUNT(*) FROM object_payloads"), 1);
  EXPECT_EQ(scalarTextQuery(databasePath, "SELECT internal_checksum_algorithm FROM object_payloads"),
            extora::xxh3_128_checksum_algorithm.value);
  EXPECT_EQ(scalarTextQuery(databasePath, "SELECT internal_checksum_value FROM object_payloads"), abcXxh3);

  store.reset();
  store = extora::open_object_store(options, error);
  ASSERT_TRUE(succeeded(error)) << error.message;
  ASSERT_TRUE(store);
  VectorWriter writer;
  extora::open_object_options getOptions;
  getOptions.verify_integrity = true;
  ASSERT_TRUE(succeeded(readObject(*store, extora::bucket_name{"photos"}, extora::object_key{"second"}, writer,
                                   getOptions, ignoredOpenObjectResult())));
  EXPECT_EQ(stringFromBytes(writer.bytes()), "abc");
  EXPECT_EQ(extensionFactory->calls(), 0u);
}

TEST(StorageHashingTest, InternalChecksumBecomesDefaultPublicChecksum)
{
  const std::string root = makeTempRoot();
  const std::shared_ptr<RejectingExtensionFactory> extensionFactory = std::make_shared<RejectingExtensionFactory>();
  extora::object_store_options options;
  options.root_directory = root;
  options.custom_hasher_factory = extensionFactory;

  extora::storage_error error;
  std::unique_ptr<extora::object_store> store = extora::open_object_store(options, error);
  ASSERT_TRUE(succeeded(error)) << error.message;
  ASSERT_TRUE(store);
  ASSERT_TRUE(succeeded(store->create_bucket(extora::bucket_name{"photos"})));

  VectorReader reader{bytesFromString("abc"), 3};
  ASSERT_TRUE(succeeded(store->put_object(extora::bucket_name{"photos"}, extora::object_key{"internal-only"}, reader,
                                          extora::object_metadata{}, ignoredPutResult())));
  EXPECT_EQ(extensionFactory->calls(), 0u);
  EXPECT_EQ(scalarTextQuery(joinPath(root, "index.sqlite3"), "SELECT internal_checksum_value FROM object_payloads"),
            abcXxh3);

  store.reset();
  store = extora::open_object_store(options, error);
  ASSERT_TRUE(succeeded(error)) << error.message;
  ASSERT_TRUE(store);
  extora::object_info info;
  ASSERT_TRUE(succeeded(store->head_object(extora::bucket_name{"photos"}, extora::object_key{"internal-only"}, info)));
  ASSERT_TRUE(info.checksum.has_value());
  EXPECT_EQ(info.checksum->checksum_algorithm.value, extora::xxh3_128_checksum_algorithm.value);
  EXPECT_EQ(info.checksum->value, abcXxh3);
  EXPECT_EQ(extensionFactory->calls(), 0u);
}

} // namespace extoraTest
