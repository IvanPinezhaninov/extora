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

#include <filesystem>
#include <memory>

#include <gtest/gtest.h>

#include "ObjectDataStoreContract.h"
#include "extora/core/segment_data_store.h"

namespace extoraTest {

struct SegmentDataStoreBackend {
  static std::unique_ptr<extora::core::object_data_store> create(const std::filesystem::path& root,
                                                                 extora::storage_error& error)
  {
    auto store = std::make_unique<extora::core::segment_data_store>(root, 1024 * 1024);
    error = store->open();
    return store;
  }
};

using ObjectDataStoreBackends = testing::Types<SegmentDataStoreBackend>;
INSTANTIATE_TYPED_TEST_SUITE_P(Segment, ObjectDataStoreContract, ObjectDataStoreBackends, );

} // namespace extoraTest
