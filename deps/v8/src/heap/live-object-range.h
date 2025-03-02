// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_HEAP_LIVE_OBJECT_RANGE_H_
#define V8_HEAP_LIVE_OBJECT_RANGE_H_

#include <utility>

#include "src/heap/marking.h"
#include "src/objects/heap-object.h"

namespace v8::internal {

class PageMetadata;

class LiveObjectRange final {
 public:
  class iterator final {
   public:
    using value_type = std::pair<Tagged<HeapObject>, int /* size */>;
    using pointer = const value_type*;
    using reference = const value_type&;
    using iterator_category = std::forward_iterator_tag;

    inline iterator();
    explicit inline iterator(const PageMetadata* page);

    inline iterator& operator++();
    inline iterator operator++(int);

    bool operator==(iterator other) const {
      return current_object_ == other.current_object_;
    }
    bool operator!=(iterator other) const { return !(*this == other); }

    value_type operator*() {
      return std::make_pair(current_object_, current_size_);
    }

   private:
    inline bool AdvanceToNextMarkedObject();
    inline void AdvanceToNextValidObject();

    const PageMetadata* const page_ = nullptr;
    const MarkBit::CellType* const cells_ = nullptr;
    const PtrComprCageBase cage_base_;
    MarkingBitmap::CellIndex current_cell_index_ = 0;
    MarkingBitmap::CellType current_cell_ = 0;
    Tagged<HeapObject> current_object_;
    Tagged<Map> current_map_;
    int current_size_ = 0;
  };

  explicit LiveObjectRange(const PageMetadata* page) : page_(page) {}

  inline iterator begin();
  inline iterator end();

 private:
  const PageMetadata* const page_;
};

}  // namespace v8::internal

#endif  // V8_HEAP_LIVE_OBJECT_RANGE_H_
