// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_REGEXP_REGEXP_PARSER_H_
#define V8_REGEXP_REGEXP_PARSER_H_

#include "src/common/assert-scope.h"
#include "src/handles/handles.h"
#include "src/regexp/regexp-flags.h"

namespace v8 {
namespace internal {

class String;
class Zone;

struct RegExpCompileData;

class V8_EXPORT_PRIVATE RegExpParser : public AllStatic {
 public:
  static bool ParseRegExpFromHeapString(Isolate* isolate, Zone* zone,
                                        DirectHandle<String> input,
                                        RegExpFlags flags,
                                        RegExpCompileData* result);

  template <class CharT>
  static bool VerifyRegExpSyntax(Zone* zone, uintptr_t stack_limit,
                                 const CharT* input, int input_length,
                                 RegExpFlags flags, RegExpCompileData* result,
                                 const DisallowGarbageCollection& no_gc);
};

}  // namespace internal
}  // namespace v8

#endif  // V8_REGEXP_REGEXP_PARSER_H_
