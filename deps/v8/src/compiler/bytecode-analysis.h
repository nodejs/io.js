// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_COMPILER_BYTECODE_ANALYSIS_H_
#define V8_COMPILER_BYTECODE_ANALYSIS_H_

#include "src/compiler/bytecode-liveness-map.h"
#include "src/handles/handles.h"
#include "src/interpreter/bytecode-register.h"
#include "src/utils/bit-vector.h"
#include "src/utils/utils.h"
#include "src/zone/zone-containers.h"

namespace v8 {
namespace internal {

class BytecodeArray;

namespace compiler {

class V8_EXPORT_PRIVATE BytecodeLoopAssignments {
 public:
  BytecodeLoopAssignments(int parameter_count, int register_count, Zone* zone);

  void Add(interpreter::Register r);
  void AddList(interpreter::Register r, uint32_t count);
  void Union(const BytecodeLoopAssignments& other);

  bool ContainsParameter(int index) const;
  bool ContainsLocal(int index) const;

  int parameter_count() const { return parameter_count_; }
  int local_count() const { return bit_vector_->length() - parameter_count_; }

 private:
  int const parameter_count_;
  BitVector* const bit_vector_;
};

// Jump targets for resuming a suspended generator.
class V8_EXPORT_PRIVATE ResumeJumpTarget {
 public:
  // Create a resume jump target representing an actual resume.
  static ResumeJumpTarget Leaf(int suspend_id, int target_offset);

  // Create a resume jump target at a loop header, which will have another
  // resume jump after the loop header is crossed.
  static ResumeJumpTarget AtLoopHeader(int loop_header_offset,
                                       const ResumeJumpTarget& next);

  int suspend_id() const { return suspend_id_; }
  int target_offset() const { return target_offset_; }
  bool is_leaf() const { return target_offset_ == final_target_offset_; }

 private:
  // The suspend id of the resume.
  int suspend_id_;
  // The target offset of this resume jump.
  int target_offset_;
  // The final offset of this resume, which may be across multiple jumps.
  int final_target_offset_;

  ResumeJumpTarget(int suspend_id, int target_offset, int final_target_offset);
};

struct V8_EXPORT_PRIVATE LoopInfo {
 public:
  LoopInfo(int parent_offset, int loop_start, int loop_end, int parameter_count,
           int register_count, Zone* zone)
      : parent_offset_(parent_offset),
        loop_start_(loop_start),
        loop_end_(loop_end),
        assignments_(parameter_count, register_count, zone),
        resume_jump_targets_(zone) {}

  int parent_offset() const { return parent_offset_; }
  int loop_start() const { return loop_start_; }
  int loop_end() const { return loop_end_; }
  bool resumable() const { return resumable_; }
  void mark_resumable() { resumable_ = true; }
  bool innermost() const { return innermost_; }
  void mark_not_innermost() { innermost_ = false; }
  bool trivial() const { return trivial_; }
  void mark_non_trivial() { trivial_ = false; }

  bool Contains(int offset) const {
    return offset >= loop_start_ && offset < loop_end_;
  }

  const ZoneVector<ResumeJumpTarget>& resume_jump_targets() const {
    return resume_jump_targets_;
  }
  void AddResumeTarget(const ResumeJumpTarget& target) {
    resume_jump_targets_.push_back(target);
  }

  BytecodeLoopAssignments& assignments() { return assignments_; }
  const BytecodeLoopAssignments& assignments() const { return assignments_; }

 private:
  // The offset to the parent loop, or -1 if there is no parent.
  int parent_offset_;
  int loop_start_;
  int loop_end_;
  bool resumable_ = false;
  bool innermost_ = true;
  bool trivial_ = true;
  BytecodeLoopAssignments assignments_;
  ZoneVector<ResumeJumpTarget> resume_jump_targets_;
};

// Analyze the bytecodes to find the loop ranges, loop nesting, loop assignments
// and liveness.  NOTE: The broker/serializer relies on the fact that an
// analysis for OSR (osr_bailout_id is not None) subsumes an analysis for
// non-OSR (osr_bailout_id is None).
class V8_EXPORT_PRIVATE BytecodeAnalysis : public ZoneObject {
 public:
  BytecodeAnalysis(Handle<BytecodeArray> bytecode_array, Zone* zone,
                   BytecodeOffset osr_bailout_id, bool analyze_liveness);
  BytecodeAnalysis(const BytecodeAnalysis&) = delete;
  BytecodeAnalysis& operator=(const BytecodeAnalysis&) = delete;

  // Return true if the given offset is a loop header
  bool IsLoopHeader(int offset) const;
  // Get the loop header offset of the containing loop for arbitrary
  // {offset}, or -1 if the {offset} is not inside any loop.
  int GetLoopOffsetFor(int offset) const;
  // Get the loop end offset given the header offset of an innermost loop
  int GetLoopEndOffsetForInnermost(int header_offset) const;
  // Get the loop info of the loop header at {header_offset}.
  const LoopInfo& GetLoopInfoFor(int header_offset) const;
  // Try to get the loop info of the loop header at {header_offset}, returning
  // null if there isn't any.
  const LoopInfo* TryGetLoopInfoFor(int header_offset) const;

  const ZoneMap<int, LoopInfo>& GetLoopInfos() const { return header_to_info_; }

  // Get the top-level resume jump targets.
  const ZoneVector<ResumeJumpTarget>& resume_jump_targets() const {
    return resume_jump_targets_;
  }

  // Gets the in-/out-liveness for the bytecode at {offset}.
  const BytecodeLivenessState* GetInLivenessFor(int offset) const;
  const BytecodeLivenessState* GetOutLivenessFor(int offset) const;

  // In the case of OSR, the analysis also computes the (bytecode offset of the)
  // OSR entry point from the {osr_bailout_id} that was given to the
  // constructor.
  int osr_entry_point() const {
    CHECK_LE(0, osr_entry_point_);
    return osr_entry_point_;
  }
  // Return the osr_bailout_id (for verification purposes).
  BytecodeOffset osr_bailout_id() const { return osr_bailout_id_; }

  // Return whether liveness analysis was performed (for verification purposes).
  bool liveness_analyzed() const { return analyze_liveness_; }

  // Return the number of bytecodes (i.e. the number of bytecode operations, as
  // opposed to the number of bytes in the bytecode).
  int bytecode_count() const { return bytecode_count_; }

 private:
  struct LoopStackEntry {
    int header_offset;
    LoopInfo* loop_info;
  };

  void Analyze();
  void PushLoop(int loop_header, int loop_end);

#if DEBUG
  bool ResumeJumpTargetsAreValid();
  bool ResumeJumpTargetLeavesResolveSuspendIds(
      int parent_offset,
      const ZoneVector<ResumeJumpTarget>& resume_jump_targets,
      std::map<int, int>* unresolved_suspend_ids);

  bool LivenessIsValid();
#endif

  Zone* zone() const { return zone_; }
  Handle<BytecodeArray> bytecode_array() const { return bytecode_array_; }
  BytecodeLivenessMap& liveness_map() {
    DCHECK(analyze_liveness_);
    return *liveness_map_;
  }
  const BytecodeLivenessMap& liveness_map() const {
    DCHECK(analyze_liveness_);
    return *liveness_map_;
  }

  std::ostream& PrintLivenessTo(std::ostream& os) const;

  Handle<BytecodeArray> const bytecode_array_;
  Zone* const zone_;
  BytecodeOffset const osr_bailout_id_;
  bool const analyze_liveness_;
  ZoneStack<LoopStackEntry> loop_stack_;
  ZoneVector<int> loop_end_index_queue_;
  ZoneVector<ResumeJumpTarget> resume_jump_targets_;
  ZoneMap<int, int> end_to_header_;
  ZoneMap<int, LoopInfo> header_to_info_;
  int osr_entry_point_;
  base::Optional<BytecodeLivenessMap> liveness_map_;
  int bytecode_count_;
};

}  // namespace compiler
}  // namespace internal
}  // namespace v8

#endif  // V8_COMPILER_BYTECODE_ANALYSIS_H_
