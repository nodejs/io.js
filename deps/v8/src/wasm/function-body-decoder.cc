// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/wasm/function-body-decoder.h"

#include "src/utils/ostreams.h"
#include "src/wasm/decoder.h"
#include "src/wasm/function-body-decoder-impl.h"
#include "src/wasm/wasm-engine.h"
#include "src/wasm/wasm-limits.h"
#include "src/wasm/wasm-linkage.h"
#include "src/wasm/wasm-module.h"
#include "src/wasm/wasm-opcodes-inl.h"

namespace v8 {
namespace internal {
namespace wasm {

template <typename ValidationTag>
bool DecodeLocalDecls(WasmFeatures enabled, BodyLocalDecls* decls,
                      const WasmModule* module, bool is_shared,
                      const uint8_t* start, const uint8_t* end, Zone* zone) {
  if constexpr (ValidationTag::validate) DCHECK_NOT_NULL(module);
  WasmFeatures no_features = WasmFeatures::None();
  constexpr FixedSizeSignature<ValueType, 0, 0> kNoSig;
  WasmDecoder<ValidationTag> decoder(zone, module, enabled, &no_features,
                                     &kNoSig, is_shared, start, end);
  decls->encoded_size = decoder.DecodeLocals(decoder.pc());
  if (ValidationTag::validate && decoder.failed()) {
    DCHECK_EQ(0, decls->encoded_size);
    return false;
  }
  DCHECK(decoder.ok());
  // Copy the decoded locals types into {decls->local_types}.
  DCHECK_NULL(decls->local_types);
  decls->num_locals = decoder.num_locals_;
  decls->local_types = decoder.local_types_;
  return true;
}

void DecodeLocalDecls(WasmFeatures enabled, BodyLocalDecls* decls,
                      const uint8_t* start, const uint8_t* end, Zone* zone) {
  constexpr WasmModule* kNoModule = nullptr;
  DecodeLocalDecls<Decoder::NoValidationTag>(enabled, decls, kNoModule, false,
                                             start, end, zone);
}

bool ValidateAndDecodeLocalDeclsForTesting(WasmFeatures enabled,
                                           BodyLocalDecls* decls,
                                           const WasmModule* module,
                                           bool is_shared, const uint8_t* start,
                                           const uint8_t* end, Zone* zone) {
  return DecodeLocalDecls<Decoder::BooleanValidationTag>(
      enabled, decls, module, is_shared, start, end, zone);
}

BytecodeIterator::BytecodeIterator(const uint8_t* start, const uint8_t* end)
    : Decoder(start, end) {}

BytecodeIterator::BytecodeIterator(const uint8_t* start, const uint8_t* end,
                                   BodyLocalDecls* decls, Zone* zone)
    : Decoder(start, end) {
  DCHECK_NOT_NULL(decls);
  DCHECK_NOT_NULL(zone);
  DecodeLocalDecls(WasmFeatures::All(), decls, start, end, zone);
  pc_ += decls->encoded_size;
  if (pc_ > end_) pc_ = end_;
}

DecodeResult ValidateFunctionBody(Zone* zone, WasmFeatures enabled,
                                  const WasmModule* module,
                                  WasmFeatures* detected,
                                  const FunctionBody& body) {
  // Asm.js functions should never be validated; they are valid by design.
  DCHECK_EQ(kWasmOrigin, module->origin);
  WasmFullDecoder<Decoder::FullValidationTag, EmptyInterface> decoder(
      zone, module, enabled, detected, body);
  decoder.Decode();
  return decoder.toResult(nullptr);
}

unsigned OpcodeLength(const uint8_t* pc, const uint8_t* end) {
  WasmFeatures unused_detected_features;
  Zone* no_zone = nullptr;
  WasmModule* no_module = nullptr;
  FunctionSig* no_sig = nullptr;
  constexpr bool kIsShared = false;
  WasmDecoder<Decoder::NoValidationTag> decoder(
      no_zone, no_module, WasmFeatures::All(), &unused_detected_features,
      no_sig, kIsShared, pc, end, 0);
  return WasmDecoder<Decoder::NoValidationTag>::OpcodeLength(&decoder, pc);
}

bool CheckHardwareSupportsSimd() { return CpuFeatures::SupportsWasmSimd128(); }

void PrintRawWasmCode(const uint8_t* start, const uint8_t* end) {
  AccountingAllocator allocator;
  constexpr bool kIsShared = false;  // TODO(14616): Is this correct?
  PrintRawWasmCode(&allocator, FunctionBody{nullptr, 0, start, end, kIsShared},
                   nullptr, kPrintLocals);
}

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

bool PrintRawWasmCode(AccountingAllocator* allocator, const FunctionBody& body,
                      const WasmModule* module, PrintLocals print_locals) {
  StdoutStream os;
  return PrintRawWasmCode(allocator, body, module, print_locals, os);
}

bool PrintRawWasmCode(AccountingAllocator* allocator, const FunctionBody& body,
                      const WasmModule* module, PrintLocals print_locals,
                      std::ostream& os, std::vector<int>* line_numbers) {
  Zone zone(allocator, ZONE_NAME);
  WasmFeatures unused_detected_features = WasmFeatures::None();
  WasmDecoder<Decoder::NoValidationTag> decoder(
      &zone, module, WasmFeatures::All(), &unused_detected_features, body.sig,
      body.is_shared, body.start, body.end);
  int line_nr = 0;
  constexpr int kNoByteCode = -1;

  // Print the function signature.
  if (body.sig) {
    os << "// signature: " << *body.sig << std::endl;
    if (line_numbers) line_numbers->push_back(kNoByteCode);
    ++line_nr;
  }

  // Print the local declarations.
  BodyLocalDecls decls;
  BytecodeIterator i(body.start, body.end, &decls, &zone);
  if (body.start != i.pc() && print_locals == kPrintLocals) {
    os << "// locals:";
    if (decls.num_locals > 0) {
      ValueType type = decls.local_types[0];
      uint32_t count = 0;
      for (size_t pos = 0; pos < decls.num_locals; ++pos) {
        if (decls.local_types[pos] == type) {
          ++count;
        } else {
          os << " " << count << " " << type.name();
          type = decls.local_types[pos];
          count = 1;
        }
      }
      os << " " << count << " " << type.name();
    }
    os << std::endl;
    if (line_numbers) line_numbers->push_back(kNoByteCode);
    ++line_nr;

    for (const uint8_t* locals = body.start; locals < i.pc(); locals++) {
      os << (locals == body.start ? "0x" : " 0x") << AsHex(*locals, 2) << ",";
    }
    os << std::endl;
    if (line_numbers) line_numbers->push_back(kNoByteCode);
    ++line_nr;
  }

  os << "// body:" << std::endl;
  if (line_numbers) line_numbers->push_back(kNoByteCode);
  ++line_nr;
  unsigned control_depth = 0;
  for (; i.has_next(); i.next()) {
    unsigned length =
        WasmDecoder<Decoder::NoValidationTag>::OpcodeLength(&decoder, i.pc());

    unsigned offset = 1;
    WasmOpcode opcode = i.current();
    WasmOpcode prefix = kExprUnreachable;
    bool has_prefix = WasmOpcodes::IsPrefixOpcode(opcode);
    if (has_prefix) {
      prefix = i.current();
      opcode = i.prefixed_opcode();
      offset = 2;
    }
    if (line_numbers) line_numbers->push_back(i.position());
    if (opcode == kExprElse || opcode == kExprCatch ||
        opcode == kExprCatchAll || opcode == kExprDelegate) {
      control_depth--;
    }

    int num_whitespaces = control_depth < 32 ? 2 * control_depth : 64;

    // 64 whitespaces
    const char* padding =
        "                                                                ";
    os.write(padding, num_whitespaces);

    if (has_prefix) {
      os << PrefixName(prefix) << ", ";
    }

    if (WasmOpcodes::IsRelaxedSimdOpcode(opcode)) {
      // Expand multi-byte opcodes.
      os << "...";
      offset += 1;
    }
    os << RawOpcodeName(opcode) << ",";

    if (opcode == kExprLoop || opcode == kExprIf || opcode == kExprBlock ||
        opcode == kExprTry) {
      if (i.pc()[1] & 0x80) {
        auto [type, temp_length] =
            value_type_reader::read_value_type<Decoder::NoValidationTag>(
                &decoder, i.pc() + 1, WasmFeatures::All());
        if (temp_length == 1) {
          os << type.name() << ",";
        } else {
          // TODO(manoskouk): Improve this for rtts and (nullable) refs.
          for (unsigned j = offset; j < length; ++j) {
            os << " 0x" << AsHex(i.pc()[j], 2) << ",";
          }
        }
      } else {
        for (unsigned j = offset; j < length; ++j) {
          os << " 0x" << AsHex(i.pc()[j], 2) << ",";
        }
      }
    } else {
      for (unsigned j = offset; j < length; ++j) {
        os << " 0x" << AsHex(i.pc()[j], 2) << ",";
      }
    }

    os << "  // " << WasmOpcodes::OpcodeName(opcode);

    switch (opcode) {
      case kExprElse:
      case kExprCatch:
      case kExprCatchAll:
        os << " @" << i.pc_offset();
        control_depth++;
        break;
      case kExprLoop:
      case kExprIf:
      case kExprBlock:
      case kExprTry: {
        BlockTypeImmediate imm(WasmFeatures::All(), &i, i.pc() + 1,
                               Decoder::kNoValidation);
        os << " @" << i.pc_offset();
        CHECK(decoder.Validate(i.pc() + 1, imm));
        for (uint32_t j = 0; j < imm.out_arity(); j++) {
          os << " " << imm.out_type(j).name();
        }
        control_depth++;
        break;
      }
      case kExprTryTable: {
        BlockTypeImmediate block_imm(WasmFeatures::All(), &i, i.pc() + 1,
                                     Decoder::kNoValidation);
        TryTableImmediate imm(&i, i.pc() + block_imm.length + 1,
                              Decoder::kNoValidation);
        os << " entries=" << imm.table_count;
        control_depth++;
        break;
      }
      case kExprEnd:
        os << " @" << i.pc_offset();
        control_depth--;
        break;
      case kExprBr: {
        BranchDepthImmediate imm(&i, i.pc() + 1, Decoder::kNoValidation);
        os << " depth=" << imm.depth;
        break;
      }
      case kExprBrIf: {
        BranchDepthImmediate imm(&i, i.pc() + 1, Decoder::kNoValidation);
        os << " depth=" << imm.depth;
        break;
      }
      case kExprBrTable: {
        BranchTableImmediate imm(&i, i.pc() + 1, Decoder::kNoValidation);
        os << " entries=" << imm.table_count;
        break;
      }
      case kExprCallIndirect: {
        CallIndirectImmediate imm(&i, i.pc() + 1, Decoder::kNoValidation);
        os << " sig #" << imm.sig_imm.index;
        CHECK(decoder.Validate(i.pc() + 1, imm));
        os << ": " << *imm.sig;
        break;
      }
      case kExprCallRef: {
        SigIndexImmediate imm(&i, i.pc() + 1, Decoder::kNoValidation);
        CHECK(decoder.Validate(i.pc() + 1, imm));
        os << ": " << *imm.sig;
        break;
      }
      case kExprCallFunction: {
        CallFunctionImmediate imm(&i, i.pc() + 1, Decoder::kNoValidation);
        os << " function #" << imm.index;
        CHECK(decoder.Validate(i.pc() + 1, imm));
        os << ": " << *imm.sig;
        break;
      }
      default:
        break;
    }
    os << std::endl;
    ++line_nr;
  }
  DCHECK(!line_numbers || line_numbers->size() == static_cast<size_t>(line_nr));
  USE(line_nr);

  return decoder.ok();
}

BitVector* AnalyzeLoopAssignmentForTesting(Zone* zone, uint32_t num_locals,
                                           const uint8_t* start,
                                           const uint8_t* end,
                                           bool* loop_is_innermost) {
  WasmFeatures no_features = WasmFeatures::None();
  constexpr bool kIsShared = false;  // TODO(14616): Extend this.
  WasmDecoder<Decoder::FullValidationTag> decoder(zone, nullptr, no_features,
                                                  &no_features, nullptr,
                                                  kIsShared, start, end, 0);
  return WasmDecoder<Decoder::FullValidationTag>::AnalyzeLoopAssignment(
      &decoder, start, num_locals, zone, loop_is_innermost);
}

}  // namespace wasm
}  // namespace internal
}  // namespace v8
