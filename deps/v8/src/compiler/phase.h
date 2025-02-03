// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_COMPILER_PHASE_H_
#define V8_COMPILER_PHASE_H_

#include "src/compiler/backend/code-generator.h"
#include "src/logging/runtime-call-stats.h"

#ifdef V8_RUNTIME_CALL_STATS
#define DECL_PIPELINE_PHASE_CONSTANTS_HELPER(Name, Kind, Mode)  \
  static constexpr PhaseKind kKind = Kind;                      \
  static const char* phase_name() { return "V8.TF" #Name; }     \
  static constexpr RuntimeCallCounterId kRuntimeCallCounterId = \
      RuntimeCallCounterId::kOptimize##Name;                    \
  static constexpr RuntimeCallStats::CounterMode kCounterMode = Mode;
#else  // V8_RUNTIME_CALL_STATS
#define DECL_PIPELINE_PHASE_CONSTANTS_HELPER(Name, Kind, Mode) \
  static constexpr PhaseKind kKind = Kind;                     \
  static const char* phase_name() { return "V8.TF" #Name; }
#endif  // V8_RUNTIME_CALL_STATS

#define DECL_PIPELINE_PHASE_CONSTANTS(Name)                        \
  DECL_PIPELINE_PHASE_CONSTANTS_HELPER(Name, PhaseKind::kTurbofan, \
                                       RuntimeCallStats::kThreadSpecific)

#define DECL_MAIN_THREAD_PIPELINE_PHASE_CONSTANTS(Name)            \
  DECL_PIPELINE_PHASE_CONSTANTS_HELPER(Name, PhaseKind::kTurbofan, \
                                       RuntimeCallStats::kExact)

namespace v8::internal {

class OptimizedCompilationInfo;

namespace compiler {

inline constexpr char kCodegenZoneName[] = "codegen-zone";
inline constexpr char kGraphZoneName[] = "graph-zone";
inline constexpr char kInstructionZoneName[] = "instruction-zone";
inline constexpr char kRegisterAllocationZoneName[] =
    "register-allocation-zone";
inline constexpr char kRegisterAllocatorVerifierZoneName[] =
    "register-allocator-verifier-zone";

class TFPipelineData;
class Schedule;
void PrintCode(Isolate* isolate, DirectHandle<Code> code,
               OptimizedCompilationInfo* info);
void TraceSchedule(OptimizedCompilationInfo* info, TFPipelineData* data,
                   Schedule* schedule, const char* phase_name);

enum class PhaseKind {
  kTurbofan,
  kTurboshaft,
};

struct InstructionStartsAsJSON {
  const ZoneVector<TurbolizerInstructionStartInfo>* instr_starts;
};

inline std::ostream& operator<<(std::ostream& out, InstructionStartsAsJSON s) {
  out << ", \"instructionOffsetToPCOffset\": {";
  bool needs_comma = false;
  for (size_t i = 0; i < s.instr_starts->size(); ++i) {
    if (needs_comma) out << ", ";
    const TurbolizerInstructionStartInfo& info = (*s.instr_starts)[i];
    out << "\"" << i << "\": {";
    out << "\"gap\": " << info.gap_pc_offset;
    out << ", \"arch\": " << info.arch_instr_pc_offset;
    out << ", \"condition\": " << info.condition_pc_offset;
    out << "}";
    needs_comma = true;
  }
  out << "}";
  return out;
}

struct TurbolizerCodeOffsetsInfoAsJSON {
  const TurbolizerCodeOffsetsInfo* offsets_info;
};

inline std::ostream& operator<<(std::ostream& out,
                                TurbolizerCodeOffsetsInfoAsJSON s) {
  out << ", \"codeOffsetsInfo\": {";
  out << "\"codeStartRegisterCheck\": "
      << s.offsets_info->code_start_register_check << ", ";
  out << "\"deoptCheck\": " << s.offsets_info->deopt_check << ", ";
  out << "\"blocksStart\": " << s.offsets_info->blocks_start << ", ";
  out << "\"outOfLineCode\": " << s.offsets_info->out_of_line_code << ", ";
  out << "\"deoptimizationExits\": " << s.offsets_info->deoptimization_exits
      << ", ";
  out << "\"pools\": " << s.offsets_info->pools << ", ";
  out << "\"jumpTables\": " << s.offsets_info->jump_tables;
  out << "}";
  return out;
}

struct BlockStartsAsJSON {
  const ZoneVector<int>* block_starts;
};

inline std::ostream& operator<<(std::ostream& out, BlockStartsAsJSON s) {
  out << ", \"blockIdToOffset\": {";
  bool needs_comma = false;
  for (size_t i = 0; i < s.block_starts->size(); ++i) {
    if (needs_comma) out << ", ";
    int offset = (*s.block_starts)[i];
    out << "\"" << i << "\":" << offset;
    needs_comma = true;
  }
  out << "},";
  return out;
}

}  // namespace compiler
}  // namespace v8::internal

#endif  // V8_COMPILER_PHASE_H_
