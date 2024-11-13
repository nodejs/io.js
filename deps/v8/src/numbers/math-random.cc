// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/numbers/math-random.h"

#include "src/base/utils/random-number-generator.h"
#include "src/common/assert-scope.h"
#include "src/execution/isolate.h"
#include "src/objects/contexts-inl.h"
#include "src/objects/fixed-array.h"
#include "src/objects/smi.h"

namespace v8 {
namespace internal {

void MathRandom::InitializeContext(Isolate* isolate,
                                   DirectHandle<Context> native_context) {
  auto cache = Cast<FixedDoubleArray>(
      isolate->factory()->NewFixedDoubleArray(kCacheSize));
  for (int i = 0; i < kCacheSize; i++) cache->set(i, 0);
  native_context->set_math_random_cache(*cache);
  DirectHandle<PodArray<State>> pod =
      PodArray<State>::New(isolate, 1, AllocationType::kOld);
  native_context->set_math_random_state(*pod);
  ResetContext(*native_context);
}

void MathRandom::ResetContext(Tagged<Context> native_context) {
  native_context->set_math_random_index(Smi::zero());
  State state = {0, 0};
  Cast<PodArray<State>>(native_context->math_random_state())->set(0, state);
}

Address MathRandom::RefillCache(Isolate* isolate, Address raw_native_context) {
  Tagged<Context> native_context =
      Cast<Context>(Tagged<Object>(raw_native_context));
  DisallowGarbageCollection no_gc;
  Tagged<PodArray<State>> pod =
      Cast<PodArray<State>>(native_context->math_random_state());
  State state = pod->get(0);
  // Initialize state if not yet initialized. If a fixed random seed was
  // requested, use it to reset our state the first time a script asks for
  // random numbers in this context. This ensures the script sees a consistent
  // sequence.
  if (state.s0 == 0 && state.s1 == 0) {
    uint64_t seed;
    if (v8_flags.random_seed != 0) {
      seed = v8_flags.random_seed;
    } else {
      isolate->random_number_generator()->NextBytes(&seed, sizeof(seed));
    }
    state.s0 = base::RandomNumberGenerator::MurmurHash3(seed);
    state.s1 = base::RandomNumberGenerator::MurmurHash3(~seed);
    CHECK(state.s0 != 0 || state.s1 != 0);
  }

  Tagged<FixedDoubleArray> cache =
      Cast<FixedDoubleArray>(native_context->math_random_cache());
  // Create random numbers.
  for (int i = 0; i < kCacheSize; i++) {
    // Generate random numbers using xorshift128+.
    base::RandomNumberGenerator::XorShift128(&state.s0, &state.s1);
    cache->set(i, base::RandomNumberGenerator::ToDouble(state.s0));
  }
  pod->set(0, state);

  Tagged<Smi> new_index = Smi::FromInt(kCacheSize);
  native_context->set_math_random_index(new_index);
  return new_index.ptr();
}

}  // namespace internal
}  // namespace v8
