// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/fuzzilli/fuzzilli.h"

#include "include/v8-extension.h"
#include "include/v8-primitive.h"
#include "include/v8-template.h"
#include "src/api/api.h"
#include "src/execution/isolate-inl.h"
#include "src/execution/isolate.h"
#include "src/fuzzilli/cov.h"
#include "src/sandbox/sandbox.h"
#include "src/sandbox/testing.h"

#ifdef V8_OS_LINUX
#include <signal.h>
#include <unistd.h>
#endif  // V8_OS_LINUX

namespace v8 {
namespace internal {

v8::Local<v8::FunctionTemplate> FuzzilliExtension::GetNativeFunctionTemplate(
    v8::Isolate* isolate, v8::Local<v8::String> str) {
  return v8::FunctionTemplate::New(isolate, FuzzilliExtension::Fuzzilli);
}

// We have to assume that the fuzzer will be able to call this function e.g. by
// enumerating the properties of the global object and eval'ing them. As such
// this function is implemented in a way that requires passing some magic value
// as first argument (with the idea being that the fuzzer won't be able to
// generate this value) which then also acts as a selector for the operation
// to perform.
void FuzzilliExtension::Fuzzilli(const FunctionCallbackInfo<Value>& info) {
  v8::Isolate* isolate = info.GetIsolate();

  v8::String::Utf8Value operation(isolate, info[0]);
  if (*operation == nullptr) {
    return;
  }

  if (strcmp(*operation, "FUZZILLI_CRASH") == 0) {
    auto arg = info[1]
                   ->Int32Value(info.GetIsolate()->GetCurrentContext())
                   .FromMaybe(0);
    switch (arg) {
      case 0:
        IMMEDIATE_CRASH();
        break;
      case 1:
        CHECK(false);
        break;
      case 2:
        DCHECK(false);
        break;
      case 3: {
        // Access an invalid address.
        // We want to use an "interesting" address for the access (instead of
        // e.g. nullptr). In the (unlikely) case that the address is actually
        // mapped, simply increment the pointer until it crashes.
        // The cast ensures that this works correctly on both 32-bit and 64-bit.
        Address addr = static_cast<Address>(0x414141414141ull);
        char* ptr = reinterpret_cast<char*>(addr);
        for (int i = 0; i < 1024; i++) {
          *ptr = 'A';
          ptr += 1 * i::MB;
        }
        break;
      }
      case 4: {
        // Use-after-free, likely only crashes in ASan builds.
        auto* vec = new std::vector<int>(4);
        delete vec;
        USE(vec->at(0));
        break;
      }
      case 5: {
        // Out-of-bounds access (1), likely only crashes in ASan or
        // "hardened"/"safe" libc++ builds.
        std::vector<int> vec(5);
        USE(vec[5]);
        break;
      }
      case 6: {
        // Out-of-bounds access (2), likely only crashes in ASan builds.
        std::vector<int> vec(6);
        memset(vec.data(), 42, 0x100);
        break;
      }
      case 7: {
        if (i::v8_flags.hole_fuzzing) {
          // This should crash with a segmentation fault only
          // when --hole-fuzzing is used.
          char* ptr = reinterpret_cast<char*>(0x414141414141ull);
          for (int i = 0; i < 1024; i++) {
            *ptr = 'A';
            ptr += 1 * i::GB;
          }
        }
        break;
      }
      case 8: {
        // This allows Fuzzilli to check that DEBUG is defined, which should be
        // the case if dcheck_always_on is set. This is useful for fuzzing as
        // there are some integrity checks behind DEBUG.
#ifdef DEBUG
        IMMEDIATE_CRASH();
#endif
        break;
      }
      default:
        break;
    }
  } else if (strcmp(*operation, "FUZZILLI_PRINT") == 0) {
    static FILE* fzliout = fdopen(REPRL_DWFD, "w");
    if (!fzliout) {
      fprintf(
          stderr,
          "Fuzzer output channel not available, printing to stdout instead\n");
      fzliout = stdout;
    }

    v8::String::Utf8Value string(isolate, info[1]);
    if (*string == nullptr) {
      return;
    }
    fprintf(fzliout, "%s\n", *string);
    fflush(fzliout);
  }
}

}  // namespace internal
}  // namespace v8
