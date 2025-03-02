// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_REGEXP_REGEXP_RESULT_VECTOR_H_
#define V8_REGEXP_REGEXP_RESULT_VECTOR_H_

#include "src/base/macros.h"
#include "src/common/globals.h"

namespace v8 {
namespace internal {

class RegExpResultVectorScope final {
 public:
  explicit RegExpResultVectorScope(Isolate* isolate);
  RegExpResultVectorScope(Isolate* isolate, int size);
  ~RegExpResultVectorScope();

  int32_t* Initialize(int size);

  int32_t* value() const {
    DCHECK_NOT_NULL(value_);
    return value_;
  }

 private:
  Isolate* const isolate_;
  bool is_dynamic_ = false;
  int32_t* value_ = nullptr;
};

class RegExpResultVector final : public AllStatic {
 public:
  static int32_t* Allocate(Isolate* isolate, uint32_t size);
  static void Free(Isolate* isolate, int32_t* vector);
};

}  // namespace internal
}  // namespace v8

#endif  // V8_REGEXP_REGEXP_RESULT_VECTOR_H_
