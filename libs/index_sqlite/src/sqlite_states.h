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

#ifndef EXTORA_INDEX_SQLITE_SQLITE_STATES_H
#define EXTORA_INDEX_SQLITE_SQLITE_STATES_H

#include <cstdint>

#include <sqlite3.h>

#include <extora/storage_error.h>

namespace extora::core::sqlite_detail {

enum class physical_extent_state : std::uint8_t {
  reserved = 1,
  committed = 2,
  garbage = 3,
  abandoned = 4,
  releasing = 5,
  free = 6,
};

enum class object_state : std::uint8_t {
  current = 1,
  superseded = 2,
  corrupted = 3,
  deleted = 4,
};

inline std::int64_t to_int(physical_extent_state state)
{
  return static_cast<std::int64_t>(state);
}

inline std::int64_t to_int(object_state state)
{
  return static_cast<std::int64_t>(state);
}

storage_error bind_physical_extent_state(sqlite3* database, sqlite3_stmt* statement, int index,
                                         physical_extent_state state);

storage_error bind_object_state(sqlite3* database, sqlite3_stmt* statement, int index, object_state state);

} // namespace extora::core::sqlite_detail

#endif // EXTORA_INDEX_SQLITE_SQLITE_STATES_H
