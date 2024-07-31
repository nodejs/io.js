// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_OBJECTS_ABSTRACT_CODE_H_
#define V8_OBJECTS_ABSTRACT_CODE_H_

#include "src/objects/code-kind.h"
#include "src/objects/heap-object.h"

// Has to be the last include (doesn't have include guards):
#include "src/objects/object-macros.h"

namespace v8 {
namespace internal {

enum class Builtin;
class BytecodeArray;
class Code;

// AbstractCode is a helper wrapper around {Code|BytecodeArray}.
// TODO(jgruber): Consider removing this wrapper as it's mainly used for
// profiling. Perhaps methods should be specialized instead of this wrapper
// class?
class AbstractCode : public HeapObject {
 public:
  int SourcePosition(Isolate* isolate, int offset);
  int SourceStatementPosition(Isolate* isolate, int offset);

  inline Address InstructionStart(PtrComprCageBase cage_base);
  inline Address InstructionEnd(PtrComprCageBase cage_base);
  inline int InstructionSize(PtrComprCageBase cage_base);

  // Return the source position table for interpreter code.
  inline Tagged<TrustedByteArray> SourcePositionTable(
      Isolate* isolate, Tagged<SharedFunctionInfo> sfi);

  void DropStackFrameCache(PtrComprCageBase cage_base);

  // Returns the size of instructions and the metadata.
  inline int SizeIncludingMetadata(PtrComprCageBase cage_base);

  // Returns true if pc is inside this object's instructions.
  inline bool contains(Isolate* isolate, Address pc);

  // Returns the kind of the code.
  inline CodeKind kind(PtrComprCageBase cage_base);

  inline Builtin builtin_id(PtrComprCageBase cage_base);

  inline bool has_instruction_stream(PtrComprCageBase cage_base);

  DECL_CAST(AbstractCode)

  inline Tagged<Code> GetCode();
  inline Tagged<BytecodeArray> GetBytecodeArray();

 private:
  inline Tagged<TrustedByteArray> SourcePositionTableInternal(Isolate* isolate);

  OBJECT_CONSTRUCTORS(AbstractCode, HeapObject);
};

// Currently we must use full-pointer comparisons (instead of
// compressed-pointer comparisons) when comparing AbstractCode. This is because
// AbstractCode is either a Code or a BytecodeArray, and the latter lives in
// trusted space (outside of the main pointer compression cage) while the
// former still lives inside of the sandbox.
static_assert(!kAllCodeObjectsLiveInTrustedSpace);
constexpr bool operator==(const Tagged<AbstractCode> lhs,
                          const Tagged<AbstractCode> rhs) {
  return lhs->ptr() == rhs->ptr();
}

}  // namespace internal
}  // namespace v8

#include "src/objects/object-macros-undef.h"

#endif  // V8_OBJECTS_ABSTRACT_CODE_H_
