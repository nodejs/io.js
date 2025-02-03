// Copyright 2013 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_CODEGEN_ARM64_DECODER_ARM64_H_
#define V8_CODEGEN_ARM64_DECODER_ARM64_H_

#include <list>

#include "src/codegen/arm64/instructions-arm64.h"
#include "src/common/globals.h"

namespace v8 {
namespace internal {

// List macro containing all visitors needed by the decoder class.

#define VISITOR_LIST(V)                 \
  V(PCRelAddressing)                    \
  V(AddSubImmediate)                    \
  V(LogicalImmediate)                   \
  V(MoveWideImmediate)                  \
  V(Bitfield)                           \
  V(Extract)                            \
  V(UnconditionalBranch)                \
  V(UnconditionalBranchToRegister)      \
  V(CompareBranch)                      \
  V(TestBranch)                         \
  V(ConditionalBranch)                  \
  V(System)                             \
  V(Exception)                          \
  V(LoadStorePairPostIndex)             \
  V(LoadStorePairOffset)                \
  V(LoadStorePairPreIndex)              \
  V(LoadLiteral)                        \
  V(LoadStoreUnscaledOffset)            \
  V(LoadStorePostIndex)                 \
  V(LoadStorePreIndex)                  \
  V(LoadStoreRegisterOffset)            \
  V(LoadStoreUnsignedOffset)            \
  V(LoadStoreAcquireRelease)            \
  V(AtomicMemory)                       \
  V(LogicalShifted)                     \
  V(AddSubShifted)                      \
  V(AddSubExtended)                     \
  V(AddSubWithCarry)                    \
  V(ConditionalCompareRegister)         \
  V(ConditionalCompareImmediate)        \
  V(ConditionalSelect)                  \
  V(DataProcessing1Source)              \
  V(DataProcessing2Source)              \
  V(DataProcessing3Source)              \
  V(FPCompare)                          \
  V(FPConditionalCompare)               \
  V(FPConditionalSelect)                \
  V(FPImmediate)                        \
  V(FPDataProcessing1Source)            \
  V(FPDataProcessing2Source)            \
  V(FPDataProcessing3Source)            \
  V(FPIntegerConvert)                   \
  V(FPFixedPointConvert)                \
  V(NEON2RegMisc)                       \
  V(NEON3Different)                     \
  V(NEON3Extension)                     \
  V(NEON3Same)                          \
  V(NEON3SameHP)                        \
  V(NEONAcrossLanes)                    \
  V(NEONByIndexedElement)               \
  V(NEONCopy)                           \
  V(NEONExtract)                        \
  V(NEONLoadStoreMultiStruct)           \
  V(NEONLoadStoreMultiStructPostIndex)  \
  V(NEONLoadStoreSingleStruct)          \
  V(NEONLoadStoreSingleStructPostIndex) \
  V(NEONModifiedImmediate)              \
  V(NEONScalar2RegMisc)                 \
  V(NEONScalar3Diff)                    \
  V(NEONScalar3Same)                    \
  V(NEONScalarByIndexedElement)         \
  V(NEONScalarCopy)                     \
  V(NEONScalarPairwise)                 \
  V(NEONScalarShiftImmediate)           \
  V(NEONShiftImmediate)                 \
  V(NEONTable)                          \
  V(NEONPerm)                           \
  V(Unallocated)                        \
  V(Unimplemented)

// The Visitor interface. Disassembler and simulator (and other tools)
// must provide implementations for all of these functions.
class V8_EXPORT_PRIVATE DecoderVisitor {
 public:
  virtual ~DecoderVisitor() {}

#define DECLARE(A) virtual void Visit##A(Instruction* instr) = 0;
  VISITOR_LIST(DECLARE)
#undef DECLARE
};

// A visitor that dispatches to a list of visitors.
class V8_EXPORT_PRIVATE DispatchingDecoderVisitor : public DecoderVisitor {
 public:
  DispatchingDecoderVisitor() {}
  virtual ~DispatchingDecoderVisitor() {}

  // Register a new visitor class with the decoder.
  // Decode() will call the corresponding visitor method from all registered
  // visitor classes when decoding reaches the leaf node of the instruction
  // decode tree.
  // Visitors are called in the order.
  // A visitor can only be registered once.
  // Registering an already registered visitor will update its position.
  //
  //   d.AppendVisitor(V1);
  //   d.AppendVisitor(V2);
  //   d.PrependVisitor(V2);            // Move V2 at the start of the list.
  //   d.InsertVisitorBefore(V3, V2);
  //   d.AppendVisitor(V4);
  //   d.AppendVisitor(V4);             // No effect.
  //
  //   d.Decode(i);
  //
  // will call in order visitor methods in V3, V2, V1, V4.
  void AppendVisitor(DecoderVisitor* visitor);
  void PrependVisitor(DecoderVisitor* visitor);
  void InsertVisitorBefore(DecoderVisitor* new_visitor,
                           DecoderVisitor* registered_visitor);
  void InsertVisitorAfter(DecoderVisitor* new_visitor,
                          DecoderVisitor* registered_visitor);

  // Remove a previously registered visitor class from the list of visitors
  // stored by the decoder.
  void RemoveVisitor(DecoderVisitor* visitor);

  void VisitNEONShiftImmediate(const Instruction* instr);

#define DECLARE(A) void Visit##A(Instruction* instr);
  VISITOR_LIST(DECLARE)
#undef DECLARE

 private:
  // Visitors are registered in a list.
  std::list<DecoderVisitor*> visitors_;
};

template <typename V>
class Decoder : public V {
 public:
  Decoder() {}
  virtual ~Decoder() {}

  // Top-level instruction decoder function. Decodes an instruction and calls
  // the visitor functions registered with the Decoder class.
  virtual void Decode(Instruction* instr);

 private:
  // Decode the PC relative addressing instruction, and call the corresponding
  // visitors.
  // On entry, instruction bits 27:24 = 0x0.
  void DecodePCRelAddressing(Instruction* instr);

  // Decode the add/subtract immediate instruction, and call the corresponding
  // visitors.
  // On entry, instruction bits 27:24 = 0x1.
  void DecodeAddSubImmediate(Instruction* instr);

  // Decode the branch, system command, and exception generation parts of
  // the instruction tree, and call the corresponding visitors.
  // On entry, instruction bits 27:24 = {0x4, 0x5, 0x6, 0x7}.
  void DecodeBranchSystemException(Instruction* instr);

  // Decode the load and store parts of the instruction tree, and call
  // the corresponding visitors.
  // On entry, instruction bits 27:24 = {0x8, 0x9, 0xC, 0xD}.
  void DecodeLoadStore(Instruction* instr);

  // Decode the logical immediate and move wide immediate parts of the
  // instruction tree, and call the corresponding visitors.
  // On entry, instruction bits 27:24 = 0x2.
  void DecodeLogical(Instruction* instr);

  // Decode the bitfield and extraction parts of the instruction tree,
  // and call the corresponding visitors.
  // On entry, instruction bits 27:24 = 0x3.
  void DecodeBitfieldExtract(Instruction* instr);

  // Decode the data processing parts of the instruction tree, and call the
  // corresponding visitors.
  // On entry, instruction bits 27:24 = {0x1, 0xA, 0xB}.
  void DecodeDataProcessing(Instruction* instr);

  // Decode the floating point parts of the instruction tree, and call the
  // corresponding visitors.
  // On entry, instruction bits 27:24 = {0xE, 0xF}.
  void DecodeFP(Instruction* instr);

  // Decode the Advanced SIMD (NEON) load/store part of the instruction tree,
  // and call the corresponding visitors.
  // On entry, instruction bits 29:25 = 0x6.
  void DecodeNEONLoadStore(Instruction* instr);

  // Decode the Advanced SIMD (NEON) data processing part of the instruction
  // tree, and call the corresponding visitors.
  // On entry, instruction bits 27:25 = 0x7.
  void DecodeNEONVectorDataProcessing(Instruction* instr);

  // Decode the Advanced SIMD (NEON) scalar data processing part of the
  // instruction tree, and call the corresponding visitors.
  // On entry, instruction bits 28:25 = 0xF.
  void DecodeNEONScalarDataProcessing(Instruction* instr);
};

}  // namespace internal
}  // namespace v8

#endif  // V8_CODEGEN_ARM64_DECODER_ARM64_H_
