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

#include <gtest/gtest.h>

#include "PublicTestSupport.h"
#include "extora/core/segment_data_store.h"

namespace extoraTest {

#if defined(__linux__)
namespace {

std::size_t openFileDescriptorCount()
{
  std::size_t count = 0;
  std::error_code errorCode;
  for ([[maybe_unused]] const std::filesystem::directory_entry& entry :
       std::filesystem::directory_iterator{"/proc/self/fd", errorCode})
    ++count;

  return errorCode ? 0 : count;
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

TEST(SegmentDataStoreTest, ReadHandleRetainsExtentCoordinates)
{
  const std::string root = makeTempRoot();
  ASSERT_FALSE(root.empty());

  extora::core::segment_data_store dataStore{root, 2, extora::storage_durability::relaxed};
  ASSERT_TRUE(succeeded(dataStore.open()));

  const std::byte firstValue{0x11};
  extora::core::physical_extent firstExtent;
  firstExtent.segment_id = 1;
  firstExtent.offset = 0;
  firstExtent.length = 1;
  ASSERT_TRUE(succeeded(writeExtent(dataStore, firstExtent, 0, &firstValue, 1)));

  const std::byte secondValue{0x22};
  extora::core::physical_extent secondExtent;
  secondExtent.segment_id = 1;
  secondExtent.offset = 1;
  secondExtent.length = 1;
  ASSERT_TRUE(succeeded(writeExtent(dataStore, secondExtent, 0, &secondValue, 1)));

  extora::core::data_read_handle handle;
  ASSERT_TRUE(succeeded(dataStore.begin_read(firstExtent, handle)));

  std::byte value;
  std::size_t bytesRead = 0;
  ASSERT_TRUE(succeeded(dataStore.read(handle, 0, &value, 1, bytesRead)));
  EXPECT_EQ(bytesRead, 1);
  EXPECT_EQ(value, firstValue);
  dataStore.finish_read(handle);
}

TEST(SegmentDataStoreTest, RejectsFinishedReadHandle)
{
  const std::string root = makeTempRoot();
  ASSERT_FALSE(root.empty());

  extora::core::segment_data_store dataStore{root, 1, extora::storage_durability::relaxed};
  ASSERT_TRUE(succeeded(dataStore.open()));

  const std::byte source{0x2a};
  extora::core::physical_extent extent;
  extent.segment_id = 1;
  extent.length = 1;
  ASSERT_TRUE(succeeded(writeExtent(dataStore, extent, 0, &source, 1)));

  extora::core::data_read_handle handle;
  ASSERT_TRUE(succeeded(dataStore.begin_read(extent, handle)));
  dataStore.finish_read(handle);

  extora::core::data_read_handle replacementHandle;
  ASSERT_TRUE(succeeded(dataStore.begin_read(extent, replacementHandle)));

  std::byte value;
  std::size_t bytesRead = 0;
  const extora::storage_error error = dataStore.read(handle, 0, &value, 1, bytesRead);
  EXPECT_EQ(error.code, extora::storage_error_code::backend_failure);
  EXPECT_EQ(bytesRead, 0);

  ASSERT_TRUE(succeeded(dataStore.read(replacementHandle, 0, &value, 1, bytesRead)));
  EXPECT_EQ(bytesRead, 1);
  EXPECT_EQ(value, source);
  dataStore.finish_read(replacementHandle);
}

TEST(SegmentDataStoreTest, KeepsManyReadHandlesIndependent)
{
  constexpr std::size_t handleCount = 128;

  const std::string root = makeTempRoot();
  ASSERT_FALSE(root.empty());

  extora::core::segment_data_store dataStore{root, 1, extora::storage_durability::relaxed};
  ASSERT_TRUE(succeeded(dataStore.open()));

  const std::byte source{0x2a};
  extora::core::physical_extent extent;
  extent.segment_id = 1;
  extent.length = 1;
  ASSERT_TRUE(succeeded(writeExtent(dataStore, extent, 0, &source, 1)));

  std::array<extora::core::data_read_handle, handleCount> handles;
  for (extora::core::data_read_handle& handle : handles)
    ASSERT_TRUE(succeeded(dataStore.begin_read(extent, handle)));

  for (extora::core::data_read_handle handle : handles) {
    std::byte value;
    std::size_t bytesRead = 0;
    ASSERT_TRUE(succeeded(dataStore.read(handle, 0, &value, 1, bytesRead)));
    EXPECT_EQ(bytesRead, 1);
    EXPECT_EQ(value, source);
    dataStore.finish_read(handle);
  }
}

TEST(SegmentDataStoreTest, RejectsFinishedWriteHandleAfterSlotReuse)
{
  const std::string root = makeTempRoot();
  ASSERT_FALSE(root.empty());

  extora::core::segment_data_store dataStore{root, 1, extora::storage_durability::relaxed};
  ASSERT_TRUE(succeeded(dataStore.open()));

  extora::core::physical_extent extent;
  extent.segment_id = 1;
  extent.length = 1;

  extora::core::data_write_handle handle;
  ASSERT_TRUE(succeeded(dataStore.begin_write(extent, handle)));
  dataStore.finish_write(handle);

  extora::core::data_write_handle replacementHandle;
  ASSERT_TRUE(succeeded(dataStore.begin_write(extent, replacementHandle)));

  const std::byte source{0x2a};
  EXPECT_EQ(dataStore.write(handle, 0, &source, 1).code, extora::storage_error_code::backend_failure);
  ASSERT_TRUE(succeeded(dataStore.write(replacementHandle, 0, &source, 1)));
  dataStore.finish_write(replacementHandle);

  std::byte value;
  std::size_t bytesRead = 0;
  ASSERT_TRUE(succeeded(readExtent(dataStore, extent, 0, &value, 1, bytesRead)));
  EXPECT_EQ(bytesRead, 1);
  EXPECT_EQ(value, source);
}

TEST(SegmentDataStoreTest, RemovesOnlySegmentsWithoutActiveHandlesAndRecreatesThemOnWrite)
{
  const std::string root = makeTempRoot();
  ASSERT_FALSE(root.empty());

  extora::core::segment_data_store dataStore{root, 4, extora::storage_durability::relaxed};
  ASSERT_TRUE(succeeded(dataStore.open()));

  const std::byte firstValue{0x11};
  extora::core::physical_extent extent;
  extent.segment_id = 1;
  extent.length = 1;
  ASSERT_TRUE(succeeded(writeExtent(dataStore, extent, 0, &firstValue, 1)));

  const std::string segmentPath = joinPath(joinPath(root, "segments"), "0000000000000001.dat");
  ASSERT_TRUE(regularFileExists(segmentPath));

  extora::core::data_read_handle handle;
  ASSERT_TRUE(succeeded(dataStore.begin_read(extent, handle)));
  extora::core::segment_removal_result removal;
  ASSERT_TRUE(succeeded(dataStore.remove_segment(1, removal)));
  EXPECT_FALSE(removal.segment_absent);
  EXPECT_FALSE(removal.segment_removed);
  EXPECT_TRUE(regularFileExists(segmentPath));

  dataStore.finish_read(handle);
  ASSERT_TRUE(succeeded(dataStore.remove_segment(1, removal)));
  EXPECT_TRUE(removal.segment_absent);
  EXPECT_TRUE(removal.segment_removed);
  EXPECT_EQ(removal.released_bytes, 4);
  EXPECT_FALSE(regularFileExists(segmentPath));

  const std::byte secondValue{0x22};
  ASSERT_TRUE(succeeded(writeExtent(dataStore, extent, 0, &secondValue, 1)));
  EXPECT_TRUE(regularFileExists(segmentPath));

  std::byte value;
  std::size_t bytesRead = 0;
  ASSERT_TRUE(succeeded(readExtent(dataStore, extent, 0, &value, 1, bytesRead)));
  EXPECT_EQ(bytesRead, 1);
  EXPECT_EQ(value, secondValue);
}

TEST(SegmentDataStoreTest, RejectsHandlesOutsideActiveSlots)
{
  extora::core::segment_data_store dataStore{makeTempRoot(), 1, extora::storage_durability::relaxed};
  ASSERT_TRUE(succeeded(dataStore.open()));

  const std::byte source{0x2a};
  extora::core::data_write_handle writeHandle;
  writeHandle.value = 1;
  EXPECT_EQ(dataStore.write(writeHandle, 0, &source, 1).code, extora::storage_error_code::backend_failure);

  std::byte destination;
  std::size_t bytesRead = 1;
  extora::core::data_read_handle readHandle;
  EXPECT_EQ(dataStore.read(readHandle, 0, &destination, 1, bytesRead).code,
            extora::storage_error_code::backend_failure);
  EXPECT_EQ(bytesRead, 0u);
}

TEST(SegmentDataStoreTest, ReopensEvictedSegmentsAndKeepsActiveReadsPinned)
{
  const std::string root = makeTempRoot();
  ASSERT_FALSE(root.empty());

  extora::core::segment_data_store dataStore{root, 1, extora::storage_durability::relaxed};
  ASSERT_TRUE(succeeded(dataStore.open()));

  const std::byte firstByte{0x2a};
  extora::core::physical_extent firstExtent;
  firstExtent.segment_id = 1;
  firstExtent.length = 1;
  ASSERT_TRUE(succeeded(writeExtent(dataStore, firstExtent, 0, &firstByte, 1)));

  extora::core::data_read_handle firstHandle;
  ASSERT_TRUE(succeeded(dataStore.begin_read(firstExtent, firstHandle)));

  for (std::uint64_t segmentId = 2; segmentId <= 96; ++segmentId) {
    const std::byte value{static_cast<unsigned char>(segmentId)};
    extora::core::physical_extent extent;
    extent.segment_id = segmentId;
    extent.length = 1;
    ASSERT_TRUE(succeeded(writeExtent(dataStore, extent, 0, &value, 1)));
  }

  std::byte value;
  std::size_t bytesRead = 0;
  ASSERT_TRUE(succeeded(dataStore.read(firstHandle, 0, &value, 1, bytesRead)));
  EXPECT_EQ(bytesRead, 1);
  EXPECT_EQ(value, firstByte);
  dataStore.finish_read(firstHandle);

  ASSERT_TRUE(succeeded(readExtent(dataStore, firstExtent, 0, &value, 1, bytesRead)));
  EXPECT_EQ(bytesRead, 1);
  EXPECT_EQ(value, firstByte);

  extora::core::physical_extent evictedExtent;
  evictedExtent.segment_id = 2;
  evictedExtent.length = 1;
  ASSERT_TRUE(succeeded(readExtent(dataStore, evictedExtent, 0, &value, 1, bytesRead)));
  EXPECT_EQ(bytesRead, 1);
  EXPECT_EQ(value, std::byte{0x02});

  extora::core::physical_extent lastExtent;
  lastExtent.segment_id = 96;
  lastExtent.length = 1;
  ASSERT_TRUE(succeeded(readExtent(dataStore, lastExtent, 0, &value, 1, bytesRead)));
  EXPECT_EQ(bytesRead, 1);
  EXPECT_EQ(value, std::byte{0x60});
}

#if defined(__linux__)
TEST(SegmentDataStoreTest, BoundsOpenSegmentFileDescriptors)
{
  const std::size_t descriptorsBefore = openFileDescriptorCount();
  ASSERT_NE(descriptorsBefore, 0);

  const std::string root = makeTempRoot();
  ASSERT_FALSE(root.empty());

  extora::core::segment_data_store dataStore{root, 1, extora::storage_durability::relaxed};
  ASSERT_TRUE(succeeded(dataStore.open()));

  for (std::uint64_t segmentId = 1; segmentId <= 128; ++segmentId) {
    const std::byte value{static_cast<unsigned char>(segmentId)};
    extora::core::physical_extent extent;
    extent.segment_id = segmentId;
    extent.length = 1;
    ASSERT_TRUE(succeeded(writeExtent(dataStore, extent, 0, &value, 1)));
  }

  const std::size_t descriptorsAfter = openFileDescriptorCount();
  ASSERT_NE(descriptorsAfter, 0);
  EXPECT_LE(descriptorsAfter, descriptorsBefore + 64);
}

#endif // defined(__linux__)

} // namespace extoraTest
