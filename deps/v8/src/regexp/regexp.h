// Copyright 2012 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_REGEXP_REGEXP_H_
#define V8_REGEXP_REGEXP_H_

#include "src/common/assert-scope.h"
#include "src/handles/handles.h"
#include "src/regexp/regexp-error.h"
#include "src/regexp/regexp-flags.h"
#include "src/zone/zone-containers.h"

namespace v8 {
namespace internal {

class JSRegExp;
class RegExpCapture;
class RegExpMatchInfo;
class RegExpNode;
class RegExpTree;

enum class RegExpCompilationTarget : int { kBytecode, kNative };

// TODO(jgruber): Do not expose in regexp.h.
// TODO(jgruber): Consider splitting between ParseData and CompileData.
struct RegExpCompileData {
  // The parsed AST as produced by the RegExpParser.
  RegExpTree* tree = nullptr;

  // The compiled Node graph as produced by RegExpTree::ToNode methods.
  RegExpNode* node = nullptr;

  // Either the generated code as produced by the compiler or a trampoline
  // to the interpreter.
  Handle<Object> code;

  // True, iff the pattern is a 'simple' atom with zero captures. In other
  // words, the pattern consists of a string with no metacharacters and special
  // regexp features, and can be implemented as a standard string search.
  bool simple = true;

  // True, iff the pattern is anchored at the start of the string with '^'.
  bool contains_anchor = false;

  // Only set if the pattern contains named captures.
  // Note: the lifetime equals that of the parse/compile zone.
  ZoneVector<RegExpCapture*>* named_captures = nullptr;

  // The error message. Only used if an error occurred during parsing or
  // compilation.
  RegExpError error = RegExpError::kNone;

  // The position at which the error was detected. Only used if an
  // error occurred.
  int error_pos = 0;

  // The number of capture groups, without the global capture \0.
  int capture_count = 0;

  // The number of registers used by the generated code.
  int register_count = 0;

  // The compilation target (bytecode or native code).
  RegExpCompilationTarget compilation_target;
};

class RegExp final : public AllStatic {
 public:
  // Whether the irregexp engine generates interpreter bytecode.
  static bool CanGenerateBytecode();

  // Verify that the given flags combination is valid.
  V8_EXPORT_PRIVATE static bool VerifyFlags(RegExpFlags flags);

  // Verify the given pattern, i.e. check that parsing succeeds. If
  // verification fails, `regexp_error_out` is set.
  template <class CharT>
  static bool VerifySyntax(Zone* zone, uintptr_t stack_limit,
                           const CharT* input, int input_length,
                           RegExpFlags flags, RegExpError* regexp_error_out,
                           const DisallowGarbageCollection& no_gc);

  // Parses the RegExp pattern and prepares the JSRegExp object with
  // generic data and choice of implementation - as well as what
  // the implementation wants to store in the data field.
  // Returns false if compilation fails.
  V8_WARN_UNUSED_RESULT static MaybeHandle<Object> Compile(
      Isolate* isolate, Handle<JSRegExp> re, Handle<String> pattern,
      RegExpFlags flags, uint32_t backtrack_limit);

  // Ensures that a regexp is fully compiled and ready to be executed on a
  // subject string.  Returns true on success. Throw and return false on
  // failure.
  V8_WARN_UNUSED_RESULT static bool EnsureFullyCompiled(Isolate* isolate,
                                                        Handle<JSRegExp> re,
                                                        Handle<String> subject);

  enum CallOrigin : int {
    kFromRuntime = 0,
    kFromJs = 1,
  };

  enum class ExecQuirks {
    kNone,
    // Used to work around an issue in the RegExpPrototypeSplit fast path,
    // which diverges from the spec by not creating a sticky copy of the RegExp
    // instance and calling `exec` in a loop. If called in this context, we
    // must not update the last_match_info on a successful match at the subject
    // string end. See crbug.com/1075514 for more information.
    kTreatMatchAtEndAsFailure,
  };

  // See ECMA-262 section 15.10.6.2.
  // This function calls the garbage collector if necessary.
  V8_EXPORT_PRIVATE V8_WARN_UNUSED_RESULT static MaybeHandle<Object> Exec(
      Isolate* isolate, Handle<JSRegExp> regexp, Handle<String> subject,
      int index, Handle<RegExpMatchInfo> last_match_info,
      ExecQuirks exec_quirks = ExecQuirks::kNone);

  V8_EXPORT_PRIVATE V8_WARN_UNUSED_RESULT static MaybeHandle<Object>
  ExperimentalOneshotExec(Isolate* isolate, Handle<JSRegExp> regexp,
                          Handle<String> subject, int index,
                          Handle<RegExpMatchInfo> last_match_info,
                          ExecQuirks exec_quirks = ExecQuirks::kNone);

  // Integral return values used throughout regexp code layers.
  static constexpr int kInternalRegExpFailure = 0;
  static constexpr int kInternalRegExpSuccess = 1;
  static constexpr int kInternalRegExpException = -1;
  static constexpr int kInternalRegExpRetry = -2;
  static constexpr int kInternalRegExpFallbackToExperimental = -3;
  static constexpr int kInternalRegExpSmallestResult = -3;

  enum IrregexpResult : int32_t {
    RE_FAILURE = kInternalRegExpFailure,
    RE_SUCCESS = kInternalRegExpSuccess,
    RE_EXCEPTION = kInternalRegExpException,
    RE_RETRY = kInternalRegExpRetry,
    RE_FALLBACK_TO_EXPERIMENTAL = kInternalRegExpFallbackToExperimental,
  };

  // Set last match info.  If match is nullptr, then setting captures is
  // omitted.
  static Handle<RegExpMatchInfo> SetLastMatchInfo(
      Isolate* isolate, Handle<RegExpMatchInfo> last_match_info,
      Handle<String> subject, int capture_count, int32_t* match);

  V8_EXPORT_PRIVATE static bool CompileForTesting(
      Isolate* isolate, Zone* zone, RegExpCompileData* input, RegExpFlags flags,
      Handle<String> pattern, Handle<String> sample_subject, bool is_one_byte);

  V8_EXPORT_PRIVATE static void DotPrintForTesting(const char* label,
                                                   RegExpNode* node);

  static const int kRegExpTooLargeToOptimize = 20 * KB;

  V8_WARN_UNUSED_RESULT
  static MaybeHandle<Object> ThrowRegExpException(Isolate* isolate,
                                                  Handle<JSRegExp> re,
                                                  RegExpFlags flags,
                                                  Handle<String> pattern,
                                                  RegExpError error);
  static void ThrowRegExpException(Isolate* isolate, Handle<JSRegExp> re,
                                   RegExpError error_text);

  static bool IsUnmodifiedRegExp(Isolate* isolate, Handle<JSRegExp> regexp);

  static Handle<FixedArray> CreateCaptureNameMap(
      Isolate* isolate, ZoneVector<RegExpCapture*>* named_captures);
};

// Uses a special global mode of irregexp-generated code to perform a global
// search and return multiple results at once. As such, this is essentially an
// iterator over multiple results (retrieved batch-wise in advance).
class RegExpGlobalCache final {
 public:
  RegExpGlobalCache(Handle<JSRegExp> regexp, Handle<String> subject,
                    Isolate* isolate);

  ~RegExpGlobalCache();

  // Fetch the next entry in the cache for global regexp match results.
  // This does not set the last match info.  Upon failure, nullptr is
  // returned. The cause can be checked with Result().  The previous result is
  // still in available in memory when a failure happens.
  int32_t* FetchNext();

  int32_t* LastSuccessfulMatch();

  bool HasException() { return num_matches_ < 0; }

 private:
  int AdvanceZeroLength(int last_index);

  int num_matches_;
  int max_matches_;
  int current_match_index_;
  int registers_per_match_;
  // Pointer to the last set of captures.
  int32_t* register_array_;
  int register_array_size_;
  Handle<JSRegExp> regexp_;
  Handle<String> subject_;
  Isolate* isolate_;
};

// Caches results for specific regexp queries on the isolate. At the time of
// writing, this is used during global calls to RegExp.prototype.exec and
// @@split.
class RegExpResultsCache final : public AllStatic {
 public:
  enum ResultsCacheType { REGEXP_MULTIPLE_INDICES, STRING_SPLIT_SUBSTRINGS };

  // Attempt to retrieve a cached result.  On failure, 0 is returned as a Smi.
  // On success, the returned result is guaranteed to be a COW-array.
  static Tagged<Object> Lookup(Heap* heap, Tagged<String> key_string,
                               Tagged<Object> key_pattern,
                               Tagged<FixedArray>* last_match_out,
                               ResultsCacheType type);
  // Attempt to add value_array to the cache specified by type.  On success,
  // value_array is turned into a COW-array.
  static void Enter(Isolate* isolate, Handle<String> key_string,
                    Handle<Object> key_pattern, Handle<FixedArray> value_array,
                    Handle<FixedArray> last_match_cache, ResultsCacheType type);
  static void Clear(Tagged<FixedArray> cache);

  static constexpr int kRegExpResultsCacheSize = 0x100;

 private:
  static constexpr int kStringOffset = 0;
  static constexpr int kPatternOffset = 1;
  static constexpr int kArrayOffset = 2;
  static constexpr int kLastMatchOffset = 3;
  static constexpr int kArrayEntriesPerCacheEntry = 4;
};

}  // namespace internal
}  // namespace v8

#endif  // V8_REGEXP_REGEXP_H_
