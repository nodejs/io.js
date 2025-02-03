// Copyright 2013 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_PROFILER_HEAP_SNAPSHOT_GENERATOR_H_
#define V8_PROFILER_HEAP_SNAPSHOT_GENERATOR_H_

#include <deque>
#include <memory>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "include/v8-profiler.h"
#include "src/base/platform/time.h"
#include "src/execution/isolate.h"
#include "src/objects/fixed-array.h"
#include "src/objects/hash-table.h"
#include "src/objects/heap-object.h"
#include "src/objects/js-objects.h"
#include "src/objects/literal-objects.h"
#include "src/objects/objects.h"
#include "src/objects/string.h"
#include "src/objects/visitors.h"
#include "src/profiler/strings-storage.h"
#include "src/strings/string-hasher.h"

#ifdef V8_ENABLE_HEAP_SNAPSHOT_VERIFY
#include "src/heap/reference-summarizer.h"
#endif

namespace v8::internal {

class AllocationTraceNode;
class HeapEntry;
class HeapProfiler;
class HeapSnapshot;
class HeapSnapshotGenerator;
class IsolateSafepointScope;
class JSArrayBuffer;
class JSCollection;
class JSGeneratorObject;
class JSGlobalObject;
class JSGlobalProxy;
class JSPromise;
class JSWeakCollection;

struct EntrySourceLocation {
  EntrySourceLocation(int entry_index, int scriptId, int line, int col)
      : entry_index(entry_index), scriptId(scriptId), line(line), col(col) {}

  const int entry_index;
  const int scriptId;
  const int line;
  const int col;
};

class HeapGraphEdge {
 public:
  enum Type {
    kContextVariable = v8::HeapGraphEdge::kContextVariable,
    kElement = v8::HeapGraphEdge::kElement,
    kProperty = v8::HeapGraphEdge::kProperty,
    kInternal = v8::HeapGraphEdge::kInternal,
    kHidden = v8::HeapGraphEdge::kHidden,
    kShortcut = v8::HeapGraphEdge::kShortcut,
    kWeak = v8::HeapGraphEdge::kWeak
  };

  HeapGraphEdge(Type type, const char* name, HeapEntry* from, HeapEntry* to);
  HeapGraphEdge(Type type, int index, HeapEntry* from, HeapEntry* to);

  Type type() const { return TypeField::decode(bit_field_); }
  int index() const {
    DCHECK(type() == kElement || type() == kHidden);
    return index_;
  }
  const char* name() const {
    DCHECK(type() == kContextVariable || type() == kProperty ||
           type() == kInternal || type() == kShortcut || type() == kWeak);
    return name_;
  }
  V8_INLINE HeapEntry* from() const;
  HeapEntry* to() const { return to_entry_; }

  V8_INLINE Isolate* isolate() const;

 private:
  V8_INLINE HeapSnapshot* snapshot() const;
  int from_index() const { return FromIndexField::decode(bit_field_); }

  using TypeField = base::BitField<Type, 0, 3>;
  using FromIndexField = base::BitField<int, 3, 29>;
  uint32_t bit_field_;
  HeapEntry* to_entry_;
  union {
    int index_;
    const char* name_;
  };
};


// HeapEntry instances represent an entity from the heap (or a special
// virtual node, e.g. root).
class HeapEntry {
 public:
  enum Type {
    kHidden = v8::HeapGraphNode::kHidden,
    kArray = v8::HeapGraphNode::kArray,
    kString = v8::HeapGraphNode::kString,
    kObject = v8::HeapGraphNode::kObject,
    kCode = v8::HeapGraphNode::kCode,
    kClosure = v8::HeapGraphNode::kClosure,
    kRegExp = v8::HeapGraphNode::kRegExp,
    kHeapNumber = v8::HeapGraphNode::kHeapNumber,
    kNative = v8::HeapGraphNode::kNative,
    kSynthetic = v8::HeapGraphNode::kSynthetic,
    kConsString = v8::HeapGraphNode::kConsString,
    kSlicedString = v8::HeapGraphNode::kSlicedString,
    kSymbol = v8::HeapGraphNode::kSymbol,
    kBigInt = v8::HeapGraphNode::kBigInt,
    kObjectShape = v8::HeapGraphNode::kObjectShape,
    kNumTypes,
  };

  HeapEntry(HeapSnapshot* snapshot, int index, Type type, const char* name,
            SnapshotObjectId id, size_t self_size, unsigned trace_node_id);

  HeapSnapshot* snapshot() { return snapshot_; }
  Type type() const { return static_cast<Type>(type_); }
  void set_type(Type type) { type_ = static_cast<unsigned>(type); }
  const char* name() const { return name_; }
  void set_name(const char* name) { name_ = name; }
  SnapshotObjectId id() const { return id_; }
  size_t self_size() const { return self_size_; }
  void add_self_size(size_t size) { self_size_ += size; }
  unsigned trace_node_id() const { return trace_node_id_; }
  int index() const { return index_; }
  V8_INLINE int children_count() const;
  V8_INLINE int set_children_index(int index);
  V8_INLINE void add_child(HeapGraphEdge* edge);
  V8_INLINE HeapGraphEdge* child(int i);
  V8_INLINE Isolate* isolate() const;

  void set_detachedness(v8::EmbedderGraph::Node::Detachedness value) {
    detachedness_ = static_cast<uint8_t>(value);
  }
  uint8_t detachedness() const { return detachedness_; }

  enum ReferenceVerification {
    // Verify that the reference can be found via marking, if verification is
    // enabled.
    kVerify,

    // Skip verifying that the reference can be found via marking, for any of
    // the following reasons:

    kEphemeron,
    kOffHeapPointer,
    kCustomWeakPointer,
  };

  void VerifyReference(HeapGraphEdge::Type type, HeapEntry* entry,
                       HeapSnapshotGenerator* generator,
                       ReferenceVerification verification);
  void SetIndexedReference(HeapGraphEdge::Type type, int index,
                           HeapEntry* entry, HeapSnapshotGenerator* generator,
                           ReferenceVerification verification = kVerify);
  void SetNamedReference(HeapGraphEdge::Type type, const char* name,
                         HeapEntry* entry, HeapSnapshotGenerator* generator,
                         ReferenceVerification verification = kVerify);
  void SetIndexedAutoIndexReference(
      HeapGraphEdge::Type type, HeapEntry* child,
      HeapSnapshotGenerator* generator,
      ReferenceVerification verification = kVerify) {
    SetIndexedReference(type, children_count_ + 1, child, generator,
                        verification);
  }
  void SetNamedAutoIndexReference(HeapGraphEdge::Type type,
                                  const char* description, HeapEntry* child,
                                  StringsStorage* strings,
                                  HeapSnapshotGenerator* generator,
                                  ReferenceVerification verification = kVerify);

  V8_EXPORT_PRIVATE void Print(const char* prefix, const char* edge_name,
                               int max_depth, int indent) const;

 private:
  V8_INLINE std::vector<HeapGraphEdge*>::iterator children_begin() const;
  V8_INLINE std::vector<HeapGraphEdge*>::iterator children_end() const;
  const char* TypeAsString() const;

  static_assert(kNumTypes <= 1 << 4);
  unsigned type_ : 4;
  unsigned index_ : 28;  // Supports up to ~250M objects.
  union {
    // The count is used during the snapshot build phase,
    // then it gets converted into the index by the |FillChildren| function.
    unsigned children_count_;
    unsigned children_end_index_;
  };
#ifdef V8_TARGET_ARCH_64_BIT
  size_t self_size_ : 48;
#else   // !V8_TARGET_ARCH_64_BIT
  size_t self_size_;
#endif  // !V8_TARGET_ARCH_64_BIT
  uint8_t detachedness_ = 0;
  HeapSnapshot* snapshot_;
  const char* name_;
  SnapshotObjectId id_;
  // id of allocation stack trace top node
  unsigned trace_node_id_;
};

// HeapSnapshot represents a single heap snapshot. It is stored in
// HeapProfiler, which is also a factory for
// HeapSnapshots. All HeapSnapshots share strings copied from JS heap
// to be able to return them even if they were collected.
// HeapSnapshotGenerator fills in a HeapSnapshot.
class HeapSnapshot {
 public:
  HeapSnapshot(HeapProfiler* profiler,
               v8::HeapProfiler::HeapSnapshotMode snapshot_mode,
               v8::HeapProfiler::NumericsMode numerics_mode);
  HeapSnapshot(const HeapSnapshot&) = delete;
  HeapSnapshot& operator=(const HeapSnapshot&) = delete;
  void Delete();

  HeapProfiler* profiler() const { return profiler_; }
  HeapEntry* root() const { return root_entry_; }
  HeapEntry* gc_roots() const { return gc_roots_entry_; }
  HeapEntry* gc_subroot(Root root) const {
    return gc_subroot_entries_[static_cast<int>(root)];
  }
  std::deque<HeapEntry>& entries() { return entries_; }
  const std::deque<HeapEntry>& entries() const { return entries_; }
  std::deque<HeapGraphEdge>& edges() { return edges_; }
  const std::deque<HeapGraphEdge>& edges() const { return edges_; }
  std::vector<HeapGraphEdge*>& children() { return children_; }
  const std::vector<EntrySourceLocation>& locations() const {
    return locations_;
  }
  void RememberLastJSObjectId();
  SnapshotObjectId max_snapshot_js_object_id() const {
    return max_snapshot_js_object_id_;
  }
  bool is_complete() const { return !children_.empty(); }
  bool capture_numeric_value() const {
    return numerics_mode_ ==
           v8::HeapProfiler::NumericsMode::kExposeNumericValues;
  }
  bool expose_internals() const {
    return snapshot_mode_ ==
           v8::HeapProfiler::HeapSnapshotMode::kExposeInternals;
  }

  void AddLocation(HeapEntry* entry, int scriptId, int line, int col);
  HeapEntry* AddEntry(HeapEntry::Type type,
                      const char* name,
                      SnapshotObjectId id,
                      size_t size,
                      unsigned trace_node_id);
  void AddSyntheticRootEntries();
  HeapEntry* GetEntryById(SnapshotObjectId id);
  void FillChildren();

  void AddScriptLineEnds(int script_id, String::LineEndsVector&& line_ends);
  String::LineEndsVector& GetScriptLineEnds(int script_id);

  void Print(int max_depth);

 private:
  void AddRootEntry();
  void AddGcRootsEntry();
  void AddGcSubrootEntry(Root root, SnapshotObjectId id);

  HeapProfiler* profiler_;
  HeapEntry* root_entry_ = nullptr;
  HeapEntry* gc_roots_entry_ = nullptr;
  HeapEntry* gc_subroot_entries_[static_cast<int>(Root::kNumberOfRoots)];
  // For |entries_| we rely on the deque property, that it never reallocates
  // backing storage, thus all entry pointers remain valid for the duration
  // of snapshotting.
  std::deque<HeapEntry> entries_;
  std::deque<HeapGraphEdge> edges_;
  std::vector<HeapGraphEdge*> children_;
  std::unordered_map<SnapshotObjectId, HeapEntry*> entries_by_id_cache_;
  std::vector<EntrySourceLocation> locations_;
  SnapshotObjectId max_snapshot_js_object_id_ = -1;
  v8::HeapProfiler::HeapSnapshotMode snapshot_mode_;
  v8::HeapProfiler::NumericsMode numerics_mode_;

  // The ScriptsLineEndsMap instance stores the line ends of scripts that did
  // not get their line_ends() information populated in heap.
  using ScriptId = int;
  using ScriptsLineEndsMap =
      std::unordered_map<ScriptId, String::LineEndsVector>;
  ScriptsLineEndsMap scripts_line_ends_map_;
};


class HeapObjectsMap {
 public:
  struct TimeInterval {
    explicit TimeInterval(SnapshotObjectId id)
        : id(id), size(0), count(0), timestamp(base::TimeTicks::Now()) {}
    SnapshotObjectId last_assigned_id() const { return id - kObjectIdStep; }
    SnapshotObjectId id;
    uint32_t size;
    uint32_t count;
    base::TimeTicks timestamp;
  };
  enum class MarkEntryAccessed {
    kNo,
    kYes,
  };
  enum class IsNativeObject {
    kNo,
    kYes,
  };

  explicit HeapObjectsMap(Heap* heap);
  HeapObjectsMap(const HeapObjectsMap&) = delete;
  HeapObjectsMap& operator=(const HeapObjectsMap&) = delete;

  Heap* heap() const { return heap_; }

  SnapshotObjectId FindEntry(Address addr);
  SnapshotObjectId FindOrAddEntry(
      Address addr, unsigned int size,
      MarkEntryAccessed accessed = MarkEntryAccessed::kYes,
      IsNativeObject is_native_object = IsNativeObject::kNo);
  SnapshotObjectId FindMergedNativeEntry(NativeObject addr);
  void AddMergedNativeEntry(NativeObject addr, Address canonical_addr);
  bool MoveObject(Address from, Address to, int size);
  void UpdateObjectSize(Address addr, int size);
  SnapshotObjectId last_assigned_id() const {
    return next_id_ - kObjectIdStep;
  }
  SnapshotObjectId get_next_id() {
    next_id_ += kObjectIdStep;
    return next_id_ - kObjectIdStep;
  }
  SnapshotObjectId get_next_native_id() {
    next_native_id_ += kObjectIdStep;
    return next_native_id_ - kObjectIdStep;
  }

  void StopHeapObjectsTracking();
  SnapshotObjectId PushHeapObjectsStats(OutputStream* stream,
                                        int64_t* timestamp_us);
  const std::vector<TimeInterval>& samples() const { return time_intervals_; }

  static const int kObjectIdStep = 2;
  static const SnapshotObjectId kInternalRootObjectId;
  static const SnapshotObjectId kGcRootsObjectId;
  static const SnapshotObjectId kGcRootsFirstSubrootId;
  static const SnapshotObjectId kFirstAvailableObjectId;
  static const SnapshotObjectId kFirstAvailableNativeId;

  void UpdateHeapObjectsMap();
  void RemoveDeadEntries();

 private:
  struct EntryInfo {
    EntryInfo(SnapshotObjectId id, Address addr, unsigned int size,
              bool accessed)
        : id(id), addr(addr), size(size), accessed(accessed) {}
    SnapshotObjectId id;
    Address addr;
    unsigned int size;
    bool accessed;
  };

  SnapshotObjectId next_id_;
  SnapshotObjectId next_native_id_;
  // TODO(jkummerow): Use a map that uses {Address} as the key type.
  base::HashMap entries_map_;
  std::vector<EntryInfo> entries_;
  std::vector<TimeInterval> time_intervals_;
  // Map from NativeObject to EntryInfo index in entries_.
  std::unordered_map<NativeObject, size_t> merged_native_entries_map_;
  Heap* heap_;
};

// A typedef for referencing anything that can be snapshotted living
// in any kind of heap memory.
using HeapThing = void*;

// An interface that creates HeapEntries by HeapThings.
class HeapEntriesAllocator {
 public:
  virtual ~HeapEntriesAllocator() = default;
  virtual HeapEntry* AllocateEntry(HeapThing ptr) = 0;
  virtual HeapEntry* AllocateEntry(Tagged<Smi> smi) = 0;
};

class SnapshottingProgressReportingInterface {
 public:
  virtual ~SnapshottingProgressReportingInterface() = default;
  virtual void ProgressStep() = 0;
  virtual bool ProgressReport(bool force) = 0;
};

// An implementation of V8 heap graph extractor.
class V8_EXPORT_PRIVATE V8HeapExplorer : public HeapEntriesAllocator {
 public:
  V8HeapExplorer(HeapSnapshot* snapshot,
                 SnapshottingProgressReportingInterface* progress,
                 v8::HeapProfiler::ObjectNameResolver* resolver);
  ~V8HeapExplorer() override = default;
  V8HeapExplorer(const V8HeapExplorer&) = delete;
  V8HeapExplorer& operator=(const V8HeapExplorer&) = delete;

  V8_INLINE Isolate* isolate() { return Isolate::FromHeap(heap_); }

  HeapEntry* AllocateEntry(HeapThing ptr) override;
  HeapEntry* AllocateEntry(Tagged<Smi> smi) override;
  uint32_t EstimateObjectsCount();
  void PopulateLineEnds();
  bool IterateAndExtractReferences(HeapSnapshotGenerator* generator);

  using TemporaryGlobalObjectTags =
      std::vector<std::pair<v8::Global<v8::Object>, const char*>>;
  // Modifies heap. Must not be run during heap traversal. Collects a temporary
  // list of global objects and their tags. The list may be invalidated after
  // running GC.
  TemporaryGlobalObjectTags CollectTemporaryGlobalObjectsTags();
  // Converts the temporary list of global objects and their tags into a map
  // that can be used throughout snapshot generation.
  void MakeGlobalObjectTagMap(TemporaryGlobalObjectTags&&);

  void TagBuiltinCodeObject(Tagged<Code> code, const char* name);
  HeapEntry* AddEntry(Address address,
                      HeapEntry::Type type,
                      const char* name,
                      size_t size);

  static Tagged<JSFunction> GetConstructor(Isolate* isolate,
                                           Tagged<JSReceiver> receiver);
  static Tagged<String> GetConstructorName(Isolate* isolate,
                                           Tagged<JSObject> object);

 private:
  void MarkVisitedField(int offset);

  HeapEntry* AddEntry(Tagged<HeapObject> object);
  HeapEntry* AddEntry(Tagged<HeapObject> object, HeapEntry::Type type,
                      const char* name);

  const char* GetSystemEntryName(Tagged<HeapObject> object);
  HeapEntry::Type GetSystemEntryType(Tagged<HeapObject> object);

  Tagged<JSFunction> GetLocationFunction(Tagged<HeapObject> object);
  void ExtractLocation(HeapEntry* entry, Tagged<HeapObject> object);
  void ExtractLocationForJSFunction(HeapEntry* entry, Tagged<JSFunction> func);
  void ExtractReferences(HeapEntry* entry, Tagged<HeapObject> obj);
  void ExtractJSGlobalProxyReferences(HeapEntry* entry,
                                      Tagged<JSGlobalProxy> proxy);
  void ExtractJSObjectReferences(HeapEntry* entry, Tagged<JSObject> js_obj);
  void ExtractStringReferences(HeapEntry* entry, Tagged<String> obj);
  void ExtractSymbolReferences(HeapEntry* entry, Tagged<Symbol> symbol);
  void ExtractJSCollectionReferences(HeapEntry* entry,
                                     Tagged<JSCollection> collection);
  void ExtractJSWeakCollectionReferences(HeapEntry* entry,
                                         Tagged<JSWeakCollection> collection);
  void ExtractEphemeronHashTableReferences(HeapEntry* entry,
                                           Tagged<EphemeronHashTable> table);
  void ExtractContextReferences(HeapEntry* entry, Tagged<Context> context);
  void ExtractMapReferences(HeapEntry* entry, Tagged<Map> map);
  void ExtractSharedFunctionInfoReferences(HeapEntry* entry,
                                           Tagged<SharedFunctionInfo> shared);
  void ExtractScriptReferences(HeapEntry* entry, Tagged<Script> script);
  void ExtractAccessorInfoReferences(HeapEntry* entry,
                                     Tagged<AccessorInfo> accessor_info);
  void ExtractAccessorPairReferences(HeapEntry* entry,
                                     Tagged<AccessorPair> accessors);
  void ExtractCodeReferences(HeapEntry* entry, Tagged<Code> code);
  void ExtractInstructionStreamReferences(HeapEntry* entry,
                                          Tagged<InstructionStream> code);
  void ExtractCellReferences(HeapEntry* entry, Tagged<Cell> cell);
  void ExtractJSWeakRefReferences(HeapEntry* entry,
                                  Tagged<JSWeakRef> js_weak_ref);
  void ExtractWeakCellReferences(HeapEntry* entry, Tagged<WeakCell> weak_cell);
  void ExtractFeedbackCellReferences(HeapEntry* entry,
                                     Tagged<FeedbackCell> feedback_cell);
  void ExtractPropertyCellReferences(HeapEntry* entry,
                                     Tagged<PropertyCell> cell);
  void ExtractPrototypeInfoReferences(HeapEntry* entry,
                                      Tagged<PrototypeInfo> info);
  void ExtractAllocationSiteReferences(HeapEntry* entry,
                                       Tagged<AllocationSite> site);
  void ExtractArrayBoilerplateDescriptionReferences(
      HeapEntry* entry, Tagged<ArrayBoilerplateDescription> value);
  void ExtractRegExpBoilerplateDescriptionReferences(
      HeapEntry* entry, Tagged<RegExpBoilerplateDescription> value);
  void ExtractJSArrayBufferReferences(HeapEntry* entry,
                                      Tagged<JSArrayBuffer> buffer);
  void ExtractJSPromiseReferences(HeapEntry* entry, Tagged<JSPromise> promise);
  void ExtractJSGeneratorObjectReferences(HeapEntry* entry,
                                          Tagged<JSGeneratorObject> generator);
  void ExtractFixedArrayReferences(HeapEntry* entry, Tagged<FixedArray> array);
  void ExtractNumberReference(HeapEntry* entry, Tagged<Object> number);
  void ExtractBytecodeArrayReferences(HeapEntry* entry,
                                      Tagged<BytecodeArray> bytecode);
  void ExtractScopeInfoReferences(HeapEntry* entry, Tagged<ScopeInfo> info);
  void ExtractFeedbackVectorReferences(HeapEntry* entry,
                                       Tagged<FeedbackVector> feedback_vector);
  void ExtractDescriptorArrayReferences(HeapEntry* entry,
                                        Tagged<DescriptorArray> array);
  void ExtractEnumCacheReferences(HeapEntry* entry, Tagged<EnumCache> cache);
  void ExtractTransitionArrayReferences(HeapEntry* entry,
                                        Tagged<TransitionArray> transitions);
  template <typename T>
  void ExtractWeakArrayReferences(int header_size, HeapEntry* entry,
                                  Tagged<T> array);
  void ExtractPropertyReferences(Tagged<JSObject> js_obj, HeapEntry* entry);
  void ExtractAccessorPairProperty(HeapEntry* entry, Tagged<Name> key,
                                   Tagged<Object> callback_obj,
                                   int field_offset = -1);
  void ExtractElementReferences(Tagged<JSObject> js_obj, HeapEntry* entry);
  void ExtractInternalReferences(Tagged<JSObject> js_obj, HeapEntry* entry);

#if V8_ENABLE_WEBASSEMBLY
  void ExtractWasmStructReferences(Tagged<WasmStruct> obj, HeapEntry* entry);
  void ExtractWasmArrayReferences(Tagged<WasmArray> obj, HeapEntry* entry);
  void ExtractWasmTrustedInstanceDataReferences(
      Tagged<WasmTrustedInstanceData> obj, HeapEntry* entry);
  void ExtractWasmInstanceObjectReferences(Tagged<WasmInstanceObject> obj,
                                           HeapEntry* entry);
  void ExtractWasmModuleObjectReferences(Tagged<WasmModuleObject> obj,
                                         HeapEntry* entry);
#endif  // V8_ENABLE_WEBASSEMBLY

  bool IsEssentialObject(Tagged<Object> object);
  bool IsEssentialHiddenReference(Tagged<Object> parent, int field_offset);

  void SetContextReference(HeapEntry* parent_entry,
                           Tagged<String> reference_name, Tagged<Object> child,
                           int field_offset);
  void SetNativeBindReference(HeapEntry* parent_entry,
                              const char* reference_name, Tagged<Object> child);
  void SetElementReference(HeapEntry* parent_entry, int index,
                           Tagged<Object> child);
  void SetInternalReference(HeapEntry* parent_entry, const char* reference_name,
                            Tagged<Object> child, int field_offset = -1);
  void SetInternalReference(HeapEntry* parent_entry, int index,
                            Tagged<Object> child, int field_offset = -1);
  void SetHiddenReference(Tagged<HeapObject> parent_obj,
                          HeapEntry* parent_entry, int index,
                          Tagged<Object> child, int field_offset);
  void SetWeakReference(
      HeapEntry* parent_entry, const char* reference_name,
      Tagged<Object> child_obj, int field_offset,
      HeapEntry::ReferenceVerification verification = HeapEntry::kVerify);
  void SetWeakReference(HeapEntry* parent_entry, int index,
                        Tagged<Object> child_obj,
                        std::optional<int> field_offset);
  void SetPropertyReference(HeapEntry* parent_entry,
                            Tagged<Name> reference_name, Tagged<Object> child,
                            const char* name_format_string = nullptr,
                            int field_offset = -1);
  void SetDataOrAccessorPropertyReference(
      PropertyKind kind, HeapEntry* parent_entry, Tagged<Name> reference_name,
      Tagged<Object> child, const char* name_format_string = nullptr,
      int field_offset = -1);

  void SetUserGlobalReference(Tagged<Object> user_global);
  void SetRootGcRootsReference();
  void SetGcRootsReference(Root root);
  void SetGcSubrootReference(Root root, const char* description, bool is_weak,
                             Tagged<Object> child);
  const char* GetStrongGcSubrootName(Tagged<HeapObject> object);
  void TagObject(Tagged<Object> obj, const char* tag,
                 std::optional<HeapEntry::Type> type = {},
                 bool overwrite_existing_name = false);
  void RecursivelyTagConstantPool(Tagged<Object> obj, const char* tag,
                                  HeapEntry::Type type, int recursion_limit);

  HeapEntry* GetEntry(Tagged<Object> obj);

  Heap* heap_;
  HeapSnapshot* snapshot_;
  StringsStorage* names_;
  HeapObjectsMap* heap_object_map_;
  SnapshottingProgressReportingInterface* progress_;
  HeapSnapshotGenerator* generator_ = nullptr;
  std::unordered_map<Tagged<JSGlobalObject>, const char*, Object::Hasher>
      global_object_tag_map_;
  UnorderedHeapObjectMap<const char*> strong_gc_subroot_names_;
  std::unordered_set<Tagged<JSGlobalObject>, Object::Hasher> user_roots_;
  v8::HeapProfiler::ObjectNameResolver* global_object_name_resolver_;

  std::vector<bool> visited_fields_;
  size_t max_pointers_;

  friend class IndexedReferencesExtractor;
  friend class RootsReferencesExtractor;
};

// An implementation of retained native objects extractor.
class NativeObjectsExplorer {
 public:
  NativeObjectsExplorer(HeapSnapshot* snapshot,
                        SnapshottingProgressReportingInterface* progress);
  NativeObjectsExplorer(const NativeObjectsExplorer&) = delete;
  NativeObjectsExplorer& operator=(const NativeObjectsExplorer&) = delete;
  bool IterateAndExtractReferences(HeapSnapshotGenerator* generator);

 private:
  // Returns an entry for a given node, where node may be a V8 node or an
  // embedder node. Returns the coresponding wrapper node if present.
  HeapEntry* EntryForEmbedderGraphNode(EmbedderGraph::Node* node);
  void MergeNodeIntoEntry(HeapEntry* entry, EmbedderGraph::Node* original_node,
                          EmbedderGraph::Node* wrapper_node);

  Isolate* isolate_;
  HeapSnapshot* snapshot_;
  StringsStorage* names_;
  HeapObjectsMap* heap_object_map_;
  std::unique_ptr<HeapEntriesAllocator> embedder_graph_entries_allocator_;
  // Used during references extraction.
  HeapSnapshotGenerator* generator_ = nullptr;

  static HeapThing const kNativesRootObject;

  friend class GlobalHandlesExtractor;
};

class HeapEntryVerifier;

class HeapSnapshotGenerator : public SnapshottingProgressReportingInterface {
 public:
  // The HeapEntriesMap instance is used to track a mapping between
  // real heap objects and their representations in heap snapshots.
  using HeapEntriesMap = base::HashMap;
  // The SmiEntriesMap instance is used to track a mapping between smi and
  // their representations in heap snapshots.
  using SmiEntriesMap = std::unordered_map<int, HeapEntry*>;

  HeapSnapshotGenerator(HeapSnapshot* snapshot, v8::ActivityControl* control,
                        v8::HeapProfiler::ObjectNameResolver* resolver,
                        Heap* heap, cppgc::EmbedderStackState stack_state);
  HeapSnapshotGenerator(const HeapSnapshotGenerator&) = delete;
  HeapSnapshotGenerator& operator=(const HeapSnapshotGenerator&) = delete;
  bool GenerateSnapshot();
  bool GenerateSnapshotAfterGC();

  HeapEntry* FindEntry(HeapThing ptr) {
    HeapEntriesMap::Entry* entry =
        entries_map_.Lookup(ptr, ComputePointerHash(ptr));
    return entry ? static_cast<HeapEntry*>(entry->value) : nullptr;
  }

  HeapEntry* FindEntry(Tagged<Smi> smi) {
    auto it = smis_map_.find(smi.value());
    return it != smis_map_.end() ? it->second : nullptr;
  }

#ifdef V8_ENABLE_HEAP_SNAPSHOT_VERIFY
  HeapThing FindHeapThingForHeapEntry(HeapEntry* entry) {
    // The reverse lookup map is only populated if the verification flag is
    // enabled.
    DCHECK(v8_flags.heap_snapshot_verify);

    auto it = reverse_entries_map_.find(entry);
    return it == reverse_entries_map_.end() ? nullptr : it->second;
  }

  HeapEntryVerifier* verifier() const { return verifier_; }
  void set_verifier(HeapEntryVerifier* verifier) {
    DCHECK_IMPLIES(verifier_, !verifier);
    verifier_ = verifier;
  }
#endif

  HeapEntry* AddEntry(Tagged<Smi> smi, HeapEntriesAllocator* allocator) {
    return smis_map_.emplace(smi.value(), allocator->AllocateEntry(smi))
        .first->second;
  }

  HeapEntry* FindOrAddEntry(HeapThing ptr, HeapEntriesAllocator* allocator) {
    HeapEntriesMap::Entry* entry =
        entries_map_.LookupOrInsert(ptr, ComputePointerHash(ptr));
    if (entry->value != nullptr) {
      return static_cast<HeapEntry*>(entry->value);
    }
    HeapEntry* result = allocator->AllocateEntry(ptr);
    entry->value = result;
#ifdef V8_ENABLE_HEAP_SNAPSHOT_VERIFY
    if (v8_flags.heap_snapshot_verify) {
      reverse_entries_map_.emplace(result, ptr);
    }
#endif
    return result;
  }

  HeapEntry* FindOrAddEntry(Tagged<Smi> smi, HeapEntriesAllocator* allocator) {
    HeapEntry* entry = FindEntry(smi);
    return entry != nullptr ? entry : AddEntry(smi, allocator);
  }

  Heap* heap() const { return heap_; }

 private:
  bool FillReferences();
  void ProgressStep() override;
  bool ProgressReport(bool force = false) override;
  void InitProgressCounter();

  HeapSnapshot* snapshot_;
  v8::ActivityControl* control_;
  V8HeapExplorer v8_heap_explorer_;
  NativeObjectsExplorer dom_explorer_;
  // Mapping from HeapThing pointers to HeapEntry indices.
  HeapEntriesMap entries_map_;
  SmiEntriesMap smis_map_;
  // Used during snapshot generation.
  uint32_t progress_counter_;
  uint32_t progress_total_;
  Heap* heap_;
  cppgc::EmbedderStackState stack_state_;

#ifdef V8_ENABLE_HEAP_SNAPSHOT_VERIFY
  std::unordered_map<HeapEntry*, HeapThing> reverse_entries_map_;
  HeapEntryVerifier* verifier_ = nullptr;
#endif
};

class OutputStreamWriter;

class HeapSnapshotJSONSerializer {
 public:
  explicit HeapSnapshotJSONSerializer(HeapSnapshot* snapshot)
      : snapshot_(snapshot),
        strings_(StringsMatch),
        next_node_id_(1),
        next_string_id_(1),
        writer_(nullptr) {}
  HeapSnapshotJSONSerializer(const HeapSnapshotJSONSerializer&) = delete;
  HeapSnapshotJSONSerializer& operator=(const HeapSnapshotJSONSerializer&) =
      delete;
  void Serialize(v8::OutputStream* stream);

 private:
  V8_INLINE static bool StringsMatch(void* key1, void* key2) {
    return strcmp(reinterpret_cast<char*>(key1),
                  reinterpret_cast<char*>(key2)) == 0;
  }

  V8_INLINE static uint32_t StringHash(const void* string);

  int GetStringId(const char* s);
  V8_INLINE int to_node_index(const HeapEntry* e);
  V8_INLINE int to_node_index(int entry_index);
  void SerializeEdge(HeapGraphEdge* edge, bool first_edge);
  void SerializeEdges();
  void SerializeImpl();
  void SerializeNode(const HeapEntry* entry);
  void SerializeNodes();
  void SerializeSnapshot();
  void SerializeTraceTree();
  void SerializeTraceNode(AllocationTraceNode* node);
  void SerializeTraceNodeInfos();
  void SerializeSamples();
  void SerializeString(const unsigned char* s);
  void SerializeStrings();
  void SerializeLocation(const EntrySourceLocation& location);
  void SerializeLocations();

  static const int kEdgeFieldsCount;
  static const int kNodeFieldsCount;

  HeapSnapshot* snapshot_;
  base::CustomMatcherHashMap strings_;
  int next_node_id_;
  int next_string_id_;
  OutputStreamWriter* writer_;

  friend class HeapSnapshotJSONSerializerEnumerator;
  friend class HeapSnapshotJSONSerializerIterator;
};

}  // namespace v8::internal

#endif  // V8_PROFILER_HEAP_SNAPSHOT_GENERATOR_H_
