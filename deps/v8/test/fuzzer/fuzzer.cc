// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include <vector>

extern "C" int LLVMFuzzerInitialize(int* argc, char*** argv);
extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size);

int main(int argc, char* argv[]) {
  if (LLVMFuzzerInitialize(&argc, &argv)) {
    fprintf(stderr, "Failed to initialize fuzzer target\n");
    return 1;
  }

  std::vector<uint8_t> input_data;

  bool after_dash_dash = false;
  for (int arg_idx = 1; arg_idx < argc; ++arg_idx) {
    const char* const arg = argv[arg_idx];
    // Ignore first '--' argument.
    if (!after_dash_dash && arg[0] == '-' && arg[1] == '-' && arg[2] == '\0') {
      after_dash_dash = true;
      continue;
    }

    FILE* input = fopen(arg, "rb");
    if (!input) {
      fprintf(stderr, "Failed to open '%s'\n", arg);
      return 1;
    }

    fseek(input, 0, SEEK_END);
    size_t size = ftell(input);
    fseek(input, 0, SEEK_SET);

    size_t old_size = input_data.size();
    input_data.resize(old_size + size);

    size_t bytes_read = fread(input_data.data() + old_size, 1, size, input);
    fclose(input);

    if (bytes_read != size) {
      fprintf(stderr, "Failed to read %zu bytes from %s\n", size, arg);
      return 1;
    }
  }

  // Ensure that {input_data.data()} is not {nullptr} to avoid having to handle
  // this in specific fuzzers.
  if (input_data.empty()) input_data.reserve(1);

  return LLVMFuzzerTestOneInput(input_data.data(), input_data.size());
}
