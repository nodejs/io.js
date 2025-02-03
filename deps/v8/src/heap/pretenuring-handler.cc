// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/heap/pretenuring-handler.h"

#include "src/common/globals.h"
#include "src/execution/isolate.h"
#include "src/flags/flags.h"
#include "src/handles/global-handles-inl.h"
#include "src/heap/gc-tracer-inl.h"
#include "src/heap/new-spaces.h"
#include "src/objects/allocation-site-inl.h"

namespace v8 {
namespace internal {

PretenuringHandler::PretenuringHandler(Heap* heap)
    : heap_(heap), global_pretenuring_feedback_(kInitialFeedbackCapacity) {}

PretenuringHandler::~PretenuringHandler() = default;

namespace {

static constexpr int kMinMementoCount = 100;

double GetPretenuringRatioThreshold(size_t new_space_capacity) {
  static constexpr double kScavengerPretenureRatio = 0.85;
  // MinorMS allows for a much larger new space, thus we require a lower
  // survival rate for pretenuring.
  static constexpr double kMinorMSPretenureMaxRatio = 0.8;
  static constexpr double kMinorMSMinCapacity = 16 * MB;
  if (!v8_flags.minor_ms) return kScavengerPretenureRatio;
  if (new_space_capacity <= kMinorMSMinCapacity)
    return kMinorMSPretenureMaxRatio;
  // When capacity is 64MB, the pretenuring ratio would be 0.2.
  return kMinorMSPretenureMaxRatio * kMinorMSMinCapacity / new_space_capacity;
}

inline bool MakePretenureDecision(
    Tagged<AllocationSite> site,
    AllocationSite::PretenureDecision current_decision, double ratio,
    bool new_space_capacity_was_above_pretenuring_threshold,
    size_t new_space_capacity) {
  // Here we just allow state transitions from undecided or maybe tenure
  // to don't tenure, maybe tenure, or tenure.
  if ((current_decision == AllocationSite::kUndecided ||
       current_decision == AllocationSite::kMaybeTenure)) {
    if (ratio >= GetPretenuringRatioThreshold(new_space_capacity)) {
      // We just transition into tenure state when the semi-space was at
      // maximum capacity.
      if (new_space_capacity_was_above_pretenuring_threshold) {
        site->set_deopt_dependent_code(true);
        site->set_pretenure_decision(AllocationSite::kTenure);
        // Currently we just need to deopt when we make a state transition to
        // tenure.
        return true;
      }
      site->set_pretenure_decision(AllocationSite::kMaybeTenure);
    } else {
      site->set_pretenure_decision(AllocationSite::kDontTenure);
    }
  }
  return false;
}

// Clear feedback calculation fields until the next gc.
inline void ResetPretenuringFeedback(Tagged<AllocationSite> site) {
  site->set_memento_found_count(0);
  site->set_memento_create_count(0);
}

inline bool DigestPretenuringFeedback(
    Isolate* isolate, Tagged<AllocationSite> site,
    bool new_space_capacity_was_above_pretenuring_threshold,
    size_t new_space_capacity) {
  bool deopt = false;
  int create_count = site->memento_create_count();
  int found_count = site->memento_found_count();
  bool minimum_mementos_created = create_count >= kMinMementoCount;
  double ratio =
      minimum_mementos_created || v8_flags.trace_pretenuring_statistics
          ? static_cast<double>(found_count) / create_count
          : 0.0;
  AllocationSite::PretenureDecision current_decision =
      site->pretenure_decision();

  if (minimum_mementos_created) {
    deopt = MakePretenureDecision(
        site, current_decision, ratio,
        new_space_capacity_was_above_pretenuring_threshold, new_space_capacity);
  }

  if (v8_flags.trace_pretenuring_statistics) {
    PrintIsolate(isolate,
                 "pretenuring: AllocationSite(%p): (created, found, ratio) "
                 "(%d, %d, %f) %s => %s\n",
                 reinterpret_cast<void*>(site.ptr()), create_count, found_count,
                 ratio, site->PretenureDecisionName(current_decision),
                 site->PretenureDecisionName(site->pretenure_decision()));
  }

  ResetPretenuringFeedback(site);
  return deopt;
}

bool PretenureAllocationSiteManually(Isolate* isolate,
                                     Tagged<AllocationSite> site) {
  AllocationSite::PretenureDecision current_decision =
      site->pretenure_decision();
  bool deopt = true;
  if (current_decision == AllocationSite::kUndecided ||
      current_decision == AllocationSite::kMaybeTenure) {
    site->set_deopt_dependent_code(true);
    site->set_pretenure_decision(AllocationSite::kTenure);
  } else {
    deopt = false;
  }
  if (v8_flags.trace_pretenuring_statistics) {
    PrintIsolate(isolate,
                 "pretenuring manually requested: AllocationSite(%p): "
                 "%s => %s\n",
                 reinterpret_cast<void*>(site.ptr()),
                 site->PretenureDecisionName(current_decision),
                 site->PretenureDecisionName(site->pretenure_decision()));
  }

  ResetPretenuringFeedback(site);
  return deopt;
}

}  // namespace

// static
int PretenuringHandler::GetMinMementoCountForTesting() {
  return kMinMementoCount;
}

void PretenuringHandler::MergeAllocationSitePretenuringFeedback(
    const PretenuringFeedbackMap& local_pretenuring_feedback) {
  PtrComprCageBase cage_base(heap_->isolate());
  Tagged<AllocationSite> site;
  for (auto& site_and_count : local_pretenuring_feedback) {
    site = site_and_count.first;
    MapWord map_word = site->map_word(cage_base, kRelaxedLoad);
    if (map_word.IsForwardingAddress()) {
      site = Cast<AllocationSite>(map_word.ToForwardingAddress(site));
    }

    // We have not validated the allocation site yet, since we have not
    // dereferenced the site during collecting information.
    // This is an inlined check of AllocationMemento::IsValid.
    if (!IsAllocationSite(site) || site->IsZombie()) continue;

    const int value = static_cast<int>(site_and_count.second);
    DCHECK_LT(0, value);
    if (site->IncrementMementoFoundCount(value) >= kMinMementoCount) {
      // For sites in the global map the count is accessed through the site.
      global_pretenuring_feedback_.insert(std::make_pair(site, 0));
    }
  }
}

void PretenuringHandler::RemoveAllocationSitePretenuringFeedback(
    Tagged<AllocationSite> site) {
  global_pretenuring_feedback_.erase(site);
}

void PretenuringHandler::ProcessPretenuringFeedback(
    size_t new_space_capacity_before_gc) {
  // The minimum new space capacity from which allocation sites can be
  // pretenured. A too small capacity means frequent GCs. Objects thus don't get
  // a chance to die before being promoted, which may lead to wrong pretenuring
  // decisions.
  static constexpr size_t kDefaultMinNewSpaceCapacityForPretenuring =
      8192 * KB * Heap::kPointerMultiplier;

  DCHECK(heap_->tracer()->IsInAtomicPause());

  if (!v8_flags.allocation_site_pretenuring) return;

  // TODO(333906585): Adjust capacity for sticky bits.
  const size_t max_capacity = v8_flags.sticky_mark_bits
                                  ? heap_->sticky_space()->Capacity()
                                  : heap_->new_space()->MaximumCapacity();
  const size_t min_new_space_capacity_for_pretenuring =
      std::min(max_capacity, kDefaultMinNewSpaceCapacityForPretenuring);

  bool trigger_deoptimization = false;
  int tenure_decisions = 0;
  int dont_tenure_decisions = 0;
  int allocation_mementos_found = 0;
  int allocation_sites = 0;
  int active_allocation_sites = 0;

  Tagged<AllocationSite> site;

  // Step 1: Digest feedback for recorded allocation sites.
  // This is the pretenuring trigger for allocation sites that are in maybe
  // tenure state. When we switched to a large enough new space size we
  // deoptimize the code that belongs to the allocation site and derive the
  // lifetime of the allocation site.
  bool new_space_was_above_pretenuring_threshold =
      new_space_capacity_before_gc >= min_new_space_capacity_for_pretenuring;

  for (auto& site_and_count : global_pretenuring_feedback_) {
    allocation_sites++;
    site = site_and_count.first;
    // Count is always access through the site.
    DCHECK_EQ(0, site_and_count.second);
    int found_count = site->memento_found_count();
    // An entry in the storage does not imply that the count is > 0 because
    // allocation sites might have been reset due to too many objects dying
    // in old space.
    if (found_count > 0) {
      DCHECK(IsAllocationSite(site));
      active_allocation_sites++;
      allocation_mementos_found += found_count;
      if (DigestPretenuringFeedback(heap_->isolate(), site,
                                    new_space_was_above_pretenuring_threshold,
                                    new_space_capacity_before_gc)) {
        trigger_deoptimization = true;
      }
      if (site->GetAllocationType() == AllocationType::kOld) {
        tenure_decisions++;
      } else {
        dont_tenure_decisions++;
      }
    }
  }

  // Step 2: Pretenure allocation sites for manual requests.
  if (allocation_sites_to_pretenure_) {
    while (!allocation_sites_to_pretenure_->empty()) {
      auto pretenure_site = allocation_sites_to_pretenure_->Pop();
      if (PretenureAllocationSiteManually(heap_->isolate(), pretenure_site)) {
        trigger_deoptimization = true;
      }
    }
    allocation_sites_to_pretenure_.reset();
  }

  // Step 3: Deopt maybe tenured allocation sites if necessary.
  // New space capacity was too low for pretenuring but is now above the
  // threshold. Maybe tenured allocation sites may be pretenured on the next GC.
  bool deopt_maybe_tenured = (heap_->NewSpaceTargetCapacity() >=
                              min_new_space_capacity_for_pretenuring) &&
                             !new_space_was_above_pretenuring_threshold;
  if (deopt_maybe_tenured) {
    heap_->ForeachAllocationSite(heap_->allocation_sites_list(),
                                 [&allocation_sites, &trigger_deoptimization](
                                     Tagged<AllocationSite> site) {
                                   DCHECK(IsAllocationSite(site));
                                   allocation_sites++;
                                   if (site->IsMaybeTenure()) {
                                     site->set_deopt_dependent_code(true);
                                     trigger_deoptimization = true;
                                   }
                                 });
  }

  if (trigger_deoptimization) {
    heap_->isolate()->stack_guard()->RequestDeoptMarkedAllocationSites();
  }

  if (v8_flags.trace_pretenuring_statistics &&
      (allocation_mementos_found > 0 || tenure_decisions > 0 ||
       dont_tenure_decisions > 0)) {
    PrintIsolate(
        heap_->isolate(),
        "pretenuring: threshold=%.2f deopt_maybe_tenured=%d visited_sites=%d "
        "active_sites=%d "
        "mementos=%d tenured=%d not_tenured=%d\n",
        GetPretenuringRatioThreshold(new_space_capacity_before_gc),
        deopt_maybe_tenured ? 1 : 0, allocation_sites, active_allocation_sites,
        allocation_mementos_found, tenure_decisions, dont_tenure_decisions);
  }

  global_pretenuring_feedback_.clear();
  global_pretenuring_feedback_.reserve(kInitialFeedbackCapacity);
}

void PretenuringHandler::PretenureAllocationSiteOnNextCollection(
    Tagged<AllocationSite> site) {
  if (!allocation_sites_to_pretenure_) {
    allocation_sites_to_pretenure_.reset(
        new GlobalHandleVector<AllocationSite>(heap_));
  }
  allocation_sites_to_pretenure_->Push(site);
}

void PretenuringHandler::reset() { allocation_sites_to_pretenure_.reset(); }

}  // namespace internal
}  // namespace v8
