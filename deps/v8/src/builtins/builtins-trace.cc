// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/api/api-inl.h"
#include "src/builtins/builtins-utils-inl.h"
#include "src/builtins/builtins.h"
#include "src/heap/heap-inl.h"  // For ToBoolean. TODO(jkummerow): Drop.
#include "src/json/json-stringifier.h"
#include "src/logging/counters.h"
#include "src/objects/objects-inl.h"
#include "src/tracing/traced-value.h"

#if defined(V8_USE_PERFETTO)
#include "protos/perfetto/trace/track_event/debug_annotation.pbzero.h"
#endif

namespace v8 {
namespace internal {

namespace {

#define MAX_STACK_LENGTH 100

class MaybeUtf8 {
 public:
  explicit MaybeUtf8(Isolate* isolate, Handle<String> string) : buf_(data_) {
    string = String::Flatten(isolate, string);
    int len;
    if (string->IsOneByteRepresentation()) {
      // Technically this allows unescaped latin1 characters but the trace
      // events mechanism currently does the same and the current consuming
      // tools are tolerant of it. A more correct approach here would be to
      // escape non-ascii characters but this is easier and faster.
      len = string->length();
      AllocateSufficientSpace(len);
      if (len > 0) {
        // Why copy? Well, the trace event mechanism requires null-terminated
        // strings, the bytes we get from SeqOneByteString are not. buf_ is
        // guaranteed to be null terminated.
        DisallowGarbageCollection no_gc;
        memcpy(buf_, Handle<SeqOneByteString>::cast(string)->GetChars(no_gc),
               len);
      }
    } else {
      Local<v8::String> local = Utils::ToLocal(string);
      auto* v8_isolate = reinterpret_cast<v8::Isolate*>(isolate);
      len = local->Utf8Length(v8_isolate);
      AllocateSufficientSpace(len);
      if (len > 0) {
        local->WriteUtf8(v8_isolate, reinterpret_cast<char*>(buf_));
      }
    }
    buf_[len] = 0;
  }
  const char* operator*() const { return reinterpret_cast<const char*>(buf_); }

 private:
  void AllocateSufficientSpace(int len) {
    if (len + 1 > MAX_STACK_LENGTH) {
      allocated_ = std::make_unique<uint8_t[]>(len + 1);
      buf_ = allocated_.get();
    }
  }

  // In the most common cases, the buffer here will be stack allocated.
  // A heap allocation will only occur if the data is more than MAX_STACK_LENGTH
  // Given that this is used primarily for trace event categories and names,
  // the MAX_STACK_LENGTH should be more than enough.
  uint8_t* buf_;
  uint8_t data_[MAX_STACK_LENGTH];
  std::unique_ptr<uint8_t[]> allocated_;
};

#if !defined(V8_USE_PERFETTO)
class JsonTraceValue : public ConvertableToTraceFormat {
 public:
  explicit JsonTraceValue(Isolate* isolate, Handle<String> object) {
    // object is a JSON string serialized using JSON.stringify() from within
    // the BUILTIN(Trace) method. This may (likely) contain UTF8 values so
    // to grab the appropriate buffer data we have to serialize it out. We
    // hold on to the bits until the AppendAsTraceFormat method is called.
    MaybeUtf8 data(isolate, object);
    data_ = *data;
  }

  void AppendAsTraceFormat(std::string* out) const override { *out += data_; }

 private:
  std::string data_;
};

const uint8_t* GetCategoryGroupEnabled(Isolate* isolate,
                                       Handle<String> string) {
  MaybeUtf8 category(isolate, string);
  return TRACE_EVENT_API_GET_CATEGORY_GROUP_ENABLED(*category);
}
#endif  // !defined(V8_USE_PERFETTO)

#undef MAX_STACK_LENGTH

}  // namespace

// Builins::kIsTraceCategoryEnabled(category) : bool
BUILTIN(IsTraceCategoryEnabled) {
  HandleScope scope(isolate);
  Handle<Object> category = args.atOrUndefined(isolate, 1);
  if (!IsString(*category)) {
    THROW_NEW_ERROR_RETURN_FAILURE(
        isolate, NewTypeError(MessageTemplate::kTraceEventCategoryError));
  }
  bool enabled;
#if defined(V8_USE_PERFETTO)
  MaybeUtf8 category_str(isolate, Handle<String>::cast(category));
  perfetto::DynamicCategory dynamic_category{*category_str};
  enabled = TRACE_EVENT_CATEGORY_ENABLED(dynamic_category);
#else
  enabled = *GetCategoryGroupEnabled(isolate, Handle<String>::cast(category));
#endif
  return isolate->heap()->ToBoolean(enabled);
}

// Builtin::kTrace(phase, category, name, id, data) : bool
BUILTIN(Trace) {
  HandleScope handle_scope(isolate);

  Handle<Object> phase_arg = args.atOrUndefined(isolate, 1);
  Handle<Object> category = args.atOrUndefined(isolate, 2);
  Handle<Object> name_arg = args.atOrUndefined(isolate, 3);
  Handle<Object> id_arg = args.atOrUndefined(isolate, 4);
  Handle<Object> data_arg = args.atOrUndefined(isolate, 5);

  // Exit early if the category group is not enabled.
#if defined(V8_USE_PERFETTO)
  MaybeUtf8 category_str(isolate, Handle<String>::cast(category));
  perfetto::DynamicCategory dynamic_category{*category_str};
  if (!TRACE_EVENT_CATEGORY_ENABLED(dynamic_category))
    return ReadOnlyRoots(isolate).false_value();
#else
  const uint8_t* category_group_enabled =
      GetCategoryGroupEnabled(isolate, Handle<String>::cast(category));
  if (!*category_group_enabled) return ReadOnlyRoots(isolate).false_value();
#endif

  if (!IsNumber(*phase_arg)) {
    THROW_NEW_ERROR_RETURN_FAILURE(
        isolate, NewTypeError(MessageTemplate::kTraceEventPhaseError));
  }
  char phase = static_cast<char>(DoubleToInt32(Object::Number(*phase_arg)));
  if (!IsString(*category)) {
    THROW_NEW_ERROR_RETURN_FAILURE(
        isolate, NewTypeError(MessageTemplate::kTraceEventCategoryError));
  }
  if (!IsString(*name_arg)) {
    THROW_NEW_ERROR_RETURN_FAILURE(
        isolate, NewTypeError(MessageTemplate::kTraceEventNameError));
  }

  uint32_t flags = TRACE_EVENT_FLAG_COPY;
  int32_t id = 0;
  if (!IsNullOrUndefined(*id_arg, isolate)) {
    if (!IsNumber(*id_arg)) {
      THROW_NEW_ERROR_RETURN_FAILURE(
          isolate, NewTypeError(MessageTemplate::kTraceEventIDError));
    }
    flags |= TRACE_EVENT_FLAG_HAS_ID;
    id = DoubleToInt32(Object::Number(*id_arg));
  }

  Handle<String> name_str = Handle<String>::cast(name_arg);
  if (name_str->length() == 0) {
    THROW_NEW_ERROR_RETURN_FAILURE(
        isolate, NewTypeError(MessageTemplate::kTraceEventNameLengthError));
  }
  MaybeUtf8 name(isolate, name_str);

  // We support passing one additional trace event argument with the
  // name "data". Any JSON serializable value may be passed.
  static const char* arg_name = "data";
  Handle<Object> arg_json;
  int32_t num_args = 0;
  if (!IsUndefined(*data_arg, isolate)) {
    // Serializes the data argument as a JSON string, which is then
    // copied into an object. This eliminates duplicated code but
    // could have perf costs. It is also subject to all the same
    // limitations as JSON.stringify() as it relates to circular
    // references and value limitations (e.g. BigInt is not supported).
    ASSIGN_RETURN_FAILURE_ON_EXCEPTION(
        isolate, arg_json,
        JsonStringify(isolate, data_arg, isolate->factory()->undefined_value(),
                      isolate->factory()->undefined_value()));
    num_args++;
  }

#if defined(V8_USE_PERFETTO)
  // TODO(skyostil): Use interned names to reduce trace size.
  auto trace_args = [&](perfetto::EventContext ctx) {
    if (num_args) {
      MaybeUtf8 arg_contents(isolate, Handle<String>::cast(arg_json));
      auto annotation = ctx.event()->add_debug_annotations();
      annotation->set_name(arg_name);
      annotation->set_legacy_json_value(*arg_contents);
    }
    if (flags & TRACE_EVENT_FLAG_HAS_ID) {
      auto legacy_event = ctx.event()->set_legacy_event();
      legacy_event->set_global_id(id);
    }
  };

  switch (phase) {
    case TRACE_EVENT_PHASE_BEGIN:
      TRACE_EVENT_BEGIN(dynamic_category, perfetto::DynamicString(*name),
                        trace_args);
      break;
    case TRACE_EVENT_PHASE_END:
      TRACE_EVENT_END(dynamic_category, trace_args);
      break;
    case TRACE_EVENT_PHASE_INSTANT:
      TRACE_EVENT_INSTANT(dynamic_category, perfetto::DynamicString(*name),
                          trace_args);
      break;
    default:
      THROW_NEW_ERROR_RETURN_FAILURE(
          isolate, NewTypeError(MessageTemplate::kTraceEventPhaseError));
  }

#else   // !defined(V8_USE_PERFETTO)
  uint8_t arg_type;
  uint64_t arg_value;
  if (num_args) {
    std::unique_ptr<JsonTraceValue> traced_value(
        new JsonTraceValue(isolate, Handle<String>::cast(arg_json)));
    tracing::SetTraceValue(std::move(traced_value), &arg_type, &arg_value);
  }

  TRACE_EVENT_API_ADD_TRACE_EVENT(
      phase, category_group_enabled, *name, tracing::kGlobalScope, id,
      tracing::kNoId, num_args, &arg_name, &arg_type, &arg_value, flags);
#endif  // !defined(V8_USE_PERFETTO)

  return ReadOnlyRoots(isolate).true_value();
}

}  // namespace internal
}  // namespace v8
