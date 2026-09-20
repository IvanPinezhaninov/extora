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

set(extora_benchmarks_target ExtoraBenchmarkObjectStore)

option(EXTORA_BENCHMARKS_ENABLE_LARGE_OBJECTS "Enable expensive large-object and extent-matrix benchmarks" OFF)

set(extora_benchmarks_sources
  src/BenchmarkSupport.cpp
  src/BenchmarkSupport.h
  src/BenchmarkConcurrentIo.cpp
  src/BenchmarkObjectMetadata.cpp
  src/BenchmarkObjectRead.cpp
  src/BenchmarkObjectWrite.cpp
  src/main.cpp
)

find_package(Threads REQUIRED)
find_package(Python3 COMPONENTS Interpreter QUIET)

add_executable(${extora_benchmarks_target}
  ${extora_benchmarks_sources}
)

target_compile_features(${extora_benchmarks_target}
  PRIVATE
    cxx_std_17
)

set_target_properties(${extora_benchmarks_target}
  PROPERTIES
    CXX_EXTENSIONS OFF
)

target_link_libraries(${extora_benchmarks_target}
  PRIVATE
    extora::extora
    benchmark::benchmark
    Threads::Threads
)

extora_enable_developer_options(${extora_benchmarks_target})

target_compile_definitions(${extora_benchmarks_target}
  PRIVATE
    EXTORA_BENCHMARKS_ENABLE_LARGE_OBJECTS=$<BOOL:${EXTORA_BENCHMARKS_ENABLE_LARGE_OBJECTS}>
)

set(extora_benchmarks_result_json
  "${CMAKE_CURRENT_BINARY_DIR}/ExtoraBenchmarkObjectStore.json"
)

set(extora_benchmarks_analyzer
  "${CMAKE_CURRENT_SOURCE_DIR}/scripts/analyze_benchmarks.py"
)

target_compile_definitions(${extora_benchmarks_target}
  PRIVATE
    EXTORA_BENCHMARKS_RESULT_JSON="${extora_benchmarks_result_json}"
    EXTORA_BENCHMARKS_ANALYZER="${extora_benchmarks_analyzer}"
)

if(Python3_FOUND AND Python3_EXECUTABLE AND EXISTS "${extora_benchmarks_analyzer}")
  target_compile_definitions(${extora_benchmarks_target}
    PRIVATE
      EXTORA_BENCHMARKS_ENABLE_ANALYTICS=1
      EXTORA_BENCHMARKS_PYTHON="${Python3_EXECUTABLE}"
  )

  message(STATUS "Extora benchmark analytics: enabled")
else()
  target_compile_definitions(${extora_benchmarks_target}
    PRIVATE
      EXTORA_BENCHMARKS_ENABLE_ANALYTICS=0
      EXTORA_BENCHMARKS_PYTHON=""
  )

  message(STATUS "Extora benchmark analytics: disabled")
endif()
