// Copyright 2006-2009 the V8 project authors. All rights reserved.
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
//     * Redistributions of source code must retain the above copyright
//       notice, this list of conditions and the following disclaimer.
//     * Redistributions in binary form must reproduce the above
//       copyright notice, this list of conditions and the following
//       disclaimer in the documentation and/or other materials provided
//       with the distribution.
//     * Neither the name of Google Inc. nor the names of its
//       contributors may be used to endorse or promote products derived
//       from this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
//
// Tests of logging functions from log.h

#include "src/logging/log.h"

#include <unordered_set>
#include <vector>

#include "include/v8-function.h"
#include "src/api/api-inl.h"
#include "src/base/strings.h"
#include "src/builtins/builtins.h"
#include "src/codegen/compilation-cache.h"
#include "src/execution/vm-state-inl.h"
#include "src/init/v8.h"
#include "src/logging/log-file.h"
#include "src/objects/objects-inl.h"
#include "src/profiler/cpu-profiler.h"
#include "src/utils/ostreams.h"
#include "src/utils/version.h"
#include "test/unittests/test-utils.h"
#include "testing/gtest/include/gtest/gtest.h"

using v8::base::EmbeddedVector;
using v8::internal::Address;
using v8::internal::V8FileLogger;

namespace v8 {
namespace {

class LogTest : public TestWithIsolate {
 public:
  static void SetUpTestSuite() {
    i::v8_flags.log = true;
    i::v8_flags.prof = true;
    i::v8_flags.log_code = true;
    i::v8_flags.logfile = i::LogFile::kLogToTemporaryFile;
    i::v8_flags.logfile_per_isolate = false;
    TestWithIsolate::SetUpTestSuite();
  }
};

static std::vector<std::string> Split(const std::string& s, char delimiter) {
  std::vector<std::string> result;
  std::string line;
  std::istringstream stream(s);
  while (std::getline(stream, line, delimiter)) {
    result.push_back(line);
  }
  return result;
}

class V8_NODISCARD ScopedLoggerInitializer {
 public:
  explicit ScopedLoggerInitializer(v8::Isolate* isolate)
      : temp_file_(nullptr),
        isolate_(isolate),
        isolate_scope_(isolate),
        scope_(isolate),
        env_(v8::Context::New(isolate)) {
    env_->Enter();
  }

  ~ScopedLoggerInitializer() {
    env_->Exit();
    FILE* log_file = v8_file_logger()->TearDownAndGetLogFile();
    if (log_file != nullptr) fclose(log_file);
  }

  ScopedLoggerInitializer(const ScopedLoggerInitializer&) = delete;
  ScopedLoggerInitializer& operator=(const ScopedLoggerInitializer&) = delete;

  v8::Local<v8::Context>& env() { return env_; }

  v8::Isolate* isolate() { return isolate_; }

  i::Isolate* i_isolate() { return reinterpret_cast<i::Isolate*>(isolate()); }

  V8FileLogger* v8_file_logger() { return i_isolate()->v8_file_logger(); }

  i::Logger* logger() { return i_isolate()->logger(); }

  v8::Local<v8::String> GetLogString() {
    int length = static_cast<int>(raw_log_.size());
    return v8::String::NewFromUtf8(isolate_, raw_log_.c_str(),
                                   v8::NewStringType::kNormal, length)
        .ToLocalChecked();
  }

  void PrintLog() {
    i::StdoutStream os;
    os << raw_log_ << std::flush;
  }

  void StopLogging() {
    bool exists = false;
    raw_log_ = i::ReadFile(StopLoggingGetTempFile(), &exists, true);
    log_ = Split(raw_log_, '\n');
    CHECK(exists);
  }

  // Searches |log_| for a line which contains all the strings in |search_terms|
  // as substrings, starting from the index |start|, and returns the index of
  // the found line. Returns std::string::npos if no line is found.
  size_t IndexOfLine(const std::vector<std::string>& search_terms,
                     size_t start = 0) {
    for (size_t i = start; i < log_.size(); ++i) {
      const std::string& line = log_.at(i);
      bool all_terms_found = true;
      for (const std::string& term : search_terms) {
        all_terms_found &= line.find(term) != std::string::npos;
      }
      if (all_terms_found) return i;
    }
    return std::string::npos;
  }

  bool ContainsLine(const std::vector<std::string>& search_terms,
                    size_t start = 0) {
    return IndexOfLine(search_terms, start) != std::string::npos;
  }

  // Calls IndexOfLine for each set of substring terms in
  // |all_line_search_terms|, in order. Returns true if they're all found.
  bool ContainsLinesInOrder(
      const std::vector<std::vector<std::string>>& all_line_search_terms,
      size_t start = 0) {
    CHECK_GT(log_.size(), 0);
    for (auto& search_terms : all_line_search_terms) {
      size_t next = IndexOfLine(search_terms, start);
      if (next == std::string::npos) {
        for (size_t i = 0; i < search_terms.size(); ++i) {
          printf("%s ", search_terms[i].c_str());
        }
        printf(" -- mismatch\n");
        printf("Log contents:\n");
        for (size_t i = start; i < log_.size(); ++i) {
          printf("%s\n", log_.at(start).c_str());
        }
        return false;
      }
      start = next + 1;  // Skip the found line.
    }
    return true;
  }

  std::unordered_set<uintptr_t> ExtractLogAddresses(std::string search_term,
                                                    size_t address_column,
                                                    bool allow_duplicates) {
    CHECK_GT(log_.size(), 0);
    // Map addresses of Maps to log_lines.
    std::unordered_map<uintptr_t, std::string> map;
    size_t current = 0;
    while (true) {
      current = IndexOfLine({search_term}, current);
      if (current == std::string::npos) break;
      std::string current_line = log_.at(current);
      std::vector<std::string> columns = Split(current_line, ',');
      ++current;  // Skip the found line.
      // TODO(crbug.com/v8/8084): These two continue lines should really be
      // errors. But on Windows the log is sometimes mysteriously cut off at the
      // end. If the cut-off point happens to fall in the address field, the
      // conditions will be triggered.
      if (address_column >= columns.size()) continue;
      uintptr_t address =
          strtoull(columns.at(address_column).c_str(), nullptr, 16);
      if (address == 0) continue;
      if (!allow_duplicates) {
        auto match = map.find(address);
        // Ignore same address but different log line.
        if (match != map.end() && match->second.compare(current_line) == 0) {
          for (size_t i = 0; i < current; i++) {
            printf("%s\n", log_.at(i).c_str());
          }
          printf("%zu\n", current);
          FATAL("%s, ... %p apperead twice:\n    %s", search_term.c_str(),
                reinterpret_cast<void*>(address), current_line.c_str());
        }
      }
      map.insert({address, current_line});
    }
    // Extract all keys.
    std::unordered_set<uintptr_t> result;
    for (auto key_value : map) {
      result.insert(key_value.first);
    }
    return result;
  }

  void LogCodeObjects() { v8_file_logger()->LogCodeObjects(); }
  void LogCompiledFunctions() { v8_file_logger()->LogCompiledFunctions(); }

  void StringEvent(const char* name, const char* value) {
    v8_file_logger()->StringEvent(name, value);
  }

 private:
  FILE* StopLoggingGetTempFile() {
    temp_file_ = v8_file_logger()->TearDownAndGetLogFile();
    CHECK(temp_file_);
    rewind(temp_file_);
    return temp_file_;
  }

  FILE* temp_file_;
  v8::Isolate* isolate_;
  v8::Isolate::Scope isolate_scope_;
  v8::HandleScope scope_;
  v8::Local<v8::Context> env_;

  std::string raw_log_;
  std::vector<std::string> log_;
};

class TestCodeEventHandler : public v8::CodeEventHandler {
 public:
  explicit TestCodeEventHandler(v8::Isolate* isolate)
      : v8::CodeEventHandler(isolate), isolate_(isolate) {}

  size_t CountLines(std::string prefix, std::string suffix = std::string()) {
    if (event_log_.empty()) return 0;

    size_t match = 0;
    for (const std::string& line : event_log_) {
      size_t prefix_pos = line.find(prefix);
      if (prefix_pos == std::string::npos) continue;
      size_t suffix_pos = line.rfind(suffix);
      if (suffix_pos == std::string::npos) continue;
      if (suffix_pos != line.length() - suffix.length()) continue;
      if (prefix_pos >= suffix_pos) continue;
      match++;
    }

    return match;
  }

  void Handle(v8::CodeEvent* code_event) override {
    std::string log_line = "";
    log_line += v8::CodeEvent::GetCodeEventTypeName(code_event->GetCodeType());
    log_line += " ";
    log_line += FormatName(code_event);
    event_log_.push_back(log_line);
  }

 private:
  std::string FormatName(v8::CodeEvent* code_event) {
    std::string name = std::string(code_event->GetComment());
    if (name.empty()) {
      v8::Local<v8::String> functionName = code_event->GetFunctionName();
      size_t buffer_size = functionName->Utf8LengthV2(isolate_) + 1;
      std::string buffer(buffer_size, 0);
      functionName->WriteUtf8V2(isolate_, &buffer[0], buffer_size,
                                String::WriteFlags::kNullTerminate);
      // Sanitize name, removing unwanted \0 resulted from WriteUtf8
      name = std::string(buffer.c_str());
    }

    return name;
  }

  std::vector<std::string> event_log_;
  v8::Isolate* isolate_;
};

}  // namespace

// Test for issue http://crbug.com/23768 in Chromium.
// Heap can contain scripts with already disposed external sources.
// We need to verify that LogCompiledFunctions doesn't crash on them.
namespace {

class SimpleExternalString : public v8::String::ExternalStringResource {
 public:
  explicit SimpleExternalString(const char* source) {
    size_t len = strlen(source);
    utf_source_ = base::OwnedVector<uint16_t>::NewForOverwrite(len);
    std::copy(source, source + len, utf_source_.data());
  }
  ~SimpleExternalString() override = default;
  size_t length() const override { return utf_source_.size(); }
  const uint16_t* data() const override { return utf_source_.begin(); }

 private:
  v8::base::OwnedVector<uint16_t> utf_source_;
};

}  // namespace

TEST_F(TestWithIsolate, Issue23768) {
  v8::HandleScope scope(isolate());
  v8::Local<v8::Context> env = v8::Context::New(isolate());
  env->Enter();

  SimpleExternalString source_ext_str("(function ext() {})();");
  v8::Local<v8::String> source =
      v8::String::NewExternalTwoByte(isolate(), &source_ext_str)
          .ToLocalChecked();
  // Script needs to have a name in order to trigger InitLineEnds execution.
  v8::Local<v8::String> origin =
      v8::String::NewFromUtf8Literal(isolate(), "issue-23768-test");
  v8::Local<v8::Script> evil_script = CompileWithOrigin(source, origin, false);
  CHECK(!evil_script.IsEmpty());
  CHECK(!evil_script->Run(env).IsEmpty());
  i::DirectHandle<i::ExternalTwoByteString> i_source(
      i::Cast<i::ExternalTwoByteString>(*v8::Utils::OpenDirectHandle(*source)),
      i_isolate());
  // This situation can happen if source was an external string disposed
  // by its owner.
  i_source->SetResource(i_isolate(), nullptr);

  // Must not crash.
  i_isolate()->v8_file_logger()->LogCompiledFunctions();
}

static void ObjMethod1(const v8::FunctionCallbackInfo<v8::Value>& info) {
  CHECK(i::ValidateCallbackInfo(info));
}

TEST_F(LogTest, LogCallbacks) {
  {
    ScopedLoggerInitializer logger(isolate());

    v8::Local<v8::FunctionTemplate> obj = v8::Local<v8::FunctionTemplate>::New(
        isolate(), v8::FunctionTemplate::New(isolate()));
    obj->SetClassName(NewString("Obj"));
    v8::Local<v8::ObjectTemplate> proto = obj->PrototypeTemplate();
    v8::Local<v8::Signature> signature = v8::Signature::New(isolate(), obj);
    proto->Set(NewString("method1"),
               v8::FunctionTemplate::New(isolate(), ObjMethod1,
                                         v8::Local<v8::Value>(), signature),
               static_cast<v8::PropertyAttribute>(v8::DontDelete));

    logger.env()
        ->Global()
        ->Set(logger.env(), NewString("Obj"),
              obj->GetFunction(logger.env()).ToLocalChecked())
        .FromJust();
    RunJS("Obj.prototype.method1.toString();");

    logger.LogCompiledFunctions();
    logger.StopLogging();

    Address ObjMethod1_entry = reinterpret_cast<Address>(ObjMethod1);
#if USES_FUNCTION_DESCRIPTORS
    ObjMethod1_entry = *FUNCTION_ENTRYPOINT_ADDRESS(ObjMethod1_entry);
#endif
    v8::base::EmbeddedVector<char, 100> suffix_buffer;
    v8::base::SNPrintF(suffix_buffer, ",0x%" V8PRIxPTR ",1,method1",
                       ObjMethod1_entry);
    CHECK(logger.ContainsLine(
        {"code-creation,Callback,-2,", std::string(suffix_buffer.begin())}));
  }
}

static void Prop1Getter(v8::Local<v8::Name> property,
                        const v8::PropertyCallbackInfo<v8::Value>& info) {}

static void Prop1Setter(v8::Local<v8::Name> property,
                        v8::Local<v8::Value> value,
                        const v8::PropertyCallbackInfo<void>& info) {}

static void Prop2Getter(v8::Local<v8::Name> property,
                        const v8::PropertyCallbackInfo<v8::Value>& info) {}

TEST_F(LogTest, LogAccessorCallbacks) {
  {
    ScopedLoggerInitializer logger(isolate());

    v8::Local<v8::FunctionTemplate> obj = v8::Local<v8::FunctionTemplate>::New(
        isolate(), v8::FunctionTemplate::New(isolate()));
    obj->SetClassName(NewString("Obj"));
    v8::Local<v8::ObjectTemplate> inst = obj->InstanceTemplate();
    inst->SetNativeDataProperty(NewString("prop1"), Prop1Getter, Prop1Setter);
    inst->SetNativeDataProperty(NewString("prop2"), Prop2Getter);

    logger.v8_file_logger()->LogAccessorCallbacks();

    logger.StopLogging();

    Address Prop1Getter_entry = reinterpret_cast<Address>(Prop1Getter);
#if USES_FUNCTION_DESCRIPTORS
    Prop1Getter_entry = *FUNCTION_ENTRYPOINT_ADDRESS(Prop1Getter_entry);
#endif
    v8::base::EmbeddedVector<char, 100> prop1_getter_record;
    v8::base::SNPrintF(prop1_getter_record, ",0x%" V8PRIxPTR ",1,get prop1",
                       Prop1Getter_entry);
    CHECK(logger.ContainsLine({"code-creation,Callback,-2,",
                               std::string(prop1_getter_record.begin())}));

    Address Prop1Setter_entry = reinterpret_cast<Address>(Prop1Setter);
#if USES_FUNCTION_DESCRIPTORS
    Prop1Setter_entry = *FUNCTION_ENTRYPOINT_ADDRESS(Prop1Setter_entry);
#endif
    v8::base::EmbeddedVector<char, 100> prop1_setter_record;
    v8::base::SNPrintF(prop1_setter_record, ",0x%" V8PRIxPTR ",1,set prop1",
                       Prop1Setter_entry);
    CHECK(logger.ContainsLine({"code-creation,Callback,-2,",
                               std::string(prop1_setter_record.begin())}));

    Address Prop2Getter_entry = reinterpret_cast<Address>(Prop2Getter);
#if USES_FUNCTION_DESCRIPTORS
    Prop2Getter_entry = *FUNCTION_ENTRYPOINT_ADDRESS(Prop2Getter_entry);
#endif
    v8::base::EmbeddedVector<char, 100> prop2_getter_record;
    v8::base::SNPrintF(prop2_getter_record, ",0x%" V8PRIxPTR ",1,get prop2",
                       Prop2Getter_entry);
    CHECK(logger.ContainsLine({"code-creation,Callback,-2,",
                               std::string(prop2_getter_record.begin())}));
  }
}

TEST_F(LogTest, LogVersion) {
  {
    ScopedLoggerInitializer logger(isolate());
    logger.StopLogging();

    v8::base::EmbeddedVector<char, 100> line_buffer;
    v8::base::SNPrintF(line_buffer, "%d,%d,%d,%d,%d", i::Version::GetMajor(),
                       i::Version::GetMinor(), i::Version::GetBuild(),
                       i::Version::GetPatch(), i::Version::IsCandidate());
    CHECK(
        logger.ContainsLine({"v8-version,", std::string(line_buffer.begin())}));
  }
}

// https://crbug.com/539892
// CodeCreateEvents with really large names should not crash.
TEST_F(LogTest, Issue539892) {
  i::FakeCodeEventLogger code_event_logger(i_isolate());

  {
    ScopedLoggerInitializer logger(isolate());
    logger.logger()->AddListener(&code_event_logger);

    // Function with a really large name.
    const char* source_text =
        "(function "
        "baaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
        "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
        "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
        "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
        "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
        "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
        "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
        "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
        "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
        "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
        "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
        "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
        "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
        "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
        "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
        "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
        "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaac"
        "(){})();";

    RunJS(source_text);

    // Must not crash.
    logger.LogCompiledFunctions();
    logger.logger()->RemoveListener(&code_event_logger);
  }
}

class LogAllTest : public LogTest {
 public:
  static void SetUpTestSuite() {
    i::v8_flags.log_all = true;
    i::v8_flags.log_deopt = true;
    i::v8_flags.turbo_inlining = false;
    i::v8_flags.log_timer_events = true;
    i::v8_flags.allow_natives_syntax = true;
    LogTest::SetUpTestSuite();
  }
};

TEST_F(LogAllTest, LogAll) {
  {
    ScopedLoggerInitializer logger(isolate());

    const char* source_text = R"(
        function testAddFn(a,b) {
          return a + b
        };
        let result;

        // Warm up the ICs.
        %PrepareFunctionForOptimization(testAddFn);
        for (let i = 0; i < 100000; i++) {
          result = testAddFn(i, i);
        };

        // Enforce optimization.
        %OptimizeFunctionOnNextCall(testAddFn);
        result = testAddFn(1, 1);

        // Cause deopt.
        testAddFn('1', 1)
        for (let i = 0; i < 100000; i++) {
          result = testAddFn('1', i);
        }
      )";
    RunJS(source_text);

    logger.StopLogging();

    // We should find at least one code-creation even for testAddFn();
    CHECK(logger.ContainsLine({"timer-event-start", "V8.CompileCode"}));
    CHECK(logger.ContainsLine({"timer-event-end", "V8.CompileCode"}));
    CHECK(logger.ContainsLine({"code-creation,Script", ":1:1"}));
    CHECK(logger.ContainsLine({"code-creation,JS,", "testAddFn"}));

    if (i::v8_flags.turbofan && !i::v8_flags.always_turbofan) {
      CHECK(logger.ContainsLine({"code-deopt,", "not a Smi"}));
      CHECK(logger.ContainsLine({"timer-event-start", "V8.DeoptimizeCode"}));
      CHECK(logger.ContainsLine({"timer-event-end", "V8.DeoptimizeCode"}));
    }
  }
}

class LogInterpretedFramesNativeStackTest : public LogTest {
 public:
  static void SetUpTestSuite() {
    i::v8_flags.interpreted_frames_native_stack = true;
    LogTest::SetUpTestSuite();
  }
};

TEST_F(LogInterpretedFramesNativeStackTest, LogInterpretedFramesNativeStack) {
  {
    ScopedLoggerInitializer logger(isolate());

    const char* source_text =
        "function testLogInterpretedFramesNativeStack(a,b) { return a + b };"
        "testLogInterpretedFramesNativeStack('1', 1);";
    RunJS(source_text);

    logger.StopLogging();

    CHECK(logger.ContainsLinesInOrder(
        {{"JS", "testLogInterpretedFramesNativeStack"},
         {"JS", "testLogInterpretedFramesNativeStack"}}));
  }
}

class LogInterpretedFramesNativeStackWithSerializationTest
    : public TestWithPlatform {
 public:
  LogInterpretedFramesNativeStackWithSerializationTest()
      : array_buffer_allocator_(
            v8::ArrayBuffer::Allocator::NewDefaultAllocator()) {}
  static void SetUpTestSuite() {
    i::v8_flags.log = true;
    i::v8_flags.prof = true;
    i::v8_flags.log_code = true;
    i::v8_flags.logfile = i::LogFile::kLogToTemporaryFile;
    i::v8_flags.logfile_per_isolate = false;
    i::v8_flags.interpreted_frames_native_stack = true;
    i::v8_flags.always_turbofan = false;
    TestWithPlatform::SetUpTestSuite();
  }

  v8::Local<v8::String> NewString(const char* source) {
    return v8::String::NewFromUtf8(v8::Isolate::GetCurrent(), source)
        .ToLocalChecked();
  }

  v8::ArrayBuffer::Allocator* array_buffer_allocator() {
    return array_buffer_allocator_.get();
  }

 private:
  std::unique_ptr<v8::ArrayBuffer::Allocator> array_buffer_allocator_;
};

TEST_F(LogInterpretedFramesNativeStackWithSerializationTest,
       LogInterpretedFramesNativeStackWithSerialization) {
  v8::Isolate::CreateParams create_params;
  create_params.array_buffer_allocator = array_buffer_allocator();

  v8::ScriptCompiler::CachedData* cache = nullptr;

  bool has_cache = cache != nullptr;
  // NOTE(mmarchini): Runs the test two times. The first time it will compile
  // our script and will create a code cache for it. The second time we'll
  // deserialize the cache and check if our function was logged correctly.
  // We disallow compilation on the second run to ensure we're loading from
  // cache.
  do {
    v8::Isolate* isolate = v8::Isolate::New(create_params);

    {
      ScopedLoggerInitializer logger(isolate);

      has_cache = cache != nullptr;
      v8::ScriptCompiler::CompileOptions options =
          has_cache ? v8::ScriptCompiler::kConsumeCodeCache
                    : v8::ScriptCompiler::kEagerCompile;

      v8::HandleScope scope(isolate);
      v8::Isolate::Scope isolate_scope(isolate);
      v8::Local<v8::Context> context = v8::Context::New(isolate);
      v8::Local<v8::String> source = NewString(
          "function eyecatcher() { return a * a; } return eyecatcher();");
      v8::Local<v8::String> arg_str = NewString("a");
      v8::ScriptOrigin origin(NewString("filename"));

      i::DisallowCompilation* no_compile_expected =
          has_cache ? new i::DisallowCompilation(
                          reinterpret_cast<i::Isolate*>(isolate))
                    : nullptr;

      v8::ScriptCompiler::Source script_source(source, origin, cache);
      v8::Local<v8::Function> fun =
          v8::ScriptCompiler::CompileFunction(context, &script_source, 1,
                                              &arg_str, 0, nullptr, options)
              .ToLocalChecked();
      if (has_cache) {
        logger.StopLogging();
        logger.PrintLog();
        // Function is logged twice: once as interpreted, and once as the
        // interpreter entry trampoline builtin.
        CHECK(logger.ContainsLinesInOrder(
            {{"JS", "eyecatcher"}, {"JS", "eyecatcher"}}));
      }
      v8::Local<v8::Value> arg = Number::New(isolate, 3);
      v8::Local<v8::Value> result =
          fun->Call(context, v8::Undefined(isolate), 1, &arg).ToLocalChecked();
      CHECK_EQ(9, result->Int32Value(context).FromJust());
      cache = v8::ScriptCompiler::CreateCodeCacheForFunction(fun);

      if (no_compile_expected != nullptr) delete no_compile_expected;
    }

    isolate->Dispose();
  } while (!has_cache);
  delete cache;
}

class LogExternalLogEventListenerTest : public TestWithIsolate {
 public:
  static void SetUpTestSuite() {
    i::v8_flags.log = false;
    i::v8_flags.prof = false;
    TestWithIsolate::SetUpTestSuite();
  }
};

TEST_F(LogExternalLogEventListenerTest, ExternalLogEventListener) {
  {
    v8::HandleScope scope(isolate());
    v8::Isolate::Scope isolate_scope(isolate());
    v8::Local<v8::Context> context = v8::Context::New(isolate());
    v8::Context::Scope context_scope(context);

    TestCodeEventHandler code_event_handler(isolate());

    const char* source_text_before_start =
        "function testLogEventListenerBeforeStart(a,b) { return a + b };"
        "testLogEventListenerBeforeStart('1', 1);";
    RunJS(source_text_before_start);

    CHECK_EQ(code_event_handler.CountLines("Function",
                                           "testLogEventListenerBeforeStart"),
             0);
    // We no longer log LazyCompile.
    CHECK_EQ(code_event_handler.CountLines("LazyCompile",
                                           "testLogEventListenerBeforeStart"),
             0);

    code_event_handler.Enable();

    CHECK_GE(code_event_handler.CountLines("Function",
                                           "testLogEventListenerBeforeStart"),
             1);

    const char* source_text_after_start =
        "function testLogEventListenerAfterStart(a,b) { return a + b };"
        "testLogEventListenerAfterStart('1', 1);";
    RunJS(source_text_after_start);

    CHECK_GE(code_event_handler.CountLines("Function",
                                           "testLogEventListenerAfterStart"),
             1);
    // We no longer log LazyCompile.
    CHECK_GE(code_event_handler.CountLines("LazyCompile",
                                           "testLogEventListenerAfterStart"),
             0);
  }
}

class LogExternalLogEventListenerInnerFunctionTest : public TestWithPlatform {
 public:
  LogExternalLogEventListenerInnerFunctionTest()
      : array_buffer_allocator_(
            v8::ArrayBuffer::Allocator::NewDefaultAllocator()) {}
  static void SetUpTestSuite() {
    i::v8_flags.log = false;
    i::v8_flags.prof = false;
    TestWithPlatform::SetUpTestSuite();
  }

  v8::ArrayBuffer::Allocator* array_buffer_allocator() {
    return array_buffer_allocator_.get();
  }
  v8::Local<v8::String> NewString(const char* source) {
    return v8::String::NewFromUtf8(v8::Isolate::GetCurrent(), source)
        .ToLocalChecked();
  }

 private:
  std::unique_ptr<v8::ArrayBuffer::Allocator> array_buffer_allocator_;
};

TEST_F(LogExternalLogEventListenerInnerFunctionTest,
       ExternalLogEventListenerInnerFunctions) {
  v8::ScriptCompiler::CachedData* cache;
  static const char* source_cstring =
      "(function f1() { return (function f2() {}); })()";

  v8::Isolate::CreateParams create_params;
  create_params.array_buffer_allocator = array_buffer_allocator();

  v8::Isolate* isolate1 = v8::Isolate::New(create_params);
  {  // Test that we emit the correct code events from eagerly compiling.
    v8::HandleScope scope(isolate1);
    v8::Isolate::Scope isolate_scope(isolate1);
    v8::Local<v8::Context> context = v8::Context::New(isolate1);
    v8::Context::Scope context_scope(context);

    TestCodeEventHandler code_event_handler(isolate1);
    code_event_handler.Enable();

    v8::Local<v8::String> source_string = NewString(source_cstring);
    v8::ScriptOrigin origin(NewString("test"));
    v8::ScriptCompiler::Source source(source_string, origin);
    v8::Local<v8::UnboundScript> script =
        v8::ScriptCompiler::CompileUnboundScript(isolate1, &source)
            .ToLocalChecked();
    CHECK_EQ(code_event_handler.CountLines("Function", "f1"),
             1 + (i::v8_flags.stress_background_compile ? 1 : 0) +
                 (i::v8_flags.always_sparkplug ? 1 : 0));
    CHECK_EQ(code_event_handler.CountLines("Function", "f2"),
             1 + (i::v8_flags.stress_background_compile ? 1 : 0) +
                 (i::v8_flags.always_sparkplug ? 1 : 0));
    cache = v8::ScriptCompiler::CreateCodeCache(script);
  }
  isolate1->Dispose();

  v8::Isolate* isolate2 = v8::Isolate::New(create_params);
  {  // Test that we emit the correct code events from deserialization.
    v8::HandleScope scope(isolate2);
    v8::Isolate::Scope isolate_scope(isolate2);
    v8::Local<v8::Context> context = v8::Context::New(isolate2);
    v8::Context::Scope context_scope(context);

    TestCodeEventHandler code_event_handler(isolate2);
    code_event_handler.Enable();

    v8::Local<v8::String> source_string = NewString(source_cstring);
    v8::ScriptOrigin origin(NewString("test"));
    v8::ScriptCompiler::Source source(source_string, origin, cache);
    {
      i::DisallowCompilation no_compile_expected(
          reinterpret_cast<i::Isolate*>(isolate2));
      v8::ScriptCompiler::CompileUnboundScript(
          isolate2, &source, v8::ScriptCompiler::kConsumeCodeCache)
          .ToLocalChecked();
    }
    CHECK_EQ(code_event_handler.CountLines("Function", "f1"), 1);
    CHECK_EQ(code_event_handler.CountLines("Function", "f2"), 1);
  }
  isolate2->Dispose();
}

#ifndef V8_TARGET_ARCH_ARM

class LogExternalInterpretedFramesNativeStackTest : public TestWithIsolate {
 public:
  static void SetUpTestSuite() {
    i::v8_flags.log = false;
    i::v8_flags.prof = false;
    i::v8_flags.interpreted_frames_native_stack = true;
    TestWithIsolate::SetUpTestSuite();
  }
};
TEST_F(LogExternalInterpretedFramesNativeStackTest,
       ExternalLogEventListenerWithInterpretedFramesNativeStack) {
  {
    v8::HandleScope scope(isolate());
    v8::Isolate::Scope isolate_scope(isolate());
    v8::Local<v8::Context> context = v8::Context::New(isolate());
    context->Enter();

    i::FakeCodeEventLogger code_event_logger(i_isolate());
    CHECK(i_isolate()->logger()->AddListener(&code_event_logger));

    TestCodeEventHandler code_event_handler(isolate());

    const char* source_text_before_start =
        "function testLogEventListenerBeforeStart(a,b) { return a + b };"
        "testLogEventListenerBeforeStart('1', 1);";
    RunJS(source_text_before_start);

    CHECK_EQ(code_event_handler.CountLines("Function",
                                           "testLogEventListenerBeforeStart"),
             0);

    code_event_handler.Enable();

    CHECK_GE(code_event_handler.CountLines("Function",
                                           "testLogEventListenerBeforeStart"),
             2);

    const char* source_text_after_start =
        "function testLogEventListenerAfterStart(a,b) { return a + b };"
        "testLogEventListenerAfterStart('1', 1);";
    RunJS(source_text_after_start);

    CHECK_GE(code_event_handler.CountLines("Function",
                                           "testLogEventListenerAfterStart"),
             2);

    CHECK_EQ(
        code_event_handler.CountLines("Builtin", "InterpreterEntryTrampoline"),
        1);

    context->Exit();
    CHECK(i_isolate()->logger()->RemoveListener(&code_event_logger));
  }
}
#endif  // V8_TARGET_ARCH_ARM

class LogMapsTest : public LogTest {
 public:
  static void SetUpTestSuite() {
    i::v8_flags.log_maps = true;
    LogTest::SetUpTestSuite();
  }
};
TEST_F(LogMapsTest, TraceMaps) {
  {
    ScopedLoggerInitializer logger(isolate());
    // Try to create many different kind of maps to make sure the logging won't
    // crash. More detailed tests are implemented separately.
    const char* source_text = R"(
      let a = {};
      for (let i = 0; i < 500; i++) {
        a['p'+i] = i
      };
      class Test {
        constructor(i) {
          this.a = 1;
          this['p'+i] = 1;
        }
      };
      let t = new Test();
      t.b = 1; t.c = 1; t.d = 3;
      for (let i = 0; i < 100; i++) {
        t = new Test(i)
      };
      t.b = {};
    )";
    RunJS(source_text);

    logger.StopLogging();

    // Mostly superficial checks.
    CHECK(logger.ContainsLine({"map,InitialMap", ",0x"}));
    CHECK(logger.ContainsLine({"map,Transition", ",0x"}));
    CHECK(logger.ContainsLine({"map-details", ",0x"}));
  }
}

namespace {
// Ensure that all Maps found on the heap have a single corresponding map-create
// and map-details entry in the v8.log.
void ValidateMapDetailsLogging(v8::Isolate* isolate,
                               ScopedLoggerInitializer* logger) {
  // map-create might have duplicates if a Map address is reused after a gc.
  std::unordered_set<uintptr_t> map_create_addresses =
      logger->ExtractLogAddresses("map-create", 2, true);
  std::unordered_set<uintptr_t> map_details_addresses =
      logger->ExtractLogAddresses("map-details", 2, false);

  // Iterate over all maps on the heap.
  i::Heap* heap = reinterpret_cast<i::Isolate*>(isolate)->heap();
  i::HeapObjectIterator iterator(heap);
  i::DisallowGarbageCollection no_gc;
  size_t i = 0;
  for (i::Tagged<i::HeapObject> obj = iterator.Next(); !obj.is_null();
       obj = iterator.Next()) {
    if (!IsMap(obj)) continue;
    i++;
    uintptr_t address = obj.ptr();
    if (map_create_addresses.find(address) == map_create_addresses.end()) {
      // logger->PrintLog();
      i::Print(i::Cast<i::Map>(obj));
      FATAL(
          "Map (%p, #%zu) creation not logged during startup with "
          "--log-maps!"
          "\n# Expected Log Line: map-create, ... %p",
          reinterpret_cast<void*>(obj.ptr()), i,
          reinterpret_cast<void*>(obj.ptr()));
    } else if (map_details_addresses.find(address) ==
               map_details_addresses.end()) {
      // logger->PrintLog();
      i::Print(i::Cast<i::Map>(obj));
      FATAL(
          "Map (%p, #%zu) details not logged during startup with "
          "--log-maps!"
          "\n# Expected Log Line: map-details, ... %p",
          reinterpret_cast<void*>(obj.ptr()), i,
          reinterpret_cast<void*>(obj.ptr()));
    }
  }
}

}  // namespace

TEST_F(LogMapsTest, LogMapsDetailsStartup) {
  // Reusing map addresses might cause these tests to fail.
  if (i::v8_flags.gc_global || i::v8_flags.stress_compaction ||
      i::v8_flags.stress_incremental_marking) {
    return;
  }
  // Test that all Map details from Maps in the snapshot are logged properly.
  {
    ScopedLoggerInitializer logger(isolate());
    logger.StopLogging();
    ValidateMapDetailsLogging(isolate(), &logger);
  }
}

class LogMapsCodeTest : public LogTest {
 public:
  static void SetUpTestSuite() {
    i::v8_flags.retain_maps_for_n_gc = 0xFFFFFFF;
    i::v8_flags.log_maps = true;
    LogTest::SetUpTestSuite();
  }
};

TEST_F(LogMapsCodeTest, LogMapsDetailsCode) {
  // Reusing map addresses might cause these tests to fail.
  if (i::v8_flags.gc_global || i::v8_flags.stress_compaction ||
      i::v8_flags.stress_incremental_marking) {
    return;
  }

  const char* source = R"(
    // Normal properties overflowing into dict-mode.
    let a = {};
    for (let i = 0; i < 500; i++) {
      a['p'+i] = i
    };
    // Constructor / initial maps
    function Constructor(dictElements=false) {
      this.a = 1;
      this.b = 2;
      this.c = 3;
      if (dictElements) {
        this[0xFFFFF] = 1;
      }
      this.d = 4;
      this.e = 5;
      this.f = 5;
    }
    // Keep objects and their maps alive to avoid reusing map addresses.
    let instances = [];
    let instance;
    for (let i =0; i < 500; i++) {
      instances.push(new Constructor());
    }
    // Map deprecation.
    for (let i =0; i < 500; i++) {
      instance = new Constructor();
      instance.d = 1.1;
      instances.push(instance);
    }
    for (let i =0; i < 500; i++) {
      instance = new Constructor();
      instance.b = 1.1;
      instances.push(instance);
    }
    for (let i =0; i < 500; i++) {
      instance = new Constructor();
      instance.c = Object;
      instances.push(instance);
    }
    // Create instance with dict-elements.
    instances.push(new Constructor(true));

    // Class
    class Test {
      constructor(i) {
        this.a = 1;
        this['p'+i] = 1;
      }
    };
    let t = new Test();
    t.b = 1; t.c = 1; t.d = 3;
    for (let i = 0; i < 100; i++) {
      t = new Test(i);
      instances.push(t);
    }
    t.b = {};

    // Anonymous classes
    function create(value) {
      return new class {
        constructor() {
          this.value = value;
        }
      }
    }
    for (let i = 0; i < 100; i++) {
      instances.push(create(i));
    };

    // Modifying some protoypes.
    Array.prototype.helper = () => 1;
    [1,2,3].helper();
  )";
  {
    ScopedLoggerInitializer logger(isolate());
    RunJS(source);
    logger.StopLogging();
    ValidateMapDetailsLogging(isolate(), &logger);
  }
}

TEST_F(LogMapsTest, LogMapsDetailsContexts) {
  // Reusing map addresses might cause these tests to fail.
  if (i::v8_flags.gc_global || i::v8_flags.stress_compaction ||
      i::v8_flags.stress_incremental_marking) {
    return;
  }
  // Test that all Map details from Maps in the snapshot are logged properly.
  {
    ScopedLoggerInitializer logger(isolate());
    // Use the default context.
    RunJS("{a:1}");
    // Create additional contexts.
    v8::Local<v8::Context> env1 = v8::Context::New(isolate());
    env1->Enter();
    RunJS("{b:1}");

    v8::Local<v8::Context> env2 = v8::Context::New(isolate());
    env2->Enter();
    RunJS("{c:1}");
    env2->Exit();
    env1->Exit();

    logger.StopLogging();
    ValidateMapDetailsLogging(isolate(), &logger);
  }
}

class LogTimerTest : public LogTest {
 public:
  static void SetUpTestSuite() {
    i::v8_flags.log_timer_events = true;
    LogTest::SetUpTestSuite();
  }
};

TEST_F(LogTimerTest, ConsoleTimeEvents) {
  {
    ScopedLoggerInitializer logger(isolate());
    {
      // setup console global.
      v8::HandleScope scope(isolate());
      v8::Local<v8::String> name = v8::String::NewFromUtf8Literal(
          isolate(), "console", v8::NewStringType::kInternalized);
      v8::Local<v8::Context> context = isolate()->GetCurrentContext();
      v8::Local<v8::Value> console = context->GetExtrasBindingObject()
                                         ->Get(context, name)
                                         .ToLocalChecked();
      context->Global()->Set(context, name, console).FromJust();
    }
    // Test that console time events are properly logged
    const char* source_text =
        "console.time();"
        "console.timeEnd();"
        "console.timeLog();"
        "console.time('timerEvent1');"
        "console.timeEnd('timerEvent1');"
        "console.timeLog('timerEvent2');"
        "console.timeLog('timerEvent3');";
    RunJS(source_text);

    logger.StopLogging();

    std::vector<std::vector<std::string>> lines = {
        {"timer-event-start,default,"},   {"timer-event-end,default,"},
        {"timer-event,default,"},         {"timer-event-start,timerEvent1,"},
        {"timer-event-end,timerEvent1,"}, {"timer-event,timerEvent2,"},
        {"timer-event,timerEvent3,"}};
    CHECK(logger.ContainsLinesInOrder(lines));
  }
}

class LogFunctionEventsTest : public LogTest {
 public:
  static void SetUpTestSuite() {
    i::v8_flags.log_function_events = true;
    LogTest::SetUpTestSuite();
  }
};

TEST_F(LogFunctionEventsTest, LogFunctionEvents) {
  // --always-turbofan will break the fine-grained log order.
  if (i::v8_flags.always_turbofan) return;

  {
    ScopedLoggerInitializer logger(isolate());

    // Run some warmup code to help ignoring existing log entries.
    RunJS(
        "function warmUp(a) {"
        " let b = () => 1;"
        " return function(c) { return a+b+c; };"
        "};"
        "warmUp(1)(2);"
        "(function warmUpEndMarkerFunction(){})();");

    const char* source_text =
        "function lazyNotExecutedFunction() { return 'lazy' };"
        "function lazyFunction() { "
        "  function lazyInnerFunction() { return 'lazy' };"
        "  return lazyInnerFunction;"
        "};"
        "let innerFn = lazyFunction();"
        "innerFn();"
        "(function eagerFunction(){ return 'eager' })();"
        "function Foo() { this.foo = function(){}; };"
        "let i = new Foo(); i.foo();";
    RunJS(source_text);

    logger.StopLogging();

    // TODO(cbruni): Reimplement first-execution logging if needed.
    std::vector<std::vector<std::string>> lines = {
        // Create a new script
        {"script,create"},
        {"script-details"},
        // Step 1: parsing top-level script, preparsing functions
        {"function,preparse-", ",lazyNotExecutedFunction"},
        // Missing name for preparsing lazyInnerFunction
        // {"function,preparse-", nullptr},
        {"function,preparse-", ",lazyFunction"},
        {"function,full-parse,", ",eagerFunction"},
        {"function,preparse-", ",Foo"},
        // Missing name for inner preparsing of Foo.foo
        // {"function,preparse-", nullptr},
        // Missing name for top-level script.
        {"function,parse-script,"},

        // Step 2: compiling top-level script and eager functions
        // - Compiling script without name.
        {"function,interpreter,"},
        {"function,interpreter,", ",eagerFunction"},

        // Step 3: start executing script
        // Step 4. - lazy parse, lazy compiling and execute skipped functions
        //         - execute eager functions.
        {"function,parse-function,", ",lazyFunction"},
        {"function,interpreter,", ",lazyFunction"},

        {"function,parse-function,", ",lazyInnerFunction"},
        {"function,interpreter,", ",lazyInnerFunction"},

        {"function,parse-function,", ",Foo"},
        {"function,interpreter,", ",Foo"},

        {"function,parse-function,", ",Foo.foo"},
        {"function,interpreter,", ",Foo.foo"},
    };
    CHECK(logger.ContainsLinesInOrder(lines));
  }
}

TEST_F(LogTest, BuiltinsNotLoggedAsLazyCompile) {
  {
    ScopedLoggerInitializer logger(isolate());

    logger.LogCodeObjects();
    logger.LogCompiledFunctions();
    logger.StopLogging();

    i::Isolate* i_isolate = logger.i_isolate();
    i::DirectHandle<i::Code> builtin =
        BUILTIN_CODE(i_isolate, BooleanConstructor);
    v8::base::EmbeddedVector<char, 100> buffer;

    // Should only be logged as "Builtin" with a name, never as "Function".
    v8::base::SNPrintF(buffer, ",0x%" V8PRIxPTR ",%d,BooleanConstructor",
                       builtin->instruction_start(),
                       builtin->instruction_size());
    CHECK(logger.ContainsLine(
        {"code-creation,Builtin,2,", std::string(buffer.begin())}));

    v8::base::SNPrintF(buffer, ",0x%" V8PRIxPTR ",%d,",
                       builtin->instruction_start(),
                       builtin->instruction_size());
    CHECK(!logger.ContainsLine(
        {"code-creation,JS,2,", std::string(buffer.begin())}));
  }
}
}  // namespace v8
