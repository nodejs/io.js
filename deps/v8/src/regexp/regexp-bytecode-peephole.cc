// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/regexp/regexp-bytecode-peephole.h"

#include "src/flags/flags.h"
#include "src/objects/fixed-array-inl.h"
#include "src/regexp/regexp-bytecodes.h"
#include "src/utils/memcopy.h"
#include "src/utils/utils.h"
#include "src/zone/zone-containers.h"
#include "src/zone/zone.h"

namespace v8 {
namespace internal {

namespace {

struct BytecodeArgument {
  int offset;
  int length;

  BytecodeArgument(int offset, int length) : offset(offset), length(length) {}
};

struct BytecodeArgumentMapping : BytecodeArgument {
  int new_length;

  BytecodeArgumentMapping(int offset, int length, int new_length)
      : BytecodeArgument(offset, length), new_length(new_length) {}
};

struct BytecodeArgumentCheck : BytecodeArgument {
  enum CheckType { kCheckAddress = 0, kCheckValue };
  CheckType type;
  int check_offset;
  int check_length;

  BytecodeArgumentCheck(int offset, int length, int check_offset)
      : BytecodeArgument(offset, length),
        type(kCheckAddress),
        check_offset(check_offset) {}
  BytecodeArgumentCheck(int offset, int length, int check_offset,
                        int check_length)
      : BytecodeArgument(offset, length),
        type(kCheckValue),
        check_offset(check_offset),
        check_length(check_length) {}
};

// Trie-Node for storing bytecode sequences we want to optimize.
class BytecodeSequenceNode {
 public:
  // Dummy bytecode used when we need to store/return a bytecode but it's not a
  // valid bytecode in the current context.
  static constexpr int kDummyBytecode = -1;

  BytecodeSequenceNode(int bytecode, Zone* zone);
  // Adds a new node as child of the current node if it isn't a child already.
  BytecodeSequenceNode& FollowedBy(int bytecode);
  // Marks the end of a sequence and sets optimized bytecode to replace all
  // bytecodes of the sequence with.
  BytecodeSequenceNode& ReplaceWith(int bytecode);
  // Maps arguments of bytecodes in the sequence to the optimized bytecode.
  // Order of invocation determines order of arguments in the optimized
  // bytecode.
  // Invoking this method is only allowed on nodes that mark the end of a valid
  // sequence (i.e. after ReplaceWith()).
  // bytecode_index_in_sequence: Zero-based index of the referred bytecode
  // within the sequence (e.g. the bytecode passed to CreateSequence() has
  // index 0).
  // argument_offset: Zero-based offset to the argument within the bytecode
  // (e.g. the first argument that's not packed with the bytecode has offset 4).
  // argument_byte_length: Length of the argument.
  // new_argument_byte_length: Length of the argument in the new bytecode
  // (= argument_byte_length if omitted).
  BytecodeSequenceNode& MapArgument(int bytecode_index_in_sequence,
                                    int argument_offset,
                                    int argument_byte_length,
                                    int new_argument_byte_length = 0);
  // Adds a check to the sequence node making it only a valid sequence when the
  // argument of the current bytecode at the specified offset matches the offset
  // to check against.
  // argument_offset: Zero-based offset to the argument within the bytecode
  // (e.g. the first argument that's not packed with the bytecode has offset 4).
  // argument_byte_length: Length of the argument.
  // check_byte_offset: Zero-based offset relative to the beginning of the
  // sequence that needs to match the value given by argument_offset. (e.g.
  // check_byte_offset 0 matches the address of the first bytecode in the
  // sequence).
  BytecodeSequenceNode& IfArgumentEqualsOffset(int argument_offset,
                                               int argument_byte_length,
                                               int check_byte_offset);
  // Adds a check to the sequence node making it only a valid sequence when the
  // argument of the current bytecode at the specified offset matches the
  // argument of another bytecode in the sequence.
  // This is similar to IfArgumentEqualsOffset, except that this method matches
  // the values of both arguments.
  BytecodeSequenceNode& IfArgumentEqualsValueAtOffset(
      int argument_offset, int argument_byte_length,
      int other_bytecode_index_in_sequence, int other_argument_offset,
      int other_argument_byte_length);
  // Marks an argument as unused.
  // All arguments that are not mapped explicitly have to be marked as unused.
  // bytecode_index_in_sequence: Zero-based index of the referred bytecode
  // within the sequence (e.g. the bytecode passed to CreateSequence() has
  // index 0).
  // argument_offset: Zero-based offset to the argument within the bytecode
  // (e.g. the first argument that's not packed with the bytecode has offset 4).
  // argument_byte_length: Length of the argument.
  BytecodeSequenceNode& IgnoreArgument(int bytecode_index_in_sequence,
                                       int argument_offset,
                                       int argument_byte_length);
  // Checks if the current node is valid for the sequence. I.e. all conditions
  // set by IfArgumentEqualsOffset and IfArgumentEquals are fulfilled by this
  // node for the actual bytecode sequence.
  bool CheckArguments(const uint8_t* bytecode, int pc);
  // Returns whether this node marks the end of a valid sequence (i.e. can be
  // replaced with an optimized bytecode).
  bool IsSequence() const;
  // Returns the length of the sequence in bytes.
  int SequenceLength() const;
  // Returns the optimized bytecode for the node or kDummyBytecode if it is not
  // the end of a valid sequence.
  int OptimizedBytecode() const;
  // Returns the child of the current node matching the given bytecode or
  // nullptr if no such child is found.
  BytecodeSequenceNode* Find(int bytecode) const;
  // Returns number of arguments mapped to the current node.
  // Invoking this method is only allowed on nodes that mark the end of a valid
  // sequence (i.e. if IsSequence())
  size_t ArgumentSize() const;
  // Returns the argument-mapping of the argument at index.
  // Invoking this method is only allowed on nodes that mark the end of a valid
  // sequence (i.e. if IsSequence())
  BytecodeArgumentMapping ArgumentMapping(size_t index) const;
  // Returns an iterator to begin of ignored arguments.
  // Invoking this method is only allowed on nodes that mark the end of a valid
  // sequence (i.e. if IsSequence())
  ZoneLinkedList<BytecodeArgument>::iterator ArgumentIgnoredBegin() const;
  // Returns an iterator to end of ignored arguments.
  // Invoking this method is only allowed on nodes that mark the end of a valid
  // sequence (i.e. if IsSequence())
  ZoneLinkedList<BytecodeArgument>::iterator ArgumentIgnoredEnd() const;
  // Returns whether the current node has ignored argument or not.
  bool HasIgnoredArguments() const;

 private:
  // Returns a node in the sequence specified by its index within the sequence.
  BytecodeSequenceNode& GetNodeByIndexInSequence(int index_in_sequence);
  Zone* zone() const;

  int bytecode_;
  int bytecode_replacement_;
  int index_in_sequence_;
  int start_offset_;
  BytecodeSequenceNode* parent_;
  ZoneUnorderedMap<int, BytecodeSequenceNode*> children_;
  ZoneVector<BytecodeArgumentMapping>* argument_mapping_;
  ZoneLinkedList<BytecodeArgumentCheck>* argument_check_;
  ZoneLinkedList<BytecodeArgument>* argument_ignored_;

  Zone* zone_;
};

// These definitions are here in order to please the linker, which in debug mode
// sometimes requires static constants to be defined in .cc files.
constexpr int BytecodeSequenceNode::kDummyBytecode;

class RegExpBytecodePeephole {
 public:
  RegExpBytecodePeephole(Zone* zone, size_t buffer_size,
                         const ZoneUnorderedMap<int, int>& jump_edges);

  // Parses bytecode and fills the internal buffer with the potentially
  // optimized bytecode. Returns true when optimizations were performed, false
  // otherwise.
  bool OptimizeBytecode(const uint8_t* bytecode, int length);
  // Copies the internal bytecode buffer to another buffer. The caller is
  // responsible for allocating/freeing the memory.
  void CopyOptimizedBytecode(uint8_t* to_address) const;
  int Length() const;

 private:
  // Sets up all sequences that are going to be used.
  void DefineStandardSequences();
  // Starts a new bytecode sequence.
  BytecodeSequenceNode& CreateSequence(int bytecode);
  // Checks for optimization candidates at pc and emits optimized bytecode to
  // the internal buffer. Returns the length of replaced bytecodes in bytes.
  int TryOptimizeSequence(const uint8_t* bytecode, int bytecode_length,
                          int start_pc);
  // Emits optimized bytecode to the internal buffer. start_pc points to the
  // start of the sequence in bytecode and last_node is the last
  // BytecodeSequenceNode of the matching sequence found.
  void EmitOptimization(int start_pc, const uint8_t* bytecode,
                        const BytecodeSequenceNode& last_node);
  // Adds a relative jump source fixup at pos.
  // Jump source fixups are used to find offsets in the new bytecode that
  // contain jump sources.
  void AddJumpSourceFixup(int fixup, int pos);
  // Adds a relative jump destination fixup at pos.
  // Jump destination fixups are used to find offsets in the new bytecode that
  // can be jumped to.
  void AddJumpDestinationFixup(int fixup, int pos);
  // Sets an absolute jump destination fixup at pos.
  void SetJumpDestinationFixup(int fixup, int pos);
  // Prepare internal structures used to fixup jumps.
  void PrepareJumpStructures(const ZoneUnorderedMap<int, int>& jump_edges);
  // Updates all jump targets in the new bytecode.
  void FixJumps();
  // Update a single jump.
  void FixJump(int jump_source, int jump_destination);
  void AddSentinelFixups(int pos);
  template <typename T>
  void EmitValue(T value);
  template <typename T>
  void OverwriteValue(int offset, T value);
  void CopyRangeToOutput(const uint8_t* orig_bytecode, int start, int length);
  void SetRange(uint8_t value, int count);
  void EmitArgument(int start_pc, const uint8_t* bytecode,
                    BytecodeArgumentMapping arg);
  int pc() const;
  Zone* zone() const;

  ZoneVector<uint8_t> optimized_bytecode_buffer_;
  BytecodeSequenceNode* sequences_;
  // Jumps used in old bytecode.
  // Key: Jump source (offset where destination is stored in old bytecode)
  // Value: Destination
  ZoneMap<int, int> jump_edges_;
  // Jumps used in new bytecode.
  // Key: Jump source (offset where destination is stored in new bytecode)
  // Value: Destination
  ZoneMap<int, int> jump_edges_mapped_;
  // Number of times a jump destination is used within the bytecode.
  // Key: Jump destination (offset in old bytecode).
  // Value: Number of times jump destination is used.
  ZoneMap<int, int> jump_usage_counts_;
  // Maps offsets in old bytecode to fixups of sources (delta to new bytecode).
  // Key: Offset in old bytecode from where the fixup is valid.
  // Value: Delta to map jump source from old bytecode to new bytecode in bytes.
  ZoneMap<int, int> jump_source_fixups_;
  // Maps offsets in old bytecode to fixups of destinations (delta to new
  // bytecode).
  // Key: Offset in old bytecode from where the fixup is valid.
  // Value: Delta to map jump destinations from old bytecode to new bytecode in
  // bytes.
  ZoneMap<int, int> jump_destination_fixups_;

  Zone* zone_;

  DISALLOW_IMPLICIT_CONSTRUCTORS(RegExpBytecodePeephole);
};

template <typename T>
T GetValue(const uint8_t* buffer, int pos) {
  DCHECK(IsAligned(reinterpret_cast<Address>(buffer + pos), alignof(T)));
  return *reinterpret_cast<const T*>(buffer + pos);
}

int32_t GetArgumentValue(const uint8_t* bytecode, int offset, int length) {
  switch (length) {
    case 1:
      return GetValue<uint8_t>(bytecode, offset);
    case 2:
      return GetValue<int16_t>(bytecode, offset);
    case 4:
      return GetValue<int32_t>(bytecode, offset);
    default:
      UNREACHABLE();
  }
}

BytecodeSequenceNode::BytecodeSequenceNode(int bytecode, Zone* zone)
    : bytecode_(bytecode),
      bytecode_replacement_(kDummyBytecode),
      index_in_sequence_(0),
      start_offset_(0),
      parent_(nullptr),
      children_(ZoneUnorderedMap<int, BytecodeSequenceNode*>(zone)),
      argument_mapping_(zone->New<ZoneVector<BytecodeArgumentMapping>>(zone)),
      argument_check_(zone->New<ZoneLinkedList<BytecodeArgumentCheck>>(zone)),
      argument_ignored_(zone->New<ZoneLinkedList<BytecodeArgument>>(zone)),
      zone_(zone) {}

BytecodeSequenceNode& BytecodeSequenceNode::FollowedBy(int bytecode) {
  DCHECK(0 <= bytecode && bytecode < kRegExpBytecodeCount);

  if (children_.find(bytecode) == children_.end()) {
    BytecodeSequenceNode* new_node =
        zone()->New<BytecodeSequenceNode>(bytecode, zone());
    // If node is not the first in the sequence, set offsets and parent.
    if (bytecode_ != kDummyBytecode) {
      new_node->start_offset_ = start_offset_ + RegExpBytecodeLength(bytecode_);
      new_node->index_in_sequence_ = index_in_sequence_ + 1;
      new_node->parent_ = this;
    }
    children_[bytecode] = new_node;
  }

  return *children_[bytecode];
}

BytecodeSequenceNode& BytecodeSequenceNode::ReplaceWith(int bytecode) {
  DCHECK(0 <= bytecode && bytecode < kRegExpBytecodeCount);

  bytecode_replacement_ = bytecode;

  return *this;
}

BytecodeSequenceNode& BytecodeSequenceNode::MapArgument(
    int bytecode_index_in_sequence, int argument_offset,
    int argument_byte_length, int new_argument_byte_length) {
  DCHECK(IsSequence());
  DCHECK_LE(bytecode_index_in_sequence, index_in_sequence_);

  BytecodeSequenceNode& ref_node =
      GetNodeByIndexInSequence(bytecode_index_in_sequence);
  DCHECK_LT(argument_offset, RegExpBytecodeLength(ref_node.bytecode_));

  int absolute_offset = ref_node.start_offset_ + argument_offset;
  if (new_argument_byte_length == 0) {
    new_argument_byte_length = argument_byte_length;
  }

  argument_mapping_->push_back(BytecodeArgumentMapping{
      absolute_offset, argument_byte_length, new_argument_byte_length});

  return *this;
}

BytecodeSequenceNode& BytecodeSequenceNode::IfArgumentEqualsOffset(
    int argument_offset, int argument_byte_length, int check_byte_offset) {
  DCHECK_LT(argument_offset, RegExpBytecodeLength(bytecode_));
  DCHECK(argument_byte_length == 1 || argument_byte_length == 2 ||
         argument_byte_length == 4);

  int absolute_offset = start_offset_ + argument_offset;

  argument_check_->push_back(BytecodeArgumentCheck{
      absolute_offset, argument_byte_length, check_byte_offset});

  return *this;
}

BytecodeSequenceNode& BytecodeSequenceNode::IfArgumentEqualsValueAtOffset(
    int argument_offset, int argument_byte_length,
    int other_bytecode_index_in_sequence, int other_argument_offset,
    int other_argument_byte_length) {
  DCHECK_LT(argument_offset, RegExpBytecodeLength(bytecode_));
  DCHECK_LE(other_bytecode_index_in_sequence, index_in_sequence_);
  DCHECK_EQ(argument_byte_length, other_argument_byte_length);

  BytecodeSequenceNode& ref_node =
      GetNodeByIndexInSequence(other_bytecode_index_in_sequence);
  DCHECK_LT(other_argument_offset, RegExpBytecodeLength(ref_node.bytecode_));

  int absolute_offset = start_offset_ + argument_offset;
  int other_absolute_offset = ref_node.start_offset_ + other_argument_offset;

  argument_check_->push_back(
      BytecodeArgumentCheck{absolute_offset, argument_byte_length,
                            other_absolute_offset, other_argument_byte_length});

  return *this;
}

BytecodeSequenceNode& BytecodeSequenceNode::IgnoreArgument(
    int bytecode_index_in_sequence, int argument_offset,
    int argument_byte_length) {
  DCHECK(IsSequence());
  DCHECK_LE(bytecode_index_in_sequence, index_in_sequence_);

  BytecodeSequenceNode& ref_node =
      GetNodeByIndexInSequence(bytecode_index_in_sequence);
  DCHECK_LT(argument_offset, RegExpBytecodeLength(ref_node.bytecode_));

  int absolute_offset = ref_node.start_offset_ + argument_offset;

  argument_ignored_->push_back(
      BytecodeArgument{absolute_offset, argument_byte_length});

  return *this;
}

bool BytecodeSequenceNode::CheckArguments(const uint8_t* bytecode, int pc) {
  bool is_valid = true;
  for (auto check_iter = argument_check_->begin();
       check_iter != argument_check_->end() && is_valid; check_iter++) {
    auto value =
        GetArgumentValue(bytecode, pc + check_iter->offset, check_iter->length);
    if (check_iter->type == BytecodeArgumentCheck::kCheckAddress) {
      is_valid &= value == pc + check_iter->check_offset;
    } else if (check_iter->type == BytecodeArgumentCheck::kCheckValue) {
      auto other_value = GetArgumentValue(
          bytecode, pc + check_iter->check_offset, check_iter->check_length);
      is_valid &= value == other_value;
    } else {
      UNREACHABLE();
    }
  }
  return is_valid;
}

bool BytecodeSequenceNode::IsSequence() const {
  return bytecode_replacement_ != kDummyBytecode;
}

int BytecodeSequenceNode::SequenceLength() const {
  return start_offset_ + RegExpBytecodeLength(bytecode_);
}

int BytecodeSequenceNode::OptimizedBytecode() const {
  return bytecode_replacement_;
}

BytecodeSequenceNode* BytecodeSequenceNode::Find(int bytecode) const {
  auto found = children_.find(bytecode);
  if (found == children_.end()) return nullptr;
  return found->second;
}

size_t BytecodeSequenceNode::ArgumentSize() const {
  DCHECK(IsSequence());
  return argument_mapping_->size();
}

BytecodeArgumentMapping BytecodeSequenceNode::ArgumentMapping(
    size_t index) const {
  DCHECK(IsSequence());
  DCHECK(argument_mapping_ != nullptr);
  DCHECK_LT(index, argument_mapping_->size());

  return argument_mapping_->at(index);
}

ZoneLinkedList<BytecodeArgument>::iterator
BytecodeSequenceNode::ArgumentIgnoredBegin() const {
  DCHECK(IsSequence());
  DCHECK(argument_ignored_ != nullptr);
  return argument_ignored_->begin();
}

ZoneLinkedList<BytecodeArgument>::iterator
BytecodeSequenceNode::ArgumentIgnoredEnd() const {
  DCHECK(IsSequence());
  DCHECK(argument_ignored_ != nullptr);
  return argument_ignored_->end();
}

bool BytecodeSequenceNode::HasIgnoredArguments() const {
  return argument_ignored_ != nullptr;
}

BytecodeSequenceNode& BytecodeSequenceNode::GetNodeByIndexInSequence(
    int index_in_sequence) {
  DCHECK_LE(index_in_sequence, index_in_sequence_);

  if (index_in_sequence < index_in_sequence_) {
    DCHECK(parent_ != nullptr);
    return parent_->GetNodeByIndexInSequence(index_in_sequence);
  } else {
    return *this;
  }
}

Zone* BytecodeSequenceNode::zone() const { return zone_; }

RegExpBytecodePeephole::RegExpBytecodePeephole(
    Zone* zone, size_t buffer_size,
    const ZoneUnorderedMap<int, int>& jump_edges)
    : optimized_bytecode_buffer_(zone),
      sequences_(zone->New<BytecodeSequenceNode>(
          BytecodeSequenceNode::kDummyBytecode, zone)),
      jump_edges_(zone),
      jump_edges_mapped_(zone),
      jump_usage_counts_(zone),
      jump_source_fixups_(zone),
      jump_destination_fixups_(zone),
      zone_(zone) {
  optimized_bytecode_buffer_.reserve(buffer_size);
  PrepareJumpStructures(jump_edges);
  DefineStandardSequences();
  // Sentinel fixups at beginning of bytecode (position -1) so we don't have to
  // check for end of iterator inside the fixup loop.
  // In general fixups are deltas of original offsets of jump
  // sources/destinations (in the old bytecode) to find them in the new
  // bytecode. All jump targets are fixed after the new bytecode is fully
  // emitted in the internal buffer.
  AddSentinelFixups(-1);
  // Sentinel fixups at end of (old) bytecode so we don't have to check for
  // end of iterator inside the fixup loop.
  DCHECK_LE(buffer_size, std::numeric_limits<int>::max());
  AddSentinelFixups(static_cast<int>(buffer_size));
}

void RegExpBytecodePeephole::DefineStandardSequences() {
  // Commonly used sequences can be found by creating regexp bytecode traces
  // (--trace-regexp-bytecodes) and using v8/tools/regexp-sequences.py.
  CreateSequence(BC_LOAD_CURRENT_CHAR)
      .FollowedBy(BC_CHECK_BIT_IN_TABLE)
      .FollowedBy(BC_ADVANCE_CP_AND_GOTO)
      // Sequence is only valid if the jump target of ADVANCE_CP_AND_GOTO is the
      // first bytecode in this sequence.
      .IfArgumentEqualsOffset(4, 4, 0)
      .ReplaceWith(BC_SKIP_UNTIL_BIT_IN_TABLE)
      .MapArgument(0, 1, 3)      // load offset
      .MapArgument(2, 1, 3, 4)   // advance by
      .MapArgument(1, 8, 16)     // bit table
      .MapArgument(1, 4, 4)      // goto when match
      .MapArgument(0, 4, 4)      // goto on failure
      .IgnoreArgument(2, 4, 4);  // loop jump

  CreateSequence(BC_CHECK_CURRENT_POSITION)
      .FollowedBy(BC_LOAD_CURRENT_CHAR_UNCHECKED)
      .FollowedBy(BC_CHECK_CHAR)
      .FollowedBy(BC_ADVANCE_CP_AND_GOTO)
      // Sequence is only valid if the jump target of ADVANCE_CP_AND_GOTO is the
      // first bytecode in this sequence.
      .IfArgumentEqualsOffset(4, 4, 0)
      .ReplaceWith(BC_SKIP_UNTIL_CHAR_POS_CHECKED)
      .MapArgument(1, 1, 3)      // load offset
      .MapArgument(3, 1, 3, 2)   // advance_by
      .MapArgument(2, 1, 3, 2)   // c
      .MapArgument(0, 1, 3, 4)   // eats at least
      .MapArgument(2, 4, 4)      // goto when match
      .MapArgument(0, 4, 4)      // goto on failure
      .IgnoreArgument(3, 4, 4);  // loop jump

  CreateSequence(BC_CHECK_CURRENT_POSITION)
      .FollowedBy(BC_LOAD_CURRENT_CHAR_UNCHECKED)
      .FollowedBy(BC_AND_CHECK_CHAR)
      .FollowedBy(BC_ADVANCE_CP_AND_GOTO)
      // Sequence is only valid if the jump target of ADVANCE_CP_AND_GOTO is the
      // first bytecode in this sequence.
      .IfArgumentEqualsOffset(4, 4, 0)
      .ReplaceWith(BC_SKIP_UNTIL_CHAR_AND)
      .MapArgument(1, 1, 3)      // load offset
      .MapArgument(3, 1, 3, 2)   // advance_by
      .MapArgument(2, 1, 3, 2)   // c
      .MapArgument(2, 4, 4)      // mask
      .MapArgument(0, 1, 3, 4)   // eats at least
      .MapArgument(2, 8, 4)      // goto when match
      .MapArgument(0, 4, 4)      // goto on failure
      .IgnoreArgument(3, 4, 4);  // loop jump

  // TODO(pthier): It might make sense for short sequences like this one to only
  // optimize them if the resulting optimization is not longer than the current
  // one. This could be the case if there are jumps inside the sequence and we
  // have to replicate parts of the sequence. A method to mark such sequences
  // might be useful.
  CreateSequence(BC_LOAD_CURRENT_CHAR)
      .FollowedBy(BC_CHECK_CHAR)
      .FollowedBy(BC_ADVANCE_CP_AND_GOTO)
      // Sequence is only valid if the jump target of ADVANCE_CP_AND_GOTO is the
      // first bytecode in this sequence.
      .IfArgumentEqualsOffset(4, 4, 0)
      .ReplaceWith(BC_SKIP_UNTIL_CHAR)
      .MapArgument(0, 1, 3)      // load offset
      .MapArgument(2, 1, 3, 2)   // advance by
      .MapArgument(1, 1, 3, 2)   // character
      .MapArgument(1, 4, 4)      // goto when match
      .MapArgument(0, 4, 4)      // goto on failure
      .IgnoreArgument(2, 4, 4);  // loop jump

  CreateSequence(BC_LOAD_CURRENT_CHAR)
      .FollowedBy(BC_CHECK_CHAR)
      .FollowedBy(BC_CHECK_CHAR)
      // Sequence is only valid if the jump targets of both CHECK_CHAR bytecodes
      // are equal.
      .IfArgumentEqualsValueAtOffset(4, 4, 1, 4, 4)
      .FollowedBy(BC_ADVANCE_CP_AND_GOTO)
      // Sequence is only valid if the jump target of ADVANCE_CP_AND_GOTO is the
      // first bytecode in this sequence.
      .IfArgumentEqualsOffset(4, 4, 0)
      .ReplaceWith(BC_SKIP_UNTIL_CHAR_OR_CHAR)
      .MapArgument(0, 1, 3)      // load offset
      .MapArgument(3, 1, 3, 4)   // advance by
      .MapArgument(1, 1, 3, 2)   // character 1
      .MapArgument(2, 1, 3, 2)   // character 2
      .MapArgument(1, 4, 4)      // goto when match
      .MapArgument(0, 4, 4)      // goto on failure
      .IgnoreArgument(2, 4, 4)   // goto when match 2
      .IgnoreArgument(3, 4, 4);  // loop jump

  CreateSequence(BC_LOAD_CURRENT_CHAR)
      .FollowedBy(BC_CHECK_GT)
      // Sequence is only valid if the jump target of CHECK_GT is the first
      // bytecode AFTER the whole sequence.
      .IfArgumentEqualsOffset(4, 4, 56)
      .FollowedBy(BC_CHECK_BIT_IN_TABLE)
      // Sequence is only valid if the jump target of CHECK_BIT_IN_TABLE is
      // the ADVANCE_CP_AND_GOTO bytecode at the end of the sequence.
      .IfArgumentEqualsOffset(4, 4, 48)
      .FollowedBy(BC_GOTO)
      // Sequence is only valid if the jump target of GOTO is the same as the
      // jump target of CHECK_GT (i.e. both jump to the first bytecode AFTER the
      // whole sequence.
      .IfArgumentEqualsValueAtOffset(4, 4, 1, 4, 4)
      .FollowedBy(BC_ADVANCE_CP_AND_GOTO)
      // Sequence is only valid if the jump target of ADVANCE_CP_AND_GOTO is the
      // first bytecode in this sequence.
      .IfArgumentEqualsOffset(4, 4, 0)
      .ReplaceWith(BC_SKIP_UNTIL_GT_OR_NOT_BIT_IN_TABLE)
      .MapArgument(0, 1, 3)      // load offset
      .MapArgument(4, 1, 3, 2)   // advance by
      .MapArgument(1, 1, 3, 2)   // character
      .MapArgument(2, 8, 16)     // bit table
      .MapArgument(1, 4, 4)      // goto when match
      .MapArgument(0, 4, 4)      // goto on failure
      .IgnoreArgument(2, 4, 4)   // indirect loop jump
      .IgnoreArgument(3, 4, 4)   // jump out of loop
      .IgnoreArgument(4, 4, 4);  // loop jump
}

bool RegExpBytecodePeephole::OptimizeBytecode(const uint8_t* bytecode,
                                              int length) {
  int old_pc = 0;
  bool did_optimize = false;

  while (old_pc < length) {
    int replaced_len = TryOptimizeSequence(bytecode, length, old_pc);
    if (replaced_len > 0) {
      old_pc += replaced_len;
      did_optimize = true;
    } else {
      int bc = bytecode[old_pc];
      int bc_len = RegExpBytecodeLength(bc);
      CopyRangeToOutput(bytecode, old_pc, bc_len);
      old_pc += bc_len;
    }
  }

  if (did_optimize) {
    FixJumps();
  }

  return did_optimize;
}

void RegExpBytecodePeephole::CopyOptimizedBytecode(uint8_t* to_address) const {
  MemCopy(to_address, &(*optimized_bytecode_buffer_.begin()), Length());
}

int RegExpBytecodePeephole::Length() const { return pc(); }

BytecodeSequenceNode& RegExpBytecodePeephole::CreateSequence(int bytecode) {
  DCHECK(sequences_ != nullptr);
  DCHECK(0 <= bytecode && bytecode < kRegExpBytecodeCount);

  return sequences_->FollowedBy(bytecode);
}

int RegExpBytecodePeephole::TryOptimizeSequence(const uint8_t* bytecode,
                                                int bytecode_length,
                                                int start_pc) {
  BytecodeSequenceNode* seq_node = sequences_;
  BytecodeSequenceNode* valid_seq_end = nullptr;

  int current_pc = start_pc;

  // Check for the longest valid sequence matching any of the pre-defined
  // sequences in the Trie data structure.
  while (current_pc < bytecode_length) {
    seq_node = seq_node->Find(bytecode[current_pc]);
    if (seq_node == nullptr) break;
    if (!seq_node->CheckArguments(bytecode, start_pc)) break;

    if (seq_node->IsSequence()) valid_seq_end = seq_node;
    current_pc += RegExpBytecodeLength(bytecode[current_pc]);
  }

  if (valid_seq_end) {
    EmitOptimization(start_pc, bytecode, *valid_seq_end);
    return valid_seq_end->SequenceLength();
  }

  return 0;
}

void RegExpBytecodePeephole::EmitOptimization(
    int start_pc, const uint8_t* bytecode,
    const BytecodeSequenceNode& last_node) {
#ifdef DEBUG
  int optimized_start_pc = pc();
#endif
  // Jump sources that are mapped or marked as unused will be deleted at the end
  // of this method. We don't delete them immediately as we might need the
  // information when we have to preserve bytecodes at the end.
  // TODO(pthier): Replace with a stack-allocated data structure.
  ZoneLinkedList<int> delete_jumps = ZoneLinkedList<int>(zone());

  uint32_t bc = last_node.OptimizedBytecode();
  EmitValue(bc);

  for (size_t arg = 0; arg < last_node.ArgumentSize(); arg++) {
    BytecodeArgumentMapping arg_map = last_node.ArgumentMapping(arg);
    int arg_pos = start_pc + arg_map.offset;
    // If we map any jump source we mark the old source for deletion and insert
    // a new jump.
    auto jump_edge_iter = jump_edges_.find(arg_pos);
    if (jump_edge_iter != jump_edges_.end()) {
      int jump_source = jump_edge_iter->first;
      int jump_destination = jump_edge_iter->second;
      // Add new jump edge add current position.
      jump_edges_mapped_.emplace(Length(), jump_destination);
      // Mark old jump edge for deletion.
      delete_jumps.push_back(jump_source);
      // Decrement usage count of jump destination.
      auto jump_count_iter = jump_usage_counts_.find(jump_destination);
      DCHECK(jump_count_iter != jump_usage_counts_.end());
      int& usage_count = jump_count_iter->second;
      --usage_count;
    }
    // TODO(pthier): DCHECK that mapped arguments are never sources of jumps
    // to destinations inside the sequence.
    EmitArgument(start_pc, bytecode, arg_map);
  }
  DCHECK_EQ(pc(), optimized_start_pc +
                      RegExpBytecodeLength(last_node.OptimizedBytecode()));

  // Remove jumps from arguments we ignore.
  if (last_node.HasIgnoredArguments()) {
    for (auto ignored_arg = last_node.ArgumentIgnoredBegin();
         ignored_arg != last_node.ArgumentIgnoredEnd(); ignored_arg++) {
      auto jump_edge_iter = jump_edges_.find(start_pc + ignored_arg->offset);
      if (jump_edge_iter != jump_edges_.end()) {
        int jump_source = jump_edge_iter->first;
        int jump_destination = jump_edge_iter->second;
        // Mark old jump edge for deletion.
        delete_jumps.push_back(jump_source);
        // Decrement usage count of jump destination.
        auto jump_count_iter = jump_usage_counts_.find(jump_destination);
        DCHECK(jump_count_iter != jump_usage_counts_.end());
        int& usage_count = jump_count_iter->second;
        --usage_count;
      }
    }
  }

  int fixup_length = RegExpBytecodeLength(bc) - last_node.SequenceLength();

  // Check if there are any jumps inside the old sequence.
  // If so we have to keep the bytecodes that are jumped to around.
  auto jump_destination_candidate = jump_usage_counts_.upper_bound(start_pc);
  int jump_candidate_destination = jump_destination_candidate->first;
  int jump_candidate_count = jump_destination_candidate->second;
  // Jump destinations only jumped to from inside the sequence will be ignored.
  while (jump_destination_candidate != jump_usage_counts_.end() &&
         jump_candidate_count == 0) {
    ++jump_destination_candidate;
    jump_candidate_destination = jump_destination_candidate->first;
    jump_candidate_count = jump_destination_candidate->second;
  }

  int preserve_from = start_pc + last_node.SequenceLength();
  if (jump_destination_candidate != jump_usage_counts_.end() &&
      jump_candidate_destination < start_pc + last_node.SequenceLength()) {
    preserve_from = jump_candidate_destination;
    // Check if any jump in the sequence we are preserving has a jump
    // destination inside the optimized sequence before the current position we
    // want to preserve. If so we have to preserve all bytecodes starting at
    // this jump destination.
    for (auto jump_iter = jump_edges_.lower_bound(preserve_from);
         jump_iter != jump_edges_.end() &&
         jump_iter->first /* jump source */ <
             start_pc + last_node.SequenceLength();
         ++jump_iter) {
      int jump_destination = jump_iter->second;
      if (jump_destination > start_pc && jump_destination < preserve_from) {
        preserve_from = jump_destination;
      }
    }

    // We preserve everything to the end of the sequence. This is conservative
    // since it would be enough to preserve all bytecudes up to an unconditional
    // jump.
    int preserve_length = start_pc + last_node.SequenceLength() - preserve_from;
    fixup_length += preserve_length;
    // Jumps after the start of the preserved sequence need fixup.
    AddJumpSourceFixup(fixup_length,
                       start_pc + last_node.SequenceLength() - preserve_length);
    // All jump targets after the start of the optimized sequence need to be
    // fixed relative to the length of the optimized sequence including
    // bytecodes we preserved.
    AddJumpDestinationFixup(fixup_length, start_pc + 1);
    // Jumps to the sequence we preserved need absolute fixup as they could
    // occur before or after the sequence.
    SetJumpDestinationFixup(pc() - preserve_from, preserve_from);
    CopyRangeToOutput(bytecode, preserve_from, preserve_length);
  } else {
    AddJumpDestinationFixup(fixup_length, start_pc + 1);
    // Jumps after the end of the old sequence need fixup.
    AddJumpSourceFixup(fixup_length, start_pc + last_node.SequenceLength());
  }

  // Delete jumps we definitely don't need anymore
  for (int del : delete_jumps) {
    if (del < preserve_from) {
      jump_edges_.erase(del);
    }
  }
}

void RegExpBytecodePeephole::AddJumpSourceFixup(int fixup, int pos) {
  auto previous_fixup = jump_source_fixups_.lower_bound(pos);
  DCHECK(previous_fixup != jump_source_fixups_.end());
  DCHECK(previous_fixup != jump_source_fixups_.begin());

  int previous_fixup_value = (--previous_fixup)->second;
  jump_source_fixups_[pos] = previous_fixup_value + fixup;
}

void RegExpBytecodePeephole::AddJumpDestinationFixup(int fixup, int pos) {
  auto previous_fixup = jump_destination_fixups_.lower_bound(pos);
  DCHECK(previous_fixup != jump_destination_fixups_.end());
  DCHECK(previous_fixup != jump_destination_fixups_.begin());

  int previous_fixup_value = (--previous_fixup)->second;
  jump_destination_fixups_[pos] = previous_fixup_value + fixup;
}

void RegExpBytecodePeephole::SetJumpDestinationFixup(int fixup, int pos) {
  auto previous_fixup = jump_destination_fixups_.lower_bound(pos);
  DCHECK(previous_fixup != jump_destination_fixups_.end());
  DCHECK(previous_fixup != jump_destination_fixups_.begin());

  int previous_fixup_value = (--previous_fixup)->second;
  jump_destination_fixups_.emplace(pos, fixup);
  jump_destination_fixups_.emplace(pos + 1, previous_fixup_value);
}

void RegExpBytecodePeephole::PrepareJumpStructures(
    const ZoneUnorderedMap<int, int>& jump_edges) {
  for (auto jump_edge : jump_edges) {
    int jump_source = jump_edge.first;
    int jump_destination = jump_edge.second;

    jump_edges_.emplace(jump_source, jump_destination);
    jump_usage_counts_[jump_destination]++;
  }
}

void RegExpBytecodePeephole::FixJumps() {
  int position_fixup = 0;
  // Next position where fixup changes.
  auto next_source_fixup = jump_source_fixups_.lower_bound(0);
  int next_source_fixup_offset = next_source_fixup->first;
  int next_source_fixup_value = next_source_fixup->second;

  for (auto jump_edge : jump_edges_) {
    int jump_source = jump_edge.first;
    int jump_destination = jump_edge.second;
    while (jump_source >= next_source_fixup_offset) {
      position_fixup = next_source_fixup_value;
      ++next_source_fixup;
      next_source_fixup_offset = next_source_fixup->first;
      next_source_fixup_value = next_source_fixup->second;
    }
    jump_source += position_fixup;

    FixJump(jump_source, jump_destination);
  }

  // Mapped jump edges don't need source fixups, as the position already is an
  // offset in the new bytecode.
  for (auto jump_edge : jump_edges_mapped_) {
    int jump_source = jump_edge.first;
    int jump_destination = jump_edge.second;

    FixJump(jump_source, jump_destination);
  }
}

void RegExpBytecodePeephole::FixJump(int jump_source, int jump_destination) {
  int fixed_jump_destination =
      jump_destination +
      (--jump_destination_fixups_.upper_bound(jump_destination))->second;
  DCHECK_LT(fixed_jump_destination, Length());
#ifdef DEBUG
  // TODO(pthier): This check could be better if we track the bytecodes
  // actually used and check if we jump to one of them.
  uint8_t jump_bc = optimized_bytecode_buffer_[fixed_jump_destination];
  DCHECK_GT(jump_bc, 0);
  DCHECK_LT(jump_bc, kRegExpBytecodeCount);
#endif

  if (jump_destination != fixed_jump_destination) {
    OverwriteValue<uint32_t>(jump_source, fixed_jump_destination);
  }
}

void RegExpBytecodePeephole::AddSentinelFixups(int pos) {
  jump_source_fixups_.emplace(pos, 0);
  jump_destination_fixups_.emplace(pos, 0);
}

template <typename T>
void RegExpBytecodePeephole::EmitValue(T value) {
  DCHECK(optimized_bytecode_buffer_.begin() + pc() ==
         optimized_bytecode_buffer_.end());
  uint8_t* value_byte_iter = reinterpret_cast<uint8_t*>(&value);
  optimized_bytecode_buffer_.insert(optimized_bytecode_buffer_.end(),
                                    value_byte_iter,
                                    value_byte_iter + sizeof(T));
}

template <typename T>
void RegExpBytecodePeephole::OverwriteValue(int offset, T value) {
  uint8_t* value_byte_iter = reinterpret_cast<uint8_t*>(&value);
  uint8_t* value_byte_iter_end = value_byte_iter + sizeof(T);
  while (value_byte_iter < value_byte_iter_end) {
    optimized_bytecode_buffer_[offset++] = *value_byte_iter++;
  }
}

void RegExpBytecodePeephole::CopyRangeToOutput(const uint8_t* orig_bytecode,
                                               int start, int length) {
  DCHECK(optimized_bytecode_buffer_.begin() + pc() ==
         optimized_bytecode_buffer_.end());
  optimized_bytecode_buffer_.insert(optimized_bytecode_buffer_.end(),
                                    orig_bytecode + start,
                                    orig_bytecode + start + length);
}

void RegExpBytecodePeephole::SetRange(uint8_t value, int count) {
  DCHECK(optimized_bytecode_buffer_.begin() + pc() ==
         optimized_bytecode_buffer_.end());
  optimized_bytecode_buffer_.insert(optimized_bytecode_buffer_.end(), count,
                                    value);
}

void RegExpBytecodePeephole::EmitArgument(int start_pc, const uint8_t* bytecode,
                                          BytecodeArgumentMapping arg) {
  int arg_pos = start_pc + arg.offset;
  switch (arg.length) {
    case 1:
      DCHECK_EQ(arg.new_length, arg.length);
      EmitValue(GetValue<uint8_t>(bytecode, arg_pos));
      break;
    case 2:
      DCHECK_EQ(arg.new_length, arg.length);
      EmitValue(GetValue<uint16_t>(bytecode, arg_pos));
      break;
    case 3: {
      // Length 3 only occurs in 'packed' arguments where the lowermost byte is
      // the current bytecode, and the remaining 3 bytes are the packed value.
      //
      // We load 4 bytes from position - 1 and shift out the bytecode.
#ifdef V8_TARGET_BIG_ENDIAN
      UNIMPLEMENTED();
      int32_t val = 0;
#else
      int32_t val = GetValue<int32_t>(bytecode, arg_pos - 1) >> kBitsPerByte;
#endif  // V8_TARGET_BIG_ENDIAN

      switch (arg.new_length) {
        case 2:
          EmitValue<uint16_t>(val);
          break;
        case 3: {
          // Pack with previously emitted value.
          auto prev_val =
              GetValue<int32_t>(&(*optimized_bytecode_buffer_.begin()),
                                Length() - sizeof(uint32_t));
#ifdef V8_TARGET_BIG_ENDIAN
      UNIMPLEMENTED();
      USE(prev_val);
#else
          DCHECK_EQ(prev_val & 0xFFFFFF00, 0);
          OverwriteValue<uint32_t>(
              pc() - sizeof(uint32_t),
              (static_cast<uint32_t>(val) << 8) | (prev_val & 0xFF));
#endif  // V8_TARGET_BIG_ENDIAN
          break;
        }
        case 4:
          EmitValue<uint32_t>(val);
          break;
      }
      break;
    }
    case 4:
      DCHECK_EQ(arg.new_length, arg.length);
      EmitValue(GetValue<uint32_t>(bytecode, arg_pos));
      break;
    case 8:
      DCHECK_EQ(arg.new_length, arg.length);
      EmitValue(GetValue<uint64_t>(bytecode, arg_pos));
      break;
    default:
      CopyRangeToOutput(bytecode, arg_pos,
                        std::min(arg.length, arg.new_length));
      if (arg.length < arg.new_length) {
        SetRange(0x00, arg.new_length - arg.length);
      }
      break;
  }
}

int RegExpBytecodePeephole::pc() const {
  DCHECK_LE(optimized_bytecode_buffer_.size(), std::numeric_limits<int>::max());
  return static_cast<int>(optimized_bytecode_buffer_.size());
}

Zone* RegExpBytecodePeephole::zone() const { return zone_; }

}  // namespace

// static
Handle<ByteArray> RegExpBytecodePeepholeOptimization::OptimizeBytecode(
    Isolate* isolate, Zone* zone, Handle<String> source,
    const uint8_t* bytecode, int length,
    const ZoneUnorderedMap<int, int>& jump_edges) {
  RegExpBytecodePeephole peephole(zone, length, jump_edges);
  bool did_optimize = peephole.OptimizeBytecode(bytecode, length);
  Handle<ByteArray> array = isolate->factory()->NewByteArray(peephole.Length());
  peephole.CopyOptimizedBytecode(array->begin());

  if (did_optimize && v8_flags.trace_regexp_peephole_optimization) {
    PrintF("Original Bytecode:\n");
    RegExpBytecodeDisassemble(bytecode, length, source->ToCString().get());
    PrintF("Optimized Bytecode:\n");
    RegExpBytecodeDisassemble(array->begin(), peephole.Length(),
                              source->ToCString().get());
  }

  return array;
}

}  // namespace internal
}  // namespace v8
