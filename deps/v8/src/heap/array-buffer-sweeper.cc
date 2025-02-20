// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/heap/array-buffer-sweeper.h"

#include <atomic>
#include <memory>
#include <utility>

#include "src/base/logging.h"
#include "src/heap/gc-tracer-inl.h"
#include "src/heap/gc-tracer.h"
#include "src/heap/heap-inl.h"
#include "src/heap/heap-layout-inl.h"
#include "src/heap/heap.h"
#include "src/objects/js-array-buffer.h"

namespace v8 {
namespace internal {

size_t ArrayBufferList::Append(ArrayBufferExtension* extension) {
  if (head_ == nullptr) {
    DCHECK_NULL(tail_);
    head_ = tail_ = extension;
  } else {
    tail_->set_next(extension);
    tail_ = extension;
  }

  const size_t accounting_length = [&] {
    if (age_ == ArrayBufferExtension::Age::kOld) {
      return extension->SetOld().accounting_length();
    } else {
      return extension->SetYoung().accounting_length();
    }
  }();
  DCHECK_GE(bytes_ + accounting_length, bytes_);
  bytes_ += accounting_length;
  extension->set_next(nullptr);
  return accounting_length;
}

void ArrayBufferList::Append(ArrayBufferList& list) {
  DCHECK_EQ(age_, list.age_);

  if (head_ == nullptr) {
    DCHECK_NULL(tail_);
    head_ = list.head_;
    tail_ = list.tail_;
  } else if (list.head_) {
    DCHECK_NOT_NULL(list.tail_);
    tail_->set_next(list.head_);
    tail_ = list.tail_;
  } else {
    DCHECK_NULL(list.tail_);
  }

  bytes_ += list.ApproximateBytes();
  list = ArrayBufferList(age_);
}

bool ArrayBufferList::ContainsSlow(ArrayBufferExtension* extension) const {
  for (ArrayBufferExtension* current = head_; current;
       current = current->next()) {
    if (current == extension) return true;
  }
  return false;
}

size_t ArrayBufferList::BytesSlow() const {
  ArrayBufferExtension* current = head_;
  size_t sum = 0;
  while (current) {
    sum += current->accounting_length();
    current = current->next();
  }
  DCHECK_GE(sum, ApproximateBytes());
  return sum;
}

bool ArrayBufferList::IsEmpty() const {
  DCHECK_IMPLIES(head_, tail_);
  DCHECK_IMPLIES(!head_, bytes_ == 0);
  return head_ == nullptr;
}

class ArrayBufferSweeper::SweepingState final {
  enum class Status { kInProgress, kDone };

 public:
  SweepingState(Heap* heap, ArrayBufferList young, ArrayBufferList old,
                SweepingType type,
                TreatAllYoungAsPromoted treat_all_young_as_promoted,
                uint64_t trace_id);

  ~SweepingState() { DCHECK(job_handle_ && !job_handle_->IsValid()); }

  void SetDone() { status_.store(Status::kDone, std::memory_order_relaxed); }
  bool IsDone() const {
    return status_.load(std::memory_order_relaxed) == Status::kDone;
  }

  void MergeTo(ArrayBufferSweeper* sweeper) {
    // the worker may see a difference between `young/old_bytes_accounted_` and
    // `initial_young/old_bytes_` due to concurrent main thread adjustments
    // (resizing).
    sweeper->young_bytes_adjustment_while_sweeping_ +=
        initial_young_bytes_ - young_bytes_accounted_;
    sweeper->old_bytes_adjustment_while_sweeping_ +=
        initial_old_bytes_ - old_bytes_accounted_;
    DCHECK_GE(new_young_.bytes_ +
                  sweeper->young_bytes_adjustment_while_sweeping_ +
                  sweeper->young_.bytes_,
              0);
    DCHECK_GE(new_old_.bytes_ + sweeper->old_bytes_adjustment_while_sweeping_ +
                  sweeper->old_.bytes_,
              0);
    sweeper->young_.Append(new_young_);
    sweeper->old_.Append(new_old_);
    // Apply pending adjustments from resizing and detaching.
    sweeper->young_.bytes_ +=
        std::exchange(sweeper->young_bytes_adjustment_while_sweeping_, 0);
    sweeper->old_.bytes_ +=
        std::exchange(sweeper->old_bytes_adjustment_while_sweeping_, 0);
    sweeper->DecrementExternalMemoryCounters(freed_bytes_);
  }

  void StartBackgroundSweeping() { job_handle_->NotifyConcurrencyIncrease(); }
  void FinishSweeping() {
    DCHECK(job_handle_ && job_handle_->IsValid());
    job_handle_->Join();
  }

 private:
  class SweepingJob;

  std::atomic<Status> status_{Status::kInProgress};
  ArrayBufferList new_young_{ArrayBufferList::Age::kYoung};
  ArrayBufferList new_old_{ArrayBufferList::Age::kOld};
  size_t freed_bytes_{0};
  const uint64_t initial_young_bytes_{0};
  const uint64_t initial_old_bytes_{0};
  // Track bytes accounted bytes during sweeping, including freed and promoted
  // bytes. This is used to compute adjustment when sweeping finishes.
  uint64_t young_bytes_accounted_{0};
  uint64_t old_bytes_accounted_{0};
  std::unique_ptr<JobHandle> job_handle_;
};

class ArrayBufferSweeper::SweepingState::SweepingJob final : public JobTask {
 public:
  SweepingJob(Heap* heap, SweepingState& state, ArrayBufferList young,
              ArrayBufferList old, SweepingType type,
              TreatAllYoungAsPromoted treat_all_young_as_promoted,
              uint64_t trace_id)
      : heap_(heap),
        state_(state),
        young_(young),
        old_(old),
        type_(type),
        treat_all_young_as_promoted_(treat_all_young_as_promoted),
        trace_id_(trace_id),
        local_sweeper_(heap_->sweeper()) {}

  ~SweepingJob() override = default;

  SweepingJob(const SweepingJob&) = delete;
  SweepingJob& operator=(const SweepingJob&) = delete;

  void Run(JobDelegate* delegate) final;

  size_t GetMaxConcurrency(size_t worker_count) const override {
    return state_.IsDone() ? 0 : 1;
  }

 private:
  void Sweep(JobDelegate* delegate);
  // Returns true if sweeping finished. Returns false if sweeping yielded while
  // there are still array buffers left to sweep.
  bool SweepYoung(JobDelegate* delegate);
  bool SweepFull(JobDelegate* delegate);
  bool SweepListFull(JobDelegate* delegate, ArrayBufferList& list,
                     ArrayBufferExtension::Age age);

  Heap* const heap_;
  SweepingState& state_;
  ArrayBufferList young_{ArrayBufferList::Age::kYoung};
  ArrayBufferList old_{ArrayBufferList::Age::kOld};
  const SweepingType type_;
  const TreatAllYoungAsPromoted treat_all_young_as_promoted_;
  const uint64_t trace_id_;
  Sweeper::LocalSweeper local_sweeper_;
};

void ArrayBufferSweeper::SweepingState::SweepingJob::Run(
    JobDelegate* delegate) {
  const ThreadKind thread_kind =
      delegate->IsJoiningThread() ? ThreadKind::kMain : ThreadKind::kBackground;
  if (treat_all_young_as_promoted_ == TreatAllYoungAsPromoted::kNo) {
    // Waiting for promoted page iteration is only needed when not all young
    // array buffers are promoted.
    GCTracer::Scope::ScopeId scope_id =
        type_ == SweepingType::kYoung
            ? thread_kind == ThreadKind::kMain
                  ? GCTracer::Scope::MINOR_MS_SWEEP
                  : GCTracer::Scope::MINOR_MS_BACKGROUND_SWEEPING
        : thread_kind == ThreadKind::kMain
            ? GCTracer::Scope::MC_SWEEP
            : GCTracer::Scope::MC_BACKGROUND_SWEEPING;
    TRACE_GC_EPOCH_WITH_FLOW(
        heap_->tracer(), scope_id, thread_kind,
        heap_->sweeper()->GetTraceIdForFlowEvent(scope_id),
        TRACE_EVENT_FLAG_FLOW_IN | TRACE_EVENT_FLAG_FLOW_OUT);
    const bool finished =
        local_sweeper_.ContributeAndWaitForPromotedPagesIteration(delegate);
    DCHECK_IMPLIES(delegate->IsJoiningThread(), finished);
    if (!finished) return;
    DCHECK(!heap_->sweeper()->IsIteratingPromotedPages());
  }
  GCTracer::Scope::ScopeId scope_id =
      type_ == SweepingType::kYoung
          ? thread_kind == ThreadKind::kMain
                ? GCTracer::Scope::YOUNG_ARRAY_BUFFER_SWEEP
                : GCTracer::Scope::BACKGROUND_YOUNG_ARRAY_BUFFER_SWEEP
      : thread_kind == ThreadKind::kMain
          ? GCTracer::Scope::FULL_ARRAY_BUFFER_SWEEP
          : GCTracer::Scope::BACKGROUND_FULL_ARRAY_BUFFER_SWEEP;
  TRACE_GC_EPOCH_WITH_FLOW(heap_->tracer(), scope_id, thread_kind, trace_id_,
                           TRACE_EVENT_FLAG_FLOW_IN);
  Sweep(delegate);
}

ArrayBufferSweeper::SweepingState::SweepingState(
    Heap* heap, ArrayBufferList young, ArrayBufferList old,
    ArrayBufferSweeper::SweepingType type,
    ArrayBufferSweeper::TreatAllYoungAsPromoted treat_all_young_as_promoted,
    uint64_t trace_id)
    : initial_young_bytes_(young.bytes_),
      initial_old_bytes_(old.bytes_),
      job_handle_(V8::GetCurrentPlatform()->CreateJob(
          TaskPriority::kUserVisible,
          std::make_unique<SweepingJob>(
              heap, *this, std::move(young), std::move(old), type,
              treat_all_young_as_promoted, trace_id))) {}

ArrayBufferSweeper::ArrayBufferSweeper(Heap* heap) : heap_(heap) {}

ArrayBufferSweeper::~ArrayBufferSweeper() {
  EnsureFinished();
  ReleaseAll(&old_);
  ReleaseAll(&young_);
}

void ArrayBufferSweeper::EnsureFinished() {
  if (!sweeping_in_progress()) return;

  Finish();
}

void ArrayBufferSweeper::Finish() {
  state_->FinishSweeping();

  Finalize();
  DCHECK_LE(heap_->backing_store_bytes(), SIZE_MAX);
  DCHECK(!sweeping_in_progress());
}

void ArrayBufferSweeper::FinishIfDone() {
  if (sweeping_in_progress()) {
    DCHECK(state_);
    if (state_->IsDone()) {
      Finish();
    }
  }
}

void ArrayBufferSweeper::RequestSweep(
    SweepingType type, TreatAllYoungAsPromoted treat_all_young_as_promoted) {
  DCHECK(!sweeping_in_progress());

  if (young_.IsEmpty() && (old_.IsEmpty() || type == SweepingType::kYoung))
    return;

  GCTracer::Scope::ScopeId scope_id =
      type == SweepingType::kYoung
          ? v8_flags.minor_ms
                ? GCTracer::Scope::MINOR_MS_FINISH_SWEEP_ARRAY_BUFFERS
                : GCTracer::Scope::SCAVENGER_SWEEP_ARRAY_BUFFERS
          : GCTracer::Scope::MC_FINISH_SWEEP_ARRAY_BUFFERS;
  auto trace_id = GetTraceIdForFlowEvent(scope_id);
  TRACE_GC_WITH_FLOW(heap_->tracer(), scope_id, trace_id,
                     TRACE_EVENT_FLAG_FLOW_OUT);
  Prepare(type, treat_all_young_as_promoted, trace_id);
  DCHECK_IMPLIES(v8_flags.minor_ms && type == SweepingType::kYoung,
                 !heap_->ShouldReduceMemory());
  if (!heap_->IsTearingDown() && !heap_->ShouldReduceMemory() &&
      v8_flags.concurrent_array_buffer_sweeping &&
      heap_->ShouldUseBackgroundThreads()) {
    state_->StartBackgroundSweeping();
  } else {
    Finish();
  }
}

void ArrayBufferSweeper::Prepare(
    SweepingType type, TreatAllYoungAsPromoted treat_all_young_as_promoted,
    uint64_t trace_id) {
  DCHECK(!sweeping_in_progress());
  DCHECK_IMPLIES(type == SweepingType::kFull,
                 treat_all_young_as_promoted == TreatAllYoungAsPromoted::kYes);
  switch (type) {
    case SweepingType::kYoung: {
      state_ = std::make_unique<SweepingState>(
          heap_, std::move(young_), ArrayBufferList(ArrayBufferList::Age::kOld),
          type, treat_all_young_as_promoted, trace_id);
      young_ = ArrayBufferList(ArrayBufferList::Age::kYoung);
    } break;
    case SweepingType::kFull: {
      state_ = std::make_unique<SweepingState>(
          heap_, std::move(young_), std::move(old_), type,
          treat_all_young_as_promoted, trace_id);
      young_ = ArrayBufferList(ArrayBufferList::Age::kYoung);
      old_ = ArrayBufferList(ArrayBufferList::Age::kOld);
    } break;
  }
  DCHECK(sweeping_in_progress());
}

void ArrayBufferSweeper::Finalize() {
  DCHECK(sweeping_in_progress());
  CHECK(state_->IsDone());
  state_->MergeTo(this);
  state_.reset();
  DCHECK(!sweeping_in_progress());
}

void ArrayBufferSweeper::ReleaseAll(ArrayBufferList* list) {
  ArrayBufferExtension* current = list->head_;
  while (current) {
    ArrayBufferExtension* next = current->next();
    const size_t bytes = current->ClearAccountingLength().accounting_length();
    DecrementExternalMemoryCounters(bytes);
    FinalizeAndDelete(current);
    current = next;
  }
  *list = ArrayBufferList(list->age_);
}

void ArrayBufferSweeper::Append(ArrayBufferExtension* extension) {
  size_t bytes = extension->accounting_length();

  FinishIfDone();

  switch (extension->age()) {
    case ArrayBufferExtension::Age::kYoung:
      young_.Append(extension);
      break;
    case v8::internal::ArrayBufferExtension::Age::kOld:
      old_.Append(extension);
      break;
  }

  IncrementExternalMemoryCounters(bytes);
}

void ArrayBufferSweeper::Resize(ArrayBufferExtension* extension,
                                int64_t delta) {
  FinishIfDone();

  const ArrayBufferExtension::AccountingState previous_value =
      extension->UpdateAccountingLength(delta);
  UpdateApproximateBytes(delta, previous_value.age());
  if (delta > 0) {
    IncrementExternalMemoryCounters(delta);
  } else {
    DecrementExternalMemoryCounters(-delta);
  }
}

void ArrayBufferSweeper::Detach(ArrayBufferExtension* extension) {
  // Finish sweeping here first such that the code below is guaranteed to
  // observe the same sweeping state.
  FinishIfDone();

  ArrayBufferExtension::AccountingState previous_value =
      extension->ClearAccountingLength();

  // We cannot free the extension eagerly here, since extensions are tracked in
  // a singly linked list. The next GC will remove it automatically.

  UpdateApproximateBytes(-previous_value.accounting_length(),
                         previous_value.age());
  DecrementExternalMemoryCounters(previous_value.accounting_length());
}

void ArrayBufferSweeper::UpdateApproximateBytes(int64_t delta,
                                                ArrayBufferExtension::Age age) {
  switch (age) {
    case ArrayBufferExtension::Age::kYoung:
      if (!sweeping_in_progress()) {
        DCHECK_GE(young_.bytes_, -delta);
        young_.bytes_ += delta;
      } else {
        young_bytes_adjustment_while_sweeping_ += delta;
      }
      break;
    case ArrayBufferExtension::Age::kOld:
      if (!sweeping_in_progress()) {
        DCHECK_GE(old_.bytes_, -delta);
        old_.bytes_ += delta;
      } else {
        old_bytes_adjustment_while_sweeping_ += delta;
      }
  }
}

void ArrayBufferSweeper::IncrementExternalMemoryCounters(size_t bytes) {
  if (bytes == 0) return;
  heap_->IncrementExternalBackingStoreBytes(
      ExternalBackingStoreType::kArrayBuffer, bytes);
  external_memory_accounter_.Increase(
      reinterpret_cast<v8::Isolate*>(heap_->isolate()), bytes);
}

void ArrayBufferSweeper::DecrementExternalMemoryCounters(size_t bytes) {
  if (bytes == 0) return;
  heap_->DecrementExternalBackingStoreBytes(
      ExternalBackingStoreType::kArrayBuffer, bytes);
  external_memory_accounter_.Decrease(
      reinterpret_cast<v8::Isolate*>(heap_->isolate()), bytes);
}

void ArrayBufferSweeper::FinalizeAndDelete(ArrayBufferExtension* extension) {
#ifdef V8_COMPRESS_POINTERS
  extension->ZapExternalPointerTableEntry();
#endif  // V8_COMPRESS_POINTERS
  delete extension;
}

void ArrayBufferSweeper::SweepingState::SweepingJob::Sweep(
    JobDelegate* delegate) {
  CHECK(!state_.IsDone());
  bool is_finished;
  switch (type_) {
    case SweepingType::kYoung:
      is_finished = SweepYoung(delegate);
      break;
    case SweepingType::kFull:
      is_finished = SweepFull(delegate);
      break;
  }
  if (is_finished) {
    state_.SetDone();
  } else {
    TRACE_GC_NOTE("ArrayBufferSweeper Preempted");
  }
}

bool ArrayBufferSweeper::SweepingState::SweepingJob::SweepFull(
    JobDelegate* delegate) {
  DCHECK_EQ(SweepingType::kFull, type_);
  if (!SweepListFull(delegate, young_, ArrayBufferExtension::Age::kYoung))
    return false;
  return SweepListFull(delegate, old_, ArrayBufferExtension::Age::kOld);
}

bool ArrayBufferSweeper::SweepingState::SweepingJob::SweepListFull(
    JobDelegate* delegate, ArrayBufferList& list,
    ArrayBufferExtension::Age age) {
  static constexpr size_t kYieldCheckInterval = 256;
  static_assert(base::bits::IsPowerOfTwo(kYieldCheckInterval),
                "kYieldCheckInterval must be power of 2");

  ArrayBufferExtension* current = list.head_;

  ArrayBufferList& new_old = state_.new_old_;
  size_t freed_bytes = 0;
  size_t accounted_bytes = 0;
  size_t swept_extensions = 0;

  while (current) {
    DCHECK_EQ(list.age_, current->age());
    if ((swept_extensions++ & (kYieldCheckInterval - 1)) == 0) {
      if (delegate->ShouldYield()) break;
    }
    ArrayBufferExtension* next = current->next();

    if (!current->IsMarked()) {
      freed_bytes += current->accounting_length();
      FinalizeAndDelete(current);
    } else {
      current->Unmark();
      accounted_bytes += new_old.Append(current);
    }

    current = next;
  }

  state_.freed_bytes_ += freed_bytes;
  if (age == ArrayBufferExtension::Age::kYoung) {
    state_.young_bytes_accounted_ += (freed_bytes + accounted_bytes);
  } else {
    state_.old_bytes_accounted_ += (freed_bytes + accounted_bytes);
  }

  list.head_ = current;
  return !current;
}

bool ArrayBufferSweeper::SweepingState::SweepingJob::SweepYoung(
    JobDelegate* delegate) {
  static constexpr size_t kYieldCheckInterval = 256;
  static_assert(base::bits::IsPowerOfTwo(kYieldCheckInterval),
                "kYieldCheckInterval must be power of 2");

  DCHECK_EQ(SweepingType::kYoung, type_);
  ArrayBufferExtension* current = young_.head_;

  ArrayBufferList& new_old = state_.new_old_;
  ArrayBufferList& new_young = state_.new_young_;
  size_t freed_bytes = 0;
  size_t accounted_bytes = 0;
  size_t swept_extensions = 0;

  while (current) {
    DCHECK_EQ(ArrayBufferExtension::Age::kYoung, current->age());
    if ((swept_extensions++ & (kYieldCheckInterval - 1)) == 0) {
      if (delegate->ShouldYield()) break;
    }
    ArrayBufferExtension* next = current->next();

    if (!current->IsYoungMarked()) {
      const size_t bytes = current->accounting_length();
      FinalizeAndDelete(current);
      if (bytes) freed_bytes += bytes;
    } else {
      if ((treat_all_young_as_promoted_ == TreatAllYoungAsPromoted::kYes) ||
          current->IsYoungPromoted()) {
        current->YoungUnmark();
        accounted_bytes += new_old.Append(current);
      } else {
        current->YoungUnmark();
        accounted_bytes += new_young.Append(current);
      }
    }

    current = next;
  }

  state_.freed_bytes_ += freed_bytes;
  // Update young/old_bytes_accounted_; the worker may see a difference between
  // this and `initial_young/old_bytes_` due to concurrent main thread
  // adjustments.
  state_.young_bytes_accounted_ += (freed_bytes + accounted_bytes);

  young_.head_ = current;
  return !current;
}

uint64_t ArrayBufferSweeper::GetTraceIdForFlowEvent(
    GCTracer::Scope::ScopeId scope_id) const {
  return reinterpret_cast<uint64_t>(this) ^
         heap_->tracer()->CurrentEpoch(scope_id);
}

}  // namespace internal
}  // namespace v8
