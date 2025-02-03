// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_COMMON_CALL_TESTER_H_
#define V8_COMMON_CALL_TESTER_H_

#include "src/execution/simulator.h"
#include "src/handles/handles.h"
#include "src/objects/code.h"
#include "test/common/c-signature.h"

namespace v8 {
namespace internal {
namespace compiler {

template <typename R>
class CallHelper {
 public:
  explicit CallHelper(Isolate* isolate, MachineSignature* csig)
      : csig_(csig), isolate_(isolate) {
    USE(isolate_);
  }
  virtual ~CallHelper() = default;

  template <typename... Params>
  R Call(Params... args) {
    CSignature::VerifyParams<Params...>(csig_);
    Address entry = Generate();
    auto fn = GeneratedCode<R, Params...>::FromAddress(isolate_, entry);
    return fn.Call(args...);
  }

 protected:
  MachineSignature* csig_;

  virtual Address Generate() = 0;

 private:
  Isolate* isolate_;
};

template <>
template <typename... Params>
Tagged<Object> CallHelper<Tagged<Object>>::Call(Params... args) {
  CSignature::VerifyParams<Params...>(csig_);
  Address entry = Generate();
  auto fn = GeneratedCode<Address, Params...>::FromAddress(isolate_, entry);
  return Tagged<Object>(fn.Call(args...));
}

// A call helper that calls the given code object assuming C calling convention.
template <typename T>
class CodeRunner : public CallHelper<T> {
 public:
  CodeRunner(Isolate* isolate, Handle<InstructionStream> istream,
             MachineSignature* csig)
      : CallHelper<T>(isolate, csig), istream_(istream) {}
  CodeRunner(Isolate* isolate, DirectHandle<Code> code, MachineSignature* csig)
      : CallHelper<T>(isolate, csig),
        istream_(code->instruction_stream(), isolate) {}
  ~CodeRunner() override = default;

  Address Generate() override { return istream_->instruction_start(); }

 private:
  Handle<InstructionStream> istream_;
};

}  // namespace compiler
}  // namespace internal
}  // namespace v8

#endif  // V8_COMMON_CALL_TESTER_H_
