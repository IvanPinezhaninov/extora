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

set(extora_segment_data_target extora_segment_data)

set(extora_segment_data_headers
  include/extora/core/segment_handle_pool.h
  include/extora/core/segment_data_store.h
)

set(extora_segment_data_private_headers
  src/segment_file_io.h
  src/segment_layout.h
)

set(extora_segment_data_sources
  src/segment_handle_pool.cpp
  src/segment_file_io.cpp
  src/segment_data_store.cpp
  src/segment_layout.cpp
)

add_library(${extora_segment_data_target} OBJECT)
extora_enable_target_coverage(${extora_segment_data_target})
extora_enable_developer_options(${extora_segment_data_target})

target_sources(${extora_segment_data_target}
  PRIVATE
    ${extora_segment_data_headers}
    ${extora_segment_data_private_headers}
    ${extora_segment_data_sources}
)

target_include_directories(${extora_segment_data_target}
  PUBLIC
    include
)

target_link_libraries(${extora_segment_data_target}
  PUBLIC
    extora::core
)

target_compile_features(${extora_segment_data_target}
  PUBLIC
    cxx_std_17
)

set_target_properties(${extora_segment_data_target}
  PROPERTIES
    CXX_EXTENSIONS OFF
    CXX_VISIBILITY_PRESET hidden
    POSITION_INDEPENDENT_CODE ON
    VISIBILITY_INLINES_HIDDEN ON
)

add_library(extora::segment_data ALIAS ${extora_segment_data_target})

if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS 9)
  target_link_libraries(${extora_segment_data_target}
    PUBLIC
      stdc++fs
  )
endif()
