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

set(extora_http_protocol_target ExtoraExampleHttpProtocol)
set(extora_http_protocol_headers
  HttpConditions.h
  HttpJson.h
  HttpMultipartRequest.h
  HttpProtocol.h
  HttpQuery.h
  HttpRange.h
  HttpRequestCommon.h
  HttpRoute.h
)
set(extora_http_protocol_sources
  HttpConditions.cpp
  HttpJson.cpp
  HttpMultipartRequest.cpp
  HttpProtocol.cpp
  HttpQuery.cpp
  HttpRange.cpp
  HttpRequestCommon.cpp
  HttpRoute.cpp
)

add_library(${extora_http_protocol_target} STATIC)
target_sources(${extora_http_protocol_target}
  PRIVATE
    ${extora_http_protocol_headers}
    ${extora_http_protocol_sources}
)
target_include_directories(${extora_http_protocol_target}
  PUBLIC
    .
)
target_compile_features(${extora_http_protocol_target}
  PUBLIC
    cxx_std_17
)
set_target_properties(${extora_http_protocol_target}
  PROPERTIES
    CXX_EXTENSIONS OFF
)
target_link_libraries(${extora_http_protocol_target}
  PUBLIC
    extora::boost
    extora::extora
)
extora_enable_developer_options(${extora_http_protocol_target})
