// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/maglev/maglev.h"

#include <memory>

#include "src/common/globals.h"
#include "src/logging/runtime-call-stats-scope.h"
#include "src/maglev/maglev-compilation-info.h"
#include "src/maglev/maglev-compiler.h"
#include "src/utils/utils.h"

namespace v8 {
namespace internal {

MaybeHandle<Code> Maglev::Compile(Isolate* isolate, Handle<JSFunction> function,
                                  BytecodeOffset osr_offset) {
  DCHECK(v8_flags.maglev);
  RCS_SCOPE(isolate, RuntimeCallCounterId::kOptimizeNonConcurrentMaglev);
  std::unique_ptr<maglev::MaglevCompilationInfo> info =
      maglev::MaglevCompilationInfo::New(isolate, function, osr_offset);
  if (!maglev::MaglevCompiler::Compile(isolate->main_thread_local_isolate(),
                                       info.get())) {
    return {};
  }
  return maglev::MaglevCompiler::GenerateCode(isolate, info.get());
}

}  // namespace internal
}  // namespace v8
