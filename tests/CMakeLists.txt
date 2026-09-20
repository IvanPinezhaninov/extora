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

option(EXTORA_BUILD_SOAK_TESTS "Build long-running lifecycle tests" OFF)

if(NOT TARGET Threads::Threads)
  find_package(Threads REQUIRED)
endif()

include(GoogleTest)

function(extora_configure_test_target target label)
  target_compile_features(${target}
    PRIVATE
      cxx_std_17
  )

  set_target_properties(${target}
    PROPERTIES
      CXX_EXTENSIONS OFF
  )

  target_link_libraries(${target}
    PRIVATE
      ExtoraTestSupport
      GTest::gtest_main
      Threads::Threads
  )

  gtest_discover_tests(${target}
    DISCOVERY_MODE PRE_TEST
    PROPERTIES
      LABELS ${label}
  )

  extora_enable_target_coverage(${target})
  extora_enable_developer_options(${target})
  set_property(GLOBAL APPEND PROPERTY extora_coverage_test_targets ${target})
endfunction()

add_subdirectory(Support)
if(TARGET extora_asio)
  add_subdirectory(Asio)
endif()
add_subdirectory(Api)
add_subdirectory(Compile)
add_subdirectory(Core)
add_subdirectory(Integration)
add_subdirectory(ObjectDataStore)
add_subdirectory(ObjectIndex)

get_property(extora_coverage_test_targets GLOBAL PROPERTY extora_coverage_test_targets)

if(EXTORA_ENABLE_INSTALL)
  set(extora_consume_installed_asio OFF)
  if(TARGET extora_asio)
    set(extora_consume_installed_asio ON)
  endif()
  add_test(
    NAME CMakeIntegration.InstalledPackage
    COMMAND
      ${CMAKE_COMMAND}
      -DEXTORA_BINARY_DIR=${PROJECT_BINARY_DIR}
      -DEXTORA_SOURCE_DIR=${PROJECT_SOURCE_DIR}
      -DEXTORA_GENERATOR=${CMAKE_GENERATOR}
      -DEXTORA_CONFIG=$<CONFIG>
      -DEXTORA_CONSUME_ASIO=${extora_consume_installed_asio}
      -DEXTORA_BOOST_SOURCE_DIR=${extora_boost_source_dir}
      -DEXTORA_INSTALL_LICENSEDIR=${EXTORA_INSTALL_LICENSEDIR}
      -P ${CMAKE_CURRENT_LIST_DIR}/CMake/TestInstalledPackage.cmake
  )
  set_tests_properties(CMakeIntegration.InstalledPackage
    PROPERTIES
      LABELS package
  )
endif()

add_test(
  NAME CMakeIntegration.SanitizerInstallGuard
  COMMAND
    ${CMAKE_COMMAND}
    -DEXTORA_BINARY_DIR=${PROJECT_BINARY_DIR}
    -DEXTORA_SOURCE_DIR=${PROJECT_SOURCE_DIR}
    -DEXTORA_GENERATOR=${CMAKE_GENERATOR}
    -P ${CMAKE_CURRENT_LIST_DIR}/CMake/TestSanitizerInstallGuard.cmake
)
set_tests_properties(CMakeIntegration.SanitizerInstallGuard
  PROPERTIES
    LABELS package
)

if(extora_sqlite_bundled AND extora_xxhash_bundled)
  add_test(
    NAME CMakeIntegration.ParentDependencies
    COMMAND
      ${CMAKE_COMMAND}
      -DEXTORA_BINARY_DIR=${PROJECT_BINARY_DIR}
      -DEXTORA_SOURCE_DIR=${PROJECT_SOURCE_DIR}
      -DEXTORA_SQLITE_SOURCE_DIR=${extora_sqlite_source_dir}
      -DEXTORA_XXHASH_SOURCE_DIR=${extora_xxhash_source_dir}
      -DEXTORA_GENERATOR=${CMAKE_GENERATOR}
      -DEXTORA_CONFIG=$<CONFIG>
      -P ${CMAKE_CURRENT_LIST_DIR}/CMake/TestParentDependencies.cmake
  )
  set_tests_properties(CMakeIntegration.ParentDependencies
    PROPERTIES
      LABELS package
  )
endif()

if(extora_build_shared_variant AND CMAKE_SYSTEM_NAME STREQUAL "Linux")
  add_test(
    NAME CMakeIntegration.SymbolVisibility
    COMMAND
      ${CMAKE_COMMAND}
      -DEXTORA_NM=${CMAKE_NM}
      -DEXTORA_SHARED_LIBRARY=$<TARGET_FILE:extora_shared>
      -P ${CMAKE_CURRENT_LIST_DIR}/CMake/TestSharedExports.cmake
  )
  set_tests_properties(CMakeIntegration.SymbolVisibility
    PROPERTIES
      LABELS abi
  )
endif()

extora_add_coverage_target(
  ${extora_coverage_test_targets}
)
