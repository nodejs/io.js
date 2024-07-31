// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_REGEXP_EXPERIMENTAL_EXPERIMENTAL_H_
#define V8_REGEXP_EXPERIMENTAL_EXPERIMENTAL_H_

#include "src/regexp/regexp-flags.h"
#include "src/regexp/regexp.h"

namespace v8 {
namespace internal {

class ExperimentalRegExp final : public AllStatic {
 public:
  // Initialization & Compilation
  // -------------------------------------------------------------------------
  // Check whether a parsed regexp pattern can be compiled and executed by the
  // EXPERIMENTAL engine.
  // TODO(mbid, v8:10765): This walks the RegExpTree, but it could also be
  // checked on the fly in the parser.  Not done currently because walking the
  // AST again is more flexible and less error prone (but less performant).
  static bool CanBeHandled(RegExpTree* tree, Handle<String> pattern,
                           RegExpFlags flags, int capture_count);
  static void Initialize(Isolate* isolate, Handle<JSRegExp> re,
                         Handle<String> pattern, RegExpFlags flags,
                         int capture_count);
  static bool IsCompiled(Handle<JSRegExp> re, Isolate* isolate);
  V8_WARN_UNUSED_RESULT
  static bool Compile(Isolate* isolate, Handle<JSRegExp> re);

  // Execution:
  static int32_t MatchForCallFromJs(Address subject, int32_t start_position,
                                    Address input_start, Address input_end,
                                    int* output_registers,
                                    int32_t output_register_count,
                                    RegExp::CallOrigin call_origin,
                                    Isolate* isolate, Address regexp);
  static MaybeHandle<Object> Exec(
      Isolate* isolate, Handle<JSRegExp> regexp, Handle<String> subject,
      int index, Handle<RegExpMatchInfo> last_match_info,
      RegExp::ExecQuirks exec_quirks = RegExp::ExecQuirks::kNone);
  static int32_t ExecRaw(Isolate* isolate, RegExp::CallOrigin call_origin,
                         Tagged<JSRegExp> regexp, Tagged<String> subject,
                         int32_t* output_registers,
                         int32_t output_register_count, int32_t subject_index);

  // Compile and execute a regexp with the experimental engine, regardless of
  // its type tag.  The regexp itself is not changed (apart from lastIndex).
  static MaybeHandle<Object> OneshotExec(
      Isolate* isolate, Handle<JSRegExp> regexp, Handle<String> subject,
      int index, Handle<RegExpMatchInfo> last_match_info,
      RegExp::ExecQuirks exec_quirks = RegExp::ExecQuirks::kNone);
  static int32_t OneshotExecRaw(Isolate* isolate, Handle<JSRegExp> regexp,
                                Handle<String> subject,
                                int32_t* output_registers,
                                int32_t output_register_count,
                                int32_t subject_index);

  static constexpr bool kSupportsUnicode = false;
};

}  // namespace internal
}  // namespace v8

#endif  // V8_REGEXP_EXPERIMENTAL_EXPERIMENTAL_H_
