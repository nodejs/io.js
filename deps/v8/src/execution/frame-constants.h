// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_EXECUTION_FRAME_CONSTANTS_H_
#define V8_EXECUTION_FRAME_CONSTANTS_H_

#include "src/common/globals.h"
#include "src/flags/flags.h"

namespace v8 {
namespace internal {

// Every pointer in a frame has a slot id. On 32-bit platforms, doubles consume
// two slots.
//
// Stack slot indices >= 0 access the callee stack with slot 0 corresponding to
// the callee's saved return address and 1 corresponding to the saved frame
// pointer. Some frames have additional information stored in the fixed header,
// for example JSFunctions store the function context and marker in the fixed
// header, with slot index 2 corresponding to the current function context and 3
// corresponding to the frame marker/JSFunction.
//
//  slot      JS frame
//       +-----------------+--------------------------------
//  -n-1 |   parameter n   |                            ^
//       |- - - - - - - - -|                            |
//  -n   |  parameter n-1  |                          Caller
//  ...  |       ...       |                       frame slots
//  -2   |   parameter 1   |                       (slot < 0)
//       |- - - - - - - - -|                            |
//  -1   |   parameter 0   |                            v
//  -----+-----------------+--------------------------------
//   0   |   return addr   |   ^                        ^
//       |- - - - - - - - -|   |                        |
//   1   | saved frame ptr | Fixed                      |
//       |- - - - - - - - -| Header <-- frame ptr       |
//   2   | [Constant Pool] |   |                        |
//       |- - - - - - - - -|   |                        |
// 2+cp  |Context/Frm. Type|   v   if a constant pool   |
//       |-----------------+----    is used, cp = 1,    |
// 3+cp  |                 |   ^   otherwise, cp = 0    |
//       |- - - - - - - - -|   |                        |
// 4+cp  |                 |   |                      Callee
//       |- - - - - - - - -|   |                   frame slots
//  ...  |                 | Frame slots           (slot >= 0)
//       |- - - - - - - - -|   |                        |
//       |                 |   v                        |
//  -----+-----------------+----- <-- stack ptr -------------
//
class CommonFrameConstants : public AllStatic {
 public:
  static constexpr int kCallerFPOffset = 0 * kSystemPointerSize;
  static constexpr int kCallerPCOffset = kCallerFPOffset + 1 * kFPOnStackSize;
  static constexpr int kCallerSPOffset = kCallerPCOffset + 1 * kPCOnStackSize;

  // Fixed part of the frame consists of return address, caller fp,
  // constant pool (if V8_EMBEDDED_CONSTANT_POOL_BOOL), context, and
  // function. CommonFrame::IterateExpressions assumes that kLastObjectOffset
  // is the last object pointer.
  static constexpr int kFixedFrameSizeAboveFp = kPCOnStackSize + kFPOnStackSize;
  static constexpr int kFixedSlotCountAboveFp =
      kFixedFrameSizeAboveFp / kSystemPointerSize;
  static constexpr int kCPSlotSize =
      V8_EMBEDDED_CONSTANT_POOL_BOOL ? kSystemPointerSize : 0;
  static constexpr int kCPSlotCount = kCPSlotSize / kSystemPointerSize;
  static constexpr int kConstantPoolOffset =
      kCPSlotSize ? -1 * kSystemPointerSize : 0;
  static constexpr int kContextOrFrameTypeSize = kSystemPointerSize;
  static constexpr int kContextOrFrameTypeOffset =
      -(kCPSlotSize + kContextOrFrameTypeSize);
};

// StandardFrames are used for both unoptimized and optimized JavaScript
// frames. They always have a context below the saved fp/constant
// pool, below that the JSFunction of the executing function and below that an
// integer (not a Smi) containing the actual number of arguments passed to the
// JavaScript code.
//
//  slot      JS frame
//       +-----------------+--------------------------------
//  -n-1 |   parameter n   |                            ^
//       |- - - - - - - - -|                            |
//  -n   |  parameter n-1  |                          Caller
//  ...  |       ...       |                       frame slots
//  -2   |   parameter 1   |                       (slot < 0)
//       |- - - - - - - - -|                            |
//  -1   |   parameter 0   |                            v
//  -----+-----------------+--------------------------------
//   0   |   return addr   |   ^                        ^
//       |- - - - - - - - -|   |                        |
//   1   | saved frame ptr | Fixed                      |
//       |- - - - - - - - -| Header <-- frame ptr       |
//   2   | [Constant Pool] |   |                        |
//       |- - - - - - - - -|   |                        |
// 2+cp  |     Context     |   |   if a constant pool   |
//       |- - - - - - - - -|   |    is used, cp = 1,    |
// 3+cp  |    JSFunction   |   |   otherwise, cp = 0    |
//       |- - - - - - - - -|   |                        |
// 4+cp  |      argc       |   v                        |
//       +-----------------+----                        |
// 5+cp  |  expressions or |   ^                      Callee
//       |- - - - - - - - -|   |                   frame slots
//  ...  |  pushed values  | Frame slots           (slot >= 0)
//       |- - - - - - - - -|   |                        |
//       |                 |   v                        |
//  -----+-----------------+----- <-- stack ptr -------------
//
class StandardFrameConstants : public CommonFrameConstants {
 public:
  static constexpr int kFixedFrameSizeFromFp =
      3 * kSystemPointerSize + kCPSlotSize;
  static constexpr int kFixedFrameSize =
      kFixedFrameSizeAboveFp + kFixedFrameSizeFromFp;
  static constexpr int kFixedSlotCountFromFp =
      kFixedFrameSizeFromFp / kSystemPointerSize;
  static constexpr int kFixedSlotCount = kFixedFrameSize / kSystemPointerSize;
  static constexpr int kContextOffset = kContextOrFrameTypeOffset;
  static constexpr int kFunctionOffset = -2 * kSystemPointerSize - kCPSlotSize;
  static constexpr int kArgCOffset = -3 * kSystemPointerSize - kCPSlotSize;
  static constexpr int kExpressionsOffset =
      -4 * kSystemPointerSize - kCPSlotSize;
  static constexpr int kFirstPushedFrameValueOffset = kExpressionsOffset;
  static constexpr int kLastObjectOffset = kContextOffset;
};

// TypedFrames have a type maker value below the saved FP/constant pool to
// distinguish them from StandardFrames, which have a context in that position
// instead.
//
//  slot      JS frame
//       +-----------------+--------------------------------
//  -n-1 |   parameter n   |                            ^
//       |- - - - - - - - -|                            |
//  -n   |  parameter n-1  |                          Caller
//  ...  |       ...       |                       frame slots
//  -2   |   parameter 1   |                       (slot < 0)
//       |- - - - - - - - -|                            |
//  -1   |   parameter 0   |                            v
//  -----+-----------------+--------------------------------
//   0   |   return addr   |   ^                        ^
//       |- - - - - - - - -|   |                        |
//   1   | saved frame ptr | Fixed                      |
//       |- - - - - - - - -| Header <-- frame ptr       |
//   2   | [Constant Pool] |   |                        |
//       |- - - - - - - - -|   |                        |
// 2+cp  |Frame Type Marker|   v   if a constant pool   |
//       |-----------------+----    is used, cp = 1,    |
// 3+cp  |  pushed value 0 |   ^   otherwise, cp = 0    |
//       |- - - - - - - - -|   |                        |
// 4+cp  |  pushed value 1 |   |                      Callee
//       |- - - - - - - - -|   |                   frame slots
//  ...  |                 | Frame slots           (slot >= 0)
//       |- - - - - - - - -|   |                        |
//       |                 |   v                        |
//  -----+-----------------+----- <-- stack ptr -------------
//
class TypedFrameConstants : public CommonFrameConstants {
 public:
  // FP-relative.
  static constexpr int kFrameTypeSize = kContextOrFrameTypeSize;
  static constexpr int kFrameTypeOffset = kContextOrFrameTypeOffset;
  static constexpr int kFixedFrameSizeFromFp = kCPSlotSize + kFrameTypeSize;
  static constexpr int kFixedSlotCountFromFp =
      kFixedFrameSizeFromFp / kSystemPointerSize;
  static constexpr int kFixedFrameSize =
      StandardFrameConstants::kFixedFrameSizeAboveFp + kFixedFrameSizeFromFp;
  static constexpr int kFixedSlotCount = kFixedFrameSize / kSystemPointerSize;
  static constexpr int kFirstPushedFrameValueOffset =
      -kFixedFrameSizeFromFp - kSystemPointerSize;
};

#define FRAME_PUSHED_VALUE_OFFSET(parent, x) \
  (parent::kFirstPushedFrameValueOffset - (x)*kSystemPointerSize)
#define FRAME_SIZE(parent, count) \
  (parent::kFixedFrameSize + (count)*kSystemPointerSize)
#define FRAME_SIZE_FROM_FP(parent, count) \
  (parent::kFixedFrameSizeFromFp + (count)*kSystemPointerSize)
#define DEFINE_FRAME_SIZES(parent, count)                                      \
  static constexpr int kFixedFrameSize = FRAME_SIZE(parent, count);            \
  static constexpr int kFixedSlotCount = kFixedFrameSize / kSystemPointerSize; \
  static constexpr int kFixedFrameSizeFromFp =                                 \
      FRAME_SIZE_FROM_FP(parent, count);                                       \
  static constexpr int kFixedSlotCountFromFp =                                 \
      kFixedFrameSizeFromFp / kSystemPointerSize;                              \
  static constexpr int kFirstPushedFrameValueOffset =                          \
      parent::kFirstPushedFrameValueOffset - (count) * kSystemPointerSize;     \
  /* The number of slots added on top of given parent frame type. */           \
  template <typename TParentFrameConstants>                                    \
  static constexpr int getExtraSlotsCountFrom() {                              \
    return kFixedSlotCount - TParentFrameConstants::kFixedSlotCount;           \
  }                                                                            \
  /* TODO(ishell): remove in favour of getExtraSlotsCountFrom() because */     \
  /* it's not clear from which base should we count "extra" - from direct */   \
  /* parent or maybe from parent's parent? */                                  \
  static constexpr int kExtraSlotCount =                                       \
      kFixedSlotCount - parent::kFixedSlotCount

#define STANDARD_FRAME_EXTRA_PUSHED_VALUE_OFFSET(x) \
  FRAME_PUSHED_VALUE_OFFSET(StandardFrameConstants, x)
#define DEFINE_STANDARD_FRAME_SIZES(count) \
  DEFINE_FRAME_SIZES(StandardFrameConstants, count)

#define TYPED_FRAME_PUSHED_VALUE_OFFSET(x) \
  FRAME_PUSHED_VALUE_OFFSET(TypedFrameConstants, x)
#define DEFINE_TYPED_FRAME_SIZES(count) \
  DEFINE_FRAME_SIZES(TypedFrameConstants, count)

class BuiltinFrameConstants : public TypedFrameConstants {
 public:
  // FP-relative.
  static constexpr int kFunctionOffset = TYPED_FRAME_PUSHED_VALUE_OFFSET(0);
  static constexpr int kLengthOffset = TYPED_FRAME_PUSHED_VALUE_OFFSET(1);
  DEFINE_TYPED_FRAME_SIZES(2);
};

class ConstructFrameConstants : public TypedFrameConstants {
 public:
  // FP-relative.
  static constexpr int kContextOffset = TYPED_FRAME_PUSHED_VALUE_OFFSET(0);
  static constexpr int kLengthOffset = TYPED_FRAME_PUSHED_VALUE_OFFSET(1);
  static constexpr int kConstructorOffset = TYPED_FRAME_PUSHED_VALUE_OFFSET(2);
  static constexpr int kPaddingOffset = TYPED_FRAME_PUSHED_VALUE_OFFSET(3);
  static constexpr int kNewTargetOrImplicitReceiverOffset =
      TYPED_FRAME_PUSHED_VALUE_OFFSET(4);
  DEFINE_TYPED_FRAME_SIZES(5);
  static constexpr int kLastObjectOffset = kContextOffset;
};

class FastConstructFrameConstants : public TypedFrameConstants {
 public:
  // FP-relative.
  static constexpr int kContextOffset = TYPED_FRAME_PUSHED_VALUE_OFFSET(0);
  static constexpr int kImplicitReceiverOffset =
      TYPED_FRAME_PUSHED_VALUE_OFFSET(1);
  DEFINE_TYPED_FRAME_SIZES(2);
};

#if V8_ENABLE_WEBASSEMBLY
class CWasmEntryFrameConstants : public TypedFrameConstants {
 public:
  // FP-relative:
  static constexpr int kCEntryFPOffset = TYPED_FRAME_PUSHED_VALUE_OFFSET(0);
  DEFINE_TYPED_FRAME_SIZES(1);
};

class WasmFrameConstants : public TypedFrameConstants {
 public:
  // FP-relative.
  static constexpr int kWasmInstanceDataOffset =
      TYPED_FRAME_PUSHED_VALUE_OFFSET(0);
  DEFINE_TYPED_FRAME_SIZES(1);

  // The WasmTrapHandlerLandingPad builtin gets called from the WebAssembly
  // trap handler when an out-of-bounds memory access happened or when a null
  // reference gets dereferenced. This builtin then fakes a call from the
  // instruction that triggered the signal to the runtime. This is done by
  // setting a return address and then jumping to a builtin which will call
  // further to the runtime. As the return address we use the fault address +
  // {kProtectedInstructionReturnAddressOffset}. Using the fault address itself
  // would cause problems with safepoints and source positions.
  //
  // The problem with safepoints is that a safepoint has to be registered at the
  // return address, and that at most one safepoint should be registered at a
  // location. However, there could already be a safepoint registered at the
  // fault address if the fault address is the return address of a call.
  //
  // The problem with source positions is that the stack trace code looks for
  // the source position of a call before the return address. The source
  // position of the faulty memory access, however, is recorded at the fault
  // address. Therefore the stack trace code would not find the source position
  // if we used the fault address as the return address.
  static constexpr int kProtectedInstructionReturnAddressOffset = 1;
};

#if V8_ENABLE_DRUMBRAKE
class WasmInterpreterFrameConstants : public TypedFrameConstants {
 public:
  // FP-relative.
  static constexpr int kWasmInstanceObjectOffset =
      TYPED_FRAME_PUSHED_VALUE_OFFSET(0);
  DEFINE_TYPED_FRAME_SIZES(1);
};

// Fixed frame slots shared by the interpreter wasm-to-js wrapper.
class WasmToJSInterpreterFrameConstants : public TypedFrameConstants {
 public:
  // This slot contains the number of slots at the top of the frame that need to
  // be scanned by the GC.
  static constexpr int kGCScanSlotLimitOffset =
      TYPED_FRAME_PUSHED_VALUE_OFFSET(0);

  // The stack pointer at the moment of the JS function call.
  static constexpr int kGCSPOffset = TYPED_FRAME_PUSHED_VALUE_OFFSET(1);
};

class WasmInterpreterCWasmEntryConstants : public TypedFrameConstants {
 public:
  // FP-relative:
  static constexpr int kCEntryFPOffset = TYPED_FRAME_PUSHED_VALUE_OFFSET(0);
  static constexpr int kSPFPOffset = TYPED_FRAME_PUSHED_VALUE_OFFSET(1);
  DEFINE_TYPED_FRAME_SIZES(2);
};
#endif  // V8_ENABLE_DRUMBRAKE

class WasmExitFrameConstants : public WasmFrameConstants {
 public:
  // FP-relative.
  static const int kCallingPCOffset = TYPED_FRAME_PUSHED_VALUE_OFFSET(1);
  DEFINE_TYPED_FRAME_SIZES(2);
};

// Fixed frame slots used by the js-to-wasm wrapper.
class JSToWasmWrapperFrameConstants : public TypedFrameConstants {
 public:
  // FP-relative.
  static constexpr int kResultArrayParamOffset = 2 * kSystemPointerSize;
  // A WasmTrustedInstanceData or WasmImportData depending on the callee.
  static constexpr int kImplicitArgOffset = 3 * kSystemPointerSize;

  // Contains RawPtr to stack-allocated buffer.
  static constexpr int kWrapperBufferOffset =
      TYPED_FRAME_PUSHED_VALUE_OFFSET(0);

  // Offsets into the wrapper buffer for values passed from Torque to the
  // assembly builtin.
  static constexpr size_t kWrapperBufferReturnCount = 0;
  static constexpr size_t kWrapperBufferRefReturnCount = 4;
  static constexpr size_t kWrapperBufferSigRepresentationArray = 8;
  static constexpr size_t kWrapperBufferStackReturnBufferSize = 16;
  static constexpr size_t kWrapperBufferCallTarget = 24;
  static constexpr size_t kWrapperBufferParamStart = 32;
  static constexpr size_t kWrapperBufferParamEnd = 40;

  // Offsets into the wrapper buffer for values passed from the assembly builtin
  // to Torque.
  static constexpr size_t kWrapperBufferStackReturnBufferStart = 16;
  static constexpr size_t kWrapperBufferFPReturnRegister1 = 24;
  static constexpr size_t kWrapperBufferFPReturnRegister2 = 32;
  static constexpr size_t kWrapperBufferGPReturnRegister1 = 40;
  static constexpr size_t kWrapperBufferGPReturnRegister2 =
      kWrapperBufferGPReturnRegister1 + kSystemPointerSize;

  // Size of the wrapper buffer
  static constexpr int kWrapperBufferSize =
      kWrapperBufferGPReturnRegister2 + kSystemPointerSize;
  static_assert(kWrapperBufferParamEnd + kSystemPointerSize <=
                kWrapperBufferSize);
};

// Fixed frame slots used by the ReturnPromiseOnSuspendAsm wrapper
// and the WasmResume wrapper.
class StackSwitchFrameConstants : public JSToWasmWrapperFrameConstants {
 public:
  //  StackSwitching stack layout
  //  ------+-----------------+----------------------
  //        |  return addr    |
  //    fp  |- - - - - - - - -|  -------------------|
  //        |       fp        |                     |
  //   fp-p |- - - - - - - - -|                     |
  //        |  frame marker   |                     | no GC scan
  //  fp-2p |- - - - - - - - -|                     |
  //        |   scan_count    |                     |
  //  fp-3p |- - - - - - - - -|  -------------------|
  //        |  wasm_instance  |                     |
  //  fp-4p |- - - - - - - - -|                     | fixed GC scan
  //        |  result_array   |                     |
  //  fp-5p |- - - - - - - - -|  -------------------|
  //        |      ....       | <- spill_slot_limit |
  //        |   spill slots   |                     | GC scan scan_count slots
  //        |      ....       | <- spill_slot_base--|
  //        |- - - - - - - - -|                     |
  // This slot contains the number of slots at the top of the frame that need to
  // be scanned by the GC.
  static constexpr int kGCScanSlotCountOffset =
      TYPED_FRAME_PUSHED_VALUE_OFFSET(1);
  // Tagged pointer to WasmTrustedInstanceData or WasmImportData.
  static constexpr int kImplicitArgOffset = TYPED_FRAME_PUSHED_VALUE_OFFSET(2);
  // Tagged pointer to a JS Array for result values.
  static constexpr int kResultArrayOffset = TYPED_FRAME_PUSHED_VALUE_OFFSET(3);

  static constexpr int kLastSpillOffset = kResultArrayOffset;
  static constexpr int kNumSpillSlots = 4;
};

class WasmToJSWrapperConstants {
 public:
  // FP-relative.
  static constexpr size_t kSignatureOffset = 2 * kSystemPointerSize;
};

#if V8_ENABLE_DRUMBRAKE
class BuiltinWasmInterpreterWrapperConstants : public TypedFrameConstants {
 public:
  // This slot contains the number of slots at the top of the frame that need to
  // be scanned by the GC.
  static constexpr int kGCScanSlotCountOffset =
      TYPED_FRAME_PUSHED_VALUE_OFFSET(0);
  // The number of parameters passed to this function.
  static constexpr int kInParamCountOffset = TYPED_FRAME_PUSHED_VALUE_OFFSET(1);
  // The number of parameters according to the signature.
  static constexpr int kParamCountOffset = TYPED_FRAME_PUSHED_VALUE_OFFSET(2);
  // The number of return values according to the siganture.
  static constexpr int kReturnCountOffset = TYPED_FRAME_PUSHED_VALUE_OFFSET(3);
  // `reps_` of wasm::FunctionSig.
  static constexpr int kSigRepsOffset = TYPED_FRAME_PUSHED_VALUE_OFFSET(4);
  // The current valuetype in the function signature being converted.
  static constexpr int kValueTypesArrayStartOffset =
      TYPED_FRAME_PUSHED_VALUE_OFFSET(5);
  // Array of arguments/return values.
  static constexpr int kArgRetsAddressOffset =
      TYPED_FRAME_PUSHED_VALUE_OFFSET(6);
  // Whether the array is for arguments or return values.
  static constexpr int kArgRetsIsArgsOffset =
      TYPED_FRAME_PUSHED_VALUE_OFFSET(7);
  // The index of the argument or return value being converted.
  static constexpr int kCurrentIndexOffset = TYPED_FRAME_PUSHED_VALUE_OFFSET(8);
  // Precomputed signature data.
  static constexpr int kSignatureDataOffset =
      TYPED_FRAME_PUSHED_VALUE_OFFSET(9);
};
#endif  // V8_ENABLE_DRUMBRAKE
#endif  // V8_ENABLE_WEBASSEMBLY

class BuiltinContinuationFrameConstants : public TypedFrameConstants {
 public:
  // FP-relative.
  static constexpr int kFunctionOffset = TYPED_FRAME_PUSHED_VALUE_OFFSET(0);
  static constexpr int kFrameSPtoFPDeltaAtDeoptimize =
      TYPED_FRAME_PUSHED_VALUE_OFFSET(1);
  static constexpr int kBuiltinContextOffset =
      TYPED_FRAME_PUSHED_VALUE_OFFSET(2);
  static constexpr int kBuiltinIndexOffset = TYPED_FRAME_PUSHED_VALUE_OFFSET(3);

  // The argument count is in the first allocatable register, stored below the
  // fixed part of the frame and therefore is not part of the fixed frame size.
  static constexpr int kArgCOffset = TYPED_FRAME_PUSHED_VALUE_OFFSET(4);
  DEFINE_TYPED_FRAME_SIZES(4);

  // Returns the number of padding stack slots needed when we have
  // 'register_count' register slots.
  // This is needed on some architectures to ensure the stack pointer is
  // aligned.
  static int PaddingSlotCount(int register_count);
};

class ExitFrameConstants : public TypedFrameConstants {
 public:
  // FP-relative.
  static constexpr int kSPOffset = TYPED_FRAME_PUSHED_VALUE_OFFSET(0);
  static constexpr int kLastExitFrameField = kSPOffset;
  DEFINE_TYPED_FRAME_SIZES(1);

  // FP-relative displacement of the caller's SP.  It points just
  // below the saved PC.
  static constexpr int kCallerSPDisplacement = kCallerSPOffset;
};
#define EXIT_FRAME_PUSHED_VALUE_OFFSET(x) \
  FRAME_PUSHED_VALUE_OFFSET(ExitFrameConstants, x)
#define DEFINE_EXIT_FRAME_SIZES(x) DEFINE_FRAME_SIZES(ExitFrameConstants, x);

// Behaves like an exit frame but with extra arguments (target, new target and
// JS arguments count), followed by JS arguments passed to the JS function
// (receiver and etc.).
//
//  slot      JS frame
//       +-----------------+--------------------------------
// -n-1-k|   parameter n   |                            ^
//       |- - - - - - - - -|                            |
//  -n-k |  parameter n-1  |                          Caller
//  ...  |       ...       |                       frame slots
//  -2-k |   parameter 1   |                       (slot < 0)
//       |- - - - - - - - -|                            |
//  -1-k |    receiver     |                            v
//  -----+-----------------+--------------------------------
//  -k   |  extra arg k-1  |                            ^
//       |- - - - - - - - -|                            |
//  -k+1 |  extra arg k-2  |                  Extra arguments passed
//  ...  |       ...       |                      to CPP builtin
//  -2   |   extra arg 1   |                    k := kNumExtraArgs
//       |- - - - - - - - -|                            |
//  -1   |   extra arg 0   |                            v
//  -----+-----------------+--------------------------------
//   0   |   return addr   |   ^                        ^
//       |- - - - - - - - -|   |                        |
//   1   | saved frame ptr | ExitFrame                  |
//       |- - - - - - - - -| Header     <-- frame ptr   |
//   2   | [Constant Pool] |   |                        |
//       |- - - - - - - - -|   |                        |
// 2+cp  |Frame Type Marker|   |   if a constant pool   |
//       |- - - - - - - - -|   |    is used, cp = 1,    |
// 3+cp  |    caller SP    |   v   otherwise, cp = 0    |
//       |-----------------+----                        |
// 4+cp  |       ...       |   ^                      Callee
//       |- - - - - - - - -|   |                   frame slots
//  ...  | C function args | Frame slots           (slot >= 0)
//       |- - - - - - - - -|   |                        |
//       |                 |   v                        |
//  -----+-----------------+----- <-- stack ptr -------------
//
class BuiltinExitFrameConstants : public ExitFrameConstants {
 public:
  // The following constants must be in sync with BuiltinArguments' extra
  // arguments layout. This is guaraneed by static_asserts elsewhere.
  static constexpr int kNewTargetIndex = 0;
  static constexpr int kTargetIndex = 1;
  static constexpr int kArgcIndex = 2;
  // TODO(ishell): this padding is required only on Arm64.
  static constexpr int kPaddingIndex = 3;
  static constexpr int kNumExtraArgs = 4;
  static constexpr int kNumExtraArgsWithReceiver = kNumExtraArgs + 1;

  // BuiltinArguments' arguments_ array.
  static constexpr int kArgumentsArrayOffset = kFixedFrameSizeAboveFp;
  static constexpr int kTargetOffset =
      kArgumentsArrayOffset + kTargetIndex * kSystemPointerSize;
  static constexpr int kNewTargetOffset =
      kArgumentsArrayOffset + kNewTargetIndex * kSystemPointerSize;
  static constexpr int kArgcOffset =
      kArgumentsArrayOffset + kArgcIndex * kSystemPointerSize;

  // JS arguments.
  static constexpr int kReceiverOffset =
      kArgumentsArrayOffset + kNumExtraArgs * kSystemPointerSize;

  static constexpr int kFirstArgumentOffset =
      kReceiverOffset + kSystemPointerSize;
};

// Behaves like an exit frame but with v8::FunctionCallbackInfo's implicit
// arguments (FCI), followed by JS arguments passed to the JS function
// (receiver and etc.).
//
//  slot      JS frame
//       +-----------------+--------------------------------
// -n-1-k|   parameter n   |                            ^
//       |- - - - - - - - -|                            |
//  -n-k |  parameter n-1  |                          Caller
//  ...  |       ...       |                       frame slots
//  -2-k |   parameter 1   |                       (slot < 0)
//       |- - - - - - - - -|                            |
//  -1-k |    receiver     |                            v
//  -----+-----------------+--------------------------------
//  -k   |   FCI slot k-1  |                            ^
//       |- - - - - - - - -|                            |
//  -k+1 |   FCI slot k-2  |                 v8::FunctionCallbackInfo's
//  ...  |       ...       |                   FCI::implicit_args[k]
//  -2   |   FCI slot 1    |                   k := FCI::kArgsLength
//       |- - - - - - - - -|                            |
//  -1   |   FCI slot 0    |                            v
//  -----+-----------------+--------------------------------
//   0   |   return addr   |   ^                        ^
//       |- - - - - - - - -|   |                        |
//   1   | saved frame ptr | ExitFrame                  |
//       |- - - - - - - - -| Header     <-- frame ptr   |
//   2   | [Constant Pool] |   |                        |
//       |- - - - - - - - -|   |                        |
// 2+cp  |Frame Type Marker|   |   if a constant pool   |
//       |- - - - - - - - -|   |    is used, cp = 1,    |
// 3+cp  |    caller SP    |   v   otherwise, cp = 0    |
//       |-----------------+----                        |
// 4+cp  | FCI::argc_      |   ^                      Callee
//       |- - - - - - - - -|   |                   frame slots
// 5+cp  | FCI::values_    |   |                   (slot >= 0)
//       |- - - - - - - - -|   |                        |
// 6+cp  | FCI::imp._args_ | Frame slots                |
//       |- - - - - - - - -|   |                        |
//  ...  | C function args |   |                        |
//       |- - - - - - - - -|   |                        |
//       |                 |   v                        |
//  -----+-----------------+----- <-- stack ptr -------------
//
class ApiCallbackExitFrameConstants : public ExitFrameConstants {
 public:
  // The following constants must be in sync with v8::FunctionCallbackInfo's
  // layout. This is guaraneed by static_asserts elsewhere.
  static constexpr int kFunctionCallbackInfoContextIndex = 2;
  static constexpr int kFunctionCallbackInfoReturnValueIndex = 3;
  static constexpr int kFunctionCallbackInfoTargetIndex = 4;
  static constexpr int kFunctionCallbackInfoNewTargetIndex = 5;
  static constexpr int kFunctionCallbackInfoArgsLength = 6;

  // FP-relative.
  // v8::FunctionCallbackInfo struct (implicit_args_, args_, argc_) is pushed
  // on top of the ExitFrame.
  static constexpr int kFCIArgcOffset = EXIT_FRAME_PUSHED_VALUE_OFFSET(0);
  static constexpr int kFCIValuesOffset = EXIT_FRAME_PUSHED_VALUE_OFFSET(1);
  static constexpr int kFCIImplicitArgsOffset =
      EXIT_FRAME_PUSHED_VALUE_OFFSET(2);

  DEFINE_EXIT_FRAME_SIZES(3)
  static_assert(kSPOffset - kSystemPointerSize == kFCIArgcOffset);

  // v8::FunctionCallbackInfo's struct allocated right below the exit frame.
  static constexpr int kFunctionCallbackInfoOffset = kFCIImplicitArgsOffset;

  // v8::FunctionCallbackInfo's implicit_args array.
  static constexpr int kImplicitArgsArrayOffset = kFixedFrameSizeAboveFp;
  static constexpr int kTargetOffset =
      kImplicitArgsArrayOffset +
      kFunctionCallbackInfoTargetIndex * kSystemPointerSize;
  static constexpr int kNewTargetOffset =
      kImplicitArgsArrayOffset +
      kFunctionCallbackInfoNewTargetIndex * kSystemPointerSize;
  static constexpr int kContextOffset =
      kImplicitArgsArrayOffset +
      kFunctionCallbackInfoContextIndex * kSystemPointerSize;
  static constexpr int kReturnValueOffset =
      kImplicitArgsArrayOffset +
      kFunctionCallbackInfoReturnValueIndex * kSystemPointerSize;

  // JS arguments.
  static constexpr int kReceiverOffset =
      kImplicitArgsArrayOffset +
      kFunctionCallbackInfoArgsLength * kSystemPointerSize;

  static constexpr int kFirstArgumentOffset =
      kReceiverOffset + kSystemPointerSize;
};

// Behaves like an exit frame but with v8::PropertyCallbackInfo's (PCI)
// fields allocated in GC-ed area of the exit frame, followed by zero or
// more parameters (required by some callback kinds).
//
//  slot      JS frame
//       +-----------------+--------------------------------
// -n-1-k|   parameter n   |                            ^
//       |- - - - - - - - -|                            |
//  -n-k |  parameter n-1  |                          Caller
//  ...  |       ...       |                       frame slots
//  -2-k |   parameter 1   |                       (slot < 0)
//       |- - - - - - - - -|                            |
//  -1-k |   parameter 0   |                            v
//  -----+-----------------+--------------------------------
//  -k   |   PCI slot k-1  |                            ^
//       |- - - - - - - - -|                            |
//  -k+1 |   PCI slot k-2  |                 v8::PropertyCallbackInfo's
//  ...  |       ...       |                       PCI::args[k]
//  -2   |   PCI slot 1    |                   k := PCI::kArgsLength
//       |- - - - - - - - -|                            |
//  -1   |   PCI slot 0    |                            v
//  -----+-----------------+--------------------------------   <-- PCI object
//   0   |   return addr   |   ^                        ^
//       |- - - - - - - - -|   |                        |
//   1   | saved frame ptr | ExitFrame                  |
//       |- - - - - - - - -| Header     <-- frame ptr   |
//   2   | [Constant Pool] |   |                        |
//       |- - - - - - - - -|   |                        |
// 2+cp  |Frame Type Marker|   |   if a constant pool   |
//       |- - - - - - - - -|   |    is used, cp = 1,    |
// 3+cp  |    caller SP    |   v   otherwise, cp = 0    |
//       |-----------------+----                        |
// 4+cp  |                 |   ^                      Callee
//       |- - - - - - - - -|   |                   frame slots
//  ...  | C function args | Frame slots           (slot >= 0)
//       |- - - - - - - - -|   |                        |
//       |                 |   v                        |
//  -----+-----------------+----- <-- stack ptr -------------
//
class ApiAccessorExitFrameConstants : public ExitFrameConstants {
 public:
  // The following constants must be in sync with v8::PropertyCallbackInfo's
  // layout. This is guaraneed by static_asserts elsewhere.
  static constexpr int kPropertyCallbackInfoPropertyKeyIndex = 0;
  static constexpr int kPropertyCallbackInfoHolderIndex = 2;
  static constexpr int kPropertyCallbackInfoReturnValueIndex = 5;
  static constexpr int kPropertyCallbackInfoReceiverIndex = 7;
  static constexpr int kPropertyCallbackInfoArgsLength = 8;

  // FP-relative.

  // v8::PropertyCallbackInfo's args array.
  static constexpr int kArgsArrayOffset = kFixedFrameSizeAboveFp;
  static constexpr int kPropertyNameOffset =
      kArgsArrayOffset +
      kPropertyCallbackInfoPropertyKeyIndex * kSystemPointerSize;
  static constexpr int kReturnValueOffset =
      kArgsArrayOffset +
      kPropertyCallbackInfoReturnValueIndex * kSystemPointerSize;
  static constexpr int kReceiverOffset =
      kArgsArrayOffset +
      kPropertyCallbackInfoReceiverIndex * kSystemPointerSize;
  static constexpr int kHolderOffset =
      kArgsArrayOffset + kPropertyCallbackInfoHolderIndex * kSystemPointerSize;

  // v8::PropertyCallbackInfo's address is equal to address of the args_ array.
  static constexpr int kPropertyCallbackInfoOffset = kArgsArrayOffset;
};

// Unoptimized frames are used for interpreted and baseline-compiled JavaScript
// frames. They are a "standard" frame, with an additional fixed header for the
// BytecodeArray, bytecode offset (if running interpreted), feedback vector (if
// running baseline code), and then the interpreter register file.
//
//  slot      JS frame
//       +-----------------+--------------------------------
//  -n-1 |   parameter n   |                            ^
//       |- - - - - - - - -|                            |
//  -n   |  parameter n-1  |                          Caller
//  ...  |       ...       |                       frame slots
//  -2   |   parameter 1   |                       (slot < 0)
//       |- - - - - - - - -|                            |
//  -1   |   parameter 0   |                            v
//  -----+-----------------+--------------------------------
//   0   |   return addr   |   ^                        ^
//       |- - - - - - - - -|   |                        |
//   1   | saved frame ptr | Fixed                      |
//       |- - - - - - - - -| Header <-- frame ptr       |
//   2   | [Constant Pool] |   |                        |
//       |- - - - - - - - -|   |                        |
// 2+cp  |     Context     |   |   if a constant pool   |
//       |- - - - - - - - -|   |    is used, cp = 1,    |
// 3+cp  |    JSFunction   |   |   otherwise, cp = 0    |
//       |- - - - - - - - -|   |                        |
// 4+cp  |      argc       |   v                        |
//       +-----------------+----                        |
// 5+cp  |  BytecodeArray  |   ^                        |
//       |- - - - - - - - -|   |                        |
// 6+cp  |  offset / cell  | Unoptimized code header    |
//       |- - - - - - - - -|   |                        |
// 7+cp  |      FBV        |   v                        |
//       +-----------------+----                        |
// 8+cp  |   register 0    |   ^                     Callee
//       |- - - - - - - - -|   |                   frame slots
// 9+cp  |   register 1    | Register file         (slot >= 0)
//  ...  |       ...       |   |                        |
//       |  register n-1   |   |                        |
//       |- - - - - - - - -|   |                        |
// 9+cp+n|   register n    |   v                        v
//  -----+-----------------+----- <-- stack ptr -------------
//
class UnoptimizedFrameConstants : public StandardFrameConstants {
 public:
  // FP-relative.
  static constexpr int kBytecodeArrayFromFp =
      STANDARD_FRAME_EXTRA_PUSHED_VALUE_OFFSET(0);
  static constexpr int kBytecodeOffsetOrFeedbackCellFromFp =
      STANDARD_FRAME_EXTRA_PUSHED_VALUE_OFFSET(1);
  static constexpr int kFeedbackVectorFromFp =
      STANDARD_FRAME_EXTRA_PUSHED_VALUE_OFFSET(2);
  DEFINE_STANDARD_FRAME_SIZES(3);

  static constexpr int kFirstParamFromFp =
      StandardFrameConstants::kCallerSPOffset;
  static constexpr int kRegisterFileFromFp =
      -kFixedFrameSizeFromFp - kSystemPointerSize;
  static constexpr int kExpressionsOffset = kRegisterFileFromFp;

  // Expression index for {JavaScriptFrame::GetExpressionAddress}.
  static constexpr int kBytecodeArrayExpressionIndex = -3;
  static constexpr int kBytecodeOffsetOrFeedbackCellExpressionIndex = -2;
  static constexpr int kFeedbackVectorExpressionIndex = -1;
  static constexpr int kRegisterFileExpressionIndex = 0;

  // Returns the number of stack slots needed for 'register_count' registers.
  // This is needed because some architectures must pad the stack frame with
  // additional stack slots to ensure the stack pointer is aligned.
  static int RegisterStackSlotCount(int register_count);
};

// Interpreter frames are unoptimized frames that are being executed by the
// interpreter. In this case, the "offset or cell" slot contains the bytecode
// offset of the currently executing bytecode.
class InterpreterFrameConstants : public UnoptimizedFrameConstants {
 public:
  static constexpr int kBytecodeOffsetExpressionIndex =
      kBytecodeOffsetOrFeedbackCellExpressionIndex;

  static constexpr int kBytecodeOffsetFromFp =
      kBytecodeOffsetOrFeedbackCellFromFp;
};

// Sparkplug frames are unoptimized frames that are being executed by
// sparkplug-compiled baseline code. base. In this case, the "offset or cell"
// slot contains the closure feedback cell.
class BaselineFrameConstants : public UnoptimizedFrameConstants {
 public:
  static constexpr int kFeedbackCellExpressionIndex =
      kBytecodeOffsetOrFeedbackCellExpressionIndex;

  static constexpr int kFeedbackCellFromFp =
      kBytecodeOffsetOrFeedbackCellFromFp;
};

inline static int FPOffsetToFrameSlot(int frame_offset) {
  return StandardFrameConstants::kFixedSlotCountAboveFp - 1 -
         frame_offset / kSystemPointerSize;
}

inline static int FrameSlotToFPOffset(int slot) {
  return (StandardFrameConstants::kFixedSlotCountAboveFp - 1 - slot) *
         kSystemPointerSize;
}

}  // namespace internal
}  // namespace v8

#if V8_TARGET_ARCH_IA32
#include "src/execution/ia32/frame-constants-ia32.h"
#elif V8_TARGET_ARCH_X64
#include "src/execution/x64/frame-constants-x64.h"
#elif V8_TARGET_ARCH_ARM64
#include "src/execution/arm64/frame-constants-arm64.h"
#elif V8_TARGET_ARCH_ARM
#include "src/execution/arm/frame-constants-arm.h"
#elif V8_TARGET_ARCH_PPC64
#include "src/execution/ppc/frame-constants-ppc.h"
#elif V8_TARGET_ARCH_MIPS64
#include "src/execution/mips64/frame-constants-mips64.h"
#elif V8_TARGET_ARCH_LOONG64
#include "src/execution/loong64/frame-constants-loong64.h"
#elif V8_TARGET_ARCH_S390X
#include "src/execution/s390/frame-constants-s390.h"
#elif V8_TARGET_ARCH_RISCV32 || V8_TARGET_ARCH_RISCV64
#include "src/execution/riscv/frame-constants-riscv.h"
#else
#error Unsupported target architecture.
#endif

#endif  // V8_EXECUTION_FRAME_CONSTANTS_H_
