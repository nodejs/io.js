// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_REGEXP_EXPERIMENTAL_EXPERIMENTAL_INTERPRETER_H_
#define V8_REGEXP_EXPERIMENTAL_EXPERIMENTAL_INTERPRETER_H_

#include "src/regexp/experimental/experimental-bytecode.h"
#include "src/regexp/regexp.h"

namespace v8 {
namespace internal {

class TrustedByteArray;
class String;
class Zone;

class ExperimentalRegExpInterpreter final : public AllStatic {
 public:
  // Executes a bytecode program in breadth-first NFA mode, without
  // backtracking, to find matching substrings.  Tries to find up to
  // `max_match_num` matches in `input`, starting at `start_index`.  Returns
  // the actual number of matches found.  The boundaries of matching subranges
  // are written to `matches_out`.  Provided in variants for one-byte and
  // two-byte strings.
  static int FindMatches(Isolate* isolate, RegExp::CallOrigin call_origin,
                         Tagged<TrustedByteArray> bytecode, int capture_count,
                         Tagged<String> input, int start_index,
                         int32_t* output_registers, int output_register_count,
                         Zone* zone);
};

}  // namespace internal
}  // namespace v8

#endif  // V8_REGEXP_EXPERIMENTAL_EXPERIMENTAL_INTERPRETER_H_
