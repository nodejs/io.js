// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/wasm/signature-hashing.h"

#include "test/unittests/test-utils.h"

namespace v8::internal::wasm::signature_hashing_unittest {

#if V8_ENABLE_SANDBOX

class WasmSignatureHashingTest : public TestWithPlatform {
 public:
  uint64_t H(std::initializer_list<ValueType> params,
             std::initializer_list<ValueType> returns) {
    const FunctionSig* sig = FunctionSig::Build(&zone_, returns, params);
    return SignatureHasher::Hash(sig);
  }

 private:
  AccountingAllocator allocator_;
  Zone zone_{&allocator_, "WasmSignatureHashingTestZone"};
};

TEST_F(WasmSignatureHashingTest, SignatureHashing) {
  ValueType i = kWasmI32;
  ValueType l = kWasmI64;
  ValueType d = kWasmF64;
  ValueType s = kWasmS128;
  ValueType r = kWasmExternRef;
  USE(l);

  std::vector<uint64_t> distinct_hashes{
      // Some simple signatures.
      H({}, {}),   // --
      H({i}, {}),  // --
      H({r}, {}),  // --
      H({}, {i}),  // --
      H({}, {r}),  // --

      // These two have the same number of parameters, but need different
      // numbers of stack slots for them. Assume that no more than 8
      // untagged params can be passed in registers; the 9th must be on the
      // stack.
      H({d, d, d, d, d, d, d, d, d}, {}),  // --
      H({d, d, d, d, d, d, d, d, s}, {}),  // --

#if V8_TARGET_ARCH_32_BIT
      // Same, but only relevant for 32-bit platforms.
      H({i, i, i, i, i, i, i, i, i}, {}),  // --
      H({i, i, i, i, i, i, i, i, l}, {}),
#endif  // V8_TARGET_ARCH_32_BIT

      // Same, but for returns. We only use 2 return registers.
      H({}, {d, d, d, d}),  // --
      H({}, {d, d, s, d}),  // --

      // These two have the same number of stack parameters, but some are
      // tagged.
      H({i, i, i, i, i, i, i, i, i, i}, {}),  // --
      H({i, i, i, i, i, i, i, i, i, r}, {}),  // --
  };

  for (size_t j = 0; j < distinct_hashes.size(); j++) {
    for (size_t k = j + 1; k < distinct_hashes.size(); k++) {
      uint64_t hash_j = distinct_hashes[j];
      uint64_t hash_k = distinct_hashes[k];
      if (hash_j == hash_k) {
        PrintF("Hash collision for signatures %zu and %zu\n", j, k);
      }
      EXPECT_NE(hash_j, hash_k);
    }
  }
}

#endif  // V8_ENABLE_SANDBOX

}  // namespace v8::internal::wasm::signature_hashing_unittest
