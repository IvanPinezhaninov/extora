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

# Reusable byte-storage contract. It deliberately has no index or coordinator
# dependency, so object naming and allocation policy cannot leak into it.
set(extora_object_data_store_tests_target ExtoraTestObjectDataStore)
set(extora_object_data_store_tests_sources
  ObjectDataStoreContract.h
  test_ObjectDataStoreContract.cpp
  test_SegmentHandles.cpp
  test_SegmentStorage.cpp
)

add_executable(${extora_object_data_store_tests_target}
  ${extora_object_data_store_tests_sources}
)
target_include_directories(${extora_object_data_store_tests_target}
  PRIVATE
    .
)
extora_configure_test_target(${extora_object_data_store_tests_target} object-data-store)
target_link_libraries(${extora_object_data_store_tests_target}
  PRIVATE
    extora::segment_data
    ${extora_internal_test_target}
)
