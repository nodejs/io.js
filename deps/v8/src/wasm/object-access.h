// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#if !V8_ENABLE_WEBASSEMBLY
#error This header should only be included if WebAssembly is enabled.
#endif  // !V8_ENABLE_WEBASSEMBLY

#ifndef V8_WASM_OBJECT_ACCESS_H_
#define V8_WASM_OBJECT_ACCESS_H_

#include "src/common/globals.h"
#include "src/objects/fixed-array.h"
#include "src/objects/js-function.h"
#include "src/objects/shared-function-info.h"

namespace v8 {
namespace internal {
namespace wasm {

class ObjectAccess : public AllStatic {
 public:
  // Convert an offset into an object to an offset into a tagged object.
  static constexpr int ToTagged(int offset) { return offset - kHeapObjectTag; }

  // Get the offset into a fixed array for a given {index}.
  static constexpr int ElementOffsetInTaggedFixedArray(int index) {
    return ToTagged(FixedArray::OffsetOfElementAt(index));
  }

  // Get the offset into a fixed uint8 array for a given {index}.
  static constexpr int ElementOffsetInTaggedFixedUInt8Array(int index) {
    return ToTagged(FixedUInt8Array::OffsetOfElementAt(index));
  }

  // Get the offset into a fixed uint32 array for a given {index}.
  static constexpr int ElementOffsetInTaggedFixedUInt32Array(int index) {
    return ToTagged(FixedUInt32Array::OffsetOfElementAt(index));
  }

  // Get the offset into a fixed address array for a given {index}.
  static constexpr int ElementOffsetInTaggedFixedAddressArray(int index) {
    return ToTagged(FixedAddressArray::OffsetOfElementAt(index));
  }

  // Get the offset into a trusted fixed address array for a given {index}.
  static constexpr int ElementOffsetInTaggedTrustedFixedAddressArray(
      int index) {
    return ToTagged(TrustedFixedAddressArray::OffsetOfElementAt(index));
  }

  // Get the offset into a external pointer array for a given {index}.
  static constexpr int ElementOffsetInTaggedExternalPointerArray(int index) {
    return ToTagged(ExternalPointerArray::OffsetOfElementAt(index));
  }

  // Get the offset into a ProtectedFixedArray for a given {index}.
  static constexpr int ElementOffsetInProtectedFixedArray(int index) {
    return ToTagged(ProtectedFixedArray::OffsetOfElementAt(index));
  }

  // Get the offset of the context stored in a {JSFunction} object.
  static constexpr int ContextOffsetInTaggedJSFunction() {
    return ToTagged(JSFunction::kContextOffset);
  }

  // Get the offset of the shared function info in a {JSFunction} object.
  static constexpr int SharedFunctionInfoOffsetInTaggedJSFunction() {
    return ToTagged(JSFunction::kSharedFunctionInfoOffset);
  }

  // Get the offset of the formal parameter count in a {SharedFunctionInfo}
  // object.
  static constexpr int FormalParameterCountOffsetInSharedFunctionInfo() {
    return ToTagged(SharedFunctionInfo::kFormalParameterCountOffset);
  }

  // Get the offset of the flags in a {SharedFunctionInfo} object.
  static constexpr int FlagsOffsetInSharedFunctionInfo() {
    return ToTagged(SharedFunctionInfo::kFlagsOffset);
  }
};

}  // namespace wasm
}  // namespace internal
}  // namespace v8

#endif  // V8_WASM_OBJECT_ACCESS_H_
