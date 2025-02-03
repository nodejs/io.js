// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_OBJECTS_CALL_SITE_INFO_H_
#define V8_OBJECTS_CALL_SITE_INFO_H_

#include <optional>

#include "src/objects/struct.h"
#include "torque-generated/bit-fields.h"

// Has to be the last include (doesn't have include guards):
#include "src/objects/object-macros.h"

namespace v8::internal {

class MessageLocation;
class WasmInstanceObject;
class StructBodyDescriptor;

#include "torque-generated/src/objects/call-site-info-tq.inc"

class CallSiteInfo : public TorqueGeneratedCallSiteInfo<CallSiteInfo, Struct> {
 public:
  NEVER_READ_ONLY_SPACE
  DEFINE_TORQUE_GENERATED_CALL_SITE_INFO_FLAGS()

#if V8_ENABLE_WEBASSEMBLY
  inline bool IsWasm() const;
  inline bool IsAsmJsWasm() const;
  inline bool IsAsmJsAtNumberConversion() const;
#if V8_ENABLE_DRUMBRAKE
  inline bool IsWasmInterpretedFrame() const;
#endif  // V8_ENABLE_DRUMBRAKE
  inline bool IsBuiltin() const;
#endif  // V8_ENABLE_WEBASSEMBLY

  inline bool IsStrict() const;
  inline bool IsConstructor() const;
  inline bool IsAsync() const;
  bool IsEval() const;
  bool IsUserJavaScript() const;
  bool IsSubjectToDebugging() const;
  bool IsMethodCall() const;
  bool IsToplevel() const;
  bool IsPromiseAll() const;
  bool IsPromiseAllSettled() const;
  bool IsPromiseAny() const;
  bool IsNative() const;

  inline Tagged<HeapObject> code_object(IsolateForSandbox isolate) const;
  inline void set_code_object(Tagged<HeapObject> code, WriteBarrierMode mode);

  // Dispatched behavior.
  DECL_VERIFIER(CallSiteInfo)

  // Used to signal that the requested field is unknown.
  static constexpr int kUnknown = kNoSourcePosition;

  V8_EXPORT_PRIVATE static int GetLineNumber(DirectHandle<CallSiteInfo> info);
  V8_EXPORT_PRIVATE static int GetColumnNumber(DirectHandle<CallSiteInfo> info);

  static int GetEnclosingLineNumber(DirectHandle<CallSiteInfo> info);
  static int GetEnclosingColumnNumber(DirectHandle<CallSiteInfo> info);

  // Returns the script ID if one is attached,
  // Message::kNoScriptIdInfo otherwise.
  static MaybeHandle<Script> GetScript(Isolate* isolate,
                                       DirectHandle<CallSiteInfo> info);
  int GetScriptId() const;
  Tagged<Object> GetScriptName() const;
  Tagged<Object> GetScriptNameOrSourceURL() const;
  Tagged<Object> GetScriptSource() const;
  Tagged<Object> GetScriptSourceMappingURL() const;

  static Handle<PrimitiveHeapObject> GetEvalOrigin(
      DirectHandle<CallSiteInfo> info);
  V8_EXPORT_PRIVATE static Handle<PrimitiveHeapObject> GetFunctionName(
      DirectHandle<CallSiteInfo> info);
  static Handle<String> GetFunctionDebugName(DirectHandle<CallSiteInfo> info);
  static Handle<Object> GetMethodName(DirectHandle<CallSiteInfo> info);
  static Handle<String> GetScriptHash(DirectHandle<CallSiteInfo> info);
  static Handle<Object> GetTypeName(DirectHandle<CallSiteInfo> info);

#if V8_ENABLE_WEBASSEMBLY
  // These methods are only valid for Wasm and asm.js Wasm frames.
  uint32_t GetWasmFunctionIndex() const;
  Tagged<WasmInstanceObject> GetWasmInstance() const;
  static Handle<Object> GetWasmModuleName(DirectHandle<CallSiteInfo> info);
#endif  // V8_ENABLE_WEBASSEMBLY

  // Returns the 0-based source position, which is the offset into the
  // Script in case of JavaScript and Asm.js, and the wire byte offset
  // in the module in case of actual Wasm. In case of async promise
  // combinator frames, this returns the index of the promise.
  static int GetSourcePosition(DirectHandle<CallSiteInfo> info);

  // Attempts to fill the |location| based on the |info|, and avoids
  // triggering source position table building for JavaScript frames.
  static bool ComputeLocation(DirectHandle<CallSiteInfo> info,
                              MessageLocation* location);

  class BodyDescriptor;

 private:
  static int ComputeSourcePosition(DirectHandle<CallSiteInfo> info, int offset);

  std::optional<Tagged<Script>> GetScript() const;
  Tagged<SharedFunctionInfo> GetSharedFunctionInfo() const;

  TQ_OBJECT_CONSTRUCTORS(CallSiteInfo)
};

class IncrementalStringBuilder;
void SerializeCallSiteInfo(Isolate* isolate, DirectHandle<CallSiteInfo> frame,
                           IncrementalStringBuilder* builder);
V8_EXPORT_PRIVATE
MaybeHandle<String> SerializeCallSiteInfo(Isolate* isolate,
                                          DirectHandle<CallSiteInfo> frame);

}  // namespace v8::internal

#include "src/objects/object-macros-undef.h"

#endif  // V8_OBJECTS_CALL_SITE_INFO_H_
