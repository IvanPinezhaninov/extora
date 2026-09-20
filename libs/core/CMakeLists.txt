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

set(extora_core_target extora_core)

set(extora_core_headers
  include/extora/core/active_object_registry.h
  include/extora/core/object_data_store.h
  include/extora/core/object_index.h
  include/extora/core/object_lookup_cache.h
  include/extora/core/opaque_id.h
  include/extora/core/operation_limiter.h
  include/extora/core/object_store_core.h
  include/extora/core/storage_types.h
  src/data_store_session.h
  src/extent_writer.h
  src/hashing_object_writer.h
  src/object_store_core_helpers.h
  src/operation_tracker.h
  src/payload_hashes.h
  src/payload_write.h
)

set(extora_core_sources
  src/active_object_registry.cpp
  src/extent_writer.cpp
  src/object_lookup_cache.cpp
  src/object_store_copy.cpp
  src/object_store_core.cpp
  src/object_store_listing.cpp
  src/object_store_maintenance.cpp
  src/object_store_multipart.cpp
  src/object_store_read.cpp
  src/opaque_id.cpp
  src/operation_limiter.cpp
  src/payload_hashes.cpp
  src/payload_write.cpp
)

add_library(${extora_core_target} OBJECT)
extora_enable_target_coverage(${extora_core_target})
extora_enable_developer_options(${extora_core_target})

target_sources(${extora_core_target}
  PRIVATE
    ${extora_core_sources}
    ${extora_core_headers}
)

target_include_directories(${extora_core_target}
  PRIVATE
    src
  PUBLIC
    include
    ../../include
)

target_compile_features(${extora_core_target}
  PUBLIC
    cxx_std_17
)

set_target_properties(${extora_core_target}
  PROPERTIES
    CXX_EXTENSIONS OFF
    CXX_VISIBILITY_PRESET hidden
    POSITION_INDEPENDENT_CODE ON
    VISIBILITY_INLINES_HIDDEN ON
)

add_library(extora::core ALIAS ${extora_core_target})
