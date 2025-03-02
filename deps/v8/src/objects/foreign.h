// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_OBJECTS_FOREIGN_H_
#define V8_OBJECTS_FOREIGN_H_

#include "src/objects/heap-object.h"
#include "src/objects/objects-body-descriptors.h"

// Has to be the last include (doesn't have include guards):
#include "src/objects/object-macros.h"

namespace v8::internal {

#include "torque-generated/src/objects/foreign-tq.inc"

// Foreign describes objects pointing from JavaScript to C structures.
class Foreign : public TorqueGeneratedForeign<Foreign, HeapObject> {
 public:
  // [foreign_address]: field containing the address.
  template <ExternalPointerTag tag>
  inline Address foreign_address(IsolateForSandbox isolate) const;
  // Deprecated. Prefer to use the variant above with an isolate parameter.
  template <ExternalPointerTag tag>
  inline Address foreign_address() const;
  template <ExternalPointerTag tag>
  inline void set_foreign_address(IsolateForSandbox isolate,
                                  const Address value);
  template <ExternalPointerTag tag>
  inline void init_foreign_address(IsolateForSandbox isolate,
                                   const Address initial_value);

  // Load the address without performing a type check. Only use this when the
  // returned pointer will not be dereferenced.
  inline Address foreign_address_unchecked() const;

  // Get the tag of this foreign from the external pointer table. Non-sandbox
  // builds will always return {kAnyExternalPointerTag}.
  inline ExternalPointerTag GetTag() const;

  // Dispatched behavior.
  DECL_PRINTER(Foreign)

#ifdef V8_COMPRESS_POINTERS
  // TODO(ishell, v8:8875): When pointer compression is enabled the
  // kForeignAddressOffset is only kTaggedSize aligned but we can keep using
  // unaligned access since both x64 and arm64 architectures (where pointer
  // compression is supported) allow unaligned access to full words.
  static_assert(IsAligned(kForeignAddressOffset, kTaggedSize));
#else
  static_assert(IsAligned(kForeignAddressOffset, kExternalPointerSlotSize));
#endif

  using BodyDescriptor = StackedBodyDescriptor<
      FixedBodyDescriptorFor<Foreign>,
      WithExternalPointer<kForeignAddressOffset,
                          kAnyForeignExternalPointerTagRange>>;

 private:
  TQ_OBJECT_CONSTRUCTORS(Foreign)
};

// TrustedForeign is similar to Foreign but lives in trusted space.
class TrustedForeign
    : public TorqueGeneratedTrustedForeign<TrustedForeign, TrustedObject> {
 public:
  // Dispatched behavior.
  DECL_PRINTER(TrustedForeign)

  using BodyDescriptor = FixedBodyDescriptorFor<TrustedForeign>;

 private:
  TQ_OBJECT_CONSTRUCTORS(TrustedForeign)
};

}  // namespace v8::internal

#include "src/objects/object-macros-undef.h"

#endif  // V8_OBJECTS_FOREIGN_H_
