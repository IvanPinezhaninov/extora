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
   NOT DEFINED EXTORA_GENERATOR)
  message(FATAL_ERROR "sanitizer install guard test arguments are incomplete")
endif()

set(test_build_dir "${EXTORA_BINARY_DIR}/sanitizer-install-guard-test")
file(REMOVE_RECURSE "${test_build_dir}")

execute_process(
  COMMAND
    "${CMAKE_COMMAND}"
    -S "${EXTORA_SOURCE_DIR}"
    -B "${test_build_dir}"
    -G "${EXTORA_GENERATOR}"
    -DEXTORA_BUILD_EXAMPLES=OFF
    -DEXTORA_BUILD_TESTS=OFF
    -DEXTORA_ENABLE_INSTALL=ON
    -DEXTORA_ENABLE_SANITIZERS=ON
  RESULT_VARIABLE configure_result
  OUTPUT_VARIABLE configure_output
  ERROR_VARIABLE configure_error
)
if(configure_result EQUAL 0)
  message(FATAL_ERROR "sanitizer-enabled install configuration unexpectedly succeeded")
endif()

set(configure_log "${configure_output}${configure_error}")
if(NOT configure_log MATCHES "Sanitizer-enabled Extora builds cannot be installed")
  message(FATAL_ERROR "sanitizer install configuration failed for an unexpected reason:\n${configure_log}")
endif()
