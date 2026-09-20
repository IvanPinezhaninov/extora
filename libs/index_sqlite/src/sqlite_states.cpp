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

#include "sqlite_states.h"

#include "sqlite_common.h"

namespace extora::core::sqlite_detail {

namespace {

storage_error bind_state(sqlite3* database, sqlite3_stmt* statement, int index, std::int64_t value)
{
  if (sqlite3_bind_int64(statement, index, value) != SQLITE_OK)
    return make_sqlite_error(database, storage_error_code::index_failure, "failed to bind state");

  return {};
}

} // namespace

storage_error bind_physical_extent_state(sqlite3* database, sqlite3_stmt* statement, int index,
                                         physical_extent_state state)
{
  return bind_state(database, statement, index, to_int(state));
}

storage_error bind_object_state(sqlite3* database, sqlite3_stmt* statement, int index, object_state state)
{
  return bind_state(database, statement, index, to_int(state));
}

} // namespace extora::core::sqlite_detail
