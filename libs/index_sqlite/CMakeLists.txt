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

set(extora_index_sqlite_target extora_index_sqlite)

set(extora_index_sqlite_headers
  include/extora/core/sqlite_object_index.h
)

set(extora_index_sqlite_private_headers
  src/sqlite_bucket_records.h
  src/sqlite_common.h
  src/sqlite_compaction_records.h
  src/sqlite_extent_records.h
  src/sqlite_multipart_records.h
  src/sqlite_object_record_support.h
  src/sqlite_object_records.h
  src/sqlite_normalization.h
  src/sqlite_schema.h
  src/sqlite_states.h
)

set(extora_index_sqlite_sources
  src/sqlite_bucket_records.cpp
  src/sqlite_common.cpp
  src/sqlite_compaction_records.cpp
  src/sqlite_extent_records.cpp
  src/sqlite_multipart_records.cpp
  src/sqlite_object_listing.cpp
  src/sqlite_object_lookup.cpp
  src/sqlite_object_mutation.cpp
  src/sqlite_object_recovery.cpp
  src/sqlite_object_records.cpp
  src/sqlite_object_index.cpp
  src/sqlite_normalization.cpp
  src/sqlite_schema.cpp
  src/sqlite_states.cpp
)

add_library(${extora_index_sqlite_target} OBJECT)
extora_enable_target_coverage(${extora_index_sqlite_target})
extora_enable_developer_options(${extora_index_sqlite_target})

target_sources(${extora_index_sqlite_target}
  PRIVATE
    ${extora_index_sqlite_headers}
    ${extora_index_sqlite_private_headers}
    ${extora_index_sqlite_sources}
)

target_include_directories(${extora_index_sqlite_target}
  PRIVATE
    src
  PUBLIC
    include
)

target_link_libraries(${extora_index_sqlite_target}
  PUBLIC
    extora::core
  PRIVATE
    SQLite::SQLite3
)

target_compile_features(${extora_index_sqlite_target}
  PUBLIC
    cxx_std_17
)

set_target_properties(${extora_index_sqlite_target}
  PROPERTIES
    CXX_EXTENSIONS OFF
    CXX_VISIBILITY_PRESET hidden
    POSITION_INDEPENDENT_CODE ON
    VISIBILITY_INLINES_HIDDEN ON
)

add_library(extora::index_sqlite ALIAS ${extora_index_sqlite_target})
