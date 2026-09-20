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

#include "HttpRoute.h"

namespace extoraHttpExample {

TEST(HttpRouteTest, RecognizesRootAndServiceEndpoints)
{
  EXPECT_EQ(parseRoute("/").type, RouteType::root);
  EXPECT_EQ(parseRoute("/favicon.ico").type, RouteType::favicon);
  EXPECT_EQ(parseRoute("/_extora/health").type, RouteType::health);
  EXPECT_EQ(parseRoute("/_extora/buckets").type, RouteType::buckets);
  EXPECT_EQ(parseRoute("/_extora/reclamation").type, RouteType::reclamation);
  EXPECT_EQ(parseRoute("/_extora/compaction").type, RouteType::compaction);
}

TEST(HttpRouteTest, ParsesBucketAndUsageResources)
{
  const Route bucket = parseRoute("/photos?versions&prefix=2026");
  EXPECT_EQ(bucket.type, RouteType::bucket);
  EXPECT_EQ(bucket.bucket.value, "photos");
  EXPECT_EQ(bucket.query, "versions&prefix=2026");

  const Route usage = parseRoute("/_extora/buckets/team%2Dphotos/usage");
  EXPECT_EQ(usage.type, RouteType::bucket_usage);
  EXPECT_EQ(usage.bucket.value, "team-photos");
}

TEST(HttpRouteTest, DecodesObjectKeysWithoutTreatingThemAsPaths)
{
  const Route object = parseRoute("/photos/folder%2Fsummer%20photo.jpg?version-id=v1");
  EXPECT_EQ(object.type, RouteType::object);
  EXPECT_EQ(object.bucket.value, "photos");
  EXPECT_EQ(object.key.value, "folder/summer photo.jpg");
  EXPECT_EQ(object.query, "version-id=v1");
}

TEST(HttpRouteTest, RejectsMalformedTargets)
{
  EXPECT_EQ(parseRoute("photos/file.jpg").type, RouteType::invalid);
  EXPECT_EQ(parseRoute("/%zz").type, RouteType::invalid);
  EXPECT_EQ(parseRoute("/photos/").type, RouteType::invalid);
  EXPECT_EQ(parseRoute("/photos/%zz").type, RouteType::invalid);
  EXPECT_EQ(parseRoute("/_extora/buckets/photos/extra/usage").type, RouteType::invalid);
}

} // namespace extoraHttpExample
