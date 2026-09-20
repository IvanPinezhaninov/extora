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

# Public contract: only installed headers and the assembled Extora library are
# available here. This dependency boundary prevents accidental white-box tests.
set(extora_api_tests_target ExtoraTestApi)
set(extora_api_tests_sources
  ApiTestSupport.cpp
  ApiTestSupport.h
  test_Buckets.cpp
  test_Checksums.cpp
  test_Multipart.cpp
  test_OperationObserver.cpp
  test_ObjectConditions.cpp
  test_ObjectCopy.cpp
  test_ObjectIo.cpp
  test_ObjectListing.cpp
  test_ObjectRanges.cpp
  test_ObjectVersioning.cpp
  test_StorageError.cpp
)

add_executable(${extora_api_tests_target}
  ${extora_api_tests_sources}
)
target_link_libraries(${extora_api_tests_target}
  PRIVATE
    extora::extora
)
target_include_directories(${extora_api_tests_target}
  PRIVATE
    .
)
extora_configure_test_target(${extora_api_tests_target} api)
