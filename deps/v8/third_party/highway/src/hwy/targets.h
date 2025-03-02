// Copyright 2020 Google LLC
// SPDX-License-Identifier: Apache-2.0
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef HIGHWAY_HWY_TARGETS_H_
#define HIGHWAY_HWY_TARGETS_H_

// Allows opting out of C++ standard library usage, which is not available in
// some Compiler Explorer environments.
#ifndef HWY_NO_LIBCXX
#include <vector>
#endif

// For SIMD module implementations and their callers. Defines which targets to
// generate and call.

#include "hwy/base.h"
#include "hwy/detect_targets.h"
#include "hwy/highway_export.h"

#if !defined(HWY_NO_LIBCXX)
#include <atomic>
#endif

namespace hwy {

// Returns bitfield of enabled targets that are supported on this CPU; there is
// always at least one such target, hence the return value is never 0. The
// targets returned may change after calling DisableTargets. This function is
// always defined, but the HWY_SUPPORTED_TARGETS wrapper may allow eliding
// calls to it if there is only a single target enabled.
HWY_DLLEXPORT int64_t SupportedTargets();

// Evaluates to a function call, or literal if there is a single target.
#if (HWY_TARGETS & (HWY_TARGETS - 1)) == 0
#define HWY_SUPPORTED_TARGETS HWY_TARGETS
#else
#define HWY_SUPPORTED_TARGETS hwy::SupportedTargets()
#endif

// Subsequent SupportedTargets will not return targets whose bit(s) are set in
// `disabled_targets`. Exception: if SupportedTargets would return 0, it will
// instead return HWY_STATIC_TARGET (there must always be one target to call).
//
// This function is useful for disabling targets known to be buggy, or if the
// best available target is undesirable (perhaps due to throttling or memory
// bandwidth limitations). Use SetSupportedTargetsForTest instead of this
// function for iteratively enabling specific targets for testing.
HWY_DLLEXPORT void DisableTargets(int64_t disabled_targets);

// Subsequent SupportedTargets will return the given set of targets, except
// those disabled via DisableTargets. Call with a mask of 0 to disable the mock
// and return to the normal SupportedTargets behavior. Used to run tests for
// all targets.
HWY_DLLEXPORT void SetSupportedTargetsForTest(int64_t targets);

#ifndef HWY_NO_LIBCXX

// Return the list of targets in HWY_TARGETS supported by the CPU as a list of
// individual HWY_* target macros such as HWY_SCALAR or HWY_NEON. This list
// is affected by the current SetSupportedTargetsForTest() mock if any.
HWY_INLINE std::vector<int64_t> SupportedAndGeneratedTargets() {
  std::vector<int64_t> ret;
  for (int64_t targets = SupportedTargets() & HWY_TARGETS; targets != 0;
       targets = targets & (targets - 1)) {
    int64_t current_target = targets & ~(targets - 1);
    ret.push_back(current_target);
  }
  return ret;
}

#endif  // HWY_NO_LIBCXX

static inline HWY_MAYBE_UNUSED const char* TargetName(int64_t target) {
  switch (target) {
#if HWY_ARCH_X86
    case HWY_SSE2:
      return "SSE2";
    case HWY_SSSE3:
      return "SSSE3";
    case HWY_SSE4:
      return "SSE4";
    case HWY_AVX2:
      return "AVX2";
    case HWY_AVX3:
      return "AVX3";
    case HWY_AVX3_DL:
      return "AVX3_DL";
    case HWY_AVX3_ZEN4:
      return "AVX3_ZEN4";
    case HWY_AVX3_SPR:
      return "AVX3_SPR";
#endif

#if HWY_ARCH_ARM
    case HWY_SVE2_128:
      return "SVE2_128";
    case HWY_SVE_256:
      return "SVE_256";
    case HWY_SVE2:
      return "SVE2";
    case HWY_SVE:
      return "SVE";
    case HWY_NEON_BF16:
      return "NEON_BF16";
    case HWY_NEON:
      return "NEON";
    case HWY_NEON_WITHOUT_AES:
      return "NEON_WITHOUT_AES";
#endif

#if HWY_ARCH_PPC
    case HWY_PPC8:
      return "PPC8";
    case HWY_PPC9:
      return "PPC9";
    case HWY_PPC10:
      return "PPC10";
#endif

#if HWY_ARCH_S390X
    case HWY_Z14:
      return "Z14";
    case HWY_Z15:
      return "Z15";
#endif

#if HWY_ARCH_WASM
    case HWY_WASM:
      return "WASM";
    case HWY_WASM_EMU256:
      return "WASM_EMU256";
#endif

#if HWY_ARCH_RISCV
    case HWY_RVV:
      return "RVV";
#endif

    case HWY_EMU128:
      return "EMU128";
    case HWY_SCALAR:
      return "SCALAR";

    default:
      return "Unknown";  // must satisfy gtest IsValidParamName()
  }
}

// The maximum number of dynamic targets on any architecture is defined by
// HWY_MAX_DYNAMIC_TARGETS and depends on the arch.

// For the ChosenTarget mask and index we use a different bit arrangement than
// in the HWY_TARGETS mask. Only the targets involved in the current
// architecture are used in this mask, and therefore only the least significant
// (HWY_MAX_DYNAMIC_TARGETS + 2) bits of the int64_t mask are used. The least
// significant bit is set when the mask is not initialized, the next
// HWY_MAX_DYNAMIC_TARGETS more significant bits are a range of bits from the
// HWY_TARGETS or SupportedTargets() mask for the given architecture shifted to
// that position and the next more significant bit is used for HWY_SCALAR (if
// HWY_COMPILE_ONLY_SCALAR is defined) or HWY_EMU128. Because of this we need to
// define equivalent values for HWY_TARGETS in this representation.
// This mask representation allows to use ctz() on this mask and obtain a small
// number that's used as an index of the table for dynamic dispatch. In this
// way the first entry is used when the mask is uninitialized, the following
// HWY_MAX_DYNAMIC_TARGETS are for dynamic dispatch and the last one is for
// scalar.

// The HWY_SCALAR/HWY_EMU128 bit in the ChosenTarget mask format.
#define HWY_CHOSEN_TARGET_MASK_SCALAR (1LL << (HWY_MAX_DYNAMIC_TARGETS + 1))

// Converts from a HWY_TARGETS mask to a ChosenTarget mask format for the
// current architecture.
#define HWY_CHOSEN_TARGET_SHIFT(X)                                    \
  ((((X) >> (HWY_HIGHEST_TARGET_BIT + 1 - HWY_MAX_DYNAMIC_TARGETS)) & \
    ((1LL << HWY_MAX_DYNAMIC_TARGETS) - 1))                           \
   << 1)

// The HWY_TARGETS mask in the ChosenTarget mask format.
#define HWY_CHOSEN_TARGET_MASK_TARGETS \
  (HWY_CHOSEN_TARGET_SHIFT(HWY_TARGETS) | HWY_CHOSEN_TARGET_MASK_SCALAR | 1LL)

#if HWY_ARCH_X86
// Maximum number of dynamic targets, changing this value is an ABI incompatible
// change
#define HWY_MAX_DYNAMIC_TARGETS 15
#define HWY_HIGHEST_TARGET_BIT HWY_HIGHEST_TARGET_BIT_X86
// These must match the order in which the HWY_TARGETS are defined
// starting by the least significant (HWY_HIGHEST_TARGET_BIT + 1 -
// HWY_MAX_DYNAMIC_TARGETS) bit. This list must contain exactly
// HWY_MAX_DYNAMIC_TARGETS elements and does not include SCALAR. The first entry
// corresponds to the best target. Don't include a "," at the end of the list.
#define HWY_CHOOSE_TARGET_LIST(func_name)                     \
  nullptr,                             /* reserved */         \
      nullptr,                         /* reserved */         \
      nullptr,                         /* reserved */         \
      nullptr,                         /* reserved */         \
      HWY_CHOOSE_AVX3_SPR(func_name),  /* AVX3_SPR */         \
      nullptr,                         /* reserved */         \
      HWY_CHOOSE_AVX3_ZEN4(func_name), /* AVX3_ZEN4 */        \
      HWY_CHOOSE_AVX3_DL(func_name),   /* AVX3_DL */          \
      HWY_CHOOSE_AVX3(func_name),      /* AVX3 */             \
      HWY_CHOOSE_AVX2(func_name),      /* AVX2 */             \
      nullptr,                         /* AVX */              \
      HWY_CHOOSE_SSE4(func_name),      /* SSE4 */             \
      HWY_CHOOSE_SSSE3(func_name),     /* SSSE3 */            \
      nullptr,                         /* reserved - SSE3? */ \
      HWY_CHOOSE_SSE2(func_name)       /* SSE2 */

#elif HWY_ARCH_ARM
// See HWY_ARCH_X86 above for details.
#define HWY_MAX_DYNAMIC_TARGETS 15
#define HWY_HIGHEST_TARGET_BIT HWY_HIGHEST_TARGET_BIT_ARM
#define HWY_CHOOSE_TARGET_LIST(func_name)                              \
  nullptr,                                   /* reserved */            \
      nullptr,                               /* reserved */            \
      nullptr,                               /* reserved */            \
      HWY_CHOOSE_SVE2_128(func_name),        /* SVE2 128-bit */        \
      HWY_CHOOSE_SVE_256(func_name),         /* SVE 256-bit */         \
      nullptr,                               /* reserved */            \
      nullptr,                               /* reserved */            \
      nullptr,                               /* reserved */            \
      HWY_CHOOSE_SVE2(func_name),            /* SVE2 */                \
      HWY_CHOOSE_SVE(func_name),             /* SVE */                 \
      nullptr,                               /* reserved */            \
      HWY_CHOOSE_NEON_BF16(func_name),       /* NEON + f16/dot/bf16 */ \
      nullptr,                               /* reserved */            \
      HWY_CHOOSE_NEON(func_name),            /* NEON */                \
      HWY_CHOOSE_NEON_WITHOUT_AES(func_name) /* NEON without AES */

#elif HWY_ARCH_RISCV
// See HWY_ARCH_X86 above for details.
#define HWY_MAX_DYNAMIC_TARGETS 9
#define HWY_HIGHEST_TARGET_BIT HWY_HIGHEST_TARGET_BIT_RVV
#define HWY_CHOOSE_TARGET_LIST(func_name)       \
  nullptr,                       /* reserved */ \
      nullptr,                   /* reserved */ \
      nullptr,                   /* reserved */ \
      nullptr,                   /* reserved */ \
      nullptr,                   /* reserved */ \
      nullptr,                   /* reserved */ \
      nullptr,                   /* reserved */ \
      HWY_CHOOSE_RVV(func_name), /* RVV */      \
      nullptr                    /* reserved */

#elif HWY_ARCH_PPC || HWY_ARCH_S390X
// See HWY_ARCH_X86 above for details.
#define HWY_MAX_DYNAMIC_TARGETS 9
#define HWY_HIGHEST_TARGET_BIT HWY_HIGHEST_TARGET_BIT_PPC
#define HWY_CHOOSE_TARGET_LIST(func_name)         \
  nullptr,                         /* reserved */ \
      nullptr,                     /* reserved */ \
      nullptr,                     /* reserved */ \
      nullptr,                     /* reserved */ \
      HWY_CHOOSE_PPC10(func_name), /* PPC10 */    \
      HWY_CHOOSE_PPC9(func_name),  /* PPC9 */     \
      HWY_CHOOSE_PPC8(func_name),  /* PPC8 */     \
      HWY_CHOOSE_Z15(func_name),   /* Z15 */      \
      HWY_CHOOSE_Z14(func_name)    /* Z14 */

#elif HWY_ARCH_WASM
// See HWY_ARCH_X86 above for details.
#define HWY_MAX_DYNAMIC_TARGETS 9
#define HWY_HIGHEST_TARGET_BIT HWY_HIGHEST_TARGET_BIT_WASM
#define HWY_CHOOSE_TARGET_LIST(func_name)                  \
  nullptr,                               /* reserved */    \
      nullptr,                           /* reserved */    \
      nullptr,                           /* reserved */    \
      nullptr,                           /* reserved */    \
      nullptr,                           /* reserved */    \
      nullptr,                           /* reserved */    \
      HWY_CHOOSE_WASM_EMU256(func_name), /* WASM_EMU256 */ \
      HWY_CHOOSE_WASM(func_name),        /* WASM */        \
      nullptr                            /* reserved */

#else
// Unknown architecture, will use HWY_SCALAR without dynamic dispatch, though
// still creating single-entry tables in HWY_EXPORT to ensure portability.
#define HWY_MAX_DYNAMIC_TARGETS 1
#define HWY_HIGHEST_TARGET_BIT HWY_HIGHEST_TARGET_BIT_SCALAR
#endif

// Bitfield of supported and enabled targets. The format differs from that of
// HWY_TARGETS; the lowest bit governs the first function pointer (which is
// special in that it calls FunctionCache, then Update, then dispatches to the
// actual implementation) in the tables created by HWY_EXPORT. Monostate (see
// GetChosenTarget), thread-safe except on RVV.
struct ChosenTarget {
 public:
  // Reset bits according to `targets` (typically the return value of
  // SupportedTargets()). Postcondition: IsInitialized() == true.
  void Update(int64_t targets) {
    // These are `targets` shifted downwards, see above. Also include SCALAR
    // (corresponds to the last entry in the function table) as fallback.
    StoreMask(HWY_CHOSEN_TARGET_SHIFT(targets) | HWY_CHOSEN_TARGET_MASK_SCALAR);
  }

  // Reset to the uninitialized state, so that FunctionCache will call Update
  // during the next HWY_DYNAMIC_DISPATCH, and IsInitialized returns false.
  void DeInit() { StoreMask(1); }

  // Whether Update was called. This indicates whether any HWY_DYNAMIC_DISPATCH
  // function was called, which we check in tests.
  bool IsInitialized() const { return LoadMask() != 1; }

  // Return the index in the dynamic dispatch table to be used by the current
  // CPU. Note that this method must be in the header file so it uses the value
  // of HWY_CHOSEN_TARGET_MASK_TARGETS defined in the translation unit that
  // calls it, which may be different from others. This means we only enable
  // those targets that were actually compiled in this module.
  size_t HWY_INLINE GetIndex() const {
    return hwy::Num0BitsBelowLS1Bit_Nonzero64(
        static_cast<uint64_t>(LoadMask() & HWY_CHOSEN_TARGET_MASK_TARGETS));
  }

 private:
#if defined(HWY_NO_LIBCXX)
  int64_t LoadMask() const { return mask_; }
  void StoreMask(int64_t mask) { mask_ = mask; }

  int64_t mask_{1};  // Initialized to 1 so GetIndex() returns 0.
#else
  int64_t LoadMask() const { return mask_.load(); }
  void StoreMask(int64_t mask) { mask_.store(mask); }

  std::atomic<int64_t> mask_{1};  // Initialized to 1 so GetIndex() returns 0.
#endif  // HWY_ARCH_RISCV
};

// For internal use (e.g. by FunctionCache and DisableTargets).
HWY_DLLEXPORT ChosenTarget& GetChosenTarget();

}  // namespace hwy

#endif  // HIGHWAY_HWY_TARGETS_H_
