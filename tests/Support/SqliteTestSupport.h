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

#ifndef EXTORA_TEST_SQLITE_TEST_SUPPORT_H
#define EXTORA_TEST_SQLITE_TEST_SUPPORT_H

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>

#include <extora/core/object_index.h>

namespace extoraTest {

constexpr int sqliteExtentStateReserved = 1;
constexpr int sqliteExtentStateCommitted = 2;
constexpr int sqliteExtentStateGarbage = 3;
constexpr int sqliteExtentStateAbandoned = 4;
constexpr int sqliteExtentStateReleasing = 5;
constexpr int sqliteExtentStateFree = 6;
constexpr int sqliteObjectStateCurrent = 1;
constexpr int sqliteObjectStateSuperseded = 2;
constexpr int sqliteObjectStateCorrupted = 3;
constexpr int sqliteObjectStateDeleted = 4;

bool executeSql(const std::filesystem::path& databasePath, const std::string& sql);

int scalarQuery(const std::filesystem::path& databasePath, const char* sql);

std::string scalarTextQuery(const std::filesystem::path& databasePath, const char* sql);

int countPhysicalExtentsInState(const std::filesystem::path& databasePath, int state);

int countObjectExtentLinksInState(const std::filesystem::path& databasePath, int state);

int countObjectsInState(const std::filesystem::path& databasePath, int state);

bool tableHasColumn(const std::filesystem::path& databasePath, std::string_view table, std::string_view column);

int storageExtentLength(const std::filesystem::path& databasePath, std::uint64_t segmentId, std::uint64_t offset);

extora::storage_error reserveTestExtent(extora::core::object_index& index, std::uint64_t requestedLength,
                                        extora::core::physical_extent& extent);

} // namespace extoraTest

#endif // EXTORA_TEST_SQLITE_TEST_SUPPORT_H
