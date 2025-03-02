// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#if !V8_ENABLE_WEBASSEMBLY
#error This header should only be included if WebAssembly is enabled.
#endif  // !V8_ENABLE_WEBASSEMBLY

#ifndef V8_WASM_WASM_ARGUMENTS_H_
#define V8_WASM_WASM_ARGUMENTS_H_

#include <stdint.h>
#include <vector>

#include "src/base/memory.h"
#include "src/codegen/signature.h"
#include "src/common/globals.h"
#include "src/wasm/value-type.h"

namespace v8 {
namespace internal {
namespace wasm {

// Helper class for {Push}ing Wasm value arguments onto the stack in the format
// that the CWasmEntryStub expects, as well as for {Pop}ping return values.
// {Reset} must be called if a packer instance used for pushing is then
// reused for popping: it resets the internal pointer to the beginning of
// the stack region.
class CWasmArgumentsPacker {
 public:
  explicit CWasmArgumentsPacker(size_t buffer_size)
      : heap_buffer_(buffer_size <= kMaxOnStackBuffer ? 0 : buffer_size),
        buffer_((buffer_size <= kMaxOnStackBuffer) ? on_stack_buffer_
                                                   : heap_buffer_.data()) {}
  i::Address argv() const { return reinterpret_cast<i::Address>(buffer_); }
  void Reset() { offset_ = 0; }

  template <typename T>
  void Push(T val) {
    Address address = reinterpret_cast<Address>(buffer_ + offset_);
    offset_ += sizeof(val);
    base::WriteUnalignedValue(address, val);
  }

  template <typename T>
  T Pop() {
    Address address = reinterpret_cast<Address>(buffer_ + offset_);
    offset_ += sizeof(T);
    return base::ReadUnalignedValue<T>(address);
  }

  static int TotalSize(const CanonicalSig* sig) {
    int return_size = 0;
    for (CanonicalValueType t : sig->returns()) {
      return_size += t.value_kind_full_size();
    }
    int param_size = 0;
    for (CanonicalValueType t : sig->parameters()) {
      param_size += t.value_kind_full_size();
    }
    return std::max(return_size, param_size);
  }

 private:
  static const size_t kMaxOnStackBuffer = 10 * i::kSystemPointerSize;

  uint8_t on_stack_buffer_[kMaxOnStackBuffer];
  std::vector<uint8_t> heap_buffer_;
  uint8_t* buffer_;
  size_t offset_ = 0;
};

}  // namespace wasm
}  // namespace internal
}  // namespace v8

#endif  // V8_WASM_WASM_ARGUMENTS_H_
