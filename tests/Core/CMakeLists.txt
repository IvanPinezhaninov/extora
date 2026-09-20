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

# Coordinator policy and behavior independent of the public factory.
set(extora_core_tests_target ExtoraTestCore)
set(extora_core_tests_sources
  CoreTestSupport.cpp
  CoreTestSupport.h
  ../Support/SqliteTestSupport.cpp
  ../Support/SqliteTestSupport.h
  test_ActiveObjectRegistry.cpp
  test_CoreBuckets.cpp
  test_CoreConditions.cpp
  test_CoreCopy.cpp
  test_CoreDelimiterListing.cpp
  test_CoreEtag.cpp
  test_CoreChecksum.cpp
  test_CoreConcurrency.cpp
  test_CoreIo.cpp
  test_CoreListing.cpp
  test_CoreMultipart.cpp
  test_CoreMutation.cpp
  test_CorePersistence.cpp
  test_CoreRecovery.cpp
  test_CoreVersioning.cpp
  test_ObjectLookupCache.cpp
  test_OpaqueId.cpp
)

add_executable(${extora_core_tests_target}
  ${extora_core_tests_sources}
)
target_include_directories(${extora_core_tests_target}
  PRIVATE
    .
)
extora_configure_test_target(${extora_core_tests_target} core)
target_link_libraries(${extora_core_tests_target}
  PRIVATE
    extora::core
    extora::hash_xxhash
    extora::index_sqlite
    extora::segment_data
    extora::xxhash
    SQLite::SQLite3
    ${extora_internal_test_target}
)
