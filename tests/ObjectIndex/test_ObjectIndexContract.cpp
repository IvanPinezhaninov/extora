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

#include "ObjectIndexContract.h"
#include "extora/core/sqlite_object_index.h"

namespace extoraTest {

struct SqliteObjectIndexBackend {
  static std::unique_ptr<extora::core::object_index> create(const std::filesystem::path& root,
                                                            extora::storage_error& error)
  {
    auto index = std::make_unique<extora::core::sqlite_object_index>(joinPath(root, "index.sqlite3"), 1024 * 1024);
    error = index->open();
    return index;
  }
};

using ObjectIndexBackends = testing::Types<SqliteObjectIndexBackend>;
INSTANTIATE_TYPED_TEST_SUITE_P(Sqlite, ObjectIndexContract, ObjectIndexBackends, );

} // namespace extoraTest
