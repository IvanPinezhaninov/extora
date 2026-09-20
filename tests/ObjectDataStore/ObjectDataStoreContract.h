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

#ifndef EXTORA_TEST_OBJECT_DATA_STORE_CONTRACT_H
#define EXTORA_TEST_OBJECT_DATA_STORE_CONTRACT_H

#include <cstddef>
#include <memory>
#include <vector>

#include <gtest/gtest.h>

#include <extora/core/object_data_store.h>

#include <PublicTestSupport.h>

namespace extoraTest {

template<typename TypeParam>
class ObjectDataStoreContract : public testing::Test {
protected:
  void SetUp() override
  {
    extora::storage_error error;
    m_store = TypeParam::create(makeTempRoot(), error);
    ASSERT_NE(m_store, nullptr);
    ASSERT_TRUE(succeeded(error)) << error.message;
  }

  std::unique_ptr<extora::core::object_data_store> m_store;
};

TYPED_TEST_SUITE_P(ObjectDataStoreContract);

TYPED_TEST_P(ObjectDataStoreContract, WritesFlushesValidatesAndReadsAnExtent)
{
  const std::vector<std::byte> source = bytesFromString("contract-data");
  extora::core::physical_extent extent;
  extent.segment_id = 1;
  extent.length = 32;

  extora::core::data_write_handle writeHandle;
  ASSERT_TRUE(succeeded(this->m_store->begin_write(extent, writeHandle)));
  ASSERT_TRUE(succeeded(this->m_store->write(writeHandle, 0, source.data(), source.size())));
  this->m_store->finish_write(writeHandle);
  ASSERT_TRUE(succeeded(this->m_store->flush(extent.segment_id)));

  extent.length = source.size();
  ASSERT_TRUE(succeeded(this->m_store->validate_extent(extent)));

  extora::core::data_read_handle readHandle;
  ASSERT_TRUE(succeeded(this->m_store->begin_read(extent, readHandle)));
  std::vector<std::byte> destination(source.size());
  std::size_t bytesRead = 0;
  ASSERT_TRUE(succeeded(this->m_store->read(readHandle, 0, destination.data(), destination.size(), bytesRead)));
  this->m_store->finish_read(readHandle);

  EXPECT_EQ(bytesRead, source.size());
  EXPECT_EQ(destination, source);
}

TYPED_TEST_P(ObjectDataStoreContract, BindsOperationsToLiveHandles)
{
  extora::core::physical_extent extent;
  extent.segment_id = 1;
  extent.length = 16;
  extora::core::data_write_handle writeHandle;
  ASSERT_TRUE(succeeded(this->m_store->begin_write(extent, writeHandle)));
  this->m_store->finish_write(writeHandle);
  std::byte value{0x2a};
  EXPECT_TRUE(failed(this->m_store->write(writeHandle, 0, &value, 1)));

  extent.length = 1;
  extora::core::data_read_handle readHandle;
  ASSERT_TRUE(succeeded(this->m_store->begin_read(extent, readHandle)));
  this->m_store->finish_read(readHandle);
  std::size_t bytesRead = 0;
  EXPECT_TRUE(failed(this->m_store->read(readHandle, 0, &value, 1, bytesRead)));
}

TYPED_TEST_P(ObjectDataStoreContract, RemovesOnlySegmentsWithoutLiveHandles)
{
  extora::core::physical_extent extent;
  extent.segment_id = 1;
  extent.length = 16;

  extora::core::data_write_handle handle;
  ASSERT_TRUE(succeeded(this->m_store->begin_write(extent, handle)));
  extora::core::segment_storage_usage usage;
  ASSERT_TRUE(succeeded(this->m_store->get_segment_storage_usage(usage)));
  EXPECT_EQ(usage.segment_count, 1);
  EXPECT_EQ(usage.total_bytes, this->m_store->max_extent_size());
  extora::core::segment_removal_result removal;
  ASSERT_TRUE(succeeded(this->m_store->remove_segment(extent.segment_id, removal)));
  EXPECT_FALSE(removal.segment_absent);
  EXPECT_FALSE(removal.segment_removed);
  EXPECT_EQ(removal.released_bytes, 0);

  this->m_store->finish_write(handle);
  ASSERT_TRUE(succeeded(this->m_store->remove_segment(extent.segment_id, removal)));
  EXPECT_TRUE(removal.segment_absent);
  EXPECT_TRUE(removal.segment_removed);
  EXPECT_EQ(removal.released_bytes, this->m_store->max_extent_size());
  ASSERT_TRUE(succeeded(this->m_store->get_segment_storage_usage(usage)));
  EXPECT_EQ(usage.segment_count, 0);
  EXPECT_EQ(usage.total_bytes, 0);
  ASSERT_TRUE(succeeded(this->m_store->remove_segment(extent.segment_id, removal)));
  EXPECT_TRUE(removal.segment_absent);
  EXPECT_FALSE(removal.segment_removed);
  EXPECT_EQ(removal.released_bytes, 0);

  ASSERT_TRUE(succeeded(this->m_store->begin_write(extent, handle)));
  this->m_store->finish_write(handle);
}

REGISTER_TYPED_TEST_SUITE_P(ObjectDataStoreContract, WritesFlushesValidatesAndReadsAnExtent,
                            BindsOperationsToLiveHandles, RemovesOnlySegmentsWithoutLiveHandles);

} // namespace extoraTest

#endif // EXTORA_TEST_OBJECT_DATA_STORE_CONTRACT_H
