// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_HEAP_READ_ONLY_HEAP_H_
#define V8_HEAP_READ_ONLY_HEAP_H_

#include <memory>
#include <utility>
#include <vector>

#include "src/base/macros.h"
#include "src/objects/heap-object.h"
#include "src/objects/objects.h"
#include "src/roots/roots.h"
#include "src/sandbox/code-pointer-table.h"
#include "src/sandbox/js-dispatch-table.h"

namespace v8 {

class SharedMemoryStatistics;

namespace internal {

class Isolate;
class PageMetadata;
class ReadOnlyArtifacts;
class ReadOnlyPageMetadata;
class ReadOnlySpace;
class SharedReadOnlySpace;
class SnapshotData;

// This class transparently manages read-only space, roots and cache creation
// and destruction.
class ReadOnlyHeap final {
 public:
  static constexpr size_t kEntriesCount =
      static_cast<size_t>(RootIndex::kReadOnlyRootsCount);

  explicit ReadOnlyHeap(ReadOnlySpace* ro_space);
  ~ReadOnlyHeap();

  ReadOnlyHeap(const ReadOnlyHeap&) = delete;
  ReadOnlyHeap& operator=(const ReadOnlyHeap&) = delete;

  // If necessary creates read-only heap and initializes its artifacts (if the
  // deserializer is provided). Then attaches the read-only heap to the isolate.
  // If the deserializer is not provided, then the read-only heap will be only
  // finish initializing when initial heap object creation in the Isolate is
  // completed, which is signalled by calling OnCreateHeapObjectsComplete.
  // A lock will be held until that method is called.
  // TODO(v8:7464): Ideally we'd create this without needing a heap.
  static void SetUp(Isolate* isolate, SnapshotData* read_only_snapshot_data,
                    bool can_rehash);

  // Indicates that the isolate has been set up and all read-only space objects
  // have been created and will not be written to. This should only be called if
  // a deserializer was not previously provided to Setup. This releases the
  // ReadOnlyHeap creation lock.
  V8_EXPORT_PRIVATE void OnCreateHeapObjectsComplete(Isolate* isolate);
  // Indicates that all objects reachable by the read only roots table have been
  // set up.
  void OnCreateRootsComplete(Isolate* isolate);
  // If the read-only heap is shared, then populate |statistics| with its stats,
  // otherwise the read-only heap stats are set to 0.
  static void PopulateReadOnlySpaceStatistics(
      SharedMemoryStatistics* statistics);

  // Returns whether the address is within the read-only space.
  V8_EXPORT_PRIVATE static bool Contains(Address address);
  // Returns whether the object resides in the read-only space.
  V8_EXPORT_PRIVATE static bool Contains(Tagged<HeapObject> object);
  V8_EXPORT_PRIVATE static bool SandboxSafeContains(Tagged<HeapObject> object);
  // Returns the current isolates roots table during initialization as opposed
  // to the shared one in case the latter is not initialized yet.
  V8_EXPORT_PRIVATE inline static ReadOnlyRoots EarlyGetReadOnlyRoots(
      Tagged<HeapObject> object);

  ReadOnlySpace* read_only_space() const { return read_only_space_; }

#ifdef V8_ENABLE_SANDBOX
  CodePointerTable::Space* code_pointer_space() { return &code_pointer_space_; }
#endif  // V8_ENABLE_SANDBOX
#ifdef V8_ENABLE_LEAPTIERING
  JSDispatchTable::Space* js_dispatch_table_space() {
    return &js_dispatch_table_space_;
  }
#endif  // V8_ENABLE_LEAPTIERING

  void InitializeIsolateRoots(Isolate* isolate);
  void InitializeFromIsolateRoots(Isolate* isolate);

  bool roots_init_complete() const { return roots_init_complete_; }

 protected:
  friend class ReadOnlyArtifacts;

  // Creates a new read-only heap and attaches it to the provided isolate. Only
  // used the first time when creating a ReadOnlyHeap for sharing.
  static void CreateInitialHeapForBootstrapping(Isolate* isolate,
                                                ReadOnlyArtifacts* artifacts);
  // Runs the read-only deserializer and calls InitFromIsolate to complete
  // read-only heap initialization.
  void DeserializeIntoIsolate(Isolate* isolate,
                              SnapshotData* read_only_snapshot_data,
                              bool can_rehash);
  // Initializes read-only heap from an already set-up isolate, copying
  // read-only roots from the isolate. This then seals the space off from
  // further writes, marks it as read-only and detaches it from the heap
  // (unless sharing is disabled).
  void InitFromIsolate(Isolate* isolate);

  bool roots_init_complete_ = false;
  ReadOnlySpace* read_only_space_ = nullptr;

#ifdef V8_ENABLE_SANDBOX
  // The read-only heap has its own code pointer space. Entries in this space
  // are never deallocated.
  CodePointerTable::Space code_pointer_space_;
#endif  // V8_ENABLE_SANDBOX
#ifdef V8_ENABLE_LEAPTIERING
  JSDispatchTable::Space js_dispatch_table_space_;
#endif  // V8_ENABLE_LEAPTIERING

 private:
  friend ReadOnlyRoots GetReadOnlyRoots();
  Address read_only_roots_[kEntriesCount];
};

enum class SkipFreeSpaceOrFiller {
  kYes,
  kNo,
};

// This class enables iterating over all read-only heap objects on a
// ReadOnlyPage.
class V8_EXPORT_PRIVATE ReadOnlyPageObjectIterator final {
 public:
  explicit ReadOnlyPageObjectIterator(
      const ReadOnlyPageMetadata* page,
      SkipFreeSpaceOrFiller skip_free_space_or_filler =
          SkipFreeSpaceOrFiller::kYes);
  ReadOnlyPageObjectIterator(const ReadOnlyPageMetadata* page,
                             Address current_addr,
                             SkipFreeSpaceOrFiller skip_free_space_or_filler =
                                 SkipFreeSpaceOrFiller::kYes);

  Tagged<HeapObject> Next();

 private:
  void Reset(const ReadOnlyPageMetadata* page);

  const ReadOnlyPageMetadata* page_;
  Address current_addr_;
  const SkipFreeSpaceOrFiller skip_free_space_or_filler_;

  friend class ReadOnlyHeapObjectIterator;
};

// This class enables iterating over all read-only heap objects in the
// ReadOnlyHeap/ReadOnlySpace.
class V8_EXPORT_PRIVATE ReadOnlyHeapObjectIterator final {
 public:
  explicit ReadOnlyHeapObjectIterator(const ReadOnlyHeap* ro_heap);
  explicit ReadOnlyHeapObjectIterator(const ReadOnlySpace* ro_space);

  Tagged<HeapObject> Next();

 private:
  const ReadOnlySpace* const ro_space_;
  std::vector<ReadOnlyPageMetadata*>::const_iterator current_page_;
  ReadOnlyPageObjectIterator page_iterator_;
};

}  // namespace internal
}  // namespace v8

#endif  // V8_HEAP_READ_ONLY_HEAP_H_
