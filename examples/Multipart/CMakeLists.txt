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

set(extora_multipart_example_target ExtoraExampleMultipart)

set(extora_multipart_example_sources
  src/main.cpp
)

add_executable(${extora_multipart_example_target}
  ${extora_multipart_example_sources}
)

target_compile_features(${extora_multipart_example_target}
  PRIVATE
    cxx_std_17
)

set_target_properties(${extora_multipart_example_target}
  PROPERTIES
    CXX_EXTENSIONS OFF
    OUTPUT_NAME extora-multipart-example
)

target_link_libraries(${extora_multipart_example_target}
  PRIVATE
    ExtoraExampleCommon
    extora::extora
)
extora_enable_developer_options(${extora_multipart_example_target})
