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

cmake_minimum_required(VERSION 3.28)

project(ExtoraConsumer
  LANGUAGES CXX
)

option(EXTORA_CONSUME_ASIO "Consume the installed Extora Asio adapter" OFF)

if(EXTORA_CONSUME_ASIO AND EXTORA_BOOST_SOURCE_DIR)
  set(BOOST_INCLUDE_LIBRARIES asio system)
  set(BOOST_SKIP_INSTALL_RULES ON)
  add_subdirectory("${EXTORA_BOOST_SOURCE_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/boost" EXCLUDE_FROM_ALL)
endif()

find_package(extora CONFIG REQUIRED)

add_executable(ExtoraConsumer
  src/main.cpp
)

set_target_properties(ExtoraConsumer PROPERTIES CXX_EXTENSIONS OFF)
if(MSVC)
  target_compile_options(ExtoraConsumer PRIVATE /W4 /WX)
else()
  target_compile_options(ExtoraConsumer PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

target_link_libraries(ExtoraConsumer
  PRIVATE
    extora::extora
)

if(EXTORA_CONSUME_ASIO)
  target_compile_definitions(ExtoraConsumer PRIVATE EXTORA_CONSUME_ASIO)
  target_link_libraries(ExtoraConsumer PRIVATE extora::asio)
endif()
