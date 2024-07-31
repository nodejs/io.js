// Copyright 2012 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/zone/zone.h"

#include <cstring>
#include <memory>

#include "src/base/sanitizer/asan.h"
#include "src/init/v8.h"
#include "src/utils/utils.h"
#include "src/zone/type-stats.h"

namespace v8 {
namespace internal {

namespace {

#ifdef V8_USE_ADDRESS_SANITIZER

constexpr size_t kASanRedzoneBytes = 24;  // Must be a multiple of 8.

#else  // !V8_USE_ADDRESS_SANITIZER

constexpr size_t kASanRedzoneBytes = 0;

#endif  // V8_USE_ADDRESS_SANITIZER

}  // namespace

Zone::Zone(AccountingAllocator* allocator, const char* name,
           bool support_compression)
    : allocator_(allocator),
      name_(name),
      supports_compression_(support_compression) {
  allocator_->TraceZoneCreation(this);
}

Zone::~Zone() {
  DeleteAll();
  DCHECK_EQ(segment_bytes_allocated_.load(), 0);
}

void* Zone::AsanNew(size_t size) {
  CHECK(!sealed_);

  // Round up the requested size to fit the alignment.
  size = RoundUp(size, kAlignmentInBytes);

  // Check if the requested size is available without expanding.
  const size_t size_with_redzone = size + kASanRedzoneBytes;
  DCHECK_LE(position_, limit_);
  if (V8_UNLIKELY(size_with_redzone > limit_ - position_)) {
    Expand(size_with_redzone);
  }
  DCHECK_LE(size_with_redzone, limit_ - position_);

  Address result = position_;
  position_ += size_with_redzone;

  Address redzone_position = result + size;
  DCHECK_EQ(redzone_position + kASanRedzoneBytes, position_);
  ASAN_POISON_MEMORY_REGION(reinterpret_cast<void*>(redzone_position),
                            kASanRedzoneBytes);

  // Check that the result has the proper alignment and return it.
  DCHECK(IsAligned(result, kAlignmentInBytes));
  return reinterpret_cast<void*>(result);
}

void Zone::Reset() {
  if (!segment_head_) return;
  Segment* keep = segment_head_;
  segment_head_ = segment_head_->next();
  if (segment_head_ != nullptr) {
    // Reset the position to the end of the new head, and uncommit its
    // allocation size (which will be re-committed in DeleteAll).
    position_ = segment_head_->end();
    allocation_size_ -= segment_head_->end() - segment_head_->start();
  }
  keep->set_next(nullptr);
  DeleteAll();
  allocator_->TraceZoneCreation(this);

  // Un-poison the kept segment content so we can zap and re-use it.
  ASAN_UNPOISON_MEMORY_REGION(reinterpret_cast<void*>(keep->start()),
                              keep->capacity());
  keep->ZapContents();

  segment_head_ = keep;
  position_ = RoundUp(keep->start(), kAlignmentInBytes);
  limit_ = keep->end();
  DCHECK_LT(allocation_size(), kAlignmentInBytes);
  DCHECK_EQ(segment_bytes_allocated_, keep->total_size());
}

#ifdef DEBUG
bool Zone::Contains(void* ptr) {
  Address address = reinterpret_cast<Address>(ptr);
  for (Segment* segment = segment_head_; segment != nullptr;
       segment = segment->next()) {
    if (address >= segment->start() && address < segment->end()) {
      return true;
    }
  }
  return false;
}
#endif

void Zone::DeleteAll() {
  Segment* current = segment_head_;
  if (current) {
    // Commit the allocation_size_ of segment_head_ and disconnect the segments
    // list from the zone in order to ensure that tracing accounting allocator
    // will observe value including memory from the head segment.
    allocation_size_ = allocation_size();
    segment_head_ = nullptr;
  }
  allocator_->TraceZoneDestruction(this);

  // Traverse the chained list of segments and return them all to the allocator.
  while (current) {
    Segment* next = current->next();
    segment_bytes_allocated_ -= current->total_size();
    ReleaseSegment(current);
    current = next;
  }

  position_ = limit_ = 0;
  allocation_size_ = 0;
#ifdef V8_ENABLE_PRECISE_ZONE_STATS
  allocation_size_for_tracing_ = 0;
#endif
}

void Zone::ReleaseSegment(Segment* segment) {
  // Un-poison the segment content so we can re-use or zap it later.
  ASAN_UNPOISON_MEMORY_REGION(reinterpret_cast<void*>(segment->start()),
                              segment->capacity());
  allocator_->ReturnSegment(segment, supports_compression());
}

void Zone::Expand(size_t size) {
  // Make sure the requested size is already properly aligned and that
  // there isn't enough room in the Zone to satisfy the request.
  DCHECK_EQ(size, RoundDown(size, kAlignmentInBytes));
  DCHECK_LT(limit_ - position_, size);

  // Compute the new segment size. We use a 'high water mark'
  // strategy, where we increase the segment size every time we expand
  // except that we employ a maximum segment size when we delete. This
  // is to avoid excessive malloc() and free() overhead.
  Segment* head = segment_head_;
  const size_t old_size = head ? head->total_size() : 0;
  static const size_t kSegmentOverhead = sizeof(Segment) + kAlignmentInBytes;
  const size_t new_size_no_overhead = size + (old_size << 1);
  size_t new_size = kSegmentOverhead + new_size_no_overhead;
  const size_t min_new_size = kSegmentOverhead + size;
  // Guard against integer overflow.
  if (new_size_no_overhead < size || new_size < kSegmentOverhead) {
    V8::FatalProcessOutOfMemory(nullptr, "Zone");
  }
  if (new_size < kMinimumSegmentSize) {
    new_size = kMinimumSegmentSize;
  } else if (new_size >= kMaximumSegmentSize) {
    // Limit the size of new segments to avoid growing the segment size
    // exponentially, thus putting pressure on contiguous virtual address space.
    // All the while making sure to allocate a segment large enough to hold the
    // requested size.
    new_size = std::max({min_new_size, kMaximumSegmentSize});
  }
  if (new_size > INT_MAX) {
    V8::FatalProcessOutOfMemory(nullptr, "Zone");
  }
  Segment* segment =
      allocator_->AllocateSegment(new_size, supports_compression());
  if (segment == nullptr) {
    V8::FatalProcessOutOfMemory(nullptr, "Zone");
  }

  DCHECK_GE(segment->total_size(), new_size);
  segment_bytes_allocated_ += segment->total_size();
  segment->set_zone(this);
  segment->set_next(segment_head_);
  // Commit the allocation_size_ of segment_head_ if any, in order to ensure
  // that tracing accounting allocator will observe value including memory
  // from the previous head segment.
  allocation_size_ = allocation_size();
  segment_head_ = segment;
  allocator_->TraceAllocateSegment(segment);

  // Recompute 'top' and 'limit' based on the new segment.
  position_ = RoundUp(segment->start(), kAlignmentInBytes);
  limit_ = segment->end();
  DCHECK_LE(position_, limit_);
  DCHECK_LE(size, limit_ - position_);
}

ZoneScope::ZoneScope(Zone* zone)
    : zone_(zone),
#ifdef V8_ENABLE_PRECISE_ZONE_STATS
      allocation_size_for_tracing_(zone->allocation_size_for_tracing_),
      freed_size_for_tracing_(zone->freed_size_for_tracing_),
#endif
      allocation_size_(zone->allocation_size_),
      segment_bytes_allocated_(zone->segment_bytes_allocated_),
      position_(zone->position_),
      limit_(zone->limit_),
      segment_head_(zone->segment_head_) {
}

ZoneScope::~ZoneScope() {
  // Release segments up to the stored segment_head_.
  Segment* current = zone_->segment_head_;
  while (current != segment_head_) {
    Segment* next = current->next();
    zone_->ReleaseSegment(current);
    current = next;
  }

  // Un-poison the trailing segment content so we can re-use or zap it later.
  if (segment_head_ != nullptr) {
    void* const start = reinterpret_cast<void*>(position_);
    DCHECK_GE(start, reinterpret_cast<void*>(current->start()));
    DCHECK_LE(start, reinterpret_cast<void*>(current->end()));
    const size_t length = current->end() - reinterpret_cast<Address>(start);
    ASAN_UNPOISON_MEMORY_REGION(start, length);
  }

  // Reset the Zone to the stored state.
  zone_->allocation_size_ = allocation_size_;
  zone_->segment_bytes_allocated_ = segment_bytes_allocated_;
  zone_->position_ = position_;
  zone_->limit_ = limit_;
  zone_->segment_head_ = segment_head_;
#ifdef V8_ENABLE_PRECISE_ZONE_STATS
  zone_->allocation_size_for_tracing_ = allocation_size_for_tracing_;
  zone_->freed_size_for_tracing_ = freed_size_for_tracing_;
#endif
}

}  // namespace internal
}  // namespace v8
