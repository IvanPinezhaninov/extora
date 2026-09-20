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

add_subdirectory(3rdparty)
set(extora_sqlite_bundled ${extora_sqlite_bundled} PARENT_SCOPE)
set(extora_sqlite_external ${extora_sqlite_external} PARENT_SCOPE)
set(extora_sqlite_source_dir "${extora_sqlite_source_dir}" PARENT_SCOPE)
set(extora_xxhash_bundled ${extora_xxhash_bundled} PARENT_SCOPE)
set(extora_xxhash_external ${extora_xxhash_external} PARENT_SCOPE)
set(extora_xxhash_external_target ${extora_xxhash_external_target} PARENT_SCOPE)
set(extora_xxhash_source_dir "${extora_xxhash_source_dir}" PARENT_SCOPE)
if(TARGET extora_boost)
  set(extora_boost_header_target ${extora_boost_header_target} PARENT_SCOPE)
  set(extora_boost_min_version ${extora_boost_min_version} PARENT_SCOPE)
  set(extora_boost_source_dir "${extora_boost_source_dir}" PARENT_SCOPE)
endif()
add_subdirectory(core)
add_subdirectory(hash_xxhash)
add_subdirectory(index_sqlite)
add_subdirectory(segment_data)
