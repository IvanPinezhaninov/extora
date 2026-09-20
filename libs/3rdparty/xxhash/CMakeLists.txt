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

set(extora_xxhash_min_version 0.8.3)
set(extora_bundled_xxhash_version 0.8.3)
set(extora_xxhash_bundled OFF)
set(extora_xxhash_external OFF)
set(extora_xxhash_target)

if(TARGET xxHash::xxhash)
  set(extora_xxhash_target xxHash::xxhash)
elseif(TARGET xxHash::xxhash_static)
  set(extora_xxhash_target xxHash::xxhash_static)
endif()

if(extora_xxhash_target)
  set(extora_xxhash_external ON)
  message(STATUS "Using xxHash target provided by the parent project")
  add_library(extora_xxhash INTERFACE)
  target_link_libraries(extora_xxhash INTERFACE ${extora_xxhash_target})
elseif(EXTORA_USE_BUNDLED_XXHASH)
  include(FetchContent)

  FetchContent_Declare(
    extora_xxhash_source
    URL https://github.com/Cyan4973/xxHash/archive/refs/tags/v${extora_bundled_xxhash_version}.tar.gz
    URL_HASH SHA256=aae608dfe8213dfd05d909a57718ef82f30722c392344583d3f39050c7f29a80
    DOWNLOAD_EXTRACT_TIMESTAMP ON
    SOURCE_SUBDIR __extora_populate_only__
  )
  FetchContent_MakeAvailable(extora_xxhash_source)

  set(extora_xxhash_source_dir "${extora_xxhash_source_SOURCE_DIR}")
  add_library(extora_xxhash OBJECT)
  target_sources(
    extora_xxhash
    PRIVATE
      "${extora_xxhash_source_dir}/xxhash.c"
      "${extora_xxhash_source_dir}/xxhash.h"
  )
  target_include_directories(extora_xxhash PUBLIC "$<BUILD_INTERFACE:${extora_xxhash_source_dir}>")
  set_target_properties(
    extora_xxhash
    PROPERTIES
      C_VISIBILITY_PRESET hidden
      POSITION_INDEPENDENT_CODE ON
  )

  set(extora_xxhash_bundled ON)
  message(STATUS "Using bundled xxHash ${extora_bundled_xxhash_version} from ${extora_xxhash_source_dir}")
else()
  find_package(xxHash ${extora_xxhash_min_version} CONFIG REQUIRED)
  foreach(extora_xxhash_candidate xxHash::xxhash xxHash::xxhash_static)
    extora_promote_imported_target(${extora_xxhash_candidate})
    if(NOT extora_xxhash_target AND TARGET ${extora_xxhash_candidate})
      set(extora_xxhash_target ${extora_xxhash_candidate})
    endif()
  endforeach()
  if(NOT extora_xxhash_target)
    message(FATAL_ERROR "The selected xxHash dependency must provide xxHash::xxhash or xxHash::xxhash_static")
  endif()

  add_library(extora_xxhash INTERFACE)
  target_link_libraries(extora_xxhash INTERFACE ${extora_xxhash_target})
  set(extora_xxhash_external ON)
  message(STATUS "Using system xxHash")
endif()

add_library(extora::xxhash ALIAS extora_xxhash)

set(extora_xxhash_bundled ${extora_xxhash_bundled} PARENT_SCOPE)
set(extora_xxhash_external ${extora_xxhash_external} PARENT_SCOPE)
set(extora_xxhash_external_target ${extora_xxhash_target} PARENT_SCOPE)
set(extora_xxhash_source_dir "${extora_xxhash_source_dir}" PARENT_SCOPE)
