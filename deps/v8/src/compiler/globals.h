// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_COMPILER_GLOBALS_H_
#define V8_COMPILER_GLOBALS_H_

#include <ostream>

#include "src/common/globals.h"
#include "src/flags/flags.h"
#include "src/objects/js-objects.h"
#include "src/runtime/runtime.h"

namespace v8 {
namespace internal {
namespace compiler {

// The nci flag is currently used to experiment with feedback collection in
// optimized code produced by generic lowering.
// Considerations:
// - Should we increment the call count? https://crbug.com/v8/10524
// - Is feedback already megamorphic in all these cases?
//
// TODO(jgruber): Remove once we've made a decision whether to collect feedback
// unconditionally.
inline bool CollectFeedbackInGenericLowering() {
  return v8_flags.turbo_collect_feedback_in_generic_lowering;
}

enum class StackCheckKind : uint8_t {
  kJSFunctionEntry = 0,
  kJSIterationBody,
  kCodeStubAssembler,
  kWasm,
};

inline Runtime::FunctionId GetBuiltinForStackCheckKind(StackCheckKind kind) {
  if (kind == StackCheckKind::kJSFunctionEntry) {
    return Runtime::kStackGuardWithGap;
  } else if (kind == StackCheckKind::kJSIterationBody) {
    return Runtime::kHandleNoHeapWritesInterrupts;
  } else {
    return Runtime::kStackGuard;
  }
}

enum class CanThrow : uint8_t { kNo, kYes };
enum class LazyDeoptOnThrow : uint8_t { kNo, kYes };

inline std::ostream& operator<<(std::ostream& os,
                                LazyDeoptOnThrow lazy_deopt_on_throw) {
  switch (lazy_deopt_on_throw) {
    case LazyDeoptOnThrow::kYes:
      return os << "LazyDeoptOnThrow";
    case LazyDeoptOnThrow::kNo:
      return os << "DoNOTLazyDeoptOnThrow";
  }
}

inline std::ostream& operator<<(std::ostream& os, StackCheckKind kind) {
  switch (kind) {
    case StackCheckKind::kJSFunctionEntry:
      return os << "JSFunctionEntry";
    case StackCheckKind::kJSIterationBody:
      return os << "JSIterationBody";
    case StackCheckKind::kCodeStubAssembler:
      return os << "CodeStubAssembler";
    case StackCheckKind::kWasm:
      return os << "Wasm";
  }
  UNREACHABLE();
}

inline size_t hash_value(StackCheckKind kind) {
  return static_cast<size_t>(kind);
}

enum class CheckForMinusZeroMode : uint8_t {
  kCheckForMinusZero,
  kDontCheckForMinusZero,
};

inline size_t hash_value(CheckForMinusZeroMode mode) {
  return static_cast<size_t>(mode);
}

inline std::ostream& operator<<(std::ostream& os, CheckForMinusZeroMode mode) {
  switch (mode) {
    case CheckForMinusZeroMode::kCheckForMinusZero:
      return os << "check-for-minus-zero";
    case CheckForMinusZeroMode::kDontCheckForMinusZero:
      return os << "dont-check-for-minus-zero";
  }
  UNREACHABLE();
}

// The CallFeedbackRelation provides the meaning of the call feedback for a
// TurboFan JSCall operator
// - kReceiver: The call target was Function.prototype.apply and its receiver
//   was recorded as the feedback value.
// - kTarget: The call target was recorded as the feedback value.
// - kUnrelated: The feedback is no longer related to the call. If, during
//   lowering, a JSCall (e.g. of a higher order function) is replaced by a
//   JSCall with another target, the feedback has to be kept but is now
//   unrelated.
enum class CallFeedbackRelation { kReceiver, kTarget, kUnrelated };

inline std::ostream& operator<<(std::ostream& os,
                                CallFeedbackRelation call_feedback_relation) {
  switch (call_feedback_relation) {
    case CallFeedbackRelation::kReceiver:
      return os << "CallFeedbackRelation::kReceiver";
    case CallFeedbackRelation::kTarget:
      return os << "CallFeedbackRelation::kTarget";
    case CallFeedbackRelation::kUnrelated:
      return os << "CallFeedbackRelation::kUnrelated";
  }
  UNREACHABLE();
}

// Maximum depth and total number of elements and properties for literal
// graphs to be considered for fast deep-copying. The limit is chosen to
// match the maximum number of inobject properties, to ensure that the
// performance of using object literals is not worse than using constructor
// functions, see crbug.com/v8/6211 for details.
const int kMaxFastLiteralDepth = 3;
const int kMaxFastLiteralProperties = JSObject::kMaxInObjectProperties;

enum BaseTaggedness : uint8_t { kUntaggedBase, kTaggedBase };

enum class MemoryAccessKind : uint8_t {
  kNormal,
  kUnaligned,
  kProtected,
};

size_t hash_value(MemoryAccessKind);

V8_EXPORT_PRIVATE std::ostream& operator<<(std::ostream&, MemoryAccessKind);

inline ExternalArrayType GetArrayTypeFromElementsKind(ElementsKind kind) {
  switch (kind) {
#define TYPED_ARRAY_CASE(Type, type, TYPE, ctype) \
  case TYPE##_ELEMENTS:                           \
  case RAB_GSAB_##TYPE##_ELEMENTS:                \
    return kExternal##Type##Array;
    TYPED_ARRAYS(TYPED_ARRAY_CASE)
#undef TYPED_ARRAY_CASE
    default:
      break;
  }
  UNREACHABLE();
}

inline int ExternalArrayElementSize(const ExternalArrayType element_type) {
  switch (element_type) {
#define TYPED_ARRAY_CASE(Type, type, TYPE, ctype) \
  case kExternal##Type##Array:                    \
    DCHECK_LE(sizeof(ctype), 8);                  \
    return sizeof(ctype);
    TYPED_ARRAYS(TYPED_ARRAY_CASE)
    default:
      UNREACHABLE();
#undef TYPED_ARRAY_CASE
  }
}

}  // namespace compiler
}  // namespace internal
}  // namespace v8

// The biggest double value that fits within the int64_t/uint64_t value range.
// This is different from safe integer range in that there are gaps of integers
// in-between that cannot be represented as a double.
constexpr double kMaxDoubleRepresentableInt64 = 9223372036854774784.0;
constexpr double kMinDoubleRepresentableInt64 =
    std::numeric_limits<int64_t>::min();
constexpr double kMaxDoubleRepresentableUint64 = 18446744073709549568.0;

// There is no (currently) available constexpr version of base::bit_cast, so
// we have to make do with constructing the -0.0 bits manually (by setting the
// sign bit to 1 and everything else to 0).
// TODO(leszeks): Revisit when upgrading to C++20.
constexpr int32_t kMinusZeroLoBits = static_cast<int32_t>(0);
constexpr int32_t kMinusZeroHiBits = static_cast<int32_t>(1) << 31;
constexpr int64_t kMinusZeroBits =
    (static_cast<uint64_t>(kMinusZeroHiBits) << 32) | kMinusZeroLoBits;

#endif  // V8_COMPILER_GLOBALS_H_
