// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_HEAP_CPPGC_JS_CROSS_HEAP_REMEMBERED_SET_H_
#define V8_HEAP_CPPGC_JS_CROSS_HEAP_REMEMBERED_SET_H_

#include <vector>

#include "src/base/macros.h"
#include "src/handles/handles.h"
#include "src/objects/tagged.h"

namespace cppgc::internal {
class HeapBase;
}

namespace v8::internal {

class JSObject;

// The class is used to remember V8 to Oilpan references.
class V8_EXPORT_PRIVATE CrossHeapRememberedSet final {
 public:
  explicit CrossHeapRememberedSet(cppgc::internal::HeapBase& heap_base)
      : heap_base_(heap_base) {}

  CrossHeapRememberedSet(const CrossHeapRememberedSet&) = delete;
  CrossHeapRememberedSet(CrossHeapRememberedSet&&) = delete;

  void RememberReferenceIfNeeded(Isolate& isolate, Tagged<JSObject> host_obj,
                                 void* cppgc_object);
  void Reset(Isolate& isolate);

  template <typename F>
  void Visit(Isolate&, F);

  bool IsEmpty() const { return remembered_v8_to_cppgc_references_.empty(); }

 private:
  cppgc::internal::HeapBase& heap_base_;
  // The vector keeps handles to remembered V8 objects that have outgoing
  // references to the cppgc heap. Please note that the handles are global.
  std::vector<IndirectHandle<JSObject>> remembered_v8_to_cppgc_references_;
};

template <typename F>
void CrossHeapRememberedSet::Visit(Isolate& isolate, F f) {
  for (auto& obj : remembered_v8_to_cppgc_references_) {
    f(*obj);
  }
}

}  // namespace v8::internal

#endif  // V8_HEAP_CPPGC_JS_CROSS_HEAP_REMEMBERED_SET_H_
