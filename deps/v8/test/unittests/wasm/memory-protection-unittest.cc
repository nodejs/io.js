// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <optional>

#include "include/v8config.h"

// TODO(clemensb): Extend this to other OSes.
#if V8_OS_POSIX && !V8_OS_FUCHSIA
#include <signal.h>
#endif  // V8_OS_POSIX && !V8_OS_FUCHSIA

#include "src/base/macros.h"
#include "src/flags/flags.h"
#include "src/wasm/code-space-access.h"
#include "src/wasm/module-compiler.h"
#include "src/wasm/module-decoder.h"
#include "src/wasm/wasm-engine.h"
#include "src/wasm/wasm-features.h"
#include "src/wasm/wasm-opcodes.h"
#include "test/common/wasm/wasm-macro-gen.h"
#include "test/unittests/test-utils.h"
#include "testing/gmock/include/gmock/gmock-matchers.h"

namespace v8::internal::wasm {

class MemoryProtectionTest : public TestWithNativeContext {
 public:
  void SetUp() override {
    v8_flags.wasm_lazy_compilation = false;
    // The key is initially write-protected.
    CHECK_IMPLIES(WasmCodeManager::HasMemoryProtectionKeySupport(),
                  !WasmCodeManager::MemoryProtectionKeyWritable());
  }

  void CompileModule() {
    CHECK_NULL(native_module_);
    native_module_ = CompileNativeModule();
    code_ = native_module_->GetCode(0);
  }

  NativeModule* native_module() const { return native_module_.get(); }

  WasmCode* code() const { return code_; }

  bool code_is_protected() {
    return V8_HAS_PTHREAD_JIT_WRITE_PROTECT ||
           V8_HAS_BECORE_JIT_WRITE_PROTECT || uses_pku();
  }

  void WriteToCode() { code_->instructions()[0] = 0; }

  void AssertCodeEventuallyProtected() {
    if (!code_is_protected()) {
      // Without protection, writing to code should always work.
      WriteToCode();
      return;
    }
    ASSERT_DEATH_IF_SUPPORTED(
        {
          WriteToCode();
          base::OS::Sleep(base::TimeDelta::FromMilliseconds(10));
        },
        "");
  }

  bool uses_pku() {
    // M1 always uses MAP_JIT.
    if (V8_HAS_PTHREAD_JIT_WRITE_PROTECT || V8_HAS_BECORE_JIT_WRITE_PROTECT) {
      return false;
    }
    return WasmCodeManager::HasMemoryProtectionKeySupport();
  }

 private:
  std::shared_ptr<NativeModule> CompileNativeModule() {
    // Define the bytes for a module with a single empty function.
    static const uint8_t module_bytes[] = {
        WASM_MODULE_HEADER, SECTION(Type, ENTRY_COUNT(1), SIG_ENTRY_v_v),
        SECTION(Function, ENTRY_COUNT(1), SIG_INDEX(0)),
        SECTION(Code, ENTRY_COUNT(1), ADD_COUNT(0 /* locals */, kExprEnd))};

    base::OwnedVector<const uint8_t> bytes = base::OwnedCopyOf(module_bytes);

    WasmDetectedFeatures detected_features;
    ModuleResult result =
        DecodeWasmModule(WasmEnabledFeatures::All(), bytes.as_vector(), false,
                         kWasmOrigin, &detected_features);
    CHECK(result.ok());

    ErrorThrower thrower(isolate(), "");
    constexpr int kNoCompilationId = 0;
    constexpr ProfileInformation* kNoProfileInformation = nullptr;
    std::shared_ptr<NativeModule> native_module = CompileToNativeModule(
        isolate(), WasmEnabledFeatures::All(), detected_features,
        CompileTimeImports{}, &thrower, std::move(result).value(),
        std::move(bytes), kNoCompilationId,
        v8::metrics::Recorder::ContextId::Empty(), kNoProfileInformation);
    CHECK(!thrower.error());
    CHECK_NOT_NULL(native_module);

    return native_module;
  }

  std::shared_ptr<NativeModule> native_module_;
  WasmCodeRefScope code_refs_;
  WasmCode* code_;
};

TEST_F(MemoryProtectionTest, CodeNotWritableAfterCompilation) {
  CompileModule();
  AssertCodeEventuallyProtected();
}

TEST_F(MemoryProtectionTest, CodeWritableWithinScope) {
  CompileModule();
  CodeSpaceWriteScope write_scope;
  WriteToCode();
}

TEST_F(MemoryProtectionTest, CodeNotWritableAfterScope) {
  CompileModule();
  {
    CodeSpaceWriteScope write_scope;
    WriteToCode();
  }
  AssertCodeEventuallyProtected();
}

#if V8_OS_POSIX && !V8_OS_FUCHSIA
class ParameterizedMemoryProtectionTestWithSignalHandling
    : public MemoryProtectionTest,
      public ::testing::WithParamInterface<std::tuple<bool, bool>> {
 public:
  class SignalHandlerScope {
   public:
    SignalHandlerScope() {
      CHECK_NULL(current_handler_scope_);
      current_handler_scope_ = this;
      struct sigaction sa;
      sa.sa_sigaction = &HandleSignal;
      sigemptyset(&sa.sa_mask);
      sa.sa_flags = SA_RESTART | SA_SIGINFO | SA_ONSTACK;
      CHECK_EQ(0, sigaction(SIGPROF, &sa, &old_signal_handler_));
    }

    ~SignalHandlerScope() {
      CHECK_EQ(current_handler_scope_, this);
      current_handler_scope_ = nullptr;
      sigaction(SIGPROF, &old_signal_handler_, nullptr);
    }

    void SetAddressToWriteToOnSignal(uint8_t* address) {
      CHECK_NULL(code_address_);
      CHECK_NOT_NULL(address);
      code_address_ = address;
    }

    int num_handled_signals() const { return handled_signals_; }

   private:
    static void HandleSignal(int signal, siginfo_t*, void*) {
      // We execute on POSIX only, so we just directly use {printf} and friends.
      if (signal == SIGPROF) {
        printf("Handled SIGPROF.\n");
      } else {
        printf("Handled unknown signal: %d.\n", signal);
      }
      CHECK_NOT_NULL(current_handler_scope_);
      current_handler_scope_->handled_signals_ += 1;
      if (uint8_t* write_address = current_handler_scope_->code_address_) {
        // Print to the error output such that we can check against this message
        // in the ASSERT_DEATH_IF_SUPPORTED below.
        fprintf(stderr, "Writing to code.\n");
        // This write will crash if code is protected.
        *write_address = 0;
        fprintf(stderr, "Successfully wrote to code.\n");
      }
    }

    struct sigaction old_signal_handler_;
    int handled_signals_ = 0;
    uint8_t* code_address_ = nullptr;

    // These are accessed from the signal handler.
    static SignalHandlerScope* current_handler_scope_;
  };
};

// static
ParameterizedMemoryProtectionTestWithSignalHandling::SignalHandlerScope*
    ParameterizedMemoryProtectionTestWithSignalHandling::SignalHandlerScope::
        current_handler_scope_ = nullptr;

std::string PrintMemoryProtectionAndSignalHandlingTestParam(
    ::testing::TestParamInfo<std::tuple<bool, bool>> info) {
  const bool write_in_signal_handler = std::get<0>(info.param);
  const bool open_write_scope = std::get<1>(info.param);
  return std::string(write_in_signal_handler ? "Write" : "NoWrite") + "_" +
         (open_write_scope ? "WithScope" : "NoScope");
}

INSTANTIATE_TEST_SUITE_P(MemoryProtection,
                         ParameterizedMemoryProtectionTestWithSignalHandling,
                         ::testing::Combine(::testing::Bool(),
                                            ::testing::Bool()),
                         PrintMemoryProtectionAndSignalHandlingTestParam);

TEST_P(ParameterizedMemoryProtectionTestWithSignalHandling, TestSignalHandler) {
  // We must run in the "threadsafe" mode in order to make the spawned process
  // for the death test(s) re-execute the whole unit test up to the point of the
  // death test. Otherwise we would not really test the signal handling setup
  // that we use in the wild.
  // (see https://google.github.io/googletest/reference/assertions.html)
  CHECK_EQ("threadsafe", GTEST_FLAG_GET(death_test_style));

  const bool write_in_signal_handler = std::get<0>(GetParam());
  const bool open_write_scope = std::get<1>(GetParam());
  CompileModule();
  SignalHandlerScope signal_handler_scope;

  CHECK_EQ(0, signal_handler_scope.num_handled_signals());
  pthread_kill(pthread_self(), SIGPROF);
  CHECK_EQ(1, signal_handler_scope.num_handled_signals());

  uint8_t* code_start_ptr = &code()->instructions()[0];
  uint8_t code_start = *code_start_ptr;
  CHECK_NE(0, code_start);
  if (write_in_signal_handler) {
    signal_handler_scope.SetAddressToWriteToOnSignal(code_start_ptr);
  }

  // If the signal handler writes to protected code we expect a crash.
  // An exception is M1, where an open scope still has an effect in the signal
  // handler.
  bool expect_crash = write_in_signal_handler && code_is_protected() &&
                      ((!V8_HAS_PTHREAD_JIT_WRITE_PROTECT &&
                        !V8_HAS_BECORE_JIT_WRITE_PROTECT) ||
                       !open_write_scope);
  if (expect_crash) {
    // Avoid {ASSERT_DEATH_IF_SUPPORTED}, because it only accepts a regex as
    // second parameter, and not a matcher as {ASSERT_DEATH}.
#if GTEST_HAS_DEATH_TEST
    ASSERT_DEATH(
        {
          std::optional<CodeSpaceWriteScope> write_scope;
          if (open_write_scope) write_scope.emplace();
          pthread_kill(pthread_self(), SIGPROF);
          base::OS::Sleep(base::TimeDelta::FromMilliseconds(10));
        },
        // Check that the subprocess tried to write, but did not succeed.
        ::testing::AnyOf(
            // non-sanitizer builds:
            ::testing::EndsWith("Writing to code.\n"),
            // ASan:
            ::testing::HasSubstr("Writing to code.\n"
                                 "AddressSanitizer:DEADLYSIGNAL"),
            // MSan:
            ::testing::HasSubstr("Writing to code.\n"
                                 "MemorySanitizer:DEADLYSIGNAL"),
            // UBSan:
            ::testing::HasSubstr("Writing to code.\n"
                                 "UndefinedBehaviorSanitizer:DEADLYSIGNAL")));
#endif  // GTEST_HAS_DEATH_TEST
  } else {
    std::optional<CodeSpaceWriteScope> write_scope;
    if (open_write_scope) write_scope.emplace();
    // The signal handler does not write or code is not protected, hence this
    // should succeed.
    pthread_kill(pthread_self(), SIGPROF);

    CHECK_EQ(2, signal_handler_scope.num_handled_signals());
    CHECK_EQ(write_in_signal_handler ? 0 : code_start, *code_start_ptr);
  }
}
#endif  // V8_OS_POSIX && !V8_OS_FUCHSIA

}  // namespace v8::internal::wasm
