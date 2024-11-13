// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/common/globals.h"
#include "src/execution/futex-emulation.h"
#include "src/numbers/conversions-inl.h"
#include "src/objects/js-array-buffer-inl.h"

// Implement Futex API for SharedArrayBuffers as defined in the
// SharedArrayBuffer draft spec, found here:
// https://github.com/tc39/ecmascript_sharedmem

namespace v8 {
namespace internal {

RUNTIME_FUNCTION(Runtime_AtomicsNumWaitersForTesting) {
  HandleScope scope(isolate);
  DCHECK_EQ(2, args.length());
  DirectHandle<JSTypedArray> sta = args.at<JSTypedArray>(0);
  size_t index = NumberToSize(args[1]);
  CHECK(!sta->WasDetached());
  CHECK(sta->GetBuffer()->is_shared());
  CHECK_LT(index, sta->GetLength());
  CHECK_EQ(sta->type(), kExternalInt32Array);

  DirectHandle<JSArrayBuffer> array_buffer = sta->GetBuffer();
  size_t addr = (index << 2) + sta->byte_offset();

  return Smi::FromInt(
      FutexEmulation::NumWaitersForTesting(*array_buffer, addr));
}

RUNTIME_FUNCTION(Runtime_AtomicsNumUnresolvedAsyncPromisesForTesting) {
  HandleScope scope(isolate);
  DCHECK_EQ(2, args.length());
  DirectHandle<JSTypedArray> sta = args.at<JSTypedArray>(0);
  size_t index = NumberToSize(args[1]);
  CHECK(!sta->WasDetached());
  CHECK(sta->GetBuffer()->is_shared());
  CHECK_LT(index, sta->GetLength());
  CHECK_EQ(sta->type(), kExternalInt32Array);

  DirectHandle<JSArrayBuffer> array_buffer = sta->GetBuffer();
  size_t addr = (index << 2) + sta->byte_offset();

  return Smi::FromInt(FutexEmulation::NumUnresolvedAsyncPromisesForTesting(
      *array_buffer, addr));
}

RUNTIME_FUNCTION(Runtime_SetAllowAtomicsWait) {
  HandleScope scope(isolate);
  DCHECK_EQ(1, args.length());
  bool set = Cast<Boolean>(args[0])->ToBool(isolate);

  isolate->set_allow_atomics_wait(set);
  return ReadOnlyRoots(isolate).undefined_value();
}

}  // namespace internal
}  // namespace v8
