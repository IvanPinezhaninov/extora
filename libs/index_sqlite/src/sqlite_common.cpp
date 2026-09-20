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

#include "sqlite_common.h"

#include <limits>

namespace extora::core::sqlite_detail {

storage_error make_sqlite_error(sqlite3* database, storage_error_code code, const char* message)
{
  std::string result = message;
  if (database != nullptr) {
    result += ": ";
    result += sqlite3_errmsg(database);
  }

  return make_error(code, result);
}

std::int64_t to_unix_ms(std::chrono::system_clock::time_point value)
{
  return std::chrono::duration_cast<std::chrono::milliseconds>(value.time_since_epoch()).count();
}

std::chrono::system_clock::time_point from_unix_ms(std::int64_t value)
{
  return std::chrono::system_clock::time_point{std::chrono::milliseconds{value}};
}

statement::statement(sqlite3* database, const char* sql)
{
  m_status = sqlite3_prepare_v2(database, sql, -1, &m_statement, nullptr);
}

statement::~statement()
{
  if (m_statement != nullptr) sqlite3_finalize(m_statement);
}

bool statement::prepared() const
{
  return m_status == SQLITE_OK;
}

sqlite3_stmt* statement::get() const
{
  return m_statement;
}

int statement::step()
{
  return sqlite3_step(m_statement);
}

void statement::reset()
{
  sqlite3_reset(m_statement);
  sqlite3_clear_bindings(m_statement);
}

transaction::transaction(sqlite3* database)
  : m_database{database}
{}

transaction::~transaction()
{
  if (m_active) sqlite3_exec(m_database, R"sql(ROLLBACK)sql", nullptr, nullptr, nullptr);
}

storage_error transaction::begin()
{
  const storage_error error = execute(m_database, R"sql(BEGIN IMMEDIATE)sql");
  m_active = !failed(error);
  return error;
}

storage_error transaction::commit()
{
  const storage_error error = execute(m_database, R"sql(COMMIT)sql");
  if (!failed(error)) m_active = false;
  return error;
}

storage_error bind_text(sqlite3* database, sqlite3_stmt* statement, int index, std::string_view value)
{
  if (value.size() > static_cast<std::size_t>(std::numeric_limits<int>::max()))
    return make_error(storage_error_code::index_failure, "text value is too large for SQLite");

  const char* data = value.empty() ? "" : value.data();
  if (sqlite3_bind_text(statement, index, data, static_cast<int>(value.size()), SQLITE_TRANSIENT) != SQLITE_OK)
    return make_sqlite_error(database, storage_error_code::index_failure, "failed to bind text");

  return {};
}

storage_error bind_optional_text(sqlite3* database, sqlite3_stmt* statement, int index,
                                 const std::optional<std::string>& value)
{
  if (!value.has_value()) return bind_null(database, statement, index);

  return bind_text(database, statement, index, *value);
}

storage_error bind_optional_time_point(sqlite3* database, sqlite3_stmt* statement, int index,
                                       const std::optional<std::chrono::system_clock::time_point>& value)
{
  if (!value.has_value()) return bind_null(database, statement, index);

  return bind_int64(database, statement, index, to_unix_ms(*value));
}

storage_error bind_optional_checksum(sqlite3* database, sqlite3_stmt* statement, int algorithm_index, int value_index,
                                     const std::optional<object_checksum>& checksum, int type_index)
{
  storage_error error = checksum.has_value()
                            ? bind_text(database, statement, algorithm_index, checksum->checksum_algorithm.value)
                            : bind_null(database, statement, algorithm_index);
  if (failed(error)) return error;

  error = checksum.has_value() ? bind_text(database, statement, value_index, checksum->value)
                               : bind_null(database, statement, value_index);
  if (failed(error) || type_index < 0) return error;
  const object_checksum_type type = checksum.has_value() ? checksum->type : object_checksum_type::full_object;
  return bind_int64(database, statement, type_index, static_cast<std::int64_t>(type));
}

storage_error bind_int64(sqlite3* database, sqlite3_stmt* statement, int index, std::int64_t value)
{
  if (sqlite3_bind_int64(statement, index, static_cast<sqlite3_int64>(value)) != SQLITE_OK)
    return make_sqlite_error(database, storage_error_code::index_failure, "failed to bind int64");

  return {};
}

storage_error bind_uint64(sqlite3* database, sqlite3_stmt* statement, int index, std::uint64_t value)
{
  if (value > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()))
    return make_error(storage_error_code::index_failure, "unsigned integer value is too large for SQLite");

  return bind_int64(database, statement, index, static_cast<std::int64_t>(value));
}

storage_error bind_null(sqlite3* database, sqlite3_stmt* statement, int index)
{
  if (sqlite3_bind_null(statement, index) != SQLITE_OK)
    return make_sqlite_error(database, storage_error_code::index_failure, "failed to bind null");

  return {};
}

bool column_is_null(sqlite3_stmt* statement, int index)
{
  return sqlite3_column_type(statement, index) == SQLITE_NULL;
}

std::string column_text(sqlite3_stmt* statement, int index)
{
  const unsigned char* value = sqlite3_column_text(statement, index);
  if (value == nullptr) return {};

  const int size = sqlite3_column_bytes(statement, index);
  return std::string{reinterpret_cast<const char*>(value), static_cast<std::size_t>(size)};
}

std::optional<std::string> column_optional_text(sqlite3_stmt* statement, int index)
{
  if (column_is_null(statement, index)) return std::nullopt;
  return column_text(statement, index);
}

std::optional<std::chrono::system_clock::time_point> column_optional_time_point(sqlite3_stmt* statement, int index)
{
  if (column_is_null(statement, index)) return std::nullopt;
  return from_unix_ms(sqlite3_column_int64(statement, index));
}

std::optional<object_checksum> column_optional_checksum(sqlite3_stmt* statement, int algorithm_index, int value_index,
                                                        int type_index)
{
  if (column_is_null(statement, algorithm_index) || column_is_null(statement, value_index)) return std::nullopt;
  const object_checksum_type type =
      type_index < 0 ? object_checksum_type::full_object
                     : static_cast<object_checksum_type>(sqlite3_column_int64(statement, type_index));
  return object_checksum{checksum_algorithm_name{column_text(statement, algorithm_index)},
                         column_text(statement, value_index), type};
}

storage_error execute(sqlite3* database, const char* sql)
{
  char* message = nullptr;
  const int result = sqlite3_exec(database, sql, nullptr, nullptr, &message);
  if (result == SQLITE_OK) return {};

  std::string error_message = "failed to execute SQL";
  if (message != nullptr) {
    error_message += ": ";
    error_message += message;
    sqlite3_free(message);
  }

  return make_error(storage_error_code::index_failure, error_message);
}

} // namespace extora::core::sqlite_detail
