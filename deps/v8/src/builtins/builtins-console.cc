// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stack>

#include "src/api/api-inl.h"
#include "src/builtins/builtins-utils-inl.h"
#include "src/builtins/builtins.h"
#include "src/debug/interface-types.h"
#include "src/logging/counters.h"
#include "src/logging/log.h"
#include "src/objects/objects-inl.h"

namespace v8 {
namespace internal {

// -----------------------------------------------------------------------------
// Console

#define CONSOLE_METHOD_LIST(V) \
  V(Dir, dir)                  \
  V(DirXml, dirXml)            \
  V(Table, table)              \
  V(GroupEnd, groupEnd)        \
  V(Clear, clear)              \
  V(Count, count)              \
  V(CountReset, countReset)    \
  V(Profile, profile)          \
  V(ProfileEnd, profileEnd)

#define CONSOLE_METHOD_WITH_FORMATTER_LIST(V) \
  V(Debug, debug, 1)                          \
  V(Error, error, 1)                          \
  V(Info, info, 1)                            \
  V(Log, log, 1)                              \
  V(Warn, warn, 1)                            \
  V(Trace, trace, 1)                          \
  V(Group, group, 1)                          \
  V(GroupCollapsed, groupCollapsed, 1)        \
  V(Assert, assert, 2)

namespace {

// 2.2 Formatter(args) [https://console.spec.whatwg.org/#formatter]
//
// This implements the formatter operation defined in the Console
// specification to the degree that it makes sense for V8.  That
// means we primarily deal with %s, %i, %f, and %d, and any side
// effects caused by the type conversions, and we preserve the %o,
// %c, and %O specifiers and their parameters unchanged, and instead
// leave it to the debugger front-end to make sense of those.
//
// Chrome also supports the non-standard bypass format specifier %_
// which just skips over the parameter.
//
// This implementation updates the |args| in-place with the results
// from the conversion.
//
// The |index| describes the position of the format string within,
// |args| (starting with 1, since |args| also includes the receiver),
// which is different for example in case of `console.log` where it
// is 1 compared to `console.assert` where it is 2.
bool Formatter(Isolate* isolate, BuiltinArguments& args, int index) {
  if (args.length() < index + 2 || !IsString(args[index])) {
    return true;
  }
  struct State {
    Handle<String> str;
    int off;
  };
  std::stack<State> states;
  HandleScope scope(isolate);
  auto percent = isolate->factory()->LookupSingleCharacterStringFromCode('%');
  states.push({args.at<String>(index++), 0});
  while (!states.empty() && index < args.length()) {
    State& state = states.top();
    state.off = String::IndexOf(isolate, state.str, percent, state.off);
    if (state.off < 0 || state.off == state.str->length() - 1) {
      states.pop();
      continue;
    }
    Handle<Object> current = args.at(index);
    uint16_t specifier = state.str->Get(state.off + 1, isolate);
    if (specifier == 'd' || specifier == 'f' || specifier == 'i') {
      if (IsSymbol(*current)) {
        current = isolate->factory()->nan_value();
      } else {
        Handle<Object> params[] = {current,
                                   isolate->factory()->NewNumberFromInt(10)};
        auto builtin = specifier == 'f' ? isolate->global_parse_float_fun()
                                        : isolate->global_parse_int_fun();
        if (!Execution::CallBuiltin(isolate, builtin,
                                    isolate->factory()->undefined_value(),
                                    arraysize(params), params)
                 .ToHandle(&current)) {
          return false;
        }
      }
    } else if (specifier == 's') {
      Handle<Object> params[] = {current};
      if (!Execution::CallBuiltin(isolate, isolate->string_function(),
                                  isolate->factory()->undefined_value(),
                                  arraysize(params), params)
               .ToHandle(&current)) {
        return false;
      }

      // Recurse into string results from type conversions, as they
      // can themselves contain formatting specifiers.
      states.push({Cast<String>(current), 0});
    } else if (specifier == 'c' || specifier == 'o' || specifier == 'O' ||
               specifier == '_') {
      // We leave the interpretation of %c (CSS), %o (optimally useful
      // formatting), and %O (generic JavaScript object formatting) as
      // well as the non-standard %_ (bypass formatter in Chrome) to
      // the debugger front-end, and preserve these specifiers as well
      // as their arguments verbatim.
      index++;
      state.off += 2;
      continue;
    } else if (specifier == '%') {
      // Chrome also supports %% as a way to generate a single % in the
      // output.
      state.off += 2;
      continue;
    } else {
      state.off++;
      continue;
    }

    // Replace the |specifier| (including the '%' character) in |target|
    // with the |current| value. We perform the replacement only morally
    // by updating the argument to the conversion result, but leave it to
    // the debugger front-end to perform the actual substitution.
    args.set_at(index++, *current);
    state.off += 2;
  }
  return true;
}

// The closures installed on objects returned from `console.context()`
// get a special builtin context with 2 slots, to hold the unique ID of
// the console context and its name.
enum {
  CONSOLE_CONTEXT_ID_INDEX = Context::MIN_CONTEXT_SLOTS,
  CONSOLE_CONTEXT_NAME_INDEX,
  CONSOLE_CONTEXT_SLOTS,
};

void ConsoleCall(
    Isolate* isolate, const internal::BuiltinArguments& args,
    void (debug::ConsoleDelegate::*func)(const v8::debug::ConsoleCallArguments&,
                                         const v8::debug::ConsoleContext&)) {
  if (isolate->is_execution_terminating()) return;
  CHECK(!isolate->has_exception());
  if (!isolate->console_delegate()) return;
  HandleScope scope(isolate);
  int context_id = 0;
  Handle<String> context_name = isolate->factory()->anonymous_string();
  if (!IsNativeContext(args.target()->context())) {
    DirectHandle<Context> context(args.target()->context(), isolate);
    CHECK_EQ(CONSOLE_CONTEXT_SLOTS, context->length());
    context_id = Cast<Smi>(context->get(CONSOLE_CONTEXT_ID_INDEX)).value();
    context_name =
        handle(Cast<String>(context->get(CONSOLE_CONTEXT_NAME_INDEX)), isolate);
  }
  (isolate->console_delegate()->*func)(
      debug::ConsoleCallArguments(isolate, args),
      v8::debug::ConsoleContext(context_id, Utils::ToLocal(context_name)));
}

void LogTimerEvent(Isolate* isolate, BuiltinArguments args,
                   v8::LogEventStatus se) {
  if (!v8_flags.log_timer_events) return;
  HandleScope scope(isolate);
  std::unique_ptr<char[]> name;
  const char* raw_name = "default";
  if (args.length() > 1 && IsString(args[1])) {
    // Try converting the first argument to a string.
    name = args.at<String>(1)->ToCString();
    raw_name = name.get();
  }
  LOG(isolate, TimerEvent(se, raw_name));
}

}  // namespace

#define CONSOLE_BUILTIN_IMPLEMENTATION(call, name)             \
  BUILTIN(Console##call) {                                     \
    ConsoleCall(isolate, args, &debug::ConsoleDelegate::call); \
    RETURN_FAILURE_IF_EXCEPTION(isolate);                      \
    return ReadOnlyRoots(isolate).undefined_value();           \
  }
CONSOLE_METHOD_LIST(CONSOLE_BUILTIN_IMPLEMENTATION)
#undef CONSOLE_BUILTIN_IMPLEMENTATION

#define CONSOLE_BUILTIN_IMPLEMENTATION(call, name, index)      \
  BUILTIN(Console##call) {                                     \
    if (!Formatter(isolate, args, index)) {                    \
      return ReadOnlyRoots(isolate).exception();               \
    }                                                          \
    ConsoleCall(isolate, args, &debug::ConsoleDelegate::call); \
    RETURN_FAILURE_IF_EXCEPTION(isolate);                      \
    return ReadOnlyRoots(isolate).undefined_value();           \
  }
CONSOLE_METHOD_WITH_FORMATTER_LIST(CONSOLE_BUILTIN_IMPLEMENTATION)
#undef CONSOLE_BUILTIN_IMPLEMENTATION

BUILTIN(ConsoleTime) {
  LogTimerEvent(isolate, args, v8::LogEventStatus::kStart);
  ConsoleCall(isolate, args, &debug::ConsoleDelegate::Time);
  RETURN_FAILURE_IF_EXCEPTION(isolate);
  return ReadOnlyRoots(isolate).undefined_value();
}

BUILTIN(ConsoleTimeEnd) {
  LogTimerEvent(isolate, args, v8::LogEventStatus::kEnd);
  ConsoleCall(isolate, args, &debug::ConsoleDelegate::TimeEnd);
  RETURN_FAILURE_IF_EXCEPTION(isolate);
  return ReadOnlyRoots(isolate).undefined_value();
}

BUILTIN(ConsoleTimeLog) {
  LogTimerEvent(isolate, args, v8::LogEventStatus::kLog);
  ConsoleCall(isolate, args, &debug::ConsoleDelegate::TimeLog);
  RETURN_FAILURE_IF_EXCEPTION(isolate);
  return ReadOnlyRoots(isolate).undefined_value();
}

BUILTIN(ConsoleTimeStamp) {
  ConsoleCall(isolate, args, &debug::ConsoleDelegate::TimeStamp);
  RETURN_FAILURE_IF_EXCEPTION(isolate);
  return ReadOnlyRoots(isolate).undefined_value();
}

namespace {

void InstallContextFunction(Isolate* isolate, Handle<JSObject> target,
                            const char* name, Builtin builtin,
                            Handle<Context> context) {
  Factory* const factory = isolate->factory();

  Handle<Map> map = isolate->sloppy_function_without_prototype_map();

  Handle<String> name_string = factory->InternalizeUtf8String(name);

  Handle<SharedFunctionInfo> info =
      factory->NewSharedFunctionInfoForBuiltin(name_string, builtin);
  info->set_language_mode(LanguageMode::kSloppy);
  info->set_native(true);
  info->DontAdaptArguments();
  info->set_length(1);

  DirectHandle<JSFunction> fun =
      Factory::JSFunctionBuilder{isolate, info, context}.set_map(map).Build();

  JSObject::AddProperty(isolate, target, name_string, fun, NONE);
}

}  // namespace

BUILTIN(ConsoleContext) {
  HandleScope scope(isolate);
  Factory* const factory = isolate->factory();

  isolate->CountUsage(v8::Isolate::UseCounterFeature::kConsoleContext);

  // Generate a unique ID for the new `console.context`
  // and convert the parameter to a string (defaults to
  // 'anonymous' if unspecified).
  Handle<String> context_name = factory->anonymous_string();
  if (args.length() > 1) {
    ASSIGN_RETURN_FAILURE_ON_EXCEPTION(isolate, context_name,
                                       Object::ToString(isolate, args.at(1)));
  }
  int context_id = isolate->last_console_context_id() + 1;
  isolate->set_last_console_context_id(context_id);

  Handle<SharedFunctionInfo> info = factory->NewSharedFunctionInfoForBuiltin(
      factory->InternalizeUtf8String("Context"), Builtin::kIllegal);
  info->set_language_mode(LanguageMode::kSloppy);

  Handle<JSFunction> cons =
      Factory::JSFunctionBuilder{isolate, info, isolate->native_context()}
          .Build();

  Handle<JSObject> prototype = factory->NewJSObject(isolate->object_function());
  JSFunction::SetPrototype(cons, prototype);

  Handle<JSObject> console_context =
      factory->NewJSObject(cons, AllocationType::kOld);
  DCHECK(IsJSObject(*console_context));

  Handle<Context> context = factory->NewBuiltinContext(
      isolate->native_context(), CONSOLE_CONTEXT_SLOTS);
  context->set(CONSOLE_CONTEXT_ID_INDEX, Smi::FromInt(context_id));
  context->set(CONSOLE_CONTEXT_NAME_INDEX, *context_name);

#define CONSOLE_BUILTIN_SETUP(call, name, ...)            \
  InstallContextFunction(isolate, console_context, #name, \
                         Builtin::kConsole##call, context);
  CONSOLE_METHOD_LIST(CONSOLE_BUILTIN_SETUP)
  CONSOLE_METHOD_WITH_FORMATTER_LIST(CONSOLE_BUILTIN_SETUP)
  CONSOLE_BUILTIN_SETUP(Time, time)
  CONSOLE_BUILTIN_SETUP(TimeLog, timeLog)
  CONSOLE_BUILTIN_SETUP(TimeEnd, timeEnd)
  CONSOLE_BUILTIN_SETUP(TimeStamp, timeStamp)
#undef CONSOLE_BUILTIN_SETUP

  return *console_context;
}

#undef CONSOLE_METHOD_LIST

}  // namespace internal
}  // namespace v8
