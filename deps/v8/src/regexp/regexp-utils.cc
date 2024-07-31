// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/regexp/regexp-utils.h"

#include "src/execution/isolate.h"
#include "src/execution/protectors-inl.h"
#include "src/heap/factory.h"
#include "src/objects/js-regexp-inl.h"
#include "src/objects/objects-inl.h"
#include "src/regexp/regexp.h"

namespace v8 {
namespace internal {

// static
Handle<String> RegExpUtils::GenericCaptureGetter(
    Isolate* isolate, Handle<RegExpMatchInfo> match_info, int capture,
    bool* ok) {
  const int capture_start_index = RegExpMatchInfo::capture_start_index(capture);
  if (capture_start_index >= match_info->number_of_capture_registers()) {
    if (ok != nullptr) *ok = false;
    return isolate->factory()->empty_string();
  }

  const int capture_end_index = RegExpMatchInfo::capture_end_index(capture);
  const int match_start = match_info->capture(capture_start_index);
  const int match_end = match_info->capture(capture_end_index);
  if (match_start == -1 || match_end == -1) {
    if (ok != nullptr) *ok = false;
    return isolate->factory()->empty_string();
  }

  if (ok != nullptr) *ok = true;
  Handle<String> last_subject(match_info->last_subject(), isolate);
  return isolate->factory()->NewSubString(last_subject, match_start, match_end);
}

// static
bool RegExpUtils::IsMatchedCapture(Tagged<RegExpMatchInfo> match_info,
                                   int capture) {
  // Sentinel used as failure indicator in other functions.
  if (capture == -1) return false;

  const int capture_start_index = RegExpMatchInfo::capture_start_index(capture);
  if (capture_start_index >= match_info->number_of_capture_registers()) {
    return false;
  }

  const int capture_end_index = RegExpMatchInfo::capture_end_index(capture);
  const int match_start = match_info->capture(capture_start_index);
  const int match_end = match_info->capture(capture_end_index);
  return match_start != -1 && match_end != -1;
}

namespace {

V8_INLINE bool HasInitialRegExpMap(Isolate* isolate, Tagged<JSReceiver> recv) {
  return recv->map() == isolate->regexp_function()->initial_map();
}

}  // namespace

MaybeHandle<Object> RegExpUtils::SetLastIndex(Isolate* isolate,
                                              Handle<JSReceiver> recv,
                                              uint64_t value) {
  Handle<Object> value_as_object =
      isolate->factory()->NewNumberFromInt64(value);
  if (HasInitialRegExpMap(isolate, *recv)) {
    JSRegExp::cast(*recv)->set_last_index(*value_as_object,
                                          UPDATE_WRITE_BARRIER);
    return recv;
  } else {
    return Object::SetProperty(
        isolate, recv, isolate->factory()->lastIndex_string(), value_as_object,
        StoreOrigin::kMaybeKeyed, Just(kThrowOnError));
  }
}

MaybeHandle<Object> RegExpUtils::GetLastIndex(Isolate* isolate,
                                              Handle<JSReceiver> recv) {
  if (HasInitialRegExpMap(isolate, *recv)) {
    return handle(JSRegExp::cast(*recv)->last_index(), isolate);
  } else {
    return Object::GetProperty(isolate, recv,
                               isolate->factory()->lastIndex_string());
  }
}

// ES#sec-regexpexec Runtime Semantics: RegExpExec ( R, S )
// Also takes an optional exec method in case our caller
// has already fetched exec.
MaybeHandle<Object> RegExpUtils::RegExpExec(Isolate* isolate,
                                            Handle<JSReceiver> regexp,
                                            Handle<String> string,
                                            Handle<Object> exec) {
  if (IsUndefined(*exec, isolate)) {
    ASSIGN_RETURN_ON_EXCEPTION(
        isolate, exec,
        Object::GetProperty(isolate, regexp, isolate->factory()->exec_string()),
        Object);
  }

  if (IsCallable(*exec)) {
    const int argc = 1;
    base::ScopedVector<Handle<Object>> argv(argc);
    argv[0] = string;

    Handle<Object> result;
    ASSIGN_RETURN_ON_EXCEPTION(
        isolate, result,
        Execution::Call(isolate, exec, regexp, argc, argv.begin()), Object);

    if (!IsJSReceiver(*result) && !IsNull(*result, isolate)) {
      THROW_NEW_ERROR(isolate,
                      NewTypeError(MessageTemplate::kInvalidRegExpExecResult),
                      Object);
    }
    return result;
  }

  if (!IsJSRegExp(*regexp)) {
    THROW_NEW_ERROR(isolate,
                    NewTypeError(MessageTemplate::kIncompatibleMethodReceiver,
                                 isolate->factory()->NewStringFromAsciiChecked(
                                     "RegExp.prototype.exec"),
                                 regexp),
                    Object);
  }

  {
    Handle<JSFunction> regexp_exec = isolate->regexp_exec_function();

    const int argc = 1;
    base::ScopedVector<Handle<Object>> argv(argc);
    argv[0] = string;

    return Execution::Call(isolate, regexp_exec, regexp, argc, argv.begin());
  }
}

bool RegExpUtils::IsUnmodifiedRegExp(Isolate* isolate, Handle<Object> obj) {
#ifdef V8_ENABLE_FORCE_SLOW_PATH
  if (isolate->force_slow_path()) return false;
#endif

  if (!IsJSReceiver(*obj)) return false;

  Tagged<JSReceiver> recv = JSReceiver::cast(*obj);

  if (!HasInitialRegExpMap(isolate, recv)) return false;

  // Check the receiver's prototype's map.
  Tagged<Object> proto = recv->map()->prototype();
  if (!IsJSReceiver(proto)) return false;

  Handle<Map> initial_proto_initial_map = isolate->regexp_prototype_map();
  Tagged<Map> proto_map = JSReceiver::cast(proto)->map();
  if (proto_map != *initial_proto_initial_map) {
    return false;
  }

  // Check that the "exec" method is unmodified.
  // Check that the index refers to "exec" method (this has to be consistent
  // with the init order in the bootstrapper).
  InternalIndex kExecIndex(JSRegExp::kExecFunctionDescriptorIndex);
  DCHECK_EQ(*(isolate->factory()->exec_string()),
            proto_map->instance_descriptors(isolate)->GetKey(kExecIndex));
  if (proto_map->instance_descriptors(isolate)
          ->GetDetails(kExecIndex)
          .constness() != PropertyConstness::kConst) {
    return false;
  }

  // Note: Unlike the more involved check in CSA (see BranchIfFastRegExp), this
  // does not go on to check the actual value of the exec property. This would
  // not be valid since this method is called from places that access the flags
  // property. Similar spots in CSA would use BranchIfFastRegExp_Strict in this
  // case.

  if (!Protectors::IsRegExpSpeciesLookupChainIntact(isolate)) return false;

  // The smi check is required to omit ToLength(lastIndex) calls with possible
  // user-code execution on the fast path.
  Tagged<Object> last_index = JSRegExp::cast(recv)->last_index();
  return IsSmi(last_index) && Smi::ToInt(last_index) >= 0;
}

uint64_t RegExpUtils::AdvanceStringIndex(Handle<String> string, uint64_t index,
                                         bool unicode) {
  DCHECK_LE(static_cast<double>(index), kMaxSafeInteger);
  const uint64_t string_length = static_cast<uint64_t>(string->length());
  if (unicode && index < string_length) {
    const uint16_t first = string->Get(static_cast<uint32_t>(index));
    if (first >= 0xD800 && first <= 0xDBFF && index + 1 < string_length) {
      DCHECK_LT(index, std::numeric_limits<uint64_t>::max());
      const uint16_t second = string->Get(static_cast<uint32_t>(index + 1));
      if (second >= 0xDC00 && second <= 0xDFFF) {
        return index + 2;
      }
    }
  }

  return index + 1;
}

MaybeHandle<Object> RegExpUtils::SetAdvancedStringIndex(
    Isolate* isolate, Handle<JSReceiver> regexp, Handle<String> string,
    bool unicode) {
  Handle<Object> last_index_obj;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, last_index_obj,
      Object::GetProperty(isolate, regexp,
                          isolate->factory()->lastIndex_string()),
      Object);

  ASSIGN_RETURN_ON_EXCEPTION(isolate, last_index_obj,
                             Object::ToLength(isolate, last_index_obj), Object);
  const uint64_t last_index = PositiveNumberToUint64(*last_index_obj);
  const uint64_t new_last_index =
      AdvanceStringIndex(string, last_index, unicode);

  return SetLastIndex(isolate, regexp, new_last_index);
}

}  // namespace internal
}  // namespace v8
