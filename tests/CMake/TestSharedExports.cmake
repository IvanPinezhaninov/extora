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

if(NOT DEFINED EXTORA_NM OR NOT DEFINED EXTORA_SHARED_LIBRARY)
  message(FATAL_ERROR "shared export test arguments are incomplete")
endif()

execute_process(
  COMMAND
    "${EXTORA_NM}"
    -D
    --defined-only
    --demangle
    "${EXTORA_SHARED_LIBRARY}"
  RESULT_VARIABLE nm_result
  OUTPUT_VARIABLE dynamic_symbols
  ERROR_VARIABLE nm_error
)
if(NOT nm_result EQUAL 0)
  message(FATAL_ERROR "failed to inspect shared exports: ${nm_error}")
endif()

set(forbidden_exports
  " sqlite3_"
  " XXH"
  " extora::core::"
)
foreach(forbidden_export IN LISTS forbidden_exports)
  string(FIND "${dynamic_symbols}" "${forbidden_export}" export_position)
  if(NOT export_position EQUAL -1)
    message(FATAL_ERROR
      "shared library exports internal symbol prefix '${forbidden_export}'"
    )
  endif()
endforeach()

string(FIND
  "${dynamic_symbols}"
  " extora::open_object_store("
  factory_export_position
)
if(factory_export_position EQUAL -1)
  message(FATAL_ERROR "shared library does not export open_object_store()")
endif()
