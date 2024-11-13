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

#ifndef ABSL_STRINGS_INTERNAL_CORDZ_INFO_H_
#define ABSL_STRINGS_INTERNAL_CORDZ_INFO_H_

#include <atomic>
#include <cstdint>
#include <functional>

#include "absl/base/config.h"
#include "absl/base/internal/raw_logging.h"
#include "absl/base/internal/spinlock.h"
#include "absl/base/thread_annotations.h"
#include "absl/strings/internal/cord_internal.h"
#include "absl/strings/internal/cordz_functions.h"
#include "absl/strings/internal/cordz_handle.h"
#include "absl/strings/internal/cordz_statistics.h"
#include "absl/strings/internal/cordz_update_tracker.h"
#include "absl/synchronization/mutex.h"
#include "absl/types/span.h"

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace cord_internal {

// CordzInfo tracks a profiled Cord. Each of these objects can be in two places.
// If a Cord is alive, the CordzInfo will be in the global_cordz_infos map, and
// can also be retrieved via the linked list starting with
// global_cordz_infos_head and continued via the cordz_info_next() method. When
// a Cord has reached the end of its lifespan, the CordzInfo object will be
// migrated out of the global_cordz_infos list and the global_cordz_infos_map,
// and will either be deleted or appended to the global_delete_queue. If it is
// placed on the global_delete_queue, the CordzInfo object will be cleaned in
// the destructor of a CordzSampleToken object.
class ABSL_LOCKABLE CordzInfo : public CordzHandle {
 public:
  using MethodIdentifier = CordzUpdateTracker::MethodIdentifier;

  // TrackCord creates a CordzInfo instance which tracks important metrics of
  // a sampled cord, and stores the created CordzInfo instance into `cord'. All
  // CordzInfo instances are placed in a global list which is used to discover
  // and snapshot all actively tracked cords. Callers are responsible for
  // calling UntrackCord() before the tracked Cord instance is deleted, or to
  // stop tracking the sampled Cord. Callers are also responsible for guarding
  // changes to the 'tree' value of a Cord (InlineData.tree) through the Lock()
  // and Unlock() calls. Any change resulting in a new tree value for the cord
  // requires a call to SetCordRep() before the old tree has been unreffed
  // and/or deleted. `method` identifies the Cord public API method initiating
  // the cord to be sampled.
  // Requires `cord` to hold a tree, and `cord.cordz_info()` to be null.
  static void TrackCord(InlineData& cord, MethodIdentifier method,
                        int64_t sampling_stride);

  // Identical to TrackCord(), except that this function fills the
  // `parent_stack` and `parent_method` properties of the returned CordzInfo
  // instance from the provided `src` instance if `src` is sampled.
  // This function should be used for sampling 'copy constructed' and 'copy
  // assigned' cords. This function allows 'cord` to be already sampled, in
  // which case the CordzInfo will be newly created from `src`.
  static void TrackCord(InlineData& cord, const InlineData& src,
                        MethodIdentifier method);

  // Maybe sample the cord identified by 'cord' for method 'method'.
  // Uses `cordz_should_profile` to randomly pick cords to be sampled, and if
  // so, invokes `TrackCord` to start sampling `cord`.
  static void MaybeTrackCord(InlineData& cord, MethodIdentifier method);

  // Maybe sample the cord identified by 'cord' for method 'method'.
  // `src` identifies a 'parent' cord which is assigned to `cord`, typically the
  // input cord for a copy constructor, or an assign method such as `operator=`
  // `cord` will be sampled if (and only if) `src` is sampled.
  // If `cord` is currently being sampled and `src` is not being sampled, then
  // this function will stop sampling the cord and reset the cord's cordz_info.
  //
  // Previously this function defined that `cord` will be sampled if either
  // `src` is sampled, or if `cord` is randomly picked for sampling. However,
  // this can cause issues, as there may be paths where some cord is assigned an
  // indirect copy of it's own value. As such a 'string of copies' would then
  // remain sampled (`src.is_profiled`), then assigning such a cord back to
  // 'itself' creates a cycle where the cord will converge to 'always sampled`.
  //
  // For example:
  //
  //   Cord x;
  //   for (...) {
  //     // Copy ctor --> y.is_profiled := x.is_profiled | random(...)
  //     Cord y = x;
  //     ...
  //     // Assign x = y --> x.is_profiled = y.is_profiled | random(...)
  //     //              ==> x.is_profiled |= random(...)
  //     //              ==> x converges to 'always profiled'
  //     x = y;
  //   }
  static void MaybeTrackCord(InlineData& cord, const InlineData& src,
                             MethodIdentifier method);

  // Stops tracking changes for a sampled cord, and deletes the provided info.
  // This function must be called before the sampled cord instance is deleted,
  // and before the root cordrep of the sampled cord is unreffed.
  // This function may extend the lifetime of the cordrep in cases where the
  // CordInfo instance is being held by a concurrent collection thread.
  void Untrack();

  // Invokes UntrackCord() on `info` if `info` is not null.
  static void MaybeUntrackCord(CordzInfo* info);

  CordzInfo() = delete;
  CordzInfo(const CordzInfo&) = delete;
  CordzInfo& operator=(const CordzInfo&) = delete;

  // Retrieves the oldest existing CordzInfo.
  static CordzInfo* Head(const CordzSnapshot& snapshot)
      ABSL_NO_THREAD_SAFETY_ANALYSIS;

  // Retrieves the next oldest existing CordzInfo older than 'this' instance.
  CordzInfo* Next(const CordzSnapshot& snapshot) const
      ABSL_NO_THREAD_SAFETY_ANALYSIS;

  // Locks this instance for the update identified by `method`.
  // Increases the count for `method` in `update_tracker`.
  void Lock(MethodIdentifier method) ABSL_EXCLUSIVE_LOCK_FUNCTION(mutex_);

  // Unlocks this instance. If the contained `rep` has been set to null
  // indicating the Cord has been cleared or is otherwise no longer sampled,
  // then this method will delete this CordzInfo instance.
  void Unlock() ABSL_UNLOCK_FUNCTION(mutex_);

  // Asserts that this CordzInfo instance is locked.
  void AssertHeld() ABSL_ASSERT_EXCLUSIVE_LOCK(mutex_);

  // Updates the `rep` property of this instance. This methods is invoked by
  // Cord logic each time the root node of a sampled Cord changes, and before
  // the old root reference count is deleted. This guarantees that collection
  // code can always safely take a reference on the tracked cord.
  // Requires a lock to be held through the `Lock()` method.
  // TODO(b/117940323): annotate with ABSL_EXCLUSIVE_LOCKS_REQUIRED once all
  // Cord code is in a state where this can be proven true by the compiler.
  void SetCordRep(CordRep* rep);

  // Returns the current `rep` property of this instance with a reference
  // added, or null if this instance represents a cord that has since been
  // deleted or untracked.
  CordRep* RefCordRep() const ABSL_LOCKS_EXCLUDED(mutex_);

  // Returns the current value of `rep_` for testing purposes only.
  CordRep* GetCordRepForTesting() const ABSL_NO_THREAD_SAFETY_ANALYSIS {
    return rep_;
  }

  // Sets the current value of `rep_` for testing purposes only.
  void SetCordRepForTesting(CordRep* rep) ABSL_NO_THREAD_SAFETY_ANALYSIS {
    rep_ = rep;
  }

  // Returns the stack trace for where the cord was first sampled. Cords are
  // potentially sampled when they promote from an inlined cord to a tree or
  // ring representation, which is not necessarily the location where the cord
  // was first created. Some cords are created as inlined cords, and only as
  // data is added do they become a non-inlined cord. However, typically the
  // location represents reasonably well where the cord is 'created'.
  absl::Span<void* const> GetStack() const;

  // Returns the stack trace for a sampled cord's 'parent stack trace'. This
  // value may be set if the cord is sampled (promoted) after being created
  // from, or being assigned the value of an existing (sampled) cord.
  absl::Span<void* const> GetParentStack() const;

  // Retrieves the CordzStatistics associated with this Cord. The statistics
  // are only updated when a Cord goes through a mutation, such as an Append
  // or RemovePrefix.
  CordzStatistics GetCordzStatistics() const;

  int64_t sampling_stride() const { return sampling_stride_; }

 private:
  using SpinLock = absl::base_internal::SpinLock;
  using SpinLockHolder = ::absl::base_internal::SpinLockHolder;

  // Global cordz info list. CordzInfo stores a pointer to the global list
  // instance to harden against ODR violations.
  struct List {
    constexpr explicit List(absl::ConstInitType)
        : mutex(absl::kConstInit,
                absl::base_internal::SCHEDULE_COOPERATIVE_AND_KERNEL) {}

    SpinLock mutex;
    std::atomic<CordzInfo*> head ABSL_GUARDED_BY(mutex){nullptr};
  };

  static constexpr size_t kMaxStackDepth = 64;

  explicit CordzInfo(CordRep* rep, const CordzInfo* src,
                     MethodIdentifier method, int64_t weight);
  ~CordzInfo() override;

  // Sets `rep_` without holding a lock.
  void UnsafeSetCordRep(CordRep* rep) ABSL_NO_THREAD_SAFETY_ANALYSIS;

  void Track();

  // Returns the parent method from `src`, which is either `parent_method_` or
  // `method_` depending on `parent_method_` being kUnknown.
  // Returns kUnknown if `src` is null.
  static MethodIdentifier GetParentMethod(const CordzInfo* src);

  // Fills the provided stack from `src`, copying either `parent_stack_` or
  // `stack_` depending on `parent_stack_` being empty, returning the size of
  // the parent stack.
  // Returns 0 if `src` is null.
  static size_t FillParentStack(const CordzInfo* src, void** stack);

  void ODRCheck() const {
#ifndef NDEBUG
    ABSL_RAW_CHECK(list_ == &global_list_, "ODR violation in Cord");
#endif
  }

  // Non-inlined implementation of `MaybeTrackCord`, which is executed if
  // either `src` is sampled or `cord` is sampled, and either untracks or
  // tracks `cord` as documented per `MaybeTrackCord`.
  static void MaybeTrackCordImpl(InlineData& cord, const InlineData& src,
                                 MethodIdentifier method);

  ABSL_CONST_INIT static List global_list_;
  List* const list_ = &global_list_;

  // ci_prev_ and ci_next_ require the global list mutex to be held.
  // Unfortunately we can't use thread annotations such that the thread safety
  // analysis understands that list_ and global_list_ are one and the same.
  std::atomic<CordzInfo*> ci_prev_{nullptr};
  std::atomic<CordzInfo*> ci_next_{nullptr};

  mutable absl::Mutex mutex_;
  CordRep* rep_ ABSL_GUARDED_BY(mutex_);

  void* stack_[kMaxStackDepth];
  void* parent_stack_[kMaxStackDepth];
  const size_t stack_depth_;
  const size_t parent_stack_depth_;
  const MethodIdentifier method_;
  const MethodIdentifier parent_method_;
  CordzUpdateTracker update_tracker_;
  const absl::Time create_time_;
  const int64_t sampling_stride_;
};

inline ABSL_ATTRIBUTE_ALWAYS_INLINE void CordzInfo::MaybeTrackCord(
    InlineData& cord, MethodIdentifier method) {
  auto stride = cordz_should_profile();
  if (ABSL_PREDICT_FALSE(stride > 0)) {
    TrackCord(cord, method, stride);
  }
}

inline ABSL_ATTRIBUTE_ALWAYS_INLINE void CordzInfo::MaybeTrackCord(
    InlineData& cord, const InlineData& src, MethodIdentifier method) {
  if (ABSL_PREDICT_FALSE(InlineData::is_either_profiled(cord, src))) {
    MaybeTrackCordImpl(cord, src, method);
  }
}

inline ABSL_ATTRIBUTE_ALWAYS_INLINE void CordzInfo::MaybeUntrackCord(
    CordzInfo* info) {
  if (ABSL_PREDICT_FALSE(info)) {
    info->Untrack();
  }
}

inline void CordzInfo::AssertHeld() ABSL_ASSERT_EXCLUSIVE_LOCK(mutex_) {
#ifndef NDEBUG
  mutex_.AssertHeld();
#endif
}

inline void CordzInfo::SetCordRep(CordRep* rep) {
  AssertHeld();
  rep_ = rep;
}

inline void CordzInfo::UnsafeSetCordRep(CordRep* rep) { rep_ = rep; }

inline CordRep* CordzInfo::RefCordRep() const ABSL_LOCKS_EXCLUDED(mutex_) {
  MutexLock lock(&mutex_);
  return rep_ ? CordRep::Ref(rep_) : nullptr;
}

}  // namespace cord_internal
ABSL_NAMESPACE_END
}  // namespace absl

#endif  // ABSL_STRINGS_INTERNAL_CORDZ_INFO_H_
