// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_EXECUTION_POINTER_AUTHENTICATION_DUMMY_H_
#define V8_EXECUTION_POINTER_AUTHENTICATION_DUMMY_H_

#include "include/v8-internal.h"
#include "src/base/logging.h"
#include "src/base/macros.h"
#include "src/execution/pointer-authentication.h"
#include "src/flags/flags.h"

namespace v8 {
namespace internal {

// Dummy implementation of the PointerAuthentication class methods, to be used
// when CFI is not enabled.

// Load return address from {pc_address} and return it.
V8_INLINE Address PointerAuthentication::AuthenticatePC(Address* pc_address,
                                                        unsigned) {
  return *pc_address;
}

// Return {pc} unmodified.
V8_INLINE Address PointerAuthentication::StripPAC(Address pc) { return pc; }

// Store {new_pc} to {pc_address} without signing.
V8_INLINE void PointerAuthentication::ReplacePC(Address* pc_address,
                                                Address new_pc, int) {
  *pc_address = new_pc;
}

// Return {pc} unmodified.
V8_INLINE Address PointerAuthentication::SignAndCheckPC(Isolate*, Address pc,
                                                        Address) {
  return pc;
}

V8_INLINE Address PointerAuthentication::MoveSignedPC(Isolate*, Address pc,
                                                      Address, Address) {
#if V8_ENABLE_WEBASSEMBLY
  // Only used by wasm deoptimizations and growable stacks.
  CHECK(v8_flags.wasm_deopt || v8_flags.experimental_wasm_growable_stacks);
  return pc;
#else
  UNREACHABLE();
#endif
}

}  // namespace internal
}  // namespace v8

#endif  // V8_EXECUTION_POINTER_AUTHENTICATION_DUMMY_H_
