// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <optional>

#include "src/objects/swiss-name-dictionary-inl.h"
#include "test/cctest/cctest.h"
#include "test/cctest/test-swiss-name-dictionary-infra.h"
#include "test/cctest/test-swiss-name-dictionary-shared-tests.h"

namespace v8 {
namespace internal {
namespace test_swiss_hash_table {

// Executes tests by executing C++ versions of dictionary operations.
class RuntimeTestRunner {
 public:
  RuntimeTestRunner(Isolate* isolate, int initial_capacity, KeyCache& keys)
      : isolate_{isolate}, keys_{keys} {
    table = isolate->factory()->NewSwissNameDictionaryWithCapacity(
        initial_capacity, AllocationType::kYoung);
  }

  // The runtime implementations does not depend on the CPU features and
  // therefore always work.
  static bool IsEnabled() { return true; }

  void Add(DirectHandle<Name> key, DirectHandle<Object> value,
           PropertyDetails details);
  InternalIndex FindEntry(DirectHandle<Name> key);
  // Updates the value and property details of the given entry.
  void Put(InternalIndex entry, DirectHandle<Object> new_value,
           PropertyDetails new_details);
  void Delete(InternalIndex entry);
  void RehashInplace();
  void Shrink();

  // Retrieves data associated with |entry|, which must be an index pointing to
  // an existing entry. The returned array contains key, value, property details
  // in that order.
  DirectHandle<FixedArray> GetData(InternalIndex entry);

  // Tests that the current table has the given capacity, and number of
  // (deleted) elements, based on which optional values are present.
  void CheckCounts(std::optional<int> capacity, std::optional<int> elements,
                   std::optional<int> deleted);
  // Checks that |expected_keys| contains exactly the keys in the current table,
  // in the given order.
  void CheckEnumerationOrder(const std::vector<std::string>& expected_keys);
  void CheckCopy();
  void VerifyHeap();

  // Just for debugging.
  void PrintTable();

  DirectHandle<SwissNameDictionary> table;

 private:
  Isolate* isolate_;
  KeyCache& keys_;
};

void RuntimeTestRunner::Add(DirectHandle<Name> key, DirectHandle<Object> value,
                            PropertyDetails details) {
  DirectHandle<SwissNameDictionary> updated_table =
      SwissNameDictionary::Add(isolate_, this->table, key, value, details);
  this->table = updated_table;
}

InternalIndex RuntimeTestRunner::FindEntry(DirectHandle<Name> key) {
  return table->FindEntry(isolate_, key);
}

DirectHandle<FixedArray> RuntimeTestRunner::GetData(InternalIndex entry) {
  if (entry.is_found()) {
    DirectHandle<FixedArray> data = isolate_->factory()->NewFixedArray(3);
    data->set(0, table->KeyAt(entry));
    data->set(1, table->ValueAt(entry));
    data->set(2, table->DetailsAt(entry).AsSmi());
    return data;
  } else {
    return direct_handle(ReadOnlyRoots(isolate_).empty_fixed_array(), isolate_);
  }
}

void RuntimeTestRunner::Put(InternalIndex entry, DirectHandle<Object> new_value,
                            PropertyDetails new_details) {
  CHECK(entry.is_found());

  table->ValueAtPut(entry, *new_value);
  table->DetailsAtPut(entry, new_details);
}

void RuntimeTestRunner::Delete(InternalIndex entry) {
  CHECK(entry.is_found());
  table = table->DeleteEntry(isolate_, table, entry);
}

void RuntimeTestRunner::CheckCounts(std::optional<int> capacity,
                                    std::optional<int> elements,
                                    std::optional<int> deleted) {
  if (capacity.has_value()) {
    CHECK_EQ(capacity.value(), table->Capacity());
  }
  if (elements.has_value()) {
    CHECK_EQ(elements.value(), table->NumberOfElements());
  }
  if (deleted.has_value()) {
    CHECK_EQ(deleted.value(), table->NumberOfDeletedElements());
  }
}

void RuntimeTestRunner::CheckEnumerationOrder(
    const std::vector<std::string>& expected_keys) {
  ReadOnlyRoots roots(isolate_);
  int i = 0;
  for (InternalIndex index : table->IterateEntriesOrdered()) {
    Tagged<Object> key;
    if (table->ToKey(roots, index, &key)) {
      CHECK_LT(i, expected_keys.size());
      DirectHandle<Name> expected_key =
          CreateKeyWithHash(isolate_, this->keys_, Key{expected_keys[i]});

      CHECK_EQ(key, *expected_key);
      ++i;
    }
  }
  CHECK_EQ(i, expected_keys.size());
}

void RuntimeTestRunner::RehashInplace() { table->Rehash(isolate_); }

void RuntimeTestRunner::Shrink() {
  table = SwissNameDictionary::Shrink(isolate_, table);
}

void RuntimeTestRunner::CheckCopy() {
  DirectHandle<SwissNameDictionary> copy =
      SwissNameDictionary::ShallowCopy(isolate_, table);

  CHECK(table->EqualsForTesting(*copy));
}

void RuntimeTestRunner::VerifyHeap() {
#if VERIFY_HEAP
  table->SwissNameDictionaryVerify(isolate_, true);
#endif
}

void RuntimeTestRunner::PrintTable() {
#ifdef OBJECT_PRINT
  table->SwissNameDictionaryPrint(std::cout);
#endif
}

TEST(CapacityFor) {
  for (int elements = 0; elements <= 32; elements++) {
    int capacity = SwissNameDictionary::CapacityFor(elements);
    if (elements == 0) {
      CHECK_EQ(0, capacity);
    } else if (elements <= 3) {
      CHECK_EQ(4, capacity);
    } else if (elements == 4) {
      CHECK_IMPLIES(SwissNameDictionary::kGroupWidth == 8, capacity == 8);
      CHECK_IMPLIES(SwissNameDictionary::kGroupWidth == 16, capacity == 4);
    } else if (elements <= 7) {
      CHECK_EQ(8, capacity);
    } else if (elements <= 14) {
      CHECK_EQ(16, capacity);
    } else if (elements <= 28) {
      CHECK_EQ(32, capacity);
    } else if (elements <= 32) {
      CHECK_EQ(64, capacity);
    }
  }
}

TEST(MaxUsableCapacity) {
  CHECK_EQ(0, SwissNameDictionary::MaxUsableCapacity(0));
  CHECK_IMPLIES(SwissNameDictionary::kGroupWidth == 8,
                SwissNameDictionary::MaxUsableCapacity(4) == 3);
  CHECK_IMPLIES(SwissNameDictionary::kGroupWidth == 16,
                SwissNameDictionary::MaxUsableCapacity(4) == 4);
  CHECK_EQ(7, SwissNameDictionary::MaxUsableCapacity(8));
  CHECK_EQ(14, SwissNameDictionary::MaxUsableCapacity(16));
  CHECK_EQ(28, SwissNameDictionary::MaxUsableCapacity(32));
}

TEST(SizeFor) {
  int baseline = HeapObject::kHeaderSize +
                 // prefix:
                 4 +
                 // capacity:
                 4 +
                 // meta table:
                 kTaggedSize;

  int size_0 = baseline +
               // ctrl table:
               SwissNameDictionary::kGroupWidth;

  int size_4 = baseline +
               // data table:
               4 * 2 * kTaggedSize +
               // ctrl table:
               4 + SwissNameDictionary::kGroupWidth +
               // property details table:
               4;

  int size_8 = baseline +
               // data table:
               8 * 2 * kTaggedSize +
               // ctrl table:
               8 + SwissNameDictionary::kGroupWidth +
               // property details table:
               8;

  CHECK_EQ(SwissNameDictionary::SizeFor(0), size_0);
  CHECK_EQ(SwissNameDictionary::SizeFor(4), size_4);
  CHECK_EQ(SwissNameDictionary::SizeFor(8), size_8);
}

// Executes the tests defined in test-swiss-name-dictionary-shared-tests.h as if
// they were defined in this file, using the RuntimeTestRunner. See comments in
// test-swiss-name-dictionary-shared-tests.h and in
// swiss-name-dictionary-infra.h for details.
const char kRuntimeTestFileName[] = __FILE__;
SharedSwissTableTests<RuntimeTestRunner, kRuntimeTestFileName>
    execute_shared_tests_runtime;

}  // namespace test_swiss_hash_table
}  // namespace internal
}  // namespace v8
