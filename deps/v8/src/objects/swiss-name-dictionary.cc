// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Only including the -inl.h file directly makes the linter complain.
#include "src/objects/swiss-name-dictionary.h"

#include "src/heap/heap-inl.h"
#include "src/objects/swiss-name-dictionary-inl.h"

namespace v8 {
namespace internal {

// static
Handle<SwissNameDictionary> SwissNameDictionary::DeleteEntry(
    Isolate* isolate, Handle<SwissNameDictionary> table, InternalIndex entry) {
  // GetCtrl() does the bounds check.
  DCHECK(IsFull(table->GetCtrl(entry.as_int())));

  int i = entry.as_int();

  table->SetCtrl(i, Ctrl::kDeleted);
  table->ClearDataTableEntry(isolate, i);
  // We leave the PropertyDetails unchanged because they are not relevant for
  // GC.

  int nof = table->NumberOfElements();
  table->SetNumberOfElements(nof - 1);
  int nod = table->NumberOfDeletedElements();
  table->SetNumberOfDeletedElements(nod + 1);

  // TODO(v8:11388) Abseil's flat_hash_map doesn't shrink on deletion, but may
  // decide on addition to do an in-place rehash to remove deleted elements. We
  // shrink on deletion here to follow what NameDictionary and
  // OrderedNameDictionary do. We should investigate which approach works
  // better.
  return Shrink(isolate, table);
}

// static
template <typename IsolateT>
Handle<SwissNameDictionary> SwissNameDictionary::Rehash(
    IsolateT* isolate, DirectHandle<SwissNameDictionary> table,
    int new_capacity) {
  DCHECK(IsValidCapacity(new_capacity));
  DCHECK_LE(table->NumberOfElements(), MaxUsableCapacity(new_capacity));
  ReadOnlyRoots roots(isolate);

  Handle<SwissNameDictionary> new_table =
      isolate->factory()->NewSwissNameDictionaryWithCapacity(
          new_capacity, Heap::InYoungGeneration(*table) ? AllocationType::kYoung
                                                        : AllocationType::kOld);

  DisallowHeapAllocation no_gc;

  int new_enum_index = 0;
  new_table->SetNumberOfElements(table->NumberOfElements());
  for (int enum_index = 0; enum_index < table->UsedCapacity(); ++enum_index) {
    int entry = table->EntryForEnumerationIndex(enum_index);

    Tagged<Object> key;

    if (table->ToKey(roots, entry, &key)) {
      Tagged<Object> value = table->ValueAtRaw(entry);
      PropertyDetails details = table->DetailsAt(entry);

      int new_entry = new_table->AddInternal(Cast<Name>(key), value, details);

      // TODO(v8::11388) Investigate ways of hoisting the branching needed to
      // select the correct meta table entry size (based on the capacity of the
      // table) out of the loop.
      new_table->SetEntryForEnumerationIndex(new_enum_index, new_entry);
      ++new_enum_index;
    }
  }

  new_table->SetHash(table->Hash());
  return new_table;
}

bool SwissNameDictionary::EqualsForTesting(Tagged<SwissNameDictionary> other) {
  if (Capacity() != other->Capacity() ||
      NumberOfElements() != other->NumberOfElements() ||
      NumberOfDeletedElements() != other->NumberOfDeletedElements() ||
      Hash() != other->Hash()) {
    return false;
  }

  for (int i = 0; i < Capacity() + kGroupWidth; i++) {
    if (CtrlTable()[i] != other->CtrlTable()[i]) {
      return false;
    }
  }
  for (int i = 0; i < Capacity(); i++) {
    if (KeyAt(i) != other->KeyAt(i) || ValueAtRaw(i) != other->ValueAtRaw(i)) {
      return false;
    }
    if (IsFull(GetCtrl(i))) {
      if (DetailsAt(i) != other->DetailsAt(i)) return false;
    }
  }
  for (int i = 0; i < UsedCapacity(); i++) {
    if (EntryForEnumerationIndex(i) != other->EntryForEnumerationIndex(i)) {
      return false;
    }
  }
  return true;
}

// static
Handle<SwissNameDictionary> SwissNameDictionary::ShallowCopy(
    Isolate* isolate, Handle<SwissNameDictionary> table) {
  // TODO(v8:11388) Consider doing some cleanup during copying: For example, we
  // could turn kDeleted into kEmpty in certain situations. But this would
  // require tidying up the enumeration table in a similar fashion as would be
  // required when trying to re-use deleted entries.

  if (table->Capacity() == 0) {
    return table;
  }

  int capacity = table->Capacity();
  int used_capacity = table->UsedCapacity();

  Handle<SwissNameDictionary> new_table =
      isolate->factory()->NewSwissNameDictionaryWithCapacity(
          capacity, Heap::InYoungGeneration(*table) ? AllocationType::kYoung
                                                    : AllocationType::kOld);

  new_table->SetHash(table->Hash());

  DisallowGarbageCollection no_gc;
  WriteBarrierMode mode = new_table->GetWriteBarrierMode(no_gc);

  if (mode == WriteBarrierMode::SKIP_WRITE_BARRIER) {
    // Copy data table and ctrl table, which are stored next to each other.
    void* original_start =
        reinterpret_cast<void*>(table->field_address(DataTableStartOffset()));
    void* new_table_start = reinterpret_cast<void*>(
        new_table->field_address(DataTableStartOffset()));
    size_t bytes_to_copy = DataTableSize(capacity) + CtrlTableSize(capacity);
    DCHECK(DataTableEndOffset(capacity) == CtrlTableStartOffset(capacity));
    MemCopy(new_table_start, original_start, bytes_to_copy);
  } else {
    DCHECK_EQ(UPDATE_WRITE_BARRIER, mode);

    // We may have to trigger write barriers when copying the data table.
    for (int i = 0; i < capacity; ++i) {
      Tagged<Object> key = table->KeyAt(i);
      Tagged<Object> value = table->ValueAtRaw(i);

      // Cannot use SetKey/ValueAtPut because they don't accept the hole as data
      // to store.
      new_table->StoreToDataTable(i, kDataTableKeyEntryIndex, key);
      new_table->StoreToDataTable(i, kDataTableValueEntryIndex, value);
    }

    void* original_ctrl_table = table->CtrlTable();
    void* new_ctrl_table = new_table->CtrlTable();
    MemCopy(new_ctrl_table, original_ctrl_table, CtrlTableSize(capacity));
  }

  // PropertyDetails table may contain uninitialized data for unused slots.
  for (int i = 0; i < capacity; ++i) {
    if (IsFull(table->GetCtrl(i))) {
      new_table->DetailsAtPut(i, table->DetailsAt(i));
    }
  }

  // Meta table is only initialized for the first 2 + UsedCapacity() entries,
  // where size of each entry depends on table capacity.
  int size_per_meta_table_entry = MetaTableSizePerEntryFor(capacity);
  int meta_table_used_bytes = (2 + used_capacity) * size_per_meta_table_entry;
  MemCopy(new_table->meta_table()->begin(), table->meta_table()->begin(),
          meta_table_used_bytes);

  return new_table;
}

// static
Handle<SwissNameDictionary> SwissNameDictionary::Shrink(
    Isolate* isolate, Handle<SwissNameDictionary> table) {
  // TODO(v8:11388) We're using the same logic to decide whether or not to
  // shrink as OrderedNameDictionary and NameDictionary here. We should compare
  // this with the logic used by Abseil's flat_hash_map, which has a heuristic
  // for triggering an (in-place) rehash on addition, but never shrinks the
  // table. Abseil's heuristic doesn't take the numbere of deleted elements into
  // account, because it doesn't track that.

  int nof = table->NumberOfElements();
  int capacity = table->Capacity();
  if (nof >= (capacity >> 2)) return table;
  int new_capacity = std::max(capacity / 2, kInitialCapacity);
  return Rehash(isolate, table, new_capacity);
}

// TODO(v8::11388) Copying all data into a std::vector and then re-adding into
// the table doesn't seem like a good algorithm. Abseil's Swiss Tables come with
// a clever algorithm for re-hashing in place: It first changes the control
// table, effectively changing the roles of full, empty and deleted buckets. It
// then moves each entry to its new bucket by swapping entries (see
// drop_deletes_without_resize in Abseil's raw_hash_set.h). This algorithm could
// generally adapted to work on our insertion order preserving implementation,
// too. However, it would require a mapping from hash table buckets back to
// enumeration indices. This could either be be created in this function
// (requiring a vector with Capacity() entries and a separate pass over the
// enumeration table) or by creating this backwards mapping ahead of time and
// storing it somewhere in the main table or the meta table, for those
// SwissNameDictionaries that we know will be in-place rehashed, most notably
// those stored in the snapshot.
template <typename IsolateT>
void SwissNameDictionary::Rehash(IsolateT* isolate) {
  DisallowHeapAllocation no_gc;

  struct Entry {
    Tagged<Name> key;
    Tagged<Object> value;
    PropertyDetails details;
  };

  if (Capacity() == 0) return;

  Entry dummy{Tagged<Name>(), Tagged<Object>(), PropertyDetails::Empty()};
  std::vector<Entry> data(NumberOfElements(), dummy);

  ReadOnlyRoots roots(isolate);
  int data_index = 0;
  for (int enum_index = 0; enum_index < UsedCapacity(); ++enum_index) {
    int entry = EntryForEnumerationIndex(enum_index);
    Tagged<Object> key;
    if (!ToKey(roots, entry, &key)) continue;

    data[data_index++] =
        Entry{Cast<Name>(key), ValueAtRaw(entry), DetailsAt(entry)};
  }

  Initialize(isolate, meta_table(), Capacity());

  int new_enum_index = 0;
  SetNumberOfElements(static_cast<int>(data.size()));
  for (Entry& e : data) {
    int new_entry = AddInternal(e.key, e.value, e.details);

    // TODO(v8::11388) Investigate ways of hoisting the branching needed to
    // select the correct meta table entry size (based on the capacity of the
    // table) out of the loop.
    SetEntryForEnumerationIndex(new_enum_index, new_entry);
    ++new_enum_index;
  }
}

// TODO(emrich,v8:11388): This is almost an identical copy of
// HashTable<..>::NumberOfEnumerableProperties. Consolidate both versions
// elsewhere (e.g., hash-table-utils)?
int SwissNameDictionary::NumberOfEnumerableProperties() {
  ReadOnlyRoots roots = this->GetReadOnlyRoots();
  int result = 0;
  for (InternalIndex i : this->IterateEntries()) {
    Tagged<Object> k;
    if (!this->ToKey(roots, i, &k)) continue;
    if (Object::FilterKey(k, ENUMERABLE_STRINGS)) continue;
    PropertyDetails details = this->DetailsAt(i);
    PropertyAttributes attr = details.attributes();
    if ((int{attr} & ONLY_ENUMERABLE) == 0) result++;
  }
  return result;
}

// TODO(emrich, v8:11388): This is almost an identical copy of
// Dictionary<..>::SlowReverseLookup. Consolidate both versions elsewhere (e.g.,
// hash-table-utils)?
Tagged<Object> SwissNameDictionary::SlowReverseLookup(Isolate* isolate,
                                                      Tagged<Object> value) {
  ReadOnlyRoots roots(isolate);
  for (InternalIndex i : IterateEntries()) {
    Tagged<Object> k;
    if (!ToKey(roots, i, &k)) continue;
    Tagged<Object> e = this->ValueAt(i);
    if (e == value) return k;
  }
  return roots.undefined_value();
}

// The largest value we ever have to store in the enumeration table is
// Capacity() - 1. The largest value we ever have to store for the present or
// deleted element count is MaxUsableCapacity(Capacity()). All data in the
// meta table is unsigned. Using this, we verify the values of the constants
// |kMax1ByteMetaTableCapacity| and |kMax2ByteMetaTableCapacity|.
static_assert(SwissNameDictionary::kMax1ByteMetaTableCapacity - 1 <=
              std::numeric_limits<uint8_t>::max());
static_assert(SwissNameDictionary::MaxUsableCapacity(
                  SwissNameDictionary::kMax1ByteMetaTableCapacity) <=
              std::numeric_limits<uint8_t>::max());
static_assert(SwissNameDictionary::kMax2ByteMetaTableCapacity - 1 <=
              std::numeric_limits<uint16_t>::max());
static_assert(SwissNameDictionary::MaxUsableCapacity(
                  SwissNameDictionary::kMax2ByteMetaTableCapacity) <=
              std::numeric_limits<uint16_t>::max());

template V8_EXPORT_PRIVATE void SwissNameDictionary::Initialize(
    Isolate* isolate, Tagged<ByteArray> meta_table, int capacity);
template V8_EXPORT_PRIVATE void SwissNameDictionary::Initialize(
    LocalIsolate* isolate, Tagged<ByteArray> meta_table, int capacity);

template V8_EXPORT_PRIVATE Handle<SwissNameDictionary>
SwissNameDictionary::Rehash(LocalIsolate* isolate,
                            DirectHandle<SwissNameDictionary> table,
                            int new_capacity);
template V8_EXPORT_PRIVATE Handle<SwissNameDictionary>
SwissNameDictionary::Rehash(Isolate* isolate,
                            DirectHandle<SwissNameDictionary> table,
                            int new_capacity);

template V8_EXPORT_PRIVATE void SwissNameDictionary::Rehash(
    LocalIsolate* isolate);
template V8_EXPORT_PRIVATE void SwissNameDictionary::Rehash(Isolate* isolate);

constexpr int SwissNameDictionary::kInitialCapacity;
constexpr int SwissNameDictionary::kGroupWidth;

}  // namespace internal
}  // namespace v8
