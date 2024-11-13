// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_OBJECTS_STRING_TABLE_H_
#define V8_OBJECTS_STRING_TABLE_H_

#include "src/common/assert-scope.h"
#include "src/objects/string.h"
#include "src/roots/roots.h"

// Has to be the last include (doesn't have include guards):
#include "src/objects/object-macros.h"

namespace v8 {
namespace internal {

// A generic key for lookups into the string table, which allows heteromorphic
// lookup and on-demand creation of new strings.
class StringTableKey {
 public:
  virtual ~StringTableKey() = default;
  inline StringTableKey(uint32_t raw_hash_field, int length);

  uint32_t raw_hash_field() const {
    DCHECK_NE(0, raw_hash_field_);
    return raw_hash_field_;
  }

  inline uint32_t hash() const;
  int length() const { return length_; }

 protected:
  inline void set_raw_hash_field(uint32_t raw_hash_field);

 private:
  uint32_t raw_hash_field_ = 0;
  int length_;
};

class SeqOneByteString;

// StringTable, for internalizing strings. The Lookup methods are designed to be
// thread-safe, in combination with GC safepoints.
//
// The string table layout is defined by its Data implementation class, see
// StringTable::Data for details.
class V8_EXPORT_PRIVATE StringTable {
 public:
  static constexpr Tagged<Smi> empty_element() { return Smi::FromInt(0); }
  static constexpr Tagged<Smi> deleted_element() { return Smi::FromInt(1); }

  explicit StringTable(Isolate* isolate);
  ~StringTable();

  int Capacity() const;
  int NumberOfElements() const;

  // Find string in the string table. If it is not there yet, it is
  // added. The return value is the string found.
  DirectHandle<String> LookupString(Isolate* isolate, DirectHandle<String> key);

  // Find string in the string table, using the given key. If the string is not
  // there yet, it is created (by the key) and added. The return value is the
  // string found.
  template <typename StringTableKey, typename IsolateT>
  DirectHandle<String> LookupKey(IsolateT* isolate, StringTableKey* key);

  // {raw_string} must be a tagged String pointer.
  // Returns a tagged pointer: either a Smi if the string is an array index, an
  // internalized string, or a Smi sentinel.
  static Address TryStringToIndexOrLookupExisting(Isolate* isolate,
                                                  Address raw_string);

  // Insert a range of strings. Only for use during isolate deserialization.
  void InsertForIsolateDeserialization(
      Isolate* isolate, const base::Vector<DirectHandle<String>>& strings);

  // Insert the single empty string. Only for use during heap bootstrapping.
  void InsertEmptyStringForBootstrapping(Isolate* isolate);

  void Print(PtrComprCageBase cage_base) const;
  size_t GetCurrentMemoryUsage() const;

  // The following methods must be called either while holding the write lock,
  // or while in a Heap safepoint.
  void IterateElements(RootVisitor* visitor);
  void DropOldData();
  void NotifyElementsRemoved(int count);

  void VerifyIfOwnedBy(Isolate* isolate);

 private:
  class OffHeapStringHashSet;
  class Data;

  Data* EnsureCapacity(PtrComprCageBase cage_base, int additional_elements);

  std::atomic<Data*> data_;
  // Write mutex is mutable so that readers of concurrently mutated values (e.g.
  // NumberOfElements) are allowed to lock it while staying const.
  mutable base::Mutex write_mutex_;
  Isolate* isolate_;
};

}  // namespace internal
}  // namespace v8

#include "src/objects/object-macros-undef.h"

#endif  // V8_OBJECTS_STRING_TABLE_H_
