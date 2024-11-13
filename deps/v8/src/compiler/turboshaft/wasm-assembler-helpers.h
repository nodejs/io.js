// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#if !V8_ENABLE_WEBASSEMBLY
#error This header should only be included if WebAssembly is enabled.
#endif  // !V8_ENABLE_WEBASSEMBLY

#ifndef V8_COMPILER_TURBOSHAFT_WASM_ASSEMBLER_HELPERS_H_
#define V8_COMPILER_TURBOSHAFT_WASM_ASSEMBLER_HELPERS_H_

#include "src/compiler/turboshaft/operations.h"
#include "src/roots/roots.h"

namespace v8::internal::compiler::turboshaft {

struct RootTypes {
#define DEFINE_TYPE(type, name, CamelName) using k##CamelName##Type = type;
  ROOT_LIST(DEFINE_TYPE)
#undef DEFINE_TYPE
};

template <typename AssemblerT>
OpIndex LoadRootHelper(AssemblerT&& assembler, RootIndex index) {
  if (RootsTable::IsImmortalImmovable(index)) {
    // Note that we skip the bit cast here as the value does not need to be
    // tagged as the object will never be collected / moved.
    return assembler.Load(
        assembler.LoadRootRegister(), LoadOp::Kind::RawAligned().Immutable(),
        MemoryRepresentation::UintPtr(), IsolateData::root_slot_offset(index));
  } else {
    return assembler.BitcastWordPtrToTagged(assembler.Load(
        assembler.LoadRootRegister(), LoadOp::Kind::RawAligned(),
        MemoryRepresentation::UintPtr(), IsolateData::root_slot_offset(index)));
  }
}

#define LOAD_INSTANCE_FIELD(instance, name, representation)     \
  __ Load(instance, LoadOp::Kind::TaggedBase(), representation, \
          WasmTrustedInstanceData::k##name##Offset)

#define LOAD_PROTECTED_INSTANCE_FIELD(instance, name, type) \
  V<type>::Cast(__ LoadProtectedPointerField(               \
      instance, LoadOp::Kind::TaggedBase(),                 \
      WasmTrustedInstanceData::kProtected##name##Offset))

#define LOAD_IMMUTABLE_PROTECTED_INSTANCE_FIELD(instance, name, type) \
  V<type>::Cast(__ LoadProtectedPointerField(                         \
      instance, LoadOp::Kind::TaggedBase().Immutable(),               \
      WasmTrustedInstanceData::kProtected##name##Offset))

#define LOAD_IMMUTABLE_INSTANCE_FIELD(instance, name, representation)       \
  __ Load(instance, LoadOp::Kind::TaggedBase().Immutable(), representation, \
          WasmTrustedInstanceData::k##name##Offset)

#define LOAD_ROOT(name)                                    \
  V<compiler::turboshaft::RootTypes::k##name##Type>::Cast( \
      LoadRootHelper(Asm(), RootIndex::k##name))

}  // namespace v8::internal::compiler::turboshaft

#endif  // V8_COMPILER_TURBOSHAFT_WASM_ASSEMBLER_HELPERS_H_
