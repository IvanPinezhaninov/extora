#============================================================================
#
# Copyright (C) 2026 Ivan Pinezhaninov <ivan.pinezhaninov@gmail.com>
#
# This file is part of the extora which can be found at
# https://github.com/IvanPinezhaninov/extora/.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
# IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
# DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
# OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR
# THE USE OR OTHER DEALINGS IN THE SOFTWARE.
#
#============================================================================

set(extora_sqlite_min_version 3.27.2)
set(extora_bundled_sqlite_version 3.53.2)
set(extora_sqlite_bundled OFF)
set(extora_sqlite_external OFF)

if(TARGET SQLite::SQLite3)
  if(DEFINED SQLite3_VERSION AND SQLite3_VERSION VERSION_LESS extora_sqlite_min_version)
    message(FATAL_ERROR "Extora requires SQLite ${extora_sqlite_min_version} or newer, found ${SQLite3_VERSION}")
  endif()
  set(extora_sqlite_external ON)
  message(STATUS "Using SQLite::SQLite3 provided by the parent project")
elseif(EXTORA_USE_BUNDLED_SQLITE)
  include(FetchContent)

  string(REPLACE "." ";" extora_sqlite_version_parts "${extora_bundled_sqlite_version}")
  list(GET extora_sqlite_version_parts 0 extora_sqlite_version_major)
  list(GET extora_sqlite_version_parts 1 extora_sqlite_version_minor)
  list(GET extora_sqlite_version_parts 2 extora_sqlite_version_patch)
  math(
    EXPR extora_sqlite_archive_version
    "${extora_sqlite_version_major} * 1000000 + ${extora_sqlite_version_minor} * 10000 + ${extora_sqlite_version_patch} * 100"
  )

  FetchContent_Declare(
    extora_sqlite
    URL https://sqlite.org/2026/sqlite-amalgamation-${extora_sqlite_archive_version}.zip
    URL_HASH SHA256=8a310d0a16c7a90cacd4c884e70faa51c902afed2a89f63aaa0126ab83558a32
    DOWNLOAD_EXTRACT_TIMESTAMP ON
    SOURCE_SUBDIR __extora_populate_only__
  )
  FetchContent_MakeAvailable(extora_sqlite)

  set(extora_sqlite_source_dir "${extora_sqlite_SOURCE_DIR}")
  add_library(
    extora_sqlite
    OBJECT
      "${extora_sqlite_source_dir}/sqlite3.c"
      "${extora_sqlite_source_dir}/sqlite3.h"
  )
  add_library(SQLite::SQLite3 ALIAS extora_sqlite)
  target_compile_definitions(
    extora_sqlite
    PRIVATE
      SQLITE_DEFAULT_MEMSTATUS=0
      SQLITE_DQS=0
      SQLITE_OMIT_DEPRECATED=1
      SQLITE_OMIT_LOAD_EXTENSION=1
      SQLITE_OMIT_PROGRESS_CALLBACK=1
      SQLITE_OMIT_SHARED_CACHE=1
  )
  target_include_directories(extora_sqlite PUBLIC "$<BUILD_INTERFACE:${extora_sqlite_source_dir}>")
  set_target_properties(
    extora_sqlite
    PROPERTIES
      C_VISIBILITY_PRESET hidden
      POSITION_INDEPENDENT_CODE ON
  )

  set(extora_sqlite_bundled ON)
  message(STATUS "Using bundled SQLite ${extora_bundled_sqlite_version} from ${extora_sqlite_source_dir}")
else()
  find_package(SQLite3 ${extora_sqlite_min_version} REQUIRED)
  extora_promote_imported_target(SQLite::SQLite3)
  set(extora_sqlite_external ON)
  message(STATUS "Using system SQLite ${SQLite3_VERSION}")
endif()

if(NOT TARGET SQLite::SQLite3)
  message(FATAL_ERROR "The selected SQLite dependency must provide SQLite::SQLite3")
endif()

set(extora_sqlite_bundled ${extora_sqlite_bundled} PARENT_SCOPE)
set(extora_sqlite_external ${extora_sqlite_external} PARENT_SCOPE)
set(extora_sqlite_source_dir "${extora_sqlite_source_dir}" PARENT_SCOPE)
