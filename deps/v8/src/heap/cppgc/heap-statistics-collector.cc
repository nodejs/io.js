// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/heap/cppgc/heap-statistics-collector.h"

#include <string>
#include <unordered_map>

#include "include/cppgc/heap-statistics.h"
#include "include/cppgc/name-provider.h"
#include "src/heap/cppgc/free-list.h"
#include "src/heap/cppgc/globals.h"
#include "src/heap/cppgc/heap-base.h"
#include "src/heap/cppgc/heap-object-header.h"
#include "src/heap/cppgc/page-memory.h"
#include "src/heap/cppgc/raw-heap.h"
#include "src/heap/cppgc/stats-collector.h"

namespace cppgc {
namespace internal {

namespace {

std::string GetNormalPageSpaceName(size_t index) {
  // Check that space is not a large object space.
  DCHECK_NE(RawHeap::kNumberOfRegularSpaces - 1, index);
  // Handle regular normal page spaces.
  if (index < RawHeap::kNumberOfRegularSpaces) {
    return "NormalPageSpace" + std::to_string(index);
  }
  // Space is a custom space.
  return "CustomSpace" +
         std::to_string(index - RawHeap::kNumberOfRegularSpaces);
}

HeapStatistics::SpaceStatistics* InitializeSpace(HeapStatistics* stats,
                                                 std::string name) {
  stats->space_stats.emplace_back();
  HeapStatistics::SpaceStatistics* space_stats = &stats->space_stats.back();
  space_stats->name = std::move(name);
  return space_stats;
}

HeapStatistics::PageStatistics* InitializePage(
    HeapStatistics::SpaceStatistics* stats) {
  stats->page_stats.emplace_back();
  HeapStatistics::PageStatistics* page_stats = &stats->page_stats.back();
  return page_stats;
}

void FinalizePage(HeapStatistics::SpaceStatistics* space_stats,
                  HeapStatistics::PageStatistics** page_stats) {
  if (*page_stats) {
    DCHECK_NOT_NULL(space_stats);
    space_stats->committed_size_bytes += (*page_stats)->committed_size_bytes;
    space_stats->resident_size_bytes += (*page_stats)->resident_size_bytes;
    space_stats->used_size_bytes += (*page_stats)->used_size_bytes;
  }
  *page_stats = nullptr;
}

void FinalizeSpace(HeapStatistics* stats,
                   HeapStatistics::SpaceStatistics** space_stats,
                   HeapStatistics::PageStatistics** page_stats) {
  FinalizePage(*space_stats, page_stats);
  if (*space_stats) {
    DCHECK_NOT_NULL(stats);
    stats->committed_size_bytes += (*space_stats)->committed_size_bytes;
    stats->resident_size_bytes += (*space_stats)->resident_size_bytes;
    stats->used_size_bytes += (*space_stats)->used_size_bytes;
  }
  *space_stats = nullptr;
}

void RecordObjectType(
    std::unordered_map<const void*, size_t>& type_map,
    std::vector<HeapStatistics::ObjectStatsEntry>& object_statistics,
    HeapObjectHeader* header, size_t object_size) {
  if (NameProvider::SupportsCppClassNamesAsObjectNames()) {
    // Tries to insert a new entry into the typemap with a running counter. If
    // the entry is already present, just returns the old one.
    const auto it = type_map.insert({header->GetName().value, type_map.size()});
    const size_t type_index = it.first->second;
    if (object_statistics.size() <= type_index) {
      object_statistics.resize(type_index + 1);
    }
    object_statistics[type_index].allocated_bytes += object_size;
    object_statistics[type_index].object_count++;
  }
}

}  // namespace

HeapStatistics HeapStatisticsCollector::CollectDetailedStatistics(
    HeapBase* heap) {
  HeapStatistics stats;
  stats.detail_level = HeapStatistics::DetailLevel::kDetailed;
  current_stats_ = &stats;

  ClassNameAsHeapObjectNameScope class_names_scope(*heap);

  Traverse(heap->raw_heap());
  FinalizeSpace(current_stats_, &current_space_stats_, &current_page_stats_);

  if (NameProvider::SupportsCppClassNamesAsObjectNames()) {
    stats.type_names.resize(type_name_to_index_map_.size());
    for (auto& it : type_name_to_index_map_) {
      stats.type_names[it.second] = reinterpret_cast<const char*>(it.first);
    }
  }

  // Resident set size may be smaller than the than the recorded size in
  // `StatsCollector` due to discarded memory that is tracked on page level.
  // This only holds before we account for pooled memory.
  DCHECK_GE(heap->stats_collector()->allocated_memory_size(),
            stats.resident_size_bytes);

  size_t pooled_memory = heap->page_backend()->page_pool().PooledMemory();
  stats.committed_size_bytes += pooled_memory;
  stats.resident_size_bytes += pooled_memory;
  stats.pooled_memory_size_bytes = pooled_memory;

  return stats;
}

bool HeapStatisticsCollector::VisitNormalPageSpace(NormalPageSpace& space) {
  DCHECK_EQ(0u, space.linear_allocation_buffer().size());

  FinalizeSpace(current_stats_, &current_space_stats_, &current_page_stats_);

  current_space_stats_ =
      InitializeSpace(current_stats_, GetNormalPageSpaceName(space.index()));

  space.free_list().CollectStatistics(current_space_stats_->free_list_stats);

  return false;
}

bool HeapStatisticsCollector::VisitLargePageSpace(LargePageSpace& space) {
  FinalizeSpace(current_stats_, &current_space_stats_, &current_page_stats_);

  current_space_stats_ = InitializeSpace(current_stats_, "LargePageSpace");

  return false;
}

bool HeapStatisticsCollector::VisitNormalPage(NormalPage& page) {
  DCHECK_NOT_NULL(current_space_stats_);
  FinalizePage(current_space_stats_, &current_page_stats_);

  current_page_stats_ = InitializePage(current_space_stats_);
  current_page_stats_->committed_size_bytes = kPageSize;
  current_page_stats_->resident_size_bytes =
      kPageSize - page.discarded_memory();
  return false;
}

bool HeapStatisticsCollector::VisitLargePage(LargePage& page) {
  DCHECK_NOT_NULL(current_space_stats_);
  FinalizePage(current_space_stats_, &current_page_stats_);

  const size_t object_size = page.PayloadSize();
  const size_t allocated_size = LargePage::AllocationSize(object_size);
  current_page_stats_ = InitializePage(current_space_stats_);
  current_page_stats_->committed_size_bytes = allocated_size;
  current_page_stats_->resident_size_bytes = allocated_size;
  return false;
}

bool HeapStatisticsCollector::VisitHeapObjectHeader(HeapObjectHeader& header) {
  if (header.IsFree()) return true;

  DCHECK_NOT_NULL(current_space_stats_);
  DCHECK_NOT_NULL(current_page_stats_);
  // For the purpose of heap statistics, the header counts towards the allocated
  // object size.
  const size_t allocated_object_size =
      header.IsLargeObject()
          ? LargePage::From(
                BasePage::FromPayload(const_cast<HeapObjectHeader*>(&header)))
                ->PayloadSize()
          : header.AllocatedSize();
  RecordObjectType(type_name_to_index_map_,
                   current_page_stats_->object_statistics, &header,
                   allocated_object_size);
  current_page_stats_->used_size_bytes += allocated_object_size;
  return true;
}

}  // namespace internal
}  // namespace cppgc
