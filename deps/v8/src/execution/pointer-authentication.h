// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_EXECUTION_POINTER_AUTHENTICATION_H_
#define V8_EXECUTION_POINTER_AUTHENTICATION_H_

#include "include/v8-internal.h"
#include "src/base/macros.h"
#include "src/common/globals.h"

namespace v8 {
namespace internal {

class PointerAuthentication : public AllStatic {
 public:
  // When CFI is enabled, authenticate the address stored in {pc_address} and
  // return the authenticated address. {offset_from_sp} is the offset between
  // {pc_address} and the pointer used as a context for signing.
  // When CFI is not enabled, simply load return address from {pc_address} and
  // return it.
  V8_INLINE static Address AuthenticatePC(Address* pc_address,
                                          unsigned offset_from_sp);

  // When CFI is enabled, strip Pointer Authentication Code (PAC) from {pc} and
  // return the raw value.
  // When CFI is not enabled, return {pc} unmodified.
  V8_INLINE static Address StripPAC(Address pc);

  // When CFI is enabled, authenticate the address stored in {pc_address} and
  // replace it with {new_pc}, after signing it. {offset_from_sp} is the offset
  // between {pc_address} and the pointer used as a context for signing.
  // When CFI is not enabled, store {new_pc} to {pc_address} without signing.
  V8_INLINE static void ReplacePC(Address* pc_address, Address new_pc,
                                  int offset_from_sp);

  // When CFI is enabled, sign {pc} using {sp}, check the address and return the
  // signed value. When CFI is not enabled, return {pc} unmodified. This method
  // only applies in the deoptimizer.
  V8_INLINE static Address SignAndCheckPC(Isolate* isolate, Address pc,
                                          Address sp);

  // When CFI is enabled, verify that {pc} is signed correctly for {old_sp},
  // then re-sign {pc} using {new_sp} and return the signed value. When CFI is
  // not enabled, return {pc} unmodified. This method only applies in the
  // deoptimizer for wasm deoptimizations.
  V8_INLINE static Address MoveSignedPC(Isolate* isolate, Address pc,
                                        Address new_sp, Address old_sp);
};

}  // namespace internal
}  // namespace v8

#ifdef V8_ENABLE_CONTROL_FLOW_INTEGRITY

#ifndef V8_TARGET_ARCH_ARM64
#error "V8_ENABLE_CONTROL_FLOW_INTEGRITY should imply V8_TARGET_ARCH_ARM64"
#endif
#include "src/execution/arm64/pointer-authentication-arm64.h"

#else

#include "src/execution/pointer-authentication-dummy.h"

#endif

#endif  // V8_EXECUTION_POINTER_AUTHENTICATION_H_
