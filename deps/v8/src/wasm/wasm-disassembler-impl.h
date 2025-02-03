// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#if !V8_ENABLE_WEBASSEMBLY
#error This header should only be included if WebAssembly is enabled.
#endif  // !V8_ENABLE_WEBASSEMBLY

#ifndef V8_WASM_WASM_DISASSEMBLER_IMPL_H_
#define V8_WASM_WASM_DISASSEMBLER_IMPL_H_

#include "src/wasm/function-body-decoder-impl.h"
#include "src/wasm/names-provider.h"
#include "src/wasm/string-builder-multiline.h"
#include "src/wasm/wasm-opcodes.h"
#include "src/zone/zone.h"

namespace v8 {
namespace internal {
namespace wasm {

template <typename ValidationTag>
class ImmediatesPrinter;

using IndexAsComment = NamesProvider::IndexAsComment;

////////////////////////////////////////////////////////////////////////////////
// Configuration flags for aspects of behavior where we might want to change
// our minds. {true} is the legacy DevTools behavior.
constexpr bool kSkipFunctionTypesInTypeSection = true;
constexpr IndexAsComment kIndicesAsComments = NamesProvider::kIndexAsComment;
constexpr bool kSkipDataSegmentNames = true;

////////////////////////////////////////////////////////////////////////////////
// Helpers.

class Indentation {
 public:
  Indentation(int current, int delta) : current_(current), delta_(delta) {
    DCHECK_GE(current, 0);
    DCHECK_GE(delta, 0);
  }

  Indentation Extra(int extra) { return {current_ + extra, delta_}; }

  void increase() { current_ += delta_; }
  void decrease() {
    DCHECK_GE(current_, delta_);
    current_ -= delta_;
  }
  int current() { return current_; }

 private:
  int current_;
  int delta_;
};

inline StringBuilder& operator<<(StringBuilder& sb, Indentation indentation) {
  char* ptr = sb.allocate(indentation.current());
  memset(ptr, ' ', indentation.current());
  return sb;
}

inline StringBuilder& operator<<(StringBuilder& sb, uint64_t n) {
  if (n == 0) {
    *sb.allocate(1) = '0';
    return sb;
  }
  static constexpr size_t kBufferSize = 20;  // Just enough for a uint64.
  char buffer[kBufferSize];
  char* end = buffer + kBufferSize;
  char* out = end;
  while (n != 0) {
    *(--out) = '0' + (n % 10);
    n /= 10;
  }
  sb.write(out, static_cast<size_t>(end - out));
  return sb;
}

V8_EXPORT_PRIVATE void PrintSignatureOneLine(
    StringBuilder& out, const FunctionSig* sig, uint32_t func_index,
    NamesProvider* names, bool param_names,
    IndexAsComment indices_as_comments = NamesProvider::kDontPrintIndex);

////////////////////////////////////////////////////////////////////////////////
// OffsetsProvider.

class OffsetsProvider : public ITracer {
 public:
  struct RecGroup {
    uint32_t offset{kInvalid};
    uint32_t start_type_index{kInvalid};
    uint32_t end_type_index{kInvalid};  // Exclusive.

    // For convenience: built-in support for "maybe" values, useful at the
    // end of iteration.
    static constexpr uint32_t kInvalid = ~0u;
    static constexpr RecGroup Invalid() { return {}; }
    bool valid() { return start_type_index != kInvalid; }
  };

  OffsetsProvider() = default;

  // All-in-one, expects to be called on a freshly constructed {OffsetsProvider}
  // when the {WasmModule} already exists.
  // The alternative is to pass an {OffsetsProvider} as a tracer to the initial
  // decoding of the wire bytes, letting it record offsets on the fly.
  V8_EXPORT_PRIVATE void CollectOffsets(const WasmModule* module,
                                        base::Vector<const uint8_t> wire_bytes);

  void TypeOffset(uint32_t offset) override { type_offsets_.push_back(offset); }

  void ImportOffset(uint32_t offset) override {
    import_offsets_.push_back(offset);
  }

  void TableOffset(uint32_t offset) override {
    table_offsets_.push_back(offset);
  }

  void MemoryOffset(uint32_t offset) override { memory_offset_ = offset; }

  void TagOffset(uint32_t offset) override { tag_offsets_.push_back(offset); }

  void GlobalOffset(uint32_t offset) override {
    global_offsets_.push_back(offset);
  }

  void StartOffset(uint32_t offset) override { start_offset_ = offset; }

  void ElementOffset(uint32_t offset) override {
    element_offsets_.push_back(offset);
  }

  void DataOffset(uint32_t offset) override { data_offsets_.push_back(offset); }

  void StringOffset(uint32_t offset) override {
    string_offsets_.push_back(offset);
  }

  void RecGroupOffset(uint32_t offset, uint32_t group_size) override {
    uint32_t start_index = static_cast<uint32_t>(type_offsets_.size());
    recgroups_.push_back({offset, start_index, start_index + group_size});
  }

  void ImportsDone(const WasmModule* module) override {
    num_imported_tables_ = module->num_imported_tables;
    num_imported_globals_ = module->num_imported_globals;
    num_imported_tags_ = module->num_imported_tags;
  }

  // Unused by this tracer:
  void Bytes(const uint8_t* start, uint32_t count) override {}
  void Description(const char* desc) override {}
  void Description(const char* desc, size_t length) override {}
  void Description(uint32_t number) override {}
  void Description(ValueType type) override {}
  void Description(HeapType type) override {}
  void Description(const FunctionSig* sig) override {}
  void NextLine() override {}
  void NextLineIfFull() override {}
  void NextLineIfNonEmpty() override {}
  void InitializerExpression(const uint8_t* start, const uint8_t* end,
                             ValueType expected_type) override {}
  void FunctionBody(const WasmFunction* func, const uint8_t* start) override {}
  void FunctionName(uint32_t func_index) override {}
  void NameSection(const uint8_t* start, const uint8_t* end,
                   uint32_t offset) override {}

#define GETTER(name)                        \
  uint32_t name##_offset(uint32_t index) {  \
    DCHECK(index < name##_offsets_.size()); \
    return name##_offsets_[index];          \
  }
  GETTER(type)
  GETTER(import)
  GETTER(element)
  GETTER(data)
  GETTER(string)
#undef GETTER

#define IMPORT_ADJUSTED_GETTER(name)                                  \
  uint32_t name##_offset(uint32_t index) {                            \
    DCHECK(index >= num_imported_##name##s_ &&                        \
           index - num_imported_##name##s_ < name##_offsets_.size()); \
    return name##_offsets_[index - num_imported_##name##s_];          \
  }
  IMPORT_ADJUSTED_GETTER(table)
  IMPORT_ADJUSTED_GETTER(tag)
  IMPORT_ADJUSTED_GETTER(global)
#undef IMPORT_ADJUSTED_GETTER

  uint32_t memory_offset() { return memory_offset_; }

  uint32_t start_offset() { return start_offset_; }

  RecGroup recgroup(uint32_t index) {
    if (index >= recgroups_.size()) return RecGroup::Invalid();
    return recgroups_[index];
  }

 private:
  uint32_t num_imported_tables_{0};
  uint32_t num_imported_globals_{0};
  uint32_t num_imported_tags_{0};
  std::vector<uint32_t> type_offsets_;
  std::vector<uint32_t> import_offsets_;
  std::vector<uint32_t> table_offsets_;
  std::vector<uint32_t> tag_offsets_;
  std::vector<uint32_t> global_offsets_;
  std::vector<uint32_t> element_offsets_;
  std::vector<uint32_t> data_offsets_;
  std::vector<uint32_t> string_offsets_;
  uint32_t memory_offset_{0};
  uint32_t start_offset_{0};
  std::vector<RecGroup> recgroups_;
};

inline std::unique_ptr<OffsetsProvider> AllocateOffsetsProvider() {
  return std::make_unique<OffsetsProvider>();
}

////////////////////////////////////////////////////////////////////////////////
// FunctionBodyDisassembler.

class V8_EXPORT_PRIVATE FunctionBodyDisassembler
    : public WasmDecoder<Decoder::FullValidationTag> {
 public:
  using ValidationTag = Decoder::FullValidationTag;
  enum FunctionHeader : bool { kSkipHeader = false, kPrintHeader = true };

  FunctionBodyDisassembler(Zone* zone, const WasmModule* module,
                           uint32_t func_index, bool shared,
                           WasmDetectedFeatures* detected,
                           const FunctionSig* sig, const uint8_t* start,
                           const uint8_t* end, uint32_t offset,
                           const ModuleWireBytes wire_bytes,
                           NamesProvider* names)
      : WasmDecoder<ValidationTag>(zone, module, WasmEnabledFeatures::All(),
                                   detected, sig, shared, start, end, offset),
        func_index_(func_index),
        wire_bytes_(wire_bytes),
        names_(names) {}

  void DecodeAsWat(MultiLineStringBuilder& out, Indentation indentation,
                   FunctionHeader include_header = kPrintHeader,
                   uint32_t* first_instruction_offset = nullptr);

  void DecodeGlobalInitializer(StringBuilder& out);

  std::set<uint32_t>& used_types() { return used_types_; }

 protected:
  WasmOpcode GetOpcode();

  uint32_t PrintImmediatesAndGetLength(StringBuilder& out);

  void PrintHexNumber(StringBuilder& out, uint64_t number);

  LabelInfo& label_info(int depth) {
    return label_stack_[label_stack_.size() - 1 - depth];
  }

  friend class ImmediatesPrinter<ValidationTag>;
  uint32_t func_index_;
  WasmOpcode current_opcode_ = kExprUnreachable;
  const ModuleWireBytes wire_bytes_;
  NamesProvider* names_;
  std::set<uint32_t> used_types_;
  std::vector<LabelInfo> label_stack_;
  MultiLineStringBuilder* out_;
  // Labels use two different indexing systems: for looking them up in the
  // name section, they're indexed by order of occurrence; for generating names
  // like "$label0", the order in which they show up as targets of branch
  // instructions is used for generating consecutive names.
  // (This is legacy wasmparser behavior; we could change it.)
  uint32_t label_occurrence_index_ = 0;
  uint32_t label_generation_index_ = 0;
};

////////////////////////////////////////////////////////////////////////////////
// ModuleDisassembler.

class ModuleDisassembler {
 public:
  V8_EXPORT_PRIVATE ModuleDisassembler(
      MultiLineStringBuilder& out, const WasmModule* module,
      NamesProvider* names, const ModuleWireBytes wire_bytes,
      AccountingAllocator* allocator,
      std::unique_ptr<OffsetsProvider> offsets_provider = {},
      std::vector<int>* function_body_offsets = nullptr);
  V8_EXPORT_PRIVATE ~ModuleDisassembler();

  V8_EXPORT_PRIVATE void PrintTypeDefinition(uint32_t type_index,
                                             Indentation indendation,
                                             IndexAsComment index_as_comment);
  V8_EXPORT_PRIVATE void PrintModule(Indentation indentation, size_t max_mb);

 private:
  void PrintImportName(const WasmImport& import);
  void PrintExportName(ImportExportKindCode kind, uint32_t index);
  void PrintMutableType(bool mutability, ValueType type);
  void PrintTable(const WasmTable& table);
  void PrintMemory(const WasmMemory& memory);
  void PrintGlobal(const WasmGlobal& global);
  void PrintInitExpression(const ConstantExpression& init,
                           ValueType expected_type);
  void PrintTagSignature(const FunctionSig* sig);
  void PrintString(WireBytesRef ref);
  void PrintStringAsJSON(WireBytesRef ref);
  void LineBreakOrSpace(bool break_lines, Indentation indentation,
                        uint32_t byte_offset);

  MultiLineStringBuilder& out_;
  const WasmModule* module_;
  NamesProvider* names_;
  const ModuleWireBytes wire_bytes_;
  const uint8_t* start_;
  Zone zone_;
  std::unique_ptr<OffsetsProvider> offsets_;
  std::vector<int>* function_body_offsets_;
};

}  // namespace wasm
}  // namespace internal
}  // namespace v8

#endif  // V8_WASM_WASM_DISASSEMBLER_IMPL_H_
