#ifndef SRC_NODE_PERF_COMMON_H_
#define SRC_NODE_PERF_COMMON_H_

#if defined(NODE_WANT_INTERNALS) && NODE_WANT_INTERNALS

#include "aliased_buffer.h"
#include "node.h"
#include "uv.h"
#include "v8.h"

#include <algorithm>
#include <iostream>
#include <map>
#include <string>

namespace node {
namespace performance {

#define PERFORMANCE_NOW() uv_hrtime()

// These occur before the environment is created. Cache them
// here and add them to the milestones when the env is init'd.
extern const uint64_t performance_process_start;
extern const double performance_process_start_timestamp;
extern uint64_t performance_v8_start;

#define NODE_PERFORMANCE_MILESTONES(V)                                         \
  V(TIME_ORIGIN_TIMESTAMP, "timeOriginTimestamp")                              \
  V(TIME_ORIGIN, "timeOrigin")                                                 \
  V(ENVIRONMENT, "environment")                                                \
  V(NODE_START, "nodeStart")                                                   \
  V(V8_START, "v8Start")                                                       \
  V(LOOP_START, "loopStart")                                                   \
  V(LOOP_EXIT, "loopExit")                                                     \
  V(BOOTSTRAP_COMPLETE, "bootstrapComplete")

#define NODE_PERFORMANCE_ENTRY_TYPES(V)                                       \
  V(GC, "gc")                                                                 \
  V(HTTP, "http")                                                             \
  V(HTTP2, "http2")                                                           \
  V(NET, "net")                                                               \
  V(DNS, "dns")

enum PerformanceMilestone {
#define V(name, _) NODE_PERFORMANCE_MILESTONE_##name,
  NODE_PERFORMANCE_MILESTONES(V)
#undef V
  NODE_PERFORMANCE_MILESTONE_INVALID
};

enum PerformanceEntryType {
#define V(name, _) NODE_PERFORMANCE_ENTRY_TYPE_##name,
  NODE_PERFORMANCE_ENTRY_TYPES(V)
#undef V
  NODE_PERFORMANCE_ENTRY_TYPE_INVALID
};

class PerformanceState {
 public:
  struct SerializeInfo {
    AliasedBufferIndex root;
    AliasedBufferIndex milestones;
    AliasedBufferIndex observers;
  };

  explicit PerformanceState(v8::Isolate* isolate,
                            uint64_t time_origin,
                            double time_origin_timestamp,
                            const SerializeInfo* info);
  SerializeInfo Serialize(v8::Local<v8::Context> context,
                          v8::SnapshotCreator* creator);
  void Deserialize(v8::Local<v8::Context> context,
                   uint64_t time_origin,
                   double time_origin_timestamp);
  friend std::ostream& operator<<(std::ostream& o, const SerializeInfo& i);

  AliasedUint8Array root;
  AliasedFloat64Array milestones;
  AliasedUint32Array observers;

  uint64_t performance_last_gc_start_mark = 0;
  uint16_t current_gc_type = 0;

  void Mark(enum PerformanceMilestone milestone,
            uint64_t ts = PERFORMANCE_NOW());

 private:
  void Initialize(uint64_t time_origin, double time_origin_timestamp);
  void ResetMilestones();
  struct performance_state_internal {
    // doubles first so that they are always sizeof(double)-aligned
    double milestones[NODE_PERFORMANCE_MILESTONE_INVALID];
    uint32_t observers[NODE_PERFORMANCE_ENTRY_TYPE_INVALID];
  };
};

}  // namespace performance
}  // namespace node

#endif  // defined(NODE_WANT_INTERNALS) && NODE_WANT_INTERNALS

#endif  // SRC_NODE_PERF_COMMON_H_
