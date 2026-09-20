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

set(extora_minimum_gcovr_version 8.6)

if(EXTORA_ENABLE_COVERAGE)
  if(NOT EXTORA_BUILD_TESTS)
    message(FATAL_ERROR "EXTORA_ENABLE_COVERAGE requires EXTORA_BUILD_TESTS=ON")
  endif()

  if(NOT CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    message(FATAL_ERROR "EXTORA_ENABLE_COVERAGE currently supports only GCC")
  endif()

  find_program(extora_gcovr_executable
    NAMES
      gcovr
  )

  if(NOT extora_gcovr_executable)
    message(FATAL_ERROR
      "gcovr ${extora_minimum_gcovr_version} or newer is required for coverage. "
      "Install it with 'python3 -m pip install --upgrade gcovr'."
    )
  endif()

  execute_process(
    COMMAND ${extora_gcovr_executable} --version
    RESULT_VARIABLE extora_gcovr_result
    OUTPUT_VARIABLE extora_gcovr_version_output
    ERROR_VARIABLE extora_gcovr_version_error
    OUTPUT_STRIP_TRAILING_WHITESPACE
  )

  if(NOT extora_gcovr_result EQUAL 0)
    message(FATAL_ERROR "Unable to run gcovr: ${extora_gcovr_version_error}")
  endif()

  string(REGEX MATCH "^gcovr ([0-9]+\\.[0-9]+(\\.[0-9]+)?)" extora_gcovr_version_match "${extora_gcovr_version_output}")
  set(extora_gcovr_version "${CMAKE_MATCH_1}")

  if(NOT extora_gcovr_version_match)
    message(FATAL_ERROR
      "${extora_gcovr_executable} does not identify itself as gcovr: '${extora_gcovr_version_output}'"
    )
  endif()

  if(extora_gcovr_version VERSION_LESS extora_minimum_gcovr_version)
    message(FATAL_ERROR
      "gcovr ${extora_minimum_gcovr_version} or newer is required; found '${extora_gcovr_version_output}'"
    )
  endif()
endif()

function(extora_enable_target_coverage target)
  if(NOT EXTORA_ENABLE_COVERAGE)
    return()
  endif()

  target_compile_options(${target}
    PRIVATE
      --coverage
      -fprofile-abs-path
      -O0
      -g
  )

  get_target_property(extora_coverage_target_type ${target} TYPE)
  if(extora_coverage_target_type STREQUAL "EXECUTABLE" OR
      extora_coverage_target_type STREQUAL "SHARED_LIBRARY" OR
      extora_coverage_target_type STREQUAL "MODULE_LIBRARY")
    target_link_options(${target}
      PRIVATE
        --coverage
    )
  endif()
endfunction()

function(extora_add_coverage_target)
  if(NOT EXTORA_ENABLE_COVERAGE)
    return()
  endif()

  set(extora_coverage_report_directory "${PROJECT_BINARY_DIR}/report")

  add_custom_target(ExtoraCoverage
    COMMAND
      ${CMAKE_COMMAND}
      -DEXTORA_COVERAGE_DATA_DIRECTORY=${PROJECT_BINARY_DIR}
      -P ${PROJECT_SOURCE_DIR}/cmake/ClearCoverageData.cmake
    COMMAND
      ${CMAKE_COMMAND}
      -E remove_directory
      ${extora_coverage_report_directory}
    COMMAND
      ${CMAKE_CTEST_COMMAND}
      --output-on-failure
      -C $<CONFIG>
      -LE package
    COMMAND
      ${CMAKE_COMMAND}
      -E make_directory
      ${extora_coverage_report_directory}
    COMMAND
      ${extora_gcovr_executable}
      --config ${PROJECT_SOURCE_DIR}/gcovr.cfg
      --gcov-ignore-parse-errors negative_hits.warn_once_per_file
      --object-directory ${PROJECT_BINARY_DIR}
      --fail-under-line 90
      --fail-under-function 98
      --fail-under-branch 70
      --html-details ${extora_coverage_report_directory}/index.html
      --cobertura ${extora_coverage_report_directory}/coverage.xml
      --cobertura-pretty
      --txt ${extora_coverage_report_directory}/summary.txt
      --txt-summary
      --delete-input-files
      -j 0
      ${PROJECT_BINARY_DIR}
    DEPENDS
      ${ARGN}
    USES_TERMINAL
    WORKING_DIRECTORY ${PROJECT_BINARY_DIR}
    COMMENT "Running Extora tests and generating coverage reports"
  )
endfunction()
