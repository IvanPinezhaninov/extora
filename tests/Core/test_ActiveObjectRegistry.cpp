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

#include "extora/core/active_object_registry.h"
#include "extora/core/storage_types.h"

namespace extoraTest {

TEST(ActiveObjectRegistryTest, ProtectsRegisteredPhysicalExtentsUntilLastRegistrationIsReleased)
{
  extora::core::indexed_object indexed;

  extora::core::physical_extent first;
  first.segment_id = 1;
  first.offset = 0;
  indexed.payload.extents.push_back(first);

  extora::core::physical_extent second;
  second.segment_id = 2;
  second.offset = 4;
  indexed.payload.extents.push_back(second);

  extora::core::active_object_registry registry;
  {
    auto firstLookupGuard = registry.acquire_object_lookup_guard();
    auto firstRegistration = firstLookupGuard.register_object(indexed);

    {
      auto secondLookupGuard = registry.acquire_object_lookup_guard();
      auto secondRegistration = secondLookupGuard.register_object(indexed);

      auto protectedExtents = registry.acquire_protected_extents_guard();
      EXPECT_EQ(protectedExtents.extents().size(), 2);
    }

    auto protectedExtents = registry.acquire_protected_extents_guard();
    EXPECT_EQ(protectedExtents.extents().size(), 2);
  }

  auto protectedExtents = registry.acquire_protected_extents_guard();
  EXPECT_TRUE(protectedExtents.extents().empty());
}

} // namespace extoraTest
