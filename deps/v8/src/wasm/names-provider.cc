// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/wasm/names-provider.h"

#include "src/strings/unicode-decoder.h"
#include "src/wasm/canonical-types.h"
#include "src/wasm/module-decoder.h"
#include "src/wasm/std-object-sizes.h"
#include "src/wasm/string-builder.h"
#include "src/wasm/wasm-code-manager.h"
#include "src/wasm/wasm-engine.h"

namespace v8 {
namespace internal {
namespace wasm {

NamesProvider::NamesProvider(const WasmModule* module,
                             base::Vector<const uint8_t> wire_bytes)
    : module_(module), wire_bytes_(wire_bytes) {}

NamesProvider::~NamesProvider() = default;

void NamesProvider::DecodeNamesIfNotYetDone() {
  base::SpinningMutexGuard lock(&mutex_);
  if (has_decoded_) return;
  has_decoded_ = true;
  name_section_names_.reset(
      new DecodedNameSection(wire_bytes_, module_->name_section));
  ComputeNamesFromImportsExports();
}

// Function names are generally handled separately from other names; in
// particular we support decoding function names without decoding any other
// names, in which case also computing fallback names from imports and exports
// must happen separately.
void NamesProvider::ComputeFunctionNamesFromImportsExports() {
  DCHECK(!has_computed_function_import_names_);
  has_computed_function_import_names_ = true;
  // When tracing streaming compilations, we might not yet have wire bytes.
  if (wire_bytes_.empty()) return;
  for (const WasmImport& import : module_->import_table) {
    if (import.kind != kExternalFunction) continue;
    if (module_->lazily_generated_names.Has(import.index)) continue;
    ComputeImportName(import, import_export_function_names_);
  }
  for (const WasmExport& ex : module_->export_table) {
    if (ex.kind != kExternalFunction) continue;
    if (module_->lazily_generated_names.Has(ex.index)) continue;
    ComputeExportName(ex, import_export_function_names_);
  }
}

void NamesProvider::ComputeNamesFromImportsExports() {
  DCHECK(!has_computed_import_names_);
  has_computed_import_names_ = true;
  // When tracing streaming compilations, we might not yet have wire bytes.
  if (wire_bytes_.empty()) return;
  DCHECK(has_decoded_);
  for (const WasmImport import : module_->import_table) {
    switch (import.kind) {
      case kExternalFunction:
        continue;  // Functions are handled separately.
      case kExternalTable:
        if (name_section_names_->table_names_.Has(import.index)) continue;
        ComputeImportName(import, import_export_table_names_);
        break;
      case kExternalMemory:
        if (name_section_names_->memory_names_.Has(import.index)) continue;
        ComputeImportName(import, import_export_memory_names_);
        break;
      case kExternalGlobal:
        if (name_section_names_->global_names_.Has(import.index)) continue;
        ComputeImportName(import, import_export_global_names_);
        break;
      case kExternalTag:
        if (name_section_names_->tag_names_.Has(import.index)) continue;
        ComputeImportName(import, import_export_tag_names_);
        break;
    }
  }
  for (const WasmExport& ex : module_->export_table) {
    switch (ex.kind) {
      case kExternalFunction:
        continue;  // Functions are handled separately.
      case kExternalTable:
        if (name_section_names_->table_names_.Has(ex.index)) continue;
        ComputeExportName(ex, import_export_table_names_);
        break;
      case kExternalMemory:
        if (name_section_names_->memory_names_.Has(ex.index)) continue;
        ComputeExportName(ex, import_export_memory_names_);
        break;
      case kExternalGlobal:
        if (name_section_names_->global_names_.Has(ex.index)) continue;
        ComputeExportName(ex, import_export_global_names_);
        break;
      case kExternalTag:
        if (name_section_names_->tag_names_.Has(ex.index)) continue;
        ComputeExportName(ex, import_export_tag_names_);
        break;
    }
  }
}

namespace {
// Any disallowed characters get replaced with '_'. Reference:
// https://webassembly.github.io/spec/core/text/values.html#text-id
static constexpr char kIdentifierChar[] = {
    '_', '!', '_', '#', '$',  '%', '&', '\'',  // --
    '_', '_', '*', '+', '_',  '-', '.', '/',   // --
    '0', '1', '2', '3', '4',  '5', '6', '7',   // --
    '8', '9', ':', '_', '<',  '=', '>', '?',   // --
    '@', 'A', 'B', 'C', 'D',  'E', 'F', 'G',   // --
    'H', 'I', 'J', 'K', 'L',  'M', 'N', 'O',   // --
    'P', 'Q', 'R', 'S', 'T',  'U', 'V', 'W',   // --
    'X', 'Y', 'Z', '_', '\\', '_', '^', '_',   // --
    '`', 'a', 'b', 'c', 'd',  'e', 'f', 'g',   // --
    'h', 'i', 'j', 'k', 'l',  'm', 'n', 'o',   // --
    'p', 'q', 'r', 's', 't',  'u', 'v', 'w',   // --
    'x', 'y', 'z', '_', '|',  '_', '~', '_',   // --
};

// To match legacy wasmparser behavior, we emit one '_' per invalid UTF16
// code unit.
// We could decide that we don't care much how exactly non-ASCII names are
// rendered and simplify this to "one '_' per invalid UTF8 byte".
void SanitizeUnicodeName(StringBuilder& out, const uint8_t* utf8_src,
                         size_t length) {
  if (length == 0) return;  // Illegal nullptrs arise below when length == 0.
  base::Vector<const uint8_t> utf8_data(utf8_src, length);
  Utf8Decoder decoder(utf8_data);
  std::vector<uint16_t> utf16(decoder.utf16_length());
  decoder.Decode(utf16.data(), utf8_data);
  for (uint16_t c : utf16) {
    if (c < 32 || c >= 127) {
      out << '_';
    } else {
      out << kIdentifierChar[c - 32];
    }
  }
}
}  // namespace

void NamesProvider::ComputeImportName(const WasmImport& import,
                                      std::map<uint32_t, std::string>& target) {
  const uint8_t* mod_start = wire_bytes_.begin() + import.module_name.offset();
  size_t mod_length = import.module_name.length();
  const uint8_t* field_start = wire_bytes_.begin() + import.field_name.offset();
  size_t field_length = import.field_name.length();
  StringBuilder buffer;
  buffer << '$';
  SanitizeUnicodeName(buffer, mod_start, mod_length);
  buffer << '.';
  SanitizeUnicodeName(buffer, field_start, field_length);
  target[import.index] = std::string(buffer.start(), buffer.length());
}

void NamesProvider::ComputeExportName(const WasmExport& ex,
                                      std::map<uint32_t, std::string>& target) {
  if (target.find(ex.index) != target.end()) return;
  size_t length = ex.name.length();
  if (length == 0) return;
  StringBuilder buffer;
  buffer << '$';
  SanitizeUnicodeName(buffer, wire_bytes_.begin() + ex.name.offset(), length);
  target[ex.index] = std::string(buffer.start(), buffer.length());
}

namespace {

V8_INLINE void MaybeAddComment(StringBuilder& out, uint32_t index,
                               bool add_comment) {
  if (add_comment) out << " (;" << index << ";)";
}

}  // namespace

void NamesProvider::WriteRef(StringBuilder& out, WireBytesRef ref) {
  out.write(wire_bytes_.begin() + ref.offset(), ref.length());
}

void NamesProvider::PrintFunctionName(StringBuilder& out,
                                      uint32_t function_index,
                                      FunctionNamesBehavior behavior,
                                      IndexAsComment index_as_comment) {
  // Function names are stored elsewhere, because we need to access them
  // during (streaming) compilation when the NamesProvider isn't ready yet.
  WireBytesRef ref = module_->lazily_generated_names.LookupFunctionName(
      ModuleWireBytes(wire_bytes_), function_index);
  if (ref.is_set()) {
    if (behavior == kDevTools) {
      out << '$';
      WriteRef(out, ref);
      MaybeAddComment(out, function_index, index_as_comment);
    } else {
      // For kWasmInternal behavior, function names don't get a `$` prefix.
      WriteRef(out, ref);
    }
    return;
  }

  if (behavior == kWasmInternal) return;
  {
    base::SpinningMutexGuard lock(&mutex_);
    if (!has_computed_function_import_names_) {
      ComputeFunctionNamesFromImportsExports();
    }
  }
  auto it = import_export_function_names_.find(function_index);
  if (it != import_export_function_names_.end()) {
    out << it->second;
    MaybeAddComment(out, function_index, index_as_comment);
  } else {
    out << "$func" << function_index;
  }
}

WireBytesRef Get(const NameMap& map, uint32_t index) {
  const WireBytesRef* result = map.Get(index);
  if (!result) return {};
  return *result;
}

WireBytesRef Get(const IndirectNameMap& map, uint32_t outer_index,
                 uint32_t inner_index) {
  const NameMap* inner = map.Get(outer_index);
  if (!inner) return {};
  return Get(*inner, inner_index);
}

void NamesProvider::PrintLocalName(StringBuilder& out, uint32_t function_index,
                                   uint32_t local_index,
                                   IndexAsComment index_as_comment) {
  DecodeNamesIfNotYetDone();
  WireBytesRef ref =
      Get(name_section_names_->local_names_, function_index, local_index);
  if (ref.is_set()) {
    out << '$';
    WriteRef(out, ref);
    MaybeAddComment(out, local_index, index_as_comment);
  } else {
    out << "$var" << local_index;
  }
}

void NamesProvider::PrintLabelName(StringBuilder& out, uint32_t function_index,
                                   uint32_t label_index,
                                   uint32_t fallback_index) {
  DecodeNamesIfNotYetDone();
  WireBytesRef ref =
      Get(name_section_names_->label_names_, function_index, label_index);
  if (ref.is_set()) {
    out << '$';
    WriteRef(out, ref);
  } else {
    out << "$label" << fallback_index;
  }
}

void NamesProvider::PrintTypeName(StringBuilder& out, uint32_t type_index,
                                  IndexAsComment index_as_comment) {
  DecodeNamesIfNotYetDone();
  WireBytesRef ref = Get(name_section_names_->type_names_, type_index);
  if (ref.is_set()) {
    out << '$';
    WriteRef(out, ref);
    return MaybeAddComment(out, type_index, index_as_comment);
  }
  out << "$type" << type_index;
}

void NamesProvider::PrintTableName(StringBuilder& out, uint32_t table_index,
                                   IndexAsComment index_as_comment) {
  DecodeNamesIfNotYetDone();
  WireBytesRef ref = Get(name_section_names_->table_names_, table_index);
  if (ref.is_set()) {
    out << '$';
    WriteRef(out, ref);
    return MaybeAddComment(out, table_index, index_as_comment);
  }

  auto it = import_export_table_names_.find(table_index);
  if (it != import_export_table_names_.end()) {
    out << it->second;
    return MaybeAddComment(out, table_index, index_as_comment);
  }
  out << "$table" << table_index;
}

void NamesProvider::PrintMemoryName(StringBuilder& out, uint32_t memory_index,
                                    IndexAsComment index_as_comment) {
  DecodeNamesIfNotYetDone();
  WireBytesRef ref = Get(name_section_names_->memory_names_, memory_index);
  if (ref.is_set()) {
    out << '$';
    WriteRef(out, ref);
    return MaybeAddComment(out, memory_index, index_as_comment);
  }

  auto it = import_export_memory_names_.find(memory_index);
  if (it != import_export_memory_names_.end()) {
    out << it->second;
    return MaybeAddComment(out, memory_index, index_as_comment);
  }

  out << "$memory" << memory_index;
}

void NamesProvider::PrintGlobalName(StringBuilder& out, uint32_t global_index,
                                    IndexAsComment index_as_comment) {
  DecodeNamesIfNotYetDone();
  WireBytesRef ref = Get(name_section_names_->global_names_, global_index);
  if (ref.is_set()) {
    out << '$';
    WriteRef(out, ref);
    return MaybeAddComment(out, global_index, index_as_comment);
  }

  auto it = import_export_global_names_.find(global_index);
  if (it != import_export_global_names_.end()) {
    out << it->second;
    return MaybeAddComment(out, global_index, index_as_comment);
  }

  out << "$global" << global_index;
}

void NamesProvider::PrintElementSegmentName(StringBuilder& out,
                                            uint32_t element_segment_index,
                                            IndexAsComment index_as_comment) {
  DecodeNamesIfNotYetDone();
  WireBytesRef ref =
      Get(name_section_names_->element_segment_names_, element_segment_index);
  if (ref.is_set()) {
    out << '$';
    WriteRef(out, ref);
    MaybeAddComment(out, element_segment_index, index_as_comment);
  } else {
    out << "$elem" << element_segment_index;
  }
}

void NamesProvider::PrintDataSegmentName(StringBuilder& out,
                                         uint32_t data_segment_index,
                                         IndexAsComment index_as_comment) {
  DecodeNamesIfNotYetDone();
  WireBytesRef ref =
      Get(name_section_names_->data_segment_names_, data_segment_index);
  if (ref.is_set()) {
    out << '$';
    WriteRef(out, ref);
    MaybeAddComment(out, data_segment_index, index_as_comment);
  } else {
    out << "$data" << data_segment_index;
  }
}

void NamesProvider::PrintFieldName(StringBuilder& out, uint32_t struct_index,
                                   uint32_t field_index,
                                   IndexAsComment index_as_comment) {
  DecodeNamesIfNotYetDone();
  WireBytesRef ref =
      Get(name_section_names_->field_names_, struct_index, field_index);
  if (ref.is_set()) {
    out << '$';
    WriteRef(out, ref);
    return MaybeAddComment(out, field_index, index_as_comment);
  }
  out << "$field" << field_index;
}

void NamesProvider::PrintTagName(StringBuilder& out, uint32_t tag_index,
                                 IndexAsComment index_as_comment) {
  DecodeNamesIfNotYetDone();
  WireBytesRef ref = Get(name_section_names_->tag_names_, tag_index);
  if (ref.is_set()) {
    out << '$';
    WriteRef(out, ref);
    return MaybeAddComment(out, tag_index, index_as_comment);
  }
  auto it = import_export_tag_names_.find(tag_index);
  if (it != import_export_tag_names_.end()) {
    out << it->second;
    return MaybeAddComment(out, tag_index, index_as_comment);
  }
  out << "$tag" << tag_index;
}

void NamesProvider::PrintHeapType(StringBuilder& out, HeapType type) {
  if (type.is_index()) {
    PrintTypeName(out, type.ref_index());
  } else {
    out << type.name();
  }
}

void NamesProvider::PrintValueType(StringBuilder& out, ValueType type) {
  switch (type.kind()) {
    case kRef:
    case kRefNull:
      if (type.encoding_needs_heap_type()) {
        out << (type.kind() == kRef ? "(ref " : "(ref null ");
        PrintHeapType(out, type.heap_type());
        out << ')';
      } else {
        out << type.heap_type().name() << "ref";
      }
      break;
    case kRtt:
      out << "(rtt ";
      PrintTypeName(out, type.ref_index());
      out << ')';
      break;
    default:
      out << wasm::name(type.kind());
  }
}

namespace {
size_t StringMapSize(const std::map<uint32_t, std::string>& map) {
  size_t result = ContentSize(map);
  for (const auto& entry : map) {
    result += entry.second.size();
  }
  return result;
}
}  // namespace

size_t NamesProvider::EstimateCurrentMemoryConsumption() const {
  UPDATE_WHEN_CLASS_CHANGES(NamesProvider, 168);
  size_t result = sizeof(NamesProvider);
  if (name_section_names_) {
    DecodedNameSection* names = name_section_names_.get();
    result += names->local_names_.EstimateCurrentMemoryConsumption();
    result += names->label_names_.EstimateCurrentMemoryConsumption();
    result += names->type_names_.EstimateCurrentMemoryConsumption();
    result += names->table_names_.EstimateCurrentMemoryConsumption();
    result += names->memory_names_.EstimateCurrentMemoryConsumption();
    result += names->global_names_.EstimateCurrentMemoryConsumption();
    result += names->element_segment_names_.EstimateCurrentMemoryConsumption();
    result += names->data_segment_names_.EstimateCurrentMemoryConsumption();
    result += names->field_names_.EstimateCurrentMemoryConsumption();
    result += names->tag_names_.EstimateCurrentMemoryConsumption();
  }
  {
    base::SpinningMutexGuard lock(&mutex_);
    result += StringMapSize(import_export_function_names_);
    result += StringMapSize(import_export_table_names_);
    result += StringMapSize(import_export_memory_names_);
    result += StringMapSize(import_export_global_names_);
    result += StringMapSize(import_export_tag_names_);
  }
  if (v8_flags.trace_wasm_offheap_memory) {
    PrintF("NamesProvider: %zu\n", result);
  }
  return result;
}

size_t CanonicalTypeNamesProvider::EstimateCurrentMemoryConsumption() const {
  size_t result = sizeof(this) + payload_size_estimate_;
  result += type_names_.capacity() * sizeof(StringT);
  result += ContentSize(field_names_);
  for (const auto& entry : field_names_) {
    const std::vector<StringT>& vec = entry.second;
    result += vec.capacity() * sizeof(StringT);
  }
  if (v8_flags.trace_wasm_offheap_memory) {
    PrintF("CanonicalTypeNamesProvider: %zu\n", result);
  }
  return result;
}

void CanonicalTypeNamesProvider::DecodeNameSections() {
  // TODO(jkummerow): We'll probably need to lock read accesses too.
  base::SpinningMutexGuard lock(&mutex_);
  type_names_.resize(GetTypeCanonicalizer()->GetCurrentNumberOfTypes());
  GetWasmEngine()->DecodeAllNameSections(this);
}

void CanonicalTypeNamesProvider::DecodeNames(NativeModule* native_module) {
  const WasmModule* module = native_module->module();
  if (module->canonical_typenames_decoded) return;
  module->canonical_typenames_decoded = true;
  base::Vector<const uint8_t> wire_bytes = native_module->wire_bytes();
  WireBytesRef name_section = module->name_section;
  if (name_section.is_empty()) return;
  size_t added_size = 0;
  DecodeCanonicalTypeNames(wire_bytes, module, type_names_, field_names_,
                           &added_size);
  payload_size_estimate_ += added_size;
}

void CanonicalTypeNamesProvider::PrintTypeName(
    StringBuilder& out, CanonicalTypeIndex type_index,
    NamesProvider::IndexAsComment index_as_comment) {
  uint32_t index = type_index.index;
  if (index > type_names_.size() || type_names_[index].empty()) {
    DecodeNameSections();
  }
  // {index} should now always be in range, but let's be robust towards
  // invalid parameter values.
  if (index > type_names_.size() || type_names_[index].empty()) {
    out << "$canon" << index;
    return;
  }
  StringT& name = type_names_[index];
  out << '$';
  out.write(name.data(), name.size());
  MaybeAddComment(out, index, index_as_comment);
}

void CanonicalTypeNamesProvider::PrintValueType(StringBuilder& out,
                                                CanonicalValueType type) {
  switch (type.kind()) {
    case kRef:
    case kRefNull:
      if (type.encoding_needs_heap_type()) {
        out << (type.kind() == kRef ? "(ref " : "(ref null ");
        if (type.has_index()) {
          PrintTypeName(out, type.ref_index());
        } else {
          out << type.name();
        }
        out << ')';
      } else {
        out << type.name();
      }
      break;
    case kRtt:
      out << "(rtt ";
      PrintTypeName(out, type.ref_index());
      out << ')';
      break;
    default:
      out << wasm::name(type.kind());
  }
}

void CanonicalTypeNamesProvider::PrintFieldName(StringBuilder& out,
                                                CanonicalTypeIndex struct_index,
                                                uint32_t field_index) {
  uint32_t index = struct_index.index;
  if (index > type_names_.size()) DecodeNameSections();

  auto per_type = field_names_.find(index);
  if (per_type != field_names_.end()) {
    std::vector<StringT>& field_names = per_type->second;
    if (field_index < field_names.size() && !field_names[field_index].empty()) {
      const StringT& name = field_names[field_index];
      out << '$';
      out.write(name.data(), name.size());
      return;
    }
  }
  out << "$field" << field_index;
}

// At the time of this writing, different std::string implementations
// support 15 to 23 characters for inline storage. For accurate tracking
// of memory consumption, dynamically determine this threshold.
size_t CanonicalTypeNamesProvider::DetectInlineStringThreshold() {
  for (size_t i = 0; i < 32; i++) {
    std::string s(i, 'c');
    Address str = reinterpret_cast<Address>(&s);
    Address data = reinterpret_cast<Address>(s.data());
    if (data < str || data >= str + sizeof(s)) return i;
  }
  return 32;
}

}  // namespace wasm
}  // namespace internal
}  // namespace v8
