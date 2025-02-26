// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_HEAP_EPHEMERON_REMEMBERED_SET_H_
#define V8_HEAP_EPHEMERON_REMEMBERED_SET_H_

#include <unordered_map>
#include <unordered_set>

#include "src/base/platform/mutex.h"
#include "src/heap/base/worklist.h"
#include "src/objects/hash-table.h"

namespace v8::internal {

// Stores ephemeron entries where the EphemeronHashTable is in old-space,
// and the key of the entry is in new-space. Such keys do not appear in the
// usual OLD_TO_NEW remembered set. The remembered set is used to avoid
// strongifying keys in such hash tables in young generation garbage
// collections.
class EphemeronRememberedSet final {
 public:
  static constexpr int kEphemeronTableListSegmentSize = 128;
  using TableList = ::heap::base::Worklist<Tagged<EphemeronHashTable>,
                                           kEphemeronTableListSegmentSize>;

  using IndicesSet = std::unordered_set<int>;
  using TableMap = std::unordered_map<Tagged<EphemeronHashTable>, IndicesSet,
                                      Object::Hasher>;

  void RecordEphemeronKeyWrite(Tagged<EphemeronHashTable> table,
                               Address key_slot);
  void RecordEphemeronKeyWrites(Tagged<EphemeronHashTable> table,
                                IndicesSet indices);

  TableMap* tables() { return &tables_; }

 private:
  base::SpinningMutex insertion_mutex_;
  TableMap tables_;
};

}  // namespace v8::internal

#endif  // V8_HEAP_EPHEMERON_REMEMBERED_SET_H_
