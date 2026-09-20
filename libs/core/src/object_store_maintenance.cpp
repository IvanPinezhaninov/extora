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

#include <algorithm>
#include <array>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "data_store_session.h"
#include "extora/core/object_data_store.h"
#include "extora/core/object_index.h"
#include "extora/core/object_store_core.h"
#include "extora/core/opaque_id.h"
#include "object_store_core_helpers.h"
#include "operation_tracker.h"

namespace extora::core {

using object_store_detail::validate_bucket_name;

namespace {

struct compaction_candidate {
  indexed_object object;
  std::vector<std::vector<physical_extent>> targets_by_source;
  std::vector<physical_extent> replacement_extents;
  std::vector<physical_extent> target_extents;
  std::uint64_t moved_bytes = 0;
  std::uint64_t replaced_extent_count = 0;
};

struct compaction_segment {
  std::uint64_t occupied_bytes = 0;
  std::uint64_t reusable_bytes = 0;
  bool fixed = false;
  bool retained = false;
  std::vector<physical_extent> reusable_extents;
};

void saturating_add(std::uint64_t value, std::uint64_t& total)
{
  if (value > (std::numeric_limits<std::uint64_t>::max)() - total) {
    total = (std::numeric_limits<std::uint64_t>::max)();
    return;
  }
  total += value;
}

storage_error relocate_candidate(object_data_store& data_store, hasher_factory& hash_factory,
                                 compaction_candidate& candidate, operation_tracker& operation)
{
  storage_error error;
  std::unique_ptr<hasher> payload_hasher =
      hash_factory.create_hasher(candidate.object.payload.internal_checksum.checksum_algorithm, error);
  if (failed(error)) return error;
  if (!payload_hasher)
    return make_error(storage_error_code::index_failure, "payload internal checksum hasher is unavailable");

  std::unordered_set<std::uint64_t> target_segment_ids;
  std::array<std::byte, 64 * 1024> buffer;
  for (std::size_t source_index = 0; source_index < candidate.object.payload.extents.size(); ++source_index) {
    const physical_extent& source = candidate.object.payload.extents[source_index];
    const std::vector<physical_extent>& targets = candidate.targets_by_source[source_index];
    data_read_session read_session{data_store};
    error = read_session.open(source);
    if (failed(error)) return error;

    data_write_session write_session{data_store};
    std::size_t target_index = 0;
    std::uint64_t target_offset = 0;
    std::uint64_t source_offset = 0;
    while (source_offset < source.length) {
      const std::size_t requested =
          static_cast<std::size_t>(std::min(source.length - source_offset, static_cast<std::uint64_t>(buffer.size())));
      std::size_t bytes_read = 0;
      error = data_store.read(read_session.handle(), source_offset, buffer.data(), requested, bytes_read);
      if (failed(error)) return error;
      if (bytes_read == 0 || bytes_read > requested)
        return make_error(storage_error_code::backend_failure, "compaction source extent returned an invalid size");
      error = payload_hasher->update(buffer.data(), bytes_read);
      if (failed(error)) return error;

      std::size_t written = 0;
      while (written < bytes_read && !targets.empty()) {
        if (!write_session.is_open()) {
          if (target_index == targets.size())
            return make_error(storage_error_code::index_failure, "compaction target extents are too short");
          error = write_session.open(targets[target_index]);
          if (failed(error)) return error;
          target_segment_ids.insert(targets[target_index].segment_id);
        }
        const physical_extent& target = targets[target_index];
        const std::size_t chunk_size = static_cast<std::size_t>(
            std::min(target.length - target_offset, static_cast<std::uint64_t>(bytes_read - written)));
        error = data_store.write(write_session.handle(), target_offset, buffer.data() + written, chunk_size);
        if (failed(error)) return error;
        written += chunk_size;
        target_offset += chunk_size;
        if (target_offset == target.length) {
          write_session.close();
          ++target_index;
          target_offset = 0;
        }
      }
      if (!targets.empty()) operation.advance(bytes_read);
      source_offset += bytes_read;
    }
    if (!targets.empty() && (target_index != targets.size() || target_offset != 0))
      return make_error(storage_error_code::index_failure, "compaction target extents are too long");
  }

  std::string checksum_value;
  error = payload_hasher->finish(checksum_value);
  if (failed(error)) return error;
  if (checksum_value != candidate.object.payload.internal_checksum.value)
    return make_error(storage_error_code::checksum_mismatch, "payload internal checksum does not match stored data");

  for (const std::uint64_t segment_id : target_segment_ids) {
    error = data_store.flush(segment_id);
    if (failed(error)) return error;
  }
  return {};
}

} // namespace

storage_error object_store_core::recover()
{
  m_object_cache.clear();

  std::vector<indexed_object> objects;
  {
    const std::lock_guard<std::shared_mutex> index_lock{m_index_mutex};
    const storage_error error = m_index.list_object_generations_for_recovery(objects);
    if (failed(error)) return error;
  }

  std::unordered_map<std::uint64_t, bool> valid_payloads;
  for (const indexed_object& object : objects) {
    bool invalid_object = false;
    const auto cached = valid_payloads.find(object.payload.id);
    if (cached != valid_payloads.end()) {
      invalid_object = !cached->second;
    } else {
      for (const physical_extent& extent : object.payload.extents) {
        const storage_error error = m_data_store.validate_extent(extent);
        if (failed(error)) {
          invalid_object = true;
          break;
        }
      }
      if (!invalid_object)
        invalid_object = failed(verify_payload_integrity(object.payload.extents, object.payload.internal_checksum));
      valid_payloads.emplace(object.payload.id, !invalid_object);
    }

    if (!invalid_object) continue;

    const std::lock_guard<std::shared_mutex> index_lock{m_index_mutex};
    const storage_error error = m_index.mark_payload_corrupted(object.payload.id);
    if (failed(error)) return error;
  }

  list_buckets_options bucket_options;
  do {
    bucket_list buckets;
    {
      const std::lock_guard<std::shared_mutex> index_lock{m_index_mutex};
      const storage_error error = m_index.list_buckets(bucket_options, buckets);
      if (failed(error)) return error;
    }
    for (const bucket_info& bucket : buckets.buckets) {
      std::vector<indexed_multipart_upload> uploads;
      {
        const std::lock_guard<std::shared_mutex> index_lock{m_index_mutex};
        const storage_error error = m_index.list_multipart_uploads(bucket.name, uploads);
        if (failed(error)) return error;
      }
      for (const indexed_multipart_upload& upload : uploads) {
        bool invalid_upload = false;
        for (const indexed_multipart_part& part : upload.parts) {
          for (const physical_extent& extent : part.extents) {
            if (failed(m_data_store.validate_extent(extent))) {
              invalid_upload = true;
              break;
            }
          }
          if (!invalid_upload) invalid_upload = failed(verify_payload_integrity(part.extents, part.internal_checksum));
          if (invalid_upload) break;
        }
        if (!invalid_upload) continue;

        const std::lock_guard<std::shared_mutex> index_lock{m_index_mutex};
        const storage_error error = m_index.abort_multipart_upload(upload.upload_id, upload.bucket, upload.key);
        if (failed(error)) return error;
      }
    }
    if (!buckets.next_continuation_token.has_value()) break;
    bucket_options.continuation_token = std::move(*buckets.next_continuation_token);
  } while (true);

  return {};
}

storage_error object_store_core::get_reclamation_estimate(reclamation_estimate& estimate)
{
  estimate = {};
  const std::shared_lock<std::shared_mutex> index_lock{m_index_mutex};
  return m_index.get_reclamation_estimate(estimate);
}

storage_error object_store_core::get_bucket_usage(const bucket_name& bucket, bucket_usage& usage)
{
  usage = {};
  const storage_error validation_error = validate_bucket_name(bucket);
  if (failed(validation_error)) return validation_error;

  const std::shared_lock<std::shared_mutex> index_lock{m_index_mutex};
  return m_index.get_bucket_usage(bucket, usage);
}

storage_error object_store_core::reclaim_storage_if_needed()
{
  if (!m_automatic_reclamation_threshold_bytes.has_value()) return {};

  reclamation_estimate estimate;
  const storage_error error = get_reclamation_estimate(estimate);
  if (failed(error) || estimate.reclaimable_extent_count == 0 ||
      estimate.reclaimable_bytes < *m_automatic_reclamation_threshold_bytes)
    return error;

  reclaim_storage_result result;
  return reclaim_storage(result);
}

storage_error object_store_core::reclaim_storage(reclaim_storage_result& result, const reclaim_storage_options& options)
{
  result = {};
  const std::lock_guard<std::mutex> reclamation_lock{m_reclamation_mutex};
  storage_reclamation_plan plan;
  reclaim_storage_result pending_result;
  active_object_registry::protected_extents_guard protected_extents =
      m_active_objects.acquire_protected_extents_guard();
  const std::lock_guard<std::shared_mutex> index_lock{m_index_mutex};
  storage_error error = m_index.prepare_reclamation(protected_extents.extents(), options, plan, pending_result);
  if (failed(error)) return error;
  if (options.delete_corrupted_objects) m_object_cache.clear();
  error = m_index.finish_reclamation(plan);
  if (!failed(error)) result = pending_result;
  return error;
}

storage_error object_store_core::compact_reclaimed_storage(const compact_storage_options& options,
                                                           compact_storage_result& result)
{
  if (!options.delete_empty_segments) return {};

  const std::lock_guard<std::mutex> reclamation_lock{m_reclamation_mutex};
  storage_reclamation_plan plan;
  reclaim_storage_result reclaim_result;
  storage_error physical_error;
  active_object_registry::protected_extents_guard protected_extents =
      m_active_objects.acquire_protected_extents_guard();
  const std::lock_guard<std::shared_mutex> index_lock{m_index_mutex};
  storage_error error = m_index.prepare_reclamation(protected_extents.extents(), {}, plan, reclaim_result);
  if (failed(error)) return error;

  for (const std::uint64_t segment_id : plan.fully_free_segment_ids) {
    segment_removal_result removal;
    error = m_data_store.remove_segment(segment_id, removal);
    if (failed(error) && !failed(physical_error)) physical_error = error;
    if (failed(error)) continue;
    if (removal.segment_absent) plan.absent_segment_ids.push_back(segment_id);
    if (removal.segment_removed) {
      saturating_add(1, result.removed_segment_count);
      saturating_add(removal.released_bytes, result.released_bytes);
    }
  }

  const storage_error finish_error = m_index.finish_reclamation(plan);
  return failed(finish_error) ? finish_error : physical_error;
}

storage_error object_store_core::compact_storage(compact_storage_result& result, const compact_storage_options& options)
{
  result = {};
  operation_tracker operation{m_observer.get(),
                              m_next_operation_id,
                              m_operation_progress_interval_bytes,
                              operation_type::compact_storage,
                              {},
                              {}};
  std::unique_lock<std::mutex> compaction_lock{m_compaction_mutex, std::try_to_lock};
  if (!compaction_lock.owns_lock()) {
    const storage_error error =
        make_error(storage_error_code::concurrency_limit_exceeded, "storage compaction is already running");
    operation.finish(error);
    return error;
  }

  segment_storage_usage storage_usage;
  storage_error error = m_data_store.get_segment_storage_usage(storage_usage);
  if (failed(error)) {
    operation.finish(error);
    return error;
  }
  result.segment_count_before = storage_usage.segment_count;

  const std::uint64_t extent_size = std::min(m_max_extent_size, m_data_store.max_extent_size());
  if (extent_size == 0) {
    error = make_error(storage_error_code::backend_failure, "maximum extent size is zero");
    operation.finish(error);
    return error;
  }

  reclaim_storage_result reclaim_result;
  error = reclaim_storage(reclaim_result);
  if (failed(error)) {
    operation.finish(error);
    return error;
  }

  std::vector<compaction_candidate> candidates;
  std::vector<physical_extent> evacuation_guards;
  std::optional<active_object_registry::object_registration> protected_candidates;
  {
    active_object_registry::object_lookup_guard lookup_guard = m_active_objects.acquire_object_lookup_guard();
    std::vector<indexed_object> objects;
    storage_compaction_layout layout;
    std::unique_lock<std::shared_mutex> index_lock{m_index_mutex};
    error = m_index.list_object_generations_for_recovery(objects);
    if (!failed(error)) error = m_index.prepare_compaction(layout);

    std::unordered_set<std::uint64_t> seen_payloads;
    std::vector<compaction_candidate> examined_candidates;
    indexed_object protected_object;
    for (indexed_object& object : objects) {
      if (!seen_payloads.insert(object.payload.id).second) continue;
      ++result.examined_payload_count;
      if (!object.metadata.content_length.has_value() || object.payload.extents.empty()) continue;
      protected_object.payload.extents.insert(protected_object.payload.extents.end(), object.payload.extents.begin(),
                                              object.payload.extents.end());
      compaction_candidate candidate;
      candidate.object = std::move(object);
      examined_candidates.push_back(std::move(candidate));
    }

    std::map<std::uint64_t, compaction_segment> segments;
    for (const compaction_extent& item : layout.extents) {
      compaction_segment& segment = segments[item.extent.segment_id];
      if (item.kind == compaction_extent_kind::reusable) {
        if (item.extent.length > (std::numeric_limits<std::uint64_t>::max)() - segment.reusable_bytes) {
          error = make_error(storage_error_code::index_failure, "compaction layout size overflow");
          break;
        }
        segment.reusable_bytes += item.extent.length;
        segment.reusable_extents.push_back(item.extent);
        continue;
      }
      if (item.extent.length > (std::numeric_limits<std::uint64_t>::max)() - segment.occupied_bytes) {
        error = make_error(storage_error_code::index_failure, "compaction layout size overflow");
        break;
      }
      segment.occupied_bytes += item.extent.length;
      segment.fixed = segment.fixed || item.kind == compaction_extent_kind::fixed;
    }

    std::uint64_t evacuation_bytes = 0;
    std::uint64_t target_free_bytes = 0;
    if (!failed(error)) {
      for (auto& entry : segments) {
        compaction_segment& segment = entry.second;
        if (segment.fixed) {
          segment.retained = true;
          if (segment.reusable_bytes > (std::numeric_limits<std::uint64_t>::max)() - target_free_bytes) {
            error = make_error(storage_error_code::index_failure, "compaction target size overflow");
            break;
          }
          target_free_bytes += segment.reusable_bytes;
        } else {
          if (segment.occupied_bytes > (std::numeric_limits<std::uint64_t>::max)() - evacuation_bytes) {
            error = make_error(storage_error_code::index_failure, "compaction source size overflow");
            break;
          }
          evacuation_bytes += segment.occupied_bytes;
        }
      }
    }
    if (!failed(error)) {
      for (auto& entry : segments) {
        if (evacuation_bytes <= target_free_bytes) break;
        compaction_segment& segment = entry.second;
        if (segment.retained) continue;
        segment.retained = true;
        evacuation_bytes -= segment.occupied_bytes;
        if (segment.reusable_bytes > (std::numeric_limits<std::uint64_t>::max)() - target_free_bytes) {
          error = make_error(storage_error_code::index_failure, "compaction target size overflow");
          break;
        }
        target_free_bytes += segment.reusable_bytes;
      }
      if (!failed(error) && evacuation_bytes > target_free_bytes)
        error = make_error(storage_error_code::index_failure, "compaction targets have insufficient free space");
    }

    std::vector<physical_extent> target_holes;
    if (!failed(error) && evacuation_bytes != 0) {
      for (const auto& entry : segments) {
        const compaction_segment& segment = entry.second;
        if (segment.retained)
          target_holes.insert(target_holes.end(), segment.reusable_extents.begin(), segment.reusable_extents.end());
      }
      std::sort(target_holes.begin(), target_holes.end(),
                [](const physical_extent& left, const physical_extent& right) {
                  if (left.segment_id != right.segment_id) return left.segment_id < right.segment_id;
                  return left.offset < right.offset;
                });

      std::size_t hole_index = 0;
      std::uint64_t hole_offset = 0;
      const std::string operation_id = generate_opaque_id(write_operation_id_prefix);
      std::uint64_t operation_ordinal = 0;
      std::vector<physical_extent> reserved_extents;
      for (compaction_candidate& candidate : examined_candidates) {
        candidate.targets_by_source.resize(candidate.object.payload.extents.size());
        for (std::size_t source_index = 0; source_index < candidate.object.payload.extents.size(); ++source_index) {
          const physical_extent& source = candidate.object.payload.extents[source_index];
          const auto segment = segments.find(source.segment_id);
          if (segment == segments.end()) {
            error = make_error(storage_error_code::index_failure, "payload extent is missing from compaction layout");
            break;
          }
          if (segment->second.retained) {
            candidate.replacement_extents.push_back(source);
            continue;
          }

          ++candidate.replaced_extent_count;
          saturating_add(source.length, candidate.moved_bytes);
          std::uint64_t remaining = source.length;
          while (remaining != 0) {
            while (hole_index < target_holes.size() && hole_offset == target_holes[hole_index].length) {
              ++hole_index;
              hole_offset = 0;
            }
            if (hole_index == target_holes.size()) {
              error = make_error(storage_error_code::index_failure, "retained segments have insufficient free space");
              break;
            }
            const physical_extent& hole = target_holes[hole_index];
            const std::uint64_t target_length = std::min({remaining, hole.length - hole_offset, extent_size});
            const physical_extent target{hole.segment_id, hole.offset + hole_offset, target_length, target_length};
            error = m_index.reserve_extent_at(operation_id, operation_ordinal++, target);
            if (failed(error)) break;
            reserved_extents.push_back(target);
            candidate.targets_by_source[source_index].push_back(target);
            candidate.target_extents.push_back(target);
            candidate.replacement_extents.push_back(target);
            remaining -= target_length;
            hole_offset += target_length;
          }
          if (failed(error)) break;
        }
        if (failed(error)) break;
        if (candidate.moved_bytes != 0) candidates.push_back(std::move(candidate));
      }

      if (!failed(error)) {
        for (const auto& entry : segments) {
          const compaction_segment& segment = entry.second;
          if (segment.retained) continue;
          for (const physical_extent& reusable_extent : segment.reusable_extents) {
            error = m_index.reserve_extent_at(operation_id, operation_ordinal++, reusable_extent);
            if (failed(error)) break;
            reserved_extents.push_back(reusable_extent);
            evacuation_guards.push_back(reusable_extent);
          }
          if (failed(error)) break;
        }
      }

      if (failed(error) && !reserved_extents.empty()) {
        const storage_error abandon_error = m_index.abandon_extents(reserved_extents);
        if (failed(abandon_error)) error = abandon_error;
        candidates.clear();
        evacuation_guards.clear();
      }
    }
    if (!failed(error) && !candidates.empty()) {
      std::uint64_t moved_bytes = 0;
      for (const compaction_candidate& candidate : candidates)
        saturating_add(candidate.moved_bytes, moved_bytes);
      operation.set_total_bytes(moved_bytes);
      index_lock.unlock();
      protected_candidates.emplace(lookup_guard.register_object(protected_object));
    }
  }

  std::size_t candidate_index = 0;
  for (; candidate_index < candidates.size() && !failed(error); ++candidate_index) {
    compaction_candidate& candidate = candidates[candidate_index];
    error = relocate_candidate(m_data_store, m_hash_factory, candidate, operation);
    if (failed(error) && error.code == storage_error_code::checksum_mismatch) {
      const std::lock_guard<std::shared_mutex> index_lock{m_index_mutex};
      const storage_error quarantine_error = m_index.mark_payload_corrupted(candidate.object.payload.id);
      m_object_cache.clear();
      if (failed(quarantine_error)) error = quarantine_error;
    }
    if (failed(error)) break;

    bool replaced = false;
    {
      active_object_registry::protected_extents_guard protected_extents =
          m_active_objects.acquire_protected_extents_guard();
      const std::lock_guard<std::shared_mutex> index_lock{m_index_mutex};
      error = m_index.replace_payload_extents(candidate.object.payload.id, candidate.object.payload.extents,
                                              candidate.replacement_extents, replaced);
      if (!failed(error) && replaced) m_object_cache.clear();
    }
    if (failed(error)) break;
    if (!replaced) {
      const std::lock_guard<std::shared_mutex> index_lock{m_index_mutex};
      error = m_index.abandon_extents(candidate.target_extents);
      if (failed(error)) break;
      continue;
    }

    ++result.compacted_payload_count;
    saturating_add(candidate.moved_bytes, result.compacted_bytes);
    saturating_add(candidate.replaced_extent_count, result.replaced_extent_count);
    saturating_add(static_cast<std::uint64_t>(candidate.target_extents.size()), result.compacted_extent_count);
  }

  std::vector<physical_extent> unused_extents = evacuation_guards;
  if (failed(error)) {
    for (std::size_t index = candidate_index; index < candidates.size(); ++index)
      unused_extents.insert(unused_extents.end(), candidates[index].target_extents.begin(),
                            candidates[index].target_extents.end());
  }
  if (!unused_extents.empty()) {
    const std::lock_guard<std::shared_mutex> index_lock{m_index_mutex};
    const storage_error abandon_error = m_index.abandon_extents(unused_extents);
    if (failed(abandon_error)) error = abandon_error;
  }

  protected_candidates.reset();
  if (!failed(error)) error = reclaim_storage(reclaim_result);
  if (!failed(error)) error = compact_reclaimed_storage(options, result);
  if (!failed(error)) {
    error = m_data_store.get_segment_storage_usage(storage_usage);
    if (!failed(error)) result.segment_count_after = storage_usage.segment_count;
  }
  operation.finish(error);
  return error;
}

} // namespace extora::core
