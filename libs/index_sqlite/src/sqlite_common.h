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

#ifndef EXTORA_INDEX_SQLITE_SQLITE_COMMON_H
#define EXTORA_INDEX_SQLITE_SQLITE_COMMON_H

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include <sqlite3.h>

#include <extora/checksum.h>
#include <extora/storage_error.h>

namespace extora::core::sqlite_detail {

storage_error make_sqlite_error(sqlite3* database, storage_error_code code, const char* message);

std::int64_t to_unix_ms(std::chrono::system_clock::time_point value);

std::chrono::system_clock::time_point from_unix_ms(std::int64_t value);

class statement final {
public:
  statement(sqlite3* database, const char* sql);
  ~statement();

  statement(const statement&) = delete;
  statement& operator=(const statement&) = delete;

  bool prepared() const;
  sqlite3_stmt* get() const;
  int step();
  void reset();

private:
  sqlite3_stmt* m_statement = nullptr;
  int m_status = SQLITE_OK;
};

class transaction final {
public:
  explicit transaction(sqlite3* database);
  ~transaction();

  transaction(const transaction&) = delete;
  transaction& operator=(const transaction&) = delete;

  storage_error begin();
  storage_error commit();

private:
  sqlite3* m_database = nullptr;
  bool m_active = false;
};

storage_error bind_text(sqlite3* database, sqlite3_stmt* statement, int index, std::string_view value);

storage_error bind_optional_text(sqlite3* database, sqlite3_stmt* statement, int index,
                                 const std::optional<std::string>& value);

storage_error bind_optional_time_point(sqlite3* database, sqlite3_stmt* statement, int index,
                                       const std::optional<std::chrono::system_clock::time_point>& value);

storage_error bind_optional_checksum(sqlite3* database, sqlite3_stmt* statement, int algorithm_index, int value_index,
                                     const std::optional<object_checksum>& checksum, int type_index = -1);

storage_error bind_int64(sqlite3* database, sqlite3_stmt* statement, int index, std::int64_t value);

storage_error bind_uint64(sqlite3* database, sqlite3_stmt* statement, int index, std::uint64_t value);

storage_error bind_null(sqlite3* database, sqlite3_stmt* statement, int index);

bool column_is_null(sqlite3_stmt* statement, int index);

std::string column_text(sqlite3_stmt* statement, int index);

std::optional<std::string> column_optional_text(sqlite3_stmt* statement, int index);

std::optional<std::chrono::system_clock::time_point> column_optional_time_point(sqlite3_stmt* statement, int index);

std::optional<object_checksum> column_optional_checksum(sqlite3_stmt* statement, int algorithm_index, int value_index,
                                                        int type_index = -1);

storage_error execute(sqlite3* database, const char* sql);

} // namespace extora::core::sqlite_detail

#endif // EXTORA_INDEX_SQLITE_SQLITE_COMMON_H
