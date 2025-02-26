// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_RUNTIME_RUNTIME_UTILS_H_
#define V8_RUNTIME_RUNTIME_UTILS_H_

#include "src/objects/objects.h"

namespace v8 {
namespace internal {

// A mechanism to return a pair of Object pointers in registers (if possible).
// How this is achieved is calling convention-dependent.
// All currently supported x86 compiles uses calling conventions that are cdecl
// variants where a 64-bit value is returned in two 32-bit registers
// (edx:eax on ia32, r1:r0 on ARM).
// In AMD-64 calling convention a struct of two pointers is returned in rdx:rax.
// In Win64 calling convention, a struct of two pointers is returned in memory,
// allocated by the caller, and passed as a pointer in a hidden first parameter.
#ifdef V8_HOST_ARCH_64_BIT
struct ObjectPair {
  Address x;
  Address y;
};

static inline ObjectPair MakePair(Tagged<Object> x, Tagged<Object> y) {
  ObjectPair result = {x.ptr(), y.ptr()};
  // Pointers x and y returned in rax and rdx, in AMD-x64-abi.
  // In Win64 they are assigned to a hidden first argument.
  return result;
}
#else
using ObjectPair = uint64_t;
static inline ObjectPair MakePair(Tagged<Object> x, Tagged<Object> y) {
#if defined(V8_TARGET_LITTLE_ENDIAN)
  return x.ptr() | (static_cast<ObjectPair>(y.ptr()) << 32);
#elif defined(V8_TARGET_BIG_ENDIAN)
  return y.ptr() | (static_cast<ObjectPair>(x.ptr()) << 32);
#else
#error Unknown endianness
#endif
}
#endif

// TODO(40192807): Drop this when the "SaveAndClearThreadInWasmFlag" approach is
// no longer needed.
class V8_NODISCARD [[maybe_unused]] SaveAndClearThreadInWasmFlag {
 public:
#if V8_ENABLE_WEBASSEMBLY
  explicit SaveAndClearThreadInWasmFlag(Isolate* isolate);
  ~SaveAndClearThreadInWasmFlag();

 private:
  bool thread_was_in_wasm_ = false;
  Isolate* isolate_;
#else
  explicit SaveAndClearThreadInWasmFlag(Isolate*) {}
#endif
};

}  // namespace internal
}  // namespace v8

#endif  // V8_RUNTIME_RUNTIME_UTILS_H_
