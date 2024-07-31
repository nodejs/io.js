// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_OBJECTS_BYTECODE_ARRAY_H_
#define V8_OBJECTS_BYTECODE_ARRAY_H_

#include "src/objects/struct.h"
#include "src/objects/trusted-object.h"

// Has to be the last include (doesn't have include guards):
#include "src/objects/object-macros.h"

namespace v8 {
namespace internal {

class BytecodeWrapper;

namespace interpreter {
class Register;
}  // namespace interpreter

// TODO(jgruber): These should no longer be included here; instead, all
// TorqueGeneratedFooAsserts should be emitted into a global .cc file.
#include "torque-generated/src/objects/bytecode-array-tq.inc"

// BytecodeArray represents a sequence of interpreter bytecodes.
class BytecodeArray : public ExposedTrustedObject {
 public:
  // The length of this bytecode array, in bytes.
  inline int length() const;
  inline int length(AcquireLoadTag tag) const;
  inline void set_length(int value);
  inline void set_length(int value, ReleaseStoreTag tag);

  // The handler table contains offsets of exception handlers.
  DECL_PROTECTED_POINTER_ACCESSORS(handler_table, TrustedByteArray)

  DECL_PROTECTED_POINTER_ACCESSORS(constant_pool, TrustedFixedArray)

  // The BytecodeWrapper for this BytecodeArray. When the sandbox is enabled,
  // the BytecodeArray lives in trusted space outside of the sandbox, but the
  // wrapper object lives inside the main heap and therefore inside the
  // sandbox. As such, the wrapper object can be used in cases where a
  // BytecodeArray needs to be referenced alongside other tagged pointer
  // references (so for example inside a FixedArray).
  DECL_ACCESSORS(wrapper, Tagged<BytecodeWrapper>)

  // Source position table. Can contain:
  // * Smi::zero() (initial value, or if an error occurred while explicitly
  // collecting source positions for pre-existing bytecode).
  // * empty_trusted_byte_array (for bytecode generated for functions that will
  // never have source positions, e.g. native functions).
  // * TrustedByteArray (if source positions were collected for the bytecode)
  DECL_RELEASE_ACQUIRE_PROTECTED_POINTER_ACCESSORS(source_position_table,
                                                   TrustedByteArray)

  DECL_INT32_ACCESSORS(frame_size)

  static constexpr int SizeFor(int length) {
    return OBJECT_POINTER_ALIGN(kHeaderSize + length);
  }

  inline uint8_t get(int index) const;
  inline void set(int index, uint8_t value);

  inline Address GetFirstBytecodeAddress();

  // Note: The register count is derived from frame_size.
  inline int register_count() const;

  // Note: the parameter count includes the implicit 'this' receiver.
  inline int32_t parameter_count() const;
  inline void set_parameter_count(int32_t number_of_parameters);

  inline interpreter::Register incoming_new_target_or_generator_register()
      const;
  inline void set_incoming_new_target_or_generator_register(
      interpreter::Register incoming_new_target_or_generator_register);

  inline bool HasSourcePositionTable() const;

  // If source positions have not been collected or an exception has been thrown
  // this will return the empty_trusted_byte_array.
  DECL_GETTER(SourcePositionTable, Tagged<TrustedByteArray>)

  // Raw accessors to access these fields during code cache deserialization.
  DECL_GETTER(raw_constant_pool, Tagged<Object>)
  DECL_GETTER(raw_handler_table, Tagged<Object>)
  // This accessor can also be used when it's not guaranteed that a source
  // position table exists, for example because it hasn't been collected. In
  // that case, Smi::zero() will be returned.
  DECL_ACQUIRE_GETTER(raw_source_position_table, Tagged<Object>)

  // Indicates that an attempt was made to collect source positions, but that it
  // failed, most likely due to stack exhaustion. When in this state
  // |SourcePositionTable| will return an empty byte array.
  inline void SetSourcePositionsFailedToCollect();

  inline int BytecodeArraySize() const;

  // Returns the size of bytecode and its metadata. This includes the size of
  // bytecode, constant pool, source position table, and handler table.
  DECL_GETTER(SizeIncludingMetadata, int)

  DECL_CAST(BytecodeArray)
  DECL_PRINTER(BytecodeArray)
  DECL_VERIFIER(BytecodeArray)

  V8_EXPORT_PRIVATE void PrintJson(std::ostream& os);
  V8_EXPORT_PRIVATE void Disassemble(std::ostream& os);

  V8_EXPORT_PRIVATE static void Disassemble(Handle<BytecodeArray> handle,
                                            std::ostream& os);

  void CopyBytecodesTo(Tagged<BytecodeArray> to);

  // Clear uninitialized padding space. This ensures that the snapshot content
  // is deterministic.
  inline void clear_padding();

  // Maximal memory consumption for a single BytecodeArray.
  static const int kMaxSize = 512 * MB;
  // Maximal length of a single BytecodeArray.
  static const int kMaxLength = kMaxSize - kHeaderSize;

#define FIELD_LIST(V)                                                   \
  V(kLengthOffset, kTaggedSize)                                         \
  V(kWrapperOffset, kTaggedSize)                                        \
  V(kSourcePositionTableOffset, kTaggedSize)                            \
  V(kHandlerTableOffset, kTaggedSize)                                   \
  V(kConstantPoolOffset, kTaggedSize)                                   \
  V(kFrameSizeOffset, kInt32Size)                                       \
  V(kParameterSizeOffset, kInt32Size)                                   \
  V(kIncomingNewTargetOrGeneratorRegisterOffset, kInt32Size)            \
  V(kOptionalPaddingOffset, 0)                                          \
  V(kUnalignedHeaderSize, OBJECT_POINTER_PADDING(kUnalignedHeaderSize)) \
  V(kHeaderSize, 0)                                                     \
  V(kBytesOffset, 0)

  DEFINE_FIELD_OFFSET_CONSTANTS(ExposedTrustedObject::kHeaderSize, FIELD_LIST)
#undef FIELD_LIST

  class BodyDescriptor;

  OBJECT_CONSTRUCTORS(BytecodeArray, ExposedTrustedObject);
};

// A BytecodeWrapper wraps a BytecodeArray but lives inside the sandbox. This
// can be useful for example when a reference to a BytecodeArray needs to be
// stored along other tagged pointers inside an array or similar datastructure.
class BytecodeWrapper : public Struct {
 public:
  DECL_TRUSTED_POINTER_ACCESSORS(bytecode, BytecodeArray)

  DECL_CAST(BytecodeWrapper)
  DECL_PRINTER(BytecodeWrapper)
  DECL_VERIFIER(BytecodeWrapper)

  // When flushing bytecode, we in-place convert the wrapper object to an
  // UncompiledData object (we cannot convert the BytecodeArray itself as that
  // lives in trusted space). As such, the wrapper object must be at least as
  // large as an UncompiledData object and therefore requires padding.
#define FIELD_LIST(V)                     \
  V(kBytecodeOffset, kTrustedPointerSize) \
  V(kPadding1Offset, kInt32Size)          \
  V(kPadding2Offset, kInt32Size)          \
  V(kHeaderSize, 0)                       \
  V(kSize, 0)

  DEFINE_FIELD_OFFSET_CONSTANTS(Struct::kHeaderSize, FIELD_LIST)
#undef FIELD_LIST

  inline void clear_padding();

  class BodyDescriptor;

  OBJECT_CONSTRUCTORS(BytecodeWrapper, Struct);
};

}  // namespace internal
}  // namespace v8

#include "src/objects/object-macros-undef.h"

#endif  // V8_OBJECTS_BYTECODE_ARRAY_H_
