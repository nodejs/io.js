// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_OBJECTS_ARGUMENTS_H_
#define V8_OBJECTS_ARGUMENTS_H_

#include "src/objects/fixed-array.h"
#include "src/objects/hole.h"
#include "src/objects/js-objects.h"
#include "src/objects/struct.h"

// Has to be the last include (doesn't have include guards):
#include "src/objects/object-macros.h"

namespace v8 {
namespace internal {

class StructBodyDescriptor;

#include "torque-generated/src/objects/arguments-tq.inc"

// Superclass for all objects with instance type {JS_ARGUMENTS_OBJECT_TYPE}
class JSArgumentsObject
    : public TorqueGeneratedJSArgumentsObject<JSArgumentsObject, JSObject> {
 public:
  DECL_VERIFIER(JSArgumentsObject)
  DECL_PRINTER(JSArgumentsObject)
  TQ_OBJECT_CONSTRUCTORS(JSArgumentsObject)
};

// JSSloppyArgumentsObject is just a JSArgumentsObject with specific initial
// map. This initial map adds in-object properties for "length" and "callee".
class JSSloppyArgumentsObject
    : public TorqueGeneratedJSSloppyArgumentsObject<JSSloppyArgumentsObject,
                                                    JSArgumentsObject> {
 public:
  // Indices of in-object properties.
  static const int kLengthIndex = 0;
  static const int kCalleeIndex = kLengthIndex + 1;

 private:
  DISALLOW_IMPLICIT_CONSTRUCTORS(JSSloppyArgumentsObject);
};

// JSStrictArgumentsObject is just a JSArgumentsObject with specific initial
// map. This initial map adds an in-object property for "length".
class JSStrictArgumentsObject
    : public TorqueGeneratedJSStrictArgumentsObject<JSStrictArgumentsObject,
                                                    JSArgumentsObject> {
 public:
  // Indices of in-object properties.
  static const int kLengthIndex = 0;
  static_assert(kLengthIndex == JSSloppyArgumentsObject::kLengthIndex);

 private:
  DISALLOW_IMPLICIT_CONSTRUCTORS(JSStrictArgumentsObject);
};

// Representation of a slow alias as part of a sloppy arguments objects.
// For fast aliases (if HasSloppyArgumentsElements()):
// - the parameter map contains an index into the context
// - all attributes of the element have default values
// For slow aliases (if HasDictionaryArgumentsElements()):
// - the parameter map contains no fast alias mapping (i.e. the hole)
// - this struct (in the slow backing store) contains an index into the context
// - all attributes are available as part if the property details
class AliasedArgumentsEntry
    : public TorqueGeneratedAliasedArgumentsEntry<AliasedArgumentsEntry,
                                                  Struct> {
 public:
  using BodyDescriptor = StructBodyDescriptor;

  TQ_OBJECT_CONSTRUCTORS(AliasedArgumentsEntry)
};

class SloppyArgumentsElementsShape final : public AllStatic {
 public:
  using ElementT = UnionOf<Smi, Hole>;
  using CompressionScheme = V8HeapCompressionScheme;
  static constexpr RootIndex kMapRootIndex =
      RootIndex::kSloppyArgumentsElementsMap;
  static constexpr bool kLengthEqualsCapacity = true;

  V8_ARRAY_EXTRA_FIELDS({
    TaggedMember<Context> context_;
    TaggedMember<UnionOf<FixedArray, NumberDictionary>> arguments_;
  });
};

// Helper class to access FAST_ and SLOW_SLOPPY_ARGUMENTS_ELEMENTS, dividing
// arguments into two types for a given SloppyArgumentsElements object:
// mapped and unmapped.
//
// For clarity SloppyArgumentsElements fields are qualified with "elements."
// below.
//
// Mapped arguments are actual arguments. Unmapped arguments are values added
// to the arguments object after it was created for the call. Mapped arguments
// are stored in the context at indexes given by elements.mapped_entries[key].
// Unmapped arguments are stored as regular indexed properties in the arguments
// array which can be accessed from elements.arguments.
//
// elements.length is min(number_of_actual_arguments,
// number_of_formal_arguments) for a concrete call to a function.
//
// Once a SloppyArgumentsElements is generated, lookup of an argument with index
// |key| in |elements| works as follows:
//
// If key >= elements.length then attempt to look in the unmapped arguments
// array and return the value at key, missing to the runtime if the unmapped
// arguments array is not a fixed array or if key >= elements.arguments.length.
//
// Otherwise, t = elements.mapped_entries[key]. If t is the hole, then the
// entry has been deleted from the arguments object, and value is looked up in
// the unmapped arguments array, as described above. Otherwise, t is a Smi
// index into the context array specified at elements.context, and the return
// value is elements.context[t].
//
// A graphic representation of a SloppyArgumentsElements object and a
// corresponding unmapped arguments FixedArray:
//
// SloppyArgumentsElements
// +---+-----------------------+
// | Context context           |
// +---------------------------+
// | FixedArray arguments      +----+ HOLEY_ELEMENTS
// +---------------------------+    v-----+-----------+
// | 0 | Object mapped_entries |    |  0  | the_hole  |
// |...| ...                   |    | ... | ...       |
// |n-1| Object mapped_entries |    | n-1 | the_hole  |
// +---------------------------+    |  n  | element_1 |
//                                  | ... | ...       |
//                                  |n+m-1| element_m |
//                                  +-----------------+
//
// The elements.arguments backing store kind depends on the ElementsKind of
// the outer JSArgumentsObject:
// - FAST_SLOPPY_ARGUMENTS_ELEMENTS: HOLEY_ELEMENTS
// - SLOW_SLOPPY_ARGUMENTS_ELEMENTS: DICTIONARY_ELEMENTS
class SloppyArgumentsElements
    : public TaggedArrayBase<SloppyArgumentsElements,
                             SloppyArgumentsElementsShape> {
 public:
  inline Tagged<Context> context() const;
  inline void set_context(Tagged<Context> value,
                          WriteBarrierMode mode = UPDATE_WRITE_BARRIER);

  inline Tagged<UnionOf<FixedArray, NumberDictionary>> arguments() const;
  inline void set_arguments(Tagged<UnionOf<FixedArray, NumberDictionary>> value,
                            WriteBarrierMode mode = UPDATE_WRITE_BARRIER);

  // Returns: Smi|TheHole.
  inline Tagged<UnionOf<Smi, Hole>> mapped_entries(int index,
                                                   RelaxedLoadTag) const;
  inline void set_mapped_entries(int index, Tagged<UnionOf<Smi, Hole>> value);
  inline void set_mapped_entries(int index, Tagged<UnionOf<Smi, Hole>> value,
                                 RelaxedStoreTag);

  DECL_PRINTER(SloppyArgumentsElements)
  DECL_VERIFIER(SloppyArgumentsElements)

  class BodyDescriptor;
};

}  // namespace internal
}  // namespace v8

#include "src/objects/object-macros-undef.h"

#endif  // V8_OBJECTS_ARGUMENTS_H_
