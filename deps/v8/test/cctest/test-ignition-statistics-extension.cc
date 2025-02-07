// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/execution/isolate.h"
#include "src/interpreter/bytecodes.h"
#include "src/interpreter/interpreter.h"
#include "test/cctest/cctest.h"

namespace v8 {
namespace internal {

class IgnitionStatisticsTester {
 public:
  explicit IgnitionStatisticsTester(Isolate* isolate) : isolate_(isolate) {
    // In case the build specified v8_enable_ignition_dispatch_counting, the
    // interpreter already has a dispatch counters table and the bytecode
    // handlers will update it. To avoid crashes, we keep that array alive here.
    // This file doesn't test the results in the real array since there is no
    // automated testing on configurations with
    // v8_enable_ignition_dispatch_counting.
    original_bytecode_dispatch_counters_table_ =
        std::move(isolate->interpreter()->bytecode_dispatch_counters_table_);

    // This sets up the counters array, but does not rewrite the bytecode
    // handlers to update it.
    isolate->interpreter()->InitDispatchCounters();
  }

  void SetDispatchCounter(interpreter::Bytecode from, interpreter::Bytecode to,
                          uintptr_t value) const {
    int from_index = interpreter::Bytecodes::ToByte(from);
    int to_index = interpreter::Bytecodes::ToByte(to);
    isolate_->interpreter()->bytecode_dispatch_counters_table_
        [from_index * interpreter::Bytecodes::kBytecodeCount + to_index] =
        value;
    CHECK_EQ(isolate_->interpreter()->GetDispatchCounter(from, to), value);
  }

 private:
  Isolate* isolate_;
  std::unique_ptr<uintptr_t[]> original_bytecode_dispatch_counters_table_;
};

TEST(IgnitionStatisticsExtension) {
  v8_flags.expose_ignition_statistics = true;
  CcTest::InitializeVM();
  v8::Isolate* isolate = CcTest::isolate();
  v8::HandleScope scope(isolate);
  IgnitionStatisticsTester tester(CcTest::i_isolate());

  Local<Value> typeof_result =
      CompileRun("typeof getIgnitionDispatchCounters === 'function'");
  CHECK(typeof_result->BooleanValue(isolate));

  // Get the list of all bytecode names into a JavaScript array.
#define BYTECODE_NAME_WITH_COMMA(Name, ...) "'" #Name "', "
  const char* kBytecodeNames = "var bytecodeNames = [" BYTECODE_LIST(
      BYTECODE_NAME_WITH_COMMA, BYTECODE_NAME_WITH_COMMA) "];";
#undef BYTECODE_NAME_WITH_COMMA
  CompileRun(kBytecodeNames);

  // Check that the dispatch counters object is a non-empty object of objects
  // where each property name is a bytecode name, in order, and each inner
  // object is empty.
  const char* kEmptyTest = R"(
    var emptyCounters = getIgnitionDispatchCounters();
    function isEmptyDispatchCounters(counters) {
      if (typeof counters !== "object") return false;
      var i = 0;
      for (var sourceBytecode in counters) {
        if (sourceBytecode !== bytecodeNames[i]) return false;
        var countersRow = counters[sourceBytecode];
        if (typeof countersRow !== "object") return false;
        for (var counter in countersRow) {
          return false;
        }
        ++i;
      }
      return true;
    }
    isEmptyDispatchCounters(emptyCounters);)";
  Local<Value> empty_result = CompileRun(kEmptyTest);
  CHECK(empty_result->BooleanValue(isolate));

  // Simulate running some code, which would update the counters.
  tester.SetDispatchCounter(interpreter::Bytecode::kLdar,
                            interpreter::Bytecode::kStar, 3);
  tester.SetDispatchCounter(interpreter::Bytecode::kLdar,
                            interpreter::Bytecode::kLdar, 4);
  tester.SetDispatchCounter(interpreter::Bytecode::kMov,
                            interpreter::Bytecode::kLdar, 5);

  // Check that the dispatch counters object is a non-empty object of objects
  // where each property name is a bytecode name, in order, and the inner
  // objects reflect the new state.
  const char* kNonEmptyTest = R"(
    var nonEmptyCounters = getIgnitionDispatchCounters();
    function isUpdatedDispatchCounters(counters) {
      if (typeof counters !== "object") return false;
      var i = 0;
      for (var sourceBytecode in counters) {
        if (sourceBytecode !== bytecodeNames[i]) return false;
        var countersRow = counters[sourceBytecode];
        if (typeof countersRow !== "object") return false;
        switch (sourceBytecode) {
          case "Ldar":
            if (JSON.stringify(countersRow) !== '{"Ldar":4,"Star":3}')
              return false;
            break;
          case "Mov":
            if (JSON.stringify(countersRow) !== '{"Ldar":5}')
              return false;
            break;
          default:
            for (var counter in countersRow) {
              return false;
            }
        }
        ++i;
      }
      return true;
    }
    isUpdatedDispatchCounters(nonEmptyCounters);)";
  Local<Value> non_empty_result = CompileRun(kNonEmptyTest);
  CHECK(non_empty_result->BooleanValue(isolate));
}

}  // namespace internal
}  // namespace v8
