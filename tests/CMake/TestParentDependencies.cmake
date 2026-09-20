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

if(NOT DEFINED EXTORA_BINARY_DIR OR
   NOT DEFINED EXTORA_SOURCE_DIR OR
   NOT DEFINED EXTORA_SQLITE_SOURCE_DIR OR
   NOT DEFINED EXTORA_XXHASH_SOURCE_DIR OR
   NOT DEFINED EXTORA_GENERATOR)
  message(FATAL_ERROR "parent dependency test arguments are incomplete")
endif()

set(consumer_build_dir "${EXTORA_BINARY_DIR}/parent-dependencies-test")
file(REMOVE_RECURSE "${consumer_build_dir}")

execute_process(
  COMMAND
    "${CMAKE_COMMAND}"
    -S "${EXTORA_SOURCE_DIR}/tests/ParentDependencies"
    -B "${consumer_build_dir}"
    -G "${EXTORA_GENERATOR}"
    "-DEXTORA_SOURCE_DIR=${EXTORA_SOURCE_DIR}"
    "-DEXTORA_SQLITE_SOURCE_DIR=${EXTORA_SQLITE_SOURCE_DIR}"
    "-DEXTORA_XXHASH_SOURCE_DIR=${EXTORA_XXHASH_SOURCE_DIR}"
  RESULT_VARIABLE configure_result
)
if(NOT configure_result EQUAL 0)
  message(FATAL_ERROR "failed to configure consumer with parent dependency targets")
endif()

execute_process(
  COMMAND
    "${CMAKE_COMMAND}"
    --build "${consumer_build_dir}"
    --config "${EXTORA_CONFIG}"
  RESULT_VARIABLE build_result
)
if(NOT build_result EQUAL 0)
  message(FATAL_ERROR "failed to build consumer with parent dependency targets")
endif()
