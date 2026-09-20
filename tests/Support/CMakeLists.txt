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

set(extora_test_support_target ExtoraTestSupport)
set(extora_test_support_headers
  PublicTestSupport.h
)
set(extora_test_support_sources
  PublicTestSupport.cpp
)

add_library(${extora_test_support_target} STATIC)
target_sources(${extora_test_support_target}
  PRIVATE
    ${extora_test_support_headers}
    ${extora_test_support_sources}
)
target_compile_features(${extora_test_support_target}
  PRIVATE
    cxx_std_17
)
set_target_properties(${extora_test_support_target}
  PROPERTIES
    CXX_EXTENSIONS OFF
)
target_include_directories(${extora_test_support_target}
  PUBLIC
    .
    ../../include
)

extora_enable_developer_options(${extora_test_support_target})
