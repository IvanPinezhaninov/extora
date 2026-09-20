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

if(NOT EXTORA_BUILD_HTTP_SERVER_EXAMPLE)
  return()
endif()

add_subdirectory(libs)

set(extora_http_server_example_target ExtoraExampleHttpServer)
add_executable(${extora_http_server_example_target})
target_sources(${extora_http_server_example_target}
  PRIVATE
    src/main.cpp
)
target_compile_features(${extora_http_server_example_target}
  PRIVATE
    cxx_std_17
)
set_target_properties(${extora_http_server_example_target}
  PROPERTIES
    CXX_EXTENSIONS OFF
    OUTPUT_NAME extora-http-server-example
)
target_link_libraries(${extora_http_server_example_target}
  PRIVATE
    ExtoraExampleHttpServerLibrary
)
extora_enable_developer_options(${extora_http_server_example_target})

if(EXTORA_BUILD_TESTS)
  add_subdirectory(tests)
endif()
