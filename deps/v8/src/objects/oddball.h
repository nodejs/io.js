// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_OBJECTS_ODDBALL_H_
#define V8_OBJECTS_ODDBALL_H_

#include "src/objects/primitive-heap-object.h"

// Has to be the last include (doesn't have include guards):
#include "src/objects/object-macros.h"

namespace v8 {
namespace internal {

// The Oddball describes objects null, undefined, true, and false.
V8_OBJECT class Oddball : public PrimitiveHeapObject {
 public:
  // [to_number_raw]: Cached raw to_number computed at startup.
  DECL_PRIMITIVE_ACCESSORS(to_number_raw, double)
  inline void set_to_number_raw_as_bits(uint64_t bits);

  // [to_string]: Cached to_string computed at startup.
  inline Tagged<String> to_string() const;
  inline void set_to_string(Tagged<String> value,
                            WriteBarrierMode mode = UPDATE_WRITE_BARRIER);

  // [to_number]: Cached to_number computed at startup.
  inline Tagged<Object> to_number() const;
  inline void set_to_number(Tagged<Object> value,
                            WriteBarrierMode mode = UPDATE_WRITE_BARRIER);

  // [typeof]: Cached type_of computed at startup.
  inline Tagged<String> type_of() const;
  inline void set_type_of(Tagged<String> value,
                          WriteBarrierMode mode = UPDATE_WRITE_BARRIER);

  inline uint8_t kind() const;
  inline void set_kind(uint8_t kind);

  // ES6 section 7.1.3 ToNumber for Boolean, Null, Undefined.
  V8_WARN_UNUSED_RESULT static inline Handle<Object> ToNumber(
      Isolate* isolate, Handle<Oddball> input);

  DECL_CAST(Oddball)

  // Dispatched behavior.
  DECL_VERIFIER(Oddball)

  // Initialize the fields.
  static void Initialize(Isolate* isolate, Handle<Oddball> oddball,
                         const char* to_string, Handle<Object> to_number,
                         const char* type_of, uint8_t kind);

  static constexpr uint8_t kFalse = 0;
  static constexpr uint8_t kTrue = 1;
  static constexpr uint8_t kNotBooleanMask = static_cast<uint8_t>(~1);
  static constexpr uint8_t kNull = 3;
  static constexpr uint8_t kUndefined = 4;

  DECL_PRINTER(Oddball)

 private:
  friend struct ObjectTraits<Oddball>;
  friend struct OffsetsForDebug;
  friend class CodeStubAssembler;
  friend class maglev::MaglevAssembler;
  friend class compiler::AccessBuilder;
  friend class TorqueGeneratedOddballAsserts;

  UnalignedDoubleMember to_number_raw_;
  TaggedMember<String> to_string_;
  TaggedMember<Object> to_number_;
  TaggedMember<String> type_of_;
  TaggedMember<Smi> kind_;
} V8_OBJECT_END;

template <>
struct ObjectTraits<Oddball> {
  using BodyDescriptor =
      FixedBodyDescriptor<offsetof(Oddball, to_number_raw_),
                          offsetof(Oddball, kind_), sizeof(Oddball)>;

  static_assert(offsetof(Oddball, kind_) == Internals::kOddballKindOffset);
  static_assert(Oddball::kNull == Internals::kNullOddballKind);
  static_assert(Oddball::kUndefined == Internals::kUndefinedOddballKind);
};

V8_OBJECT class Null : public Oddball {
 public:
  inline Null();
  DECL_CAST(Null)
} V8_OBJECT_END;

V8_OBJECT class Undefined : public Oddball {
 public:
  inline Undefined();
  DECL_CAST(Undefined)
} V8_OBJECT_END;

V8_OBJECT class Boolean : public Oddball {
 public:
  inline Boolean();
  DECL_CAST(Boolean)

  V8_INLINE bool ToBool(Isolate* isolate) const;
} V8_OBJECT_END;

V8_OBJECT class True : public Boolean {
 public:
  inline True();
  DECL_CAST(True)
} V8_OBJECT_END;

V8_OBJECT class False : public Boolean {
 public:
  inline False();
  DECL_CAST(False)
} V8_OBJECT_END;

}  // namespace internal
}  // namespace v8

#include "src/objects/object-macros-undef.h"

#endif  // V8_OBJECTS_ODDBALL_H_
