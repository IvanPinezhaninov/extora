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
   NOT DEFINED EXTORA_GENERATOR OR
   NOT DEFINED EXTORA_CONSUME_ASIO OR
   NOT DEFINED EXTORA_INSTALL_LICENSEDIR)
  message(FATAL_ERROR "installed package test arguments are incomplete")
endif()

set(test_root "${EXTORA_BINARY_DIR}/installed-package-test")
set(install_dir "${test_root}/install")
set(consumer_build_dir "${test_root}/consumer-build")

file(REMOVE_RECURSE "${test_root}")

execute_process(
  COMMAND
    "${CMAKE_COMMAND}"
    "-DCMAKE_INSTALL_PREFIX=${install_dir}"
    "-DCMAKE_INSTALL_CONFIG_NAME=${EXTORA_CONFIG}"
    -P "${EXTORA_BINARY_DIR}/cmake_install.cmake"
  RESULT_VARIABLE install_result
)
if(NOT install_result EQUAL 0)
  message(FATAL_ERROR "failed to install Extora package")
endif()

if(IS_ABSOLUTE "${EXTORA_INSTALL_LICENSEDIR}")
  set(installed_license_dir "${EXTORA_INSTALL_LICENSEDIR}")
else()
  set(installed_license_dir "${install_dir}/${EXTORA_INSTALL_LICENSEDIR}")
endif()

set(required_license_files
  LICENSE
  NOTICE.md
  LICENSES/xxhash-BSD-2-Clause.txt
)
foreach(required_license_file IN LISTS required_license_files)
  if(NOT EXISTS "${installed_license_dir}/${required_license_file}")
    message(FATAL_ERROR "installed package is missing ${required_license_file}")
  endif()
endforeach()

if(EXTORA_CONSUME_ASIO)
  foreach(required_asio_header IN ITEMS
      async_admission_limiter.h
      async_object_store.h
      buffered_object_reader.h)
    if(NOT EXISTS "${install_dir}/include/extora/asio/${required_asio_header}")
      message(FATAL_ERROR "installed package is missing Asio header ${required_asio_header}")
    endif()
  endforeach()
endif()

set(development_only_license_files
  DEVELOPMENT_NOTICE.md
  LICENSES/google-benchmark-Apache-2.0.txt
  LICENSES/googletest-BSD-3-Clause.txt
)
foreach(development_only_license_file IN LISTS development_only_license_files)
  if(EXISTS "${installed_license_dir}/${development_only_license_file}")
    message(FATAL_ERROR
      "installed package contains development-only ${development_only_license_file}"
    )
  endif()
endforeach()

execute_process(
  COMMAND
    "${CMAKE_COMMAND}"
    -S "${EXTORA_SOURCE_DIR}/tests/Consumer"
    -B "${consumer_build_dir}"
    -G "${EXTORA_GENERATOR}"
    "-DCMAKE_PREFIX_PATH=${install_dir}"
    "-DEXTORA_BOOST_SOURCE_DIR=${EXTORA_BOOST_SOURCE_DIR}"
    "-DEXTORA_CONSUME_ASIO=${EXTORA_CONSUME_ASIO}"
  RESULT_VARIABLE configure_result
)
if(NOT configure_result EQUAL 0)
  message(FATAL_ERROR "failed to configure Extora package consumer")
endif()

execute_process(
  COMMAND
    "${CMAKE_COMMAND}"
    --build "${consumer_build_dir}"
    --config "${EXTORA_CONFIG}"
  RESULT_VARIABLE build_result
)
if(NOT build_result EQUAL 0)
  message(FATAL_ERROR "failed to build Extora package consumer")
endif()
