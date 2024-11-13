// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_TOOLS_WASM_MJSUNIT_MODULE_DISASSEMBLER_IMPL_H_
#define V8_TOOLS_WASM_MJSUNIT_MODULE_DISASSEMBLER_IMPL_H_

#include <ctime>

#include "src/numbers/conversions.h"
#include "src/wasm/function-body-decoder-impl.h"
#include "src/wasm/module-decoder-impl.h"
#include "src/wasm/string-builder-multiline.h"
#include "src/wasm/wasm-disassembler-impl.h"
#include "src/wasm/wasm-module.h"
#include "src/wasm/wasm-opcodes-inl.h"

namespace v8::internal::wasm {

// Provides an implementation of a module disassembler that can produce
// test cases in "mjsunit format", i.e. using the WasmModuleBuilder from
// test/mjsunit/wasm/wasm-module-builder.js to define the module.
//
// The one relevant public method is MjsunitModuleDis::PrintModule().

static constexpr char kHexChars[] = "0123456789abcdef";

StringBuilder& operator<<(StringBuilder& sb, base::Vector<const char> chars) {
  sb.write(chars.cbegin(), chars.size());
  return sb;
}

enum OutputContext : bool {
  // Print "kAnyRefCode" and "kWasmRef, 1," etc (inside function bodies).
  kEmitWireBytes = true,
  // Print "kWasmAnyRef" and "wasmRefType(1)" etc (in module builder functions).
  kEmitObjects = false,
};

// Helper to surround a value by an optional ...wasmUnsignedLeb() call.
class MaybeLebScope {
 public:
  MaybeLebScope(StringBuilder& out, uint32_t index) : out(out), index(index) {
    if (index > 0x7F) {
      out << "...wasmUnsignedLeb(";
    }
  }
  ~MaybeLebScope() {
    if (index > 0x7F) {
      out << ')';
    }
  }

 private:
  StringBuilder& out;
  uint32_t index;
};

class MjsunitNamesProvider {
 public:
  static constexpr const char* kLocalPrefix = "$var";

  MjsunitNamesProvider(const WasmModule* module, ModuleWireBytes wire_bytes)
      : module_(module), wire_bytes_(wire_bytes) {
    function_variable_names_.resize(module->functions.size());
    // Algorithm for selecting function names:
    // 1. If the name section defines a suitable name, use that.
    // 2. Else, if the function is imported and the import "field name" is
    //    a suitable name, use that.
    // 3. Else, if the function is exported and its export name is a
    //    suitable name, use that.
    // 4. Else, generate a name like "$func123".
    // The definition of "suitable" is:
    // - the name has at least one character
    // - and all characters are in the set [a-zA-Z0-9$_]
    // - and the name doesn't clash with common auto-generated names.
    for (uint32_t i = 0; i < module->functions.size(); i++) {
      WireBytesRef name =
          module_->lazily_generated_names.LookupFunctionName(wire_bytes, i);
      if (IsSuitableFunctionVariableName(name)) {
        function_variable_names_[i] = name;
      }
    }
    for (const WasmImport& imp : module_->import_table) {
      if (imp.kind != kExternalFunction) continue;
      if (function_variable_names_[imp.index].is_set()) continue;
      if (IsSuitableFunctionVariableName(imp.field_name)) {
        function_variable_names_[imp.index] = imp.field_name;
      }
    }
    for (const WasmExport& ex : module_->export_table) {
      if (ex.kind != kExternalFunction) continue;
      if (function_variable_names_[ex.index].is_set()) continue;
      if (IsSuitableFunctionVariableName(ex.name)) {
        function_variable_names_[ex.index] = ex.name;
      }
    }
  }

  bool HasFunctionName(uint32_t function_index) {
    WireBytesRef ref = module_->lazily_generated_names.LookupFunctionName(
        wire_bytes_, function_index);
    return ref.is_set();
  }

  bool FunctionNameEquals(uint32_t function_index, WireBytesRef ref) {
    WireBytesRef name_ref = module_->lazily_generated_names.LookupFunctionName(
        wire_bytes_, function_index);
    if (name_ref.length() != ref.length()) return false;
    if (name_ref.offset() == ref.offset()) return true;
    WasmName name = wire_bytes_.GetNameOrNull(name_ref);
    WasmName question = wire_bytes_.GetNameOrNull(ref);
    return memcmp(name.begin(), question.begin(), name.length()) == 0;
  }

  void PrintTypeVariableName(StringBuilder& out, uint32_t index) {
    // The name creation scheme must be in sync with {PrintStructType} etc.
    // below!
    if (module_->has_struct(index)) {
      out << "$struct" << index;
    } else if (module_->has_array(index)) {
      out << "$array" << index;
    } else {
      // This function is meant for dumping the type section, so we can assume
      // validity.
      DCHECK(module_->has_signature(index));
      out << "$sig" << index;
    }
  }

  void PrintStructType(StringBuilder& out, uint32_t index, OutputContext mode) {
    DCHECK(module_->has_struct(index));
    PrintMaybeLEB(out, "$struct", index, mode);
  }

  void PrintArrayType(StringBuilder& out, uint32_t index, OutputContext mode) {
    DCHECK(module_->has_array(index));
    PrintMaybeLEB(out, "$array", index, mode);
  }

  void PrintSigType(StringBuilder& out, uint32_t index, OutputContext mode) {
    DCHECK(module_->has_signature(index));
    PrintMaybeLEB(out, "$sig", index, mode);
  }

  void PrintTypeIndex(StringBuilder& out, uint32_t index, OutputContext mode) {
    if (module_->has_struct(index)) {
      PrintStructType(out, index, mode);
    } else if (module_->has_array(index)) {
      PrintArrayType(out, index, mode);
    } else if (module_->has_signature(index)) {
      PrintSigType(out, index, mode);
    } else {
      // Support building invalid modules for testing.
      PrintMaybeLEB(out, "/* invalid type */ ", index, mode);
    }
  }

  // For the name section.
  void PrintFunctionName(StringBuilder& out, uint32_t index) {
    WireBytesRef ref =
        module_->lazily_generated_names.LookupFunctionName(wire_bytes_, index);
    DCHECK(ref.is_set());  // Callers should use `HasFunctionName` to check.
    out.write(wire_bytes_.start() + ref.offset(), ref.length());
  }

  // For the JS variable referring to the function.
  void PrintFunctionVariableName(StringBuilder& out, uint32_t index) {
    if (index >= function_variable_names_.size()) {
      // Invalid module.
      out << "$invalid" << index;
      return;
    }
    WasmName name = wire_bytes_.GetNameOrNull(function_variable_names_[index]);
    if (name.size() > 0) {
      out << name << index;
    } else {
      out << "$func" << index;
    }
  }

  // Prints "$func" or "$func.index" depending on whether $func is imported
  // or defined by `builder.addFunction`.
  void PrintFunctionReference(StringBuilder& out, uint32_t index) {
    PrintFunctionVariableName(out, index);
    if (index < module_->functions.size() &&
        !module_->functions[index].imported) {
      out << ".index";
    }
  }
  void PrintFunctionReferenceLeb(StringBuilder& out, uint32_t index) {
    MaybeLebScope leb_scope(out, index);
    PrintFunctionReference(out, index);
  }

  // We only use this for comments, so it doesn't need to bother with LEBs.
  void PrintLocalName(StringBuilder& out, uint32_t index) {
    out << kLocalPrefix << index;
  }

  void PrintGlobalName(StringBuilder& out, uint32_t index) {
    out << "$global" << index;
  }
  void PrintGlobalReference(StringBuilder& out, uint32_t index) {
    PrintGlobalName(out, index);
    if (index < module_->globals.size() && !module_->globals[index].imported) {
      out << ".index";
    }
  }
  void PrintGlobalReferenceLeb(StringBuilder& out, uint32_t index) {
    MaybeLebScope leb_scope(out, index);
    PrintGlobalReference(out, index);
  }

  void PrintTableName(StringBuilder& out, uint32_t index) {
    out << "$table" << index;
  }
  void PrintTableReference(StringBuilder& out, uint32_t index) {
    PrintTableName(out, index);
    if (index < module_->tables.size() && !module_->tables[index].imported) {
      out << ".index";
    }
  }

  void PrintTableReferenceLeb(StringBuilder& out, uint32_t index) {
    MaybeLebScope leb_scope(out, index);
    PrintTableReference(out, index);
  }

  void PrintMemoryName(StringBuilder& out, uint32_t index) {
    out << "$mem" << index;
  }
  void PrintMemoryReferenceLeb(StringBuilder& out, uint32_t index) {
    MaybeLebScope leb_scope(out, index);
    PrintMemoryName(out, index);
  }

  void PrintTagName(StringBuilder& out, uint32_t index) {
    out << "$tag" << index;
  }
  void PrintTagReferenceLeb(StringBuilder& out, uint32_t index) {
    MaybeLebScope leb_scope(out, index);
    PrintTagName(out, index);
  }

  void PrintDataSegmentName(StringBuilder& out, uint32_t index) {
    out << "$data" << index;
  }
  void PrintDataSegmentReferenceLeb(StringBuilder& out, uint32_t index) {
    MaybeLebScope leb_scope(out, index);
    PrintDataSegmentName(out, index);
  }

  void PrintElementSegmentName(StringBuilder& out, uint32_t index) {
    out << "$segment" << index;
  }
  void PrintElementSegmentReferenceLeb(StringBuilder& out, uint32_t index) {
    MaybeLebScope leb_scope(out, index);
    PrintElementSegmentName(out, index);
  }

  // Format: HeapType::* enum value, JS global constant.
#define ABSTRACT_TYPE_LIST(V)                                     \
  V(kAny, kWasmAnyRef, kAnyRefCode)                               \
  V(kArray, kWasmArrayRef, kArrayRefCode)                         \
  V(kEq, kWasmEqRef, kEqRefCode)                                  \
  V(kExn, kWasmExnRef, kExnRefCode)                               \
  V(kExtern, kWasmExternRef, kExternRefCode)                      \
  V(kFunc, kWasmFuncRef, kFuncRefCode)                            \
  V(kI31, kWasmI31Ref, kI31RefCode)                               \
  V(kNone, kWasmNullRef, kNullRefCode)                            \
  V(kNoExn, kWasmNullExnRef, kNullExnRefCode)                     \
  V(kNoExtern, kWasmNullExternRef, kNullExternRefCode)            \
  V(kNoFunc, kWasmNullFuncRef, kNullFuncRefCode)                  \
  V(kString, kWasmStringRef, kStringRefCode)                      \
  V(kStruct, kWasmStructRef, kStructRefCode)

// Same, but for types where the shorthand is non-nullable.
#define ABSTRACT_NN_TYPE_LIST(V)                                  \
  V(kStringViewWtf16, kWasmStringViewWtf16, kStringViewWtf16Code) \
  V(kStringViewWtf8, kWasmStringViewWtf8, kStringViewWtf8Code)    \
  V(kStringViewIter, kWasmStringViewIter, kStringViewIterCode)

  void PrintHeapType(StringBuilder& out, HeapType type, OutputContext mode) {
    switch (type.representation()) {
#define CASE(kCpp, JS, JSCode)                       \
  case HeapType::kCpp:                               \
    out << (mode == kEmitWireBytes ? #JSCode : #JS); \
    return;
      ABSTRACT_TYPE_LIST(CASE)
      ABSTRACT_NN_TYPE_LIST(CASE)
#undef CASE
      case HeapType::kBottom:
        UNREACHABLE();
      default:
        PrintTypeIndex(out, type.ref_index(), mode);
    }
  }

  void PrintValueType(StringBuilder& out, ValueType type, OutputContext mode) {
    switch (type.kind()) {
        // clang-format off
      case kI8:   out << "kWasmI8";   return;
      case kI16:  out << "kWasmI16";  return;
      case kI32:  out << "kWasmI32";  return;
      case kI64:  out << "kWasmI64";  return;
      case kF16:  out << "kWasmF16";  return;
      case kF32:  out << "kWasmF32";  return;
      case kF64:  out << "kWasmF64";  return;
      case kS128: out << "kWasmS128"; return;
      // clang-format on
      case kRefNull:
        switch (type.heap_representation()) {
#define CASE(kCpp, _, _2) case HeapType::kCpp:
          ABSTRACT_TYPE_LIST(CASE)
#undef CASE
          return PrintHeapType(out, type.heap_type(), mode);
          case HeapType::kBottom:
            UNREACHABLE();
          default:
            out << (mode == kEmitObjects ? "wasmRefNullType("
                                         : "kWasmRefNull, ");
            break;
        }
        break;
      case kRef:
        switch (type.heap_representation()) {
#define CASE(kCpp, _, _2) case HeapType::kCpp:
          ABSTRACT_NN_TYPE_LIST(CASE)
#undef CASE
          return PrintHeapType(out, type.heap_type(), mode);
          case HeapType::kBottom:
            UNREACHABLE();
          default:
            out << (mode == kEmitObjects ? "wasmRefType(" : "kWasmRef, ");
            break;
        }
        break;
      case kBottom:
        out << "/*<bot>*/";
        return;
      case kRtt:
      case kVoid:
        UNREACHABLE();
    }
    PrintHeapType(out, type.heap_type(), mode);
    if (mode == kEmitObjects) out << ")";
  }

  void PrintMakeSignature(StringBuilder& out, const FunctionSig* sig) {
    // Check if we can use an existing definition (for a couple of
    // common cases).
    // TODO(jkummerow): Is more complete coverage here worth it?
#define PREDEFINED(name)           \
  if (*sig == impl::kSig_##name) { \
    out << "kSig_" #name;          \
    return;                        \
  }
    PREDEFINED(d_d)
    PREDEFINED(d_dd)
    PREDEFINED(i_i)
    PREDEFINED(i_ii)
    PREDEFINED(i_iii)
    PREDEFINED(i_v)
    PREDEFINED(l_l)
    PREDEFINED(l_ll)
    PREDEFINED(v_i)
    PREDEFINED(v_ii)
    PREDEFINED(v_v)
#undef PREDEFINED

    // No hit among predefined signatures we checked for; define our own.
    out << "makeSig([";
    for (size_t i = 0; i < sig->parameter_count(); i++) {
      if (i > 0) out << ", ";
      PrintValueType(out, sig->GetParam(i), kEmitObjects);
    }
    out << "], [";
    for (size_t i = 0; i < sig->return_count(); i++) {
      if (i > 0) out << ", ";
      PrintValueType(out, sig->GetReturn(i), kEmitObjects);
    }
    out << "])";
  }

  void PrintSignatureComment(StringBuilder& out, const FunctionSig* sig) {
    out << ": [";
    for (uint32_t i = 0; i < sig->parameter_count(); i++) {
      if (i > 0) out << ", ";
      if (sig->parameter_count() > 3) {
        out << kLocalPrefix << i << ":";
      }
      PrintValueType(out, sig->GetParam(i), kEmitObjects);
    }
    out << "] -> [";
    for (uint32_t i = 0; i < sig->return_count(); i++) {
      if (i > 0) out << ", ";
      PrintValueType(out, sig->GetReturn(i), kEmitObjects);
    }
    out << "]";
  }

 private:
  bool IsSuitableFunctionVariableName(WireBytesRef ref) {
    if (!ref.is_set()) return false;
    if (ref.length() == 0) return false;
    WasmName name = wire_bytes_.GetNameOrNull(ref);
    // Check for invalid characters.
    for (uint32_t i = 0; i < ref.length(); i++) {
      char c = name[i];
      char uc = c | 0x20;
      if (uc >= 'a' && uc <= 'z') continue;
      if (c == '$' || c == '_') continue;
      if (c >= '0' && c <= '9') continue;
      return false;
    }
    // Check for clashes with auto-generated names.
    // This isn't perfect: any collision with a function (e.g. "makeSig")
    // or constant (e.g. "kFooRefCode") would also break the generated test,
    // but it doesn't seem feasible to accurately guard against all of those.
    if (name.length() >= 8) {
      if (memcmp(name.begin(), "$segment", 8) == 0) return false;
    }
    if (name.length() >= 7) {
      if (memcmp(name.begin(), "$global", 7) == 0) return false;
      if (memcmp(name.begin(), "$struct", 7) == 0) return false;
    }
    if (name.length() >= 6) {
      if (memcmp(name.begin(), "$array", 6) == 0) return false;
      if (memcmp(name.begin(), "$table", 6) == 0) return false;
    }
    if (name.length() >= 5) {
      if (memcmp(name.begin(), "$data", 5) == 0) return false;
      if (memcmp(name.begin(), "$func", 5) == 0) return false;
      if (memcmp(name.begin(), "kExpr", 5) == 0) return false;
      if (memcmp(name.begin(), "kSig_", 5) == 0) return false;
      if (memcmp(name.begin(), "kWasm", 5) == 0) return false;
    }
    if (name.length() >= 4) {
      if (memcmp(name.begin(), "$mem", 4) == 0) return false;
      if (memcmp(name.begin(), "$sig", 4) == 0) return false;
      if (memcmp(name.begin(), "$tag", 4) == 0) return false;
    }
    return true;
  }

  void PrintMaybeLEB(StringBuilder& out, const char* prefix, uint32_t index,
                     OutputContext mode) {
    if (index <= 0x3F || mode == kEmitObjects) {
      out << prefix << index;
    } else {
      out << "...wasmSignedLeb(" << prefix << index << ")";
    }
  }

  const WasmModule* module_;
  ModuleWireBytes wire_bytes_;
  std::vector<WireBytesRef> function_variable_names_;
};

namespace {
const char* RawOpcodeName(WasmOpcode opcode) {
  switch (opcode) {
#define DECLARE_NAME_CASE(name, ...) \
  case kExpr##name:                  \
    return "kExpr" #name;
    FOREACH_OPCODE(DECLARE_NAME_CASE)
#undef DECLARE_NAME_CASE
    default:
      break;
  }
  return "Unknown";
}
const char* PrefixName(WasmOpcode prefix_opcode) {
  switch (prefix_opcode) {
#define DECLARE_PREFIX_CASE(name, opcode) \
  case k##name##Prefix:                   \
    return "k" #name "Prefix";
    FOREACH_PREFIX(DECLARE_PREFIX_CASE)
#undef DECLARE_PREFIX_CASE
    default:
      return "Unknown prefix";
  }
}
}  // namespace

template <typename ValidationTag>
class MjsunitImmediatesPrinter;
class MjsunitFunctionDis : public WasmDecoder<Decoder::FullValidationTag> {
 public:
  using ValidationTag = Decoder::FullValidationTag;

  MjsunitFunctionDis(Zone* zone, const WasmModule* module, uint32_t func_index,
                     bool shared, WasmDetectedFeatures* detected,
                     const FunctionSig* sig, const uint8_t* start,
                     const uint8_t* end, uint32_t offset,
                     MjsunitNamesProvider* mjsunit_names,
                     Indentation indentation)
      : WasmDecoder<ValidationTag>(zone, module, WasmEnabledFeatures::All(),
                                   detected, sig, shared, start, end, offset),
        names_(mjsunit_names),
        indentation_(indentation) {}

  void WriteMjsunit(MultiLineStringBuilder& out);

  // TODO(jkummerow): Support for compilation hints is missing.

  void DecodeGlobalInitializer(StringBuilder& out);

  uint32_t PrintMjsunitImmediatesAndGetLength(StringBuilder& out);

 private:
  template <typename ValidationTag>
  friend class MjsunitImmediatesPrinter;

  WasmOpcode PrintPrefixedOpcode(StringBuilder& out, WasmOpcode prefix) {
    auto [prefixed, prefixed_length] = read_u32v<ValidationTag>(pc_ + 1);
    if (failed()) {
      out << PrefixName(prefix) << ", ";
      return prefix;
    }
    int shift = prefixed > 0xFF ? 12 : 8;
    WasmOpcode opcode = static_cast<WasmOpcode>((prefix << shift) | prefixed);
    if (prefixed <= 0x7F) {
      if (opcode != kExprS128Const) {
        out << PrefixName(prefix) << ", ";
        out << RawOpcodeName(opcode) << ",";
      }
      if (opcode == kExprAtomicFence) {
        // Unused zero-byte.
        out << " 0,";
      }
    } else if (prefix == kSimdPrefix) {
      if (prefixed > 0xFF) {
        out << "kSimdPrefix, ..." << RawOpcodeName(opcode) << ",";
      } else {
        out << "...SimdInstr(" << RawOpcodeName(opcode) << "),";
      }
    } else if (prefix == kGCPrefix) {
      out << "...GCInstr(" << RawOpcodeName(opcode) << "),";
    } else {
      // Invalid module.
      out << "0x" << kHexChars[prefix >> 4] << kHexChars[prefix & 0xF] << ", ";
      while (prefixed > 0) {
        uint32_t chunk = prefixed & 0x7F;
        prefixed >>= 7;
        if (prefixed) chunk |= 0x80;
        out << "0x" << kHexChars[chunk >> 4] << kHexChars[chunk & 0xF] << ", ";
      }
    }
    return opcode;
  }

  MjsunitNamesProvider* names() { return names_; }

  MjsunitNamesProvider* names_;
  Indentation indentation_;
  WasmOpcode current_opcode_;
};

void MjsunitFunctionDis::WriteMjsunit(MultiLineStringBuilder& out) {
  if (!more()) {
    out << ".addBodyWithEnd([]);  // Invalid: missing kExprEnd.";
    return;
  }
  if (end_ == pc_ + 1 && *pc_ == static_cast<uint8_t>(kExprEnd)) {
    out << ".addBody([]);";
    return;
  }

  // Emit the locals.
  uint32_t locals_length = DecodeLocals(pc_);
  if (failed()) {
    out << "// Failed to decode locals\n";
    return;
  }
  uint32_t num_params = static_cast<uint32_t>(sig_->parameter_count());
  if (num_locals_ > num_params) {
    for (uint32_t pos = num_params, count = 1; pos < num_locals_;
         pos += count, count = 1) {
      ValueType type = local_types_[pos];
      while (pos + count < num_locals_ && local_types_[pos + count] == type) {
        count++;
      }
      if (pos > num_params) out << indentation_;
      out << ".addLocals(";
      names()->PrintValueType(out, type, kEmitObjects);
      out << ", " << count << ")  // ";
      names()->PrintLocalName(out, pos);
      if (count > 1) {
        out << " - ";
        names()->PrintLocalName(out, pos + count - 1);
      }
      out.NextLine(0);
    }
    out << indentation_;
  }
  consume_bytes(locals_length);

  // Emit the function body.
  out << ".addBody([";
  out.NextLine(0);
  indentation_.increase();
  int base_indentation = indentation_.current();
  while (pc_ < end_ && ok()) {
    WasmOpcode opcode = static_cast<WasmOpcode>(*pc_);
    if (WasmOpcodes::IsPrefixOpcode(opcode)) {
      out << indentation_;
      opcode = PrintPrefixedOpcode(out, opcode);
    } else {
      bool decrease_indentation = false;
      bool increase_indentation_after = false;
      bool bailout = false;
      bool print_instruction = true;
      switch (opcode) {
        case kExprEnd:
          // Don't print the final "end", it's implicit in {addBody()}.
          if (pc_ + 1 == end_) {
            bailout = true;
          } else {
            decrease_indentation = true;
          }
          break;
        case kExprDelegate:
          decrease_indentation = true;
          break;
        case kExprElse:
        case kExprCatch:
        case kExprCatchAll:
          decrease_indentation = true;
          increase_indentation_after = true;
          break;
        case kExprBlock:
        case kExprIf:
        case kExprLoop:
        case kExprTry:
        case kExprTryTable:
          increase_indentation_after = true;
          break;
        case kExprI32Const:
        case kExprI64Const:
        case kExprF32Const:
        case kExprF64Const:
          print_instruction = false;
          break;
        default:
          // The other instructions get no special treatment.
          break;
      }
      if (decrease_indentation && indentation_.current() > base_indentation) {
        indentation_.decrease();
      }
      if (bailout) break;
      out << indentation_;
      if (print_instruction) out << RawOpcodeName(opcode) << ",";
      if (increase_indentation_after) indentation_.increase();
    }
    current_opcode_ = opcode;
    pc_ += PrintMjsunitImmediatesAndGetLength(out);
    out.NextLine(0);
  }

  indentation_.decrease();
  out << indentation_ << "]);";
  out.NextLine(0);
}

void PrintF32Const(StringBuilder& out, ImmF32Immediate& imm) {
  uint32_t bits = base::bit_cast<uint32_t>(imm.value);
  if (bits == 0x80000000) {
    out << "wasmF32Const(-0)";
    return;
  }
  if (std::isnan(imm.value)) {
    out << "[kExprF32Const";
    for (int i = 0; i < 4; i++) {
      uint32_t chunk = bits & 0xFF;
      bits >>= 8;
      out << ", 0x" << kHexChars[chunk >> 4] << kHexChars[chunk & 0xF];
    }
    out << "]";
    return;
  }
  char buffer[100];
  const char* str =
      DoubleToCString(imm.value, base::VectorOf(buffer, sizeof(buffer)));
  out << "wasmF32Const(" << str << ")";
}

void PrintF64Const(StringBuilder& out, ImmF64Immediate& imm) {
  uint64_t bits = base::bit_cast<uint64_t>(imm.value);
  if (bits == base::bit_cast<uint64_t>(-0.0)) {
    out << "wasmF64Const(-0)";
    return;
  }
  if (std::isnan(imm.value)) {
    out << "[kExprF64Const";
    for (int i = 0; i < 8; i++) {
      uint32_t chunk = bits & 0xFF;
      bits >>= 8;
      out << ", 0x" << kHexChars[chunk >> 4] << kHexChars[chunk & 0xF];
    }
    out << "]";
    return;
  }
  char buffer[100];
  const char* str =
      DoubleToCString(imm.value, base::VectorOf(buffer, sizeof(buffer)));
  out << "wasmF64Const(" << str << ")";
}

void PrintI64Const(StringBuilder& out, ImmI64Immediate& imm) {
  out << "wasmI64Const(";
  if (imm.value >= 0) {
    out << static_cast<uint64_t>(imm.value);
  } else {
    out << "-" << ((~static_cast<uint64_t>(imm.value)) + 1);
  }
  out << "n)";  // `n` to make it a BigInt literal.
}

void MjsunitFunctionDis::DecodeGlobalInitializer(StringBuilder& out) {
  // Special: Pretty-print simple constants (that aren't handled by the
  // i32 special case at the caller).
  uint32_t length = static_cast<uint32_t>(end_ - pc_);
  if (*(end_ - 1) == kExprEnd) {
    if (*pc_ == kExprF32Const && length == 6) {
      ImmF32Immediate imm(this, pc_ + 1, validate);
      return PrintF32Const(out, imm);
    }
    if (*pc_ == kExprF64Const && length == 10) {
      ImmF64Immediate imm(this, pc_ + 1, validate);
      return PrintF64Const(out, imm);
    }
    if (*pc_ == kExprI64Const) {
      ImmI64Immediate imm(this, pc_ + 1, validate);
      if (length == 2 + imm.length) {
        return PrintI64Const(out, imm);
      }
    }
  }
  // Regular path.
  out << "[";
  const char* old_cursor = out.cursor();
  while (pc_ < end_ && ok()) {
    WasmOpcode opcode = static_cast<WasmOpcode>(*pc_);
    if (WasmOpcodes::IsPrefixOpcode(opcode)) {
      opcode = PrintPrefixedOpcode(out, opcode);
    } else {
      // Don't print the final "end".
      if (opcode == kExprEnd && pc_ + 1 == end_) break;
      // Constants will decide whether to print the instruction.
      if (opcode != kExprI32Const && opcode != kExprI64Const &&
          opcode != kExprF32Const && opcode != kExprF64Const) {
        out << RawOpcodeName(opcode) << ",";
      }
    }
    current_opcode_ = opcode;
    pc_ += PrintMjsunitImmediatesAndGetLength(out);
  }
  if (out.cursor() != old_cursor) {
    // If anything was written, then it ends with a comma. Erase that to
    // replace it with ']' for conciseness.
    DCHECK_EQ(*(out.cursor() - 1), ',');
    out.backspace();
  }
  out << "]";
}

template <typename ValidationTag>
class MjsunitImmediatesPrinter {
 public:
  MjsunitImmediatesPrinter(StringBuilder& out, MjsunitFunctionDis* owner)
      : out_(out), owner_(owner) {}

  MjsunitNamesProvider* names() { return owner_->names_; }

  void PrintSignature(uint32_t sig_index) {
    out_ << " ";
    if (owner_->module_->has_signature(sig_index)) {
      names()->PrintSigType(out_, sig_index, kEmitWireBytes);
    } else {
      out_ << sig_index << " /* invalid signature */";
    }
    out_ << ",";
  }

  void BlockType(BlockTypeImmediate& imm) {
    if (imm.sig.all().begin() == nullptr) {
      PrintSignature(imm.sig_index);
    } else if (imm.sig.return_count() == 0) {
      out_ << " kWasmVoid,";
    } else {
      out_ << " ";
      names()->PrintValueType(out_, imm.sig.GetReturn(), kEmitWireBytes);
      out_ << ",";
    }
  }

  void HeapType(HeapTypeImmediate& imm) {
    out_ << " ";
    names()->PrintHeapType(out_, imm.type, kEmitWireBytes);
    out_ << ",";
  }

  // TODO(mliedtke): This is used for br_on_cast[_fail] and currently does not
  // create a valid br_on_cast instruction.
  void ValueType(HeapTypeImmediate& imm, bool is_nullable) {
    out_ << " ";
    names()->PrintValueType(
        out_,
        ValueType::RefMaybeNull(imm.type,
                                is_nullable ? kNullable : kNonNullable),
        kEmitWireBytes);
    out_ << ",";
  }

  void BranchDepth(BranchDepthImmediate& imm) { WriteUnsignedLEB(imm.depth); }

  void BranchTable(BranchTableImmediate& imm) {
    WriteUnsignedLEB(imm.table_count);
    const uint8_t* pc = imm.table;
    // i == table_count is the default case.
    for (uint32_t i = 0; i <= imm.table_count; i++) {
      auto [target, length] = owner_->read_u32v<ValidationTag>(pc);
      pc += length;
      WriteUnsignedLEB(target);
    }
  }

  void TryTable(TryTableImmediate& imm) {
    WriteUnsignedLEB(imm.table_count);
    owner_->indentation_.increase();
    owner_->indentation_.increase();
    const uint8_t* pc = imm.table;
    for (uint32_t i = 0; i < imm.table_count; i++) {
      out_ << "\n" << owner_->indentation_;
      uint8_t kind = owner_->read_u8<ValidationTag>(pc++);
      switch (kind) {
        case kCatch:
          out_ << "kCatchNoRef, ";
          break;
        case kCatchRef:
          out_ << "kCatchRef, ";
          break;
        case kCatchAll:
          out_ << "kCatchAllNoRef, ";
          break;
        case kCatchAllRef:
          out_ << "kCatchAllRef, ";
          break;
        default:
          out_ << kind;
      }
      if (kind == kCatch || kind == kCatchRef) {
        auto [tag, length] = owner_->read_u32v<ValidationTag>(pc);
        pc += length;
        names()->PrintTagReferenceLeb(out_, tag);
        out_ << ", ";
      }
      auto [target, length] = owner_->read_u32v<ValidationTag>(pc);
      pc += length;
      out_ << target << ",";
    }
    owner_->indentation_.decrease();
    owner_->indentation_.decrease();
  }

  void CallIndirect(CallIndirectImmediate& imm) {
    PrintSignature(imm.sig_imm.index);
    TableIndex(imm.table_imm);
  }

  void SelectType(SelectTypeImmediate& imm) {
    out_ << " 1, ";  // One type.
    names()->PrintValueType(out_, imm.type, kEmitWireBytes);
    out_ << ",";
  }

  void MemoryAccess(MemoryAccessImmediate& imm) {
    uint32_t align = imm.alignment;
    if (imm.mem_index != 0) {
      align |= 0x40;
      WriteUnsignedLEB(align);
      WriteUnsignedLEB(imm.mem_index);
    } else {
      WriteUnsignedLEB(align);
    }
    if (imm.mem_index < owner_->module_->memories.size() &&
        owner_->module_->memories[imm.mem_index].is_memory64) {
      WriteLEB64(imm.offset);
    } else {
      DCHECK_LE(imm.offset, std::numeric_limits<uint32_t>::max());
      WriteUnsignedLEB(static_cast<uint32_t>(imm.offset));
    }
  }

  void SimdLane(SimdLaneImmediate& imm) { out_ << " " << imm.lane << ","; }

  void Field(FieldImmediate& imm) {
    TypeIndex(imm.struct_imm);
    WriteUnsignedLEB(imm.field_imm.index);
  }

  void Length(IndexImmediate& imm) { WriteUnsignedLEB(imm.index); }

  void TagIndex(TagIndexImmediate& imm) {
    out_ << " ";
    names()->PrintTagReferenceLeb(out_, imm.index);
    out_ << ",";
  }

  void FunctionIndex(IndexImmediate& imm) {
    out_ << " ";
    names()->PrintFunctionReferenceLeb(out_, imm.index);
    out_ << ",";
  }

  void TypeIndex(IndexImmediate& imm) {
    out_ << " ";
    names()->PrintTypeIndex(out_, imm.index, kEmitWireBytes);
    out_ << ",";
  }

  void LocalIndex(IndexImmediate& imm) {
    WriteUnsignedLEB(imm.index);
    out_ << "  // ";
    names()->PrintLocalName(out_, imm.index);
  }

  void GlobalIndex(IndexImmediate& imm) {
    out_ << " ";
    names()->PrintGlobalReferenceLeb(out_, imm.index);
    out_ << ",";
  }

  void TableIndex(TableIndexImmediate& imm) {
    out_ << " ";
    names()->PrintTableReferenceLeb(out_, imm.index);
    out_ << ",";
  }

  void MemoryIndex(MemoryIndexImmediate& imm) {
    out_ << " ";
    names()->PrintMemoryReferenceLeb(out_, imm.index);
    out_ << ",";
  }

  void DataSegmentIndex(IndexImmediate& imm) {
    out_ << " ";
    names()->PrintDataSegmentReferenceLeb(out_, imm.index);
    out_ << ",";
  }

  void ElemSegmentIndex(IndexImmediate& imm) {
    out_ << " ";
    names()->PrintElementSegmentReferenceLeb(out_, imm.index);
    out_ << ",";
  }

  void I32Const(ImmI32Immediate& imm) {
    if (imm.value >= 0 && imm.value <= 0x3F) {
      out_ << "kExprI32Const, " << imm.value << ",";
    } else {
      out_ << "...wasmI32Const(" << imm.value << "),";
    }
  }

  void I64Const(ImmI64Immediate& imm) {
    if (imm.value >= 0 && imm.value <= 0x3F) {
      out_ << "kExprI64Const, " << static_cast<uint32_t>(imm.value) << ',';
      return;
    }
    out_ << "...";
    PrintI64Const(out_, imm);
    out_ << ",";
  }

  void F32Const(ImmF32Immediate& imm) {
    out_ << "...";
    PrintF32Const(out_, imm);
    out_ << ",";
  }

  void F64Const(ImmF64Immediate& imm) {
    out_ << "...";
    PrintF64Const(out_, imm);
    out_ << ",";
  }

  void S128Const(Simd128Immediate& imm) {
    if (owner_->current_opcode_ == kExprI8x16Shuffle) {
      for (int i = 0; i < 16; i++) {
        out_ << " " << uint32_t{imm.value[i]} << ",";
      }
    } else {
      DCHECK_EQ(owner_->current_opcode_, kExprS128Const);
      out_ << "...wasmS128Const([";
      for (int i = 0; i < 16; i++) {
        if (i > 0) out_ << ", ";
        out_ << uint32_t{imm.value[i]};
      }
      out_ << "]),";
    }
  }

  void StringConst(StringConstImmediate& imm) {
    // TODO(jkummerow): Support for string constants is incomplete, we never
    // emit a strings section.
    WriteUnsignedLEB(imm.index);
  }

  void MemoryInit(MemoryInitImmediate& imm) {
    DataSegmentIndex(imm.data_segment);
    WriteUnsignedLEB(imm.memory.index);
  }

  void MemoryCopy(MemoryCopyImmediate& imm) {
    out_ << " ";
    names()->PrintMemoryReferenceLeb(out_, imm.memory_dst.index);
    out_ << ", ";
    names()->PrintMemoryReferenceLeb(out_, imm.memory_src.index);
    out_ << ",";
  }

  void TableInit(TableInitImmediate& imm) {
    out_ << " ";
    names()->PrintElementSegmentReferenceLeb(out_, imm.element_segment.index);
    out_ << ", ";
    names()->PrintTableReferenceLeb(out_, imm.table.index);
    out_ << ",";
  }

  void TableCopy(TableCopyImmediate& imm) {
    out_ << " ";
    names()->PrintTableReferenceLeb(out_, imm.table_dst.index);
    out_ << ", ";
    names()->PrintTableReferenceLeb(out_, imm.table_src.index);
    out_ << ",";
  }

  void ArrayCopy(IndexImmediate& dst, IndexImmediate& src) {
    out_ << " ";
    names()->PrintTypeIndex(out_, dst.index, kEmitWireBytes);
    out_ << ", ";
    names()->PrintTypeIndex(out_, src.index, kEmitWireBytes);
    out_ << ",";
  }

 private:
  void WriteUnsignedLEB(uint32_t value) {
    if (value < 128) {
      out_ << " " << value << ",";
    } else {
      out_ << " ...wasmUnsignedLeb(" << value << "),";
    }
  }
  void WriteLEB64(uint64_t value) {
    if (value < 128) {
      out_ << " " << value << ",";
    } else {
      // TODO(jkummerow): Technically we should use an unsigned version,
      // but the module builder doesn't offer one yet.
      out_ << " ...wasmSignedLeb64(" << value << "n),";
    }
  }

  StringBuilder& out_;
  MjsunitFunctionDis* owner_;
};

// For opcodes that produce constants (such as `kExprI32Const`), this prints
// more than just the immediate: it also decides whether to use
// "kExprI32Const, 0," or "...wasmI32Const(1234567)".
uint32_t MjsunitFunctionDis::PrintMjsunitImmediatesAndGetLength(
    StringBuilder& out) {
  using Printer = MjsunitImmediatesPrinter<ValidationTag>;
  Printer imm_printer(out, this);
  return WasmDecoder::OpcodeLength<Printer>(this, this->pc_, imm_printer);
}

class MjsunitModuleDis {
 public:
  MjsunitModuleDis(MultiLineStringBuilder& out, const WasmModule* module,
                   NamesProvider* names, const ModuleWireBytes wire_bytes,
                   AccountingAllocator* allocator, bool has_error = false)
      : out_(out),
        module_(module),
        names_provider_(names),
        mjsunit_names_(module, wire_bytes),
        wire_bytes_(wire_bytes),
        zone_(allocator, "disassembler"),
        has_error_(has_error) {
    offsets_.CollectOffsets(module, wire_bytes.module_bytes());
  }

  void PrintModule() {
    tzset();
    time_t current_time = time(nullptr);
    struct tm current_localtime;
#ifdef V8_OS_WIN
    localtime_s(&current_localtime, &current_time);
#else
    localtime_r(&current_time, &current_localtime);
#endif
    int year = 1900 + current_localtime.tm_year;

    out_ << "// Copyright " << year
         << " the V8 project authors. All rights reserved.\n"
            "// Use of this source code is governed by a BSD-style license "
            "that can be\n"
            "// found in the LICENSE file.\n"
            "\n"
            "// Flags: --wasm-staging --wasm-inlining-call-indirect\n"
            "\n"
            "d8.file.execute('test/mjsunit/wasm/wasm-module-builder.js');\n"
            "\n"
            "const builder = new WasmModuleBuilder();";
    out_.NextLine(0);

    // Module name, if present.
    if (module_->name.is_set()) {
      out_ << "builder.setName('";
      PrintName(module_->name);
      out_ << "');";
      out_.NextLine(0);
    }

    // Types.
    // TODO(14616): Support shared types.

    // Support self-referential and mutually-recursive types.
    std::vector<uint32_t> needed_at(module_->types.size(), kMaxUInt32);
    auto MarkAsNeededHere = [&needed_at](ValueType vt, uint32_t here) {
      if (!vt.is_object_reference()) return;
      HeapType ht = vt.heap_type();
      if (!ht.is_index()) return;
      if (ht.ref_index() < here) return;
      if (needed_at[ht.ref_index()] < here) return;
      needed_at[ht.ref_index()] = here;
    };
    for (uint32_t i = 0; i < module_->types.size(); i++) {
      if (module_->has_struct(i)) {
        const StructType* struct_type = module_->types[i].struct_type;
        for (uint32_t fi = 0; fi < struct_type->field_count(); fi++) {
          MarkAsNeededHere(struct_type->field(fi), i);
        }
      } else if (module_->has_array(i)) {
        MarkAsNeededHere(module_->types[i].array_type->element_type(), i);
      } else {
        DCHECK(module_->has_signature(i));
        const FunctionSig* sig = module_->types[i].function_sig;
        for (size_t pi = 0; pi < sig->parameter_count(); pi++) {
          MarkAsNeededHere(sig->GetParam(pi), i);
        }
        for (size_t ri = 0; ri < sig->return_count(); ri++) {
          MarkAsNeededHere(sig->GetReturn(ri), i);
        }
      }
    }

    uint32_t recgroup_index = 0;
    OffsetsProvider::RecGroup recgroup = offsets_.recgroup(recgroup_index++);
    bool in_explicit_recgroup = false;
    for (uint32_t i = 0; i < module_->types.size(); i++) {
      while (i == recgroup.start_type_index) {
        out_ << "builder.startRecGroup();";
        out_.NextLine(0);
        if (V8_UNLIKELY(recgroup.end_type_index == i)) {
          // Empty recgroup.
          out_ << "builder.endRecGroup();";
          out_.NextLine(0);
          DCHECK(!in_explicit_recgroup);
          recgroup = offsets_.recgroup(recgroup_index++);
          continue;
        } else {
          in_explicit_recgroup = true;
          break;
        }
      }
      uint32_t end_index =
          recgroup.end_type_index != OffsetsProvider::RecGroup::kInvalid
              ? recgroup.end_type_index
              : i + 1;
      for (uint32_t pre = i; pre < end_index; pre++) {
        if (needed_at[pre] == i) {
          out_ << "let ";
          names()->PrintTypeVariableName(out_, pre);
          if (pre == i) {
            out_ << " = builder.nextTypeIndex();";
          } else {
            out_ << " = builder.nextTypeIndex() + " << (pre - i) << ";";
          }
          out_.NextLine(0);
        }
      }
      uint32_t supertype = module_->types[i].supertype;
      bool is_final = module_->types[i].is_final;
      if (needed_at[i] == kMaxUInt32) {
        out_ << "let ";
        names()->PrintTypeVariableName(out_, i);
        out_ << " = ";
      } else {
        out_ << "/* ";
        names()->PrintTypeVariableName(out_, i);
        out_ << " */ ";
      }
      if (module_->has_struct(i)) {
        const StructType* struct_type = module_->types[i].struct_type;
        out_ << "builder.addStruct([";
        for (uint32_t fi = 0; fi < struct_type->field_count(); fi++) {
          if (fi > 0) out_ << ", ";
          out_ << "makeField(";
          names()->PrintValueType(out_, struct_type->field(fi), kEmitObjects);
          out_ << ", " << (struct_type->mutability(fi) ? "true" : "false");
          out_ << ")";
        }
        out_ << "], ";
        if (supertype != kNoSuperType) {
          names()->PrintTypeIndex(out_, supertype, kEmitObjects);
        } else {
          out_ << "kNoSuperType";
        }
        out_ << ", " << (is_final ? "true" : "false") << ");";
        out_.NextLine(0);
      } else if (module_->has_array(i)) {
        const ArrayType* array_type = module_->types[i].array_type;
        out_ << "builder.addArray(";
        names()->PrintValueType(out_, array_type->element_type(), kEmitObjects);
        out_ << ", ";
        out_ << (array_type->mutability() ? "true" : "false") << ", ";
        if (supertype != kNoSuperType) {
          names()->PrintTypeIndex(out_, supertype, kEmitObjects);
        } else {
          out_ << "kNoSuperType";
        }
        out_ << ", " << (is_final ? "true" : "false") << ");";
        out_.NextLine(0);
      } else {
        DCHECK(module_->has_signature(i));
        const FunctionSig* sig = module_->types[i].function_sig;
        out_ << "builder.addType(";
        names()->PrintMakeSignature(out_, sig);
        if (!is_final || supertype != kNoSuperType) {
          out_ << ", ";
          if (supertype != kNoSuperType) {
            names()->PrintTypeIndex(out_, supertype, kEmitObjects);
          } else {
            out_ << "kNoSuperType";
          }
          if (!is_final) out_ << ", false";
        }
        out_ << ");";
        out_.NextLine(0);
      }
      if (in_explicit_recgroup && i == recgroup.end_type_index - 1) {
        in_explicit_recgroup = false;
        out_ << "builder.endRecGroup();";
        out_.NextLine(0);
        recgroup = offsets_.recgroup(recgroup_index++);
      }
    }
    while (recgroup.valid()) {
      // There could be empty recgroups at the end of the type section.
      DCHECK_GE(recgroup.start_type_index, module_->types.size());
      DCHECK_EQ(recgroup.start_type_index, recgroup.end_type_index);
      out_ << "builder.startRecgroup();\nbuilder.endRecGroup();";
      out_.NextLine(0);
      recgroup = offsets_.recgroup(recgroup_index++);
    }

    // Imports.
    for (const WasmImport& imported : module_->import_table) {
      out_ << "let ";
      switch (imported.kind) {
        case kExternalFunction:
          names()->PrintFunctionVariableName(out_, imported.index);
          out_ << " = builder.addImport('" << V(imported.module_name);
          out_ << "', '" << V(imported.field_name) << "', ";
          names()->PrintTypeIndex(
              out_, module_->functions[imported.index].sig_index, kEmitObjects);
          break;

        case kExternalTable: {
          names()->PrintTableName(out_, imported.index);
          out_ << " = builder.addImportedTable('" << V(imported.module_name);
          out_ << "', '" << V(imported.field_name) << "', ";
          const WasmTable& table = module_->tables[imported.index];
          out_ << table.initial_size << ", ";
          if (table.has_maximum_size) {
            out_ << table.maximum_size << ", ";
          } else {
            out_ << "undefined, ";
          }
          names()->PrintValueType(out_, table.type, kEmitObjects);
          break;
        }
        case kExternalGlobal: {
          names()->PrintGlobalName(out_, imported.index);
          out_ << " = builder.addImportedGlobal('" << V(imported.module_name);
          out_ << "', '" << V(imported.field_name) << "', ";
          const WasmGlobal& global = module_->globals[imported.index];
          names()->PrintValueType(out_, global.type, kEmitObjects);
          if (global.mutability || global.shared) {
            out_ << ", " << (global.mutability ? "true" : "false");
          }
          if (global.shared) out_ << ", true";
          break;
        }
        case kExternalMemory: {
          names()->PrintMemoryName(out_, imported.index);
          out_ << " = builder.addImportedMemory('" << V(imported.module_name);
          out_ << "', '" << V(imported.field_name) << "', ";
          const WasmMemory& memory = module_->memories[imported.index];
          out_ << memory.initial_pages << ", ";
          if (memory.has_maximum_pages) {
            out_ << memory.maximum_pages << ", ";
          } else {
            out_ << "undefined, ";
          }
          out_ << (memory.is_shared ? "true" : "false");
          if (memory.is_memory64) out_ << ", true";
          break;
        }
        case kExternalTag: {
          names()->PrintTagName(out_, imported.index);
          out_ << " = builder.addImportedTag('" << V(imported.module_name);
          out_ << "', '" << V(imported.field_name) << "', ";
          names()->PrintTypeIndex(out_, module_->tags[imported.index].sig_index,
                                  kEmitObjects);
          break;
        }
      }
      out_ << ");";
      out_.NextLine(0);
    }

    // Declare functions (without bodies).
    //
    // TODO(jkummerow): We need function variables to be defined in case they
    // are used by init expressions, element segments, or in function bodies.
    // For now, we just declare all functions up front. We could do this
    // selectively (in the interest of conciseness), if we performed a pre-scan
    // of the module to find functions that are referenced by index anywhere.
    //
    // For testing, we ensure that the order of exports remains the same.
    // So when there are non-function imports, we don't annotate functions
    // as exported right away, but postpone that until the exports section.
    // This behavior is not required for correctness, it just helps with
    // differential testing (roundtripping a module through `wami --mjsunit`
    // and `d8 --dump-wasm-module`).
    static constexpr bool kMaintainExportOrder = true;
    bool export_functions_late = false;
    if constexpr (kMaintainExportOrder) {
      for (const WasmExport& ex : module_->export_table) {
        if (ex.kind != kExternalFunction ||
            module_->functions[ex.index].imported) {
          export_functions_late = true;
          break;
        }
      }
    }
    for (const WasmFunction& func : module_->functions) {
      if (func.imported) continue;
      uint32_t index = func.func_index;
      out_ << "let ";
      names()->PrintFunctionVariableName(out_, index);
      out_ << " = builder.addFunction(";
      if (names()->HasFunctionName(index)) {
        out_ << '"';
        names()->PrintFunctionName(out_, index);
        out_ << '"';
      } else {
        out_ << "undefined";
      }
      out_ << ", ";
      out_ << "$sig" << func.sig_index;
      out_ << ")";
      if (func.exported && !export_functions_late) {
        for (const WasmExport& ex : module_->export_table) {
          if (ex.kind != kExternalFunction || ex.index != index) continue;
          if (names()->FunctionNameEquals(index, ex.name)) {
            out_ << ".exportFunc();";
          } else {
            out_ << ".exportAs('";
            PrintName(ex.name);
            out_ << "');";
          }
        }
      } else {
        out_ << ";";
      }
      out_.NextLine(0);
    }

    // Start function.
    if (module_->start_function_index >= 0) {
      out_ << "builder.addStart(";
      names()->PrintFunctionReference(out_, module_->start_function_index);
      out_ << ");";
      out_.NextLine(0);
    }

    // Memories.
    for (const WasmMemory& memory : module_->memories) {
      if (memory.imported) continue;
      out_ << "let ";
      names()->PrintMemoryName(out_, memory.index);
      if (memory.is_memory64) {
        out_ << " = builder.addMemory64(";
      } else {
        out_ << " = builder.addMemory(";
      }
      out_ << memory.initial_pages;
      if (memory.has_maximum_pages) {
        out_ << ", " << memory.maximum_pages;
      } else {
        out_ << ", undefined";
      }
      if (memory.is_shared) {
        out_ << ", true";
      }
      out_ << ");";
      out_.NextLine(0);
    }

    // Data segments.
    for (uint32_t i = 0; i < module_->data_segments.size(); i++) {
      const WasmDataSegment& segment = module_->data_segments[i];
      base::Vector<const uint8_t> data = wire_bytes_.module_bytes().SubVector(
          segment.source.offset(), segment.source.end_offset());
      out_ << "let ";
      names()->PrintDataSegmentName(out_, i);
      if (segment.active) {
        out_ << " = builder.addActiveDataSegment(" << segment.memory_index
             << ", ";
        DecodeAndAppendInitExpr(segment.dest_addr, kWasmI32);
        out_ << ", ";
      } else {
        out_ << " = builder.addPassiveDataSegment(";
      }
      out_ << "[";
      uint32_t num_bytes = static_cast<uint32_t>(data.size());
      if (num_bytes > 0) out_ << uint32_t{data[0]};
      for (uint32_t i = 1; i < num_bytes; i++) {
        out_ << ", " << uint32_t{data[i]};
      }
      out_ << "]";
      if (segment.shared) out_ << ", true";
      out_ << ");";
      out_.NextLine(0);
    }

    // Globals.
    for (uint32_t i = module_->num_imported_globals;
         i < module_->globals.size(); i++) {
      const WasmGlobal& global = module_->globals[i];
      out_ << "let ";
      names()->PrintGlobalName(out_, i);
      out_ << " = builder.addGlobal(";
      names()->PrintValueType(out_, global.type, kEmitObjects);
      out_ << ", " << (global.mutability ? "true" : "false") << ", ";
      out_ << (global.shared ? "true" : "false") << ", ";
      DecodeAndAppendInitExpr(global.init, global.type);
      if (!kMaintainExportOrder && global.exported) {
        out_ << ").exportAs('";
        PrintExportName(kExternalGlobal, i);
        out_ << "'";
      }
      out_ << ");";
      out_.NextLine(0);
    }

    // Tables.
    for (uint32_t i = module_->num_imported_tables; i < module_->tables.size();
         i++) {
      const WasmTable& table = module_->tables[i];
      out_ << "let ";
      names()->PrintTableName(out_, i);
      out_ << " = builder.addTable(";
      names()->PrintValueType(out_, table.type, kEmitObjects);
      out_ << ", " << table.initial_size << ", ";
      if (table.has_maximum_size) {
        out_ << table.maximum_size;
      } else {
        out_ << "undefined";
      }
      if (table.initial_value.is_set()) {
        out_ << ", ";
        DecodeAndAppendInitExpr(table.initial_value, table.type);
      } else if (table.shared) {
        out_ << ", undefined";
      }
      if (table.shared) out_ << ", true";
      if (!kMaintainExportOrder && table.exported) {
        out_ << ").exportAs('";
        PrintExportName(kExternalTable, i);
        out_ << "'";
      }
      out_ << ");";
      out_.NextLine(0);
    }

    // Element segments.
    for (uint32_t i = 0; i < module_->elem_segments.size(); i++) {
      const WasmElemSegment& segment = module_->elem_segments[i];
      out_ << "let ";
      names()->PrintElementSegmentName(out_, i);
      if (segment.status == WasmElemSegment::kStatusActive) {
        out_ << " = builder.addActiveElementSegment(";
        names()->PrintTableReference(out_, segment.table_index);
        out_ << ", ";
        DecodeAndAppendInitExpr(segment.offset, kWasmI32);
        out_ << ", ";
      } else if (segment.status == WasmElemSegment::kStatusPassive) {
        out_ << " = builder.addPassiveElementSegment(";
      } else {
        DCHECK_EQ(segment.status, WasmElemSegment::kStatusDeclarative);
        out_ << " = builder.addDeclarativeElementSegment(";
      }
      out_ << "[";
      ModuleDecoderImpl decoder(WasmEnabledFeatures::All(),
                                wire_bytes_.module_bytes(),
                                ModuleOrigin::kWasmOrigin);
      // This implementation detail is load-bearing: if we simply let the
      // {decoder} start at this offset, it could produce WireBytesRefs that
      // start at offset 0, which violates DCHECK-guarded assumptions.
      decoder.consume_bytes(segment.elements_wire_bytes_offset);
      for (uint32_t j = 0; j < segment.element_count; j++) {
        if (j > 0) out_ << ", ";
        ConstantExpression expr = decoder.consume_element_segment_entry(
            const_cast<WasmModule*>(module_), segment);
        if (segment.element_type == WasmElemSegment::kExpressionElements) {
          DecodeAndAppendInitExpr(expr, segment.type);
        } else {
          names()->PrintFunctionReference(out_, expr.index());
        }
      }
      out_ << "]";
      if (segment.element_type == WasmElemSegment::kExpressionElements) {
        out_ << ", ";
        names()->PrintValueType(out_, segment.type, kEmitObjects);
      }
      if (segment.shared) out_ << ", true";
      out_ << ");";
      out_.NextLine(0);
    }

    // Tags.
    for (uint32_t i = module_->num_imported_tags; i < module_->tags.size();
         i++) {
      const WasmTag& tag = module_->tags[i];
      out_ << "let ";
      names()->PrintTagName(out_, i);
      out_ << " = builder.addTag(";
      // The signature was already emitted as one of the types.
      // TODO(jkummerow): For conciseness, consider pre-scanning signatures
      // that are only used by tags, and using {PrintMakeSignature(
      // tag.ToFunctionSig())} here.
      names()->PrintSigType(out_, tag.sig_index, kEmitObjects);
      out_ << ");";
      out_.NextLine(0);
    }

    // Functions.
    for (const WasmFunction& func : module_->functions) {
      if (func.imported) continue;
      uint32_t index = func.func_index;

      // Header and signature.
      out_.NextLine(0);
      out_ << "// func ";
      names_provider_->PrintFunctionName(out_, index, NamesProvider::kDevTools);
      names()->PrintSignatureComment(out_, func.sig);
      out_.NextLine(0);

      names()->PrintFunctionVariableName(out_, index);

      base::Vector<const uint8_t> func_code =
          wire_bytes_.GetFunctionBytes(&func);

      // Locals and body.
      bool shared = module_->types[func.sig_index].is_shared;
      WasmDetectedFeatures detected;
      MjsunitFunctionDis d(&zone_, module_, index, shared, &detected, func.sig,
                           func_code.begin(), func_code.end(),
                           func.code.offset(), &mjsunit_names_,
                           Indentation{2, 2});
      d.WriteMjsunit(out_);
      if (d.failed()) has_error_ = true;
    }
    out_.NextLine(0);

    // Exports.
    bool added_any_export = false;
    for (const WasmExport& ex : module_->export_table) {
      switch (ex.kind) {
        case kExternalFunction:
          if (!export_functions_late &&
              !module_->functions[ex.index].imported) {
            continue;  // Handled above.
          }
          out_ << "builder.addExport('";
          PrintName(ex.name);
          out_ << "', ";
          names()->PrintFunctionReference(out_, ex.index);
          out_ << ");";
          break;
        case kExternalMemory:
          out_ << "builder.exportMemoryAs('";
          PrintName(ex.name);
          out_ << "', ";
          names()->PrintMemoryName(out_, ex.index);
          out_ << ");";
          break;
        case kExternalGlobal:
          if (!kMaintainExportOrder &&
              ex.index >= module_->num_imported_globals) {
            continue;
          }
          out_ << "builder.addExportOfKind('";
          PrintName(ex.name);
          out_ << "', kExternalGlobal, ";
          names()->PrintGlobalReference(out_, ex.index);
          out_ << ");";
          break;
        case kExternalTable:
          if (!kMaintainExportOrder &&
              ex.index >= module_->num_imported_tables) {
            continue;
          }
          out_ << "builder.addExportOfKind('";
          PrintName(ex.name);
          out_ << "', kExternalTable, ";
          names()->PrintTableReference(out_, ex.index);
          out_ << ");";
          break;
        case kExternalTag:
          out_ << "builder.addExportOfKind('";
          PrintName(ex.name);
          out_ << "', kExternalTag, ";
          names()->PrintTagName(out_, ex.index);
          out_ << ");";
          break;
      }
      out_.NextLine(0);
      added_any_export = true;
    }

    // Instantiate and invoke.
    if (added_any_export) out_.NextLine(0);
    bool compiles = !has_error_;
    if (compiles) {
      out_ << "let kBuiltins = { builtins: ['js-string', 'text-decoder', "
              "'text-encoder'] };\n"
              "const instance = builder.instantiate({}, kBuiltins);\n"
              "try {\n"
              "  print(instance.exports.main(1, 2, 3));\n"
              "} catch (e) {\n"
              "  print('caught exception', e);\n"
              "}";
      out_.NextLine(0);
    } else {
      out_ << "assertThrows(() => builder.instantiate(), "
              "WebAssembly.CompileError);";
      out_.NextLine(0);
    }
  }

 private:
  base::Vector<const char> V(WireBytesRef ref) {
    return {reinterpret_cast<const char*>(wire_bytes_.start()) + ref.offset(),
            ref.length()};
  }
  void PrintName(WireBytesRef ref) {
    out_.write(wire_bytes_.start() + ref.offset(), ref.length());
  }

  void PrintExportName(ImportExportKindCode kind, uint32_t index) {
    for (const WasmExport& ex : module_->export_table) {
      if (ex.kind != kind || ex.index != index) continue;
      PrintName(ex.name);
    }
  }

  void DecodeAndAppendInitExpr(ConstantExpression init, ValueType expected) {
    switch (init.kind()) {
      case ConstantExpression::kEmpty:
        UNREACHABLE();
      case ConstantExpression::kI32Const:
        out_ << "wasmI32Const(" << init.i32_value() << ")";
        break;
      case ConstantExpression::kRefNull:
        out_ << "[kExprRefNull, ";
        names()->PrintHeapType(out_, HeapType(init.repr()), kEmitWireBytes);
        out_ << "]";
        break;
      case ConstantExpression::kRefFunc:
        out_ << "[kExprRefFunc, ";
        names()->PrintFunctionReferenceLeb(out_, init.index());
        out_ << "]";
        break;
      case ConstantExpression::kWireBytesRef: {
        WireBytesRef ref = init.wire_bytes_ref();
        const uint8_t* start = wire_bytes_.start() + ref.offset();
        const uint8_t* end = start + ref.length();
        auto sig = FixedSizeSignature<ValueType>::Returns(expected);
        WasmDetectedFeatures detected;
        MjsunitFunctionDis d(&zone_, module_, 0, false, &detected, &sig, start,
                             end, ref.offset(), &mjsunit_names_,
                             Indentation{0, 0});
        d.DecodeGlobalInitializer(out_);
        if (d.failed()) has_error_ = true;
        break;
      }
    }
  }

  MjsunitNamesProvider* names() { return &mjsunit_names_; }

  MultiLineStringBuilder& out_;
  const WasmModule* module_;
  NamesProvider* names_provider_;
  MjsunitNamesProvider mjsunit_names_;
  OffsetsProvider offsets_;
  const ModuleWireBytes wire_bytes_;
  Zone zone_;
  bool has_error_{false};
};

}  // namespace v8::internal::wasm

#endif  // V8_TOOLS_WASM_MJSUNIT_MODULE_DISASSEMBLER_IMPL_H_
