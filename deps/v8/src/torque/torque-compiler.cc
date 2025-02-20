// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/torque/torque-compiler.h"

#include <fstream>
#include <optional>

#include "src/torque/declarable.h"
#include "src/torque/declaration-visitor.h"
#include "src/torque/global-context.h"
#include "src/torque/implementation-visitor.h"
#include "src/torque/torque-parser.h"
#include "src/torque/type-oracle.h"

namespace v8::internal::torque {

namespace {

std::optional<std::string> ReadFile(const std::string& path) {
  std::ifstream file_stream(path);
  if (!file_stream.good()) return std::nullopt;

  return std::string{std::istreambuf_iterator<char>(file_stream),
                     std::istreambuf_iterator<char>()};
}

void ReadAndParseTorqueFile(const std::string& path) {
  SourceId source_id = SourceFileMap::AddSource(path);
  CurrentSourceFile::Scope source_id_scope(source_id);

  // path might be either a normal file path or an encoded URI.
  auto maybe_content = ReadFile(SourceFileMap::AbsolutePath(source_id));
  if (!maybe_content) {
    if (auto maybe_path = FileUriDecode(path)) {
      maybe_content = ReadFile(*maybe_path);
    }
  }

  if (!maybe_content) {
    Error("Cannot open file path/uri: ", path).Throw();
  }

  ParseTorque(*maybe_content);
}

void CompileCurrentAst(TorqueCompilerOptions options) {
  GlobalContext::Scope global_context(std::move(CurrentAst::Get()));
  if (options.collect_language_server_data) {
    GlobalContext::SetCollectLanguageServerData();
  }
  if (options.collect_kythe_data) {
    GlobalContext::SetCollectKytheData();
  }
  if (options.force_assert_statements) {
    GlobalContext::SetForceAssertStatements();
  }
  if (options.annotate_ir) {
    GlobalContext::SetAnnotateIR();
  }
  TypeOracle::Scope type_oracle;
  CurrentScope::Scope current_namespace(GlobalContext::GetDefaultNamespace());

  // Two-step process of predeclaration + resolution allows to resolve type
  // declarations independent of the order they are given.
  PredeclarationVisitor::Predeclare(GlobalContext::ast());
  PredeclarationVisitor::ResolvePredeclarations();

  // Process other declarations.
  DeclarationVisitor::Visit(GlobalContext::ast());

  // A class types' fields are resolved here, which allows two class fields to
  // mutually refer to each others.
  TypeOracle::FinalizeAggregateTypes();

  std::string output_directory = options.output_directory;

  ImplementationVisitor implementation_visitor;
  implementation_visitor.SetDryRun(output_directory.empty());

  implementation_visitor.GenerateInstanceTypes(output_directory);
  implementation_visitor.BeginGeneratedFiles();
  implementation_visitor.BeginDebugMacrosFile();

  implementation_visitor.VisitAllDeclarables();

  ReportAllUnusedMacros();

  implementation_visitor.GenerateBuiltinDefinitionsAndInterfaceDescriptors(
      output_directory);
  implementation_visitor.GenerateVisitorLists(output_directory);
  implementation_visitor.GenerateBitFields(output_directory);
  implementation_visitor.GeneratePrintDefinitions(output_directory);
  implementation_visitor.GenerateClassDefinitions(output_directory);
  implementation_visitor.GenerateClassVerifiers(output_directory);
  implementation_visitor.GenerateClassDebugReaders(output_directory);
  implementation_visitor.GenerateEnumVerifiers(output_directory);
  implementation_visitor.GenerateBodyDescriptors(output_directory);
  implementation_visitor.GenerateExportedMacrosAssembler(output_directory);
  implementation_visitor.GenerateCSATypes(output_directory);

  implementation_visitor.EndGeneratedFiles();
  implementation_visitor.EndDebugMacrosFile();
  implementation_visitor.GenerateImplementation(output_directory);

  if (GlobalContext::collect_language_server_data()) {
    LanguageServerData::SetGlobalContext(std::move(GlobalContext::Get()));
    LanguageServerData::SetTypeOracle(std::move(TypeOracle::Get()));
  }
}

}  // namespace

TorqueCompilerResult CompileTorque(const std::string& source,
                                   TorqueCompilerOptions options) {
  TargetArchitecture::Scope target_architecture(options.force_32bit_output);
  SourceFileMap::Scope source_map_scope(options.v8_root);
  CurrentSourceFile::Scope no_file_scope(
      SourceFileMap::AddSource("dummy-filename.tq"));
  CurrentAst::Scope ast_scope;
  TorqueMessages::Scope messages_scope;
  LanguageServerData::Scope server_data_scope;

  TorqueCompilerResult result;
  try {
    ParseTorque(source);
    CompileCurrentAst(options);
  } catch (TorqueAbortCompilation&) {
    // Do nothing. The relevant TorqueMessage is part of the
    // TorqueMessages contextual.
  }

  result.source_file_map = SourceFileMap::Get();
  result.language_server_data = std::move(LanguageServerData::Get());
  result.messages = std::move(TorqueMessages::Get());

  return result;
}

TorqueCompilerResult CompileTorque(const std::vector<std::string>& files,
                                   TorqueCompilerOptions options) {
  TargetArchitecture::Scope target_architecture(options.force_32bit_output);
  SourceFileMap::Scope source_map_scope(options.v8_root);
  CurrentSourceFile::Scope unknown_source_file_scope(SourceId::Invalid());
  CurrentAst::Scope ast_scope;
  TorqueMessages::Scope messages_scope;
  LanguageServerData::Scope server_data_scope;

  TorqueCompilerResult result;
  try {
    for (const auto& path : files) {
      ReadAndParseTorqueFile(path);
    }
    CompileCurrentAst(options);
  } catch (TorqueAbortCompilation&) {
    // Do nothing. The relevant TorqueMessage is part of the
    // TorqueMessages contextual.
  }

  result.source_file_map = SourceFileMap::Get();
  result.language_server_data = std::move(LanguageServerData::Get());
  result.messages = std::move(TorqueMessages::Get());

  return result;
}

TorqueCompilerResult CompileTorqueForKythe(
    std::vector<TorqueCompilationUnit> units, TorqueCompilerOptions options,
    KytheConsumer* consumer) {
  TargetArchitecture::Scope target_architecture(options.force_32bit_output);
  SourceFileMap::Scope source_map_scope(options.v8_root);
  CurrentSourceFile::Scope unknown_source_file_scope(SourceId::Invalid());
  CurrentAst::Scope ast_scope;
  TorqueMessages::Scope messages_scope;
  LanguageServerData::Scope server_data_scope;
  KytheData::Scope kythe_scope;

  KytheData::Get().SetConsumer(consumer);

  TorqueCompilerResult result;
  try {
    for (const auto& unit : units) {
      SourceId source_id = SourceFileMap::AddSource(unit.source_file_path);
      CurrentSourceFile::Scope source_id_scope(source_id);
      ParseTorque(unit.file_content);
    }
    CompileCurrentAst(options);
  } catch (TorqueAbortCompilation&) {
    // Do nothing. The relevant TorqueMessage is part of the
    // TorqueMessages contextual.
  }

  result.source_file_map = SourceFileMap::Get();
  result.language_server_data = std::move(LanguageServerData::Get());
  result.messages = std::move(TorqueMessages::Get());

  return result;
}

}  // namespace v8::internal::torque
