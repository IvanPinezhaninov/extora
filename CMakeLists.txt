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

project(extora
  VERSION 0.1.0
  LANGUAGES C CXX
)

set(extora_abi_version "${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}")

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

if(NOT TARGET Threads::Threads)
  find_package(Threads REQUIRED)
endif()

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

if(PROJECT_IS_TOP_LEVEL)
  set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
  set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/bin")
endif()

option(BUILD_SHARED_LIBS "Build shared libraries" OFF)
option(EXTORA_BUILD_STATIC_AND_SHARED "Build both static and shared libraries" OFF)
option(EXTORA_BUILD_BENCHMARKS "Build benchmarks" OFF)
option(EXTORA_BUILD_EXAMPLES "Build examples" ${PROJECT_IS_TOP_LEVEL})
option(EXTORA_BUILD_TESTS "Build tests" ${PROJECT_IS_TOP_LEVEL})
option(EXTORA_BUILD_ASIO_ADAPTER "Build the Boost.Asio adapter" OFF)
option(EXTORA_BUILD_HTTP_SERVER_EXAMPLE "Build the Boost.Beast HTTP server example" OFF)
option(EXTORA_ENABLE_COVERAGE "Enable test coverage reporting" OFF)
option(EXTORA_ENABLE_SANITIZERS "Enable AddressSanitizer and UndefinedBehaviorSanitizer" OFF)
option(EXTORA_ENABLE_THREAD_SANITIZER "Enable ThreadSanitizer" OFF)

set(extora_enable_install_default ${PROJECT_IS_TOP_LEVEL})
if(EXTORA_ENABLE_SANITIZERS OR EXTORA_ENABLE_THREAD_SANITIZER)
  set(extora_enable_install_default OFF)
endif()
option(EXTORA_ENABLE_INSTALL "Generate Extora installation rules" ${extora_enable_install_default})

if(EXTORA_ENABLE_INSTALL AND (EXTORA_ENABLE_SANITIZERS OR EXTORA_ENABLE_THREAD_SANITIZER))
  message(FATAL_ERROR "Sanitizer-enabled Extora builds cannot be installed; set EXTORA_ENABLE_INSTALL=OFF")
endif()

option(EXTORA_USE_BUNDLED_BENCHMARK "Use Extora's pinned Google Benchmark archive" ${PROJECT_IS_TOP_LEVEL})
option(EXTORA_USE_BUNDLED_BOOST "Use Extora's pinned Boost archive" ${PROJECT_IS_TOP_LEVEL})
option(EXTORA_USE_BUNDLED_GTEST "Use Extora's pinned GoogleTest archive" ${PROJECT_IS_TOP_LEVEL})
option(EXTORA_USE_BUNDLED_SQLITE "Use Extora's pinned SQLite archive" ${PROJECT_IS_TOP_LEVEL})
option(EXTORA_USE_BUNDLED_XXHASH "Use Extora's pinned xxHash archive" ${PROJECT_IS_TOP_LEVEL})

if(EXTORA_ENABLE_SANITIZERS AND EXTORA_ENABLE_THREAD_SANITIZER)
  message(FATAL_ERROR "AddressSanitizer/UndefinedBehaviorSanitizer and ThreadSanitizer cannot be enabled together")
endif()

include(Coverage)
include(ExtoraDeveloperOptions)

set(extora_build_static_variant OFF)
set(extora_build_shared_variant OFF)
if(EXTORA_BUILD_STATIC_AND_SHARED)
  set(extora_build_static_variant ON)
  set(extora_build_shared_variant ON)
elseif(BUILD_SHARED_LIBS)
  set(extora_build_shared_variant ON)
else()
  set(extora_build_static_variant ON)
endif()

set(EXTORA_INSTALL_CMAKEDIR "${CMAKE_INSTALL_LIBDIR}/cmake/extora" CACHE STRING "CMake package install directory")
set(EXTORA_INSTALL_LICENSEDIR "${CMAKE_INSTALL_DATADIR}/extora" CACHE STRING "License install directory")

add_subdirectory(libs)

set(extora_public_headers
  include/extora/bucket_types.h
  include/extora/checksum.h
  include/extora/extora.h
  include/extora/export.h
  include/extora/maintenance_types.h
  include/extora/managed_object_store.h
  include/extora/multipart_types.h
  include/extora/object_store.h
  include/extora/object_store_factory.h
  include/extora/object_store_options.h
  include/extora/object_stream.h
  include/extora/object_types.h
  include/extora/operation_observer.h
  include/extora/storage_error.h
)

set(extora_private_sources
  src/public_interfaces.cpp
  src/object_store_factory.cpp
  src/storage_root_lock.cpp
  src/storage_root_lock.h
)

function(extora_setup_target target_name)
  target_sources(${target_name}
    PUBLIC
      FILE_SET public_headers
      TYPE HEADERS
      BASE_DIRS include
      FILES ${extora_public_headers}
    PRIVATE
      ${extora_private_sources}
  )

  target_include_directories(${target_name}
    PUBLIC
      $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
      $<INSTALL_INTERFACE:include>
  )

  target_compile_features(${target_name}
    PUBLIC
      cxx_std_17
  )

  target_link_libraries(${target_name}
    PRIVATE
      Threads::Threads
      $<BUILD_INTERFACE:extora::core>
      $<BUILD_INTERFACE:extora::hash_xxhash>
      $<BUILD_INTERFACE:extora::index_sqlite>
      $<BUILD_INTERFACE:extora::segment_data>
  )

  if(extora_xxhash_external)
    target_link_libraries(${target_name} PRIVATE ${extora_xxhash_external_target})
  else()
    target_link_libraries(${target_name} PRIVATE $<BUILD_INTERFACE:extora::xxhash>)
  endif()

  if(extora_sqlite_external)
    target_link_libraries(${target_name}
      PRIVATE
        SQLite::SQLite3
    )
  else()
    target_link_libraries(${target_name}
      PRIVATE
        $<BUILD_INTERFACE:SQLite::SQLite3>
    )
  endif()

  if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS 9)
    target_link_libraries(${target_name}
      PUBLIC
        stdc++fs
    )
  endif()

  set_target_properties(${target_name}
    PROPERTIES
      CXX_EXTENSIONS OFF
      CXX_VISIBILITY_PRESET hidden
      OUTPUT_NAME ${PROJECT_NAME}
      VISIBILITY_INLINES_HIDDEN ON
  )

  extora_enable_developer_options(${target_name})
  extora_enable_target_coverage(${target_name})
endfunction()

if(extora_build_static_variant)
  add_library(extora_static STATIC)
  extora_setup_target(extora_static)

  set_target_properties(extora_static
    PROPERTIES
      EXPORT_NAME static
      OUTPUT_NAME ${PROJECT_NAME}_static
  )

  add_library(extora::static ALIAS extora_static)
endif()

if(extora_build_shared_variant)
  add_library(extora_shared SHARED)
  extora_setup_target(extora_shared)

  set_target_properties(extora_shared
    PROPERTIES
      EXPORT_NAME shared
      SOVERSION ${extora_abi_version}
      VERSION ${PROJECT_VERSION}
  )

  target_compile_definitions(extora_shared
    PRIVATE
      EXTORA_LIBRARY_BUILD
    INTERFACE
      EXTORA_USE_SHARED
  )

  add_library(extora::shared ALIAS extora_shared)
endif()

if(BUILD_SHARED_LIBS)
  set(extora_default_target extora_shared)
  set(extora_default_package_target extora::shared)
else()
  set(extora_default_target extora_static)
  set(extora_default_package_target extora::static)
endif()

add_library(extora::extora ALIAS ${extora_default_target})

if(EXTORA_BUILD_TESTS)
  if(TARGET extora_static)
    set(extora_internal_test_target extora_static)
  else()
    add_library(extora_test_static STATIC)
    extora_setup_target(extora_test_static)
    set_target_properties(extora_test_static
      PROPERTIES
        OUTPUT_NAME extora_test_static
    )
    set(extora_internal_test_target extora_test_static)
  endif()

  enable_testing()
endif()

if((EXTORA_BUILD_ASIO_ADAPTER OR (EXTORA_BUILD_EXAMPLES AND EXTORA_BUILD_HTTP_SERVER_EXAMPLE)) AND
   NOT TARGET extora_asio)
  add_subdirectory(adapters)
endif()

if(EXTORA_BUILD_EXAMPLES)
  add_subdirectory(examples)
endif()

if(EXTORA_ENABLE_INSTALL)
  include(Install)
endif()

if(EXTORA_BUILD_BENCHMARKS)
  add_subdirectory(benchmarks)
endif()

if(EXTORA_BUILD_TESTS)
  add_subdirectory(tests)
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND PROJECT_IS_TOP_LEVEL)
  include(Packaging)
endif()
