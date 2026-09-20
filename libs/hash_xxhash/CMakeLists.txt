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

set(extora_hash_xxhash_target extora_hash_xxhash)
set(extora_hash_xxhash_headers
  include/extora/core/xxhash_hasher_factory.h
)
set(extora_hash_xxhash_sources
  src/xxhash_hasher_factory.cpp
)

add_library(${extora_hash_xxhash_target} OBJECT)
extora_enable_target_coverage(${extora_hash_xxhash_target})
extora_enable_developer_options(${extora_hash_xxhash_target})

target_sources(${extora_hash_xxhash_target}
  PRIVATE
    ${extora_hash_xxhash_headers}
    ${extora_hash_xxhash_sources}
)

target_include_directories(${extora_hash_xxhash_target}
  PUBLIC
    include
    ../../include
)

target_link_libraries(${extora_hash_xxhash_target}
  PRIVATE
    extora::xxhash
)

target_compile_features(${extora_hash_xxhash_target}
  PUBLIC
    cxx_std_17
)

set_target_properties(${extora_hash_xxhash_target}
  PROPERTIES
    CXX_EXTENSIONS OFF
    CXX_VISIBILITY_PRESET hidden
    POSITION_INDEPENDENT_CODE ON
    VISIBILITY_INLINES_HIDDEN ON
)

add_library(extora::hash_xxhash ALIAS ${extora_hash_xxhash_target})
