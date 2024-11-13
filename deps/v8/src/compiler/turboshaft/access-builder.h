// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_COMPILER_TURBOSHAFT_ACCESS_BUILDER_H_
#define V8_COMPILER_TURBOSHAFT_ACCESS_BUILDER_H_

#include "src/base/compiler-specific.h"
#include "src/common/globals.h"
#include "src/compiler/access-builder.h"
#include "src/compiler/turboshaft/index.h"

namespace v8::internal::compiler::turboshaft {

class AccessBuilderTS;

// TODO(nicohartmann): Rename this to `FieldAccess` and rely on proper
// namespaces.
template <typename Class, typename T>
struct FieldAccessTS : public compiler::FieldAccess {
  using type = T;

 private:
  friend class AccessBuilderTS;
  explicit FieldAccessTS(const compiler::FieldAccess& base)
      : compiler::FieldAccess(base) {}
};

// TODO(nicohartmann): Rename this to `ElementAccess` and rely on proper
// namespaces.
template <typename Class, typename T>
struct ElementAccessTS : public compiler::ElementAccess {
  using type = T;

  const bool is_array_buffer_load;

 private:
  friend class AccessBuilderTS;
  explicit ElementAccessTS(const compiler::ElementAccess& base,
                           bool is_array_buffer_load)
      : compiler::ElementAccess(base),
        is_array_buffer_load(is_array_buffer_load) {}
};

// TODO(nicohartmann): Rename this to `AccessBuilder` and rely on proper
// namespaces.
class AccessBuilderTS : public AllStatic {
 public:
  template <typename Class>
  static constexpr bool is_array_buffer_v = std::is_same_v<Class, ArrayBuffer>;

#define TF_FIELD_ACCESS(Class, T, name)                              \
  static FieldAccessTS<Class, T> name() {                            \
    return FieldAccessTS<Class, T>(compiler::AccessBuilder::name()); \
  }
  TF_FIELD_ACCESS(String, Word32, ForStringLength)
  TF_FIELD_ACCESS(Name, Word32, ForNameRawHashField)
  TF_FIELD_ACCESS(HeapNumber, Float64, ForHeapNumberValue)
  using HeapNumberOrOddballOrHole = Union<HeapNumber, Oddball, Hole>;
  TF_FIELD_ACCESS(HeapNumberOrOddballOrHole, Float64,
                  ForHeapNumberOrOddballOrHoleValue)
#undef TF_ACCESS
  static FieldAccessTS<Object, Map> ForMap(
      WriteBarrierKind write_barrier = kMapWriteBarrier) {
    return FieldAccessTS<Object, Map>(
        compiler::AccessBuilder::ForMap(write_barrier));
  }

#define TF_ELEMENT_ACCESS(Class, T, name)                                     \
  static ElementAccessTS<Class, T> name() {                                   \
    return ElementAccessTS<Class, T>{compiler::AccessBuilder::name(), false}; \
  }
  TF_ELEMENT_ACCESS(SeqOneByteString, Word32, ForSeqOneByteStringCharacter)
  TF_ELEMENT_ACCESS(SeqTwoByteString, Word32, ForSeqTwoByteStringCharacter)
#undef TF_ELEMENT_ACCESS

  template <CONCEPT(IsTagged) T>
  static ElementAccessTS<FixedArray, T> ForFixedArrayElement() {
    static_assert(!is_array_buffer_v<FixedArray>);
    return ElementAccessTS<FixedArray, T>{
        compiler::AccessBuilder::ForFixedArrayElement(), false};
  }
};

}  // namespace v8::internal::compiler::turboshaft

#endif  // V8_COMPILER_TURBOSHAFT_ACCESS_BUILDER_H_
