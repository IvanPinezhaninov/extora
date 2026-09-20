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

cmake_minimum_required(VERSION 3.28)

project(ExtoraParentDependencies
  LANGUAGES C CXX
)

if(NOT DEFINED EXTORA_SOURCE_DIR OR
   NOT DEFINED EXTORA_SQLITE_SOURCE_DIR OR
   NOT DEFINED EXTORA_XXHASH_SOURCE_DIR)
  message(FATAL_ERROR "parent dependency consumer arguments are incomplete")
endif()

add_library(ExtoraParentSqlite STATIC
  "${EXTORA_SQLITE_SOURCE_DIR}/sqlite3.c"
)
target_compile_definitions(ExtoraParentSqlite
  PRIVATE
    SQLITE_DEFAULT_MEMSTATUS=0
    SQLITE_DQS=0
    SQLITE_OMIT_DEPRECATED=1
    SQLITE_OMIT_LOAD_EXTENSION=1
    SQLITE_OMIT_PROGRESS_CALLBACK=1
    SQLITE_OMIT_SHARED_CACHE=1
)
target_include_directories(ExtoraParentSqlite
  PUBLIC
    "${EXTORA_SQLITE_SOURCE_DIR}"
)
set_target_properties(ExtoraParentSqlite
  PROPERTIES
    POSITION_INDEPENDENT_CODE ON
)
add_library(SQLite::SQLite3 ALIAS ExtoraParentSqlite)

add_library(ExtoraParentXxHash STATIC
  "${EXTORA_XXHASH_SOURCE_DIR}/xxhash.c"
)
target_include_directories(ExtoraParentXxHash PUBLIC "${EXTORA_XXHASH_SOURCE_DIR}")
set_target_properties(ExtoraParentXxHash PROPERTIES POSITION_INDEPENDENT_CODE ON)
add_library(xxHash::xxhash ALIAS ExtoraParentXxHash)

set(EXTORA_BUILD_BENCHMARKS OFF CACHE BOOL "" FORCE)
set(EXTORA_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(EXTORA_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(EXTORA_USE_BUNDLED_SQLITE ON CACHE BOOL "" FORCE)
set(EXTORA_USE_BUNDLED_XXHASH ON CACHE BOOL "" FORCE)
add_subdirectory("${EXTORA_SOURCE_DIR}" extora)

add_executable(ExtoraParentDependencies
  src/main.cpp
)
set_target_properties(ExtoraParentDependencies PROPERTIES CXX_EXTENSIONS OFF)
if(MSVC)
  target_compile_options(ExtoraParentDependencies PRIVATE /W4 /WX)
else()
  target_compile_options(ExtoraParentDependencies PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()
target_link_libraries(ExtoraParentDependencies
  PRIVATE
    extora::extora
)
