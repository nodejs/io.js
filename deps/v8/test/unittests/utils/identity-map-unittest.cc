// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/utils/identity-map.h"

#include <set>

#include "src/execution/isolate.h"
#include "src/heap/factory-inl.h"
#include "src/objects/heap-number-inl.h"
#include "src/objects/objects.h"
#include "src/zone/zone.h"
#include "test/unittests/heap/heap-utils.h"
#include "test/unittests/test-utils.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace v8 {
namespace internal {

// Helper for testing. A "friend" of the IdentityMapBase class, it is able to
// "move" objects to simulate GC for testing the internals of the map.
class IdentityMapTester {
 public:
  IdentityMap<void*, ZoneAllocationPolicy> map;

  IdentityMapTester(Heap* heap, Zone* zone)
      : map(heap, ZoneAllocationPolicy(zone)) {}

  void TestInsertFind(DirectHandle<Object> key1, void* val1,
                      DirectHandle<Object> key2, void* val2) {
    CHECK_NULL(map.Find(key1));
    CHECK_NULL(map.Find(key2));

    // Set {key1} the first time.
    auto find_result = map.FindOrInsert(key1);
    CHECK_NOT_NULL(find_result.entry);
    CHECK(!find_result.already_exists);
    *find_result.entry = val1;

    for (int i = 0; i < 3; i++) {  // Get and find {key1} K times.
      {
        auto new_find_result = map.FindOrInsert(key1);
        CHECK(new_find_result.already_exists);
        CHECK_EQ(find_result.entry, new_find_result.entry);
        CHECK_EQ(val1, *new_find_result.entry);
        CHECK_NULL(map.Find(key2));
      }
      {
        void** nentry = map.Find(key1);
        CHECK_EQ(find_result.entry, nentry);
        CHECK_EQ(val1, *nentry);
        CHECK_NULL(map.Find(key2));
      }
    }

    // Set {key2} the first time.
    auto find_result2 = map.FindOrInsert(key2);
    CHECK_NOT_NULL(find_result2.entry);
    CHECK(!find_result2.already_exists);
    *find_result2.entry = val2;

    for (int i = 0; i < 3; i++) {  // Get and find {key1} and {key2} K times.
      {
        auto new_find_result = map.FindOrInsert(key2);
        CHECK_EQ(find_result2.entry, new_find_result.entry);
        CHECK_EQ(val2, *new_find_result.entry);
      }
      {
        void** nentry = map.Find(key2);
        CHECK_EQ(find_result2.entry, nentry);
        CHECK_EQ(val2, *nentry);
      }
      {
        void** nentry = map.Find(key1);
        CHECK_EQ(val1, *nentry);
      }
    }
  }

  void TestFindDelete(DirectHandle<Object> key1, void* val1,
                      DirectHandle<Object> key2, void* val2) {
    CHECK_NULL(map.Find(key1));
    CHECK_NULL(map.Find(key2));

    // Set {key1} and {key2} for the first time.
    auto find_result1 = map.FindOrInsert(key1);
    CHECK(!find_result1.already_exists);
    CHECK_NOT_NULL(find_result1.entry);
    *find_result1.entry = val1;
    auto find_result2 = map.FindOrInsert(key2);
    CHECK(!find_result1.already_exists);
    CHECK_NOT_NULL(find_result2.entry);
    *find_result2.entry = val2;

    for (int i = 0; i < 3; i++) {  // Find {key1} and {key2} 3 times.
      {
        void** nentry = map.Find(key2);
        CHECK_EQ(val2, *nentry);
      }
      {
        void** nentry = map.Find(key1);
        CHECK_EQ(val1, *nentry);
      }
    }

    // Delete {key1}
    void* deleted_entry_1;
    CHECK(map.Delete(key1, &deleted_entry_1));
    CHECK_NOT_NULL(deleted_entry_1);
    deleted_entry_1 = val1;

    for (int i = 0; i < 3; i++) {  // Find {key1} and not {key2} 3 times.
      {
        void** nentry = map.Find(key1);
        CHECK_NULL(nentry);
      }
      {
        void** nentry = map.Find(key2);
        CHECK_EQ(val2, *nentry);
      }
    }

    // Delete {key2}
    void* deleted_entry_2;
    CHECK(map.Delete(key2, &deleted_entry_2));
    CHECK_NOT_NULL(deleted_entry_2);
    deleted_entry_2 = val2;

    for (int i = 0; i < 3; i++) {  // Don't find {key1} and {key2} 3 times.
      {
        void** nentry = map.Find(key1);
        CHECK_NULL(nentry);
      }
      {
        void** nentry = map.Find(key2);
        CHECK_NULL(nentry);
      }
    }
  }

  void SimulateGCByIncrementingSmisBy(int shift) {
    for (int i = 0; i < map.capacity_; i++) {
      Address key = map.keys_[i];
      if (!Internals::HasHeapObjectTag(key)) {
        map.keys_[i] =
            Internals::IntegralToSmi(Internals::SmiValue(key) + shift);
      }
    }
    map.gc_counter_ = -1;
  }

  void CheckFind(DirectHandle<Object> key, void* value) {
    void** entry = map.Find(key);
    CHECK_NOT_NULL(entry);
    CHECK_EQ(value, *entry);
  }

  void CheckFindOrInsert(DirectHandle<Object> key, void* value) {
    auto find_result = map.FindOrInsert(key);
    CHECK(find_result.already_exists);
    CHECK_NOT_NULL(find_result.entry);
    CHECK_EQ(value, *find_result.entry);
  }

  void CheckDelete(DirectHandle<Object> key, void* value) {
    void* entry;
    CHECK(map.Delete(key, &entry));
    CHECK_NOT_NULL(entry);
    CHECK_EQ(value, entry);
  }

  void PrintMap() {
    PrintF("{\n");
    for (int i = 0; i < map.capacity_; i++) {
      PrintF("  %3d: %p => %p\n", i, reinterpret_cast<void*>(map.keys_[i]),
             reinterpret_cast<void*>(map.values_[i]));
    }
    PrintF("}\n");
  }

  void Resize() { map.Resize(map.capacity_ * 4); }

  void Rehash() { map.Rehash(); }
};

class IdentityMapTest : public TestWithIsolateAndZone {
 public:
  Handle<Smi> smi(int value) {
    return Handle<Smi>(Smi::FromInt(value), isolate());
  }

  Handle<Object> num(double value) {
    return isolate()->factory()->NewNumber(value);
  }

  void IterateCollisionTest(int stride) {
    for (int load = 15; load <= 120; load = load * 2) {
      IdentityMapTester t(isolate()->heap(), zone());

      {  // Add entries to the map.
        HandleScope scope(isolate());
        int next = 1;
        for (int i = 0; i < load; i++) {
          t.map.Insert(smi(next), reinterpret_cast<void*>(next));
          t.CheckFind(smi(next), reinterpret_cast<void*>(next));
          next = next + stride;
        }
      }
      // Iterate through the map and check we see all elements only once.
      std::set<intptr_t> seen;
      {
        IdentityMap<void*, ZoneAllocationPolicy>::IteratableScope it_scope(
            &t.map);
        for (auto it = it_scope.begin(); it != it_scope.end(); ++it) {
          CHECK(seen.find(reinterpret_cast<intptr_t>(**it)) == seen.end());
          seen.insert(reinterpret_cast<intptr_t>(**it));
        }
      }
      // Check get and find on map.
      {
        HandleScope scope(isolate());
        int next = 1;
        for (int i = 0; i < load; i++) {
          CHECK(seen.find(next) != seen.end());
          t.CheckFind(smi(next), reinterpret_cast<void*>(next));
          t.CheckFindOrInsert(smi(next), reinterpret_cast<void*>(next));
          next = next + stride;
        }
      }
    }
  }

  void CollisionTest(int stride, bool rehash = false, bool resize = false) {
    for (int load = 15; load <= 120; load = load * 2) {
      IdentityMapTester t(isolate()->heap(), zone());

      {  // Add entries to the map.
        HandleScope scope(isolate());
        int next = 1;
        for (int i = 0; i < load; i++) {
          t.map.Insert(smi(next), reinterpret_cast<void*>(next));
          t.CheckFind(smi(next), reinterpret_cast<void*>(next));
          next = next + stride;
        }
      }
      if (resize) t.Resize();  // Explicit resize (internal method).
      if (rehash) t.Rehash();  // Explicit rehash (internal method).
      {                        // Check find and get.
        HandleScope scope(isolate());
        int next = 1;
        for (int i = 0; i < load; i++) {
          t.CheckFind(smi(next), reinterpret_cast<void*>(next));
          t.CheckFindOrInsert(smi(next), reinterpret_cast<void*>(next));
          next = next + stride;
        }
      }
    }
  }
};

TEST_F(IdentityMapTest, Find_smi_not_found) {
  IdentityMapTester t(isolate()->heap(), zone());
  for (int i = 0; i < 100; i++) {
    CHECK_NULL(t.map.Find(smi(i)));
  }
}

TEST_F(IdentityMapTest, Find_num_not_found) {
  IdentityMapTester t(isolate()->heap(), zone());
  for (int i = 0; i < 100; i++) {
    CHECK_NULL(t.map.Find(num(i + 0.2)));
  }
}

TEST_F(IdentityMapTest, Delete_smi_not_found) {
  IdentityMapTester t(isolate()->heap(), zone());
  for (int i = 0; i < 100; i++) {
    void* deleted_value = &t;
    CHECK(!t.map.Delete(smi(i), &deleted_value));
    CHECK_EQ(&t, deleted_value);
  }
}

TEST_F(IdentityMapTest, Delete_num_not_found) {
  IdentityMapTester t(isolate()->heap(), zone());
  for (int i = 0; i < 100; i++) {
    void* deleted_value = &t;
    CHECK(!t.map.Delete(num(i + 0.2), &deleted_value));
    CHECK_EQ(&t, deleted_value);
  }
}

TEST_F(IdentityMapTest, GetFind_smi_0) {
  IdentityMapTester t(isolate()->heap(), zone());
  t.TestInsertFind(smi(0), isolate(), smi(1), isolate()->heap());
}

TEST_F(IdentityMapTest, GetFind_smi_13) {
  IdentityMapTester t(isolate()->heap(), zone());
  t.TestInsertFind(smi(13), isolate(), smi(17), isolate()->heap());
}

TEST_F(IdentityMapTest, GetFind_num_13) {
  IdentityMapTester t(isolate()->heap(), zone());
  t.TestInsertFind(num(13.1), isolate(), num(17.1), isolate()->heap());
}

TEST_F(IdentityMapTest, Delete_smi_13) {
  IdentityMapTester t(isolate()->heap(), zone());
  t.TestFindDelete(smi(13), isolate(), smi(17), isolate()->heap());
  CHECK(t.map.empty());
}

TEST_F(IdentityMapTest, Delete_num_13) {
  IdentityMapTester t(isolate()->heap(), zone());
  t.TestFindDelete(num(13.1), isolate(), num(17.1), isolate()->heap());
  CHECK(t.map.empty());
}

TEST_F(IdentityMapTest, GetFind_smi_17m) {
  const int kInterval = 17;
  const int kShift = 1099;
  IdentityMapTester t(isolate()->heap(), zone());

  for (int i = 1; i < 100; i += kInterval) {
    t.map.Insert(smi(i), reinterpret_cast<void*>(i + kShift));
  }

  for (int i = 1; i < 100; i += kInterval) {
    t.CheckFind(smi(i), reinterpret_cast<void*>(i + kShift));
  }

  for (int i = 1; i < 100; i += kInterval) {
    t.CheckFindOrInsert(smi(i), reinterpret_cast<void*>(i + kShift));
  }

  for (int i = 1; i < 100; i++) {
    void** entry = t.map.Find(smi(i));
    if ((i % kInterval) != 1) {
      CHECK_NULL(entry);
    } else {
      CHECK_NOT_NULL(entry);
      CHECK_EQ(reinterpret_cast<void*>(i + kShift), *entry);
    }
  }
}

TEST_F(IdentityMapTest, Delete_smi_17m) {
  const int kInterval = 17;
  const int kShift = 1099;
  IdentityMapTester t(isolate()->heap(), zone());

  for (int i = 1; i < 100; i += kInterval) {
    t.map.Insert(smi(i), reinterpret_cast<void*>(i + kShift));
  }

  for (int i = 1; i < 100; i += kInterval) {
    t.CheckFind(smi(i), reinterpret_cast<void*>(i + kShift));
  }

  for (int i = 1; i < 100; i += kInterval) {
    t.CheckDelete(smi(i), reinterpret_cast<void*>(i + kShift));
    for (int j = 1; j < 100; j += kInterval) {
      auto entry = t.map.Find(smi(j));
      if (j <= i) {
        CHECK_NULL(entry);
      } else {
        CHECK_NOT_NULL(entry);
        CHECK_EQ(reinterpret_cast<void*>(j + kShift), *entry);
      }
    }
  }
}

TEST_F(IdentityMapTest, GetFind_num_1000) {
  const int kPrime = 137;
  IdentityMapTester t(isolate()->heap(), zone());
  int val1;
  int val2;

  for (int i = 0; i < 1000; i++) {
    t.TestInsertFind(smi(i * kPrime), &val1, smi(i * kPrime + 1), &val2);
  }
}

TEST_F(IdentityMapTest, Delete_num_1000) {
  const int kPrime = 137;
  IdentityMapTester t(isolate()->heap(), zone());

  for (int i = 0; i < 1000; i++) {
    t.map.Insert(smi(i * kPrime), reinterpret_cast<void*>(i * kPrime));
  }

  // Delete every second value in reverse.
  for (int i = 999; i >= 0; i -= 2) {
    void* entry;
    CHECK(t.map.Delete(smi(i * kPrime), &entry));
    CHECK_EQ(reinterpret_cast<void*>(i * kPrime), entry);
  }

  for (int i = 0; i < 1000; i++) {
    auto entry = t.map.Find(smi(i * kPrime));
    if (i % 2) {
      CHECK_NULL(entry);
    } else {
      CHECK_NOT_NULL(entry);
      CHECK_EQ(reinterpret_cast<void*>(i * kPrime), *entry);
    }
  }

  // Delete the rest.
  for (int i = 0; i < 1000; i += 2) {
    void* entry;
    CHECK(t.map.Delete(smi(i * kPrime), &entry));
    CHECK_EQ(reinterpret_cast<void*>(i * kPrime), entry);
  }

  for (int i = 0; i < 1000; i++) {
    auto entry = t.map.Find(smi(i * kPrime));
    CHECK_NULL(entry);
  }
}

TEST_F(IdentityMapTest, GetFind_smi_gc) {
  const int kKey = 33;
  const int kShift = 1211;
  IdentityMapTester t(isolate()->heap(), zone());

  t.map.Insert(smi(kKey), &t);
  t.SimulateGCByIncrementingSmisBy(kShift);
  t.CheckFind(smi(kKey + kShift), &t);
  t.CheckFindOrInsert(smi(kKey + kShift), &t);
}

TEST_F(IdentityMapTest, Delete_smi_gc) {
  const int kKey = 33;
  const int kShift = 1211;
  IdentityMapTester t(isolate()->heap(), zone());

  t.map.Insert(smi(kKey), &t);
  t.SimulateGCByIncrementingSmisBy(kShift);
  t.CheckDelete(smi(kKey + kShift), &t);
}

TEST_F(IdentityMapTest, GetFind_smi_gc2) {
  int kKey1 = 1;
  int kKey2 = 33;
  const int kShift = 1211;
  IdentityMapTester t(isolate()->heap(), zone());

  t.map.Insert(smi(kKey1), &kKey1);
  t.map.Insert(smi(kKey2), &kKey2);
  t.SimulateGCByIncrementingSmisBy(kShift);
  t.CheckFind(smi(kKey1 + kShift), &kKey1);
  t.CheckFindOrInsert(smi(kKey1 + kShift), &kKey1);
  t.CheckFind(smi(kKey2 + kShift), &kKey2);
  t.CheckFindOrInsert(smi(kKey2 + kShift), &kKey2);
}

TEST_F(IdentityMapTest, Delete_smi_gc2) {
  int kKey1 = 1;
  int kKey2 = 33;
  const int kShift = 1211;
  IdentityMapTester t(isolate()->heap(), zone());

  t.map.Insert(smi(kKey1), &kKey1);
  t.map.Insert(smi(kKey2), &kKey2);
  t.SimulateGCByIncrementingSmisBy(kShift);
  t.CheckDelete(smi(kKey1 + kShift), &kKey1);
  t.CheckDelete(smi(kKey2 + kShift), &kKey2);
}

TEST_F(IdentityMapTest, GetFind_smi_gc_n) {
  const int kShift = 12011;
  IdentityMapTester t(isolate()->heap(), zone());
  int keys[12] = {1,      2,      7,      8,      15,      23,
                  1 + 32, 2 + 32, 7 + 32, 8 + 32, 15 + 32, 23 + 32};
  // Initialize the map first.
  for (size_t i = 0; i < arraysize(keys); i += 2) {
    t.TestInsertFind(smi(keys[i]), &keys[i], smi(keys[i + 1]), &keys[i + 1]);
  }
  // Check the above initialization.
  for (size_t i = 0; i < arraysize(keys); i++) {
    t.CheckFind(smi(keys[i]), &keys[i]);
  }
  // Simulate a GC by "moving" the smis in the internal keys array.
  t.SimulateGCByIncrementingSmisBy(kShift);
  // Check that searching for the incremented smis finds the same values.
  for (size_t i = 0; i < arraysize(keys); i++) {
    t.CheckFind(smi(keys[i] + kShift), &keys[i]);
  }
  // Check that searching for the incremented smis gets the same values.
  for (size_t i = 0; i < arraysize(keys); i++) {
    t.CheckFindOrInsert(smi(keys[i] + kShift), &keys[i]);
  }
}

TEST_F(IdentityMapTest, Delete_smi_gc_n) {
  const int kShift = 12011;
  IdentityMapTester t(isolate()->heap(), zone());
  int keys[12] = {1,      2,      7,      8,      15,      23,
                  1 + 32, 2 + 32, 7 + 32, 8 + 32, 15 + 32, 23 + 32};
  // Initialize the map first.
  for (size_t i = 0; i < arraysize(keys); i++) {
    t.map.Insert(smi(keys[i]), &keys[i]);
  }
  // Simulate a GC by "moving" the smis in the internal keys array.
  t.SimulateGCByIncrementingSmisBy(kShift);
  // Check that deleting for the incremented smis finds the same values.
  for (size_t i = 0; i < arraysize(keys); i++) {
    t.CheckDelete(smi(keys[i] + kShift), &keys[i]);
  }
}

TEST_F(IdentityMapTest, GetFind_smi_num_gc_n) {
  const int kShift = 12019;
  IdentityMapTester t(isolate()->heap(), zone());
  int smi_keys[] = {1, 2, 7, 15, 23};
  Handle<Object> num_keys[] = {num(1.1), num(2.2), num(3.3), num(4.4),
                               num(5.5), num(6.6), num(7.7), num(8.8),
                               num(9.9), num(10.1)};
  // Initialize the map first.
  for (size_t i = 0; i < arraysize(smi_keys); i++) {
    t.map.Insert(smi(smi_keys[i]), &smi_keys[i]);
  }
  for (size_t i = 0; i < arraysize(num_keys); i++) {
    t.map.Insert(num_keys[i], &num_keys[i]);
  }
  // Check the above initialization.
  for (size_t i = 0; i < arraysize(smi_keys); i++) {
    t.CheckFind(smi(smi_keys[i]), &smi_keys[i]);
  }
  for (size_t i = 0; i < arraysize(num_keys); i++) {
    t.CheckFind(num_keys[i], &num_keys[i]);
  }

  // Simulate a GC by moving SMIs.
  // Ironically the SMIs "move", but the heap numbers don't!
  t.SimulateGCByIncrementingSmisBy(kShift);

  // Check that searching for the incremented smis finds the same values.
  for (size_t i = 0; i < arraysize(smi_keys); i++) {
    t.CheckFind(smi(smi_keys[i] + kShift), &smi_keys[i]);
    t.CheckFindOrInsert(smi(smi_keys[i] + kShift), &smi_keys[i]);
  }

  // Check that searching for the numbers finds the same values.
  for (size_t i = 0; i < arraysize(num_keys); i++) {
    t.CheckFind(num_keys[i], &num_keys[i]);
    t.CheckFindOrInsert(num_keys[i], &num_keys[i]);
  }
}

TEST_F(IdentityMapTest, Delete_smi_num_gc_n) {
  const int kShift = 12019;
  IdentityMapTester t(isolate()->heap(), zone());
  int smi_keys[] = {1, 2, 7, 15, 23};
  Handle<Object> num_keys[] = {num(1.1), num(2.2), num(3.3), num(4.4),
                               num(5.5), num(6.6), num(7.7), num(8.8),
                               num(9.9), num(10.1)};
  // Initialize the map first.
  for (size_t i = 0; i < arraysize(smi_keys); i++) {
    t.map.Insert(smi(smi_keys[i]), &smi_keys[i]);
  }
  for (size_t i = 0; i < arraysize(num_keys); i++) {
    t.map.Insert(num_keys[i], &num_keys[i]);
  }

  // Simulate a GC by moving SMIs.
  // Ironically the SMIs "move", but the heap numbers don't!
  t.SimulateGCByIncrementingSmisBy(kShift);

  // Check that deleting for the incremented smis finds the same values.
  for (size_t i = 0; i < arraysize(smi_keys); i++) {
    t.CheckDelete(smi(smi_keys[i] + kShift), &smi_keys[i]);
  }

  // Check that deleting the numbers finds the same values.
  for (size_t i = 0; i < arraysize(num_keys); i++) {
    t.CheckDelete(num_keys[i], &num_keys[i]);
  }
}

TEST_F(IdentityMapTest, Delete_smi_resizes) {
  const int kKeyCount = 1024;
  const int kValueOffset = 27;
  IdentityMapTester t(isolate()->heap(), zone());

  // Insert one element to initialize map.
  t.map.Insert(smi(0), reinterpret_cast<void*>(kValueOffset));

  int initial_capacity = t.map.capacity();
  CHECK_LT(initial_capacity, kKeyCount);

  // Insert another kKeyCount - 1 keys.
  for (int i = 1; i < kKeyCount; i++) {
    t.map.Insert(smi(i), reinterpret_cast<void*>(i + kValueOffset));
  }

  // Check capacity increased.
  CHECK_GT(t.map.capacity(), initial_capacity);
  CHECK_GE(t.map.capacity(), kKeyCount);

  // Delete all the keys.
  for (int i = 0; i < kKeyCount; i++) {
    t.CheckDelete(smi(i), reinterpret_cast<void*>(i + kValueOffset));
  }

  // Should resize back to initial capacity.
  CHECK_EQ(t.map.capacity(), initial_capacity);
}

TEST_F(IdentityMapTest, Iterator_smi_num) {
  IdentityMapTester t(isolate()->heap(), zone());
  int smi_keys[] = {1, 2, 7, 15, 23};
  Handle<Object> num_keys[] = {num(1.1), num(2.2), num(3.3), num(4.4),
                               num(5.5), num(6.6), num(7.7), num(8.8),
                               num(9.9), num(10.1)};
  // Initialize the map.
  for (size_t i = 0; i < arraysize(smi_keys); i++) {
    t.map.Insert(smi(smi_keys[i]), reinterpret_cast<void*>(i));
  }
  for (size_t i = 0; i < arraysize(num_keys); i++) {
    t.map.Insert(num_keys[i], reinterpret_cast<void*>(i + 5));
  }

  // Check iterator sees all values once.
  std::set<intptr_t> seen;
  {
    IdentityMap<void*, ZoneAllocationPolicy>::IteratableScope it_scope(&t.map);
    for (auto it = it_scope.begin(); it != it_scope.end(); ++it) {
      CHECK(seen.find(reinterpret_cast<intptr_t>(**it)) == seen.end());
      seen.insert(reinterpret_cast<intptr_t>(**it));
    }
  }
  for (intptr_t i = 0; i < 15; i++) {
    CHECK(seen.find(i) != seen.end());
  }
}

TEST_F(IdentityMapTest, Iterator_smi_num_gc) {
  const int kShift = 16039;
  IdentityMapTester t(isolate()->heap(), zone());
  int smi_keys[] = {1, 2, 7, 15, 23};
  Handle<Object> num_keys[] = {num(1.1), num(2.2), num(3.3), num(4.4),
                               num(5.5), num(6.6), num(7.7), num(8.8),
                               num(9.9), num(10.1)};
  // Initialize the map.
  for (size_t i = 0; i < arraysize(smi_keys); i++) {
    t.map.Insert(smi(smi_keys[i]), reinterpret_cast<void*>(i));
  }
  for (size_t i = 0; i < arraysize(num_keys); i++) {
    t.map.Insert(num_keys[i], reinterpret_cast<void*>(i + 5));
  }

  // Simulate GC by moving the SMIs.
  t.SimulateGCByIncrementingSmisBy(kShift);

  // Check iterator sees all values.
  std::set<intptr_t> seen;
  {
    IdentityMap<void*, ZoneAllocationPolicy>::IteratableScope it_scope(&t.map);
    for (auto it = it_scope.begin(); it != it_scope.end(); ++it) {
      CHECK(seen.find(reinterpret_cast<intptr_t>(**it)) == seen.end());
      seen.insert(reinterpret_cast<intptr_t>(**it));
    }
  }
  for (intptr_t i = 0; i < 15; i++) {
    CHECK(seen.find(i) != seen.end());
  }
}

TEST_F(IdentityMapTest, IterateCollisions_1) { IterateCollisionTest(1); }
TEST_F(IdentityMapTest, IterateCollisions_2) { IterateCollisionTest(2); }
TEST_F(IdentityMapTest, IterateCollisions_3) { IterateCollisionTest(3); }
TEST_F(IdentityMapTest, IterateCollisions_5) { IterateCollisionTest(5); }
TEST_F(IdentityMapTest, IterateCollisions_7) { IterateCollisionTest(7); }

TEST_F(IdentityMapTest, Collisions_1) { CollisionTest(1); }
TEST_F(IdentityMapTest, Collisions_2) { CollisionTest(2); }
TEST_F(IdentityMapTest, Collisions_3) { CollisionTest(3); }
TEST_F(IdentityMapTest, Collisions_5) { CollisionTest(5); }
TEST_F(IdentityMapTest, Collisions_7) { CollisionTest(7); }
TEST_F(IdentityMapTest, Resize) { CollisionTest(9, false, true); }
TEST_F(IdentityMapTest, Rehash) { CollisionTest(11, true, false); }

TEST_F(IdentityMapTest, ExplicitGC) {
  IdentityMapTester t(isolate()->heap(), zone());
  Handle<Object> num_keys[] = {num(2.1), num(2.4), num(3.3), num(4.3),
                               num(7.5), num(6.4), num(7.3), num(8.3),
                               num(8.9), num(10.4)};

  // Insert some objects that should be in new space.
  for (size_t i = 0; i < arraysize(num_keys); i++) {
    t.map.Insert(num_keys[i], &num_keys[i]);
  }

  // Do an explicit, real GC.
  InvokeMinorGC();

  // Check that searching for the numbers finds the same values.
  for (size_t i = 0; i < arraysize(num_keys); i++) {
    t.CheckFind(num_keys[i], &num_keys[i]);
    t.CheckFindOrInsert(num_keys[i], &num_keys[i]);
  }
}

TEST_F(IdentityMapTest, GCShortCutting) {
  if (v8_flags.single_generation) return;
  // We don't create ThinStrings immediately when using the forwarding table.
  if (v8_flags.always_use_string_forwarding_table) return;
  v8_flags.shortcut_strings_with_stack = true;
  ManualGCScope manual_gc_scope(isolate());
  IdentityMapTester t(isolate()->heap(), zone());
  Factory* factory = isolate()->factory();
  const int kDummyValue = 0;

  for (int i = 0; i < 16; i++) {
    // Insert a varying number of Smis as padding to ensure some tests straddle
    // a boundary where the thin string short cutting will cause size_ to be
    // greater to capacity_ if not corrected by IdentityMap
    // (see crbug.com/704132).
    for (int j = 0; j < i; j++) {
      t.map.Insert(smi(j), reinterpret_cast<void*>(kDummyValue));
    }

    Handle<String> thin_string =
        factory->NewStringFromAsciiChecked("thin_string");
    Handle<String> internalized_string =
        factory->InternalizeString(thin_string);
    DCHECK(IsThinString(*thin_string));
    DCHECK_NE(*thin_string, *internalized_string);

    // Insert both keys into the map.
    t.map.Insert(thin_string, &thin_string);
    t.map.Insert(internalized_string, &internalized_string);

    // Do an explicit, real GC, this should short-cut the thin string to point
    // to the internalized string (this is not implemented for MinorMS).
    InvokeMinorGC();
    DCHECK_IMPLIES(!v8_flags.minor_ms && !v8_flags.optimize_for_size,
                   *thin_string == *internalized_string);

    // Check that getting the object points to one of the handles.
    void** thin_string_entry = t.map.Find(thin_string);
    CHECK(*thin_string_entry == &thin_string ||
          *thin_string_entry == &internalized_string);
    void** internalized_string_entry = t.map.Find(internalized_string);
    CHECK(*internalized_string_entry == &thin_string ||
          *internalized_string_entry == &internalized_string);

    // Trigger resize.
    for (int j = 0; j < 16; j++) {
      t.map.Insert(smi(j + 16), reinterpret_cast<void*>(kDummyValue));
    }
    t.map.Clear();
  }
}

}  // namespace internal
}  // namespace v8
