// Copyright 2009-2010 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/profiler/heap-profiler.h"

#include <fstream>

#include "include/v8-profiler.h"
#include "src/api/api-inl.h"
#include "src/base/optional.h"
#include "src/debug/debug.h"
#include "src/heap/combined-heap.h"
#include "src/heap/heap-inl.h"
#include "src/heap/heap.h"
#include "src/objects/js-array-buffer-inl.h"
#include "src/profiler/allocation-tracker.h"
#include "src/profiler/heap-snapshot-generator-inl.h"
#include "src/profiler/sampling-heap-profiler.h"

namespace v8 {
namespace internal {

HeapProfiler::HeapProfiler(Heap* heap)
    : ids_(new HeapObjectsMap(heap)),
      names_(new StringsStorage()),
      is_tracking_object_moves_(false),
      is_taking_snapshot_(false) {}

HeapProfiler::~HeapProfiler() = default;

void HeapProfiler::DeleteAllSnapshots() {
  snapshots_.clear();
  MaybeClearStringsStorage();
}

void HeapProfiler::MaybeClearStringsStorage() {
  if (snapshots_.empty() && !sampling_heap_profiler_ && !allocation_tracker_ &&
      !is_taking_snapshot_) {
    names_.reset(new StringsStorage());
  }
}

void HeapProfiler::RemoveSnapshot(HeapSnapshot* snapshot) {
  snapshots_.erase(
      std::find_if(snapshots_.begin(), snapshots_.end(),
                   [&](const std::unique_ptr<HeapSnapshot>& entry) {
                     return entry.get() == snapshot;
                   }));
}

void HeapProfiler::AddBuildEmbedderGraphCallback(
    v8::HeapProfiler::BuildEmbedderGraphCallback callback, void* data) {
  build_embedder_graph_callbacks_.push_back({callback, data});
}

void HeapProfiler::RemoveBuildEmbedderGraphCallback(
    v8::HeapProfiler::BuildEmbedderGraphCallback callback, void* data) {
  auto it = std::find(build_embedder_graph_callbacks_.begin(),
                      build_embedder_graph_callbacks_.end(),
                      std::make_pair(callback, data));
  if (it != build_embedder_graph_callbacks_.end())
    build_embedder_graph_callbacks_.erase(it);
}

void HeapProfiler::BuildEmbedderGraph(Isolate* isolate,
                                      v8::EmbedderGraph* graph) {
  for (const auto& cb : build_embedder_graph_callbacks_) {
    cb.first(reinterpret_cast<v8::Isolate*>(isolate), graph, cb.second);
  }
}

void HeapProfiler::SetGetDetachednessCallback(
    v8::HeapProfiler::GetDetachednessCallback callback, void* data) {
  get_detachedness_callback_ = {callback, data};
}

v8::EmbedderGraph::Node::Detachedness HeapProfiler::GetDetachedness(
    const v8::Local<v8::Value> v8_value, uint16_t class_id) {
  DCHECK(HasGetDetachednessCallback());
  return get_detachedness_callback_.first(
      reinterpret_cast<v8::Isolate*>(heap()->isolate()), v8_value, class_id,
      get_detachedness_callback_.second);
}

HeapSnapshot* HeapProfiler::TakeSnapshot(
    const v8::HeapProfiler::HeapSnapshotOptions options) {
  is_taking_snapshot_ = true;
  HeapSnapshot* result =
      new HeapSnapshot(this, options.snapshot_mode, options.numerics_mode);

  // We need a stack marker here to allow deterministic passes over the stack.
  // The garbage collection and the filling of references in GenerateSnapshot
  // should scan the same part of the stack.
  heap()->stack().SetMarkerIfNeededAndCallback([this, &options, &result]() {
    base::Optional<CppClassNamesAsHeapObjectNameScope> use_cpp_class_name;
    if (result->expose_internals() && heap()->cpp_heap()) {
      use_cpp_class_name.emplace(heap()->cpp_heap());
    }

    HeapSnapshotGenerator generator(result, options.control,
                                    options.global_object_name_resolver, heap(),
                                    options.stack_state);
    if (!generator.GenerateSnapshot()) {
      delete result;
      result = nullptr;
    } else {
      snapshots_.emplace_back(result);
    }
  });
  ids_->RemoveDeadEntries();
  if (native_move_listener_) {
    native_move_listener_->StartListening();
  }
  is_tracking_object_moves_ = true;
  heap()->isolate()->UpdateLogObjectRelocation();
  is_taking_snapshot_ = false;

  return result;
}

class FileOutputStream : public v8::OutputStream {
 public:
  explicit FileOutputStream(const char* filename) : os_(filename) {}
  ~FileOutputStream() override { os_.close(); }

  WriteResult WriteAsciiChunk(char* data, int size) override {
    os_.write(data, size);
    return kContinue;
  }

  void EndOfStream() override { os_.close(); }

 private:
  std::ofstream os_;
};

// Precondition: only call this if you have just completed a full GC cycle.
void HeapProfiler::WriteSnapshotToDiskAfterGC() {
  // We need to set a stack marker for the stack walk performed by the
  // snapshot generator to work.
  heap()->stack().SetMarkerIfNeededAndCallback([this]() {
    int64_t time = V8::GetCurrentPlatform()->CurrentClockTimeMilliseconds();
    std::string filename = "v8-heap-" + std::to_string(time) + ".heapsnapshot";
    v8::HeapProfiler::HeapSnapshotOptions options;
    std::unique_ptr<HeapSnapshot> result(
        new HeapSnapshot(this, options.snapshot_mode, options.numerics_mode));
    HeapSnapshotGenerator generator(result.get(), options.control,
                                    options.global_object_name_resolver, heap(),
                                    options.stack_state);
    if (!generator.GenerateSnapshotAfterGC()) return;
    FileOutputStream stream(filename.c_str());
    HeapSnapshotJSONSerializer serializer(result.get());
    serializer.Serialize(&stream);
    PrintF("Wrote heap snapshot to %s.\n", filename.c_str());
  });
}

void HeapProfiler::TakeSnapshotToFile(
    const v8::HeapProfiler::HeapSnapshotOptions options, std::string filename) {
  HeapSnapshot* snapshot = TakeSnapshot(options);
  FileOutputStream stream(filename.c_str());
  HeapSnapshotJSONSerializer serializer(snapshot);
  serializer.Serialize(&stream);
}

bool HeapProfiler::StartSamplingHeapProfiler(
    uint64_t sample_interval, int stack_depth,
    v8::HeapProfiler::SamplingFlags flags) {
  if (sampling_heap_profiler_) return false;
  sampling_heap_profiler_.reset(new SamplingHeapProfiler(
      heap(), names_.get(), sample_interval, stack_depth, flags));
  return true;
}

void HeapProfiler::StopSamplingHeapProfiler() {
  sampling_heap_profiler_.reset();
  MaybeClearStringsStorage();
}

v8::AllocationProfile* HeapProfiler::GetAllocationProfile() {
  if (sampling_heap_profiler_) {
    return sampling_heap_profiler_->GetAllocationProfile();
  } else {
    return nullptr;
  }
}

void HeapProfiler::StartHeapObjectsTracking(bool track_allocations) {
  ids_->UpdateHeapObjectsMap();
  if (native_move_listener_) {
    native_move_listener_->StartListening();
  }
  is_tracking_object_moves_ = true;
  heap()->isolate()->UpdateLogObjectRelocation();
  DCHECK(!allocation_tracker_);
  if (track_allocations) {
    allocation_tracker_.reset(new AllocationTracker(ids_.get(), names_.get()));
    heap()->AddHeapObjectAllocationTracker(this);
  }
}

SnapshotObjectId HeapProfiler::PushHeapObjectsStats(OutputStream* stream,
                                                    int64_t* timestamp_us) {
  return ids_->PushHeapObjectsStats(stream, timestamp_us);
}

void HeapProfiler::StopHeapObjectsTracking() {
  ids_->StopHeapObjectsTracking();
  if (allocation_tracker_) {
    allocation_tracker_.reset();
    MaybeClearStringsStorage();
    heap()->RemoveHeapObjectAllocationTracker(this);
  }
}

int HeapProfiler::GetSnapshotsCount() const {
  return static_cast<int>(snapshots_.size());
}

bool HeapProfiler::IsTakingSnapshot() const { return is_taking_snapshot_; }

HeapSnapshot* HeapProfiler::GetSnapshot(int index) {
  return snapshots_.at(index).get();
}

SnapshotObjectId HeapProfiler::GetSnapshotObjectId(Handle<Object> obj) {
  if (!IsHeapObject(*obj)) return v8::HeapProfiler::kUnknownObjectId;
  return ids_->FindEntry(HeapObject::cast(*obj).address());
}

SnapshotObjectId HeapProfiler::GetSnapshotObjectId(NativeObject obj) {
  // Try to find id of regular native node first.
  SnapshotObjectId id = ids_->FindEntry(reinterpret_cast<Address>(obj));
  // In case no id has been found, check whether there exists an entry where the
  // native objects has been merged into a V8 entry.
  if (id == v8::HeapProfiler::kUnknownObjectId) {
    id = ids_->FindMergedNativeEntry(obj);
  }
  return id;
}

void HeapProfilerNativeMoveListener::ObjectMoveEvent(Address from, Address to,
                                                     int size) {
  profiler_->ObjectMoveEvent(from, to, size, /*is_native_object=*/true);
}

void HeapProfiler::ObjectMoveEvent(Address from, Address to, int size,
                                   bool is_native_object) {
  base::MutexGuard guard(&profiler_mutex_);
  bool known_object = ids_->MoveObject(from, to, size);
  if (!known_object && allocation_tracker_ && !is_native_object) {
    allocation_tracker_->address_to_trace()->MoveObject(from, to, size);
  }
}

void HeapProfiler::AllocationEvent(Address addr, int size) {
  DisallowGarbageCollection no_gc;
  if (allocation_tracker_) {
    allocation_tracker_->AllocationEvent(addr, size);
  }
}

void HeapProfiler::UpdateObjectSizeEvent(Address addr, int size) {
  ids_->UpdateObjectSize(addr, size);
}

Handle<HeapObject> HeapProfiler::FindHeapObjectById(SnapshotObjectId id) {
  CombinedHeapObjectIterator iterator(heap(),
                                      HeapObjectIterator::kFilterUnreachable);
  // Make sure that the object with the given id is still reachable.
  for (Tagged<HeapObject> obj = iterator.Next(); !obj.is_null();
       obj = iterator.Next()) {
    if (ids_->FindEntry(obj.address()) == id)
      return Handle<HeapObject>(obj, isolate());
  }
  return Handle<HeapObject>();
}

void HeapProfiler::ClearHeapObjectMap() {
  ids_.reset(new HeapObjectsMap(heap()));
  if (!allocation_tracker_) {
    if (native_move_listener_) {
      native_move_listener_->StopListening();
    }
    is_tracking_object_moves_ = false;
    heap()->isolate()->UpdateLogObjectRelocation();
  }
}

Heap* HeapProfiler::heap() const { return ids_->heap(); }

Isolate* HeapProfiler::isolate() const { return heap()->isolate(); }

void HeapProfiler::QueryObjects(Handle<Context> context,
                                v8::QueryObjectPredicate* predicate,
                                std::vector<v8::Global<v8::Object>>* objects) {
  // We need a stack marker here to allow deterministic passes over the stack.
  // The garbage collection and the two object heap iterators should scan the
  // same part of the stack.
  heap()->stack().SetMarkerIfNeededAndCallback([this, predicate, objects]() {
    {
      HandleScope handle_scope(isolate());
      std::vector<Handle<JSTypedArray>> on_heap_typed_arrays;
      CombinedHeapObjectIterator heap_iterator(
          heap(), HeapObjectIterator::kFilterUnreachable);
      for (Tagged<HeapObject> heap_obj = heap_iterator.Next();
           !heap_obj.is_null(); heap_obj = heap_iterator.Next()) {
        if (IsFeedbackVector(heap_obj)) {
          FeedbackVector::cast(heap_obj)->ClearSlots(isolate());
        } else if (IsJSTypedArray(heap_obj) &&
                   JSTypedArray::cast(heap_obj)->is_on_heap()) {
          // Cannot call typed_array->GetBuffer() here directly because it may
          // trigger GC. Defer that call by collecting the object in a vector.
          on_heap_typed_arrays.push_back(
              handle(JSTypedArray::cast(heap_obj), isolate()));
        }
      }
      for (auto& typed_array : on_heap_typed_arrays) {
        // Convert the on-heap typed array into off-heap typed array, so that
        // its ArrayBuffer becomes valid and can be returned in the result.
        typed_array->GetBuffer();
      }
    }
    // We should return accurate information about live objects, so we need to
    // collect all garbage first.
    heap()->CollectAllAvailableGarbage(GarbageCollectionReason::kHeapProfiler);
    CombinedHeapObjectIterator heap_iterator(
        heap(), HeapObjectIterator::kFilterUnreachable);
    PtrComprCageBase cage_base(isolate());
    for (Tagged<HeapObject> heap_obj = heap_iterator.Next();
         !heap_obj.is_null(); heap_obj = heap_iterator.Next()) {
      if (!IsJSObject(heap_obj, cage_base) ||
          IsJSExternalObject(heap_obj, cage_base))
        continue;
      v8::Local<v8::Object> v8_obj(
          Utils::ToLocal(handle(JSObject::cast(heap_obj), isolate())));
      if (!predicate->Filter(v8_obj)) continue;
      objects->emplace_back(reinterpret_cast<v8::Isolate*>(isolate()), v8_obj);
    }
  });
}

}  // namespace internal
}  // namespace v8
