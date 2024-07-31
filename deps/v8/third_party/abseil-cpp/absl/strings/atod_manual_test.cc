// Copyright 2022 The Abseil Authors
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     https://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

// This program tests the absl::SimpleAtod and absl::SimpleAtof functions. Run
// it as "atod_manual_test pnftd/data/*.txt" where the pnftd directory is a
// local checkout of the https://github.com/nigeltao/parse-number-fxx-test-data
// repository. The test suite lives in a separate repository because its more
// than 5 million test cases weigh over several hundred megabytes and because
// the test cases are also useful to other software projects, not just Abseil.
// Its data/*.txt files contain one test case per line, like:
//
// 3C00 3F800000 3FF0000000000000 1
// 3D00 3FA00000 3FF4000000000000 1.25
// 3D9A 3FB33333 3FF6666666666666 1.4
// 57B7 42F6E979 405EDD2F1A9FBE77 123.456
// 622A 44454000 4088A80000000000 789
// 7C00 7F800000 7FF0000000000000 123.456e789
//
// For each line (and using 0-based column indexes), columns [5..13] and
// [14..30] contain the 32-bit float and 64-bit double result of parsing
// columns [31..].
//
// For example, parsing "1.4" as a float gives the bits 0x3FB33333.
//
// In this 6-line example, the final line's float and double values are all
// infinity. The largest finite float and double values are approximately
// 3.40e+38 and 1.80e+308.

#include <cstdint>
#include <cstdio>
#include <string>

#include "absl/base/casts.h"
#include "absl/strings/numbers.h"
#include "absl/strings/str_format.h"
#include "absl/strings/string_view.h"
#include "absl/types/optional.h"

static constexpr uint8_t kUnhex[256] = {
    0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,  //
    0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,  //
    0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,  //
    0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,  //
    0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,  //
    0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,  //
    0x0, 0x1, 0x2, 0x3, 0x4, 0x5, 0x6, 0x7,  // '0' ..= '7'
    0x8, 0x9, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,  // '8' ..= '9'

    0x0, 0xA, 0xB, 0xC, 0xD, 0xE, 0xF, 0x0,  // 'A' ..= 'F'
    0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,  //
    0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,  //
    0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,  //
    0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,  //
    0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,  //
    0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,  //
    0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,  //

    0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,  //
    0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,  //
    0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,  //
    0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,  //
    0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,  //
    0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,  //
    0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,  //
    0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,  //

    0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,  //
    0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,  //
    0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,  //
    0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,  //
    0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,  //
    0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,  //
    0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,  //
    0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,  //
};

static absl::optional<std::string> ReadFileToString(const char* filename) {
  FILE* f = fopen(filename, "rb");
  if (!f) {
    return absl::nullopt;
  }
  fseek(f, 0, SEEK_END);
  size_t size = ftell(f);
  fseek(f, 0, SEEK_SET);
  std::string s(size, '\x00');
  size_t n = fread(&s[0], 1, size, f);
  fclose(f);
  if (n != size) {
    return absl::nullopt;
  }
  return s;
}

static bool ProcessOneTestFile(const char* filename) {
  absl::optional<std::string> contents = ReadFileToString(filename);
  if (!contents) {
    absl::FPrintF(stderr, "Invalid file: %s\n", filename);
    return false;
  }

  int num_cases = 0;
  for (absl::string_view v(*contents); !v.empty();) {
    size_t new_line = v.find('\n');
    if ((new_line == absl::string_view::npos) || (new_line < 32)) {
      break;
    }
    absl::string_view input = v.substr(31, new_line - 31);

    // Test absl::SimpleAtof.
    {
      float f;
      if (!absl::SimpleAtof(input, &f)) {
        absl::FPrintF(stderr, "Could not parse \"%s\" in %s\n", input,
                      filename);
        return false;
      }
      uint32_t have32 = absl::bit_cast<uint32_t>(f);

      uint32_t want32 = 0;
      for (int i = 0; i < 8; i++) {
        want32 = (want32 << 4) | kUnhex[static_cast<unsigned char>(v[5 + i])];
      }

      if (have32 != want32) {
        absl::FPrintF(stderr,
                      "absl::SimpleAtof failed parsing \"%s\" in %s\n  have  "
                      "%08X\n  want  %08X\n",
                      input, filename, have32, want32);
        return false;
      }
    }

    // Test absl::SimpleAtod.
    {
      double d;
      if (!absl::SimpleAtod(input, &d)) {
        absl::FPrintF(stderr, "Could not parse \"%s\" in %s\n", input,
                      filename);
        return false;
      }
      uint64_t have64 = absl::bit_cast<uint64_t>(d);

      uint64_t want64 = 0;
      for (int i = 0; i < 16; i++) {
        want64 = (want64 << 4) | kUnhex[static_cast<unsigned char>(v[14 + i])];
      }

      if (have64 != want64) {
        absl::FPrintF(stderr,
                      "absl::SimpleAtod failed parsing \"%s\" in %s\n  have  "
                      "%016X\n  want  %016X\n",
                      input, filename, have64, want64);
        return false;
      }
    }

    num_cases++;
    v = v.substr(new_line + 1);
  }
  printf("%8d OK in %s\n", num_cases, filename);
  return true;
}

int main(int argc, char** argv) {
  if (argc < 2) {
    absl::FPrintF(
        stderr,
        "Usage: %s pnftd/data/*.txt\nwhere the pnftd directory is a local "
        "checkout of "
        "the\nhttps://github.com/nigeltao/parse-number-fxx-test-data "
        "repository.\n",
        argv[0]);
    return 1;
  }

  for (int i = 1; i < argc; i++) {
    if (!ProcessOneTestFile(argv[i])) {
      return 1;
    }
  }
  return 0;
}
