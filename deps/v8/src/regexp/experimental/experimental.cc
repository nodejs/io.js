// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/regexp/experimental/experimental.h"

#include "src/common/assert-scope.h"
#include "src/objects/js-regexp-inl.h"
#include "src/regexp/experimental/experimental-compiler.h"
#include "src/regexp/experimental/experimental-interpreter.h"
#include "src/regexp/regexp-parser.h"
#include "src/utils/ostreams.h"

namespace v8 {
namespace internal {

bool ExperimentalRegExp::CanBeHandled(RegExpTree* tree, Handle<String> pattern,
                                      RegExpFlags flags, int capture_count) {
  DCHECK(v8_flags.enable_experimental_regexp_engine ||
         v8_flags.enable_experimental_regexp_engine_on_excessive_backtracks);
  bool can_be_handled =
      ExperimentalRegExpCompiler::CanBeHandled(tree, flags, capture_count);
  if (!can_be_handled && v8_flags.trace_experimental_regexp_engine) {
    StdoutStream{} << "Pattern not supported by experimental engine: "
                   << pattern << std::endl;
  }
  return can_be_handled;
}

void ExperimentalRegExp::Initialize(Isolate* isolate, Handle<JSRegExp> re,
                                    Handle<String> source, RegExpFlags flags,
                                    int capture_count) {
  DCHECK(v8_flags.enable_experimental_regexp_engine);
  if (v8_flags.trace_experimental_regexp_engine) {
    StdoutStream{} << "Initializing experimental regexp " << *source
                   << std::endl;
  }

  isolate->factory()->SetRegExpExperimentalData(
      re, source, JSRegExp::AsJSRegExpFlags(flags), capture_count);
}

bool ExperimentalRegExp::IsCompiled(Handle<JSRegExp> re, Isolate* isolate) {
  DCHECK(v8_flags.enable_experimental_regexp_engine);
  DCHECK_EQ(re->type_tag(), JSRegExp::EXPERIMENTAL);
#ifdef VERIFY_HEAP
  if (v8_flags.verify_heap) re->JSRegExpVerify(isolate);
#endif

  static constexpr bool kIsLatin1 = true;
  return re->bytecode(kIsLatin1) != Smi::FromInt(JSRegExp::kUninitializedValue);
}

template <class T>
Handle<ByteArray> VectorToByteArray(Isolate* isolate, base::Vector<T> data) {
  static_assert(std::is_trivial<T>::value);

  int byte_length = sizeof(T) * data.length();
  Handle<ByteArray> byte_array = isolate->factory()->NewByteArray(byte_length);
  DisallowGarbageCollection no_gc;
  MemCopy(byte_array->begin(), data.begin(), byte_length);
  return byte_array;
}

namespace {

struct CompilationResult {
  Handle<ByteArray> bytecode;
  Handle<FixedArray> capture_name_map;
};

// Compiles source pattern, but doesn't change the regexp object.
base::Optional<CompilationResult> CompileImpl(Isolate* isolate,
                                              Handle<JSRegExp> regexp) {
  Zone zone(isolate->allocator(), ZONE_NAME);

  Handle<String> source(regexp->source(), isolate);

  // Parse and compile the regexp source.
  RegExpCompileData parse_result;
  DCHECK(!isolate->has_exception());

  RegExpFlags flags = JSRegExp::AsRegExpFlags(regexp->flags());
  bool parse_success = RegExpParser::ParseRegExpFromHeapString(
      isolate, &zone, source, flags, &parse_result);
  if (!parse_success) {
    // The pattern was already parsed successfully during initialization, so
    // the only way parsing can fail now is because of stack overflow.
    DCHECK_EQ(parse_result.error, RegExpError::kStackOverflow);
    USE(RegExp::ThrowRegExpException(isolate, regexp, flags, source,
                                     parse_result.error));
    return base::nullopt;
  }

  ZoneList<RegExpInstruction> bytecode = ExperimentalRegExpCompiler::Compile(
      parse_result.tree, JSRegExp::AsRegExpFlags(regexp->flags()), &zone);

  CompilationResult result;
  result.bytecode = VectorToByteArray(isolate, bytecode.ToVector());
  result.capture_name_map =
      RegExp::CreateCaptureNameMap(isolate, parse_result.named_captures);
  return result;
}

}  // namespace

bool ExperimentalRegExp::Compile(Isolate* isolate, Handle<JSRegExp> re) {
  DCHECK(v8_flags.enable_experimental_regexp_engine);
  DCHECK_EQ(re->type_tag(), JSRegExp::EXPERIMENTAL);
#ifdef VERIFY_HEAP
  if (v8_flags.verify_heap) re->JSRegExpVerify(isolate);
#endif

  Handle<String> source(re->source(), isolate);
  if (v8_flags.trace_experimental_regexp_engine) {
    StdoutStream{} << "Compiling experimental regexp " << *source << std::endl;
  }

  base::Optional<CompilationResult> compilation_result =
      CompileImpl(isolate, re);
  if (!compilation_result.has_value()) {
    DCHECK(isolate->has_exception());
    return false;
  }

  re->set_bytecode_and_trampoline(isolate, compilation_result->bytecode);
  re->set_capture_name_map(compilation_result->capture_name_map);

  return true;
}

base::Vector<RegExpInstruction> AsInstructionSequence(
    Tagged<ByteArray> raw_bytes) {
  RegExpInstruction* inst_begin =
      reinterpret_cast<RegExpInstruction*>(raw_bytes->begin());
  int inst_num = raw_bytes->length() / sizeof(RegExpInstruction);
  DCHECK_EQ(sizeof(RegExpInstruction) * inst_num, raw_bytes->length());
  return base::Vector<RegExpInstruction>(inst_begin, inst_num);
}

namespace {

int32_t ExecRawImpl(Isolate* isolate, RegExp::CallOrigin call_origin,
                    Tagged<ByteArray> bytecode, Tagged<String> subject,
                    int capture_count, int32_t* output_registers,
                    int32_t output_register_count, int32_t subject_index) {
  DisallowGarbageCollection no_gc;
  // TODO(cbruni): remove once gcmole is fixed.
  DisableGCMole no_gc_mole;

  int register_count_per_match =
      JSRegExp::RegistersForCaptureCount(capture_count);

  int32_t result;
  DCHECK(subject->IsFlat());
  Zone zone(isolate->allocator(), ZONE_NAME);
  result = ExperimentalRegExpInterpreter::FindMatches(
      isolate, call_origin, bytecode, register_count_per_match, subject,
      subject_index, output_registers, output_register_count, &zone);
  return result;
}

}  // namespace

// Returns the number of matches.
int32_t ExperimentalRegExp::ExecRaw(
    Isolate* isolate, RegExp::CallOrigin call_origin, Tagged<JSRegExp> regexp,
    Tagged<String> subject, int32_t* output_registers,
    int32_t output_register_count, int32_t subject_index) {
  CHECK(v8_flags.enable_experimental_regexp_engine);
  DisallowGarbageCollection no_gc;

  if (v8_flags.trace_experimental_regexp_engine) {
    StdoutStream{} << "Executing experimental regexp " << regexp->source()
                   << std::endl;
  }

  static constexpr bool kIsLatin1 = true;
  Tagged<ByteArray> bytecode = ByteArray::cast(regexp->bytecode(kIsLatin1));

  return ExecRawImpl(isolate, call_origin, bytecode, subject,
                     regexp->capture_count(), output_registers,
                     output_register_count, subject_index);
}

int32_t ExperimentalRegExp::MatchForCallFromJs(
    Address subject, int32_t start_position, Address input_start,
    Address input_end, int* output_registers, int32_t output_register_count,
    RegExp::CallOrigin call_origin, Isolate* isolate, Address regexp) {
  DCHECK(v8_flags.enable_experimental_regexp_engine);
  DCHECK_NOT_NULL(isolate);
  DCHECK_NOT_NULL(output_registers);
  DCHECK(call_origin == RegExp::CallOrigin::kFromJs);

  DisallowGarbageCollection no_gc;
  DisallowJavascriptExecution no_js(isolate);
  DisallowHandleAllocation no_handles;
  DisallowHandleDereference no_deref;

  Tagged<String> subject_string = String::cast(Tagged<Object>(subject));

  Tagged<JSRegExp> regexp_obj = JSRegExp::cast(Tagged<Object>(regexp));

  return ExecRaw(isolate, RegExp::kFromJs, regexp_obj, subject_string,
                 output_registers, output_register_count, start_position);
}

MaybeHandle<Object> ExperimentalRegExp::Exec(
    Isolate* isolate, Handle<JSRegExp> regexp, Handle<String> subject,
    int subject_index, Handle<RegExpMatchInfo> last_match_info,
    RegExp::ExecQuirks exec_quirks) {
  DCHECK(v8_flags.enable_experimental_regexp_engine);
  DCHECK_EQ(regexp->type_tag(), JSRegExp::EXPERIMENTAL);
#ifdef VERIFY_HEAP
  if (v8_flags.verify_heap) regexp->JSRegExpVerify(isolate);
#endif

  if (!IsCompiled(regexp, isolate) && !Compile(isolate, regexp)) {
    DCHECK(isolate->has_exception());
    return MaybeHandle<Object>();
  }

  DCHECK(IsCompiled(regexp, isolate));

  subject = String::Flatten(isolate, subject);

  int capture_count = regexp->capture_count();
  int output_register_count = JSRegExp::RegistersForCaptureCount(capture_count);

  int32_t* output_registers;
  std::unique_ptr<int32_t[]> output_registers_release;
  if (output_register_count <= Isolate::kJSRegexpStaticOffsetsVectorSize) {
    output_registers = isolate->jsregexp_static_offsets_vector();
  } else {
    output_registers = NewArray<int32_t>(output_register_count);
    output_registers_release.reset(output_registers);
  }

  do {
    int num_matches =
        ExecRaw(isolate, RegExp::kFromRuntime, *regexp, *subject,
                output_registers, output_register_count, subject_index);

    if (num_matches > 0) {
      DCHECK_EQ(num_matches, 1);
      if (exec_quirks == RegExp::ExecQuirks::kTreatMatchAtEndAsFailure) {
        if (output_registers[0] >= subject->length()) {
          return isolate->factory()->null_value();
        }
      }
      return RegExp::SetLastMatchInfo(isolate, last_match_info, subject,
                                      capture_count, output_registers);
    } else if (num_matches == 0) {
      return isolate->factory()->null_value();
    } else {
      DCHECK_LT(num_matches, 0);
      if (num_matches == RegExp::kInternalRegExpRetry) {
        // Re-run execution.
        continue;
      }
      DCHECK(isolate->has_exception());
      return MaybeHandle<Object>();
    }
  } while (true);
  UNREACHABLE();
}

int32_t ExperimentalRegExp::OneshotExecRaw(Isolate* isolate,
                                           Handle<JSRegExp> regexp,
                                           Handle<String> subject,
                                           int32_t* output_registers,
                                           int32_t output_register_count,
                                           int32_t subject_index) {
  CHECK(v8_flags.enable_experimental_regexp_engine_on_excessive_backtracks);

  if (v8_flags.trace_experimental_regexp_engine) {
    StdoutStream{} << "Experimental execution (oneshot) of regexp "
                   << regexp->source() << std::endl;
  }

  base::Optional<CompilationResult> compilation_result =
      CompileImpl(isolate, regexp);
  if (!compilation_result.has_value()) return RegExp::kInternalRegExpException;

  DisallowGarbageCollection no_gc;
  return ExecRawImpl(isolate, RegExp::kFromRuntime,
                     *compilation_result->bytecode, *subject,
                     regexp->capture_count(), output_registers,
                     output_register_count, subject_index);
}

MaybeHandle<Object> ExperimentalRegExp::OneshotExec(
    Isolate* isolate, Handle<JSRegExp> regexp, Handle<String> subject,
    int subject_index, Handle<RegExpMatchInfo> last_match_info,
    RegExp::ExecQuirks exec_quirks) {
  DCHECK(v8_flags.enable_experimental_regexp_engine_on_excessive_backtracks);
  DCHECK_NE(regexp->type_tag(), JSRegExp::NOT_COMPILED);

  int capture_count = regexp->capture_count();
  int output_register_count = JSRegExp::RegistersForCaptureCount(capture_count);

  int32_t* output_registers;
  std::unique_ptr<int32_t[]> output_registers_release;
  if (output_register_count <= Isolate::kJSRegexpStaticOffsetsVectorSize) {
    output_registers = isolate->jsregexp_static_offsets_vector();
  } else {
    output_registers = NewArray<int32_t>(output_register_count);
    output_registers_release.reset(output_registers);
  }

  do {
    int num_matches = OneshotExecRaw(isolate, regexp, subject, output_registers,
                                     output_register_count, subject_index);

    if (num_matches > 0) {
      DCHECK_EQ(num_matches, 1);
      if (exec_quirks == RegExp::ExecQuirks::kTreatMatchAtEndAsFailure) {
        if (output_registers[0] >= subject->length()) {
          return isolate->factory()->null_value();
        }
      }
      return RegExp::SetLastMatchInfo(isolate, last_match_info, subject,
                                      capture_count, output_registers);
    } else if (num_matches == 0) {
      return isolate->factory()->null_value();
    } else {
      DCHECK_LT(num_matches, 0);
      if (num_matches == RegExp::kInternalRegExpRetry) {
        // Re-run execution.
        continue;
      }
      DCHECK(isolate->has_exception());
      return MaybeHandle<Object>();
    }
  } while (true);
  UNREACHABLE();
}

}  // namespace internal
}  // namespace v8
