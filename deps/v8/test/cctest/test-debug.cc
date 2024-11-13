// Copyright 2012 the V8 project authors. All rights reserved.
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
//     * Redistributions of source code must retain the above copyright
//       notice, this list of conditions and the following disclaimer.
//     * Redistributions in binary form must reproduce the above
//       copyright notice, this list of conditions and the following
//       disclaimer in the documentation and/or other materials provided
//       with the distribution.
//     * Neither the name of Google Inc. nor the names of its
//       contributors may be used to endorse or promote products derived
//       from this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

#include <stdlib.h>

#include "include/v8-extension.h"
#include "include/v8-function.h"
#include "include/v8-json.h"
#include "include/v8-locker.h"
#include "src/api/api-inl.h"
#include "src/base/strings.h"
#include "src/codegen/compilation-cache.h"
#include "src/debug/debug-interface.h"
#include "src/debug/debug-scopes.h"
#include "src/debug/debug.h"
#include "src/deoptimizer/deoptimizer.h"
#include "src/execution/frames-inl.h"
#include "src/execution/microtask-queue.h"
#include "src/objects/objects-inl.h"
#include "src/utils/utils.h"
#include "test/cctest/cctest.h"
#include "test/cctest/heap/heap-utils.h"

using ::v8::internal::DirectHandle;
using ::v8::internal::Handle;
using ::v8::internal::StepInto;  // From StepAction enum
using ::v8::internal::StepNone;  // From StepAction enum
using ::v8::internal::StepOut;   // From StepAction enum
using ::v8::internal::StepOver;  // From StepAction enum

// --- H e l p e r   F u n c t i o n s

// Compile and run the supplied source and return the requested function.
static v8::Local<v8::Function> CompileFunction(v8::Isolate* isolate,
                                               const char* source,
                                               const char* function_name) {
  CompileRunChecked(isolate, source);
  v8::Local<v8::String> name = v8_str(isolate, function_name);
  v8::Local<v8::Context> context = isolate->GetCurrentContext();
  v8::MaybeLocal<v8::Value> maybe_function =
      context->Global()->Get(context, name);
  return v8::Local<v8::Function>::Cast(maybe_function.ToLocalChecked());
}


// Compile and run the supplied source and return the requested function.
static v8::Local<v8::Function> CompileFunction(LocalContext* env,
                                               const char* source,
                                               const char* function_name) {
  return CompileFunction((*env)->GetIsolate(), source, function_name);
}

// Is there any debug info for the function?
static bool HasBreakInfo(v8::Local<v8::Function> fun) {
  DirectHandle<v8::internal::JSFunction> f =
      Cast<v8::internal::JSFunction>(v8::Utils::OpenDirectHandle(*fun));
  return f->shared()->HasBreakInfo(f->GetIsolate());
}

// Set a break point in a function with a position relative to function start,
// and return the associated break point number.
static i::Handle<i::BreakPoint> SetBreakPoint(v8::Local<v8::Function> fun,
                                              int position,
                                              const char* condition = nullptr) {
  i::DirectHandle<i::JSFunction> function =
      i::Cast<i::JSFunction>(v8::Utils::OpenDirectHandle(*fun));
  position += function->shared()->StartPosition();
  static int break_point_index = 0;
  i::Isolate* isolate = function->GetIsolate();
  i::DirectHandle<i::String> condition_string =
      condition ? isolate->factory()->NewStringFromAsciiChecked(condition)
                : isolate->factory()->empty_string();
  i::Debug* debug = isolate->debug();
  i::Handle<i::BreakPoint> break_point =
      isolate->factory()->NewBreakPoint(++break_point_index, condition_string);

  debug->SetBreakpoint(handle(function->shared(), isolate), break_point,
                       &position);
  return break_point;
}

static void ClearBreakPoint(i::DirectHandle<i::BreakPoint> break_point) {
  v8::internal::Isolate* isolate = CcTest::i_isolate();
  v8::internal::Debug* debug = isolate->debug();
  debug->ClearBreakPoint(break_point);
}

// Change break on exception.
static void ChangeBreakOnException(v8::Isolate* isolate, bool caught,
                                   bool uncaught) {
  v8::internal::Debug* debug =
      reinterpret_cast<v8::internal::Isolate*>(isolate)->debug();
  debug->ChangeBreakOnException(v8::internal::BreakCaughtException, caught);
  debug->ChangeBreakOnException(v8::internal::BreakUncaughtException, uncaught);
}

// Prepare to step to next break location.
static void PrepareStep(i::StepAction step_action) {
  v8::internal::Debug* debug = CcTest::i_isolate()->debug();
  debug->PrepareStep(step_action);
}

// This function is in namespace v8::internal to be friend with class
// v8::internal::Debug.
namespace v8 {
namespace internal {

Handle<FixedArray> GetDebuggedFunctions() {
  i::Isolate* isolate = CcTest::i_isolate();
  DebugInfoCollection* infos = &isolate->debug()->debug_infos_;

  int count = static_cast<int>(infos->Size());
  Handle<FixedArray> debugged_functions =
      CcTest::i_isolate()->factory()->NewFixedArray(count);

  int i = 0;
  DebugInfoCollection::Iterator it(infos);
  for (; it.HasNext(); it.Advance()) {
    DirectHandle<DebugInfo> debug_info(it.Next(), isolate);
    debugged_functions->set(i++, *debug_info);
  }

  return debugged_functions;
}

// Check that the debugger has been fully unloaded.
void CheckDebuggerUnloaded() {
  // Check that the debugger context is cleared and that there is no debug
  // information stored for the debugger.
  CHECK_EQ(CcTest::i_isolate()->debug()->debug_infos_.Size(), 0);

  // Collect garbage to ensure weak handles are cleared.
  {
    // We need to invoke GC without stack, otherwise some objects may not be
    // reclaimed because of conservative stack scanning.
    i::DisableConservativeStackScanningScopeForTesting no_stack_scanning(
        CcTest::heap());
    heap::InvokeMajorGC(CcTest::heap());
    heap::InvokeMajorGC(CcTest::heap());
  }

  // Iterate the heap and check that there are no debugger related objects left.
  HeapObjectIterator iterator(CcTest::heap());
  for (Tagged<HeapObject> obj = iterator.Next(); !obj.is_null();
       obj = iterator.Next()) {
    CHECK(!IsDebugInfo(obj));
  }
}


}  // namespace internal
}  // namespace v8


// Check that the debugger has been fully unloaded.
static void CheckDebuggerUnloaded() { v8::internal::CheckDebuggerUnloaded(); }

// --- D e b u g   E v e n t   H a n d l e r s
// ---
// --- The different tests uses a number of debug event handlers.
// ---

// Debug event handler which counts a number of events.
int break_point_hit_count = 0;
int break_point_hit_count_deoptimize = 0;
class DebugEventCounter : public v8::debug::DebugDelegate {
 public:
  void BreakProgramRequested(v8::Local<v8::Context>,
                             const std::vector<v8::debug::BreakpointId>&,
                             v8::debug::BreakReasons break_reasons) override {
    break_point_hit_count++;
    // Perform a full deoptimization when the specified number of
    // breaks have been hit.
    if (break_point_hit_count == break_point_hit_count_deoptimize) {
      i::Deoptimizer::DeoptimizeAll(CcTest::i_isolate());
    }
    if (step_action_ != StepNone) PrepareStep(step_action_);
  }

  void set_step_action(i::StepAction step_action) {
    step_action_ = step_action;
  }

 private:
  i::StepAction step_action_ = StepNone;
};

// Debug event handler which performs a garbage collection.
class DebugEventBreakPointCollectGarbage : public v8::debug::DebugDelegate {
 public:
  void BreakProgramRequested(v8::Local<v8::Context>,
                             const std::vector<v8::debug::BreakpointId>&,
                             v8::debug::BreakReasons break_reasons) override {
    // Perform a garbage collection when break point is hit and continue. Based
    // on the number of break points hit either scavenge or mark compact
    // collector is used.
    break_point_hit_count++;
    if (break_point_hit_count % 2 == 0) {
      // Scavenge.
      i::heap::InvokeMinorGC(CcTest::heap());
    } else {
      // Mark sweep compact.
      i::heap::InvokeMajorGC(CcTest::heap());
    }
  }
};

// Debug event handler which re-issues a debug break and calls the garbage
// collector to have the heap verified.
class DebugEventBreak : public v8::debug::DebugDelegate {
 public:
  void BreakProgramRequested(v8::Local<v8::Context>,
                             const std::vector<v8::debug::BreakpointId>&,
                             v8::debug::BreakReasons break_reasons) override {
    // Count the number of breaks.
    break_point_hit_count++;

    // Run the garbage collector to enforce heap verification if option
    // --verify-heap is set.
    i::heap::InvokeMinorGC(CcTest::heap());

    // Set the break flag again to come back here as soon as possible.
    v8::debug::SetBreakOnNextFunctionCall(CcTest::isolate());
  }
};

v8::debug::BreakReasons break_right_now_reasons = {};
static void BreakRightNow(v8::Isolate* isolate, void*) {
  v8::debug::BreakRightNow(isolate, break_right_now_reasons);
}

// Debug event handler which re-issues a debug break until a limit has been
// reached.
int max_break_point_hit_count = 0;
bool terminate_after_max_break_point_hit = false;
class DebugEventBreakMax : public v8::debug::DebugDelegate {
 public:
  void BreakProgramRequested(v8::Local<v8::Context>,
                             const std::vector<v8::debug::BreakpointId>&,
                             v8::debug::BreakReasons break_reasons) override {
    v8::Isolate* v8_isolate = CcTest::isolate();
    v8::internal::Isolate* isolate = CcTest::i_isolate();
    if (break_point_hit_count < max_break_point_hit_count) {
      // Count the number of breaks.
      break_point_hit_count++;

      // Set the break flag again to come back here as soon as possible.
      v8_isolate->RequestInterrupt(BreakRightNow, nullptr);

    } else if (terminate_after_max_break_point_hit) {
      // Terminate execution after the last break if requested.
      v8_isolate->TerminateExecution();
    }

    // Perform a full deoptimization when the specified number of
    // breaks have been hit.
    if (break_point_hit_count == break_point_hit_count_deoptimize) {
      i::Deoptimizer::DeoptimizeAll(isolate);
    }
  }
};

// --- T h e   A c t u a l   T e s t s

// Test that the debug info in the VM is in sync with the functions being
// debugged.
TEST(DebugInfo) {
  LocalContext env;
  v8::HandleScope scope(env->GetIsolate());
  // Create a couple of functions for the test.
  v8::Local<v8::Function> foo =
      CompileFunction(&env, "function foo(){}", "foo");
  v8::Local<v8::Function> bar =
      CompileFunction(&env, "function bar(){}", "bar");
  // Initially no functions are debugged.
  CHECK_EQ(0, v8::internal::GetDebuggedFunctions()->length());
  CHECK(!HasBreakInfo(foo));
  CHECK(!HasBreakInfo(bar));
  EnableDebugger(env->GetIsolate());
  // One function (foo) is debugged.
  i::DirectHandle<i::BreakPoint> bp1 = SetBreakPoint(foo, 0);
  CHECK_EQ(1, v8::internal::GetDebuggedFunctions()->length());
  CHECK(HasBreakInfo(foo));
  CHECK(!HasBreakInfo(bar));
  // Two functions are debugged.
  i::DirectHandle<i::BreakPoint> bp2 = SetBreakPoint(bar, 0);
  CHECK_EQ(2, v8::internal::GetDebuggedFunctions()->length());
  CHECK(HasBreakInfo(foo));
  CHECK(HasBreakInfo(bar));
  // One function (bar) is debugged.
  ClearBreakPoint(bp1);
  CHECK_EQ(1, v8::internal::GetDebuggedFunctions()->length());
  CHECK(!HasBreakInfo(foo));
  CHECK(HasBreakInfo(bar));
  // No functions are debugged.
  ClearBreakPoint(bp2);
  DisableDebugger(env->GetIsolate());
  CHECK_EQ(0, v8::internal::GetDebuggedFunctions()->length());
  CHECK(!HasBreakInfo(foo));
  CHECK(!HasBreakInfo(bar));
}


// Test that a break point can be set at an IC store location.
TEST(BreakPointICStore) {
  break_point_hit_count = 0;
  LocalContext env;
  v8::HandleScope scope(env->GetIsolate());

  DebugEventCounter delegate;
  v8::debug::SetDebugDelegate(env->GetIsolate(), &delegate);
  v8::Local<v8::Function> foo =
      CompileFunction(&env, "function foo(){bar=0;}", "foo");

  // Run without breakpoints.
  foo->Call(env.local(), env->Global(), 0, nullptr).ToLocalChecked();
  CHECK_EQ(0, break_point_hit_count);

  // Run with breakpoint
  i::DirectHandle<i::BreakPoint> bp = SetBreakPoint(foo, 0);
  foo->Call(env.local(), env->Global(), 0, nullptr).ToLocalChecked();
  CHECK_EQ(1, break_point_hit_count);
  foo->Call(env.local(), env->Global(), 0, nullptr).ToLocalChecked();
  CHECK_EQ(2, break_point_hit_count);

  // Run without breakpoints.
  ClearBreakPoint(bp);
  foo->Call(env.local(), env->Global(), 0, nullptr).ToLocalChecked();
  CHECK_EQ(2, break_point_hit_count);

  v8::debug::SetDebugDelegate(env->GetIsolate(), nullptr);
  CheckDebuggerUnloaded();
}

// Test that a break point can be set at an IC store location.
TEST(BreakPointCondition) {
  break_point_hit_count = 0;
  LocalContext env;
  v8::HandleScope scope(env->GetIsolate());

  DebugEventCounter delegate;
  v8::debug::SetDebugDelegate(env->GetIsolate(), &delegate);
  CompileRun("var a = false");
  v8::Local<v8::Function> foo =
      CompileFunction(&env, "function foo() { return 1 }", "foo");
  // Run without breakpoints.
  CompileRun("foo()");
  CHECK_EQ(0, break_point_hit_count);

  // Run with breakpoint
  i::DirectHandle<i::BreakPoint> bp = SetBreakPoint(foo, 0, "a == true");
  CompileRun("foo()");
  CHECK_EQ(0, break_point_hit_count);

  CompileRun("a = true");
  CompileRun("foo()");
  CHECK_EQ(1, break_point_hit_count);

  // Run without breakpoints.
  ClearBreakPoint(bp);
  CompileRun("foo()");
  CHECK_EQ(1, break_point_hit_count);

  v8::debug::SetDebugDelegate(env->GetIsolate(), nullptr);
  CheckDebuggerUnloaded();
}

// Test that a break point can be set at an IC load location.
TEST(BreakPointICLoad) {
  break_point_hit_count = 0;
  LocalContext env;
  v8::HandleScope scope(env->GetIsolate());
  DebugEventCounter delegate;
  v8::debug::SetDebugDelegate(env->GetIsolate(), &delegate);

  CompileRunChecked(env->GetIsolate(), "bar=1");
  v8::Local<v8::Function> foo =
      CompileFunction(&env, "function foo(){var x=bar;}", "foo");

  // Run without breakpoints.
  foo->Call(env.local(), env->Global(), 0, nullptr).ToLocalChecked();
  CHECK_EQ(0, break_point_hit_count);

  // Run with breakpoint.
  i::DirectHandle<i::BreakPoint> bp = SetBreakPoint(foo, 0);
  foo->Call(env.local(), env->Global(), 0, nullptr).ToLocalChecked();
  CHECK_EQ(1, break_point_hit_count);
  foo->Call(env.local(), env->Global(), 0, nullptr).ToLocalChecked();
  CHECK_EQ(2, break_point_hit_count);

  // Run without breakpoints.
  ClearBreakPoint(bp);
  foo->Call(env.local(), env->Global(), 0, nullptr).ToLocalChecked();
  CHECK_EQ(2, break_point_hit_count);

  v8::debug::SetDebugDelegate(env->GetIsolate(), nullptr);
  CheckDebuggerUnloaded();
}


// Test that a break point can be set at an IC call location.
TEST(BreakPointICCall) {
  break_point_hit_count = 0;
  LocalContext env;
  v8::HandleScope scope(env->GetIsolate());
  DebugEventCounter delegate;
  v8::debug::SetDebugDelegate(env->GetIsolate(), &delegate);
  CompileRunChecked(env->GetIsolate(), "function bar(){}");
  v8::Local<v8::Function> foo =
      CompileFunction(&env, "function foo(){bar();}", "foo");

  // Run without breakpoints.
  foo->Call(env.local(), env->Global(), 0, nullptr).ToLocalChecked();
  CHECK_EQ(0, break_point_hit_count);

  // Run with breakpoint
  i::DirectHandle<i::BreakPoint> bp = SetBreakPoint(foo, 0);
  foo->Call(env.local(), env->Global(), 0, nullptr).ToLocalChecked();
  CHECK_EQ(1, break_point_hit_count);
  foo->Call(env.local(), env->Global(), 0, nullptr).ToLocalChecked();
  CHECK_EQ(2, break_point_hit_count);

  // Run without breakpoints.
  ClearBreakPoint(bp);
  foo->Call(env.local(), env->Global(), 0, nullptr).ToLocalChecked();
  CHECK_EQ(2, break_point_hit_count);

  v8::debug::SetDebugDelegate(env->GetIsolate(), nullptr);
  CheckDebuggerUnloaded();
}


// Test that a break point can be set at an IC call location and survive a GC.
TEST(BreakPointICCallWithGC) {
  break_point_hit_count = 0;
  LocalContext env;
  v8::HandleScope scope(env->GetIsolate());
  DebugEventBreakPointCollectGarbage delegate;
  v8::debug::SetDebugDelegate(env->GetIsolate(), &delegate);
  CompileRunChecked(env->GetIsolate(), "function bar(){return 1;}");
  v8::Local<v8::Function> foo =
      CompileFunction(&env, "function foo(){return bar();}", "foo");
  v8::Local<v8::Context> context = env.local();

  // Run without breakpoints.
  CHECK_EQ(1, foo->Call(context, env->Global(), 0, nullptr)
                  .ToLocalChecked()
                  ->Int32Value(context)
                  .FromJust());
  CHECK_EQ(0, break_point_hit_count);

  // Run with breakpoint.
  i::DirectHandle<i::BreakPoint> bp = SetBreakPoint(foo, 0);
  CHECK_EQ(1, foo->Call(context, env->Global(), 0, nullptr)
                  .ToLocalChecked()
                  ->Int32Value(context)
                  .FromJust());
  CHECK_EQ(1, break_point_hit_count);
  CHECK_EQ(1, foo->Call(context, env->Global(), 0, nullptr)
                  .ToLocalChecked()
                  ->Int32Value(context)
                  .FromJust());
  CHECK_EQ(2, break_point_hit_count);

  // Run without breakpoints.
  ClearBreakPoint(bp);
  foo->Call(context, env->Global(), 0, nullptr).ToLocalChecked();
  CHECK_EQ(2, break_point_hit_count);

  v8::debug::SetDebugDelegate(env->GetIsolate(), nullptr);
  CheckDebuggerUnloaded();
}


// Test that a break point can be set at an IC call location and survive a GC.
TEST(BreakPointConstructCallWithGC) {
  break_point_hit_count = 0;
  LocalContext env;
  v8::HandleScope scope(env->GetIsolate());
  DebugEventBreakPointCollectGarbage delegate;
  v8::debug::SetDebugDelegate(env->GetIsolate(), &delegate);
  CompileRunChecked(env->GetIsolate(), "function bar(){ this.x = 1;}");
  v8::Local<v8::Function> foo =
      CompileFunction(&env, "function foo(){return new bar(1).x;}", "foo");
  v8::Local<v8::Context> context = env.local();

  // Run without breakpoints.
  CHECK_EQ(1, foo->Call(context, env->Global(), 0, nullptr)
                  .ToLocalChecked()
                  ->Int32Value(context)
                  .FromJust());
  CHECK_EQ(0, break_point_hit_count);

  // Run with breakpoint.
  i::DirectHandle<i::BreakPoint> bp = SetBreakPoint(foo, 0);
  CHECK_EQ(1, foo->Call(context, env->Global(), 0, nullptr)
                  .ToLocalChecked()
                  ->Int32Value(context)
                  .FromJust());
  CHECK_EQ(1, break_point_hit_count);
  CHECK_EQ(1, foo->Call(context, env->Global(), 0, nullptr)
                  .ToLocalChecked()
                  ->Int32Value(context)
                  .FromJust());
  CHECK_EQ(2, break_point_hit_count);

  // Run without breakpoints.
  ClearBreakPoint(bp);
  foo->Call(context, env->Global(), 0, nullptr).ToLocalChecked();
  CHECK_EQ(2, break_point_hit_count);

  v8::debug::SetDebugDelegate(env->GetIsolate(), nullptr);
  CheckDebuggerUnloaded();
}


TEST(BreakPointBuiltin) {
  LocalContext env;
  v8::HandleScope scope(env->GetIsolate());

  DebugEventCounter delegate;
  v8::debug::SetDebugDelegate(env->GetIsolate(), &delegate);

  v8::Local<v8::Function> builtin;
  i::DirectHandle<i::BreakPoint> bp;

  // === Test simple builtin ===
  break_point_hit_count = 0;
  builtin = CompileRun("String.prototype.repeat").As<v8::Function>();

  // Run with breakpoint.
  bp = SetBreakPoint(builtin, 0, "this != 1");
  ExpectString("'b'.repeat(10)", "bbbbbbbbbb");
  CHECK_EQ(1, break_point_hit_count);

  ExpectString("'b'.repeat(10)", "bbbbbbbbbb");
  CHECK_EQ(2, break_point_hit_count);

  // Run without breakpoints.
  ClearBreakPoint(bp);
  ExpectString("'b'.repeat(10)", "bbbbbbbbbb");
  CHECK_EQ(2, break_point_hit_count);

  v8::debug::SetDebugDelegate(env->GetIsolate(), nullptr);
  CheckDebuggerUnloaded();
}

TEST(BreakPointApiIntrinsics) {
  LocalContext env;
  v8::HandleScope scope(env->GetIsolate());

  DebugEventCounter delegate;
  v8::debug::SetDebugDelegate(env->GetIsolate(), &delegate);

  // === Test that using API-exposed functions won't trigger breakpoints ===
  {
    v8::Local<v8::Function> weakmap_get =
        CompileRun("WeakMap.prototype.get").As<v8::Function>();
    SetBreakPoint(weakmap_get, 0);
    v8::Local<v8::Function> weakmap_set =
        CompileRun("WeakMap.prototype.set").As<v8::Function>();
    SetBreakPoint(weakmap_set, 0);

    // Run with breakpoint.
    break_point_hit_count = 0;
    CompileRun("var w = new WeakMap(); w.set(w, 1); w.get(w);");
    CHECK_EQ(2, break_point_hit_count);

    break_point_hit_count = 0;
    v8::Local<v8::debug::EphemeronTable> weakmap =
        v8::debug::EphemeronTable::New(env->GetIsolate());
    v8::Local<v8::Object> key = v8::Object::New(env->GetIsolate());
    CHECK(!weakmap->Set(env->GetIsolate(), key, v8_num(1)).IsEmpty());
    CHECK(!weakmap->Get(env->GetIsolate(), key).IsEmpty());
    CHECK_EQ(0, break_point_hit_count);
  }

  {
    v8::Local<v8::Function> object_to_string =
        CompileRun("Object.prototype.toString").As<v8::Function>();
    SetBreakPoint(object_to_string, 0);

    // Run with breakpoint.
    break_point_hit_count = 0;
    CompileRun("var o = {}; o.toString();");
    CHECK_EQ(1, break_point_hit_count);

    break_point_hit_count = 0;
    v8::Local<v8::Object> object = v8::Object::New(env->GetIsolate());
    CHECK(!object->ObjectProtoToString(env.local()).IsEmpty());
    CHECK_EQ(0, break_point_hit_count);
  }

  {
    v8::Local<v8::Function> map_set =
        CompileRun("Map.prototype.set").As<v8::Function>();
    v8::Local<v8::Function> map_get =
        CompileRun("Map.prototype.get").As<v8::Function>();
    v8::Local<v8::Function> map_has =
        CompileRun("Map.prototype.has").As<v8::Function>();
    v8::Local<v8::Function> map_delete =
        CompileRun("Map.prototype.delete").As<v8::Function>();
    SetBreakPoint(map_set, 0);
    SetBreakPoint(map_get, 0);
    SetBreakPoint(map_has, 0);
    SetBreakPoint(map_delete, 0);

    // Run with breakpoint.
    break_point_hit_count = 0;
    CompileRun(
        "var m = new Map(); m.set(m, 1); m.get(m); m.has(m); m.delete(m);");
    CHECK_EQ(4, break_point_hit_count);

    break_point_hit_count = 0;
    v8::Local<v8::Map> map = v8::Map::New(env->GetIsolate());
    CHECK(!map->Set(env.local(), map, v8_num(1)).IsEmpty());
    CHECK(!map->Get(env.local(), map).IsEmpty());
    CHECK(map->Has(env.local(), map).FromJust());
    CHECK(map->Delete(env.local(), map).FromJust());
    CHECK_EQ(0, break_point_hit_count);
  }

  {
    v8::Local<v8::Function> set_add =
        CompileRun("Set.prototype.add").As<v8::Function>();
    v8::Local<v8::Function> set_get =
        CompileRun("Set.prototype.has").As<v8::Function>();
    v8::Local<v8::Function> set_delete =
        CompileRun("Set.prototype.delete").As<v8::Function>();
    SetBreakPoint(set_add, 0);
    SetBreakPoint(set_get, 0);
    SetBreakPoint(set_delete, 0);

    // Run with breakpoint.
    break_point_hit_count = 0;
    CompileRun("var s = new Set(); s.add(s); s.has(s); s.delete(s);");
    CHECK_EQ(3, break_point_hit_count);

    break_point_hit_count = 0;
    v8::Local<v8::Set> set = v8::Set::New(env->GetIsolate());
    CHECK(!set->Add(env.local(), set).IsEmpty());
    CHECK(set->Has(env.local(), set).FromJust());
    CHECK(set->Delete(env.local(), set).FromJust());
    CHECK_EQ(0, break_point_hit_count);
  }

  v8::debug::SetDebugDelegate(env->GetIsolate(), nullptr);
  CheckDebuggerUnloaded();
}

TEST(BreakPointJSBuiltin) {
  LocalContext env;
  v8::HandleScope scope(env->GetIsolate());

  DebugEventCounter delegate;
  v8::debug::SetDebugDelegate(env->GetIsolate(), &delegate);

  v8::Local<v8::Function> builtin;
  i::DirectHandle<i::BreakPoint> bp;

  // === Test JS builtin ===
  break_point_hit_count = 0;
  builtin = CompileRun("Array.prototype.sort").As<v8::Function>();

  // Run with breakpoint.
  bp = SetBreakPoint(builtin, 0);
  CompileRun("[1,2,3].sort()");
  CHECK_EQ(1, break_point_hit_count);

  CompileRun("[1,2,3].sort()");
  CHECK_EQ(2, break_point_hit_count);

  // Run without breakpoints.
  ClearBreakPoint(bp);
  CompileRun("[1,2,3].sort()");
  CHECK_EQ(2, break_point_hit_count);

  v8::debug::SetDebugDelegate(env->GetIsolate(), nullptr);
  CheckDebuggerUnloaded();
}

TEST(BreakPointBoundBuiltin) {
  LocalContext env;
  v8::HandleScope scope(env->GetIsolate());

  DebugEventCounter delegate;
  v8::debug::SetDebugDelegate(env->GetIsolate(), &delegate);

  v8::Local<v8::Function> builtin;
  i::DirectHandle<i::BreakPoint> bp;

  // === Test bound function from a builtin ===
  break_point_hit_count = 0;
  builtin = CompileRun(
                "var boundrepeat = String.prototype.repeat.bind('a');"
                "String.prototype.repeat")
                .As<v8::Function>();
  ExpectString("boundrepeat(10)", "aaaaaaaaaa");
  CHECK_EQ(0, break_point_hit_count);

  // Run with breakpoint.
  bp = SetBreakPoint(builtin, 0);
  ExpectString("boundrepeat(10)", "aaaaaaaaaa");
  CHECK_EQ(1, break_point_hit_count);

  // Run without breakpoints.
  ClearBreakPoint(bp);
  ExpectString("boundrepeat(10)", "aaaaaaaaaa");
  CHECK_EQ(1, break_point_hit_count);

  v8::debug::SetDebugDelegate(env->GetIsolate(), nullptr);
  CheckDebuggerUnloaded();
}

TEST(BreakPointConstructorBuiltin) {
  LocalContext env;
  v8::HandleScope scope(env->GetIsolate());

  DebugEventCounter delegate;
  v8::debug::SetDebugDelegate(env->GetIsolate(), &delegate);

  v8::Local<v8::Function> builtin;
  i::DirectHandle<i::BreakPoint> bp;

  // === Test Promise constructor ===
  break_point_hit_count = 0;
  builtin = CompileRun("Promise").As<v8::Function>();
  ExpectString("(new Promise(()=>{})).toString()", "[object Promise]");
  CHECK_EQ(0, break_point_hit_count);

  // Run with breakpoint.
  bp = SetBreakPoint(builtin, 0, "this != 1");
  ExpectString("(new Promise(()=>{})).toString()", "[object Promise]");
  CHECK_EQ(1, break_point_hit_count);

  // Run without breakpoints.
  ClearBreakPoint(bp);
  ExpectString("(new Promise(()=>{})).toString()", "[object Promise]");
  CHECK_EQ(1, break_point_hit_count);

  // === Test Object constructor ===
  break_point_hit_count = 0;
  builtin = CompileRun("Object").As<v8::Function>();
  CompileRun("new Object()");
  CHECK_EQ(0, break_point_hit_count);

  // Run with breakpoint.
  bp = SetBreakPoint(builtin, 0);
  CompileRun("new Object()");
  CHECK_EQ(1, break_point_hit_count);

  // Run without breakpoints.
  ClearBreakPoint(bp);
  CompileRun("new Object()");
  CHECK_EQ(1, break_point_hit_count);

  // === Test Number constructor ===
  break_point_hit_count = 0;
  builtin = CompileRun("Number").As<v8::Function>();
  CompileRun("new Number()");
  CHECK_EQ(0, break_point_hit_count);

  // Run with breakpoint.
  bp = SetBreakPoint(builtin, 0);
  CompileRun("new Number()");
  CHECK_EQ(1, break_point_hit_count);

  // Run without breakpoints.
  ClearBreakPoint(bp);
  CompileRun("new Number()");
  CHECK_EQ(1, break_point_hit_count);

  v8::debug::SetDebugDelegate(env->GetIsolate(), nullptr);
  CheckDebuggerUnloaded();
}

TEST(BreakPointInlinedBuiltin) {
  i::v8_flags.allow_natives_syntax = true;
  LocalContext env;
  v8::HandleScope scope(env->GetIsolate());

  DebugEventCounter delegate;
  v8::debug::SetDebugDelegate(env->GetIsolate(), &delegate);

  v8::Local<v8::Function> builtin;
  i::DirectHandle<i::BreakPoint> bp;

  // === Test inlined builtin ===
  break_point_hit_count = 0;
  builtin = CompileRun("Math.sin").As<v8::Function>();
  CompileRun("function test(x) { return 1 + Math.sin(x) }");
  CompileRun(
      "%PrepareFunctionForOptimization(test);"
      "test(0.5); test(0.6);"
      "%OptimizeFunctionOnNextCall(test); test(0.7);");
  CHECK_EQ(0, break_point_hit_count);

  // Run with breakpoint.
  bp = SetBreakPoint(builtin, 0, "this != 1");
  CompileRun("Math.sin(0.1);");
  CHECK_EQ(1, break_point_hit_count);
  CompileRun("test(0.2);");
  CHECK_EQ(2, break_point_hit_count);

  // Re-optimize.
  CompileRun(
      "%PrepareFunctionForOptimization(test);"
      "%OptimizeFunctionOnNextCall(test);");
  ExpectBoolean("test(0.3) < 2", true);
  CHECK_EQ(3, break_point_hit_count);

  // Run without breakpoints.
  ClearBreakPoint(bp);
  CompileRun("test(0.3);");
  CHECK_EQ(3, break_point_hit_count);

  v8::debug::SetDebugDelegate(env->GetIsolate(), nullptr);
  CheckDebuggerUnloaded();
}

TEST(BreakPointInlineBoundBuiltin) {
  i::v8_flags.allow_natives_syntax = true;
  LocalContext env;
  v8::HandleScope scope(env->GetIsolate());

  DebugEventCounter delegate;
  v8::debug::SetDebugDelegate(env->GetIsolate(), &delegate);

  v8::Local<v8::Function> builtin;
  i::DirectHandle<i::BreakPoint> bp;

  // === Test inlined bound builtin ===
  break_point_hit_count = 0;

  builtin = CompileRun(
                "var boundrepeat = String.prototype.repeat.bind('a');"
                "String.prototype.repeat")
                .As<v8::Function>();
  CompileRun("function test(x) { return 'a' + boundrepeat(x) }");
  CompileRun(
      "%PrepareFunctionForOptimization(test);"
      "test(4); test(5);"
      "%OptimizeFunctionOnNextCall(test); test(6);");
  CHECK_EQ(0, break_point_hit_count);

  // Run with breakpoint.
  bp = SetBreakPoint(builtin, 0, "this != 1");
  CompileRun("'a'.repeat(2);");
  CHECK_EQ(1, break_point_hit_count);
  CompileRun("test(7);");
  CHECK_EQ(2, break_point_hit_count);

  // Re-optimize.
  CompileRun(
      "%PrepareFunctionForOptimization(f);"
      "%OptimizeFunctionOnNextCall(test);");
  CompileRun("test(8);");
  CHECK_EQ(3, break_point_hit_count);

  // Run without breakpoints.
  ClearBreakPoint(bp);
  CompileRun("test(9);");
  CHECK_EQ(3, break_point_hit_count);

  v8::debug::SetDebugDelegate(env->GetIsolate(), nullptr);
  CheckDebuggerUnloaded();
}

TEST(BreakPointInlinedConstructorBuiltin) {
  i::v8_flags.allow_natives_syntax = true;
  LocalContext env;
  v8::HandleScope scope(env->GetIsolate());

  DebugEventCounter delegate;
  v8::debug::SetDebugDelegate(env->GetIsolate(), &delegate);

  v8::Local<v8::Function> builtin;
  i::DirectHandle<i::BreakPoint> bp;

  // === Test inlined constructor builtin (regular construct builtin) ===
  break_point_hit_count = 0;
  builtin = CompileRun("Promise").As<v8::Function>();
  CompileRun("function test(x) { return new Promise(()=>x); }");
  CompileRun(
      "%PrepareFunctionForOptimization(test);"
      "test(4); test(5);"
      "%OptimizeFunctionOnNextCall(test); test(6);");
  CHECK_EQ(0, break_point_hit_count);

  // Run with breakpoint.
  bp = SetBreakPoint(builtin, 0, "this != 1");
  CompileRun("new Promise(()=>{});");
  CHECK_EQ(1, break_point_hit_count);
  CompileRun("test(7);");
  CHECK_EQ(2, break_point_hit_count);

  // Re-optimize.
  CompileRun(
      "%PrepareFunctionForOptimization(f);"
      "%OptimizeFunctionOnNextCall(test);");
  CompileRun("test(8);");
  CHECK_EQ(3, break_point_hit_count);

  // Run without breakpoints.
  ClearBreakPoint(bp);
  CompileRun("test(9);");
  CHECK_EQ(3, break_point_hit_count);

  v8::debug::SetDebugDelegate(env->GetIsolate(), nullptr);
  CheckDebuggerUnloaded();
}

TEST(BreakPointBuiltinConcurrentOpt) {
  i::v8_flags.allow_natives_syntax = true;
  LocalContext env;
  v8::HandleScope scope(env->GetIsolate());

  DebugEventCounter delegate;
  v8::debug::SetDebugDelegate(env->GetIsolate(), &delegate);

  v8::Local<v8::Function> builtin;
  i::DirectHandle<i::BreakPoint> bp;

  // === Test concurrent optimization ===
  break_point_hit_count = 0;
  builtin = CompileRun("Math.sin").As<v8::Function>();
  CompileRun("function test(x) { return 1 + Math.sin(x) }");
  CompileRun(
      "%PrepareFunctionForOptimization(test);"
      "test(0.5); test(0.6);"
      "%DisableOptimizationFinalization();"
      "%OptimizeFunctionOnNextCall(test, 'concurrent');"
      "test(0.7);");
  CHECK_EQ(0, break_point_hit_count);

  // Run with breakpoint.
  bp = SetBreakPoint(builtin, 0);
  // Have the concurrent compile job finish now.
  CompileRun(
      "%FinalizeOptimization();"
      "%GetOptimizationStatus(test);");
  CompileRun("test(0.2);");
  CHECK_EQ(1, break_point_hit_count);

  // Run without breakpoints.
  ClearBreakPoint(bp);
  CompileRun("test(0.3);");
  CHECK_EQ(1, break_point_hit_count);

  v8::debug::SetDebugDelegate(env->GetIsolate(), nullptr);
  CheckDebuggerUnloaded();
}

TEST(BreakPointBuiltinTFOperator) {
  i::v8_flags.allow_natives_syntax = true;
  LocalContext env;
  v8::HandleScope scope(env->GetIsolate());

  DebugEventCounter delegate;
  v8::debug::SetDebugDelegate(env->GetIsolate(), &delegate);

  v8::Local<v8::Function> builtin;
  i::DirectHandle<i::BreakPoint> bp;

  // === Test builtin represented as operator ===
  break_point_hit_count = 0;
  builtin = CompileRun("String.prototype.indexOf").As<v8::Function>();
  CompileRun("function test(x) { return 1 + 'foo'.indexOf(x) }");
  CompileRun(
      "%PrepareFunctionForOptimization(f);"
      "test('a'); test('b');"
      "%OptimizeFunctionOnNextCall(test); test('c');");
  CHECK_EQ(0, break_point_hit_count);

  // Run with breakpoint.
  bp = SetBreakPoint(builtin, 0);
  CompileRun("'bar'.indexOf('x');");
  CHECK_EQ(1, break_point_hit_count);
  CompileRun("test('d');");
  CHECK_EQ(2, break_point_hit_count);

  // Re-optimize.
  CompileRun(
      "%PrepareFunctionForOptimization(f);"
      "%OptimizeFunctionOnNextCall(test);");
  CompileRun("test('e');");
  CHECK_EQ(3, break_point_hit_count);

  // Run without breakpoints.
  ClearBreakPoint(bp);
  CompileRun("test('f');");
  CHECK_EQ(3, break_point_hit_count);

  v8::debug::SetDebugDelegate(env->GetIsolate(), nullptr);
  CheckDebuggerUnloaded();
}

TEST(BreakPointBuiltinNewContext) {
  LocalContext env;
  v8::HandleScope scope(env->GetIsolate());

  DebugEventCounter delegate;
  v8::debug::SetDebugDelegate(env->GetIsolate(), &delegate);

  v8::Local<v8::Function> builtin;
  i::DirectHandle<i::BreakPoint> bp;

  // === Test builtin from a new context ===
  break_point_hit_count = 0;
  builtin = CompileRun("String.prototype.repeat").As<v8::Function>();
  CompileRun("'a'.repeat(10)");
  CHECK_EQ(0, break_point_hit_count);
  // Set breakpoint.
  bp = SetBreakPoint(builtin, 0);

  {
    // Create and use new context after breakpoint has been set.
    v8::HandleScope handle_scope(env->GetIsolate());
    v8::Local<v8::Context> new_context = v8::Context::New(env->GetIsolate());
    v8::Context::Scope context_scope(new_context);

    // Run with breakpoint.
    CompileRun("'b'.repeat(10)");
    CHECK_EQ(1, break_point_hit_count);

    CompileRun("'b'.repeat(10)");
    CHECK_EQ(2, break_point_hit_count);

    // Run without breakpoints.
    ClearBreakPoint(bp);
    CompileRun("'b'.repeat(10)");
    CHECK_EQ(2, break_point_hit_count);
  }

  v8::debug::SetDebugDelegate(env->GetIsolate(), nullptr);
  CheckDebuggerUnloaded();
}

void NoOpFunctionCallback(const v8::FunctionCallbackInfo<v8::Value>& info) {
  CHECK(i::ValidateCallbackInfo(info));
  info.GetReturnValue().Set(v8_num(2));
}

TEST(BreakPointApiFunction) {
  LocalContext env;
  v8::HandleScope scope(env->GetIsolate());

  DebugEventCounter delegate;
  v8::debug::SetDebugDelegate(env->GetIsolate(), &delegate);

  i::DirectHandle<i::BreakPoint> bp;

  v8::Local<v8::FunctionTemplate> function_template =
      v8::FunctionTemplate::New(env->GetIsolate(), NoOpFunctionCallback);

  v8::Local<v8::Function> function =
      function_template->GetFunction(env.local()).ToLocalChecked();

  env->Global()->Set(env.local(), v8_str("f"), function).ToChecked();

  // === Test simple builtin ===
  break_point_hit_count = 0;

  // Run with breakpoint.
  bp = SetBreakPoint(function, 0, "this != 1");
  ExpectInt32("f()", 2);
  CHECK_EQ(1, break_point_hit_count);

  ExpectInt32("f()", 2);
  CHECK_EQ(2, break_point_hit_count);

  // Direct call through API does not trigger breakpoint.
  function->Call(env.local(), v8::Undefined(env->GetIsolate()), 0, nullptr)
      .ToLocalChecked();
  CHECK_EQ(2, break_point_hit_count);

  // Run without breakpoints.
  ClearBreakPoint(bp);
  ExpectInt32("f()", 2);
  CHECK_EQ(2, break_point_hit_count);

  v8::debug::SetDebugDelegate(env->GetIsolate(), nullptr);
  CheckDebuggerUnloaded();
}

TEST(BreakPointApiConstructor) {
  LocalContext env;
  v8::HandleScope scope(env->GetIsolate());

  DebugEventCounter delegate;
  v8::debug::SetDebugDelegate(env->GetIsolate(), &delegate);

  i::DirectHandle<i::BreakPoint> bp;

  v8::Local<v8::FunctionTemplate> function_template =
      v8::FunctionTemplate::New(env->GetIsolate(), NoOpFunctionCallback);

  v8::Local<v8::Function> function =
      function_template->GetFunction(env.local()).ToLocalChecked();

  env->Global()->Set(env.local(), v8_str("f"), function).ToChecked();

  // === Test simple builtin ===
  break_point_hit_count = 0;

  // Run with breakpoint.
  bp = SetBreakPoint(function, 0, "this != 1");
  CompileRun("new f()");
  CHECK_EQ(1, break_point_hit_count);
  CompileRun("new f()");
  CHECK_EQ(2, break_point_hit_count);

  // Direct call through API does not trigger breakpoint.
  function->NewInstance(env.local()).ToLocalChecked();
  CHECK_EQ(2, break_point_hit_count);

  // Run without breakpoints.
  ClearBreakPoint(bp);
  CompileRun("new f()");
  CHECK_EQ(2, break_point_hit_count);

  v8::debug::SetDebugDelegate(env->GetIsolate(), nullptr);
  CheckDebuggerUnloaded();
}

void GetWrapperCallback(const v8::FunctionCallbackInfo<v8::Value>& info) {
  CHECK(i::ValidateCallbackInfo(info));
  info.GetReturnValue().Set(
      info[0]
          .As<v8::Object>()
          ->Get(info.GetIsolate()->GetCurrentContext(), info[1])
          .ToLocalChecked());
}

TEST(BreakPointApiGetter) {
  LocalContext env;
  v8::HandleScope scope(env->GetIsolate());

  DebugEventCounter delegate;
  v8::debug::SetDebugDelegate(env->GetIsolate(), &delegate);

  i::DirectHandle<i::BreakPoint> bp;

  v8::Local<v8::FunctionTemplate> function_template =
      v8::FunctionTemplate::New(env->GetIsolate(), NoOpFunctionCallback);
  v8::Local<v8::FunctionTemplate> get_template =
      v8::FunctionTemplate::New(env->GetIsolate(), GetWrapperCallback);

  v8::Local<v8::Function> function =
      function_template->GetFunction(env.local()).ToLocalChecked();
  v8::Local<v8::Function> get =
      get_template->GetFunction(env.local()).ToLocalChecked();

  env->Global()->Set(env.local(), v8_str("f"), function).ToChecked();
  env->Global()->Set(env.local(), v8_str("get_wrapper"), get).ToChecked();
  CompileRun(
      "var o = {};"
      "Object.defineProperty(o, 'f', { get: f, enumerable: true });");

  // === Test API builtin as getter ===
  break_point_hit_count = 0;

  // Run with breakpoint.
  bp = SetBreakPoint(function, 0);
  CompileRun("get_wrapper(o, 'f')");
  CHECK_EQ(0, break_point_hit_count);

  CompileRun("o.f");
  CHECK_EQ(1, break_point_hit_count);

  // Run without breakpoints.
  ClearBreakPoint(bp);
  CompileRun("get_wrapper(o, 'f', 2)");
  CompileRun("o.f");
  CHECK_EQ(1, break_point_hit_count);

  v8::debug::SetDebugDelegate(env->GetIsolate(), nullptr);
  CheckDebuggerUnloaded();
}

void SetWrapperCallback(const v8::FunctionCallbackInfo<v8::Value>& info) {
  CHECK(i::ValidateCallbackInfo(info));
  CHECK(info[0]
            .As<v8::Object>()
            ->Set(info.GetIsolate()->GetCurrentContext(), info[1], info[2])
            .FromJust());
}

TEST(BreakPointApiSetter) {
  LocalContext env;
  v8::HandleScope scope(env->GetIsolate());

  DebugEventCounter delegate;
  v8::debug::SetDebugDelegate(env->GetIsolate(), &delegate);

  i::DirectHandle<i::BreakPoint> bp;

  v8::Local<v8::FunctionTemplate> function_template =
      v8::FunctionTemplate::New(env->GetIsolate(), NoOpFunctionCallback);
  v8::Local<v8::FunctionTemplate> set_template =
      v8::FunctionTemplate::New(env->GetIsolate(), SetWrapperCallback);

  v8::Local<v8::Function> function =
      function_template->GetFunction(env.local()).ToLocalChecked();
  v8::Local<v8::Function> set =
      set_template->GetFunction(env.local()).ToLocalChecked();

  env->Global()->Set(env.local(), v8_str("f"), function).ToChecked();
  env->Global()->Set(env.local(), v8_str("set_wrapper"), set).ToChecked();

  CompileRun(
      "var o = {};"
      "Object.defineProperty(o, 'f', { set: f, enumerable: true });");

  // === Test API builtin as setter ===
  break_point_hit_count = 0;

  // Run with breakpoint.
  bp = SetBreakPoint(function, 0);

  CompileRun("o.f = 3");
  CHECK_EQ(1, break_point_hit_count);

  CompileRun("set_wrapper(o, 'f', 2)");
  CHECK_EQ(1, break_point_hit_count);

  // Run without breakpoints.
  ClearBreakPoint(bp);
  CompileRun("o.f = 3");
  CHECK_EQ(1, break_point_hit_count);

  // === Test API builtin as setter, with condition ===
  break_point_hit_count = 0;

  // Run with breakpoint.
  bp = SetBreakPoint(function, 0, "arguments[0] == 3");
  CompileRun("set_wrapper(o, 'f', 2)");
  CHECK_EQ(0, break_point_hit_count);

  CompileRun("set_wrapper(o, 'f', 3)");
  CHECK_EQ(0, break_point_hit_count);

  CompileRun("o.f = 3");
  CHECK_EQ(1, break_point_hit_count);

  // Run without breakpoints.
  ClearBreakPoint(bp);
  CompileRun("set_wrapper(o, 'f', 2)");
  CompileRun("o.f = 3");
  CHECK_EQ(1, break_point_hit_count);

  v8::debug::SetDebugDelegate(env->GetIsolate(), nullptr);
  CheckDebuggerUnloaded();
}

TEST(BreakPointApiAccessor) {
  LocalContext env;
  v8::HandleScope scope(env->GetIsolate());

  DebugEventCounter delegate;
  v8::debug::SetDebugDelegate(env->GetIsolate(), &delegate);

  i::DirectHandle<i::BreakPoint> bp;

  // Create 'foo' class, with a hidden property.
  v8::Local<v8::ObjectTemplate> obj_template =
      v8::ObjectTemplate::New(env->GetIsolate());
  v8::Local<v8::FunctionTemplate> accessor_template =
      v8::FunctionTemplate::New(env->GetIsolate(), NoOpFunctionCallback);
  obj_template->SetAccessorProperty(v8_str("f"), accessor_template,
                                    accessor_template);
  v8::Local<v8::Object> obj =
      obj_template->NewInstance(env.local()).ToLocalChecked();
  env->Global()->Set(env.local(), v8_str("o"), obj).ToChecked();

  v8::Local<v8::Function> function =
      CompileRun("Object.getOwnPropertyDescriptor(o, 'f').set")
          .As<v8::Function>();

  // === Test API accessor ===
  break_point_hit_count = 0;

  CompileRun("function get_loop() { for (var i = 0; i < 10; i++) o.f }");
  CompileRun("function set_loop() { for (var i = 0; i < 10; i++) o.f = 2 }");

  CompileRun("get_loop(); set_loop();");  // Initialize ICs.

  // Run with breakpoint.
  bp = SetBreakPoint(function, 0);

  CompileRun("o.f = 3");
  CHECK_EQ(1, break_point_hit_count);

  CompileRun("o.f");
  CHECK_EQ(2, break_point_hit_count);

  CompileRun("for (var i = 0; i < 10; i++) o.f");
  CHECK_EQ(12, break_point_hit_count);

  CompileRun("get_loop();");
  CHECK_EQ(22, break_point_hit_count);

  CompileRun("for (var i = 0; i < 10; i++) o.f = 2");
  CHECK_EQ(32, break_point_hit_count);

  CompileRun("set_loop();");
  CHECK_EQ(42, break_point_hit_count);

  // Test that the break point also works when we install the function
  // template on a new property (with a fresh AccessorPair instance).
  v8::Local<v8::ObjectTemplate> baz_template =
      v8::ObjectTemplate::New(env->GetIsolate());
  baz_template->SetAccessorProperty(v8_str("g"), accessor_template,
                                    accessor_template);
  v8::Local<v8::Object> baz =
      baz_template->NewInstance(env.local()).ToLocalChecked();
  env->Global()->Set(env.local(), v8_str("b"), baz).ToChecked();

  CompileRun("b.g = 4");
  CHECK_EQ(43, break_point_hit_count);

  CompileRun("b.g");
  CHECK_EQ(44, break_point_hit_count);

  // Run without breakpoints.
  ClearBreakPoint(bp);
  CompileRun("o.f = 3");
  CompileRun("o.f");
  CHECK_EQ(44, break_point_hit_count);

  v8::debug::SetDebugDelegate(env->GetIsolate(), nullptr);
  CheckDebuggerUnloaded();
}

TEST(Regress1163547) {
  LocalContext env;
  v8::HandleScope scope(env->GetIsolate());

  DebugEventCounter delegate;
  v8::debug::SetDebugDelegate(env->GetIsolate(), &delegate);

  i::DirectHandle<i::BreakPoint> bp;

  auto constructor_tmpl = v8::FunctionTemplate::New(env->GetIsolate());
  auto prototype_tmpl = constructor_tmpl->PrototypeTemplate();
  auto accessor_tmpl =
      v8::FunctionTemplate::New(env->GetIsolate(), NoOpFunctionCallback);
  prototype_tmpl->SetAccessorProperty(v8_str("f"), accessor_tmpl);

  auto constructor =
      constructor_tmpl->GetFunction(env.local()).ToLocalChecked();
  env->Global()->Set(env.local(), v8_str("C"), constructor).ToChecked();

  CompileRun("o = new C();");
  v8::Local<v8::Function> function =
      CompileRun("Object.getOwnPropertyDescriptor(C.prototype, 'f').get")
          .As<v8::Function>();

  // === Test API accessor ===
  break_point_hit_count = 0;

  // At this point, the C.prototype - which holds the "f" accessor - is in
  // dictionary mode.
  auto constructor_fun =
      Cast<i::JSFunction>(v8::Utils::OpenHandle(*constructor));
  CHECK(
      !i::Cast<i::JSObject>(constructor_fun->prototype())->HasFastProperties());

  // Run with breakpoint.
  bp = SetBreakPoint(function, 0);

  CompileRun("o.f");
  CHECK_EQ(1, break_point_hit_count);

  v8::debug::SetDebugDelegate(env->GetIsolate(), nullptr);
  CheckDebuggerUnloaded();
}

TEST(BreakPointOnLazyAccessorInNewContexts) {
  // Check that breakpoints on a lazy accessor still get hit after creating new
  // contexts.
  // Regression test for parts of http://crbug.com/1368554.
  v8::Isolate* isolate = CcTest::isolate();
  v8::HandleScope scope(isolate);

  DebugEventCounter delegate;
  v8::debug::SetDebugDelegate(isolate, &delegate);

  auto accessor_tmpl = v8::FunctionTemplate::New(isolate, NoOpFunctionCallback);
  accessor_tmpl->SetClassName(v8_str("get f"));
  auto object_tmpl = v8::ObjectTemplate::New(isolate);
  object_tmpl->SetAccessorProperty(v8_str("f"), accessor_tmpl);

  {
    v8::Local<v8::Context> context1 = v8::Context::New(isolate);
    context1->Global()
        ->Set(context1, v8_str("o"),
              object_tmpl->NewInstance(context1).ToLocalChecked())
        .ToChecked();
    v8::Context::Scope context_scope(context1);

    // 1. Set the breakpoint
    v8::Local<v8::Function> function =
        CompileRun(context1, "Object.getOwnPropertyDescriptor(o, 'f').get")
            .ToLocalChecked()
            .As<v8::Function>();
    SetBreakPoint(function, 0);

    // 2. Run and check that we hit the breakpoint
    break_point_hit_count = 0;
    CompileRun(context1, "o.f");
    CHECK_EQ(1, break_point_hit_count);
  }

  {
    // Create a second context and check that we also hit the breakpoint
    // without setting it again.
    v8::Local<v8::Context> context2 = v8::Context::New(isolate);
    context2->Global()
        ->Set(context2, v8_str("o"),
              object_tmpl->NewInstance(context2).ToLocalChecked())
        .ToChecked();
    v8::Context::Scope context_scope(context2);

    CompileRun(context2, "o.f");
    CHECK_EQ(2, break_point_hit_count);
  }

  {
    // Create a third context, but this time we use a global template instead
    // and let the bootstrapper initialize "o" instead.
    auto global_tmpl = v8::ObjectTemplate::New(isolate);
    global_tmpl->Set(v8_str("o"), object_tmpl);
    v8::Local<v8::Context> context3 =
        v8::Context::New(isolate, nullptr, global_tmpl);
    v8::Context::Scope context_scope(context3);

    CompileRun(context3, "o.f");
    CHECK_EQ(3, break_point_hit_count);
  }

  v8::debug::SetDebugDelegate(isolate, nullptr);
  CheckDebuggerUnloaded();
}

TEST(BreakPointInlineApiFunction) {
  i::v8_flags.allow_natives_syntax = true;
  LocalContext env;
  v8::HandleScope scope(env->GetIsolate());

  DebugEventCounter delegate;
  v8::debug::SetDebugDelegate(env->GetIsolate(), &delegate);

  i::DirectHandle<i::BreakPoint> bp;

  v8::Local<v8::FunctionTemplate> function_template =
      v8::FunctionTemplate::New(env->GetIsolate(), NoOpFunctionCallback);

  v8::Local<v8::Function> function =
      function_template->GetFunction(env.local()).ToLocalChecked();

  env->Global()->Set(env.local(), v8_str("f"), function).ToChecked();
  CompileRun(
      "function g() { return 1 +  f(); };"
      "%PrepareFunctionForOptimization(g);");

  // === Test simple builtin ===
  break_point_hit_count = 0;

  // Run with breakpoint.
  bp = SetBreakPoint(function, 0);
  ExpectInt32("g()", 3);
  CHECK_EQ(1, break_point_hit_count);

  ExpectInt32("g()", 3);
  CHECK_EQ(2, break_point_hit_count);

  CompileRun("%OptimizeFunctionOnNextCall(g)");
  ExpectInt32("g()", 3);
  CHECK_EQ(3, break_point_hit_count);

  // Run without breakpoints.
  ClearBreakPoint(bp);
  ExpectInt32("g()", 3);
  CHECK_EQ(3, break_point_hit_count);

  v8::debug::SetDebugDelegate(env->GetIsolate(), nullptr);
  CheckDebuggerUnloaded();
}

// Test that a break point can be set at a return store location.
TEST(BreakPointConditionBuiltin) {
  i::v8_flags.allow_natives_syntax = true;
  LocalContext env;
  v8::HandleScope scope(env->GetIsolate());

  DebugEventCounter delegate;
  v8::debug::SetDebugDelegate(env->GetIsolate(), &delegate);
  v8::Local<v8::Function> builtin;
  i::DirectHandle<i::BreakPoint> bp;

  // === Test global variable ===
  break_point_hit_count = 0;
  builtin = CompileRun("String.prototype.repeat").As<v8::Function>();
  CompileRun("var condition = false");
  CompileRun("'a'.repeat(10)");
  CHECK_EQ(0, break_point_hit_count);

  // Run with breakpoint.
  bp = SetBreakPoint(builtin, 0, "condition == true");
  CompileRun("'b'.repeat(10)");
  CHECK_EQ(0, break_point_hit_count);

  CompileRun("condition = true");
  CompileRun("'b'.repeat(10)");
  CHECK_EQ(1, break_point_hit_count);

  // Run without breakpoints.
  ClearBreakPoint(bp);
  CompileRun("'b'.repeat(10)");
  CHECK_EQ(1, break_point_hit_count);

  // === Test arguments ===
  break_point_hit_count = 0;
  builtin = CompileRun("String.prototype.repeat").As<v8::Function>();
  CompileRun("function f(x) { return 'a'.repeat(x * 2); }");
  CHECK_EQ(0, break_point_hit_count);

  // Run with breakpoint.
  bp = SetBreakPoint(builtin, 0, "arguments[0] == 20");
  ExpectString("f(5)", "aaaaaaaaaa");
  CHECK_EQ(0, break_point_hit_count);

  ExpectString("f(10)", "aaaaaaaaaaaaaaaaaaaa");
  CHECK_EQ(1, break_point_hit_count);

  // Run without breakpoints.
  ClearBreakPoint(bp);
  ExpectString("f(10)", "aaaaaaaaaaaaaaaaaaaa");
  CHECK_EQ(1, break_point_hit_count);

  // === Test adapted arguments ===
  break_point_hit_count = 0;
  builtin = CompileRun("String.prototype.repeat").As<v8::Function>();
  CompileRun("function f(x) { return 'a'.repeat(x * 2, x); }");
  CHECK_EQ(0, break_point_hit_count);

  // Run with breakpoint.
  bp = SetBreakPoint(builtin, 0,
                     "arguments[1] == 10 && arguments[2] == undefined");
  ExpectString("f(5)", "aaaaaaaaaa");
  CHECK_EQ(0, break_point_hit_count);

  ExpectString("f(10)", "aaaaaaaaaaaaaaaaaaaa");
  CHECK_EQ(1, break_point_hit_count);

  // Run without breakpoints.
  ClearBreakPoint(bp);
  ExpectString("f(10)", "aaaaaaaaaaaaaaaaaaaa");
  CHECK_EQ(1, break_point_hit_count);

  // === Test var-arg builtins ===
  break_point_hit_count = 0;
  builtin = CompileRun("String.fromCharCode").As<v8::Function>();
  CompileRun("function f() { return String.fromCharCode(1, 2, 3); }");
  CHECK_EQ(0, break_point_hit_count);

  // Run with breakpoint.
  bp = SetBreakPoint(builtin, 0, "arguments.length == 3 && arguments[1] == 2");
  CompileRun("f(1, 2, 3)");
  CHECK_EQ(1, break_point_hit_count);

  // Run without breakpoints.
  ClearBreakPoint(bp);
  CompileRun("f(1, 2, 3)");
  CHECK_EQ(1, break_point_hit_count);

  // === Test rest arguments ===
  break_point_hit_count = 0;
  builtin = CompileRun("String.fromCharCode").As<v8::Function>();
  CompileRun("function f(...info) { return String.fromCharCode(...info); }");
  CHECK_EQ(0, break_point_hit_count);

  // Run with breakpoint.
  bp = SetBreakPoint(builtin, 0, "arguments.length == 3 && arguments[1] == 2");
  CompileRun("f(1, 2, 3)");
  CHECK_EQ(1, break_point_hit_count);

  ClearBreakPoint(bp);
  CompileRun("f(1, 3, 3)");
  CHECK_EQ(1, break_point_hit_count);

  // Run without breakpoints.
  ClearBreakPoint(bp);
  CompileRun("f(1, 2, 3)");
  CHECK_EQ(1, break_point_hit_count);

  // === Test receiver ===
  break_point_hit_count = 0;
  builtin = CompileRun("String.prototype.repeat").As<v8::Function>();
  CompileRun("function f(x) { return x.repeat(10); }");
  CHECK_EQ(0, break_point_hit_count);

  // Run with breakpoint.
  bp = SetBreakPoint(builtin, 0, "this == 'a'");
  ExpectString("f('b')", "bbbbbbbbbb");
  CHECK_EQ(0, break_point_hit_count);

  ExpectString("f('a')", "aaaaaaaaaa");
  CHECK_EQ(1, break_point_hit_count);

  // Run without breakpoints.
  ClearBreakPoint(bp);
  ExpectString("f('a')", "aaaaaaaaaa");
  CHECK_EQ(1, break_point_hit_count);

  v8::debug::SetDebugDelegate(env->GetIsolate(), nullptr);
  CheckDebuggerUnloaded();
}

TEST(BreakPointInlining) {
  i::v8_flags.allow_natives_syntax = true;
  break_point_hit_count = 0;
  LocalContext env;
  v8::HandleScope scope(env->GetIsolate());

  DebugEventCounter delegate;
  v8::debug::SetDebugDelegate(env->GetIsolate(), &delegate);

  break_point_hit_count = 0;
  v8::Local<v8::Function> inlinee =
      CompileRun("function f(x) { return x*2; } f").As<v8::Function>();
  CompileRun("function test(x) { return 1 + f(x) }");
  CompileRun(
      "%PrepareFunctionForOptimization(test);"
      "test(0.5); test(0.6);"
      "%OptimizeFunctionOnNextCall(test); test(0.7);");
  CHECK_EQ(0, break_point_hit_count);

  // Run with breakpoint.
  i::DirectHandle<i::BreakPoint> bp = SetBreakPoint(inlinee, 0);
  CompileRun("f(0.1);");
  CHECK_EQ(1, break_point_hit_count);
  CompileRun("test(0.2);");
  CHECK_EQ(2, break_point_hit_count);

  // Re-optimize.
  CompileRun(
      "%PrepareFunctionForOptimization(test);"
      "%OptimizeFunctionOnNextCall(test);");
  CompileRun("test(0.3);");
  CHECK_EQ(3, break_point_hit_count);

  // Run without breakpoints.
  ClearBreakPoint(bp);
  CompileRun("test(0.3);");
  CHECK_EQ(3, break_point_hit_count);

  v8::debug::SetDebugDelegate(env->GetIsolate(), nullptr);
  CheckDebuggerUnloaded();
}

static void CallWithBreakPoints(v8::Local<v8::Context> context,
                                v8::Local<v8::Object> recv,
                                v8::Local<v8::Function> f,
                                int break_point_count, int call_count) {
  break_point_hit_count = 0;
  for (int i = 0; i < call_count; i++) {
    f->Call(context, recv, 0, nullptr).ToLocalChecked();
    CHECK_EQ((i + 1) * break_point_count, break_point_hit_count);
  }
}


// Test GC during break point processing.
TEST(GCDuringBreakPointProcessing) {
  break_point_hit_count = 0;
  LocalContext env;
  v8::HandleScope scope(env->GetIsolate());
  v8::Local<v8::Context> context = env.local();

  DebugEventBreakPointCollectGarbage delegate;
  v8::debug::SetDebugDelegate(env->GetIsolate(), &delegate);
  v8::Local<v8::Function> foo;

  // Test IC store break point with garbage collection.
  foo = CompileFunction(&env, "function foo(){bar=0;}", "foo");
  SetBreakPoint(foo, 0);
  CallWithBreakPoints(context, env->Global(), foo, 1, 10);

  // Test IC load break point with garbage collection.
  foo = CompileFunction(&env, "bar=1;function foo(){var x=bar;}", "foo");
  SetBreakPoint(foo, 0);
  CallWithBreakPoints(context, env->Global(), foo, 1, 10);

  // Test IC call break point with garbage collection.
  foo = CompileFunction(&env, "function bar(){};function foo(){bar();}", "foo");
  SetBreakPoint(foo, 0);
  CallWithBreakPoints(context, env->Global(), foo, 1, 10);

  // Test return break point with garbage collection.
  foo = CompileFunction(&env, "function foo(){}", "foo");
  SetBreakPoint(foo, 0);
  CallWithBreakPoints(context, env->Global(), foo, 1, 25);

  // Test debug break slot break point with garbage collection.
  foo = CompileFunction(&env, "function foo(){var a;}", "foo");
  SetBreakPoint(foo, 0);
  CallWithBreakPoints(context, env->Global(), foo, 1, 25);

  v8::debug::SetDebugDelegate(env->GetIsolate(), nullptr);
  CheckDebuggerUnloaded();
}


// Call the function three times with different garbage collections in between
// and make sure that the break point survives.
static void CallAndGC(v8::Local<v8::Context> context,
                      v8::Local<v8::Object> recv, v8::Local<v8::Function> f) {
  break_point_hit_count = 0;

  for (int i = 0; i < 3; i++) {
    // Call function.
    f->Call(context, recv, 0, nullptr).ToLocalChecked();
    CHECK_EQ(1 + i * 3, break_point_hit_count);

    // Scavenge and call function.
    i::heap::InvokeMinorGC(CcTest::heap());
    f->Call(context, recv, 0, nullptr).ToLocalChecked();
    CHECK_EQ(2 + i * 3, break_point_hit_count);

    // Mark sweep (and perhaps compact) and call function.
    i::heap::InvokeMajorGC(CcTest::heap());
    f->Call(context, recv, 0, nullptr).ToLocalChecked();
    CHECK_EQ(3 + i * 3, break_point_hit_count);
  }
}


// Test that a break point can be set at a return store location.
TEST(BreakPointSurviveGC) {
  break_point_hit_count = 0;
  LocalContext env;
  v8::HandleScope scope(env->GetIsolate());
  v8::Local<v8::Context> context = env.local();

  DebugEventCounter delegate;
  v8::debug::SetDebugDelegate(env->GetIsolate(), &delegate);
  v8::Local<v8::Function> foo;

  // Test IC store break point with garbage collection.
  {
    CompileFunction(&env, "function foo(){}", "foo");
    foo = CompileFunction(&env, "function foo(){bar=0;}", "foo");
    SetBreakPoint(foo, 0);
  }
  CallAndGC(context, env->Global(), foo);

  // Test IC load break point with garbage collection.
  {
    CompileFunction(&env, "function foo(){}", "foo");
    foo = CompileFunction(&env, "bar=1;function foo(){var x=bar;}", "foo");
    SetBreakPoint(foo, 0);
  }
  CallAndGC(context, env->Global(), foo);

  // Test IC call break point with garbage collection.
  {
    CompileFunction(&env, "function foo(){}", "foo");
    foo = CompileFunction(&env,
                          "function bar(){};function foo(){bar();}",
                          "foo");
    SetBreakPoint(foo, 0);
  }
  CallAndGC(context, env->Global(), foo);

  // Test return break point with garbage collection.
  {
    CompileFunction(&env, "function foo(){}", "foo");
    foo = CompileFunction(&env, "function foo(){}", "foo");
    SetBreakPoint(foo, 0);
  }
  CallAndGC(context, env->Global(), foo);

  // Test non IC break point with garbage collection.
  {
    CompileFunction(&env, "function foo(){}", "foo");
    foo = CompileFunction(&env, "function foo(){var bar=0;}", "foo");
    SetBreakPoint(foo, 0);
  }
  CallAndGC(context, env->Global(), foo);

  v8::debug::SetDebugDelegate(env->GetIsolate(), nullptr);
  CheckDebuggerUnloaded();
}

// Test that the debugger statement causes a break.
TEST(DebuggerStatement) {
  break_point_hit_count = 0;
  LocalContext env;
  v8::HandleScope scope(env->GetIsolate());
  DebugEventCounter delegate;
  v8::debug::SetDebugDelegate(env->GetIsolate(), &delegate);
  v8::Local<v8::Context> context = env.local();
  v8::Script::Compile(context,
                      v8_str(env->GetIsolate(), "function bar(){debugger}"))
      .ToLocalChecked()
      ->Run(context)
      .ToLocalChecked();
  v8::Script::Compile(
      context, v8_str(env->GetIsolate(), "function foo(){debugger;debugger;}"))
      .ToLocalChecked()
      ->Run(context)
      .ToLocalChecked();
  v8::Local<v8::Function> foo = v8::Local<v8::Function>::Cast(
      env->Global()
          ->Get(context, v8_str(env->GetIsolate(), "foo"))
          .ToLocalChecked());
  v8::Local<v8::Function> bar = v8::Local<v8::Function>::Cast(
      env->Global()
          ->Get(context, v8_str(env->GetIsolate(), "bar"))
          .ToLocalChecked());

  // Run function with debugger statement
  bar->Call(context, env->Global(), 0, nullptr).ToLocalChecked();
  CHECK_EQ(1, break_point_hit_count);

  // Run function with two debugger statement
  foo->Call(context, env->Global(), 0, nullptr).ToLocalChecked();
  CHECK_EQ(3, break_point_hit_count);

  v8::debug::SetDebugDelegate(env->GetIsolate(), nullptr);
  CheckDebuggerUnloaded();
}


// Test setting a breakpoint on the debugger statement.
TEST(DebuggerStatementBreakpoint) {
    break_point_hit_count = 0;
    LocalContext env;
    v8::HandleScope scope(env->GetIsolate());
    v8::Local<v8::Context> context = env.local();
    DebugEventCounter delegate;
    v8::debug::SetDebugDelegate(env->GetIsolate(), &delegate);
    v8::Script::Compile(context,
                        v8_str(env->GetIsolate(), "function foo(){debugger;}"))
        .ToLocalChecked()
        ->Run(context)
        .ToLocalChecked();
    v8::Local<v8::Function> foo = v8::Local<v8::Function>::Cast(
        env->Global()
            ->Get(context, v8_str(env->GetIsolate(), "foo"))
            .ToLocalChecked());

    // The debugger statement triggers breakpoint hit
    foo->Call(context, env->Global(), 0, nullptr).ToLocalChecked();
    CHECK_EQ(1, break_point_hit_count);

    i::DirectHandle<i::BreakPoint> bp = SetBreakPoint(foo, 0);

    // Set breakpoint does not duplicate hits
    foo->Call(context, env->Global(), 0, nullptr).ToLocalChecked();
    CHECK_EQ(2, break_point_hit_count);

    ClearBreakPoint(bp);
    v8::debug::SetDebugDelegate(env->GetIsolate(), nullptr);
    CheckDebuggerUnloaded();
}


// Test that the conditional breakpoints work event if code generation from
// strings is prohibited in the debugee context.
TEST(ConditionalBreakpointWithCodeGenerationDisallowed) {
  LocalContext env;
  v8::HandleScope scope(env->GetIsolate());

  DebugEventCounter delegate;
  v8::debug::SetDebugDelegate(env->GetIsolate(), &delegate);

  v8::Local<v8::Context> context = env.local();
  v8::Local<v8::Function> foo = CompileFunction(&env,
    "function foo(x) {\n"
    "  var s = 'String value2';\n"
    "  return s + x;\n"
    "}",
    "foo");

  // Set conditional breakpoint with condition 'true'.
  SetBreakPoint(foo, 4, "true");

  break_point_hit_count = 0;
  env->AllowCodeGenerationFromStrings(false);
  foo->Call(context, env->Global(), 0, nullptr).ToLocalChecked();
  CHECK_EQ(1, break_point_hit_count);

  v8::debug::SetDebugDelegate(env->GetIsolate(), nullptr);
  CheckDebuggerUnloaded();
}


// Simple test of the stepping mechanism using only store ICs.
TEST(DebugStepLinear) {
  LocalContext env;
  v8::HandleScope scope(env->GetIsolate());

  // Create a function for testing stepping.
  v8::Local<v8::Function> foo = CompileFunction(&env,
                                                "function foo(){a=1;b=1;c=1;}",
                                                "foo");

  // Run foo to allow it to get optimized.
  CompileRun("a=0; b=0; c=0; foo();");

  // Register a debug event listener which steps and counts.
  DebugEventCounter run_step;
  v8::debug::SetDebugDelegate(env->GetIsolate(), &run_step);

  SetBreakPoint(foo, 3);

  run_step.set_step_action(StepInto);
  break_point_hit_count = 0;
  v8::Local<v8::Context> context = env.local();
  foo->Call(context, env->Global(), 0, nullptr).ToLocalChecked();

  // With stepping all break locations are hit.
  CHECK_EQ(4, break_point_hit_count);

  v8::debug::SetDebugDelegate(env->GetIsolate(), nullptr);
  CheckDebuggerUnloaded();
}

TEST(DebugCountLinear) {
  LocalContext env;
  v8::HandleScope scope(env->GetIsolate());

  // Create a function for testing stepping.
  v8::Local<v8::Function> foo =
      CompileFunction(&env, "function foo(){a=1;b=1;c=1;}", "foo");

  // Run foo to allow it to get optimized.
  CompileRun("a=0; b=0; c=0; foo();");

  // Register a debug event listener which just counts.
  DebugEventCounter delegate;
  v8::debug::SetDebugDelegate(env->GetIsolate(), &delegate);

  SetBreakPoint(foo, 3);
  break_point_hit_count = 0;
  v8::Local<v8::Context> context = env.local();
  foo->Call(context, env->Global(), 0, nullptr).ToLocalChecked();

  // Without stepping only active break points are hit.
  CHECK_EQ(1, break_point_hit_count);

  v8::debug::SetDebugDelegate(env->GetIsolate(), nullptr);
  CheckDebuggerUnloaded();
}

// Test of the stepping mechanism for keyed load in a loop.
TEST(DebugStepKeyedLoadLoop) {
  LocalContext env;
  v8::HandleScope scope(env->GetIsolate());

  // Register a debug event listener which steps and counts.
  DebugEventCounter run_step;
  v8::debug::SetDebugDelegate(env->GetIsolate(), &run_step);

  // Create a function for testing stepping of keyed load. The statement 'y=1'
  // is there to have more than one breakable statement in the loop, TODO(315).
  v8::Local<v8::Function> foo = CompileFunction(
      &env,
      "function foo(a) {\n"
      "  var x;\n"
      "  var len = a.length;\n"
      "  for (var i = 0; i < len; i++) {\n"
      "    y = 1;\n"
      "    x = a[i];\n"
      "  }\n"
      "}\n"
      "y=0\n",
      "foo");

  v8::Local<v8::Context> context = env.local();
  // Create array [0,1,2,3,4,5,6,7,8,9]
  v8::Local<v8::Array> a = v8::Array::New(env->GetIsolate(), 10);
  for (int i = 0; i < 10; i++) {
    CHECK(a->Set(context, v8::Number::New(env->GetIsolate(), i),
                 v8::Number::New(env->GetIsolate(), i))
              .FromJust());
  }

  // Call function without any break points to ensure inlining is in place.
  const int kArgc = 1;
  v8::Local<v8::Value> args[kArgc] = {a};
  foo->Call(context, env->Global(), kArgc, args).ToLocalChecked();

  // Set up break point and step through the function.
  SetBreakPoint(foo, 3);
  run_step.set_step_action(StepOver);
  break_point_hit_count = 0;
  foo->Call(context, env->Global(), kArgc, args).ToLocalChecked();

  // With stepping all break locations are hit.
  CHECK_EQ(44, break_point_hit_count);

  v8::debug::SetDebugDelegate(env->GetIsolate(), nullptr);
  CheckDebuggerUnloaded();
}


// Test of the stepping mechanism for keyed store in a loop.
TEST(DebugStepKeyedStoreLoop) {
  LocalContext env;
  v8::HandleScope scope(env->GetIsolate());

  // Register a debug event listener which steps and counts.
  DebugEventCounter run_step;
  v8::debug::SetDebugDelegate(env->GetIsolate(), &run_step);

  // Create a function for testing stepping of keyed store. The statement 'y=1'
  // is there to have more than one breakable statement in the loop, TODO(315).
  v8::Local<v8::Function> foo = CompileFunction(
      &env,
      "function foo(a) {\n"
      "  var len = a.length;\n"
      "  for (var i = 0; i < len; i++) {\n"
      "    y = 1;\n"
      "    a[i] = 42;\n"
      "  }\n"
      "}\n"
      "y=0\n",
      "foo");

  v8::Local<v8::Context> context = env.local();
  // Create array [0,1,2,3,4,5,6,7,8,9]
  v8::Local<v8::Array> a = v8::Array::New(env->GetIsolate(), 10);
  for (int i = 0; i < 10; i++) {
    CHECK(a->Set(context, v8::Number::New(env->GetIsolate(), i),
                 v8::Number::New(env->GetIsolate(), i))
              .FromJust());
  }

  // Call function without any break points to ensure inlining is in place.
  const int kArgc = 1;
  v8::Local<v8::Value> args[kArgc] = {a};
  foo->Call(context, env->Global(), kArgc, args).ToLocalChecked();

  // Set up break point and step through the function.
  SetBreakPoint(foo, 3);
  run_step.set_step_action(StepOver);
  break_point_hit_count = 0;
  foo->Call(context, env->Global(), kArgc, args).ToLocalChecked();

  // With stepping all break locations are hit.
  CHECK_EQ(44, break_point_hit_count);

  v8::debug::SetDebugDelegate(env->GetIsolate(), nullptr);
  CheckDebuggerUnloaded();
}


// Test of the stepping mechanism for named load in a loop.
TEST(DebugStepNamedLoadLoop) {
  LocalContext env;
  v8::HandleScope scope(env->GetIsolate());

  // Register a debug event listener which steps and counts.
  DebugEventCounter run_step;
  v8::debug::SetDebugDelegate(env->GetIsolate(), &run_step);

  v8::Local<v8::Context> context = env.local();
  // Create a function for testing stepping of named load.
  v8::Local<v8::Function> foo = CompileFunction(
      &env,
      "function foo() {\n"
          "  var a = [];\n"
          "  var s = \"\";\n"
          "  for (var i = 0; i < 10; i++) {\n"
          "    var v = new V(i, i + 1);\n"
          "    v.y;\n"
          "    a.length;\n"  // Special case: array length.
          "    s.length;\n"  // Special case: string length.
          "  }\n"
          "}\n"
          "function V(x, y) {\n"
          "  this.x = x;\n"
          "  this.y = y;\n"
          "}\n",
          "foo");

  // Call function without any break points to ensure inlining is in place.
  foo->Call(context, env->Global(), 0, nullptr).ToLocalChecked();

  // Set up break point and step through the function.
  SetBreakPoint(foo, 4);
  run_step.set_step_action(StepOver);
  break_point_hit_count = 0;
  foo->Call(context, env->Global(), 0, nullptr).ToLocalChecked();

  // With stepping all break locations are hit.
  CHECK_EQ(65, break_point_hit_count);

  v8::debug::SetDebugDelegate(env->GetIsolate(), nullptr);
  CheckDebuggerUnloaded();
}


static void DoDebugStepNamedStoreLoop(int expected) {
  LocalContext env;
  v8::HandleScope scope(env->GetIsolate());

  // Register a debug event listener which steps and counts.
  DebugEventCounter run_step;
  v8::debug::SetDebugDelegate(env->GetIsolate(), &run_step);

  // Create a function for testing stepping of named store.
  v8::Local<v8::Context> context = env.local();
  v8::Local<v8::Function> foo = CompileFunction(
      &env,
      "function foo() {\n"
          "  var a = {a:1};\n"
          "  for (var i = 0; i < 10; i++) {\n"
          "    a.a = 2\n"
          "  }\n"
          "}\n",
          "foo");

  // Call function without any break points to ensure inlining is in place.
  foo->Call(context, env->Global(), 0, nullptr).ToLocalChecked();

  // Set up break point and step through the function.
  SetBreakPoint(foo, 3);
  run_step.set_step_action(StepOver);
  break_point_hit_count = 0;
  foo->Call(context, env->Global(), 0, nullptr).ToLocalChecked();

  // With stepping all expected break locations are hit.
  CHECK_EQ(expected, break_point_hit_count);

  v8::debug::SetDebugDelegate(env->GetIsolate(), nullptr);
  CheckDebuggerUnloaded();
}


// Test of the stepping mechanism for named load in a loop.
TEST(DebugStepNamedStoreLoop) { DoDebugStepNamedStoreLoop(34); }

// Test the stepping mechanism with different ICs.
TEST(DebugStepLinearMixedICs) {
  LocalContext env;
  v8::HandleScope scope(env->GetIsolate());

  // Register a debug event listener which steps and counts.
  DebugEventCounter run_step;
  v8::debug::SetDebugDelegate(env->GetIsolate(), &run_step);

  v8::Local<v8::Context> context = env.local();
  // Create a function for testing stepping.
  v8::Local<v8::Function> foo = CompileFunction(&env,
      "function bar() {};"
      "function foo() {"
      "  var x;"
      "  var index='name';"
      "  var y = {};"
      "  a=1;b=2;x=a;y[index]=3;x=y[index];bar();}", "foo");

  // Run functions to allow them to get optimized.
  CompileRun("a=0; b=0; bar(); foo();");

  SetBreakPoint(foo, 0);

  run_step.set_step_action(StepInto);
  break_point_hit_count = 0;
  foo->Call(context, env->Global(), 0, nullptr).ToLocalChecked();

  // With stepping all break locations are hit.
  CHECK_EQ(10, break_point_hit_count);

  v8::debug::SetDebugDelegate(env->GetIsolate(), nullptr);
  CheckDebuggerUnloaded();
}

TEST(DebugCountLinearMixedICs) {
  LocalContext env;
  v8::HandleScope scope(env->GetIsolate());

  v8::Local<v8::Context> context = env.local();
  // Create a function for testing stepping.
  v8::Local<v8::Function> foo =
      CompileFunction(&env,
                      "function bar() {};"
                      "function foo() {"
                      "  var x;"
                      "  var index='name';"
                      "  var y = {};"
                      "  a=1;b=2;x=a;y[index]=3;x=y[index];bar();}",
                      "foo");

  // Run functions to allow them to get optimized.
  CompileRun("a=0; b=0; bar(); foo();");

  // Register a debug event listener which just counts.
  DebugEventCounter delegate;
  v8::debug::SetDebugDelegate(env->GetIsolate(), &delegate);

  SetBreakPoint(foo, 0);
  break_point_hit_count = 0;
  foo->Call(context, env->Global(), 0, nullptr).ToLocalChecked();

  // Without stepping only active break points are hit.
  CHECK_EQ(1, break_point_hit_count);

  v8::debug::SetDebugDelegate(env->GetIsolate(), nullptr);
  CheckDebuggerUnloaded();
}

TEST(DebugStepDeclarations) {
  LocalContext env;
  v8::HandleScope scope(env->GetIsolate());

  // Register a debug event listener which steps and counts.
  DebugEventCounter run_step;
  v8::debug::SetDebugDelegate(env->GetIsolate(), &run_step);

  v8::Local<v8::Context> context = env.local();
  // Create a function for testing stepping. Run it to allow it to get
  // optimized.
  const char* src = "function foo() { "
                    "  var a;"
                    "  var b = 1;"
                    "  var c = foo;"
                    "  var d = Math.floor;"
                    "  var e = b + d(1.2);"
                    "}"
                    "foo()";
  v8::Local<v8::Function> foo = CompileFunction(&env, src, "foo");

  SetBreakPoint(foo, 0);

  // Stepping through the declarations.
  run_step.set_step_action(StepInto);
  break_point_hit_count = 0;
  foo->Call(context, env->Global(), 0, nullptr).ToLocalChecked();
  CHECK_EQ(5, break_point_hit_count);

  // Get rid of the debug event listener.
  v8::debug::SetDebugDelegate(env->GetIsolate(), nullptr);
  CheckDebuggerUnloaded();
}


TEST(DebugStepLocals) {
  LocalContext env;
  v8::HandleScope scope(env->GetIsolate());

  // Register a debug event listener which steps and counts.
  DebugEventCounter run_step;
  v8::debug::SetDebugDelegate(env->GetIsolate(), &run_step);

  v8::Local<v8::Context> context = env.local();
  // Create a function for testing stepping. Run it to allow it to get
  // optimized.
  const char* src = "function foo() { "
                    "  var a,b;"
                    "  a = 1;"
                    "  b = a + 2;"
                    "  b = 1 + 2 + 3;"
                    "  a = Math.floor(b);"
                    "}"
                    "foo()";
  v8::Local<v8::Function> foo = CompileFunction(&env, src, "foo");

  SetBreakPoint(foo, 0);

  // Stepping through the declarations.
  run_step.set_step_action(StepInto);
  break_point_hit_count = 0;
  foo->Call(context, env->Global(), 0, nullptr).ToLocalChecked();
  CHECK_EQ(5, break_point_hit_count);

  // Get rid of the debug event listener.
  v8::debug::SetDebugDelegate(env->GetIsolate(), nullptr);
  CheckDebuggerUnloaded();
}


TEST(DebugStepIf) {
  LocalContext env;
  v8::Isolate* isolate = env->GetIsolate();
  v8::HandleScope scope(isolate);

  // Register a debug event listener which steps and counts.
  DebugEventCounter run_step;
  v8::debug::SetDebugDelegate(env->GetIsolate(), &run_step);

  v8::Local<v8::Context> context = env.local();
  // Create a function for testing stepping. Run it to allow it to get
  // optimized.
  const int argc = 1;
  const char* src = "function foo(x) { "
                    "  a = 1;"
                    "  if (x) {"
                    "    b = 1;"
                    "  } else {"
                    "    c = 1;"
                    "    d = 1;"
                    "  }"
                    "}"
                    "a=0; b=0; c=0; d=0; foo()";
  v8::Local<v8::Function> foo = CompileFunction(&env, src, "foo");
  SetBreakPoint(foo, 0);

  // Stepping through the true part.
  run_step.set_step_action(StepInto);
  break_point_hit_count = 0;
  v8::Local<v8::Value> argv_true[argc] = {v8::True(isolate)};
  foo->Call(context, env->Global(), argc, argv_true).ToLocalChecked();
  CHECK_EQ(4, break_point_hit_count);

  // Stepping through the false part.
  run_step.set_step_action(StepInto);
  break_point_hit_count = 0;
  v8::Local<v8::Value> argv_false[argc] = {v8::False(isolate)};
  foo->Call(context, env->Global(), argc, argv_false).ToLocalChecked();
  CHECK_EQ(5, break_point_hit_count);

  // Get rid of the debug event listener.
  v8::debug::SetDebugDelegate(env->GetIsolate(), nullptr);
  CheckDebuggerUnloaded();
}


TEST(DebugStepSwitch) {
  LocalContext env;
  v8::Isolate* isolate = env->GetIsolate();
  v8::HandleScope scope(isolate);

  // Register a debug event listener which steps and counts.
  DebugEventCounter run_step;
  v8::debug::SetDebugDelegate(env->GetIsolate(), &run_step);

  v8::Local<v8::Context> context = env.local();
  // Create a function for testing stepping. Run it to allow it to get
  // optimized.
  const int argc = 1;
  const char* src = "function foo(x) { "
                    "  a = 1;"
                    "  switch (x) {"
                    "    case 1:"
                    "      b = 1;"
                    "    case 2:"
                    "      c = 1;"
                    "      break;"
                    "    case 3:"
                    "      d = 1;"
                    "      e = 1;"
                    "      f = 1;"
                    "      break;"
                    "  }"
                    "}"
                    "a=0; b=0; c=0; d=0; e=0; f=0; foo()";
  v8::Local<v8::Function> foo = CompileFunction(&env, src, "foo");
  SetBreakPoint(foo, 0);

  // One case with fall-through.
  run_step.set_step_action(StepInto);
  break_point_hit_count = 0;
  v8::Local<v8::Value> argv_1[argc] = {v8::Number::New(isolate, 1)};
  foo->Call(context, env->Global(), argc, argv_1).ToLocalChecked();
  CHECK_EQ(6, break_point_hit_count);

  // Another case.
  run_step.set_step_action(StepInto);
  break_point_hit_count = 0;
  v8::Local<v8::Value> argv_2[argc] = {v8::Number::New(isolate, 2)};
  foo->Call(context, env->Global(), argc, argv_2).ToLocalChecked();
  CHECK_EQ(5, break_point_hit_count);

  // Last case.
  run_step.set_step_action(StepInto);
  break_point_hit_count = 0;
  v8::Local<v8::Value> argv_3[argc] = {v8::Number::New(isolate, 3)};
  foo->Call(context, env->Global(), argc, argv_3).ToLocalChecked();
  CHECK_EQ(7, break_point_hit_count);

  // Get rid of the debug event listener.
  v8::debug::SetDebugDelegate(env->GetIsolate(), nullptr);
  CheckDebuggerUnloaded();
}


TEST(DebugStepWhile) {
  LocalContext env;
  v8::Isolate* isolate = env->GetIsolate();
  v8::HandleScope scope(isolate);

  // Register a debug event listener which steps and counts.
  DebugEventCounter run_step;
  v8::debug::SetDebugDelegate(env->GetIsolate(), &run_step);

  v8::Local<v8::Context> context = env.local();
  // Create a function for testing stepping. Run it to allow it to get
  // optimized.
  const int argc = 1;
  const char* src = "function foo(x) { "
                    "  var a = 0;"
                    "  while (a < x) {"
                    "    a++;"
                    "  }"
                    "}"
                    "foo()";
  v8::Local<v8::Function> foo = CompileFunction(&env, src, "foo");
  SetBreakPoint(foo, 8);  // "var a = 0;"

  // Looping 0 times.  We still should break at the while-condition once.
  run_step.set_step_action(StepInto);
  break_point_hit_count = 0;
  v8::Local<v8::Value> argv_0[argc] = {v8::Number::New(isolate, 0)};
  foo->Call(context, env->Global(), argc, argv_0).ToLocalChecked();
  CHECK_EQ(3, break_point_hit_count);

  // Looping 10 times.
  run_step.set_step_action(StepInto);
  break_point_hit_count = 0;
  v8::Local<v8::Value> argv_10[argc] = {v8::Number::New(isolate, 10)};
  foo->Call(context, env->Global(), argc, argv_10).ToLocalChecked();
  CHECK_EQ(23, break_point_hit_count);

  // Looping 100 times.
  run_step.set_step_action(StepInto);
  break_point_hit_count = 0;
  v8::Local<v8::Value> argv_100[argc] = {v8::Number::New(isolate, 100)};
  foo->Call(context, env->Global(), argc, argv_100).ToLocalChecked();
  CHECK_EQ(203, break_point_hit_count);

  // Get rid of the debug event listener.
  v8::debug::SetDebugDelegate(env->GetIsolate(), nullptr);
  CheckDebuggerUnloaded();
}


TEST(DebugStepDoWhile) {
  LocalContext env;
  v8::Isolate* isolate = env->GetIsolate();
  v8::HandleScope scope(isolate);

  // Register a debug event listener which steps and counts.
  DebugEventCounter run_step;
  v8::debug::SetDebugDelegate(env->GetIsolate(), &run_step);

  v8::Local<v8::Context> context = env.local();
  // Create a function for testing stepping. Run it to allow it to get
  // optimized.
  const int argc = 1;
  const char* src = "function foo(x) { "
                    "  var a = 0;"
                    "  do {"
                    "    a++;"
                    "  } while (a < x)"
                    "}"
                    "foo()";
  v8::Local<v8::Function> foo = CompileFunction(&env, src, "foo");
  SetBreakPoint(foo, 8);  // "var a = 0;"

  // Looping 0 times.
  run_step.set_step_action(StepInto);
  break_point_hit_count = 0;
  v8::Local<v8::Value> argv_0[argc] = {v8::Number::New(isolate, 0)};
  foo->Call(context, env->Global(), argc, argv_0).ToLocalChecked();
  CHECK_EQ(4, break_point_hit_count);

  // Looping 10 times.
  run_step.set_step_action(StepInto);
  break_point_hit_count = 0;
  v8::Local<v8::Value> argv_10[argc] = {v8::Number::New(isolate, 10)};
  foo->Call(context, env->Global(), argc, argv_10).ToLocalChecked();
  CHECK_EQ(22, break_point_hit_count);

  // Looping 100 times.
  run_step.set_step_action(StepInto);
  break_point_hit_count = 0;
  v8::Local<v8::Value> argv_100[argc] = {v8::Number::New(isolate, 100)};
  foo->Call(context, env->Global(), argc, argv_100).ToLocalChecked();
  CHECK_EQ(202, break_point_hit_count);

  // Get rid of the debug event listener.
  v8::debug::SetDebugDelegate(env->GetIsolate(), nullptr);
  CheckDebuggerUnloaded();
}


TEST(DebugStepFor) {
  LocalContext env;
  v8::Isolate* isolate = env->GetIsolate();
  v8::HandleScope scope(isolate);

  // Register a debug event listener which steps and counts.
  DebugEventCounter run_step;
  v8::debug::SetDebugDelegate(env->GetIsolate(), &run_step);

  v8::Local<v8::Context> context = env.local();
  // Create a function for testing stepping. Run it to allow it to get
  // optimized.
  const int argc = 1;
  const char* src = "function foo(x) { "
                    "  a = 1;"
                    "  for (i = 0; i < x; i++) {"
                    "    b = 1;"
                    "  }"
                    "}"
                    "a=0; b=0; i=0; foo()";
  v8::Local<v8::Function> foo = CompileFunction(&env, src, "foo");

  SetBreakPoint(foo, 8);  // "a = 1;"

  // Looping 0 times.
  run_step.set_step_action(StepInto);
  break_point_hit_count = 0;
  v8::Local<v8::Value> argv_0[argc] = {v8::Number::New(isolate, 0)};
  foo->Call(context, env->Global(), argc, argv_0).ToLocalChecked();
  CHECK_EQ(4, break_point_hit_count);

  // Looping 10 times.
  run_step.set_step_action(StepInto);
  break_point_hit_count = 0;
  v8::Local<v8::Value> argv_10[argc] = {v8::Number::New(isolate, 10)};
  foo->Call(context, env->Global(), argc, argv_10).ToLocalChecked();
  CHECK_EQ(34, break_point_hit_count);

  // Looping 100 times.
  run_step.set_step_action(StepInto);
  break_point_hit_count = 0;
  v8::Local<v8::Value> argv_100[argc] = {v8::Number::New(isolate, 100)};
  foo->Call(context, env->Global(), argc, argv_100).ToLocalChecked();
  CHECK_EQ(304, break_point_hit_count);

  // Get rid of the debug event listener.
  v8::debug::SetDebugDelegate(env->GetIsolate(), nullptr);
  CheckDebuggerUnloaded();
}


TEST(DebugStepForContinue) {
  LocalContext env;
  v8::Isolate* isolate = env->GetIsolate();
  v8::HandleScope scope(isolate);

  // Register a debug event listener which steps and counts.
  DebugEventCounter run_step;
  v8::debug::SetDebugDelegate(env->GetIsolate(), &run_step);

  v8::Local<v8::Context> context = env.local();
  // Create a function for testing stepping. Run it to allow it to get
  // optimized.
  const int argc = 1;
  const char* src = "function foo(x) { "
                    "  var a = 0;"
                    "  var b = 0;"
                    "  var c = 0;"
                    "  for (var i = 0; i < x; i++) {"
                    "    a++;"
                    "    if (a % 2 == 0) continue;"
                    "    b++;"
                    "    c++;"
                    "  }"
                    "  return b;"
                    "}"
                    "foo()";
  v8::Local<v8::Function> foo = CompileFunction(&env, src, "foo");
  v8::Local<v8::Value> result;
  SetBreakPoint(foo, 8);  // "var a = 0;"

  // Each loop generates 4 or 5 steps depending on whether a is equal.

  // Looping 10 times.
  run_step.set_step_action(StepInto);
  break_point_hit_count = 0;
  v8::Local<v8::Value> argv_10[argc] = {v8::Number::New(isolate, 10)};
  result = foo->Call(context, env->Global(), argc, argv_10).ToLocalChecked();
  CHECK_EQ(5, result->Int32Value(context).FromJust());
  CHECK_EQ(62, break_point_hit_count);

  // Looping 100 times.
  run_step.set_step_action(StepInto);
  break_point_hit_count = 0;
  v8::Local<v8::Value> argv_100[argc] = {v8::Number::New(isolate, 100)};
  result = foo->Call(context, env->Global(), argc, argv_100).ToLocalChecked();
  CHECK_EQ(50, result->Int32Value(context).FromJust());
  CHECK_EQ(557, break_point_hit_count);

  // Get rid of the debug event listener.
  v8::debug::SetDebugDelegate(env->GetIsolate(), nullptr);
  CheckDebuggerUnloaded();
}


TEST(DebugStepForBreak) {
  LocalContext env;
  v8::Isolate* isolate = env->GetIsolate();
  v8::HandleScope scope(isolate);

  // Register a debug event listener which steps and counts.
  DebugEventCounter run_step;
  v8::debug::SetDebugDelegate(env->GetIsolate(), &run_step);

  v8::Local<v8::Context> context = env.local();
  // Create a function for testing stepping. Run it to allow it to get
  // optimized.
  const int argc = 1;
  const char* src = "function foo(x) { "
                    "  var a = 0;"
                    "  var b = 0;"
                    "  var c = 0;"
                    "  for (var i = 0; i < 1000; i++) {"
                    "    a++;"
                    "    if (a == x) break;"
                    "    b++;"
                    "    c++;"
                    "  }"
                    "  return b;"
                    "}"
                    "foo()";
  v8::Local<v8::Function> foo = CompileFunction(&env, src, "foo");
  v8::Local<v8::Value> result;
  SetBreakPoint(foo, 8);  // "var a = 0;"

  // Each loop generates 5 steps except for the last (when break is executed)
  // which only generates 4.

  // Looping 10 times.
  run_step.set_step_action(StepInto);
  break_point_hit_count = 0;
  v8::Local<v8::Value> argv_10[argc] = {v8::Number::New(isolate, 10)};
  result = foo->Call(context, env->Global(), argc, argv_10).ToLocalChecked();
  CHECK_EQ(9, result->Int32Value(context).FromJust());
  CHECK_EQ(64, break_point_hit_count);

  // Looping 100 times.
  run_step.set_step_action(StepInto);
  break_point_hit_count = 0;
  v8::Local<v8::Value> argv_100[argc] = {v8::Number::New(isolate, 100)};
  result = foo->Call(context, env->Global(), argc, argv_100).ToLocalChecked();
  CHECK_EQ(99, result->Int32Value(context).FromJust());
  CHECK_EQ(604, break_point_hit_count);

  // Get rid of the debug event listener.
  v8::debug::SetDebugDelegate(env->GetIsolate(), nullptr);
  CheckDebuggerUnloaded();
}


TEST(DebugStepForIn) {
  LocalContext env;
  v8::HandleScope scope(env->GetIsolate());

  // Register a debug event listener which steps and counts.
  DebugEventCounter run_step;
  v8::debug::SetDebugDelegate(env->GetIsolate(), &run_step);

  v8::Local<v8::Context> context = env.local();
  // Create a function for testing stepping. Run it to allow it to get
  // optimized.
  v8::Local<v8::Function> foo;
  const char* src_1 = "function foo() { "
                      "  var a = [1, 2];"
                      "  for (x in a) {"
                      "    b = 0;"
                      "  }"
                      "}"
                      "foo()";
  foo = CompileFunction(&env, src_1, "foo");
  SetBreakPoint(foo, 0);  // "var a = ..."

  run_step.set_step_action(StepInto);
  break_point_hit_count = 0;
  foo->Call(context, env->Global(), 0, nullptr).ToLocalChecked();
  CHECK_EQ(8, break_point_hit_count);

  // Create a function for testing stepping. Run it to allow it to get
  // optimized.
  const char* src_2 = "function foo() { "
                      "  var a = {a:[1, 2, 3]};"
                      "  for (x in a.a) {"
                      "    b = 0;"
                      "  }"
                      "}"
                      "foo()";
  foo = CompileFunction(&env, src_2, "foo");
  SetBreakPoint(foo, 0);  // "var a = ..."

  run_step.set_step_action(StepInto);
  break_point_hit_count = 0;
  foo->Call(context, env->Global(), 0, nullptr).ToLocalChecked();
  CHECK_EQ(10, break_point_hit_count);

  // Get rid of the debug event listener.
  v8::debug::SetDebugDelegate(env->GetIsolate(), nullptr);
  CheckDebuggerUnloaded();
}


TEST(DebugStepWith) {
  LocalContext env;
  v8::HandleScope scope(env->GetIsolate());

  // Register a debug event listener which steps and counts.
  DebugEventCounter run_step;
  v8::debug::SetDebugDelegate(env->GetIsolate(), &run_step);

  v8::Local<v8::Context> context = env.local();
  // Create a function for testing stepping. Run it to allow it to get
  // optimized.
  const char* src = "function foo(x) { "
                    "  var a = {};"
                    "  with (a) {}"
                    "  with (b) {}"
                    "}"
                    "foo()";
  CHECK(env->Global()
            ->Set(context, v8_str(env->GetIsolate(), "b"),
                  v8::Object::New(env->GetIsolate()))
            .FromJust());
  v8::Local<v8::Function> foo = CompileFunction(&env, src, "foo");
  SetBreakPoint(foo, 8);  // "var a = {};"

  run_step.set_step_action(StepInto);
  break_point_hit_count = 0;
  foo->Call(context, env->Global(), 0, nullptr).ToLocalChecked();
  CHECK_EQ(4, break_point_hit_count);

  // Get rid of the debug event listener.
  v8::debug::SetDebugDelegate(env->GetIsolate(), nullptr);
  CheckDebuggerUnloaded();
}


TEST(DebugConditional) {
  LocalContext env;
  v8::Isolate* isolate = env->GetIsolate();
  v8::HandleScope scope(isolate);

  // Register a debug event listener which steps and counts.
  DebugEventCounter run_step;
  v8::debug::SetDebugDelegate(env->GetIsolate(), &run_step);

  v8::Local<v8::Context> context = env.local();
  // Create a function for testing stepping. Run it to allow it to get
  // optimized.
  const char* src =
      "function foo(x) { "
      "  return x ? 1 : 2;"
      "}"
      "foo()";
  v8::Local<v8::Function> foo = CompileFunction(&env, src, "foo");
  SetBreakPoint(foo, 0);  // "var a;"

  run_step.set_step_action(StepInto);
  break_point_hit_count = 0;
  foo->Call(context, env->Global(), 0, nullptr).ToLocalChecked();
  CHECK_EQ(2, break_point_hit_count);

  run_step.set_step_action(StepInto);
  break_point_hit_count = 0;
  const int argc = 1;
  v8::Local<v8::Value> argv_true[argc] = {v8::True(isolate)};
  foo->Call(context, env->Global(), argc, argv_true).ToLocalChecked();
  CHECK_EQ(2, break_point_hit_count);

  // Get rid of the debug event listener.
  v8::debug::SetDebugDelegate(env->GetIsolate(), nullptr);
  CheckDebuggerUnloaded();
}

// Test that step in does not step into native functions.
TEST(DebugStepNatives) {
  LocalContext env;
  v8::HandleScope scope(env->GetIsolate());

  // Create a function for testing stepping.
  v8::Local<v8::Function> foo =
      CompileFunction(&env, "function foo(){debugger;Math.sin(1);}", "foo");

  // Register a debug event listener which steps and counts.
  DebugEventCounter run_step;
  v8::debug::SetDebugDelegate(env->GetIsolate(), &run_step);

  v8::Local<v8::Context> context = env.local();
  run_step.set_step_action(StepInto);
  break_point_hit_count = 0;
  foo->Call(context, env->Global(), 0, nullptr).ToLocalChecked();

  // With stepping all break locations are hit.
  CHECK_EQ(3, break_point_hit_count);

  v8::debug::SetDebugDelegate(env->GetIsolate(), nullptr);
  CheckDebuggerUnloaded();
}

TEST(DebugCountNatives) {
  LocalContext env;
  v8::HandleScope scope(env->GetIsolate());

  // Create a function for testing stepping.
  v8::Local<v8::Function> foo =
      CompileFunction(&env, "function foo(){debugger;Math.sin(1);}", "foo");

  v8::Local<v8::Context> context = env.local();

  // Register a debug event listener which just counts.
  DebugEventCounter delegate;
  v8::debug::SetDebugDelegate(env->GetIsolate(), &delegate);

  break_point_hit_count = 0;
  foo->Call(context, env->Global(), 0, nullptr).ToLocalChecked();

  // Without stepping only active break points are hit.
  CHECK_EQ(1, break_point_hit_count);

  v8::debug::SetDebugDelegate(env->GetIsolate(), nullptr);
  CheckDebuggerUnloaded();
}

// Test that step in works with function.apply.
TEST(DebugStepFunctionApply) {
  LocalContext env;
  v8::HandleScope scope(env->GetIsolate());

  // Create a function for testing stepping.
  v8::Local<v8::Function> foo =
      CompileFunction(&env,
                      "function bar(x, y, z) { if (x == 1) { a = y; b = z; } }"
                      "function foo(){ debugger; bar.apply(this, [1,2,3]); }",
                      "foo");

  // Register a debug event listener which steps and counts.
  DebugEventCounter run_step;
  v8::debug::SetDebugDelegate(env->GetIsolate(), &run_step);

  v8::Local<v8::Context> context = env.local();
  run_step.set_step_action(StepInto);
  break_point_hit_count = 0;
  foo->Call(context, env->Global(), 0, nullptr).ToLocalChecked();

  // With stepping all break locations are hit.
  CHECK_EQ(7, break_point_hit_count);

  v8::debug::SetDebugDelegate(env->GetIsolate(), nullptr);
  CheckDebuggerUnloaded();
}

// Test that step in works with function.apply.
TEST(DebugCountFunctionApply) {
  LocalContext env;
  v8::HandleScope scope(env->GetIsolate());

  // Create a function for testing stepping.
  v8::Local<v8::Function> foo =
      CompileFunction(&env,
                      "function bar(x, y, z) { if (x == 1) { a = y; b = z; } }"
                      "function foo(){ debugger; bar.apply(this, [1,2,3]); }",
                      "foo");

  // Register a debug event listener which just counts.
  DebugEventCounter delegate;
  v8::debug::SetDebugDelegate(env->GetIsolate(), &delegate);

  break_point_hit_count = 0;
  v8::Local<v8::Context> context = env.local();
  foo->Call(context, env->Global(), 0, nullptr).ToLocalChecked();

  // Without stepping only the debugger statement is hit.
  CHECK_EQ(1, break_point_hit_count);

  v8::debug::SetDebugDelegate(env->GetIsolate(), nullptr);
  CheckDebuggerUnloaded();
}

// Test that step in works with function.call.
TEST(DebugStepFunctionCall) {
  LocalContext env;
  v8::Isolate* isolate = env->GetIsolate();
  v8::HandleScope scope(isolate);

  v8::Local<v8::Context> context = env.local();
  // Create a function for testing stepping.
  v8::Local<v8::Function> foo = CompileFunction(
      &env,
      "function bar(x, y, z) { if (x == 1) { a = y; b = z; } }"
      "function foo(a){ debugger;"
      "                 if (a) {"
      "                   bar.call(this, 1, 2, 3);"
      "                 } else {"
      "                   bar.call(this, 0);"
      "                 }"
      "}",
      "foo");

  // Register a debug event listener which steps and counts.
  DebugEventCounter run_step;
  v8::debug::SetDebugDelegate(env->GetIsolate(), &run_step);
  run_step.set_step_action(StepInto);

  // Check stepping where the if condition in bar is false.
  break_point_hit_count = 0;
  foo->Call(context, env->Global(), 0, nullptr).ToLocalChecked();
  CHECK_EQ(6, break_point_hit_count);

  // Check stepping where the if condition in bar is true.
  break_point_hit_count = 0;
  const int argc = 1;
  v8::Local<v8::Value> argv[argc] = {v8::True(isolate)};
  foo->Call(context, env->Global(), argc, argv).ToLocalChecked();
  CHECK_EQ(8, break_point_hit_count);

  v8::debug::SetDebugDelegate(env->GetIsolate(), nullptr);
  CheckDebuggerUnloaded();
}

TEST(DebugCountFunctionCall) {
  LocalContext env;
  v8::Isolate* isolate = env->GetIsolate();
  v8::HandleScope scope(isolate);

  v8::Local<v8::Context> context = env.local();
  // Create a function for testing stepping.
  v8::Local<v8::Function> foo =
      CompileFunction(&env,
                      "function bar(x, y, z) { if (x == 1) { a = y; b = z; } }"
                      "function foo(a){ debugger;"
                      "                 if (a) {"
                      "                   bar.call(this, 1, 2, 3);"
                      "                 } else {"
                      "                   bar.call(this, 0);"
                      "                 }"
                      "}",
                      "foo");

  // Register a debug event listener which just counts.
  DebugEventCounter delegate;
  v8::debug::SetDebugDelegate(env->GetIsolate(), &delegate);

  break_point_hit_count = 0;
  foo->Call(context, env->Global(), 0, nullptr).ToLocalChecked();

  // Without stepping only the debugger statement is hit.
  CHECK_EQ(1, break_point_hit_count);

  v8::debug::SetDebugDelegate(isolate, nullptr);
  CheckDebuggerUnloaded();
}

// Test that step in works with Function.call.apply.
TEST(DebugStepFunctionCallApply) {
  LocalContext env;
  v8::Isolate* isolate = env->GetIsolate();
  v8::HandleScope scope(isolate);

  v8::Local<v8::Context> context = env.local();
  // Create a function for testing stepping.
  v8::Local<v8::Function> foo =
      CompileFunction(&env,
                      "function bar() { }"
                      "function foo(){ debugger;"
                      "                Function.call.apply(bar);"
                      "                Function.call.apply(Function.call, "
                      "[Function.call, bar]);"
                      "}",
                      "foo");

  // Register a debug event listener which steps and counts.
  DebugEventCounter run_step;
  v8::debug::SetDebugDelegate(env->GetIsolate(), &run_step);
  run_step.set_step_action(StepInto);

  break_point_hit_count = 0;
  foo->Call(context, env->Global(), 0, nullptr).ToLocalChecked();
  CHECK_EQ(6, break_point_hit_count);

  v8::debug::SetDebugDelegate(env->GetIsolate(), nullptr);
  CheckDebuggerUnloaded();
}

TEST(DebugCountFunctionCallApply) {
  LocalContext env;
  v8::Isolate* isolate = env->GetIsolate();
  v8::HandleScope scope(isolate);

  v8::Local<v8::Context> context = env.local();
  // Create a function for testing stepping.
  v8::Local<v8::Function> foo =
      CompileFunction(&env,
                      "function bar() { }"
                      "function foo(){ debugger;"
                      "                Function.call.apply(bar);"
                      "                Function.call.apply(Function.call, "
                      "[Function.call, bar]);"
                      "}",
                      "foo");

  // Register a debug event listener which just counts.
  DebugEventCounter delegate;
  v8::debug::SetDebugDelegate(env->GetIsolate(), &delegate);

  break_point_hit_count = 0;
  foo->Call(context, env->Global(), 0, nullptr).ToLocalChecked();

  // Without stepping only the debugger statement is hit.
  CHECK_EQ(1, break_point_hit_count);

  v8::debug::SetDebugDelegate(isolate, nullptr);
  CheckDebuggerUnloaded();
}

// Tests that breakpoint will be hit if it's set in script.
TEST(PauseInScript) {
  LocalContext env;
  v8::HandleScope scope(env->GetIsolate());
  i::Isolate* isolate = reinterpret_cast<i::Isolate*>(env->GetIsolate());

  // Register a debug event listener which counts.
  DebugEventCounter event_counter;
  v8::debug::SetDebugDelegate(env->GetIsolate(), &event_counter);

  v8::Local<v8::Context> context = env.local();
  // Create a script that returns a function.
  const char* src = "(function (evt) {})";
  const char* script_name = "StepInHandlerTest";

  v8::ScriptOrigin origin(v8_str(env->GetIsolate(), script_name));
  v8::Local<v8::Script> script =
      v8::Script::Compile(context, v8_str(env->GetIsolate(), src), &origin)
          .ToLocalChecked();

  // Set breakpoint in the script.
  i::Handle<i::Script> i_script(
      i::Cast<i::Script>(
          v8::Utils::OpenDirectHandle(*script)->shared()->script()),
      isolate);
  i::DirectHandle<i::String> condition = isolate->factory()->empty_string();
  int position = 0;
  int id;
  isolate->debug()->SetBreakPointForScript(i_script, condition, &position, &id);
  break_point_hit_count = 0;

  v8::Local<v8::Value> r = script->Run(context).ToLocalChecked();

  CHECK(r->IsFunction());
  CHECK_EQ(1, break_point_hit_count);

  // Get rid of the debug delegate.
  v8::debug::SetDebugDelegate(env->GetIsolate(), nullptr);
  CheckDebuggerUnloaded();
}

int message_callback_count = 0;

TEST(DebugBreak) {
  i::v8_flags.stress_compaction = false;
#ifdef VERIFY_HEAP
  i::v8_flags.verify_heap = true;
#endif
  LocalContext env;
  v8::Isolate* isolate = env->GetIsolate();
  v8::HandleScope scope(isolate);

  // Register a debug event listener which sets the break flag and counts.
  DebugEventBreak delegate;
  v8::debug::SetDebugDelegate(isolate, &delegate);

  v8::Local<v8::Context> context = env.local();
  // Create a function for testing stepping.
  const char* src = "function f0() {}"
                    "function f1(x1) {}"
                    "function f2(x1,x2) {}"
                    "function f3(x1,x2,x3) {}";
  v8::Local<v8::Function> f0 = CompileFunction(&env, src, "f0");
  v8::Local<v8::Function> f1 = CompileFunction(&env, src, "f1");
  v8::Local<v8::Function> f2 = CompileFunction(&env, src, "f2");
  v8::Local<v8::Function> f3 = CompileFunction(&env, src, "f3");

  // Call the function to make sure it is compiled.
  v8::Local<v8::Value> argv[] = {
      v8::Number::New(isolate, 1), v8::Number::New(isolate, 1),
      v8::Number::New(isolate, 1), v8::Number::New(isolate, 1)};

  // Call all functions to make sure that they are compiled.
  f0->Call(context, env->Global(), 0, nullptr).ToLocalChecked();
  f1->Call(context, env->Global(), 0, nullptr).ToLocalChecked();
  f2->Call(context, env->Global(), 0, nullptr).ToLocalChecked();
  f3->Call(context, env->Global(), 0, nullptr).ToLocalChecked();

  // Set the debug break flag.
  v8::debug::SetBreakOnNextFunctionCall(env->GetIsolate());

  // Call all functions with different argument count.
  break_point_hit_count = 0;
  for (unsigned int i = 0; i < arraysize(argv); i++) {
    f0->Call(context, env->Global(), i, argv).ToLocalChecked();
    f1->Call(context, env->Global(), i, argv).ToLocalChecked();
    f2->Call(context, env->Global(), i, argv).ToLocalChecked();
    f3->Call(context, env->Global(), i, argv).ToLocalChecked();
  }

  // One break for each function called.
  CHECK_EQ(4 * arraysize(argv), break_point_hit_count);

  // Get rid of the debug event listener.
  v8::debug::SetDebugDelegate(env->GetIsolate(), nullptr);
  CheckDebuggerUnloaded();
}

class DebugScopingListener : public v8::debug::DebugDelegate {
 public:
  void ExceptionThrown(v8::Local<v8::Context> paused_context,
                       v8::Local<v8::Value> exception,
                       v8::Local<v8::Value> promise, bool is_uncaught,
                       v8::debug::ExceptionType exception_type) override {
    break_count_++;
    auto stack_traces =
        v8::debug::StackTraceIterator::Create(CcTest::isolate());
    v8::debug::Location location = stack_traces->GetSourceLocation();
    CHECK_EQ(26, location.GetColumnNumber());
    CHECK_EQ(0, location.GetLineNumber());

    auto scopes = stack_traces->GetScopeIterator();
    CHECK_EQ(v8::debug::ScopeIterator::ScopeTypeWith, scopes->GetType());
    CHECK_EQ(19, scopes->GetStartLocation().GetColumnNumber());
    CHECK_EQ(31, scopes->GetEndLocation().GetColumnNumber());

    scopes->Advance();
    CHECK_EQ(v8::debug::ScopeIterator::ScopeTypeLocal, scopes->GetType());
    CHECK_EQ(0, scopes->GetStartLocation().GetColumnNumber());
    CHECK_EQ(68, scopes->GetEndLocation().GetColumnNumber());

    scopes->Advance();
    CHECK_EQ(v8::debug::ScopeIterator::ScopeTypeGlobal, scopes->GetType());

    scopes->Advance();
    CHECK(scopes->Done());
  }
  unsigned break_count() const { return break_count_; }

 private:
  unsigned break_count_ = 0;
};

TEST(DebugBreakInWrappedScript) {
  i::v8_flags.stress_compaction = false;
#ifdef VERIFY_HEAP
  i::v8_flags.verify_heap = true;
#endif
  LocalContext env;
  v8::Isolate* isolate = env->GetIsolate();
  v8::HandleScope scope(isolate);

  // Register a debug event listener which sets the break flag and counts.
  DebugScopingListener delegate;
  v8::debug::SetDebugDelegate(isolate, &delegate);

  static const char* source =
      //   0         1         2         3         4         5         6 7
      "try { with({o : []}){ o[0](); } } catch (e) { return e.toString(); }";
  static const char* expect = "TypeError: o[0] is not a function";

  // For this test, we want to break on uncaught exceptions:
  ChangeBreakOnException(isolate, true, true);

  {
    v8::ScriptCompiler::Source script_source(v8_str(source));
    v8::Local<v8::Function> fun =
        v8::ScriptCompiler::CompileFunction(env.local(), &script_source)
            .ToLocalChecked();
    v8::Local<v8::Value> result =
        fun->Call(env.local(), env->Global(), 0, nullptr).ToLocalChecked();
    CHECK(result->IsString());
    CHECK(v8::Local<v8::String>::Cast(result)
              ->Equals(env.local(), v8_str(expect))
              .FromJust());
  }

  // Get rid of the debug event listener.
  v8::debug::SetDebugDelegate(env->GetIsolate(), nullptr);
  CHECK_EQ(1, delegate.break_count());
  CheckDebuggerUnloaded();
}

static void EmptyHandler(const v8::FunctionCallbackInfo<v8::Value>& info) {
  CHECK(i::ValidateCallbackInfo(info));
}

TEST(DebugScopeIteratorWithFunctionTemplate) {
  LocalContext env;
  v8::HandleScope handle_scope(env->GetIsolate());
  v8::Isolate* isolate = env->GetIsolate();
  EnableDebugger(isolate);
  v8::Local<v8::Function> func =
      v8::Function::New(env.local(), EmptyHandler).ToLocalChecked();
  std::unique_ptr<v8::debug::ScopeIterator> iterator =
      v8::debug::ScopeIterator::CreateForFunction(isolate, func);
  CHECK(iterator->Done());
  DisableDebugger(isolate);
}

TEST(DebugBreakWithoutJS) {
  i::v8_flags.stress_compaction = false;
#ifdef VERIFY_HEAP
  i::v8_flags.verify_heap = true;
#endif
  LocalContext env;
  v8::Isolate* isolate = env->GetIsolate();
  v8::HandleScope scope(isolate);
  v8::Local<v8::Context> context = env.local();

  // Register a debug event listener which sets the break flag and counts.
  DebugEventBreak delegate;
  v8::debug::SetDebugDelegate(isolate, &delegate);

  // Set the debug break flag.
  v8::debug::SetBreakOnNextFunctionCall(env->GetIsolate());

  v8::Local<v8::String> json = v8_str("[1]");
  v8::Local<v8::Value> parsed = v8::JSON::Parse(context, json).ToLocalChecked();
  CHECK(v8::JSON::Stringify(context, parsed)
            .ToLocalChecked()
            ->Equals(context, json)
            .FromJust());
  CHECK_EQ(0, break_point_hit_count);
  CompileRun("");
  CHECK_EQ(1, break_point_hit_count);

  // Get rid of the debug event listener.
  v8::debug::SetDebugDelegate(env->GetIsolate(), nullptr);
  CheckDebuggerUnloaded();
}

// Test to ensure that JavaScript code keeps running while the debug break
// through the stack limit flag is set but breaks are disabled.
TEST(DisableBreak) {
  LocalContext env;
  v8::HandleScope scope(env->GetIsolate());

  // Register a debug event listener which sets the break flag and counts.
  DebugEventCounter delegate;
  v8::debug::SetDebugDelegate(env->GetIsolate(), &delegate);

  v8::Local<v8::Context> context = env.local();
  // Create a function for testing stepping.
  const char* src = "function f() {g()};function g(){i=0; while(i<10){i++}}";
  v8::Local<v8::Function> f = CompileFunction(&env, src, "f");

  // Set, test and cancel debug break.
  v8::debug::SetBreakOnNextFunctionCall(env->GetIsolate());
  v8::debug::ClearBreakOnNextFunctionCall(env->GetIsolate());

  // Set the debug break flag.
  v8::debug::SetBreakOnNextFunctionCall(env->GetIsolate());

  // Call all functions with different argument count.
  break_point_hit_count = 0;
  f->Call(context, env->Global(), 0, nullptr).ToLocalChecked();
  CHECK_EQ(1, break_point_hit_count);

  {
    v8::debug::SetBreakOnNextFunctionCall(env->GetIsolate());
    i::Isolate* isolate = reinterpret_cast<i::Isolate*>(env->GetIsolate());
    v8::internal::DisableBreak disable_break(isolate->debug());
    f->Call(context, env->Global(), 0, nullptr).ToLocalChecked();
    CHECK_EQ(1, break_point_hit_count);
  }

  f->Call(context, env->Global(), 0, nullptr).ToLocalChecked();
  CHECK_EQ(2, break_point_hit_count);

  // Get rid of the debug event listener.
  v8::debug::SetDebugDelegate(env->GetIsolate(), nullptr);
  CheckDebuggerUnloaded();
}

TEST(DisableDebuggerStatement) {
  LocalContext env;
  v8::HandleScope scope(env->GetIsolate());

  // Register a debug event listener which sets the break flag and counts.
  DebugEventCounter delegate;
  v8::debug::SetDebugDelegate(env->GetIsolate(), &delegate);

  CompileRun("debugger;");
  CHECK_EQ(1, break_point_hit_count);

  // Check that we ignore debugger statement when breakpoints aren't active.
  i::Isolate* isolate = reinterpret_cast<i::Isolate*>(env->GetIsolate());
  isolate->debug()->set_break_points_active(false);
  CompileRun("debugger;");
  CHECK_EQ(1, break_point_hit_count);

  v8::debug::SetDebugDelegate(env->GetIsolate(), nullptr);
}

static const char* kSimpleExtensionSource =
  "(function Foo() {"
  "  return 4;"
  "})() ";

// http://crbug.com/28933
// Test that debug break is disabled when bootstrapper is active.
TEST(NoBreakWhenBootstrapping) {
  v8::Isolate* isolate = CcTest::isolate();
  v8::HandleScope scope(isolate);

  // Register a debug event listener which sets the break flag and counts.
  DebugEventCounter delegate;
  v8::debug::SetDebugDelegate(isolate, &delegate);

  // Set the debug break flag.
  v8::debug::SetBreakOnNextFunctionCall(isolate);
  break_point_hit_count = 0;
  {
    // Create a context with an extension to make sure that some JavaScript
    // code is executed during bootstrapping.
    v8::RegisterExtension(
        std::make_unique<v8::Extension>("simpletest", kSimpleExtensionSource));
    const char* extension_names[] = { "simpletest" };
    v8::ExtensionConfiguration extensions(1, extension_names);
    v8::HandleScope handle_scope(isolate);
    v8::Context::New(isolate, &extensions);
  }
  // Check that no DebugBreak events occurred during the context creation.
  CHECK_EQ(0, break_point_hit_count);

  // Get rid of the debug event listener.
  v8::debug::SetDebugDelegate(isolate, nullptr);
  CheckDebuggerUnloaded();
}

TEST(SetDebugEventListenerOnUninitializedVM) {
  v8::HandleScope scope(CcTest::isolate());
  EnableDebugger(CcTest::isolate());
}

// Test that clearing the debug event listener actually clears all break points
// and related information.
TEST(DebuggerUnload) {
  LocalContext env;
  v8::HandleScope handle_scope(env->GetIsolate());
  // Check debugger is unloaded before it is used.
  CheckDebuggerUnloaded();

  // Set a debug event listener.
  break_point_hit_count = 0;
  DebugEventCounter delegate;
  v8::debug::SetDebugDelegate(env->GetIsolate(), &delegate);
  v8::Local<v8::Context> context = env.local();
  {
    v8::HandleScope scope(env->GetIsolate());
    // Create a couple of functions for the test.
    v8::Local<v8::Function> foo =
        CompileFunction(&env, "function foo(){x=1}", "foo");
    v8::Local<v8::Function> bar =
        CompileFunction(&env, "function bar(){y=2}", "bar");

    // Set some break points.
    SetBreakPoint(foo, 0);
    SetBreakPoint(foo, 4);
    SetBreakPoint(bar, 0);
    SetBreakPoint(bar, 4);

    // Make sure that the break points are there.
    break_point_hit_count = 0;
    foo->Call(context, env->Global(), 0, nullptr).ToLocalChecked();
    CHECK_EQ(2, break_point_hit_count);
    bar->Call(context, env->Global(), 0, nullptr).ToLocalChecked();
    CHECK_EQ(4, break_point_hit_count);
  }

  // Remove the debug event listener without clearing breakpoints. Do this
  // outside a handle scope.
  v8::debug::SetDebugDelegate(env->GetIsolate(), nullptr);
  CheckDebuggerUnloaded();
}

int event_listener_hit_count = 0;

// Test for issue http://code.google.com/p/v8/issues/detail?id=289.
// Make sure that DebugGetLoadedScripts doesn't return scripts
// with disposed external source.
class EmptyExternalStringResource : public v8::String::ExternalStringResource {
 public:
  EmptyExternalStringResource() { empty_[0] = 0; }
  ~EmptyExternalStringResource() override = default;
  size_t length() const override { return empty_.length(); }
  const uint16_t* data() const override { return empty_.begin(); }

 private:
  ::v8::base::EmbeddedVector<uint16_t, 1> empty_;
};

TEST(DebugScriptLineEndsAreAscending) {
  LocalContext env;
  v8::Isolate* isolate = env->GetIsolate();
  v8::HandleScope scope(isolate);

  // Compile a test script.
  v8::Local<v8::String> script_source = v8_str(isolate,
                                               "function f() {\n"
                                               "  debugger;\n"
                                               "}\n");

  v8::ScriptOrigin origin1 = v8::ScriptOrigin(v8_str(isolate, "name"));
  v8::Local<v8::Script> script =
      v8::Script::Compile(env.local(), script_source, &origin1)
          .ToLocalChecked();
  USE(script);

  DirectHandle<v8::internal::FixedArray> instances;
  {
    v8::internal::Debug* debug = CcTest::i_isolate()->debug();
    instances = debug->GetLoadedScripts();
  }

  CHECK_GT(instances->length(), 0);
  for (int i = 0; i < instances->length(); i++) {
    DirectHandle<v8::internal::Script> new_script(
        v8::internal::Cast<v8::internal::Script>(instances->get(i)),
        CcTest::i_isolate());

    v8::internal::Script::InitLineEnds(CcTest::i_isolate(), new_script);
    v8::internal::Tagged<v8::internal::FixedArray> ends =
        v8::internal::Cast<v8::internal::FixedArray>(new_script->line_ends());
    CHECK_GT(ends->length(), 0);

    int prev_end = -1;
    for (int j = 0; j < ends->length(); j++) {
      const int curr_end = v8::internal::Smi::ToInt(ends->get(j));
      CHECK_GT(curr_end, prev_end);
      prev_end = curr_end;
    }
  }
}

static v8::Global<v8::Context> expected_context_global;
static v8::Global<v8::Value> expected_context_data_global;

class ContextCheckEventListener : public v8::debug::DebugDelegate {
 public:
  void BreakProgramRequested(
      v8::Local<v8::Context> paused_context,
      const std::vector<v8::debug::BreakpointId>& inspector_break_points_hit,
      v8::debug::BreakReasons break_reasons) override {
    CheckContext();
  }
  void ScriptCompiled(v8::Local<v8::debug::Script> script, bool is_live_edited,
                      bool has_compile_error) override {
    CheckContext();
  }
  void ExceptionThrown(v8::Local<v8::Context> paused_context,
                       v8::Local<v8::Value> exception,
                       v8::Local<v8::Value> promise, bool is_uncaught,
                       v8::debug::ExceptionType) override {
    CheckContext();
  }
  bool IsFunctionBlackboxed(v8::Local<v8::debug::Script> script,
                            const v8::debug::Location& start,
                            const v8::debug::Location& end) override {
    CheckContext();
    return false;
  }

 private:
  void CheckContext() {
    v8::Local<v8::Context> context = CcTest::isolate()->GetCurrentContext();
    CHECK_EQ(context, expected_context_global.Get(CcTest::isolate()));
    CHECK(context->GetEmbedderData(0)->StrictEquals(
        expected_context_data_global.Get(CcTest::isolate())));
    event_listener_hit_count++;
  }
};

// Test which creates two contexts and sets different embedder data on each.
// Checks that this data is set correctly and that when the debug event
// listener is called the expected context is the one active.
TEST(ContextData) {
  v8::Isolate* isolate = CcTest::isolate();
  v8::HandleScope scope(isolate);

  // Create two contexts.
  v8::Local<v8::Context> context_1;
  v8::Local<v8::Context> context_2;
  v8::Local<v8::ObjectTemplate> global_template =
      v8::Local<v8::ObjectTemplate>();
  v8::Local<v8::Value> global_object = v8::Local<v8::Value>();
  context_1 =
      v8::Context::New(isolate, nullptr, global_template, global_object);
  context_2 =
      v8::Context::New(isolate, nullptr, global_template, global_object);

  ContextCheckEventListener delegate;
  v8::debug::SetDebugDelegate(isolate, &delegate);

  // Default data value is undefined.
  CHECK_EQ(0, context_1->GetNumberOfEmbedderDataFields());
  CHECK_EQ(0, context_2->GetNumberOfEmbedderDataFields());

  // Set and check different data values.
  v8::Local<v8::String> data_1 = v8_str(isolate, "1");
  v8::Local<v8::String> data_2 = v8_str(isolate, "2");
  context_1->SetEmbedderData(0, data_1);
  context_2->SetEmbedderData(0, data_2);
  CHECK(context_1->GetEmbedderData(0)->StrictEquals(data_1));
  CHECK(context_2->GetEmbedderData(0)->StrictEquals(data_2));

  // Simple test function which causes a break.
  const char* source = "function f() { debugger; }";

  // Enter and run function in the first context.
  {
    v8::Context::Scope context_scope(context_1);
    expected_context_global.Reset(isolate, context_1);
    expected_context_data_global.Reset(isolate, data_1);
    v8::Local<v8::Function> f = CompileFunction(isolate, source, "f");
    f->Call(context_1, context_1->Global(), 0, nullptr).ToLocalChecked();
  }

  // Enter and run function in the second context.
  {
    v8::Context::Scope context_scope(context_2);
    expected_context_global.Reset(isolate, context_2);
    expected_context_data_global.Reset(isolate, data_2);
    v8::Local<v8::Function> f = CompileFunction(isolate, source, "f");
    f->Call(context_2, context_2->Global(), 0, nullptr).ToLocalChecked();
  }

  // Two times compile event and two times break event.
  CHECK_GT(event_listener_hit_count, 3);

  v8::debug::SetDebugDelegate(isolate, nullptr);
  CheckDebuggerUnloaded();

  expected_context_global.Reset();
  expected_context_data_global.Reset();
}

// Test which creates a context and sets embedder data on it. Checks that this
// data is set correctly and that when the debug event listener is called for
// break event in an eval statement the expected context is the one returned by
// Message.GetEventContext.
TEST(EvalContextData) {
  v8::Isolate* isolate = CcTest::isolate();
  v8::HandleScope scope(isolate);

  v8::Local<v8::Context> context_1;
  v8::Local<v8::ObjectTemplate> global_template =
      v8::Local<v8::ObjectTemplate>();
  context_1 = v8::Context::New(isolate, nullptr, global_template);

  ContextCheckEventListener delegate;
  v8::debug::SetDebugDelegate(isolate, &delegate);

  // Contexts initially do not have embedder data fields.
  CHECK_EQ(0, context_1->GetNumberOfEmbedderDataFields());

  // Set and check a data value.
  v8::Local<v8::String> data_1 = v8_str(isolate, "1");
  context_1->SetEmbedderData(0, data_1);
  CHECK(context_1->GetEmbedderData(0)->StrictEquals(data_1));

  // Simple test function with eval that causes a break.
  const char* source = "function f() { eval('debugger;'); }";

  // Enter and run function in the context.
  {
    v8::Context::Scope context_scope(context_1);
    expected_context_global.Reset(isolate, context_1);
    expected_context_data_global.Reset(isolate, data_1);
    v8::Local<v8::Function> f = CompileFunction(isolate, source, "f");
    f->Call(context_1, context_1->Global(), 0, nullptr).ToLocalChecked();
  }

  v8::debug::SetDebugDelegate(isolate, nullptr);

  // One time compile event and one time break event.
  CHECK_GT(event_listener_hit_count, 2);
  CheckDebuggerUnloaded();

  expected_context_global.Reset();
  expected_context_data_global.Reset();
}

// Debug event listener which counts script compiled events.
class ScriptCompiledDelegate : public v8::debug::DebugDelegate {
 public:
  void ScriptCompiled(v8::Local<v8::debug::Script>, bool,
                      bool has_compile_error) override {
    if (!has_compile_error) {
      after_compile_event_count++;
    } else {
      compile_error_event_count++;
    }
  }

  int after_compile_event_count = 0;
  int compile_error_event_count = 0;
};

// Tests that after compile event is sent as many times as there are scripts
// compiled.
TEST(AfterCompileEventWhenEventListenerIsReset) {
  LocalContext env;
  v8::HandleScope scope(env->GetIsolate());
  v8::Local<v8::Context> context = env.local();
  const char* script = "var a=1";

  ScriptCompiledDelegate delegate;
  v8::debug::SetDebugDelegate(env->GetIsolate(), &delegate);
  v8::Script::Compile(context, v8_str(env->GetIsolate(), script))
      .ToLocalChecked()
      ->Run(context)
      .ToLocalChecked();
  v8::debug::SetDebugDelegate(env->GetIsolate(), nullptr);

  v8::debug::SetDebugDelegate(env->GetIsolate(), &delegate);
  v8::debug::SetBreakOnNextFunctionCall(env->GetIsolate());
  v8::Script::Compile(context, v8_str(env->GetIsolate(), script))
      .ToLocalChecked()
      ->Run(context)
      .ToLocalChecked();

  // Setting listener to nullptr should cause debugger unload.
  v8::debug::SetDebugDelegate(env->GetIsolate(), nullptr);
  CheckDebuggerUnloaded();

  // Compilation cache should be disabled when debugger is active.
  CHECK_EQ(2, delegate.after_compile_event_count);
}

// Tests that syntax error event is sent as many times as there are scripts
// with syntax error compiled.
TEST(SyntaxErrorEventOnSyntaxException) {
  LocalContext env;
  v8::HandleScope scope(env->GetIsolate());

  // For this test, we want to break on uncaught exceptions:
  ChangeBreakOnException(env->GetIsolate(), false, true);

  ScriptCompiledDelegate delegate;
  v8::debug::SetDebugDelegate(env->GetIsolate(), &delegate);
  v8::Local<v8::Context> context = env.local();

  // Check initial state.
  CHECK_EQ(0, delegate.compile_error_event_count);

  // Throws SyntaxError: Unexpected end of input
  CHECK(
      v8::Script::Compile(context, v8_str(env->GetIsolate(), "+++")).IsEmpty());
  CHECK_EQ(1, delegate.compile_error_event_count);

  CHECK(v8::Script::Compile(context, v8_str(env->GetIsolate(), "/sel\\/: \\"))
            .IsEmpty());
  CHECK_EQ(2, delegate.compile_error_event_count);

  v8::Local<v8::Script> script =
      v8::Script::Compile(context,
                          v8_str(env->GetIsolate(), "JSON.parse('1234:')"))
          .ToLocalChecked();
  CHECK_EQ(2, delegate.compile_error_event_count);
  CHECK(script->Run(context).IsEmpty());
  CHECK_EQ(3, delegate.compile_error_event_count);

  v8::Script::Compile(context,
                      v8_str(env->GetIsolate(), "new RegExp('/\\/\\\\');"))
      .ToLocalChecked();
  CHECK_EQ(3, delegate.compile_error_event_count);

  v8::Script::Compile(context, v8_str(env->GetIsolate(), "throw 1;"))
      .ToLocalChecked();
  CHECK_EQ(3, delegate.compile_error_event_count);
}

class ExceptionEventCounter : public v8::debug::DebugDelegate {
 public:
  void ExceptionThrown(v8::Local<v8::Context> paused_context,
                       v8::Local<v8::Value> exception,
                       v8::Local<v8::Value> promise, bool is_uncaught,
                       v8::debug::ExceptionType) override {
    exception_event_count++;
  }
  int exception_event_count = 0;
};

UNINITIALIZED_TEST(NoBreakOnStackOverflow) {
  // We must set v8_flags.stack_size before initializing the isolate.
  i::v8_flags.stack_size = 100;
  v8::Isolate::CreateParams create_params;
  create_params.array_buffer_allocator = CcTest::array_buffer_allocator();
  v8::Isolate* isolate = v8::Isolate::New(create_params);
  isolate->Enter();
  {
    LocalContext env(isolate);
    v8::HandleScope scope(isolate);

    ChangeBreakOnException(isolate, true, true);

    ExceptionEventCounter delegate;
    v8::debug::SetDebugDelegate(isolate, &delegate);
    CHECK_EQ(0, delegate.exception_event_count);

    CompileRun(
        "function f() { return f(); }"
        "try { f() } catch {}");

    CHECK_EQ(0, delegate.exception_event_count);
  }
  isolate->Exit();
  isolate->Dispose();
}

// Tests that break event is sent when event listener is reset.
TEST(BreakEventWhenEventListenerIsReset) {
  LocalContext env;
  v8::HandleScope scope(env->GetIsolate());
  v8::Local<v8::Context> context = env.local();
  const char* script = "function f() {};";

  ScriptCompiledDelegate delegate;
  v8::debug::SetDebugDelegate(env->GetIsolate(), &delegate);
  v8::Script::Compile(context, v8_str(env->GetIsolate(), script))
      .ToLocalChecked()
      ->Run(context)
      .ToLocalChecked();
  v8::debug::SetDebugDelegate(env->GetIsolate(), nullptr);

  v8::debug::SetDebugDelegate(env->GetIsolate(), &delegate);
  v8::debug::SetBreakOnNextFunctionCall(env->GetIsolate());
  v8::Local<v8::Function> f = v8::Local<v8::Function>::Cast(
      env->Global()
          ->Get(context, v8_str(env->GetIsolate(), "f"))
          .ToLocalChecked());
  f->Call(context, env->Global(), 0, nullptr).ToLocalChecked();

  // Setting event listener to nullptr should cause debugger unload.
  v8::debug::SetDebugDelegate(env->GetIsolate(), nullptr);
  CheckDebuggerUnloaded();

  // Compilation cache should be disabled when debugger is active.
  CHECK_EQ(1, delegate.after_compile_event_count);
}

// Tests that script is reported as compiled when bound to context.
TEST(AfterCompileEventOnBindToContext) {
  LocalContext env;
  v8::Isolate* isolate = env->GetIsolate();
  v8::HandleScope handle_scope(isolate);
  ScriptCompiledDelegate delegate;
  v8::debug::SetDebugDelegate(isolate, &delegate);

  v8::ScriptCompiler::Source script_source(
      v8::String::NewFromUtf8Literal(isolate, "var a=1"));

  v8::Local<v8::UnboundScript> unbound =
      v8::ScriptCompiler::CompileUnboundScript(isolate, &script_source)
          .ToLocalChecked();
  CHECK_EQ(delegate.after_compile_event_count, 0);

  unbound->BindToCurrentContext();
  CHECK_EQ(delegate.after_compile_event_count, 1);
  v8::debug::SetDebugDelegate(isolate, nullptr);
}


// Test that if DebugBreak is forced it is ignored when code from
// debug-delay.js is executed.
TEST(NoDebugBreakInAfterCompileEventListener) {
  LocalContext env;
  v8::HandleScope scope(env->GetIsolate());
  v8::Local<v8::Context> context = env.local();

  // Register a debug event listener which sets the break flag and counts.
  DebugEventCounter delegate;
  v8::debug::SetDebugDelegate(env->GetIsolate(), &delegate);

  // Set the debug break flag.
  v8::debug::SetBreakOnNextFunctionCall(env->GetIsolate());

  // Create a function for testing stepping.
  const char* src = "function f() { eval('var x = 10;'); } ";
  v8::Local<v8::Function> f = CompileFunction(&env, src, "f");

  // There should be only one break event.
  CHECK_EQ(1, break_point_hit_count);

  // Set the debug break flag again.
  v8::debug::SetBreakOnNextFunctionCall(env->GetIsolate());
  f->Call(context, env->Global(), 0, nullptr).ToLocalChecked();
  // There should be one more break event when the script is evaluated in 'f'.
  CHECK_EQ(2, break_point_hit_count);

  // Get rid of the debug event listener.
  v8::debug::SetDebugDelegate(env->GetIsolate(), nullptr);
  CheckDebuggerUnloaded();
}


// Test that the debug break flag works with function.apply.
TEST(RepeatDebugBreak) {
  // Test that we can repeatedly set a break without JS execution continuing.
  LocalContext env;
  v8::HandleScope scope(env->GetIsolate());
  v8::Local<v8::Context> context = env.local();

  // Create a function for testing breaking in apply.
  v8::Local<v8::Function> foo =
      CompileFunction(&env, "function foo() {}", "foo");

  // Register a debug delegate which repeatedly sets a break and counts.
  DebugEventBreakMax delegate;
  v8::debug::SetDebugDelegate(env->GetIsolate(), &delegate);

  // Set the debug break flag before calling the code using function.apply.
  v8::debug::SetBreakOnNextFunctionCall(env->GetIsolate());

  // Trigger a break by calling into foo().
  break_point_hit_count = 0;
  max_break_point_hit_count = 10000;
  foo->Call(context, env->Global(), 0, nullptr).ToLocalChecked();

  // When keeping the debug break several break will happen.
  CHECK_EQ(break_point_hit_count, max_break_point_hit_count);

  v8::debug::SetDebugDelegate(env->GetIsolate(), nullptr);
  CheckDebuggerUnloaded();
}

// Test that setting the terminate execution flag during debug break processing.
static void TestDebugBreakInLoop(const char* loop_head,
                                 const char** loop_bodies,
                                 const char* loop_tail) {
  // Receive 10 breaks for each test and then terminate JavaScript execution.
  static const int kBreaksPerTest = 10;

  for (int i = 0; loop_bodies[i] != nullptr; i++) {
    // Perform a lazy deoptimization after various numbers of breaks
    // have been hit.

    v8::base::EmbeddedVector<char, 1024> buffer;
    v8::base::SNPrintF(buffer, "function f() {%s%s%s}", loop_head,
                       loop_bodies[i], loop_tail);

    i::PrintF("%s\n", buffer.begin());

    for (int j = 0; j < 3; j++) {
      break_point_hit_count_deoptimize = j;
      if (j == 2) {
        break_point_hit_count_deoptimize = kBreaksPerTest;
      }

      break_point_hit_count = 0;
      max_break_point_hit_count = kBreaksPerTest;
      terminate_after_max_break_point_hit = true;

      // Function with infinite loop.
      CompileRun(buffer.begin());

      // Set the debug break to enter the debugger as soon as possible.
      v8::debug::SetBreakOnNextFunctionCall(CcTest::isolate());

      // Call function with infinite loop.
      CompileRun("f();");
      CHECK_EQ(kBreaksPerTest, break_point_hit_count);

      CHECK(!CcTest::isolate()->IsExecutionTerminating());
    }
  }
}

static const char* loop_bodies_1[] = {"",
                                      "g()",
                                      "if (a == 0) { g() }",
                                      "if (a == 1) { g() }",
                                      "if (a == 0) { g() } else { h() }",
                                      "if (a == 0) { continue }",
                                      nullptr};

static const char* loop_bodies_2[] = {
    "if (a == 1) { continue }",
    "switch (a) { case 1: g(); }",
    "switch (a) { case 1: continue; }",
    "switch (a) { case 1: g(); break; default: h() }",
    "switch (a) { case 1: continue; break; default: h() }",
    nullptr};

void DebugBreakLoop(const char* loop_header, const char** loop_bodies,
                    const char* loop_footer) {
  LocalContext env;
  v8::HandleScope scope(env->GetIsolate());

  // Register a debug delegate which repeatedly sets the break flag and counts.
  DebugEventBreakMax delegate;
  v8::debug::SetDebugDelegate(env->GetIsolate(), &delegate);

  CompileRun(
      "var a = 1;\n"
      "function g() { }\n"
      "function h() { }");

  TestDebugBreakInLoop(loop_header, loop_bodies, loop_footer);

  // Also test with "Scheduled" break reason.
  break_right_now_reasons =
      v8::debug::BreakReasons{v8::debug::BreakReason::kScheduled};
  TestDebugBreakInLoop(loop_header, loop_bodies, loop_footer);
  break_right_now_reasons = v8::debug::BreakReasons{};

  // Get rid of the debug event listener.
  v8::debug::SetDebugDelegate(env->GetIsolate(), nullptr);
  CheckDebuggerUnloaded();
}


TEST(DebugBreakInWhileTrue1) {
  DebugBreakLoop("while (true) {", loop_bodies_1, "}");
}


TEST(DebugBreakInWhileTrue2) {
  DebugBreakLoop("while (true) {", loop_bodies_2, "}");
}


TEST(DebugBreakInWhileCondition1) {
  DebugBreakLoop("while (a == 1) {", loop_bodies_1, "}");
}


TEST(DebugBreakInWhileCondition2) {
  DebugBreakLoop("while (a == 1) {", loop_bodies_2, "}");
}


TEST(DebugBreakInDoWhileTrue1) {
  DebugBreakLoop("do {", loop_bodies_1, "} while (true)");
}


TEST(DebugBreakInDoWhileTrue2) {
  DebugBreakLoop("do {", loop_bodies_2, "} while (true)");
}


TEST(DebugBreakInDoWhileCondition1) {
  DebugBreakLoop("do {", loop_bodies_1, "} while (a == 1)");
}


TEST(DebugBreakInDoWhileCondition2) {
  DebugBreakLoop("do {", loop_bodies_2, "} while (a == 1)");
}


TEST(DebugBreakInFor1) { DebugBreakLoop("for (;;) {", loop_bodies_1, "}"); }


TEST(DebugBreakInFor2) { DebugBreakLoop("for (;;) {", loop_bodies_2, "}"); }


TEST(DebugBreakInForCondition1) {
  DebugBreakLoop("for (;a == 1;) {", loop_bodies_1, "}");
}


TEST(DebugBreakInForCondition2) {
  DebugBreakLoop("for (;a == 1;) {", loop_bodies_2, "}");
}

class DebugBreakInlineListener : public v8::debug::DebugDelegate {
 public:
  void BreakProgramRequested(
      v8::Local<v8::Context> paused_context,
      const std::vector<v8::debug::BreakpointId>& inspector_break_points_hit,
      v8::debug::BreakReasons break_reasons) override {
    int expected_frame_count = 4;
    int expected_line_number[] = {1, 4, 7, 13};

    int frame_count = 0;
    auto iterator = v8::debug::StackTraceIterator::Create(CcTest::isolate());
    for (; !iterator->Done(); iterator->Advance(), ++frame_count) {
      v8::debug::Location loc = iterator->GetSourceLocation();
      CHECK_EQ(expected_line_number[frame_count], loc.GetLineNumber());
    }
    CHECK_EQ(frame_count, expected_frame_count);
  }
};

TEST(DebugBreakInline) {
  i::v8_flags.allow_natives_syntax = true;
  LocalContext env;
  v8::HandleScope scope(env->GetIsolate());
  v8::Local<v8::Context> context = env.local();
  const char* source =
      "function debug(b) {                 \n"
      "  if (b) debugger;                  \n"
      "}                                   \n"
      "function f(b) {                     \n"
      "  debug(b)                          \n"
      "};                                  \n"
      "function g(b) {                     \n"
      "  f(b);                             \n"
      "};                                  \n"
      "%PrepareFunctionForOptimization(g); \n"
      "g(false);                           \n"
      "g(false);                           \n"
      "%OptimizeFunctionOnNextCall(g);     \n"
      "g(true);";
  DebugBreakInlineListener delegate;
  v8::debug::SetDebugDelegate(env->GetIsolate(), &delegate);
  v8::Local<v8::Script> inline_script =
      v8::Script::Compile(context, v8_str(env->GetIsolate(), source))
          .ToLocalChecked();
  inline_script->Run(context).ToLocalChecked();
  v8::debug::SetDebugDelegate(env->GetIsolate(), nullptr);
}

static void RunScriptInANewCFrame(const char* source) {
  v8::TryCatch try_catch(CcTest::isolate());
  CompileRun(source);
  CHECK(try_catch.HasCaught());
}


TEST(Regress131642) {
  // Bug description:
  // When doing StepOver through the first script, the debugger is not reset
  // after exiting through exception.  A flawed implementation enabling the
  // debugger to step into Array.prototype.forEach breaks inside the callback
  // for forEach in the second script under the assumption that we are in a
  // recursive call.  In an attempt to step out, we crawl the stack using the
  // recorded frame pointer from the first script and fail when not finding it
  // on the stack.
  LocalContext env;
  v8::HandleScope scope(env->GetIsolate());
  DebugEventCounter delegate;
  delegate.set_step_action(StepOver);
  v8::debug::SetDebugDelegate(env->GetIsolate(), &delegate);

  // We step through the first script.  It exits through an exception.  We run
  // this inside a new frame to record a different FP than the second script
  // would expect.
  const char* script_1 = "debugger; throw new Error();";
  RunScriptInANewCFrame(script_1);

  // The second script uses forEach.
  const char* script_2 = "[0].forEach(function() { });";
  CompileRun(script_2);

  v8::debug::SetDebugDelegate(env->GetIsolate(), nullptr);
}

class DebugBreakStackTraceListener : public v8::debug::DebugDelegate {
 public:
  void BreakProgramRequested(
      v8::Local<v8::Context> paused_context,
      const std::vector<v8::debug::BreakpointId>& inspector_break_points_hit,
      v8::debug::BreakReasons break_reasons) override {
    v8::StackTrace::CurrentStackTrace(CcTest::isolate(), 10);
  }
};

static void AddDebugBreak(const v8::FunctionCallbackInfo<v8::Value>& info) {
  CHECK(i::ValidateCallbackInfo(info));
  v8::debug::SetBreakOnNextFunctionCall(info.GetIsolate());
}

TEST(DebugBreakStackTrace) {
  LocalContext env;
  v8::HandleScope scope(env->GetIsolate());
  DebugBreakStackTraceListener delegate;
  v8::debug::SetDebugDelegate(env->GetIsolate(), &delegate);
  v8::Local<v8::Context> context = env.local();
  v8::Local<v8::FunctionTemplate> add_debug_break_template =
      v8::FunctionTemplate::New(env->GetIsolate(), AddDebugBreak);
  v8::Local<v8::Function> add_debug_break =
      add_debug_break_template->GetFunction(context).ToLocalChecked();
  CHECK(env->Global()
            ->Set(context, v8_str("add_debug_break"), add_debug_break)
            .FromJust());

  CompileRun("(function loop() {"
             "  for (var j = 0; j < 1000; j++) {"
             "    for (var i = 0; i < 1000; i++) {"
             "      if (i == 999) add_debug_break();"
             "    }"
             "  }"
             "})()");
}


v8::base::Semaphore terminate_requested_semaphore(0);
v8::base::Semaphore terminate_fired_semaphore(0);

class DebugBreakTriggerTerminate : public v8::debug::DebugDelegate {
 public:
  void BreakProgramRequested(
      v8::Local<v8::Context> paused_context,
      const std::vector<v8::debug::BreakpointId>& inspector_break_points_hit,
      v8::debug::BreakReasons break_reasons) override {
    if (terminate_already_fired_) return;
    terminate_requested_semaphore.Signal();
    // Wait for at most 2 seconds for the terminate request.
    CHECK(
        terminate_fired_semaphore.WaitFor(v8::base::TimeDelta::FromSeconds(2)));
    terminate_already_fired_ = true;
  }

 private:
  bool terminate_already_fired_ = false;
};

class TerminationThread : public v8::base::Thread {
 public:
  explicit TerminationThread(v8::Isolate* isolate)
      : Thread(Options("terminator")), isolate_(isolate) {}

  void Run() override {
    terminate_requested_semaphore.Wait();
    isolate_->TerminateExecution();
    terminate_fired_semaphore.Signal();
  }

 private:
  v8::Isolate* isolate_;
};


TEST(DebugBreakOffThreadTerminate) {
  LocalContext env;
  v8::Isolate* isolate = env->GetIsolate();
  v8::HandleScope scope(isolate);
  DebugBreakTriggerTerminate delegate;
  v8::debug::SetDebugDelegate(isolate, &delegate);
  TerminationThread terminator(isolate);
  CHECK(terminator.Start());
  v8::TryCatch try_catch(env->GetIsolate());
  env->GetIsolate()->RequestInterrupt(BreakRightNow, nullptr);
  CompileRun("while (true);");
  CHECK(try_catch.HasTerminated());
}

class ArchiveRestoreThread : public v8::base::Thread,
                             public v8::debug::DebugDelegate {
 public:
  ArchiveRestoreThread(v8::Isolate* isolate, int spawn_count)
      : Thread(Options("ArchiveRestoreThread")),
        isolate_(isolate),
        debug_(reinterpret_cast<i::Isolate*>(isolate_)->debug()),
        spawn_count_(spawn_count),
        break_count_(0) {}

  void Run() override {
    {
      v8::Locker locker(isolate_);
      v8::Isolate::Scope i_scope(isolate_);

      v8::HandleScope scope(isolate_);
      v8::Local<v8::Context> context = v8::Context::New(isolate_);
      v8::Context::Scope context_scope(context);
      auto callback = [](const v8::FunctionCallbackInfo<v8::Value>& info) {
        v8::Local<v8::Value> value = info.Data();
        CHECK(value->IsExternal());
        auto art = static_cast<ArchiveRestoreThread*>(
            v8::Local<v8::External>::Cast(value)->Value());
        art->MaybeSpawnChildThread();
      };
      v8::Local<v8::FunctionTemplate> fun = v8::FunctionTemplate::New(
          isolate_, callback, v8::External::New(isolate_, this));
      CHECK(context->Global()
                ->Set(context, v8_str("maybeSpawnChildThread"),
                      fun->GetFunction(context).ToLocalChecked())
                .FromJust());

      v8::Local<v8::Function> test =
          CompileFunction(isolate_,
                          "function test(n) {\n"
                          "  debugger;\n"
                          "  nest();\n"
                          "  middle();\n"
                          "  return n + 1;\n"
                          "  function middle() {\n"
                          "     debugger;\n"
                          "     nest();\n"
                          "     Date.now();\n"
                          "  }\n"
                          "  function nest() {\n"
                          "    maybeSpawnChildThread();\n"
                          "  }\n"
                          "}\n",
                          "test");

      debug_->SetDebugDelegate(this);
      v8::internal::DisableBreak enable_break(debug_, false);

      v8::Local<v8::Value> args[1] = {v8::Integer::New(isolate_, spawn_count_)};

      int result = test->Call(context, context->Global(), 1, args)
                       .ToLocalChecked()
                       ->Int32Value(context)
                       .FromJust();

      // Verify that test(spawn_count_) returned spawn_count_ + 1.
      CHECK_EQ(spawn_count_ + 1, result);
    }
  }

  void BreakProgramRequested(v8::Local<v8::Context> context,
                             const std::vector<v8::debug::BreakpointId>&,
                             v8::debug::BreakReasons break_reasons) override {
    auto stack_traces = v8::debug::StackTraceIterator::Create(isolate_);
    if (!stack_traces->Done()) {
      v8::debug::Location location = stack_traces->GetSourceLocation();

      i::PrintF("ArchiveRestoreThread #%d hit breakpoint at line %d\n",
                spawn_count_, location.GetLineNumber());

      const int expectedLineNumber[] = {1, 2, 3, 6, 4};
      CHECK_EQ(expectedLineNumber[break_count_], location.GetLineNumber());
      switch (break_count_) {
        case 0:  // debugger;
        case 1:  // nest();
        case 2:  // middle();

          // Attempt to stop on the next line after the first debugger
          // statement. If debug->{Archive,Restore}Debug() improperly reset
          // thread-local debug information, the debugger will fail to stop
          // before the test function returns.
          debug_->PrepareStep(StepOver);

          // Spawning threads while handling the current breakpoint verifies
          // that the parent thread correctly archived and restored the
          // state necessary to stop on the next line. If not, then control
          // will simply continue past the `return n + 1` statement.
          //
          // A real world multi-threading app would probably never unlock the
          // Isolate at a break point as that adds a thread switch point while
          // debugging where none existed in the application and a
          // multi-threaded should be able to count on not thread switching
          // over a certain range of instructions.
          MaybeSpawnChildThread();

          break;

        case 3:  // debugger; in middle();
          // Attempt to stop on the next line after the first debugger
          // statement. If debug->{Archive,Restore}Debug() improperly reset
          // thread-local debug information, the debugger will fail to stop
          // before the test function returns.
          debug_->PrepareStep(StepOut);
          break;

        case 4:  // return n + 1;
          break;

        default:
          CHECK(false);
      }
    }

    ++break_count_;
  }

  void MaybeSpawnChildThread() {
    if (spawn_count_ <= 1) return;
    {
      isolate_->Exit();
      v8::Unlocker unlocker(isolate_);

      // Spawn a thread that spawns a thread that spawns a thread (and so
      // on) so that the ThreadManager is forced to archive and restore
      // the current thread.
      ArchiveRestoreThread child(isolate_, spawn_count_ - 1);
      CHECK(child.Start());
      child.Join();

      // The child thread sets itself as the debug delegate, so we need to
      // usurp it after the child finishes, or else future breakpoints
      // will be delegated to a destroyed ArchiveRestoreThread object.
      debug_->SetDebugDelegate(this);

      // This is the most important check in this test, since
      // child.GetBreakCount() will return 1 if the debugger fails to stop
      // on the `next()` line after the grandchild thread returns.
      CHECK_EQ(child.GetBreakCount(), 5);

      // This test on purpose unlocks the isolate without exiting and
      // re-entering. It must however update the stack start, which would have
      // been done automatically if the isolate was properly re-entered.
      reinterpret_cast<i::Isolate*>(isolate_)->heap()->SetStackStart();
    }
    isolate_->Enter();
  }

  int GetBreakCount() { return break_count_; }

 private:
  v8::Isolate* isolate_;
  v8::internal::Debug* debug_;
  const int spawn_count_;
  int break_count_;
};

TEST(DebugArchiveRestore) {
  v8::Isolate* isolate = CcTest::isolate();

  // This test uses the multi-threaded model and v8::Locker, so the main
  // thread must exit the isolate before the test starts.
  isolate->Exit();

  ArchiveRestoreThread thread(isolate, 4);
  // Instead of calling thread.Start() and thread.Join() here, we call
  // thread.Run() directly, to make sure we exercise archive/restore
  // logic on the *current* thread as well as other threads.
  thread.Run();
  CHECK_EQ(thread.GetBreakCount(), 5);

  // The isolate must be entered again, before teardown.
  isolate->Enter();
}

namespace {
class ThreadJustUsingV8Locker : public v8::base::Thread {
 public:
  explicit ThreadJustUsingV8Locker(v8::Isolate* isolate)
      : Thread(Options("thread using v8::Locker")), isolate_(isolate) {}

  void Run() override {
    v8::Locker locker(isolate_);
    v8::Isolate::Scope isolate_scope(isolate_);
    v8::HandleScope scope(isolate_);
    // This thread does nothing useful.
  }

 private:
  v8::Isolate* isolate_;
};
}  // anonymous namespace

UNINITIALIZED_TEST(Bug1511649UnlockerRestoreDebug) {
  v8::Isolate::CreateParams create_params;
  create_params.array_buffer_allocator = CcTest::array_buffer_allocator();
  v8::Isolate* isolate = v8::Isolate::New(create_params);

  {
    v8::Locker locker(isolate);
    v8::Isolate::Scope isolate_scope(isolate);
    v8::HandleScope scope(isolate);
    v8::Local<v8::Context> context = v8::Context::New(isolate);
    v8::Context::Scope context_scope(context);

    v8::Local<v8::Function> test =
        CompileFunction(isolate, "function test() {}", "test");
    i::DirectHandle<i::BreakPoint> bp = SetBreakPoint(test, 0);

    {
      isolate->Exit();
      v8::Unlocker unlocker(isolate);

      ThreadJustUsingV8Locker thread(isolate);
      CHECK(thread.Start());
      thread.Join();
    }
    isolate->Enter();

    i::Isolate* i_isolate = reinterpret_cast<i::Isolate*>(isolate);
    i::Debug* debug = i_isolate->debug();
    debug->ClearBreakPoint(bp);
  }
  isolate->Dispose();
}

class DebugEventExpectNoException : public v8::debug::DebugDelegate {
 public:
  void ExceptionThrown(v8::Local<v8::Context> paused_context,
                       v8::Local<v8::Value> exception,
                       v8::Local<v8::Value> promise, bool is_uncaught,
                       v8::debug::ExceptionType) override {
    CHECK(false);
  }
};

static void TryCatchWrappedThrowCallback(
    const v8::FunctionCallbackInfo<v8::Value>& info) {
  CHECK(i::ValidateCallbackInfo(info));
  v8::TryCatch try_catch(info.GetIsolate());
  CompileRun("throw 'rejection';");
  CHECK(try_catch.HasCaught());
}

TEST(DebugPromiseInterceptedByTryCatch) {
  LocalContext env;
  v8::Isolate* isolate = env->GetIsolate();
  v8::HandleScope scope(isolate);
  DebugEventExpectNoException delegate;
  v8::debug::SetDebugDelegate(isolate, &delegate);
  v8::Local<v8::Context> context = env.local();
  ChangeBreakOnException(isolate, false, true);

  v8::Local<v8::FunctionTemplate> fun =
      v8::FunctionTemplate::New(isolate, TryCatchWrappedThrowCallback);
  CHECK(env->Global()
            ->Set(context, v8_str("fun"),
                  fun->GetFunction(context).ToLocalChecked())
            .FromJust());

  CompileRun("var p = new Promise(function(res, rej) { fun(); res(); });");
  CompileRun(
      "var r;"
      "p.then(function() { r = 'resolved'; },"
      "       function() { r = 'rejected'; });");
  CHECK(CompileRun("r")->Equals(context, v8_str("resolved")).FromJust());
}

class NoInterruptsOnDebugEvent : public v8::debug::DebugDelegate {
 public:
  void ScriptCompiled(v8::Local<v8::debug::Script> script, bool is_live_edited,
                      bool has_compile_error) override {
    ++after_compile_handler_depth_;
    // Do not allow nested AfterCompile events.
    CHECK_LE(after_compile_handler_depth_, 1);
    v8::Isolate* isolate = CcTest::isolate();
    v8::Isolate::AllowJavascriptExecutionScope allow_script(isolate);
    isolate->RequestInterrupt(&HandleInterrupt, this);
    CompileRun("function foo() {}; foo();");
    --after_compile_handler_depth_;
  }

 private:
  static void HandleInterrupt(v8::Isolate* isolate, void* data) {
    NoInterruptsOnDebugEvent* d = static_cast<NoInterruptsOnDebugEvent*>(data);
    CHECK_EQ(0, d->after_compile_handler_depth_);
  }

  int after_compile_handler_depth_ = 0;
};

TEST(NoInterruptsInDebugListener) {
  LocalContext env;
  v8::HandleScope handle_scope(env->GetIsolate());
  NoInterruptsOnDebugEvent delegate;
  v8::debug::SetDebugDelegate(env->GetIsolate(), &delegate);
  CompileRun("void(0);");
}

TEST(BreakLocationIterator) {
  LocalContext env;
  v8::Isolate* isolate = env->GetIsolate();
  i::Isolate* i_isolate = reinterpret_cast<i::Isolate*>(isolate);
  v8::HandleScope scope(isolate);

  v8::Local<v8::Value> result = CompileRun(
      "function f() {\n"
      "  debugger;   \n"
      "  f();        \n"
      "  debugger;   \n"
      "}             \n"
      "f");
  Handle<i::Object> function_obj = v8::Utils::OpenHandle(*result);
  DirectHandle<i::JSFunction> function = Cast<i::JSFunction>(function_obj);
  Handle<i::SharedFunctionInfo> shared(function->shared(), i_isolate);

  EnableDebugger(isolate);
  CHECK(i_isolate->debug()->EnsureBreakInfo(shared));
  i_isolate->debug()->PrepareFunctionForDebugExecution(shared);

  Handle<i::DebugInfo> debug_info(shared->GetDebugInfo(i_isolate), i_isolate);

  {
    i::BreakIterator iterator(debug_info);
    CHECK(iterator.GetBreakLocation().IsDebuggerStatement());
    CHECK_EQ(17, iterator.GetBreakLocation().position());
    iterator.Next();
    CHECK(iterator.GetBreakLocation().IsDebugBreakSlot());
    CHECK_EQ(32, iterator.GetBreakLocation().position());
    iterator.Next();
    CHECK(iterator.GetBreakLocation().IsCall());
    CHECK_EQ(32, iterator.GetBreakLocation().position());
    iterator.Next();
    CHECK(iterator.GetBreakLocation().IsDebuggerStatement());
    CHECK_EQ(47, iterator.GetBreakLocation().position());
    iterator.Next();
    CHECK(iterator.GetBreakLocation().IsReturn());
    CHECK_EQ(60, iterator.GetBreakLocation().position());
    iterator.Next();
    CHECK(iterator.Done());
  }

  DisableDebugger(isolate);
}

class DebugStepOverFunctionWithCaughtExceptionListener
    : public v8::debug::DebugDelegate {
 public:
  void BreakProgramRequested(
      v8::Local<v8::Context> paused_context,
      const std::vector<v8::debug::BreakpointId>& inspector_break_points_hit,
      v8::debug::BreakReasons break_reasons) override {
    ++break_point_hit_count;
    if (break_point_hit_count >= 3) return;
    PrepareStep(StepOver);
  }
  int break_point_hit_count = 0;
};

TEST(DebugStepOverFunctionWithCaughtException) {
  i::v8_flags.allow_natives_syntax = true;

  LocalContext env;
  v8::Isolate* isolate = env->GetIsolate();
  v8::HandleScope scope(isolate);
  DebugStepOverFunctionWithCaughtExceptionListener delegate;
  v8::debug::SetDebugDelegate(isolate, &delegate);

  CompileRun(
      "function foo() {\n"
      "  try { throw new Error(); } catch (e) {}\n"
      "}\n"
      "debugger;\n"
      "foo();\n"
      "foo();\n");

  v8::debug::SetDebugDelegate(env->GetIsolate(), nullptr);
  CHECK_EQ(3, delegate.break_point_hit_count);
}

bool near_heap_limit_callback_called = false;
size_t NearHeapLimitCallback(void* data, size_t current_heap_limit,
                             size_t initial_heap_limit) {
  near_heap_limit_callback_called = true;
  return initial_heap_limit + 10u * i::MB;
}

UNINITIALIZED_TEST(DebugSetOutOfMemoryListener) {
  i::v8_flags.stress_concurrent_allocation = false;
  v8::Isolate::CreateParams create_params;
  create_params.array_buffer_allocator = CcTest::array_buffer_allocator();
  create_params.constraints.set_max_old_generation_size_in_bytes(10 * i::MB);
  v8::Isolate* isolate = v8::Isolate::New(create_params);
  i::Isolate* i_isolate = reinterpret_cast<i::Isolate*>(isolate);
  {
    v8::Isolate::Scope i_scope(isolate);
    v8::HandleScope scope(isolate);
    LocalContext context(isolate);
    isolate->AddNearHeapLimitCallback(NearHeapLimitCallback, nullptr);
    CHECK(!near_heap_limit_callback_called);
    // The following allocation fails unless the out-of-memory callback
    // increases the heap limit.
    int length = 10 * i::MB / i::kTaggedSize;
    i_isolate->factory()->NewFixedArray(length, i::AllocationType::kOld);
    CHECK(near_heap_limit_callback_called);
    isolate->RemoveNearHeapLimitCallback(NearHeapLimitCallback, 0);
  }
  isolate->Dispose();
}

TEST(DebugCoverage) {
  i::v8_flags.always_turbofan = false;
  LocalContext env;
  v8::Isolate* isolate = env->GetIsolate();
  v8::HandleScope scope(isolate);
  v8::debug::Coverage::SelectMode(isolate,
                                  v8::debug::CoverageMode::kPreciseCount);
  v8::Local<v8::String> source = v8_str(
      "function f() {\n"
      "}\n"
      "f();\n"
      "f();");
  CompileRun(source);
  v8::debug::Coverage coverage = v8::debug::Coverage::CollectPrecise(isolate);
  CHECK_EQ(1u, coverage.ScriptCount());
  v8::debug::Coverage::ScriptData script_data = coverage.GetScriptData(0);
  v8::Local<v8::debug::Script> script = script_data.GetScript();
  CHECK(script->Source()
            ->JavaScriptCode()
            .ToLocalChecked()
            ->Equals(env.local(), source)
            .FromMaybe(false));

  CHECK_EQ(2u, script_data.FunctionCount());
  v8::debug::Coverage::FunctionData function_data =
      script_data.GetFunctionData(0);
  v8::debug::Location start =
      script->GetSourceLocation(function_data.StartOffset());
  v8::debug::Location end =
      script->GetSourceLocation(function_data.EndOffset());
  CHECK_EQ(0, start.GetLineNumber());
  CHECK_EQ(0, start.GetColumnNumber());
  CHECK_EQ(3, end.GetLineNumber());
  CHECK_EQ(4, end.GetColumnNumber());
  CHECK_EQ(1, function_data.Count());

  function_data = script_data.GetFunctionData(1);
  start = script->GetSourceLocation(function_data.StartOffset());
  end = script->GetSourceLocation(function_data.EndOffset());
  CHECK_EQ(0, start.GetLineNumber());
  CHECK_EQ(0, start.GetColumnNumber());
  CHECK_EQ(1, end.GetLineNumber());
  CHECK_EQ(1, end.GetColumnNumber());
  CHECK_EQ(2, function_data.Count());
}

namespace {
v8::debug::Coverage::ScriptData GetScriptDataAndDeleteCoverage(
    v8::Isolate* isolate) {
  v8::debug::Coverage coverage = v8::debug::Coverage::CollectPrecise(isolate);
  CHECK_EQ(1u, coverage.ScriptCount());
  return coverage.GetScriptData(0);
}
}  // namespace

TEST(DebugCoverageWithCoverageOutOfScope) {
  i::v8_flags.always_turbofan = false;
  LocalContext env;
  v8::Isolate* isolate = env->GetIsolate();
  v8::HandleScope scope(isolate);
  v8::debug::Coverage::SelectMode(isolate,
                                  v8::debug::CoverageMode::kPreciseCount);
  v8::Local<v8::String> source = v8_str(
      "function f() {\n"
      "}\n"
      "f();\n"
      "f();");
  CompileRun(source);
  v8::debug::Coverage::ScriptData script_data =
      GetScriptDataAndDeleteCoverage(isolate);
  v8::Local<v8::debug::Script> script = script_data.GetScript();
  CHECK(script->Source()
            ->JavaScriptCode()
            .ToLocalChecked()
            ->Equals(env.local(), source)
            .FromMaybe(false));

  CHECK_EQ(2u, script_data.FunctionCount());
  v8::debug::Coverage::FunctionData function_data =
      script_data.GetFunctionData(0);

  CHECK_EQ(0, function_data.StartOffset());
  CHECK_EQ(26, function_data.EndOffset());

  v8::debug::Location start =
      script->GetSourceLocation(function_data.StartOffset());
  v8::debug::Location end =
      script->GetSourceLocation(function_data.EndOffset());
  CHECK_EQ(0, start.GetLineNumber());
  CHECK_EQ(0, start.GetColumnNumber());
  CHECK_EQ(3, end.GetLineNumber());
  CHECK_EQ(4, end.GetColumnNumber());
  CHECK_EQ(1, function_data.Count());

  function_data = script_data.GetFunctionData(1);
  start = script->GetSourceLocation(function_data.StartOffset());
  end = script->GetSourceLocation(function_data.EndOffset());

  CHECK_EQ(0, function_data.StartOffset());
  CHECK_EQ(16, function_data.EndOffset());

  CHECK_EQ(0, start.GetLineNumber());
  CHECK_EQ(0, start.GetColumnNumber());
  CHECK_EQ(1, end.GetLineNumber());
  CHECK_EQ(1, end.GetColumnNumber());
  CHECK_EQ(2, function_data.Count());
}

namespace {
v8::debug::Coverage::FunctionData GetFunctionDataAndDeleteCoverage(
    v8::Isolate* isolate) {
  v8::debug::Coverage coverage = v8::debug::Coverage::CollectPrecise(isolate);
  CHECK_EQ(1u, coverage.ScriptCount());

  v8::debug::Coverage::ScriptData script_data = coverage.GetScriptData(0);

  CHECK_EQ(2u, script_data.FunctionCount());
  v8::debug::Coverage::FunctionData function_data =
      script_data.GetFunctionData(0);
  CHECK_EQ(1, function_data.Count());
  CHECK_EQ(0, function_data.StartOffset());
  CHECK_EQ(26, function_data.EndOffset());
  return function_data;
}
}  // namespace

TEST(DebugCoverageWithScriptDataOutOfScope) {
  i::v8_flags.always_turbofan = false;
  LocalContext env;
  v8::Isolate* isolate = env->GetIsolate();
  v8::HandleScope scope(isolate);
  v8::debug::Coverage::SelectMode(isolate,
                                  v8::debug::CoverageMode::kPreciseCount);
  v8::Local<v8::String> source = v8_str(
      "function f() {\n"
      "}\n"
      "f();\n"
      "f();");
  CompileRun(source);

  v8::debug::Coverage::FunctionData function_data =
      GetFunctionDataAndDeleteCoverage(isolate);
  CHECK_EQ(1, function_data.Count());
  CHECK_EQ(0, function_data.StartOffset());
  CHECK_EQ(26, function_data.EndOffset());
}

TEST(DebugGetPossibleBreakpointsReturnLocations) {
  LocalContext env;
  v8::Isolate* isolate = env->GetIsolate();
  v8::HandleScope scope(isolate);
  v8::Local<v8::String> source = v8_str(
      "function fib(x) {\n"
      "  if (x < 0) return;\n"
      "  if (x === 0) return 1;\n"
      "  if (x === 1) return fib(0);\n"
      "  return x > 2 ? fib(x - 1) + fib(x - 2) : fib(1) + fib(0);\n"
      "}");
  CompileRun(source);
  std::vector<v8::Global<v8::debug::Script>> scripts;
  v8::debug::GetLoadedScripts(isolate, scripts);
  CHECK_EQ(scripts.size(), 1);
  std::vector<v8::debug::BreakLocation> locations;
  CHECK(scripts[0].Get(isolate)->GetPossibleBreakpoints(
      v8::debug::Location(0, 17), v8::debug::Location(), true, &locations));
  int returns_count = 0;
  for (size_t i = 0; i < locations.size(); ++i) {
    if (locations[i].type() == v8::debug::kReturnBreakLocation) {
      ++returns_count;
    }
  }
  // With Ignition we generate one return location per return statement,
  // each has line = 5, column = 0 as statement position.
  CHECK_EQ(returns_count, 4);
}

TEST(DebugEvaluateNoSideEffect) {
  LocalContext env;
  v8::HandleScope scope(env->GetIsolate());
  EnableDebugger(env->GetIsolate());
  i::Isolate* isolate = CcTest::i_isolate();
  std::vector<i::Handle<i::JSFunction>> all_functions;
  {
    i::HeapObjectIterator iterator(isolate->heap());
    for (i::Tagged<i::HeapObject> obj = iterator.Next(); !obj.is_null();
         obj = iterator.Next()) {
      if (!IsJSFunction(obj)) continue;
      i::Tagged<i::JSFunction> fun = i::Cast<i::JSFunction>(obj);
      all_functions.emplace_back(fun, isolate);
    }
  }

  // Perform side effect check on all built-in functions. The side effect check
  // itself contains additional sanity checks.
  for (i::Handle<i::JSFunction> fun : all_functions) {
    bool failed = false;
    isolate->debug()->StartSideEffectCheckMode();
    failed = !isolate->debug()->PerformSideEffectCheck(
        fun, v8::Utils::OpenHandle(*env->Global()));
    isolate->debug()->StopSideEffectCheckMode();
    if (failed) isolate->clear_exception();
  }
  DisableDebugger(env->GetIsolate());
}

TEST(DebugEvaluateGlobalSharedCrossOrigin) {
  LocalContext env;
  v8::Isolate* isolate = env->GetIsolate();
  v8::HandleScope scope(isolate);
  v8::TryCatch tryCatch(isolate);
  tryCatch.SetCaptureMessage(true);
  v8::MaybeLocal<v8::Value> result =
      v8::debug::EvaluateGlobal(isolate, v8_str(isolate, "throw new Error()"),
                                v8::debug::EvaluateGlobalMode::kDefault);
  CHECK(result.IsEmpty());
  CHECK(tryCatch.HasCaught());
  CHECK(tryCatch.Message()->IsSharedCrossOrigin());
}

TEST(DebugEvaluateLocalSharedCrossOrigin) {
  struct BreakProgramDelegate : public v8::debug::DebugDelegate {
    void BreakProgramRequested(v8::Local<v8::Context> context,
                               std::vector<v8::debug::BreakpointId> const&,
                               v8::debug::BreakReasons) final {
      v8::Isolate* isolate = context->GetIsolate();
      v8::TryCatch tryCatch(isolate);
      tryCatch.SetCaptureMessage(true);
      std::unique_ptr<v8::debug::StackTraceIterator> it =
          v8::debug::StackTraceIterator::Create(isolate);
      v8::MaybeLocal<v8::Value> result =
          it->Evaluate(v8_str(isolate, "throw new Error()"), false);
      CHECK(result.IsEmpty());
      CHECK(tryCatch.HasCaught());
      CHECK(tryCatch.Message()->IsSharedCrossOrigin());
    }
  } delegate;
  LocalContext env;
  v8::Isolate* isolate = env->GetIsolate();
  v8::HandleScope scope(isolate);
  v8::debug::SetDebugDelegate(isolate, &delegate);
  v8::Script::Compile(env.local(), v8_str(isolate, "debugger;"))
      .ToLocalChecked()
      ->Run(env.local())
      .ToLocalChecked();
  v8::debug::SetDebugDelegate(isolate, nullptr);
}

TEST(DebugEvaluateImportMetaInScript) {
  struct BreakProgramDelegate : public v8::debug::DebugDelegate {
    void BreakProgramRequested(v8::Local<v8::Context> context,
                               std::vector<v8::debug::BreakpointId> const&,
                               v8::debug::BreakReasons) final {
      v8::Isolate* isolate = context->GetIsolate();
      v8::TryCatch tryCatch(isolate);
      tryCatch.SetCaptureMessage(true);
      std::unique_ptr<v8::debug::StackTraceIterator> it =
          v8::debug::StackTraceIterator::Create(isolate);
      auto result =
          it->Evaluate(v8_str(isolate, "import.meta"), false).ToLocalChecked();

      // Within the context of a devtools evaluation, import.meta is
      // always permitted, and will return `undefined` when outside of a
      // module.
      CHECK(result->IsUndefined());
      CHECK(!tryCatch.HasCaught());
    }
  } delegate;
  LocalContext env;
  v8::Isolate* isolate = env->GetIsolate();
  v8::HandleScope scope(isolate);
  v8::debug::SetDebugDelegate(isolate, &delegate);
  v8::Script::Compile(env.local(), v8_str(isolate, "debugger;"))
      .ToLocalChecked()
      ->Run(env.local())
      .ToLocalChecked();
  v8::debug::SetDebugDelegate(isolate, nullptr);
}

static v8::MaybeLocal<v8::Module> UnexpectedModuleResolveCallback(
    v8::Local<v8::Context> context, v8::Local<v8::String> specifier,
    v8::Local<v8::FixedArray> import_assertions,
    v8::Local<v8::Module> referrer) {
  CHECK_WITH_MSG(false, "Unexpected call to resolve callback");
}

TEST(DebugEvaluateImportMetaInModule) {
  struct BreakProgramDelegate : public v8::debug::DebugDelegate {
    void BreakProgramRequested(v8::Local<v8::Context> context,
                               std::vector<v8::debug::BreakpointId> const&,
                               v8::debug::BreakReasons) final {
      v8::Isolate* isolate = context->GetIsolate();
      v8::TryCatch tryCatch(isolate);
      tryCatch.SetCaptureMessage(true);
      std::unique_ptr<v8::debug::StackTraceIterator> it =
          v8::debug::StackTraceIterator::Create(isolate);
      auto result =
          it->Evaluate(v8_str(isolate, "import.meta"), false).ToLocalChecked();
      CHECK(result->IsObject());
      CHECK(!tryCatch.HasCaught());
    }
  } delegate;
  LocalContext env;
  v8::Isolate* isolate = env->GetIsolate();
  v8::HandleScope scope(isolate);
  v8::debug::SetDebugDelegate(isolate, &delegate);

  v8::ScriptOrigin script_origin(v8_str("test"), 0, 0, false, -1,
                                 v8::Local<v8::Value>(), false, false, true);
  v8::ScriptCompiler::Source script_compiler_source(v8_str("debugger;"),
                                                    script_origin);
  v8::Local<v8::Module> module =
      v8::ScriptCompiler::CompileModule(isolate, &script_compiler_source)
          .ToLocalChecked();

  CHECK_EQ(
      module->InstantiateModule(env.local(), UnexpectedModuleResolveCallback)
          .ToChecked(),
      true);
  module->Evaluate(env.local()).ToLocalChecked();

  v8::debug::SetDebugDelegate(isolate, nullptr);
}

namespace {
i::MaybeHandle<i::Script> FindScript(
    i::Isolate* isolate, const std::vector<i::Handle<i::Script>>& scripts,
    const char* name) {
  DirectHandle<i::String> i_name =
      isolate->factory()->NewStringFromAsciiChecked(name);
  for (const auto& script : scripts) {
    if (!IsString(script->name())) continue;
    if (i_name->Equals(i::Cast<i::String>(script->name()))) return script;
  }
  return i::MaybeHandle<i::Script>();
}
}  // anonymous namespace

UNINITIALIZED_TEST(LoadedAtStartupScripts) {
  i::v8_flags.expose_gc = true;

  v8::Isolate::CreateParams create_params;
  create_params.array_buffer_allocator = CcTest::array_buffer_allocator();
  v8::Isolate* isolate = v8::Isolate::New(create_params);
  i::Isolate* i_isolate = reinterpret_cast<i::Isolate*>(isolate);
  {
    v8::Isolate::Scope i_scope(isolate);
    v8::HandleScope scope(isolate);
    LocalContext context(isolate);

    std::vector<i::Handle<i::Script>> scripts;
    CompileWithOrigin(v8_str("function foo(){}"), v8_str("normal.js"), false);
    std::unordered_map<i::Script::Type, int> count_by_type;
    {
      i::DisallowGarbageCollection no_gc;
      i::Script::Iterator iterator(i_isolate);
      for (i::Tagged<i::Script> script = iterator.Next(); !script.is_null();
           script = iterator.Next()) {
        if (script->type() == i::Script::Type::kNative &&
            IsUndefined(script->name(), i_isolate)) {
          continue;
        }
        ++count_by_type[script->type()];
        scripts.emplace_back(script, i_isolate);
      }
    }
    CHECK_EQ(count_by_type[i::Script::Type::kNative], 0);
    CHECK_EQ(count_by_type[i::Script::Type::kExtension], 1);
    CHECK_EQ(count_by_type[i::Script::Type::kNormal], 1);
#if V8_ENABLE_WEBASSEMBLY
    CHECK_EQ(count_by_type[i::Script::Type::kWasm], 0);
#endif  // V8_ENABLE_WEBASSEMBLY
    CHECK_EQ(count_by_type[i::Script::Type::kInspector], 0);

    i::DirectHandle<i::Script> gc_script =
        FindScript(i_isolate, scripts, "v8/gc").ToHandleChecked();
    CHECK_EQ(gc_script->type(), i::Script::Type::kExtension);

    i::DirectHandle<i::Script> normal_script =
        FindScript(i_isolate, scripts, "normal.js").ToHandleChecked();
    CHECK_EQ(normal_script->type(), i::Script::Type::kNormal);
  }
  isolate->Dispose();
}

TEST(SourceInfo) {
  LocalContext env;
  v8::HandleScope scope(env->GetIsolate());
  const char* source =
      "//\n"
      "function a() { b(); };\n"
      "function    b() {\n"
      "  c(true);\n"
      "};\n"
      "  function c(x) {\n"
      "    if (x) {\n"
      "      return 1;\n"
      "    } else {\n"
      "      return 1;\n"
      "    }\n"
      "  };\n"
      "function d(x) {\n"
      "  x = 1 ;\n"
      "  x = 2 ;\n"
      "  x = 3 ;\n"
      "  x = 4 ;\n"
      "  x = 5 ;\n"
      "  x = 6 ;\n"
      "  x = 7 ;\n"
      "  x = 8 ;\n"
      "  x = 9 ;\n"
      "  x = 10;\n"
      "  x = 11;\n"
      "  x = 12;\n"
      "  x = 13;\n"
      "  x = 14;\n"
      "  x = 15;\n"
      "}\n";
  v8::Local<v8::Script> v8_script =
      v8::Script::Compile(env.local(), v8_str(source)).ToLocalChecked();
  i::DirectHandle<i::Script> i_script(
      i::Cast<i::Script>(
          v8::Utils::OpenDirectHandle(*v8_script)->shared()->script()),
      CcTest::i_isolate());
  v8::Local<v8::debug::Script> script =
      v8::ToApiHandle<v8::debug::Script>(i_script);

  // Test that when running through source positions the position, line and
  // column progresses as expected.
  v8::debug::Location prev_location = script->GetSourceLocation(0);
  CHECK_EQ(prev_location.GetLineNumber(), 0);
  CHECK_EQ(prev_location.GetColumnNumber(), 0);
  for (int offset = 1; offset < 100; ++offset) {
    v8::debug::Location location = script->GetSourceLocation(offset);
    if (prev_location.GetLineNumber() == location.GetLineNumber()) {
      CHECK_EQ(location.GetColumnNumber(), prev_location.GetColumnNumber() + 1);
    } else {
      CHECK_EQ(location.GetLineNumber(), prev_location.GetLineNumber() + 1);
      CHECK_EQ(location.GetColumnNumber(), 0);
    }
    prev_location = location;
  }

  // Every line of d() is the same length.  Verify we can loop through all
  // positions and find the right line # for each.
  // The position of the first line of d(), i.e. "x = 1 ;".
  const int start_line_d = 13;
  const int start_code_d =
      static_cast<int>(strstr(source, "  x = 1 ;") - source);
  const int num_lines_d = 15;
  const int line_length_d = 10;
  int p = start_code_d;
  for (int line = 0; line < num_lines_d; ++line) {
    for (int column = 0; column < line_length_d; ++column) {
      v8::debug::Location location = script->GetSourceLocation(p);
      CHECK_EQ(location.GetLineNumber(), start_line_d + line);
      CHECK_EQ(location.GetColumnNumber(), column);
      ++p;
    }
  }

  // Test first position.
  CHECK_EQ(script->GetSourceLocation(0).GetLineNumber(), 0);
  CHECK_EQ(script->GetSourceLocation(0).GetColumnNumber(), 0);

  // Test second position.
  CHECK_EQ(script->GetSourceLocation(1).GetLineNumber(), 0);
  CHECK_EQ(script->GetSourceLocation(1).GetColumnNumber(), 1);

  // Test first position in function a().
  const int start_a =
      static_cast<int>(strstr(source, "function a") - source) + 10;
  CHECK_EQ(script->GetSourceLocation(start_a).GetLineNumber(), 1);
  CHECK_EQ(script->GetSourceLocation(start_a).GetColumnNumber(), 10);

  // Test first position in function b().
  const int start_b =
      static_cast<int>(strstr(source, "function    b") - source) + 13;
  CHECK_EQ(script->GetSourceLocation(start_b).GetLineNumber(), 2);
  CHECK_EQ(script->GetSourceLocation(start_b).GetColumnNumber(), 13);

  // Test first position in function c().
  const int start_c =
      static_cast<int>(strstr(source, "function c") - source) + 10;
  CHECK_EQ(script->GetSourceLocation(start_c).GetLineNumber(), 5);
  CHECK_EQ(script->GetSourceLocation(start_c).GetColumnNumber(), 12);

  // Test first position in function d().
  const int start_d =
      static_cast<int>(strstr(source, "function d") - source) + 10;
  CHECK_EQ(script->GetSourceLocation(start_d).GetLineNumber(), 12);
  CHECK_EQ(script->GetSourceLocation(start_d).GetColumnNumber(), 10);

  // Test offsets.
  CHECK_EQ(script->GetSourceOffset(v8::debug::Location(1, 10)),
           v8::Just(start_a));
  CHECK_EQ(script->GetSourceOffset(v8::debug::Location(2, 13)),
           v8::Just(start_b));
  CHECK_EQ(script->GetSourceOffset(v8::debug::Location(3, 0)),
           v8::Just(start_b + 5));
  CHECK_EQ(script->GetSourceOffset(v8::debug::Location(3, 2)),
           v8::Just(start_b + 7));
  CHECK_EQ(script->GetSourceOffset(v8::debug::Location(4, 0)),
           v8::Just(start_b + 16));
  CHECK_EQ(script->GetSourceOffset(v8::debug::Location(5, 12)),
           v8::Just(start_c));
  CHECK_EQ(script->GetSourceOffset(v8::debug::Location(6, 0)),
           v8::Just(start_c + 6));
  CHECK_EQ(script->GetSourceOffset(v8::debug::Location(7, 0)),
           v8::Just(start_c + 19));
  CHECK_EQ(script->GetSourceOffset(v8::debug::Location(8, 0)),
           v8::Just(start_c + 35));
  CHECK_EQ(script->GetSourceOffset(v8::debug::Location(9, 0)),
           v8::Just(start_c + 48));
  CHECK_EQ(script->GetSourceOffset(v8::debug::Location(10, 0)),
           v8::Just(start_c + 64));
  CHECK_EQ(script->GetSourceOffset(v8::debug::Location(11, 0)),
           v8::Just(start_c + 70));
  CHECK_EQ(script->GetSourceOffset(v8::debug::Location(12, 10)),
           v8::Just(start_d));
  CHECK_EQ(script->GetSourceOffset(v8::debug::Location(13, 0)),
           v8::Just(start_d + 6));
  for (int i = 1; i <= num_lines_d; ++i) {
    CHECK_EQ(script->GetSourceOffset(v8::debug::Location(start_line_d + i, 0)),
             v8::Just(6 + (i * line_length_d) + start_d));
  }
  CHECK_EQ(script->GetSourceOffset(v8::debug::Location(start_line_d + 17, 0)),
           v8::Nothing<int>());

  // Make sure invalid inputs work properly.
  const int last_position = static_cast<int>(strlen(source)) - 1;
  CHECK_EQ(script->GetSourceLocation(-1).GetLineNumber(), 0);
  CHECK_EQ(script->GetSourceLocation(last_position + 2).GetLineNumber(),
           i::kNoSourcePosition);

  // Test last position.
  CHECK_EQ(script->GetSourceLocation(last_position).GetLineNumber(), 28);
  CHECK_EQ(script->GetSourceLocation(last_position).GetColumnNumber(), 1);
  CHECK_EQ(script->GetSourceLocation(last_position + 1).GetLineNumber(), 29);
  CHECK_EQ(script->GetSourceLocation(last_position + 1).GetColumnNumber(), 0);
}

namespace {
class SetBreakpointOnScriptCompiled : public v8::debug::DebugDelegate {
 public:
  void ScriptCompiled(v8::Local<v8::debug::Script> script, bool is_live_edited,
                      bool has_compile_error) override {
    v8::Local<v8::String> name;
    if (!script->SourceURL().ToLocal(&name)) return;
    v8::Local<v8::Context> context = CcTest::isolate()->GetCurrentContext();
    if (!name->Equals(context, v8_str("test")).FromJust()) return;
    CHECK(!has_compile_error);
    v8::debug::Location loc(1, 2);
    CHECK(script->SetBreakpoint(v8_str(""), &loc, &id_));
    CHECK_EQ(loc.GetLineNumber(), 1);
    CHECK_EQ(loc.GetColumnNumber(), 10);
  }

  void BreakProgramRequested(
      v8::Local<v8::Context> paused_context,
      const std::vector<v8::debug::BreakpointId>& inspector_break_points_hit,
      v8::debug::BreakReasons break_reasons) override {
    ++break_count_;
    CHECK_EQ(inspector_break_points_hit[0], id_);
  }

  int break_count() const { return break_count_; }

 private:
  int break_count_ = 0;
  v8::debug::BreakpointId id_;
};
}  // anonymous namespace

TEST(Regress517592) {
  LocalContext env;
  v8::HandleScope handle_scope(env->GetIsolate());
  SetBreakpointOnScriptCompiled delegate;
  v8::debug::SetDebugDelegate(env->GetIsolate(), &delegate);
  CompileRun(
      v8_str("eval('var foo = function foo() {\\n' +\n"
             "'  var a = 1;\\n' +\n"
             "'}\\n' +\n"
             "'//@ sourceURL=test')"));
  CHECK_EQ(delegate.break_count(), 0);
  CompileRun(v8_str("foo()"));
  CHECK_EQ(delegate.break_count(), 1);
  v8::debug::SetDebugDelegate(env->GetIsolate(), nullptr);
}

namespace {
std::string FromString(v8::Isolate* isolate, v8::Local<v8::String> str) {
  v8::String::Utf8Value utf8(isolate, str);
  return std::string(*utf8);
}
}  // namespace

TEST(GetPrivateFields) {
  LocalContext env;
  v8::Isolate* v8_isolate = CcTest::isolate();
  v8::HandleScope scope(v8_isolate);
  v8::Local<v8::Context> context = env.local();
  v8::Local<v8::String> source = v8_str(
      "var X = class {\n"
      "  #field_number = 1;\n"
      "  #field_function = function() {};\n"
      "}\n"
      "var x = new X()");
  CompileRun(source);
  v8::Local<v8::Object> object = v8::Local<v8::Object>::Cast(
      env->Global()
          ->Get(context, v8_str(env->GetIsolate(), "x"))
          .ToLocalChecked());
  v8::LocalVector<v8::Value> names(v8_isolate);
  v8::LocalVector<v8::Value> values(v8_isolate);
  int filter = static_cast<int>(v8::debug::PrivateMemberFilter::kPrivateFields);
  CHECK(v8::debug::GetPrivateMembers(context, object, filter, &names, &values));

  CHECK_EQ(names.size(), 2);
  for (int i = 0; i < 2; i++) {
    v8::Local<v8::Value> name = names[i];
    v8::Local<v8::Value> value = values[i];
    CHECK(name->IsString());
    std::string name_str = FromString(v8_isolate, name.As<v8::String>());
    if (name_str == "#field_number") {
      CHECK(value->Equals(context, v8_num(1)).FromJust());
    } else {
      CHECK_EQ(name_str, "#field_function");
      CHECK(value->IsFunction());
    }
  }

  source = v8_str(
      "var Y = class {\n"
      "  #base_field_number = 2;\n"
      "}\n"
      "var X = class extends Y{\n"
      "  #field_number = 1;\n"
      "  #field_function = function() {};\n"
      "}\n"
      "var x = new X()");
  CompileRun(source);
  names.clear();
  values.clear();
  object = v8::Local<v8::Object>::Cast(
      env->Global()
          ->Get(context, v8_str(env->GetIsolate(), "x"))
          .ToLocalChecked());
  CHECK(v8::debug::GetPrivateMembers(context, object, filter, &names, &values));

  CHECK_EQ(names.size(), 3);
  for (int i = 0; i < 3; i++) {
    v8::Local<v8::Value> name = names[i];
    v8::Local<v8::Value> value = values[i];
    std::string name_str = FromString(v8_isolate, name.As<v8::String>());
    if (name_str == "#base_field_number") {
      CHECK(value->Equals(context, v8_num(2)).FromJust());
    } else if (name_str == "#field_number") {
      CHECK(value->Equals(context, v8_num(1)).FromJust());
    } else {
      CHECK_EQ(name_str, "#field_function");
      CHECK(value->IsFunction());
    }
  }

  source = v8_str(
      "var Y = class {\n"
      "  constructor() {"
      "    return new Proxy({}, {});"
      "  }"
      "}\n"
      "var X = class extends Y{\n"
      "  #field_number = 1;\n"
      "  #field_function = function() {};\n"
      "}\n"
      "var x = new X()");
  CompileRun(source);
  names.clear();
  values.clear();
  object = v8::Local<v8::Object>::Cast(
      env->Global()
          ->Get(context, v8_str(env->GetIsolate(), "x"))
          .ToLocalChecked());
  CHECK(v8::debug::GetPrivateMembers(context, object, filter, &names, &values));

  CHECK_EQ(names.size(), 2);
  for (int i = 0; i < 2; i++) {
    v8::Local<v8::Value> name = names[i];
    v8::Local<v8::Value> value = values[i];
    CHECK(name->IsString());
    std::string name_str = FromString(v8_isolate, name.As<v8::String>());
    if (name_str == "#field_number") {
      CHECK(value->Equals(context, v8_num(1)).FromJust());
    } else {
      CHECK_EQ(name_str, "#field_function");
      CHECK(value->IsFunction());
    }
  }
}

TEST(GetPrivateMethodsAndAccessors) {
  LocalContext env;
  v8::Isolate* v8_isolate = CcTest::isolate();
  v8::HandleScope scope(v8_isolate);
  v8::Local<v8::Context> context = env.local();

  v8::Local<v8::String> source = v8_str(
      "var X = class {\n"
      "  #method() { }\n"
      "  get #accessor() { }\n"
      "  set #accessor(val) { }\n"
      "  get #readOnly() { }\n"
      "  set #writeOnly(val) { }\n"
      "}\n"
      "var x = new X()");
  CompileRun(source);
  v8::Local<v8::Object> object = v8::Local<v8::Object>::Cast(
      env->Global()
          ->Get(context, v8_str(env->GetIsolate(), "x"))
          .ToLocalChecked());
  v8::LocalVector<v8::Value> names(v8_isolate);
  v8::LocalVector<v8::Value> values(v8_isolate);

  int accessor_filter =
      static_cast<int>(v8::debug::PrivateMemberFilter::kPrivateAccessors);
  int method_filter =
      static_cast<int>(v8::debug::PrivateMemberFilter::kPrivateMethods);

  CHECK(v8::debug::GetPrivateMembers(context, object, method_filter, &names,
                                     &values));
  CHECK_EQ(names.size(), 1);
  {
    v8::Local<v8::Value> name = names[0];
    v8::Local<v8::Value> value = values[0];
    CHECK(name->IsString());
    CHECK(v8_str("#method")->Equals(context, name.As<v8::String>()).FromJust());
    CHECK(value->IsFunction());
  }

  names.clear();
  values.clear();
  CHECK(v8::debug::GetPrivateMembers(context, object, accessor_filter, &names,
                                     &values));
  CHECK_EQ(names.size(), 3);
  for (int i = 0; i < 3; i++) {
    v8::Local<v8::Value> name = names[i];
    v8::Local<v8::Value> value = values[i];
    CHECK(name->IsString());
    std::string name_str = FromString(v8_isolate, name.As<v8::String>());
    CHECK(v8::debug::AccessorPair::IsAccessorPair(value));
    v8::Local<v8::debug::AccessorPair> accessors =
        value.As<v8::debug::AccessorPair>();
    if (name_str == "#accessor") {
      CHECK(accessors->getter()->IsFunction());
      CHECK(accessors->setter()->IsFunction());
    } else if (name_str == "#readOnly") {
      CHECK(accessors->getter()->IsFunction());
      CHECK(accessors->setter()->IsNull());
    } else {
      CHECK_EQ(name_str, "#writeOnly");
      CHECK(accessors->getter()->IsNull());
      CHECK(accessors->setter()->IsFunction());
    }
  }

  source = v8_str(
      "var Y = class {\n"
      "  #method() {}\n"
      "  get #accessor() {}\n"
      "  set #accessor(val) {};\n"
      "}\n"
      "var X = class extends Y{\n"
      "  get #readOnly() {}\n"
      "  set #writeOnly(val) {};\n"
      "}\n"
      "var x = new X()");
  CompileRun(source);
  names.clear();
  values.clear();
  object = v8::Local<v8::Object>::Cast(
      env->Global()
          ->Get(context, v8_str(env->GetIsolate(), "x"))
          .ToLocalChecked());

  CHECK(v8::debug::GetPrivateMembers(context, object, method_filter, &names,
                                     &values));
  CHECK_EQ(names.size(), 1);
  {
    v8::Local<v8::Value> name = names[0];
    v8::Local<v8::Value> value = values[0];
    CHECK(name->IsString());
    CHECK(v8_str("#method")->Equals(context, name.As<v8::String>()).FromJust());
    CHECK(value->IsFunction());
  }

  names.clear();
  values.clear();
  CHECK(v8::debug::GetPrivateMembers(context, object, accessor_filter, &names,
                                     &values));
  CHECK_EQ(names.size(), 3);
  for (int i = 0; i < 3; i++) {
    v8::Local<v8::Value> name = names[i];
    v8::Local<v8::Value> value = values[i];
    CHECK(name->IsString());
    std::string name_str = FromString(v8_isolate, name.As<v8::String>());
    CHECK(v8::debug::AccessorPair::IsAccessorPair(value));
    v8::Local<v8::debug::AccessorPair> accessors =
        value.As<v8::debug::AccessorPair>();
    if (name_str == "#accessor") {
      CHECK(accessors->getter()->IsFunction());
      CHECK(accessors->setter()->IsFunction());
    } else if (name_str == "#readOnly") {
      CHECK(accessors->getter()->IsFunction());
      CHECK(accessors->setter()->IsNull());
    } else {
      CHECK_EQ(name_str, "#writeOnly");
      CHECK(accessors->getter()->IsNull());
      CHECK(accessors->setter()->IsFunction());
    }
  }

  source = v8_str(
      "var Y = class {\n"
      "  constructor() {"
      "    return new Proxy({}, {});"
      "  }"
      "}\n"
      "var X = class extends Y{\n"
      "  #method() {}\n"
      "  get #accessor() {}\n"
      "  set #accessor(val) {};\n"
      "}\n"
      "var x = new X()");
  CompileRun(source);
  names.clear();
  values.clear();
  object = v8::Local<v8::Object>::Cast(
      env->Global()
          ->Get(context, v8_str(env->GetIsolate(), "x"))
          .ToLocalChecked());

  CHECK(v8::debug::GetPrivateMembers(context, object, method_filter, &names,
                                     &values));
  CHECK_EQ(names.size(), 1);
  {
    v8::Local<v8::Value> name = names[0];
    v8::Local<v8::Value> value = values[0];
    CHECK(name->IsString());
    CHECK(v8_str("#method")->Equals(context, name.As<v8::String>()).FromJust());
    CHECK(value->IsFunction());
  }

  names.clear();
  values.clear();
  CHECK(v8::debug::GetPrivateMembers(context, object, accessor_filter, &names,
                                     &values));
  CHECK_EQ(names.size(), 1);
  {
    v8::Local<v8::Value> name = names[0];
    v8::Local<v8::Value> value = values[0];
    CHECK(name->IsString());
    CHECK(
        v8_str("#accessor")->Equals(context, name.As<v8::String>()).FromJust());
    CHECK(v8::debug::AccessorPair::IsAccessorPair(value));
    v8::Local<v8::debug::AccessorPair> accessors =
        value.As<v8::debug::AccessorPair>();
    CHECK(accessors->getter()->IsFunction());
    CHECK(accessors->setter()->IsFunction());
  }
}

TEST(GetPrivateStaticMethodsAndAccessors) {
  LocalContext env;
  v8::Isolate* v8_isolate = CcTest::isolate();
  v8::HandleScope scope(v8_isolate);
  v8::Local<v8::Context> context = env.local();

  v8::Local<v8::String> source = v8_str(
      "var X = class {\n"
      "  static #staticMethod() { }\n"
      "  static get #staticAccessor() { }\n"
      "  static set #staticAccessor(val) { }\n"
      "  static get #staticReadOnly() { }\n"
      "  static set #staticWriteOnly(val) { }\n"
      "}\n");
  CompileRun(source);
  v8::Local<v8::Object> object = v8::Local<v8::Object>::Cast(
      env->Global()
          ->Get(context, v8_str(env->GetIsolate(), "X"))
          .ToLocalChecked());
  v8::LocalVector<v8::Value> names(v8_isolate);
  v8::LocalVector<v8::Value> values(v8_isolate);

  int accessor_filter =
      static_cast<int>(v8::debug::PrivateMemberFilter::kPrivateAccessors);
  int method_filter =
      static_cast<int>(v8::debug::PrivateMemberFilter::kPrivateMethods);

  CHECK(v8::debug::GetPrivateMembers(context, object, method_filter, &names,
                                     &values));
  CHECK_EQ(names.size(), 1);
  {
    v8::Local<v8::Value> name = names[0];
    v8::Local<v8::Value> value = values[0];
    CHECK(name->IsString());
    CHECK(v8_str("#staticMethod")
              ->Equals(context, name.As<v8::String>())
              .FromJust());
    CHECK(value->IsFunction());
  }

  names.clear();
  values.clear();
  CHECK(v8::debug::GetPrivateMembers(context, object, accessor_filter, &names,
                                     &values));
  CHECK_EQ(names.size(), 3);
  for (int i = 0; i < 3; i++) {
    v8::Local<v8::Value> name = names[i];
    v8::Local<v8::Value> value = values[i];
    CHECK(name->IsString());
    std::string name_str = FromString(v8_isolate, name.As<v8::String>());
    CHECK(v8::debug::AccessorPair::IsAccessorPair(value));
    v8::Local<v8::debug::AccessorPair> accessors =
        value.As<v8::debug::AccessorPair>();
    if (name_str == "#staticAccessor") {
      CHECK(accessors->getter()->IsFunction());
      CHECK(accessors->setter()->IsFunction());
    } else if (name_str == "#staticReadOnly") {
      CHECK(accessors->getter()->IsFunction());
      CHECK(accessors->setter()->IsNull());
    } else {
      CHECK_EQ(name_str, "#staticWriteOnly");
      CHECK(accessors->getter()->IsNull());
      CHECK(accessors->setter()->IsFunction());
    }
  }
}

TEST(GetPrivateStaticAndInstanceMethodsAndAccessors) {
  LocalContext env;
  v8::Isolate* v8_isolate = CcTest::isolate();
  v8::HandleScope scope(v8_isolate);
  v8::Local<v8::Context> context = env.local();

  v8::Local<v8::String> source = v8_str(
      "var X = class {\n"
      "  static #staticMethod() { }\n"
      "  static get #staticAccessor() { }\n"
      "  static set #staticAccessor(val) { }\n"
      "  static get #staticReadOnly() { }\n"
      "  static set #staticWriteOnly(val) { }\n"
      "  #method() { }\n"
      "  get #accessor() { }\n"
      "  set #accessor(val) { }\n"
      "  get #readOnly() { }\n"
      "  set #writeOnly(val) { }\n"
      "}\n"
      "var x = new X()\n");
  CompileRun(source);
  v8::Local<v8::Object> object = v8::Local<v8::Object>::Cast(
      env->Global()
          ->Get(context, v8_str(env->GetIsolate(), "X"))
          .ToLocalChecked());
  v8::LocalVector<v8::Value> names(v8_isolate);
  v8::LocalVector<v8::Value> values(v8_isolate);
  int accessor_filter =
      static_cast<int>(v8::debug::PrivateMemberFilter::kPrivateAccessors);
  int method_filter =
      static_cast<int>(v8::debug::PrivateMemberFilter::kPrivateMethods);

  CHECK(v8::debug::GetPrivateMembers(context, object, method_filter, &names,
                                     &values));
  CHECK_EQ(names.size(), 1);
  {
    v8::Local<v8::Value> name = names[0];
    v8::Local<v8::Value> value = values[0];
    CHECK(name->IsString());
    CHECK(v8_str("#staticMethod")
              ->Equals(context, name.As<v8::String>())
              .FromJust());
    CHECK(value->IsFunction());
  }

  names.clear();
  values.clear();
  CHECK(v8::debug::GetPrivateMembers(context, object, accessor_filter, &names,
                                     &values));
  CHECK_EQ(names.size(), 3);
  for (int i = 0; i < 3; i++) {
    v8::Local<v8::Value> name = names[i];
    v8::Local<v8::Value> value = values[i];
    CHECK(name->IsString());
    std::string name_str = FromString(v8_isolate, name.As<v8::String>());
    CHECK(v8::debug::AccessorPair::IsAccessorPair(value));
    v8::Local<v8::debug::AccessorPair> accessors =
        value.As<v8::debug::AccessorPair>();
    if (name_str == "#staticAccessor") {
      CHECK(accessors->getter()->IsFunction());
      CHECK(accessors->setter()->IsFunction());
    } else if (name_str == "#staticReadOnly") {
      CHECK(accessors->getter()->IsFunction());
      CHECK(accessors->setter()->IsNull());
    } else {
      CHECK_EQ(name_str, "#staticWriteOnly");
      CHECK(accessors->getter()->IsNull());
      CHECK(accessors->setter()->IsFunction());
    }
  }

  names.clear();
  values.clear();
  object = v8::Local<v8::Object>::Cast(
      env->Global()
          ->Get(context, v8_str(env->GetIsolate(), "x"))
          .ToLocalChecked());
  CHECK(v8::debug::GetPrivateMembers(context, object, method_filter, &names,
                                     &values));
  CHECK_EQ(names.size(), 1);
  {
    v8::Local<v8::Value> name = names[0];
    v8::Local<v8::Value> value = values[0];
    CHECK(name->IsString());
    CHECK(v8_str("#method")->Equals(context, name.As<v8::String>()).FromJust());
    CHECK(value->IsFunction());
  }

  names.clear();
  values.clear();
  CHECK(v8::debug::GetPrivateMembers(context, object, accessor_filter, &names,
                                     &values));
  CHECK_EQ(names.size(), 3);
  for (int i = 0; i < 3; i++) {
    v8::Local<v8::Value> name = names[i];
    v8::Local<v8::Value> value = values[i];
    CHECK(name->IsString());
    std::string name_str = FromString(v8_isolate, name.As<v8::String>());
    CHECK(v8::debug::AccessorPair::IsAccessorPair(value));
    v8::Local<v8::debug::AccessorPair> accessors =
        value.As<v8::debug::AccessorPair>();
    if (name_str == "#accessor") {
      CHECK(accessors->getter()->IsFunction());
      CHECK(accessors->setter()->IsFunction());
    } else if (name_str == "#readOnly") {
      CHECK(accessors->getter()->IsFunction());
      CHECK(accessors->setter()->IsNull());
    } else {
      CHECK_EQ(name_str, "#writeOnly");
      CHECK(accessors->getter()->IsNull());
      CHECK(accessors->setter()->IsFunction());
    }
  }
}

TEST(GetPrivateAutoAccessors) {
  i::v8_flags.js_decorators = true;
  LocalContext env;
  v8::Isolate* v8_isolate = CcTest::isolate();
  v8::HandleScope scope(v8_isolate);
  v8::Local<v8::Context> context = env.local();
  v8::Local<v8::String> source = v8_str(
      "var Y = class {\n"
      "  static accessor #static_base_field = 4;\n"
      "  accessor #base_field = 3;\n"
      "}\n"
      "var X = class extends Y{\n"
      "  static accessor #static_field = 2\n;"
      "  accessor #field = 1;\n"
      "}\n"
      "var y = new Y();\n"
      "var x = new X();");
  CompileRun(source);
  int field_filter =
      static_cast<int>(v8::debug::PrivateMemberFilter::kPrivateFields);
  int accessor_filter =
      static_cast<int>(v8::debug::PrivateMemberFilter::kPrivateAccessors);

  v8::Local<v8::Object> object = v8::Local<v8::Object>::Cast(
      env->Global()
          ->Get(context, v8_str(env->GetIsolate(), "Y"))
          .ToLocalChecked());
  v8::LocalVector<v8::Value> names(v8_isolate);
  v8::LocalVector<v8::Value> values(v8_isolate);
  CHECK(v8::debug::GetPrivateMembers(context, object, field_filter, &names,
                                     &values));

  CHECK_EQ(names.size(), 1);
  CHECK(names[0]->IsString());
  {
    std::string name_str = FromString(v8_isolate, names[0].As<v8::String>());
    CHECK_EQ(name_str, ".accessor-storage-0");
    CHECK(values[0]->Equals(context, v8_num(4)).FromJust());
  }

  names.clear();
  values.clear();
  CHECK(v8::debug::GetPrivateMembers(context, object, accessor_filter, &names,
                                     &values));

  CHECK_EQ(names.size(), 1);
  CHECK(names[0]->IsString());
  {
    std::string name_str = FromString(v8_isolate, names[0].As<v8::String>());
    CHECK(v8::debug::AccessorPair::IsAccessorPair(values[0]));
    v8::Local<v8::debug::AccessorPair> accessors =
        values[0].As<v8::debug::AccessorPair>();
    CHECK_EQ(name_str, "#static_base_field");
    CHECK(accessors->getter()->IsFunction());
    CHECK(accessors->setter()->IsFunction());
  }

  object = v8::Local<v8::Object>::Cast(
      env->Global()
          ->Get(context, v8_str(env->GetIsolate(), "y"))
          .ToLocalChecked());
  names.clear();
  values.clear();
  CHECK(v8::debug::GetPrivateMembers(context, object, field_filter, &names,
                                     &values));

  CHECK_EQ(names.size(), 1);
  CHECK(names[0]->IsString());
  {
    std::string name_str = FromString(v8_isolate, names[0].As<v8::String>());
    CHECK_EQ(name_str, ".accessor-storage-1");
    CHECK(values[0]->Equals(context, v8_num(3)).FromJust());
  }

  names.clear();
  values.clear();
  CHECK(v8::debug::GetPrivateMembers(context, object, accessor_filter, &names,
                                     &values));

  CHECK_EQ(names.size(), 1);
  CHECK(names[0]->IsString());
  {
    std::string name_str = FromString(v8_isolate, names[0].As<v8::String>());
    CHECK(v8::debug::AccessorPair::IsAccessorPair(values[0]));
    v8::Local<v8::debug::AccessorPair> accessors =
        values[0].As<v8::debug::AccessorPair>();
    CHECK_EQ(name_str, "#base_field");
    CHECK(accessors->getter()->IsFunction());
    CHECK(accessors->setter()->IsFunction());
  }

  object = v8::Local<v8::Object>::Cast(
      env->Global()
          ->Get(context, v8_str(env->GetIsolate(), "X"))
          .ToLocalChecked());
  names.clear();
  values.clear();

  CHECK(v8::debug::GetPrivateMembers(context, object, field_filter, &names,
                                     &values));

  CHECK_EQ(names.size(), 1);
  CHECK(names[0]->IsString());
  {
    std::string name_str = FromString(v8_isolate, names[0].As<v8::String>());
    CHECK_EQ(name_str, ".accessor-storage-0");
    CHECK(values[0]->Equals(context, v8_num(2)).FromJust());
  }

  names.clear();
  values.clear();
  CHECK(v8::debug::GetPrivateMembers(context, object, accessor_filter, &names,
                                     &values));

  CHECK_EQ(names.size(), 1);
  CHECK(names[0]->IsString());
  {
    std::string name_str = FromString(v8_isolate, names[0].As<v8::String>());
    CHECK(v8::debug::AccessorPair::IsAccessorPair(values[0]));
    v8::Local<v8::debug::AccessorPair> accessors =
        values[0].As<v8::debug::AccessorPair>();
    CHECK_EQ(name_str, "#static_field");
    CHECK(accessors->getter()->IsFunction());
    CHECK(accessors->setter()->IsFunction());
  }

  object = v8::Local<v8::Object>::Cast(
      env->Global()
          ->Get(context, v8_str(env->GetIsolate(), "x"))
          .ToLocalChecked());
  names.clear();
  values.clear();
  CHECK(v8::debug::GetPrivateMembers(context, object, field_filter, &names,
                                     &values));

  CHECK_EQ(names.size(), 2);
  int expected[2] = {/*#base_field=*/3, /*#field=*/1};
  for (int i = 0; i < 2; i++) {
    v8::Local<v8::Value> name = names[i];
    v8::Local<v8::Value> value = values[i];
    CHECK(name->IsString());
    std::string name_str = FromString(v8_isolate, name.As<v8::String>());
    CHECK_EQ(name_str, ".accessor-storage-1");
    CHECK(value->Equals(context, v8_num(expected[i])).FromJust());
  }

  names.clear();
  values.clear();
  CHECK(v8::debug::GetPrivateMembers(context, object, accessor_filter, &names,
                                     &values));

  CHECK_EQ(names.size(), 2);
  for (int i = 0; i < 2; i++) {
    v8::Local<v8::Value> name = names[i];
    v8::Local<v8::Value> value = values[i];
    CHECK(name->IsString());
    std::string name_str = FromString(v8_isolate, name.As<v8::String>());
    CHECK(v8::debug::AccessorPair::IsAccessorPair(value));
    v8::Local<v8::debug::AccessorPair> accessors =
        value.As<v8::debug::AccessorPair>();
    if (name_str == "#base_field") {
      CHECK(accessors->getter()->IsFunction());
      CHECK(accessors->setter()->IsFunction());
    } else {
      CHECK_EQ(name_str, "#field");
      CHECK(accessors->getter()->IsFunction());
      CHECK(accessors->setter()->IsFunction());
    }
  }
}

namespace {
class SetTerminateOnResumeDelegate : public v8::debug::DebugDelegate {
 public:
  enum Options {
    kNone,
    kPerformMicrotaskCheckpointAtBreakpoint,
    kRunJavaScriptAtBreakpoint
  };
  explicit SetTerminateOnResumeDelegate(Options options = kNone)
      : options_(options) {}
  void BreakProgramRequested(
      v8::Local<v8::Context> paused_context,
      const std::vector<v8::debug::BreakpointId>& inspector_break_points_hit,
      v8::debug::BreakReasons break_reasons) override {
    break_count_++;
    v8::Isolate* isolate = paused_context->GetIsolate();
    v8::debug::SetTerminateOnResume(isolate);
    if (options_ == kPerformMicrotaskCheckpointAtBreakpoint) {
      v8::MicrotasksScope::PerformCheckpoint(isolate);
    }
    if (options_ == kRunJavaScriptAtBreakpoint) {
      CompileRun("globalVariable = globalVariable + 1");
    }
  }

  void ExceptionThrown(v8::Local<v8::Context> paused_context,
                       v8::Local<v8::Value> exception,
                       v8::Local<v8::Value> promise, bool is_uncaught,
                       v8::debug::ExceptionType exception_type) override {
    exception_thrown_count_++;
    v8::debug::SetTerminateOnResume(paused_context->GetIsolate());
  }

  int break_count() const { return break_count_; }
  int exception_thrown_count() const { return exception_thrown_count_; }

 private:
  int break_count_ = 0;
  int exception_thrown_count_ = 0;
  Options options_;
};
}  // anonymous namespace

TEST(TerminateOnResumeAtBreakpoint) {
  break_point_hit_count = 0;
  LocalContext env;
  v8::HandleScope scope(env->GetIsolate());
  SetTerminateOnResumeDelegate delegate;
  v8::debug::SetDebugDelegate(env->GetIsolate(), &delegate);
  v8::Local<v8::Context> context = env.local();
  {
    v8::TryCatch try_catch(env->GetIsolate());
    // If the delegate doesn't request termination on resume from breakpoint,
    // foo diverges.
    v8::Script::Compile(
        context,
        v8_str(env->GetIsolate(), "function foo(){debugger; while(true){}}"))
        .ToLocalChecked()
        ->Run(context)
        .ToLocalChecked();
    v8::Local<v8::Function> foo = v8::Local<v8::Function>::Cast(
        env->Global()
            ->Get(context, v8_str(env->GetIsolate(), "foo"))
            .ToLocalChecked());

    v8::MaybeLocal<v8::Value> val =
        foo->Call(context, env->Global(), 0, nullptr);
    CHECK(val.IsEmpty());
    CHECK(try_catch.HasTerminated());
    CHECK_EQ(delegate.break_count(), 1);
  }
  // Exiting the TryCatch brought the isolate back to a state where JavaScript
  // can be executed.
  ExpectInt32("1 + 1", 2);
  v8::debug::SetDebugDelegate(env->GetIsolate(), nullptr);
  CheckDebuggerUnloaded();
}

namespace {
bool microtask_one_ran = false;
static void MicrotaskOne(const v8::FunctionCallbackInfo<v8::Value>& info) {
  CHECK(i::ValidateCallbackInfo(info));
  CHECK(v8::MicrotasksScope::IsRunningMicrotasks(info.GetIsolate()));
  v8::HandleScope scope(info.GetIsolate());
  v8::MicrotasksScope microtasks(info.GetIsolate()->GetCurrentContext(),
                                 v8::MicrotasksScope::kDoNotRunMicrotasks);
  ExpectInt32("1 + 1", 2);
  microtask_one_ran = true;
}
}  // namespace

TEST(TerminateOnResumeRunMicrotaskAtBreakpoint) {
  LocalContext env;
  v8::HandleScope scope(env->GetIsolate());
  SetTerminateOnResumeDelegate delegate(
      SetTerminateOnResumeDelegate::kPerformMicrotaskCheckpointAtBreakpoint);
  v8::debug::SetDebugDelegate(env->GetIsolate(), &delegate);
  v8::Local<v8::Context> context = env.local();
  {
    v8::TryCatch try_catch(env->GetIsolate());
    // Enqueue a microtask that gets run while we are paused at the breakpoint.
    env->GetIsolate()->EnqueueMicrotask(
        v8::Function::New(env.local(), MicrotaskOne).ToLocalChecked());

    // If the delegate doesn't request termination on resume from breakpoint,
    // foo diverges.
    v8::Script::Compile(
        context,
        v8_str(env->GetIsolate(), "function foo(){debugger; while(true){}}"))
        .ToLocalChecked()
        ->Run(context)
        .ToLocalChecked();
    v8::Local<v8::Function> foo = v8::Local<v8::Function>::Cast(
        env->Global()
            ->Get(context, v8_str(env->GetIsolate(), "foo"))
            .ToLocalChecked());

    v8::MaybeLocal<v8::Value> val =
        foo->Call(context, env->Global(), 0, nullptr);
    CHECK(val.IsEmpty());
    CHECK(try_catch.HasTerminated());
    CHECK_EQ(delegate.break_count(), 1);
    CHECK(microtask_one_ran);
  }
  // Exiting the TryCatch brought the isolate back to a state where JavaScript
  // can be executed.
  ExpectInt32("1 + 1", 2);
  v8::debug::SetDebugDelegate(env->GetIsolate(), nullptr);
  CheckDebuggerUnloaded();
}

TEST(TerminateOnResumeRunJavaScriptAtBreakpoint) {
  LocalContext env;
  v8::HandleScope scope(env->GetIsolate());
  CompileRun("var globalVariable = 0;");
  SetTerminateOnResumeDelegate delegate(
      SetTerminateOnResumeDelegate::kRunJavaScriptAtBreakpoint);
  v8::debug::SetDebugDelegate(env->GetIsolate(), &delegate);
  v8::Local<v8::Context> context = env.local();
  {
    v8::TryCatch try_catch(env->GetIsolate());
    // If the delegate doesn't request termination on resume from breakpoint,
    // foo diverges.
    v8::Script::Compile(
        context,
        v8_str(env->GetIsolate(), "function foo(){debugger; while(true){}}"))
        .ToLocalChecked()
        ->Run(context)
        .ToLocalChecked();
    v8::Local<v8::Function> foo = v8::Local<v8::Function>::Cast(
        env->Global()
            ->Get(context, v8_str(env->GetIsolate(), "foo"))
            .ToLocalChecked());

    v8::MaybeLocal<v8::Value> val =
        foo->Call(context, env->Global(), 0, nullptr);
    CHECK(val.IsEmpty());
    CHECK(try_catch.HasTerminated());
    CHECK_EQ(delegate.break_count(), 1);
  }
  // Exiting the TryCatch brought the isolate back to a state where JavaScript
  // can be executed.
  ExpectInt32("1 + 1", 2);
  ExpectInt32("globalVariable", 1);
  v8::debug::SetDebugDelegate(env->GetIsolate(), nullptr);
  CheckDebuggerUnloaded();
}

TEST(TerminateOnResumeAtException) {
  LocalContext env;
  v8::HandleScope scope(env->GetIsolate());
  ChangeBreakOnException(env->GetIsolate(), true, true);
  SetTerminateOnResumeDelegate delegate;
  v8::debug::SetDebugDelegate(env->GetIsolate(), &delegate);
  v8::Local<v8::Context> context = env.local();
  {
    v8::TryCatch try_catch(env->GetIsolate());
    const char* source = "throw new Error(); while(true){};";

    v8::ScriptCompiler::Source script_source(v8_str(source));
    v8::Local<v8::Function> foo =
        v8::ScriptCompiler::CompileFunction(env.local(), &script_source)
            .ToLocalChecked();

    v8::MaybeLocal<v8::Value> val =
        foo->Call(context, env->Global(), 0, nullptr);
    CHECK(val.IsEmpty());
    CHECK(try_catch.HasTerminated());
    CHECK_EQ(delegate.break_count(), 0);
    CHECK_EQ(delegate.exception_thrown_count(), 1);
  }
  // Exiting the TryCatch brought the isolate back to a state where JavaScript
  // can be executed.
  ExpectInt32("1 + 1", 2);
  v8::debug::SetDebugDelegate(env->GetIsolate(), nullptr);
  CheckDebuggerUnloaded();
}

TEST(TerminateOnResumeAtBreakOnEntry) {
  LocalContext env;
  v8::HandleScope scope(env->GetIsolate());
  SetTerminateOnResumeDelegate delegate;
  v8::debug::SetDebugDelegate(env->GetIsolate(), &delegate);
  {
    v8::TryCatch try_catch(env->GetIsolate());
    v8::Local<v8::Function> builtin =
        CompileRun("String.prototype.repeat").As<v8::Function>();
    SetBreakPoint(builtin, 0);
    v8::Local<v8::Value> val = CompileRun("'b'.repeat(10)");
    CHECK_EQ(delegate.break_count(), 1);
    CHECK(val.IsEmpty());
    CHECK(try_catch.HasTerminated());
    CHECK_EQ(delegate.exception_thrown_count(), 0);
  }
  // Exiting the TryCatch brought the isolate back to a state where JavaScript
  // can be executed.
  ExpectInt32("1 + 1", 2);
  v8::debug::SetDebugDelegate(env->GetIsolate(), nullptr);
  CheckDebuggerUnloaded();
}

TEST(TerminateOnResumeAtBreakOnEntryUserDefinedFunction) {
  LocalContext env;
  v8::HandleScope scope(env->GetIsolate());
  SetTerminateOnResumeDelegate delegate;
  v8::debug::SetDebugDelegate(env->GetIsolate(), &delegate);
  {
    v8::TryCatch try_catch(env->GetIsolate());
    v8::Local<v8::Function> foo =
        CompileFunction(&env, "function foo(b) { while (b > 0) {} }", "foo");

    // Run without breakpoints to compile source to bytecode.
    CompileRun("foo(-1)");
    CHECK_EQ(delegate.break_count(), 0);

    SetBreakPoint(foo, 0);
    v8::Local<v8::Value> val = CompileRun("foo(1)");
    CHECK_EQ(delegate.break_count(), 1);
    CHECK(val.IsEmpty());
    CHECK(try_catch.HasTerminated());
    CHECK_EQ(delegate.exception_thrown_count(), 0);
  }
  // Exiting the TryCatch brought the isolate back to a state where JavaScript
  // can be executed.
  ExpectInt32("1 + 1", 2);
  v8::debug::SetDebugDelegate(env->GetIsolate(), nullptr);
  CheckDebuggerUnloaded();
}

TEST(TerminateOnResumeAtUnhandledRejection) {
  LocalContext env;
  v8::HandleScope scope(env->GetIsolate());
  ChangeBreakOnException(env->GetIsolate(), true, true);
  SetTerminateOnResumeDelegate delegate;
  v8::debug::SetDebugDelegate(env->GetIsolate(), &delegate);
  v8::Local<v8::Context> context = env.local();
  {
    v8::TryCatch try_catch(env->GetIsolate());
    v8::Local<v8::Function> foo = CompileFunction(
        &env, "async function foo() { Promise.reject(); while(true) {} }",
        "foo");

    v8::MaybeLocal<v8::Value> val =
        foo->Call(context, env->Global(), 0, nullptr);
    CHECK(val.IsEmpty());
    CHECK(try_catch.HasTerminated());
    CHECK_EQ(delegate.break_count(), 0);
    CHECK_EQ(delegate.exception_thrown_count(), 1);
  }
  // Exiting the TryCatch brought the isolate back to a state where JavaScript
  // can be executed.
  ExpectInt32("1 + 1", 2);
  v8::debug::SetDebugDelegate(env->GetIsolate(), nullptr);
  CheckDebuggerUnloaded();
}

namespace {
void RejectPromiseThroughCppInternal(
    const v8::FunctionCallbackInfo<v8::Value>& info, bool silent) {
  CHECK(i::ValidateCallbackInfo(info));
  auto data = reinterpret_cast<std::pair<v8::Isolate*, LocalContext*>*>(
      info.Data().As<v8::External>()->Value());

  v8::Local<v8::String> value1 =
      v8::String::NewFromUtf8Literal(data->first, "foo");

  v8::Local<v8::Promise::Resolver> resolver =
      v8::Promise::Resolver::New(data->second->local()).ToLocalChecked();
  v8::Local<v8::Promise> promise = resolver->GetPromise();
  CHECK_EQ(promise->State(), v8::Promise::PromiseState::kPending);

  if (silent) {
    promise->MarkAsSilent();
  }

  resolver->Reject(data->second->local(), value1).ToChecked();
  CHECK_EQ(promise->State(), v8::Promise::PromiseState::kRejected);
}

void RejectPromiseThroughCpp(const v8::FunctionCallbackInfo<v8::Value>& info) {
  RejectPromiseThroughCppInternal(info, false);
}

void SilentRejectPromiseThroughCpp(
    const v8::FunctionCallbackInfo<v8::Value>& info) {
  RejectPromiseThroughCppInternal(info, true);
}

}  // namespace

TEST(TerminateOnResumeAtUnhandledRejectionCppImpl) {
  LocalContext env;
  v8::Isolate* isolate = env->GetIsolate();
  v8::HandleScope scope(env->GetIsolate());
  ChangeBreakOnException(isolate, true, true);
  SetTerminateOnResumeDelegate delegate;
  auto data = std::make_pair(isolate, &env);
  v8::debug::SetDebugDelegate(env->GetIsolate(), &delegate);
  {
    // We want to trigger a breakpoint upon Promise rejection, but we will only
    // get the callback if there is at least one JavaScript frame in the stack.
    v8::Local<v8::Function> func =
        v8::Function::New(env.local(), RejectPromiseThroughCpp,
                          v8::External::New(isolate, &data))
            .ToLocalChecked();
    CHECK(env->Global()
              ->Set(env.local(), v8_str("RejectPromiseThroughCpp"), func)
              .FromJust());

    CompileRun("RejectPromiseThroughCpp(); while (true) {}");
    CHECK_EQ(delegate.break_count(), 0);
    CHECK_EQ(delegate.exception_thrown_count(), 1);
  }
  ExpectInt32("1 + 1", 2);
  v8::debug::SetDebugDelegate(env->GetIsolate(), nullptr);
  CheckDebuggerUnloaded();
}

TEST(NoTerminateOnResumeAtSilentUnhandledRejectionCppImpl) {
  LocalContext env;
  v8::Isolate* isolate = env->GetIsolate();
  v8::HandleScope scope(env->GetIsolate());
  ChangeBreakOnException(isolate, true, true);
  SetTerminateOnResumeDelegate delegate;
  auto data = std::make_pair(isolate, &env);
  v8::debug::SetDebugDelegate(env->GetIsolate(), &delegate);
  {
    // We want to reject in a way that would trigger a breakpoint if it were
    // not silenced (as in TerminateOnResumeAtUnhandledRejectionCppImpl), but
    // that would also requre that there is at least one JavaScript frame
    // on the stack.
    v8::Local<v8::Function> func =
        v8::Function::New(env.local(), SilentRejectPromiseThroughCpp,
                          v8::External::New(isolate, &data))
            .ToLocalChecked();
    CHECK(env->Global()
              ->Set(env.local(), v8_str("RejectPromiseThroughCpp"), func)
              .FromJust());

    CompileRun("RejectPromiseThroughCpp(); debugger;");
    CHECK_EQ(delegate.break_count(), 1);
    CHECK_EQ(delegate.exception_thrown_count(), 0);
  }
  ExpectInt32("1 + 1", 2);
  v8::debug::SetDebugDelegate(env->GetIsolate(), nullptr);
  CheckDebuggerUnloaded();
}

namespace {
static void UnreachableMicrotask(
    const v8::FunctionCallbackInfo<v8::Value>& info) {
  UNREACHABLE();
}
}  // namespace

TEST(TerminateOnResumeFromMicrotask) {
  LocalContext env;
  v8::HandleScope scope(env->GetIsolate());
  SetTerminateOnResumeDelegate delegate(
      SetTerminateOnResumeDelegate::kPerformMicrotaskCheckpointAtBreakpoint);
  ChangeBreakOnException(env->GetIsolate(), true, true);
  v8::debug::SetDebugDelegate(env->GetIsolate(), &delegate);
  {
    v8::TryCatch try_catch(env->GetIsolate());
    // Enqueue a microtask that gets run while we are paused at the breakpoint.
    v8::Local<v8::Function> foo = CompileFunction(
        &env, "function foo(){ Promise.reject(); while (true) {} }", "foo");
    env->GetIsolate()->EnqueueMicrotask(foo);
    env->GetIsolate()->EnqueueMicrotask(
        v8::Function::New(env.local(), UnreachableMicrotask).ToLocalChecked());

    CHECK_EQ(2,
             CcTest::i_isolate()->native_context()->microtask_queue()->size());

    v8::MicrotasksScope::PerformCheckpoint(env->GetIsolate());

    CHECK_EQ(0,
             CcTest::i_isolate()->native_context()->microtask_queue()->size());

    CHECK(try_catch.HasTerminated());
    CHECK_EQ(delegate.break_count(), 0);
    CHECK_EQ(delegate.exception_thrown_count(), 1);
  }
  ExpectInt32("1 + 1", 2);
  v8::debug::SetDebugDelegate(env->GetIsolate(), nullptr);
  CheckDebuggerUnloaded();
}

class FutexInterruptionThread : public v8::base::Thread {
 public:
  FutexInterruptionThread(v8::Isolate* isolate, v8::base::Semaphore* enter,
                          v8::base::Semaphore* exit)
      : Thread(Options("FutexInterruptionThread")),
        isolate_(isolate),
        enter_(enter),
        exit_(exit) {}

  void Run() override {
    enter_->Wait();
    v8::debug::SetTerminateOnResume(isolate_);
    exit_->Signal();
  }

 private:
  v8::Isolate* isolate_;
  v8::base::Semaphore* enter_;
  v8::base::Semaphore* exit_;
};

namespace {
class SemaphoreTriggerOnBreak : public v8::debug::DebugDelegate {
 public:
  SemaphoreTriggerOnBreak() : enter_(0), exit_(0) {}
  void BreakProgramRequested(
      v8::Local<v8::Context> paused_context,
      const std::vector<v8::debug::BreakpointId>& inspector_break_points_hit,
      v8::debug::BreakReasons break_reasons) override {
    break_count_++;
    enter_.Signal();
    exit_.Wait();
  }

  v8::base::Semaphore* enter() { return &enter_; }
  v8::base::Semaphore* exit() { return &exit_; }
  int break_count() const { return break_count_; }

 private:
  v8::base::Semaphore enter_;
  v8::base::Semaphore exit_;
  int break_count_ = 0;
};
}  // anonymous namespace

TEST(TerminateOnResumeFromOtherThread) {
  LocalContext env;
  v8::HandleScope scope(env->GetIsolate());
  ChangeBreakOnException(env->GetIsolate(), true, true);

  SemaphoreTriggerOnBreak delegate;
  v8::debug::SetDebugDelegate(env->GetIsolate(), &delegate);

  FutexInterruptionThread timeout_thread(env->GetIsolate(), delegate.enter(),
                                         delegate.exit());
  CHECK(timeout_thread.Start());

  v8::Local<v8::Context> context = env.local();
  {
    v8::TryCatch try_catch(env->GetIsolate());
    const char* source = "debugger; while(true){};";

    v8::ScriptCompiler::Source script_source(v8_str(source));
    v8::Local<v8::Function> foo =
        v8::ScriptCompiler::CompileFunction(env.local(), &script_source)
            .ToLocalChecked();

    v8::MaybeLocal<v8::Value> val =
        foo->Call(context, env->Global(), 0, nullptr);
    CHECK(val.IsEmpty());
    CHECK(try_catch.HasTerminated());
    CHECK_EQ(delegate.break_count(), 1);
  }
  // Exiting the TryCatch brought the isolate back to a state where JavaScript
  // can be executed.
  ExpectInt32("1 + 1", 2);
  v8::debug::SetDebugDelegate(env->GetIsolate(), nullptr);
  CheckDebuggerUnloaded();
}

namespace {
class InterruptionBreakRightNow : public v8::base::Thread {
 public:
  explicit InterruptionBreakRightNow(v8::Isolate* isolate)
      : Thread(Options("InterruptionBreakRightNow")), isolate_(isolate) {}

  void Run() override {
    // Wait a bit before terminating.
    v8::base::OS::Sleep(v8::base::TimeDelta::FromMilliseconds(100));
    isolate_->RequestInterrupt(BreakRightNow, nullptr);
  }

 private:
  static void BreakRightNow(v8::Isolate* isolate, void* data) {
    v8::debug::BreakRightNow(isolate);
  }
  v8::Isolate* isolate_;
};

}  // anonymous namespace

TEST(TerminateOnResumeAtInterruptFromOtherThread) {
  LocalContext env;
  v8::HandleScope scope(env->GetIsolate());
  ChangeBreakOnException(env->GetIsolate(), true, true);

  SetTerminateOnResumeDelegate delegate;
  v8::debug::SetDebugDelegate(env->GetIsolate(), &delegate);

  InterruptionBreakRightNow timeout_thread(env->GetIsolate());

  v8::Local<v8::Context> context = env.local();
  {
    v8::TryCatch try_catch(env->GetIsolate());
    const char* source = "while(true){}";

    v8::ScriptCompiler::Source script_source(v8_str(source));
    v8::Local<v8::Function> foo =
        v8::ScriptCompiler::CompileFunction(env.local(), &script_source)
            .ToLocalChecked();

    CHECK(timeout_thread.Start());
    v8::MaybeLocal<v8::Value> val =
        foo->Call(context, env->Global(), 0, nullptr);
    CHECK(val.IsEmpty());
    CHECK(try_catch.HasTerminated());
    CHECK_EQ(delegate.break_count(), 1);
  }
  // Exiting the TryCatch brought the isolate back to a state where JavaScript
  // can be executed.
  ExpectInt32("1 + 1", 2);
  v8::debug::SetDebugDelegate(env->GetIsolate(), nullptr);
  CheckDebuggerUnloaded();
}

namespace {

class NoopDelegate : public v8::debug::DebugDelegate {};

}  // namespace

TEST(CreateMessageFromOldException) {
  LocalContext context;
  v8::HandleScope scope(context->GetIsolate());

  context->GetIsolate()->SetCaptureStackTraceForUncaughtExceptions(true);

  v8::Local<v8::Value> error;
  {
    v8::TryCatch try_catch(context->GetIsolate());
    CompileRun(R"javascript(
        function f1() {
          throw new Error('error in f1');
        };
        f1();
    )javascript");
    CHECK(try_catch.HasCaught());

    error = try_catch.Exception();
  }
  CHECK(error->IsObject());

  v8::Local<v8::Message> message =
      v8::debug::CreateMessageFromException(context->GetIsolate(), error);
  CHECK(!message.IsEmpty());
  CHECK_EQ(3, message->GetLineNumber(context.local()).FromJust());
  CHECK_EQ(16, message->GetStartColumn(context.local()).FromJust());

  v8::Local<v8::StackTrace> stackTrace = message->GetStackTrace();
  CHECK(!stackTrace.IsEmpty());
  CHECK_EQ(2, stackTrace->GetFrameCount());

  stackTrace = v8::Exception::GetStackTrace(error);
  CHECK(!stackTrace.IsEmpty());
  CHECK_EQ(2, stackTrace->GetFrameCount());
}

TEST(CreateMessageDoesNotInspectStack) {
  LocalContext context;
  v8::HandleScope scope(context->GetIsolate());

  // Do not enable Isolate::SetCaptureStackTraceForUncaughtExceptions.

  v8::Local<v8::Value> error;
  {
    v8::TryCatch try_catch(context->GetIsolate());
    CompileRun(R"javascript(
        function f1() {
          throw new Error('error in f1');
        };
        f1();
    )javascript");
    CHECK(try_catch.HasCaught());

    error = try_catch.Exception();
  }
  // The caught error should not have a stack trace attached.
  CHECK(error->IsObject());
  CHECK(v8::Exception::GetStackTrace(error).IsEmpty());

  // The corresponding message should also not have a stack trace.
  v8::Local<v8::Message> message =
      v8::debug::CreateMessageFromException(context->GetIsolate(), error);
  CHECK(!message.IsEmpty());
  CHECK(message->GetStackTrace().IsEmpty());
}

namespace {

class ScopeListener : public v8::debug::DebugDelegate {
 public:
  void BreakProgramRequested(v8::Local<v8::Context> context,
                             const std::vector<v8::debug::BreakpointId>&,
                             v8::debug::BreakReasons break_reasons) override {
    i::Isolate* isolate = CcTest::i_isolate();
    i::DebuggableStackFrameIterator iterator_(
        isolate, isolate->debug()->break_frame_id());
    // Go up one frame so we are on the script level.
    iterator_.Advance();

    auto frame_inspector =
        std::make_unique<i::FrameInspector>(iterator_.frame(), 0, isolate);
    i::ScopeIterator scope_iterator(
        isolate, frame_inspector.get(),
        i::ScopeIterator::ReparseStrategy::kScriptIfNeeded);

    // Iterate all scopes triggering block list creation along the way. This
    // should not run into any CHECKs.
    while (!scope_iterator.Done()) scope_iterator.Next();
  }
};

}  // namespace

TEST(ScopeIteratorDoesNotCreateBlocklistForScriptScope) {
  LocalContext env;
  v8::Isolate* isolate = env->GetIsolate();
  v8::HandleScope scope(isolate);

  // Register a debug event listener which creates a ScopeIterator.
  ScopeListener delegate;
  v8::debug::SetDebugDelegate(isolate, &delegate);

  CompileRun(R"javascript(
    function foo() { debugger; }
    foo();
  )javascript");

  // Get rid of the debug event listener.
  v8::debug::SetDebugDelegate(isolate, nullptr);
  CheckDebuggerUnloaded();
}

namespace {

class DebugEvaluateListener : public v8::debug::DebugDelegate {
 public:
  void BreakProgramRequested(v8::Local<v8::Context> context,
                             const std::vector<v8::debug::BreakpointId>&,
                             v8::debug::BreakReasons break_reasons) override {
    v8::Isolate* isolate = context->GetIsolate();
    auto it = v8::debug::StackTraceIterator::Create(isolate);
    v8::Local<v8::Value> result =
        it->Evaluate(v8_str(isolate, "x"), /* throw_on_side_effect */ false)
            .ToLocalChecked();
    CHECK_EQ(42, result->ToInteger(context).ToLocalChecked()->Value());
  }
};

}  // namespace

// This test checks that the debug-evaluate blocklist logic correctly handles
// scopes created by `ScriptCompiler::CompileFunction`. It creates a function
// scope nested inside an eval scope with the exact same source positions.
// This can confuse the blocklist mechanism if not handled correctly.
TEST(DebugEvaluateInWrappedScript) {
  LocalContext env;
  v8::Isolate* isolate = env->GetIsolate();
  v8::HandleScope scope(isolate);

  // Register a debug event listener which evaluates 'x'.
  DebugEvaluateListener delegate;
  v8::debug::SetDebugDelegate(isolate, &delegate);

  static const char* source = "const x = 42; () => x; debugger;";

  {
    v8::ScriptCompiler::Source script_source(v8_str(source));
    v8::Local<v8::Function> fun =
        v8::ScriptCompiler::CompileFunction(env.local(), &script_source)
            .ToLocalChecked();

    fun->Call(env.local(), env->Global(), 0, nullptr).ToLocalChecked();
  }

  // Get rid of the debug event listener.
  v8::debug::SetDebugDelegate(env->GetIsolate(), nullptr);
  CheckDebuggerUnloaded();
}

namespace {

class ConditionListener : public v8::debug::DebugDelegate {
 public:
  void BreakpointConditionEvaluated(
      v8::Local<v8::Context> context, v8::debug::BreakpointId breakpoint_id_arg,
      bool exception_thrown_arg, v8::Local<v8::Value> exception_arg) override {
    breakpoint_id = breakpoint_id_arg;
    exception_thrown = exception_thrown_arg;
    exception = exception_arg;
  }

  void BreakProgramRequested(v8::Local<v8::Context> context,
                             const std::vector<v8::debug::BreakpointId>&,
                             v8::debug::BreakReasons break_reasons) override {
    break_point_hit_count++;
  }

  v8::debug::BreakpointId breakpoint_id;
  bool exception_thrown = false;
  v8::Local<v8::Value> exception;
};

}  // namespace

TEST(SuccessfulBreakpointConditionEvaluationEvent) {
  break_point_hit_count = 0;
  LocalContext env;
  v8::Isolate* isolate = env->GetIsolate();
  v8::HandleScope scope(isolate);

  ConditionListener delegate;
  v8::debug::SetDebugDelegate(isolate, &delegate);

  v8::Local<v8::Function> foo =
      CompileFunction(&env, "function foo() { const x = 5; }", "foo");

  i::DirectHandle<i::BreakPoint> bp = SetBreakPoint(foo, 0, "true");
  foo->Call(env.local(), env->Global(), 0, nullptr).ToLocalChecked();
  CHECK_EQ(1, break_point_hit_count);
  CHECK_EQ(bp->id(), delegate.breakpoint_id);
  CHECK(!delegate.exception_thrown);
  CHECK(delegate.exception.IsEmpty());
}

// Checks that SyntaxErrors in breakpoint conditions are reported to the
// DebugDelegate.
TEST(FailedBreakpointConditoinEvaluationEvent) {
  break_point_hit_count = 0;
  LocalContext env;
  v8::Isolate* isolate = env->GetIsolate();
  v8::HandleScope scope(isolate);

  ConditionListener delegate;
  v8::debug::SetDebugDelegate(isolate, &delegate);

  v8::Local<v8::Function> foo =
      CompileFunction(&env, "function foo() { const x = 5; }", "foo");

  i::DirectHandle<i::BreakPoint> bp = SetBreakPoint(foo, 0, "bar().");
  foo->Call(env.local(), env->Global(), 0, nullptr).ToLocalChecked();
  CHECK_EQ(0, break_point_hit_count);
  CHECK_EQ(bp->id(), delegate.breakpoint_id);
  CHECK(delegate.exception_thrown);
  CHECK(!delegate.exception.IsEmpty());
}

class ExceptionCatchPredictionChecker : public v8::debug::DebugDelegate {
 public:
  void ExceptionThrown(v8::Local<v8::Context> paused_context,
                       v8::Local<v8::Value> exception,
                       v8::Local<v8::Value> promise, bool is_uncaught,
                       v8::debug::ExceptionType) override {
    exception_event_count++;
    was_uncaught = is_uncaught;
    // Check that exception is the string 'f' so we know that we are
    // only throwing the intended exception.
    CHECK(v8_str(paused_context->GetIsolate(), "f")
              ->Equals(paused_context, exception)
              .ToChecked());
  }

  int exception_event_count = 0;
  bool was_uncaught = false;
  int functions_checked = 0;
};

void RunExceptionCatchPredictionTest(bool predict_uncaught, const char* code) {
  i::v8_flags.allow_natives_syntax = true;
  LocalContext env;
  v8::Isolate* isolate = env->GetIsolate();
  v8::HandleScope scope(isolate);
  ExceptionCatchPredictionChecker delegate;
  v8::debug::SetDebugDelegate(isolate, &delegate);
  ChangeBreakOnException(isolate, true, true);

  CompileRun(code);
  CHECK_EQ(0, delegate.exception_event_count);

  CompileRun("%PrepareFunctionForOptimization(test);\n");
  CompileRun("test();\n");
  CHECK_EQ(1, delegate.exception_event_count);
  CHECK_EQ(predict_uncaught, delegate.was_uncaught);

  // Second time should be same result as first
  delegate.exception_event_count = 0;
  CompileRun("test();\n");
  CHECK_EQ(1, delegate.exception_event_count);
  CHECK_EQ(predict_uncaught, delegate.was_uncaught);

  // Now ensure optimization doesn't change the reported exception
  delegate.exception_event_count = 0;
  CompileRun("%OptimizeFunctionOnNextCall(test);\n");
  CompileRun("test();\n");
  CHECK_EQ(1, delegate.exception_event_count);
  CHECK_EQ(predict_uncaught, delegate.was_uncaught);
}

class FunctionBlackboxedCheckCounter : public v8::debug::DebugDelegate {
 public:
  void ExceptionThrown(v8::Local<v8::Context> paused_context,
                       v8::Local<v8::Value> exception,
                       v8::Local<v8::Value> promise, bool is_uncaught,
                       v8::debug::ExceptionType) override {
    // Should never happen due to consistent blackboxing
    UNREACHABLE();
  }
  bool IsFunctionBlackboxed(v8::Local<v8::debug::Script> script,
                            const v8::debug::Location& start,
                            const v8::debug::Location& end) override {
    functions_checked++;
    // Return true to ensure it keeps walking the callstack
    return true;
  }
  int functions_checked = 0;
};

void RunAndIgnore(v8::Local<v8::Script> script,
                  v8::Local<v8::Context> context) {
  auto result = script->Run(context);
  if (!result.IsEmpty()) result.ToLocalChecked();
}

void RunExceptionBlackboxCheckTest(int functions_checked, const char* code) {
  i::v8_flags.allow_natives_syntax = true;
  LocalContext env;
  v8::Isolate* isolate = env->GetIsolate();
  v8::HandleScope scope(isolate);
  FunctionBlackboxedCheckCounter delegate;
  v8::debug::SetDebugDelegate(isolate, &delegate);
  ChangeBreakOnException(isolate, true, true);

  CompileRun(code);
  CHECK_EQ(0, delegate.functions_checked);

  CompileRun("%PrepareFunctionForOptimization(test);\n");

  // Need to compile this script once and run it multiple times so the call
  // stack doesn't change.
  v8::Local<v8::Context> context = env.local();
  v8::Local<v8::Script> test_script =
      v8::Script::Compile(context, v8_str(isolate, "test();\n"))
          .ToLocalChecked();
  RunAndIgnore(test_script, context);
  CHECK_EQ(functions_checked, delegate.functions_checked);

  // Second time should not do any checks due to cached function debug info
  delegate.functions_checked = 0;
  RunAndIgnore(test_script, context);
  CHECK_EQ(0, delegate.functions_checked);

  // Now ensure optimization doesn't lead to additional frames being checked
  delegate.functions_checked = 0;
  CompileRun("%OptimizeFunctionOnNextCall(test);\n");
  RunAndIgnore(test_script, context);
  // Will fail if we iterate over more stack frames than expected. Would be
  // nice to figure out how to use something like
  // v8::debug::ResetBlackboxedStateCache so we can ensure the same functions
  // are being checked.
  CHECK_EQ(0, delegate.functions_checked);
}

void RunExceptionOptimizedCallstackWalkTest(bool predict_uncaught,
                                            int functions_checked,
                                            const char* code) {
  RunExceptionCatchPredictionTest(predict_uncaught, code);
  RunExceptionBlackboxCheckTest(functions_checked, code);
}

TEST(CatchPredictionWithLongStar) {
  // Simple scan for catch method, but we first exhaust the short registers
  // in the bytecode so that it doesn't use the short star instructions
  RunExceptionOptimizedCallstackWalkTest(false, 1, R"javascript(
    function test() {
      let r1 = 1;
      let r2 = 2;
      let r3 = r1 + r2;
      let r4 = r2 * 2;
      let r5 = r2 + r3;
      let r6 = r4 + r2;
      let r7 = 7;
      let r8 = r5 + r3;
      let r9 = r7 + r2;
      let r10 = r4 + r6;
      let r11 = r8 + r3;
      let r12 = r7 + r5;
      let r13 = r11 + r2;
      let r14 = r10 + r4;
      let r15 = r9 + r6;
      let r16 = r15 + r1;
      let p = Promise.reject('f').catch(()=>17);
      return {p, r16, r14, r13, r12};
    }
  )javascript");
}

TEST(CatchPredictionInlineExceptionCaught) {
  // Simple throw and catch, but make sure inlined functions don't affect
  // prediction.
  RunExceptionOptimizedCallstackWalkTest(false, 3, R"javascript(
    function thrower() {
      throw 'f';
    }

    function throwerWrapper() {
      thrower();
    }

    function catcher() {
      try {
        throwerWrapper();
      } catch(e) {}
    }

    function test() {
      catcher();
    }

    %PrepareFunctionForOptimization(catcher);
    %PrepareFunctionForOptimization(throwerWrapper);
    %PrepareFunctionForOptimization(thrower);
  )javascript");
}

TEST(CatchPredictionInlineExceptionUncaught) {
  // Simple uncaught throw, but make sure inlined functions don't affect
  // prediction.
  RunExceptionOptimizedCallstackWalkTest(true, 4, R"javascript(
    function thrower() {
      throw 'f';
    }

    function throwerWrapper() {
      thrower();
    }

    function test() {
      throwerWrapper();
    }

    %PrepareFunctionForOptimization(throwerWrapper);
    %PrepareFunctionForOptimization(thrower);
  )javascript");
}

TEST(CatchPredictionExceptionCaughtAsPromise) {
  // Throw turns into promise rejection in async function, then caught
  // by catch method. Multiple intermediate stack frames with decoy catches
  // that won't actually catch and shouldn't be predicted to catch. Make sure
  // we walk the correct number of frames and that inlining does not affect
  // our behavior.
  RunExceptionOptimizedCallstackWalkTest(false, 6, R"javascript(
    function thrower() {
      throw 'f';
    }

    function throwerWrapper() {
      return thrower().catch(()=>{});
    }

    async function promiseWrapper() {
      throwerWrapper();
    }

    function fakeCatcher() {
      try {
        return promiseWrapper();
      } catch(e) {}
    }

    async function awaiter() {
      await fakeCatcher();
    }

    function catcher() {
      return awaiter().then(()=>{}).catch(()=>{});
    }

    function test() {
      catcher();
    }

    %PrepareFunctionForOptimization(catcher);
    %PrepareFunctionForOptimization(awaiter);
    %PrepareFunctionForOptimization(fakeCatcher);
    %PrepareFunctionForOptimization(promiseWrapper);
    %PrepareFunctionForOptimization(throwerWrapper);
    %PrepareFunctionForOptimization(thrower);
  )javascript");
}

TEST(CatchPredictionExceptionCaughtAsPromiseInAsyncFunction) {
  // Throw as promise rejection in async function, then caught
  // by catch method. Ensure we scan for catch method in an async
  // function.
  RunExceptionOptimizedCallstackWalkTest(false, 3, R"javascript(
    async function thrower() {
      throw 'f';
    }

    function throwerWrapper() {
      return thrower();
    }

    async function catcher() {
      await throwerWrapper().catch(()=>{});
    }

    function test() {
      catcher();
    }

    %PrepareFunctionForOptimization(catcher);
    %PrepareFunctionForOptimization(throwerWrapper);
    %PrepareFunctionForOptimization(thrower);
  )javascript");
}

TEST(CatchPredictionExceptionCaughtAsPromiseInCatchingFunction) {
  // Throw as promise rejection in async function, then caught
  // by catch method. Ensure we scan for catch method in function
  // with a (decoy) catch block.
  RunExceptionOptimizedCallstackWalkTest(false, 3, R"javascript(
    async function thrower() {
      throw 'f';
    }

    function throwerWrapper() {
      return thrower();
    }

    function catcher() {
      try {
        return throwerWrapper().catch(()=>{});
      } catch (e) {}
    }

    function test() {
      catcher();
    }

    %PrepareFunctionForOptimization(catcher);
    %PrepareFunctionForOptimization(throwerWrapper);
    %PrepareFunctionForOptimization(thrower);
  )javascript");
}

TEST(CatchPredictionTopLevelEval) {
  // Statement returning rejected promise is immediately followed by statement
  // catching it in top level eval context.
  RunExceptionCatchPredictionTest(false, R"javascript(
    function test() {
      eval(`let result = Promise.reject('f');
      result.catch(()=>{});`);
    }
  )javascript");
}

TEST(CatchPredictionClosureCapture) {
  // Statement returning rejected promise is immediately followed by statement
  // catching it, but original promise is captured in a closure.
  RunExceptionOptimizedCallstackWalkTest(false, 1, R"javascript(
    function test() {
      let result = Promise.reject('f');
      result.catch(()=>{});
      return (() => result);
    }
  )javascript");
}

TEST(CatchPredictionNestedContext) {
  // Statement returning rejected promise stores in a variable in an outer
  // context.
  RunExceptionOptimizedCallstackWalkTest(false, 1, R"javascript(
    function test() {
      let result = null;
      {
        let otherObj = {};
        result = Promise.reject('f');
        result.catch(()=>otherObj);
      }
      return (() => result);
    }
  )javascript");
}

TEST(CatchPredictionWithContext) {
  // Statement returning rejected promise stores in a variable outside a with
  // context.
  RunExceptionOptimizedCallstackWalkTest(false, 1, R"javascript(
    function test() {
      let result = null;
      let otherObj = {};
      with (otherObj) {
        result = Promise.reject('f');
        result.catch(()=>{});
      }
      return (() => result);
    }
  )javascript");
}
