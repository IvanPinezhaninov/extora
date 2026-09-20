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

TEST_F(StoreCoreTest, ReturnsEtagFromPutStatAndList)
{
  VectorReader reader{bytesFromString("value"), 2};
  extora::put_object_result putResult;
  ASSERT_TRUE(succeeded(m_core->put_object(extora::bucket_name{"photos"}, extora::object_key{"etag"}, reader,
                                           extora::object_metadata{}, putResult)));
  EXPECT_FALSE(putResult.etag.empty());

  extora::object_info info;
  ASSERT_TRUE(succeeded(m_core->head_object(extora::bucket_name{"photos"}, extora::object_key{"etag"}, info)));
  EXPECT_EQ(info.etag, putResult.etag);

  extora::object_list list;
  ASSERT_TRUE(succeeded(m_core->list_objects(extora::bucket_name{"photos"}, list)));
  ASSERT_EQ(list.objects.size(), 1);
  EXPECT_EQ(list.objects[0].etag, putResult.etag);
}

} // namespace extoraTest
