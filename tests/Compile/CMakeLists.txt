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

set(extora_public_header_check_dir "${CMAKE_CURRENT_BINARY_DIR}/generated")
file(MAKE_DIRECTORY "${extora_public_header_check_dir}")

set(extora_public_header_check_template PublicHeader.cpp.in)
set(extora_compile_public_headers ${extora_public_headers})
if(TARGET extora_asio)
  list(APPEND extora_compile_public_headers
    adapters/asio/include/extora/asio/async_admission_limiter.h
    adapters/asio/include/extora/asio/async_object_store.h
    adapters/asio/include/extora/asio/buffered_object_reader.h
  )
endif()

file(READ "${PROJECT_SOURCE_DIR}/include/extora/extora.h" extora_umbrella_header_content)
set(extora_public_header_check_sources)
foreach(extora_public_header IN LISTS extora_compile_public_headers)
  string(REGEX REPLACE "^(.*/)?include/" "" extora_public_header_include "${extora_public_header}")
  get_filename_component(extora_public_header_name "${extora_public_header_include}" NAME)
  get_filename_component(extora_public_header_stem "${extora_public_header_include}" NAME_WE)

  if(extora_public_header MATCHES "^include/" AND NOT extora_public_header_name STREQUAL "extora.h")
    string(
      FIND
      "${extora_umbrella_header_content}"
      "#include <extora/${extora_public_header_name}>"
      extora_umbrella_include_position
    )
    if(extora_umbrella_include_position EQUAL -1)
      message(FATAL_ERROR "extora.h does not export public header ${extora_public_header_name}")
    endif()
  endif()

  set(extora_public_header_check_source "${extora_public_header_check_dir}/${extora_public_header_stem}.cpp")
  configure_file("${extora_public_header_check_template}" "${extora_public_header_check_source}" @ONLY)
  list(APPEND extora_public_header_check_sources "${extora_public_header_check_source}")
endforeach()

set_source_files_properties(${extora_public_header_check_template} PROPERTIES HEADER_FILE_ONLY ON)

add_library(ExtoraCheckPublicHeaders OBJECT
  ${extora_public_header_check_template}
  ${extora_public_header_check_sources}
)
target_compile_features(ExtoraCheckPublicHeaders PRIVATE cxx_std_17)
target_link_libraries(ExtoraCheckPublicHeaders PRIVATE extora::extora)
if(TARGET extora_asio)
  target_link_libraries(ExtoraCheckPublicHeaders PRIVATE extora::asio)
endif()
extora_enable_developer_options(ExtoraCheckPublicHeaders)
