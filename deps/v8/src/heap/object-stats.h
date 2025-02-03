// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_HEAP_OBJECT_STATS_H_
#define V8_HEAP_OBJECT_STATS_H_

#include "src/objects/code.h"
#include "src/objects/objects.h"

// These instance types do not exist for actual use but are merely introduced
// for object stats tracing. In contrast to InstructionStream and FixedArray sub
// types these types are not known to other counters outside of object stats
// tracing.
//
// Update LAST_VIRTUAL_TYPE below when changing this macro.
#define VIRTUAL_INSTANCE_TYPE_LIST(V)            \
  CODE_KIND_LIST(V)                              \
  V(ARRAY_BOILERPLATE_DESCRIPTION_ELEMENTS_TYPE) \
  V(ARRAY_DICTIONARY_ELEMENTS_TYPE)              \
  V(ARRAY_ELEMENTS_TYPE)                         \
  V(BOILERPLATE_ELEMENTS_TYPE)                   \
  V(BOILERPLATE_PROPERTY_ARRAY_TYPE)             \
  V(BOILERPLATE_PROPERTY_DICTIONARY_TYPE)        \
  V(BYTECODE_ARRAY_CONSTANT_POOL_TYPE)           \
  V(BYTECODE_ARRAY_HANDLER_TABLE_TYPE)           \
  V(COW_ARRAY_TYPE)                              \
  V(DEOPTIMIZATION_DATA_TYPE)                    \
  V(DEPENDENT_CODE_TYPE)                         \
  V(DEPRECATED_DESCRIPTOR_ARRAY_TYPE)            \
  V(EMBEDDED_OBJECT_TYPE)                        \
  V(ENUM_KEYS_CACHE_TYPE)                        \
  V(ENUM_INDICES_CACHE_TYPE)                     \
  V(FEEDBACK_VECTOR_ENTRY_TYPE)                  \
  V(FEEDBACK_VECTOR_HEADER_TYPE)                 \
  V(FEEDBACK_VECTOR_SLOT_CALL_TYPE)              \
  V(FEEDBACK_VECTOR_SLOT_CALL_UNUSED_TYPE)       \
  V(FEEDBACK_VECTOR_SLOT_ENUM_TYPE)              \
  V(FEEDBACK_VECTOR_SLOT_LOAD_TYPE)              \
  V(FEEDBACK_VECTOR_SLOT_LOAD_UNUSED_TYPE)       \
  V(FEEDBACK_VECTOR_SLOT_OTHER_TYPE)             \
  V(FEEDBACK_VECTOR_SLOT_STORE_TYPE)             \
  V(FEEDBACK_VECTOR_SLOT_STORE_UNUSED_TYPE)      \
  V(FUNCTION_TEMPLATE_INFO_ENTRIES_TYPE)         \
  V(GLOBAL_ELEMENTS_TYPE)                        \
  V(GLOBAL_PROPERTIES_TYPE)                      \
  V(JS_ARRAY_BOILERPLATE_TYPE)                   \
  V(JS_COLLECTION_TABLE_TYPE)                    \
  V(JS_OBJECT_BOILERPLATE_TYPE)                  \
  V(JS_UNCOMPILED_FUNCTION_TYPE)                 \
  V(MAP_ABANDONED_PROTOTYPE_TYPE)                \
  V(MAP_DEPRECATED_TYPE)                         \
  V(MAP_DICTIONARY_TYPE)                         \
  V(MAP_PROTOTYPE_DICTIONARY_TYPE)               \
  V(MAP_PROTOTYPE_TYPE)                          \
  V(MAP_STABLE_TYPE)                             \
  V(NUMBER_STRING_CACHE_TYPE)                    \
  V(OBJECT_DICTIONARY_ELEMENTS_TYPE)             \
  V(OBJECT_ELEMENTS_TYPE)                        \
  V(OBJECT_PROPERTY_ARRAY_TYPE)                  \
  V(OBJECT_PROPERTY_DICTIONARY_TYPE)             \
  V(OBJECT_TO_CODE_TYPE)                         \
  V(OPTIMIZED_CODE_LITERALS_TYPE)                \
  V(OTHER_CONTEXT_TYPE)                          \
  V(PROTOTYPE_DESCRIPTOR_ARRAY_TYPE)             \
  V(PROTOTYPE_PROPERTY_ARRAY_TYPE)               \
  V(PROTOTYPE_PROPERTY_DICTIONARY_TYPE)          \
  V(PROTOTYPE_USERS_TYPE)                        \
  V(REGEXP_MULTIPLE_CACHE_TYPE)                  \
  V(RELOC_INFO_TYPE)                             \
  V(RETAINED_MAPS_TYPE)                          \
  V(SCRIPT_LIST_TYPE)                            \
  V(SCRIPT_INFOS_TYPE)                           \
  V(SCRIPT_SOURCE_EXTERNAL_ONE_BYTE_TYPE)        \
  V(SCRIPT_SOURCE_EXTERNAL_TWO_BYTE_TYPE)        \
  V(SCRIPT_SOURCE_NON_EXTERNAL_ONE_BYTE_TYPE)    \
  V(SCRIPT_SOURCE_NON_EXTERNAL_TWO_BYTE_TYPE)    \
  V(SERIALIZED_OBJECTS_TYPE)                     \
  V(SINGLE_CHARACTER_STRING_TABLE_TYPE)          \
  V(STRING_SPLIT_CACHE_TYPE)                     \
  V(STRING_EXTERNAL_RESOURCE_ONE_BYTE_TYPE)      \
  V(STRING_EXTERNAL_RESOURCE_TWO_BYTE_TYPE)      \
  V(SOURCE_POSITION_TABLE_TYPE)                  \
  V(UNCOMPILED_SHARED_FUNCTION_INFO_TYPE)        \
  V(WEAK_NEW_SPACE_OBJECT_TO_CODE_TYPE)

namespace v8 {
namespace internal {

class Heap;
class Isolate;

class ObjectStats {
 public:
  static const size_t kNoOverAllocation = 0;

  explicit ObjectStats(Heap* heap) : heap_(heap) { ClearObjectStats(true); }

  // See description on VIRTUAL_INSTANCE_TYPE_LIST.
  enum VirtualInstanceType {
#define DEFINE_VIRTUAL_INSTANCE_TYPE(type) type,
    VIRTUAL_INSTANCE_TYPE_LIST(DEFINE_VIRTUAL_INSTANCE_TYPE)
#undef DEFINE_FIXED_ARRAY_SUB_INSTANCE_TYPE
        LAST_VIRTUAL_TYPE = WEAK_NEW_SPACE_OBJECT_TO_CODE_TYPE,
  };

  // ObjectStats are kept in two arrays, counts and sizes. Related stats are
  // stored in a contiguous linear buffer. Stats groups are stored one after
  // another.
  static constexpr int FIRST_VIRTUAL_TYPE = LAST_TYPE + 1;
  static constexpr int OBJECT_STATS_COUNT =
      FIRST_VIRTUAL_TYPE + LAST_VIRTUAL_TYPE + 1;

  void ClearObjectStats(bool clear_last_time_stats = false);

  void PrintJSON(const char* key);
  void Dump(std::stringstream& stream);

  void CheckpointObjectStats();
  void RecordObjectStats(InstanceType type, size_t size,
                         size_t over_allocated = kNoOverAllocation);
  void RecordVirtualObjectStats(VirtualInstanceType type, size_t size,
                                size_t over_allocated);

  size_t object_count_last_gc(size_t index) {
    return object_counts_last_time_[index];
  }

  size_t object_size_last_gc(size_t index) {
    return object_sizes_last_time_[index];
  }

  Isolate* isolate();
  Heap* heap() { return heap_; }

 private:
  static const int kFirstBucketShift = 5;  // <32
  static const int kLastBucketShift = 20;  // >=1M
  static const int kFirstBucket = 1 << kFirstBucketShift;
  static const int kLastBucket = 1 << kLastBucketShift;
  static const int kNumberOfBuckets = kLastBucketShift - kFirstBucketShift + 1;
  static const int kLastValueBucketIndex = kLastBucketShift - kFirstBucketShift;

  void PrintKeyAndId(const char* key, int gc_count);
  // The following functions are excluded from inline to reduce the overall
  // binary size of VB. On x64 this save around 80KB.
  V8_NOINLINE void PrintInstanceTypeJSON(const char* key, int gc_count,
                                         const char* name, int index);
  V8_NOINLINE void DumpInstanceTypeData(std::stringstream& stream,
                                        const char* name, int index);

  int HistogramIndexFromSize(size_t size);

  Heap* heap_;
  // Object counts and used memory by InstanceType.
  size_t object_counts_[OBJECT_STATS_COUNT];
  size_t object_counts_last_time_[OBJECT_STATS_COUNT];
  size_t object_sizes_[OBJECT_STATS_COUNT];
  size_t object_sizes_last_time_[OBJECT_STATS_COUNT];
  // Approximation of overallocated memory by InstanceType.
  size_t over_allocated_[OBJECT_STATS_COUNT];
  // Detailed histograms by InstanceType.
  size_t size_histogram_[OBJECT_STATS_COUNT][kNumberOfBuckets];
  size_t over_allocated_histogram_[OBJECT_STATS_COUNT][kNumberOfBuckets];

  size_t tagged_fields_count_;
  size_t embedder_fields_count_;
  size_t inobject_smi_fields_count_;
  size_t boxed_double_fields_count_;
  size_t string_data_count_;
  size_t raw_fields_count_;

  friend class ObjectStatsCollectorImpl;
};

class ObjectStatsCollector {
 public:
  ObjectStatsCollector(Heap* heap, ObjectStats* live, ObjectStats* dead)
      : heap_(heap), live_(live), dead_(dead) {
    DCHECK_NOT_NULL(heap_);
    DCHECK_NOT_NULL(live_);
    DCHECK_NOT_NULL(dead_);
  }

  // Collects type information of live and dead objects. Requires mark bits to
  // be present.
  void Collect();

 private:
  Heap* const heap_;
  ObjectStats* const live_;
  ObjectStats* const dead_;
};

}  // namespace internal
}  // namespace v8

#endif  // V8_HEAP_OBJECT_STATS_H_
