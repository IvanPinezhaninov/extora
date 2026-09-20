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

# Public composition tests may inspect the SQLite database and segment files together.
set(extora_integration_tests_target ExtoraTestIntegration)
set(extora_integration_tests_sources
  ../Support/SqliteTestSupport.cpp
  ../Support/SqliteTestSupport.h
  test_StorageCompaction.cpp
  test_StorageCrashRecovery.cpp
  test_StorageDeduplication.cpp
  test_StorageHashing.cpp
  test_StorageMultipartPersistence.cpp
  test_StorageOptions.cpp
  test_StorageOwnership.cpp
  test_StorageReclamation.cpp
)

add_executable(${extora_integration_tests_target}
  ${extora_integration_tests_sources}
)
target_include_directories(${extora_integration_tests_target}
  PRIVATE
    ../../libs/core/include
)
extora_configure_test_target(${extora_integration_tests_target} integration)
target_link_libraries(${extora_integration_tests_target}
  PRIVATE
    SQLite::SQLite3
    ${extora_internal_test_target}
)

# Internal composition tests instantiate concrete backends directly. Keep them
# separate from public factory tests so shared builds do not link a second copy
# of the implementation into the same executable.
set(extora_internal_integration_tests_target ExtoraTestInternalIntegration)
set(extora_internal_integration_tests_sources
  ../Core/CoreTestSupport.cpp
  ../Core/CoreTestSupport.h
  ../Support/SqliteTestSupport.cpp
  ../Support/SqliteTestSupport.h
  test_StorageMultiExtent.cpp
  test_StorageRecovery.cpp
)

add_executable(${extora_internal_integration_tests_target}
  ${extora_internal_integration_tests_sources}
)
target_include_directories(${extora_internal_integration_tests_target}
  PRIVATE
    ../Core
)
extora_configure_test_target(${extora_internal_integration_tests_target} integration)
target_link_libraries(${extora_internal_integration_tests_target}
  PRIVATE
    extora::core
    extora::hash_xxhash
    extora::index_sqlite
    extora::segment_data
    extora::xxhash
    SQLite::SQLite3
    ${extora_internal_test_target}
)

if(EXTORA_BUILD_SOAK_TESTS)
  set(extora_lifecycle_soak_tests_target ExtoraTestLifecycleSoak)
  add_executable(${extora_lifecycle_soak_tests_target}
    test_StorageLifecycleSoak.cpp
  )
  target_link_libraries(${extora_lifecycle_soak_tests_target}
    PRIVATE
      extora::extora
  )
  extora_configure_test_target(${extora_lifecycle_soak_tests_target} soak)
endif()
