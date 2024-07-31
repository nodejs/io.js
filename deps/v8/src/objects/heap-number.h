// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_OBJECTS_HEAP_NUMBER_H_
#define V8_OBJECTS_HEAP_NUMBER_H_

#include "src/objects/primitive-heap-object.h"
#include "src/objects/tagged-field.h"

// Has to be the last include (doesn't have include guards):
#include "src/objects/object-macros.h"

namespace v8 {
namespace internal {

namespace maglev {
class MaglevGraphBuilder;
class StoreDoubleField;
}  // namespace maglev

namespace compiler {
class GraphAssembler;
}  // namespace compiler

// The HeapNumber class describes heap allocated numbers that cannot be
// represented in a Smi (small integer).
V8_OBJECT class HeapNumber : public PrimitiveHeapObject {
 public:
  inline double value() const;
  inline void set_value(double value);

  inline uint64_t value_as_bits() const;
  inline void set_value_as_bits(uint64_t bits);

  static const uint32_t kSignMask = 0x80000000u;
  static const uint32_t kExponentMask = 0x7ff00000u;
  static const uint32_t kMantissaMask = 0xfffffu;
  static const int kMantissaBits = 52;
  static const int kExponentBits = 11;
  static const int kExponentBias = 1023;
  static const int kExponentShift = 20;
  static const int kInfinityOrNanExponent =
      (kExponentMask >> kExponentShift) - kExponentBias;
  static const int kMantissaBitsInTopWord = 20;
  static const int kNonMantissaBitsInTopWord = 12;

  DECL_CAST(HeapNumber)
  DECL_PRINTER(HeapNumber)
  DECL_VERIFIER(HeapNumber)
  V8_EXPORT_PRIVATE void HeapNumberShortPrint(std::ostream& os);

  class BodyDescriptor;

 private:
  friend struct OffsetsForDebug;
  friend class CodeStubAssembler;
  friend class AccessorAssembler;
  friend class maglev::MaglevAssembler;
  friend class maglev::MaglevGraphBuilder;
  friend class maglev::StoreDoubleField;
  friend class compiler::AccessBuilder;
  friend class compiler::GraphAssembler;
  friend class TorqueGeneratedHeapNumberAsserts;
  friend AllocationAlignment HeapObject::RequiredAlignment(Tagged<Map> map);

  UnalignedDoubleMember value_;
} V8_OBJECT_END;

}  // namespace internal
}  // namespace v8

#include "src/objects/object-macros-undef.h"

#endif  // V8_OBJECTS_HEAP_NUMBER_H_
