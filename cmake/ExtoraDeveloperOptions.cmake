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

include_guard(GLOBAL)

add_library(extora_developer_options INTERFACE)

if(MSVC)
  target_compile_options(extora_developer_options INTERFACE /W4 /WX)
else()
  target_compile_options(extora_developer_options INTERFACE -Wall -Wextra -Wpedantic -Werror)
endif()

if(EXTORA_ENABLE_SANITIZERS)
  if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU" AND NOT WIN32)
    target_compile_options(
      extora_developer_options
      INTERFACE
        -fsanitize=address,undefined
        -fno-omit-frame-pointer
    )
    target_link_options(extora_developer_options INTERFACE -fsanitize=address,undefined)
  else()
    message(FATAL_ERROR "EXTORA_ENABLE_SANITIZERS is unsupported by this compiler")
  endif()
endif()

if(EXTORA_ENABLE_THREAD_SANITIZER)
  if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU" AND NOT WIN32)
    target_compile_options(
      extora_developer_options
      INTERFACE
        -fsanitize=thread
        -fno-omit-frame-pointer
    )
    target_link_options(extora_developer_options INTERFACE -fsanitize=thread)
  else()
    message(FATAL_ERROR "EXTORA_ENABLE_THREAD_SANITIZER is unsupported by this compiler")
  endif()
endif()

function(extora_enable_developer_options target_name)
  set_property(TARGET ${target_name} PROPERTY CXX_EXTENSIONS OFF)
  target_link_libraries(${target_name} PRIVATE $<BUILD_INTERFACE:extora_developer_options>)
endfunction()
