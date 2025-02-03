// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/objects/string-table.h"

#include <atomic>

#include "src/base/atomicops.h"
#include "src/base/macros.h"
#include "src/common/assert-scope.h"
#include "src/common/globals.h"
#include "src/common/ptr-compr-inl.h"
#include "src/execution/isolate-utils-inl.h"
#include "src/heap/safepoint.h"
#include "src/objects/internal-index.h"
#include "src/objects/object-list-macros.h"
#include "src/objects/off-heap-hash-table-inl.h"
#include "src/objects/slots-inl.h"
#include "src/objects/slots.h"
#include "src/objects/string-inl.h"
#include "src/objects/string-table-inl.h"
#include "src/snapshot/deserializer.h"
#include "src/utils/allocation.h"
#include "src/utils/ostreams.h"

namespace v8 {
namespace internal {

class StringTable::OffHeapStringHashSet
    : public OffHeapHashTableBase<OffHeapStringHashSet> {
 public:
  static constexpr int kEntrySize = 1;
  static constexpr int kMaxEmptyFactor = 4;
  static constexpr int kMinCapacity = 2048;

  explicit OffHeapStringHashSet(int capacity)
      : OffHeapHashTableBase<OffHeapStringHashSet>(capacity) {}

  static uint32_t Hash(PtrComprCageBase, Tagged<Object> key) {
    return Cast<String>(key)->hash();
  }

  template <typename IsolateT, typename StringTableKey>
  static bool KeyIsMatch(IsolateT* isolate, StringTableKey* key,
                         Tagged<Object> obj) {
    auto string = Cast<String>(obj);
    if (string->hash() != key->hash()) return false;
    if (string->length() != key->length()) return false;
    return key->IsMatch(isolate, string);
  }

  Tagged<Object> GetKey(PtrComprCageBase cage_base, InternalIndex index) const {
    return slot(index).Acquire_Load(cage_base);
  }

  void SetKey(InternalIndex index, Tagged<Object> key) {
    DCHECK(IsString(key));
    slot(index).Release_Store(key);
  }
  void Set(InternalIndex index, Tagged<String> key) { SetKey(index, key); }

  void CopyEntryExcludingKeyInto(PtrComprCageBase, InternalIndex,
                                 OffHeapStringHashSet*, InternalIndex) {
    // Do nothing, since the entry size is 1 (just the key).
  }

 private:
  friend class StringTable::Data;
};

// Data holds the actual data of the string table, including capacity and number
// of elements.
//
// It is a variable sized structure, with a "header" followed directly in memory
// by the elements themselves. These are accessed as offsets from the elements_
// field, which itself provides storage for the first element.
//
// The elements themselves are stored as an open-addressed hash table, with
// quadratic probing and Smi 0 and Smi 1 as the empty and deleted sentinels,
// respectively.
class StringTable::Data {
 public:
  static std::unique_ptr<Data> New(int capacity);
  static std::unique_ptr<Data> Resize(PtrComprCageBase cage_base,
                                      std::unique_ptr<Data> data, int capacity);

  void* operator new(size_t size, int capacity);
  void* operator new(size_t size) = delete;
  void operator delete(void* description);

  OffHeapStringHashSet& table() { return table_; }
  const OffHeapStringHashSet& table() const { return table_; }

  // Helper method for StringTable::TryStringToIndexOrLookupExisting.
  template <typename Char>
  static Address TryStringToIndexOrLookupExisting(Isolate* isolate,
                                                  Tagged<String> string,
                                                  Tagged<String> source,
                                                  size_t start);

  void IterateElements(RootVisitor* visitor) {
    table_.IterateElements(Root::kStringTable, visitor);
  }

  Data* PreviousData() { return previous_data_.get(); }
  void DropPreviousData() { previous_data_.reset(); }

  void Print(PtrComprCageBase cage_base) const;
  size_t GetCurrentMemoryUsage() const;

 private:
  explicit Data(int capacity) : table_(capacity) {}

  std::unique_ptr<Data> previous_data_;
  OffHeapStringHashSet table_;
};

void* StringTable::Data::operator new(size_t size, int capacity) {
  // Make sure the size given is the size of the Data structure.
  DCHECK_EQ(size, sizeof(StringTable::Data));
  return OffHeapStringHashSet::Allocate<Data, offsetof(Data, table_.elements_)>(
      capacity);
}

void StringTable::Data::operator delete(void* table) {
  OffHeapStringHashSet::Free(table);
}

size_t StringTable::Data::GetCurrentMemoryUsage() const {
  size_t usage = sizeof(*this) + table_.GetSizeExcludingHeader();
  if (previous_data_) {
    usage += previous_data_->GetCurrentMemoryUsage();
  }
  return usage;
}

std::unique_ptr<StringTable::Data> StringTable::Data::New(int capacity) {
  return std::unique_ptr<Data>(new (capacity) Data(capacity));
}

std::unique_ptr<StringTable::Data> StringTable::Data::Resize(
    PtrComprCageBase cage_base, std::unique_ptr<Data> data, int capacity) {
  std::unique_ptr<Data> new_data(new (capacity) Data(capacity));
  data->table_.RehashInto(cage_base, &new_data->table_);
  new_data->previous_data_ = std::move(data);
  return new_data;
}

void StringTable::Data::Print(PtrComprCageBase cage_base) const {
  OFStream os(stdout);
  os << "StringTable {" << std::endl;
  for (InternalIndex i : InternalIndex::Range(table_.capacity())) {
    os << "  " << i.as_uint32() << ": " << Brief(table_.GetKey(cage_base, i))
       << std::endl;
  }
  os << "}" << std::endl;
}

StringTable::StringTable(Isolate* isolate)
    : data_(Data::New(OffHeapStringHashSet::kMinCapacity).release()),
      isolate_(isolate) {
  DCHECK_EQ(empty_element(), OffHeapStringHashSet::empty_element());
  DCHECK_EQ(deleted_element(), OffHeapStringHashSet::deleted_element());
}

StringTable::~StringTable() { delete data_; }

int StringTable::Capacity() const {
  return data_.load(std::memory_order_acquire)->table().capacity();
}
int StringTable::NumberOfElements() const {
  {
    base::MutexGuard table_write_guard(&write_mutex_);
    return data_.load(std::memory_order_relaxed)->table().number_of_elements();
  }
}

// InternalizedStringKey carries a string/internalized-string object as key.
class InternalizedStringKey final : public StringTableKey {
 public:
  explicit InternalizedStringKey(DirectHandle<String> string, uint32_t hash)
      : StringTableKey(hash, string->length()), string_(string) {
    // When sharing the string table, it's possible that another thread already
    // internalized the key, in which case StringTable::LookupKey will perform a
    // redundant lookup and return the already internalized copy.
    DCHECK_IMPLIES(!v8_flags.shared_string_table,
                   !IsInternalizedString(*string));
    DCHECK(string->IsFlat());
    DCHECK(String::IsHashFieldComputed(hash));
  }

  bool IsMatch(Isolate* isolate, Tagged<String> string) {
    DCHECK(!SharedStringAccessGuardIfNeeded::IsNeeded(string));
    return string_->SlowEquals(string);
  }

  void PrepareForInsertion(Isolate* isolate) {
    StringTransitionStrategy strategy =
        isolate->factory()->ComputeInternalizationStrategyForString(
            string_, &maybe_internalized_map_);
    switch (strategy) {
      case StringTransitionStrategy::kCopy:
        break;
      case StringTransitionStrategy::kInPlace:
        // In-place transition will be done in GetHandleForInsertion, when we
        // are sure that we are going to insert the string into the table.
        return;
      case StringTransitionStrategy::kAlreadyTransitioned:
        // We can see already internalized strings here only when sharing the
        // string table and allowing concurrent internalization.
        DCHECK(v8_flags.shared_string_table);
        internalized_string_ = string_;
        return;
    }

    // Copying the string here is always threadsafe, as no instance type
    // requiring a copy can transition any further.
    StringShape shape(*string_);
    // External strings get special treatment, to avoid copying their
    // contents as long as they are not uncached or the string table is shared.
    // If the string table is shared, another thread could lookup a string with
    // the same content before this thread completes MakeThin (which sets the
    // resource), resulting in a string table hit returning the string we just
    // created that is not correctly initialized.
    const bool can_avoid_copy =
        !v8_flags.shared_string_table && !shape.IsUncachedExternal();
    if (can_avoid_copy && shape.IsExternalOneByte()) {
      // Shared external strings are always in-place internalizable.
      // If this assumption is invalidated in the future, make sure that we
      // fully initialize (copy contents) for shared external strings, as the
      // original string is not transitioned to a ThinString (setting the
      // resource) immediately.
      DCHECK(!shape.IsShared());
      internalized_string_ =
          isolate->factory()->InternalizeExternalString<ExternalOneByteString>(
              string_);
    } else if (can_avoid_copy && shape.IsExternalTwoByte()) {
      // Shared external strings are always in-place internalizable.
      // If this assumption is invalidated in the future, make sure that we
      // fully initialize (copy contents) for shared external strings, as the
      // original string is not transitioned to a ThinString (setting the
      // resource) immediately.
      DCHECK(!shape.IsShared());
      internalized_string_ =
          isolate->factory()->InternalizeExternalString<ExternalTwoByteString>(
              string_);
    } else {
      // Otherwise allocate a new internalized string.
      internalized_string_ = isolate->factory()->NewInternalizedStringImpl(
          string_, length(), raw_hash_field());
    }
  }

  DirectHandle<String> GetHandleForInsertion() {
    DirectHandle<Map> internalized_map;
    // When preparing the string, the strategy was to in-place migrate it.
    if (maybe_internalized_map_.ToHandle(&internalized_map)) {
      // It is always safe to overwrite the map. The only transition possible
      // is another thread migrated the string to internalized already.
      // Migrations to thin are impossible, as we only call this method on table
      // misses inside the critical section.
      string_->set_map_safe_transition_no_write_barrier(*internalized_map);
      DCHECK(IsInternalizedString(*string_));
      return string_;
    }
    // We prepared an internalized copy for the string or the string was already
    // internalized.
    // In theory we could have created a copy of a SeqString in young generation
    // that has been promoted to old space by now. In that case we could
    // in-place migrate the original string instead of internalizing the copy
    // and migrating the original string to a ThinString. This scenario doesn't
    // seem to be common enough to justify re-computing the strategy here.
    return internalized_string_.ToHandleChecked();
  }

 private:
  DirectHandle<String> string_;
  // Copy of the string to be internalized (only set if the string is not
  // in-place internalizable). We can't override the original string, as
  // internalized external strings don't set the resource directly (deferred to
  // MakeThin to ensure unique ownership of the resource), and thus would break
  // equality checks in case of hash collisions.
  MaybeDirectHandle<String> internalized_string_;
  MaybeDirectHandle<Map> maybe_internalized_map_;
};

namespace {

void SetInternalizedReference(Isolate* isolate, Tagged<String> string,
                              Tagged<String> internalized) {
  DCHECK(!IsThinString(string));
  DCHECK(!IsInternalizedString(string));
  DCHECK(IsInternalizedString(internalized));
  DCHECK(!internalized->HasInternalizedForwardingIndex(kAcquireLoad));
  if (string->IsShared() || v8_flags.always_use_string_forwarding_table) {
    uint32_t field = string->raw_hash_field(kAcquireLoad);
    // Don't use the forwarding table for strings that have an integer index.
    // Using the hash field for the integer index is more beneficial than
    // using it to store the forwarding index to the internalized string.
    if (Name::IsIntegerIndex(field)) return;
    // Check one last time if we already have an internalized forwarding index
    // to prevent too many copies of the string in the forwarding table.
    if (Name::IsInternalizedForwardingIndex(field)) return;

    // If we already have an entry for an external resource in the table, update
    // the entry instead of creating a new one. There is no guarantee that we
    // will always update existing records instead of creating new ones, but
    // races should be rare.
    if (Name::IsForwardingIndex(field)) {
      const int forwarding_index =
          Name::ForwardingIndexValueBits::decode(field);
      isolate->string_forwarding_table()->UpdateForwardString(forwarding_index,
                                                              internalized);
      // Update the forwarding index type to include internalized.
      field = Name::IsInternalizedForwardingIndexBit::update(field, true);
      string->set_raw_hash_field(field, kReleaseStore);
    } else {
      const int forwarding_index =
          isolate->string_forwarding_table()->AddForwardString(string,
                                                               internalized);
      string->set_raw_hash_field(
          String::CreateInternalizedForwardingIndex(forwarding_index),
          kReleaseStore);
    }
  } else {
    DCHECK(!string->HasForwardingIndex(kAcquireLoad));
    string->MakeThin(isolate, internalized);
  }
}

}  // namespace

DirectHandle<String> StringTable::LookupString(Isolate* isolate,
                                               DirectHandle<String> string) {
  // When sharing the string table, internalization is allowed to be concurrent
  // from multiple Isolates, assuming that:
  //
  //  - All in-place internalizable strings (i.e. old-generation flat strings)
  //    and internalized strings are in the shared heap.
  //  - LookupKey supports concurrent access (see comment below).
  //
  // These assumptions guarantee the following properties:
  //
  //  - String::Flatten is not threadsafe but is only called on non-shared
  //    strings, since non-flat strings are not shared.
  //
  //  - String::ComputeAndSetRawHash is threadsafe on flat strings. This is safe
  //    because the characters are immutable and the same hash will be
  //    computed. The hash field is set with relaxed memory order. A thread that
  //    doesn't see the hash may do redundant work but will not be incorrect.
  //
  //  - In-place internalizable strings do not incur a copy regardless of string
  //    table sharing. The map mutation is threadsafe even with relaxed memory
  //    order, because for concurrent table lookups, the "losing" thread will be
  //    correctly ordered by LookupKey's write mutex and see the updated map
  //    during the re-lookup.
  //
  // For lookup misses, the internalized string map is the same map in RO space
  // regardless of which thread is doing the lookup.
  //
  // For lookup hits, we use the StringForwardingTable for shared strings to
  // delay the transition into a ThinString to the next stop-the-world GC.
  DirectHandle<String> result =
      String::Flatten(isolate, indirect_handle(string, isolate));
  if (!IsInternalizedString(*result)) {
    uint32_t raw_hash_field = result->raw_hash_field(kAcquireLoad);

    if (String::IsInternalizedForwardingIndex(raw_hash_field)) {
      const int index =
          String::ForwardingIndexValueBits::decode(raw_hash_field);
      result = direct_handle(
          isolate->string_forwarding_table()->GetForwardString(isolate, index),
          isolate);
    } else {
      if (!Name::IsHashFieldComputed(raw_hash_field)) {
        raw_hash_field = result->EnsureRawHash();
      }
      InternalizedStringKey key(result, raw_hash_field);
      result = LookupKey(isolate, &key);
    }
  }
  if (*string != *result && !IsThinString(*string)) {
    SetInternalizedReference(isolate, *string, *result);
  }
  return result;
}

template <typename StringTableKey, typename IsolateT>
DirectHandle<String> StringTable::LookupKey(IsolateT* isolate,
                                            StringTableKey* key) {
  // String table lookups are allowed to be concurrent, assuming that:
  //
  //   - The Heap access is allowed to be concurrent (using LocalHeap or
  //     similar),
  //   - All writes to the string table are guarded by the Isolate string table
  //     mutex,
  //   - Resizes of the string table first copies the old contents to the new
  //     table, and only then sets the new string table pointer to the new
  //     table,
  //   - Only GCs can remove elements from the string table.
  //
  // These assumptions allow us to make the following statement:
  //
  //   "Reads are allowed when not holding the lock, as long as false negatives
  //    (misses) are ok. We will never get a false positive (hit of an entry no
  //    longer in the table)"
  //
  // This is because we _know_ that if we find an entry in the string table, any
  // entry will also be in all reallocations of that tables. This is required
  // for strong consistency of internalized string equality implying reference
  // equality.
  //
  // We therefore try to optimistically read from the string table without
  // taking the lock (both here and in the NoAllocate version of the lookup),
  // and on a miss we take the lock and try to write the entry, with a second
  // read lookup in case the non-locked read missed a write.
  //
  // One complication is allocation -- we don't want to allocate while holding
  // the string table lock. This applies to both allocation of new strings, and
  // re-allocation of the string table on resize. So, we optimistically allocate
  // (without copying values) outside the lock, and potentially discard the
  // allocation if another write also did an allocation. This assumes that
  // writes are rarer than reads.

  // Load the current string table data, in case another thread updates the
  // data while we're reading.
  Data* const current_data = data_.load(std::memory_order_acquire);
  OffHeapStringHashSet& current_table = current_data->table();

  // First try to find the string in the table. This is safe to do even if the
  // table is now reallocated; we won't find a stale entry in the old table
  // because the new table won't delete it's corresponding entry until the
  // string is dead, in which case it will die in this table too and worst
  // case we'll have a false miss.
  InternalIndex entry = current_table.FindEntry(isolate, key, key->hash());
  if (entry.is_found()) {
    DirectHandle<String> result(
        Cast<String>(current_table.GetKey(isolate, entry)), isolate);
    DCHECK_IMPLIES(v8_flags.shared_string_table, InAnySharedSpace(*result));
    return result;
  }

  // No entry found, so adding new string.
  key->PrepareForInsertion(isolate);
  {
    base::MutexGuard table_write_guard(&write_mutex_);

    Data* data = EnsureCapacity(isolate, 1);
    OffHeapStringHashSet& table = data->table();

    // Check one last time if the key is present in the table, in case it was
    // added after the check.
    entry = table.FindEntryOrInsertionEntry(isolate, key, key->hash());

    Tagged<Object> element = table.GetKey(isolate, entry);
    if (element == OffHeapStringHashSet::empty_element()) {
      // This entry is empty, so write it and register that we added an
      // element.
      DirectHandle<String> new_string = key->GetHandleForInsertion();
      DCHECK_IMPLIES(v8_flags.shared_string_table, new_string->IsShared());
      table.AddAt(isolate, entry, *new_string);
      return new_string;
    } else if (element == OffHeapStringHashSet::deleted_element()) {
      // This entry was deleted, so overwrite it and register that we
      // overwrote a deleted element.
      DirectHandle<String> new_string = key->GetHandleForInsertion();
      DCHECK_IMPLIES(v8_flags.shared_string_table, new_string->IsShared());
      table.OverwriteDeletedAt(isolate, entry, *new_string);
      return new_string;
    } else {
      // Return the existing string as a handle.
      return direct_handle(Cast<String>(element), isolate);
    }
  }
}

template DirectHandle<String> StringTable::LookupKey(Isolate* isolate,
                                                     OneByteStringKey* key);
template DirectHandle<String> StringTable::LookupKey(Isolate* isolate,
                                                     TwoByteStringKey* key);
template DirectHandle<String> StringTable::LookupKey(
    Isolate* isolate, SeqOneByteSubStringKey* key);
template DirectHandle<String> StringTable::LookupKey(
    Isolate* isolate, SeqTwoByteSubStringKey* key);

template DirectHandle<String> StringTable::LookupKey(LocalIsolate* isolate,
                                                     OneByteStringKey* key);
template DirectHandle<String> StringTable::LookupKey(LocalIsolate* isolate,
                                                     TwoByteStringKey* key);

template DirectHandle<String> StringTable::LookupKey(
    Isolate* isolate, StringTableInsertionKey* key);
template DirectHandle<String> StringTable::LookupKey(
    LocalIsolate* isolate, StringTableInsertionKey* key);

StringTable::Data* StringTable::EnsureCapacity(PtrComprCageBase cage_base,
                                               int additional_elements) {
  // This call is only allowed while the write mutex is held.
  write_mutex_.AssertHeld();

  // This load can be relaxed as the table pointer can only be modified while
  // the lock is held.
  Data* data = data_.load(std::memory_order_relaxed);

  int new_capacity;
  if (data->table().ShouldResizeToAdd(additional_elements, &new_capacity)) {
    std::unique_ptr<Data> new_data =
        Data::Resize(cage_base, std::unique_ptr<Data>(data), new_capacity);
    // `new_data` is the new owner of `data`.
    DCHECK_EQ(new_data->PreviousData(), data);
    // Release-store the new data pointer as `data_`, so that it can be
    // acquire-loaded by other threads. This string table becomes the owner of
    // the pointer.
    data = new_data.release();
    data_.store(data, std::memory_order_release);
  }

  return data;
}

namespace {
template <typename Char>
class CharBuffer {
 public:
  void Reset(size_t length) {
    if (length >= kInlinedBufferSize)
      outofline_ = std::make_unique<Char[]>(length);
  }

  Char* Data() {
    if (outofline_)
      return outofline_.get();
    else
      return inlined_;
  }

 private:
  static constexpr size_t kInlinedBufferSize = 256;
  Char inlined_[kInlinedBufferSize];
  std::unique_ptr<Char[]> outofline_;
};
}  // namespace

// static
template <typename Char>
Address StringTable::Data::TryStringToIndexOrLookupExisting(
    Isolate* isolate, Tagged<String> string, Tagged<String> source,
    size_t start) {
  // TODO(leszeks): This method doesn't really belong on StringTable::Data.
  // Ideally it would be a free function in an anonymous namespace, but that
  // causes issues around method and class visibility.

  DisallowGarbageCollection no_gc;

  int length = string->length();
  // The source hash is usable if it is not from a sliced string.
  // For sliced strings we need to recalculate the hash from the given offset
  // with the correct length.
  const bool is_source_hash_usable = start == 0 && length == source->length();

  // First check if the string constains a forwarding index.
  uint32_t raw_hash_field = source->raw_hash_field(kAcquireLoad);
  if (Name::IsInternalizedForwardingIndex(raw_hash_field) &&
      is_source_hash_usable) {
    const int index = Name::ForwardingIndexValueBits::decode(raw_hash_field);
    Tagged<String> internalized =
        isolate->string_forwarding_table()->GetForwardString(isolate, index);
    return internalized.ptr();
  }

  uint64_t seed = HashSeed(isolate);

  CharBuffer<Char> buffer;
  const Char* chars;

  SharedStringAccessGuardIfNeeded access_guard(isolate);
  if (IsConsString(source, isolate)) {
    DCHECK(!source->IsFlat());
    buffer.Reset(length);
    String::WriteToFlat(source, buffer.Data(), 0, length, access_guard);
    chars = buffer.Data();
  } else {
    chars = source->GetDirectStringChars<Char>(no_gc, access_guard) + start;
  }

  if (!Name::IsHashFieldComputed(raw_hash_field) || !is_source_hash_usable) {
    raw_hash_field =
        StringHasher::HashSequentialString<Char>(chars, length, seed);
  }
  // TODO(verwaest): Internalize to one-byte when possible.
  SequentialStringKey<Char> key(raw_hash_field,
                                base::Vector<const Char>(chars, length), seed);

  // String could be an array index.
  if (Name::ContainsCachedArrayIndex(raw_hash_field)) {
    return Smi::FromInt(String::ArrayIndexValueBits::decode(raw_hash_field))
        .ptr();
  }

  if (Name::IsIntegerIndex(raw_hash_field)) {
    // It is an index, but it's not cached.
    return Smi::FromInt(ResultSentinel::kUnsupported).ptr();
  }

  Data* string_table_data =
      isolate->string_table()->data_.load(std::memory_order_acquire);

  InternalIndex entry =
      string_table_data->table().FindEntry(isolate, &key, key.hash());
  if (entry.is_not_found()) {
    // A string that's not an array index, and not in the string table,
    // cannot have been used as a property name before.
    return Smi::FromInt(ResultSentinel::kNotFound).ptr();
  }

  Tagged<String> internalized =
      Cast<String>(string_table_data->table().GetKey(isolate, entry));
  // string can be internalized here, if another thread internalized it.
  // If we found and entry in the string table and string is not internalized,
  // there is no way that it can transition to internalized later on. So a last
  // check here is sufficient.
  if (!IsInternalizedString(string)) {
    SetInternalizedReference(isolate, string, internalized);
  } else {
    DCHECK(v8_flags.shared_string_table);
  }
  return internalized.ptr();
}

// static
Address StringTable::TryStringToIndexOrLookupExisting(Isolate* isolate,
                                                      Address raw_string) {
  Tagged<String> string = Cast<String>(Tagged<Object>(raw_string));
  if (IsInternalizedString(string)) {
    // string could be internalized, if the string table is shared and another
    // thread internalized it.
    DCHECK(v8_flags.shared_string_table);
    return raw_string;
  }

  // Valid array indices are >= 0, so they cannot be mixed up with any of
  // the result sentinels, which are negative.
  static_assert(
      !String::ArrayIndexValueBits::is_valid(ResultSentinel::kUnsupported));
  static_assert(
      !String::ArrayIndexValueBits::is_valid(ResultSentinel::kNotFound));

  size_t start = 0;
  Tagged<String> source = string;
  if (IsSlicedString(source)) {
    Tagged<SlicedString> sliced = Cast<SlicedString>(source);
    start = sliced->offset();
    source = sliced->parent();
  } else if (IsConsString(source) && source->IsFlat()) {
    source = Cast<ConsString>(source)->first();
  }
  if (IsThinString(source)) {
    source = Cast<ThinString>(source)->actual();
    if (string->length() == source->length()) {
      return source.ptr();
    }
  }

  if (source->IsOneByteRepresentation()) {
    return StringTable::Data::TryStringToIndexOrLookupExisting<uint8_t>(
        isolate, string, source, start);
  }
  return StringTable::Data::TryStringToIndexOrLookupExisting<uint16_t>(
      isolate, string, source, start);
}

void StringTable::InsertForIsolateDeserialization(
    Isolate* isolate, const base::Vector<DirectHandle<String>>& strings) {
  DCHECK_EQ(NumberOfElements(), 0);

  const int length = static_cast<int>(strings.size());
  {
    base::MutexGuard table_write_guard(&write_mutex_);

    Data* const data = EnsureCapacity(isolate, length);

    for (const DirectHandle<String>& s : strings) {
      StringTableInsertionKey key(
          isolate, s, DeserializingUserCodeOption::kNotDeserializingUserCode);
      InternalIndex entry =
          data->table().FindEntryOrInsertionEntry(isolate, &key, key.hash());

      DirectHandle<String> inserted_string = key.GetHandleForInsertion();
      DCHECK_IMPLIES(v8_flags.shared_string_table, inserted_string->IsShared());
      data->table().AddAt(isolate, entry, *inserted_string);
    }
  }

  DCHECK_EQ(NumberOfElements(), length);
}

void StringTable::InsertEmptyStringForBootstrapping(Isolate* isolate) {
  DCHECK_EQ(NumberOfElements(), 0);
  {
    base::MutexGuard table_write_guard(&write_mutex_);

    Data* const data = EnsureCapacity(isolate, 1);

    DirectHandle<String> empty_string =
        ReadOnlyRoots(isolate).empty_string_handle();
    uint32_t hash = empty_string->EnsureHash();

    InternalIndex entry = data->table().FindInsertionEntry(isolate, hash);

    DCHECK_IMPLIES(v8_flags.shared_string_table, empty_string->IsShared());
    data->table().AddAt(isolate, entry, *empty_string);
  }
  DCHECK_EQ(NumberOfElements(), 1);
}

void StringTable::Print(PtrComprCageBase cage_base) const {
  data_.load(std::memory_order_acquire)->Print(cage_base);
}

size_t StringTable::GetCurrentMemoryUsage() const {
  return sizeof(*this) +
         data_.load(std::memory_order_acquire)->GetCurrentMemoryUsage();
}

void StringTable::IterateElements(RootVisitor* visitor) {
  // This should only happen during garbage collection when background threads
  // are paused, so the load can be relaxed.
  isolate_->heap()->safepoint()->AssertActive();
  data_.load(std::memory_order_relaxed)->IterateElements(visitor);
}

void StringTable::DropOldData() {
  // This should only happen during garbage collection when background threads
  // are paused, so the load can be relaxed.
  isolate_->heap()->safepoint()->AssertActive();
  DCHECK_NE(isolate_->heap()->gc_state(), Heap::NOT_IN_GC);
  data_.load(std::memory_order_relaxed)->DropPreviousData();
}

void StringTable::NotifyElementsRemoved(int count) {
  // This should only happen during garbage collection when background threads
  // are paused, so the load can be relaxed.
  isolate_->heap()->safepoint()->AssertActive();
  DCHECK_NE(isolate_->heap()->gc_state(), Heap::NOT_IN_GC);
  data_.load(std::memory_order_relaxed)->table().ElementsRemoved(count);
}

}  // namespace internal
}  // namespace v8
