// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_REGEXP_REGEXP_UTILS_H_
#define V8_REGEXP_REGEXP_UTILS_H_

#include "src/common/globals.h"

namespace v8 {
namespace internal {

class JSReceiver;
class Object;
class RegExpMatchInfo;
class String;

// Helper methods for C++ regexp builtins.
class RegExpUtils : public AllStatic {
 public:
  // Last match info accessors.
  static Handle<String> GenericCaptureGetter(
      Isolate* isolate, DirectHandle<RegExpMatchInfo> match_info, int capture,
      bool* ok = nullptr);
  // Checks if the capture group referred to by index |capture| is part of the
  // match.
  static bool IsMatchedCapture(Tagged<RegExpMatchInfo> match_info, int capture);

  // Last index (RegExp.lastIndex) accessors.
  static V8_WARN_UNUSED_RESULT MaybeDirectHandle<Object> SetLastIndex(
      Isolate* isolate, DirectHandle<JSReceiver> regexp, uint64_t value);
  static V8_WARN_UNUSED_RESULT MaybeDirectHandle<Object> GetLastIndex(
      Isolate* isolate, DirectHandle<JSReceiver> recv);

  // ES#sec-regexpexec Runtime Semantics: RegExpExec ( R, S )
  static V8_WARN_UNUSED_RESULT MaybeHandle<JSAny> RegExpExec(
      Isolate* isolate, Handle<JSReceiver> regexp, DirectHandle<String> string,
      DirectHandle<Object> exec);

  // Checks whether the given object is an unmodified JSRegExp instance.
  // Neither the object's map, nor its prototype's map, nor any relevant
  // method on the prototype may be modified.
  //
  // Note: This check is limited may only be used in situations where the only
  // relevant property is 'exec'.
  static bool IsUnmodifiedRegExp(Isolate* isolate, DirectHandle<Object> obj);

  // ES#sec-advancestringindex
  // AdvanceStringIndex ( S, index, unicode )
  static uint64_t AdvanceStringIndex(Tagged<String> string, uint64_t index,
                                     bool unicode);
  static V8_WARN_UNUSED_RESULT MaybeDirectHandle<Object> SetAdvancedStringIndex(
      Isolate* isolate, DirectHandle<JSReceiver> regexp,
      DirectHandle<String> string, bool unicode);
};

}  // namespace internal
}  // namespace v8

#endif  // V8_REGEXP_REGEXP_UTILS_H_
