// Copyright 2011 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/execution/messages.h"

#include <memory>

#include "src/api/api-inl.h"
#include "src/ast/ast.h"
#include "src/ast/prettyprinter.h"
#include "src/execution/execution.h"
#include "src/execution/frames-inl.h"
#include "src/execution/frames.h"
#include "src/execution/isolate-inl.h"
#include "src/execution/isolate.h"
#include "src/handles/maybe-handles.h"
#include "src/logging/runtime-call-stats-scope.h"
#include "src/objects/call-site-info-inl.h"
#include "src/objects/foreign-inl.h"
#include "src/objects/js-array-inl.h"
#include "src/objects/property-descriptor.h"
#include "src/objects/struct-inl.h"
#include "src/parsing/parse-info.h"
#include "src/parsing/parsing.h"
#include "src/roots/roots.h"
#include "src/strings/string-builder-inl.h"

namespace v8 {
namespace internal {

MessageLocation::MessageLocation(Handle<Script> script, int start_pos,
                                 int end_pos)
    : script_(script),
      start_pos_(start_pos),
      end_pos_(end_pos),
      bytecode_offset_(-1) {}

MessageLocation::MessageLocation(Handle<Script> script, int start_pos,
                                 int end_pos, Handle<SharedFunctionInfo> shared)
    : script_(script),
      start_pos_(start_pos),
      end_pos_(end_pos),
      bytecode_offset_(-1),
      shared_(shared) {}

MessageLocation::MessageLocation(Handle<Script> script,
                                 Handle<SharedFunctionInfo> shared,
                                 int bytecode_offset)
    : script_(script),
      start_pos_(-1),
      end_pos_(-1),
      bytecode_offset_(bytecode_offset),
      shared_(shared) {}

MessageLocation::MessageLocation()
    : start_pos_(-1), end_pos_(-1), bytecode_offset_(-1) {}

// If no message listeners have been registered this one is called
// by default.
void MessageHandler::DefaultMessageReport(Isolate* isolate,
                                          const MessageLocation* loc,
                                          DirectHandle<Object> message_obj) {
  std::unique_ptr<char[]> str = GetLocalizedMessage(isolate, message_obj);
  if (loc == nullptr) {
    PrintF("%s\n", str.get());
  } else {
    HandleScope scope(isolate);
    DirectHandle<Object> data(loc->script()->name(), isolate);
    std::unique_ptr<char[]> data_str;
    if (IsString(*data))
      data_str = Cast<String>(data)->ToCString(DISALLOW_NULLS);
    PrintF("%s:%i: %s\n", data_str ? data_str.get() : "<unknown>",
           loc->start_pos(), str.get());
  }
}

Handle<JSMessageObject> MessageHandler::MakeMessageObject(
    Isolate* isolate, MessageTemplate message, const MessageLocation* location,
    DirectHandle<Object> argument, DirectHandle<FixedArray> stack_frames) {
  Factory* factory = isolate->factory();

  int start = -1;
  int end = -1;
  int bytecode_offset = -1;
  DirectHandle<Script> script_handle = isolate->factory()->empty_script();
  DirectHandle<SharedFunctionInfo> shared_info;
  if (location != nullptr && !v8_flags.correctness_fuzzer_suppressions) {
    start = location->start_pos();
    end = location->end_pos();
    script_handle = location->script();
    bytecode_offset = location->bytecode_offset();
    shared_info = location->shared();
  }

  DirectHandle<Object> stack_frames_handle =
      stack_frames.is_null() ? Cast<Object>(factory->undefined_value())
                             : Cast<Object>(stack_frames);

  Handle<JSMessageObject> message_obj = factory->NewJSMessageObject(
      message, argument, start, end, shared_info, bytecode_offset,
      script_handle, stack_frames_handle);

  return message_obj;
}

void MessageHandler::ReportMessage(Isolate* isolate, const MessageLocation* loc,
                                   DirectHandle<JSMessageObject> message) {
  v8::Local<v8::Message> api_message_obj = v8::Utils::MessageToLocal(message);

  if (api_message_obj->ErrorLevel() != v8::Isolate::kMessageError) {
    ReportMessageNoExceptions(isolate, loc, message, v8::Local<v8::Value>());
    return;
  }

  // We are calling into embedder's code which can throw exceptions.
  // Thus we need to save current exception state, reset it to the clean one
  // and ignore scheduled exceptions callbacks can throw.

  // We pass the exception object into the message handler callback though.
  Handle<Object> exception = isolate->factory()->undefined_value();
  if (isolate->has_exception()) {
    exception = handle(isolate->exception(), isolate);
  }

  Isolate::ExceptionScope exception_scope(isolate);
  isolate->clear_pending_message();

  // Turn the exception on the message into a string if it is an object.
  if (IsJSObject(message->argument())) {
    HandleScope scope(isolate);
    DirectHandle<Object> argument(message->argument(), isolate);

    MaybeDirectHandle<Object> maybe_stringified;
    DirectHandle<Object> stringified;
    // Make sure we don't leak uncaught internally generated Error objects.
    if (IsJSError(*argument)) {
      maybe_stringified = Object::NoSideEffectsToString(isolate, argument);
    } else {
      v8::TryCatch catcher(reinterpret_cast<v8::Isolate*>(isolate));
      catcher.SetVerbose(false);
      catcher.SetCaptureMessage(false);

      maybe_stringified = Object::ToString(isolate, argument);
    }

    if (!maybe_stringified.ToHandle(&stringified)) {
      isolate->clear_pending_message();
      stringified = isolate->factory()->exception_string();
    }
    message->set_argument(*stringified);
  }

  v8::Local<v8::Value> api_exception_obj = v8::Utils::ToLocal(exception);
  ReportMessageNoExceptions(isolate, loc, message, api_exception_obj);
}

void MessageHandler::ReportMessageNoExceptions(
    Isolate* isolate, const MessageLocation* loc, DirectHandle<Object> message,
    v8::Local<v8::Value> api_exception_obj) {
  v8::Local<v8::Message> api_message_obj = v8::Utils::MessageToLocal(message);
  int error_level = api_message_obj->ErrorLevel();

  DirectHandle<ArrayList> global_listeners =
      isolate->factory()->message_listeners();
  int global_length = global_listeners->length();
  if (global_length == 0) {
    DefaultMessageReport(isolate, loc, message);
  } else {
    for (int i = 0; i < global_length; i++) {
      HandleScope scope(isolate);
      if (IsUndefined(global_listeners->get(i), isolate)) continue;
      Tagged<FixedArray> listener = Cast<FixedArray>(global_listeners->get(i));
      Tagged<Foreign> callback_obj = Cast<Foreign>(listener->get(0));
      int32_t message_levels =
          static_cast<int32_t>(Smi::ToInt(listener->get(2)));
      if (!(message_levels & error_level)) {
        continue;
      }
      v8::MessageCallback callback = FUNCTION_CAST<v8::MessageCallback>(
          callback_obj->foreign_address<kMessageListenerTag>());
      DirectHandle<Object> callback_data(listener->get(1), isolate);
      {
        RCS_SCOPE(isolate, RuntimeCallCounterId::kMessageListenerCallback);
        // Do not allow exceptions to propagate.
        v8::TryCatch try_catch(reinterpret_cast<v8::Isolate*>(isolate));
        callback(api_message_obj, IsUndefined(*callback_data, isolate)
                                      ? api_exception_obj
                                      : v8::Utils::ToLocal(callback_data));
      }
    }
  }
}

Handle<String> MessageHandler::GetMessage(Isolate* isolate,
                                          DirectHandle<Object> data) {
  DirectHandle<JSMessageObject> message = Cast<JSMessageObject>(data);
  DirectHandle<Object> arg{message->argument(), isolate};
  return MessageFormatter::Format(isolate, message->type(),
                                  base::VectorOf({arg}));
}

std::unique_ptr<char[]> MessageHandler::GetLocalizedMessage(
    Isolate* isolate, DirectHandle<Object> data) {
  HandleScope scope(isolate);
  return GetMessage(isolate, data)->ToCString(DISALLOW_NULLS);
}

namespace {

// Convert the raw frames as written by Isolate::CaptureSimpleStackTrace into
// a JSArray of JSCallSite objects.
MaybeHandle<JSArray> GetStackFrames(Isolate* isolate,
                                    DirectHandle<FixedArray> frames) {
  int frame_count = frames->length();
  Handle<JSFunction> constructor = isolate->callsite_function();
  DirectHandle<FixedArray> sites =
      isolate->factory()->NewFixedArray(frame_count);
  for (int i = 0; i < frame_count; ++i) {
    Handle<CallSiteInfo> frame(Cast<CallSiteInfo>(frames->get(i)), isolate);
    Handle<JSObject> site;
    ASSIGN_RETURN_ON_EXCEPTION(isolate, site,
                               JSObject::New(constructor, constructor,
                                             Handle<AllocationSite>::null()));
    RETURN_ON_EXCEPTION(
        isolate, JSObject::SetOwnPropertyIgnoreAttributes(
                     site, isolate->factory()->call_site_info_symbol(), frame,
                     DONT_ENUM));
    sites->set(i, *site);
  }

  return isolate->factory()->NewJSArrayWithElements(sites);
}

MaybeHandle<Object> AppendErrorString(Isolate* isolate, Handle<Object> error,
                                      IncrementalStringBuilder* builder) {
  v8::TryCatch try_catch(reinterpret_cast<v8::Isolate*>(isolate));
  try_catch.SetVerbose(false);
  try_catch.SetCaptureMessage(false);
  MaybeHandle<String> err_str = ErrorUtils::ToString(
      isolate, Cast<Object>(error),
      ErrorUtils::ToStringMessageSource::kPreferOriginalMessage);
  if (err_str.is_null()) {
    // Error.toString threw. Try to return a string representation of the thrown
    // exception instead.

    DCHECK(isolate->has_exception());
    if (isolate->is_execution_terminating()) {
      return {};
    }
    Handle<Object> exception = handle(isolate->exception(), isolate);
    try_catch.Reset();

    err_str = ErrorUtils::ToString(
        isolate, exception,
        ErrorUtils::ToStringMessageSource::kPreferOriginalMessage);
    if (err_str.is_null()) {
      // Formatting the thrown exception threw again, give up.
      DCHECK(isolate->has_exception());
      if (isolate->is_execution_terminating()) return {};
      builder->AppendCStringLiteral("<error>");
    } else {
      // Formatted thrown exception successfully, append it.
      builder->AppendCStringLiteral("<error: ");
      builder->AppendString(err_str.ToHandleChecked());
      builder->AppendCharacter('>');
    }
  } else {
    builder->AppendString(err_str.ToHandleChecked());
  }

  return error;
}

class V8_NODISCARD PrepareStackTraceScope {
 public:
  explicit PrepareStackTraceScope(Isolate* isolate) : isolate_(isolate) {
    DCHECK(!isolate_->formatting_stack_trace());
    isolate_->set_formatting_stack_trace(true);
  }

  ~PrepareStackTraceScope() { isolate_->set_formatting_stack_trace(false); }

  PrepareStackTraceScope(const PrepareStackTraceScope&) = delete;
  PrepareStackTraceScope& operator=(const PrepareStackTraceScope&) = delete;

 private:
  Isolate* isolate_;
};

}  // namespace

// static
MaybeHandle<Object> ErrorUtils::FormatStackTrace(
    Isolate* isolate, Handle<JSObject> error, DirectHandle<Object> raw_stack) {
  if (v8_flags.correctness_fuzzer_suppressions) {
    return isolate->factory()->empty_string();
  }
  DCHECK(IsFixedArray(*raw_stack));
  auto elems = Cast<FixedArray>(raw_stack);

  const bool in_recursion = isolate->formatting_stack_trace();
  const bool has_overflowed = i::StackLimitCheck{isolate}.HasOverflowed();
  Handle<NativeContext> error_context;
  if (!in_recursion && !has_overflowed &&
      error->GetCreationContext(isolate).ToHandle(&error_context)) {
    if (isolate->HasPrepareStackTraceCallback()) {
      PrepareStackTraceScope scope(isolate);

      Handle<JSArray> sites;
      ASSIGN_RETURN_ON_EXCEPTION(isolate, sites,
                                 GetStackFrames(isolate, elems));

      Handle<Object> result;
      ASSIGN_RETURN_ON_EXCEPTION(
          isolate, result,
          isolate->RunPrepareStackTraceCallback(error_context, error, sites));
      return result;
    } else {
      Handle<JSFunction> global_error =
          handle(error_context->error_function(), isolate);

      // If there's a user-specified "prepareStackTrace" function, call it on
      // the frames and use its result.

      Handle<Object> prepare_stack_trace;
      ASSIGN_RETURN_ON_EXCEPTION(
          isolate, prepare_stack_trace,
          JSFunction::GetProperty(isolate, global_error, "prepareStackTrace"));

      if (IsJSFunction(*prepare_stack_trace)) {
        PrepareStackTraceScope scope(isolate);

        isolate->CountUsage(v8::Isolate::kErrorPrepareStackTrace);

        Handle<JSArray> sites;
        ASSIGN_RETURN_ON_EXCEPTION(isolate, sites,
                                   GetStackFrames(isolate, elems));

        const int argc = 2;
        base::ScopedVector<Handle<Object>> argv(argc);
        if (V8_UNLIKELY(IsJSGlobalObject(*error))) {
          // Pass global proxy instead of global object.
          argv[0] =
              handle(Cast<JSGlobalObject>(*error)->global_proxy(), isolate);
        } else {
          argv[0] = error;
        }
        argv[1] = sites;

        Handle<Object> result;

        ASSIGN_RETURN_ON_EXCEPTION(
            isolate, result,
            Execution::Call(isolate, prepare_stack_trace, global_error, argc,
                            argv.begin()));

        return result;
      }
    }
  }

  // Otherwise, run our internal formatting logic.
  IncrementalStringBuilder builder(isolate);

  RETURN_ON_EXCEPTION(isolate, AppendErrorString(isolate, error, &builder));

  for (int i = 0; i < elems->length(); ++i) {
    builder.AppendCStringLiteral("\n    at ");

    DirectHandle<CallSiteInfo> frame(Cast<CallSiteInfo>(elems->get(i)),
                                     isolate);

    v8::TryCatch try_catch(reinterpret_cast<v8::Isolate*>(isolate));
    SerializeCallSiteInfo(isolate, frame, &builder);

    if (isolate->has_exception()) {
      // CallSite.toString threw. Parts of the current frame might have been
      // stringified already regardless. Still, try to append a string
      // representation of the thrown exception.

      Handle<Object> exception(isolate->exception(), isolate);
      try_catch.Reset();

      MaybeHandle<String> exception_string =
          ErrorUtils::ToString(isolate, exception);
      if (exception_string.is_null()) {
        // Formatting the thrown exception threw again, give up.

        builder.AppendCStringLiteral("<error>");
      } else {
        // Formatted thrown exception successfully, append it.
        builder.AppendCStringLiteral("<error: ");
        builder.AppendString(exception_string.ToHandleChecked());
        builder.AppendCStringLiteral("<error>");
      }
    }
  }

  return indirect_handle(builder.Finish(), isolate);
}

Handle<String> MessageFormatter::Format(
    Isolate* isolate, MessageTemplate index,
    base::Vector<const DirectHandle<Object>> args) {
  constexpr size_t kMaxArgs = 3;
  DirectHandle<String> arg_strings[kMaxArgs];
  DCHECK_LE(args.size(), kMaxArgs);
  for (size_t i = 0; i < args.size(); ++i) {
    DCHECK(!args[i].is_null());
    arg_strings[i] = Object::NoSideEffectsToString(isolate, args[i]);
  }
  v8::TryCatch try_catch(reinterpret_cast<v8::Isolate*>(isolate));
  try_catch.SetVerbose(false);
  try_catch.SetCaptureMessage(false);
  MaybeHandle<String> maybe_result_string = MessageFormatter::TryFormat(
      isolate, index, base::VectorOf(arg_strings, args.size()));
  Handle<String> result_string;
  if (!maybe_result_string.ToHandle(&result_string)) {
    DCHECK(isolate->has_exception());
    return isolate->factory()->InternalizeString(
        base::StaticCharVector("<error>"));
  }
  // A string that has been obtained from JS code in this way is
  // likely to be a complicated ConsString of some sort.  We flatten it
  // here to improve the efficiency of converting it to a C string and
  // other operations that are likely to take place (see GetLocalizedMessage
  // for example).
  return String::Flatten(isolate, result_string);
}

const char* MessageFormatter::TemplateString(MessageTemplate index) {
  switch (index) {
#define CASE(NAME, STRING)       \
  case MessageTemplate::k##NAME: \
    return STRING;
    MESSAGE_TEMPLATES(CASE)
#undef CASE
    case MessageTemplate::kMessageCount:
      UNREACHABLE();
  }
}

MaybeHandle<String> MessageFormatter::TryFormat(
    Isolate* isolate, MessageTemplate index,
    base::Vector<const DirectHandle<String>> args) {
  const char* template_string = TemplateString(index);

  IncrementalStringBuilder builder(isolate);

  // TODO(14386): Get this list empty.
  static constexpr MessageTemplate kTemplatesWithMismatchedArguments[] = {
      MessageTemplate::kConstAssign,
      MessageTemplate::kConstructorNotReceiver,
      MessageTemplate::kDataCloneErrorDetachedArrayBuffer,
      MessageTemplate::kDataCloneErrorOutOfMemory,
      MessageTemplate::kIncompatibleMethodReceiver,
      MessageTemplate::kInvalidArgument,
      MessageTemplate::kInvalidArrayLength,
      MessageTemplate::kInvalidAtomicAccessIndex,
      MessageTemplate::kInvalidDataViewLength,
      MessageTemplate::kInvalidIndex,
      MessageTemplate::kInvalidLhsInAssignment,
      MessageTemplate::kInvalidLhsInFor,
      MessageTemplate::kInvalidLhsInPostfixOp,
      MessageTemplate::kInvalidLhsInPrefixOp,
      MessageTemplate::kInvalidPrivateBrandReinitialization,
      MessageTemplate::kInvalidPrivateFieldReinitialization,
      MessageTemplate::kInvalidPrivateMemberWrite,
      MessageTemplate::kInvalidRegExpExecResult,
      MessageTemplate::kInvalidTimeValue,
      MessageTemplate::kInvalidWeakMapKey,
      MessageTemplate::kInvalidWeakSetValue,
      MessageTemplate::kIteratorReduceNoInitial,
      MessageTemplate::kJsonParseShortString,
      MessageTemplate::kJsonParseUnexpectedEOS,
      MessageTemplate::kJsonParseUnexpectedTokenEndStringWithContext,
      MessageTemplate::kJsonParseUnexpectedTokenShortString,
      MessageTemplate::kJsonParseUnexpectedTokenStartStringWithContext,
      MessageTemplate::kJsonParseUnexpectedTokenSurroundStringWithContext,
      MessageTemplate::kMustBePositive,
      MessageTemplate::kNotIterable,
      MessageTemplate::kNotTypedArray,
      MessageTemplate::kProxyNonObject,
      MessageTemplate::kProxyPrivate,
      MessageTemplate::kProxyRevoked,
      MessageTemplate::kProxyTrapReturnedFalsishFor,
      MessageTemplate::kReduceNoInitial,
      MessageTemplate::kSpreadIteratorSymbolNonCallable,
      MessageTemplate::kSymbolIteratorInvalid,
      MessageTemplate::kTopLevelAwaitStalled,
      MessageTemplate::kUndefinedOrNullToObject,
      MessageTemplate::kUnexpectedStrictReserved,
      MessageTemplate::kUnexpectedTokenIdentifier,
      MessageTemplate::kWeakRefsCleanupMustBeCallable};

  base::Vector<const DirectHandle<String>> remaining_args = args;
  for (const char* c = template_string; *c != '\0'; c++) {
    if (*c == '%') {
      // %% results in verbatim %.
      if (*(c + 1) == '%') {
        c++;
        builder.AppendCharacter('%');
      } else {
        // TODO(14386): Remove this fallback.
        if (remaining_args.empty()) {
          if (std::count(std::begin(kTemplatesWithMismatchedArguments),
                         std::end(kTemplatesWithMismatchedArguments), index)) {
            builder.AppendCString("undefined");
          } else {
            FATAL("Missing argument to template (got %zu): %s", args.size(),
                  template_string);
          }
        } else {
          DirectHandle<String> arg = remaining_args[0];
          remaining_args += 1;
          builder.AppendString(arg);
        }
      }
    } else {
      builder.AppendCharacter(*c);
    }
  }
  if (!remaining_args.empty() &&
      std::count(std::begin(kTemplatesWithMismatchedArguments),
                 std::end(kTemplatesWithMismatchedArguments), index) == 0) {
    FATAL("Too many arguments to template (expected %zu, got %zu): %s",
          args.size() - remaining_args.size(), args.size(), template_string);
  }

  return indirect_handle(builder.Finish(), isolate);
}

MaybeHandle<JSObject> ErrorUtils::Construct(Isolate* isolate,
                                            Handle<JSFunction> target,
                                            Handle<Object> new_target,
                                            DirectHandle<Object> message,
                                            Handle<Object> options) {
  FrameSkipMode mode = SKIP_FIRST;
  Handle<Object> caller;

  // When we're passed a JSFunction as new target, we can skip frames until that
  // specific function is seen instead of unconditionally skipping the first
  // frame.
  if (IsJSFunction(*new_target)) {
    mode = SKIP_UNTIL_SEEN;
    caller = new_target;
  }

  return ErrorUtils::Construct(isolate, target, new_target, message, options,
                               mode, caller,
                               ErrorUtils::StackTraceCollection::kEnabled);
}

MaybeHandle<JSObject> ErrorUtils::Construct(
    Isolate* isolate, Handle<JSFunction> target, Handle<Object> new_target,
    DirectHandle<Object> message, Handle<Object> options, FrameSkipMode mode,
    Handle<Object> caller, StackTraceCollection stack_trace_collection) {
  if (v8_flags.correctness_fuzzer_suppressions) {
    // Abort range errors in correctness fuzzing, as their causes differ
    // accross correctness-fuzzing scenarios.
    if (target.is_identical_to(isolate->range_error_function())) {
      FATAL("Aborting on range error");
    }
    // Ignore error messages in correctness fuzzing, because the spec leaves
    // room for undefined behavior.
    message = isolate->factory()->InternalizeUtf8String(
        "Message suppressed for fuzzers (--correctness-fuzzer-suppressions)");
  }

  // 1. If NewTarget is undefined, let newTarget be the active function object,
  // else let newTarget be NewTarget.
  Handle<JSReceiver> new_target_recv = IsJSReceiver(*new_target)
                                           ? Cast<JSReceiver>(new_target)
                                           : Cast<JSReceiver>(target);

  // 2. Let O be ? OrdinaryCreateFromConstructor(newTarget, "%ErrorPrototype%",
  //    « [[ErrorData]] »).
  Handle<JSObject> err;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, err,
      JSObject::New(target, new_target_recv, Handle<AllocationSite>::null()));

  // 3. If message is not undefined, then
  //  a. Let msg be ? ToString(message).
  //  b. Let msgDesc be the PropertyDescriptor{[[Value]]: msg, [[Writable]]:
  //     true, [[Enumerable]]: false, [[Configurable]]: true}.
  //  c. Perform ! DefinePropertyOrThrow(O, "message", msgDesc).
  // 4. Return O.
  if (!IsUndefined(*message, isolate)) {
    Handle<String> msg_string;
    ASSIGN_RETURN_ON_EXCEPTION(
        isolate, msg_string,
        indirect_handle(Object::ToString(isolate, message), isolate));
    RETURN_ON_EXCEPTION(isolate, JSObject::SetOwnPropertyIgnoreAttributes(
                                     err, isolate->factory()->message_string(),
                                     msg_string, DONT_ENUM));

    if (v8_flags.use_original_message_for_stack_trace) {
      RETURN_ON_EXCEPTION(isolate,
                          JSObject::SetOwnPropertyIgnoreAttributes(
                              err, isolate->factory()->error_message_symbol(),
                              msg_string, DONT_ENUM));
    }
  }

  if (!IsUndefined(*options, isolate)) {
    // If Type(options) is Object and ? HasProperty(options, "cause") then
    //   a. Let cause be ? Get(options, "cause").
    //   b. Perform ! CreateNonEnumerableDataPropertyOrThrow(O, "cause", cause).
    Handle<Name> cause_string = isolate->factory()->cause_string();
    if (IsJSReceiver(*options)) {
      Handle<JSReceiver> js_options = Cast<JSReceiver>(options);
      Maybe<bool> has_cause =
          JSObject::HasProperty(isolate, js_options, cause_string);
      if (has_cause.IsNothing()) {
        DCHECK((isolate)->has_exception());
        return MaybeHandle<JSObject>();
      }
      if (has_cause.ToChecked()) {
        Handle<Object> cause;
        ASSIGN_RETURN_ON_EXCEPTION(
            isolate, cause,
            JSObject::GetProperty(isolate, js_options, cause_string));
        RETURN_ON_EXCEPTION(isolate, JSObject::SetOwnPropertyIgnoreAttributes(
                                         err, cause_string, cause, DONT_ENUM));
      }
    }
  }

  switch (stack_trace_collection) {
    case StackTraceCollection::kEnabled:
      RETURN_ON_EXCEPTION(isolate,
                          isolate->CaptureAndSetErrorStack(err, mode, caller));
      break;
    case StackTraceCollection::kDisabled:
      break;
  }
  return err;
}

namespace {

MaybeHandle<String> GetStringPropertyOrDefault(Isolate* isolate,
                                               Handle<JSReceiver> recv,
                                               Handle<String> key,
                                               Handle<String> default_str) {
  Handle<Object> obj;
  ASSIGN_RETURN_ON_EXCEPTION(isolate, obj,
                             JSObject::GetProperty(isolate, recv, key));

  Handle<String> str;
  if (IsUndefined(*obj, isolate)) {
    str = default_str;
  } else {
    ASSIGN_RETURN_ON_EXCEPTION(isolate, str, Object::ToString(isolate, obj));
  }

  return str;
}

}  // namespace

// ES6 section 19.5.3.4 Error.prototype.toString ( )
MaybeHandle<String> ErrorUtils::ToString(Isolate* isolate,
                                         Handle<Object> receiver,
                                         ToStringMessageSource message_source) {
  // 1. Let O be the this value.
  // 2. If Type(O) is not Object, throw a TypeError exception.
  if (!IsJSReceiver(*receiver)) {
    THROW_NEW_ERROR(isolate,
                    NewTypeError(MessageTemplate::kIncompatibleMethodReceiver,
                                 isolate->factory()->NewStringFromAsciiChecked(
                                     "Error.prototype.toString"),
                                 receiver));
  }
  Handle<JSReceiver> recv = Cast<JSReceiver>(receiver);

  // 3. Let name be ? Get(O, "name").
  // 4. If name is undefined, let name be "Error"; otherwise let name be
  // ? ToString(name).
  Handle<String> name_key = isolate->factory()->name_string();
  Handle<String> name_default = isolate->factory()->Error_string();
  Handle<String> name;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, name,
      GetStringPropertyOrDefault(isolate, recv, name_key, name_default));

  // 5. Let msg be ? Get(O, "message").
  // 6. If msg is undefined, let msg be the empty String; otherwise let msg be
  // ? ToString(msg).
  Handle<String> msg;
  Handle<String> msg_default = isolate->factory()->empty_string();
  if (message_source == ToStringMessageSource::kPreferOriginalMessage) {
    // V8-specific extension for Error.stack: Use the original message with
    // which the Error constructor was called. This keeps Error.stack consistent
    // w.r.t. "message" property changes regardless of the time when Error.stack
    // is accessed the first time.
    //
    // If |recv| was not constructed with %Error%, use the "message" property.
    LookupIterator it(isolate, LookupIterator::PROTOTYPE_CHAIN_SKIP_INTERCEPTOR,
                      receiver, isolate->factory()->error_message_symbol());
    Handle<Object> result = JSReceiver::GetDataProperty(&it);
    if (it.IsFound() && IsUndefined(*result, isolate)) {
      msg = msg_default;
    } else if (it.IsFound()) {
      ASSIGN_RETURN_ON_EXCEPTION(isolate, msg,
                                 Object::ToString(isolate, result));
    }
  }

  if (msg.is_null()) {
    Handle<String> msg_key = isolate->factory()->message_string();
    ASSIGN_RETURN_ON_EXCEPTION(
        isolate, msg,
        GetStringPropertyOrDefault(isolate, recv, msg_key, msg_default));
  }

  // 7. If name is the empty String, return msg.
  // 8. If msg is the empty String, return name.
  if (name->length() == 0) return msg;
  if (msg->length() == 0) return name;

  // 9. Return the result of concatenating name, the code unit 0x003A (COLON),
  // the code unit 0x0020 (SPACE), and msg.
  IncrementalStringBuilder builder(isolate);
  builder.AppendString(name);
  builder.AppendCStringLiteral(": ");
  builder.AppendString(msg);

  Handle<String> result;
  ASSIGN_RETURN_ON_EXCEPTION(isolate, result,
                             indirect_handle(builder.Finish(), isolate));
  return result;
}

// static
Handle<JSObject> ErrorUtils::MakeGenericError(
    Isolate* isolate, Handle<JSFunction> constructor, MessageTemplate index,
    base::Vector<const DirectHandle<Object>> args, FrameSkipMode mode) {
  if (v8_flags.clear_exceptions_on_js_entry) {
    // This function used to be implemented in JavaScript, and JSEntry
    // clears any exceptions - so whenever we'd call this from C++,
    // exceptions would be cleared. Preserve this behavior.
    isolate->clear_exception();
    isolate->clear_pending_message();
  }
  DirectHandle<String> msg = MessageFormatter::Format(isolate, index, args);
  Handle<Object> options = isolate->factory()->undefined_value();

  DCHECK(mode != SKIP_UNTIL_SEEN);

  Handle<Object> no_caller;
  // The call below can't fail because constructor is a builtin.
  DCHECK(constructor->shared()->HasBuiltinId());
  return ErrorUtils::Construct(isolate, constructor, constructor, msg, options,
                               mode, no_caller, StackTraceCollection::kEnabled)
      .ToHandleChecked();
}

// static
Handle<JSObject> ErrorUtils::ShadowRealmConstructTypeErrorCopy(
    Isolate* isolate, Handle<Object> original, MessageTemplate index,
    base::Vector<const DirectHandle<Object>> args) {
  if (v8_flags.clear_exceptions_on_js_entry) {
    // This function used to be implemented in JavaScript, and JSEntry
    // clears any exceptions - so whenever we'd call this from C++,
    // exceptions would be cleared. Preserve this behavior.
    isolate->clear_exception();
    isolate->clear_pending_message();
  }
  DirectHandle<String> msg = MessageFormatter::Format(isolate, index, args);
  Handle<Object> options = isolate->factory()->undefined_value();

  Handle<JSObject> maybe_error_object;
  Handle<Object> error_stack;
  StackTraceCollection collection = StackTraceCollection::kEnabled;
  if (IsJSObject(*original)) {
    maybe_error_object = Cast<JSObject>(original);
    if (!ErrorUtils::GetFormattedStack(isolate, maybe_error_object)
             .ToHandle(&error_stack)) {
      DCHECK(isolate->has_exception());
      DirectHandle<Object> exception = handle(isolate->exception(), isolate);
      isolate->clear_exception();
      // Return a new side-effect-free TypeError to be loud about inner error.
      DirectHandle<String> string =
          Object::NoSideEffectsToString(isolate, exception);
      return isolate->factory()->NewTypeError(
          MessageTemplate::kShadowRealmErrorStackThrows, string);
    } else if (IsNullOrUndefined(*error_stack)) {
      // If the error stack property is null or undefined, create a new error.
      collection = StackTraceCollection::kEnabled;
    } else if (IsPrimitive(*error_stack)) {
      // If the error stack property is found (must be a formatted string, not
      // an unformatted FixedArray), set collection to disabled and reuse the
      // existing stack. If the `Error.prepareStackTrace` returned a primitive,
      // use it as the stack as well.
      collection = StackTraceCollection::kDisabled;
    } else {
      // The error stack property is an arbitrary value. Return a new TypeError
      // about the non-string value.
      DirectHandle<String> string =
          Object::NoSideEffectsToString(isolate, error_stack);
      return isolate->factory()->NewTypeError(
          MessageTemplate::kShadowRealmErrorStackNonString, string);
    }
  }

  Handle<Object> no_caller;
  Handle<JSFunction> constructor = isolate->type_error_function();
  Handle<JSObject> new_error =
      ErrorUtils::Construct(isolate, constructor, constructor, msg, options,
                            FrameSkipMode::SKIP_NONE, no_caller, collection)
          .ToHandleChecked();

  // If collection is disabled, reuse the existing stack string from the
  // original error object.
  if (collection == StackTraceCollection::kDisabled) {
    // Error stack symbol is a private symbol and set it on an error object
    // created from built-in error constructor should not throw.
    Object::SetProperty(
        isolate, new_error, isolate->factory()->error_stack_symbol(),
        error_stack, StoreOrigin::kMaybeKeyed, Just(ShouldThrow::kThrowOnError))
        .Check();
  }

  return new_error;
}

namespace {

bool ComputeLocation(Isolate* isolate, MessageLocation* target) {
  JavaScriptStackFrameIterator it(isolate);
  if (!it.done()) {
    // Compute the location from the function and the relocation info of the
    // baseline code. For optimized code this will use the deoptimization
    // information to get canonical location information.
    std::vector<FrameSummary> frames;
    it.frame()->Summarize(&frames);
    auto& summary = frames.back().AsJavaScript();
    Handle<SharedFunctionInfo> shared(summary.function()->shared(), isolate);
    Handle<Object> script(shared->script(), isolate);
    SharedFunctionInfo::EnsureSourcePositionsAvailable(isolate, shared);
    int pos =
        summary.abstract_code()->SourcePosition(isolate, summary.code_offset());
    if (IsScript(*script) &&
        !(IsUndefined(Cast<Script>(script)->source(), isolate))) {
      Handle<Script> casted_script = Cast<Script>(script);
      *target = MessageLocation(casted_script, pos, pos + 1, shared);
      return true;
    }
  }
  return false;
}

Handle<String> BuildDefaultCallSite(Isolate* isolate, Handle<Object> object) {
  IncrementalStringBuilder builder(isolate);

  builder.AppendString(Object::TypeOf(isolate, object));
  if (IsString(*object)) {
    builder.AppendCStringLiteral(" \"");
    Handle<String> string = Cast<String>(object);
    // This threshold must be sufficiently far below String::kMaxLength that
    // the {builder}'s result can never exceed that limit.
    constexpr int kMaxPrintedStringLength = 100;
    if (string->length() <= kMaxPrintedStringLength) {
      builder.AppendString(string);
    } else {
      string = isolate->factory()->NewProperSubString(string, 0,
                                                      kMaxPrintedStringLength);
      builder.AppendString(string);
      builder.AppendCStringLiteral("<...>");
    }
    builder.AppendCStringLiteral("\"");
  } else if (IsNull(*object, isolate)) {
    builder.AppendCStringLiteral(" null");
  } else if (IsTrue(*object, isolate)) {
    builder.AppendCStringLiteral(" true");
  } else if (IsFalse(*object, isolate)) {
    builder.AppendCStringLiteral(" false");
  } else if (IsNumber(*object)) {
    builder.AppendCharacter(' ');
    builder.AppendString(isolate->factory()->NumberToString(object));
  }

  return indirect_handle(builder.Finish().ToHandleChecked(), isolate);
}

Handle<String> RenderCallSite(Isolate* isolate, Handle<Object> object,
                              MessageLocation* location,
                              CallPrinter::ErrorHint* hint) {
  if (ComputeLocation(isolate, location)) {
    UnoptimizedCompileFlags flags = UnoptimizedCompileFlags::ForFunctionCompile(
        isolate, *location->shared());
    flags.set_is_reparse(true);
    UnoptimizedCompileState compile_state;
    ReusableUnoptimizedCompileState reusable_state(isolate);
    ParseInfo info(isolate, flags, &compile_state, &reusable_state);
    if (parsing::ParseAny(&info, location->shared(), isolate,
                          parsing::ReportStatisticsMode::kNo)) {
      info.ast_value_factory()->Internalize(isolate);
      CallPrinter printer(isolate, location->shared()->IsUserJavaScript());
      Handle<String> str = printer.Print(info.literal(), location->start_pos());
      *hint = printer.GetErrorHint();
      if (str->length() > 0) return str;
    }
  }
  return BuildDefaultCallSite(isolate, object);
}

MessageTemplate UpdateErrorTemplate(CallPrinter::ErrorHint hint,
                                    MessageTemplate default_id) {
  switch (hint) {
    case CallPrinter::ErrorHint::kNormalIterator:
      return MessageTemplate::kNotIterable;

    case CallPrinter::ErrorHint::kCallAndNormalIterator:
      return MessageTemplate::kNotCallableOrIterable;

    case CallPrinter::ErrorHint::kAsyncIterator:
      return MessageTemplate::kNotAsyncIterable;

    case CallPrinter::ErrorHint::kCallAndAsyncIterator:
      return MessageTemplate::kNotCallableOrAsyncIterable;

    case CallPrinter::ErrorHint::kNone:
      return default_id;
  }
}

}  // namespace

Handle<JSObject> ErrorUtils::NewIteratorError(Isolate* isolate,
                                              Handle<Object> source) {
  MessageLocation location;
  CallPrinter::ErrorHint hint = CallPrinter::ErrorHint::kNone;
  Handle<String> callsite = RenderCallSite(isolate, source, &location, &hint);
  MessageTemplate id = MessageTemplate::kNotIterableNoSymbolLoad;

  if (hint == CallPrinter::ErrorHint::kNone) {
    Handle<Symbol> iterator_symbol = isolate->factory()->iterator_symbol();
    return isolate->factory()->NewTypeError(id, callsite, iterator_symbol);
  }

  id = UpdateErrorTemplate(hint, id);
  return isolate->factory()->NewTypeError(id, callsite);
}

Tagged<Object> ErrorUtils::ThrowSpreadArgError(Isolate* isolate,
                                               MessageTemplate id,
                                               Handle<Object> object) {
  MessageLocation location;
  Handle<String> callsite;
  if (ComputeLocation(isolate, &location)) {
    UnoptimizedCompileFlags flags = UnoptimizedCompileFlags::ForFunctionCompile(
        isolate, *location.shared());
    flags.set_is_reparse(true);
    UnoptimizedCompileState compile_state;
    ReusableUnoptimizedCompileState reusable_state(isolate);
    ParseInfo info(isolate, flags, &compile_state, &reusable_state);
    if (parsing::ParseAny(&info, location.shared(), isolate,
                          parsing::ReportStatisticsMode::kNo)) {
      info.ast_value_factory()->Internalize(isolate);
      CallPrinter printer(isolate, location.shared()->IsUserJavaScript(),
                          CallPrinter::SpreadErrorInArgsHint::kErrorInArgs);
      Handle<String> str = printer.Print(info.literal(), location.start_pos());
      callsite =
          str->length() > 0 ? str : BuildDefaultCallSite(isolate, object);

      if (printer.spread_arg() != nullptr) {
        // Change the message location to point at the property name.
        int pos = printer.spread_arg()->position();
        location =
            MessageLocation(location.script(), pos, pos + 1, location.shared());
      }
    } else {
      callsite = BuildDefaultCallSite(isolate, object);
    }
  }

  isolate->ThrowAt(isolate->factory()->NewTypeError(id, callsite, object),
                   &location);
  return ReadOnlyRoots(isolate).exception();
}

Handle<JSObject> ErrorUtils::NewCalledNonCallableError(Isolate* isolate,
                                                       Handle<Object> source) {
  MessageLocation location;
  CallPrinter::ErrorHint hint = CallPrinter::ErrorHint::kNone;
  Handle<String> callsite = RenderCallSite(isolate, source, &location, &hint);
  MessageTemplate id = MessageTemplate::kCalledNonCallable;
  id = UpdateErrorTemplate(hint, id);
  return isolate->factory()->NewTypeError(id, callsite);
}

Handle<JSObject> ErrorUtils::NewConstructedNonConstructable(
    Isolate* isolate, Handle<Object> source) {
  MessageLocation location;
  CallPrinter::ErrorHint hint = CallPrinter::ErrorHint::kNone;
  Handle<String> callsite = RenderCallSite(isolate, source, &location, &hint);
  MessageTemplate id = MessageTemplate::kNotConstructor;
  return isolate->factory()->NewTypeError(id, callsite);
}

Tagged<Object> ErrorUtils::ThrowLoadFromNullOrUndefined(
    Isolate* isolate, Handle<Object> object, MaybeDirectHandle<Object> key) {
  DCHECK(IsNullOrUndefined(*object));

  MaybeDirectHandle<String> maybe_property_name;

  // Try to extract the property name from the given key, if any.
  DirectHandle<Object> key_handle;
  if (key.ToHandle(&key_handle)) {
    if (IsString(*key_handle)) {
      maybe_property_name = Cast<String>(key_handle);
    } else {
      maybe_property_name =
          Object::NoSideEffectsToMaybeString(isolate, key_handle);
    }
  }

  Handle<String> callsite;

  // Inline the RenderCallSite logic here so that we can additionally access the
  // destructuring property.
  bool location_computed = false;
  bool is_destructuring = false;
  MessageLocation location;
  if (ComputeLocation(isolate, &location)) {
    location_computed = true;

    UnoptimizedCompileFlags flags = UnoptimizedCompileFlags::ForFunctionCompile(
        isolate, *location.shared());
    flags.set_is_reparse(true);
    UnoptimizedCompileState compile_state;
    ReusableUnoptimizedCompileState reusable_state(isolate);
    ParseInfo info(isolate, flags, &compile_state, &reusable_state);
    if (parsing::ParseAny(&info, location.shared(), isolate,
                          parsing::ReportStatisticsMode::kNo)) {
      info.ast_value_factory()->Internalize(isolate);
      CallPrinter printer(isolate, location.shared()->IsUserJavaScript());
      Handle<String> str = printer.Print(info.literal(), location.start_pos());

      int pos = -1;
      is_destructuring = printer.destructuring_assignment() != nullptr;

      if (is_destructuring) {
        // If we don't have one yet, try to extract the property name from the
        // destructuring property in the AST.
        ObjectLiteralProperty* destructuring_prop =
            printer.destructuring_prop();
        if (maybe_property_name.is_null() && destructuring_prop != nullptr &&
            destructuring_prop->key()->IsPropertyName()) {
          maybe_property_name = destructuring_prop->key()
                                    ->AsLiteral()
                                    ->AsRawPropertyName()
                                    ->string();
          // Change the message location to point at the property name.
          pos = destructuring_prop->key()->position();
        }
        if (maybe_property_name.is_null()) {
          // Change the message location to point at the destructured value.
          pos = printer.destructuring_assignment()->value()->position();
        }

        // If we updated the pos to a valid pos, rewrite the location.
        if (pos != -1) {
          location = MessageLocation(location.script(), pos, pos + 1,
                                     location.shared());
        }
      }

      if (str->length() > 0) callsite = str;
    }
  }

  if (callsite.is_null()) {
    callsite = BuildDefaultCallSite(isolate, object);
  }

  Handle<JSObject> error;
  DirectHandle<String> property_name;
  if (is_destructuring) {
    if (maybe_property_name.ToHandle(&property_name)) {
      error = isolate->factory()->NewTypeError(
          MessageTemplate::kNonCoercibleWithProperty, property_name, callsite,
          object);
    } else {
      error = isolate->factory()->NewTypeError(MessageTemplate::kNonCoercible,
                                               callsite, object);
    }
  } else {
    if (!key.ToHandle(&key_handle) ||
        !maybe_property_name.ToHandle(&property_name)) {
      error = isolate->factory()->NewTypeError(
          MessageTemplate::kNonObjectPropertyLoad, object);
    } else if (*key_handle == ReadOnlyRoots(isolate).iterator_symbol()) {
      error = NewIteratorError(isolate, object);
    } else {
      error = isolate->factory()->NewTypeError(
          MessageTemplate::kNonObjectPropertyLoadWithProperty, object,
          property_name);
    }
  }

  if (location_computed) {
    isolate->ThrowAt(error, &location);
  } else {
    isolate->Throw(*error);
  }
  return ReadOnlyRoots(isolate).exception();
}

// static
bool ErrorUtils::HasErrorStackSymbolOwnProperty(Isolate* isolate,
                                                Handle<JSObject> object) {
  // TODO(v8:5962): consider adding object->IsWasmExceptionPackage() here
  // once it's guaranteed that WasmExceptionPackage has |error_stack_symbol|
  // property.
  Handle<Name> name = isolate->factory()->error_stack_symbol();
  if (IsJSError(*object)) {
    DCHECK(JSReceiver::HasOwnProperty(isolate, object, name).FromMaybe(false));
    return true;
  }
  return JSReceiver::HasOwnProperty(isolate, object, name).FromMaybe(false);
}

// static
ErrorUtils::StackPropertyLookupResult ErrorUtils::GetErrorStackProperty(
    Isolate* isolate, Handle<JSReceiver> maybe_error_object) {
  LookupIterator it(isolate, LookupIterator::PROTOTYPE_CHAIN_SKIP_INTERCEPTOR,
                    maybe_error_object,
                    isolate->factory()->error_stack_symbol());
  Handle<Object> result = JSReceiver::GetDataProperty(&it);

  if (!it.IsFound()) {
    return {MaybeHandle<JSObject>{}, isolate->factory()->undefined_value()};
  }
  return {it.GetHolder<JSObject>(), result};
}

// static
MaybeHandle<Object> ErrorUtils::GetFormattedStack(
    Isolate* isolate, Handle<JSObject> maybe_error_object) {
  TRACE_EVENT0(TRACE_DISABLED_BY_DEFAULT("v8.stack_trace"), __func__);

  ErrorUtils::StackPropertyLookupResult lookup =
      ErrorUtils::GetErrorStackProperty(isolate, maybe_error_object);

  if (IsErrorStackData(*lookup.error_stack)) {
    auto error_stack_data = Cast<ErrorStackData>(lookup.error_stack);
    if (error_stack_data->HasFormattedStack()) {
      return handle(error_stack_data->formatted_stack(), isolate);
    }
    ErrorStackData::EnsureStackFrameInfos(isolate, error_stack_data);

    Handle<JSObject> error_object =
        lookup.error_stack_symbol_holder.ToHandleChecked();
    Handle<Object> formatted_stack;
    ASSIGN_RETURN_ON_EXCEPTION(
        isolate, formatted_stack,
        FormatStackTrace(isolate, error_object,
                         handle(error_stack_data->call_site_infos(), isolate)));
    error_stack_data->set_formatted_stack(*formatted_stack);
    return formatted_stack;
  }

  if (IsFixedArray(*lookup.error_stack)) {
    Handle<JSObject> error_object =
        lookup.error_stack_symbol_holder.ToHandleChecked();
    Handle<Object> formatted_stack;
    ASSIGN_RETURN_ON_EXCEPTION(
        isolate, formatted_stack,
        FormatStackTrace(isolate, error_object,
                         Cast<FixedArray>(lookup.error_stack)));
    RETURN_ON_EXCEPTION(
        isolate, Object::SetProperty(isolate, error_object,
                                     isolate->factory()->error_stack_symbol(),
                                     formatted_stack, StoreOrigin::kMaybeKeyed,
                                     Just(ShouldThrow::kThrowOnError)));
    return formatted_stack;
  }

  return lookup.error_stack;
}

// static
void ErrorUtils::SetFormattedStack(Isolate* isolate,
                                   Handle<JSObject> maybe_error_object,
                                   Handle<Object> formatted_stack) {
  ErrorUtils::StackPropertyLookupResult lookup =
      ErrorUtils::GetErrorStackProperty(isolate, maybe_error_object);

  Handle<JSObject> error_object;
  // Do nothing in case |maybe_error_object| is not an Error, i.e. its
  // prototype doesn't contain objects with |error_stack_symbol| property.
  if (!lookup.error_stack_symbol_holder.ToHandle(&error_object)) return;

  if (IsErrorStackData(*lookup.error_stack)) {
    auto error_stack_data = Cast<ErrorStackData>(lookup.error_stack);
    ErrorStackData::EnsureStackFrameInfos(isolate, error_stack_data);
    error_stack_data->set_formatted_stack(*formatted_stack);
  } else {
    Object::SetProperty(isolate, error_object,
                        isolate->factory()->error_stack_symbol(),
                        formatted_stack, StoreOrigin::kMaybeKeyed,
                        Just(ShouldThrow::kThrowOnError))
        .Check();
  }
}

// static
MaybeHandle<Object> ErrorUtils::CaptureStackTrace(Isolate* isolate,
                                                  Handle<JSObject> object,
                                                  FrameSkipMode mode,
                                                  Handle<Object> caller) {
  Factory* factory = isolate->factory();
  Handle<Name> name = factory->stack_string();

  // Explicitly check for frozen objects to simplify things since we need to
  // add both "stack" and "error_stack_symbol" properties in one go.
  if (!JSObject::IsExtensible(isolate, object)) {
    THROW_NEW_ERROR(isolate,
                    NewTypeError(MessageTemplate::kDefineDisallowed, name));
  }

  // Add the stack accessors.
  PropertyDescriptor desc;
  desc.set_enumerable(false);
  desc.set_configurable(true);
  desc.set_get(factory->error_stack_getter_fun_template());
  desc.set_set(factory->error_stack_setter_fun_template());
  Maybe<bool> success = JSReceiver::DefineOwnProperty(
      isolate, object, name, &desc, Just(kThrowOnError));

  MAYBE_RETURN(success, {});

  // Collect the stack trace and store it in |object|'s private
  // "error_stack_symbol" property.
  RETURN_ON_EXCEPTION(isolate,
                      isolate->CaptureAndSetErrorStack(object, mode, caller));

  return isolate->factory()->undefined_value();
}

}  // namespace internal
}  // namespace v8
