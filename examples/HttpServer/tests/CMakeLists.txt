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
# OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
# USE OR OTHER DEALINGS IN THE SOFTWARE.
#
#============================================================================

include(GoogleTest)

set(extora_http_tests_target ExtoraTestHttpServer)
set(extora_http_tests_sources
  test_HttpConditions.cpp
  test_HttpHandlerSupport.cpp
  test_HttpJson.cpp
  test_HttpMultipartRequest.cpp
  test_HttpProtocol.cpp
  test_HttpQuery.cpp
  test_HttpRange.cpp
  test_HttpRoute.cpp
  test_HttpSession.cpp
)

add_executable(${extora_http_tests_target})
target_sources(${extora_http_tests_target}
  PRIVATE
    ${extora_http_tests_sources}
)
target_compile_features(${extora_http_tests_target}
  PRIVATE
    cxx_std_17
)
set_target_properties(${extora_http_tests_target}
  PROPERTIES
    CXX_EXTENSIONS OFF
)
target_link_libraries(${extora_http_tests_target}
  PRIVATE
    ExtoraExampleHttpServerLibrary
    GTest::gtest_main
)
extora_enable_developer_options(${extora_http_tests_target})

gtest_discover_tests(${extora_http_tests_target}
  DISCOVERY_MODE PRE_TEST
  DISCOVERY_TIMEOUT 30
  PROPERTIES
    LABELS http-example
)
