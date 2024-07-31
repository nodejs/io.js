// Copyright 2011 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/diagnostics/disassembler.h"

#include <algorithm>
#include <iomanip>
#include <memory>
#include <sstream>
#include <unordered_map>
#include <vector>

#include "src/base/memory.h"
#include "src/base/strings.h"
#include "src/base/vector.h"
#include "src/codegen/assembler-inl.h"
#include "src/codegen/code-comments.h"
#include "src/codegen/code-reference.h"
#include "src/codegen/external-reference-encoder.h"
#include "src/codegen/macro-assembler.h"
#include "src/debug/debug.h"
#include "src/deoptimizer/deoptimizer.h"
#include "src/diagnostics/disasm.h"
#include "src/execution/isolate-data.h"
#include "src/ic/ic.h"
#include "src/objects/objects-inl.h"
#include "src/snapshot/embedded/embedded-data.h"
#include "src/strings/string-stream.h"

#if V8_ENABLE_WEBASSEMBLY
#include "src/wasm/wasm-code-manager.h"
#include "src/wasm/wasm-engine.h"
#endif  // V8_ENABLE_WEBASSEMBLY

namespace v8 {
namespace internal {

#ifdef ENABLE_DISASSEMBLER

class V8NameConverter : public disasm::NameConverter {
 public:
  explicit V8NameConverter(Isolate* isolate, CodeReference code = {})
      : isolate_(isolate), code_(code) {}
  const char* NameOfAddress(uint8_t* pc) const override;
  const char* NameInCode(uint8_t* addr) const override;
  const char* RootRelativeName(int offset) const override;

  const CodeReference& code() const { return code_; }

 private:
  void InitExternalRefsCache() const;

  Isolate* isolate_;
  CodeReference code_;

  base::EmbeddedVector<char, 128> v8_buffer_;

  // Map from root-register relative offset of the external reference value to
  // the external reference name (stored in the external reference table).
  // This cache is used to recognize [root_reg + offs] patterns as direct
  // access to certain external reference's value.
  mutable std::unordered_map<int, const char*> directly_accessed_external_refs_;
};

void V8NameConverter::InitExternalRefsCache() const {
  ExternalReferenceTable* external_reference_table =
      isolate_->external_reference_table();
  if (!external_reference_table->is_initialized()) return;

  base::AddressRegion addressable_region =
      isolate_->root_register_addressable_region();
  Address isolate_root = isolate_->isolate_root();

  for (uint32_t i = 0; i < ExternalReferenceTable::kSize; i++) {
    Address address = external_reference_table->address(i);
    if (addressable_region.contains(address)) {
      int offset = static_cast<int>(address - isolate_root);
      const char* name = external_reference_table->name(i);
      directly_accessed_external_refs_.insert({offset, name});
    }
  }
}

const char* V8NameConverter::NameOfAddress(uint8_t* pc) const {
  if (!code_.is_null()) {
    const char* name =
        isolate_ ? isolate_->builtins()->Lookup(reinterpret_cast<Address>(pc))
                 : nullptr;

    if (name != nullptr) {
      SNPrintF(v8_buffer_, "%p  (%s)", static_cast<void*>(pc), name);
      return v8_buffer_.begin();
    }

    int offs = static_cast<int>(reinterpret_cast<Address>(pc) -
                                code_.instruction_start());
    // print as code offset, if it seems reasonable
    if (0 <= offs && offs < code_.instruction_size()) {
      SNPrintF(v8_buffer_, "%p  <+0x%x>", static_cast<void*>(pc), offs);
      return v8_buffer_.begin();
    }

#if V8_ENABLE_WEBASSEMBLY
    if (auto* wasm_code = wasm::GetWasmCodeManager()->LookupCode(
            isolate_, reinterpret_cast<Address>(pc))) {
      SNPrintF(v8_buffer_, "%p  (%s)", static_cast<void*>(pc),
               wasm::GetWasmCodeKindAsString(wasm_code->kind()));
      return v8_buffer_.begin();
    }
#endif  // V8_ENABLE_WEBASSEMBLY
  }

  return disasm::NameConverter::NameOfAddress(pc);
}

const char* V8NameConverter::NameInCode(uint8_t* addr) const {
  // The V8NameConverter is used for well known code, so we can "safely"
  // dereference pointers in generated code.
  return code_.is_null() ? "" : reinterpret_cast<const char*>(addr);
}

const char* V8NameConverter::RootRelativeName(int offset) const {
  if (isolate_ == nullptr) return nullptr;

  const int kRootsTableStart = IsolateData::roots_table_offset();
  const unsigned kRootsTableSize = sizeof(RootsTable);
  const int kExtRefsTableStart = IsolateData::external_reference_table_offset();
  const unsigned kExtRefsTableSize = ExternalReferenceTable::kSizeInBytes;
  const int kBuiltinTier0TableStart = IsolateData::builtin_tier0_table_offset();
  const unsigned kBuiltinTier0TableSize =
      Builtins::kBuiltinTier0Count * kSystemPointerSize;
  const int kBuiltinTableStart = IsolateData::builtin_table_offset();
  const unsigned kBuiltinTableSize =
      Builtins::kBuiltinCount * kSystemPointerSize;

  if (static_cast<unsigned>(offset - kRootsTableStart) < kRootsTableSize) {
    uint32_t offset_in_roots_table = offset - kRootsTableStart;

    // Fail safe in the unlikely case of an arbitrary root-relative offset.
    if (offset_in_roots_table % kSystemPointerSize != 0) return nullptr;

    RootIndex root_index =
        static_cast<RootIndex>(offset_in_roots_table / kSystemPointerSize);

    SNPrintF(v8_buffer_, "root (%s)", RootsTable::name(root_index));
    return v8_buffer_.begin();
  } else if (static_cast<unsigned>(offset - kExtRefsTableStart) <
             kExtRefsTableSize) {
    uint32_t offset_in_extref_table = offset - kExtRefsTableStart;

    // Fail safe in the unlikely case of an arbitrary root-relative offset.
    if (offset_in_extref_table % ExternalReferenceTable::kEntrySize != 0) {
      return nullptr;
    }

    // Likewise if the external reference table is uninitialized.
    if (!isolate_->external_reference_table()->is_initialized()) {
      return nullptr;
    }

    SNPrintF(v8_buffer_, "external reference (%s)",
             isolate_->external_reference_table()->NameFromOffset(
                 offset_in_extref_table));
    return v8_buffer_.begin();
  } else if (static_cast<unsigned>(offset - kBuiltinTier0TableStart) <
             kBuiltinTier0TableSize) {
    uint32_t offset_in_builtins_table = (offset - kBuiltinTier0TableStart);

    Builtin builtin =
        Builtins::FromInt(offset_in_builtins_table / kSystemPointerSize);
    const char* name = Builtins::name(builtin);
    SNPrintF(v8_buffer_, "builtin (%s)", name);
    return v8_buffer_.begin();
  } else if (static_cast<unsigned>(offset - kBuiltinTableStart) <
             kBuiltinTableSize) {
    uint32_t offset_in_builtins_table = (offset - kBuiltinTableStart);

    Builtin builtin =
        Builtins::FromInt(offset_in_builtins_table / kSystemPointerSize);
    const char* name = Builtins::name(builtin);
    SNPrintF(v8_buffer_, "builtin (%s)", name);
    return v8_buffer_.begin();
  } else {
    // It must be a direct access to one of the external values.
    if (directly_accessed_external_refs_.empty()) {
      InitExternalRefsCache();
    }

    auto iter = directly_accessed_external_refs_.find(offset);
    if (iter != directly_accessed_external_refs_.end()) {
      SNPrintF(v8_buffer_, "external value (%s)", iter->second);
      return v8_buffer_.begin();
    }
    return nullptr;
  }
}

// Output the contents of the string stream and empty it.
static void DumpBuffer(std::ostream& os, std::ostringstream& out) {
  os << out.str() << std::endl;
  out.str("");
}

static const int kRelocInfoPosition = 57;

static void PrintRelocInfo(std::ostringstream& out, Isolate* isolate,
                           const ExternalReferenceEncoder* ref_encoder,
                           std::ostream& os, CodeReference host,
                           RelocInfo* relocinfo, bool first_reloc_info = true) {
  // Indent the printing of the reloc info.
  int padding = kRelocInfoPosition;
  if (first_reloc_info) {
    // The first reloc info is printed after the disassembled instruction.
    padding -= std::min(padding, static_cast<int>(out.tellp()));
  } else {
    // Additional reloc infos are printed on separate lines.
    DumpBuffer(os, out);
  }
  std::fill_n(std::ostream_iterator<char>(out), padding, ' ');

  RelocInfo::Mode rmode = relocinfo->rmode();
  if (rmode == RelocInfo::DEOPT_SCRIPT_OFFSET) {
    out << "    ;; debug: deopt position, script offset '"
        << static_cast<int>(relocinfo->data()) << "'";
  } else if (rmode == RelocInfo::DEOPT_INLINING_ID) {
    out << "    ;; debug: deopt position, inlining id '"
        << static_cast<int>(relocinfo->data()) << "'";
  } else if (rmode == RelocInfo::DEOPT_REASON) {
    DeoptimizeReason reason = static_cast<DeoptimizeReason>(relocinfo->data());
    out << "    ;; debug: deopt reason '" << DeoptimizeReasonToString(reason)
        << "'";
  } else if (rmode == RelocInfo::DEOPT_ID) {
    out << "    ;; debug: deopt index " << static_cast<int>(relocinfo->data());
  } else if (rmode == RelocInfo::DEOPT_NODE_ID) {
#ifdef DEBUG
    out << "    ;; debug: deopt node id "
        << static_cast<uint32_t>(relocinfo->data());
#else   // DEBUG
    UNREACHABLE();
#endif  // DEBUG
  } else if (RelocInfo::IsEmbeddedObjectMode(rmode)) {
    HeapStringAllocator allocator;
    StringStream accumulator(&allocator);
    ShortPrint(relocinfo->target_object(isolate), &accumulator);
    std::unique_ptr<char[]> obj_name = accumulator.ToCString();
    const bool is_compressed = RelocInfo::IsCompressedEmbeddedObject(rmode);
    out << "    ;; " << (is_compressed ? "(compressed) " : "")
        << "object: " << obj_name.get();
  } else if (rmode == RelocInfo::EXTERNAL_REFERENCE) {
    Address address = relocinfo->target_external_reference();
    const char* reference_name =
        ref_encoder
            ? ref_encoder->NameOfAddress(isolate, address)
            : ExternalReferenceTable::NameOfIsolateIndependentAddress(address);
    out << "    ;; external reference (" << reference_name << ")";
  } else if (RelocInfo::IsCodeTargetMode(rmode)) {
    out << "    ;; code:";
    Tagged<Code> code =
        isolate->heap()->FindCodeForInnerPointer(relocinfo->target_address());
    CodeKind kind = code->kind();
    if (code->is_builtin()) {
      out << " Builtin::" << Builtins::name(code->builtin_id());
    } else {
      out << " " << CodeKindToString(kind);
    }
#if V8_ENABLE_WEBASSEMBLY
  } else if (RelocInfo::IsWasmStubCall(rmode) && host.is_wasm_code()) {
    // Host is isolate-independent, try wasm native module instead.
    const char* runtime_stub_name = Builtins::name(
        host.as_wasm_code()->native_module()->GetBuiltinInJumptableSlot(
            relocinfo->wasm_stub_call_address()));
    out << "    ;; wasm stub: " << runtime_stub_name;
#endif  // V8_ENABLE_WEBASSEMBLY
  } else {
    out << "    ;; " << RelocInfo::RelocModeName(rmode);
  }
}

static int DecodeIt(Isolate* isolate, ExternalReferenceEncoder* ref_encoder,
                    std::ostream& os, CodeReference code,
                    const V8NameConverter& converter, uint8_t* begin,
                    uint8_t* end, Address current_pc, size_t range_limit) {
  CHECK(!code.is_null());
  v8::base::EmbeddedVector<char, 128> decode_buffer;
  std::ostringstream out;
  uint8_t* pc = begin;
  disasm::Disassembler d(converter,
                         disasm::Disassembler::kContinueOnUnimplementedOpcode);
  RelocIterator rit(code);
  CodeCommentsIterator cit(code.code_comments(), code.code_comments_size());
  int constants = -1;  // no constants being decoded at the start

  while (pc < end) {
    // First decode instruction so that we know its length.
    uint8_t* prev_pc = pc;
    bool decoding_constant_pool = constants > 0;
    if (decoding_constant_pool) {
      SNPrintF(
          decode_buffer, "%08x       constant",
          base::ReadUnalignedValue<int32_t>(reinterpret_cast<Address>(pc)));
      constants--;
      pc += 4;
    } else {
      int num_const = d.ConstantPoolSizeAt(pc);
      if (num_const >= 0) {
        SNPrintF(
            decode_buffer, "%08x       constant pool begin (num_const = %d)",
            base::ReadUnalignedValue<int32_t>(reinterpret_cast<Address>(pc)),
            num_const);
        constants = num_const;
        pc += 4;
      } else if (!rit.done() &&
                 rit.rinfo()->pc() == reinterpret_cast<Address>(pc) &&
                 rit.rinfo()->rmode() == RelocInfo::INTERNAL_REFERENCE) {
        // A raw pointer embedded in code stream.
        uint8_t* ptr =
            base::ReadUnalignedValue<uint8_t*>(reinterpret_cast<Address>(pc));
        SNPrintF(decode_buffer, "%08" V8PRIxPTR "       jump table entry %4zu",
                 reinterpret_cast<intptr_t>(ptr),
                 static_cast<size_t>(ptr - begin));
        pc += sizeof(ptr);
#ifdef V8_TARGET_ARCH_X64
      } else if (!rit.done() &&
                 rit.rinfo()->pc() == reinterpret_cast<Address>(pc) &&
                 rit.rinfo()->rmode() ==
                     RelocInfo::RELATIVE_SWITCH_TABLE_ENTRY) {
        int target_pc_offset = static_cast<int>(rit.rinfo()->data());
        uint8_t* ptr = begin + target_pc_offset;
        SNPrintF(decode_buffer, "%08" V8PRIxPTR "       jump table entry %4zx",
                 reinterpret_cast<intptr_t>(ptr),
                 static_cast<size_t>(target_pc_offset));
        // We use emitl (4 bytes) for the value in the table.
        pc += 4;
#endif  // V8_TARGET_ARCH_X64
      } else {
        decode_buffer[0] = '\0';
        pc += d.InstructionDecode(decode_buffer, pc);
      }
    }

    Address pc_address = reinterpret_cast<Address>(pc);
    if (range_limit != 0) {
      if (pc_address > current_pc + range_limit) break;
      if (pc_address <= current_pc - range_limit) continue;
    }

    // Collect RelocInfo for this instruction (prev_pc .. pc-1)
    std::vector<const char*> comments;
    std::vector<Address> pcs;
    std::vector<RelocInfo::Mode> rmodes;
    std::vector<intptr_t> datas;
    while (!rit.done() && rit.rinfo()->pc() < reinterpret_cast<Address>(pc)) {
      // Collect all data.
      pcs.push_back(rit.rinfo()->pc());
      rmodes.push_back(rit.rinfo()->rmode());
      datas.push_back(rit.rinfo()->data());
      rit.next();
    }
    while (cit.HasCurrent()) {
      Address cur = cit.GetPCOffset();
      if (cur >= static_cast<Address>(pc - begin)) break;
      if (range_limit == 0 ||
          cur + range_limit > current_pc - reinterpret_cast<Address>(begin)) {
        comments.push_back(cit.GetComment());
      }
      cit.Next();
    }

    // Comments.
    for (size_t i = 0; i < comments.size(); i++) {
      if (v8_flags.log_colour) {
        out << "\033[34m";
      }
      out << "                  " << comments[i];
      if (v8_flags.log_colour) {
        out << "\033[;m";
      }
      DumpBuffer(os, out);
    }

    // Instruction address and instruction offset.
    if (v8_flags.log_colour &&
        reinterpret_cast<Address>(prev_pc) == current_pc) {
      // If this is the given "current" pc, make it yellow and bold.
      out << "\033[33;1m";
    }
    out << static_cast<void*>(prev_pc) << "  " << std::setw(4) << std::hex
        << prev_pc - begin << "  ";

    // Instruction.
    out << decode_buffer.begin();

    // Print all the reloc info for this instruction which are not comments.
    for (size_t i = 0; i < pcs.size(); i++) {
      // Put together the reloc info.
      const CodeReference& host = code;
      Address constant_pool =
          host.is_null() ? kNullAddress : host.constant_pool();
      Handle<Code> code_handle;
      if (host.is_code()) {
        code_handle = host.as_code();

        RelocInfo relocinfo(pcs[i], rmodes[i], datas[i], constant_pool);
        bool first_reloc_info = (i == 0);
        PrintRelocInfo(out, isolate, ref_encoder, os, code, &relocinfo,
                       first_reloc_info);
      }
    }

    // If this is a constant pool load and we haven't found any RelocInfo
    // already, check if we can find some RelocInfo for the target address in
    // the constant pool.
    // Make sure we're also not currently in the middle of decoding a constant
    // pool itself, rather than a contant pool load. Since it can store any
    // bytes, a constant could accidentally match with the bit-pattern checked
    // by IsInConstantPool() below.
    if (pcs.empty() && !code.is_null() && !decoding_constant_pool) {
      RelocInfo dummy_rinfo(reinterpret_cast<Address>(prev_pc),
                            RelocInfo::NO_INFO);
      if (dummy_rinfo.IsInConstantPool()) {
        Address constant_pool_entry_address =
            dummy_rinfo.constant_pool_entry_address();
        RelocIterator reloc_it(code);
        while (!reloc_it.done()) {
          if (reloc_it.rinfo()->IsInConstantPool() &&
              (reloc_it.rinfo()->constant_pool_entry_address() ==
               constant_pool_entry_address)) {
            PrintRelocInfo(out, isolate, ref_encoder, os, code,
                           reloc_it.rinfo());
            break;
          }
          reloc_it.next();
        }
      }
    }

    if (v8_flags.log_colour &&
        reinterpret_cast<Address>(prev_pc) == current_pc) {
      out << "\033[m";
    }

    DumpBuffer(os, out);
  }

  // Emit comments following the last instruction (if any).
  while (cit.HasCurrent()) {
    Address cur = cit.GetPCOffset();
    if (range_limit == 0 ||
        cur + range_limit == current_pc - reinterpret_cast<Address>(begin)) {
      out << "                  " << cit.GetComment();
      DumpBuffer(os, out);
    }
    cit.Next();
  }

  return static_cast<int>(pc - begin);
}

int Disassembler::Decode(Isolate* isolate, std::ostream& os, uint8_t* begin,
                         uint8_t* end, CodeReference code, Address current_pc,
                         size_t range_limit) {
  DCHECK_WITH_MSG(v8_flags.text_is_readable,
                  "Builtins disassembly requires a readable .text section");
  V8NameConverter v8NameConverter(isolate, code);
  if (isolate) {
    // We have an isolate, so support external reference names from V8 and
    // embedder.
    SealHandleScope shs(isolate);
    DisallowGarbageCollection no_alloc;
    ExternalReferenceEncoder ref_encoder(isolate);
    return DecodeIt(isolate, &ref_encoder, os, code, v8NameConverter, begin,
                    end, current_pc, range_limit);
  } else {
    // No isolate => isolate-independent code. Only V8 External references
    // available.
    return DecodeIt(nullptr, nullptr, os, code, v8NameConverter, begin, end,
                    current_pc, range_limit);
  }
}

#else  // ENABLE_DISASSEMBLER

int Disassembler::Decode(Isolate* isolate, std::ostream& os, uint8_t* begin,
                         uint8_t* end, CodeReference code, Address current_pc,
                         size_t range_limit) {
  return 0;
}

#endif  // ENABLE_DISASSEMBLER

}  // namespace internal
}  // namespace v8
