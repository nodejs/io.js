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
    Isolate* isolate, DirectHandle<RegExpMatchInfo> match_info, int capture,
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

MaybeDirectHandle<Object> RegExpUtils::SetLastIndex(
    Isolate* isolate, DirectHandle<JSReceiver> recv, uint64_t value) {
  DirectHandle<Object> value_as_object =
      isolate->factory()->NewNumberFromInt64(value);
  if (HasInitialRegExpMap(isolate, *recv)) {
    Cast<JSRegExp>(*recv)->set_last_index(*value_as_object,
                                          UPDATE_WRITE_BARRIER);
    return recv;
  } else {
    return Object::SetProperty(
        isolate, recv, isolate->factory()->lastIndex_string(), value_as_object,
        StoreOrigin::kMaybeKeyed, Just(kThrowOnError));
  }
}

MaybeDirectHandle<Object> RegExpUtils::GetLastIndex(
    Isolate* isolate, DirectHandle<JSReceiver> recv) {
  if (HasInitialRegExpMap(isolate, *recv)) {
    return direct_handle(Cast<JSRegExp>(*recv)->last_index(), isolate);
  } else {
    return Object::GetProperty(isolate, recv,
                               isolate->factory()->lastIndex_string());
  }
}

// ES#sec-regexpexec Runtime Semantics: RegExpExec ( R, S )
// Also takes an optional exec method in case our caller
// has already fetched exec.
MaybeHandle<JSAny> RegExpUtils::RegExpExec(Isolate* isolate,
                                           Handle<JSReceiver> regexp,
                                           DirectHandle<String> string,
                                           DirectHandle<Object> exec) {
  if (IsUndefined(*exec, isolate)) {
    ASSIGN_RETURN_ON_EXCEPTION(
        isolate, exec,
        Object::GetProperty(isolate, regexp,
                            isolate->factory()->exec_string()));
  }

  if (IsCallable(*exec)) {
    constexpr int argc = 1;
    std::array<DirectHandle<Object>, argc> args = {string};

    Handle<JSAny> result;
    ASSIGN_RETURN_ON_EXCEPTION(
        isolate, result,
        Cast<JSAny>(
            Execution::Call(isolate, exec, regexp, base::VectorOf(args))));

    if (!IsJSReceiver(*result) && !IsNull(*result, isolate)) {
      THROW_NEW_ERROR(isolate,
                      NewTypeError(MessageTemplate::kInvalidRegExpExecResult));
    }
    return result;
  }

  if (!IsJSRegExp(*regexp)) {
    THROW_NEW_ERROR(isolate,
                    NewTypeError(MessageTemplate::kIncompatibleMethodReceiver,
                                 isolate->factory()->NewStringFromAsciiChecked(
                                     "RegExp.prototype.exec"),
                                 regexp));
  }

  {
    DirectHandle<JSFunction> regexp_exec = isolate->regexp_exec_function();

    constexpr int argc = 1;
    std::array<DirectHandle<Object>, argc> args = {string};

    return Cast<JSAny>(
        Execution::Call(isolate, regexp_exec, regexp, base::VectorOf(args)));
  }
}

bool RegExpUtils::IsUnmodifiedRegExp(Isolate* isolate,
                                     DirectHandle<Object> obj) {
#ifdef V8_ENABLE_FORCE_SLOW_PATH
  if (isolate->force_slow_path()) return false;
#endif

  if (!IsJSReceiver(*obj)) return false;

  Tagged<JSReceiver> recv = Cast<JSReceiver>(*obj);

  if (!HasInitialRegExpMap(isolate, recv)) return false;

  // Check the receiver's prototype's map.
  Tagged<Object> proto = recv->map()->prototype();
  if (!IsJSReceiver(proto)) return false;

  DirectHandle<Map> initial_proto_initial_map = isolate->regexp_prototype_map();
  Tagged<Map> proto_map = Cast<JSReceiver>(proto)->map();
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
  Tagged<Object> last_index = Cast<JSRegExp>(recv)->last_index();
  return IsSmi(last_index) && Smi::ToInt(last_index) >= 0;
}

uint64_t RegExpUtils::AdvanceStringIndex(Tagged<String> string, uint64_t index,
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

MaybeDirectHandle<Object> RegExpUtils::SetAdvancedStringIndex(
    Isolate* isolate, DirectHandle<JSReceiver> regexp,
    DirectHandle<String> string, bool unicode) {
  DirectHandle<Object> last_index_obj;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, last_index_obj,
      Object::GetProperty(isolate, regexp,
                          isolate->factory()->lastIndex_string()));

  ASSIGN_RETURN_ON_EXCEPTION(isolate, last_index_obj,
                             Object::ToLength(isolate, last_index_obj));
  const uint64_t last_index = PositiveNumberToUint64(*last_index_obj);
  const uint64_t new_last_index =
      AdvanceStringIndex(*string, last_index, unicode);

  return SetLastIndex(isolate, regexp, new_last_index);
}

}  // namespace internal
}  // namespace v8
