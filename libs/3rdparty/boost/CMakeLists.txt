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

set(extora_boost_min_version 1.92.0)
set(extora_bundled_boost_version 1.92.0)
set(extora_boost_found_in_tree OFF)
set(extora_boost_header_target)
set(extora_boost_components system)

if(EXTORA_BUILD_EXAMPLES AND EXTORA_BUILD_HTTP_SERVER_EXAMPLE)
  list(APPEND extora_boost_components beast context coroutine json)
endif()

foreach(extora_boost_target headers boost asio system context coroutine json)
  if(TARGET Boost::${extora_boost_target})
    set(extora_boost_found_in_tree ON)
  endif()
endforeach()

if(TARGET Boost::headers)
  set(extora_boost_header_target Boost::headers)
elseif(TARGET Boost::boost)
  set(extora_boost_header_target Boost::boost)
elseif(TARGET Boost::asio)
  set(extora_boost_header_target Boost::asio)
endif()

if(extora_boost_found_in_tree)
  if(NOT extora_boost_header_target)
    message(FATAL_ERROR "The parent project provides Boost targets but no usable header target")
  endif()
  foreach(extora_boost_component IN LISTS extora_boost_components)
    if(NOT TARGET Boost::${extora_boost_component})
      message(FATAL_ERROR "The parent project does not provide required Boost::${extora_boost_component}")
    endif()
  endforeach()
  if(DEFINED Boost_VERSION AND Boost_VERSION VERSION_LESS extora_boost_min_version)
    message(FATAL_ERROR "Extora requires Boost ${extora_boost_min_version} or newer, found ${Boost_VERSION}")
  endif()
  message(STATUS "Using Boost targets provided by the parent project")
elseif(EXTORA_USE_BUNDLED_BOOST)
  include(FetchContent)

  set(BOOST_INCLUDE_LIBRARIES asio beast context coroutine json system)
  set(BOOST_SKIP_INSTALL_RULES ON)
  FetchContent_Declare(
    extora_boost_source
    URL
      https://github.com/boostorg/boost/releases/download/boost-${extora_bundled_boost_version}/boost-${extora_bundled_boost_version}-cmake.tar.xz
    URL_HASH SHA256=9bed76128d4e46755dbe818487788c6fceb6f72b378f4daa49b7e1e600d9088d
    DOWNLOAD_EXTRACT_TIMESTAMP ON
    EXCLUDE_FROM_ALL
    SYSTEM
  )
  FetchContent_MakeAvailable(extora_boost_source)
  set(extora_boost_header_target Boost::asio)
  message(STATUS "Using bundled Boost ${extora_bundled_boost_version}")
else()
  find_package(
    Boost ${extora_boost_min_version} REQUIRED
    COMPONENTS ${extora_boost_components}
  )
  foreach(extora_boost_target headers boost asio system context coroutine json)
    extora_promote_imported_target(Boost::${extora_boost_target})
  endforeach()
  if(TARGET Boost::headers)
    set(extora_boost_header_target Boost::headers)
  else()
    set(extora_boost_header_target Boost::boost)
  endif()
  message(STATUS "Using system Boost ${Boost_VERSION}")
endif()

set(extora_boost_targets ${extora_boost_header_target})
foreach(extora_boost_component IN LISTS extora_boost_components)
  list(APPEND extora_boost_targets Boost::${extora_boost_component})
endforeach()
foreach(extora_boost_target IN LISTS extora_boost_targets)
  if(NOT TARGET ${extora_boost_target})
    message(FATAL_ERROR "The selected Boost dependency must provide ${extora_boost_target}")
  endif()
endforeach()

add_library(extora_boost INTERFACE)
add_library(extora::boost ALIAS extora_boost)
target_link_libraries(extora_boost INTERFACE ${extora_boost_targets})

set(extora_boost_header_target ${extora_boost_header_target} PARENT_SCOPE)
set(extora_boost_min_version ${extora_boost_min_version} PARENT_SCOPE)
if(DEFINED extora_boost_source_SOURCE_DIR)
  set(extora_boost_source_dir "${extora_boost_source_SOURCE_DIR}" PARENT_SCOPE)
endif()
