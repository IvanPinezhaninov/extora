/******************************************************************************
**
** Copyright (C) 2026 Ivan Pinezhaninov <ivan.pinezhaninov@gmail.com>
**
** This file is part of the extora which can be found at
** https://github.com/IvanPinezhaninov/extora/.
**
** THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
** IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
** FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
** IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
** DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
** OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR
** THE USE OR OTHER DEALINGS IN THE SOFTWARE.
**
******************************************************************************/

#ifndef EXTORA_CORE_PAYLOAD_WRITE_H
#define EXTORA_CORE_PAYLOAD_WRITE_H

#include <cstdint>
#include <string>
#include <vector>

#include <extora/core/storage_types.h>

namespace extora::core {

class operation_tracker;

struct payload_write_options {
  std::optional<std::uint64_t> expected_content_length;
  std::optional<object_checksum> expected_checksum;
  checksum_algorithm_name public_checksum_algorithm;
  bool deduplicate = false;
  extent_allocation_mode allocation_mode = extent_allocation_mode::reuse;
  operation_tracker* operation = nullptr;
};

struct payload_write_result {
  std::vector<physical_extent> extents;
  std::uint64_t content_length = 0;
  std::string etag;
  object_checksum internal_checksum;
  object_checksum checksum;
  bool deduplicate = false;
};

} // namespace extora::core

#endif // EXTORA_CORE_PAYLOAD_WRITE_H
