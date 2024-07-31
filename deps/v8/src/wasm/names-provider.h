// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#if !V8_ENABLE_WEBASSEMBLY
#error This header should only be included if WebAssembly is enabled.
#endif  // !V8_ENABLE_WEBASSEMBLY

#ifndef V8_WASM_NAMES_PROVIDER_H_
#define V8_WASM_NAMES_PROVIDER_H_

#include <map>
#include <string>

#include "src/base/vector.h"
#include "src/wasm/wasm-module.h"

namespace v8 {
namespace internal {
namespace wasm {

class DecodedNameSection;
class StringBuilder;

class V8_EXPORT_PRIVATE NamesProvider {
 public:
  // {kWasmInternal}: only return raw name from name section.
  // {kDevTools}: prepend '$', use import/export names as fallback,
  // or "$funcN" as default.
  enum FunctionNamesBehavior : bool { kWasmInternal = false, kDevTools = true };

  enum IndexAsComment : bool {
    kDontPrintIndex = false,
    kIndexAsComment = true
  };

  NamesProvider(const WasmModule* module,
                base::Vector<const uint8_t> wire_bytes);
  ~NamesProvider();

  void PrintFunctionName(StringBuilder& out, uint32_t function_index,
                         FunctionNamesBehavior behavior = kWasmInternal,
                         IndexAsComment index_as_comment = kDontPrintIndex);
  void PrintLocalName(StringBuilder& out, uint32_t function_index,
                      uint32_t local_index,
                      IndexAsComment index_as_comment = kDontPrintIndex);
  void PrintLabelName(StringBuilder& out, uint32_t function_index,
                      uint32_t label_index, uint32_t fallback_index);
  void PrintTypeName(StringBuilder& out, uint32_t type_index,
                     IndexAsComment index_as_comment = kDontPrintIndex);
  void PrintTableName(StringBuilder& out, uint32_t table_index,
                      IndexAsComment index_as_comment = kDontPrintIndex);
  void PrintMemoryName(StringBuilder& out, uint32_t memory_index,
                       IndexAsComment index_as_comment = kDontPrintIndex);
  void PrintGlobalName(StringBuilder& out, uint32_t global_index,
                       IndexAsComment index_as_comment = kDontPrintIndex);
  void PrintElementSegmentName(
      StringBuilder& out, uint32_t element_segment_index,
      IndexAsComment index_as_comment = kDontPrintIndex);
  void PrintDataSegmentName(StringBuilder& out, uint32_t data_segment_index,
                            IndexAsComment index_as_comment = kDontPrintIndex);
  void PrintFieldName(StringBuilder& out, uint32_t struct_index,
                      uint32_t field_index,
                      IndexAsComment index_as_comment = kDontPrintIndex);
  void PrintTagName(StringBuilder& out, uint32_t tag_index,
                    IndexAsComment index_as_comment = kDontPrintIndex);

  void PrintHeapType(StringBuilder& out, HeapType type);
  void PrintValueType(StringBuilder& out, ValueType type);

  size_t EstimateCurrentMemoryConsumption() const;

 private:
  void DecodeNamesIfNotYetDone();
  void ComputeFunctionNamesFromImportsExports();
  void ComputeNamesFromImportsExports();
  void ComputeImportName(const WasmImport& import,
                         std::map<uint32_t, std::string>& target);
  void ComputeExportName(const WasmExport& ex,
                         std::map<uint32_t, std::string>& target);
  void WriteRef(StringBuilder& out, WireBytesRef ref);

  // Lazy loading must guard against concurrent modifications from multiple
  // {WasmModuleObject}s.
  base::Mutex mutex_;
  bool has_decoded_{false};
  bool has_computed_function_import_names_{false};
  bool has_computed_import_names_{false};
  const WasmModule* module_;
  base::Vector<const uint8_t> wire_bytes_;
  std::unique_ptr<DecodedNameSection> name_section_names_;
  std::map<uint32_t, std::string> import_export_function_names_;
  std::map<uint32_t, std::string> import_export_table_names_;
  std::map<uint32_t, std::string> import_export_memory_names_;
  std::map<uint32_t, std::string> import_export_global_names_;
  std::map<uint32_t, std::string> import_export_tag_names_;
};

}  // namespace wasm
}  // namespace internal
}  // namespace v8

#endif  // V8_WASM_NAMES_PROVIDER_H_
