#ifndef SRC_NODE_PERF_H_
#define SRC_NODE_PERF_H_

#if defined(NODE_WANT_INTERNALS) && NODE_WANT_INTERNALS

#include "base_object-inl.h"
#include "histogram.h"
#include "node.h"
#include "node_internals.h"
#include "node_perf_common.h"

#include "v8.h"
#include "uv.h"

#include <string>

namespace node {

class Environment;
class ExternalReferenceRegistry;

namespace performance {

inline const char* GetPerformanceMilestoneName(
    PerformanceMilestone milestone) {
  switch (milestone) {
#define V(name, label) case NODE_PERFORMANCE_MILESTONE_##name: return label;
  NODE_PERFORMANCE_MILESTONES(V)
#undef V
    default:
      UNREACHABLE();
  }
}

inline PerformanceMilestone ToPerformanceMilestoneEnum(const char* str) {
#define V(name, label)                                                        \
  if (strcmp(str, label) == 0) return NODE_PERFORMANCE_MILESTONE_##name;
  NODE_PERFORMANCE_MILESTONES(V)
#undef V
  return NODE_PERFORMANCE_MILESTONE_INVALID;
}

inline const char* GetPerformanceEntryTypeName(
    PerformanceEntryType type) {
  switch (type) {
#define V(name, label) case NODE_PERFORMANCE_ENTRY_TYPE_##name: return label;
  NODE_PERFORMANCE_ENTRY_TYPES(V)
#undef V
    default:
      UNREACHABLE();
  }
}

inline PerformanceEntryType ToPerformanceEntryTypeEnum(
    const char* type) {
#define V(name, label)                                                        \
  if (strcmp(type, label) == 0) return NODE_PERFORMANCE_ENTRY_TYPE_##name;
  NODE_PERFORMANCE_ENTRY_TYPES(V)
#undef V
  return NODE_PERFORMANCE_ENTRY_TYPE_INVALID;
}

enum PerformanceGCKind {
  NODE_PERFORMANCE_GC_MAJOR = v8::GCType::kGCTypeMarkSweepCompact,
  NODE_PERFORMANCE_GC_MINOR = v8::GCType::kGCTypeScavenge,
  NODE_PERFORMANCE_GC_INCREMENTAL = v8::GCType::kGCTypeIncrementalMarking,
  NODE_PERFORMANCE_GC_WEAKCB = v8::GCType::kGCTypeProcessWeakCallbacks
};

enum PerformanceGCFlags {
  NODE_PERFORMANCE_GC_FLAGS_NO =
    v8::GCCallbackFlags::kNoGCCallbackFlags,
  NODE_PERFORMANCE_GC_FLAGS_CONSTRUCT_RETAINED =
    v8::GCCallbackFlags::kGCCallbackFlagConstructRetainedObjectInfos,
  NODE_PERFORMANCE_GC_FLAGS_FORCED =
    v8::GCCallbackFlags::kGCCallbackFlagForced,
  NODE_PERFORMANCE_GC_FLAGS_SYNCHRONOUS_PHANTOM_PROCESSING =
    v8::GCCallbackFlags::kGCCallbackFlagSynchronousPhantomCallbackProcessing,
  NODE_PERFORMANCE_GC_FLAGS_ALL_AVAILABLE_GARBAGE =
    v8::GCCallbackFlags::kGCCallbackFlagCollectAllAvailableGarbage,
  NODE_PERFORMANCE_GC_FLAGS_ALL_EXTERNAL_MEMORY =
    v8::GCCallbackFlags::kGCCallbackFlagCollectAllExternalMemory,
  NODE_PERFORMANCE_GC_FLAGS_SCHEDULE_IDLE =
    v8::GCCallbackFlags::kGCCallbackScheduleIdleGarbageCollection
};

template <typename Traits>
struct PerformanceEntry {
  using Details = typename Traits::Details;
  std::string name;
  double start_time;
  double duration;
  Details details;

  PerformanceEntry(
    const std::string& name_,
    double start_time_,
    double duration_,
    const Details& details_)
    : name(name_),
      start_time(start_time_),
      duration(duration_),
      details(details_) {}

  static v8::MaybeLocal<v8::Object> GetDetails(
      Environment* env,
      const PerformanceEntry<Traits>& entry) {
    return Traits::GetDetails(env, entry);
  }

  void Notify(Environment* env) {
    v8::HandleScope handle_scope(env->isolate());
    v8::Context::Scope scope(env->context());
    AliasedUint32Array& observers = env->performance_state()->observers;
    if (env->performance_entry_callback().IsEmpty() ||
        !observers[Traits::kType]) {
      return;
    }

    v8::Local<v8::Object> detail;
    if (!Traits::GetDetails(env, *this).ToLocal(&detail)) {
      // TODO(@jasnell): Handle the error here
      return;
    }

    v8::Local<v8::Value> argv[] = {
        OneByteString(env->isolate(), name),
        OneByteString(env->isolate(),
                      GetPerformanceEntryTypeName(Traits::kType)),
        v8::Number::New(env->isolate(), start_time),
        v8::Number::New(env->isolate(), duration),
        detail};

    node::MakeSyncCallback(
        env->isolate(),
        env->context()->Global(),
        env->performance_entry_callback(),
        arraysize(argv),
        argv);
  }
};

struct GCPerformanceEntryTraits {
  static constexpr PerformanceEntryType kType =
      NODE_PERFORMANCE_ENTRY_TYPE_GC;
  struct Details {
    PerformanceGCKind kind;
    PerformanceGCFlags flags;

    Details(PerformanceGCKind kind_, PerformanceGCFlags flags_)
        : kind(kind_), flags(flags_) {}
  };

  static v8::MaybeLocal<v8::Object> GetDetails(
      Environment* env,
      const PerformanceEntry<GCPerformanceEntryTraits>& entry);
};

using GCPerformanceEntry = PerformanceEntry<GCPerformanceEntryTraits>;

}  // namespace performance
}  // namespace node

#endif  // defined(NODE_WANT_INTERNALS) && NODE_WANT_INTERNALS

#endif  // SRC_NODE_PERF_H_
