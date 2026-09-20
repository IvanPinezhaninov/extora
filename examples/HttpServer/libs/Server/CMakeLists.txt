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

set(extora_http_server_target ExtoraExampleHttpServerLibrary)
set(extora_http_server_headers
  BucketHandlers.h
  HttpConstants.h
  HttpHandlerSupport.h
  HttpRouter.h
  HttpServer.h
  HttpSession.h
  HttpStreams.h
  HttpStorage.h
  MultipartHandlers.h
  ObjectHandlers.h
  RootPage.h
)
set(extora_http_server_sources
  BucketHandlers.cpp
  HttpHandlerSupport.cpp
  HttpRouter.cpp
  HttpServer.cpp
  HttpSession.cpp
  HttpStorage.cpp
  HttpStreams.cpp
  MultipartHandlers.cpp
  ObjectHandlers.cpp
  RootPage.cpp
)

add_library(${extora_http_server_target} STATIC)
target_sources(${extora_http_server_target}
  PRIVATE
    ${extora_http_server_headers}
    ${extora_http_server_sources}
)
target_include_directories(${extora_http_server_target}
  PUBLIC
    .
)
target_compile_features(${extora_http_server_target}
  PUBLIC
    cxx_std_17
)
target_compile_definitions(${extora_http_server_target}
  PRIVATE
    BOOST_COROUTINES_NO_DEPRECATION_WARNING
)
set_target_properties(${extora_http_server_target}
  PROPERTIES
    CXX_EXTENSIONS OFF
)
target_link_libraries(${extora_http_server_target}
  PUBLIC
    extora::asio
    extora::boost
    ExtoraExampleHttpProtocol
    Threads::Threads
)
extora_enable_developer_options(${extora_http_server_target})
