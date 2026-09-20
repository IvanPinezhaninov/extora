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

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

#if defined(_WIN32)
#include <process.h>
#endif // defined(_WIN32)

#include <gtest/gtest.h>

#include "PublicTestSupport.h"
#include "SqliteTestSupport.h"

namespace extoraTest {

namespace {

#if defined(_WIN32)
constexpr char crashRecoveryParentProcessIdVariable[] = "EXTORA_CRASH_RECOVERY_PARENT_PROCESS_ID";
constexpr char crashRecoveryRootVariable[] = "EXTORA_CRASH_RECOVERY_ROOT";

bool readEnvironmentVariable(const char* name, std::string& value)
{
  char* buffer = nullptr;
  std::size_t size = 0;
  if (::_dupenv_s(&buffer, &size, name) != 0 || buffer == nullptr) return false;

  value = buffer;
  std::free(buffer);
  return true;
}

bool isReexecutedDeathTestChild()
{
  std::string parentProcessId;
  return readEnvironmentVariable(crashRecoveryParentProcessIdVariable, parentProcessId) &&
         parentProcessId != std::to_string(::_getpid());
}
#else
bool isReexecutedDeathTestChild()
{
  return false;
}
#endif // defined(_WIN32)

std::string makeCrashRecoveryRoot()
{
#if defined(_WIN32)
  if (isReexecutedDeathTestChild()) {
    std::string inheritedRoot;
    if (!readEnvironmentVariable(crashRecoveryRootVariable, inheritedRoot)) return {};
    return inheritedRoot;
  }

  const std::string root = makeTempRoot();
  if (::_putenv_s(crashRecoveryRootVariable, root.c_str()) != 0 ||
      ::_putenv_s(crashRecoveryParentProcessIdVariable, std::to_string(::_getpid()).c_str()) != 0)
    return {};
  return root;
#else
  return makeTempRoot();
#endif // defined(_WIN32)
}

class CrashAfterFirstChunkReader final : public extora::object_reader {
public:
  extora::object_read_result read(std::byte* data, std::size_t size) override
  {
    if (m_firstChunkWritten) std::_Exit(23);

    constexpr char chunk[] = "partial";
    const std::size_t bytesRead = std::min(size, sizeof(chunk) - 1);
    std::memcpy(data, chunk, bytesRead);
    m_firstChunkWritten = true;
    return extora::object_read_result{bytesRead, false, {}};
  }

private:
  bool m_firstChunkWritten = false;
};

bool createBucket(extora::object_store& store)
{
  return succeeded(store.create_bucket(extora::bucket_name{"photos"}));
}

extora::storage_error putText(extora::object_store& store, std::string_view key, std::string_view content)
{
  VectorReader reader{bytesFromString(content), 3};
  return store.put_object(extora::bucket_name{"photos"}, extora::object_key{std::string{key}}, reader,
                          extora::object_metadata{}, ignoredPutResult());
}

extora::storage_error readText(extora::object_store& store, std::string_view key, std::string& content)
{
  VectorWriter writer;
  const extora::storage_error error =
      readObject(store, extora::bucket_name{"photos"}, extora::object_key{std::string{key}}, writer,
                 extora::open_object_options{}, ignoredOpenObjectResult());
  if (succeeded(error)) content = stringFromBytes(writer.bytes());
  return error;
}

[[noreturn]] void runInterruptedWriteChild(const extora::object_store_options& options)
{
  extora::storage_error error;
  std::unique_ptr<extora::object_store> store = extora::open_object_store(options, error);
  if (failed(error) || !store || !createBucket(*store)) std::_Exit(20);

  CrashAfterFirstChunkReader reader;
  static_cast<void>(store->put_object(extora::bucket_name{"photos"}, extora::object_key{"interrupted"}, reader,
                                      extora::object_metadata{}, ignoredPutResult()));
  std::_Exit(21);
}

[[noreturn]] void runCommittedPutChild(const extora::object_store_options& options)
{
  extora::storage_error error;
  std::unique_ptr<extora::object_store> store = extora::open_object_store(options, error);
  if (failed(error) || !store || !createBucket(*store)) std::_Exit(30);

  VectorReader reader{bytesFromString("wal-body"), 2};
  if (failed(store->put_object(extora::bucket_name{"photos"}, extora::object_key{"committed"}, reader,
                               extora::object_metadata{}, ignoredPutResult())))
    std::_Exit(31);
  std::_Exit(0);
}

[[noreturn]] void runInterruptedOverwriteChild(const extora::object_store_options& options)
{
  extora::storage_error error;
  std::unique_ptr<extora::object_store> store = extora::open_object_store(options, error);
  if (failed(error) || !store) std::_Exit(50);

  CrashAfterFirstChunkReader reader;
  static_cast<void>(store->put_object(extora::bucket_name{"photos"}, extora::object_key{"stable"}, reader,
                                      extora::object_metadata{}, ignoredPutResult()));
  std::_Exit(51);
}

[[noreturn]] void runCommittedMutationsChild(const extora::object_store_options& options)
{
  extora::storage_error error;
  std::unique_ptr<extora::object_store> store = extora::open_object_store(options, error);
  if (failed(error) || !store) std::_Exit(60);

  VectorReader reader{bytesFromString("replacement"), 3};
  if (failed(store->put_object(extora::bucket_name{"photos"}, extora::object_key{"stable"}, reader,
                               extora::object_metadata{}, ignoredPutResult())))
    std::_Exit(61);
  if (failed(store->delete_object(extora::bucket_name{"photos"}, extora::object_key{"deleted"}, ignoredDeleteResult())))
    std::_Exit(62);
  std::_Exit(0);
}

[[noreturn]] void runMultipartPartChild(const extora::object_store_options& options,
                                        const extora::create_multipart_upload_result& upload)
{
  extora::storage_error error;
  std::unique_ptr<extora::object_store> store = extora::open_object_store(options, error);
  if (failed(error) || !store) std::_Exit(40);

  VectorReader reader{bytesFromString("part-body"), 3};
  if (failed(store->upload_part(extora::bucket_name{"photos"}, extora::object_key{"archive"}, upload.upload_id, 1,
                                reader, ignoredUploadPartResult())))
    std::_Exit(41);
  std::_Exit(0);
}

} // namespace

TEST(StorageCrashRecoveryTest, ReclaimsWriteInterruptedAfterFirstChunk)
{
  const std::string root = makeCrashRecoveryRoot();
  ASSERT_FALSE(root.empty());
  extora::object_store_options options;
  options.root_directory = root;
  options.segment_capacity = 16;
  options.durability = extora::storage_durability::strict;

  ASSERT_EXIT(runInterruptedWriteChild(options), testing::ExitedWithCode(23), "");

  extora::storage_error error;
  std::unique_ptr<extora::object_store> recovered = extora::open_object_store(options, error);
  ASSERT_TRUE(succeeded(error)) << error.message;
  ASSERT_TRUE(recovered);

  extora::object_info info;
  EXPECT_EQ(recovered->head_object(extora::bucket_name{"photos"}, extora::object_key{"interrupted"}, info).code,
            extora::storage_error_code::object_not_found);
  const std::string databasePath = joinPath(root, "index.sqlite3");
  EXPECT_EQ(countPhysicalExtentsInState(databasePath, sqliteExtentStateReserved), 0);
  EXPECT_EQ(countPhysicalExtentsInState(databasePath, sqliteExtentStateFree), 1);

  recovered.reset();
  std::unique_ptr<extora::object_store> reopened = extora::open_object_store(options, error);
  ASSERT_TRUE(succeeded(error)) << error.message;
  ASSERT_TRUE(reopened);
  EXPECT_EQ(countPhysicalExtentsInState(databasePath, sqliteExtentStateFree), 1);
}

TEST(StorageCrashRecoveryTest, ReplaysCommittedPutWithoutCleanShutdown)
{
  const std::string root = makeCrashRecoveryRoot();
  ASSERT_FALSE(root.empty());
  extora::object_store_options options;
  options.root_directory = root;
  options.segment_capacity = 16;
  options.durability = extora::storage_durability::strict;

  ASSERT_EXIT(runCommittedPutChild(options), testing::ExitedWithCode(0), "");

  extora::storage_error error;
  std::unique_ptr<extora::object_store> recovered = extora::open_object_store(options, error);
  ASSERT_TRUE(succeeded(error)) << error.message;
  ASSERT_TRUE(recovered);

  VectorWriter writer;
  ASSERT_TRUE(succeeded(readObject(*recovered, extora::bucket_name{"photos"}, extora::object_key{"committed"}, writer,
                                   extora::open_object_options{}, ignoredOpenObjectResult())));
  EXPECT_EQ(stringFromBytes(writer.bytes()), "wal-body");
}

TEST(StorageCrashRecoveryTest, KeepsPublishedObjectAfterInterruptedOverwrite)
{
  const std::string root = makeCrashRecoveryRoot();
  ASSERT_FALSE(root.empty());
  extora::object_store_options options;
  options.root_directory = root;
  options.segment_capacity = 32;
  options.durability = extora::storage_durability::strict;

  if (!isReexecutedDeathTestChild()) {
    extora::storage_error error;
    std::unique_ptr<extora::object_store> store = extora::open_object_store(options, error);
    ASSERT_TRUE(succeeded(error)) << error.message;
    ASSERT_TRUE(store);
    ASSERT_TRUE(createBucket(*store));
    ASSERT_TRUE(succeeded(putText(*store, "stable", "original")));
  }

  ASSERT_EXIT(runInterruptedOverwriteChild(options), testing::ExitedWithCode(23), "");

  extora::storage_error error;
  std::unique_ptr<extora::object_store> recovered = extora::open_object_store(options, error);
  ASSERT_TRUE(succeeded(error)) << error.message;
  ASSERT_TRUE(recovered);

  std::string content;
  ASSERT_TRUE(succeeded(readText(*recovered, "stable", content)));
  EXPECT_EQ(content, "original");
  EXPECT_EQ(countPhysicalExtentsInState(joinPath(root, "index.sqlite3"), sqliteExtentStateReserved), 0);
}

TEST(StorageCrashRecoveryTest, ReplaysCommittedOverwriteAndDelete)
{
  const std::string root = makeCrashRecoveryRoot();
  ASSERT_FALSE(root.empty());
  extora::object_store_options options;
  options.root_directory = root;
  options.segment_capacity = 32;
  options.durability = extora::storage_durability::strict;

  if (!isReexecutedDeathTestChild()) {
    extora::storage_error error;
    std::unique_ptr<extora::object_store> store = extora::open_object_store(options, error);
    ASSERT_TRUE(succeeded(error)) << error.message;
    ASSERT_TRUE(store);
    ASSERT_TRUE(createBucket(*store));
    ASSERT_TRUE(succeeded(putText(*store, "stable", "original")));
    ASSERT_TRUE(succeeded(putText(*store, "deleted", "obsolete")));
  }

  ASSERT_EXIT(runCommittedMutationsChild(options), testing::ExitedWithCode(0), "");

  extora::storage_error error;
  std::unique_ptr<extora::object_store> recovered = extora::open_object_store(options, error);
  ASSERT_TRUE(succeeded(error)) << error.message;
  ASSERT_TRUE(recovered);

  std::string content;
  ASSERT_TRUE(succeeded(readText(*recovered, "stable", content)));
  EXPECT_EQ(content, "replacement");
  EXPECT_EQ(readText(*recovered, "deleted", content).code, extora::storage_error_code::object_not_found);
}

TEST(StorageCrashRecoveryTest, ReplaysMultipartPartWithoutCleanShutdown)
{
  const std::string root = makeCrashRecoveryRoot();
  ASSERT_FALSE(root.empty());
  extora::object_store_options options;
  options.root_directory = root;
  options.segment_capacity = 32;
  options.durability = extora::storage_durability::strict;
  extora::create_multipart_upload_result upload;
  if (!isReexecutedDeathTestChild()) {
    extora::storage_error error;
    std::unique_ptr<extora::object_store> store = extora::open_object_store(options, error);
    ASSERT_TRUE(succeeded(error)) << error.message;
    ASSERT_TRUE(store);
    ASSERT_TRUE(createBucket(*store));
    ASSERT_TRUE(succeeded(store->create_multipart_upload(extora::bucket_name{"photos"}, extora::object_key{"archive"},
                                                         extora::object_metadata{}, upload)));
#if defined(_WIN32)
    ASSERT_EQ(::_putenv_s("EXTORA_CRASH_RECOVERY_UPLOAD_ID", upload.upload_id.value.c_str()), 0);
#endif // defined(_WIN32)
  }
#if defined(_WIN32)
  else {
    ASSERT_TRUE(readEnvironmentVariable("EXTORA_CRASH_RECOVERY_UPLOAD_ID", upload.upload_id.value));
  }
#endif // defined(_WIN32)

  ASSERT_EXIT(runMultipartPartChild(options, upload), testing::ExitedWithCode(0), "");

  extora::storage_error error;
  std::unique_ptr<extora::object_store> recovered = extora::open_object_store(options, error);
  ASSERT_TRUE(succeeded(error)) << error.message;
  ASSERT_TRUE(recovered);

  extora::multipart_part_list parts;
  ASSERT_TRUE(succeeded(
      recovered->list_parts(extora::bucket_name{"photos"}, extora::object_key{"archive"}, upload.upload_id, parts)));
  ASSERT_EQ(parts.parts.size(), 1u);

  extora::complete_multipart_upload_options completeOptions;
  completeOptions.parts.push_back(extora::completed_multipart_part{1, parts.parts[0].etag, std::nullopt});
  ASSERT_TRUE(
      succeeded(recovered->complete_multipart_upload(extora::bucket_name{"photos"}, extora::object_key{"archive"},
                                                     upload.upload_id, completeOptions, ignoredPutResult())));
  recovered.reset();

  std::unique_ptr<extora::object_store> reopened = extora::open_object_store(options, error);
  ASSERT_TRUE(succeeded(error)) << error.message;
  ASSERT_TRUE(reopened);
  VectorWriter writer;
  ASSERT_TRUE(succeeded(readObject(*reopened, extora::bucket_name{"photos"}, extora::object_key{"archive"}, writer,
                                   extora::open_object_options{}, ignoredOpenObjectResult())));
  EXPECT_EQ(stringFromBytes(writer.bytes()), "part-body");
}

} // namespace extoraTest
