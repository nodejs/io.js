// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/torque/ls/json.h"
#include "src/torque/ls/message-handler.h"
#include "src/torque/ls/message.h"
#include "src/torque/server-data.h"
#include "src/torque/source-positions.h"
#include "test/unittests/test-utils.h"

namespace v8 {
namespace internal {
namespace torque {
namespace ls {

TEST(LanguageServerMessage, InitializeRequest) {
  InitializeRequest request;
  request.set_id(5);
  request.set_method("initialize");
  request.params();

  bool writer_called = false;
  HandleMessage(std::move(request.GetJsonValue()), [&](JsonValue raw_response) {
    InitializeResponse response(std::move(raw_response));

    // Check that the response id matches up with the request id, and that
    // the language server signals its support for definitions.
    EXPECT_EQ(response.id(), 5);
    EXPECT_TRUE(response.result().capabilities().definitionProvider());
    EXPECT_TRUE(response.result().capabilities().documentSymbolProvider());

    writer_called = true;
  });
  EXPECT_TRUE(writer_called);
}

TEST(LanguageServerMessage,
     RegisterDynamicCapabilitiesAfterInitializedNotification) {
  Request<bool> notification;
  notification.set_method("initialized");

  bool writer_called = false;
  HandleMessage(std::move(notification.GetJsonValue()), [&](JsonValue
                                                                raw_request) {
    RegistrationRequest request(std::move(raw_request));

    ASSERT_EQ(request.method(), "client/registerCapability");
    ASSERT_EQ(request.params().registrations_size(), (size_t)1);

    Registration registration = request.params().registrations(0);
    ASSERT_EQ(registration.method(), "workspace/didChangeWatchedFiles");

    auto options =
        registration
            .registerOptions<DidChangeWatchedFilesRegistrationOptions>();
    ASSERT_EQ(options.watchers_size(), (size_t)1);

    writer_called = true;
  });
  EXPECT_TRUE(writer_called);
}

TEST(LanguageServerMessage, GotoDefinitionUnkownFile) {
  SourceFileMap::Scope source_file_map_scope("");

  GotoDefinitionRequest request;
  request.set_id(42);
  request.set_method("textDocument/definition");
  request.params().textDocument().set_uri("file:///unknown.tq");

  bool writer_called = false;
  HandleMessage(std::move(request.GetJsonValue()), [&](JsonValue raw_response) {
    GotoDefinitionResponse response(std::move(raw_response));
    EXPECT_EQ(response.id(), 42);
    EXPECT_TRUE(response.IsNull("result"));

    writer_called = true;
  });
  EXPECT_TRUE(writer_called);
}

TEST(LanguageServerMessage, GotoDefinition) {
  SourceFileMap::Scope source_file_map_scope("");
  SourceId test_id = SourceFileMap::AddSource("file://test.tq");
  SourceId definition_id = SourceFileMap::AddSource("file://base.tq");

  LanguageServerData::Scope server_data_scope;
  LanguageServerData::AddDefinition(
      {test_id, LineAndColumn::WithUnknownOffset(1, 0),
       LineAndColumn::WithUnknownOffset(1, 10)},
      {definition_id, LineAndColumn::WithUnknownOffset(4, 1),
       LineAndColumn::WithUnknownOffset(4, 5)});

  // First, check an unknown definition. The result must be null.
  GotoDefinitionRequest request;
  request.set_id(42);
  request.set_method("textDocument/definition");
  request.params().textDocument().set_uri("file://test.tq");
  request.params().position().set_line(2);
  request.params().position().set_character(0);

  bool writer_called = false;
  HandleMessage(std::move(request.GetJsonValue()), [&](JsonValue raw_response) {
    GotoDefinitionResponse response(std::move(raw_response));
    EXPECT_EQ(response.id(), 42);
    EXPECT_TRUE(response.IsNull("result"));

    writer_called = true;
  });
  EXPECT_TRUE(writer_called);

  // Second, check a known defintion.
  request = GotoDefinitionRequest();
  request.set_id(43);
  request.set_method("textDocument/definition");
  request.params().textDocument().set_uri("file://test.tq");
  request.params().position().set_line(1);
  request.params().position().set_character(5);

  writer_called = false;
  HandleMessage(std::move(request.GetJsonValue()), [&](JsonValue raw_response) {
    GotoDefinitionResponse response(std::move(raw_response));
    EXPECT_EQ(response.id(), 43);
    ASSERT_FALSE(response.IsNull("result"));

    Location location = response.result();
    EXPECT_EQ(location.uri(), "file://base.tq");
    EXPECT_EQ(location.range().start().line(), 4);
    EXPECT_EQ(location.range().start().character(), 1);
    EXPECT_EQ(location.range().end().line(), 4);
    EXPECT_EQ(location.range().end().character(), 5);

    writer_called = true;
  });
  EXPECT_TRUE(writer_called);
}

TEST(LanguageServerMessage, CompilationErrorSendsDiagnostics) {
  DiagnosticsFiles::Scope diagnostic_files_scope;
  LanguageServerData::Scope server_data_scope;
  TorqueMessages::Scope messages_scope;
  SourceFileMap::Scope source_file_map_scope("");

  TorqueCompilerResult result;
  { Error("compilation failed somehow"); }
  result.messages = std::move(TorqueMessages::Get());
  result.source_file_map = SourceFileMap::Get();

  bool writer_called = false;
  CompilationFinished(std::move(result), [&](JsonValue raw_response) {
    PublishDiagnosticsNotification notification(std::move(raw_response));

    EXPECT_EQ(notification.method(), "textDocument/publishDiagnostics");
    ASSERT_FALSE(notification.IsNull("params"));
    EXPECT_EQ(notification.params().uri(), "<unknown>");

    ASSERT_GT(notification.params().diagnostics_size(), static_cast<size_t>(0));
    Diagnostic diagnostic = notification.params().diagnostics(0);
    EXPECT_EQ(diagnostic.severity(), Diagnostic::kError);
    EXPECT_EQ(diagnostic.message(), "compilation failed somehow");

    writer_called = true;
  });
  EXPECT_TRUE(writer_called);
}

TEST(LanguageServerMessage, LintErrorSendsDiagnostics) {
  DiagnosticsFiles::Scope diagnostic_files_scope;
  TorqueMessages::Scope messages_scope;
  LanguageServerData::Scope server_data_scope;
  SourceFileMap::Scope sourc_file_map_scope("");
  SourceId test_id = SourceFileMap::AddSource("file://test.tq");

  // No compilation errors but two lint warnings.
  {
    SourcePosition pos1{test_id, LineAndColumn::WithUnknownOffset(0, 0),
                        LineAndColumn::WithUnknownOffset(0, 1)};
    SourcePosition pos2{test_id, LineAndColumn::WithUnknownOffset(1, 0),
                        LineAndColumn::WithUnknownOffset(1, 1)};
    Lint("lint error 1").Position(pos1);
    Lint("lint error 2").Position(pos2);
  }

  TorqueCompilerResult result;
  result.messages = std::move(TorqueMessages::Get());
  result.source_file_map = SourceFileMap::Get();

  bool writer_called = false;
  CompilationFinished(std::move(result), [&](JsonValue raw_response) {
    PublishDiagnosticsNotification notification(std::move(raw_response));

    EXPECT_EQ(notification.method(), "textDocument/publishDiagnostics");
    ASSERT_FALSE(notification.IsNull("params"));
    EXPECT_EQ(notification.params().uri(), "file://test.tq");

    ASSERT_EQ(notification.params().diagnostics_size(), static_cast<size_t>(2));
    Diagnostic diagnostic1 = notification.params().diagnostics(0);
    EXPECT_EQ(diagnostic1.severity(), Diagnostic::kWarning);
    EXPECT_EQ(diagnostic1.message(), "lint error 1");

    Diagnostic diagnostic2 = notification.params().diagnostics(1);
    EXPECT_EQ(diagnostic2.severity(), Diagnostic::kWarning);
    EXPECT_EQ(diagnostic2.message(), "lint error 2");

    writer_called = true;
  });
  EXPECT_TRUE(writer_called);
}

TEST(LanguageServerMessage, CleanCompileSendsNoDiagnostics) {
  LanguageServerData::Scope server_data_scope;
  SourceFileMap::Scope sourc_file_map_scope("");

  TorqueCompilerResult result;
  result.source_file_map = SourceFileMap::Get();

  CompilationFinished(std::move(result), [](JsonValue raw_response) {
    FAIL() << "Sending unexpected response!";
  });
}

TEST(LanguageServerMessage, NoSymbolsSendsEmptyResponse) {
  LanguageServerData::Scope server_data_scope;
  SourceFileMap::Scope sourc_file_map_scope("");

  DocumentSymbolRequest request;
  request.set_id(42);
  request.set_method("textDocument/documentSymbol");
  request.params().textDocument().set_uri("file://test.tq");

  bool writer_called = false;
  HandleMessage(std::move(request.GetJsonValue()), [&](JsonValue raw_response) {
    DocumentSymbolResponse response(std::move(raw_response));
    EXPECT_EQ(response.id(), 42);
    EXPECT_EQ(response.result_size(), static_cast<size_t>(0));

    writer_called = true;
  });
  EXPECT_TRUE(writer_called);
}

}  // namespace ls
}  // namespace torque
}  // namespace internal
}  // namespace v8
