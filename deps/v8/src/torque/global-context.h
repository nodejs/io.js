// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_TORQUE_GLOBAL_CONTEXT_H_
#define V8_TORQUE_GLOBAL_CONTEXT_H_

#include <map>
#include <memory>

#include "src/base/contextual.h"
#include "src/common/globals.h"
#include "src/torque/ast.h"
#include "src/torque/cpp-builder.h"
#include "src/torque/declarable.h"

namespace v8 {
namespace internal {
namespace torque {

class GlobalContext : public base::ContextualClass<GlobalContext> {
 public:
  GlobalContext(GlobalContext&&) V8_NOEXCEPT = default;
  GlobalContext& operator=(GlobalContext&&) V8_NOEXCEPT = default;
  explicit GlobalContext(Ast ast);

  static Namespace* GetDefaultNamespace() { return Get().default_namespace_; }
  template <class T>
  T* RegisterDeclarable(std::unique_ptr<T> d) {
    T* ptr = d.get();
    declarables_.push_back(std::move(d));
    return ptr;
  }

  static const std::vector<std::unique_ptr<Declarable>>& AllDeclarables() {
    return Get().declarables_;
  }

  static void AddCppInclude(std::string include_path) {
    Get().cpp_includes_.insert(std::move(include_path));
  }
  static const std::set<std::string>& CppIncludes() {
    return Get().cpp_includes_;
  }

  static void SetCollectLanguageServerData() {
    Get().collect_language_server_data_ = true;
  }
  static bool collect_language_server_data() {
    return Get().collect_language_server_data_;
  }
  static void SetCollectKytheData() { Get().collect_kythe_data_ = true; }
  static bool collect_kythe_data() { return Get().collect_kythe_data_; }
  static void SetForceAssertStatements() {
    Get().force_assert_statements_ = true;
  }
  static bool force_assert_statements() {
    return Get().force_assert_statements_;
  }
  static void SetAnnotateIR() { Get().annotate_ir_ = true; }
  static bool annotate_ir() { return Get().annotate_ir_; }
  static Ast* ast() { return &Get().ast_; }
  static std::string MakeUniqueName(const std::string& base) {
    return base + "_" + std::to_string(Get().fresh_ids_[base]++);
  }

  struct PerFileStreams {
    PerFileStreams()
        : file(SourceId::Invalid()),
          csa_header(csa_headerfile),
          csa_cc(csa_ccfile),
          class_definition_cc(class_definition_ccfile) {}
    SourceId file;
    std::stringstream csa_headerfile;
    cpp::File csa_header;
    std::stringstream csa_ccfile;
    cpp::File csa_cc;

    std::stringstream class_definition_headerfile;

    // The beginning of the generated -inl.inc file, which includes declarations
    // for functions corresponding to Torque macros.
    std::stringstream class_definition_inline_headerfile_macro_declarations;
    // The second part of the generated -inl.inc file, which includes
    // definitions for functions declared in the first part.
    std::stringstream class_definition_inline_headerfile_macro_definitions;
    // The portion of the generated -inl.inc file containing member function
    // definitions for the generated class.
    std::stringstream class_definition_inline_headerfile;

    std::stringstream class_definition_ccfile;
    cpp::File class_definition_cc;

    std::set<SourceId> required_builtin_includes;
  };
  static PerFileStreams& GeneratedPerFile(SourceId file) {
    PerFileStreams& result = Get().generated_per_file_[file];
    result.file = file;
    return result;
  }

  static void SetInstanceTypesInitialized() {
    DCHECK(!Get().instance_types_initialized_);
    Get().instance_types_initialized_ = true;
  }
  static bool IsInstanceTypesInitialized() {
    return Get().instance_types_initialized_;
  }
  static void EnsureInCCOutputList(TorqueMacro* macro, SourceId source) {
    GlobalContext& c = Get();
    auto item = std::make_pair(macro, source);
    if (c.macros_for_cc_output_set_.insert(item).second) {
      c.macros_for_cc_output_.push_back(item);
    }
    EnsureInCCDebugOutputList(macro, source);
  }
  static const std::vector<std::pair<TorqueMacro*, SourceId>>&
  AllMacrosForCCOutput() {
    return Get().macros_for_cc_output_;
  }
  static void EnsureInCCDebugOutputList(TorqueMacro* macro, SourceId source) {
    GlobalContext& c = Get();
    auto item = std::make_pair(macro, source);
    if (c.macros_for_cc_debug_output_set_.insert(item).second) {
      c.macros_for_cc_debug_output_.push_back(item);
    }
  }
  static const std::vector<std::pair<TorqueMacro*, SourceId>>&
  AllMacrosForCCDebugOutput() {
    return Get().macros_for_cc_debug_output_;
  }

 private:
  bool collect_language_server_data_;
  bool collect_kythe_data_;
  bool force_assert_statements_;
  bool annotate_ir_;
  Namespace* default_namespace_;
  Ast ast_;
  std::vector<std::unique_ptr<Declarable>> declarables_;
  std::set<std::string> cpp_includes_;
  std::map<SourceId, PerFileStreams> generated_per_file_;
  std::map<std::string, size_t> fresh_ids_;
  std::vector<std::pair<TorqueMacro*, SourceId>> macros_for_cc_output_;
  std::set<std::pair<TorqueMacro*, SourceId>> macros_for_cc_output_set_;
  std::vector<std::pair<TorqueMacro*, SourceId>> macros_for_cc_debug_output_;
  std::set<std::pair<TorqueMacro*, SourceId>> macros_for_cc_debug_output_set_;
  bool instance_types_initialized_ = false;

  friend class LanguageServerData;
};

template <class T>
T* RegisterDeclarable(std::unique_ptr<T> d) {
  return GlobalContext::Get().RegisterDeclarable(std::move(d));
}

class TargetArchitecture : public base::ContextualClass<TargetArchitecture> {
 public:
  explicit TargetArchitecture(bool force_32bit);

  static size_t TaggedSize() { return Get().tagged_size_; }
  static size_t RawPtrSize() { return Get().raw_ptr_size_; }
  static size_t ExternalPointerSize() { return Get().external_ptr_size_; }
  static size_t IndirectPointerSize() { return Get().indirect_ptr_size_; }
  static size_t ProtectedPointerSize() { return TaggedSize(); }
  static size_t MaxHeapAlignment() { return TaggedSize(); }
  static bool ArePointersCompressed() { return TaggedSize() < RawPtrSize(); }
  static int SmiTagAndShiftSize() { return Get().smi_tag_and_shift_size_; }

 private:
  const size_t tagged_size_;
  const size_t raw_ptr_size_;
  const int smi_tag_and_shift_size_;
  const size_t external_ptr_size_;
  const size_t indirect_ptr_size_;
};

}  // namespace torque
}  // namespace internal
}  // namespace v8

#endif  // V8_TORQUE_GLOBAL_CONTEXT_H_
