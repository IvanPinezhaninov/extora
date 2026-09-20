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

if((TARGET GTest::gtest OR TARGET GTest::gtest_main) AND
   (NOT TARGET GTest::gtest OR NOT TARGET GTest::gtest_main))
  message(FATAL_ERROR "The parent project provides an incomplete set of required GoogleTest targets")
elseif(TARGET GTest::gtest_main)
  message(STATUS "Using GoogleTest targets provided by the parent project")
elseif(EXTORA_USE_BUNDLED_GTEST)
  include(FetchContent)

  set(INSTALL_GTEST OFF CACHE BOOL "Disable GoogleTest installation" FORCE)
  if(MSVC)
    set(gtest_force_shared_crt ON CACHE BOOL "Use the shared MSVC runtime" FORCE)
  endif()

  set(extora_bundled_gtest_version 1.17.0)
  FetchContent_Declare(
    extora_googletest
    URL https://github.com/google/googletest/releases/download/v${extora_bundled_gtest_version}/googletest-${extora_bundled_gtest_version}.tar.gz
    URL_HASH SHA256=65fab701d9829d38cb77c14acdc431d2108bfdbf8979e40eb8ae567edf10b27c
    DOWNLOAD_EXTRACT_TIMESTAMP ON
    EXCLUDE_FROM_ALL
    SYSTEM
  )
  FetchContent_MakeAvailable(extora_googletest)
  message(STATUS "Using bundled GoogleTest ${extora_bundled_gtest_version}")
else()
  find_package(GTest REQUIRED)
  foreach(extora_gtest_target gtest gtest_main gmock gmock_main)
    extora_promote_imported_target(GTest::${extora_gtest_target})
  endforeach()
  message(STATUS "Using system GoogleTest")
endif()

if(NOT TARGET GTest::gtest_main)
  message(FATAL_ERROR "The selected GoogleTest dependency must provide GTest::gtest_main")
endif()
