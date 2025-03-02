// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <cmath>
#include <iostream>
#include <limits>
#include <memory>

#include "src/ast/ast-value-factory.h"
#include "src/ast/ast.h"
#include "src/ast/scopes.h"
#include "src/common/assert-scope.h"
#include "src/common/globals.h"
#include "src/handles/handles-inl.h"
#include "src/handles/handles.h"
#include "src/handles/maybe-handles.h"
#include "src/heap/local-factory-inl.h"
#include "src/objects/fixed-array.h"
#include "src/objects/script.h"
#include "src/objects/shared-function-info.h"
#include "src/objects/string.h"
#include "src/parsing/parse-info.h"
#include "src/parsing/parser.h"
#include "src/parsing/rewriter.h"
#include "src/parsing/scanner-character-streams.h"
#include "src/parsing/scanner.h"
#include "src/strings/unicode-inl.h"
#include "src/utils/utils.h"
#include "test/unittests/test-utils.h"

namespace v8 {
namespace internal {

class LocalIsolate;

namespace {

std::vector<uint16_t> DecodeUtf8(const std::string& string) {
  if (string.empty()) return {};

  auto utf8_data = base::Vector<const uint8_t>::cast(
      base::VectorOf(string.data(), string.length()));
  Utf8Decoder decoder(utf8_data);

  std::vector<uint16_t> utf16(decoder.utf16_length());
  decoder.Decode(&utf16[0], utf8_data);

  return utf16;
}

}  // namespace

class LocalFactoryTest : public TestWithIsolateAndZone {
 public:
  LocalFactoryTest()
      : TestWithIsolateAndZone(),
        reusable_state_(isolate()),
        parse_info_(
            isolate(),
            UnoptimizedCompileFlags::ForToplevelCompile(
                isolate(), true, construct_language_mode(v8_flags.use_strict),
                REPLMode::kNo, ScriptType::kClassic, v8_flags.lazy),
            &state_, &reusable_state_),
        local_isolate_(isolate()->main_thread_local_isolate()) {}

  FunctionLiteral* ParseProgram(const char* source) {
    auto utf16_source = DecodeUtf8(source);

    // Normally this would be an external string or whatever, we don't have to
    // worry about it for now.
    source_string_ = factory()
                         ->NewStringFromUtf8(base::CStrVector(source))
                         .ToHandleChecked();

    script_ = parse_info_.CreateScript(local_isolate(),
                                       local_factory()->empty_string(),
                                       kNullMaybeHandle, ScriptOriginOptions());

    parse_info_.set_character_stream(
        ScannerStream::ForTesting(utf16_source.data(), utf16_source.size()));

    Parser parser(local_isolate(), parse_info());
    parser.InitializeEmptyScopeChain(parse_info());
    parser.ParseOnBackground(local_isolate(), parse_info(), script_, 0, 0,
                             kFunctionLiteralIdTopLevel);

    DeclarationScope::AllocateScopeInfos(parse_info(), script_,
                                         local_isolate());

    // Create the SFI list on the script so that SFI SetScript works.
    DirectHandle<WeakFixedArray> infos = local_factory()->NewWeakFixedArray(
        parse_info()->max_info_id() + 1, AllocationType::kOld);
    script_->set_infos(*infos);

    return parse_info()->literal();
  }

  ParseInfo* parse_info() { return &parse_info_; }

  DirectHandle<Script> script() { return script_; }

  LocalIsolate* local_isolate() { return local_isolate_; }
  LocalFactory* local_factory() { return local_isolate()->factory(); }

 private:
  SaveFlags save_flags_;
  UnoptimizedCompileState state_;
  ReusableUnoptimizedCompileState reusable_state_;
  ParseInfo parse_info_;
  LocalIsolate* local_isolate_;
  Handle<String> source_string_;
  Handle<Script> script_;
};

TEST_F(LocalFactoryTest, OneByteInternalizedString_IsAddedToStringTable) {
  base::Vector<const uint8_t> string_vector = base::StaticOneByteVector("foo");

  DirectHandle<String> string;
  {
    LocalHandleScope handle_scope(local_isolate());

    Handle<String> local_string =
        local_factory()->InternalizeString(string_vector);

    string = local_isolate()->heap()->NewPersistentHandle(local_string);
  }

  EXPECT_TRUE(string->IsOneByteEqualTo(base::CStrVector("foo")));
  EXPECT_TRUE(IsInternalizedString(*string));

  Handle<String> same_string = isolate()
                                   ->factory()
                                   ->NewStringFromOneByte(string_vector)
                                   .ToHandleChecked();
  EXPECT_NE(*string, *same_string);
  EXPECT_FALSE(IsInternalizedString(*same_string));

  DirectHandle<String> internalized_string =
      isolate()->factory()->InternalizeString(same_string);
  EXPECT_EQ(*string, *internalized_string);
}

TEST_F(LocalFactoryTest, OneByteInternalizedString_DuplicateIsDeduplicated) {
  base::Vector<const uint8_t> string_vector = base::StaticOneByteVector("foo");

  DirectHandle<String> string_1;
  DirectHandle<String> string_2;
  {
    LocalHandleScope handle_scope(local_isolate());

    Handle<String> local_string_1 =
        local_factory()->InternalizeString(string_vector);
    Handle<String> local_string_2 =
        local_factory()->InternalizeString(string_vector);

    string_1 = local_isolate()->heap()->NewPersistentHandle(local_string_1);
    string_2 = local_isolate()->heap()->NewPersistentHandle(local_string_2);
  }

  EXPECT_TRUE(string_1->IsOneByteEqualTo(base::CStrVector("foo")));
  EXPECT_TRUE(IsInternalizedString(*string_1));
  EXPECT_EQ(*string_1, *string_2);
}

TEST_F(LocalFactoryTest, AstRawString_IsInternalized) {
  AstValueFactory ast_value_factory(zone(), isolate()->ast_string_constants(),
                                    HashSeed(isolate()));

  const AstRawString* raw_string = ast_value_factory.GetOneByteString("foo");

  DirectHandle<String> string;
  {
    LocalHandleScope handle_scope(local_isolate());

    ast_value_factory.Internalize(local_isolate());

    string = local_isolate()->heap()->NewPersistentHandle(raw_string->string());
  }

  EXPECT_TRUE(string->IsOneByteEqualTo(base::CStrVector("foo")));
  EXPECT_TRUE(IsInternalizedString(*string));
}

TEST_F(LocalFactoryTest, AstConsString_CreatesConsString) {
  AstValueFactory ast_value_factory(zone(), isolate()->ast_string_constants(),
                                    HashSeed(isolate()));

  DirectHandle<String> string;
  {
    LocalHandleScope handle_scope(local_isolate());

    const AstRawString* foo_string = ast_value_factory.GetOneByteString("foo");
    const AstRawString* bar_string =
        ast_value_factory.GetOneByteString("bar-plus-padding-for-length");
    AstConsString* foobar_string =
        ast_value_factory.NewConsString(foo_string, bar_string);

    ast_value_factory.Internalize(local_isolate());

    string = local_isolate()->heap()->NewPersistentHandle(
        foobar_string->GetString(local_isolate()));
  }

  EXPECT_TRUE(IsConsString(*string));
  EXPECT_TRUE(string->Equals(*isolate()->factory()->NewStringFromStaticChars(
      "foobar-plus-padding-for-length")));
}

TEST_F(LocalFactoryTest, EmptyScript) {
  FunctionLiteral* program = ParseProgram("");

  DirectHandle<SharedFunctionInfo> shared;
  {
    LocalHandleScope handle_scope(local_isolate());

    shared = local_isolate()->heap()->NewPersistentHandle(
        local_factory()->NewSharedFunctionInfoForLiteral(program, script(),
                                                         true));
  }
  DirectHandle<SharedFunctionInfo> root_sfi = shared;

  EXPECT_EQ(root_sfi->function_literal_id(), 0);
}

TEST_F(LocalFactoryTest, LazyFunction) {
  FunctionLiteral* program = ParseProgram("function lazy() {}");
  FunctionLiteral* lazy = program->scope()
                              ->declarations()
                              ->AtForTest(0)
                              ->AsFunctionDeclaration()
                              ->fun();

  DirectHandle<SharedFunctionInfo> shared;
  {
    LocalHandleScope handle_scope(local_isolate());

    shared = local_isolate()->heap()->NewPersistentHandle(
        local_factory()->NewSharedFunctionInfoForLiteral(lazy, script(), true));
  }
  DirectHandle<SharedFunctionInfo> lazy_sfi = shared;

  EXPECT_EQ(lazy_sfi->function_literal_id(), 1);
  EXPECT_TRUE(lazy_sfi->Name()->IsOneByteEqualTo(base::CStrVector("lazy")));
  EXPECT_FALSE(lazy_sfi->is_compiled());
  EXPECT_TRUE(lazy_sfi->HasUncompiledDataWithoutPreparseData());
}

TEST_F(LocalFactoryTest, EagerFunction) {
  FunctionLiteral* program = ParseProgram("(function eager() {})");
  // Rewritten to `.result = (function eager() {}); return .result`
  FunctionLiteral* eager = program->body()
                               ->at(0)
                               ->AsExpressionStatement()
                               ->expression()
                               ->AsAssignment()
                               ->value()
                               ->AsFunctionLiteral();

  DirectHandle<SharedFunctionInfo> shared;
  {
    LocalHandleScope handle_scope(local_isolate());

    shared = local_isolate()->heap()->NewPersistentHandle(
        local_factory()->NewSharedFunctionInfoForLiteral(eager, script(),
                                                         true));
  }
  DirectHandle<SharedFunctionInfo> eager_sfi = shared;

  EXPECT_EQ(eager_sfi->function_literal_id(), 1);
  EXPECT_TRUE(eager_sfi->Name()->IsOneByteEqualTo(base::CStrVector("eager")));
  EXPECT_FALSE(eager_sfi->HasUncompiledData());
  // TODO(leszeks): Add compilation support and enable these checks.
  // EXPECT_TRUE(eager_sfi->is_compiled());
  // EXPECT_TRUE(eager_sfi->HasBytecodeArray());
}

TEST_F(LocalFactoryTest, ImplicitNameFunction) {
  FunctionLiteral* program = ParseProgram("let implicit_name = function() {}");
  FunctionLiteral* implicit_name = program->body()
                                       ->at(0)
                                       ->AsBlock()
                                       ->statements()
                                       ->at(0)
                                       ->AsExpressionStatement()
                                       ->expression()
                                       ->AsAssignment()
                                       ->value()
                                       ->AsFunctionLiteral();

  DirectHandle<SharedFunctionInfo> shared;
  {
    LocalHandleScope handle_scope(local_isolate());

    shared = local_isolate()->heap()->NewPersistentHandle(
        local_factory()->NewSharedFunctionInfoForLiteral(implicit_name,
                                                         script(), true));
  }
  DirectHandle<SharedFunctionInfo> implicit_name_sfi = shared;

  EXPECT_EQ(implicit_name_sfi->function_literal_id(), 1);
  EXPECT_TRUE(implicit_name_sfi->Name()->IsOneByteEqualTo(
      base::CStrVector("implicit_name")));
}

TEST_F(LocalFactoryTest, GCDuringPublish) {
  FunctionLiteral* program = ParseProgram("let implicit_name = function() {}");
  FunctionLiteral* implicit_name = program->body()
                                       ->at(0)
                                       ->AsBlock()
                                       ->statements()
                                       ->at(0)
                                       ->AsExpressionStatement()
                                       ->expression()
                                       ->AsAssignment()
                                       ->value()
                                       ->AsFunctionLiteral();

  DirectHandle<SharedFunctionInfo> shared;
  {
    LocalHandleScope handle_scope(local_isolate());

    shared = local_isolate()->heap()->NewPersistentHandle(
        local_factory()->NewSharedFunctionInfoForLiteral(implicit_name,
                                                         script(), true));
  }
  DirectHandle<SharedFunctionInfo> implicit_name_sfi = shared;

  EXPECT_EQ(implicit_name_sfi->function_literal_id(), 1);
  EXPECT_TRUE(implicit_name_sfi->Name()->IsOneByteEqualTo(
      base::CStrVector("implicit_name")));
}

}  // namespace internal
}  // namespace v8
