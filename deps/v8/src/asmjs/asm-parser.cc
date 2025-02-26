// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/asmjs/asm-parser.h"

#include <math.h>
#include <string.h>

#include <algorithm>
#include <optional>

#include "src/asmjs/asm-js.h"
#include "src/asmjs/asm-types.h"
#include "src/base/overflowing-math.h"
#include "src/flags/flags.h"
#include "src/numbers/conversions-inl.h"
#include "src/parsing/scanner.h"
#include "src/wasm/wasm-limits.h"
#include "src/wasm/wasm-opcodes.h"

namespace v8 {
namespace internal {
namespace wasm {

#ifdef DEBUG
#define TRACE_ASM_PARSER(...)      \
  if (v8_flags.trace_asm_parser) { \
    PrintF(__VA_ARGS__);           \
  }
#else
#define TRACE_ASM_PARSER(...)
#endif

#define FAIL_AND_RETURN(ret, msg)                                          \
  failed_ = true;                                                          \
  failure_message_ = msg;                                                  \
  failure_location_ = static_cast<int>(scanner_.Position());               \
  TRACE_ASM_PARSER("[asm.js failure: %s, token: '%s', see: %s:%d]\n", msg, \
                   scanner_.Name(scanner_.Token()).c_str(), __FILE__,      \
                   __LINE__);                                              \
  return ret;

#define FAIL(msg) FAIL_AND_RETURN(, msg)
#define FAILn(msg) FAIL_AND_RETURN(nullptr, msg)

#define EXPECT_TOKEN_OR_RETURN(ret, token)      \
  do {                                          \
    if (scanner_.Token() != token) {            \
      FAIL_AND_RETURN(ret, "Unexpected token"); \
    }                                           \
    scanner_.Next();                            \
  } while (false)

#define EXPECT_TOKEN(token) EXPECT_TOKEN_OR_RETURN(, token)
#define EXPECT_TOKENn(token) EXPECT_TOKEN_OR_RETURN(nullptr, token)

#define RECURSE_OR_RETURN(ret, call)                                       \
  do {                                                                     \
    DCHECK(!failed_);                                                      \
    if (GetCurrentStackPosition() < stack_limit_) {                        \
      FAIL_AND_RETURN(ret, "Stack overflow while parsing asm.js module."); \
    }                                                                      \
    call;                                                                  \
    if (failed_) return ret;                                               \
  } while (false)

#define RECURSE(call) RECURSE_OR_RETURN(, call)
#define RECURSEn(call) RECURSE_OR_RETURN(nullptr, call)

#define TOK(name) AsmJsScanner::kToken_##name

AsmJsParser::AsmJsParser(Zone* zone, uintptr_t stack_limit,
                         Utf16CharacterStream* stream)
    : zone_(zone),
      scanner_(stream),
      module_builder_(zone->New<WasmModuleBuilder>(zone)),
      stack_limit_(stack_limit),
      block_stack_(zone),
      global_imports_(zone) {
  module_builder_->AddMemory(0);
  InitializeStdlibTypes();
}

void AsmJsParser::InitializeStdlibTypes() {
  auto* d = AsmType::Double();
  auto* dq = AsmType::DoubleQ();
  stdlib_dq2d_ = AsmType::Function(zone(), d);
  stdlib_dq2d_->AsFunctionType()->AddArgument(dq);

  stdlib_dqdq2d_ = AsmType::Function(zone(), d);
  stdlib_dqdq2d_->AsFunctionType()->AddArgument(dq);
  stdlib_dqdq2d_->AsFunctionType()->AddArgument(dq);

  auto* f = AsmType::Float();
  auto* fh = AsmType::Floatish();
  auto* fq = AsmType::FloatQ();
  auto* fq2fh = AsmType::Function(zone(), fh);
  fq2fh->AsFunctionType()->AddArgument(fq);

  auto* s = AsmType::Signed();
  auto* u = AsmType::Unsigned();
  auto* s2u = AsmType::Function(zone(), u);
  s2u->AsFunctionType()->AddArgument(s);

  auto* i = AsmType::Int();
  stdlib_i2s_ = AsmType::Function(zone_, s);
  stdlib_i2s_->AsFunctionType()->AddArgument(i);

  stdlib_ii2s_ = AsmType::Function(zone(), s);
  stdlib_ii2s_->AsFunctionType()->AddArgument(i);
  stdlib_ii2s_->AsFunctionType()->AddArgument(i);

  // The signatures in "9 Standard Library" of the spec draft are outdated and
  // have been superseded with the following by an errata:
  //  - Math.min/max : (signed, signed...) -> signed
  //                   (double, double...) -> double
  //                   (float, float...) -> float
  auto* minmax_d = AsmType::MinMaxType(zone(), d, d);
  auto* minmax_f = AsmType::MinMaxType(zone(), f, f);
  auto* minmax_s = AsmType::MinMaxType(zone(), s, s);
  stdlib_minmax_ = AsmType::OverloadedFunction(zone());
  stdlib_minmax_->AsOverloadedFunctionType()->AddOverload(minmax_s);
  stdlib_minmax_->AsOverloadedFunctionType()->AddOverload(minmax_f);
  stdlib_minmax_->AsOverloadedFunctionType()->AddOverload(minmax_d);

  // The signatures in "9 Standard Library" of the spec draft are outdated and
  // have been superseded with the following by an errata:
  //  - Math.abs : (signed) -> unsigned
  //               (double?) -> double
  //               (float?) -> floatish
  stdlib_abs_ = AsmType::OverloadedFunction(zone());
  stdlib_abs_->AsOverloadedFunctionType()->AddOverload(s2u);
  stdlib_abs_->AsOverloadedFunctionType()->AddOverload(stdlib_dq2d_);
  stdlib_abs_->AsOverloadedFunctionType()->AddOverload(fq2fh);

  // The signatures in "9 Standard Library" of the spec draft are outdated and
  // have been superseded with the following by an errata:
  //  - Math.ceil/floor/sqrt : (double?) -> double
  //                           (float?) -> floatish
  stdlib_ceil_like_ = AsmType::OverloadedFunction(zone());
  stdlib_ceil_like_->AsOverloadedFunctionType()->AddOverload(stdlib_dq2d_);
  stdlib_ceil_like_->AsOverloadedFunctionType()->AddOverload(fq2fh);

  stdlib_fround_ = AsmType::FroundType(zone());
}

FunctionSig* AsmJsParser::ConvertSignature(AsmType* return_type,
                                           const ZoneVector<AsmType*>& params) {
  FunctionSig::Builder sig_builder(
      zone(), !return_type->IsA(AsmType::Void()) ? 1 : 0, params.size());
  for (auto param : params) {
    if (param->IsA(AsmType::Double())) {
      sig_builder.AddParam(kWasmF64);
    } else if (param->IsA(AsmType::Float())) {
      sig_builder.AddParam(kWasmF32);
    } else if (param->IsA(AsmType::Int())) {
      sig_builder.AddParam(kWasmI32);
    } else {
      UNREACHABLE();
    }
  }
  if (!return_type->IsA(AsmType::Void())) {
    if (return_type->IsA(AsmType::Double())) {
      sig_builder.AddReturn(kWasmF64);
    } else if (return_type->IsA(AsmType::Float())) {
      sig_builder.AddReturn(kWasmF32);
    } else if (return_type->IsA(AsmType::Signed())) {
      sig_builder.AddReturn(kWasmI32);
    } else {
      UNREACHABLE();
    }
  }
  return sig_builder.Get();
}

bool AsmJsParser::Run() {
  ValidateModule();
  return !failed_;
}

class V8_NODISCARD AsmJsParser::TemporaryVariableScope {
 public:
  explicit TemporaryVariableScope(AsmJsParser* parser) : parser_(parser) {
    local_depth_ = parser_->function_temp_locals_depth_;
    parser_->function_temp_locals_depth_++;
  }
  ~TemporaryVariableScope() {
    DCHECK_EQ(local_depth_, parser_->function_temp_locals_depth_ - 1);
    parser_->function_temp_locals_depth_--;
  }
  uint32_t get() const { return parser_->TempVariable(local_depth_); }

 private:
  AsmJsParser* parser_;
  int local_depth_;
};

wasm::AsmJsParser::VarInfo* AsmJsParser::GetVarInfo(
    AsmJsScanner::token_t token) {
  const bool is_global = AsmJsScanner::IsGlobal(token);
  DCHECK(is_global || AsmJsScanner::IsLocal(token));
  base::Vector<VarInfo>& var_info =
      is_global ? global_var_info_ : local_var_info_;
  size_t old_capacity = var_info.size();
  size_t index = is_global ? AsmJsScanner::GlobalIndex(token)
                           : AsmJsScanner::LocalIndex(token);
  if (is_global && index + 1 > num_globals_) num_globals_ = index + 1;
  if (index + 1 > old_capacity) {
    size_t new_size = std::max(2 * old_capacity, index + 1);
    base::Vector<VarInfo> new_info = zone_->NewVector<VarInfo>(new_size);
    std::copy(var_info.begin(), var_info.end(), new_info.begin());
    var_info = new_info;
  }
  return &var_info[index];
}

uint32_t AsmJsParser::VarIndex(VarInfo* info) {
  DCHECK_EQ(info->kind, VarKind::kGlobal);
  return info->index + static_cast<uint32_t>(global_imports_.size());
}

void AsmJsParser::AddGlobalImport(base::Vector<const char> name, AsmType* type,
                                  ValueType vtype, bool mutable_variable,
                                  VarInfo* info) {
  // Allocate a separate variable for the import.
  // TODO(asmjs): Consider using the imported global directly instead of
  // allocating a separate global variable for immutable (i.e. const) imports.
  DeclareGlobal(info, mutable_variable, type, vtype,
                WasmInitExpr::DefaultValue(vtype));

  // Record the need to initialize the global from the import.
  global_imports_.push_back({name, vtype, info});
}

void AsmJsParser::DeclareGlobal(VarInfo* info, bool mutable_variable,
                                AsmType* type, ValueType vtype,
                                WasmInitExpr init) {
  info->kind = VarKind::kGlobal;
  info->type = type;
  info->index = module_builder_->AddGlobal(vtype, true, init);
  info->mutable_variable = mutable_variable;
}

void AsmJsParser::DeclareStdlibFunc(VarInfo* info, VarKind kind,
                                    AsmType* type) {
  info->kind = kind;
  info->type = type;
  info->index = 0;  // unused
  info->mutable_variable = false;
}

uint32_t AsmJsParser::TempVariable(int index) {
  if (index + 1 > function_temp_locals_used_) {
    function_temp_locals_used_ = index + 1;
  }
  return function_temp_locals_offset_ + index;
}

base::Vector<const char> AsmJsParser::CopyCurrentIdentifierString() {
  return zone()->CloneVector(base::VectorOf(scanner_.GetIdentifierString()));
}

void AsmJsParser::SkipSemicolon() {
  if (Check(';')) {
    // Had a semicolon.
  } else if (!Peek('}') && !scanner_.IsPrecededByNewline()) {
    FAIL("Expected ;");
  }
}

void AsmJsParser::Begin(AsmJsScanner::token_t label) {
  BareBegin(BlockKind::kRegular, label);
  current_function_builder_->EmitWithU8(kExprBlock, kVoidCode);
}

void AsmJsParser::Loop(AsmJsScanner::token_t label) {
  BareBegin(BlockKind::kLoop, label);
  size_t position = scanner_.Position();
  current_function_builder_->AddAsmWasmOffset(position, position);
  current_function_builder_->EmitWithU8(kExprLoop, kVoidCode);
}

void AsmJsParser::End() {
  BareEnd();
  current_function_builder_->Emit(kExprEnd);
}

void AsmJsParser::BareBegin(BlockKind kind, AsmJsScanner::token_t label) {
  BlockInfo info;
  info.kind = kind;
  info.label = label;
  block_stack_.push_back(info);
}

void AsmJsParser::BareEnd() {
  DCHECK_GT(block_stack_.size(), 0);
  block_stack_.pop_back();
}

int AsmJsParser::FindContinueLabelDepth(AsmJsScanner::token_t label) {
  int count = 0;
  for (auto it = block_stack_.rbegin(); it != block_stack_.rend();
       ++it, ++count) {
    // A 'continue' statement targets ...
    //  - The innermost {kLoop} block if no label is given.
    //  - The matching {kLoop} block (when a label is provided).
    if (it->kind == BlockKind::kLoop &&
        (label == kTokenNone || it->label == label)) {
      return count;
    }
  }
  return -1;
}

int AsmJsParser::FindBreakLabelDepth(AsmJsScanner::token_t label) {
  int count = 0;
  for (auto it = block_stack_.rbegin(); it != block_stack_.rend();
       ++it, ++count) {
    // A 'break' statement targets ...
    //  - The innermost {kRegular} block if no label is given.
    //  - The matching {kRegular} or {kNamed} block (when a label is provided).
    if ((it->kind == BlockKind::kRegular &&
         (label == kTokenNone || it->label == label)) ||
        (it->kind == BlockKind::kNamed && it->label == label)) {
      return count;
    }
  }
  return -1;
}

// 6.1 ValidateModule
void AsmJsParser::ValidateModule() {
  RECURSE(ValidateModuleParameters());
  EXPECT_TOKEN('{');
  EXPECT_TOKEN(TOK(UseAsm));
  RECURSE(SkipSemicolon());
  RECURSE(ValidateModuleVars());
  while (Peek(TOK(function))) {
    RECURSE(ValidateFunction());
  }
  while (Peek(TOK(var))) {
    RECURSE(ValidateFunctionTable());
  }
  RECURSE(ValidateExport());
  RECURSE(SkipSemicolon());
  EXPECT_TOKEN('}');

  // Check that all functions were eventually defined.
  for (auto& info : global_var_info_.SubVector(0, num_globals_)) {
    if (info.kind == VarKind::kFunction && !info.function_defined) {
      FAIL("Undefined function");
    }
    if (info.kind == VarKind::kTable && !info.function_defined) {
      FAIL("Undefined function table");
    }
    if (info.kind == VarKind::kImportedFunction && !info.function_defined) {
      // For imported functions without a single call site, we insert a dummy
      // import here to preserve the fact that there actually was an import.
      FunctionSig* void_void_sig = FunctionSig::Builder(zone(), 0, 0).Get();
      module_builder_->AddImport(info.import->function_name, void_void_sig);
    }
  }

  // Add start function to initialize things.
  WasmFunctionBuilder* start = module_builder_->AddFunction();
  module_builder_->MarkStartFunction(start);
  for (auto& global_import : global_imports_) {
    uint32_t import_index = module_builder_->AddGlobalImport(
        global_import.import_name, global_import.value_type,
        false /* mutability */);
    start->EmitWithU32V(kExprGlobalGet, import_index);
    start->EmitWithU32V(kExprGlobalSet, VarIndex(global_import.var_info));
  }
  start->Emit(kExprEnd);
  FunctionSig::Builder b(zone(), 0, 0);
  start->SetSignature(b.Get());
}

// 6.1 ValidateModule - parameters
void AsmJsParser::ValidateModuleParameters() {
  EXPECT_TOKEN('(');
  stdlib_name_ = 0;
  foreign_name_ = 0;
  heap_name_ = 0;
  if (!Peek(')')) {
    if (!scanner_.IsGlobal()) {
      FAIL("Expected stdlib parameter");
    }
    stdlib_name_ = Consume();
    if (!Peek(')')) {
      EXPECT_TOKEN(',');
      if (!scanner_.IsGlobal()) {
        FAIL("Expected foreign parameter");
      }
      foreign_name_ = Consume();
      if (stdlib_name_ == foreign_name_) {
        FAIL("Duplicate parameter name");
      }
      if (!Peek(')')) {
        EXPECT_TOKEN(',');
        if (!scanner_.IsGlobal()) {
          FAIL("Expected heap parameter");
        }
        heap_name_ = Consume();
        if (heap_name_ == stdlib_name_ || heap_name_ == foreign_name_) {
          FAIL("Duplicate parameter name");
        }
      }
    }
  }
  EXPECT_TOKEN(')');
}

// 6.1 ValidateModule - variables
void AsmJsParser::ValidateModuleVars() {
  while (Peek(TOK(var)) || Peek(TOK(const))) {
    bool mutable_variable = true;
    if (Check(TOK(var))) {
      // Had a var.
    } else {
      EXPECT_TOKEN(TOK(const));
      mutable_variable = false;
    }
    for (;;) {
      RECURSE(ValidateModuleVar(mutable_variable));
      if (Check(',')) {
        continue;
      }
      break;
    }
    SkipSemicolon();
  }
}

// 6.1 ValidateModule - one variable
void AsmJsParser::ValidateModuleVar(bool mutable_variable) {
  if (!scanner_.IsGlobal()) {
    FAIL("Expected identifier");
  }
  AsmJsScanner::token_t identifier = Consume();
  if (identifier == stdlib_name_ || identifier == foreign_name_ ||
      identifier == heap_name_) {
    FAIL("Cannot shadow parameters");
  }
  VarInfo* info = GetVarInfo(identifier);
  if (info->kind != VarKind::kUnused) {
    FAIL("Redefinition of variable");
  }
  EXPECT_TOKEN('=');
  double dvalue = 0.0;
  uint32_t uvalue = 0;
  if (CheckForDouble(&dvalue)) {
    DeclareGlobal(info, mutable_variable, AsmType::Double(), kWasmF64,
                  WasmInitExpr(dvalue));
  } else if (CheckForUnsigned(&uvalue)) {
    if (uvalue > 0x7FFFFFFF) {
      FAIL("Numeric literal out of range");
    }
    DeclareGlobal(info, mutable_variable,
                  mutable_variable ? AsmType::Int() : AsmType::Signed(),
                  kWasmI32, WasmInitExpr(static_cast<int32_t>(uvalue)));
  } else if (Check('-')) {
    if (CheckForDouble(&dvalue)) {
      DeclareGlobal(info, mutable_variable, AsmType::Double(), kWasmF64,
                    WasmInitExpr(-dvalue));
    } else if (CheckForUnsigned(&uvalue)) {
      if (uvalue > 0x7FFFFFFF) {
        FAIL("Numeric literal out of range");
      }
      if (uvalue == 0) {
        // '-0' is treated as float.
        DeclareGlobal(info, mutable_variable, AsmType::Float(), kWasmF32,
                      WasmInitExpr(-0.f));
      } else {
        DeclareGlobal(info, mutable_variable,
                      mutable_variable ? AsmType::Int() : AsmType::Signed(),
                      kWasmI32, WasmInitExpr(-static_cast<int32_t>(uvalue)));
      }
    } else {
      FAIL("Expected numeric literal");
    }
  } else if (Check(TOK(new))) {
    RECURSE(ValidateModuleVarNewStdlib(info));
  } else if (Check(stdlib_name_)) {
    EXPECT_TOKEN('.');
    RECURSE(ValidateModuleVarStdlib(info));
  } else if (Peek(foreign_name_) || Peek('+')) {
    RECURSE(ValidateModuleVarImport(info, mutable_variable));
  } else if (scanner_.IsGlobal()) {
    RECURSE(ValidateModuleVarFromGlobal(info, mutable_variable));
  } else {
    FAIL("Bad variable declaration");
  }
}

// 6.1 ValidateModule - global float declaration
void AsmJsParser::ValidateModuleVarFromGlobal(VarInfo* info,
                                              bool mutable_variable) {
  VarInfo* src_info = GetVarInfo(Consume());
  if (!src_info->type->IsA(stdlib_fround_)) {
    if (src_info->mutable_variable) {
      FAIL("Can only use immutable variables in global definition");
    }
    if (mutable_variable) {
      FAIL("Can only define immutable variables with other immutables");
    }
    if (!src_info->type->IsA(AsmType::Int()) &&
        !src_info->type->IsA(AsmType::Float()) &&
        !src_info->type->IsA(AsmType::Double())) {
      FAIL("Expected int, float, double, or fround for global definition");
    }
    info->kind = VarKind::kGlobal;
    info->type = src_info->type;
    info->index = src_info->index;
    info->mutable_variable = false;
    return;
  }
  EXPECT_TOKEN('(');
  bool negate = false;
  if (Check('-')) {
    negate = true;
  }
  double dvalue = 0.0;
  uint32_t uvalue = 0;
  if (CheckForDouble(&dvalue)) {
    if (negate) {
      dvalue = -dvalue;
    }
    DeclareGlobal(info, mutable_variable, AsmType::Float(), kWasmF32,
                  WasmInitExpr(DoubleToFloat32(dvalue)));
  } else if (CheckForUnsigned(&uvalue)) {
    dvalue = uvalue;
    if (negate) {
      dvalue = -dvalue;
    }
    DeclareGlobal(info, mutable_variable, AsmType::Float(), kWasmF32,
                  WasmInitExpr(static_cast<float>(dvalue)));
  } else {
    FAIL("Expected numeric literal");
  }
  EXPECT_TOKEN(')');
}

// 6.1 ValidateModule - foreign imports
void AsmJsParser::ValidateModuleVarImport(VarInfo* info,
                                          bool mutable_variable) {
  if (Check('+')) {
    EXPECT_TOKEN(foreign_name_);
    EXPECT_TOKEN('.');
    base::Vector<const char> name = CopyCurrentIdentifierString();
    AddGlobalImport(name, AsmType::Double(), kWasmF64, mutable_variable, info);
    scanner_.Next();
  } else {
    EXPECT_TOKEN(foreign_name_);
    EXPECT_TOKEN('.');
    base::Vector<const char> name = CopyCurrentIdentifierString();
    scanner_.Next();
    if (Check('|')) {
      if (!CheckForZero()) {
        FAIL("Expected |0 type annotation for foreign integer import");
      }
      AddGlobalImport(name, AsmType::Int(), kWasmI32, mutable_variable, info);
    } else {
      info->kind = VarKind::kImportedFunction;
      info->import = zone()->New<FunctionImportInfo>(name, zone());
      info->mutable_variable = false;
    }
  }
}

// 6.1 ValidateModule - one variable
// 9 - Standard Library - heap types
void AsmJsParser::ValidateModuleVarNewStdlib(VarInfo* info) {
  EXPECT_TOKEN(stdlib_name_);
  EXPECT_TOKEN('.');
  switch (Consume()) {
#define V(name, _junk1, _junk2, _junk3)                          \
  case TOK(name):                                                \
    DeclareStdlibFunc(info, VarKind::kSpecial, AsmType::name()); \
    stdlib_uses_.Add(StandardMember::k##name);                   \
    break;
    STDLIB_ARRAY_TYPE_LIST(V)
#undef V
    default:
      FAIL("Expected ArrayBuffer view");
  }
  EXPECT_TOKEN('(');
  EXPECT_TOKEN(heap_name_);
  EXPECT_TOKEN(')');
}

// 6.1 ValidateModule - one variable
// 9 - Standard Library
void AsmJsParser::ValidateModuleVarStdlib(VarInfo* info) {
  if (Check(TOK(Math))) {
    EXPECT_TOKEN('.');
    switch (Consume()) {
#define V(name, const_value)                                \
  case TOK(name):                                           \
    DeclareGlobal(info, false, AsmType::Double(), kWasmF64, \
                  WasmInitExpr(const_value));               \
    stdlib_uses_.Add(StandardMember::kMath##name);          \
    break;
      STDLIB_MATH_VALUE_LIST(V)
#undef V
#define V(name, Name, op, sig)                                      \
  case TOK(name):                                                   \
    DeclareStdlibFunc(info, VarKind::kMath##Name, stdlib_##sig##_); \
    stdlib_uses_.Add(StandardMember::kMath##Name);                  \
    break;
      STDLIB_MATH_FUNCTION_LIST(V)
#undef V
      default:
        FAIL("Invalid member of stdlib.Math");
    }
  } else if (Check(TOK(Infinity))) {
    DeclareGlobal(info, false, AsmType::Double(), kWasmF64,
                  WasmInitExpr(std::numeric_limits<double>::infinity()));
    stdlib_uses_.Add(StandardMember::kInfinity);
  } else if (Check(TOK(NaN))) {
    DeclareGlobal(info, false, AsmType::Double(), kWasmF64,
                  WasmInitExpr(std::numeric_limits<double>::quiet_NaN()));
    stdlib_uses_.Add(StandardMember::kNaN);
  } else {
    FAIL("Invalid member of stdlib");
  }
}

// 6.2 ValidateExport
void AsmJsParser::ValidateExport() {
  // clang-format off
  EXPECT_TOKEN(TOK(return));
  // clang-format on
  if (Check('{')) {
    for (;;) {
      base::Vector<const char> name = CopyCurrentIdentifierString();
      if (!scanner_.IsGlobal() && !scanner_.IsLocal()) {
        FAIL("Illegal export name");
      }
      Consume();
      EXPECT_TOKEN(':');
      if (!scanner_.IsGlobal()) {
        FAIL("Expected function name");
      }
      VarInfo* info = GetVarInfo(Consume());
      if (info->kind != VarKind::kFunction) {
        FAIL("Expected function");
      }
      module_builder_->AddExport(name, info->function_builder);
      if (Check(',')) {
        if (!Peek('}')) {
          continue;
        }
      }
      break;
    }
    EXPECT_TOKEN('}');
  } else {
    if (!scanner_.IsGlobal()) {
      FAIL("Single function export must be a function name");
    }
    VarInfo* info = GetVarInfo(Consume());
    if (info->kind != VarKind::kFunction) {
      FAIL("Single function export must be a function");
    }
    module_builder_->AddExport(base::CStrVector(AsmJs::kSingleFunctionName),
                               info->function_builder);
  }
}

// 6.3 ValidateFunctionTable
void AsmJsParser::ValidateFunctionTable() {
  EXPECT_TOKEN(TOK(var));
  if (!scanner_.IsGlobal()) {
    FAIL("Expected table name");
  }
  VarInfo* table_info = GetVarInfo(Consume());
  if (table_info->kind == VarKind::kTable) {
    if (table_info->function_defined) {
      FAIL("Function table redefined");
    }
    table_info->function_defined = true;
  } else if (table_info->kind != VarKind::kUnused) {
    FAIL("Function table name collides");
  }
  EXPECT_TOKEN('=');
  EXPECT_TOKEN('[');
  uint64_t count = 0;
  for (;;) {
    if (!scanner_.IsGlobal()) {
      FAIL("Expected function name");
    }
    VarInfo* info = GetVarInfo(Consume());
    if (info->kind != VarKind::kFunction) {
      FAIL("Expected function");
    }
    // Only store the function into a table if we used the table somewhere
    // (i.e. tables are first seen at their use sites and allocated there).
    if (table_info->kind == VarKind::kTable) {
      if (count >= static_cast<uint64_t>(table_info->mask) + 1) {
        FAIL("Exceeded function table size");
      }
      if (!info->type->IsA(table_info->type)) {
        FAIL("Function table definition doesn't match use");
      }
      module_builder_->SetIndirectFunction(
          0, static_cast<uint32_t>(table_info->index + count), info->index,
          WasmModuleBuilder::WasmElemSegment::kRelativeToDeclaredFunctions);
    }
    ++count;
    if (Check(',')) {
      if (!Peek(']')) {
        continue;
      }
    }
    break;
  }
  EXPECT_TOKEN(']');
  if (table_info->kind == VarKind::kTable &&
      count != static_cast<uint64_t>(table_info->mask) + 1) {
    FAIL("Function table size does not match uses");
  }
  SkipSemicolon();
}

// 6.4 ValidateFunction
void AsmJsParser::ValidateFunction() {
  // Remember position of the 'function' token as start position.
  size_t function_start_position = scanner_.Position();

  EXPECT_TOKEN(TOK(function));
  if (!scanner_.IsGlobal()) {
    FAIL("Expected function name");
  }

  base::Vector<const char> function_name_str = CopyCurrentIdentifierString();
  AsmJsScanner::token_t function_name = Consume();
  VarInfo* function_info = GetVarInfo(function_name);
  if (function_info->kind == VarKind::kUnused) {
    function_info->kind = VarKind::kFunction;
    function_info->function_builder = module_builder_->AddFunction();
    function_info->index = function_info->function_builder->func_index();
    function_info->mutable_variable = false;
  } else if (function_info->kind != VarKind::kFunction) {
    FAIL("Function name collides with variable");
  } else if (function_info->function_defined) {
    FAIL("Function redefined");
  }

  function_info->function_defined = true;
  function_info->function_builder->SetName(function_name_str);
  current_function_builder_ = function_info->function_builder;
  return_type_ = nullptr;

  // Record start of the function, used as position for the stack check.
  current_function_builder_->SetAsmFunctionStartPosition(
      function_start_position);

  CachedVector<AsmType*> params(&cached_asm_type_p_vectors_);
  ValidateFunctionParams(&params);

  // Check against limit on number of parameters.
  if (params.size() > kV8MaxWasmFunctionParams) {
    FAIL("Number of parameters exceeds internal limit");
  }

  CachedVector<ValueType> locals(&cached_valuetype_vectors_);
  ValidateFunctionLocals(params.size(), &locals);

  function_temp_locals_offset_ = static_cast<uint32_t>(
      params.size() + locals.size());
  function_temp_locals_used_ = 0;
  function_temp_locals_depth_ = 0;

  bool last_statement_is_return = false;
  while (!failed_ && !Peek('}')) {
    // clang-format off
    last_statement_is_return = Peek(TOK(return));
    // clang-format on
    RECURSE(ValidateStatement());
  }

  size_t function_end_position = scanner_.Position() + 1;

  EXPECT_TOKEN('}');

  if (!last_statement_is_return) {
    if (return_type_ == nullptr) {
      return_type_ = AsmType::Void();
    } else if (!return_type_->IsA(AsmType::Void())) {
      FAIL("Expected return at end of non-void function");
    }
  }
  DCHECK_NOT_NULL(return_type_);

  // TODO(bradnelson): WasmModuleBuilder can't take this in the right order.
  //                   We should fix that so we can use it instead.
  FunctionSig* sig = ConvertSignature(return_type_, params);
  current_function_builder_->SetSignature(sig);
  for (auto local : locals) {
    current_function_builder_->AddLocal(local);
  }
  // Add bonus temps.
  for (int i = 0; i < function_temp_locals_used_; ++i) {
    current_function_builder_->AddLocal(kWasmI32);
  }

  // Check against limit on number of local variables.
  if (locals.size() + function_temp_locals_used_ > kV8MaxWasmFunctionLocals) {
    FAIL("Number of local variables exceeds internal limit");
  }

  // End function
  current_function_builder_->Emit(kExprEnd);

  // Emit function end position as the last position for this function.
  current_function_builder_->AddAsmWasmOffset(function_end_position,
                                              function_end_position);

  if (current_function_builder_->GetPosition() > kV8MaxWasmFunctionSize) {
    FAIL("Size of function body exceeds internal limit");
  }
  // Record (or validate) function type.
  AsmType* function_type = AsmType::Function(zone(), return_type_);
  for (auto t : params) {
    function_type->AsFunctionType()->AddArgument(t);
  }
  function_info = GetVarInfo(function_name);
  if (function_info->type->IsA(AsmType::None())) {
    DCHECK_EQ(function_info->kind, VarKind::kFunction);
    function_info->type = function_type;
  } else if (!function_type->IsA(function_info->type)) {
    // TODO(bradnelson): Should IsExactly be used here?
    FAIL("Function definition doesn't match use");
  }

  scanner_.ResetLocals();
  std::fill(local_var_info_.begin(), local_var_info_.end(), VarInfo{});
}

// 6.4 ValidateFunction
void AsmJsParser::ValidateFunctionParams(ZoneVector<AsmType*>* params) {
  // TODO(bradnelson): Do this differently so that the scanner doesn't need to
  // have a state transition that needs knowledge of how the scanner works
  // inside.
  scanner_.EnterLocalScope();
  EXPECT_TOKEN('(');
  CachedVector<AsmJsScanner::token_t> function_parameters(
      &cached_token_t_vectors_);
  while (!failed_ && !Peek(')')) {
    if (!scanner_.IsLocal()) {
      FAIL("Expected parameter name");
    }
    function_parameters.push_back(Consume());
    if (!Peek(')')) {
      EXPECT_TOKEN(',');
    }
  }
  EXPECT_TOKEN(')');
  scanner_.EnterGlobalScope();
  EXPECT_TOKEN('{');
  // 5.1 Parameter Type Annotations
  for (auto p : function_parameters) {
    EXPECT_TOKEN(p);
    EXPECT_TOKEN('=');
    VarInfo* info = GetVarInfo(p);
    if (info->kind != VarKind::kUnused) {
      FAIL("Duplicate parameter name");
    }
    if (Check(p)) {
      EXPECT_TOKEN('|');
      if (!CheckForZero()) {
        FAIL("Bad integer parameter annotation.");
      }
      info->kind = VarKind::kLocal;
      info->type = AsmType::Int();
      info->index = static_cast<uint32_t>(params->size());
      params->push_back(AsmType::Int());
    } else if (Check('+')) {
      EXPECT_TOKEN(p);
      info->kind = VarKind::kLocal;
      info->type = AsmType::Double();
      info->index = static_cast<uint32_t>(params->size());
      params->push_back(AsmType::Double());
    } else {
      if (!scanner_.IsGlobal() ||
          !GetVarInfo(Consume())->type->IsA(stdlib_fround_)) {
        FAIL("Expected fround");
      }
      EXPECT_TOKEN('(');
      EXPECT_TOKEN(p);
      EXPECT_TOKEN(')');
      info->kind = VarKind::kLocal;
      info->type = AsmType::Float();
      info->index = static_cast<uint32_t>(params->size());
      params->push_back(AsmType::Float());
    }
    SkipSemicolon();
  }
}

// 6.4 ValidateFunction - locals
void AsmJsParser::ValidateFunctionLocals(size_t param_count,
                                         ZoneVector<ValueType>* locals) {
  DCHECK(locals->empty());
  // Local Variables.
  while (Peek(TOK(var))) {
    scanner_.EnterLocalScope();
    EXPECT_TOKEN(TOK(var));
    scanner_.EnterGlobalScope();
    for (;;) {
      if (!scanner_.IsLocal()) {
        FAIL("Expected local variable identifier");
      }
      VarInfo* info = GetVarInfo(Consume());
      if (info->kind != VarKind::kUnused) {
        FAIL("Duplicate local variable name");
      }
      // Store types.
      EXPECT_TOKEN('=');
      double dvalue = 0.0;
      uint32_t uvalue = 0;
      if (Check('-')) {
        if (CheckForDouble(&dvalue)) {
          info->kind = VarKind::kLocal;
          info->type = AsmType::Double();
          info->index = static_cast<uint32_t>(param_count + locals->size());
          locals->push_back(kWasmF64);
          current_function_builder_->EmitF64Const(-dvalue);
          current_function_builder_->EmitSetLocal(info->index);
        } else if (CheckForUnsigned(&uvalue)) {
          if (uvalue > 0x7FFFFFFF) {
            FAIL("Numeric literal out of range");
          }
          info->kind = VarKind::kLocal;
          info->type = AsmType::Int();
          info->index = static_cast<uint32_t>(param_count + locals->size());
          locals->push_back(kWasmI32);
          int32_t value = -static_cast<int32_t>(uvalue);
          current_function_builder_->EmitI32Const(value);
          current_function_builder_->EmitSetLocal(info->index);
        } else {
          FAIL("Expected variable initial value");
        }
      } else if (scanner_.IsGlobal()) {
        VarInfo* sinfo = GetVarInfo(Consume());
        if (sinfo->kind == VarKind::kGlobal) {
          if (sinfo->mutable_variable) {
            FAIL("Initializing from global requires const variable");
          }
          info->kind = VarKind::kLocal;
          info->type = sinfo->type;
          info->index = static_cast<uint32_t>(param_count + locals->size());
          if (sinfo->type->IsA(AsmType::Int())) {
            locals->push_back(kWasmI32);
          } else if (sinfo->type->IsA(AsmType::Float())) {
            locals->push_back(kWasmF32);
          } else if (sinfo->type->IsA(AsmType::Double())) {
            locals->push_back(kWasmF64);
          } else {
            FAIL("Bad local variable definition");
          }
          current_function_builder_->EmitWithU32V(kExprGlobalGet,
                                                  VarIndex(sinfo));
          current_function_builder_->EmitSetLocal(info->index);
        } else if (sinfo->type->IsA(stdlib_fround_)) {
          EXPECT_TOKEN('(');
          bool negate = false;
          if (Check('-')) {
            negate = true;
          }
          if (CheckForDouble(&dvalue)) {
            info->kind = VarKind::kLocal;
            info->type = AsmType::Float();
            info->index = static_cast<uint32_t>(param_count + locals->size());
            locals->push_back(kWasmF32);
            if (negate) {
              dvalue = -dvalue;
            }
            float fvalue = DoubleToFloat32(dvalue);
            current_function_builder_->EmitF32Const(fvalue);
            current_function_builder_->EmitSetLocal(info->index);
          } else if (CheckForUnsigned(&uvalue)) {
            if (uvalue > 0x7FFFFFFF) {
              FAIL("Numeric literal out of range");
            }
            info->kind = VarKind::kLocal;
            info->type = AsmType::Float();
            info->index = static_cast<uint32_t>(param_count + locals->size());
            locals->push_back(kWasmF32);
            int32_t value = static_cast<int32_t>(uvalue);
            if (negate) {
              value = -value;
            }
            float fvalue = static_cast<float>(value);
            current_function_builder_->EmitF32Const(fvalue);
            current_function_builder_->EmitSetLocal(info->index);
          } else {
            FAIL("Expected variable initial value");
          }
          EXPECT_TOKEN(')');
        } else {
          FAIL("expected fround or const global");
        }
      } else if (CheckForDouble(&dvalue)) {
        info->kind = VarKind::kLocal;
        info->type = AsmType::Double();
        info->index = static_cast<uint32_t>(param_count + locals->size());
        locals->push_back(kWasmF64);
        current_function_builder_->EmitF64Const(dvalue);
        current_function_builder_->EmitSetLocal(info->index);
      } else if (CheckForUnsigned(&uvalue)) {
        info->kind = VarKind::kLocal;
        info->type = AsmType::Int();
        info->index = static_cast<uint32_t>(param_count + locals->size());
        locals->push_back(kWasmI32);
        int32_t value = static_cast<int32_t>(uvalue);
        current_function_builder_->EmitI32Const(value);
        current_function_builder_->EmitSetLocal(info->index);
      } else {
        FAIL("Expected variable initial value");
      }
      if (!Peek(',')) {
        break;
      }
      scanner_.EnterLocalScope();
      EXPECT_TOKEN(',');
      scanner_.EnterGlobalScope();
    }
    SkipSemicolon();
  }
}

// 6.5 ValidateStatement
void AsmJsParser::ValidateStatement() {
  call_coercion_ = nullptr;
  if (Peek('{')) {
    RECURSE(Block());
  } else if (Peek(';')) {
    RECURSE(EmptyStatement());
  } else if (Peek(TOK(if))) {
    RECURSE(IfStatement());
    // clang-format off
  } else if (Peek(TOK(return))) {
    // clang-format on
    RECURSE(ReturnStatement());
  } else if (IterationStatement()) {
    // Handled in IterationStatement.
  } else if (Peek(TOK(break))) {
    RECURSE(BreakStatement());
  } else if (Peek(TOK(continue))) {
    RECURSE(ContinueStatement());
  } else if (Peek(TOK(switch))) {
    RECURSE(SwitchStatement());
  } else {
    RECURSE(ExpressionStatement());
  }
}

// 6.5.1 Block
void AsmJsParser::Block() {
  bool can_break_to_block = pending_label_ != 0;
  if (can_break_to_block) {
    BareBegin(BlockKind::kNamed, pending_label_);
    current_function_builder_->EmitWithU8(kExprBlock, kVoidCode);
  }
  pending_label_ = 0;
  EXPECT_TOKEN('{');
  while (!failed_ && !Peek('}')) {
    RECURSE(ValidateStatement());
  }
  EXPECT_TOKEN('}');
  if (can_break_to_block) {
    End();
  }
}

// 6.5.2 ExpressionStatement
void AsmJsParser::ExpressionStatement() {
  if (scanner_.IsGlobal() || scanner_.IsLocal()) {
    // NOTE: Both global or local identifiers can also be used as labels.
    scanner_.Next();
    if (Peek(':')) {
      scanner_.Rewind();
      RECURSE(LabelledStatement());
      return;
    }
    scanner_.Rewind();
  }
  AsmType* ret;
  RECURSE(ret = ValidateExpression());
  if (!ret->IsA(AsmType::Void())) {
    current_function_builder_->Emit(kExprDrop);
  }
  SkipSemicolon();
}

// 6.5.3 EmptyStatement
void AsmJsParser::EmptyStatement() { EXPECT_TOKEN(';'); }

// 6.5.4 IfStatement
void AsmJsParser::IfStatement() {
  EXPECT_TOKEN(TOK(if));
  EXPECT_TOKEN('(');
  RECURSE(Expression(AsmType::Int()));
  EXPECT_TOKEN(')');
  BareBegin(BlockKind::kOther);
  current_function_builder_->EmitWithU8(kExprIf, kVoidCode);
  RECURSE(ValidateStatement());
  if (Check(TOK(else))) {
    current_function_builder_->Emit(kExprElse);
    RECURSE(ValidateStatement());
  }
  current_function_builder_->Emit(kExprEnd);
  BareEnd();
}

// 6.5.5 ReturnStatement
void AsmJsParser::ReturnStatement() {
  // clang-format off
  EXPECT_TOKEN(TOK(return));
  // clang-format on
  if (!Peek(';') && !Peek('}')) {
    // TODO(bradnelson): See if this can be factored out.
    AsmType* ret;
    RECURSE(ret = Expression(return_type_));
    if (ret->IsA(AsmType::Double())) {
      return_type_ = AsmType::Double();
    } else if (ret->IsA(AsmType::Float())) {
      return_type_ = AsmType::Float();
    } else if (ret->IsA(AsmType::Signed())) {
      return_type_ = AsmType::Signed();
    } else {
      FAIL("Invalid return type");
    }
  } else if (return_type_ == nullptr) {
    return_type_ = AsmType::Void();
  } else if (!return_type_->IsA(AsmType::Void())) {
    FAIL("Invalid void return type");
  }
  current_function_builder_->Emit(kExprReturn);
  SkipSemicolon();
}

// 6.5.6 IterationStatement
bool AsmJsParser::IterationStatement() {
  if (Peek(TOK(while))) {
    WhileStatement();
  } else if (Peek(TOK(do))) {
    DoStatement();
  } else if (Peek(TOK(for))) {
    ForStatement();
  } else {
    return false;
  }
  return true;
}

// 6.5.6 IterationStatement - while
void AsmJsParser::WhileStatement() {
  // a: block {
  Begin(pending_label_);
  //   b: loop {
  Loop(pending_label_);
  pending_label_ = 0;
  EXPECT_TOKEN(TOK(while));
  EXPECT_TOKEN('(');
  RECURSE(Expression(AsmType::Int()));
  EXPECT_TOKEN(')');
  //     if (!CONDITION) break a;
  current_function_builder_->Emit(kExprI32Eqz);
  current_function_builder_->EmitWithU8(kExprBrIf, 1);
  //     BODY
  RECURSE(ValidateStatement());
  //     continue b;
  current_function_builder_->EmitWithU8(kExprBr, 0);
  End();
  //   }
  // }
  End();
}

// 6.5.6 IterationStatement - do
void AsmJsParser::DoStatement() {
  // a: block {
  Begin(pending_label_);
  //   b: loop {
  Loop();
  //     c: block {  // but treated like loop so continue works
  BareBegin(BlockKind::kLoop, pending_label_);
  current_function_builder_->EmitWithU8(kExprBlock, kVoidCode);
  pending_label_ = 0;
  EXPECT_TOKEN(TOK(do));
  //       BODY
  RECURSE(ValidateStatement());
  EXPECT_TOKEN(TOK(while));
  End();
  //     }  // end c
  EXPECT_TOKEN('(');
  RECURSE(Expression(AsmType::Int()));
  //     if (!CONDITION) break a;
  current_function_builder_->Emit(kExprI32Eqz);
  current_function_builder_->EmitWithU8(kExprBrIf, 1);
  //     continue b;
  current_function_builder_->EmitWithU8(kExprBr, 0);
  EXPECT_TOKEN(')');
  //   }  // end b
  End();
  // }  // end a
  End();
  SkipSemicolon();
}

// 6.5.6 IterationStatement - for
void AsmJsParser::ForStatement() {
  EXPECT_TOKEN(TOK(for));
  EXPECT_TOKEN('(');
  if (!Peek(';')) {
    AsmType* ret;
    RECURSE(ret = Expression(nullptr));
    if (!ret->IsA(AsmType::Void())) {
      current_function_builder_->Emit(kExprDrop);
    }
  }
  EXPECT_TOKEN(';');
  // a: block {
  Begin(pending_label_);
  //   b: loop {
  Loop();
  //     c: block {  // but treated like loop so continue works
  BareBegin(BlockKind::kLoop, pending_label_);
  current_function_builder_->EmitWithU8(kExprBlock, kVoidCode);
  pending_label_ = 0;
  if (!Peek(';')) {
    //       if (!CONDITION) break a;
    RECURSE(Expression(AsmType::Int()));
    current_function_builder_->Emit(kExprI32Eqz);
    current_function_builder_->EmitWithU8(kExprBrIf, 2);
  }
  EXPECT_TOKEN(';');
  // Race past INCREMENT
  size_t increment_position = scanner_.Position();
  ScanToClosingParenthesis();
  EXPECT_TOKEN(')');
  //       BODY
  RECURSE(ValidateStatement());
  //     }  // end c
  End();
  //     INCREMENT
  size_t end_position = scanner_.Position();
  scanner_.Seek(increment_position);
  if (!Peek(')')) {
    RECURSE(Expression(nullptr));
    // NOTE: No explicit drop because below break is an implicit drop.
  }
  //     continue b;
  current_function_builder_->EmitWithU8(kExprBr, 0);
  scanner_.Seek(end_position);
  //   }  // end b
  End();
  // }  // end a
  End();
}

// 6.5.7 BreakStatement
void AsmJsParser::BreakStatement() {
  EXPECT_TOKEN(TOK(break));
  AsmJsScanner::token_t label_name = kTokenNone;
  if (scanner_.IsGlobal() || scanner_.IsLocal()) {
    // NOTE: Currently using globals/locals for labels too.
    label_name = Consume();
  }
  int depth = FindBreakLabelDepth(label_name);
  if (depth < 0) {
    FAIL("Illegal break");
  }
  current_function_builder_->EmitWithU32V(kExprBr, depth);
  SkipSemicolon();
}

// 6.5.8 ContinueStatement
void AsmJsParser::ContinueStatement() {
  EXPECT_TOKEN(TOK(continue));
  AsmJsScanner::token_t label_name = kTokenNone;
  if (scanner_.IsGlobal() || scanner_.IsLocal()) {
    // NOTE: Currently using globals/locals for labels too.
    label_name = Consume();
  }
  int depth = FindContinueLabelDepth(label_name);
  if (depth < 0) {
    FAIL("Illegal continue");
  }
  current_function_builder_->EmitWithU32V(kExprBr, depth);
  SkipSemicolon();
}

// 6.5.9 LabelledStatement
void AsmJsParser::LabelledStatement() {
  DCHECK(scanner_.IsGlobal() || scanner_.IsLocal());
  // NOTE: Currently using globals/locals for labels too.
  if (pending_label_ != 0) {
    FAIL("Double label unsupported");
  }
  pending_label_ = scanner_.Token();
  scanner_.Next();
  EXPECT_TOKEN(':');
  RECURSE(ValidateStatement());
}

// 6.5.10 SwitchStatement
void AsmJsParser::SwitchStatement() {
  EXPECT_TOKEN(TOK(switch));
  EXPECT_TOKEN('(');
  AsmType* test;
  RECURSE(test = Expression(nullptr));
  if (!test->IsA(AsmType::Signed())) {
    FAIL("Expected signed for switch value");
  }
  EXPECT_TOKEN(')');
  uint32_t tmp = TempVariable(0);
  current_function_builder_->EmitSetLocal(tmp);
  Begin(pending_label_);
  pending_label_ = 0;
  // TODO(bradnelson): Make less weird.
  CachedVector<int32_t> cases(&cached_int_vectors_);
  GatherCases(&cases);
  EXPECT_TOKEN('{');
  size_t count = cases.size() + 1;
  for (size_t i = 0; i < count; ++i) {
    BareBegin(BlockKind::kOther);
    current_function_builder_->EmitWithU8(kExprBlock, kVoidCode);
  }
  int table_pos = 0;
  for (auto c : cases) {
    current_function_builder_->EmitGetLocal(tmp);
    current_function_builder_->EmitI32Const(c);
    current_function_builder_->Emit(kExprI32Eq);
    current_function_builder_->EmitWithU32V(kExprBrIf, table_pos++);
  }
  current_function_builder_->EmitWithU32V(kExprBr, table_pos++);
  while (!failed_ && Peek(TOK(case))) {
    current_function_builder_->Emit(kExprEnd);
    BareEnd();
    RECURSE(ValidateCase());
  }
  current_function_builder_->Emit(kExprEnd);
  BareEnd();
  if (Peek(TOK(default))) {
    RECURSE(ValidateDefault());
  }
  EXPECT_TOKEN('}');
  End();
}

// 6.6. ValidateCase
void AsmJsParser::ValidateCase() {
  EXPECT_TOKEN(TOK(case));
  bool negate = false;
  if (Check('-')) {
    negate = true;
  }
  uint32_t uvalue;
  if (!CheckForUnsigned(&uvalue)) {
    FAIL("Expected numeric literal");
  }
  // TODO(bradnelson): Share negation plumbing.
  if ((negate && uvalue > 0x80000000) || (!negate && uvalue > 0x7FFFFFFF)) {
    FAIL("Numeric literal out of range");
  }
  int32_t value = static_cast<int32_t>(uvalue);
  DCHECK_IMPLIES(negate && uvalue == 0x80000000, value == kMinInt);
  if (negate && value != kMinInt) {
    value = -value;
  }
  EXPECT_TOKEN(':');
  while (!failed_ && !Peek('}') && !Peek(TOK(case)) && !Peek(TOK(default))) {
    RECURSE(ValidateStatement());
  }
}

// 6.7 ValidateDefault
void AsmJsParser::ValidateDefault() {
  EXPECT_TOKEN(TOK(default));
  EXPECT_TOKEN(':');
  while (!failed_ && !Peek('}')) {
    RECURSE(ValidateStatement());
  }
}

// 6.8 ValidateExpression
AsmType* AsmJsParser::ValidateExpression() {
  AsmType* ret;
  RECURSEn(ret = Expression(nullptr));
  return ret;
}

// 6.8.1 Expression
AsmType* AsmJsParser::Expression(AsmType* expected) {
  AsmType* a;
  for (;;) {
    RECURSEn(a = AssignmentExpression());
    if (Peek(',')) {
      if (a->IsA(AsmType::None())) {
        FAILn("Expected actual type");
      }
      if (!a->IsA(AsmType::Void())) {
        current_function_builder_->Emit(kExprDrop);
      }
      EXPECT_TOKENn(',');
      continue;
    }
    break;
  }
  if (expected != nullptr && !a->IsA(expected)) {
    FAILn("Unexpected type");
  }
  return a;
}

// 6.8.2 NumericLiteral
AsmType* AsmJsParser::NumericLiteral() {
  call_coercion_ = nullptr;
  double dvalue = 0.0;
  uint32_t uvalue = 0;
  if (CheckForDouble(&dvalue)) {
    current_function_builder_->EmitF64Const(dvalue);
    return AsmType::Double();
  } else if (CheckForUnsigned(&uvalue)) {
    if (uvalue <= 0x7FFFFFFF) {
      current_function_builder_->EmitI32Const(static_cast<int32_t>(uvalue));
      return AsmType::FixNum();
    } else {
      current_function_builder_->EmitI32Const(static_cast<int32_t>(uvalue));
      return AsmType::Unsigned();
    }
  } else {
    FAILn("Expected numeric literal.");
  }
}

// 6.8.3 Identifier
AsmType* AsmJsParser::Identifier() {
  call_coercion_ = nullptr;
  if (scanner_.IsLocal()) {
    VarInfo* info = GetVarInfo(Consume());
    if (info->kind != VarKind::kLocal) {
      FAILn("Undefined local variable");
    }
    current_function_builder_->EmitGetLocal(info->index);
    return info->type;
  } else if (scanner_.IsGlobal()) {
    VarInfo* info = GetVarInfo(Consume());
    if (info->kind != VarKind::kGlobal) {
      FAILn("Undefined global variable");
    }
    current_function_builder_->EmitWithU32V(kExprGlobalGet, VarIndex(info));
    return info->type;
  }
  UNREACHABLE();
}

// 6.8.4 CallExpression
AsmType* AsmJsParser::CallExpression() {
  AsmType* ret;
  if (scanner_.IsGlobal() &&
      GetVarInfo(scanner_.Token())->type->IsA(stdlib_fround_)) {
    ValidateFloatCoercion();
    return AsmType::Float();
  } else if (scanner_.IsGlobal() &&
             GetVarInfo(scanner_.Token())->type->IsA(AsmType::Heap())) {
    RECURSEn(ret = MemberExpression());
  } else if (Peek('(')) {
    RECURSEn(ret = ParenthesizedExpression());
  } else if (PeekCall()) {
    RECURSEn(ret = ValidateCall());
  } else if (scanner_.IsLocal() || scanner_.IsGlobal()) {
    RECURSEn(ret = Identifier());
  } else {
    RECURSEn(ret = NumericLiteral());
  }
  return ret;
}

// 6.8.5 MemberExpression
AsmType* AsmJsParser::MemberExpression() {
  call_coercion_ = nullptr;
  RECURSEn(ValidateHeapAccess());
  DCHECK_NOT_NULL(heap_access_type_);
  if (Peek('=')) {
    inside_heap_assignment_ = true;
    return heap_access_type_->StoreType();
  } else {
#define V(array_type, wasmload, wasmstore, type)                       \
  if (heap_access_type_->IsA(AsmType::array_type())) {                 \
    current_function_builder_->Emit(kExpr##type##AsmjsLoad##wasmload); \
    return heap_access_type_->LoadType();                              \
  }
    STDLIB_ARRAY_TYPE_LIST(V)
#undef V
    FAILn("Expected valid heap load");
  }
}

// 6.8.6 AssignmentExpression
AsmType* AsmJsParser::AssignmentExpression() {
  AsmType* ret;
  if (scanner_.IsGlobal() &&
      GetVarInfo(scanner_.Token())->type->IsA(AsmType::Heap())) {
    RECURSEn(ret = ConditionalExpression());
    if (Peek('=')) {
      if (!inside_heap_assignment_) {
        FAILn("Invalid assignment target");
      }
      inside_heap_assignment_ = false;
      DCHECK_NOT_NULL(heap_access_type_);
      AsmType* heap_type = heap_access_type_;
      EXPECT_TOKENn('=');
      AsmType* value;
      RECURSEn(value = AssignmentExpression());
      if (!value->IsA(ret)) {
        FAILn("Illegal type stored to heap view");
      }
      ret = value;
      if (heap_type->IsA(AsmType::Float32Array()) &&
          value->IsA(AsmType::DoubleQ())) {
        // Assignment to a float32 heap can be used to convert doubles.
        current_function_builder_->Emit(kExprF32ConvertF64);
        ret = AsmType::FloatQ();
      }
      if (heap_type->IsA(AsmType::Float64Array()) &&
          value->IsA(AsmType::FloatQ())) {
        // Assignment to a float64 heap can be used to convert floats.
        current_function_builder_->Emit(kExprF64ConvertF32);
        ret = AsmType::DoubleQ();
      }
#define V(array_type, wasmload, wasmstore, type)                         \
  if (heap_type->IsA(AsmType::array_type())) {                           \
    current_function_builder_->Emit(kExpr##type##AsmjsStore##wasmstore); \
    return ret;                                                          \
  }
      STDLIB_ARRAY_TYPE_LIST(V)
#undef V
    }
  } else if (scanner_.IsLocal() || scanner_.IsGlobal()) {
    bool is_local = scanner_.IsLocal();
    VarInfo* info = GetVarInfo(scanner_.Token());
    USE(is_local);
    ret = info->type;
    scanner_.Next();
    if (Check('=')) {
      // NOTE: Before this point, this might have been VarKind::kUnused even in
      // valid code, as it might be a label.
      if (info->kind == VarKind::kUnused) {
        FAILn("Undeclared assignment target");
      }
      if (!info->mutable_variable) {
        FAILn("Expected mutable variable in assignment");
      }
      DCHECK(is_local ? info->kind == VarKind::kLocal
                      : info->kind == VarKind::kGlobal);
      AsmType* value;
      RECURSEn(value = AssignmentExpression());
      if (!value->IsA(ret)) {
        FAILn("Type mismatch in assignment");
      }
      if (info->kind == VarKind::kLocal) {
        current_function_builder_->EmitTeeLocal(info->index);
      } else if (info->kind == VarKind::kGlobal) {
        current_function_builder_->EmitWithU32V(kExprGlobalSet, VarIndex(info));
        current_function_builder_->EmitWithU32V(kExprGlobalGet, VarIndex(info));
      } else {
        UNREACHABLE();
      }
      return ret;
    }
    scanner_.Rewind();
    RECURSEn(ret = ConditionalExpression());
  } else {
    RECURSEn(ret = ConditionalExpression());
  }
  return ret;
}

// 6.8.7 UnaryExpression
AsmType* AsmJsParser::UnaryExpression() {
  AsmType* ret;
  if (Check('-')) {
    uint32_t uvalue;
    if (CheckForUnsigned(&uvalue)) {
      if (uvalue == 0) {
        current_function_builder_->EmitF64Const(-0.0);
        ret = AsmType::Double();
      } else if (uvalue <= 0x80000000) {
        // TODO(bradnelson): was supposed to be 0x7FFFFFFF, check errata.
        current_function_builder_->EmitI32Const(
            base::NegateWithWraparound(static_cast<int32_t>(uvalue)));
        ret = AsmType::Signed();
      } else {
        FAILn("Integer numeric literal out of range.");
      }
    } else {
      RECURSEn(ret = UnaryExpression());
      if (ret->IsA(AsmType::Int())) {
        TemporaryVariableScope tmp(this);
        current_function_builder_->EmitSetLocal(tmp.get());
        current_function_builder_->EmitI32Const(0);
        current_function_builder_->EmitGetLocal(tmp.get());
        current_function_builder_->Emit(kExprI32Sub);
        ret = AsmType::Intish();
      } else if (ret->IsA(AsmType::DoubleQ())) {
        current_function_builder_->Emit(kExprF64Neg);
        ret = AsmType::Double();
      } else if (ret->IsA(AsmType::FloatQ())) {
        current_function_builder_->Emit(kExprF32Neg);
        ret = AsmType::Floatish();
      } else {
        FAILn("expected int/double?/float?");
      }
    }
  } else if (Peek('+')) {
    call_coercion_ = AsmType::Double();
    call_coercion_position_ = scanner_.Position();
    scanner_.Next();  // Done late for correct position.
    RECURSEn(ret = UnaryExpression());
    // TODO(bradnelson): Generalize.
    if (ret->IsA(AsmType::Signed())) {
      current_function_builder_->Emit(kExprF64SConvertI32);
      ret = AsmType::Double();
    } else if (ret->IsA(AsmType::Unsigned())) {
      current_function_builder_->Emit(kExprF64UConvertI32);
      ret = AsmType::Double();
    } else if (ret->IsA(AsmType::DoubleQ())) {
      ret = AsmType::Double();
    } else if (ret->IsA(AsmType::FloatQ())) {
      current_function_builder_->Emit(kExprF64ConvertF32);
      ret = AsmType::Double();
    } else {
      FAILn("expected signed/unsigned/double?/float?");
    }
  } else if (Check('!')) {
    RECURSEn(ret = UnaryExpression());
    if (!ret->IsA(AsmType::Int())) {
      FAILn("expected int");
    }
    current_function_builder_->Emit(kExprI32Eqz);
  } else if (Check('~')) {
    if (Check('~')) {
      RECURSEn(ret = UnaryExpression());
      if (ret->IsA(AsmType::Double())) {
        current_function_builder_->Emit(kExprI32AsmjsSConvertF64);
      } else if (ret->IsA(AsmType::FloatQ())) {
        current_function_builder_->Emit(kExprI32AsmjsSConvertF32);
      } else {
        FAILn("expected double or float?");
      }
      ret = AsmType::Signed();
    } else {
      RECURSEn(ret = UnaryExpression());
      if (!ret->IsA(AsmType::Intish())) {
        FAILn("operator ~ expects intish");
      }
      current_function_builder_->EmitI32Const(0xFFFFFFFF);
      current_function_builder_->Emit(kExprI32Xor);
      ret = AsmType::Signed();
    }
  } else {
    RECURSEn(ret = CallExpression());
  }
  return ret;
}

// 6.8.8 MultiplicativeExpression
AsmType* AsmJsParser::MultiplicativeExpression() {
  AsmType* a;
  uint32_t uvalue;
  if (CheckForUnsignedBelow(0x100000, &uvalue)) {
    if (Check('*')) {
      AsmType* type;
      RECURSEn(type = UnaryExpression());
      if (!type->IsA(AsmType::Int())) {
        FAILn("Expected int");
      }
      int32_t value = static_cast<int32_t>(uvalue);
      current_function_builder_->EmitI32Const(value);
      current_function_builder_->Emit(kExprI32Mul);
      return AsmType::Intish();
    } else {
      scanner_.Rewind();
      RECURSEn(a = UnaryExpression());
    }
  } else if (Check('-')) {
    if (!PeekForZero() && CheckForUnsignedBelow(0x100000, &uvalue)) {
      int32_t value = -static_cast<int32_t>(uvalue);
      current_function_builder_->EmitI32Const(value);
      if (Check('*')) {
        AsmType* type;
        RECURSEn(type = UnaryExpression());
        if (!type->IsA(AsmType::Int())) {
          FAILn("Expected int");
        }
        current_function_builder_->Emit(kExprI32Mul);
        return AsmType::Intish();
      }
      a = AsmType::Signed();
    } else {
      scanner_.Rewind();
      RECURSEn(a = UnaryExpression());
    }
  } else {
    RECURSEn(a = UnaryExpression());
  }
  for (;;) {
    if (Check('*')) {
      if (Check('-')) {
        if (!PeekForZero() && CheckForUnsigned(&uvalue)) {
          if (uvalue >= 0x100000) {
            FAILn("Constant multiple out of range");
          }
          if (!a->IsA(AsmType::Int())) {
            FAILn("Integer multiply of expects int");
          }
          int32_t value = -static_cast<int32_t>(uvalue);
          current_function_builder_->EmitI32Const(value);
          current_function_builder_->Emit(kExprI32Mul);
          return AsmType::Intish();
        }
        scanner_.Rewind();
      } else if (CheckForUnsigned(&uvalue)) {
        if (uvalue >= 0x100000) {
          FAILn("Constant multiple out of range");
        }
        if (!a->IsA(AsmType::Int())) {
          FAILn("Integer multiply of expects int");
        }
        int32_t value = static_cast<int32_t>(uvalue);
        current_function_builder_->EmitI32Const(value);
        current_function_builder_->Emit(kExprI32Mul);
        return AsmType::Intish();
      }
      AsmType* b;
      RECURSEn(b = UnaryExpression());
      if (a->IsA(AsmType::DoubleQ()) && b->IsA(AsmType::DoubleQ())) {
        current_function_builder_->Emit(kExprF64Mul);
        a = AsmType::Double();
      } else if (a->IsA(AsmType::FloatQ()) && b->IsA(AsmType::FloatQ())) {
        current_function_builder_->Emit(kExprF32Mul);
        a = AsmType::Floatish();
      } else {
        FAILn("expected doubles or floats");
      }
    } else if (Check('/')) {
      AsmType* b;
      RECURSEn(b = UnaryExpression());
      if (a->IsA(AsmType::DoubleQ()) && b->IsA(AsmType::DoubleQ())) {
        current_function_builder_->Emit(kExprF64Div);
        a = AsmType::Double();
      } else if (a->IsA(AsmType::FloatQ()) && b->IsA(AsmType::FloatQ())) {
        current_function_builder_->Emit(kExprF32Div);
        a = AsmType::Floatish();
      } else if (a->IsA(AsmType::Signed()) && b->IsA(AsmType::Signed())) {
        current_function_builder_->Emit(kExprI32AsmjsDivS);
        a = AsmType::Intish();
      } else if (a->IsA(AsmType::Unsigned()) && b->IsA(AsmType::Unsigned())) {
        current_function_builder_->Emit(kExprI32AsmjsDivU);
        a = AsmType::Intish();
      } else {
        FAILn("expected doubles or floats");
      }
    } else if (Check('%')) {
      AsmType* b;
      RECURSEn(b = UnaryExpression());
      if (a->IsA(AsmType::DoubleQ()) && b->IsA(AsmType::DoubleQ())) {
        current_function_builder_->Emit(kExprF64Mod);
        a = AsmType::Double();
      } else if (a->IsA(AsmType::Signed()) && b->IsA(AsmType::Signed())) {
        current_function_builder_->Emit(kExprI32AsmjsRemS);
        a = AsmType::Intish();
      } else if (a->IsA(AsmType::Unsigned()) && b->IsA(AsmType::Unsigned())) {
        current_function_builder_->Emit(kExprI32AsmjsRemU);
        a = AsmType::Intish();
      } else {
        FAILn("expected doubles or floats");
      }
    } else {
      break;
    }
  }
  return a;
}

// 6.8.9 AdditiveExpression
AsmType* AsmJsParser::AdditiveExpression() {
  AsmType* a;
  RECURSEn(a = MultiplicativeExpression());
  int n = 0;
  for (;;) {
    if (Check('+')) {
      AsmType* b;
      RECURSEn(b = MultiplicativeExpression());
      if (a->IsA(AsmType::Double()) && b->IsA(AsmType::Double())) {
        current_function_builder_->Emit(kExprF64Add);
        a = AsmType::Double();
      } else if (a->IsA(AsmType::FloatQ()) && b->IsA(AsmType::FloatQ())) {
        current_function_builder_->Emit(kExprF32Add);
        a = AsmType::Floatish();
      } else if (a->IsA(AsmType::Int()) && b->IsA(AsmType::Int())) {
        current_function_builder_->Emit(kExprI32Add);
        a = AsmType::Intish();
        n = 2;
      } else if (a->IsA(AsmType::Intish()) && b->IsA(AsmType::Intish())) {
        // TODO(bradnelson): b should really only be Int.
        // specialize intish to capture count.
        ++n;
        if (n > (1 << 20)) {
          FAILn("more than 2^20 additive values");
        }
        current_function_builder_->Emit(kExprI32Add);
      } else {
        FAILn("illegal types for +");
      }
    } else if (Check('-')) {
      AsmType* b;
      RECURSEn(b = MultiplicativeExpression());
      if (a->IsA(AsmType::Double()) && b->IsA(AsmType::Double())) {
        current_function_builder_->Emit(kExprF64Sub);
        a = AsmType::Double();
      } else if (a->IsA(AsmType::FloatQ()) && b->IsA(AsmType::FloatQ())) {
        current_function_builder_->Emit(kExprF32Sub);
        a = AsmType::Floatish();
      } else if (a->IsA(AsmType::Int()) && b->IsA(AsmType::Int())) {
        current_function_builder_->Emit(kExprI32Sub);
        a = AsmType::Intish();
        n = 2;
      } else if (a->IsA(AsmType::Intish()) && b->IsA(AsmType::Intish())) {
        // TODO(bradnelson): b should really only be Int.
        // specialize intish to capture count.
        ++n;
        if (n > (1 << 20)) {
          FAILn("more than 2^20 additive values");
        }
        current_function_builder_->Emit(kExprI32Sub);
      } else {
        FAILn("illegal types for +");
      }
    } else {
      break;
    }
  }
  return a;
}

// 6.8.10 ShiftExpression
AsmType* AsmJsParser::ShiftExpression() {
  AsmType* a = nullptr;
  RECURSEn(a = AdditiveExpression());
  heap_access_shift_position_ = kNoHeapAccessShift;
  // TODO(bradnelson): Implement backtracking to avoid emitting code
  // for the x >>> 0 case (similar to what's there for |0).
  for (;;) {
    switch (scanner_.Token()) {
      case TOK(SAR): {
        EXPECT_TOKENn(TOK(SAR));
        heap_access_shift_position_ = kNoHeapAccessShift;
        // Remember position allowing this shift-expression to be used as part
        // of a heap access operation expecting `a >> n:NumericLiteral`.
        bool imm = false;
        size_t old_pos;
        size_t old_code;
        uint32_t shift_imm;
        if (a->IsA(AsmType::Intish()) && CheckForUnsigned(&shift_imm)) {
          old_pos = scanner_.Position();
          old_code = current_function_builder_->GetPosition();
          scanner_.Rewind();
          imm = true;
        }
        AsmType* b = nullptr;
        RECURSEn(b = AdditiveExpression());
        // Check for `a >> n:NumericLiteral` pattern.
        if (imm && old_pos == scanner_.Position()) {
          heap_access_shift_position_ = old_code;
          heap_access_shift_value_ = shift_imm;
        }
        if (!(a->IsA(AsmType::Intish()) && b->IsA(AsmType::Intish()))) {
          FAILn("Expected intish for operator >>.");
        }
        current_function_builder_->Emit(kExprI32ShrS);
        a = AsmType::Signed();
        continue;
      }
#define HANDLE_CASE(op, opcode, name, result)                        \
  case TOK(op): {                                                    \
    EXPECT_TOKENn(TOK(op));                                          \
    heap_access_shift_position_ = kNoHeapAccessShift;                \
    AsmType* b = nullptr;                                            \
    RECURSEn(b = AdditiveExpression());                              \
    if (!(a->IsA(AsmType::Intish()) && b->IsA(AsmType::Intish()))) { \
      FAILn("Expected intish for operator " #name ".");              \
    }                                                                \
    current_function_builder_->Emit(kExpr##opcode);                  \
    a = AsmType::result();                                           \
    continue;                                                        \
  }
        HANDLE_CASE(SHL, I32Shl, "<<", Signed);
        HANDLE_CASE(SHR, I32ShrU, ">>>", Unsigned);
#undef HANDLE_CASE
      default:
        return a;
    }
  }
}

// 6.8.11 RelationalExpression
AsmType* AsmJsParser::RelationalExpression() {
  AsmType* a = nullptr;
  RECURSEn(a = ShiftExpression());
  for (;;) {
    switch (scanner_.Token()) {
#define HANDLE_CASE(op, sop, uop, dop, fop, name)                             \
  case op: {                                                                  \
    EXPECT_TOKENn(op);                                                        \
    AsmType* b = nullptr;                                                     \
    RECURSEn(b = ShiftExpression());                                          \
    if (a->IsA(AsmType::Signed()) && b->IsA(AsmType::Signed())) {             \
      current_function_builder_->Emit(kExpr##sop);                            \
    } else if (a->IsA(AsmType::Unsigned()) && b->IsA(AsmType::Unsigned())) {  \
      current_function_builder_->Emit(kExpr##uop);                            \
    } else if (a->IsA(AsmType::Double()) && b->IsA(AsmType::Double())) {      \
      current_function_builder_->Emit(kExpr##dop);                            \
    } else if (a->IsA(AsmType::Float()) && b->IsA(AsmType::Float())) {        \
      current_function_builder_->Emit(kExpr##fop);                            \
    } else {                                                                  \
      FAILn("Expected signed, unsigned, double, or float for operator " #name \
            ".");                                                             \
    }                                                                         \
    a = AsmType::Int();                                                       \
    continue;                                                                 \
  }
      HANDLE_CASE('<', I32LtS, I32LtU, F64Lt, F32Lt, "<");
      HANDLE_CASE(TOK(LE), I32LeS, I32LeU, F64Le, F32Le, "<=");
      HANDLE_CASE('>', I32GtS, I32GtU, F64Gt, F32Gt, ">");
      HANDLE_CASE(TOK(GE), I32GeS, I32GeU, F64Ge, F32Ge, ">=");
#undef HANDLE_CASE
      default:
        return a;
    }
  }
}

// 6.8.12 EqualityExpression
AsmType* AsmJsParser::EqualityExpression() {
  AsmType* a = nullptr;
  RECURSEn(a = RelationalExpression());
  for (;;) {
    switch (scanner_.Token()) {
#define HANDLE_CASE(op, sop, uop, dop, fop, name)                             \
  case op: {                                                                  \
    EXPECT_TOKENn(op);                                                        \
    AsmType* b = nullptr;                                                     \
    RECURSEn(b = RelationalExpression());                                     \
    if (a->IsA(AsmType::Signed()) && b->IsA(AsmType::Signed())) {             \
      current_function_builder_->Emit(kExpr##sop);                            \
    } else if (a->IsA(AsmType::Unsigned()) && b->IsA(AsmType::Unsigned())) {  \
      current_function_builder_->Emit(kExpr##uop);                            \
    } else if (a->IsA(AsmType::Double()) && b->IsA(AsmType::Double())) {      \
      current_function_builder_->Emit(kExpr##dop);                            \
    } else if (a->IsA(AsmType::Float()) && b->IsA(AsmType::Float())) {        \
      current_function_builder_->Emit(kExpr##fop);                            \
    } else {                                                                  \
      FAILn("Expected signed, unsigned, double, or float for operator " #name \
            ".");                                                             \
    }                                                                         \
    a = AsmType::Int();                                                       \
    continue;                                                                 \
  }
      HANDLE_CASE(TOK(EQ), I32Eq, I32Eq, F64Eq, F32Eq, "==");
      HANDLE_CASE(TOK(NE), I32Ne, I32Ne, F64Ne, F32Ne, "!=");
#undef HANDLE_CASE
      default:
        return a;
    }
  }
}

// 6.8.13 BitwiseANDExpression
AsmType* AsmJsParser::BitwiseANDExpression() {
  AsmType* a = nullptr;
  RECURSEn(a = EqualityExpression());
  while (Check('&')) {
    AsmType* b = nullptr;
    RECURSEn(b = EqualityExpression());
    if (a->IsA(AsmType::Intish()) && b->IsA(AsmType::Intish())) {
      current_function_builder_->Emit(kExprI32And);
      a = AsmType::Signed();
    } else {
      FAILn("Expected intish for operator &.");
    }
  }
  return a;
}

// 6.8.14 BitwiseXORExpression
AsmType* AsmJsParser::BitwiseXORExpression() {
  AsmType* a = nullptr;
  RECURSEn(a = BitwiseANDExpression());
  while (Check('^')) {
    AsmType* b = nullptr;
    RECURSEn(b = BitwiseANDExpression());
    if (a->IsA(AsmType::Intish()) && b->IsA(AsmType::Intish())) {
      current_function_builder_->Emit(kExprI32Xor);
      a = AsmType::Signed();
    } else {
      FAILn("Expected intish for operator &.");
    }
  }
  return a;
}

// 6.8.15 BitwiseORExpression
AsmType* AsmJsParser::BitwiseORExpression() {
  AsmType* a = nullptr;
  call_coercion_deferred_position_ = scanner_.Position();
  RECURSEn(a = BitwiseXORExpression());
  while (Check('|')) {
    AsmType* b = nullptr;
    // Remember whether the first operand to this OR-expression has requested
    // deferred validation of the |0 annotation.
    // NOTE: This has to happen here to work recursively.
    bool requires_zero =
        AsmType::IsExactly(call_coercion_deferred_, AsmType::Signed());
    call_coercion_deferred_ = nullptr;
    // TODO(bradnelson): Make it prettier.
    bool zero = false;
    size_t old_pos;
    size_t old_code;
    if (a->IsA(AsmType::Intish()) && CheckForZero()) {
      old_pos = scanner_.Position();
      old_code = current_function_builder_->GetPosition();
      scanner_.Rewind();
      zero = true;
    }
    RECURSEn(b = BitwiseXORExpression());
    // Handle |0 specially.
    if (zero && old_pos == scanner_.Position()) {
      current_function_builder_->DeleteCodeAfter(old_code);
      a = AsmType::Signed();
      continue;
    }
    // Anything not matching |0 breaks the lookahead in {ValidateCall}.
    if (requires_zero) {
      FAILn("Expected |0 type annotation for call");
    }
    if (a->IsA(AsmType::Intish()) && b->IsA(AsmType::Intish())) {
      current_function_builder_->Emit(kExprI32Ior);
      a = AsmType::Signed();
    } else {
      FAILn("Expected intish for operator |.");
    }
  }
  DCHECK_NULL(call_coercion_deferred_);
  return a;
}

// 6.8.16 ConditionalExpression
AsmType* AsmJsParser::ConditionalExpression() {
  AsmType* test = nullptr;
  RECURSEn(test = BitwiseORExpression());
  if (Check('?')) {
    if (!test->IsA(AsmType::Int())) {
      FAILn("Expected int in condition of ternary operator.");
    }
    current_function_builder_->EmitWithU8(kExprIf, kI32Code);
    size_t fixup = current_function_builder_->GetPosition() -
                   1;  // Assumes encoding knowledge.
    AsmType* cons = nullptr;
    RECURSEn(cons = AssignmentExpression());
    current_function_builder_->Emit(kExprElse);
    EXPECT_TOKENn(':');
    AsmType* alt = nullptr;
    RECURSEn(alt = AssignmentExpression());
    current_function_builder_->Emit(kExprEnd);
    if (cons->IsA(AsmType::Int()) && alt->IsA(AsmType::Int())) {
      current_function_builder_->FixupByte(fixup, kI32Code);
      return AsmType::Int();
    } else if (cons->IsA(AsmType::Double()) && alt->IsA(AsmType::Double())) {
      current_function_builder_->FixupByte(fixup, kF64Code);
      return AsmType::Double();
    } else if (cons->IsA(AsmType::Float()) && alt->IsA(AsmType::Float())) {
      current_function_builder_->FixupByte(fixup, kF32Code);
      return AsmType::Float();
    } else {
      FAILn("Type mismatch in ternary operator.");
    }
  } else {
    return test;
  }
}

// 6.8.17 ParenthesiedExpression
AsmType* AsmJsParser::ParenthesizedExpression() {
  call_coercion_ = nullptr;
  AsmType* ret;
  EXPECT_TOKENn('(');
  RECURSEn(ret = Expression(nullptr));
  EXPECT_TOKENn(')');
  return ret;
}

// 6.9 ValidateCall
AsmType* AsmJsParser::ValidateCall() {
  AsmType* return_type = call_coercion_;
  call_coercion_ = nullptr;
  size_t call_pos = scanner_.Position();
  size_t to_number_pos = call_coercion_position_;
  bool allow_peek = (call_coercion_deferred_position_ == scanner_.Position());
  AsmJsScanner::token_t function_name = Consume();

  // Distinguish between ordinary function calls and function table calls. In
  // both cases we might be seeing the {function_name} for the first time and
  // hence allocate a {VarInfo} here, all subsequent uses of the same name then
  // need to match the information stored at this point.
  std::optional<TemporaryVariableScope> tmp_scope;
  if (Check('[')) {
    AsmType* index = nullptr;
    RECURSEn(index = EqualityExpression());
    if (!index->IsA(AsmType::Intish())) {
      FAILn("Expected intish index");
    }
    EXPECT_TOKENn('&');
    uint32_t mask = 0;
    if (!CheckForUnsigned(&mask)) {
      FAILn("Expected mask literal");
    }
    if (!base::bits::IsPowerOfTwo(mask + 1)) {
      FAILn("Expected power of 2 mask");
    }
    current_function_builder_->EmitI32Const(mask);
    current_function_builder_->Emit(kExprI32And);
    EXPECT_TOKENn(']');
    VarInfo* function_info = GetVarInfo(function_name);
    if (function_info->kind == VarKind::kUnused) {
      if (module_builder_->NumTables() == 0) {
        module_builder_->AddTable(kWasmFuncRef, 0);
      }
      uint32_t func_index = module_builder_->IncreaseTableMinSize(0, mask + 1);
      if (func_index == std::numeric_limits<uint32_t>::max()) {
        FAILn("Exceeded maximum function table size");
      }
      function_info->kind = VarKind::kTable;
      function_info->mask = mask;
      function_info->index = func_index;
      function_info->mutable_variable = false;
    } else {
      if (function_info->kind != VarKind::kTable) {
        FAILn("Expected call table");
      }
      if (function_info->mask != mask) {
        FAILn("Mask size mismatch");
      }
    }
    current_function_builder_->EmitI32Const(function_info->index);
    current_function_builder_->Emit(kExprI32Add);
    // We have to use a temporary for the correct order of evaluation.
    tmp_scope.emplace(this);
    current_function_builder_->EmitSetLocal(tmp_scope->get());
    // The position of function table calls is after the table lookup.
    call_pos = scanner_.Position();
  } else {
    VarInfo* function_info = GetVarInfo(function_name);
    if (function_info->kind == VarKind::kUnused) {
      function_info->kind = VarKind::kFunction;
      function_info->function_builder = module_builder_->AddFunction();
      function_info->index = function_info->function_builder->func_index();
      function_info->mutable_variable = false;
    } else {
      if (function_info->kind != VarKind::kFunction &&
          function_info->kind < VarKind::kImportedFunction) {
        FAILn("Expected function as call target");
      }
    }
  }

  // Parse argument list and gather types.
  CachedVector<AsmType*> param_types(&cached_asm_type_p_vectors_);
  CachedVector<AsmType*> param_specific_types(&cached_asm_type_p_vectors_);
  EXPECT_TOKENn('(');
  while (!failed_ && !Peek(')')) {
    AsmType* t;
    RECURSEn(t = AssignmentExpression());
    param_specific_types.push_back(t);
    if (t->IsA(AsmType::Int())) {
      param_types.push_back(AsmType::Int());
    } else if (t->IsA(AsmType::Float())) {
      param_types.push_back(AsmType::Float());
    } else if (t->IsA(AsmType::Double())) {
      param_types.push_back(AsmType::Double());
    } else {
      FAILn("Bad function argument type");
    }
    if (!Peek(')')) {
      EXPECT_TOKENn(',');
    }
  }
  EXPECT_TOKENn(')');

  // Reload {VarInfo} after parsing arguments as table might have grown.
  VarInfo* function_info = GetVarInfo(function_name);

  // We potentially use lookahead in order to determine the return type in case
  // it is not yet clear from the call context. Special care has to be taken to
  // ensure the non-contextual lookahead is valid. The following restrictions
  // substantiate the validity of the lookahead implemented below:
  //  - All calls (except stdlib calls) require some sort of type annotation.
  //  - The coercion to "signed" is part of the {BitwiseORExpression}, any
  //    intermittent expressions like parenthesis in `(callsite(..))|0` are
  //    syntactically not considered coercions.
  //  - The coercion to "double" as part of the {UnaryExpression} has higher
  //    precedence and wins in `+callsite(..)|0` cases. Only "float" return
  //    types are overridden in `fround(callsite(..)|0)` expressions.
  //  - Expected coercions to "signed" are flagged via {call_coercion_deferred}
  //    and later on validated as part of {BitwiseORExpression} to ensure they
  //    indeed apply to the current call expression.
  //  - The deferred validation is only allowed if {BitwiseORExpression} did
  //    promise to fulfill the request via {call_coercion_deferred_position}.
  if (allow_peek && Peek('|') &&
      function_info->kind <= VarKind::kImportedFunction &&
      (return_type == nullptr || return_type->IsA(AsmType::Float()))) {
    DCHECK_NULL(call_coercion_deferred_);
    call_coercion_deferred_ = AsmType::Signed();
    to_number_pos = scanner_.Position();
    return_type = AsmType::Signed();
  } else if (return_type == nullptr) {
    to_number_pos = call_pos;  // No conversion.
    return_type = AsmType::Void();
  }

  // Compute function type and signature based on gathered types.
  AsmType* function_type = AsmType::Function(zone(), return_type);
  for (auto t : param_types) {
    function_type->AsFunctionType()->AddArgument(t);
  }
  FunctionSig* sig = ConvertSignature(return_type, param_types);
  ModuleTypeIndex signature_index = module_builder_->AddSignature(sig, true);

  // Emit actual function invocation depending on the kind. At this point we
  // also determined the complete function type and can perform checking against
  // the expected type or update the expected type in case of first occurrence.
  if (function_info->kind == VarKind::kImportedFunction) {
    if (param_types.size() > kV8MaxWasmFunctionParams) {
      FAILn("Number of parameters exceeds internal limit");
    }
    for (auto t : param_specific_types) {
      if (!t->IsA(AsmType::Extern())) {
        FAILn("Imported function args must be type extern");
      }
    }
    if (return_type->IsA(AsmType::Float())) {
      FAILn("Imported function can't be called as float");
    }
    DCHECK_NOT_NULL(function_info->import);
    // TODO(bradnelson): Factor out.
    uint32_t index;
    auto it = function_info->import->cache.find(*sig);
    if (it != function_info->import->cache.end()) {
      index = it->second;
      DCHECK(function_info->function_defined);
    } else {
      index =
          module_builder_->AddImport(function_info->import->function_name, sig);
      function_info->import->cache[*sig] = index;
      function_info->function_defined = true;
    }
    current_function_builder_->AddAsmWasmOffset(call_pos, to_number_pos);
    current_function_builder_->EmitWithU32V(kExprCallFunction, index);
  } else if (function_info->kind > VarKind::kImportedFunction) {
    AsmCallableType* callable = function_info->type->AsCallableType();
    if (!callable) {
      FAILn("Expected callable function");
    }
    // TODO(bradnelson): Refactor AsmType to not need this.
    if (callable->CanBeInvokedWith(return_type, param_specific_types)) {
      // Return type ok.
    } else if (callable->CanBeInvokedWith(AsmType::Float(),
                                          param_specific_types)) {
      return_type = AsmType::Float();
    } else if (callable->CanBeInvokedWith(AsmType::Floatish(),
                                          param_specific_types)) {
      return_type = AsmType::Floatish();
    } else if (callable->CanBeInvokedWith(AsmType::Double(),
                                          param_specific_types)) {
      return_type = AsmType::Double();
    } else if (callable->CanBeInvokedWith(AsmType::Signed(),
                                          param_specific_types)) {
      return_type = AsmType::Signed();
    } else if (callable->CanBeInvokedWith(AsmType::Unsigned(),
                                          param_specific_types)) {
      return_type = AsmType::Unsigned();
    } else {
      FAILn("Function use doesn't match definition");
    }
    switch (function_info->kind) {
#define V(name, Name, op, sig)           \
  case VarKind::kMath##Name:             \
    current_function_builder_->Emit(op); \
    break;
      STDLIB_MATH_FUNCTION_MONOMORPHIC_LIST(V)
#undef V
#define V(name, Name, op, sig)                                    \
  case VarKind::kMath##Name:                                      \
    if (param_specific_types[0]->IsA(AsmType::DoubleQ())) {       \
      current_function_builder_->Emit(kExprF64##Name);            \
    } else if (param_specific_types[0]->IsA(AsmType::FloatQ())) { \
      current_function_builder_->Emit(kExprF32##Name);            \
    } else {                                                      \
      UNREACHABLE();                                              \
    }                                                             \
    break;
      STDLIB_MATH_FUNCTION_CEIL_LIKE_LIST(V)
#undef V
      case VarKind::kMathMin:
      case VarKind::kMathMax:
        if (param_specific_types[0]->IsA(AsmType::Double())) {
          for (size_t i = 1; i < param_specific_types.size(); ++i) {
            if (function_info->kind == VarKind::kMathMin) {
              current_function_builder_->Emit(kExprF64Min);
            } else {
              current_function_builder_->Emit(kExprF64Max);
            }
          }
        } else if (param_specific_types[0]->IsA(AsmType::Float())) {
          // NOTE: Not technically part of the asm.js spec, but Firefox
          // accepts it.
          for (size_t i = 1; i < param_specific_types.size(); ++i) {
            if (function_info->kind == VarKind::kMathMin) {
              current_function_builder_->Emit(kExprF32Min);
            } else {
              current_function_builder_->Emit(kExprF32Max);
            }
          }
        } else if (param_specific_types[0]->IsA(AsmType::Signed())) {
          TemporaryVariableScope tmp_x(this);
          TemporaryVariableScope tmp_y(this);
          for (size_t i = 1; i < param_specific_types.size(); ++i) {
            current_function_builder_->EmitSetLocal(tmp_x.get());
            current_function_builder_->EmitTeeLocal(tmp_y.get());
            current_function_builder_->EmitGetLocal(tmp_x.get());
            if (function_info->kind == VarKind::kMathMin) {
              current_function_builder_->Emit(kExprI32GeS);
            } else {
              current_function_builder_->Emit(kExprI32LeS);
            }
            current_function_builder_->EmitWithU8(kExprIf, kI32Code);
            current_function_builder_->EmitGetLocal(tmp_x.get());
            current_function_builder_->Emit(kExprElse);
            current_function_builder_->EmitGetLocal(tmp_y.get());
            current_function_builder_->Emit(kExprEnd);
          }
        } else {
          UNREACHABLE();
        }
        break;

      case VarKind::kMathAbs:
        if (param_specific_types[0]->IsA(AsmType::Signed())) {
          TemporaryVariableScope tmp(this);
          current_function_builder_->EmitTeeLocal(tmp.get());
          current_function_builder_->EmitGetLocal(tmp.get());
          current_function_builder_->EmitI32Const(31);
          current_function_builder_->Emit(kExprI32ShrS);
          current_function_builder_->EmitTeeLocal(tmp.get());
          current_function_builder_->Emit(kExprI32Xor);
          current_function_builder_->EmitGetLocal(tmp.get());
          current_function_builder_->Emit(kExprI32Sub);
        } else if (param_specific_types[0]->IsA(AsmType::DoubleQ())) {
          current_function_builder_->Emit(kExprF64Abs);
        } else if (param_specific_types[0]->IsA(AsmType::FloatQ())) {
          current_function_builder_->Emit(kExprF32Abs);
        } else {
          UNREACHABLE();
        }
        break;

      case VarKind::kMathFround:
        // NOTE: Handled in {AsmJsParser::CallExpression} specially and treated
        // as a coercion to "float" type. Cannot be reached as a call here.
        UNREACHABLE();

      default:
        UNREACHABLE();
    }
  } else {
    DCHECK(function_info->kind == VarKind::kFunction ||
           function_info->kind == VarKind::kTable);
    if (function_info->type->IsA(AsmType::None())) {
      function_info->type = function_type;
    } else {
      AsmCallableType* callable = function_info->type->AsCallableType();
      if (!callable ||
          !callable->CanBeInvokedWith(return_type, param_specific_types)) {
        FAILn("Function use doesn't match definition");
      }
    }
    if (function_info->kind == VarKind::kTable) {
      current_function_builder_->EmitGetLocal(tmp_scope->get());
      current_function_builder_->AddAsmWasmOffset(call_pos, to_number_pos);
      current_function_builder_->Emit(kExprCallIndirect);
      current_function_builder_->EmitU32V(signature_index);
      current_function_builder_->EmitU32V(0);  // table index
    } else {
      current_function_builder_->AddAsmWasmOffset(call_pos, to_number_pos);
      current_function_builder_->Emit(kExprCallFunction);
      current_function_builder_->EmitDirectCallIndex(function_info->index);
    }
  }

  return return_type;
}

// 6.9 ValidateCall - helper
bool AsmJsParser::PeekCall() {
  if (!scanner_.IsGlobal()) {
    return false;
  }
  if (GetVarInfo(scanner_.Token())->kind == VarKind::kFunction) {
    return true;
  }
  if (GetVarInfo(scanner_.Token())->kind >= VarKind::kImportedFunction) {
    return true;
  }
  if (GetVarInfo(scanner_.Token())->kind == VarKind::kUnused ||
      GetVarInfo(scanner_.Token())->kind == VarKind::kTable) {
    scanner_.Next();
    if (Peek('(') || Peek('[')) {
      scanner_.Rewind();
      return true;
    }
    scanner_.Rewind();
  }
  return false;
}

// 6.10 ValidateHeapAccess
void AsmJsParser::ValidateHeapAccess() {
  VarInfo* info = GetVarInfo(Consume());
  int32_t size = info->type->ElementSizeInBytes();
  EXPECT_TOKEN('[');
  uint32_t offset;
  if (CheckForUnsigned(&offset)) {
    // TODO(bradnelson): Check more things.
    // TODO(asmjs): Clarify and explain where this limit is coming from,
    // as it is not mandated by the spec directly.
    if (offset > 0x7FFFFFFF ||
        static_cast<uint64_t>(offset) * static_cast<uint64_t>(size) >
            0x7FFFFFFF) {
      FAIL("Heap access out of range");
    }
    if (Check(']')) {
      current_function_builder_->EmitI32Const(
          static_cast<uint32_t>(offset * size));
      // NOTE: This has to happen here to work recursively.
      heap_access_type_ = info->type;
      return;
    } else {
      scanner_.Rewind();
    }
  }
  AsmType* index_type;
  if (info->type->IsA(AsmType::Int8Array()) ||
      info->type->IsA(AsmType::Uint8Array())) {
    RECURSE(index_type = Expression(nullptr));
  } else {
    RECURSE(index_type = ShiftExpression());
    if (heap_access_shift_position_ == kNoHeapAccessShift) {
      FAIL("Expected shift of word size");
    }
    if (heap_access_shift_value_ > 3) {
      FAIL("Expected valid heap access shift");
    }
    if ((1 << heap_access_shift_value_) != size) {
      FAIL("Expected heap access shift to match heap view");
    }
    // Delete the code of the actual shift operation.
    current_function_builder_->DeleteCodeAfter(heap_access_shift_position_);
    // Mask bottom bits to match asm.js behavior.
    current_function_builder_->EmitI32Const(~(size - 1));
    current_function_builder_->Emit(kExprI32And);
  }
  if (!index_type->IsA(AsmType::Intish())) {
    FAIL("Expected intish index");
  }
  EXPECT_TOKEN(']');
  // NOTE: This has to happen here to work recursively.
  heap_access_type_ = info->type;
}

// 6.11 ValidateFloatCoercion
void AsmJsParser::ValidateFloatCoercion() {
  if (!scanner_.IsGlobal() ||
      !GetVarInfo(scanner_.Token())->type->IsA(stdlib_fround_)) {
    FAIL("Expected fround");
  }
  scanner_.Next();
  EXPECT_TOKEN('(');
  call_coercion_ = AsmType::Float();
  // NOTE: The coercion position to float is not observable from JavaScript,
  // because imported functions are not allowed to have float return type.
  call_coercion_position_ = scanner_.Position();
  AsmType* ret;
  RECURSE(ret = AssignmentExpression());
  if (ret->IsA(AsmType::Floatish())) {
    // Do nothing, as already a float.
  } else if (ret->IsA(AsmType::DoubleQ())) {
    current_function_builder_->Emit(kExprF32ConvertF64);
  } else if (ret->IsA(AsmType::Signed())) {
    current_function_builder_->Emit(kExprF32SConvertI32);
  } else if (ret->IsA(AsmType::Unsigned())) {
    current_function_builder_->Emit(kExprF32UConvertI32);
  } else {
    FAIL("Illegal conversion to float");
  }
  EXPECT_TOKEN(')');
}

void AsmJsParser::ScanToClosingParenthesis() {
  int depth = 0;
  for (;;) {
    if (Peek('(')) {
      ++depth;
    } else if (Peek(')')) {
      --depth;
      if (depth < 0) {
        break;
      }
    } else if (Peek(AsmJsScanner::kEndOfInput)) {
      break;
    }
    scanner_.Next();
  }
}

void AsmJsParser::GatherCases(ZoneVector<int32_t>* cases) {
  size_t start = scanner_.Position();
  int depth = 0;
  for (;;) {
    if (Peek('{')) {
      ++depth;
    } else if (Peek('}')) {
      --depth;
      if (depth <= 0) {
        break;
      }
    } else if (depth == 1 && Peek(TOK(case))) {
      scanner_.Next();
      uint32_t uvalue;
      bool negate = false;
      if (Check('-')) negate = true;
      if (!CheckForUnsigned(&uvalue)) {
        break;
      }
      int32_t value = static_cast<int32_t>(uvalue);
      DCHECK_IMPLIES(negate && uvalue == 0x80000000, value == kMinInt);
      if (negate && value != kMinInt) {
        value = -value;
      }
      cases->push_back(value);
    } else if (Peek(AsmJsScanner::kEndOfInput) ||
               Peek(AsmJsScanner::kParseError)) {
      break;
    }
    scanner_.Next();
  }
  scanner_.Seek(start);
}

#undef TOK
#undef RECURSEn
#undef RECURSE
#undef RECURSE_OR_RETURN
#undef EXPECT_TOKENn
#undef EXPECT_TOKEN
#undef EXPECT_TOKEN_OR_RETURN
#undef FAILn
#undef FAIL
#undef FAIL_AND_RETURN
#undef TRACE_ASM_PARSER

}  // namespace wasm
}  // namespace internal
}  // namespace v8
