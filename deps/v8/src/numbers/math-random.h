// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_NUMBERS_MATH_RANDOM_H_
#define V8_NUMBERS_MATH_RANDOM_H_

#include "src/common/globals.h"
#include "src/objects/contexts.h"

namespace v8 {
namespace internal {

class MathRandom : public AllStatic {
 public:
  static void InitializeContext(Isolate* isolate,
                                DirectHandle<Context> native_context);

  static void ResetContext(Tagged<Context> native_context);
  // Takes native context as a raw Address for ExternalReference usage.
  // Returns a tagged Smi as a raw Address.
  static Address RefillCache(Isolate* isolate, Address raw_native_context);

  static const int kCacheSize = 64;
  static const int kStateSize = 2 * kInt64Size;

  struct State {
    uint64_t s0;
    uint64_t s1;
  };
};

}  // namespace internal
}  // namespace v8
#endif  // V8_NUMBERS_MATH_RANDOM_H_
