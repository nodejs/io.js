// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#if !V8_ENABLE_WEBASSEMBLY
#error This header should only be included if WebAssembly is enabled.
#endif  // !V8_ENABLE_WEBASSEMBLY

#ifndef V8_WASM_WASM_TIER_H_
#define V8_WASM_WASM_TIER_H_

#include <cstdint>

namespace v8 {
namespace internal {
namespace wasm {

// All the tiers of Wasm execution.
enum class ExecutionTier : int8_t {
  kNone,
#if V8_ENABLE_DRUMBRAKE
  kInterpreter,
#endif  // V8_ENABLE_DRUMBRAKE
  kLiftoff,
  kTurbofan,
};

inline const char* ExecutionTierToString(ExecutionTier tier) {
  switch (tier) {
    case ExecutionTier::kTurbofan:
      return "turbofan";
    case ExecutionTier::kLiftoff:
      return "liftoff";
#if V8_ENABLE_DRUMBRAKE
    case ExecutionTier::kInterpreter:
      return "interpreter";
#endif  // V8_ENABLE_DRUMBRAKE
    case ExecutionTier::kNone:
      return "none";
  }
}

// {kForDebugging} is used for default tiered-down code, {kWithBreakpoints} if
// the code also contains breakpoints, and {kForStepping} for code that is
// flooded with breakpoints.
enum ForDebugging : int8_t {
  kNotForDebugging = 0,
  kForDebugging,
  kWithBreakpoints,
  kForStepping
};

enum DebugState : bool { kNotDebugging = false, kDebugging = true };

}  // namespace wasm
}  // namespace internal
}  // namespace v8

#endif  // V8_WASM_WASM_TIER_H_
