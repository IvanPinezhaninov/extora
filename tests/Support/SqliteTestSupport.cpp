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
** OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
** USE OR OTHER DEALINGS IN THE SOFTWARE.
**
******************************************************************************/

#include "SqliteTestSupport.h"

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>

#include <sqlite3.h>

namespace extoraTest {

namespace {

int openSqliteDatabase(const std::filesystem::path& databasePath, sqlite3** database)
{
#if defined(_WIN32)
  return sqlite3_open16(databasePath.c_str(), database);
#else
  return sqlite3_open(databasePath.c_str(), database);
#endif // defined(_WIN32)
}

int countRowsInState(const std::filesystem::path& databasePath, const char* sql, int state)
{
  sqlite3* database = nullptr;
  if (openSqliteDatabase(databasePath, &database) != SQLITE_OK) {
    if (database != nullptr) sqlite3_close(database);
    return -1;
  }

  sqlite3_stmt* statement = nullptr;
  if (sqlite3_prepare_v2(database, sql, -1, &statement, nullptr) != SQLITE_OK) {
    sqlite3_close(database);
    return -1;
  }

  if (sqlite3_bind_int(statement, 1, state) != SQLITE_OK) {
    sqlite3_finalize(statement);
    sqlite3_close(database);
    return -1;
  }

  int count = -1;
  if (sqlite3_step(statement) == SQLITE_ROW) count = sqlite3_column_int(statement, 0);

  sqlite3_finalize(statement);
  sqlite3_close(database);
  return count;
}

} // namespace

bool executeSql(const std::filesystem::path& databasePath, const std::string& sql)
{
  sqlite3* database = nullptr;
  if (openSqliteDatabase(databasePath, &database) != SQLITE_OK) {
    if (database != nullptr) sqlite3_close(database);
    return false;
  }

  char* message = nullptr;
  const int result = sqlite3_exec(database, sql.c_str(), nullptr, nullptr, &message);
  if (message != nullptr) sqlite3_free(message);

  sqlite3_close(database);
  return result == SQLITE_OK;
}

int scalarQuery(const std::filesystem::path& databasePath, const char* sql)
{
  sqlite3* database = nullptr;
  if (openSqliteDatabase(databasePath, &database) != SQLITE_OK) return -1;

  sqlite3_stmt* statement = nullptr;
  if (sqlite3_prepare_v2(database, sql, -1, &statement, nullptr) != SQLITE_OK) {
    sqlite3_close(database);
    return -1;
  }

  int result = -1;
  if (sqlite3_step(statement) == SQLITE_ROW) result = sqlite3_column_int(statement, 0);
  sqlite3_finalize(statement);
  sqlite3_close(database);
  return result;
}

std::string scalarTextQuery(const std::filesystem::path& databasePath, const char* sql)
{
  sqlite3* database = nullptr;
  if (openSqliteDatabase(databasePath, &database) != SQLITE_OK) return {};

  sqlite3_stmt* statement = nullptr;
  if (sqlite3_prepare_v2(database, sql, -1, &statement, nullptr) != SQLITE_OK) {
    sqlite3_close(database);
    return {};
  }

  std::string result;
  if (sqlite3_step(statement) == SQLITE_ROW) {
    const unsigned char* value = sqlite3_column_text(statement, 0);
    if (value != nullptr) result = reinterpret_cast<const char*>(value);
  }
  sqlite3_finalize(statement);
  sqlite3_close(database);
  return result;
}

int countPhysicalExtentsInState(const std::filesystem::path& databasePath, int state)
{
  return countRowsInState(databasePath, "SELECT COUNT(*) FROM physical_extents WHERE state = ?1", state);
}

int countObjectExtentLinksInState(const std::filesystem::path& databasePath, int state)
{
  return countRowsInState(databasePath,
                          "SELECT COUNT(*) "
                          "FROM payload_extents "
                          "JOIN physical_extents ON physical_extents.id = payload_extents.physical_extent_id "
                          "WHERE physical_extents.state = ?1",
                          state);
}

int countObjectsInState(const std::filesystem::path& databasePath, int state)
{
  return countRowsInState(databasePath, "SELECT COUNT(*) FROM objects WHERE state = ?1", state);
}

bool tableHasColumn(const std::filesystem::path& databasePath, std::string_view table, std::string_view column)
{
  sqlite3* database = nullptr;
  if (openSqliteDatabase(databasePath, &database) != SQLITE_OK) {
    if (database != nullptr) sqlite3_close(database);
    return false;
  }

  sqlite3_stmt* statement = nullptr;
  std::string sql{"PRAGMA table_info("};
  sql.append(table.data(), table.size());
  sql += ')';
  if (sqlite3_prepare_v2(database, sql.c_str(), -1, &statement, nullptr) != SQLITE_OK) {
    sqlite3_close(database);
    return false;
  }

  bool found = false;
  while (sqlite3_step(statement) == SQLITE_ROW) {
    const unsigned char* name = sqlite3_column_text(statement, 1);
    if (name != nullptr && column == reinterpret_cast<const char*>(name)) {
      found = true;
      break;
    }
  }

  sqlite3_finalize(statement);
  sqlite3_close(database);
  return found;
}

int storageExtentLength(const std::filesystem::path& databasePath, std::uint64_t segmentId, std::uint64_t offset)
{
  sqlite3* database = nullptr;
  if (openSqliteDatabase(databasePath, &database) != SQLITE_OK) {
    if (database != nullptr) sqlite3_close(database);
    return -1;
  }

  sqlite3_stmt* statement = nullptr;
  const char* sql = "SELECT length FROM physical_extents WHERE segment_id = ?1 AND offset = ?2";
  if (sqlite3_prepare_v2(database, sql, -1, &statement, nullptr) != SQLITE_OK) {
    sqlite3_close(database);
    return -1;
  }

  if (sqlite3_bind_int64(statement, 1, static_cast<sqlite3_int64>(segmentId)) != SQLITE_OK ||
      sqlite3_bind_int64(statement, 2, static_cast<sqlite3_int64>(offset)) != SQLITE_OK) {
    sqlite3_finalize(statement);
    sqlite3_close(database);
    return -1;
  }

  int length = -1;
  if (sqlite3_step(statement) == SQLITE_ROW) length = sqlite3_column_int(statement, 0);

  sqlite3_finalize(statement);
  sqlite3_close(database);
  return length;
}

extora::storage_error reserveTestExtent(extora::core::object_index& index, std::uint64_t requestedLength,
                                        extora::core::physical_extent& extent)
{
  static std::atomic<std::uint64_t> sequence{0};
  const std::string operationId = "test-operation-" + std::to_string(++sequence);
  return index.reserve_extent(operationId, 0, requestedLength, extent);
}

} // namespace extoraTest
