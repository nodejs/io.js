// Copyright 2019 The Abseil Authors.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      https://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "absl/strings/internal/cordz_info.h"

#include "absl/base/config.h"
#include "absl/base/internal/spinlock.h"
#include "absl/container/inlined_vector.h"
#include "absl/debugging/stacktrace.h"
#include "absl/strings/internal/cord_internal.h"
#include "absl/strings/internal/cord_rep_btree.h"
#include "absl/strings/internal/cord_rep_crc.h"
#include "absl/strings/internal/cordz_handle.h"
#include "absl/strings/internal/cordz_statistics.h"
#include "absl/strings/internal/cordz_update_tracker.h"
#include "absl/synchronization/mutex.h"
#include "absl/time/clock.h"
#include "absl/types/span.h"

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace cord_internal {

#ifdef ABSL_INTERNAL_NEED_REDUNDANT_CONSTEXPR_DECL
constexpr size_t CordzInfo::kMaxStackDepth;
#endif

ABSL_CONST_INIT CordzInfo::List CordzInfo::global_list_{absl::kConstInit};

namespace {

// CordRepAnalyzer performs the analysis of a cord.
//
// It computes absolute node counts and total memory usage, and an 'estimated
// fair share memory usage` statistic.
// Conceptually, it divides the 'memory usage' at each location in the 'cord
// graph' by the cumulative reference count of that location. The cumulative
// reference count is the factored total of all edges leading into that node.
//
// The top level node is treated specially: we assume the current thread
// (typically called from the CordzHandler) to hold a reference purely to
// perform a safe analysis, and not being part of the application. So we
// subtract 1 from the reference count of the top node to compute the
// 'application fair share' excluding the reference of the current thread.
//
// An example of fair sharing, and why we multiply reference counts:
// Assume we have 2 CordReps, both being a Substring referencing a Flat:
//   CordSubstring A (refcount = 5) --> child Flat C (refcount = 2)
//   CordSubstring B (refcount = 9) --> child Flat C (refcount = 2)
//
// Flat C has 2 incoming edges from the 2 substrings (refcount = 2) and is not
// referenced directly anywhere else. Translated into a 'fair share', we then
// attribute 50% of the memory (memory / refcount = 2) to each incoming edge.
// Rep A has a refcount of 5, so we attribute each incoming edge 1 / 5th of the
// memory cost below it, i.e.: the fair share of Rep A of the memory used by C
// is then 'memory C / (refcount C * refcount A) + (memory A / refcount A)'.
// It is also easy to see how all incoming edges add up to 100%.
class CordRepAnalyzer {
 public:
  // Creates an analyzer instance binding to `statistics`.
  explicit CordRepAnalyzer(CordzStatistics& statistics)
      : statistics_(statistics) {}

  // Analyzes the memory statistics and node counts for the provided `rep`, and
  // adds the results to `statistics`. Note that node counts and memory sizes
  // are not initialized, computed values are added to any existing values.
  void AnalyzeCordRep(const CordRep* rep) {
    ABSL_ASSERT(rep != nullptr);

    // Process all linear nodes.
    // As per the class comments, use refcout - 1 on the top level node, as the
    // top level node is assumed to be referenced only for analysis purposes.
    size_t refcount = rep->refcount.Get();
    RepRef repref{rep, (refcount > 1) ? refcount - 1 : 1};

    // Process the top level CRC node, if present.
    if (repref.tag() == CRC) {
      statistics_.node_count++;
      statistics_.node_counts.crc++;
      memory_usage_.Add(sizeof(CordRepCrc), repref.refcount);
      repref = repref.Child(repref.rep->crc()->child);
    }

    // Process all top level linear nodes (substrings and flats).
    repref = CountLinearReps(repref, memory_usage_);

    switch (repref.tag()) {
      case CordRepKind::BTREE:
        AnalyzeBtree(repref);
        break;
      default:
        // We should have a btree node if not null.
        ABSL_ASSERT(repref.tag() == CordRepKind::UNUSED_0);
        break;
    }

    // Adds values to output
    statistics_.estimated_memory_usage += memory_usage_.total;
    statistics_.estimated_fair_share_memory_usage +=
        static_cast<size_t>(memory_usage_.fair_share);
  }

 private:
  // RepRef identifies a CordRep* inside the Cord tree with its cumulative
  // refcount including itself. For example, a tree consisting of a substring
  // with a refcount of 3 and a child flat with a refcount of 4 will have RepRef
  // refcounts of 3 and 12 respectively.
  struct RepRef {
    const CordRep* rep;
    size_t refcount;

    // Returns a 'child' RepRef which contains the cumulative reference count
    // of this instance multiplied by the child's reference count. Returns a
    // nullptr RepRef value with a refcount of 0 if `child` is nullptr.
    RepRef Child(const CordRep* child) const {
      if (child == nullptr) return RepRef{nullptr, 0};
      return RepRef{child, refcount * child->refcount.Get()};
    }

    // Returns the tag of this rep, or UNUSED_0 if this instance is null
    constexpr CordRepKind tag() const {
      ABSL_ASSERT(rep == nullptr || rep->tag != CordRepKind::UNUSED_0);
      return rep ? static_cast<CordRepKind>(rep->tag) : CordRepKind::UNUSED_0;
    }
  };

  // Memory usage values
  struct MemoryUsage {
    size_t total = 0;
    double fair_share = 0.0;

    // Adds 'size` memory usage to this class, with a cumulative (recursive)
    // reference count of `refcount`
    void Add(size_t size, size_t refcount) {
      total += size;
      fair_share += static_cast<double>(size) / refcount;
    }
  };

  // Counts a flat of the provide allocated size
  void CountFlat(size_t size) {
    statistics_.node_count++;
    statistics_.node_counts.flat++;
    if (size <= 64) {
      statistics_.node_counts.flat_64++;
    } else if (size <= 128) {
      statistics_.node_counts.flat_128++;
    } else if (size <= 256) {
      statistics_.node_counts.flat_256++;
    } else if (size <= 512) {
      statistics_.node_counts.flat_512++;
    } else if (size <= 1024) {
      statistics_.node_counts.flat_1k++;
    }
  }

  // Processes 'linear' reps (substring, flat, external) not requiring iteration
  // or recursion. Returns RefRep{null} if all reps were processed, else returns
  // the top-most non-linear concat or ring cordrep.
  // Node counts are updated into `statistics_`, memory usage is update into
  // `memory_usage`, which typically references `memory_usage_` except for ring
  // buffers where we count children unrounded.
  RepRef CountLinearReps(RepRef rep, MemoryUsage& memory_usage) {
    // Consume all substrings
    while (rep.tag() == SUBSTRING) {
      statistics_.node_count++;
      statistics_.node_counts.substring++;
      memory_usage.Add(sizeof(CordRepSubstring), rep.refcount);
      rep = rep.Child(rep.rep->substring()->child);
    }

    // Consume possible FLAT
    if (rep.tag() >= FLAT) {
      size_t size = rep.rep->flat()->AllocatedSize();
      CountFlat(size);
      memory_usage.Add(size, rep.refcount);
      return RepRef{nullptr, 0};
    }

    // Consume possible external
    if (rep.tag() == EXTERNAL) {
      statistics_.node_count++;
      statistics_.node_counts.external++;
      size_t size = rep.rep->length + sizeof(CordRepExternalImpl<intptr_t>);
      memory_usage.Add(size, rep.refcount);
      return RepRef{nullptr, 0};
    }

    return rep;
  }

  // Analyzes the provided btree.
  void AnalyzeBtree(RepRef rep) {
    statistics_.node_count++;
    statistics_.node_counts.btree++;
    memory_usage_.Add(sizeof(CordRepBtree), rep.refcount);
    const CordRepBtree* tree = rep.rep->btree();
    if (tree->height() > 0) {
      for (CordRep* edge : tree->Edges()) {
        AnalyzeBtree(rep.Child(edge));
      }
    } else {
      for (CordRep* edge : tree->Edges()) {
        CountLinearReps(rep.Child(edge), memory_usage_);
      }
    }
  }

  CordzStatistics& statistics_;
  MemoryUsage memory_usage_;
};

}  // namespace

CordzInfo* CordzInfo::Head(const CordzSnapshot& snapshot) {
  ABSL_ASSERT(snapshot.is_snapshot());

  // We can do an 'unsafe' load of 'head', as we are guaranteed that the
  // instance it points to is kept alive by the provided CordzSnapshot, so we
  // can simply return the current value using an acquire load.
  // We do enforce in DEBUG builds that the 'head' value is present in the
  // delete queue: ODR violations may lead to 'snapshot' and 'global_list_'
  // being in different libraries / modules.
  CordzInfo* head = global_list_.head.load(std::memory_order_acquire);
  ABSL_ASSERT(snapshot.DiagnosticsHandleIsSafeToInspect(head));
  return head;
}

CordzInfo* CordzInfo::Next(const CordzSnapshot& snapshot) const {
  ABSL_ASSERT(snapshot.is_snapshot());

  // Similar to the 'Head()' function, we do not need a mutex here.
  CordzInfo* next = ci_next_.load(std::memory_order_acquire);
  ABSL_ASSERT(snapshot.DiagnosticsHandleIsSafeToInspect(this));
  ABSL_ASSERT(snapshot.DiagnosticsHandleIsSafeToInspect(next));
  return next;
}

void CordzInfo::TrackCord(InlineData& cord, MethodIdentifier method) {
  assert(cord.is_tree());
  assert(!cord.is_profiled());
  CordzInfo* cordz_info = new CordzInfo(cord.as_tree(), nullptr, method);
  cord.set_cordz_info(cordz_info);
  cordz_info->Track();
}

void CordzInfo::TrackCord(InlineData& cord, const InlineData& src,
                          MethodIdentifier method) {
  assert(cord.is_tree());
  assert(src.is_tree());

  // Unsample current as we the current cord is being replaced with 'src',
  // so any method history is no longer relevant.
  CordzInfo* cordz_info = cord.cordz_info();
  if (cordz_info != nullptr) cordz_info->Untrack();

  // Start new cord sample
  cordz_info = new CordzInfo(cord.as_tree(), src.cordz_info(), method);
  cord.set_cordz_info(cordz_info);
  cordz_info->Track();
}

void CordzInfo::MaybeTrackCordImpl(InlineData& cord, const InlineData& src,
                                   MethodIdentifier method) {
  if (src.is_profiled()) {
    TrackCord(cord, src, method);
  } else if (cord.is_profiled()) {
    cord.cordz_info()->Untrack();
    cord.clear_cordz_info();
  }
}

CordzInfo::MethodIdentifier CordzInfo::GetParentMethod(const CordzInfo* src) {
  if (src == nullptr) return MethodIdentifier::kUnknown;
  return src->parent_method_ != MethodIdentifier::kUnknown ? src->parent_method_
                                                           : src->method_;
}

size_t CordzInfo::FillParentStack(const CordzInfo* src, void** stack) {
  assert(stack);
  if (src == nullptr) return 0;
  if (src->parent_stack_depth_) {
    memcpy(stack, src->parent_stack_, src->parent_stack_depth_ * sizeof(void*));
    return src->parent_stack_depth_;
  }
  memcpy(stack, src->stack_, src->stack_depth_ * sizeof(void*));
  return src->stack_depth_;
}

CordzInfo::CordzInfo(CordRep* rep,
                     const CordzInfo* src,
                     MethodIdentifier method)
    : rep_(rep),
      stack_depth_(
          static_cast<size_t>(absl::GetStackTrace(stack_,
                                                  /*max_depth=*/kMaxStackDepth,
                                                  /*skip_count=*/1))),
      parent_stack_depth_(FillParentStack(src, parent_stack_)),
      method_(method),
      parent_method_(GetParentMethod(src)),
      create_time_(absl::Now()) {
  update_tracker_.LossyAdd(method);
  if (src) {
    // Copy parent counters.
    update_tracker_.LossyAdd(src->update_tracker_);
  }
}

CordzInfo::~CordzInfo() {
  // `rep_` is potentially kept alive if CordzInfo is included
  // in a collection snapshot (which should be rare).
  if (ABSL_PREDICT_FALSE(rep_)) {
    CordRep::Unref(rep_);
  }
}

void CordzInfo::Track() {
  SpinLockHolder l(&list_->mutex);

  CordzInfo* const head = list_->head.load(std::memory_order_acquire);
  if (head != nullptr) {
    head->ci_prev_.store(this, std::memory_order_release);
  }
  ci_next_.store(head, std::memory_order_release);
  list_->head.store(this, std::memory_order_release);
}

void CordzInfo::Untrack() {
  ODRCheck();
  {
    SpinLockHolder l(&list_->mutex);

    CordzInfo* const head = list_->head.load(std::memory_order_acquire);
    CordzInfo* const next = ci_next_.load(std::memory_order_acquire);
    CordzInfo* const prev = ci_prev_.load(std::memory_order_acquire);

    if (next) {
      ABSL_ASSERT(next->ci_prev_.load(std::memory_order_acquire) == this);
      next->ci_prev_.store(prev, std::memory_order_release);
    }
    if (prev) {
      ABSL_ASSERT(head != this);
      ABSL_ASSERT(prev->ci_next_.load(std::memory_order_acquire) == this);
      prev->ci_next_.store(next, std::memory_order_release);
    } else {
      ABSL_ASSERT(head == this);
      list_->head.store(next, std::memory_order_release);
    }
  }

  // We can no longer be discovered: perform a fast path check if we are not
  // listed on any delete queue, so we can directly delete this instance.
  if (SafeToDelete()) {
    UnsafeSetCordRep(nullptr);
    delete this;
    return;
  }

  // We are likely part of a snapshot, extend the life of the CordRep
  {
    absl::MutexLock lock(&mutex_);
    if (rep_) CordRep::Ref(rep_);
  }
  CordzHandle::Delete(this);
}

void CordzInfo::Lock(MethodIdentifier method)
    ABSL_EXCLUSIVE_LOCK_FUNCTION(mutex_) {
  mutex_.Lock();
  update_tracker_.LossyAdd(method);
  assert(rep_);
}

void CordzInfo::Unlock() ABSL_UNLOCK_FUNCTION(mutex_) {
  bool tracked = rep_ != nullptr;
  mutex_.Unlock();
  if (!tracked) {
    Untrack();
  }
}

absl::Span<void* const> CordzInfo::GetStack() const {
  return absl::MakeConstSpan(stack_, stack_depth_);
}

absl::Span<void* const> CordzInfo::GetParentStack() const {
  return absl::MakeConstSpan(parent_stack_, parent_stack_depth_);
}

CordzStatistics CordzInfo::GetCordzStatistics() const {
  CordzStatistics stats;
  stats.method = method_;
  stats.parent_method = parent_method_;
  stats.update_tracker = update_tracker_;
  if (CordRep* rep = RefCordRep()) {
    stats.size = rep->length;
    CordRepAnalyzer analyzer(stats);
    analyzer.AnalyzeCordRep(rep);
    CordRep::Unref(rep);
  }
  return stats;
}

}  // namespace cord_internal
ABSL_NAMESPACE_END
}  // namespace absl
