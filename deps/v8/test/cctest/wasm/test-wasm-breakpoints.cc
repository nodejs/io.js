// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/codegen/assembler-inl.h"
#include "src/debug/debug-interface.h"
#include "src/execution/frames-inl.h"
#include "src/objects/property-descriptor.h"
#include "src/utils/utils.h"
#include "src/wasm/wasm-debug.h"
#include "src/wasm/wasm-objects-inl.h"
#include "test/cctest/cctest.h"
#include "test/cctest/wasm/wasm-run-utils.h"
#include "test/common/value-helper.h"
#include "test/common/wasm/test-signatures.h"
#include "test/common/wasm/wasm-macro-gen.h"

namespace v8 {
namespace internal {
namespace wasm {

namespace {

debug::Location TranslateLocation(WasmRunnerBase* runner,
                                  const debug::Location& loc) {
  // Convert locations from {func_index, offset_in_func} to
  // {0, offset_in_module}.
  int func_index = loc.GetLineNumber();
  int func_offset = runner->builder().GetFunctionAt(func_index)->code.offset();
  int offset = loc.GetColumnNumber() + func_offset;
  return {0, offset};
}

void CheckLocations(
    WasmRunnerBase* runner, NativeModule* native_module, debug::Location start,
    debug::Location end,
    std::initializer_list<debug::Location> expected_locations_init) {
  std::vector<debug::BreakLocation> locations;
  std::vector<debug::Location> expected_locations;
  for (auto loc : expected_locations_init) {
    expected_locations.push_back(TranslateLocation(runner, loc));
  }

  bool success = WasmScript::GetPossibleBreakpoints(
      native_module, TranslateLocation(runner, start),
      TranslateLocation(runner, end), &locations);
  CHECK(success);

  printf("got %d locations: ", static_cast<int>(locations.size()));
  for (size_t i = 0, e = locations.size(); i != e; ++i) {
    printf("%s<%d,%d>", i == 0 ? "" : ", ", locations[i].GetLineNumber(),
           locations[i].GetColumnNumber());
  }
  printf("\n");

  CHECK_EQ(expected_locations.size(), locations.size());
  for (size_t i = 0, e = locations.size(); i != e; ++i) {
    CHECK_EQ(expected_locations[i].GetLineNumber(),
             locations[i].GetLineNumber());
    CHECK_EQ(expected_locations[i].GetColumnNumber(),
             locations[i].GetColumnNumber());
  }
}

void CheckLocationsFail(WasmRunnerBase* runner, NativeModule* native_module,
                        debug::Location start, debug::Location end) {
  std::vector<debug::BreakLocation> locations;
  bool success = WasmScript::GetPossibleBreakpoints(
      native_module, TranslateLocation(runner, start),
      TranslateLocation(runner, end), &locations);
  CHECK(!success);
}

class BreakHandler : public debug::DebugDelegate {
 public:
  enum Action {
    Continue = StepAction::LastStepAction + 1,
    StepOver = StepAction::StepOver,
    StepInto = StepAction::StepInto,
    StepOut = StepAction::StepOut
  };
  struct BreakPoint {
    int position;
    Action action;
    std::function<void(void)> pre_action;
    BreakPoint(int position, Action action)
        : position(position), action(action), pre_action([]() {}) {}
    BreakPoint(int position, Action action,
               std::function<void(void)> pre_action)
        : position(position), action(action), pre_action(pre_action) {}
  };

  explicit BreakHandler(Isolate* isolate,
                        std::initializer_list<BreakPoint> expected_breaks)
      : isolate_(isolate), expected_breaks_(expected_breaks) {
    v8::debug::SetDebugDelegate(reinterpret_cast<v8::Isolate*>(isolate_), this);
  }
  ~BreakHandler() override {
    // Check that all expected breakpoints have been hit.
    CHECK_EQ(count_, expected_breaks_.size());
    v8::debug::SetDebugDelegate(reinterpret_cast<v8::Isolate*>(isolate_),
                                nullptr);
  }

  int count() const { return count_; }

 private:
  Isolate* isolate_;
  int count_ = 0;
  std::vector<BreakPoint> expected_breaks_;

  void BreakProgramRequested(v8::Local<v8::Context> paused_context,
                             const std::vector<int>&,
                             v8::debug::BreakReasons break_reasons) override {
    printf("Break #%d\n", count_);
    CHECK_GT(expected_breaks_.size(), count_);

    // Check the current position.
    DebuggableStackFrameIterator frame_it(isolate_);
    auto summ = FrameSummary::GetTop(frame_it.frame()).AsWasm();
    CHECK_EQ(expected_breaks_[count_].position, summ.code_offset());

    expected_breaks_[count_].pre_action();
    Action next_action = expected_breaks_[count_].action;
    switch (next_action) {
      case Continue:
        break;
      case StepOver:
      case StepInto:
      case StepOut:
        isolate_->debug()->PrepareStep(static_cast<StepAction>(next_action));
        break;
      default:
        UNREACHABLE();
    }
    ++count_;
  }
};

Handle<BreakPoint> SetBreakpoint(WasmRunnerBase* runner, int function_index,
                                 int byte_offset,
                                 int expected_set_byte_offset = -1) {
  runner->SwitchToDebug();
  int func_offset =
      runner->builder().GetFunctionAt(function_index)->code.offset();
  int code_offset = func_offset + byte_offset;
  if (expected_set_byte_offset == -1) expected_set_byte_offset = byte_offset;
  DirectHandle<WasmInstanceObject> instance =
      runner->builder().instance_object();
  DirectHandle<Script> script(instance->module_object()->script(),
                              runner->main_isolate());
  static int break_index = 0;
  Handle<BreakPoint> break_point =
      runner->main_isolate()->factory()->NewBreakPoint(
          break_index++, runner->main_isolate()->factory()->empty_string());
  CHECK(WasmScript::SetBreakPoint(script, &code_offset, break_point));
  return break_point;
}

void ClearBreakpoint(WasmRunnerBase* runner, int function_index,
                     int byte_offset, DirectHandle<BreakPoint> break_point) {
  int func_offset =
      runner->builder().GetFunctionAt(function_index)->code.offset();
  int code_offset = func_offset + byte_offset;
  DirectHandle<WasmInstanceObject> instance =
      runner->builder().instance_object();
  DirectHandle<Script> script(instance->module_object()->script(),
                              runner->main_isolate());
  CHECK(WasmScript::ClearBreakPoint(script, code_offset, break_point));
}

// Wrapper with operator<<.
struct WasmValWrapper {
  WasmValue val;

  bool operator==(const WasmValWrapper& other) const {
    return val == other.val;
  }
};

// Only needed in debug builds. Avoid unused warning otherwise.
#ifdef DEBUG
std::ostream& operator<<(std::ostream& out, const WasmValWrapper& wrapper) {
  switch (wrapper.val.type().kind()) {
    case kI32:
      out << "i32: " << wrapper.val.to<int32_t>();
      break;
    case kI64:
      out << "i64: " << wrapper.val.to<int64_t>();
      break;
    case kF32:
      out << "f32: " << wrapper.val.to<float>();
      break;
    case kF64:
      out << "f64: " << wrapper.val.to<double>();
      break;
    default:
      UNIMPLEMENTED();
  }
  return out;
}
#endif

class CollectValuesBreakHandler : public debug::DebugDelegate {
 public:
  struct BreakpointValues {
    std::vector<WasmValue> locals;
    std::vector<WasmValue> stack;
  };

  explicit CollectValuesBreakHandler(
      Isolate* isolate, std::initializer_list<BreakpointValues> expected_values)
      : isolate_(isolate), expected_values_(expected_values) {
    v8::debug::SetDebugDelegate(reinterpret_cast<v8::Isolate*>(isolate_), this);
  }
  ~CollectValuesBreakHandler() override {
    v8::debug::SetDebugDelegate(reinterpret_cast<v8::Isolate*>(isolate_),
                                nullptr);
  }

 private:
  Isolate* isolate_;
  int count_ = 0;
  std::vector<BreakpointValues> expected_values_;

  void BreakProgramRequested(v8::Local<v8::Context> paused_context,
                             const std::vector<int>&,
                             v8::debug::BreakReasons break_reasons) override {
    printf("Break #%d\n", count_);
    CHECK_GT(expected_values_.size(), count_);
    auto& expected = expected_values_[count_];
    ++count_;

    HandleScope handles(isolate_);

    DebuggableStackFrameIterator frame_it(isolate_);
    WasmFrame* frame = WasmFrame::cast(frame_it.frame());
    DebugInfo* debug_info = frame->native_module()->GetDebugInfo();

    int num_locals = debug_info->GetNumLocals(frame->pc(), isolate_);
    CHECK_EQ(expected.locals.size(), num_locals);
    for (int i = 0; i < num_locals; ++i) {
      WasmValue local_value = debug_info->GetLocalValue(
          i, frame->pc(), frame->fp(), frame->callee_fp(), isolate_);
      CHECK_EQ(WasmValWrapper{expected.locals[i]}, WasmValWrapper{local_value});
    }

    int stack_depth = debug_info->GetStackDepth(frame->pc(), isolate_);
    CHECK_EQ(expected.stack.size(), stack_depth);
    for (int i = 0; i < stack_depth; ++i) {
      WasmValue stack_value = debug_info->GetStackValue(
          i, frame->pc(), frame->fp(), frame->callee_fp(), isolate_);
      CHECK_EQ(WasmValWrapper{expected.stack[i]}, WasmValWrapper{stack_value});
    }

    isolate_->debug()->PrepareStep(StepAction::StepInto);
  }
};

// Special template to explicitly cast to WasmValue.
template <typename Arg>
WasmValue MakeWasmVal(Arg arg) {
  return WasmValue(arg);
}
// Translate long to i64 (ambiguous otherwise).
template <>
WasmValue MakeWasmVal(long arg) {  // NOLINT: allow long parameter
  return WasmValue(static_cast<int64_t>(arg));
}

template <typename... Args>
std::vector<WasmValue> wasmVec(Args... args) {
  std::array<WasmValue, sizeof...(args)> arr{{MakeWasmVal(args)...}};
  return std::vector<WasmValue>{arr.begin(), arr.end()};
}

int GetIntReturnValue(MaybeDirectHandle<Object> retval) {
  CHECK(!retval.is_null());
  int result;
  CHECK(Object::ToInt32(*retval.ToHandleChecked(), &result));
  return result;
}

}  // namespace

WASM_COMPILED_EXEC_TEST(WasmCollectPossibleBreakpoints) {
  WasmRunner<int> runner(execution_tier);

  runner.Build({WASM_NOP, WASM_I32_ADD(WASM_ZERO, WASM_ONE)});

  Tagged<WasmInstanceObject> instance = *runner.builder().instance_object();
  NativeModule* native_module = instance->module_object()->native_module();

  std::vector<debug::Location> locations;
  // Check all locations for function 0.
  CheckLocations(&runner, native_module, {0, 0}, {0, 10},
                 {{0, 1}, {0, 2}, {0, 4}, {0, 6}, {0, 7}});
  // Check a range ending at an instruction.
  CheckLocations(&runner, native_module, {0, 2}, {0, 4}, {{0, 2}});
  // Check a range ending one behind an instruction.
  CheckLocations(&runner, native_module, {0, 2}, {0, 5}, {{0, 2}, {0, 4}});
  // Check a range starting at an instruction.
  CheckLocations(&runner, native_module, {0, 7}, {0, 8}, {{0, 7}});
  // Check from an instruction to beginning of next function.
  CheckLocations(&runner, native_module, {0, 7}, {0, 10}, {{0, 7}});
  // Check from end of one function (no valid instruction position) to beginning
  // of next function. Must be empty, but not fail.
  CheckLocations(&runner, native_module, {0, 8}, {0, 10}, {});
  // Check from one after the end of the function. Must fail.
  CheckLocationsFail(&runner, native_module, {0, 9}, {0, 10});
}

WASM_COMPILED_EXEC_TEST(WasmSimpleBreak) {
  WasmRunner<int> runner(execution_tier);
  Isolate* isolate = runner.main_isolate();

  runner.Build({WASM_NOP, WASM_I32_ADD(WASM_I32V_1(11), WASM_I32V_1(3))});

  DirectHandle<JSFunction> main_fun_wrapper =
      runner.builder().WrapCode(runner.function_index());
  SetBreakpoint(&runner, runner.function_index(), 4, 4);

  BreakHandler count_breaks(isolate, {{4, BreakHandler::Continue}});

  DirectHandle<Object> global(isolate->context()->global_object(), isolate);
  MaybeDirectHandle<Object> retval =
      Execution::Call(isolate, main_fun_wrapper, global, {});
  CHECK_EQ(14, GetIntReturnValue(retval));
}

WASM_COMPILED_EXEC_TEST(WasmNonBreakablePosition) {
  WasmRunner<int> runner(execution_tier);
  Isolate* isolate = runner.main_isolate();

  runner.Build({WASM_RETURN(WASM_I32V_2(1024))});

  DirectHandle<JSFunction> main_fun_wrapper =
      runner.builder().WrapCode(runner.function_index());
  SetBreakpoint(&runner, runner.function_index(), 2, 4);

  BreakHandler count_breaks(isolate, {{4, BreakHandler::Continue}});

  DirectHandle<Object> global(isolate->context()->global_object(), isolate);
  MaybeDirectHandle<Object> retval =
      Execution::Call(isolate, main_fun_wrapper, global, {});
  CHECK_EQ(1024, GetIntReturnValue(retval));
}

WASM_COMPILED_EXEC_TEST(WasmSimpleStepping) {
  WasmRunner<int> runner(execution_tier);
  runner.Build({WASM_I32_ADD(WASM_I32V_1(11), WASM_I32V_1(3))});

  Isolate* isolate = runner.main_isolate();
  DirectHandle<JSFunction> main_fun_wrapper =
      runner.builder().WrapCode(runner.function_index());

  // Set breakpoint at the first I32Const.
  SetBreakpoint(&runner, runner.function_index(), 1, 1);

  BreakHandler count_breaks(isolate,
                            {
                                {1, BreakHandler::StepOver},  // I32Const
                                {3, BreakHandler::StepOver},  // I32Const
                                {5, BreakHandler::Continue}   // I32Add
                            });

  DirectHandle<Object> global(isolate->context()->global_object(), isolate);
  MaybeDirectHandle<Object> retval =
      Execution::Call(isolate, main_fun_wrapper, global, {});
  CHECK_EQ(14, GetIntReturnValue(retval));
}

WASM_COMPILED_EXEC_TEST(WasmStepInAndOut) {
  WasmRunner<int, int> runner(execution_tier);
  runner.SwitchToDebug();
  WasmFunctionCompiler& f2 = runner.NewFunction<void>();
  f2.AllocateLocal(kWasmI32);

  // Call f2 via indirect call, because a direct call requires f2 to exist when
  // we compile main, but we need to compile main first so that the order of
  // functions in the code section matches the function indexes.

  // return arg0
  runner.Build({WASM_RETURN(WASM_LOCAL_GET(0))});
  // for (int i = 0; i < 10; ++i) { f2(i); }
  f2.Build({WASM_LOOP(
      WASM_BR_IF(0,
                 WASM_BINOP(kExprI32GeU, WASM_LOCAL_GET(0), WASM_I32V_1(10))),
      WASM_LOCAL_SET(0, WASM_BINOP(kExprI32Sub, WASM_LOCAL_GET(0), WASM_ONE)),
      WASM_CALL_FUNCTION(runner.function_index(), WASM_LOCAL_GET(0)), WASM_DROP,
      WASM_BR(1))});

  Isolate* isolate = runner.main_isolate();
  DirectHandle<JSFunction> main_fun_wrapper =
      runner.builder().WrapCode(f2.function_index());

  // Set first breakpoint on the LocalGet (offset 19) before the Call.
  SetBreakpoint(&runner, f2.function_index(), 19, 19);

  BreakHandler count_breaks(isolate,
                            {
                                {19, BreakHandler::StepInto},  // LocalGet
                                {21, BreakHandler::StepInto},  // Call
                                {1, BreakHandler::StepOut},    // in f2
                                {23, BreakHandler::Continue}   // After Call
                            });

  DirectHandle<Object> global(isolate->context()->global_object(), isolate);
  CHECK(!Execution::Call(isolate, main_fun_wrapper, global, {}).is_null());
}

WASM_COMPILED_EXEC_TEST(WasmGetLocalsAndStack) {
  WasmRunner<void, int> runner(execution_tier);
  runner.AllocateLocal(kWasmI64);
  runner.AllocateLocal(kWasmF32);
  runner.AllocateLocal(kWasmF64);

  runner.Build(
      {// set [1] to 17
       WASM_LOCAL_SET(1, WASM_I64V_1(17)),
       // set [2] to <arg0> = 7
       WASM_LOCAL_SET(2, WASM_F32_SCONVERT_I32(WASM_LOCAL_GET(0))),
       // set [3] to <arg1>/2 = 8.5
       WASM_LOCAL_SET(3, WASM_F64_DIV(WASM_F64_SCONVERT_I64(WASM_LOCAL_GET(1)),
                                      WASM_F64(2)))});

  Isolate* isolate = runner.main_isolate();
  DirectHandle<JSFunction> main_fun_wrapper =
      runner.builder().WrapCode(runner.function_index());

  // Set breakpoint at the first instruction (7 bytes for local decls: num
  // entries + 3x<count, type>).
  SetBreakpoint(&runner, runner.function_index(), 7, 7);

  CollectValuesBreakHandler break_handler(
      isolate,
      {
          // params + locals          stack
          {wasmVec(7, 0L, 0.f, 0.), wasmVec()},          // 0: i64.const[17]
          {wasmVec(7, 0L, 0.f, 0.), wasmVec(17L)},       // 1: set_local[1]
          {wasmVec(7, 17L, 0.f, 0.), wasmVec()},         // 2: get_local[0]
          {wasmVec(7, 17L, 0.f, 0.), wasmVec(7)},        // 3: f32.convert_s
          {wasmVec(7, 17L, 0.f, 0.), wasmVec(7.f)},      // 4: set_local[2]
          {wasmVec(7, 17L, 7.f, 0.), wasmVec()},         // 5: get_local[1]
          {wasmVec(7, 17L, 7.f, 0.), wasmVec(17L)},      // 6: f64.convert_s
          {wasmVec(7, 17L, 7.f, 0.), wasmVec(17.)},      // 7: f64.const[2]
          {wasmVec(7, 17L, 7.f, 0.), wasmVec(17., 2.)},  // 8: f64.div
          {wasmVec(7, 17L, 7.f, 0.), wasmVec(8.5)},      // 9: set_local[3]
          {wasmVec(7, 17L, 7.f, 8.5), wasmVec()},        // 10: end
      });

  DirectHandle<Object> global(isolate->context()->global_object(), isolate);
  DirectHandle<Object> args[]{direct_handle(Smi::FromInt(7), isolate)};
  CHECK(
      !Execution::Call(isolate, main_fun_wrapper, global, base::VectorOf(args))
           .is_null());
}

WASM_COMPILED_EXEC_TEST(WasmRemoveBreakPoint) {
  WasmRunner<int> runner(execution_tier);
  Isolate* isolate = runner.main_isolate();

  runner.Build(
      {WASM_NOP, WASM_NOP, WASM_NOP, WASM_NOP, WASM_NOP, WASM_I32V_1(14)});

  DirectHandle<JSFunction> main_fun_wrapper =
      runner.builder().WrapCode(runner.function_index());

  SetBreakpoint(&runner, runner.function_index(), 1, 1);
  SetBreakpoint(&runner, runner.function_index(), 2, 2);
  Handle<BreakPoint> to_delete =
      SetBreakpoint(&runner, runner.function_index(), 3, 3);
  SetBreakpoint(&runner, runner.function_index(), 4, 4);

  BreakHandler count_breaks(isolate, {{1, BreakHandler::Continue},
                                      {2, BreakHandler::Continue,
                                       [&runner, &to_delete]() {
                                         ClearBreakpoint(
                                             &runner, runner.function_index(),
                                             3, to_delete);
                                       }},
                                      {4, BreakHandler::Continue}});

  DirectHandle<Object> global(isolate->context()->global_object(), isolate);
  MaybeDirectHandle<Object> retval =
      Execution::Call(isolate, main_fun_wrapper, global, {});
  CHECK_EQ(14, GetIntReturnValue(retval));
}

WASM_COMPILED_EXEC_TEST(WasmRemoveLastBreakPoint) {
  WasmRunner<int> runner(execution_tier);
  Isolate* isolate = runner.main_isolate();

  runner.Build(
      {WASM_NOP, WASM_NOP, WASM_NOP, WASM_NOP, WASM_NOP, WASM_I32V_1(14)});

  DirectHandle<JSFunction> main_fun_wrapper =
      runner.builder().WrapCode(runner.function_index());

  SetBreakpoint(&runner, runner.function_index(), 1, 1);
  SetBreakpoint(&runner, runner.function_index(), 2, 2);
  Handle<BreakPoint> to_delete =
      SetBreakpoint(&runner, runner.function_index(), 3, 3);

  BreakHandler count_breaks(
      isolate, {{1, BreakHandler::Continue},
                {2, BreakHandler::Continue, [&runner, &to_delete]() {
                   ClearBreakpoint(&runner, runner.function_index(), 3,
                                   to_delete);
                 }}});

  DirectHandle<Object> global(isolate->context()->global_object(), isolate);
  MaybeDirectHandle<Object> retval =
      Execution::Call(isolate, main_fun_wrapper, global, {});
  CHECK_EQ(14, GetIntReturnValue(retval));
}

WASM_COMPILED_EXEC_TEST(WasmRemoveAllBreakPoint) {
  WasmRunner<int> runner(execution_tier);
  Isolate* isolate = runner.main_isolate();

  runner.Build(
      {WASM_NOP, WASM_NOP, WASM_NOP, WASM_NOP, WASM_NOP, WASM_I32V_1(14)});

  DirectHandle<JSFunction> main_fun_wrapper =
      runner.builder().WrapCode(runner.function_index());

  Handle<BreakPoint> bp1 =
      SetBreakpoint(&runner, runner.function_index(), 1, 1);
  Handle<BreakPoint> bp2 =
      SetBreakpoint(&runner, runner.function_index(), 2, 2);
  Handle<BreakPoint> bp3 =
      SetBreakpoint(&runner, runner.function_index(), 3, 3);

  BreakHandler count_breaks(
      isolate, {{1, BreakHandler::Continue, [&runner, &bp1, &bp2, &bp3]() {
                   ClearBreakpoint(&runner, runner.function_index(), 1, bp1);
                   ClearBreakpoint(&runner, runner.function_index(), 3, bp3);
                   ClearBreakpoint(&runner, runner.function_index(), 2, bp2);
                 }}});

  DirectHandle<Object> global(isolate->context()->global_object(), isolate);
  MaybeDirectHandle<Object> retval =
      Execution::Call(isolate, main_fun_wrapper, global, {});
  CHECK_EQ(14, GetIntReturnValue(retval));
}

WASM_COMPILED_EXEC_TEST(WasmBreakInPostMVP) {
  // This test checks that we don't fail if experimental / post-MVP opcodes are
  // being used. There was a bug where we were trying to update the "detected"
  // features set, but we were passing a nullptr when compiling with
  // breakpoints.
  WasmRunner<int> runner(execution_tier);
  Isolate* isolate = runner.main_isolate();

  // [] -> [i32, i32]
  ValueType sig_types[] = {kWasmI32, kWasmI32};
  FunctionSig sig{2, 0, sig_types};
  ModuleTypeIndex sig_idx = runner.builder().AddSignature(&sig);

  constexpr int kReturn = 13;
  constexpr int kIgnored = 23;
  runner.Build(
      {WASM_BLOCK_X(sig_idx, WASM_I32V_1(kReturn), WASM_I32V_1(kIgnored)),
       WASM_DROP});

  DirectHandle<JSFunction> main_fun_wrapper =
      runner.builder().WrapCode(runner.function_index());

  SetBreakpoint(&runner, runner.function_index(), 3, 3);

  BreakHandler count_breaks(isolate, {{3, BreakHandler::Continue}});

  DirectHandle<Object> global(isolate->context()->global_object(), isolate);
  MaybeDirectHandle<Object> retval =
      Execution::Call(isolate, main_fun_wrapper, global, {});
  CHECK_EQ(kReturn, GetIntReturnValue(retval));
}

WASM_COMPILED_EXEC_TEST(Regress10889) {
  FLAG_SCOPE(print_wasm_code);
  WasmRunner<int> runner(execution_tier);
  runner.Build({WASM_I32V_1(0)});
  SetBreakpoint(&runner, runner.function_index(), 1, 1);
}

}  // namespace wasm
}  // namespace internal
}  // namespace v8
