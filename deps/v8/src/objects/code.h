// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_OBJECTS_CODE_H_
#define V8_OBJECTS_CODE_H_

#include "src/codegen/maglev-safepoint-table.h"
#include "src/objects/code-kind.h"
#include "src/objects/struct.h"
#include "src/objects/trusted-object.h"

// Has to be the last include (doesn't have include guards):
#include "src/objects/object-macros.h"

namespace v8 {
namespace internal {

class BytecodeArray;
class CodeDesc;
class CodeWrapper;
class Factory;
template <typename Impl>
class FactoryBase;
class LocalFactory;
class SafepointEntry;
class RootVisitor;

enum class Builtin;

// Code is a container for data fields related to its associated
// {InstructionStream} object. Since {InstructionStream} objects reside on
// write-protected pages within the heap, its header fields need to be
// immutable.  Every InstructionStream object has an associated Code object,
// but not every Code object has an InstructionStream (e.g. for builtins).
//
// Embedded builtins consist of on-heap Code objects, with an out-of-line body
// section. Accessors (e.g. InstructionStart), redirect to the off-heap area.
// Metadata table offsets remain relative to MetadataStart(), i.e. they point
// into the off-heap metadata section. The off-heap layout is described in
// detail in the EmbeddedData class, but at a high level one can assume a
// dedicated, out-of-line, instruction and metadata section for each embedded
// builtin:
//
//  +--------------------------+  <-- InstructionStart()
//  |   off-heap instructions  |
//  |           ...            |
//  +--------------------------+  <-- InstructionEnd()
//
//  +--------------------------+  <-- MetadataStart() (MS)
//  |    off-heap metadata     |
//  |           ...            |  <-- MS + handler_table_offset()
//  |                          |  <-- MS + constant_pool_offset()
//  |                          |  <-- MS + code_comments_offset()
//  |                          |  <-- MS + unwinding_info_offset()
//  +--------------------------+  <-- MetadataEnd()
//
// When the sandbox is enabled, Code objects are allocated outside the sandbox
// and referenced through indirect pointers, so they need to inherit from
// ExposedTrustedObject.
class Code : public ExposedTrustedObject {
 public:
  // When V8_EXTERNAL_CODE_SPACE is enabled, InstructionStream objects are
  // allocated in a separate pointer compression cage instead of the cage where
  // all the other objects are allocated.
  inline PtrComprCageBase code_cage_base() const;

  // Back-reference to the InstructionStream object.
  //
  // Note the cage-less accessor versions may not be called if the current Code
  // object is InReadOnlySpace. That may only be the case for Code objects
  // representing builtins, or in other words, Code objects for which
  // has_instruction_stream() is never true.
  DECL_GETTER(instruction_stream, Tagged<InstructionStream>)
  DECL_RELAXED_GETTER(instruction_stream, Tagged<InstructionStream>)
  DECL_ACCESSORS(raw_instruction_stream, Tagged<Object>)
  DECL_RELAXED_GETTER(raw_instruction_stream, Tagged<Object>)
  // An unchecked accessor to be used during GC.
  inline Tagged<InstructionStream> unchecked_instruction_stream() const;

  // Whether this Code object has an associated InstructionStream (embedded
  // builtins don't).
  inline bool has_instruction_stream() const;
  inline bool has_instruction_stream(RelaxedLoadTag) const;

  // The start of the associated instruction stream. Points either into an
  // on-heap InstructionStream object, or to the beginning of an embedded
  // builtin.
  DECL_GETTER(instruction_start, Address)
  DECL_PRIMITIVE_ACCESSORS(instruction_size, int)
  inline Address instruction_end() const;

  inline CodeEntrypointTag entrypoint_tag() const;

  inline void SetInstructionStreamAndInstructionStart(
      IsolateForSandbox isolate, Tagged<InstructionStream> code,
      WriteBarrierMode mode = UPDATE_WRITE_BARRIER);
  inline void SetInstructionStartForOffHeapBuiltin(IsolateForSandbox isolate,
                                                   Address entry);
  inline void ClearInstructionStartForSerialization(IsolateForSandbox isolate);
  inline void UpdateInstructionStart(IsolateForSandbox isolate,
                                     Tagged<InstructionStream> istream);

  inline void initialize_flags(CodeKind kind, bool is_turbofanned,
                               int stack_slots);

  // Clear uninitialized padding space. This ensures that the snapshot content
  // is deterministic.
  inline void clear_padding();

  // Flushes the instruction cache for the executable instructions of this code
  // object. Make sure to call this while the code is still writable.
  void FlushICache() const;

  DECL_PRIMITIVE_ACCESSORS(can_have_weak_objects, bool)
  DECL_PRIMITIVE_ACCESSORS(marked_for_deoptimization, bool)

  DECL_PRIMITIVE_ACCESSORS(metadata_size, int)
  // [handler_table_offset]: The offset where the exception handler table
  // starts.
  DECL_PRIMITIVE_ACCESSORS(handler_table_offset, int)
  // [unwinding_info_offset]: Offset of the unwinding info section.
  DECL_PRIMITIVE_ACCESSORS(unwinding_info_offset, int32_t)
  // [deoptimization_data]: Array containing data for deopt for non-baseline
  // code.
  DECL_ACCESSORS(deoptimization_data, Tagged<ProtectedFixedArray>)
  // [parameter_count]: The number of formal parameters, including the
  // receiver. Currently only available for optimized functions.
  // TODO(saelo): make this always available. This is just a matter of figuring
  // out how to obtain the parameter count during code generation when no
  // BytecodeArray is available from which it can be copied.
  DECL_PRIMITIVE_ACCESSORS(parameter_count, uint16_t)
  inline uint16_t parameter_count_without_receiver() const;
  DECL_PRIMITIVE_ACCESSORS(wasm_js_tagged_parameter_count, uint16_t)
  DECL_PRIMITIVE_ACCESSORS(wasm_js_first_tagged_parameter, uint16_t)

  // Whether this type of Code uses deoptimization data, in which case the
  // deoptimization_data field will be populated.
  inline bool uses_deoptimization_data() const;

  // If neither deoptimization data nor bytecode/interpreter data are used
  // (e.g. for builtin code), the respective field will contain Smi::zero().
  inline void clear_deoptimization_data_and_interpreter_data();
  inline bool has_deoptimization_data_or_interpreter_data() const;

  // [bytecode_or_interpreter_data]: BytecodeArray or InterpreterData for
  // baseline code.
  inline Tagged<TrustedObject> bytecode_or_interpreter_data() const;
  inline void set_bytecode_or_interpreter_data(
      Tagged<TrustedObject> value,
      WriteBarrierMode mode = UPDATE_WRITE_BARRIER);
  // [source_position_table]: ByteArray for the source positions table for
  // non-baseline code.
  DECL_ACCESSORS(source_position_table, Tagged<TrustedByteArray>)
  // [bytecode_offset_table]: ByteArray for the bytecode offset for baseline
  // code.
  DECL_ACCESSORS(bytecode_offset_table, Tagged<TrustedByteArray>)

  inline bool has_source_position_table_or_bytecode_offset_table() const;
  inline bool has_source_position_table() const;
  inline bool has_bytecode_offset_table() const;
  inline void clear_source_position_table_and_bytecode_offset_table();

  DECL_PRIMITIVE_ACCESSORS(inlined_bytecode_size, unsigned)
  DECL_PRIMITIVE_ACCESSORS(osr_offset, BytecodeOffset)
  // [code_comments_offset]: Offset of the code comment section.
  DECL_PRIMITIVE_ACCESSORS(code_comments_offset, int)
  // [constant_pool offset]: Offset of the constant pool.
  DECL_PRIMITIVE_ACCESSORS(constant_pool_offset, int)
  // [wrapper] The CodeWrapper for this Code. When the sandbox is enabled, the
  // Code object lives in trusted space outside of the sandbox, but the wrapper
  // object lives inside the main heap and therefore inside the sandbox. As
  // such, the wrapper object can be used in cases where a Code object needs to
  // be referenced alongside other tagged pointer references (so for example
  // inside a FixedArray).
  DECL_ACCESSORS(wrapper, Tagged<CodeWrapper>)

  // Unchecked accessors to be used during GC.
  inline Tagged<ProtectedFixedArray> unchecked_deoptimization_data() const;

  DECL_RELAXED_UINT32_ACCESSORS(flags)

  inline CodeKind kind() const;

  inline void set_builtin_id(Builtin builtin_id);
  inline Builtin builtin_id() const;
  inline bool is_builtin() const;

  inline bool is_optimized_code() const;
  inline bool is_wasm_code() const;

  inline bool is_interpreter_trampoline_builtin() const;
  inline bool is_baseline_trampoline_builtin() const;
  inline bool is_baseline_leave_frame_builtin() const;

  // Tells whether the code checks the tiering state in the function's feedback
  // vector.
  inline bool checks_tiering_state() const;

  // Tells whether the outgoing parameters of this code are tagged pointers.
  inline bool has_tagged_outgoing_params() const;

  // [is_maglevved]: Tells whether the code object was generated by the
  // Maglev optimizing compiler.
  inline bool is_maglevved() const;

  // [is_turbofanned]: Tells whether the code object was generated by the
  // TurboFan optimizing compiler.
  inline bool is_turbofanned() const;

  // [uses_safepoint_table]: Whether this InstructionStream object uses
  // safepoint tables (note the table may still be empty, see
  // has_safepoint_table).
  inline bool uses_safepoint_table() const;

  // [stack_slots]: If {uses_safepoint_table()}, the number of stack slots
  // reserved in the code prologue; otherwise 0.
  inline int stack_slots() const;

  inline Tagged<TrustedByteArray> SourcePositionTable(
      Isolate* isolate, Tagged<SharedFunctionInfo> sfi) const;
  int SourcePosition(int offset) const;
  int SourceStatementPosition(int offset) const;

  inline Address safepoint_table_address() const;
  inline int safepoint_table_size() const;
  inline bool has_safepoint_table() const;

  inline Address handler_table_address() const;
  inline int handler_table_size() const;
  inline bool has_handler_table() const;

  inline Address constant_pool() const;
  inline int constant_pool_size() const;
  inline bool has_constant_pool() const;

  inline Address code_comments() const;
  inline int code_comments_size() const;
  inline bool has_code_comments() const;

  inline Address unwinding_info_start() const;
  inline Address unwinding_info_end() const;
  inline int unwinding_info_size() const;
  inline bool has_unwinding_info() const;

  inline uint8_t* relocation_start() const;
  inline uint8_t* relocation_end() const;
  inline int relocation_size() const;

  inline int safepoint_table_offset() const { return 0; }

  inline Address body_start() const;
  inline Address body_end() const;
  inline int body_size() const;

  inline Address metadata_start() const;
  inline Address metadata_end() const;

  // The size of the associated InstructionStream object, if it exists.
  inline int InstructionStreamObjectSize() const;

  // TODO(jgruber): This function tries to account for various parts of the
  // object graph, but is incomplete. Take it as a lower bound for the memory
  // associated with this Code object.
  inline int SizeIncludingMetadata() const;

  // The following functions include support for short builtin calls:
  //
  // When builtins un-embedding is enabled for the Isolate
  // (see Isolate::is_short_builtin_calls_enabled()) then both embedded and
  // un-embedded builtins might be exeuted and thus two kinds of |pc|s might
  // appear on the stack.
  // Unlike the paremeterless versions of the functions above the below variants
  // ensure that the instruction start correspond to the given |pc| value.
  // Thus for off-heap trampoline InstructionStream objects the result might be
  // the instruction start/end of the embedded code stream or of un-embedded
  // one. For normal InstructionStream objects these functions just return the
  // instruction_start/end() values.
  // TODO(11527): remove these versions once the full solution is ready.
  inline Address InstructionStart(Isolate* isolate, Address pc) const;
  inline Address InstructionEnd(Isolate* isolate, Address pc) const;
  inline bool contains(Isolate* isolate, Address pc) const;
  inline int GetOffsetFromInstructionStart(Isolate* isolate, Address pc) const;
  // Support for short builtin calls END.

  SafepointEntry GetSafepointEntry(Isolate* isolate, Address pc);
  MaglevSafepointEntry GetMaglevSafepointEntry(Isolate* isolate, Address pc);

  void SetMarkedForDeoptimization(Isolate* isolate, const char* reason);

  inline bool CanContainWeakObjects();
  inline bool IsWeakObject(Tagged<HeapObject> object);
  static inline bool IsWeakObjectInOptimizedCode(Tagged<HeapObject> object);
  static inline bool IsWeakObjectInDeoptimizationLiteralArray(
      Tagged<Object> object);

  // This function should be called only from GC.
  void ClearEmbeddedObjects(Heap* heap);

  // [embedded_objects_cleared]: If CodeKindIsOptimizedJSFunction(kind), tells
  // whether the embedded objects in the code marked for deoptimization were
  // cleared. Note that embedded_objects_cleared() implies
  // marked_for_deoptimization().
  inline bool embedded_objects_cleared() const;
  inline void set_embedded_objects_cleared(bool flag);

  bool IsIsolateIndependent(Isolate* isolate);

  inline uintptr_t GetBaselineStartPCForBytecodeOffset(
      int bytecode_offset, Tagged<BytecodeArray> bytecodes);

  inline uintptr_t GetBaselineEndPCForBytecodeOffset(
      int bytecode_offset, Tagged<BytecodeArray> bytecodes);

  // Returns true if the function is inlined in the code.
  bool Inlines(Tagged<SharedFunctionInfo> sfi);

  // Returns the PC of the next bytecode in execution order.
  // If the bytecode at the given offset is JumpLoop, the PC of the jump target
  // is returned. Other jumps are not allowed.
  // For other bytecodes this is equivalent to
  // GetBaselineEndPCForBytecodeOffset.
  inline uintptr_t GetBaselinePCForNextExecutedBytecode(
      int bytecode_offset, Tagged<BytecodeArray> bytecodes);

  inline int GetBytecodeOffsetForBaselinePC(Address baseline_pc,
                                            Tagged<BytecodeArray> bytecodes);

  inline void IterateDeoptimizationLiterals(RootVisitor* v);

  static inline Tagged<Code> FromTargetAddress(Address address);

#ifdef ENABLE_DISASSEMBLER
  V8_EXPORT_PRIVATE void Disassemble(const char* name, std::ostream& os,
                                     Isolate* isolate,
                                     Address current_pc = kNullAddress);
  V8_EXPORT_PRIVATE void DisassembleOnlyCode(const char* name, std::ostream& os,
                                             Isolate* isolate,
                                             Address current_pc,
                                             size_t range_limit);
#endif  // ENABLE_DISASSEMBLER

#ifdef OBJECT_PRINT
  void CodePrint(std::ostream& os, const char* name = nullptr,
                 Address current_pc = kNullAddress);
#endif

  DECL_VERIFIER(Code)

// Layout description.
#define CODE_DATA_FIELDS(V)                                                    \
  /* The deoptimization_data_or_interpreter_data field contains: */            \
  /*  - A DeoptimizationData for optimized code (maglev or turbofan) */        \
  /*  - A BytecodeArray or InterpreterData for baseline code */                \
  /*  - Smi::zero() for all other types of code (e.g. builtin) */              \
  V(kDeoptimizationDataOrInterpreterDataOffset, kTaggedSize)                   \
  /* This field contains: */                                                   \
  /*  - A bytecode offset table (trusted byte array) for baseline code */      \
  /*  - A (possibly empty) source position table (trusted byte array) for */   \
  /*    most other types of code */                                            \
  /*  - Smi::zero() for embedded builtin code (in RO space) */                 \
  /*    TODO(saelo) once we have a  trusted RO space, we could instead use */  \
  /*    empty_trusted_byte_array to avoid using Smi::zero() at all. */         \
  V(kPositionTableOffset, kTaggedSize)                                         \
  /* Strong pointer fields. */                                                 \
  V(kStartOfStrongFieldsOffset, 0)                                             \
  V(kWrapperOffset, kTaggedSize)                                               \
  V(kEndOfStrongFieldsWithMainCageBaseOffset, 0)                               \
  /* The InstructionStream field is special: it uses code_cage_base. */        \
  V(kInstructionStreamOffset, kTaggedSize)                                     \
  V(kEndOfStrongFieldsOffset, 0)                                               \
  /* Untagged data not directly visited by GC starts here. */                  \
  /* When the sandbox is off, the instruction_start field contains a raw */    \
  /* pointer to the first instruction of this Code. */                         \
  /* If the sandbox is on, this field does not exist. Instead, the */          \
  /* instruction_start is stored in this Code's code pointer table entry */    \
  /* referenced via the kSelfIndirectPointerOffset field */                    \
  V(kInstructionStartOffset, V8_ENABLE_SANDBOX_BOOL ? 0 : kSystemPointerSize)  \
  /* The serializer needs to copy bytes starting from here verbatim. */        \
  V(kFlagsOffset, kUInt32Size)                                                 \
  V(kInstructionSizeOffset, kIntSize)                                          \
  V(kMetadataSizeOffset, kIntSize)                                             \
  /* TODO(jgruber): TF-specific fields could be merged with builtin_id. */     \
  V(kInlinedBytecodeSizeOffset, kIntSize)                                      \
  V(kOsrOffsetOffset, kInt32Size)                                              \
  V(kHandlerTableOffsetOffset, kIntSize)                                       \
  V(kUnwindingInfoOffsetOffset, kInt32Size)                                    \
  V(kConstantPoolOffsetOffset, V8_EMBEDDED_CONSTANT_POOL_BOOL ? kIntSize : 0)  \
  V(kCodeCommentsOffsetOffset, kIntSize)                                       \
  /* This field is currently only used during deoptimization. If this space */ \
  /* is ever needed for other purposes, it would probably be possible to */    \
  /* obtain the parameter count from the BytecodeArray instead. */             \
  V(kParameterCountOffset, kUInt16Size)                                        \
  /* TODO(jgruber): 12 bits would suffice, steal from here if needed. */       \
  V(kBuiltinIdOffset, kInt16Size)                                              \
  V(kUnalignedSize, OBJECT_POINTER_PADDING(kUnalignedSize))                    \
  /* Total size. */                                                            \
  V(kSize, 0)

  DEFINE_FIELD_OFFSET_CONSTANTS(ExposedTrustedObject::kHeaderSize,
                                CODE_DATA_FIELDS)

#undef CODE_DATA_FIELDS

#ifdef V8_EXTERNAL_CODE_SPACE
  template <typename T>
  using ExternalCodeField =
      TaggedField<T, kInstructionStreamOffset, ExternalCodeCompressionScheme>;
#else
  template <typename T>
  using ExternalCodeField = TaggedField<T, kInstructionStreamOffset>;
#endif  // V8_EXTERNAL_CODE_SPACE

  class BodyDescriptor;

  // Flags layout.
#define FLAGS_BIT_FIELDS(V, _)                \
  V(KindField, CodeKind, 4, _)                \
  V(IsTurbofannedField, bool, 1, _)           \
  /* Steal bits from here if needed: */       \
  V(StackSlotsField, int, 24, _)              \
  V(MarkedForDeoptimizationField, bool, 1, _) \
  V(EmbeddedObjectsClearedField, bool, 1, _)  \
  V(CanHaveWeakObjectsField, bool, 1, _)
  DEFINE_BIT_FIELDS(FLAGS_BIT_FIELDS)
#undef FLAGS_BIT_FIELDS
  static_assert(FLAGS_BIT_FIELDS_Ranges::kBitsCount == 32);
  static_assert(FLAGS_BIT_FIELDS_Ranges::kBitsCount <=
                FIELD_SIZE(kFlagsOffset) * kBitsPerByte);
  static_assert(kCodeKindCount <= KindField::kNumValues);

  // The {marked_for_deoptimization} field is accessed from generated code.
  static const int kMarkedForDeoptimizationBit =
      MarkedForDeoptimizationField::kShift;
  static const int kIsTurbofannedBit = IsTurbofannedField::kShift;

  // Reserve one argument count value as the "don't adapt arguments" sentinel.
  static const int kArgumentsBits = 16;
  static const int kMaxArguments = (1 << kArgumentsBits) - 2;

 private:
  inline void set_instruction_start(IsolateForSandbox isolate, Address value);

  // TODO(jgruber): These field names are incomplete, we've squashed in more
  // overloaded contents in the meantime. Update the field names.
  Tagged<Object> raw_deoptimization_data_or_interpreter_data() const;
  Tagged<Object> raw_position_table() const;

  enum BytecodeToPCPosition {
    kPcAtStartOfBytecode,
    // End of bytecode equals the start of the next bytecode.
    // We need it when we deoptimize to the next bytecode (lazy deopt or deopt
    // of non-topmost frame).
    kPcAtEndOfBytecode
  };
  inline uintptr_t GetBaselinePCForBytecodeOffset(
      int bytecode_offset, BytecodeToPCPosition position,
      Tagged<BytecodeArray> bytecodes);

  template <typename IsolateT>
  friend class Deserializer;
  friend Factory;
  friend FactoryBase<Factory>;
  friend FactoryBase<LocalFactory>;

  OBJECT_CONSTRUCTORS(Code, ExposedTrustedObject);
};

// A Code object when used in situations where gc might be in progress. The
// underlying pointer is guaranteed to be a Code object.
//
// Semantics around Code and InstructionStream objects are quite delicate when
// GC is in progress and objects are currently being moved, because the
// tightly-coupled object pair {Code,InstructionStream} are conceptually
// treated as a single object in our codebase, and we frequently convert
// between the two. However, during GC, extra care must be taken when accessing
// the `Code::instruction_stream` and `InstructionStream::code` slots because
// they may contain forwarding pointers.
//
// This class a) clarifies at use sites that we're dealing with a Code object
// in a situation that requires special semantics, and b) safely implements
// related functions.
//
// Note that both the underlying Code object and the associated
// InstructionStream may be forwarding pointers, thus type checks and normal
// (checked) casts do not work on GcSafeCode.
class GcSafeCode : public HeapObject {
 public:
  // Use with care, this casts away knowledge that we're dealing with a
  // special-semantics object.
  inline Tagged<Code> UnsafeCastToCode() const;

  // Safe accessors (these just forward to Code methods).
  inline Address instruction_start() const;
  inline Address instruction_end() const;
  inline bool is_builtin() const;
  inline Builtin builtin_id() const;
  inline CodeKind kind() const;
  inline bool is_interpreter_trampoline_builtin() const;
  inline bool is_baseline_trampoline_builtin() const;
  inline bool is_baseline_leave_frame_builtin() const;
  inline bool has_instruction_stream() const;
  inline bool is_maglevved() const;
  inline bool is_turbofanned() const;
  inline bool has_tagged_outgoing_params() const;
  inline bool marked_for_deoptimization() const;
  inline Tagged<Object> raw_instruction_stream() const;
  inline Address constant_pool() const;
  inline Address safepoint_table_address() const;
  inline int stack_slots() const;

  inline int GetOffsetFromInstructionStart(Isolate* isolate, Address pc) const;
  inline Address InstructionStart(Isolate* isolate, Address pc) const;
  inline Address InstructionEnd(Isolate* isolate, Address pc) const;
  inline bool CanDeoptAt(Isolate* isolate, Address pc) const;
  inline Tagged<Object> raw_instruction_stream(
      PtrComprCageBase code_cage_base) const;
  // The two following accessors repurpose the InlinedBytecodeSize field, see
  // comment in code-inl.h.
  inline uint16_t wasm_js_tagged_parameter_count() const;
  inline uint16_t wasm_js_first_tagged_parameter() const;

 private:
  OBJECT_CONSTRUCTORS(GcSafeCode, HeapObject);
};

// A CodeWrapper wraps a Code but lives inside the sandbox. This can be useful
// for example when a reference to a Code needs to be stored along other tagged
// pointers inside an array or similar container datastructure.
class CodeWrapper : public Struct {
 public:
  DECL_CODE_POINTER_ACCESSORS(code)

  DECL_PRINTER(CodeWrapper)
  DECL_VERIFIER(CodeWrapper)

#define FIELD_LIST(V)              \
  V(kCodeOffset, kCodePointerSize) \
  V(kHeaderSize, 0)                \
  V(kSize, 0)

  DEFINE_FIELD_OFFSET_CONSTANTS(Struct::kHeaderSize, FIELD_LIST)
#undef FIELD_LIST

  class BodyDescriptor;

  OBJECT_CONSTRUCTORS(CodeWrapper, Struct);
};

}  // namespace internal
}  // namespace v8

#include "src/objects/object-macros-undef.h"

#endif  // V8_OBJECTS_CODE_H_
