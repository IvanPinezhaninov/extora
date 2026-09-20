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

# Reusable object_index contract plus SQLite-specific persistence and allocator
# behavior. Future index backends instantiate the same contract in this target.
set(extora_object_index_tests_target ExtoraTestObjectIndex)
set(extora_object_index_tests_sources
  ../Support/SqliteTestSupport.cpp
  ../Support/SqliteTestSupport.h
  ObjectIndexContract.h
  test_ObjectIndexContract.cpp
  test_SqliteAllocation.cpp
  test_SqliteConcurrency.cpp
  test_SqliteExtentState.cpp
  test_SqliteObjectValidation.cpp
  test_SqliteObjectState.cpp
  test_SqliteRecovery.cpp
  test_SqliteSchema.cpp
)

add_executable(${extora_object_index_tests_target}
  ${extora_object_index_tests_sources}
)
target_include_directories(${extora_object_index_tests_target}
  PRIVATE
    .
)
extora_configure_test_target(${extora_object_index_tests_target} object-index)
target_link_libraries(${extora_object_index_tests_target}
  PRIVATE
    extora::index_sqlite
    SQLite::SQLite3
    ${extora_internal_test_target}
)
