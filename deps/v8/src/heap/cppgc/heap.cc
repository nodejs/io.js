// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/heap/cppgc/heap.h"

#include "include/cppgc/heap-consistency.h"
#include "src/heap/base/stack.h"
#include "src/heap/cppgc/garbage-collector.h"
#include "src/heap/cppgc/gc-invoker.h"
#include "src/heap/cppgc/heap-config.h"
#include "src/heap/cppgc/heap-object-header.h"
#include "src/heap/cppgc/heap-visitor.h"
#include "src/heap/cppgc/marker.h"
#include "src/heap/cppgc/marking-verifier.h"
#include "src/heap/cppgc/prefinalizer-handler.h"
#include "src/heap/cppgc/stats-collector.h"
#include "src/heap/cppgc/sweeper.h"
#include "src/heap/cppgc/unmarker.h"

namespace cppgc {

namespace {

void VerifyCustomSpaces(
    const std::vector<std::unique_ptr<CustomSpaceBase>>& custom_spaces) {
  // Ensures that user-provided custom spaces have indices that form a sequence
  // starting at 0.
#ifdef DEBUG
  for (size_t i = 0; i < custom_spaces.size(); ++i) {
    DCHECK_EQ(i, custom_spaces[i]->GetCustomSpaceIndex().value);
  }
#endif  // DEBUG
}

}  // namespace

std::unique_ptr<Heap> Heap::Create(std::shared_ptr<cppgc::Platform> platform,
                                   cppgc::Heap::HeapOptions options) {
  DCHECK(platform.get());
  VerifyCustomSpaces(options.custom_spaces);
  return std::make_unique<internal::Heap>(std::move(platform),
                                          std::move(options));
}

void Heap::ForceGarbageCollectionSlow(const char* source, const char* reason,
                                      Heap::StackState stack_state) {
  internal::Heap::From(this)->CollectGarbage(
      {internal::CollectionType::kMajor, stack_state, MarkingType::kAtomic,
       SweepingType::kAtomic,
       internal::GCConfig::FreeMemoryHandling::kDiscardWherePossible,
       internal::GCConfig::IsForcedGC::kForced});
}

AllocationHandle& Heap::GetAllocationHandle() {
  return internal::Heap::From(this)->object_allocator();
}

HeapHandle& Heap::GetHeapHandle() { return *internal::Heap::From(this); }

namespace internal {

namespace {

void CheckConfig(GCConfig config, HeapBase::MarkingType marking_support,
                 HeapBase::SweepingType sweeping_support) {
  CHECK_LE(static_cast<int>(config.marking_type),
           static_cast<int>(marking_support));
  CHECK_LE(static_cast<int>(config.sweeping_type),
           static_cast<int>(sweeping_support));
}

}  // namespace

Heap::Heap(std::shared_ptr<cppgc::Platform> platform,
           cppgc::Heap::HeapOptions options)
    : HeapBase(platform, options.custom_spaces, options.stack_support,
               options.marking_support, options.sweeping_support, gc_invoker_),
      gc_invoker_(this, platform_.get(), options.stack_support),
      growing_(&gc_invoker_, stats_collector_.get(),
               options.resource_constraints, options.marking_support,
               options.sweeping_support) {
  CHECK_IMPLIES(options.marking_support != HeapBase::MarkingType::kAtomic,
                platform_->GetForegroundTaskRunner());
  CHECK_IMPLIES(options.sweeping_support != HeapBase::SweepingType::kAtomic,
                platform_->GetForegroundTaskRunner());
#ifdef V8_ENABLE_ALLOCATION_TIMEOUT
  object_allocator().UpdateAllocationTimeout();
#endif  // V8_ENABLE_ALLOCATION_TIMEOUT
}

Heap::~Heap() {
  // Gracefully finish already running GC if any, but don't finalize live
  // objects.
  FinalizeIncrementalGarbageCollectionIfRunning(
      {CollectionType::kMajor, StackState::kMayContainHeapPointers,
       GCConfig::MarkingType::kAtomic, GCConfig::SweepingType::kAtomic});
  {
    subtle::NoGarbageCollectionScope no_gc(*this);
    sweeper_.FinishIfRunning();
  }
}

void Heap::CollectGarbage(GCConfig config) {
  DCHECK_EQ(GCConfig::MarkingType::kAtomic, config.marking_type);
  CheckConfig(config, marking_support_, sweeping_support_);

  if (!IsGCAllowed()) {
    return;
  }

  config_ = config;

  if (!IsMarking()) {
    StartGarbageCollection(config);
  }
  DCHECK(IsMarking());
  FinalizeGarbageCollection(config.stack_state);
}

void Heap::StartIncrementalGarbageCollection(GCConfig config) {
  DCHECK_NE(GCConfig::MarkingType::kAtomic, config.marking_type);
  DCHECK_NE(marking_support_, GCConfig::MarkingType::kAtomic);
  CheckConfig(config, marking_support_, sweeping_support_);

  if (IsMarking() || in_no_gc_scope()) return;

  config_ = config;

  StartGarbageCollection(config);
}

void Heap::FinalizeIncrementalGarbageCollectionIfRunning(GCConfig config) {
  CheckConfig(config, marking_support_, sweeping_support_);

  if (!IsMarking()) return;

  DCHECK(!in_no_gc_scope());

  DCHECK_NE(GCConfig::MarkingType::kAtomic, config_.marking_type);
  config_ = config;
  FinalizeGarbageCollection(config.stack_state);
}

void Heap::StartGarbageCollection(GCConfig config) {
  DCHECK(!IsMarking());
  DCHECK(!in_no_gc_scope());

  // Finish sweeping in case it is still running.
  sweeper_.FinishIfRunning();

  epoch_++;

#if defined(CPPGC_YOUNG_GENERATION)
  if (config.collection_type == CollectionType::kMajor &&
      generational_gc_supported()) {
    stats_collector()->NotifyUnmarkingStarted(config.collection_type);
    cppgc::internal::StatsCollector::EnabledScope stats_scope(
        stats_collector(), cppgc::internal::StatsCollector::kUnmark);
    SequentialUnmarker unmarker(raw_heap());
  }
#endif  // defined(CPPGC_YOUNG_GENERATION)

  const MarkingConfig marking_config{config.collection_type, config.stack_state,
                                     config.marking_type, config.is_forced_gc};
  marker_ = std::make_unique<Marker>(AsBase(), platform_.get(), marking_config);
  marker_->StartMarking();
}

void Heap::FinalizeGarbageCollection(StackState stack_state) {
  stack()->SetMarkerIfNeededAndCallback(
      [this, stack_state]() { FinalizeGarbageCollectionImpl(stack_state); });
}

void Heap::FinalizeGarbageCollectionImpl(StackState stack_state) {
  DCHECK(IsMarking());
  DCHECK(!in_no_gc_scope());
  CHECK(!IsGCForbidden());
  config_.stack_state = stack_state;
  in_atomic_pause_ = true;

#if defined(CPPGC_YOUNG_GENERATION)
  // Check if the young generation was enabled. We must enable young generation
  // before calling the custom weak callbacks to make sure that the callbacks
  // for old objects are registered in the remembered set.
  if (generational_gc_enabled_) {
    HeapBase::EnableGenerationalGC();
  }
#endif  // defined(CPPGC_YOUNG_GENERATION)
  {
    // This guards atomic pause marking, meaning that no internal method or
    // external callbacks are allowed to allocate new objects.
    cppgc::subtle::DisallowGarbageCollectionScope no_gc_scope(*this);
    marker_->FinishMarking(config_.stack_state);
  }
  marker_.reset();
  const size_t bytes_allocated_in_prefinalizers = ExecutePreFinalizers();
#if CPPGC_VERIFY_HEAP
  MarkingVerifier verifier(*this, config_.collection_type);
  verifier.Run(config_.stack_state,
               stats_collector()->marked_bytes_on_current_cycle() +
                   bytes_allocated_in_prefinalizers);
#endif  // CPPGC_VERIFY_HEAP
#ifndef CPPGC_ALLOW_ALLOCATIONS_IN_PREFINALIZERS
  DCHECK_EQ(0u, bytes_allocated_in_prefinalizers);
#endif
  USE(bytes_allocated_in_prefinalizers);

#if defined(CPPGC_YOUNG_GENERATION)
  ResetRememberedSet();
#endif  // defined(CPPGC_YOUNG_GENERATION)

  subtle::NoGarbageCollectionScope no_gc(*this);
  const SweepingConfig sweeping_config{
      config_.sweeping_type, SweepingConfig::CompactableSpaceHandling::kSweep,
      config_.free_memory_handling};
  sweeper_.Start(sweeping_config);
  if (config_.sweeping_type == SweepingConfig::SweepingType::kAtomic) {
    sweeper_.FinishIfRunning();
  }
  in_atomic_pause_ = false;
}

void Heap::EnableGenerationalGC() {
  DCHECK(!IsMarking());
  DCHECK(!generational_gc_enabled_);
  generational_gc_enabled_ = true;
}

void Heap::DisableHeapGrowingForTesting() { growing_.DisableForTesting(); }

void Heap::FinalizeIncrementalGarbageCollectionIfNeeded(
    StackState stack_state) {
  StatsCollector::EnabledScope stats_scope(
      stats_collector(), StatsCollector::kMarkIncrementalFinalize);
  FinalizeGarbageCollection(stack_state);
}

void Heap::StartIncrementalGarbageCollectionForTesting() {
  DCHECK(!IsMarking());
  DCHECK(!in_no_gc_scope());
  StartGarbageCollection({CollectionType::kMajor, StackState::kNoHeapPointers,
                          GCConfig::MarkingType::kIncrementalAndConcurrent,
                          GCConfig::SweepingType::kIncrementalAndConcurrent});
}

void Heap::FinalizeIncrementalGarbageCollectionForTesting(
    EmbedderStackState stack_state) {
  DCHECK(!in_no_gc_scope());
  DCHECK(IsMarking());
  FinalizeGarbageCollection(stack_state);
  sweeper_.FinishIfRunning();
}

}  // namespace internal
}  // namespace cppgc
