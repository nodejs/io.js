// Copyright 2013 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/profiler/heap-snapshot-generator.h"

#include <optional>
#include <utility>

#include "src/api/api-inl.h"
#include "src/base/vector.h"
#include "src/codegen/assembler-inl.h"
#include "src/common/assert-scope.h"
#include "src/common/globals.h"
#include "src/debug/debug.h"
#include "src/handles/global-handles.h"
#include "src/heap/combined-heap.h"
#include "src/heap/heap.h"
#include "src/heap/safepoint.h"
#include "src/numbers/conversions.h"
#include "src/objects/allocation-site-inl.h"
#include "src/objects/api-callbacks.h"
#include "src/objects/cell-inl.h"
#include "src/objects/feedback-cell-inl.h"
#include "src/objects/hash-table-inl.h"
#include "src/objects/js-array-buffer-inl.h"
#include "src/objects/js-array-inl.h"
#include "src/objects/js-collection-inl.h"
#include "src/objects/js-generator-inl.h"
#include "src/objects/js-objects.h"
#include "src/objects/js-promise-inl.h"
#include "src/objects/js-regexp-inl.h"
#include "src/objects/js-weak-refs-inl.h"
#include "src/objects/literal-objects-inl.h"
#include "src/objects/objects-inl.h"
#include "src/objects/prototype.h"
#include "src/objects/slots-inl.h"
#include "src/objects/struct-inl.h"
#include "src/objects/transitions-inl.h"
#include "src/objects/visitors.h"
#include "src/profiler/allocation-tracker.h"
#include "src/profiler/heap-profiler.h"
#include "src/profiler/heap-snapshot-generator-inl.h"
#include "src/profiler/output-stream-writer.h"

#if V8_ENABLE_WEBASSEMBLY
#include "src/wasm/names-provider.h"
#include "src/wasm/string-builder.h"
#include "src/wasm/wasm-objects.h"
#endif  // V8_ENABLE_WEBASSEMBLY

namespace v8::internal {

#ifdef V8_ENABLE_HEAP_SNAPSHOT_VERIFY
class HeapEntryVerifier {
 public:
  HeapEntryVerifier(HeapSnapshotGenerator* generator, Tagged<HeapObject> obj)
      : generator_(generator),
        primary_object_(obj),
        reference_summary_(
            ReferenceSummary::SummarizeReferencesFrom(generator->heap(), obj)) {
    generator->set_verifier(this);
  }
  ~HeapEntryVerifier() {
    CheckAllReferencesWereChecked();
    generator_->set_verifier(nullptr);
  }

  // Checks that `host` retains `target`, according to the marking visitor. This
  // allows us to verify, when adding edges to the snapshot, that they
  // correspond to real retaining relationships.
  void CheckStrongReference(Tagged<HeapObject> host,
                            Tagged<HeapObject> target) {
    // All references should be from the current primary object.
    CHECK_EQ(host, primary_object_);

    checked_objects_.insert(target);

    // Check whether there is a direct strong reference from host to target.
    if (reference_summary_.strong_references().find(target) !=
        reference_summary_.strong_references().end()) {
      return;
    }

    // There is no direct reference from host to target, but sometimes heap
    // snapshots include references that skip one, two, or three objects, such
    // as __proto__ on a JSObject referring to its Map's prototype, or a
    // property getter that bypasses the property array and accessor info. At
    // this point, we must check for those indirect references.
    for (size_t level = 0; level < 3; ++level) {
      const UnorderedHeapObjectSet& indirect =
          GetIndirectStrongReferences(level);
      if (indirect.find(target) != indirect.end()) {
        return;
      }
    }

    FATAL("Could not find any matching reference");
  }

  // Checks that `host` has a weak reference to `target`, according to the
  // marking visitor.
  void CheckWeakReference(Tagged<HeapObject> host, Tagged<HeapObject> target) {
    // All references should be from the current primary object.
    CHECK_EQ(host, primary_object_);

    checked_objects_.insert(target);
    CHECK_NE(reference_summary_.weak_references().find(target),
             reference_summary_.weak_references().end());
  }

  // Marks the relationship between `host` and `target` as checked, even if the
  // marking visitor found no such relationship. This is necessary for
  // ephemerons, where a pair of objects is required to retain the target.
  // Use this function with care, since it bypasses verification.
  void MarkReferenceCheckedWithoutChecking(Tagged<HeapObject> host,
                                           Tagged<HeapObject> target) {
    if (host == primary_object_) {
      checked_objects_.insert(target);
    }
  }

  // Verifies that all of the references found by the marking visitor were
  // checked via a call to CheckStrongReference or CheckWeakReference, or
  // deliberately skipped via a call to MarkReferenceCheckedWithoutChecking.
  // This ensures that there aren't retaining relationships found by the marking
  // visitor which were omitted from the heap snapshot.
  void CheckAllReferencesWereChecked() {
    // Both loops below skip pointers to read-only objects, because the heap
    // snapshot deliberately omits many of those (see IsEssentialObject).
    // Read-only objects can't ever retain normal read-write objects, so these
    // are fine to skip.
    for (Tagged<HeapObject> obj : reference_summary_.strong_references()) {
      if (!MemoryChunk::FromHeapObject(obj)->InReadOnlySpace()) {
        CHECK_NE(checked_objects_.find(obj), checked_objects_.end());
      }
    }
    for (Tagged<HeapObject> obj : reference_summary_.weak_references()) {
      if (!MemoryChunk::FromHeapObject(obj)->InReadOnlySpace()) {
        CHECK_NE(checked_objects_.find(obj), checked_objects_.end());
      }
    }
  }

 private:
  using UnorderedHeapObjectSet =
      std::unordered_set<Tagged<HeapObject>, Object::Hasher,
                         Object::KeyEqualSafe>;

  const UnorderedHeapObjectSet& GetIndirectStrongReferences(size_t level) {
    CHECK_GE(indirect_strong_references_.size(), level);

    if (indirect_strong_references_.size() == level) {
      // Expansion is needed.
      indirect_strong_references_.resize(level + 1);
      const UnorderedHeapObjectSet& previous =
          level == 0 ? reference_summary_.strong_references()
                     : indirect_strong_references_[level - 1];
      for (Tagged<HeapObject> obj : previous) {
        if (MemoryChunk::FromHeapObject(obj)->InReadOnlySpace()) {
          // Marking visitors don't expect to visit objects in read-only space,
          // and will fail DCHECKs if they are used on those objects. Read-only
          // objects can never retain anything outside read-only space, so
          // skipping those objects doesn't weaken verification.
          continue;
        }

        // Indirect references should only bypass internal structures, not
        // user-visible objects or contexts.
        if (IsJSReceiver(obj) || IsString(obj) || IsContext(obj)) {
          continue;
        }

        ReferenceSummary summary =
            ReferenceSummary::SummarizeReferencesFrom(generator_->heap(), obj);
        indirect_strong_references_[level].insert(
            summary.strong_references().begin(),
            summary.strong_references().end());
      }
    }

    return indirect_strong_references_[level];
  }

  DISALLOW_GARBAGE_COLLECTION(no_gc)
  HeapSnapshotGenerator* generator_;
  Tagged<HeapObject> primary_object_;

  // All objects referred to by primary_object_, according to a marking visitor.
  ReferenceSummary reference_summary_;

  // Objects that have been checked via a call to CheckStrongReference or
  // CheckWeakReference, or deliberately skipped via a call to
  // MarkReferenceCheckedWithoutChecking.
  std::unordered_set<Tagged<HeapObject>, Object::Hasher, Object::KeyEqualSafe>
      checked_objects_;

  // Objects transitively retained by the primary object. The objects in the set
  // at index i are retained by the primary object via a chain of i+1
  // intermediate objects.
  std::vector<UnorderedHeapObjectSet> indirect_strong_references_;
};
#endif

HeapGraphEdge::HeapGraphEdge(Type type, const char* name, HeapEntry* from,
                             HeapEntry* to)
    : bit_field_(TypeField::encode(type) |
                 FromIndexField::encode(from->index())),
      to_entry_(to),
      name_(name) {
  DCHECK(type == kContextVariable || type == kProperty || type == kInternal ||
         type == kShortcut || type == kWeak);
}

HeapGraphEdge::HeapGraphEdge(Type type, int index, HeapEntry* from,
                             HeapEntry* to)
    : bit_field_(TypeField::encode(type) |
                 FromIndexField::encode(from->index())),
      to_entry_(to),
      index_(index) {
  DCHECK(type == kElement || type == kHidden);
}

HeapEntry::HeapEntry(HeapSnapshot* snapshot, int index, Type type,
                     const char* name, SnapshotObjectId id, size_t self_size,
                     unsigned trace_node_id)
    : type_(static_cast<unsigned>(type)),
      index_(index),
      children_count_(0),
      self_size_(self_size),
      snapshot_(snapshot),
      name_(name),
      id_(id),
      trace_node_id_(trace_node_id) {
  DCHECK_GE(index, 0);
}

void HeapEntry::VerifyReference(HeapGraphEdge::Type type, HeapEntry* entry,
                                HeapSnapshotGenerator* generator,
                                ReferenceVerification verification) {
#ifdef V8_ENABLE_HEAP_SNAPSHOT_VERIFY
  if (verification == kOffHeapPointer || generator->verifier() == nullptr) {
    // Off-heap pointers are outside the scope of this verification; we just
    // trust the embedder to provide accurate data. If the verifier is null,
    // then verification is disabled.
    return;
  }
  if (verification == kCustomWeakPointer) {
    // The caller declared that this is a weak pointer ignored by the marking
    // visitor. All we can verify at this point is that the edge type declares
    // it to be weak.
    CHECK_EQ(type, HeapGraphEdge::kWeak);
    return;
  }
  Address from_address =
      reinterpret_cast<Address>(generator->FindHeapThingForHeapEntry(this));
  Address to_address =
      reinterpret_cast<Address>(generator->FindHeapThingForHeapEntry(entry));
  if (from_address == kNullAddress || to_address == kNullAddress) {
    // One of these entries doesn't correspond to a real heap object.
    // Verification is not possible.
    return;
  }
  Tagged<HeapObject> from_obj = Cast<HeapObject>(Tagged<Object>(from_address));
  Tagged<HeapObject> to_obj = Cast<HeapObject>(Tagged<Object>(to_address));
  if (MemoryChunk::FromHeapObject(to_obj)->InReadOnlySpace()) {
    // We can't verify pointers into read-only space, because marking visitors
    // might not mark those. For example, every Map has a pointer to the
    // MetaMap, but marking visitors don't bother with following that link.
    // Read-only objects are immortal and can never point to things outside of
    // read-only space, so ignoring these objects is safe from the perspective
    // of ensuring accurate retaining paths for normal read-write objects.
    // Therefore, do nothing.
  } else if (verification == kEphemeron) {
    // Ephemerons can't be verified because they aren't marked directly by the
    // marking visitor.
    generator->verifier()->MarkReferenceCheckedWithoutChecking(from_obj,
                                                               to_obj);
  } else if (type == HeapGraphEdge::kWeak) {
    generator->verifier()->CheckWeakReference(from_obj, to_obj);
  } else {
    generator->verifier()->CheckStrongReference(from_obj, to_obj);
  }
#endif
}

void HeapEntry::SetNamedReference(HeapGraphEdge::Type type, const char* name,
                                  HeapEntry* entry,
                                  HeapSnapshotGenerator* generator,
                                  ReferenceVerification verification) {
  ++children_count_;
  snapshot_->edges().emplace_back(type, name, this, entry);
  VerifyReference(type, entry, generator, verification);
}

void HeapEntry::SetIndexedReference(HeapGraphEdge::Type type, int index,
                                    HeapEntry* entry,
                                    HeapSnapshotGenerator* generator,
                                    ReferenceVerification verification) {
  ++children_count_;
  snapshot_->edges().emplace_back(type, index, this, entry);
  VerifyReference(type, entry, generator, verification);
}

void HeapEntry::SetNamedAutoIndexReference(HeapGraphEdge::Type type,
                                           const char* description,
                                           HeapEntry* child,
                                           StringsStorage* names,
                                           HeapSnapshotGenerator* generator,
                                           ReferenceVerification verification) {
  int index = children_count_ + 1;
  const char* name = description
                         ? names->GetFormatted("%d / %s", index, description)
                         : names->GetName(index);
  SetNamedReference(type, name, child, generator, verification);
}

void HeapEntry::Print(const char* prefix, const char* edge_name, int max_depth,
                      int indent) const {
  static_assert(sizeof(unsigned) == sizeof(id()));
  base::OS::Print("%6zu @%6u %*c %s%s: ", self_size(), id(), indent, ' ',
                  prefix, edge_name);
  if (type() != kString) {
    base::OS::Print("%s %.40s\n", TypeAsString(), name_);
  } else {
    base::OS::Print("\"");
    const char* c = name_;
    while (*c && (c - name_) <= 40) {
      if (*c != '\n')
        base::OS::Print("%c", *c);
      else
        base::OS::Print("\\n");
      ++c;
    }
    base::OS::Print("\"\n");
  }
  if (--max_depth == 0) return;
  for (auto i = children_begin(); i != children_end(); ++i) {
    HeapGraphEdge& edge = **i;
    const char* edge_prefix = "";
    base::EmbeddedVector<char, 64> index;
    edge_name = index.begin();
    switch (edge.type()) {
      case HeapGraphEdge::kContextVariable:
        edge_prefix = "#";
        edge_name = edge.name();
        break;
      case HeapGraphEdge::kElement:
        SNPrintF(index, "%d", edge.index());
        break;
      case HeapGraphEdge::kInternal:
        edge_prefix = "$";
        edge_name = edge.name();
        break;
      case HeapGraphEdge::kProperty:
        edge_name = edge.name();
        break;
      case HeapGraphEdge::kHidden:
        edge_prefix = "$";
        SNPrintF(index, "%d", edge.index());
        break;
      case HeapGraphEdge::kShortcut:
        edge_prefix = "^";
        edge_name = edge.name();
        break;
      case HeapGraphEdge::kWeak:
        edge_prefix = "w";
        edge_name = edge.name();
        break;
      default:
        SNPrintF(index, "!!! unknown edge type: %d ", edge.type());
    }
    edge.to()->Print(edge_prefix, edge_name, max_depth, indent + 2);
  }
}

const char* HeapEntry::TypeAsString() const {
  switch (type()) {
    case kHidden:
      return "/hidden/";
    case kObject:
      return "/object/";
    case kClosure:
      return "/closure/";
    case kString:
      return "/string/";
    case kCode:
      return "/code/";
    case kArray:
      return "/array/";
    case kRegExp:
      return "/regexp/";
    case kHeapNumber:
      return "/number/";
    case kNative:
      return "/native/";
    case kSynthetic:
      return "/synthetic/";
    case kConsString:
      return "/concatenated string/";
    case kSlicedString:
      return "/sliced string/";
    case kSymbol:
      return "/symbol/";
    case kBigInt:
      return "/bigint/";
    case kObjectShape:
      return "/object shape/";
    default:
      return "???";
  }
}

HeapSnapshot::HeapSnapshot(HeapProfiler* profiler,
                           v8::HeapProfiler::HeapSnapshotMode snapshot_mode,
                           v8::HeapProfiler::NumericsMode numerics_mode)
    : profiler_(profiler),
      snapshot_mode_(snapshot_mode),
      numerics_mode_(numerics_mode) {
  // It is very important to keep objects that form a heap snapshot
  // as small as possible. Check assumptions about data structure sizes.
  static_assert(kSystemPointerSize != 4 || sizeof(HeapGraphEdge) == 12);
  static_assert(kSystemPointerSize != 8 || sizeof(HeapGraphEdge) == 24);
  static_assert(kSystemPointerSize != 4 || sizeof(HeapEntry) == 32);
#if V8_CC_MSVC
  static_assert(kSystemPointerSize != 8 || sizeof(HeapEntry) == 48);
#else   // !V8_CC_MSVC
  static_assert(kSystemPointerSize != 8 || sizeof(HeapEntry) == 40);
#endif  // !V8_CC_MSVC
  memset(&gc_subroot_entries_, 0, sizeof(gc_subroot_entries_));
}

void HeapSnapshot::Delete() { profiler_->RemoveSnapshot(this); }

void HeapSnapshot::RememberLastJSObjectId() {
  max_snapshot_js_object_id_ = profiler_->heap_object_map()->last_assigned_id();
}

void HeapSnapshot::AddSyntheticRootEntries() {
  AddRootEntry();
  AddGcRootsEntry();
  SnapshotObjectId id = HeapObjectsMap::kGcRootsFirstSubrootId;
  for (int root = 0; root < static_cast<int>(Root::kNumberOfRoots); root++) {
    AddGcSubrootEntry(static_cast<Root>(root), id);
    id += HeapObjectsMap::kObjectIdStep;
  }
  DCHECK_EQ(HeapObjectsMap::kFirstAvailableObjectId, id);
}

void HeapSnapshot::AddRootEntry() {
  DCHECK_NULL(root_entry_);
  DCHECK(entries_.empty());  // Root entry must be the first one.
  root_entry_ = AddEntry(HeapEntry::kSynthetic, "",
                         HeapObjectsMap::kInternalRootObjectId, 0, 0);
  DCHECK_EQ(1u, entries_.size());
  DCHECK_EQ(root_entry_, &entries_.front());
}

void HeapSnapshot::AddGcRootsEntry() {
  DCHECK_NULL(gc_roots_entry_);
  gc_roots_entry_ = AddEntry(HeapEntry::kSynthetic, "(GC roots)",
                             HeapObjectsMap::kGcRootsObjectId, 0, 0);
}

void HeapSnapshot::AddGcSubrootEntry(Root root, SnapshotObjectId id) {
  DCHECK_NULL(gc_subroot_entries_[static_cast<int>(root)]);
  gc_subroot_entries_[static_cast<int>(root)] =
      AddEntry(HeapEntry::kSynthetic, RootVisitor::RootName(root), id, 0, 0);
}

void HeapSnapshot::AddLocation(HeapEntry* entry, int scriptId, int line,
                               int col) {
  locations_.emplace_back(entry->index(), scriptId, line, col);
}

HeapEntry* HeapSnapshot::AddEntry(HeapEntry::Type type, const char* name,
                                  SnapshotObjectId id, size_t size,
                                  unsigned trace_node_id) {
  DCHECK(!is_complete());
  entries_.emplace_back(this, static_cast<int>(entries_.size()), type, name, id,
                        size, trace_node_id);
  return &entries_.back();
}

void HeapSnapshot::AddScriptLineEnds(int script_id,
                                     String::LineEndsVector&& line_ends) {
  scripts_line_ends_map_.emplace(script_id, std::move(line_ends));
}

String::LineEndsVector& HeapSnapshot::GetScriptLineEnds(int script_id) {
  DCHECK(scripts_line_ends_map_.find(script_id) !=
         scripts_line_ends_map_.end());
  return scripts_line_ends_map_[script_id];
}

void HeapSnapshot::FillChildren() {
  DCHECK(children().empty());
  int children_index = 0;
  for (HeapEntry& entry : entries()) {
    children_index = entry.set_children_index(children_index);
  }
  DCHECK_EQ(edges().size(), static_cast<size_t>(children_index));
  children().resize(edges().size());
  for (HeapGraphEdge& edge : edges()) {
    edge.from()->add_child(&edge);
  }
}

HeapEntry* HeapSnapshot::GetEntryById(SnapshotObjectId id) {
  if (entries_by_id_cache_.empty()) {
    CHECK(is_complete());
    entries_by_id_cache_.reserve(entries_.size());
    for (HeapEntry& entry : entries_) {
      entries_by_id_cache_.emplace(entry.id(), &entry);
    }
  }
  auto it = entries_by_id_cache_.find(id);
  return it != entries_by_id_cache_.end() ? it->second : nullptr;
}

void HeapSnapshot::Print(int max_depth) { root()->Print("", "", max_depth, 0); }

// We split IDs on evens for embedder objects (see
// HeapObjectsMap::GenerateId) and odds for native objects.
const SnapshotObjectId HeapObjectsMap::kInternalRootObjectId = 1;
const SnapshotObjectId HeapObjectsMap::kGcRootsObjectId =
    HeapObjectsMap::kInternalRootObjectId + HeapObjectsMap::kObjectIdStep;
const SnapshotObjectId HeapObjectsMap::kGcRootsFirstSubrootId =
    HeapObjectsMap::kGcRootsObjectId + HeapObjectsMap::kObjectIdStep;
const SnapshotObjectId HeapObjectsMap::kFirstAvailableObjectId =
    HeapObjectsMap::kGcRootsFirstSubrootId +
    static_cast<int>(Root::kNumberOfRoots) * HeapObjectsMap::kObjectIdStep;
const SnapshotObjectId HeapObjectsMap::kFirstAvailableNativeId = 2;

HeapObjectsMap::HeapObjectsMap(Heap* heap)
    : next_id_(kFirstAvailableObjectId),
      next_native_id_(kFirstAvailableNativeId),
      heap_(heap) {
  // The dummy element at zero index is needed as entries_map_ cannot hold
  // an entry with zero value. Otherwise it's impossible to tell if
  // LookupOrInsert has added a new item or just returning exisiting one
  // having the value of zero.
  entries_.emplace_back(0, kNullAddress, 0, true);
}

bool HeapObjectsMap::MoveObject(Address from, Address to, int object_size) {
  DCHECK_NE(kNullAddress, to);
  DCHECK_NE(kNullAddress, from);
  if (from == to) return false;
  void* from_value = entries_map_.Remove(reinterpret_cast<void*>(from),
                                         ComputeAddressHash(from));
  if (from_value == nullptr) {
    // It may occur that some untracked object moves to an address X and there
    // is a tracked object at that address. In this case we should remove the
    // entry as we know that the object has died.
    void* to_value = entries_map_.Remove(reinterpret_cast<void*>(to),
                                         ComputeAddressHash(to));
    if (to_value != nullptr) {
      int to_entry_info_index =
          static_cast<int>(reinterpret_cast<intptr_t>(to_value));
      entries_.at(to_entry_info_index).addr = kNullAddress;
    }
  } else {
    base::HashMap::Entry* to_entry = entries_map_.LookupOrInsert(
        reinterpret_cast<void*>(to), ComputeAddressHash(to));
    if (to_entry->value != nullptr) {
      // We found the existing entry with to address for an old object.
      // Without this operation we will have two EntryInfo's with the same
      // value in addr field. It is bad because later at RemoveDeadEntries
      // one of this entry will be removed with the corresponding entries_map_
      // entry.
      int to_entry_info_index =
          static_cast<int>(reinterpret_cast<intptr_t>(to_entry->value));
      entries_.at(to_entry_info_index).addr = kNullAddress;
    }
    int from_entry_info_index =
        static_cast<int>(reinterpret_cast<intptr_t>(from_value));
    entries_.at(from_entry_info_index).addr = to;
    // Size of an object can change during its life, so to keep information
    // about the object in entries_ consistent, we have to adjust size when the
    // object is migrated.
    if (v8_flags.heap_profiler_trace_objects) {
      PrintF("Move object from %p to %p old size %6d new size %6d\n",
             reinterpret_cast<void*>(from), reinterpret_cast<void*>(to),
             entries_.at(from_entry_info_index).size, object_size);
    }
    entries_.at(from_entry_info_index).size = object_size;
    to_entry->value = from_value;
  }
  return from_value != nullptr;
}

void HeapObjectsMap::UpdateObjectSize(Address addr, int size) {
  FindOrAddEntry(addr, size, MarkEntryAccessed::kNo);
}

SnapshotObjectId HeapObjectsMap::FindEntry(Address addr) {
  base::HashMap::Entry* entry = entries_map_.Lookup(
      reinterpret_cast<void*>(addr), ComputeAddressHash(addr));
  if (entry == nullptr) return v8::HeapProfiler::kUnknownObjectId;
  int entry_index = static_cast<int>(reinterpret_cast<intptr_t>(entry->value));
  EntryInfo& entry_info = entries_.at(entry_index);
  DCHECK(static_cast<uint32_t>(entries_.size()) > entries_map_.occupancy());
  return entry_info.id;
}

SnapshotObjectId HeapObjectsMap::FindOrAddEntry(
    Address addr, unsigned int size, MarkEntryAccessed accessed,
    IsNativeObject is_native_object) {
  bool accessed_bool = accessed == MarkEntryAccessed::kYes;
  bool is_native_object_bool = is_native_object == IsNativeObject::kYes;
  DCHECK(static_cast<uint32_t>(entries_.size()) > entries_map_.occupancy());
  base::HashMap::Entry* entry = entries_map_.LookupOrInsert(
      reinterpret_cast<void*>(addr), ComputeAddressHash(addr));
  if (entry->value != nullptr) {
    int entry_index =
        static_cast<int>(reinterpret_cast<intptr_t>(entry->value));
    EntryInfo& entry_info = entries_.at(entry_index);
    entry_info.accessed = accessed_bool;
    if (v8_flags.heap_profiler_trace_objects) {
      PrintF("Update object size : %p with old size %d and new size %d\n",
             reinterpret_cast<void*>(addr), entry_info.size, size);
    }
    entry_info.size = size;
    DCHECK_EQ(is_native_object_bool, entry_info.id % 2 == 0);
    return entry_info.id;
  }
  entry->value = reinterpret_cast<void*>(entries_.size());
  SnapshotObjectId id =
      is_native_object_bool ? get_next_native_id() : get_next_id();
  entries_.push_back(EntryInfo(id, addr, size, accessed_bool));
  DCHECK(static_cast<uint32_t>(entries_.size()) > entries_map_.occupancy());
  return id;
}

SnapshotObjectId HeapObjectsMap::FindMergedNativeEntry(NativeObject addr) {
  auto it = merged_native_entries_map_.find(addr);
  if (it == merged_native_entries_map_.end())
    return v8::HeapProfiler::kUnknownObjectId;
  return entries_[it->second].id;
}

void HeapObjectsMap::AddMergedNativeEntry(NativeObject addr,
                                          Address canonical_addr) {
  base::HashMap::Entry* entry =
      entries_map_.Lookup(reinterpret_cast<void*>(canonical_addr),
                          ComputeAddressHash(canonical_addr));
  auto result = merged_native_entries_map_.insert(
      {addr, reinterpret_cast<size_t>(entry->value)});
  if (!result.second) {
    result.first->second = reinterpret_cast<size_t>(entry->value);
  }
}

void HeapObjectsMap::StopHeapObjectsTracking() { time_intervals_.clear(); }

void HeapObjectsMap::UpdateHeapObjectsMap() {
  if (v8_flags.heap_profiler_trace_objects) {
    PrintF("Begin HeapObjectsMap::UpdateHeapObjectsMap. map has %d entries.\n",
           entries_map_.occupancy());
  }
  heap_->PreciseCollectAllGarbage(GCFlag::kNoFlags,
                                  GarbageCollectionReason::kHeapProfiler);
  PtrComprCageBase cage_base(heap_->isolate());
  CombinedHeapObjectIterator iterator(heap_);
  for (Tagged<HeapObject> obj = iterator.Next(); !obj.is_null();
       obj = iterator.Next()) {
    int object_size = obj->Size(cage_base);
    FindOrAddEntry(obj.address(), object_size);
    if (v8_flags.heap_profiler_trace_objects) {
      PrintF("Update object      : %p %6d. Next address is %p\n",
             reinterpret_cast<void*>(obj.address()), object_size,
             reinterpret_cast<void*>(obj.address() + object_size));
    }
  }
  RemoveDeadEntries();
  if (v8_flags.heap_profiler_trace_objects) {
    PrintF("End HeapObjectsMap::UpdateHeapObjectsMap. map has %d entries.\n",
           entries_map_.occupancy());
  }
}

SnapshotObjectId HeapObjectsMap::PushHeapObjectsStats(OutputStream* stream,
                                                      int64_t* timestamp_us) {
  UpdateHeapObjectsMap();
  time_intervals_.emplace_back(next_id_);
  int prefered_chunk_size = stream->GetChunkSize();
  std::vector<v8::HeapStatsUpdate> stats_buffer;
  DCHECK(!entries_.empty());
  EntryInfo* entry_info = &entries_.front();
  EntryInfo* end_entry_info = &entries_.back() + 1;
  for (size_t time_interval_index = 0;
       time_interval_index < time_intervals_.size(); ++time_interval_index) {
    TimeInterval& time_interval = time_intervals_[time_interval_index];
    SnapshotObjectId time_interval_id = time_interval.id;
    uint32_t entries_size = 0;
    EntryInfo* start_entry_info = entry_info;
    while (entry_info < end_entry_info && entry_info->id < time_interval_id) {
      entries_size += entry_info->size;
      ++entry_info;
    }
    uint32_t entries_count =
        static_cast<uint32_t>(entry_info - start_entry_info);
    if (time_interval.count != entries_count ||
        time_interval.size != entries_size) {
      stats_buffer.emplace_back(static_cast<uint32_t>(time_interval_index),
                                time_interval.count = entries_count,
                                time_interval.size = entries_size);
      if (static_cast<int>(stats_buffer.size()) >= prefered_chunk_size) {
        OutputStream::WriteResult result = stream->WriteHeapStatsChunk(
            &stats_buffer.front(), static_cast<int>(stats_buffer.size()));
        if (result == OutputStream::kAbort) return last_assigned_id();
        stats_buffer.clear();
      }
    }
  }
  DCHECK(entry_info == end_entry_info);
  if (!stats_buffer.empty()) {
    OutputStream::WriteResult result = stream->WriteHeapStatsChunk(
        &stats_buffer.front(), static_cast<int>(stats_buffer.size()));
    if (result == OutputStream::kAbort) return last_assigned_id();
  }
  stream->EndOfStream();
  if (timestamp_us) {
    *timestamp_us =
        (time_intervals_.back().timestamp - time_intervals_.front().timestamp)
            .InMicroseconds();
  }
  return last_assigned_id();
}

void HeapObjectsMap::RemoveDeadEntries() {
  DCHECK(entries_.size() > 0 && entries_.at(0).id == 0 &&
         entries_.at(0).addr == kNullAddress);

  // Build up temporary reverse map.
  std::unordered_map<size_t, NativeObject> reverse_merged_native_entries_map;
  for (const auto& it : merged_native_entries_map_) {
    auto result =
        reverse_merged_native_entries_map.emplace(it.second, it.first);
    DCHECK(result.second);
    USE(result);
  }

  size_t first_free_entry = 1;
  for (size_t i = 1; i < entries_.size(); ++i) {
    EntryInfo& entry_info = entries_.at(i);
    auto merged_reverse_it = reverse_merged_native_entries_map.find(i);
    if (entry_info.accessed) {
      if (first_free_entry != i) {
        entries_.at(first_free_entry) = entry_info;
      }
      entries_.at(first_free_entry).accessed = false;
      base::HashMap::Entry* entry =
          entries_map_.Lookup(reinterpret_cast<void*>(entry_info.addr),
                              ComputeAddressHash(entry_info.addr));
      DCHECK(entry);
      entry->value = reinterpret_cast<void*>(first_free_entry);
      if (merged_reverse_it != reverse_merged_native_entries_map.end()) {
        auto it = merged_native_entries_map_.find(merged_reverse_it->second);
        DCHECK_NE(merged_native_entries_map_.end(), it);
        it->second = first_free_entry;
      }
      ++first_free_entry;
    } else {
      if (entry_info.addr) {
        entries_map_.Remove(reinterpret_cast<void*>(entry_info.addr),
                            ComputeAddressHash(entry_info.addr));
        if (merged_reverse_it != reverse_merged_native_entries_map.end()) {
          merged_native_entries_map_.erase(merged_reverse_it->second);
        }
      }
    }
  }
  entries_.erase(entries_.begin() + first_free_entry, entries_.end());

  DCHECK(static_cast<uint32_t>(entries_.size()) - 1 ==
         entries_map_.occupancy());
}

V8HeapExplorer::V8HeapExplorer(HeapSnapshot* snapshot,
                               SnapshottingProgressReportingInterface* progress,
                               v8::HeapProfiler::ObjectNameResolver* resolver)
    : heap_(snapshot->profiler()->heap_object_map()->heap()),
      snapshot_(snapshot),
      names_(snapshot_->profiler()->names()),
      heap_object_map_(snapshot_->profiler()->heap_object_map()),
      progress_(progress),
      generator_(nullptr),
      global_object_name_resolver_(resolver) {}

HeapEntry* V8HeapExplorer::AllocateEntry(HeapThing ptr) {
  return AddEntry(
      Cast<HeapObject>(Tagged<Object>(reinterpret_cast<Address>(ptr))));
}

HeapEntry* V8HeapExplorer::AllocateEntry(Tagged<Smi> smi) {
  SnapshotObjectId id = heap_object_map_->get_next_id();
  HeapEntry* entry =
      snapshot_->AddEntry(HeapEntry::kHeapNumber, "smi number", id, 0, 0);
  // XXX: Smis do not appear in CombinedHeapObjectIterator, so we need to
  // extract the references here
  ExtractNumberReference(entry, smi);
  return entry;
}

Tagged<JSFunction> V8HeapExplorer::GetLocationFunction(
    Tagged<HeapObject> object) {
  DisallowHeapAllocation no_gc;

  if (IsJSFunction(object)) {
    return Cast<JSFunction>(object);
  } else if (IsJSGeneratorObject(object)) {
    Tagged<JSGeneratorObject> gen = Cast<JSGeneratorObject>(object);
    return gen->function();
  } else if (IsJSObject(object)) {
    Tagged<JSObject> obj = Cast<JSObject>(object);
    Tagged<JSFunction> maybe_constructor =
        GetConstructor(heap_->isolate(), obj);

    return maybe_constructor;
  }

  return JSFunction();
}

void V8HeapExplorer::ExtractLocation(HeapEntry* entry,
                                     Tagged<HeapObject> object) {
  DisallowHeapAllocation no_gc;
  Tagged<JSFunction> func = GetLocationFunction(object);
  if (!func.is_null()) {
    ExtractLocationForJSFunction(entry, func);
  }
}

void V8HeapExplorer::ExtractLocationForJSFunction(HeapEntry* entry,
                                                  Tagged<JSFunction> func) {
  if (!IsScript(func->shared()->script())) return;
  Tagged<Script> script = Cast<Script>(func->shared()->script());
  int scriptId = script->id();
  int start = func->shared()->StartPosition();
  Script::PositionInfo info;
  if (script->has_line_ends()) {
    script->GetPositionInfo(start, &info);
  } else {
    script->GetPositionInfoWithLineEnds(
        start, &info, snapshot_->GetScriptLineEnds(script->id()));
  }
  snapshot_->AddLocation(entry, scriptId, info.line, info.column);
}

namespace {
// Templatized struct to statically generate the string "system / Managed<Foo>"
// from "kFooTag".
template <const char kTagNameCStr[]>
struct ManagedName {
  static constexpr std::string_view kTagName = kTagNameCStr;
  static_assert(kTagName.starts_with("k"));
  static_assert(kTagName.ends_with("Tag"));

  static constexpr std::string_view prefix = "system / Managed<";
  static constexpr std::string_view suffix = ">";

  // We strip four characters, but add prefix and suffix and null termination.
  static constexpr size_t kManagedNameLength =
      kTagName.size() - 4 + prefix.size() + suffix.size() + 1;

  static constexpr auto str_arr =
      base::make_array<kManagedNameLength>([](std::size_t i) {
        if (i < prefix.size()) return prefix[i];
        if (i == kManagedNameLength - 2) return suffix[0];
        if (i == kManagedNameLength - 1) return '\0';
        return kTagName[i - prefix.size() + 1];
      });

  // Ignore "kFirstManagedResourceTag".
  static constexpr bool ignore_me = kTagName == "kFirstManagedResourceTag";
};

// A little inline test:
constexpr const char kTagNameForTesting[] = "kFooTag";
static_assert(std::string_view{
                  ManagedName<kTagNameForTesting>::str_arr.data()} ==
              std::string_view{"system / Managed<Foo>"});
}  // namespace

HeapEntry* V8HeapExplorer::AddEntry(Tagged<HeapObject> object) {
  PtrComprCageBase cage_base(isolate());
  InstanceType instance_type = object->map(cage_base)->instance_type();
  if (InstanceTypeChecker::IsJSObject(instance_type)) {
    if (InstanceTypeChecker::IsJSFunction(instance_type)) {
      Tagged<JSFunction> func = Cast<JSFunction>(object);
      Tagged<SharedFunctionInfo> shared = func->shared();
      const char* name = names_->GetName(shared->Name());
      return AddEntry(object, HeapEntry::kClosure, name);

    } else if (InstanceTypeChecker::IsJSBoundFunction(instance_type)) {
      return AddEntry(object, HeapEntry::kClosure, "native_bind");
    }
    if (InstanceTypeChecker::IsJSRegExp(instance_type)) {
      Tagged<JSRegExp> re = Cast<JSRegExp>(object);
      return AddEntry(object, HeapEntry::kRegExp,
                      names_->GetName(re->source()));
    }
    // TODO(v8:12674) Fix and run full gcmole.
    DisableGCMole no_gcmole;
    const char* name = names_->GetName(
        GetConstructorName(heap_->isolate(), Cast<JSObject>(object)));
    if (InstanceTypeChecker::IsJSGlobalObject(instance_type)) {
      auto it = global_object_tag_map_.find(Cast<JSGlobalObject>(object));
      if (it != global_object_tag_map_.end()) {
        name = names_->GetFormatted("%s / %s", name, it->second);
      }
    }
    return AddEntry(object, HeapEntry::kObject, name);

  } else if (InstanceTypeChecker::IsString(instance_type)) {
    Tagged<String> string = Cast<String>(object);
    if (IsConsString(string, cage_base)) {
      return AddEntry(object, HeapEntry::kConsString, "(concatenated string)");
    } else if (IsSlicedString(string, cage_base)) {
      return AddEntry(object, HeapEntry::kSlicedString, "(sliced string)");
    } else {
      return AddEntry(object, HeapEntry::kString,
                      names_->GetName(Cast<String>(object)));
    }
  } else if (InstanceTypeChecker::IsSymbol(instance_type)) {
    if (Cast<Symbol>(object)->is_private())
      return AddEntry(object, HeapEntry::kHidden, "private symbol");
    else
      return AddEntry(object, HeapEntry::kSymbol, "symbol");

  } else if (InstanceTypeChecker::IsBigInt(instance_type)) {
    return AddEntry(object, HeapEntry::kBigInt, "bigint");

  } else if (InstanceTypeChecker::IsInstructionStream(instance_type) ||
             InstanceTypeChecker::IsCode(instance_type)) {
    return AddEntry(object, HeapEntry::kCode, "");

  } else if (InstanceTypeChecker::IsSharedFunctionInfo(instance_type)) {
    Tagged<String> name = Cast<SharedFunctionInfo>(object)->Name();
    return AddEntry(object, HeapEntry::kCode, names_->GetName(name));

  } else if (InstanceTypeChecker::IsScript(instance_type)) {
    Tagged<Object> name = Cast<Script>(object)->name();
    return AddEntry(object, HeapEntry::kCode,
                    IsString(name) ? names_->GetName(Cast<String>(name)) : "");

  } else if (InstanceTypeChecker::IsNativeContext(instance_type)) {
    return AddEntry(object, HeapEntry::kHidden, "system / NativeContext");

  } else if (InstanceTypeChecker::IsContext(instance_type)) {
    return AddEntry(object, HeapEntry::kObject, "system / Context");

  } else if (InstanceTypeChecker::IsHeapNumber(instance_type)) {
    return AddEntry(object, HeapEntry::kHeapNumber, "heap number");
  }
#if V8_ENABLE_WEBASSEMBLY
  if (InstanceTypeChecker::IsWasmObject(instance_type)) {
    Tagged<WasmTypeInfo> info = object->map()->wasm_type_info();
    // Getting the trusted data is safe; structs and arrays always have their
    // trusted data defined.
    wasm::NamesProvider* names =
        info->trusted_data(isolate())->native_module()->GetNamesProvider();
    wasm::StringBuilder sb;
    names->PrintTypeName(sb, info->type_index());
    sb << " (wasm)" << '\0';
    const char* name = names_->GetCopy(sb.start());
    return AddEntry(object, HeapEntry::kObject, name);
  }
  if (InstanceTypeChecker::IsWasmNull(instance_type)) {
    // Inlined copies of {GetSystemEntryType}, {GetSystemEntryName}, and
    // {AddEntry}, allowing us to override the size.
    // The actual object's size is fairly large (at the time of this writing,
    // just over 64 KB) and mostly includes a guard region. We report it as
    // much smaller to avoid confusion.
    static constexpr size_t kSize = WasmNull::kHeaderSize;
    return AddEntry(object.address(), HeapEntry::kHidden, "system / WasmNull",
                    kSize);
  }
#endif  // V8_ENABLE_WEBASSEMBLY

  if (InstanceTypeChecker::IsForeign(instance_type)) {
    Tagged<Foreign> foreign = Cast<Foreign>(object);
    ExternalPointerTag tag = foreign->GetTag();
    if (tag >= kFirstManagedResourceTag && tag < kLastManagedResourceTag) {
      // First handle special cases with more information.
#if V8_ENABLE_WEBASSEMBLY
      if (tag == kWasmNativeModuleTag) {
        wasm::NativeModule* native_module =
            Cast<Managed<wasm::NativeModule>>(foreign)->raw();
        size_t size = native_module->EstimateCurrentMemoryConsumption();
        return AddEntry(object.address(), HeapEntry::kHidden,
                        "system / Managed<wasm::NativeModule>", size);
      }
#endif  // V8_ENABLE_WEBASSEMBLY
#define MANAGED_TAG(name, ...)                                \
  if (tag == name) {                                          \
    static constexpr const char kTagName[] = #name;           \
    if constexpr (!ManagedName<kTagName>::ignore_me) {        \
      return AddEntry(object, HeapEntry::kHidden,             \
                      ManagedName<kTagName>::str_arr.data()); \
    }                                                         \
  }
      PER_ISOLATE_EXTERNAL_POINTER_TAGS(MANAGED_TAG)
#undef MANAGED_TAG
    }
  }

  return AddEntry(object, GetSystemEntryType(object),
                  GetSystemEntryName(object));
}

HeapEntry* V8HeapExplorer::AddEntry(Tagged<HeapObject> object,
                                    HeapEntry::Type type, const char* name) {
  PtrComprCageBase cage_base(isolate());
  return AddEntry(object.address(), type, name, object->Size(cage_base));
}

HeapEntry* V8HeapExplorer::AddEntry(Address address, HeapEntry::Type type,
                                    const char* name, size_t size) {
  if (v8_flags.heap_profiler_show_hidden_objects &&
      type == HeapEntry::kHidden) {
    type = HeapEntry::kNative;
  }
  SnapshotObjectId object_id = heap_object_map_->FindOrAddEntry(
      address, static_cast<unsigned int>(size));
  unsigned trace_node_id = 0;
  if (AllocationTracker* allocation_tracker =
          snapshot_->profiler()->allocation_tracker()) {
    trace_node_id =
        allocation_tracker->address_to_trace()->GetTraceNodeId(address);
  }
  return snapshot_->AddEntry(type, name, object_id, size, trace_node_id);
}

const char* V8HeapExplorer::GetSystemEntryName(Tagged<HeapObject> object) {
  if (IsMap(object)) {
    switch (Cast<Map>(object)->instance_type()) {
#define MAKE_STRING_MAP_CASE(instance_type, size, name, Name) \
  case instance_type:                                         \
    return "system / Map (" #Name ")";
      STRING_TYPE_LIST(MAKE_STRING_MAP_CASE)
#undef MAKE_STRING_MAP_CASE
      default:
        return "system / Map";
    }
  }

  InstanceType type = object->map()->instance_type();

  // Empty string names are special: TagObject can overwrite them, and devtools
  // will report them as "(internal array)".
  if (InstanceTypeChecker::IsFixedArray(type) ||
      InstanceTypeChecker::IsFixedDoubleArray(type) ||
      InstanceTypeChecker::IsByteArray(type)) {
    return "";
  }

  switch (type) {
#define MAKE_TORQUE_CASE(Name, TYPE) \
  case TYPE:                         \
    return "system / " #Name;
    // The following lists include every non-String instance type.
    // This includes a few types that already have non-"system" names assigned
    // by AddEntry, but this is a convenient way to avoid manual upkeep here.
    TORQUE_INSTANCE_CHECKERS_SINGLE_FULLY_DEFINED(MAKE_TORQUE_CASE)
    TORQUE_INSTANCE_CHECKERS_MULTIPLE_FULLY_DEFINED(MAKE_TORQUE_CASE)
    TORQUE_INSTANCE_CHECKERS_SINGLE_ONLY_DECLARED(MAKE_TORQUE_CASE)
    TORQUE_INSTANCE_CHECKERS_MULTIPLE_ONLY_DECLARED(MAKE_TORQUE_CASE)
#undef MAKE_TORQUE_CASE

    // Strings were already handled by AddEntry.
#define MAKE_STRING_CASE(instance_type, size, name, Name) \
  case instance_type:                                     \
    UNREACHABLE();
    STRING_TYPE_LIST(MAKE_STRING_CASE)
#undef MAKE_STRING_CASE
  }
}

HeapEntry::Type V8HeapExplorer::GetSystemEntryType(Tagged<HeapObject> object) {
  InstanceType type = object->map()->instance_type();
  if (InstanceTypeChecker::IsAllocationSite(type) ||
      InstanceTypeChecker::IsArrayBoilerplateDescription(type) ||
      InstanceTypeChecker::IsBytecodeArray(type) ||
      InstanceTypeChecker::IsClosureFeedbackCellArray(type) ||
      InstanceTypeChecker::IsCode(type) ||
      InstanceTypeChecker::IsFeedbackCell(type) ||
      InstanceTypeChecker::IsFeedbackMetadata(type) ||
      InstanceTypeChecker::IsFeedbackVector(type) ||
      InstanceTypeChecker::IsInstructionStream(type) ||
      InstanceTypeChecker::IsInterpreterData(type) ||
      InstanceTypeChecker::IsLoadHandler(type) ||
      InstanceTypeChecker::IsObjectBoilerplateDescription(type) ||
      InstanceTypeChecker::IsPreparseData(type) ||
      InstanceTypeChecker::IsRegExpBoilerplateDescription(type) ||
      InstanceTypeChecker::IsScopeInfo(type) ||
      InstanceTypeChecker::IsStoreHandler(type) ||
      InstanceTypeChecker::IsTemplateObjectDescription(type) ||
      InstanceTypeChecker::IsTurbofanType(type) ||
      InstanceTypeChecker::IsUncompiledData(type)) {
    return HeapEntry::kCode;
  }

  // This check must come second, because some subtypes of FixedArray are
  // determined above to represent code content.
  if (InstanceTypeChecker::IsFixedArray(type) ||
      InstanceTypeChecker::IsFixedDoubleArray(type) ||
      InstanceTypeChecker::IsByteArray(type)) {
    return HeapEntry::kArray;
  }

  // Maps in read-only space are for internal V8 data, not user-defined object
  // shapes.
  if ((InstanceTypeChecker::IsMap(type) &&
       !MemoryChunk::FromHeapObject(object)->InReadOnlySpace()) ||
      InstanceTypeChecker::IsDescriptorArray(type) ||
      InstanceTypeChecker::IsTransitionArray(type) ||
      InstanceTypeChecker::IsPrototypeInfo(type) ||
      InstanceTypeChecker::IsEnumCache(type)) {
    return HeapEntry::kObjectShape;
  }

  return HeapEntry::kHidden;
}

void V8HeapExplorer::PopulateLineEnds() {
  std::vector<Handle<Script>> scripts;
  HandleScope scope(isolate());

  {
    Script::Iterator iterator(isolate());
    for (Tagged<Script> script = iterator.Next(); !script.is_null();
         script = iterator.Next()) {
      if (!script->has_line_ends()) {
        scripts.push_back(handle(script, isolate()));
      }
    }
  }

  for (auto& script : scripts) {
    snapshot_->AddScriptLineEnds(script->id(),
                                 Script::GetLineEnds(isolate(), script));
  }
}

uint32_t V8HeapExplorer::EstimateObjectsCount() {
  CombinedHeapObjectIterator it(heap_, HeapObjectIterator::kNoFiltering);
  uint32_t objects_count = 0;
  // Avoid overflowing the objects count. In worst case, we will show the same
  // progress for a longer period of time, but we do not expect to have that
  // many objects.
  while (!it.Next().is_null() &&
         objects_count != std::numeric_limits<uint32_t>::max())
    ++objects_count;
  return objects_count;
}

#ifdef V8_TARGET_BIG_ENDIAN
namespace {
int AdjustEmbedderFieldIndex(Tagged<HeapObject> heap_obj, int field_index) {
  Tagged<Map> map = heap_obj->map();
  if (JSObject::MayHaveEmbedderFields(map)) {
    int emb_start_index = (JSObject::GetEmbedderFieldsStartOffset(map) +
                           EmbedderDataSlot::kTaggedPayloadOffset) /
                          kTaggedSize;
    int emb_field_count = JSObject::GetEmbedderFieldCount(map);
    int emb_end_index = emb_start_index + emb_field_count;
    if (base::IsInRange(field_index, emb_start_index, emb_end_index)) {
      return -EmbedderDataSlot::kTaggedPayloadOffset / kTaggedSize;
    }
  }
  return 0;
}
}  // namespace
#endif  // V8_TARGET_BIG_ENDIAN
class IndexedReferencesExtractor : public ObjectVisitorWithCageBases {
 public:
  IndexedReferencesExtractor(V8HeapExplorer* generator,
                             Tagged<HeapObject> parent_obj, HeapEntry* parent)
      : ObjectVisitorWithCageBases(generator->isolate()),
        generator_(generator),
        parent_obj_(parent_obj),
        parent_start_(parent_obj_->RawMaybeWeakField(0)),
        parent_end_(
            parent_obj_->RawMaybeWeakField(parent_obj_->Size(cage_base()))),
        parent_(parent),
        next_index_(0) {}
  void VisitPointers(Tagged<HeapObject> host, ObjectSlot start,
                     ObjectSlot end) override {
    VisitPointers(host, MaybeObjectSlot(start), MaybeObjectSlot(end));
  }
  void VisitMapPointer(Tagged<HeapObject> object) override {
    VisitSlotImpl(cage_base(), object->map_slot());
  }
  void VisitPointers(Tagged<HeapObject> host, MaybeObjectSlot start,
                     MaybeObjectSlot end) override {
    // [start,end) must be a sub-region of [parent_start_, parent_end), i.e.
    // all the slots must point inside the object.
    CHECK_LE(parent_start_, start);
    CHECK_LE(end, parent_end_);
    for (MaybeObjectSlot slot = start; slot < end; ++slot) {
      VisitSlotImpl(cage_base(), slot);
    }
  }

  void VisitInstructionStreamPointer(Tagged<Code> host,
                                     InstructionStreamSlot slot) override {
    VisitSlotImpl(code_cage_base(), slot);
  }

  void VisitCodeTarget(Tagged<InstructionStream> host,
                       RelocInfo* rinfo) override {
    Tagged<InstructionStream> target =
        InstructionStream::FromTargetAddress(rinfo->target_address());
    VisitHeapObjectImpl(target, -1);
  }

  void VisitEmbeddedPointer(Tagged<InstructionStream> host,
                            RelocInfo* rinfo) override {
    Tagged<HeapObject> object = rinfo->target_object(cage_base());
    Tagged<Code> code = UncheckedCast<Code>(host->raw_code(kAcquireLoad));
    if (code->IsWeakObject(object)) {
      generator_->SetWeakReference(parent_, next_index_++, object, {});
    } else {
      VisitHeapObjectImpl(object, -1);
    }
  }

  void VisitIndirectPointer(Tagged<HeapObject> host, IndirectPointerSlot slot,
                            IndirectPointerMode mode) override {
    VisitSlotImpl(generator_->isolate(), slot);
  }

  void VisitProtectedPointer(Tagged<TrustedObject> host,
                             ProtectedPointerSlot slot) override {
    // TODO(saelo): the cage base doesn't currently matter as it isn't used,
    // but technically we should either use the trusted cage base here or
    // remove the cage_base parameter.
    const PtrComprCageBase unused_cage_base(kNullAddress);
    VisitSlotImpl(unused_cage_base, slot);
  }

 private:
  template <typename TIsolateOrCageBase, typename TSlot>
  V8_INLINE void VisitSlotImpl(TIsolateOrCageBase isolate_or_cage_base,
                               TSlot slot) {
    int field_index =
        static_cast<int>(slot.address() - parent_start_.address()) /
        TSlot::kSlotDataSize;
#ifdef V8_TARGET_BIG_ENDIAN
    field_index += AdjustEmbedderFieldIndex(parent_obj_, field_index);
#endif
    DCHECK_GE(field_index, 0);
    if (generator_->visited_fields_[field_index]) {
      generator_->visited_fields_[field_index] = false;
    } else {
      Tagged<HeapObject> heap_object;
      auto loaded_value = slot.load(isolate_or_cage_base);
      if (loaded_value.GetHeapObjectIfStrong(&heap_object)) {
        VisitHeapObjectImpl(heap_object, field_index);
      } else if (loaded_value.GetHeapObjectIfWeak(&heap_object)) {
        generator_->SetWeakReference(parent_, next_index_++, heap_object, {});
      }
    }
  }

  V8_INLINE void VisitHeapObjectImpl(Tagged<HeapObject> heap_object,
                                     int field_index) {
    DCHECK_LE(-1, field_index);
    // The last parameter {field_offset} is only used to check some well-known
    // skipped references, so passing -1 * kTaggedSize for objects embedded
    // into code is fine.
    generator_->SetHiddenReference(parent_obj_, parent_, next_index_++,
                                   heap_object, field_index * kTaggedSize);
  }

  V8HeapExplorer* generator_;
  Tagged<HeapObject> parent_obj_;
  MaybeObjectSlot parent_start_;
  MaybeObjectSlot parent_end_;
  HeapEntry* parent_;
  int next_index_;
};

void V8HeapExplorer::ExtractReferences(HeapEntry* entry,
                                       Tagged<HeapObject> obj) {
  if (IsJSGlobalProxy(obj)) {
    ExtractJSGlobalProxyReferences(entry, Cast<JSGlobalProxy>(obj));
  } else if (IsJSArrayBuffer(obj)) {
    ExtractJSArrayBufferReferences(entry, Cast<JSArrayBuffer>(obj));
  } else if (IsJSObject(obj)) {
    if (IsJSWeakSet(obj)) {
      ExtractJSWeakCollectionReferences(entry, Cast<JSWeakSet>(obj));
    } else if (IsJSWeakMap(obj)) {
      ExtractJSWeakCollectionReferences(entry, Cast<JSWeakMap>(obj));
    } else if (IsJSSet(obj)) {
      ExtractJSCollectionReferences(entry, Cast<JSSet>(obj));
    } else if (IsJSMap(obj)) {
      ExtractJSCollectionReferences(entry, Cast<JSMap>(obj));
    } else if (IsJSPromise(obj)) {
      ExtractJSPromiseReferences(entry, Cast<JSPromise>(obj));
    } else if (IsJSGeneratorObject(obj)) {
      ExtractJSGeneratorObjectReferences(entry, Cast<JSGeneratorObject>(obj));
    } else if (IsJSWeakRef(obj)) {
      ExtractJSWeakRefReferences(entry, Cast<JSWeakRef>(obj));
#if V8_ENABLE_WEBASSEMBLY
    } else if (IsWasmInstanceObject(obj)) {
      ExtractWasmInstanceObjectReferences(Cast<WasmInstanceObject>(obj), entry);
    } else if (IsWasmModuleObject(obj)) {
      ExtractWasmModuleObjectReferences(Cast<WasmModuleObject>(obj), entry);
#endif  // V8_ENABLE_WEBASSEMBLY
    }
    ExtractJSObjectReferences(entry, Cast<JSObject>(obj));
  } else if (IsString(obj)) {
    ExtractStringReferences(entry, Cast<String>(obj));
  } else if (IsSymbol(obj)) {
    ExtractSymbolReferences(entry, Cast<Symbol>(obj));
  } else if (IsMap(obj)) {
    ExtractMapReferences(entry, Cast<Map>(obj));
  } else if (IsSharedFunctionInfo(obj)) {
    ExtractSharedFunctionInfoReferences(entry, Cast<SharedFunctionInfo>(obj));
  } else if (IsScript(obj)) {
    ExtractScriptReferences(entry, Cast<Script>(obj));
  } else if (IsAccessorInfo(obj)) {
    ExtractAccessorInfoReferences(entry, Cast<AccessorInfo>(obj));
  } else if (IsAccessorPair(obj)) {
    ExtractAccessorPairReferences(entry, Cast<AccessorPair>(obj));
  } else if (IsCode(obj)) {
    ExtractCodeReferences(entry, Cast<Code>(obj));
  } else if (IsInstructionStream(obj)) {
    ExtractInstructionStreamReferences(entry, Cast<InstructionStream>(obj));
  } else if (IsCell(obj)) {
    ExtractCellReferences(entry, Cast<Cell>(obj));
  } else if (IsFeedbackCell(obj)) {
    ExtractFeedbackCellReferences(entry, Cast<FeedbackCell>(obj));
  } else if (IsPropertyCell(obj)) {
    ExtractPropertyCellReferences(entry, Cast<PropertyCell>(obj));
  } else if (IsPrototypeInfo(obj)) {
    ExtractPrototypeInfoReferences(entry, Cast<PrototypeInfo>(obj));
  } else if (IsAllocationSite(obj)) {
    ExtractAllocationSiteReferences(entry, Cast<AllocationSite>(obj));
  } else if (IsArrayBoilerplateDescription(obj)) {
    ExtractArrayBoilerplateDescriptionReferences(
        entry, Cast<ArrayBoilerplateDescription>(obj));
  } else if (IsRegExpBoilerplateDescription(obj)) {
    ExtractRegExpBoilerplateDescriptionReferences(
        entry, Cast<RegExpBoilerplateDescription>(obj));
  } else if (IsFeedbackVector(obj)) {
    ExtractFeedbackVectorReferences(entry, Cast<FeedbackVector>(obj));
  } else if (IsDescriptorArray(obj)) {
    ExtractDescriptorArrayReferences(entry, Cast<DescriptorArray>(obj));
  } else if (IsEnumCache(obj)) {
    ExtractEnumCacheReferences(entry, Cast<EnumCache>(obj));
  } else if (IsTransitionArray(obj)) {
    ExtractTransitionArrayReferences(entry, Cast<TransitionArray>(obj));
  } else if (IsWeakFixedArray(obj)) {
    ExtractWeakArrayReferences(WeakFixedArray::kHeaderSize, entry,
                               Cast<WeakFixedArray>(obj));
  } else if (IsWeakArrayList(obj)) {
    ExtractWeakArrayReferences(WeakArrayList::kHeaderSize, entry,
                               Cast<WeakArrayList>(obj));
  } else if (IsContext(obj)) {
    ExtractContextReferences(entry, Cast<Context>(obj));
  } else if (IsEphemeronHashTable(obj)) {
    ExtractEphemeronHashTableReferences(entry, Cast<EphemeronHashTable>(obj));
  } else if (IsFixedArray(obj)) {
    ExtractFixedArrayReferences(entry, Cast<FixedArray>(obj));
  } else if (IsWeakCell(obj)) {
    ExtractWeakCellReferences(entry, Cast<WeakCell>(obj));
  } else if (IsHeapNumber(obj)) {
    if (snapshot_->capture_numeric_value()) {
      ExtractNumberReference(entry, obj);
    }
  } else if (IsBytecodeArray(obj)) {
    ExtractBytecodeArrayReferences(entry, Cast<BytecodeArray>(obj));
  } else if (IsScopeInfo(obj)) {
    ExtractScopeInfoReferences(entry, Cast<ScopeInfo>(obj));
#if V8_ENABLE_WEBASSEMBLY
  } else if (IsWasmStruct(obj)) {
    ExtractWasmStructReferences(Cast<WasmStruct>(obj), entry);
  } else if (IsWasmArray(obj)) {
    ExtractWasmArrayReferences(Cast<WasmArray>(obj), entry);
  } else if (IsWasmTrustedInstanceData(obj)) {
    ExtractWasmTrustedInstanceDataReferences(Cast<WasmTrustedInstanceData>(obj),
                                             entry);
#endif  // V8_ENABLE_WEBASSEMBLY
  }
}

void V8HeapExplorer::ExtractJSGlobalProxyReferences(
    HeapEntry* entry, Tagged<JSGlobalProxy> proxy) {}

void V8HeapExplorer::ExtractJSObjectReferences(HeapEntry* entry,
                                               Tagged<JSObject> js_obj) {
  Tagged<HeapObject> obj = js_obj;
  ExtractPropertyReferences(js_obj, entry);
  ExtractElementReferences(js_obj, entry);
  ExtractInternalReferences(js_obj, entry);
  Isolate* isolate = Isolate::FromHeap(heap_);
  PrototypeIterator iter(isolate, js_obj);
  ReadOnlyRoots roots(isolate);
  SetPropertyReference(entry, roots.proto_string(), iter.GetCurrent());
  if (IsJSBoundFunction(obj)) {
    Tagged<JSBoundFunction> js_fun = Cast<JSBoundFunction>(obj);
    TagObject(js_fun->bound_arguments(), "(bound arguments)");
    SetInternalReference(entry, "bindings", js_fun->bound_arguments(),
                         JSBoundFunction::kBoundArgumentsOffset);
    SetInternalReference(entry, "bound_this", js_fun->bound_this(),
                         JSBoundFunction::kBoundThisOffset);
    SetInternalReference(entry, "bound_function",
                         js_fun->bound_target_function(),
                         JSBoundFunction::kBoundTargetFunctionOffset);
    Tagged<FixedArray> bindings = js_fun->bound_arguments();
    for (int i = 0; i < bindings->length(); i++) {
      const char* reference_name = names_->GetFormatted("bound_argument_%d", i);
      SetNativeBindReference(entry, reference_name, bindings->get(i));
    }
  } else if (IsJSFunction(obj)) {
    Tagged<JSFunction> js_fun = Cast<JSFunction>(js_obj);
    if (js_fun->has_prototype_slot()) {
      Tagged<Object> proto_or_map =
          js_fun->prototype_or_initial_map(kAcquireLoad);
      if (!IsTheHole(proto_or_map, isolate)) {
        if (!IsMap(proto_or_map)) {
          SetPropertyReference(entry, roots.prototype_string(), proto_or_map,
                               nullptr,
                               JSFunction::kPrototypeOrInitialMapOffset);
        } else {
          SetPropertyReference(entry, roots.prototype_string(),
                               js_fun->prototype());
          SetInternalReference(entry, "initial_map", proto_or_map,
                               JSFunction::kPrototypeOrInitialMapOffset);
        }
      }
    }
    Tagged<SharedFunctionInfo> shared_info = js_fun->shared();
    TagObject(js_fun->raw_feedback_cell(), "(function feedback cell)");
    SetInternalReference(entry, "feedback_cell", js_fun->raw_feedback_cell(),
                         JSFunction::kFeedbackCellOffset);
    TagObject(shared_info, "(shared function info)");
    SetInternalReference(entry, "shared", shared_info,
                         JSFunction::kSharedFunctionInfoOffset);
    TagObject(js_fun->context(), "(context)");
    SetInternalReference(entry, "context", js_fun->context(),
                         JSFunction::kContextOffset);
    SetInternalReference(entry, "code", js_fun->code(isolate),
                         JSFunction::kCodeOffset);
  } else if (IsJSGlobalObject(obj)) {
    Tagged<JSGlobalObject> global_obj = Cast<JSGlobalObject>(obj);
    SetInternalReference(entry, "global_proxy", global_obj->global_proxy(),
                         JSGlobalObject::kGlobalProxyOffset);
  } else if (IsJSArrayBufferView(obj)) {
    Tagged<JSArrayBufferView> view = Cast<JSArrayBufferView>(obj);
    SetInternalReference(entry, "buffer", view->buffer(),
                         JSArrayBufferView::kBufferOffset);
  }

  TagObject(js_obj->raw_properties_or_hash(), "(object properties)");
  SetInternalReference(entry, "properties", js_obj->raw_properties_or_hash(),
                       JSObject::kPropertiesOrHashOffset);

  TagObject(js_obj->elements(), "(object elements)");
  SetInternalReference(entry, "elements", js_obj->elements(),
                       JSObject::kElementsOffset);
}

void V8HeapExplorer::ExtractStringReferences(HeapEntry* entry,
                                             Tagged<String> string) {
  if (IsConsString(string)) {
    Tagged<ConsString> cs = Cast<ConsString>(string);
    SetInternalReference(entry, "first", cs->first(),
                         offsetof(ConsString, first_));
    SetInternalReference(entry, "second", cs->second(),
                         offsetof(ConsString, second_));
  } else if (IsSlicedString(string)) {
    Tagged<SlicedString> ss = Cast<SlicedString>(string);
    SetInternalReference(entry, "parent", ss->parent(),
                         offsetof(SlicedString, parent_));
  } else if (IsThinString(string)) {
    Tagged<ThinString> ts = Cast<ThinString>(string);
    SetInternalReference(entry, "actual", ts->actual(),
                         offsetof(ThinString, actual_));
  }
}

void V8HeapExplorer::ExtractSymbolReferences(HeapEntry* entry,
                                             Tagged<Symbol> symbol) {
  SetInternalReference(entry, "name", symbol->description(),
                       offsetof(Symbol, description_));
}

void V8HeapExplorer::ExtractJSCollectionReferences(
    HeapEntry* entry, Tagged<JSCollection> collection) {
  SetInternalReference(entry, "table", collection->table(),
                       JSCollection::kTableOffset);
}

void V8HeapExplorer::ExtractJSWeakCollectionReferences(
    HeapEntry* entry, Tagged<JSWeakCollection> obj) {
  SetInternalReference(entry, "table", obj->table(),
                       JSWeakCollection::kTableOffset);
}

void V8HeapExplorer::ExtractEphemeronHashTableReferences(
    HeapEntry* entry, Tagged<EphemeronHashTable> table) {
  for (InternalIndex i : table->IterateEntries()) {
    int key_index = EphemeronHashTable::EntryToIndex(i) +
                    EphemeronHashTable::kEntryKeyIndex;
    int value_index = EphemeronHashTable::EntryToValueIndex(i);
    Tagged<Object> key = table->get(key_index);
    Tagged<Object> value = table->get(value_index);
    SetWeakReference(entry, key_index, key,
                     table->OffsetOfElementAt(key_index));
    SetWeakReference(entry, value_index, value,
                     table->OffsetOfElementAt(value_index));
    HeapEntry* key_entry = GetEntry(key);
    HeapEntry* value_entry = GetEntry(value);
    HeapEntry* table_entry = GetEntry(table);
    if (key_entry && value_entry && !IsUndefined(key)) {
      const char* edge_name = names_->GetFormatted(
          "part of key (%s @%u) -> value (%s @%u) pair in WeakMap (table @%u)",
          key_entry->name(), key_entry->id(), value_entry->name(),
          value_entry->id(), table_entry->id());
      key_entry->SetNamedAutoIndexReference(HeapGraphEdge::kInternal, edge_name,
                                            value_entry, names_, generator_,
                                            HeapEntry::kEphemeron);
      table_entry->SetNamedAutoIndexReference(
          HeapGraphEdge::kInternal, edge_name, value_entry, names_, generator_,
          HeapEntry::kEphemeron);
    }
  }
}

// These static arrays are used to prevent excessive code-size in
// ExtractContextReferences below, which would happen if we called
// SetInternalReference for every native context field in a macro.
static const struct {
  int index;
  const char* name;
} native_context_names[] = {
#define CONTEXT_FIELD_INDEX_NAME(index, _, name) {Context::index, #name},
    NATIVE_CONTEXT_FIELDS(CONTEXT_FIELD_INDEX_NAME)
#undef CONTEXT_FIELD_INDEX_NAME
};

void V8HeapExplorer::ExtractContextReferences(HeapEntry* entry,
                                              Tagged<Context> context) {
  DisallowGarbageCollection no_gc;
  if (!IsNativeContext(context) && context->is_declaration_context()) {
    Tagged<ScopeInfo> scope_info = context->scope_info();
    // Add context allocated locals.
    for (auto it : ScopeInfo::IterateLocalNames(scope_info, no_gc)) {
      int idx = scope_info->ContextHeaderLength() + it->index();
      SetContextReference(entry, it->name(), context->get(idx),
                          Context::OffsetOfElementAt(idx));
    }
    if (scope_info->HasContextAllocatedFunctionName()) {
      Tagged<String> name = Cast<String>(scope_info->FunctionName());
      int idx = scope_info->FunctionContextSlotIndex(name);
      if (idx >= 0) {
        SetContextReference(entry, name, context->get(idx),
                            Context::OffsetOfElementAt(idx));
      }
    }
  }

  SetInternalReference(
      entry, "scope_info", context->get(Context::SCOPE_INFO_INDEX),
      FixedArray::OffsetOfElementAt(Context::SCOPE_INFO_INDEX));
  SetInternalReference(entry, "previous", context->get(Context::PREVIOUS_INDEX),
                       FixedArray::OffsetOfElementAt(Context::PREVIOUS_INDEX));
  if (context->has_extension()) {
    SetInternalReference(
        entry, "extension", context->get(Context::EXTENSION_INDEX),
        FixedArray::OffsetOfElementAt(Context::EXTENSION_INDEX));
  }

  if (IsNativeContext(context)) {
    TagObject(context->normalized_map_cache(), "(context norm. map cache)");
    TagObject(context->embedder_data(), "(context data)");
    for (size_t i = 0; i < arraysize(native_context_names); i++) {
      int index = native_context_names[i].index;
      const char* name = native_context_names[i].name;
      SetInternalReference(entry, name, context->get(index),
                           FixedArray::OffsetOfElementAt(index));
    }

    static_assert(Context::NEXT_CONTEXT_LINK == Context::FIRST_WEAK_SLOT);
    static_assert(Context::FIRST_WEAK_SLOT + 1 ==
                  Context::NATIVE_CONTEXT_SLOTS);
  }
}

void V8HeapExplorer::ExtractMapReferences(HeapEntry* entry, Tagged<Map> map) {
  Tagged<MaybeObject> maybe_raw_transitions_or_prototype_info =
      map->raw_transitions();
  Tagged<HeapObject> raw_transitions_or_prototype_info;
  if (maybe_raw_transitions_or_prototype_info.GetHeapObjectIfWeak(
          &raw_transitions_or_prototype_info)) {
    DCHECK(IsMap(raw_transitions_or_prototype_info));
    SetWeakReference(entry, "transition", raw_transitions_or_prototype_info,
                     Map::kTransitionsOrPrototypeInfoOffset);
  } else if (maybe_raw_transitions_or_prototype_info.GetHeapObjectIfStrong(
                 &raw_transitions_or_prototype_info)) {
    if (IsTransitionArray(raw_transitions_or_prototype_info)) {
      Tagged<TransitionArray> transitions =
          Cast<TransitionArray>(raw_transitions_or_prototype_info);
      if (map->CanTransition() && transitions->HasPrototypeTransitions()) {
        TagObject(transitions->GetPrototypeTransitions(),
                  "(prototype transitions)");
      }
      TagObject(transitions, "(transition array)");
      SetInternalReference(entry, "transitions", transitions,
                           Map::kTransitionsOrPrototypeInfoOffset);
    } else if (IsFixedArray(raw_transitions_or_prototype_info)) {
      TagObject(raw_transitions_or_prototype_info, "(transition)");
      SetInternalReference(entry, "transition",
                           raw_transitions_or_prototype_info,
                           Map::kTransitionsOrPrototypeInfoOffset);
    } else if (map->is_prototype_map()) {
      TagObject(raw_transitions_or_prototype_info, "prototype_info");
      SetInternalReference(entry, "prototype_info",
                           raw_transitions_or_prototype_info,
                           Map::kTransitionsOrPrototypeInfoOffset);
    }
  }
  Tagged<DescriptorArray> descriptors = map->instance_descriptors();
  TagObject(descriptors, "(map descriptors)");
  SetInternalReference(entry, "descriptors", descriptors,
                       Map::kInstanceDescriptorsOffset);
  SetInternalReference(entry, "prototype", map->prototype(),
                       Map::kPrototypeOffset);
  if (IsContextMap(map) || IsMapMap(map)) {
    Tagged<Object> native_context = map->native_context_or_null();
    TagObject(native_context, "(native context)");
    SetInternalReference(entry, "native_context", native_context,
                         Map::kConstructorOrBackPointerOrNativeContextOffset);
  } else {
    Tagged<Object> constructor_or_back_pointer =
        map->constructor_or_back_pointer();
    if (IsMap(constructor_or_back_pointer)) {
      TagObject(constructor_or_back_pointer, "(back pointer)");
      SetInternalReference(entry, "back_pointer", constructor_or_back_pointer,
                           Map::kConstructorOrBackPointerOrNativeContextOffset);
    } else if (IsFunctionTemplateInfo(constructor_or_back_pointer)) {
      TagObject(constructor_or_back_pointer, "(constructor function data)");
      SetInternalReference(entry, "constructor_function_data",
                           constructor_or_back_pointer,
                           Map::kConstructorOrBackPointerOrNativeContextOffset);
    } else {
      SetInternalReference(entry, "constructor", constructor_or_back_pointer,
                           Map::kConstructorOrBackPointerOrNativeContextOffset);
    }
  }
  TagObject(map->dependent_code(), "(dependent code)");
  SetInternalReference(entry, "dependent_code", map->dependent_code(),
                       Map::kDependentCodeOffset);
  TagObject(map->prototype_validity_cell(kRelaxedLoad),
            "(prototype validity cell)", HeapEntry::kObjectShape);
}

void V8HeapExplorer::ExtractSharedFunctionInfoReferences(
    HeapEntry* entry, Tagged<SharedFunctionInfo> shared) {
  TagObject(shared, "(shared function info)");
  {
    std::unique_ptr<char[]> name = shared->DebugNameCStr();
    Tagged<Code> code = shared->GetCode(isolate());
    TagObject(code, name[0] != '\0'
                        ? names_->GetFormatted("(code for %s)", name.get())
                        : names_->GetFormatted("(%s code)",
                                               CodeKindToString(code->kind())));
    if (code->has_instruction_stream()) {
      TagObject(
          code->instruction_stream(),
          name[0] != '\0'
              ? names_->GetFormatted("(instruction stream for %s)", name.get())
              : names_->GetFormatted("(%s instruction stream)",
                                     CodeKindToString(code->kind())));
    }
  }

  Tagged<Object> name_or_scope_info = shared->name_or_scope_info(kAcquireLoad);
  if (IsScopeInfo(name_or_scope_info)) {
    TagObject(name_or_scope_info, "(function scope info)");
  }
  SetInternalReference(entry, "name_or_scope_info", name_or_scope_info,
                       SharedFunctionInfo::kNameOrScopeInfoOffset);
  SetInternalReference(entry, "script", shared->script(kAcquireLoad),
                       SharedFunctionInfo::kScriptOffset);
  SetInternalReference(entry, "trusted_function_data",
                       shared->GetTrustedData(isolate()),
                       SharedFunctionInfo::kTrustedFunctionDataOffset);
  SetInternalReference(entry, "untrusted_function_data",
                       shared->GetUntrustedData(),
                       SharedFunctionInfo::kUntrustedFunctionDataOffset);
  SetInternalReference(
      entry, "raw_outer_scope_info_or_feedback_metadata",
      shared->raw_outer_scope_info_or_feedback_metadata(),
      SharedFunctionInfo::kOuterScopeInfoOrFeedbackMetadataOffset);
}

void V8HeapExplorer::ExtractScriptReferences(HeapEntry* entry,
                                             Tagged<Script> script) {
  SetInternalReference(entry, "source", script->source(),
                       Script::kSourceOffset);
  SetInternalReference(entry, "name", script->name(), Script::kNameOffset);
  SetInternalReference(entry, "context_data", script->context_data(),
                       Script::kContextDataOffset);
  TagObject(script->line_ends(), "(script line ends)", HeapEntry::kCode);
  SetInternalReference(entry, "line_ends", script->line_ends(),
                       Script::kLineEndsOffset);
  TagObject(script->infos(), "(infos)", HeapEntry::kCode);
  TagObject(script->host_defined_options(), "(host-defined options)",
            HeapEntry::kCode);
#if V8_ENABLE_WEBASSEMBLY
  if (script->type() == Script::Type::kWasm) {
    // Wasm reuses some otherwise unused fields for wasm-specific information.
    SetInternalReference(entry, "wasm_breakpoint_infos",
                         script->wasm_breakpoint_infos(),
                         Script::kEvalFromSharedOrWrappedArgumentsOffset);
    SetInternalReference(entry, "wasm_managed_native_module",
                         script->wasm_managed_native_module(),
                         Script::kEvalFromPositionOffset);
    SetInternalReference(entry, "wasm_weak_instance_list",
                         script->wasm_weak_instance_list(),
                         Script::kInfosOffset);
  }
#endif
}

void V8HeapExplorer::ExtractAccessorInfoReferences(
    HeapEntry* entry, Tagged<AccessorInfo> accessor_info) {
  SetInternalReference(entry, "name", accessor_info->name(),
                       AccessorInfo::kNameOffset);
  SetInternalReference(entry, "data", accessor_info->data(),
                       AccessorInfo::kDataOffset);
}

void V8HeapExplorer::ExtractAccessorPairReferences(
    HeapEntry* entry, Tagged<AccessorPair> accessors) {
  SetInternalReference(entry, "getter", accessors->getter(),
                       AccessorPair::kGetterOffset);
  SetInternalReference(entry, "setter", accessors->setter(),
                       AccessorPair::kSetterOffset);
}

void V8HeapExplorer::ExtractJSWeakRefReferences(HeapEntry* entry,
                                                Tagged<JSWeakRef> js_weak_ref) {
  SetWeakReference(entry, "target", js_weak_ref->target(),
                   JSWeakRef::kTargetOffset);
}

void V8HeapExplorer::ExtractWeakCellReferences(HeapEntry* entry,
                                               Tagged<WeakCell> weak_cell) {
  SetWeakReference(entry, "target", weak_cell->target(),
                   WeakCell::kTargetOffset);
  SetWeakReference(entry, "unregister_token", weak_cell->unregister_token(),
                   WeakCell::kUnregisterTokenOffset);
}

void V8HeapExplorer::TagBuiltinCodeObject(Tagged<Code> code, const char* name) {
  TagObject(code, names_->GetFormatted("(%s builtin code)", name));
  if (code->has_instruction_stream()) {
    TagObject(code->instruction_stream(),
              names_->GetFormatted("(%s builtin instruction stream)", name));
  }
}

void V8HeapExplorer::ExtractCodeReferences(HeapEntry* entry,
                                           Tagged<Code> code) {
  if (!code->has_instruction_stream()) return;

  SetInternalReference(entry, "instruction_stream", code->instruction_stream(),
                       Code::kInstructionStreamOffset);

  if (code->kind() == CodeKind::BASELINE) {
    TagObject(code->bytecode_or_interpreter_data(), "(interpreter data)");
    SetInternalReference(entry, "interpreter_data",
                         code->bytecode_or_interpreter_data(),
                         Code::kDeoptimizationDataOrInterpreterDataOffset);
    TagObject(code->bytecode_offset_table(), "(bytecode offset table)",
              HeapEntry::kCode);
    SetInternalReference(entry, "bytecode_offset_table",
                         code->bytecode_offset_table(),
                         Code::kPositionTableOffset);
  } else if (code->uses_deoptimization_data()) {
    Tagged<DeoptimizationData> deoptimization_data =
        Cast<DeoptimizationData>(code->deoptimization_data());
    TagObject(deoptimization_data, "(code deopt data)", HeapEntry::kCode);
    SetInternalReference(entry, "deoptimization_data", deoptimization_data,
                         Code::kDeoptimizationDataOrInterpreterDataOffset);
    if (deoptimization_data->length() > 0) {
      TagObject(deoptimization_data->FrameTranslation(), "(code deopt data)",
                HeapEntry::kCode);
      TagObject(deoptimization_data->LiteralArray(), "(code deopt data)",
                HeapEntry::kCode);
      TagObject(deoptimization_data->InliningPositions(), "(code deopt data)",
                HeapEntry::kCode);
    }
    TagObject(code->source_position_table(), "(source position table)",
              HeapEntry::kCode);
    SetInternalReference(entry, "source_position_table",
                         code->source_position_table(),
                         Code::kPositionTableOffset);
  }
}

void V8HeapExplorer::ExtractInstructionStreamReferences(
    HeapEntry* entry, Tagged<InstructionStream> istream) {
  Tagged<Code> code;
  if (!istream->TryGetCode(&code, kAcquireLoad))
    return;  // Not yet initialized.
  TagObject(code, "(code)", HeapEntry::kCode);
  SetInternalReference(entry, "code", code, InstructionStream::kCodeOffset);

  TagObject(istream->relocation_info(), "(code relocation info)",
            HeapEntry::kCode);
  SetInternalReference(entry, "relocation_info", istream->relocation_info(),
                       InstructionStream::kRelocationInfoOffset);
}

void V8HeapExplorer::ExtractCellReferences(HeapEntry* entry,
                                           Tagged<Cell> cell) {
  SetInternalReference(entry, "value", cell->value(), Cell::kValueOffset);
}

void V8HeapExplorer::ExtractFeedbackCellReferences(
    HeapEntry* entry, Tagged<FeedbackCell> feedback_cell) {
  TagObject(feedback_cell, "(feedback cell)");
  SetInternalReference(entry, "value", feedback_cell->value(),
                       FeedbackCell::kValueOffset);
}

void V8HeapExplorer::ExtractPropertyCellReferences(HeapEntry* entry,
                                                   Tagged<PropertyCell> cell) {
  SetInternalReference(entry, "value", cell->value(),
                       PropertyCell::kValueOffset);
  TagObject(cell->dependent_code(), "(dependent code)");
  SetInternalReference(entry, "dependent_code", cell->dependent_code(),
                       PropertyCell::kDependentCodeOffset);
}

void V8HeapExplorer::ExtractPrototypeInfoReferences(
    HeapEntry* entry, Tagged<PrototypeInfo> info) {
  TagObject(info->prototype_chain_enum_cache(), "(prototype chain enum cache)",
            HeapEntry::kObjectShape);
  TagObject(info->prototype_users(), "(prototype users)",
            HeapEntry::kObjectShape);
}

void V8HeapExplorer::ExtractAllocationSiteReferences(
    HeapEntry* entry, Tagged<AllocationSite> site) {
  SetInternalReference(entry, "transition_info",
                       site->transition_info_or_boilerplate(),
                       AllocationSite::kTransitionInfoOrBoilerplateOffset);
  SetInternalReference(entry, "nested_site", site->nested_site(),
                       AllocationSite::kNestedSiteOffset);
  TagObject(site->dependent_code(), "(dependent code)", HeapEntry::kCode);
  SetInternalReference(entry, "dependent_code", site->dependent_code(),
                       AllocationSite::kDependentCodeOffset);
}

void V8HeapExplorer::ExtractArrayBoilerplateDescriptionReferences(
    HeapEntry* entry, Tagged<ArrayBoilerplateDescription> value) {
  Tagged<FixedArrayBase> constant_elements = value->constant_elements();
  SetInternalReference(entry, "constant_elements", constant_elements,
                       ArrayBoilerplateDescription::kConstantElementsOffset);
  TagObject(constant_elements, "(constant elements)", HeapEntry::kCode);
}

void V8HeapExplorer::ExtractRegExpBoilerplateDescriptionReferences(
    HeapEntry* entry, Tagged<RegExpBoilerplateDescription> value) {
  TagObject(value->data(isolate()), "(RegExpData)", HeapEntry::kCode);
}

class JSArrayBufferDataEntryAllocator : public HeapEntriesAllocator {
 public:
  JSArrayBufferDataEntryAllocator(size_t size, V8HeapExplorer* explorer)
      : size_(size), explorer_(explorer) {}
  HeapEntry* AllocateEntry(HeapThing ptr) override {
    return explorer_->AddEntry(reinterpret_cast<Address>(ptr),
                               HeapEntry::kNative, "system / JSArrayBufferData",
                               size_);
  }
  HeapEntry* AllocateEntry(Tagged<Smi> smi) override {
    DCHECK(false);
    return nullptr;
  }

 private:
  size_t size_;
  V8HeapExplorer* explorer_;
};

void V8HeapExplorer::ExtractJSArrayBufferReferences(
    HeapEntry* entry, Tagged<JSArrayBuffer> buffer) {
  // Setup a reference to a native memory backing_store object.
  if (!buffer->backing_store()) return;
  size_t data_size = buffer->byte_length();
  JSArrayBufferDataEntryAllocator allocator(data_size, this);
  HeapEntry* data_entry =
      generator_->FindOrAddEntry(buffer->backing_store(), &allocator);
  entry->SetNamedReference(HeapGraphEdge::kInternal, "backing_store",
                           data_entry, generator_, HeapEntry::kOffHeapPointer);
}

void V8HeapExplorer::ExtractJSPromiseReferences(HeapEntry* entry,
                                                Tagged<JSPromise> promise) {
  SetInternalReference(entry, "reactions_or_result",
                       promise->reactions_or_result(),
                       JSPromise::kReactionsOrResultOffset);
}

void V8HeapExplorer::ExtractJSGeneratorObjectReferences(
    HeapEntry* entry, Tagged<JSGeneratorObject> generator) {
  SetInternalReference(entry, "function", generator->function(),
                       JSGeneratorObject::kFunctionOffset);
  SetInternalReference(entry, "context", generator->context(),
                       JSGeneratorObject::kContextOffset);
  SetInternalReference(entry, "receiver", generator->receiver(),
                       JSGeneratorObject::kReceiverOffset);
  SetInternalReference(entry, "parameters_and_registers",
                       generator->parameters_and_registers(),
                       JSGeneratorObject::kParametersAndRegistersOffset);
}

void V8HeapExplorer::ExtractFixedArrayReferences(HeapEntry* entry,
                                                 Tagged<FixedArray> array) {
  for (int i = 0, l = array->length(); i < l; ++i) {
    DCHECK(!HasWeakHeapObjectTag(array->get(i)));
    SetInternalReference(entry, i, array->get(i), array->OffsetOfElementAt(i));
  }
}

void V8HeapExplorer::ExtractNumberReference(HeapEntry* entry,
                                            Tagged<Object> number) {
  DCHECK(IsNumber(number));

  // Must be large enough to fit any double, int, or size_t.
  char arr[32];
  base::Vector<char> buffer(arr, arraysize(arr));

  const char* string;
  if (IsSmi(number)) {
    int int_value = Smi::ToInt(number);
    string = IntToCString(int_value, buffer);
  } else {
    double double_value = Cast<HeapNumber>(number)->value();
    string = DoubleToCString(double_value, buffer);
  }

  const char* name = names_->GetCopy(string);

  SnapshotObjectId id = heap_object_map_->get_next_id();
  HeapEntry* child_entry =
      snapshot_->AddEntry(HeapEntry::kString, name, id, 0, 0);
  entry->SetNamedReference(HeapGraphEdge::kInternal, "value", child_entry,
                           generator_);
}

void V8HeapExplorer::ExtractBytecodeArrayReferences(
    HeapEntry* entry, Tagged<BytecodeArray> bytecode) {
  RecursivelyTagConstantPool(bytecode->constant_pool(), "(constant pool)",
                             HeapEntry::kCode, 3);
  TagObject(bytecode->handler_table(), "(handler table)", HeapEntry::kCode);
  TagObject(bytecode->raw_source_position_table(kAcquireLoad),
            "(source position table)", HeapEntry::kCode);
}

void V8HeapExplorer::ExtractScopeInfoReferences(HeapEntry* entry,
                                                Tagged<ScopeInfo> info) {
  if (!info->HasInlinedLocalNames()) {
    TagObject(info->context_local_names_hashtable(), "(context local names)",
              HeapEntry::kCode);
  }
}

void V8HeapExplorer::ExtractFeedbackVectorReferences(
    HeapEntry* entry, Tagged<FeedbackVector> feedback_vector) {
  Tagged<MaybeObject> code = feedback_vector->maybe_optimized_code();
  Tagged<HeapObject> code_heap_object;
  if (code.GetHeapObjectIfWeak(&code_heap_object)) {
    SetWeakReference(entry, "optimized code", code_heap_object,
                     FeedbackVector::kMaybeOptimizedCodeOffset);
  }
  for (int i = 0; i < feedback_vector->length(); ++i) {
    Tagged<MaybeObject> maybe_entry = *(feedback_vector->slots_start() + i);
    Tagged<HeapObject> entry;
    if (maybe_entry.GetHeapObjectIfStrong(&entry) &&
        (entry->map(isolate())->instance_type() == WEAK_FIXED_ARRAY_TYPE ||
         IsFixedArrayExact(entry))) {
      TagObject(entry, "(feedback)", HeapEntry::kCode);
    }
  }
}

void V8HeapExplorer::ExtractDescriptorArrayReferences(
    HeapEntry* entry, Tagged<DescriptorArray> array) {
  SetInternalReference(entry, "enum_cache", array->enum_cache(),
                       DescriptorArray::kEnumCacheOffset);
  MaybeObjectSlot start = MaybeObjectSlot(array->GetDescriptorSlot(0));
  MaybeObjectSlot end = MaybeObjectSlot(
      array->GetDescriptorSlot(array->number_of_all_descriptors()));
  for (int i = 0; start + i < end; ++i) {
    MaybeObjectSlot slot = start + i;
    int offset = static_cast<int>(slot.address() - array.address());
    Tagged<MaybeObject> object = *slot;
    Tagged<HeapObject> heap_object;
    if (object.GetHeapObjectIfWeak(&heap_object)) {
      SetWeakReference(entry, i, heap_object, offset);
    } else if (object.GetHeapObjectIfStrong(&heap_object)) {
      SetInternalReference(entry, i, heap_object, offset);
    }
  }
}

void V8HeapExplorer::ExtractEnumCacheReferences(HeapEntry* entry,
                                                Tagged<EnumCache> cache) {
  TagObject(cache->keys(), "(enum cache)", HeapEntry::kObjectShape);
  TagObject(cache->indices(), "(enum cache)", HeapEntry::kObjectShape);
}

void V8HeapExplorer::ExtractTransitionArrayReferences(
    HeapEntry* entry, Tagged<TransitionArray> transitions) {
  if (transitions->HasPrototypeTransitions()) {
    TagObject(transitions->GetPrototypeTransitions(), "(prototype transitions)",
              HeapEntry::kObjectShape);
  }
}

template <typename T>
void V8HeapExplorer::ExtractWeakArrayReferences(int header_size,
                                                HeapEntry* entry,
                                                Tagged<T> array) {
  for (int i = 0; i < array->length(); ++i) {
    Tagged<MaybeObject> object = array->get(i);
    Tagged<HeapObject> heap_object;
    if (object.GetHeapObjectIfWeak(&heap_object)) {
      SetWeakReference(entry, i, heap_object, header_size + i * kTaggedSize);
    } else if (object.GetHeapObjectIfStrong(&heap_object)) {
      SetInternalReference(entry, i, heap_object,
                           header_size + i * kTaggedSize);
    }
  }
}

void V8HeapExplorer::ExtractPropertyReferences(Tagged<JSObject> js_obj,
                                               HeapEntry* entry) {
  Isolate* isolate = js_obj->GetIsolate();
  if (js_obj->HasFastProperties()) {
    Tagged<DescriptorArray> descs =
        js_obj->map()->instance_descriptors(isolate);
    for (InternalIndex i : js_obj->map()->IterateOwnDescriptors()) {
      PropertyDetails details = descs->GetDetails(i);
      switch (details.location()) {
        case PropertyLocation::kField: {
          if (!snapshot_->capture_numeric_value()) {
            Representation r = details.representation();
            if (r.IsSmi() || r.IsDouble()) break;
          }

          Tagged<Name> k = descs->GetKey(i);
          FieldIndex field_index =
              FieldIndex::ForDetails(js_obj->map(), details);
          Tagged<Object> value = js_obj->RawFastPropertyAt(field_index);
          int field_offset =
              field_index.is_inobject() ? field_index.offset() : -1;

          SetDataOrAccessorPropertyReference(details.kind(), entry, k, value,
                                             nullptr, field_offset);
          break;
        }
        case PropertyLocation::kDescriptor:
          SetDataOrAccessorPropertyReference(details.kind(), entry,
                                             descs->GetKey(i),
                                             descs->GetStrongValue(i));
          break;
      }
    }
  } else if (IsJSGlobalObject(js_obj)) {
    // We assume that global objects can only have slow properties.
    Tagged<GlobalDictionary> dictionary =
        Cast<JSGlobalObject>(js_obj)->global_dictionary(kAcquireLoad);
    ReadOnlyRoots roots(isolate);
    for (InternalIndex i : dictionary->IterateEntries()) {
      if (!dictionary->IsKey(roots, dictionary->KeyAt(i))) continue;
      Tagged<PropertyCell> cell = dictionary->CellAt(i);
      Tagged<Name> name = cell->name();
      Tagged<Object> value = cell->value();
      PropertyDetails details = cell->property_details();
      SetDataOrAccessorPropertyReference(details.kind(), entry, name, value);
    }
  } else if (V8_ENABLE_SWISS_NAME_DICTIONARY_BOOL) {
    // SwissNameDictionary::IterateEntries creates a Handle, which should not
    // leak out of here.
    HandleScope scope(isolate);

    Tagged<SwissNameDictionary> dictionary =
        js_obj->property_dictionary_swiss();
    ReadOnlyRoots roots(isolate);
    for (InternalIndex i : dictionary->IterateEntries()) {
      Tagged<Object> k = dictionary->KeyAt(i);
      if (!dictionary->IsKey(roots, k)) continue;
      Tagged<Object> value = dictionary->ValueAt(i);
      PropertyDetails details = dictionary->DetailsAt(i);
      SetDataOrAccessorPropertyReference(details.kind(), entry, Cast<Name>(k),
                                         value);
    }
  } else {
    Tagged<NameDictionary> dictionary = js_obj->property_dictionary();
    ReadOnlyRoots roots(isolate);
    for (InternalIndex i : dictionary->IterateEntries()) {
      Tagged<Object> k = dictionary->KeyAt(i);
      if (!dictionary->IsKey(roots, k)) continue;
      Tagged<Object> value = dictionary->ValueAt(i);
      PropertyDetails details = dictionary->DetailsAt(i);
      SetDataOrAccessorPropertyReference(details.kind(), entry, Cast<Name>(k),
                                         value);
    }
  }
}

void V8HeapExplorer::ExtractAccessorPairProperty(HeapEntry* entry,
                                                 Tagged<Name> key,
                                                 Tagged<Object> callback_obj,
                                                 int field_offset) {
  if (!IsAccessorPair(callback_obj)) return;
  Tagged<AccessorPair> accessors = Cast<AccessorPair>(callback_obj);
  SetPropertyReference(entry, key, accessors, nullptr, field_offset);
  Tagged<Object> getter = accessors->getter();
  if (!IsOddball(getter)) {
    SetPropertyReference(entry, key, getter, "get %s");
  }
  Tagged<Object> setter = accessors->setter();
  if (!IsOddball(setter)) {
    SetPropertyReference(entry, key, setter, "set %s");
  }
}

void V8HeapExplorer::ExtractElementReferences(Tagged<JSObject> js_obj,
                                              HeapEntry* entry) {
  ReadOnlyRoots roots = js_obj->GetReadOnlyRoots();
  if (js_obj->HasObjectElements()) {
    Tagged<FixedArray> elements = Cast<FixedArray>(js_obj->elements());
    int length = IsJSArray(js_obj) ? Smi::ToInt(Cast<JSArray>(js_obj)->length())
                                   : elements->length();
    for (int i = 0; i < length; ++i) {
      if (!IsTheHole(elements->get(i), roots)) {
        SetElementReference(entry, i, elements->get(i));
      }
    }
  } else if (js_obj->HasDictionaryElements()) {
    Tagged<NumberDictionary> dictionary = js_obj->element_dictionary();
    for (InternalIndex i : dictionary->IterateEntries()) {
      Tagged<Object> k = dictionary->KeyAt(i);
      if (!dictionary->IsKey(roots, k)) continue;
      uint32_t index =
          static_cast<uint32_t>(Object::NumberValue(Cast<Number>(k)));
      SetElementReference(entry, index, dictionary->ValueAt(i));
    }
  }
}

void V8HeapExplorer::ExtractInternalReferences(Tagged<JSObject> js_obj,
                                               HeapEntry* entry) {
  int length = js_obj->GetEmbedderFieldCount();
  for (int i = 0; i < length; ++i) {
    Tagged<Object> o = js_obj->GetEmbedderField(i);
    SetInternalReference(entry, i, o, js_obj->GetEmbedderFieldOffset(i));
  }
}

#if V8_ENABLE_WEBASSEMBLY

void V8HeapExplorer::ExtractWasmStructReferences(Tagged<WasmStruct> obj,
                                                 HeapEntry* entry) {
  wasm::StructType* type = obj->type();
  Tagged<WasmTypeInfo> info = obj->map()->wasm_type_info();
  // Getting the trusted data is safe; structs always have their trusted data
  // defined.
  wasm::NamesProvider* names =
      info->trusted_data(isolate())->native_module()->GetNamesProvider();
  Isolate* isolate = heap_->isolate();
  for (uint32_t i = 0; i < type->field_count(); i++) {
    wasm::StringBuilder sb;
    names->PrintFieldName(sb, info->type_index(), i);
    sb << '\0';
    const char* field_name = names_->GetCopy(sb.start());
    switch (type->field(i).kind()) {
      case wasm::kI8:
      case wasm::kI16:
      case wasm::kI32:
      case wasm::kI64:
      case wasm::kF16:
      case wasm::kF32:
      case wasm::kF64:
      case wasm::kS128: {
        if (!snapshot_->capture_numeric_value()) continue;
        std::string value_string = obj->GetFieldValue(i).to_string();
        const char* value_name = names_->GetCopy(value_string.c_str());
        SnapshotObjectId id = heap_object_map_->get_next_id();
        HeapEntry* child_entry =
            snapshot_->AddEntry(HeapEntry::kString, value_name, id, 0, 0);
        entry->SetNamedReference(HeapGraphEdge::kInternal, field_name,
                                 child_entry, generator_);
        break;
      }
      case wasm::kRef:
      case wasm::kRefNull: {
        int field_offset = type->field_offset(i);
        Tagged<Object> value = obj->RawField(field_offset).load(isolate);
        // We could consider hiding {null} fields by default (like we do for
        // arrays, see below), but for now we always include them, in the hope
        // that they might help identify opportunities for struct size
        // reductions.
        HeapEntry* value_entry = GetEntry(value);
        entry->SetNamedReference(HeapGraphEdge::kProperty, field_name,
                                 value_entry, generator_);
        MarkVisitedField(WasmStruct::kHeaderSize + field_offset);
        break;
      }
      case wasm::kRtt:
      case wasm::kVoid:
      case wasm::kBottom:
        UNREACHABLE();
    }
  }
}

void V8HeapExplorer::ExtractWasmArrayReferences(Tagged<WasmArray> obj,
                                                HeapEntry* entry) {
  if (!obj->type()->element_type().is_reference()) return;
  Isolate* isolate = heap_->isolate();
  ReadOnlyRoots roots(isolate);
  for (uint32_t i = 0; i < obj->length(); i++) {
    Tagged<Object> value = obj->ElementSlot(i).load(isolate);
    // By default, don't show {null} entries, to reduce noise: they can make
    // it difficult to find non-null entries in sparse arrays. We piggyback
    // on the "capture numeric values" flag as an opt-in to produce more
    // detailed/verbose snapshots, including {null} entries.
    if (value != roots.wasm_null() || snapshot_->capture_numeric_value()) {
      SetElementReference(entry, i, value);
    }
    MarkVisitedField(obj->element_offset(i));
  }
}

void V8HeapExplorer::ExtractWasmTrustedInstanceDataReferences(
    Tagged<WasmTrustedInstanceData> trusted_data, HeapEntry* entry) {
  PtrComprCageBase cage_base(heap_->isolate());
  for (size_t i = 0; i < WasmTrustedInstanceData::kTaggedFieldOffsets.size();
       i++) {
    const uint16_t offset = WasmTrustedInstanceData::kTaggedFieldOffsets[i];
    SetInternalReference(
        entry, WasmTrustedInstanceData::kTaggedFieldNames[i],
        TaggedField<Object>::load(cage_base, trusted_data, offset), offset);
  }
  for (size_t i = 0; i < WasmTrustedInstanceData::kProtectedFieldNames.size();
       i++) {
    const uint16_t offset = WasmTrustedInstanceData::kProtectedFieldOffsets[i];
    SetInternalReference(
        entry, WasmTrustedInstanceData::kProtectedFieldNames[i],
        trusted_data->RawProtectedPointerField(offset).load(heap_->isolate()),
        offset);
  }
}

#define ASSERT_FIRST_FIELD(Class, Field) \
  static_assert(Class::Super::kHeaderSize == Class::k##Field##Offset)
#define ASSERT_CONSECUTIVE_FIELDS(Class, Field, NextField) \
  static_assert(Class::k##Field##OffsetEnd + 1 == Class::k##NextField##Offset)
#define ASSERT_LAST_FIELD(Class, Field) \
  static_assert(Class::k##Field##OffsetEnd + 1 == Class::kHeaderSize)

void V8HeapExplorer::ExtractWasmInstanceObjectReferences(
    Tagged<WasmInstanceObject> instance_object, HeapEntry* entry) {
  // The static assertions verify that we do not miss any fields here when we
  // update the class definition.
  ASSERT_FIRST_FIELD(WasmInstanceObject, TrustedData);
  SetInternalReference(entry, "trusted_data",
                       instance_object->trusted_data(heap_->isolate()),
                       WasmInstanceObject::kTrustedDataOffset);
  ASSERT_CONSECUTIVE_FIELDS(WasmInstanceObject, TrustedData, ModuleObject);
  SetInternalReference(entry, "module_object", instance_object->module_object(),
                       WasmInstanceObject::kModuleObjectOffset);
  ASSERT_CONSECUTIVE_FIELDS(WasmInstanceObject, ModuleObject, ExportsObject);
  SetInternalReference(entry, "exports", instance_object->exports_object(),
                       WasmInstanceObject::kExportsObjectOffset);
  ASSERT_LAST_FIELD(WasmInstanceObject, ExportsObject);
}

void V8HeapExplorer::ExtractWasmModuleObjectReferences(
    Tagged<WasmModuleObject> module_object, HeapEntry* entry) {
  // The static assertions verify that we do not miss any fields here when we
  // update the class definition.
  ASSERT_FIRST_FIELD(WasmModuleObject, ManagedNativeModule);
  SetInternalReference(entry, "managed_native_module",
                       module_object->managed_native_module(),
                       WasmModuleObject::kManagedNativeModuleOffset);
  ASSERT_CONSECUTIVE_FIELDS(WasmModuleObject, ManagedNativeModule, Script);
  SetInternalReference(entry, "script", module_object->script(),
                       WasmModuleObject::kScriptOffset);
  ASSERT_LAST_FIELD(WasmModuleObject, Script);
}

#undef ASSERT_FIRST_FIELD
#undef ASSERT_CONSECUTIVE_FIELDS
#undef ASSERT_LAST_FIELD

#endif  // V8_ENABLE_WEBASSEMBLY

Tagged<JSFunction> V8HeapExplorer::GetConstructor(Isolate* isolate,
                                                  Tagged<JSReceiver> receiver) {
  DisallowGarbageCollection no_gc;
  HandleScope scope(isolate);
  MaybeHandle<JSFunction> maybe_constructor =
      JSReceiver::GetConstructor(isolate, handle(receiver, isolate));

  if (maybe_constructor.is_null()) return JSFunction();

  return *maybe_constructor.ToHandleChecked();
}

Tagged<String> V8HeapExplorer::GetConstructorName(Isolate* isolate,
                                                  Tagged<JSObject> object) {
  DisallowGarbageCollection no_gc;
  HandleScope scope(isolate);
  return *JSReceiver::GetConstructorName(isolate, handle(object, isolate));
}

HeapEntry* V8HeapExplorer::GetEntry(Tagged<Object> obj) {
  if (IsHeapObject(obj)) {
    return generator_->FindOrAddEntry(reinterpret_cast<void*>(obj.ptr()), this);
  }

  DCHECK(IsSmi(obj));
  if (!snapshot_->capture_numeric_value()) {
    return nullptr;
  }
  return generator_->FindOrAddEntry(Cast<Smi>(obj), this);
}

class RootsReferencesExtractor : public RootVisitor {
 public:
  explicit RootsReferencesExtractor(V8HeapExplorer* explorer)
      : explorer_(explorer), visiting_weak_roots_(false) {}

  void SetVisitingWeakRoots() { visiting_weak_roots_ = true; }

  void VisitRootPointer(Root root, const char* description,
                        FullObjectSlot p) override {
    Tagged<Object> object = *p;
#ifdef V8_ENABLE_DIRECT_HANDLE
    if (object.ptr() == kTaggedNullAddress) return;
#endif
    if (root == Root::kBuiltins) {
      explorer_->TagBuiltinCodeObject(Cast<Code>(object), description);
    }
    explorer_->SetGcSubrootReference(root, description, visiting_weak_roots_,
                                     object);
  }

  void VisitRootPointers(Root root, const char* description,
                         FullObjectSlot start, FullObjectSlot end) override {
    for (FullObjectSlot p = start; p < end; ++p) {
      DCHECK(!MapWord::IsPacked(p.Relaxed_Load().ptr()));
      VisitRootPointer(root, description, p);
    }
  }

  void VisitRootPointers(Root root, const char* description,
                         OffHeapObjectSlot start,
                         OffHeapObjectSlot end) override {
    DCHECK_EQ(root, Root::kStringTable);
    PtrComprCageBase cage_base(explorer_->heap_->isolate());
    for (OffHeapObjectSlot p = start; p < end; ++p) {
      explorer_->SetGcSubrootReference(root, description, visiting_weak_roots_,
                                       p.load(cage_base));
    }
  }

  // Keep this synced with
  // MarkCompactCollector::RootMarkingVisitor::VisitRunningCode.
  void VisitRunningCode(FullObjectSlot code_slot,
                        FullObjectSlot istream_or_smi_zero_slot) final {
    Tagged<Object> istream_or_smi_zero = *istream_or_smi_zero_slot;
    if (istream_or_smi_zero != Smi::zero()) {
      Tagged<Code> code = Cast<Code>(*code_slot);
      code->IterateDeoptimizationLiterals(this);
      VisitRootPointer(Root::kStackRoots, nullptr, istream_or_smi_zero_slot);
    }
    VisitRootPointer(Root::kStackRoots, nullptr, code_slot);
  }

 private:
  V8HeapExplorer* explorer_;
  bool visiting_weak_roots_;
};

bool V8HeapExplorer::IterateAndExtractReferences(
    HeapSnapshotGenerator* generator) {
  generator_ = generator;

  // Create references to the synthetic roots.
  SetRootGcRootsReference();
  for (int root = 0; root < static_cast<int>(Root::kNumberOfRoots); root++) {
    SetGcRootsReference(static_cast<Root>(root));
  }

  // Make sure builtin code objects get their builtin tags
  // first. Otherwise a particular JSFunction object could set
  // its custom name to a generic builtin.
  RootsReferencesExtractor extractor(this);
  ReadOnlyRoots(heap_).Iterate(&extractor);
  heap_->IterateRoots(
      &extractor,
      base::EnumSet<SkipRoot>{SkipRoot::kWeak, SkipRoot::kTracedHandles});
  // TODO(v8:11800): The heap snapshot generator incorrectly considers the weak
  // string tables as strong retainers. Move IterateWeakRoots after
  // SetVisitingWeakRoots.
  heap_->IterateWeakRoots(&extractor, {});
  extractor.SetVisitingWeakRoots();
  heap_->IterateWeakGlobalHandles(&extractor);

  bool interrupted = false;

  CombinedHeapObjectIterator iterator(heap_);
  PtrComprCageBase cage_base(heap_->isolate());
  // Heap iteration need not be finished but progress reporting may depend on
  // it being finished.
  for (Tagged<HeapObject> obj = iterator.Next(); !obj.is_null();
       obj = iterator.Next(), progress_->ProgressStep()) {
    if (interrupted) continue;

    max_pointers_ = obj->Size(cage_base) / kTaggedSize;
    if (max_pointers_ > visited_fields_.size()) {
      // Reallocate to right size.
      visited_fields_.resize(max_pointers_, false);
    }

#ifdef V8_ENABLE_HEAP_SNAPSHOT_VERIFY
    std::unique_ptr<HeapEntryVerifier> verifier;
    // MarkingVisitorBase doesn't expect that we will ever visit read-only
    // objects, and fails DCHECKs if we attempt to. Read-only objects can
    // never retain read-write objects, so there is no risk in skipping
    // verification for them.
    if (v8_flags.heap_snapshot_verify &&
        !MemoryChunk::FromHeapObject(obj)->InReadOnlySpace()) {
      verifier = std::make_unique<HeapEntryVerifier>(generator, obj);
    }
#endif

    HeapEntry* entry = GetEntry(obj);
    ExtractReferences(entry, obj);
    SetInternalReference(entry, "map", obj->map(cage_base),
                         HeapObject::kMapOffset);
    // Extract unvisited fields as hidden references and restore tags
    // of visited fields.
    IndexedReferencesExtractor refs_extractor(this, obj, entry);
    obj->Iterate(cage_base, &refs_extractor);

#if DEBUG
    // Ensure visited_fields_ doesn't leak to the next object.
    for (size_t i = 0; i < max_pointers_; ++i) {
      DCHECK(!visited_fields_[i]);
    }
#endif  // DEBUG

    // Extract location for specific object types
    ExtractLocation(entry, obj);

    if (!progress_->ProgressReport(false)) interrupted = true;
  }

  generator_ = nullptr;
  return interrupted ? false : progress_->ProgressReport(true);
}

bool V8HeapExplorer::IsEssentialObject(Tagged<Object> object) {
  if (!IsHeapObject(object)) return false;
  // Avoid comparing objects in other pointer compression cages to objects
  // inside the main cage as the comparison may only look at the lower 32 bits.
  if (IsCodeSpaceObject(Cast<HeapObject>(object)) ||
      IsTrustedSpaceObject(Cast<HeapObject>(object))) {
    return true;
  }
  Isolate* isolate = heap_->isolate();
  ReadOnlyRoots roots(isolate);
  return !IsOddball(object, isolate) && object != roots.the_hole_value() &&
         object != roots.empty_byte_array() &&
         object != roots.empty_fixed_array() &&
         object != roots.empty_weak_fixed_array() &&
         object != roots.empty_descriptor_array() &&
         object != roots.fixed_array_map() && object != roots.cell_map() &&
         object != roots.global_property_cell_map() &&
         object != roots.shared_function_info_map() &&
         object != roots.free_space_map() &&
         object != roots.one_pointer_filler_map() &&
         object != roots.two_pointer_filler_map();
}

bool V8HeapExplorer::IsEssentialHiddenReference(Tagged<Object> parent,
                                                int field_offset) {
  if (IsAllocationSite(parent) &&
      field_offset == AllocationSite::kWeakNextOffset)
    return false;
  if (IsContext(parent) &&
      field_offset == Context::OffsetOfElementAt(Context::NEXT_CONTEXT_LINK))
    return false;
  if (IsJSFinalizationRegistry(parent) &&
      field_offset == JSFinalizationRegistry::kNextDirtyOffset)
    return false;
  return true;
}

void V8HeapExplorer::SetContextReference(HeapEntry* parent_entry,
                                         Tagged<String> reference_name,
                                         Tagged<Object> child_obj,
                                         int field_offset) {
  HeapEntry* child_entry = GetEntry(child_obj);
  if (child_entry == nullptr) return;
  parent_entry->SetNamedReference(HeapGraphEdge::kContextVariable,
                                  names_->GetName(reference_name), child_entry,
                                  generator_);
  MarkVisitedField(field_offset);
}

void V8HeapExplorer::MarkVisitedField(int offset) {
  if (offset < 0) return;
  int index = offset / kTaggedSize;
  DCHECK_LT(index, max_pointers_);
  DCHECK(!visited_fields_[index]);
  visited_fields_[index] = true;
}

void V8HeapExplorer::SetNativeBindReference(HeapEntry* parent_entry,
                                            const char* reference_name,
                                            Tagged<Object> child_obj) {
  HeapEntry* child_entry = GetEntry(child_obj);
  if (child_entry == nullptr) return;
  parent_entry->SetNamedReference(HeapGraphEdge::kShortcut, reference_name,
                                  child_entry, generator_);
}

void V8HeapExplorer::SetElementReference(HeapEntry* parent_entry, int index,
                                         Tagged<Object> child_obj) {
  HeapEntry* child_entry = GetEntry(child_obj);
  if (child_entry == nullptr) return;
  parent_entry->SetIndexedReference(HeapGraphEdge::kElement, index, child_entry,
                                    generator_);
}

void V8HeapExplorer::SetInternalReference(HeapEntry* parent_entry,
                                          const char* reference_name,
                                          Tagged<Object> child_obj,
                                          int field_offset) {
  if (!IsEssentialObject(child_obj)) {
    return;
  }
  HeapEntry* child_entry = GetEntry(child_obj);
  DCHECK_NOT_NULL(child_entry);
  parent_entry->SetNamedReference(HeapGraphEdge::kInternal, reference_name,
                                  child_entry, generator_);
  MarkVisitedField(field_offset);
}

void V8HeapExplorer::SetInternalReference(HeapEntry* parent_entry, int index,
                                          Tagged<Object> child_obj,
                                          int field_offset) {
  if (!IsEssentialObject(child_obj)) {
    return;
  }
  HeapEntry* child_entry = GetEntry(child_obj);
  DCHECK_NOT_NULL(child_entry);
  parent_entry->SetNamedReference(HeapGraphEdge::kInternal,
                                  names_->GetName(index), child_entry,
                                  generator_);
  MarkVisitedField(field_offset);
}

void V8HeapExplorer::SetHiddenReference(Tagged<HeapObject> parent_obj,
                                        HeapEntry* parent_entry, int index,
                                        Tagged<Object> child_obj,
                                        int field_offset) {
  DCHECK_EQ(parent_entry, GetEntry(parent_obj));
  DCHECK(!MapWord::IsPacked(child_obj.ptr()));
  if (!IsEssentialObject(child_obj)) {
    return;
  }
  HeapEntry* child_entry = GetEntry(child_obj);
  DCHECK_NOT_NULL(child_entry);
  if (IsEssentialHiddenReference(parent_obj, field_offset)) {
    parent_entry->SetIndexedReference(HeapGraphEdge::kHidden, index,
                                      child_entry, generator_);
  }
}

void V8HeapExplorer::SetWeakReference(
    HeapEntry* parent_entry, const char* reference_name,
    Tagged<Object> child_obj, int field_offset,
    HeapEntry::ReferenceVerification verification) {
  if (!IsEssentialObject(child_obj)) {
    return;
  }
  HeapEntry* child_entry = GetEntry(child_obj);
  DCHECK_NOT_NULL(child_entry);
  parent_entry->SetNamedReference(HeapGraphEdge::kWeak, reference_name,
                                  child_entry, generator_, verification);
  MarkVisitedField(field_offset);
}

void V8HeapExplorer::SetWeakReference(HeapEntry* parent_entry, int index,
                                      Tagged<Object> child_obj,
                                      std::optional<int> field_offset) {
  if (!IsEssentialObject(child_obj)) {
    return;
  }
  HeapEntry* child_entry = GetEntry(child_obj);
  DCHECK_NOT_NULL(child_entry);
  parent_entry->SetNamedReference(HeapGraphEdge::kWeak,
                                  names_->GetFormatted("%d", index),
                                  child_entry, generator_);
  if (field_offset.has_value()) {
    MarkVisitedField(*field_offset);
  }
}

void V8HeapExplorer::SetDataOrAccessorPropertyReference(
    PropertyKind kind, HeapEntry* parent_entry, Tagged<Name> reference_name,
    Tagged<Object> child_obj, const char* name_format_string,
    int field_offset) {
  if (kind == PropertyKind::kAccessor) {
    ExtractAccessorPairProperty(parent_entry, reference_name, child_obj,
                                field_offset);
  } else {
    SetPropertyReference(parent_entry, reference_name, child_obj,
                         name_format_string, field_offset);
  }
}

void V8HeapExplorer::SetPropertyReference(HeapEntry* parent_entry,
                                          Tagged<Name> reference_name,
                                          Tagged<Object> child_obj,
                                          const char* name_format_string,
                                          int field_offset) {
  HeapEntry* child_entry = GetEntry(child_obj);
  if (child_entry == nullptr) return;
  HeapGraphEdge::Type type =
      IsSymbol(reference_name) || Cast<String>(reference_name)->length() > 0
          ? HeapGraphEdge::kProperty
          : HeapGraphEdge::kInternal;
  const char* name =
      name_format_string != nullptr && IsString(reference_name)
          ? names_->GetFormatted(
                name_format_string,
                Cast<String>(reference_name)
                    ->ToCString(DISALLOW_NULLS, ROBUST_STRING_TRAVERSAL)
                    .get())
          : names_->GetName(reference_name);

  parent_entry->SetNamedReference(type, name, child_entry, generator_);
  MarkVisitedField(field_offset);
}

void V8HeapExplorer::SetRootGcRootsReference() {
  snapshot_->root()->SetIndexedAutoIndexReference(
      HeapGraphEdge::kElement, snapshot_->gc_roots(), generator_);
}

void V8HeapExplorer::SetUserGlobalReference(Tagged<Object> child_obj) {
  HeapEntry* child_entry = GetEntry(child_obj);
  DCHECK_NOT_NULL(child_entry);
  snapshot_->root()->SetNamedAutoIndexReference(
      HeapGraphEdge::kShortcut, nullptr, child_entry, names_, generator_);
}

void V8HeapExplorer::SetGcRootsReference(Root root) {
  snapshot_->gc_roots()->SetIndexedAutoIndexReference(
      HeapGraphEdge::kElement, snapshot_->gc_subroot(root), generator_);
}

void V8HeapExplorer::SetGcSubrootReference(Root root, const char* description,
                                           bool is_weak,
                                           Tagged<Object> child_obj) {
  if (IsSmi(child_obj)) {
    // TODO(arenevier): if we handle smis here, the snapshot gets 2 to 3 times
    // slower on large heaps. According to perf, The bulk of the extra works
    // happens in TemplateHashMapImpl::Probe method, when tyring to get
    // names->GetFormatted("%d / %s", index, description)
    return;
  }
  HeapEntry* child_entry = GetEntry(child_obj);
  if (child_entry == nullptr) return;
  auto child_heap_obj = Cast<HeapObject>(child_obj);
  const char* name = GetStrongGcSubrootName(child_heap_obj);
  HeapGraphEdge::Type edge_type =
      is_weak ? HeapGraphEdge::kWeak : HeapGraphEdge::kInternal;
  if (name != nullptr) {
    snapshot_->gc_subroot(root)->SetNamedReference(edge_type, name, child_entry,
                                                   generator_);
  } else {
    snapshot_->gc_subroot(root)->SetNamedAutoIndexReference(
        edge_type, description, child_entry, names_, generator_);
  }

  // For full heap snapshots we do not emit user roots but rather rely on
  // regular GC roots to retain objects.
  if (snapshot_->expose_internals()) return;

  // Add a shortcut to JS global object reference at snapshot root.
  // That allows the user to easily find global objects. They are
  // also used as starting points in distance calculations.
  if (is_weak || !IsNativeContext(child_heap_obj)) return;

  Tagged<JSGlobalObject> global =
      Cast<Context>(child_heap_obj)->global_object();
  if (!IsJSGlobalObject(global)) return;

  if (!user_roots_.insert(global).second) return;

  SetUserGlobalReference(global);
}

const char* V8HeapExplorer::GetStrongGcSubrootName(Tagged<HeapObject> object) {
  if (strong_gc_subroot_names_.empty()) {
    Isolate* isolate = Isolate::FromHeap(heap_);
    for (RootIndex root_index = RootIndex::kFirstStrongOrReadOnlyRoot;
         root_index <= RootIndex::kLastStrongOrReadOnlyRoot; ++root_index) {
      const char* name = RootsTable::name(root_index);
      Tagged<Object> root = isolate->root(root_index);
      CHECK(!IsSmi(root));
      strong_gc_subroot_names_.emplace(Cast<HeapObject>(root), name);
    }
    CHECK(!strong_gc_subroot_names_.empty());
  }
  auto it = strong_gc_subroot_names_.find(object);
  return it != strong_gc_subroot_names_.end() ? it->second : nullptr;
}

void V8HeapExplorer::TagObject(Tagged<Object> obj, const char* tag,
                               std::optional<HeapEntry::Type> type,
                               bool overwrite_existing_name) {
  if (IsEssentialObject(obj)) {
    HeapEntry* entry = GetEntry(obj);
    if (overwrite_existing_name || entry->name()[0] == '\0') {
      entry->set_name(tag);
    }
    if (type.has_value()) {
      entry->set_type(*type);
    }
  }
}

void V8HeapExplorer::RecursivelyTagConstantPool(Tagged<Object> obj,
                                                const char* tag,
                                                HeapEntry::Type type,
                                                int recursion_limit) {
  --recursion_limit;
  if (IsFixedArrayExact(obj, isolate())) {
    Tagged<FixedArray> arr = Cast<FixedArray>(obj);
    TagObject(arr, tag, type);
    if (recursion_limit <= 0) return;
    for (int i = 0; i < arr->length(); ++i) {
      RecursivelyTagConstantPool(arr->get(i), tag, type, recursion_limit);
    }
  } else if (IsTrustedFixedArray(obj, isolate())) {
    Tagged<TrustedFixedArray> arr = Cast<TrustedFixedArray>(obj);
    TagObject(arr, tag, type, /*overwrite_existing_name=*/true);
    if (recursion_limit <= 0) return;
    for (int i = 0; i < arr->length(); ++i) {
      RecursivelyTagConstantPool(arr->get(i), tag, type, recursion_limit);
    }
  } else if (IsNameDictionary(obj, isolate()) ||
             IsNumberDictionary(obj, isolate())) {
    TagObject(obj, tag, type);
  }
}

class GlobalObjectsEnumerator : public RootVisitor {
 public:
  GlobalObjectsEnumerator(Isolate* isolate,
                          std::function<void(Handle<JSGlobalObject>)> handler)
      : isolate_(isolate), handler_(handler) {}

  void VisitRootPointers(Root root, const char* description,
                         FullObjectSlot start, FullObjectSlot end) override {
    VisitRootPointersImpl(root, description, start, end);
  }

  void VisitRootPointers(Root root, const char* description,
                         OffHeapObjectSlot start,
                         OffHeapObjectSlot end) override {
    VisitRootPointersImpl(root, description, start, end);
  }

 private:
  template <typename TSlot>
  void VisitRootPointersImpl(Root root, const char* description, TSlot start,
                             TSlot end) {
    for (TSlot p = start; p < end; ++p) {
      DCHECK(!MapWord::IsPacked(p.Relaxed_Load(isolate_).ptr()));
      Tagged<Object> o = p.load(isolate_);
      if (!IsNativeContext(o, isolate_)) continue;
      Tagged<JSObject> proxy = Cast<Context>(o)->global_proxy();
      if (!IsJSGlobalProxy(proxy, isolate_)) continue;
      Tagged<Object> global = proxy->map(isolate_)->prototype(isolate_);
      if (!IsJSGlobalObject(global, isolate_)) continue;
      handler_(handle(Cast<JSGlobalObject>(global), isolate_));
    }
  }

  Isolate* isolate_;
  std::function<void(Handle<JSGlobalObject>)> handler_;
};

V8HeapExplorer::TemporaryGlobalObjectTags
V8HeapExplorer::CollectTemporaryGlobalObjectsTags() {
  if (!global_object_name_resolver_) return {};

  Isolate* isolate = heap_->isolate();
  TemporaryGlobalObjectTags global_object_tags;
  HandleScope scope(isolate);
  GlobalObjectsEnumerator enumerator(
      isolate, [this, isolate,
                &global_object_tags](Handle<JSGlobalObject> global_object) {
        if (const char* tag = global_object_name_resolver_->GetName(
                Utils::ToLocal(Cast<JSObject>(global_object)))) {
          global_object_tags.emplace_back(
              Global<v8::Object>(reinterpret_cast<v8::Isolate*>(isolate),
                                 Utils::ToLocal(Cast<JSObject>(global_object))),
              tag);
          global_object_tags.back().first.SetWeak();
        }
      });
  isolate->global_handles()->IterateAllRoots(&enumerator);
  isolate->traced_handles()->Iterate(&enumerator);
  return global_object_tags;
}

void V8HeapExplorer::MakeGlobalObjectTagMap(
    TemporaryGlobalObjectTags&& global_object_tags) {
  HandleScope scope(heap_->isolate());
  for (const auto& pair : global_object_tags) {
    if (!pair.first.IsEmpty()) {
      // Temporary local.
      auto local = Utils::OpenPersistent(pair.first);
      global_object_tag_map_.emplace(Cast<JSGlobalObject>(*local), pair.second);
    }
  }
}

class EmbedderGraphImpl : public EmbedderGraph {
 public:
  struct Edge {
    Node* from;
    Node* to;
    const char* name;
  };

  class V8NodeImpl : public Node {
   public:
    explicit V8NodeImpl(Tagged<Object> object) : object_(object) {}
    Tagged<Object> GetObject() { return object_; }

    // Node overrides.
    bool IsEmbedderNode() override { return false; }
    const char* Name() override {
      // The name should be retrieved via GetObject().
      UNREACHABLE();
    }
    size_t SizeInBytes() override {
      // The size should be retrieved via GetObject().
      UNREACHABLE();
    }

   private:
    Tagged<Object> object_;
  };

  Node* V8Node(const v8::Local<v8::Value>& value) final {
    v8::Local<v8::Data> data = value;
    return V8Node(data);
  }

  Node* V8Node(const v8::Local<v8::Data>& data) final {
    Handle<Object> object = v8::Utils::OpenHandle(*data);
    DCHECK(!object.is_null());
    return AddNode(std::unique_ptr<Node>(new V8NodeImpl(*object)));
  }

  Node* AddNode(std::unique_ptr<Node> node) final {
    Node* result = node.get();
    nodes_.push_back(std::move(node));
    return result;
  }

  void AddEdge(Node* from, Node* to, const char* name) final {
    edges_.push_back({from, to, name});
  }

  const std::vector<std::unique_ptr<Node>>& nodes() { return nodes_; }
  const std::vector<Edge>& edges() { return edges_; }

 private:
  std::vector<std::unique_ptr<Node>> nodes_;
  std::vector<Edge> edges_;
};

class EmbedderGraphEntriesAllocator : public HeapEntriesAllocator {
 public:
  explicit EmbedderGraphEntriesAllocator(HeapSnapshot* snapshot)
      : snapshot_(snapshot),
        names_(snapshot_->profiler()->names()),
        heap_object_map_(snapshot_->profiler()->heap_object_map()) {}
  HeapEntry* AllocateEntry(HeapThing ptr) override;
  HeapEntry* AllocateEntry(Tagged<Smi> smi) override;

 private:
  HeapSnapshot* snapshot_;
  StringsStorage* names_;
  HeapObjectsMap* heap_object_map_;
};

namespace {

const char* EmbedderGraphNodeName(StringsStorage* names,
                                  EmbedderGraphImpl::Node* node) {
  const char* prefix = node->NamePrefix();
  return prefix ? names->GetFormatted("%s %s", prefix, node->Name())
                : names->GetCopy(node->Name());
}

HeapEntry::Type EmbedderGraphNodeType(EmbedderGraphImpl::Node* node) {
  return node->IsRootNode() ? HeapEntry::kSynthetic : HeapEntry::kNative;
}

// Merges the names of an embedder node and its wrapper node.
// If the wrapper node name contains a tag suffix (part after '/') then the
// result is the embedder node name concatenated with the tag suffix.
// Otherwise, the result is the embedder node name.
const char* MergeNames(StringsStorage* names, const char* embedder_name,
                       const char* wrapper_name) {
  const char* suffix = strchr(wrapper_name, '/');
  return suffix ? names->GetFormatted("%s %s", embedder_name, suffix)
                : embedder_name;
}

}  // anonymous namespace

HeapEntry* EmbedderGraphEntriesAllocator::AllocateEntry(HeapThing ptr) {
  EmbedderGraphImpl::Node* node =
      reinterpret_cast<EmbedderGraphImpl::Node*>(ptr);
  DCHECK(node->IsEmbedderNode());
  size_t size = node->SizeInBytes();
  Address lookup_address = reinterpret_cast<Address>(node->GetNativeObject());
  HeapObjectsMap::MarkEntryAccessed accessed =
      HeapObjectsMap::MarkEntryAccessed::kYes;
  HeapObjectsMap::IsNativeObject is_native_object =
      HeapObjectsMap::IsNativeObject::kNo;
  if (!lookup_address) {
    // If there is not a native object associated with this embedder object,
    // then request the address of the embedder object.
    lookup_address = reinterpret_cast<Address>(node->GetAddress());
    is_native_object = HeapObjectsMap::IsNativeObject::kYes;
  }
  if (!lookup_address) {
    // If the Node implementation did not provide either a native address or an
    // embedder address, then use the address of the Node itself for the lookup.
    // In this case, we'll set the "accessed" flag on the newly created
    // HeapEntry to false, to indicate that this entry should not persist for
    // future snapshots.
    lookup_address = reinterpret_cast<Address>(node);
    accessed = HeapObjectsMap::MarkEntryAccessed::kNo;
  }
  SnapshotObjectId id = heap_object_map_->FindOrAddEntry(
      lookup_address, 0, accessed, is_native_object);
  auto* heap_entry = snapshot_->AddEntry(EmbedderGraphNodeType(node),
                                         EmbedderGraphNodeName(names_, node),
                                         id, static_cast<int>(size), 0);
  heap_entry->set_detachedness(node->GetDetachedness());
  return heap_entry;
}

HeapEntry* EmbedderGraphEntriesAllocator::AllocateEntry(Tagged<Smi> smi) {
  DCHECK(false);
  return nullptr;
}

NativeObjectsExplorer::NativeObjectsExplorer(
    HeapSnapshot* snapshot, SnapshottingProgressReportingInterface* progress)
    : isolate_(
          Isolate::FromHeap(snapshot->profiler()->heap_object_map()->heap())),
      snapshot_(snapshot),
      names_(snapshot_->profiler()->names()),
      heap_object_map_(snapshot_->profiler()->heap_object_map()),
      embedder_graph_entries_allocator_(
          new EmbedderGraphEntriesAllocator(snapshot)) {}

void NativeObjectsExplorer::MergeNodeIntoEntry(
    HeapEntry* entry, EmbedderGraph::Node* original_node,
    EmbedderGraph::Node* wrapper_node) {
  // The wrapper node may be an embedder node (for testing purposes) or a V8
  // node (production code).
  if (!wrapper_node->IsEmbedderNode()) {
    // For V8 nodes only we can add a lookup.
    EmbedderGraphImpl::V8NodeImpl* v8_node =
        static_cast<EmbedderGraphImpl::V8NodeImpl*>(wrapper_node);
    Tagged<Object> object = v8_node->GetObject();
    DCHECK(!IsSmi(object));
    if (original_node->GetNativeObject()) {
      Tagged<HeapObject> heap_object = Cast<HeapObject>(object);
      heap_object_map_->AddMergedNativeEntry(original_node->GetNativeObject(),
                                             heap_object.address());
      DCHECK_EQ(entry->id(), heap_object_map_->FindMergedNativeEntry(
                                 original_node->GetNativeObject()));
    }
  }
  entry->set_detachedness(original_node->GetDetachedness());
  entry->set_name(MergeNames(
      names_, EmbedderGraphNodeName(names_, original_node), entry->name()));
  entry->set_type(EmbedderGraphNodeType(original_node));
  DCHECK_GE(entry->self_size() + original_node->SizeInBytes(),
            entry->self_size());
  entry->add_self_size(original_node->SizeInBytes());
}

HeapEntry* NativeObjectsExplorer::EntryForEmbedderGraphNode(
    EmbedderGraphImpl::Node* node) {
  // Return the entry for the wrapper node if present.
  if (node->WrapperNode()) {
    node = node->WrapperNode();
  }
  // Node is EmbedderNode.
  if (node->IsEmbedderNode()) {
    return generator_->FindOrAddEntry(node,
                                      embedder_graph_entries_allocator_.get());
  }
  // Node is V8NodeImpl.
  Tagged<Object> object =
      static_cast<EmbedderGraphImpl::V8NodeImpl*>(node)->GetObject();
  if (IsSmi(object)) return nullptr;
  auto* entry = generator_->FindEntry(
      reinterpret_cast<void*>(Cast<Object>(object).ptr()));
  return entry;
}

bool NativeObjectsExplorer::IterateAndExtractReferences(
    HeapSnapshotGenerator* generator) {
  generator_ = generator;

  if (v8_flags.heap_profiler_use_embedder_graph &&
      snapshot_->profiler()->HasBuildEmbedderGraphCallback()) {
    v8::HandleScope scope(reinterpret_cast<v8::Isolate*>(isolate_));
    DisallowGarbageCollection no_gc;
    EmbedderGraphImpl graph;
    snapshot_->profiler()->BuildEmbedderGraph(isolate_, &graph);
    for (const auto& node : graph.nodes()) {
      // Only add embedder nodes as V8 nodes have been added already by the
      // V8HeapExplorer.
      if (!node->IsEmbedderNode()) continue;

      if (auto* entry = EntryForEmbedderGraphNode(node.get())) {
        if (node->IsRootNode()) {
          snapshot_->root()->SetIndexedAutoIndexReference(
              HeapGraphEdge::kElement, entry, generator_,
              HeapEntry::kOffHeapPointer);
        }
        if (node->WrapperNode()) {
          MergeNodeIntoEntry(entry, node.get(), node->WrapperNode());
        }
      }
    }
    // Fill edges of the graph.
    for (const auto& edge : graph.edges()) {
      // |from| and |to| can be nullptr if the corresponding node is a V8 node
      // pointing to a Smi.
      HeapEntry* from = EntryForEmbedderGraphNode(edge.from);
      if (!from) continue;
      HeapEntry* to = EntryForEmbedderGraphNode(edge.to);
      if (!to) continue;
      if (edge.name == nullptr) {
        from->SetIndexedAutoIndexReference(HeapGraphEdge::kElement, to,
                                           generator_,
                                           HeapEntry::kOffHeapPointer);
      } else {
        from->SetNamedReference(HeapGraphEdge::kInternal,
                                names_->GetCopy(edge.name), to, generator_,
                                HeapEntry::kOffHeapPointer);
      }
    }
  }
  generator_ = nullptr;
  return true;
}

HeapSnapshotGenerator::HeapSnapshotGenerator(
    HeapSnapshot* snapshot, v8::ActivityControl* control,
    v8::HeapProfiler::ObjectNameResolver* resolver, Heap* heap,
    cppgc::EmbedderStackState stack_state)
    : snapshot_(snapshot),
      control_(control),
      v8_heap_explorer_(snapshot_, this, resolver),
      dom_explorer_(snapshot_, this),
      heap_(heap),
      stack_state_(stack_state) {}

namespace {
class V8_NODISCARD NullContextForSnapshotScope {
 public:
  explicit NullContextForSnapshotScope(Isolate* isolate)
      : isolate_(isolate), prev_(isolate->context()) {
    isolate_->set_context(Context());
  }
  ~NullContextForSnapshotScope() { isolate_->set_context(prev_); }

 private:
  Isolate* isolate_;
  Tagged<Context> prev_;
};
}  // namespace

bool HeapSnapshotGenerator::GenerateSnapshot() {
  v8::base::ElapsedTimer timer;
  timer.Start();

  IsolateSafepointScope scope(heap_);

  Isolate* isolate = heap_->isolate();
  auto temporary_global_object_tags =
      v8_heap_explorer_.CollectTemporaryGlobalObjectsTags();

  EmbedderStackStateScope stack_scope(
      heap_, EmbedderStackStateOrigin::kImplicitThroughTask, stack_state_);
  heap_->CollectAllAvailableGarbage(GarbageCollectionReason::kHeapProfiler);

  // No allocation that could trigger GC from here onwards. We cannot use a
  // DisallowGarbageCollection scope as the HeapObjectIterator used during
  // snapshot creation enters a safepoint as well. However, in practice we
  // already enter a safepoint above so that should never trigger a GC.
  DisallowPositionInfoSlow no_position_info_slow;

  NullContextForSnapshotScope null_context_scope(isolate);

  v8_heap_explorer_.MakeGlobalObjectTagMap(
      std::move(temporary_global_object_tags));

  InitProgressCounter();

  snapshot_->AddSyntheticRootEntries();

  v8_heap_explorer_.PopulateLineEnds();
  if (!FillReferences()) return false;

  snapshot_->FillChildren();
  snapshot_->RememberLastJSObjectId();

  progress_counter_ = progress_total_;

  if (i::v8_flags.profile_heap_snapshot) {
    base::OS::PrintError("[Heap snapshot took %0.3f ms]\n",
                         timer.Elapsed().InMillisecondsF());
  }
  timer.Stop();
  if (!ProgressReport(true)) return false;
  return true;
}

bool HeapSnapshotGenerator::GenerateSnapshotAfterGC() {
  // Same as above, but no allocations, no GC run, and no progress report.
  IsolateSafepointScope scope(heap_);
  auto temporary_global_object_tags =
      v8_heap_explorer_.CollectTemporaryGlobalObjectsTags();
  NullContextForSnapshotScope null_context_scope(heap_->isolate());
  v8_heap_explorer_.MakeGlobalObjectTagMap(
      std::move(temporary_global_object_tags));
  snapshot_->AddSyntheticRootEntries();
  if (!FillReferences()) return false;
  snapshot_->FillChildren();
  snapshot_->RememberLastJSObjectId();
  return true;
}

void HeapSnapshotGenerator::ProgressStep() {
  // Only increment the progress_counter_ until
  // equal to progress_total -1 == progress_counter.
  // This ensures that intermediate ProgressReport calls will never signal
  // that the work is finished (i.e. progress_counter_ == progress_total_).
  // Only the forced ProgressReport() at the end of GenerateSnapshot() should,
  // after setting progress_counter_ = progress_total_, signal that the
  // work is finished because signalling finished twice
  // breaks the DevTools frontend.
  if (control_ != nullptr && progress_total_ > progress_counter_ + 1) {
    ++progress_counter_;
  }
}

bool HeapSnapshotGenerator::ProgressReport(bool force) {
  const int kProgressReportGranularity = 10000;
  if (control_ != nullptr &&
      (force || progress_counter_ % kProgressReportGranularity == 0)) {
    return control_->ReportProgressValue(progress_counter_, progress_total_) ==
           v8::ActivityControl::kContinue;
  }
  return true;
}

void HeapSnapshotGenerator::InitProgressCounter() {
  if (control_ == nullptr) return;
  progress_total_ = v8_heap_explorer_.EstimateObjectsCount();
  progress_counter_ = 0;
}

bool HeapSnapshotGenerator::FillReferences() {
  return v8_heap_explorer_.IterateAndExtractReferences(this) &&
         dom_explorer_.IterateAndExtractReferences(this);
}

// type, name, id, self_size, edge_count, trace_node_id, detachedness.
const int HeapSnapshotJSONSerializer::kNodeFieldsCount = 7;

void HeapSnapshotJSONSerializer::Serialize(v8::OutputStream* stream) {
  v8::base::ElapsedTimer timer;
  timer.Start();
  DCHECK_NULL(writer_);
  writer_ = new OutputStreamWriter(stream);
  SerializeImpl();
  delete writer_;
  writer_ = nullptr;

  if (i::v8_flags.profile_heap_snapshot) {
    base::OS::PrintError("[Serialization of heap snapshot took %0.3f ms]\n",
                         timer.Elapsed().InMillisecondsF());
  }
  timer.Stop();
}

void HeapSnapshotJSONSerializer::SerializeImpl() {
  DCHECK_EQ(0, snapshot_->root()->index());
  writer_->AddCharacter('{');
  writer_->AddString("\"snapshot\":{");
  SerializeSnapshot();
  if (writer_->aborted()) return;
  writer_->AddString("},\n");
  writer_->AddString("\"nodes\":[");
  SerializeNodes();
  if (writer_->aborted()) return;
  writer_->AddString("],\n");
  writer_->AddString("\"edges\":[");
  SerializeEdges();
  if (writer_->aborted()) return;
  writer_->AddString("],\n");

  writer_->AddString("\"trace_function_infos\":[");
  SerializeTraceNodeInfos();
  if (writer_->aborted()) return;
  writer_->AddString("],\n");
  writer_->AddString("\"trace_tree\":[");
  SerializeTraceTree();
  if (writer_->aborted()) return;
  writer_->AddString("],\n");

  writer_->AddString("\"samples\":[");
  SerializeSamples();
  if (writer_->aborted()) return;
  writer_->AddString("],\n");

  writer_->AddString("\"locations\":[");
  SerializeLocations();
  if (writer_->aborted()) return;
  writer_->AddString("],\n");

  writer_->AddString("\"strings\":[");
  SerializeStrings();
  if (writer_->aborted()) return;
  writer_->AddCharacter(']');
  writer_->AddCharacter('}');
  writer_->Finalize();
}

int HeapSnapshotJSONSerializer::GetStringId(const char* s) {
  base::HashMap::Entry* cache_entry =
      strings_.LookupOrInsert(const_cast<char*>(s), StringHash(s));
  if (cache_entry->value == nullptr) {
    cache_entry->value = reinterpret_cast<void*>(next_string_id_++);
  }
  return static_cast<int>(reinterpret_cast<intptr_t>(cache_entry->value));
}

namespace {

template <size_t size>
struct ToUnsigned;

template <>
struct ToUnsigned<1> {
  using Type = uint8_t;
};

template <>
struct ToUnsigned<4> {
  using Type = uint32_t;
};

template <>
struct ToUnsigned<8> {
  using Type = uint64_t;
};

}  // namespace

template <typename T>
static int utoa_impl(T value, base::Vector<char> buffer, int buffer_pos) {
  static_assert(static_cast<T>(-1) > 0);  // Check that T is unsigned
  int number_of_digits = 0;
  T t = value;
  do {
    ++number_of_digits;
  } while (t /= 10);

  buffer_pos += number_of_digits;
  int result = buffer_pos;
  do {
    int last_digit = static_cast<int>(value % 10);
    buffer[--buffer_pos] = '0' + last_digit;
    value /= 10;
  } while (value);
  return result;
}

template <typename T>
static int utoa(T value, base::Vector<char> buffer, int buffer_pos) {
  typename ToUnsigned<sizeof(value)>::Type unsigned_value = value;
  static_assert(sizeof(value) == sizeof(unsigned_value));
  return utoa_impl(unsigned_value, buffer, buffer_pos);
}

void HeapSnapshotJSONSerializer::SerializeEdge(HeapGraphEdge* edge,
                                               bool first_edge) {
  // The buffer needs space for 3 unsigned ints, 3 commas, \n and \0
  static const int kBufferSize =
      MaxDecimalDigitsIn<sizeof(unsigned)>::kUnsigned * 3 + 3 + 2;
  base::EmbeddedVector<char, kBufferSize> buffer;
  int edge_name_or_index = edge->type() == HeapGraphEdge::kElement ||
                                   edge->type() == HeapGraphEdge::kHidden
                               ? edge->index()
                               : GetStringId(edge->name());
  int buffer_pos = 0;
  if (!first_edge) {
    buffer[buffer_pos++] = ',';
  }
  buffer_pos = utoa(edge->type(), buffer, buffer_pos);
  buffer[buffer_pos++] = ',';
  buffer_pos = utoa(edge_name_or_index, buffer, buffer_pos);
  buffer[buffer_pos++] = ',';
  buffer_pos = utoa(to_node_index(edge->to()), buffer, buffer_pos);
  buffer[buffer_pos++] = '\n';
  buffer[buffer_pos++] = '\0';
  writer_->AddString(buffer.begin());
}

void HeapSnapshotJSONSerializer::SerializeEdges() {
  std::vector<HeapGraphEdge*>& edges = snapshot_->children();
  for (size_t i = 0; i < edges.size(); ++i) {
    DCHECK(i == 0 ||
           edges[i - 1]->from()->index() <= edges[i]->from()->index());
    SerializeEdge(edges[i], i == 0);
    if (writer_->aborted()) return;
  }
}

void HeapSnapshotJSONSerializer::SerializeNode(const HeapEntry* entry) {
  // The buffer needs space for 5 unsigned ints, 1 size_t, 1 uint8_t, 7 commas,
  // \n and \0
  static const int kBufferSize =
      5 * MaxDecimalDigitsIn<sizeof(unsigned)>::kUnsigned +
      MaxDecimalDigitsIn<sizeof(size_t)>::kUnsigned +
      MaxDecimalDigitsIn<sizeof(uint8_t)>::kUnsigned + 7 + 1 + 1;
  base::EmbeddedVector<char, kBufferSize> buffer;
  int buffer_pos = 0;
  if (to_node_index(entry) != 0) {
    buffer[buffer_pos++] = ',';
  }
  buffer_pos = utoa(entry->type(), buffer, buffer_pos);
  buffer[buffer_pos++] = ',';
  buffer_pos = utoa(GetStringId(entry->name()), buffer, buffer_pos);
  buffer[buffer_pos++] = ',';
  buffer_pos = utoa(entry->id(), buffer, buffer_pos);
  buffer[buffer_pos++] = ',';
  buffer_pos = utoa(entry->self_size(), buffer, buffer_pos);
  buffer[buffer_pos++] = ',';
  buffer_pos = utoa(entry->children_count(), buffer, buffer_pos);
  buffer[buffer_pos++] = ',';
  buffer_pos = utoa(entry->trace_node_id(), buffer, buffer_pos);
  buffer[buffer_pos++] = ',';
  buffer_pos = utoa(entry->detachedness(), buffer, buffer_pos);
  buffer[buffer_pos++] = '\n';
  buffer[buffer_pos++] = '\0';
  writer_->AddString(buffer.begin());
}

void HeapSnapshotJSONSerializer::SerializeNodes() {
  const std::deque<HeapEntry>& entries = snapshot_->entries();
  for (const HeapEntry& entry : entries) {
    SerializeNode(&entry);
    if (writer_->aborted()) return;
  }
}

void HeapSnapshotJSONSerializer::SerializeSnapshot() {
  writer_->AddString("\"meta\":");
  // The object describing node serialization layout.
  // We use a set of macros to improve readability.

  // clang-format off
#define JSON_A(s) "[" s "]"
#define JSON_O(s) "{" s "}"
#define JSON_S(s) "\"" s "\""
  writer_->AddString(JSON_O(
    JSON_S("node_fields") ":" JSON_A(
        JSON_S("type") ","
        JSON_S("name") ","
        JSON_S("id") ","
        JSON_S("self_size") ","
        JSON_S("edge_count") ","
        JSON_S("trace_node_id") ","
        JSON_S("detachedness")) ","
    JSON_S("node_types") ":" JSON_A(
        JSON_A(
            JSON_S("hidden") ","
            JSON_S("array") ","
            JSON_S("string") ","
            JSON_S("object") ","
            JSON_S("code") ","
            JSON_S("closure") ","
            JSON_S("regexp") ","
            JSON_S("number") ","
            JSON_S("native") ","
            JSON_S("synthetic") ","
            JSON_S("concatenated string") ","
            JSON_S("sliced string") ","
            JSON_S("symbol") ","
            JSON_S("bigint") ","
            JSON_S("object shape")) ","
        JSON_S("string") ","
        JSON_S("number") ","
        JSON_S("number") ","
        JSON_S("number") ","
        JSON_S("number") ","
        JSON_S("number")) ","
    JSON_S("edge_fields") ":" JSON_A(
        JSON_S("type") ","
        JSON_S("name_or_index") ","
        JSON_S("to_node")) ","
    JSON_S("edge_types") ":" JSON_A(
        JSON_A(
            JSON_S("context") ","
            JSON_S("element") ","
            JSON_S("property") ","
            JSON_S("internal") ","
            JSON_S("hidden") ","
            JSON_S("shortcut") ","
            JSON_S("weak")) ","
        JSON_S("string_or_number") ","
        JSON_S("node")) ","
    JSON_S("trace_function_info_fields") ":" JSON_A(
        JSON_S("function_id") ","
        JSON_S("name") ","
        JSON_S("script_name") ","
        JSON_S("script_id") ","
        JSON_S("line") ","
        JSON_S("column")) ","
    JSON_S("trace_node_fields") ":" JSON_A(
        JSON_S("id") ","
        JSON_S("function_info_index") ","
        JSON_S("count") ","
        JSON_S("size") ","
        JSON_S("children")) ","
    JSON_S("sample_fields") ":" JSON_A(
        JSON_S("timestamp_us") ","
        JSON_S("last_assigned_id")) ","
    JSON_S("location_fields") ":" JSON_A(
        JSON_S("object_index") ","
        JSON_S("script_id") ","
        JSON_S("line") ","
        JSON_S("column"))));
// clang-format on
#undef JSON_S
#undef JSON_O
#undef JSON_A
  writer_->AddString(",\"node_count\":");
  writer_->AddNumber(static_cast<unsigned>(snapshot_->entries().size()));
  writer_->AddString(",\"edge_count\":");
  writer_->AddNumber(static_cast<double>(snapshot_->edges().size()));
  writer_->AddString(",\"trace_function_count\":");
  uint32_t count = 0;
  AllocationTracker* tracker = snapshot_->profiler()->allocation_tracker();
  if (tracker) {
    count = static_cast<uint32_t>(tracker->function_info_list().size());
  }
  writer_->AddNumber(count);
}

static void WriteUChar(OutputStreamWriter* w, unibrow::uchar u) {
  static const char hex_chars[] = "0123456789ABCDEF";
  w->AddString("\\u");
  w->AddCharacter(hex_chars[(u >> 12) & 0xF]);
  w->AddCharacter(hex_chars[(u >> 8) & 0xF]);
  w->AddCharacter(hex_chars[(u >> 4) & 0xF]);
  w->AddCharacter(hex_chars[u & 0xF]);
}

void HeapSnapshotJSONSerializer::SerializeTraceTree() {
  AllocationTracker* tracker = snapshot_->profiler()->allocation_tracker();
  if (!tracker) return;
  AllocationTraceTree* traces = tracker->trace_tree();
  SerializeTraceNode(traces->root());
}

void HeapSnapshotJSONSerializer::SerializeTraceNode(AllocationTraceNode* node) {
  // The buffer needs space for 4 unsigned ints, 4 commas, [ and \0
  const int kBufferSize =
      4 * MaxDecimalDigitsIn<sizeof(unsigned)>::kUnsigned + 4 + 1 + 1;
  base::EmbeddedVector<char, kBufferSize> buffer;
  int buffer_pos = 0;
  buffer_pos = utoa(node->id(), buffer, buffer_pos);
  buffer[buffer_pos++] = ',';
  buffer_pos = utoa(node->function_info_index(), buffer, buffer_pos);
  buffer[buffer_pos++] = ',';
  buffer_pos = utoa(node->allocation_count(), buffer, buffer_pos);
  buffer[buffer_pos++] = ',';
  buffer_pos = utoa(node->allocation_size(), buffer, buffer_pos);
  buffer[buffer_pos++] = ',';
  buffer[buffer_pos++] = '[';
  buffer[buffer_pos++] = '\0';
  writer_->AddString(buffer.begin());

  int i = 0;
  for (AllocationTraceNode* child : node->children()) {
    if (i++ > 0) {
      writer_->AddCharacter(',');
    }
    SerializeTraceNode(child);
  }
  writer_->AddCharacter(']');
}

// 0-based position is converted to 1-based during the serialization.
static int SerializePosition(int position, base::Vector<char> buffer,
                             int buffer_pos) {
  if (position == -1) {
    buffer[buffer_pos++] = '0';
  } else {
    DCHECK_GE(position, 0);
    buffer_pos = utoa(static_cast<unsigned>(position + 1), buffer, buffer_pos);
  }
  return buffer_pos;
}

void HeapSnapshotJSONSerializer::SerializeTraceNodeInfos() {
  AllocationTracker* tracker = snapshot_->profiler()->allocation_tracker();
  if (!tracker) return;
  // The buffer needs space for 6 unsigned ints, 6 commas, \n and \0
  const int kBufferSize =
      6 * MaxDecimalDigitsIn<sizeof(unsigned)>::kUnsigned + 6 + 1 + 1;
  base::EmbeddedVector<char, kBufferSize> buffer;
  int i = 0;
  for (AllocationTracker::FunctionInfo* info : tracker->function_info_list()) {
    int buffer_pos = 0;
    if (i++ > 0) {
      buffer[buffer_pos++] = ',';
    }
    buffer_pos = utoa(info->function_id, buffer, buffer_pos);
    buffer[buffer_pos++] = ',';
    buffer_pos = utoa(GetStringId(info->name), buffer, buffer_pos);
    buffer[buffer_pos++] = ',';
    buffer_pos = utoa(GetStringId(info->script_name), buffer, buffer_pos);
    buffer[buffer_pos++] = ',';
    // The cast is safe because script id is a non-negative Smi.
    buffer_pos =
        utoa(static_cast<unsigned>(info->script_id), buffer, buffer_pos);
    buffer[buffer_pos++] = ',';
    buffer_pos = SerializePosition(info->line, buffer, buffer_pos);
    buffer[buffer_pos++] = ',';
    buffer_pos = SerializePosition(info->column, buffer, buffer_pos);
    buffer[buffer_pos++] = '\n';
    buffer[buffer_pos++] = '\0';
    writer_->AddString(buffer.begin());
  }
}

void HeapSnapshotJSONSerializer::SerializeSamples() {
  const std::vector<HeapObjectsMap::TimeInterval>& samples =
      snapshot_->profiler()->heap_object_map()->samples();
  if (samples.empty()) return;
  base::TimeTicks start_time = samples[0].timestamp;
  // The buffer needs space for 2 unsigned ints, 2 commas, \n and \0
  const int kBufferSize = MaxDecimalDigitsIn<sizeof(
                              base::TimeDelta().InMicroseconds())>::kUnsigned +
                          MaxDecimalDigitsIn<sizeof(samples[0].id)>::kUnsigned +
                          2 + 1 + 1;
  base::EmbeddedVector<char, kBufferSize> buffer;
  int i = 0;
  for (const HeapObjectsMap::TimeInterval& sample : samples) {
    int buffer_pos = 0;
    if (i++ > 0) {
      buffer[buffer_pos++] = ',';
    }
    base::TimeDelta time_delta = sample.timestamp - start_time;
    buffer_pos = utoa(time_delta.InMicroseconds(), buffer, buffer_pos);
    buffer[buffer_pos++] = ',';
    buffer_pos = utoa(sample.last_assigned_id(), buffer, buffer_pos);
    buffer[buffer_pos++] = '\n';
    buffer[buffer_pos++] = '\0';
    writer_->AddString(buffer.begin());
  }
}

void HeapSnapshotJSONSerializer::SerializeString(const unsigned char* s) {
  writer_->AddCharacter('\n');
  writer_->AddCharacter('\"');
  for (; *s != '\0'; ++s) {
    switch (*s) {
      case '\b':
        writer_->AddString("\\b");
        continue;
      case '\f':
        writer_->AddString("\\f");
        continue;
      case '\n':
        writer_->AddString("\\n");
        continue;
      case '\r':
        writer_->AddString("\\r");
        continue;
      case '\t':
        writer_->AddString("\\t");
        continue;
      case '\"':
      case '\\':
        writer_->AddCharacter('\\');
        writer_->AddCharacter(*s);
        continue;
      default:
        if (*s > 31 && *s < 128) {
          writer_->AddCharacter(*s);
        } else if (*s <= 31) {
          // Special character with no dedicated literal.
          WriteUChar(writer_, *s);
        } else {
          // Convert UTF-8 into \u UTF-16 literal.
          size_t length = 1, cursor = 0;
          for (; length <= 4 && *(s + length) != '\0'; ++length) {
          }
          unibrow::uchar c = unibrow::Utf8::CalculateValue(s, length, &cursor);
          if (c != unibrow::Utf8::kBadChar) {
            WriteUChar(writer_, c);
            DCHECK_NE(cursor, 0);
            s += cursor - 1;
          } else {
            writer_->AddCharacter('?');
          }
        }
    }
  }
  writer_->AddCharacter('\"');
}

void HeapSnapshotJSONSerializer::SerializeStrings() {
  base::ScopedVector<const unsigned char*> sorted_strings(strings_.occupancy() +
                                                          1);
  for (base::HashMap::Entry* entry = strings_.Start(); entry != nullptr;
       entry = strings_.Next(entry)) {
    int index = static_cast<int>(reinterpret_cast<uintptr_t>(entry->value));
    sorted_strings[index] = reinterpret_cast<const unsigned char*>(entry->key);
  }
  writer_->AddString("\"<dummy>\"");
  for (int i = 1; i < sorted_strings.length(); ++i) {
    writer_->AddCharacter(',');
    SerializeString(sorted_strings[i]);
    if (writer_->aborted()) return;
  }
}

void HeapSnapshotJSONSerializer::SerializeLocation(
    const EntrySourceLocation& location) {
  // The buffer needs space for 4 unsigned ints, 3 commas, \n and \0
  static const int kBufferSize =
      MaxDecimalDigitsIn<sizeof(unsigned)>::kUnsigned * 4 + 3 + 2;
  base::EmbeddedVector<char, kBufferSize> buffer;
  int buffer_pos = 0;
  buffer_pos = utoa(to_node_index(location.entry_index), buffer, buffer_pos);
  buffer[buffer_pos++] = ',';
  buffer_pos = utoa(location.scriptId, buffer, buffer_pos);
  buffer[buffer_pos++] = ',';
  buffer_pos = utoa(location.line, buffer, buffer_pos);
  buffer[buffer_pos++] = ',';
  buffer_pos = utoa(location.col, buffer, buffer_pos);
  buffer[buffer_pos++] = '\n';
  buffer[buffer_pos++] = '\0';
  writer_->AddString(buffer.begin());
}

void HeapSnapshotJSONSerializer::SerializeLocations() {
  const std::vector<EntrySourceLocation>& locations = snapshot_->locations();
  for (size_t i = 0; i < locations.size(); i++) {
    if (i > 0) writer_->AddCharacter(',');
    SerializeLocation(locations[i]);
    if (writer_->aborted()) return;
  }
}

}  // namespace v8::internal
