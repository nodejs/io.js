// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file contains runtime implementations of a few macros that are defined
// as external in Torque, so that generated runtime code can work.

#ifndef V8_TORQUE_RUNTIME_MACRO_SHIMS_H_
#define V8_TORQUE_RUNTIME_MACRO_SHIMS_H_

#include <cstdint>

#include "src/numbers/integer-literal.h"
#include "src/utils/utils.h"

namespace v8 {
namespace internal {

namespace TorqueRuntimeMacroShims {
namespace CodeStubAssembler {

inline bool BoolConstant(bool b) { return b; }
inline intptr_t ChangeInt32ToIntPtr(int32_t i) { return i; }
inline uintptr_t ChangeUint32ToWord(uint32_t u) { return u; }
inline intptr_t IntPtrAdd(intptr_t a, intptr_t b) { return a + b; }
inline intptr_t IntPtrMul(intptr_t a, intptr_t b) { return a * b; }
inline bool IntPtrLessThan(intptr_t a, intptr_t b) { return a < b; }
inline bool IntPtrLessThanOrEqual(intptr_t a, intptr_t b) { return a <= b; }
inline intptr_t Signed(uintptr_t u) { return static_cast<intptr_t>(u); }
template <typename Smi>
inline int32_t SmiUntag(Smi s) {
  return s.value();
}
inline bool UintPtrLessThan(uintptr_t a, uintptr_t b) { return a < b; }
inline uint32_t Unsigned(int32_t s) { return static_cast<uint32_t>(s); }
#if V8_HOST_ARCH_64_BIT
inline uintptr_t Unsigned(intptr_t s) { return static_cast<uintptr_t>(s); }
#endif
inline bool Word32Equal(uint32_t a, uint32_t b) { return a == b; }
inline bool Word32NotEqual(uint32_t a, uint32_t b) { return a != b; }
inline int32_t ConstexprIntegerLiteralToInt32(const IntegerLiteral& i) {
  return i.To<int32_t>();
}
inline int31_t ConstexprIntegerLiteralToInt31(const IntegerLiteral& i) {
  return int31_t(ConstexprIntegerLiteralToInt32(i));
}
inline intptr_t ConstexprIntegerLiteralToIntptr(const IntegerLiteral& i) {
  return i.To<intptr_t>();
}

inline void Print(const char* str) { PrintF("%s", str); }

}  // namespace CodeStubAssembler
}  // namespace TorqueRuntimeMacroShims
}  // namespace internal
}  // namespace v8

#endif  // V8_TORQUE_RUNTIME_MACRO_SHIMS_H_
