// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_PARSING_PARSE_INFO_H_
#define V8_PARSING_PARSE_INFO_H_

#include <memory>

#include "include/v8-callbacks.h"
#include "src/base/bit-field.h"
#include "src/base/export-template.h"
#include "src/base/logging.h"
#include "src/common/globals.h"
#include "src/handles/handles.h"
#include "src/objects/function-kind.h"
#include "src/objects/function-syntax-kind.h"
#include "src/objects/script.h"
#include "src/parsing/pending-compilation-error-handler.h"
#include "src/parsing/preparse-data.h"

namespace v8 {

class Extension;

namespace internal {

class AccountingAllocator;
class AstRawString;
class AstStringConstants;
class AstValueFactory;
class LazyCompileDispatcher;
class DeclarationScope;
class FunctionLiteral;
class RuntimeCallStats;
class V8FileLogger;
class SourceRangeMap;
class Utf16CharacterStream;
class Zone;

// The flags for a parse + unoptimized compile operation.
#define FLAG_FIELDS(V, _)                                       \
  V(is_toplevel, bool, 1, _)                                    \
  V(is_eager, bool, 1, _)                                       \
  V(is_eval, bool, 1, _)                                        \
  V(is_reparse, bool, 1, _)                                     \
  V(outer_language_mode, LanguageMode, 1, _)                    \
  V(parse_restriction, ParseRestriction, 1, _)                  \
  V(is_module, bool, 1, _)                                      \
  V(allow_lazy_parsing, bool, 1, _)                             \
  V(is_lazy_compile, bool, 1, _)                                \
  V(coverage_enabled, bool, 1, _)                               \
  V(block_coverage_enabled, bool, 1, _)                         \
  V(is_asm_wasm_broken, bool, 1, _)                             \
  V(class_scope_has_private_brand, bool, 1, _)                  \
  V(private_name_lookup_skips_outer_class, bool, 1, _)          \
  V(requires_instance_members_initializer, bool, 1, _)          \
  V(has_static_private_methods_or_accessors, bool, 1, _)        \
  V(might_always_turbofan, bool, 1, _)                          \
  V(allow_natives_syntax, bool, 1, _)                           \
  V(allow_lazy_compile, bool, 1, _)                             \
  V(post_parallel_compile_tasks_for_eager_toplevel, bool, 1, _) \
  V(post_parallel_compile_tasks_for_lazy, bool, 1, _)           \
  V(collect_source_positions, bool, 1, _)                       \
  V(is_repl_mode, bool, 1, _)                                   \
  V(produce_compile_hints, bool, 1, _)                          \
  V(compile_hints_magic_enabled, bool, 1, _)

class V8_EXPORT_PRIVATE UnoptimizedCompileFlags {
 public:
  // Set-up flags for a toplevel compilation.
  static UnoptimizedCompileFlags ForToplevelCompile(Isolate* isolate,
                                                    bool is_user_javascript,
                                                    LanguageMode language_mode,
                                                    REPLMode repl_mode,
                                                    ScriptType type, bool lazy);

  // Set-up flags for a compiling a particular function (either a lazy compile
  // or a recompile).
  static UnoptimizedCompileFlags ForFunctionCompile(
      Isolate* isolate, Tagged<SharedFunctionInfo> shared);

  // Set-up flags for a full compilation of a given script.
  static UnoptimizedCompileFlags ForScriptCompile(Isolate* isolate,
                                                  Tagged<Script> script);

  // Set-up flags for a parallel toplevel function compilation, based on the
  // flags of an existing toplevel compilation.
  static UnoptimizedCompileFlags ForToplevelFunction(
      const UnoptimizedCompileFlags toplevel_flags,
      const FunctionLiteral* literal);

  // Create flags for a test.
  static UnoptimizedCompileFlags ForTest(Isolate* isolate);

#define FLAG_GET_SET(NAME, TYPE, SIZE, _)                       \
  TYPE NAME() const { return BitFields::NAME::decode(flags_); } \
  UnoptimizedCompileFlags& set_##NAME(TYPE value) {             \
    flags_ = BitFields::NAME::update(flags_, value);            \
    return *this;                                               \
  }

  FLAG_FIELDS(FLAG_GET_SET, _)

  int script_id() const { return script_id_; }
  UnoptimizedCompileFlags& set_script_id(int value) {
    script_id_ = value;
    return *this;
  }

  FunctionKind function_kind() const { return function_kind_; }
  UnoptimizedCompileFlags& set_function_kind(FunctionKind value) {
    function_kind_ = value;
    return *this;
  }

  FunctionSyntaxKind function_syntax_kind() const {
    return function_syntax_kind_;
  }
  UnoptimizedCompileFlags& set_function_syntax_kind(FunctionSyntaxKind value) {
    function_syntax_kind_ = value;
    return *this;
  }

  ParsingWhileDebugging parsing_while_debugging() const {
    return parsing_while_debugging_;
  }
  UnoptimizedCompileFlags& set_parsing_while_debugging(
      ParsingWhileDebugging value) {
    parsing_while_debugging_ = value;
    return *this;
  }

 private:
  struct BitFields {
    DEFINE_BIT_FIELDS(FLAG_FIELDS)
  };

  UnoptimizedCompileFlags(Isolate* isolate, int script_id);

  // Set function info flags based on those in either FunctionLiteral or
  // SharedFunctionInfo |function|
  template <typename T>
  void SetFlagsFromFunction(T function);
  void SetFlagsForToplevelCompile(bool is_user_javascript,
                                  LanguageMode language_mode,
                                  REPLMode repl_mode, ScriptType type,
                                  bool lazy);
  void SetFlagsForFunctionFromScript(Tagged<Script> script);

  uint32_t flags_;
  int script_id_;
  FunctionKind function_kind_;
  FunctionSyntaxKind function_syntax_kind_;
  ParsingWhileDebugging parsing_while_debugging_;
};

#undef FLAG_FIELDS
class ParseInfo;

// The mutable state for a parse + unoptimized compile operation.
class V8_EXPORT_PRIVATE UnoptimizedCompileState {
 public:
  const PendingCompilationErrorHandler* pending_error_handler() const {
    return &pending_error_handler_;
  }
  PendingCompilationErrorHandler* pending_error_handler() {
    return &pending_error_handler_;
  }

 private:
  PendingCompilationErrorHandler pending_error_handler_;
};

// A container for ParseInfo fields that are reusable across multiple parses and
// unoptimized compiles.
//
// Note that this is different from UnoptimizedCompileState, which has mutable
// state for a single compilation that is not reusable across multiple
// compilations.
class V8_EXPORT_PRIVATE ReusableUnoptimizedCompileState {
 public:
  explicit ReusableUnoptimizedCompileState(Isolate* isolate);
  explicit ReusableUnoptimizedCompileState(LocalIsolate* isolate);
  ~ReusableUnoptimizedCompileState();

  // The AstRawString Zone stores the AstRawStrings in the AstValueFactory that
  // can be reused across parses, and thereforce should stay alive between
  // parses that reuse this reusable state and its AstValueFactory.
  Zone* ast_raw_string_zone() { return &ast_raw_string_zone_; }

  // The single parse Zone stores the data of a single parse, and can be cleared
  // when that parse completes.
  //
  // This is in "reusable" state despite being wiped per-parse, because it
  // allows us to reuse the Zone itself, and e.g. keep the same single parse
  // Zone pointer in the AstValueFactory.
  Zone* single_parse_zone() { return &single_parse_zone_; }

  void NotifySingleParseCompleted() { single_parse_zone_.Reset(); }

  AstValueFactory* ast_value_factory() const {
    return ast_value_factory_.get();
  }
  uint64_t hash_seed() const { return hash_seed_; }
  AccountingAllocator* allocator() const { return allocator_; }
  const AstStringConstants* ast_string_constants() const {
    return ast_string_constants_;
  }
  // TODO(cbruni): Switch this back to the main logger.
  V8FileLogger* v8_file_logger() const { return v8_file_logger_; }
  LazyCompileDispatcher* dispatcher() const { return dispatcher_; }

 private:
  uint64_t hash_seed_;
  AccountingAllocator* allocator_;
  V8FileLogger* v8_file_logger_;
  LazyCompileDispatcher* dispatcher_;
  const AstStringConstants* ast_string_constants_;
  Zone ast_raw_string_zone_;
  Zone single_parse_zone_;
  std::unique_ptr<AstValueFactory> ast_value_factory_;
};

// A container for the inputs, configuration options, and outputs of parsing.
class V8_EXPORT_PRIVATE ParseInfo {
 public:
  ParseInfo(Isolate* isolate, const UnoptimizedCompileFlags flags,
            UnoptimizedCompileState* state,
            ReusableUnoptimizedCompileState* reusable_state);
  ParseInfo(LocalIsolate* isolate, const UnoptimizedCompileFlags flags,
            UnoptimizedCompileState* state,
            ReusableUnoptimizedCompileState* reusable_state,
            uintptr_t stack_limit);

  ~ParseInfo();

  template <typename IsolateT>
  EXPORT_TEMPLATE_DECLARE(V8_EXPORT_PRIVATE)
  Handle<Script> CreateScript(IsolateT* isolate, Handle<String> source,
                              MaybeHandle<FixedArray> maybe_wrapped_arguments,
                              ScriptOriginOptions origin_options,
                              NativesFlag natives = NOT_NATIVES_CODE);

  Zone* zone() const { return reusable_state_->single_parse_zone(); }

  const UnoptimizedCompileFlags& flags() const { return flags_; }

  // Getters for reusable state.
  uint64_t hash_seed() const { return reusable_state_->hash_seed(); }
  AccountingAllocator* allocator() const {
    return reusable_state_->allocator();
  }
  const AstStringConstants* ast_string_constants() const {
    return reusable_state_->ast_string_constants();
  }
  V8FileLogger* v8_file_logger() const {
    return reusable_state_->v8_file_logger();
  }
  LazyCompileDispatcher* dispatcher() const {
    return reusable_state_->dispatcher();
  }
  const UnoptimizedCompileState* state() const { return state_; }

  // Getters for state.
  PendingCompilationErrorHandler* pending_error_handler() {
    return state_->pending_error_handler();
  }

  // Accessors for per-thread state.
  uintptr_t stack_limit() const { return stack_limit_; }
  RuntimeCallStats* runtime_call_stats() const { return runtime_call_stats_; }

  // Accessor methods for output flags.
  bool allow_eval_cache() const { return allow_eval_cache_; }
  void set_allow_eval_cache(bool value) { allow_eval_cache_ = value; }

#if V8_ENABLE_WEBASSEMBLY
  bool contains_asm_module() const { return contains_asm_module_; }
  void set_contains_asm_module(bool value) { contains_asm_module_ = value; }
#endif  // V8_ENABLE_WEBASSEMBLY

  LanguageMode language_mode() const { return language_mode_; }
  void set_language_mode(LanguageMode value) { language_mode_ = value; }

  Utf16CharacterStream* character_stream() const {
    return character_stream_.get();
  }
  void set_character_stream(
      std::unique_ptr<Utf16CharacterStream> character_stream);
  void ResetCharacterStream();

  v8::Extension* extension() const { return extension_; }
  void set_extension(v8::Extension* extension) { extension_ = extension; }

  void set_consumed_preparse_data(std::unique_ptr<ConsumedPreparseData> data) {
    consumed_preparse_data_.swap(data);
  }
  ConsumedPreparseData* consumed_preparse_data() {
    return consumed_preparse_data_.get();
  }

  DeclarationScope* script_scope() const { return script_scope_; }
  void set_script_scope(DeclarationScope* script_scope) {
    script_scope_ = script_scope;
  }

  AstValueFactory* ast_value_factory() const {
    return reusable_state_->ast_value_factory();
  }

  const AstRawString* function_name() const { return function_name_; }
  void set_function_name(const AstRawString* function_name) {
    function_name_ = function_name;
  }

  FunctionLiteral* literal() const { return literal_; }
  void set_literal(FunctionLiteral* literal) { literal_ = literal; }

  DeclarationScope* scope() const;

  int parameters_end_pos() const { return parameters_end_pos_; }
  void set_parameters_end_pos(int parameters_end_pos) {
    parameters_end_pos_ = parameters_end_pos;
  }

  bool is_wrapped_as_function() const {
    return flags().function_syntax_kind() == FunctionSyntaxKind::kWrapped;
  }

  int max_info_id() const { return max_info_id_; }
  void set_max_info_id(int max_info_id) { max_info_id_ = max_info_id; }

  void AllocateSourceRangeMap();
  SourceRangeMap* source_range_map() const { return source_range_map_; }
  void set_source_range_map(SourceRangeMap* source_range_map) {
    source_range_map_ = source_range_map;
  }

  void CheckFlagsForFunctionFromScript(Tagged<Script> script);

  bool is_background_compilation() const { return is_background_compilation_; }

  void set_is_background_compilation() { is_background_compilation_ = true; }

  bool is_streaming_compilation() const { return is_streaming_compilation_; }

  void set_is_streaming_compilation() { is_streaming_compilation_ = true; }

  bool has_module_in_scope_chain() const { return has_module_in_scope_chain_; }
  void set_has_module_in_scope_chain() { has_module_in_scope_chain_ = true; }

  void SetCompileHintCallbackAndData(CompileHintCallback callback, void* data) {
    DCHECK_NULL(compile_hint_callback_);
    DCHECK_NULL(compile_hint_callback_data_);
    compile_hint_callback_ = callback;
    compile_hint_callback_data_ = data;
  }

  CompileHintCallback compile_hint_callback() const {
    return compile_hint_callback_;
  }

  void* compile_hint_callback_data() const {
    return compile_hint_callback_data_;
  }

 private:
  ParseInfo(const UnoptimizedCompileFlags flags, UnoptimizedCompileState* state,
            ReusableUnoptimizedCompileState* reusable_state,
            uintptr_t stack_limit, RuntimeCallStats* runtime_call_stats);

  void CheckFlagsForToplevelCompileFromScript(Tagged<Script> script);

  //------------- Inputs to parsing and scope analysis -----------------------
  const UnoptimizedCompileFlags flags_;
  UnoptimizedCompileState* state_;
  ReusableUnoptimizedCompileState* reusable_state_;

  v8::Extension* extension_;
  DeclarationScope* script_scope_;
  uintptr_t stack_limit_;
  int parameters_end_pos_;
  int max_info_id_;

  v8::CompileHintCallback compile_hint_callback_ = nullptr;
  void* compile_hint_callback_data_ = nullptr;

  //----------- Inputs+Outputs of parsing and scope analysis -----------------
  std::unique_ptr<Utf16CharacterStream> character_stream_;
  std::unique_ptr<ConsumedPreparseData> consumed_preparse_data_;
  const AstRawString* function_name_;
  RuntimeCallStats* runtime_call_stats_;
  SourceRangeMap* source_range_map_;  // Used when block coverage is enabled.

  //----------- Output of parsing and scope analysis ------------------------
  FunctionLiteral* literal_;
  bool allow_eval_cache_ : 1;
#if V8_ENABLE_WEBASSEMBLY
  bool contains_asm_module_ : 1;
#endif  // V8_ENABLE_WEBASSEMBLY
  LanguageMode language_mode_ : 1;
  bool is_background_compilation_ : 1;
  bool is_streaming_compilation_ : 1;
  bool has_module_in_scope_chain_ : 1;
};

}  // namespace internal
}  // namespace v8

#endif  // V8_PARSING_PARSE_INFO_H_
