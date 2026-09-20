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

set(extora_asio_tests_target ExtoraTestAsio)
set(extora_asio_tests_sources
  test_AsyncAdmissionLimiter.cpp
  test_AsyncObjectStore.cpp
  test_BufferedObjectReader.cpp
)

add_executable(${extora_asio_tests_target}
  ${extora_asio_tests_sources}
)

target_link_libraries(${extora_asio_tests_target}
  PRIVATE
    extora::asio
)

extora_configure_test_target(${extora_asio_tests_target} asio)
