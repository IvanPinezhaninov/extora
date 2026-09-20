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

#include <array>
#include <atomic>
#include <cstdio>
#include <filesystem>
#include <thread>
#include <vector>

#if defined(__linux__)
#include <sys/stat.h>
#endif // defined(__linux__)

#include <gtest/gtest.h>

#include "PublicTestSupport.h"
#include "extora/core/segment_data_store.h"

namespace extoraTest {

#if defined(__linux__)
namespace {

long long allocatedBlockCount(const std::filesystem::path& path)
{
  struct stat info;
  if (::stat(path.c_str(), &info) != 0) return -1;

  return static_cast<long long>(info.st_blocks);
}

} // namespace
#endif // defined(__linux__)

namespace {

extora::storage_error writeExtent(extora::core::segment_data_store& store, const extora::core::physical_extent& extent,
                                  std::uint64_t offset, const std::byte* data, std::size_t size)
{
  extora::core::data_write_handle handle;
  extora::storage_error error = store.begin_write(extent, handle);
  if (failed(error)) return error;

  error = store.write(handle, offset, data, size);
  store.finish_write(handle);
  return error;
}

extora::storage_error readExtent(extora::core::segment_data_store& store, const extora::core::physical_extent& extent,
                                 std::uint64_t offset, std::byte* data, std::size_t size, std::size_t& bytesRead)
{
  extora::core::data_read_handle handle;
  extora::storage_error error = store.begin_read(extent, handle);
  if (failed(error)) return error;

  error = store.read(handle, offset, data, size, bytesRead);
  store.finish_read(handle);
  return error;
}

} // namespace

TEST(SegmentDataStoreTest, NamesCreatedSegmentFilesByNumericId)
{
  const std::string root = makeTempRoot();
  ASSERT_FALSE(root.empty());

  extora::core::segment_data_store dataStore{root, 16};
  ASSERT_TRUE(succeeded(dataStore.open()));

  extora::core::physical_extent extent;
  extent.segment_id = 1;
  extent.length = 1;
  extora::core::data_write_handle handle;
  ASSERT_TRUE(succeeded(dataStore.begin_write(extent, handle)));
  dataStore.finish_write(handle);

  const std::string segments = joinPath(root, "segments");
  EXPECT_TRUE(regularFileExists(joinPath(segments, "0000000000000001.dat")));
  EXPECT_FALSE(regularFileExists(joinPath(segments, "0000000000000001.dat.creating")));
}

TEST(SegmentDataStoreTest, ReplacesAStaleIncompleteSegmentCreation)
{
  const std::filesystem::path root = makeTempRoot();
  const std::filesystem::path segments = root / "segments";
  ASSERT_TRUE(std::filesystem::create_directories(segments));
  const std::filesystem::path creationPath = segments / "0000000000000001.dat.creating";
  {
#if defined(_MSC_VER)
    std::FILE* file = nullptr;
    ::_wfopen_s(&file, creationPath.c_str(), L"wb");
#else
    std::FILE* file = std::fopen(creationPath.string().c_str(), "wb");
#endif // defined(_MSC_VER)
    ASSERT_NE(file, nullptr);
    ASSERT_EQ(std::fputc(0, file), 0);
    ASSERT_EQ(std::fclose(file), 0);
  }

  extora::core::segment_data_store dataStore{root, 16, extora::storage_durability::relaxed};
  ASSERT_TRUE(succeeded(dataStore.open()));

  extora::core::physical_extent extent;
  extent.segment_id = 1;
  extent.length = 1;
  extora::core::data_write_handle handle;
  ASSERT_TRUE(succeeded(dataStore.begin_write(extent, handle)));
  dataStore.finish_write(handle);

  EXPECT_FALSE(std::filesystem::exists(creationPath));
  EXPECT_EQ(std::filesystem::file_size(segments / "0000000000000001.dat"), 16u);
}

#if defined(__linux__)
TEST(SegmentDataStoreTest, RespectsSegmentAllocationStrategy)
{
  const std::string sparseRoot = makeTempRoot();
  ASSERT_FALSE(sparseRoot.empty());

  extora::core::segment_data_store sparseDataStore{sparseRoot, 1024 * 1024, extora::storage_durability::relaxed,
                                                   extora::segment_allocation_strategy::sparse};
  ASSERT_TRUE(succeeded(sparseDataStore.open()));
  extora::core::physical_extent sparseExtent;
  sparseExtent.segment_id = 1;
  sparseExtent.length = 1;
  extora::core::data_write_handle sparseHandle;
  ASSERT_TRUE(succeeded(sparseDataStore.begin_write(sparseExtent, sparseHandle)));
  sparseDataStore.finish_write(sparseHandle);

  const std::string sparseSegment = joinPath(joinPath(sparseRoot, "segments"), "0000000000000001.dat");
  ASSERT_TRUE(regularFileExists(sparseSegment));
  EXPECT_EQ(allocatedBlockCount(sparseSegment), 0);

  const std::string reservedRoot = makeTempRoot();
  ASSERT_FALSE(reservedRoot.empty());

  extora::core::segment_data_store reservedDataStore{reservedRoot, 1024 * 1024, extora::storage_durability::relaxed,
                                                     extora::segment_allocation_strategy::reserve};
  ASSERT_TRUE(succeeded(reservedDataStore.open()));
  extora::core::physical_extent reservedExtent;
  reservedExtent.segment_id = 1;
  reservedExtent.length = 1;
  extora::core::data_write_handle reservedHandle;
  ASSERT_TRUE(succeeded(reservedDataStore.begin_write(reservedExtent, reservedHandle)));
  reservedDataStore.finish_write(reservedHandle);

  const std::string reservedSegment = joinPath(joinPath(reservedRoot, "segments"), "0000000000000001.dat");
  ASSERT_TRUE(regularFileExists(reservedSegment));
  EXPECT_GT(allocatedBlockCount(reservedSegment), 0);
}

#endif // defined(__linux__)

TEST(SegmentDataStoreTest, KeepsLengthSeparateFromSegmentCoordinates)
{
  const std::string root = makeTempRoot();
  ASSERT_FALSE(root.empty());

  extora::core::segment_data_store dataStore{root, 16};
  ASSERT_TRUE(succeeded(dataStore.open()));

  const std::vector<std::byte> bytes = bytesFromString("abc");
  extora::core::physical_extent storage;
  storage.segment_id = 1;
  storage.offset = 0;
  storage.length = 3;
  ASSERT_TRUE(succeeded(writeExtent(dataStore, storage, 0, bytes.data(), bytes.size())));
  ASSERT_TRUE(succeeded(dataStore.flush(storage.segment_id)));

  EXPECT_EQ(storage.segment_id, 1);
  EXPECT_EQ(storage.offset, 0);
  EXPECT_EQ(storage.length, 3);

  std::byte buffer[3];
  std::size_t bytesRead = 0;
  ASSERT_TRUE(succeeded(readExtent(dataStore, storage, 0, buffer, sizeof(buffer), bytesRead)));
  EXPECT_EQ(bytesRead, 3);
}

TEST(SegmentDataStoreTest, ReadsAndWritesDisjointExtentsConcurrently)
{
  constexpr std::size_t threadCount = 8;
  static constexpr std::size_t extentSize = 4096;

  const std::string root = makeTempRoot();
  ASSERT_FALSE(root.empty());

  extora::core::segment_data_store dataStore{root, threadCount * extentSize, extora::storage_durability::relaxed};
  ASSERT_TRUE(succeeded(dataStore.open()));

  std::array<std::array<std::byte, extentSize>, threadCount> sourceBuffers;
  std::array<extora::storage_error, threadCount> writeErrors;
  std::atomic<std::size_t> readyWriters{0};
  std::atomic<bool> startWriters{false};
  std::vector<std::thread> writers;
  writers.reserve(threadCount);

  for (std::size_t index = 0; index < threadCount; ++index) {
    sourceBuffers[index].fill(static_cast<std::byte>(index + 1));
    writers.emplace_back([&dataStore, &sourceBuffers, &writeErrors, &readyWriters, &startWriters, index]() {
      readyWriters.fetch_add(1, std::memory_order_release);
      while (!startWriters.load(std::memory_order_acquire))
        std::this_thread::yield();

      extora::core::physical_extent extent;
      extent.segment_id = 1;
      extent.offset = index * extentSize;
      extent.length = extentSize;
      writeErrors[index] = writeExtent(dataStore, extent, 0, sourceBuffers[index].data(), extentSize);
    });
  }

  while (readyWriters.load(std::memory_order_acquire) != threadCount)
    std::this_thread::yield();
  startWriters.store(true, std::memory_order_release);
  for (std::thread& writer : writers)
    writer.join();
  for (const extora::storage_error& error : writeErrors)
    ASSERT_TRUE(succeeded(error)) << error.message;

  std::array<std::array<std::byte, extentSize>, threadCount> destinationBuffers;
  std::array<extora::storage_error, threadCount> readErrors;
  std::array<std::size_t, threadCount> bytesRead{};
  std::atomic<std::size_t> readyReaders{0};
  std::atomic<bool> startReaders{false};
  std::vector<std::thread> readers;
  readers.reserve(threadCount);

  for (std::size_t index = 0; index < threadCount; ++index) {
    readers.emplace_back(
        [&dataStore, &destinationBuffers, &readErrors, &bytesRead, &readyReaders, &startReaders, index]() {
          readyReaders.fetch_add(1, std::memory_order_release);
          while (!startReaders.load(std::memory_order_acquire))
            std::this_thread::yield();

          extora::core::physical_extent extent;
          extent.segment_id = 1;
          extent.offset = index * extentSize;
          extent.length = extentSize;
          readErrors[index] =
              readExtent(dataStore, extent, 0, destinationBuffers[index].data(), extentSize, bytesRead[index]);
        });
  }

  while (readyReaders.load(std::memory_order_acquire) != threadCount)
    std::this_thread::yield();
  startReaders.store(true, std::memory_order_release);
  for (std::thread& reader : readers)
    reader.join();

  for (std::size_t index = 0; index < threadCount; ++index) {
    ASSERT_TRUE(succeeded(readErrors[index])) << readErrors[index].message;
    EXPECT_EQ(bytesRead[index], extentSize);
    EXPECT_EQ(destinationBuffers[index], sourceBuffers[index]);
  }
}

TEST(SegmentDataStoreTest, RejectsExtentOutsideSegmentCapacity)
{
  const std::string root = makeTempRoot();
  ASSERT_FALSE(root.empty());

  extora::core::segment_data_store dataStore{root, 8};
  ASSERT_TRUE(succeeded(dataStore.open()));

  extora::core::physical_extent extent;
  extent.segment_id = 1;
  extent.offset = 7;
  extent.length = 2;

  extora::core::data_write_handle handle;
  const extora::storage_error error = dataStore.begin_write(extent, handle);
  EXPECT_EQ(error.code, extora::storage_error_code::backend_failure);
  EXPECT_FALSE(regularFileExists(joinPath(joinPath(root, "segments"), "0000000000000001.dat")));
}

TEST(SegmentDataStoreTest, RejectsOperationsBeforeOpen)
{
  extora::core::segment_data_store dataStore{makeTempRoot(), 16, extora::storage_durability::relaxed};
  extora::core::physical_extent extent;
  extent.segment_id = 1;
  extent.length = 1;
  extora::core::data_write_handle handle;

  EXPECT_EQ(dataStore.begin_write(extent, handle).code, extora::storage_error_code::backend_failure);
}

TEST(SegmentDataStoreTest, RejectsReadAndWriteOffsetsPastExtent)
{
  extora::core::segment_data_store dataStore{makeTempRoot(), 16, extora::storage_durability::relaxed};
  ASSERT_TRUE(succeeded(dataStore.open()));

  extora::core::physical_extent extent;
  extent.segment_id = 1;
  extent.length = 4;
  extora::core::data_write_handle writeHandle;
  ASSERT_TRUE(succeeded(dataStore.begin_write(extent, writeHandle)));
  const std::byte source[2]{std::byte{0x11}, std::byte{0x22}};
  EXPECT_EQ(dataStore.write(writeHandle, 3, source, 2).code, extora::storage_error_code::invalid_range);
  dataStore.finish_write(writeHandle);

  extora::core::data_read_handle readHandle;
  ASSERT_TRUE(succeeded(dataStore.begin_read(extent, readHandle)));
  std::byte destination;
  std::size_t bytesRead = 0;
  EXPECT_EQ(dataStore.read(readHandle, 5, &destination, 1, bytesRead).code, extora::storage_error_code::invalid_range);
  EXPECT_EQ(bytesRead, 0u);
  dataStore.finish_read(readHandle);
}

TEST(SegmentDataStoreTest, RejectsTruncatedSegmentDuringExtentValidation)
{
  const std::filesystem::path root = makeTempRoot();
  extora::core::segment_data_store dataStore{root, 16, extora::storage_durability::relaxed};
  ASSERT_TRUE(succeeded(dataStore.open()));

  extora::core::physical_extent extent;
  extent.segment_id = 1;
  extent.length = 1;
  extora::core::data_write_handle handle;
  ASSERT_TRUE(succeeded(dataStore.begin_write(extent, handle)));
  dataStore.finish_write(handle);

  const std::filesystem::path segment = root / "segments" / "0000000000000001.dat";
  std::filesystem::resize_file(segment, 8);
  EXPECT_EQ(dataStore.validate_extent(extent).code, extora::storage_error_code::backend_failure);
}

TEST(SegmentDataStoreTest, RejectsDirectoryWithSegmentFileExtension)
{
  const std::filesystem::path root = makeTempRoot();
  ASSERT_TRUE(std::filesystem::create_directories(root / "segments" / "0000000000000001.dat"));

  extora::core::segment_data_store dataStore{root, 16, extora::storage_durability::relaxed};
  EXPECT_EQ(dataStore.open().code, extora::storage_error_code::backend_failure);
}

TEST(SegmentDataStoreTest, DoesNotResizeExistingSegmentForDifferentCapacity)
{
  const std::string root = makeTempRoot();
  ASSERT_FALSE(root.empty());
  const std::string segmentPath = joinPath(joinPath(root, "segments"), "0000000000000001.dat");

  {
    extora::core::segment_data_store dataStore{root, 16, extora::storage_durability::relaxed};
    ASSERT_TRUE(succeeded(dataStore.open()));

    const std::byte value{0x2a};
    extora::core::physical_extent extent;
    extent.segment_id = 1;
    extent.offset = 15;
    extent.length = 1;
    ASSERT_TRUE(succeeded(writeExtent(dataStore, extent, 0, &value, 1)));
  }

  ASSERT_EQ(std::filesystem::file_size(segmentPath), 16);

  extora::core::segment_data_store reopenedDataStore{root, 8, extora::storage_durability::relaxed};
  const extora::storage_error error = reopenedDataStore.open();
  EXPECT_EQ(error.code, extora::storage_error_code::backend_failure);
  EXPECT_EQ(std::filesystem::file_size(segmentPath), 16);
}

TEST(SegmentDataStoreTest, FlushDoesNotCreateMissingSegment)
{
  const std::string root = makeTempRoot();
  ASSERT_FALSE(root.empty());

  extora::core::segment_data_store dataStore{root, 16, extora::storage_durability::strict};
  ASSERT_TRUE(succeeded(dataStore.open()));

  const extora::storage_error error = dataStore.flush(7);
  EXPECT_EQ(error.code, extora::storage_error_code::backend_failure);
  EXPECT_FALSE(regularFileExists(joinPath(joinPath(root, "segments"), "0000000000000007.dat")));
}

} // namespace extoraTest
