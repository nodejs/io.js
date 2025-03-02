// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/builtins/builtins-utils.h"
#include "src/builtins/builtins.h"
#include "src/codegen/interface-descriptors.h"
#include "src/logging/counters.h"
#include "src/objects/objects-inl.h"

namespace v8 {
namespace internal {

BUILTIN(Illegal) {
  UNREACHABLE();
}

// TODO(ishell): remove this unused builtin.
BUILTIN(DummyBuiltin) { UNREACHABLE(); }

BUILTIN(IllegalInvocationThrower) {
  HandleScope scope(isolate);
  THROW_NEW_ERROR_RETURN_FAILURE(
      isolate, NewTypeError(MessageTemplate::kIllegalInvocation));
}

BUILTIN(EmptyFunction) { return ReadOnlyRoots(isolate).undefined_value(); }

// TODO(366374966): remove this second version of EmptyFunction once the
// CPP macro becomes the source of truth for the builtin's formal parameter
// count.
BUILTIN(EmptyFunction1) { return ReadOnlyRoots(isolate).undefined_value(); }

BUILTIN(UnsupportedThrower) {
  HandleScope scope(isolate);
  THROW_NEW_ERROR_RETURN_FAILURE(isolate,
                                 NewError(MessageTemplate::kUnsupported));
}

BUILTIN(StrictPoisonPillThrower) {
  HandleScope scope(isolate);
  THROW_NEW_ERROR_RETURN_FAILURE(
      isolate, NewTypeError(MessageTemplate::kStrictPoisonPill));
}

}  // namespace internal
}  // namespace v8
