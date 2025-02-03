// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <regex>
#include <string>

#include "src/base/vector.h"
#include "src/wasm/module-decoder.h"
#include "src/wasm/string-builder-multiline.h"
#include "src/wasm/wasm-disassembler-impl.h"
#include "test/unittests/test-utils.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace v8 {
namespace internal {
namespace wasm {

class WasmDisassemblerTest : public ::v8::TestWithPlatform {};

// Code that is shared for all tests, the only difference is the input module
// and expected disassembler output.
void CheckDisassemblerOutput(base::Vector<const uint8_t> module_bytes,
                             std::string expected_output) {
  AccountingAllocator allocator;

  std::unique_ptr<OffsetsProvider> offsets = AllocateOffsetsProvider();
  ModuleResult module_result =
      DecodeWasmModuleForDisassembler(module_bytes, offsets.get());
  ASSERT_TRUE(module_result.ok())
      << "Decoding error: " << module_result.error().message() << " at offset "
      << module_result.error().offset();
  WasmModule* module = module_result.value().get();

  ModuleWireBytes wire_bytes(module_bytes);
  NamesProvider names(module, module_bytes);

  MultiLineStringBuilder output_sb;

  constexpr bool kNoOffsets = false;
  ModuleDisassembler md(output_sb, module, &names, wire_bytes, &allocator,
                        std::move(offsets));
  constexpr size_t max_mb = 100;  // Even 1 would be enough.
  md.PrintModule({0, 2}, max_mb);

  std::ostringstream output;
  output_sb.WriteTo(output, kNoOffsets);

  // Remove comment lines from expected output since they cannot be recovered
  // by a disassembler.
  // They were also used as part of the C++/WAT polyglot trick described below.
  std::regex comment_regex(" *;;[^\\n]*\\n?");
  expected_output = std::regex_replace(expected_output, comment_regex, "");
  std::string output_str = std::regex_replace(output.str(), comment_regex, "");

  EXPECT_EQ(expected_output, output_str);
}

TEST_F(WasmDisassemblerTest, Mvp) {
  // If you want to extend this test (and the other tests below):
  // 1. Modify the included .wat.inc file(s), e.g., add more instructions.
  // 2. Convert the Wasm text file to a Wasm binary with `wat2wasm`.
  // 3. Convert the Wasm binary to an array init expression with
  // `wami --full-hexdump` and paste it into the included file below.
  // One liner example (Linux):
  // wat2wasm wasm-disassembler-unittest-mvp.wat.inc --output=-
  // | wami --full-hexdump
  // | head -n-1 | tail -n+2 > wasm-disassembler-unittest-mvp.wasm.inc
  constexpr uint8_t module_bytes[] = {
#include "wasm-disassembler-unittest-mvp.wasm.inc"
  };

  // Little trick: polyglot C++/WebAssembly text file.
  // We want to include the expected disassembler text output as a string into
  // this test (instead of reading it from the file at runtime, which would make
  // it dependent on the current working directory).
  // At the same time, we want the included file itself to be valid WAT, such
  // that it can be processed e.g. by wat2wasm to build the module bytes above.
  // For that to work, we abuse that ;; starts a line comment in WAT, but at
  // the same time, ;; in C++ are just two empty statements, which are no
  // harm when including the file here either.
  std::string expected;
#include "wasm-disassembler-unittest-mvp.wat.inc"

  CheckDisassemblerOutput(base::ArrayVector(module_bytes), expected);
}

TEST_F(WasmDisassemblerTest, Names) {
  // You can create a binary with a custom name section from the text format via
  // `wat2wasm --debug-names`.
  constexpr uint8_t module_bytes[] = {
#include "wasm-disassembler-unittest-names.wasm.inc"
  };
  std::string expected;
#include "wasm-disassembler-unittest-names.wat.inc"
  CheckDisassemblerOutput(base::ArrayVector(module_bytes), expected);
}

TEST_F(WasmDisassemblerTest, InvalidNameSection) {
  constexpr uint8_t module_bytes[] = {
#include "wasm-disassembler-unittest-bad-name-section.wasm.inc"
  };
  std::string expected(
      "(module\n"
      "  (table $x (;0;) 0 funcref)\n"
      ")\n");
  CheckDisassemblerOutput(base::ArrayVector(module_bytes), expected);
}

TEST_F(WasmDisassemblerTest, Simd) {
  constexpr uint8_t module_bytes[] = {
#include "wasm-disassembler-unittest-simd.wasm.inc"
  };
  std::string expected;
#include "wasm-disassembler-unittest-simd.wat.inc"
  CheckDisassemblerOutput(base::ArrayVector(module_bytes), expected);
}

TEST_F(WasmDisassemblerTest, Gc) {
  // Since WABT's `wat2wasm` didn't support some GC features yet, I used
  // Binaryen's `wasm-as --enable-gc --hybrid` here to produce the binary.
  constexpr uint8_t module_bytes[] = {
#include "wasm-disassembler-unittest-gc.wasm.inc"
  };
  std::string expected;
#include "wasm-disassembler-unittest-gc.wat.inc"
  CheckDisassemblerOutput(base::ArrayVector(module_bytes), expected);
}

TEST_F(WasmDisassemblerTest, TooManyends) {
  constexpr uint8_t module_bytes[] = {
#include "wasm-disassembler-unittest-too-many-ends.wasm.inc"
  };
  std::string expected;
#include "wasm-disassembler-unittest-too-many-ends.wat.inc"
  CheckDisassemblerOutput(base::ArrayVector(module_bytes), expected);
}

TEST_F(WasmDisassemblerTest, Stringref) {
  constexpr uint8_t module_bytes[] = {
#include "wasm-disassembler-unittest-stringref.wasm.inc"
  };
  std::string expected;
#include "wasm-disassembler-unittest-stringref.wat.inc"
  CheckDisassemblerOutput(base::ArrayVector(module_bytes), expected);
}

TEST_F(WasmDisassemblerTest, Exnref) {
  constexpr uint8_t module_bytes[] = {
#include "wasm-disassembler-unittest-exnref.wasm.inc"
  };
  std::string expected;
#include "wasm-disassembler-unittest-exnref.wat.inc"
  CheckDisassemblerOutput(base::ArrayVector(module_bytes), expected);
}

// TODO(dlehmann): Add tests for the following Wasm features and extensions:
// - custom name section for Wasm GC constructs (struct and array type names,
// struct fields).
// - exception-related instructions (try, catch, catch_all, delegate) and named
// exception tags.
// - atomic instructions (threads proposal, 0xfe prefix).
// - some "numeric" instructions (0xfc prefix).

}  // namespace wasm
}  // namespace internal
}  // namespace v8
