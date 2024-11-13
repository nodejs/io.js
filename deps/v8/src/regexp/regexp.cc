// Copyright 2012 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/regexp/regexp.h"

#include "src/base/strings.h"
#include "src/codegen/compilation-cache.h"
#include "src/diagnostics/code-tracer.h"
#include "src/execution/interrupts-scope.h"
#include "src/heap/heap-inl.h"
#include "src/objects/js-regexp-inl.h"
#include "src/regexp/experimental/experimental.h"
#include "src/regexp/regexp-bytecode-generator.h"
#include "src/regexp/regexp-bytecodes.h"
#include "src/regexp/regexp-compiler.h"
#include "src/regexp/regexp-dotprinter.h"
#include "src/regexp/regexp-interpreter.h"
#include "src/regexp/regexp-macro-assembler-arch.h"
#include "src/regexp/regexp-macro-assembler-tracer.h"
#include "src/regexp/regexp-parser.h"
#include "src/regexp/regexp-utils.h"
#include "src/strings/string-search.h"
#include "src/utils/ostreams.h"

namespace v8 {
namespace internal {

using namespace regexp_compiler_constants;  // NOLINT(build/namespaces)

class RegExpImpl final : public AllStatic {
 public:
  // Returns a string representation of a regular expression.
  // Implements RegExp.prototype.toString, see ECMA-262 section 15.10.6.4.
  // This function calls the garbage collector if necessary.
  static Handle<String> ToString(Handle<Object> value);

  // Prepares a JSRegExp object with Irregexp-specific data.
  static void IrregexpInitialize(Isolate* isolate, DirectHandle<JSRegExp> re,
                                 DirectHandle<String> pattern,
                                 RegExpFlags flags, int capture_count,
                                 uint32_t backtrack_limit);

  // Prepare a RegExp for being executed one or more times (using
  // IrregexpExecOnce) on the subject.
  // This ensures that the regexp is compiled for the subject, and that
  // the subject is flat.
  // Returns the number of integer spaces required by IrregexpExecOnce
  // as its "registers" argument.  If the regexp cannot be compiled,
  // an exception is thrown as indicated by a negative return value.
  static int IrregexpPrepare(Isolate* isolate,
                             DirectHandle<IrRegExpData> regexp_data,
                             Handle<String> subject);

  static void AtomCompile(Isolate* isolate, DirectHandle<JSRegExp> re,
                          DirectHandle<String> pattern, RegExpFlags flags,
                          DirectHandle<String> match_pattern);

  static int AtomExecRaw(Isolate* isolate,
                         DirectHandle<AtomRegExpData> regexp_data,
                         Handle<String> subject, int index, int32_t* output,
                         int output_size);

  static Handle<Object> AtomExec(Isolate* isolate,
                                 DirectHandle<AtomRegExpData> regexp_data,
                                 Handle<String> subject, int index,
                                 Handle<RegExpMatchInfo> last_match_info);

  // Execute a regular expression on the subject, starting from index.
  // If matching succeeds, return the number of matches.  This can be larger
  // than one in the case of global regular expressions.
  // The captures and subcaptures are stored into the registers vector.
  // If matching fails, returns RE_FAILURE.
  // If execution fails, sets a exception and returns RE_EXCEPTION.
  static int IrregexpExecRaw(Isolate* isolate,
                             DirectHandle<IrRegExpData> regexp_data,
                             Handle<String> subject, int index, int32_t* output,
                             int output_size);

  // Execute an Irregexp bytecode pattern.
  // On a successful match, the result is a JSArray containing
  // captured positions.  On a failure, the result is the null value.
  // Returns an empty handle in case of an exception.
  V8_WARN_UNUSED_RESULT static MaybeHandle<Object> IrregexpExec(
      Isolate* isolate, DirectHandle<IrRegExpData> regexp_data,
      Handle<String> subject, int index,
      Handle<RegExpMatchInfo> last_match_info,
      RegExp::ExecQuirks exec_quirks = RegExp::ExecQuirks::kNone);

  static bool CompileIrregexp(Isolate* isolate,
                              DirectHandle<IrRegExpData> re_data,
                              Handle<String> sample_subject, bool is_one_byte);
  static inline bool EnsureCompiledIrregexp(Isolate* isolate,
                                            DirectHandle<IrRegExpData> re_data,
                                            Handle<String> sample_subject,
                                            bool is_one_byte);

  // Returns true on success, false on failure.
  static bool Compile(Isolate* isolate, Zone* zone, RegExpCompileData* input,
                      RegExpFlags flags, Handle<String> pattern,
                      Handle<String> sample_subject, bool is_one_byte,
                      uint32_t& backtrack_limit);
};

// static
bool RegExp::CanGenerateBytecode() {
  return v8_flags.regexp_interpret_all || v8_flags.regexp_tier_up;
}

// static
bool RegExp::VerifyFlags(RegExpFlags flags) {
  if (IsUnicode(flags) && IsUnicodeSets(flags)) return false;
  return true;
}

// static
template <class CharT>
bool RegExp::VerifySyntax(Zone* zone, uintptr_t stack_limit, const CharT* input,
                          int input_length, RegExpFlags flags,
                          RegExpError* regexp_error_out,
                          const DisallowGarbageCollection& no_gc) {
  RegExpCompileData data;
  bool pattern_is_valid = RegExpParser::VerifyRegExpSyntax(
      zone, stack_limit, input, input_length, flags, &data, no_gc);
  *regexp_error_out = data.error;
  return pattern_is_valid;
}

template bool RegExp::VerifySyntax<uint8_t>(Zone*, uintptr_t, const uint8_t*,
                                            int, RegExpFlags,
                                            RegExpError* regexp_error_out,
                                            const DisallowGarbageCollection&);
template bool RegExp::VerifySyntax<base::uc16>(
    Zone*, uintptr_t, const base::uc16*, int, RegExpFlags,
    RegExpError* regexp_error_out, const DisallowGarbageCollection&);

MaybeHandle<Object> RegExp::ThrowRegExpException(Isolate* isolate,
                                                 RegExpFlags flags,
                                                 Handle<String> pattern,
                                                 RegExpError error) {
  base::Vector<const char> error_data =
      base::CStrVector(RegExpErrorString(error));
  Handle<String> error_text =
      isolate->factory()
          ->NewStringFromOneByte(base::Vector<const uint8_t>::cast(error_data))
          .ToHandleChecked();
  Handle<String> flag_string =
      JSRegExp::StringFromFlags(isolate, JSRegExp::AsJSRegExpFlags(flags));
  THROW_NEW_ERROR(isolate, NewSyntaxError(MessageTemplate::kMalformedRegExp,
                                          pattern, flag_string, error_text));
}

void RegExp::ThrowRegExpException(Isolate* isolate,
                                  DirectHandle<RegExpData> re_data,
                                  RegExpError error_text) {
  USE(ThrowRegExpException(isolate, JSRegExp::AsRegExpFlags(re_data->flags()),
                           Handle<String>(re_data->source(), isolate),
                           error_text));
}

bool RegExp::IsUnmodifiedRegExp(Isolate* isolate,
                                DirectHandle<JSRegExp> regexp) {
  return RegExpUtils::IsUnmodifiedRegExp(isolate, regexp);
}

namespace {

// Identifies the sort of regexps where the regexp engine is faster
// than the code used for atom matches.
bool HasFewDifferentCharacters(DirectHandle<String> pattern) {
  int length = std::min(kMaxLookaheadForBoyerMoore, pattern->length());
  if (length <= kPatternTooShortForBoyerMoore) return false;
  const int kMod = 128;
  bool character_found[kMod];
  int different = 0;
  memset(&character_found[0], 0, sizeof(character_found));
  for (int i = 0; i < length; i++) {
    int ch = (pattern->Get(i) & (kMod - 1));
    if (!character_found[ch]) {
      character_found[ch] = true;
      different++;
      // We declare a regexp low-alphabet if it has at least 3 times as many
      // characters as it has different characters.
      if (different * 3 > length) return false;
    }
  }
  return true;
}

}  // namespace

// Generic RegExp methods. Dispatches to implementation specific methods.

// static
MaybeHandle<Object> RegExp::Compile(Isolate* isolate, Handle<JSRegExp> re,
                                    Handle<String> pattern, RegExpFlags flags,
                                    uint32_t backtrack_limit) {
  DCHECK(pattern->IsFlat());

  // Caching is based only on the pattern and flags, but code also differs when
  // a backtrack limit is set. A present backtrack limit is very much *not* the
  // common case, so just skip the cache for these.
  const bool is_compilation_cache_enabled =
      (backtrack_limit == JSRegExp::kNoBacktrackLimit);

  Zone zone(isolate->allocator(), ZONE_NAME);
  CompilationCache* compilation_cache = nullptr;
  if (is_compilation_cache_enabled) {
    compilation_cache = isolate->compilation_cache();
    MaybeHandle<RegExpData> maybe_cached = compilation_cache->LookupRegExp(
        pattern, JSRegExp::AsJSRegExpFlags(flags));
    Handle<RegExpData> cached;
    if (maybe_cached.ToHandle(&cached)) {
      re->set_data(*cached);
      return re;
    }
  }

  PostponeInterruptsScope postpone(isolate);
  RegExpCompileData parse_result;
  DCHECK(!isolate->has_exception());
  if (!RegExpParser::ParseRegExpFromHeapString(isolate, &zone, pattern, flags,
                                               &parse_result)) {
    // Throw an exception if we fail to parse the pattern.
    return RegExp::ThrowRegExpException(isolate, flags, pattern,
                                        parse_result.error);
  }

  bool has_been_compiled = false;

  if (v8_flags.default_to_experimental_regexp_engine &&
      ExperimentalRegExp::CanBeHandled(parse_result.tree, pattern, flags,
                                       parse_result.capture_count)) {
    DCHECK(v8_flags.enable_experimental_regexp_engine);
    ExperimentalRegExp::Initialize(isolate, re, pattern, flags,
                                   parse_result.capture_count);
    has_been_compiled = true;
  } else if (flags & JSRegExp::kLinear) {
    DCHECK(v8_flags.enable_experimental_regexp_engine);
    if (!ExperimentalRegExp::CanBeHandled(parse_result.tree, pattern, flags,
                                          parse_result.capture_count)) {
      // TODO(mbid): The error could provide a reason for why the regexp can't
      // be executed in linear time (e.g. due to back references).
      return RegExp::ThrowRegExpException(isolate, flags, pattern,
                                          RegExpError::kNotLinear);
    }
    ExperimentalRegExp::Initialize(isolate, re, pattern, flags,
                                   parse_result.capture_count);
    has_been_compiled = true;
  } else if (parse_result.simple && !IsIgnoreCase(flags) && !IsSticky(flags) &&
             !HasFewDifferentCharacters(pattern)) {
    // Parse-tree is a single atom that is equal to the pattern.
    RegExpImpl::AtomCompile(isolate, re, pattern, flags, pattern);
    has_been_compiled = true;
  } else if (parse_result.tree->IsAtom() && !IsSticky(flags) &&
             parse_result.capture_count == 0) {
    RegExpAtom* atom = parse_result.tree->AsAtom();
    // The pattern source might (?) contain escape sequences, but they're
    // resolved in atom_string.
    base::Vector<const base::uc16> atom_pattern = atom->data();
    Handle<String> atom_string;
    ASSIGN_RETURN_ON_EXCEPTION(
        isolate, atom_string,
        isolate->factory()->NewStringFromTwoByte(atom_pattern));
    if (!IsIgnoreCase(flags) && !HasFewDifferentCharacters(atom_string)) {
      RegExpImpl::AtomCompile(isolate, re, pattern, flags, atom_string);
      has_been_compiled = true;
    }
  }
  if (!has_been_compiled) {
    RegExpImpl::IrregexpInitialize(isolate, re, pattern, flags,
                                   parse_result.capture_count, backtrack_limit);
  }
  // Compilation succeeded so the data is set on the regexp
  // and we can store it in the cache.
  DirectHandle<RegExpData> data(re->data(isolate), isolate);
  if (is_compilation_cache_enabled) {
    compilation_cache->PutRegExp(pattern, JSRegExp::AsJSRegExpFlags(flags),
                                 data);
  }

  return re;
}

// static
bool RegExp::EnsureFullyCompiled(Isolate* isolate,
                                 DirectHandle<RegExpData> re_data,
                                 Handle<String> subject) {
  switch (re_data->type_tag()) {
    case RegExpData::Type::ATOM:
      return true;
    case RegExpData::Type::IRREGEXP:
      if (RegExpImpl::IrregexpPrepare(isolate, Cast<IrRegExpData>(re_data),
                                      subject) == -1) {
        DCHECK(isolate->has_exception());
        return false;
      }
      return true;
    case RegExpData::Type::EXPERIMENTAL:
      if (!ExperimentalRegExp::IsCompiled(Cast<IrRegExpData>(re_data),
                                          isolate) &&
          !ExperimentalRegExp::Compile(isolate, Cast<IrRegExpData>(re_data))) {
        DCHECK(isolate->has_exception());
        return false;
      }
      return true;
  }
  UNREACHABLE();
}

// static
MaybeHandle<Object> RegExp::ExperimentalOneshotExec(
    Isolate* isolate, DirectHandle<JSRegExp> regexp,
    DirectHandle<String> subject, int index,
    Handle<RegExpMatchInfo> last_match_info, RegExp::ExecQuirks exec_quirks) {
  DirectHandle<RegExpData> data = direct_handle(regexp->data(isolate), isolate);
  SBXCHECK(Is<IrRegExpData>(*data));
  return ExperimentalRegExp::OneshotExec(isolate, Cast<IrRegExpData>(data),
                                         subject, index, last_match_info,
                                         exec_quirks);
}

// static
MaybeHandle<Object> RegExp::Exec(Isolate* isolate,
                                 DirectHandle<JSRegExp> regexp,
                                 Handle<String> subject, int index,
                                 Handle<RegExpMatchInfo> last_match_info,
                                 ExecQuirks exec_quirks) {
  DirectHandle<RegExpData> data = direct_handle(regexp->data(isolate), isolate);
  switch (data->type_tag()) {
    case RegExpData::Type::ATOM:
      return RegExpImpl::AtomExec(isolate, Cast<AtomRegExpData>(data), subject,
                                  index, last_match_info);
    case RegExpData::Type::IRREGEXP:
      return RegExpImpl::IrregexpExec(isolate, Cast<IrRegExpData>(data),
                                      subject, index, last_match_info,
                                      exec_quirks);
    case RegExpData::Type::EXPERIMENTAL:
      return ExperimentalRegExp::Exec(isolate, Cast<IrRegExpData>(data),
                                      subject, index, last_match_info,
                                      exec_quirks);
  }
  // This UNREACHABLE() is necessary because we don't return a value here,
  // which causes the compiler to emit potentially unsafe code for the switch
  // above. See the commit message and b/326086002 for more details.
  UNREACHABLE();
}

// RegExp Atom implementation: Simple string search using indexOf.

void RegExpImpl::AtomCompile(Isolate* isolate, DirectHandle<JSRegExp> re,
                             DirectHandle<String> pattern, RegExpFlags flags,
                             DirectHandle<String> match_pattern) {
  isolate->factory()->SetRegExpAtomData(
      re, pattern, JSRegExp::AsJSRegExpFlags(flags), match_pattern);
}

namespace {

void SetAtomLastCapture(Isolate* isolate,
                        DirectHandle<RegExpMatchInfo> last_match_info,
                        Tagged<String> subject, int from, int to) {
  SealHandleScope shs(isolate);
  last_match_info->set_number_of_capture_registers(2);
  last_match_info->set_last_subject(subject);
  last_match_info->set_last_input(subject);
  last_match_info->set_capture(0, from);
  last_match_info->set_capture(1, to);
}

}  // namespace

int RegExpImpl::AtomExecRaw(Isolate* isolate,
                            DirectHandle<AtomRegExpData> regexp_data,
                            Handle<String> subject, int index, int32_t* output,
                            int output_size) {
  DCHECK_LE(0, index);
  DCHECK_LE(index, subject->length());

  subject = String::Flatten(isolate, subject);
  DisallowGarbageCollection no_gc;  // ensure vectors stay valid

  Tagged<String> needle = regexp_data->pattern(isolate);
  int needle_len = needle->length();
  DCHECK(needle->IsFlat());
  DCHECK_LT(0, needle_len);

  if (index + needle_len > subject->length()) {
    return RegExp::RE_FAILURE;
  }

  for (int i = 0; i < output_size; i += 2) {
    String::FlatContent needle_content = needle->GetFlatContent(no_gc);
    String::FlatContent subject_content = subject->GetFlatContent(no_gc);
    DCHECK(needle_content.IsFlat());
    DCHECK(subject_content.IsFlat());
    // dispatch on type of strings
    index =
        (needle_content.IsOneByte()
             ? (subject_content.IsOneByte()
                    ? SearchString(isolate, subject_content.ToOneByteVector(),
                                   needle_content.ToOneByteVector(), index)
                    : SearchString(isolate, subject_content.ToUC16Vector(),
                                   needle_content.ToOneByteVector(), index))
             : (subject_content.IsOneByte()
                    ? SearchString(isolate, subject_content.ToOneByteVector(),
                                   needle_content.ToUC16Vector(), index)
                    : SearchString(isolate, subject_content.ToUC16Vector(),
                                   needle_content.ToUC16Vector(), index)));
    if (index == -1) {
      return i / 2;  // Return number of matches.
    } else {
      output[i] = index;
      output[i + 1] = index + needle_len;
      index += needle_len;
    }
  }
  return output_size / 2;
}

Handle<Object> RegExpImpl::AtomExec(Isolate* isolate,
                                    DirectHandle<AtomRegExpData> re_data,
                                    Handle<String> subject, int index,
                                    Handle<RegExpMatchInfo> last_match_info) {
  static const int kNumRegisters = 2;
  static_assert(kNumRegisters <= Isolate::kJSRegexpStaticOffsetsVectorSize);
  int32_t* output_registers = isolate->jsregexp_static_offsets_vector();

  int res = AtomExecRaw(isolate, re_data, subject, index, output_registers,
                        kNumRegisters);

  if (res == RegExp::RE_FAILURE) return isolate->factory()->null_value();

  DCHECK_EQ(res, RegExp::RE_SUCCESS);
  SealHandleScope shs(isolate);
  SetAtomLastCapture(isolate, last_match_info, *subject, output_registers[0],
                     output_registers[1]);
  return last_match_info;
}

// Irregexp implementation.

// Ensures that the regexp object contains a compiled version of the
// source for either one-byte or two-byte subject strings.
// If the compiled version doesn't already exist, it is compiled
// from the source pattern.
// If compilation fails, an exception is thrown and this function
// returns false.
bool RegExpImpl::EnsureCompiledIrregexp(Isolate* isolate,
                                        DirectHandle<IrRegExpData> re_data,
                                        Handle<String> sample_subject,
                                        bool is_one_byte) {
  bool has_bytecode = re_data->has_bytecode(is_one_byte);
  bool needs_initial_compilation = !re_data->has_code(is_one_byte);
  // Recompile is needed when we're dealing with the first execution of the
  // regexp after the decision to tier up has been made. If the tiering up
  // strategy is not in use, this value is always false.
  bool needs_tier_up_compilation = re_data->MarkedForTierUp() && has_bytecode;

  if (v8_flags.trace_regexp_tier_up && needs_tier_up_compilation) {
    PrintF("JSRegExp object (data: %p) needs tier-up compilation\n",
           reinterpret_cast<void*>(re_data->ptr()));
  }

  if (!needs_initial_compilation && !needs_tier_up_compilation) {
    DCHECK(re_data->has_code(is_one_byte));
    DCHECK_IMPLIES(v8_flags.regexp_interpret_all, has_bytecode);
    return true;
  }

  DCHECK_IMPLIES(needs_tier_up_compilation, has_bytecode);

  return CompileIrregexp(isolate, re_data, sample_subject, is_one_byte);
}

namespace {

#ifdef DEBUG
bool RegExpCodeIsValidForPreCompilation(IsolateForSandbox isolate,
                                        DirectHandle<IrRegExpData> re_data,
                                        bool is_one_byte) {
  bool has_code = re_data->has_code(is_one_byte);
  bool has_bytecode = re_data->has_bytecode(is_one_byte);
  if (re_data->ShouldProduceBytecode()) {
    DCHECK(!has_code);
    DCHECK(!has_bytecode);
  } else {
    DCHECK_IMPLIES(has_code, has_bytecode);
  }

  return true;
}
#endif

struct RegExpCaptureIndexLess {
  bool operator()(const RegExpCapture* lhs, const RegExpCapture* rhs) const {
    DCHECK_NOT_NULL(lhs);
    DCHECK_NOT_NULL(rhs);
    return lhs->index() < rhs->index();
  }
};

}  // namespace

// static
Handle<FixedArray> RegExp::CreateCaptureNameMap(
    Isolate* isolate, ZoneVector<RegExpCapture*>* named_captures) {
  if (named_captures == nullptr) return Handle<FixedArray>();

  DCHECK(!named_captures->empty());

  // Named captures are sorted by name (because the set is used to ensure
  // name uniqueness). But the capture name map must to be sorted by index.

  std::sort(named_captures->begin(), named_captures->end(),
            RegExpCaptureIndexLess{});

  int len = static_cast<int>(named_captures->size()) * 2;
  Handle<FixedArray> array = isolate->factory()->NewFixedArray(len);

  int i = 0;
  for (const RegExpCapture* capture : *named_captures) {
    base::Vector<const base::uc16> capture_name(capture->name()->data(),
                                                capture->name()->size());
    // CSA code in ConstructNewResultFromMatchInfo requires these strings to be
    // internalized so they can be used as property names in the 'exec' results.
    DirectHandle<String> name =
        isolate->factory()->InternalizeString(capture_name);
    array->set(i * 2, *name);
    array->set(i * 2 + 1, Smi::FromInt(capture->index()));

    i++;
  }
  DCHECK_EQ(i * 2, len);

  return array;
}

bool RegExpImpl::CompileIrregexp(Isolate* isolate,
                                 DirectHandle<IrRegExpData> re_data,
                                 Handle<String> sample_subject,
                                 bool is_one_byte) {
  // Compile the RegExp.
  Zone zone(isolate->allocator(), ZONE_NAME);
  PostponeInterruptsScope postpone(isolate);

  DCHECK(RegExpCodeIsValidForPreCompilation(isolate, re_data, is_one_byte));

  RegExpFlags flags = JSRegExp::AsRegExpFlags(re_data->flags());

  Handle<String> pattern(re_data->source(), isolate);
  pattern = String::Flatten(isolate, pattern);
  RegExpCompileData compile_data;
  if (!RegExpParser::ParseRegExpFromHeapString(isolate, &zone, pattern, flags,
                                               &compile_data)) {
    // Throw an exception if we fail to parse the pattern.
    // THIS SHOULD NOT HAPPEN. We already pre-parsed it successfully once.
    USE(RegExp::ThrowRegExpException(isolate, flags, pattern,
                                     compile_data.error));
    return false;
  }
  // The compilation target is a kBytecode if we're interpreting all regexp
  // objects, or if we're using the tier-up strategy but the tier-up hasn't
  // happened yet. The compilation target is a kNative if we're using the
  // tier-up strategy and we need to recompile to tier-up, or if we're producing
  // native code for all regexp objects.
  compile_data.compilation_target = re_data->ShouldProduceBytecode()
                                        ? RegExpCompilationTarget::kBytecode
                                        : RegExpCompilationTarget::kNative;
  uint32_t backtrack_limit = re_data->backtrack_limit();
  const bool compilation_succeeded =
      Compile(isolate, &zone, &compile_data, flags, pattern, sample_subject,
              is_one_byte, backtrack_limit);
  if (!compilation_succeeded) {
    DCHECK(compile_data.error != RegExpError::kNone);
    RegExp::ThrowRegExpException(isolate, re_data, compile_data.error);
    return false;
  }

  if (compile_data.compilation_target == RegExpCompilationTarget::kNative) {
    re_data->set_code(is_one_byte, Cast<Code>(*compile_data.code));

    // Reset bytecode to uninitialized. In case we use tier-up we know that
    // tier-up has happened this way.
    re_data->clear_bytecode(is_one_byte);
  } else {
    DCHECK_EQ(compile_data.compilation_target,
              RegExpCompilationTarget::kBytecode);
    // Store code generated by compiler in bytecode and trampoline to
    // interpreter in code.
    re_data->set_bytecode(is_one_byte,
                          Cast<TrustedByteArray>(*compile_data.code));
    DirectHandle<Code> trampoline =
        BUILTIN_CODE(isolate, RegExpInterpreterTrampoline);
    re_data->set_code(is_one_byte, *trampoline);
  }
  Handle<FixedArray> capture_name_map =
      RegExp::CreateCaptureNameMap(isolate, compile_data.named_captures);
  re_data->set_capture_name_map(capture_name_map);
  int register_max = re_data->max_register_count();
  if (compile_data.register_count > register_max) {
    re_data->set_max_register_count(compile_data.register_count);
  }
  re_data->set_backtrack_limit(backtrack_limit);

  if (v8_flags.trace_regexp_tier_up) {
    PrintF("JSRegExp data object %p %s size: %d\n",
           reinterpret_cast<void*>(re_data->ptr()),
           re_data->ShouldProduceBytecode() ? "bytecode" : "native code",
           re_data->ShouldProduceBytecode()
               ? re_data->bytecode(is_one_byte)->AllocatedSize()
               : re_data->code(isolate, is_one_byte)->Size());
  }

  return true;
}

void RegExpImpl::IrregexpInitialize(Isolate* isolate, DirectHandle<JSRegExp> re,
                                    DirectHandle<String> pattern,
                                    RegExpFlags flags, int capture_count,
                                    uint32_t backtrack_limit) {
  // Initialize compiled code entries to null.
  isolate->factory()->SetRegExpIrregexpData(re, pattern,
                                            JSRegExp::AsJSRegExpFlags(flags),
                                            capture_count, backtrack_limit);
}

// static
int RegExpImpl::IrregexpPrepare(Isolate* isolate,
                                DirectHandle<IrRegExpData> re_data,
                                Handle<String> subject) {
  DCHECK(subject->IsFlat());

  // Check representation of the underlying storage.
  bool is_one_byte = String::IsOneByteRepresentationUnderneath(*subject);
  if (!RegExpImpl::EnsureCompiledIrregexp(isolate, re_data, subject,
                                          is_one_byte)) {
    return -1;
  }

  // Only reserve room for output captures. Internal registers are allocated by
  // the engine.
  return JSRegExp::RegistersForCaptureCount(re_data->capture_count());
}

int RegExpImpl::IrregexpExecRaw(Isolate* isolate,
                                DirectHandle<IrRegExpData> regexp_data,
                                Handle<String> subject, int index,
                                int32_t* output, int output_size) {
  DCHECK_LE(0, index);
  DCHECK_LE(index, subject->length());
  DCHECK(subject->IsFlat());
  DCHECK_GE(output_size,
            JSRegExp::RegistersForCaptureCount(regexp_data->capture_count()));

  bool is_one_byte = String::IsOneByteRepresentationUnderneath(*subject);

  if (!regexp_data->ShouldProduceBytecode()) {
    do {
      EnsureCompiledIrregexp(isolate, regexp_data, subject, is_one_byte);
      // The stack is used to allocate registers for the compiled regexp code.
      // This means that in case of failure, the output registers array is left
      // untouched and contains the capture results from the previous successful
      // match.  We can use that to set the last match info lazily.
      int res = NativeRegExpMacroAssembler::Match(regexp_data, subject, output,
                                                  output_size, index, isolate);
      if (res != NativeRegExpMacroAssembler::RETRY) {
        DCHECK(res != NativeRegExpMacroAssembler::EXCEPTION ||
               isolate->has_exception());
        static_assert(static_cast<int>(NativeRegExpMacroAssembler::SUCCESS) ==
                      RegExp::RE_SUCCESS);
        static_assert(static_cast<int>(NativeRegExpMacroAssembler::FAILURE) ==
                      RegExp::RE_FAILURE);
        static_assert(static_cast<int>(NativeRegExpMacroAssembler::EXCEPTION) ==
                      RegExp::RE_EXCEPTION);
        return res;
      }
      // If result is RETRY, the string has changed representation, and we
      // must restart from scratch.
      // In this case, it means we must make sure we are prepared to handle
      // the, potentially, different subject (the string can switch between
      // being internal and external, and even between being Latin1 and
      // UC16, but the characters are always the same).
      is_one_byte = String::IsOneByteRepresentationUnderneath(*subject);
    } while (true);
    UNREACHABLE();
  } else {
    DCHECK(regexp_data->ShouldProduceBytecode());

    do {
      IrregexpInterpreter::Result result =
          IrregexpInterpreter::MatchForCallFromRuntime(
              isolate, regexp_data, subject, output, output_size, index);
      DCHECK_IMPLIES(result == IrregexpInterpreter::EXCEPTION,
                     isolate->has_exception());

      switch (result) {
        case IrregexpInterpreter::SUCCESS:
        case IrregexpInterpreter::EXCEPTION:
        case IrregexpInterpreter::FAILURE:
        case IrregexpInterpreter::FALLBACK_TO_EXPERIMENTAL:
          return result;
        case IrregexpInterpreter::RETRY:
          // The string has changed representation, and we must restart the
          // match.
          // We need to reset the tier up to start over with compilation.
          if (v8_flags.regexp_tier_up) regexp_data->ResetLastTierUpTick();
          is_one_byte = String::IsOneByteRepresentationUnderneath(*subject);
          EnsureCompiledIrregexp(isolate, regexp_data, subject, is_one_byte);
          break;
      }
    } while (true);
    UNREACHABLE();
  }
}

MaybeHandle<Object> RegExpImpl::IrregexpExec(
    Isolate* isolate, DirectHandle<IrRegExpData> regexp_data,
    Handle<String> subject, int previous_index,
    Handle<RegExpMatchInfo> last_match_info, RegExp::ExecQuirks exec_quirks) {
  subject = String::Flatten(isolate, subject);

#ifdef DEBUG
  if (v8_flags.trace_regexp_bytecodes && regexp_data->ShouldProduceBytecode()) {
    PrintF("\n\nRegexp match:   /%s/\n\n",
           regexp_data->source()->ToCString().get());
    PrintF("\n\nSubject string: '%s'\n\n", subject->ToCString().get());
  }
#endif

  // For very long subject strings, the regexp interpreter is currently much
  // slower than the jitted code execution. If the tier-up strategy is turned
  // on, we want to avoid this performance penalty so we eagerly tier-up if the
  // subject string length is equal or greater than the given heuristic value.
  if (v8_flags.regexp_tier_up &&
      subject->length() >= JSRegExp::kTierUpForSubjectLengthValue) {
    regexp_data->MarkTierUpForNextExec();
    if (v8_flags.trace_regexp_tier_up) {
      PrintF(
          "Forcing tier-up for very long strings in "
          "RegExpImpl::IrregexpExec\n");
    }
  }

  // Prepare space for the return values.
  int required_registers =
      RegExpImpl::IrregexpPrepare(isolate, regexp_data, subject);
  if (required_registers < 0) {
    // Compiling failed with an exception.
    DCHECK(isolate->has_exception());
    return MaybeHandle<Object>();
  }

  int32_t* output_registers = nullptr;
  if (required_registers > Isolate::kJSRegexpStaticOffsetsVectorSize) {
    output_registers = NewArray<int32_t>(required_registers);
  }
  std::unique_ptr<int32_t[]> auto_release(output_registers);
  if (output_registers == nullptr) {
    output_registers = isolate->jsregexp_static_offsets_vector();
  }

  int res =
      RegExpImpl::IrregexpExecRaw(isolate, regexp_data, subject, previous_index,
                                  output_registers, required_registers);

  if (res == RegExp::RE_SUCCESS) {
    if (exec_quirks == RegExp::ExecQuirks::kTreatMatchAtEndAsFailure) {
      if (output_registers[0] >= subject->length()) {
        return isolate->factory()->null_value();
      }
    }
    int capture_count = regexp_data->capture_count();
    return RegExp::SetLastMatchInfo(isolate, last_match_info, subject,
                                    capture_count, output_registers);
  } else if (res == RegExp::RE_FALLBACK_TO_EXPERIMENTAL) {
    return ExperimentalRegExp::OneshotExec(isolate, regexp_data, subject,
                                           previous_index, last_match_info);
  } else if (res == RegExp::RE_EXCEPTION) {
    DCHECK(isolate->has_exception());
    return MaybeHandle<Object>();
  } else {
    DCHECK(res == RegExp::RE_FAILURE);
    return isolate->factory()->null_value();
  }
}

// static
Handle<RegExpMatchInfo> RegExp::SetLastMatchInfo(
    Isolate* isolate, Handle<RegExpMatchInfo> last_match_info,
    DirectHandle<String> subject, int capture_count, int32_t* match) {
  // This is the only place where match infos can grow. If, after executing the
  // regexp, RegExpExecStub finds that the match info is too small, it restarts
  // execution in RegExpImpl::Exec, which finally grows the match info right
  // here.
  Handle<RegExpMatchInfo> result =
      RegExpMatchInfo::ReserveCaptures(isolate, last_match_info, capture_count);
  if (*result != *last_match_info) {
    if (*last_match_info == *isolate->regexp_last_match_info()) {
      // This inner condition is only needed for special situations like the
      // regexp fuzzer, where we pass our own custom RegExpMatchInfo to
      // RegExpImpl::Exec; there actually want to bypass the Isolate's match
      // info and execute the regexp without side effects.
      isolate->native_context()->set_regexp_last_match_info(*result);
    }
  }

  int capture_register_count =
      JSRegExp::RegistersForCaptureCount(capture_count);
  DisallowGarbageCollection no_gc;
  if (match != nullptr) {
    for (int i = 0; i < capture_register_count; i += 2) {
      result->set_capture(i, match[i]);
      result->set_capture(i + 1, match[i + 1]);
    }
  }
  result->set_last_subject(*subject);
  result->set_last_input(*subject);
  return result;
}

// static
void RegExp::DotPrintForTesting(const char* label, RegExpNode* node) {
  DotPrinter::DotPrint(label, node);
}

namespace {

// Returns true if we've either generated too much irregex code within this
// isolate, or the pattern string is too long.
bool TooMuchRegExpCode(Isolate* isolate, DirectHandle<String> pattern) {
  // Limit the space regexps take up on the heap.  In order to limit this we
  // would like to keep track of the amount of regexp code on the heap.  This
  // is not tracked, however.  As a conservative approximation we track the
  // total regexp code compiled including code that has subsequently been freed
  // and the total executable memory at any point.
  static constexpr size_t kRegExpExecutableMemoryLimit = 16 * MB;
  static constexpr size_t kRegExpCompiledLimit = 1 * MB;

  Heap* heap = isolate->heap();
  if (pattern->length() > RegExp::kRegExpTooLargeToOptimize) return true;
  return (isolate->total_regexp_code_generated() > kRegExpCompiledLimit &&
          heap->CommittedMemoryExecutable() > kRegExpExecutableMemoryLimit);
}

}  // namespace

// static
bool RegExp::CompileForTesting(Isolate* isolate, Zone* zone,
                               RegExpCompileData* data, RegExpFlags flags,
                               Handle<String> pattern,
                               Handle<String> sample_subject,
                               bool is_one_byte) {
  uint32_t backtrack_limit = JSRegExp::kNoBacktrackLimit;
  return RegExpImpl::Compile(isolate, zone, data, flags, pattern,
                             sample_subject, is_one_byte, backtrack_limit);
}

bool RegExpImpl::Compile(Isolate* isolate, Zone* zone, RegExpCompileData* data,
                         RegExpFlags flags, Handle<String> pattern,
                         Handle<String> sample_subject, bool is_one_byte,
                         uint32_t& backtrack_limit) {
  if (JSRegExp::RegistersForCaptureCount(data->capture_count) >
      RegExpMacroAssembler::kMaxRegisterCount) {
    data->error = RegExpError::kTooLarge;
    return false;
  }

  RegExpCompiler compiler(isolate, zone, data->capture_count, flags,
                          is_one_byte);

  if (compiler.optimize()) {
    compiler.set_optimize(!TooMuchRegExpCode(isolate, pattern));
  }

  // Sample some characters from the middle of the string.
  static const int kSampleSize = 128;

  sample_subject = String::Flatten(isolate, sample_subject);
  int chars_sampled = 0;
  int half_way = (sample_subject->length() - kSampleSize) / 2;
  for (int i = std::max(0, half_way);
       i < sample_subject->length() && chars_sampled < kSampleSize;
       i++, chars_sampled++) {
    compiler.frequency_collator()->CountCharacter(sample_subject->Get(i));
  }

  data->node = compiler.PreprocessRegExp(data, is_one_byte);
  data->error = AnalyzeRegExp(isolate, is_one_byte, flags, data->node);
  if (data->error != RegExpError::kNone) {
    return false;
  }

  if (v8_flags.trace_regexp_graph) DotPrinter::DotPrint("Start", data->node);

  // Create the correct assembler for the architecture.
  std::unique_ptr<RegExpMacroAssembler> macro_assembler;
  if (data->compilation_target == RegExpCompilationTarget::kNative) {
    // Native regexp implementation.
    DCHECK(!v8_flags.jitless);

    NativeRegExpMacroAssembler::Mode mode =
        is_one_byte ? NativeRegExpMacroAssembler::LATIN1
                    : NativeRegExpMacroAssembler::UC16;

    const int output_register_count =
        JSRegExp::RegistersForCaptureCount(data->capture_count);
#if V8_TARGET_ARCH_IA32
    macro_assembler.reset(new RegExpMacroAssemblerIA32(isolate, zone, mode,
                                                       output_register_count));
#elif V8_TARGET_ARCH_X64
    macro_assembler.reset(new RegExpMacroAssemblerX64(isolate, zone, mode,
                                                      output_register_count));
#elif V8_TARGET_ARCH_ARM
    macro_assembler.reset(new RegExpMacroAssemblerARM(isolate, zone, mode,
                                                      output_register_count));
#elif V8_TARGET_ARCH_ARM64
    macro_assembler.reset(new RegExpMacroAssemblerARM64(isolate, zone, mode,
                                                        output_register_count));
#elif V8_TARGET_ARCH_S390
    macro_assembler.reset(new RegExpMacroAssemblerS390(isolate, zone, mode,
                                                       output_register_count));
#elif V8_TARGET_ARCH_PPC || V8_TARGET_ARCH_PPC64
    macro_assembler.reset(new RegExpMacroAssemblerPPC(isolate, zone, mode,
                                                      output_register_count));
#elif V8_TARGET_ARCH_MIPS64
    macro_assembler.reset(new RegExpMacroAssemblerMIPS(isolate, zone, mode,
                                                       output_register_count));
#elif V8_TARGET_ARCH_RISCV64
    macro_assembler.reset(new RegExpMacroAssemblerRISCV(isolate, zone, mode,
                                                        output_register_count));
#elif V8_TARGET_ARCH_RISCV32
    macro_assembler.reset(new RegExpMacroAssemblerRISCV(isolate, zone, mode,
                                                        output_register_count));
#elif V8_TARGET_ARCH_LOONG64
    macro_assembler.reset(new RegExpMacroAssemblerLOONG64(
        isolate, zone, mode, output_register_count));
#else
#error "Unsupported architecture"
#endif
  } else {
    DCHECK_EQ(data->compilation_target, RegExpCompilationTarget::kBytecode);
    // Interpreted regexp implementation.
    macro_assembler.reset(new RegExpBytecodeGenerator(isolate, zone));
  }

  macro_assembler->set_slow_safe(TooMuchRegExpCode(isolate, pattern));
  if (v8_flags.enable_experimental_regexp_engine_on_excessive_backtracks &&
      ExperimentalRegExp::CanBeHandled(data->tree, pattern, flags,
                                       data->capture_count)) {
    if (backtrack_limit == JSRegExp::kNoBacktrackLimit) {
      backtrack_limit = v8_flags.regexp_backtracks_before_fallback;
    } else {
      backtrack_limit = std::min(
          backtrack_limit, v8_flags.regexp_backtracks_before_fallback.value());
    }
    macro_assembler->set_backtrack_limit(backtrack_limit);
    macro_assembler->set_can_fallback(true);
  } else {
    macro_assembler->set_backtrack_limit(backtrack_limit);
    macro_assembler->set_can_fallback(false);
  }

  // Inserted here, instead of in Assembler, because it depends on information
  // in the AST that isn't replicated in the Node structure.
  bool is_end_anchored = data->tree->IsAnchoredAtEnd();
  bool is_start_anchored = data->tree->IsAnchoredAtStart();
  int max_length = data->tree->max_match();
  static const int kMaxBacksearchLimit = 1024;
  if (is_end_anchored && !is_start_anchored && !IsSticky(flags) &&
      max_length < kMaxBacksearchLimit) {
    macro_assembler->SetCurrentPositionFromEnd(max_length);
  }

  if (IsGlobal(flags)) {
    RegExpMacroAssembler::GlobalMode mode = RegExpMacroAssembler::GLOBAL;
    if (data->tree->min_match() > 0) {
      mode = RegExpMacroAssembler::GLOBAL_NO_ZERO_LENGTH_CHECK;
    } else if (IsEitherUnicode(flags)) {
      mode = RegExpMacroAssembler::GLOBAL_UNICODE;
    }
    macro_assembler->set_global_mode(mode);
  }

  RegExpMacroAssembler* macro_assembler_ptr = macro_assembler.get();
#ifdef DEBUG
  std::unique_ptr<RegExpMacroAssembler> tracer_macro_assembler;
  if (v8_flags.trace_regexp_assembler) {
    tracer_macro_assembler.reset(
        new RegExpMacroAssemblerTracer(isolate, macro_assembler_ptr));
    macro_assembler_ptr = tracer_macro_assembler.get();
  }
#endif

  RegExpCompiler::CompilationResult result = compiler.Assemble(
      isolate, macro_assembler_ptr, data->node, data->capture_count, pattern);

  // Code / bytecode printing.
  {
#ifdef ENABLE_DISASSEMBLER
    if (v8_flags.print_regexp_code &&
        data->compilation_target == RegExpCompilationTarget::kNative) {
      CodeTracer::Scope trace_scope(isolate->GetCodeTracer());
      OFStream os(trace_scope.file());
      auto code = Cast<Code>(result.code);
      std::unique_ptr<char[]> pattern_cstring = pattern->ToCString();
      code->Disassemble(pattern_cstring.get(), os, isolate);
    }
#endif
    if (v8_flags.print_regexp_bytecode &&
        data->compilation_target == RegExpCompilationTarget::kBytecode) {
      auto bytecode = Cast<ByteArray>(result.code);
      std::unique_ptr<char[]> pattern_cstring = pattern->ToCString();
      RegExpBytecodeDisassemble(bytecode->begin(), bytecode->length(),
                                pattern_cstring.get());
    }
  }

  if (result.error != RegExpError::kNone) {
    if (v8_flags.correctness_fuzzer_suppressions &&
        result.error == RegExpError::kStackOverflow) {
      FATAL("Aborting on stack overflow");
    }
    data->error = result.error;
  }

  data->code = result.code;
  data->register_count = result.num_registers;

  return result.Succeeded();
}

RegExpGlobalCache::RegExpGlobalCache(Handle<RegExpData> regexp_data,
                                     Handle<String> subject, Isolate* isolate)
    : register_array_(nullptr),
      register_array_size_(0),
      regexp_data_(regexp_data),
      subject_(subject),
      isolate_(isolate) {
  DCHECK(IsGlobal(JSRegExp::AsRegExpFlags(regexp_data->flags())));

  switch (regexp_data_->type_tag()) {
    case RegExpData::Type::ATOM: {
      // ATOM regexps do not have a global loop, so we search for one match at
      // a time.
      static const int kAtomRegistersPerMatch = 2;
      registers_per_match_ = kAtomRegistersPerMatch;
      register_array_size_ = registers_per_match_;
      break;
    }
    case RegExpData::Type::IRREGEXP: {
      registers_per_match_ = RegExpImpl::IrregexpPrepare(
          isolate_, Cast<IrRegExpData>(regexp_data_), subject_);
      if (registers_per_match_ < 0) {
        num_matches_ = -1;  // Signal exception.
        return;
      }
      if (Cast<IrRegExpData>(regexp_data_)->ShouldProduceBytecode()) {
        // Global loop in interpreted regexp is not implemented.  We choose the
        // size of the offsets vector so that it can only store one match.
        register_array_size_ = registers_per_match_;
        max_matches_ = 1;
      } else {
        register_array_size_ = std::max(
            {registers_per_match_, Isolate::kJSRegexpStaticOffsetsVectorSize});
      }
      break;
    }
    case RegExpData::Type::EXPERIMENTAL: {
      if (!ExperimentalRegExp::IsCompiled(Cast<IrRegExpData>(regexp_data_),
                                          isolate_) &&
          !ExperimentalRegExp::Compile(isolate_,
                                       Cast<IrRegExpData>(regexp_data_))) {
        DCHECK(isolate->has_exception());
        num_matches_ = -1;  // Signal exception.
        return;
      }
      registers_per_match_ = JSRegExp::RegistersForCaptureCount(
          Cast<IrRegExpData>(regexp_data_)->capture_count());
      register_array_size_ = std::max(
          {registers_per_match_, Isolate::kJSRegexpStaticOffsetsVectorSize});
      break;
    }
  }

  max_matches_ = register_array_size_ / registers_per_match_;

  if (register_array_size_ > Isolate::kJSRegexpStaticOffsetsVectorSize) {
    register_array_ = NewArray<int32_t>(register_array_size_);
  } else {
    register_array_ = isolate->jsregexp_static_offsets_vector();
  }

  // Set state so that fetching the results the first time triggers a call
  // to the compiled regexp.
  current_match_index_ = max_matches_ - 1;
  num_matches_ = max_matches_;
  DCHECK_LE(2, registers_per_match_);  // Each match has at least one capture.
  DCHECK_GE(register_array_size_, registers_per_match_);
  int32_t* last_match =
      &register_array_[current_match_index_ * registers_per_match_];
  last_match[0] = -1;
  last_match[1] = 0;
}

RegExpGlobalCache::~RegExpGlobalCache() {
  // Deallocate the register array if we allocated it in the constructor
  // (as opposed to using the existing jsregexp_static_offsets_vector).
  if (register_array_size_ > Isolate::kJSRegexpStaticOffsetsVectorSize) {
    DeleteArray(register_array_);
  }
}

int RegExpGlobalCache::AdvanceZeroLength(int last_index) {
  if (IsEitherUnicode(JSRegExp::AsRegExpFlags(regexp_data_->flags())) &&
      last_index + 1 < subject_->length() &&
      unibrow::Utf16::IsLeadSurrogate(subject_->Get(last_index)) &&
      unibrow::Utf16::IsTrailSurrogate(subject_->Get(last_index + 1))) {
    // Advance over the surrogate pair.
    return last_index + 2;
  }
  return last_index + 1;
}

int32_t* RegExpGlobalCache::FetchNext() {
  current_match_index_++;

  if (current_match_index_ >= num_matches_) {
    // Current batch of results exhausted.
    // Fail if last batch was not even fully filled.
    if (num_matches_ < max_matches_) {
      num_matches_ = 0;  // Signal failed match.
      return nullptr;
    }

    int32_t* last_match =
        &register_array_[(current_match_index_ - 1) * registers_per_match_];
    int last_end_index = last_match[1];

    switch (regexp_data_->type_tag()) {
      case RegExpData::Type::ATOM:
        num_matches_ = RegExpImpl::AtomExecRaw(
            isolate_, Cast<AtomRegExpData>(regexp_data_), subject_,
            last_end_index, register_array_, register_array_size_);
        break;
      case RegExpData::Type::EXPERIMENTAL: {
        DCHECK(ExperimentalRegExp::IsCompiled(Cast<IrRegExpData>(regexp_data_),
                                              isolate_));
        DisallowGarbageCollection no_gc;
        num_matches_ = ExperimentalRegExp::ExecRaw(
            isolate_, RegExp::kFromRuntime, *Cast<IrRegExpData>(regexp_data_),
            *subject_, register_array_, register_array_size_, last_end_index);
        break;
      }
      case RegExpData::Type::IRREGEXP: {
        int last_start_index = last_match[0];
        if (last_start_index == last_end_index) {
          // Zero-length match. Advance by one code point.
          last_end_index = AdvanceZeroLength(last_end_index);
        }
        if (last_end_index > subject_->length()) {
          num_matches_ = 0;  // Signal failed match.
          return nullptr;
        }
        num_matches_ = RegExpImpl::IrregexpExecRaw(
            isolate_, Cast<IrRegExpData>(regexp_data_), subject_,
            last_end_index, register_array_, register_array_size_);
        break;
      }
    }

    // Fall back to experimental engine if needed and possible.
    if (num_matches_ == RegExp::kInternalRegExpFallbackToExperimental) {
      num_matches_ = ExperimentalRegExp::OneshotExecRaw(
          isolate_, Cast<IrRegExpData>(regexp_data_), subject_, register_array_,
          register_array_size_, last_end_index);
    }

    if (num_matches_ <= 0) {
      return nullptr;
    }

    // Number of matches can't exceed maximum matches.
    // This check is enough to prevent OOB accesses to register_array_ in the
    // else branch below, since current_match_index < num_matches_ in this
    // branch, it follows that current_match_index < max_matches_. And since
    // max_matches_ = register_array_size_ / registers_per_match it follows
    // that current_match_index * registers_per_match_ < register_array_size_.
    SBXCHECK_LE(num_matches_, max_matches_);

    current_match_index_ = 0;
    return register_array_;
  } else {
    return &register_array_[current_match_index_ * registers_per_match_];
  }
}

int32_t* RegExpGlobalCache::LastSuccessfulMatch() {
  int index = current_match_index_ * registers_per_match_;
  if (num_matches_ == 0) {
    // After a failed match we shift back by one result.
    index -= registers_per_match_;
  }
  return &register_array_[index];
}

Tagged<Object> RegExpResultsCache::Lookup(Heap* heap, Tagged<String> key_string,
                                          Tagged<Object> key_pattern,
                                          Tagged<FixedArray>* last_match_cache,
                                          ResultsCacheType type) {
  Tagged<FixedArray> cache;
  if (!IsInternalizedString(key_string)) return Smi::zero();
  if (type == STRING_SPLIT_SUBSTRINGS) {
    DCHECK(IsString(key_pattern));
    if (!IsInternalizedString(key_pattern)) return Smi::zero();
    cache = heap->string_split_cache();
  } else {
    DCHECK(type == REGEXP_MULTIPLE_INDICES);
    DCHECK(IsRegExpDataWrapper(key_pattern));
    cache = heap->regexp_multiple_cache();
  }

  uint32_t hash = key_string->hash();
  uint32_t index = ((hash & (kRegExpResultsCacheSize - 1)) &
                    ~(kArrayEntriesPerCacheEntry - 1));
  if (cache->get(index + kStringOffset) != key_string ||
      cache->get(index + kPatternOffset) != key_pattern) {
    index =
        ((index + kArrayEntriesPerCacheEntry) & (kRegExpResultsCacheSize - 1));
    if (cache->get(index + kStringOffset) != key_string ||
        cache->get(index + kPatternOffset) != key_pattern) {
      return Smi::zero();
    }
  }

  *last_match_cache = Cast<FixedArray>(cache->get(index + kLastMatchOffset));
  return cache->get(index + kArrayOffset);
}

void RegExpResultsCache::Enter(Isolate* isolate,
                               DirectHandle<String> key_string,
                               DirectHandle<Object> key_pattern,
                               DirectHandle<FixedArray> value_array,
                               DirectHandle<FixedArray> last_match_cache,
                               ResultsCacheType type) {
  Factory* factory = isolate->factory();
  DirectHandle<FixedArray> cache;
  if (!IsInternalizedString(*key_string)) return;
  if (type == STRING_SPLIT_SUBSTRINGS) {
    DCHECK(IsString(*key_pattern));
    if (!IsInternalizedString(*key_pattern)) return;
    cache = factory->string_split_cache();
  } else {
    DCHECK(type == REGEXP_MULTIPLE_INDICES);
    DCHECK(IsRegExpDataWrapper(*key_pattern));
    cache = factory->regexp_multiple_cache();
  }

  uint32_t hash = key_string->hash();
  uint32_t index = ((hash & (kRegExpResultsCacheSize - 1)) &
                    ~(kArrayEntriesPerCacheEntry - 1));
  if (cache->get(index + kStringOffset) == Smi::zero()) {
    cache->set(index + kStringOffset, *key_string);
    cache->set(index + kPatternOffset, *key_pattern);
    cache->set(index + kArrayOffset, *value_array);
    cache->set(index + kLastMatchOffset, *last_match_cache);
  } else {
    uint32_t index2 =
        ((index + kArrayEntriesPerCacheEntry) & (kRegExpResultsCacheSize - 1));
    if (cache->get(index2 + kStringOffset) == Smi::zero()) {
      cache->set(index2 + kStringOffset, *key_string);
      cache->set(index2 + kPatternOffset, *key_pattern);
      cache->set(index2 + kArrayOffset, *value_array);
      cache->set(index2 + kLastMatchOffset, *last_match_cache);
    } else {
      cache->set(index2 + kStringOffset, Smi::zero());
      cache->set(index2 + kPatternOffset, Smi::zero());
      cache->set(index2 + kArrayOffset, Smi::zero());
      cache->set(index2 + kLastMatchOffset, Smi::zero());
      cache->set(index + kStringOffset, *key_string);
      cache->set(index + kPatternOffset, *key_pattern);
      cache->set(index + kArrayOffset, *value_array);
      cache->set(index + kLastMatchOffset, *last_match_cache);
    }
  }
  // If the array is a reasonably short list of substrings, convert it into a
  // list of internalized strings.
  if (type == STRING_SPLIT_SUBSTRINGS && value_array->length() < 100) {
    for (int i = 0; i < value_array->length(); i++) {
      Handle<String> str(Cast<String>(value_array->get(i)), isolate);
      DirectHandle<String> internalized_str = factory->InternalizeString(str);
      value_array->set(i, *internalized_str);
    }
  }
  // Convert backing store to a copy-on-write array.
  value_array->set_map_no_write_barrier(
      ReadOnlyRoots(isolate).fixed_cow_array_map());
}

void RegExpResultsCache::Clear(Tagged<FixedArray> cache) {
  for (int i = 0; i < kRegExpResultsCacheSize; i++) {
    cache->set(i, Smi::zero());
  }
}

std::ostream& operator<<(std::ostream& os, RegExpFlags flags) {
#define V(Lower, Camel, LowerCamel, Char, Bit) \
  if (flags & RegExpFlag::k##Camel) os << Char;
  REGEXP_FLAG_LIST(V)
#undef V
  return os;
}

}  // namespace internal
}  // namespace v8
