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

set(extora_asio_target extora_asio)
set(extora_asio_headers
  include/extora/asio/async_admission_limiter.h
  include/extora/asio/async_object_store.h
  include/extora/asio/buffered_object_reader.h
)
set(extora_asio_sources
  src/async_admission_limiter.cpp
  src/async_object_store.cpp
  src/buffered_object_reader.cpp
)

add_library(${extora_asio_target} STATIC)
add_library(extora::asio ALIAS ${extora_asio_target})

target_sources(${extora_asio_target}
  PUBLIC
    FILE_SET public_headers
    TYPE HEADERS
    BASE_DIRS include
    FILES ${extora_asio_headers}
  PRIVATE
    ${extora_asio_sources}
)

target_include_directories(${extora_asio_target}
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_compile_features(${extora_asio_target}
  PUBLIC
    cxx_std_17
)

target_link_libraries(${extora_asio_target}
  PUBLIC
    extora::extora
    $<BUILD_LOCAL_INTERFACE:${extora_boost_header_target}>
    $<BUILD_LOCAL_INTERFACE:Boost::system>
)

set_target_properties(${extora_asio_target}
  PROPERTIES
    CXX_EXTENSIONS OFF
    EXPORT_NAME asio
    OUTPUT_NAME extora_asio
    POSITION_INDEPENDENT_CODE ON
)

extora_enable_developer_options(${extora_asio_target})
