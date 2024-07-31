// Copyright 2011 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/execution/mips64/simulator-mips64.h"

// Only build the simulator if not compiling for real MIPS hardware.
#if defined(USE_SIMULATOR)

#include <limits.h>
#include <stdarg.h>
#include <stdlib.h>

#include <cmath>

#include "src/base/bits.h"
#include "src/base/platform/memory.h"
#include "src/base/platform/platform.h"
#include "src/base/strings.h"
#include "src/base/vector.h"
#include "src/codegen/assembler-inl.h"
#include "src/codegen/macro-assembler.h"
#include "src/codegen/mips64/constants-mips64.h"
#include "src/diagnostics/disasm.h"
#include "src/heap/combined-heap.h"
#include "src/runtime/runtime-utils.h"
#include "src/utils/ostreams.h"

namespace v8 {
namespace internal {

DEFINE_LAZY_LEAKY_OBJECT_GETTER(Simulator::GlobalMonitor,
                                Simulator::GlobalMonitor::Get)

// Util functions.
inline bool HaveSameSign(int64_t a, int64_t b) { return ((a ^ b) >= 0); }

uint32_t get_fcsr_condition_bit(uint32_t cc) {
  if (cc == 0) {
    return 23;
  } else {
    return 24 + cc;
  }
}

// This macro provides a platform independent use of sscanf. The reason for
// SScanF not being implemented in a platform independent was through
// ::v8::internal::OS in the same way as base::SNPrintF is that the Windows C
// Run-Time Library does not provide vsscanf.
#define SScanF sscanf

// The MipsDebugger class is used by the simulator while debugging simulated
// code.
class MipsDebugger {
 public:
  explicit MipsDebugger(Simulator* sim) : sim_(sim) {}

  void Stop(Instruction* instr);
  void Debug();
  // Print all registers with a nice formatting.
  void PrintAllRegs();
  void PrintAllRegsIncludingFPU();

 private:
  // We set the breakpoint code to 0xFFFFF to easily recognize it.
  static const Instr kBreakpointInstr = SPECIAL | BREAK | 0xFFFFF << 6;
  static const Instr kNopInstr = 0x0;

  Simulator* sim_;

  int64_t GetRegisterValue(int regnum);
  int64_t GetFPURegisterValue(int regnum);
  float GetFPURegisterValueFloat(int regnum);
  double GetFPURegisterValueDouble(int regnum);
  bool GetValue(const char* desc, int64_t* value);

  // Set or delete a breakpoint. Returns true if successful.
  bool SetBreakpoint(Instruction* breakpc);
  bool DeleteBreakpoint(Instruction* breakpc);

  // Undo and redo all breakpoints. This is needed to bracket disassembly and
  // execution to skip past breakpoints when run from the debugger.
  void UndoBreakpoints();
  void RedoBreakpoints();
};

inline void UNSUPPORTED() { printf("Sim: Unsupported instruction.\n"); }

void MipsDebugger::Stop(Instruction* instr) {
  // Get the stop code.
  uint32_t code = instr->Bits(25, 6);
  PrintF("Simulator hit (%u)\n", code);
  Debug();
}

int64_t MipsDebugger::GetRegisterValue(int regnum) {
  if (regnum == kNumSimuRegisters) {
    return sim_->get_pc();
  } else {
    return sim_->get_register(regnum);
  }
}

int64_t MipsDebugger::GetFPURegisterValue(int regnum) {
  if (regnum == kNumFPURegisters) {
    return sim_->get_pc();
  } else {
    return sim_->get_fpu_register(regnum);
  }
}

float MipsDebugger::GetFPURegisterValueFloat(int regnum) {
  if (regnum == kNumFPURegisters) {
    return sim_->get_pc();
  } else {
    return sim_->get_fpu_register_float(regnum);
  }
}

double MipsDebugger::GetFPURegisterValueDouble(int regnum) {
  if (regnum == kNumFPURegisters) {
    return sim_->get_pc();
  } else {
    return sim_->get_fpu_register_double(regnum);
  }
}

bool MipsDebugger::GetValue(const char* desc, int64_t* value) {
  int regnum = Registers::Number(desc);
  int fpuregnum = FPURegisters::Number(desc);

  if (regnum != kInvalidRegister) {
    *value = GetRegisterValue(regnum);
    return true;
  } else if (fpuregnum != kInvalidFPURegister) {
    *value = GetFPURegisterValue(fpuregnum);
    return true;
  } else if (strncmp(desc, "0x", 2) == 0) {
    return SScanF(desc + 2, "%" SCNx64, reinterpret_cast<uint64_t*>(value)) ==
           1;
  } else {
    return SScanF(desc, "%" SCNu64, reinterpret_cast<uint64_t*>(value)) == 1;
  }
}

bool MipsDebugger::SetBreakpoint(Instruction* breakpc) {
  // Check if a breakpoint can be set. If not return without any side-effects.
  if (sim_->break_pc_ != nullptr) {
    return false;
  }

  // Set the breakpoint.
  sim_->break_pc_ = breakpc;
  sim_->break_instr_ = breakpc->InstructionBits();
  // Not setting the breakpoint instruction in the code itself. It will be set
  // when the debugger shell continues.
  return true;
}

bool MipsDebugger::DeleteBreakpoint(Instruction* breakpc) {
  if (sim_->break_pc_ != nullptr) {
    sim_->break_pc_->SetInstructionBits(sim_->break_instr_);
  }

  sim_->break_pc_ = nullptr;
  sim_->break_instr_ = 0;
  return true;
}

void MipsDebugger::UndoBreakpoints() {
  if (sim_->break_pc_ != nullptr) {
    sim_->break_pc_->SetInstructionBits(sim_->break_instr_);
  }
}

void MipsDebugger::RedoBreakpoints() {
  if (sim_->break_pc_ != nullptr) {
    sim_->break_pc_->SetInstructionBits(kBreakpointInstr);
  }
}

void MipsDebugger::PrintAllRegs() {
#define REG_INFO(n) Registers::Name(n), GetRegisterValue(n), GetRegisterValue(n)

  PrintF("\n");
  // at, v0, a0.
  PrintF("%3s: 0x%016" PRIx64 " %14" PRId64 "\t%3s: 0x%016" PRIx64 " %14" PRId64
         "\t%3s: 0x%016" PRIx64 " %14" PRId64 "\n",
         REG_INFO(1), REG_INFO(2), REG_INFO(4));
  // v1, a1.
  PrintF("%34s\t%3s: 0x%016" PRIx64 "  %14" PRId64 " \t%3s: 0x%016" PRIx64
         "  %14" PRId64 " \n",
         "", REG_INFO(3), REG_INFO(5));
  // a2.
  PrintF("%34s\t%34s\t%3s: 0x%016" PRIx64 "  %14" PRId64 " \n", "", "",
         REG_INFO(6));
  // a3.
  PrintF("%34s\t%34s\t%3s: 0x%016" PRIx64 "  %14" PRId64 " \n", "", "",
         REG_INFO(7));
  PrintF("\n");
  // a4-t3, s0-s7
  for (int i = 0; i < 8; i++) {
    PrintF("%3s: 0x%016" PRIx64 "  %14" PRId64 " \t%3s: 0x%016" PRIx64
           "  %14" PRId64 " \n",
           REG_INFO(8 + i), REG_INFO(16 + i));
  }
  PrintF("\n");
  // t8, k0, LO.
  PrintF("%3s: 0x%016" PRIx64 "  %14" PRId64 " \t%3s: 0x%016" PRIx64
         "  %14" PRId64 " \t%3s: 0x%016" PRIx64 "  %14" PRId64 " \n",
         REG_INFO(24), REG_INFO(26), REG_INFO(32));
  // t9, k1, HI.
  PrintF("%3s: 0x%016" PRIx64 "  %14" PRId64 " \t%3s: 0x%016" PRIx64
         "  %14" PRId64 " \t%3s: 0x%016" PRIx64 "  %14" PRId64 " \n",
         REG_INFO(25), REG_INFO(27), REG_INFO(33));
  // sp, fp, gp.
  PrintF("%3s: 0x%016" PRIx64 "  %14" PRId64 " \t%3s: 0x%016" PRIx64
         "  %14" PRId64 " \t%3s: 0x%016" PRIx64 "  %14" PRId64 " \n",
         REG_INFO(29), REG_INFO(30), REG_INFO(28));
  // pc.
  PrintF("%3s: 0x%016" PRIx64 "  %14" PRId64 " \t%3s: 0x%016" PRIx64
         "  %14" PRId64 " \n",
         REG_INFO(31), REG_INFO(34));

#undef REG_INFO
}

void MipsDebugger::PrintAllRegsIncludingFPU() {
#define FPU_REG_INFO(n) \
  FPURegisters::Name(n), GetFPURegisterValue(n), GetFPURegisterValueDouble(n)

  PrintAllRegs();

  PrintF("\n\n");
  // f0, f1, f2, ... f31.
  // TODO(plind): consider printing 2 columns for space efficiency.
  PrintF("%3s: 0x%016" PRIx64 "  %16.4e\n", FPU_REG_INFO(0));
  PrintF("%3s: 0x%016" PRIx64 "  %16.4e\n", FPU_REG_INFO(1));
  PrintF("%3s: 0x%016" PRIx64 "  %16.4e\n", FPU_REG_INFO(2));
  PrintF("%3s: 0x%016" PRIx64 "  %16.4e\n", FPU_REG_INFO(3));
  PrintF("%3s: 0x%016" PRIx64 "  %16.4e\n", FPU_REG_INFO(4));
  PrintF("%3s: 0x%016" PRIx64 "  %16.4e\n", FPU_REG_INFO(5));
  PrintF("%3s: 0x%016" PRIx64 "  %16.4e\n", FPU_REG_INFO(6));
  PrintF("%3s: 0x%016" PRIx64 "  %16.4e\n", FPU_REG_INFO(7));
  PrintF("%3s: 0x%016" PRIx64 "  %16.4e\n", FPU_REG_INFO(8));
  PrintF("%3s: 0x%016" PRIx64 "  %16.4e\n", FPU_REG_INFO(9));
  PrintF("%3s: 0x%016" PRIx64 "  %16.4e\n", FPU_REG_INFO(10));
  PrintF("%3s: 0x%016" PRIx64 "  %16.4e\n", FPU_REG_INFO(11));
  PrintF("%3s: 0x%016" PRIx64 "  %16.4e\n", FPU_REG_INFO(12));
  PrintF("%3s: 0x%016" PRIx64 "  %16.4e\n", FPU_REG_INFO(13));
  PrintF("%3s: 0x%016" PRIx64 "  %16.4e\n", FPU_REG_INFO(14));
  PrintF("%3s: 0x%016" PRIx64 "  %16.4e\n", FPU_REG_INFO(15));
  PrintF("%3s: 0x%016" PRIx64 "  %16.4e\n", FPU_REG_INFO(16));
  PrintF("%3s: 0x%016" PRIx64 "  %16.4e\n", FPU_REG_INFO(17));
  PrintF("%3s: 0x%016" PRIx64 "  %16.4e\n", FPU_REG_INFO(18));
  PrintF("%3s: 0x%016" PRIx64 "  %16.4e\n", FPU_REG_INFO(19));
  PrintF("%3s: 0x%016" PRIx64 "  %16.4e\n", FPU_REG_INFO(20));
  PrintF("%3s: 0x%016" PRIx64 "  %16.4e\n", FPU_REG_INFO(21));
  PrintF("%3s: 0x%016" PRIx64 "  %16.4e\n", FPU_REG_INFO(22));
  PrintF("%3s: 0x%016" PRIx64 "  %16.4e\n", FPU_REG_INFO(23));
  PrintF("%3s: 0x%016" PRIx64 "  %16.4e\n", FPU_REG_INFO(24));
  PrintF("%3s: 0x%016" PRIx64 "  %16.4e\n", FPU_REG_INFO(25));
  PrintF("%3s: 0x%016" PRIx64 "  %16.4e\n", FPU_REG_INFO(26));
  PrintF("%3s: 0x%016" PRIx64 "  %16.4e\n", FPU_REG_INFO(27));
  PrintF("%3s: 0x%016" PRIx64 "  %16.4e\n", FPU_REG_INFO(28));
  PrintF("%3s: 0x%016" PRIx64 "  %16.4e\n", FPU_REG_INFO(29));
  PrintF("%3s: 0x%016" PRIx64 "  %16.4e\n", FPU_REG_INFO(30));
  PrintF("%3s: 0x%016" PRIx64 "  %16.4e\n", FPU_REG_INFO(31));

#undef FPU_REG_INFO
}

void MipsDebugger::Debug() {
  if (v8_flags.correctness_fuzzer_suppressions) {
    PrintF("Debugger disabled for differential fuzzing.\n");
    return;
  }
  intptr_t last_pc = -1;
  bool done = false;

#define COMMAND_SIZE 63
#define ARG_SIZE 255

#define STR(a) #a
#define XSTR(a) STR(a)

  char cmd[COMMAND_SIZE + 1];
  char arg1[ARG_SIZE + 1];
  char arg2[ARG_SIZE + 1];
  char* argv[3] = {cmd, arg1, arg2};

  // Make sure to have a proper terminating character if reaching the limit.
  cmd[COMMAND_SIZE] = 0;
  arg1[ARG_SIZE] = 0;
  arg2[ARG_SIZE] = 0;

  // Undo all set breakpoints while running in the debugger shell. This will
  // make them invisible to all commands.
  UndoBreakpoints();

  while (!done && (sim_->get_pc() != Simulator::end_sim_pc)) {
    if (last_pc != sim_->get_pc()) {
      disasm::NameConverter converter;
      disasm::Disassembler dasm(converter);
      // Use a reasonably large buffer.
      v8::base::EmbeddedVector<char, 256> buffer;
      dasm.InstructionDecode(buffer,
                             reinterpret_cast<uint8_t*>(sim_->get_pc()));
      PrintF("  0x%016" PRIx64 "   %s\n", sim_->get_pc(), buffer.begin());
      last_pc = sim_->get_pc();
    }
    char* line = ReadLine("sim> ");
    if (line == nullptr) {
      break;
    } else {
      char* last_input = sim_->last_debugger_input();
      if (strcmp(line, "\n") == 0 && last_input != nullptr) {
        line = last_input;
      } else {
        // Ownership is transferred to sim_;
        sim_->set_last_debugger_input(line);
      }
      // Use sscanf to parse the individual parts of the command line. At the
      // moment no command expects more than two parameters.
      int argc = SScanF(line,
                        "%" XSTR(COMMAND_SIZE) "s "
                        "%" XSTR(ARG_SIZE) "s "
                        "%" XSTR(ARG_SIZE) "s",
                        cmd, arg1, arg2);
      if ((strcmp(cmd, "si") == 0) || (strcmp(cmd, "stepi") == 0)) {
        Instruction* instr = reinterpret_cast<Instruction*>(sim_->get_pc());
        if (!(instr->IsTrap()) ||
            instr->InstructionBits() == rtCallRedirInstr) {
          sim_->InstructionDecode(
              reinterpret_cast<Instruction*>(sim_->get_pc()));
        } else {
          // Allow si to jump over generated breakpoints.
          PrintF("/!\\ Jumping over generated breakpoint.\n");
          sim_->set_pc(sim_->get_pc() + kInstrSize);
        }
      } else if ((strcmp(cmd, "c") == 0) || (strcmp(cmd, "cont") == 0)) {
        // Execute the one instruction we broke at with breakpoints disabled.
        sim_->InstructionDecode(reinterpret_cast<Instruction*>(sim_->get_pc()));
        // Leave the debugger shell.
        done = true;
      } else if ((strcmp(cmd, "p") == 0) || (strcmp(cmd, "print") == 0)) {
        if (argc == 2) {
          int64_t value;
          double dvalue;
          if (strcmp(arg1, "all") == 0) {
            PrintAllRegs();
          } else if (strcmp(arg1, "allf") == 0) {
            PrintAllRegsIncludingFPU();
          } else {
            int regnum = Registers::Number(arg1);
            int fpuregnum = FPURegisters::Number(arg1);

            if (regnum != kInvalidRegister) {
              value = GetRegisterValue(regnum);
              PrintF("%s: 0x%08" PRIx64 "  %" PRId64 "  \n", arg1, value,
                     value);
            } else if (fpuregnum != kInvalidFPURegister) {
              value = GetFPURegisterValue(fpuregnum);
              dvalue = GetFPURegisterValueDouble(fpuregnum);
              PrintF("%3s: 0x%016" PRIx64 "  %16.4e\n",
                     FPURegisters::Name(fpuregnum), value, dvalue);
            } else {
              PrintF("%s unrecognized\n", arg1);
            }
          }
        } else {
          if (argc == 3) {
            if (strcmp(arg2, "single") == 0) {
              int64_t value;
              float fvalue;
              int fpuregnum = FPURegisters::Number(arg1);

              if (fpuregnum != kInvalidFPURegister) {
                value = GetFPURegisterValue(fpuregnum);
                value &= 0xFFFFFFFFUL;
                fvalue = GetFPURegisterValueFloat(fpuregnum);
                PrintF("%s: 0x%08" PRIx64 "  %11.4e\n", arg1, value, fvalue);
              } else {
                PrintF("%s unrecognized\n", arg1);
              }
            } else {
              PrintF("print <fpu register> single\n");
            }
          } else {
            PrintF("print <register> or print <fpu register> single\n");
          }
        }
      } else if ((strcmp(cmd, "po") == 0) ||
                 (strcmp(cmd, "printobject") == 0)) {
        if (argc == 2) {
          int64_t value;
          StdoutStream os;
          if (GetValue(arg1, &value)) {
            Tagged<Object> obj(value);
            os << arg1 << ": \n";
#ifdef DEBUG
            Print(obj, os);
            os << "\n";
#else
            os << Brief(obj) << "\n";
#endif
          } else {
            os << arg1 << " unrecognized\n";
          }
        } else {
          PrintF("printobject <value>\n");
        }
      } else if (strcmp(cmd, "stack") == 0 || strcmp(cmd, "mem") == 0 ||
                 strcmp(cmd, "dump") == 0) {
        int64_t* cur = nullptr;
        int64_t* end = nullptr;
        int next_arg = 1;

        if (strcmp(cmd, "stack") == 0) {
          cur = reinterpret_cast<int64_t*>(sim_->get_register(Simulator::sp));
        } else {  // Command "mem".
          int64_t value;
          if (!GetValue(arg1, &value)) {
            PrintF("%s unrecognized\n", arg1);
            continue;
          }
          cur = reinterpret_cast<int64_t*>(value);
          next_arg++;
        }

        int64_t words;
        if (argc == next_arg) {
          words = 10;
        } else {
          if (!GetValue(argv[next_arg], &words)) {
            words = 10;
          }
        }
        end = cur + words;

        bool skip_obj_print = (strcmp(cmd, "dump") == 0);
        while (cur < end) {
          PrintF("  0x%012" PRIxPTR " :  0x%016" PRIx64 "  %14" PRId64 " ",
                 reinterpret_cast<intptr_t>(cur), *cur, *cur);
          Tagged<Object> obj(*cur);
          Heap* current_heap = sim_->isolate_->heap();
          if (!skip_obj_print) {
            if (IsSmi(obj) ||
                IsValidHeapObject(current_heap, HeapObject::cast(obj))) {
              PrintF(" (");
              if (IsSmi(obj)) {
                PrintF("smi %d", Smi::ToInt(obj));
              } else {
                ShortPrint(obj);
              }
              PrintF(")");
            }
          }
          PrintF("\n");
          cur++;
        }

      } else if ((strcmp(cmd, "disasm") == 0) || (strcmp(cmd, "dpc") == 0) ||
                 (strcmp(cmd, "di") == 0)) {
        disasm::NameConverter converter;
        disasm::Disassembler dasm(converter);
        // Use a reasonably large buffer.
        v8::base::EmbeddedVector<char, 256> buffer;

        uint8_t* cur = nullptr;
        uint8_t* end = nullptr;

        if (argc == 1) {
          cur = reinterpret_cast<uint8_t*>(sim_->get_pc());
          end = cur + (10 * kInstrSize);
        } else if (argc == 2) {
          int regnum = Registers::Number(arg1);
          if (regnum != kInvalidRegister || strncmp(arg1, "0x", 2) == 0) {
            // The argument is an address or a register name.
            int64_t value;
            if (GetValue(arg1, &value)) {
              cur = reinterpret_cast<uint8_t*>(value);
              // Disassemble 10 instructions at <arg1>.
              end = cur + (10 * kInstrSize);
            }
          } else {
            // The argument is the number of instructions.
            int64_t value;
            if (GetValue(arg1, &value)) {
              cur = reinterpret_cast<uint8_t*>(sim_->get_pc());
              // Disassemble <arg1> instructions.
              end = cur + (value * kInstrSize);
            }
          }
        } else {
          int64_t value1;
          int64_t value2;
          if (GetValue(arg1, &value1) && GetValue(arg2, &value2)) {
            cur = reinterpret_cast<uint8_t*>(value1);
            end = cur + (value2 * kInstrSize);
          }
        }

        while (cur < end) {
          dasm.InstructionDecode(buffer, cur);
          PrintF("  0x%08" PRIxPTR "   %s\n", reinterpret_cast<intptr_t>(cur),
                 buffer.begin());
          cur += kInstrSize;
        }
      } else if (strcmp(cmd, "gdb") == 0) {
        PrintF("relinquishing control to gdb\n");
        v8::base::OS::DebugBreak();
        PrintF("regaining control from gdb\n");
      } else if (strcmp(cmd, "break") == 0) {
        if (argc == 2) {
          int64_t value;
          if (GetValue(arg1, &value)) {
            if (!SetBreakpoint(reinterpret_cast<Instruction*>(value))) {
              PrintF("setting breakpoint failed\n");
            }
          } else {
            PrintF("%s unrecognized\n", arg1);
          }
        } else {
          PrintF("break <address>\n");
        }
      } else if (strcmp(cmd, "del") == 0) {
        if (!DeleteBreakpoint(nullptr)) {
          PrintF("deleting breakpoint failed\n");
        }
      } else if (strcmp(cmd, "flags") == 0) {
        PrintF("No flags on MIPS !\n");
      } else if (strcmp(cmd, "stop") == 0) {
        int64_t value;
        intptr_t stop_pc = sim_->get_pc() - 2 * kInstrSize;
        Instruction* stop_instr = reinterpret_cast<Instruction*>(stop_pc);
        Instruction* msg_address =
            reinterpret_cast<Instruction*>(stop_pc + kInstrSize);
        if ((argc == 2) && (strcmp(arg1, "unstop") == 0)) {
          // Remove the current stop.
          if (sim_->IsStopInstruction(stop_instr)) {
            stop_instr->SetInstructionBits(kNopInstr);
            msg_address->SetInstructionBits(kNopInstr);
          } else {
            PrintF("Not at debugger stop.\n");
          }
        } else if (argc == 3) {
          // Print information about all/the specified breakpoint(s).
          if (strcmp(arg1, "info") == 0) {
            if (strcmp(arg2, "all") == 0) {
              PrintF("Stop information:\n");
              for (uint32_t i = kMaxWatchpointCode + 1; i <= kMaxStopCode;
                   i++) {
                sim_->PrintStopInfo(i);
              }
            } else if (GetValue(arg2, &value)) {
              sim_->PrintStopInfo(value);
            } else {
              PrintF("Unrecognized argument.\n");
            }
          } else if (strcmp(arg1, "enable") == 0) {
            // Enable all/the specified breakpoint(s).
            if (strcmp(arg2, "all") == 0) {
              for (uint32_t i = kMaxWatchpointCode + 1; i <= kMaxStopCode;
                   i++) {
                sim_->EnableStop(i);
              }
            } else if (GetValue(arg2, &value)) {
              sim_->EnableStop(value);
            } else {
              PrintF("Unrecognized argument.\n");
            }
          } else if (strcmp(arg1, "disable") == 0) {
            // Disable all/the specified breakpoint(s).
            if (strcmp(arg2, "all") == 0) {
              for (uint32_t i = kMaxWatchpointCode + 1; i <= kMaxStopCode;
                   i++) {
                sim_->DisableStop(i);
              }
            } else if (GetValue(arg2, &value)) {
              sim_->DisableStop(value);
            } else {
              PrintF("Unrecognized argument.\n");
            }
          }
        } else {
          PrintF("Wrong usage. Use help command for more information.\n");
        }
      } else if ((strcmp(cmd, "stat") == 0) || (strcmp(cmd, "st") == 0)) {
        // Print registers and disassemble.
        PrintAllRegs();
        PrintF("\n");

        disasm::NameConverter converter;
        disasm::Disassembler dasm(converter);
        // Use a reasonably large buffer.
        v8::base::EmbeddedVector<char, 256> buffer;

        uint8_t* cur = nullptr;
        uint8_t* end = nullptr;

        if (argc == 1) {
          cur = reinterpret_cast<uint8_t*>(sim_->get_pc());
          end = cur + (10 * kInstrSize);
        } else if (argc == 2) {
          int64_t value;
          if (GetValue(arg1, &value)) {
            cur = reinterpret_cast<uint8_t*>(value);
            // no length parameter passed, assume 10 instructions
            end = cur + (10 * kInstrSize);
          }
        } else {
          int64_t value1;
          int64_t value2;
          if (GetValue(arg1, &value1) && GetValue(arg2, &value2)) {
            cur = reinterpret_cast<uint8_t*>(value1);
            end = cur + (value2 * kInstrSize);
          }
        }

        while (cur < end) {
          dasm.InstructionDecode(buffer, cur);
          PrintF("  0x%08" PRIxPTR "   %s\n", reinterpret_cast<intptr_t>(cur),
                 buffer.begin());
          cur += kInstrSize;
        }
      } else if ((strcmp(cmd, "h") == 0) || (strcmp(cmd, "help") == 0)) {
        PrintF("cont\n");
        PrintF("  continue execution (alias 'c')\n");
        PrintF("stepi\n");
        PrintF("  step one instruction (alias 'si')\n");
        PrintF("print <register>\n");
        PrintF("  print register content (alias 'p')\n");
        PrintF("  use register name 'all' to print all registers\n");
        PrintF("printobject <register>\n");
        PrintF("  print an object from a register (alias 'po')\n");
        PrintF("stack [<words>]\n");
        PrintF("  dump stack content, default dump 10 words)\n");
        PrintF("mem <address> [<words>]\n");
        PrintF("  dump memory content, default dump 10 words)\n");
        PrintF("dump [<words>]\n");
        PrintF(
            "  dump memory content without pretty printing JS objects, default "
            "dump 10 words)\n");
        PrintF("flags\n");
        PrintF("  print flags\n");
        PrintF("disasm [<instructions>]\n");
        PrintF("disasm [<address/register>]\n");
        PrintF("disasm [[<address/register>] <instructions>]\n");
        PrintF("  disassemble code, default is 10 instructions\n");
        PrintF("  from pc (alias 'di')\n");
        PrintF("gdb\n");
        PrintF("  enter gdb\n");
        PrintF("break <address>\n");
        PrintF("  set a break point on the address\n");
        PrintF("del\n");
        PrintF("  delete the breakpoint\n");
        PrintF("stop feature:\n");
        PrintF("  Description:\n");
        PrintF("    Stops are debug instructions inserted by\n");
        PrintF("    the Assembler::stop() function.\n");
        PrintF("    When hitting a stop, the Simulator will\n");
        PrintF("    stop and give control to the Debugger.\n");
        PrintF("    All stop codes are watched:\n");
        PrintF("    - They can be enabled / disabled: the Simulator\n");
        PrintF("       will / won't stop when hitting them.\n");
        PrintF("    - The Simulator keeps track of how many times they \n");
        PrintF("      are met. (See the info command.) Going over a\n");
        PrintF("      disabled stop still increases its counter. \n");
        PrintF("  Commands:\n");
        PrintF("    stop info all/<code> : print infos about number <code>\n");
        PrintF("      or all stop(s).\n");
        PrintF("    stop enable/disable all/<code> : enables / disables\n");
        PrintF("      all or number <code> stop(s)\n");
        PrintF("    stop unstop\n");
        PrintF("      ignore the stop instruction at the current location\n");
        PrintF("      from now on\n");
      } else {
        PrintF("Unknown command: %s\n", cmd);
      }
    }
  }

  // Add all the breakpoints back to stop execution and enter the debugger
  // shell when hit.
  RedoBreakpoints();

#undef COMMAND_SIZE
#undef ARG_SIZE

#undef STR
#undef XSTR
}

bool Simulator::ICacheMatch(void* one, void* two) {
  DCHECK_EQ(reinterpret_cast<intptr_t>(one) & CachePage::kPageMask, 0);
  DCHECK_EQ(reinterpret_cast<intptr_t>(two) & CachePage::kPageMask, 0);
  return one == two;
}

static uint32_t ICacheHash(void* key) {
  return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(key)) >> 2;
}

static bool AllOnOnePage(uintptr_t start, size_t size) {
  intptr_t start_page = (start & ~CachePage::kPageMask);
  intptr_t end_page = ((start + size) & ~CachePage::kPageMask);
  return start_page == end_page;
}

void Simulator::set_last_debugger_input(char* input) {
  DeleteArray(last_debugger_input_);
  last_debugger_input_ = input;
}

void Simulator::SetRedirectInstruction(Instruction* instruction) {
  instruction->SetInstructionBits(rtCallRedirInstr);
}

void Simulator::FlushICache(base::CustomMatcherHashMap* i_cache,
                            void* start_addr, size_t size) {
  int64_t start = reinterpret_cast<int64_t>(start_addr);
  int64_t intra_line = (start & CachePage::kLineMask);
  start -= intra_line;
  size += intra_line;
  size = ((size - 1) | CachePage::kLineMask) + 1;
  int offset = (start & CachePage::kPageMask);
  while (!AllOnOnePage(start, size - 1)) {
    int bytes_to_flush = CachePage::kPageSize - offset;
    FlushOnePage(i_cache, start, bytes_to_flush);
    start += bytes_to_flush;
    size -= bytes_to_flush;
    DCHECK_EQ((int64_t)0, start & CachePage::kPageMask);
    offset = 0;
  }
  if (size != 0) {
    FlushOnePage(i_cache, start, size);
  }
}

CachePage* Simulator::GetCachePage(base::CustomMatcherHashMap* i_cache,
                                   void* page) {
  base::HashMap::Entry* entry = i_cache->LookupOrInsert(page, ICacheHash(page));
  if (entry->value == nullptr) {
    CachePage* new_page = new CachePage();
    entry->value = new_page;
  }
  return reinterpret_cast<CachePage*>(entry->value);
}

// Flush from start up to and not including start + size.
void Simulator::FlushOnePage(base::CustomMatcherHashMap* i_cache,
                             intptr_t start, size_t size) {
  DCHECK_LE(size, CachePage::kPageSize);
  DCHECK(AllOnOnePage(start, size - 1));
  DCHECK_EQ(start & CachePage::kLineMask, 0);
  DCHECK_EQ(size & CachePage::kLineMask, 0);
  void* page = reinterpret_cast<void*>(start & (~CachePage::kPageMask));
  int offset = (start & CachePage::kPageMask);
  CachePage* cache_page = GetCachePage(i_cache, page);
  char* valid_bytemap = cache_page->ValidityByte(offset);
  memset(valid_bytemap, CachePage::LINE_INVALID, size >> CachePage::kLineShift);
}

void Simulator::CheckICache(base::CustomMatcherHashMap* i_cache,
                            Instruction* instr) {
  int64_t address = reinterpret_cast<int64_t>(instr);
  void* page = reinterpret_cast<void*>(address & (~CachePage::kPageMask));
  void* line = reinterpret_cast<void*>(address & (~CachePage::kLineMask));
  int offset = (address & CachePage::kPageMask);
  CachePage* cache_page = GetCachePage(i_cache, page);
  char* cache_valid_byte = cache_page->ValidityByte(offset);
  bool cache_hit = (*cache_valid_byte == CachePage::LINE_VALID);
  char* cached_line = cache_page->CachedData(offset & ~CachePage::kLineMask);
  if (cache_hit) {
    // Check that the data in memory matches the contents of the I-cache.
    CHECK_EQ(0, memcmp(reinterpret_cast<void*>(instr),
                       cache_page->CachedData(offset), kInstrSize));
  } else {
    // Cache miss.  Load memory into the cache.
    memcpy(cached_line, line, CachePage::kLineLength);
    *cache_valid_byte = CachePage::LINE_VALID;
  }
}

Simulator::Simulator(Isolate* isolate) : isolate_(isolate) {
  // Set up simulator support first. Some of this information is needed to
  // setup the architecture state.
  size_t stack_size = AllocatedStackSize();
  stack_ = reinterpret_cast<uintptr_t>(new uint8_t[stack_size]);
  stack_limit_ = stack_ + kStackProtectionSize;
  pc_modified_ = false;
  icount_ = 0;
  break_count_ = 0;
  break_pc_ = nullptr;
  break_instr_ = 0;

  // Set up architecture state.
  // All registers are initialized to zero to start with.
  for (int i = 0; i < kNumSimuRegisters; i++) {
    registers_[i] = 0;
  }
  for (int i = 0; i < kNumFPURegisters; i++) {
    FPUregisters_[2 * i] = 0;
    FPUregisters_[2 * i + 1] = 0;  // upper part for MSA ASE
  }

  if (kArchVariant == kMips64r6) {
    FCSR_ = kFCSRNaN2008FlagMask;
    MSACSR_ = 0;
  } else {
    FCSR_ = 0;
  }

  // The sp is initialized to point to the bottom (high address) of the
  // allocated stack area. To be safe in potential stack underflows we leave
  // some buffer below.
  registers_[sp] = stack_ + stack_size - kStackProtectionSize;
  // The ra and pc are initialized to a known bad value that will cause an
  // access violation if the simulator ever tries to execute it.
  registers_[pc] = bad_ra;
  registers_[ra] = bad_ra;

  last_debugger_input_ = nullptr;
}

Simulator::~Simulator() {
  GlobalMonitor::Get()->RemoveLinkedAddress(&global_monitor_thread_);
  delete[] reinterpret_cast<uint8_t*>(stack_);
}

// Get the active Simulator for the current thread.
Simulator* Simulator::current(Isolate* isolate) {
  v8::internal::Isolate::PerIsolateThreadData* isolate_data =
      isolate->FindOrAllocatePerThreadDataForThisThread();
  DCHECK_NOT_NULL(isolate_data);

  Simulator* sim = isolate_data->simulator();
  if (sim == nullptr) {
    // TODO(146): delete the simulator object when a thread/isolate goes away.
    sim = new Simulator(isolate);
    isolate_data->set_simulator(sim);
  }
  return sim;
}

// Sets the register in the architecture state. It will also deal with updating
// Simulator internal state for special registers such as PC.
void Simulator::set_register(int reg, int64_t value) {
  DCHECK((reg >= 0) && (reg < kNumSimuRegisters));
  if (reg == pc) {
    pc_modified_ = true;
  }

  // Zero register always holds 0.
  registers_[reg] = (reg == 0) ? 0 : value;
}

void Simulator::set_dw_register(int reg, const int* dbl) {
  DCHECK((reg >= 0) && (reg < kNumSimuRegisters));
  registers_[reg] = dbl[1];
  registers_[reg] = registers_[reg] << 32;
  registers_[reg] += dbl[0];
}

void Simulator::set_fpu_register(int fpureg, int64_t value) {
  DCHECK((fpureg >= 0) && (fpureg < kNumFPURegisters));
  FPUregisters_[fpureg * 2] = value;
}

void Simulator::set_fpu_register_word(int fpureg, int32_t value) {
  // Set ONLY lower 32-bits, leaving upper bits untouched.
  DCHECK((fpureg >= 0) && (fpureg < kNumFPURegisters));
  int32_t* pword;
  if (kArchEndian == kLittle) {
    pword = reinterpret_cast<int32_t*>(&FPUregisters_[fpureg * 2]);
  } else {
    pword = reinterpret_cast<int32_t*>(&FPUregisters_[fpureg * 2]) + 1;
  }
  *pword = value;
}

void Simulator::set_fpu_register_hi_word(int fpureg, int32_t value) {
  // Set ONLY upper 32-bits, leaving lower bits untouched.
  DCHECK((fpureg >= 0) && (fpureg < kNumFPURegisters));
  int32_t* phiword;
  if (kArchEndian == kLittle) {
    phiword = (reinterpret_cast<int32_t*>(&FPUregisters_[fpureg * 2])) + 1;
  } else {
    phiword = reinterpret_cast<int32_t*>(&FPUregisters_[fpureg * 2]);
  }
  *phiword = value;
}

void Simulator::set_fpu_register_float(int fpureg, float value) {
  DCHECK((fpureg >= 0) && (fpureg < kNumFPURegisters));
  memcpy(&FPUregisters_[fpureg * 2], &value, sizeof(value));
}

void Simulator::set_fpu_register_double(int fpureg, double value) {
  DCHECK((fpureg >= 0) && (fpureg < kNumFPURegisters));
  memcpy(&FPUregisters_[fpureg * 2], &value, sizeof(value));
}

// Get the register from the architecture state. This function does handle
// the special case of accessing the PC register.
int64_t Simulator::get_register(int reg) const {
  DCHECK((reg >= 0) && (reg < kNumSimuRegisters));
  if (reg == 0)
    return 0;
  else
    return registers_[reg] + ((reg == pc) ? Instruction::kPCReadOffset : 0);
}

double Simulator::get_double_from_register_pair(int reg) {
  // TODO(plind): bad ABI stuff, refactor or remove.
  DCHECK((reg >= 0) && (reg < kNumSimuRegisters) && ((reg % 2) == 0));

  double dm_val = 0.0;
  // Read the bits from the unsigned integer register_[] array
  // into the double precision floating point value and return it.
  char buffer[sizeof(registers_[0])];
  memcpy(buffer, &registers_[reg], sizeof(registers_[0]));
  memcpy(&dm_val, buffer, sizeof(registers_[0]));
  return (dm_val);
}

int64_t Simulator::get_fpu_register(int fpureg) const {
  DCHECK((fpureg >= 0) && (fpureg < kNumFPURegisters));
  return FPUregisters_[fpureg * 2];
}

int32_t Simulator::get_fpu_register_word(int fpureg) const {
  DCHECK((fpureg >= 0) && (fpureg < kNumFPURegisters));
  return static_cast<int32_t>(FPUregisters_[fpureg * 2] & 0xFFFFFFFF);
}

int32_t Simulator::get_fpu_register_signed_word(int fpureg) const {
  DCHECK((fpureg >= 0) && (fpureg < kNumFPURegisters));
  return static_cast<int32_t>(FPUregisters_[fpureg * 2] & 0xFFFFFFFF);
}

int32_t Simulator::get_fpu_register_hi_word(int fpureg) const {
  DCHECK((fpureg >= 0) && (fpureg < kNumFPURegisters));
  return static_cast<int32_t>((FPUregisters_[fpureg * 2] >> 32) & 0xFFFFFFFF);
}

float Simulator::get_fpu_register_float(int fpureg) const {
  DCHECK((fpureg >= 0) && (fpureg < kNumFPURegisters));
  return base::bit_cast<float>(get_fpu_register_word(fpureg));
}

double Simulator::get_fpu_register_double(int fpureg) const {
  DCHECK((fpureg >= 0) && (fpureg < kNumFPURegisters));
  return base::bit_cast<double>(FPUregisters_[fpureg * 2]);
}

template <typename T>
void Simulator::get_msa_register(int wreg, T* value) {
  DCHECK((wreg >= 0) && (wreg < kNumMSARegisters));
  memcpy(value, FPUregisters_ + wreg * 2, kSimd128Size);
}

template <typename T>
void Simulator::set_msa_register(int wreg, const T* value) {
  DCHECK((wreg >= 0) && (wreg < kNumMSARegisters));
  memcpy(FPUregisters_ + wreg * 2, value, kSimd128Size);
}

// Runtime FP routines take up to two double arguments and zero
// or one integer arguments. All are constructed here,
// from a0-a3 or f12 and f13 (n64), or f14 (O32).
void Simulator::GetFpArgs(double* x, double* y, int32_t* z) {
  if (!IsMipsSoftFloatABI) {
    const int fparg2 = 13;
    *x = get_fpu_register_double(12);
    *y = get_fpu_register_double(fparg2);
    *z = static_cast<int32_t>(get_register(a2));
  } else {
    // TODO(plind): bad ABI stuff, refactor or remove.
    // We use a char buffer to get around the strict-aliasing rules which
    // otherwise allow the compiler to optimize away the copy.
    char buffer[sizeof(*x)];
    int32_t* reg_buffer = reinterpret_cast<int32_t*>(buffer);

    // Registers a0 and a1 -> x.
    reg_buffer[0] = get_register(a0);
    reg_buffer[1] = get_register(a1);
    memcpy(x, buffer, sizeof(buffer));
    // Registers a2 and a3 -> y.
    reg_buffer[0] = get_register(a2);
    reg_buffer[1] = get_register(a3);
    memcpy(y, buffer, sizeof(buffer));
    // Register 2 -> z.
    reg_buffer[0] = get_register(a2);
    memcpy(z, buffer, sizeof(*z));
  }
}

// The return value is either in v0/v1 or f0.
void Simulator::SetFpResult(const double& result) {
  if (!IsMipsSoftFloatABI) {
    set_fpu_register_double(0, result);
  } else {
    char buffer[2 * sizeof(registers_[0])];
    int64_t* reg_buffer = reinterpret_cast<int64_t*>(buffer);
    memcpy(buffer, &result, sizeof(buffer));
    // Copy result to v0 and v1.
    set_register(v0, reg_buffer[0]);
    set_register(v1, reg_buffer[1]);
  }
}

// Helper functions for setting and testing the FCSR register's bits.
void Simulator::set_fcsr_bit(uint32_t cc, bool value) {
  if (value) {
    FCSR_ |= (1 << cc);
  } else {
    FCSR_ &= ~(1 << cc);
  }
}

bool Simulator::test_fcsr_bit(uint32_t cc) { return FCSR_ & (1 << cc); }

void Simulator::clear_fcsr_cause() {
  FCSR_ &= ~kFCSRCauseMask;
}

void Simulator::set_fcsr_rounding_mode(FPURoundingMode mode) {
  FCSR_ |= mode & kFPURoundingModeMask;
}

void Simulator::set_msacsr_rounding_mode(FPURoundingMode mode) {
  MSACSR_ |= mode & kFPURoundingModeMask;
}

unsigned int Simulator::get_fcsr_rounding_mode() {
  return FCSR_ & kFPURoundingModeMask;
}

unsigned int Simulator::get_msacsr_rounding_mode() {
  return MSACSR_ & kFPURoundingModeMask;
}

// Sets the rounding error codes in FCSR based on the result of the rounding.
// Returns true if the operation was invalid.
bool Simulator::set_fcsr_round_error(double original, double rounded) {
  bool ret = false;
  double max_int32 = std::numeric_limits<int32_t>::max();
  double min_int32 = std::numeric_limits<int32_t>::min();

  clear_fcsr_cause();

  if (!std::isfinite(original) || !std::isfinite(rounded)) {
    set_fcsr_bit(kFCSRInvalidOpFlagBit, true);
    set_fcsr_bit(kFCSRInvalidOpCauseBit, true);
    ret = true;
  }

  if (original != rounded) {
    set_fcsr_bit(kFCSRInexactFlagBit, true);
    set_fcsr_bit(kFCSRInexactCauseBit, true);
  }

  if (rounded < DBL_MIN && rounded > -DBL_MIN && rounded != 0) {
    set_fcsr_bit(kFCSRUnderflowFlagBit, true);
    set_fcsr_bit(kFCSRUnderflowCauseBit, true);
    ret = true;
  }

  if (rounded > max_int32 || rounded < min_int32) {
    set_fcsr_bit(kFCSROverflowFlagBit, true);
    set_fcsr_bit(kFCSROverflowCauseBit, true);
    // The reference is not really clear but it seems this is required:
    set_fcsr_bit(kFCSRInvalidOpFlagBit, true);
    set_fcsr_bit(kFCSRInvalidOpCauseBit, true);
    ret = true;
  }

  return ret;
}

// Sets the rounding error codes in FCSR based on the result of the rounding.
// Returns true if the operation was invalid.
bool Simulator::set_fcsr_round64_error(double original, double rounded) {
  bool ret = false;
  // The value of INT64_MAX (2^63-1) can't be represented as double exactly,
  // loading the most accurate representation into max_int64, which is 2^63.
  double max_int64 = static_cast<double>(std::numeric_limits<int64_t>::max());
  double min_int64 = std::numeric_limits<int64_t>::min();

  clear_fcsr_cause();

  if (!std::isfinite(original) || !std::isfinite(rounded)) {
    set_fcsr_bit(kFCSRInvalidOpFlagBit, true);
    set_fcsr_bit(kFCSRInvalidOpCauseBit, true);
    ret = true;
  }

  if (original != rounded) {
    set_fcsr_bit(kFCSRInexactFlagBit, true);
    set_fcsr_bit(kFCSRInexactCauseBit, true);
  }

  if (rounded < DBL_MIN && rounded > -DBL_MIN && rounded != 0) {
    set_fcsr_bit(kFCSRUnderflowFlagBit, true);
    set_fcsr_bit(kFCSRUnderflowCauseBit, true);
    ret = true;
  }

  if (rounded >= max_int64 || rounded < min_int64) {
    set_fcsr_bit(kFCSROverflowFlagBit, true);
    set_fcsr_bit(kFCSROverflowCauseBit, true);
    // The reference is not really clear but it seems this is required:
    set_fcsr_bit(kFCSRInvalidOpFlagBit, true);
    set_fcsr_bit(kFCSRInvalidOpCauseBit, true);
    ret = true;
  }

  return ret;
}

// Sets the rounding error codes in FCSR based on the result of the rounding.
// Returns true if the operation was invalid.
bool Simulator::set_fcsr_round_error(float original, float rounded) {
  bool ret = false;
  double max_int32 = std::numeric_limits<int32_t>::max();
  double min_int32 = std::numeric_limits<int32_t>::min();

  clear_fcsr_cause();

  if (!std::isfinite(original) || !std::isfinite(rounded)) {
    set_fcsr_bit(kFCSRInvalidOpFlagBit, true);
    set_fcsr_bit(kFCSRInvalidOpCauseBit, true);
    ret = true;
  }

  if (original != rounded) {
    set_fcsr_bit(kFCSRInexactFlagBit, true);
    set_fcsr_bit(kFCSRInexactCauseBit, true);
  }

  if (rounded < FLT_MIN && rounded > -FLT_MIN && rounded != 0) {
    set_fcsr_bit(kFCSRUnderflowFlagBit, true);
    set_fcsr_bit(kFCSRUnderflowCauseBit, true);
    ret = true;
  }

  if (rounded > max_int32 || rounded < min_int32) {
    set_fcsr_bit(kFCSROverflowFlagBit, true);
    set_fcsr_bit(kFCSROverflowCauseBit, true);
    // The reference is not really clear but it seems this is required:
    set_fcsr_bit(kFCSRInvalidOpFlagBit, true);
    set_fcsr_bit(kFCSRInvalidOpCauseBit, true);
    ret = true;
  }

  return ret;
}

void Simulator::set_fpu_register_word_invalid_result(float original,
                                                     float rounded) {
  if (FCSR_ & kFCSRNaN2008FlagMask) {
    double max_int32 = std::numeric_limits<int32_t>::max();
    double min_int32 = std::numeric_limits<int32_t>::min();
    if (std::isnan(original)) {
      set_fpu_register_word(fd_reg(), 0);
    } else if (rounded > max_int32) {
      set_fpu_register_word(fd_reg(), kFPUInvalidResult);
    } else if (rounded < min_int32) {
      set_fpu_register_word(fd_reg(), kFPUInvalidResultNegative);
    } else {
      UNREACHABLE();
    }
  } else {
    set_fpu_register_word(fd_reg(), kFPUInvalidResult);
  }
}

void Simulator::set_fpu_register_invalid_result(float original, float rounded) {
  if (FCSR_ & kFCSRNaN2008FlagMask) {
    double max_int32 = std::numeric_limits<int32_t>::max();
    double min_int32 = std::numeric_limits<int32_t>::min();
    if (std::isnan(original)) {
      set_fpu_register(fd_reg(), 0);
    } else if (rounded > max_int32) {
      set_fpu_register(fd_reg(), kFPUInvalidResult);
    } else if (rounded < min_int32) {
      set_fpu_register(fd_reg(), kFPUInvalidResultNegative);
    } else {
      UNREACHABLE();
    }
  } else {
    set_fpu_register(fd_reg(), kFPUInvalidResult);
  }
}

void Simulator::set_fpu_register_invalid_result64(float original,
                                                  float rounded) {
  if (FCSR_ & kFCSRNaN2008FlagMask) {
    // The value of INT64_MAX (2^63-1) can't be represented as double exactly,
    // loading the most accurate representation into max_int64, which is 2^63.
    double max_int64 = static_cast<double>(std::numeric_limits<int64_t>::max());
    double min_int64 = std::numeric_limits<int64_t>::min();
    if (std::isnan(original)) {
      set_fpu_register(fd_reg(), 0);
    } else if (rounded >= max_int64) {
      set_fpu_register(fd_reg(), kFPU64InvalidResult);
    } else if (rounded < min_int64) {
      set_fpu_register(fd_reg(), kFPU64InvalidResultNegative);
    } else {
      UNREACHABLE();
    }
  } else {
    set_fpu_register(fd_reg(), kFPU64InvalidResult);
  }
}

void Simulator::set_fpu_register_word_invalid_result(double original,
                                                     double rounded) {
  if (FCSR_ & kFCSRNaN2008FlagMask) {
    double max_int32 = std::numeric_limits<int32_t>::max();
    double min_int32 = std::numeric_limits<int32_t>::min();
    if (std::isnan(original)) {
      set_fpu_register_word(fd_reg(), 0);
    } else if (rounded > max_int32) {
      set_fpu_register_word(fd_reg(), kFPUInvalidResult);
    } else if (rounded < min_int32) {
      set_fpu_register_word(fd_reg(), kFPUInvalidResultNegative);
    } else {
      UNREACHABLE();
    }
  } else {
    set_fpu_register_word(fd_reg(), kFPUInvalidResult);
  }
}

void Simulator::set_fpu_register_invalid_result(double original,
                                                double rounded) {
  if (FCSR_ & kFCSRNaN2008FlagMask) {
    double max_int32 = std::numeric_limits<int32_t>::max();
    double min_int32 = std::numeric_limits<int32_t>::min();
    if (std::isnan(original)) {
      set_fpu_register(fd_reg(), 0);
    } else if (rounded > max_int32) {
      set_fpu_register(fd_reg(), kFPUInvalidResult);
    } else if (rounded < min_int32) {
      set_fpu_register(fd_reg(), kFPUInvalidResultNegative);
    } else {
      UNREACHABLE();
    }
  } else {
    set_fpu_register(fd_reg(), kFPUInvalidResult);
  }
}

void Simulator::set_fpu_register_invalid_result64(double original,
                                                  double rounded) {
  if (FCSR_ & kFCSRNaN2008FlagMask) {
    // The value of INT64_MAX (2^63-1) can't be represented as double exactly,
    // loading the most accurate representation into max_int64, which is 2^63.
    double max_int64 = static_cast<double>(std::numeric_limits<int64_t>::max());
    double min_int64 = std::numeric_limits<int64_t>::min();
    if (std::isnan(original)) {
      set_fpu_register(fd_reg(), 0);
    } else if (rounded >= max_int64) {
      set_fpu_register(fd_reg(), kFPU64InvalidResult);
    } else if (rounded < min_int64) {
      set_fpu_register(fd_reg(), kFPU64InvalidResultNegative);
    } else {
      UNREACHABLE();
    }
  } else {
    set_fpu_register(fd_reg(), kFPU64InvalidResult);
  }
}

// Sets the rounding error codes in FCSR based on the result of the rounding.
// Returns true if the operation was invalid.
bool Simulator::set_fcsr_round64_error(float original, float rounded) {
  bool ret = false;
  // The value of INT64_MAX (2^63-1) can't be represented as double exactly,
  // loading the most accurate representation into max_int64, which is 2^63.
  double max_int64 = static_cast<double>(std::numeric_limits<int64_t>::max());
  double min_int64 = std::numeric_limits<int64_t>::min();

  clear_fcsr_cause();

  if (!std::isfinite(original) || !std::isfinite(rounded)) {
    set_fcsr_bit(kFCSRInvalidOpFlagBit, true);
    set_fcsr_bit(kFCSRInvalidOpCauseBit, true);
    ret = true;
  }

  if (original != rounded) {
    set_fcsr_bit(kFCSRInexactFlagBit, true);
    set_fcsr_bit(kFCSRInexactCauseBit, true);
  }

  if (rounded < FLT_MIN && rounded > -FLT_MIN && rounded != 0) {
    set_fcsr_bit(kFCSRUnderflowFlagBit, true);
    set_fcsr_bit(kFCSRUnderflowCauseBit, true);
    ret = true;
  }

  if (rounded >= max_int64 || rounded < min_int64) {
    set_fcsr_bit(kFCSROverflowFlagBit, true);
    set_fcsr_bit(kFCSROverflowCauseBit, true);
    // The reference is not really clear but it seems this is required:
    set_fcsr_bit(kFCSRInvalidOpFlagBit, true);
    set_fcsr_bit(kFCSRInvalidOpCauseBit, true);
    ret = true;
  }

  return ret;
}

// For cvt instructions only
void Simulator::round_according_to_fcsr(double toRound, double* rounded,
                                        int32_t* rounded_int, double fs) {
  // 0 RN (round to nearest): Round a result to the nearest
  // representable value; if the result is exactly halfway between
  // two representable values, round to zero. Behave like round_w_d.

  // 1 RZ (round toward zero): Round a result to the closest
  // representable value whose absolute value is less than or
  // equal to the infinitely accurate result. Behave like trunc_w_d.

  // 2 RP (round up, or toward +infinity): Round a result to the
  // next representable value up. Behave like ceil_w_d.

  // 3 RN (round down, or toward −infinity): Round a result to
  // the next representable value down. Behave like floor_w_d.
  switch (FCSR_ & 3) {
    case kRoundToNearest:
      *rounded = std::floor(fs + 0.5);
      *rounded_int = static_cast<int32_t>(*rounded);
      if ((*rounded_int & 1) != 0 && *rounded_int - fs == 0.5) {
        // If the number is halfway between two integers,
        // round to the even one.
        *rounded_int -= 1;
        *rounded -= 1.;
      }
      break;
    case kRoundToZero:
      *rounded = trunc(fs);
      *rounded_int = static_cast<int32_t>(*rounded);
      break;
    case kRoundToPlusInf:
      *rounded = std::ceil(fs);
      *rounded_int = static_cast<int32_t>(*rounded);
      break;
    case kRoundToMinusInf:
      *rounded = std::floor(fs);
      *rounded_int = static_cast<int32_t>(*rounded);
      break;
  }
}

void Simulator::round64_according_to_fcsr(double toRound, double* rounded,
                                          int64_t* rounded_int, double fs) {
  // 0 RN (round to nearest): Round a result to the nearest
  // representable value; if the result is exactly halfway between
  // two representable values, round to zero. Behave like round_w_d.

  // 1 RZ (round toward zero): Round a result to the closest
  // representable value whose absolute value is less than or.
  // equal to the infinitely accurate result. Behave like trunc_w_d.

  // 2 RP (round up, or toward +infinity): Round a result to the
  // next representable value up. Behave like ceil_w_d.

  // 3 RN (round down, or toward −infinity): Round a result to
  // the next representable value down. Behave like floor_w_d.
  switch (FCSR_ & 3) {
    case kRoundToNearest:
      *rounded = std::floor(fs + 0.5);
      *rounded_int = static_cast<int64_t>(*rounded);
      if ((*rounded_int & 1) != 0 && *rounded_int - fs == 0.5) {
        // If the number is halfway between two integers,
        // round to the even one.
        *rounded_int -= 1;
        *rounded -= 1.;
      }
      break;
    case kRoundToZero:
      *rounded = trunc(fs);
      *rounded_int = static_cast<int64_t>(*rounded);
      break;
    case kRoundToPlusInf:
      *rounded = std::ceil(fs);
      *rounded_int = static_cast<int64_t>(*rounded);
      break;
    case kRoundToMinusInf:
      *rounded = std::floor(fs);
      *rounded_int = static_cast<int64_t>(*rounded);
      break;
  }
}

// for cvt instructions only
void Simulator::round_according_to_fcsr(float toRound, float* rounded,
                                        int32_t* rounded_int, float fs) {
  // 0 RN (round to nearest): Round a result to the nearest
  // representable value; if the result is exactly halfway between
  // two representable values, round to zero. Behave like round_w_d.

  // 1 RZ (round toward zero): Round a result to the closest
  // representable value whose absolute value is less than or
  // equal to the infinitely accurate result. Behave like trunc_w_d.

  // 2 RP (round up, or toward +infinity): Round a result to the
  // next representable value up. Behave like ceil_w_d.

  // 3 RN (round down, or toward −infinity): Round a result to
  // the next representable value down. Behave like floor_w_d.
  switch (FCSR_ & 3) {
    case kRoundToNearest:
      *rounded = std::floor(fs + 0.5);
      *rounded_int = static_cast<int32_t>(*rounded);
      if ((*rounded_int & 1) != 0 && *rounded_int - fs == 0.5) {
        // If the number is halfway between two integers,
        // round to the even one.
        *rounded_int -= 1;
        *rounded -= 1.f;
      }
      break;
    case kRoundToZero:
      *rounded = trunc(fs);
      *rounded_int = static_cast<int32_t>(*rounded);
      break;
    case kRoundToPlusInf:
      *rounded = std::ceil(fs);
      *rounded_int = static_cast<int32_t>(*rounded);
      break;
    case kRoundToMinusInf:
      *rounded = std::floor(fs);
      *rounded_int = static_cast<int32_t>(*rounded);
      break;
  }
}

void Simulator::round64_according_to_fcsr(float toRound, float* rounded,
                                          int64_t* rounded_int, float fs) {
  // 0 RN (round to nearest): Round a result to the nearest
  // representable value; if the result is exactly halfway between
  // two representable values, round to zero. Behave like round_w_d.

  // 1 RZ (round toward zero): Round a result to the closest
  // representable value whose absolute value is less than or.
  // equal to the infinitely accurate result. Behave like trunc_w_d.

  // 2 RP (round up, or toward +infinity): Round a result to the
  // next representable value up. Behave like ceil_w_d.

  // 3 RN (round down, or toward −infinity): Round a result to
  // the next representable value down. Behave like floor_w_d.
  switch (FCSR_ & 3) {
    case kRoundToNearest:
      *rounded = std::floor(fs + 0.5);
      *rounded_int = static_cast<int64_t>(*rounded);
      if ((*rounded_int & 1) != 0 && *rounded_int - fs == 0.5) {
        // If the number is halfway between two integers,
        // round to the even one.
        *rounded_int -= 1;
        *rounded -= 1.f;
      }
      break;
    case kRoundToZero:
      *rounded = trunc(fs);
      *rounded_int = static_cast<int64_t>(*rounded);
      break;
    case kRoundToPlusInf:
      *rounded = std::ceil(fs);
      *rounded_int = static_cast<int64_t>(*rounded);
      break;
    case kRoundToMinusInf:
      *rounded = std::floor(fs);
      *rounded_int = static_cast<int64_t>(*rounded);
      break;
  }
}

template <typename T_fp, typename T_int>
void Simulator::round_according_to_msacsr(T_fp toRound, T_fp* rounded,
                                          T_int* rounded_int) {
  // 0 RN (round to nearest): Round a result to the nearest
  // representable value; if the result is exactly halfway between
  // two representable values, round to zero. Behave like round_w_d.

  // 1 RZ (round toward zero): Round a result to the closest
  // representable value whose absolute value is less than or
  // equal to the infinitely accurate result. Behave like trunc_w_d.

  // 2 RP (round up, or toward +infinity): Round a result to the
  // next representable value up. Behave like ceil_w_d.

  // 3 RN (round down, or toward −infinity): Round a result to
  // the next representable value down. Behave like floor_w_d.
  switch (get_msacsr_rounding_mode()) {
    case kRoundToNearest:
      *rounded = std::floor(toRound + 0.5);
      *rounded_int = static_cast<T_int>(*rounded);
      if ((*rounded_int & 1) != 0 && *rounded_int - toRound == 0.5) {
        // If the number is halfway between two integers,
        // round to the even one.
        *rounded_int -= 1;
        *rounded -= 1.;
      }
      break;
    case kRoundToZero:
      *rounded = trunc(toRound);
      *rounded_int = static_cast<T_int>(*rounded);
      break;
    case kRoundToPlusInf:
      *rounded = std::ceil(toRound);
      *rounded_int = static_cast<T_int>(*rounded);
      break;
    case kRoundToMinusInf:
      *rounded = std::floor(toRound);
      *rounded_int = static_cast<T_int>(*rounded);
      break;
  }
}

// Raw access to the PC register.
void Simulator::set_pc(int64_t value) {
  pc_modified_ = true;
  registers_[pc] = value;
}

bool Simulator::has_bad_pc() const {
  return ((registers_[pc] == bad_ra) || (registers_[pc] == end_sim_pc));
}

// Raw access to the PC register without the special adjustment when reading.
int64_t Simulator::get_pc() const { return registers_[pc]; }

// The MIPS cannot do unaligned reads and writes.  On some MIPS platforms an
// interrupt is caused.  On others it does a funky rotation thing.  For now we
// simply disallow unaligned reads, but at some point we may want to move to
// emulating the rotate behaviour.  Note that simulator runs have the runtime
// system running directly on the host system and only generated code is
// executed in the simulator.  Since the host is typically IA32 we will not
// get the correct MIPS-like behaviour on unaligned accesses.

// TODO(plind): refactor this messy debug code when we do unaligned access.
void Simulator::DieOrDebug() {
  if ((1)) {  // Flag for this was removed.
    MipsDebugger dbg(this);
    dbg.Debug();
  } else {
    base::OS::Abort();
  }
}

void Simulator::TraceRegWr(int64_t value, TraceType t) {
  if (v8_flags.trace_sim) {
    union {
      int64_t fmt_int64;
      int32_t fmt_int32[2];
      float fmt_float[2];
      double fmt_double;
    } v;
    v.fmt_int64 = value;

    switch (t) {
      case WORD:
        base::SNPrintF(trace_buf_,
                       "%016" PRIx64 "    (%" PRId64 ")    int32:%" PRId32
                       " uint32:%" PRIu32,
                       v.fmt_int64, icount_, v.fmt_int32[0], v.fmt_int32[0]);
        break;
      case DWORD:
        base::SNPrintF(trace_buf_,
                       "%016" PRIx64 "    (%" PRId64 ")    int64:%" PRId64
                       " uint64:%" PRIu64,
                       value, icount_, value, value);
        break;
      case FLOAT:
        base::SNPrintF(trace_buf_, "%016" PRIx64 "    (%" PRId64 ")    flt:%e",
                       v.fmt_int64, icount_, v.fmt_float[0]);
        break;
      case DOUBLE:
        base::SNPrintF(trace_buf_, "%016" PRIx64 "    (%" PRId64 ")    dbl:%e",
                       v.fmt_int64, icount_, v.fmt_double);
        break;
      case FLOAT_DOUBLE:
        base::SNPrintF(trace_buf_,
                       "%016" PRIx64 "    (%" PRId64 ")    flt:%e dbl:%e",
                       v.fmt_int64, icount_, v.fmt_float[0], v.fmt_double);
        break;
      case WORD_DWORD:
        base::SNPrintF(trace_buf_,
                       "%016" PRIx64 "    (%" PRId64 ")    int32:%" PRId32
                       " uint32:%" PRIu32 " int64:%" PRId64 " uint64:%" PRIu64,
                       v.fmt_int64, icount_, v.fmt_int32[0], v.fmt_int32[0],
                       v.fmt_int64, v.fmt_int64);
        break;
      default:
        UNREACHABLE();
    }
  }
}

template <typename T>
void Simulator::TraceMSARegWr(T* value, TraceType t) {
  if (v8_flags.trace_sim) {
    union {
      uint8_t b[16];
      uint16_t h[8];
      uint32_t w[4];
      uint64_t d[2];
      float f[4];
      double df[2];
    } v;
    memcpy(v.b, value, kSimd128Size);
    switch (t) {
      case BYTE:
        base::SNPrintF(trace_buf_,
                       "LO: %016" PRIx64 "  HI: %016" PRIx64 "    (%" PRIu64
                       ")",
                       v.d[0], v.d[1], icount_);
        break;
      case HALF:
        base::SNPrintF(trace_buf_,
                       "LO: %016" PRIx64 "  HI: %016" PRIx64 "    (%" PRIu64
                       ")",
                       v.d[0], v.d[1], icount_);
        break;
      case WORD:
        base::SNPrintF(trace_buf_,
                       "LO: %016" PRIx64 "  HI: %016" PRIx64 "    (%" PRIu64
                       ")    int32[0..3]:%" PRId32 "  %" PRId32 "  %" PRId32
                       "  %" PRId32,
                       v.d[0], v.d[1], icount_, v.w[0], v.w[1], v.w[2], v.w[3]);
        break;
      case DWORD:
        base::SNPrintF(trace_buf_,
                       "LO: %016" PRIx64 "  HI: %016" PRIx64 "    (%" PRIu64
                       ")",
                       v.d[0], v.d[1], icount_);
        break;
      case FLOAT:
        base::SNPrintF(trace_buf_,
                       "LO: %016" PRIx64 "  HI: %016" PRIx64 "    (%" PRIu64
                       ")    flt[0..3]:%e  %e  %e  %e",
                       v.d[0], v.d[1], icount_, v.f[0], v.f[1], v.f[2], v.f[3]);
        break;
      case DOUBLE:
        base::SNPrintF(trace_buf_,
                       "LO: %016" PRIx64 "  HI: %016" PRIx64 "    (%" PRIu64
                       ")    dbl[0..1]:%e  %e",
                       v.d[0], v.d[1], icount_, v.df[0], v.df[1]);
        break;
      default:
        UNREACHABLE();
    }
  }
}

template <typename T>
void Simulator::TraceMSARegWr(T* value) {
  if (v8_flags.trace_sim) {
    union {
      uint8_t b[kMSALanesByte];
      uint16_t h[kMSALanesHalf];
      uint32_t w[kMSALanesWord];
      uint64_t d[kMSALanesDword];
      float f[kMSALanesWord];
      double df[kMSALanesDword];
    } v;
    memcpy(v.b, value, kMSALanesByte);

    if (std::is_same<T, int32_t>::value) {
      base::SNPrintF(trace_buf_,
                     "LO: %016" PRIx64 "  HI: %016" PRIx64 "    (%" PRIu64
                     ")    int32[0..3]:%" PRId32 "  %" PRId32 "  %" PRId32
                     "  %" PRId32,
                     v.d[0], v.d[1], icount_, v.w[0], v.w[1], v.w[2], v.w[3]);
    } else if (std::is_same<T, float>::value) {
      base::SNPrintF(trace_buf_,
                     "LO: %016" PRIx64 "  HI: %016" PRIx64 "    (%" PRIu64
                     ")    flt[0..3]:%e  %e  %e  %e",
                     v.d[0], v.d[1], icount_, v.f[0], v.f[1], v.f[2], v.f[3]);
    } else if (std::is_same<T, double>::value) {
      base::SNPrintF(trace_buf_,
                     "LO: %016" PRIx64 "  HI: %016" PRIx64 "    (%" PRIu64
                     ")    dbl[0..1]:%e  %e",
                     v.d[0], v.d[1], icount_, v.df[0], v.df[1]);
    } else {
      base::SNPrintF(trace_buf_,
                     "LO: %016" PRIx64 "  HI: %016" PRIx64 "    (%" PRIu64 ")",
                     v.d[0], v.d[1], icount_);
    }
  }
}

// TODO(plind): consider making icount_ printing a flag option.
void Simulator::TraceMemRd(int64_t addr, int64_t value, TraceType t) {
  if (v8_flags.trace_sim) {
    union {
      int64_t fmt_int64;
      int32_t fmt_int32[2];
      float fmt_float[2];
      double fmt_double;
    } v;
    v.fmt_int64 = value;

    switch (t) {
      case WORD:
        base::SNPrintF(trace_buf_,
                       "%016" PRIx64 "  <-- [%016" PRIx64 "]    (%" PRId64
                       ")    int32:%" PRId32 " uint32:%" PRIu32,
                       v.fmt_int64, addr, icount_, v.fmt_int32[0],
                       v.fmt_int32[0]);
        break;
      case DWORD:
        base::SNPrintF(trace_buf_,
                       "%016" PRIx64 "  <-- [%016" PRIx64 "]    (%" PRId64
                       ")    int64:%" PRId64 " uint64:%" PRIu64,
                       value, addr, icount_, value, value);
        break;
      case FLOAT:
        base::SNPrintF(trace_buf_,
                       "%016" PRIx64 "  <-- [%016" PRIx64 "]    (%" PRId64
                       ")    flt:%e",
                       v.fmt_int64, addr, icount_, v.fmt_float[0]);
        break;
      case DOUBLE:
        base::SNPrintF(trace_buf_,
                       "%016" PRIx64 "  <-- [%016" PRIx64 "]    (%" PRId64
                       ")    dbl:%e",
                       v.fmt_int64, addr, icount_, v.fmt_double);
        break;
      case FLOAT_DOUBLE:
        base::SNPrintF(trace_buf_,
                       "%016" PRIx64 "  <-- [%016" PRIx64 "]    (%" PRId64
                       ")    flt:%e dbl:%e",
                       v.fmt_int64, addr, icount_, v.fmt_float[0],
                       v.fmt_double);
        break;
      default:
        UNREACHABLE();
    }
  }
}

void Simulator::TraceMemWr(int64_t addr, int64_t value, TraceType t) {
  if (v8_flags.trace_sim) {
    switch (t) {
      case BYTE:
        base::SNPrintF(trace_buf_,
                       "               %02" PRIx8 " --> [%016" PRIx64
                       "]    (%" PRId64 ")",
                       static_cast<uint8_t>(value), addr, icount_);
        break;
      case HALF:
        base::SNPrintF(trace_buf_,
                       "            %04" PRIx16 " --> [%016" PRIx64
                       "]    (%" PRId64 ")",
                       static_cast<uint16_t>(value), addr, icount_);
        break;
      case WORD:
        base::SNPrintF(trace_buf_,
                       "        %08" PRIx32 " --> [%016" PRIx64 "]    (%" PRId64
                       ")",
                       static_cast<uint32_t>(value), addr, icount_);
        break;
      case DWORD:
        base::SNPrintF(trace_buf_,
                       "%016" PRIx64 "  --> [%016" PRIx64 "]    (%" PRId64 " )",
                       value, addr, icount_);
        break;
      default:
        UNREACHABLE();
    }
  }
}

template <typename T>
void Simulator::TraceMemRd(int64_t addr, T value) {
  if (v8_flags.trace_sim) {
    switch (sizeof(T)) {
      case 1:
        base::SNPrintF(trace_buf_,
                       "%08" PRIx8 " <-- [%08" PRIx64 "]    (%" PRIu64
                       ")    int8:%" PRId8 " uint8:%" PRIu8,
                       static_cast<uint8_t>(value), addr, icount_,
                       static_cast<int8_t>(value), static_cast<uint8_t>(value));
        break;
      case 2:
        base::SNPrintF(trace_buf_,
                       "%08" PRIx16 " <-- [%08" PRIx64 "]    (%" PRIu64
                       ")    int16:%" PRId16 " uint16:%" PRIu16,
                       static_cast<uint16_t>(value), addr, icount_,
                       static_cast<int16_t>(value),
                       static_cast<uint16_t>(value));
        break;
      case 4:
        base::SNPrintF(trace_buf_,
                       "%08" PRIx32 " <-- [%08" PRIx64 "]    (%" PRIu64
                       ")    int32:%" PRId32 " uint32:%" PRIu32,
                       static_cast<uint32_t>(value), addr, icount_,
                       static_cast<int32_t>(value),
                       static_cast<uint32_t>(value));
        break;
      case 8:
        base::SNPrintF(trace_buf_,
                       "%08" PRIx64 " <-- [%08" PRIx64 "]    (%" PRIu64
                       ")    int64:%" PRId64 " uint64:%" PRIu64,
                       static_cast<uint64_t>(value), addr, icount_,
                       static_cast<int64_t>(value),
                       static_cast<uint64_t>(value));
        break;
      default:
        UNREACHABLE();
    }
  }
}

template <typename T>
void Simulator::TraceMemWr(int64_t addr, T value) {
  if (v8_flags.trace_sim) {
    switch (sizeof(T)) {
      case 1:
        base::SNPrintF(trace_buf_,
                       "      %02" PRIx8 " --> [%08" PRIx64 "]    (%" PRIu64
                       ")",
                       static_cast<uint8_t>(value), addr, icount_);
        break;
      case 2:
        base::SNPrintF(trace_buf_,
                       "    %04" PRIx16 " --> [%08" PRIx64 "]    (%" PRIu64 ")",
                       static_cast<uint16_t>(value), addr, icount_);
        break;
      case 4:
        base::SNPrintF(trace_buf_,
                       "%08" PRIx32 " --> [%08" PRIx64 "]    (%" PRIu64 ")",
                       static_cast<uint32_t>(value), addr, icount_);
        break;
      case 8:
        base::SNPrintF(trace_buf_,
                       "%16" PRIx64 " --> [%08" PRIx64 "]    (%" PRIu64 ")",
                       static_cast<uint64_t>(value), addr, icount_);
        break;
      default:
        UNREACHABLE();
    }
  }
}

// TODO(plind): sign-extend and zero-extend not implmented properly
// on all the ReadXX functions, I don't think re-interpret cast does it.
int32_t Simulator::ReadW(int64_t addr, Instruction* instr, TraceType t) {
  if (addr >= 0 && addr < 0x400) {
    // This has to be a nullptr-dereference, drop into debugger.
    PrintF("Memory read from bad address: 0x%08" PRIx64 " , pc=0x%08" PRIxPTR
           " \n",
           addr, reinterpret_cast<intptr_t>(instr));
    DieOrDebug();
  }
  if ((addr & 0x3) == 0 || kArchVariant == kMips64r6) {
    local_monitor_.NotifyLoad();
    int32_t* ptr = reinterpret_cast<int32_t*>(addr);
    TraceMemRd(addr, static_cast<int64_t>(*ptr), t);
    return *ptr;
  }
  PrintF("Unaligned read at 0x%08" PRIx64 " , pc=0x%08" V8PRIxPTR "\n", addr,
         reinterpret_cast<intptr_t>(instr));
  DieOrDebug();
  return 0;
}

uint32_t Simulator::ReadWU(int64_t addr, Instruction* instr) {
  if (addr >= 0 && addr < 0x400) {
    // This has to be a nullptr-dereference, drop into debugger.
    PrintF("Memory read from bad address: 0x%08" PRIx64 " , pc=0x%08" PRIxPTR
           " \n",
           addr, reinterpret_cast<intptr_t>(instr));
    DieOrDebug();
  }
  if ((addr & 0x3) == 0 || kArchVariant == kMips64r6) {
    local_monitor_.NotifyLoad();
    uint32_t* ptr = reinterpret_cast<uint32_t*>(addr);
    TraceMemRd(addr, static_cast<int64_t>(*ptr), WORD);
    return *ptr;
  }
  PrintF("Unaligned read at 0x%08" PRIx64 " , pc=0x%08" V8PRIxPTR "\n", addr,
         reinterpret_cast<intptr_t>(instr));
  DieOrDebug();
  return 0;
}

void Simulator::WriteW(int64_t addr, int32_t value, Instruction* instr) {
  if (addr >= 0 && addr < 0x400) {
    // This has to be a nullptr-dereference, drop into debugger.
    PrintF("Memory write to bad address: 0x%08" PRIx64 " , pc=0x%08" PRIxPTR
           " \n",
           addr, reinterpret_cast<intptr_t>(instr));
    DieOrDebug();
  }
  if ((addr & 0x3) == 0 || kArchVariant == kMips64r6) {
    local_monitor_.NotifyStore();
    base::MutexGuard lock_guard(&GlobalMonitor::Get()->mutex);
    GlobalMonitor::Get()->NotifyStore_Locked(&global_monitor_thread_);
    TraceMemWr(addr, value, WORD);
    int* ptr = reinterpret_cast<int*>(addr);
    *ptr = value;
    return;
  }
  PrintF("Unaligned write at 0x%08" PRIx64 " , pc=0x%08" V8PRIxPTR "\n", addr,
         reinterpret_cast<intptr_t>(instr));
  DieOrDebug();
}

void Simulator::WriteConditionalW(int64_t addr, int32_t value,
                                  Instruction* instr, int32_t rt_reg) {
  if (addr >= 0 && addr < 0x400) {
    // This has to be a nullptr-dereference, drop into debugger.
    PrintF("Memory write to bad address: 0x%08" PRIx64 " , pc=0x%08" PRIxPTR
           " \n",
           addr, reinterpret_cast<intptr_t>(instr));
    DieOrDebug();
  }
  if ((addr & 0x3) == 0 || kArchVariant == kMips64r6) {
    base::MutexGuard lock_guard(&GlobalMonitor::Get()->mutex);
    if (local_monitor_.NotifyStoreConditional(addr, TransactionSize::Word) &&
        GlobalMonitor::Get()->NotifyStoreConditional_Locked(
            addr, &global_monitor_thread_)) {
      local_monitor_.NotifyStore();
      GlobalMonitor::Get()->NotifyStore_Locked(&global_monitor_thread_);
      TraceMemWr(addr, value, WORD);
      int* ptr = reinterpret_cast<int*>(addr);
      *ptr = value;
      set_register(rt_reg, 1);
    } else {
      set_register(rt_reg, 0);
    }
    return;
  }
  PrintF("Unaligned write at 0x%08" PRIx64 " , pc=0x%08" V8PRIxPTR "\n", addr,
         reinterpret_cast<intptr_t>(instr));
  DieOrDebug();
}

int64_t Simulator::Read2W(int64_t addr, Instruction* instr) {
  if (addr >= 0 && addr < 0x400) {
    // This has to be a nullptr-dereference, drop into debugger.
    PrintF("Memory read from bad address: 0x%08" PRIx64 " , pc=0x%08" PRIxPTR
           " \n",
           addr, reinterpret_cast<intptr_t>(instr));
    DieOrDebug();
  }
  if ((addr & kPointerAlignmentMask) == 0 || kArchVariant == kMips64r6) {
    local_monitor_.NotifyLoad();
    int64_t* ptr = reinterpret_cast<int64_t*>(addr);
    TraceMemRd(addr, *ptr);
    return *ptr;
  }
  PrintF("Unaligned read at 0x%08" PRIx64 " , pc=0x%08" V8PRIxPTR "\n", addr,
         reinterpret_cast<intptr_t>(instr));
  DieOrDebug();
  return 0;
}

void Simulator::Write2W(int64_t addr, int64_t value, Instruction* instr) {
  if (addr >= 0 && addr < 0x400) {
    // This has to be a nullptr-dereference, drop into debugger.
    PrintF("Memory write to bad address: 0x%08" PRIx64 " , pc=0x%08" PRIxPTR
           "\n",
           addr, reinterpret_cast<intptr_t>(instr));
    DieOrDebug();
  }
  if ((addr & kPointerAlignmentMask) == 0 || kArchVariant == kMips64r6) {
    local_monitor_.NotifyStore();
    base::MutexGuard lock_guard(&GlobalMonitor::Get()->mutex);
    GlobalMonitor::Get()->NotifyStore_Locked(&global_monitor_thread_);
    TraceMemWr(addr, value, DWORD);
    int64_t* ptr = reinterpret_cast<int64_t*>(addr);
    *ptr = value;
    return;
  }
  PrintF("Unaligned write at 0x%08" PRIx64 " , pc=0x%08" V8PRIxPTR "\n", addr,
         reinterpret_cast<intptr_t>(instr));
  DieOrDebug();
}

void Simulator::WriteConditional2W(int64_t addr, int64_t value,
                                   Instruction* instr, int32_t rt_reg) {
  if (addr >= 0 && addr < 0x400) {
    // This has to be a nullptr-dereference, drop into debugger.
    PrintF("Memory write to bad address: 0x%08" PRIx64 " , pc=0x%08" PRIxPTR
           "\n",
           addr, reinterpret_cast<intptr_t>(instr));
    DieOrDebug();
  }
  if ((addr & kPointerAlignmentMask) == 0 || kArchVariant == kMips64r6) {
    base::MutexGuard lock_guard(&GlobalMonitor::Get()->mutex);
    if (local_monitor_.NotifyStoreConditional(addr,
                                              TransactionSize::DoubleWord) &&
        GlobalMonitor::Get()->NotifyStoreConditional_Locked(
            addr, &global_monitor_thread_)) {
      local_monitor_.NotifyStore();
      GlobalMonitor::Get()->NotifyStore_Locked(&global_monitor_thread_);
      TraceMemWr(addr, value, DWORD);
      int64_t* ptr = reinterpret_cast<int64_t*>(addr);
      *ptr = value;
      set_register(rt_reg, 1);
    } else {
      set_register(rt_reg, 0);
    }
    return;
  }
  PrintF("Unaligned write at 0x%08" PRIx64 " , pc=0x%08" V8PRIxPTR "\n", addr,
         reinterpret_cast<intptr_t>(instr));
  DieOrDebug();
}

double Simulator::ReadD(int64_t addr, Instruction* instr) {
  if ((addr & kDoubleAlignmentMask) == 0 || kArchVariant == kMips64r6) {
    local_monitor_.NotifyLoad();
    double* ptr = reinterpret_cast<double*>(addr);
    return *ptr;
  }
  PrintF("Unaligned (double) read at 0x%08" PRIx64 " , pc=0x%08" V8PRIxPTR "\n",
         addr, reinterpret_cast<intptr_t>(instr));
  base::OS::Abort();
}

void Simulator::WriteD(int64_t addr, double value, Instruction* instr) {
  if ((addr & kDoubleAlignmentMask) == 0 || kArchVariant == kMips64r6) {
    local_monitor_.NotifyStore();
    base::MutexGuard lock_guard(&GlobalMonitor::Get()->mutex);
    GlobalMonitor::Get()->NotifyStore_Locked(&global_monitor_thread_);
    double* ptr = reinterpret_cast<double*>(addr);
    *ptr = value;
    return;
  }
  PrintF("Unaligned (double) write at 0x%08" PRIx64 " , pc=0x%08" V8PRIxPTR
         "\n",
         addr, reinterpret_cast<intptr_t>(instr));
  DieOrDebug();
}

uint16_t Simulator::ReadHU(int64_t addr, Instruction* instr) {
  if ((addr & 1) == 0 || kArchVariant == kMips64r6) {
    local_monitor_.NotifyLoad();
    uint16_t* ptr = reinterpret_cast<uint16_t*>(addr);
    TraceMemRd(addr, static_cast<int64_t>(*ptr));
    return *ptr;
  }
  PrintF("Unaligned unsigned halfword read at 0x%08" PRIx64
         " , pc=0x%08" V8PRIxPTR "\n",
         addr, reinterpret_cast<intptr_t>(instr));
  DieOrDebug();
  return 0;
}

int16_t Simulator::ReadH(int64_t addr, Instruction* instr) {
  if ((addr & 1) == 0 || kArchVariant == kMips64r6) {
    local_monitor_.NotifyLoad();
    int16_t* ptr = reinterpret_cast<int16_t*>(addr);
    TraceMemRd(addr, static_cast<int64_t>(*ptr));
    return *ptr;
  }
  PrintF("Unaligned signed halfword read at 0x%08" PRIx64
         " , pc=0x%08" V8PRIxPTR "\n",
         addr, reinterpret_cast<intptr_t>(instr));
  DieOrDebug();
  return 0;
}

void Simulator::WriteH(int64_t addr, uint16_t value, Instruction* instr) {
  if ((addr & 1) == 0 || kArchVariant == kMips64r6) {
    local_monitor_.NotifyStore();
    base::MutexGuard lock_guard(&GlobalMonitor::Get()->mutex);
    GlobalMonitor::Get()->NotifyStore_Locked(&global_monitor_thread_);
    TraceMemWr(addr, value, HALF);
    uint16_t* ptr = reinterpret_cast<uint16_t*>(addr);
    *ptr = value;
    return;
  }
  PrintF("Unaligned unsigned halfword write at 0x%08" PRIx64
         " , pc=0x%08" V8PRIxPTR "\n",
         addr, reinterpret_cast<intptr_t>(instr));
  DieOrDebug();
}

void Simulator::WriteH(int64_t addr, int16_t value, Instruction* instr) {
  if ((addr & 1) == 0 || kArchVariant == kMips64r6) {
    local_monitor_.NotifyStore();
    base::MutexGuard lock_guard(&GlobalMonitor::Get()->mutex);
    GlobalMonitor::Get()->NotifyStore_Locked(&global_monitor_thread_);
    TraceMemWr(addr, value, HALF);
    int16_t* ptr = reinterpret_cast<int16_t*>(addr);
    *ptr = value;
    return;
  }
  PrintF("Unaligned halfword write at 0x%08" PRIx64 " , pc=0x%08" V8PRIxPTR
         "\n",
         addr, reinterpret_cast<intptr_t>(instr));
  DieOrDebug();
}

uint32_t Simulator::ReadBU(int64_t addr) {
  local_monitor_.NotifyLoad();
  uint8_t* ptr = reinterpret_cast<uint8_t*>(addr);
  TraceMemRd(addr, static_cast<int64_t>(*ptr));
  return *ptr & 0xFF;
}

int32_t Simulator::ReadB(int64_t addr) {
  local_monitor_.NotifyLoad();
  int8_t* ptr = reinterpret_cast<int8_t*>(addr);
  TraceMemRd(addr, static_cast<int64_t>(*ptr));
  return *ptr;
}

void Simulator::WriteB(int64_t addr, uint8_t value) {
  local_monitor_.NotifyStore();
  base::MutexGuard lock_guard(&GlobalMonitor::Get()->mutex);
  GlobalMonitor::Get()->NotifyStore_Locked(&global_monitor_thread_);
  TraceMemWr(addr, value, BYTE);
  uint8_t* ptr = reinterpret_cast<uint8_t*>(addr);
  *ptr = value;
}

void Simulator::WriteB(int64_t addr, int8_t value) {
  local_monitor_.NotifyStore();
  base::MutexGuard lock_guard(&GlobalMonitor::Get()->mutex);
  GlobalMonitor::Get()->NotifyStore_Locked(&global_monitor_thread_);
  TraceMemWr(addr, value, BYTE);
  int8_t* ptr = reinterpret_cast<int8_t*>(addr);
  *ptr = value;
}

template <typename T>
T Simulator::ReadMem(int64_t addr, Instruction* instr) {
  int alignment_mask = (1 << sizeof(T)) - 1;
  if ((addr & alignment_mask) == 0 || kArchVariant == kMips64r6) {
    local_monitor_.NotifyLoad();
    T* ptr = reinterpret_cast<T*>(addr);
    TraceMemRd(addr, *ptr);
    return *ptr;
  }
  PrintF("Unaligned read of type sizeof(%ld) at 0x%08lx, pc=0x%08" V8PRIxPTR
         "\n",
         sizeof(T), addr, reinterpret_cast<intptr_t>(instr));
  base::OS::Abort();
  return 0;
}

template <typename T>
void Simulator::WriteMem(int64_t addr, T value, Instruction* instr) {
  int alignment_mask = (1 << sizeof(T)) - 1;
  if ((addr & alignment_mask) == 0 || kArchVariant == kMips64r6) {
    local_monitor_.NotifyStore();
    base::MutexGuard lock_guard(&GlobalMonitor::Get()->mutex);
    GlobalMonitor::Get()->NotifyStore_Locked(&global_monitor_thread_);
    T* ptr = reinterpret_cast<T*>(addr);
    *ptr = value;
    TraceMemWr(addr, value);
    return;
  }
  PrintF("Unaligned write of type sizeof(%ld) at 0x%08lx, pc=0x%08" V8PRIxPTR
         "\n",
         sizeof(T), addr, reinterpret_cast<intptr_t>(instr));
  base::OS::Abort();
}

// Returns the limit of the stack area to enable checking for stack overflows.
uintptr_t Simulator::StackLimit(uintptr_t c_limit) const {
  // The simulator uses a separate JS stack. If we have exhausted the C stack,
  // we also drop down the JS limit to reflect the exhaustion on the JS stack.
  if (base::Stack::GetCurrentStackPosition() < c_limit) {
    return get_sp();
  }

  // Otherwise the limit is the JS stack. Leave a safety margin
  // to prevent overrunning the stack when pushing values.
  return stack_limit_ + kAdditionalStackMargin;
}

base::Vector<uint8_t> Simulator::GetCurrentStackView() const {
  // We do not add an additional safety margin as above in
  // Simulator::StackLimit, as users of this method are expected to add their
  // own margin.
  return base::VectorOf(reinterpret_cast<uint8_t*>(stack_limit_),
                        UsableStackSize());
}

// Unsupported instructions use Format to print an error and stop execution.
void Simulator::Format(Instruction* instr, const char* format) {
  PrintF("Simulator found unsupported instruction:\n 0x%08" PRIxPTR " : %s\n",
         reinterpret_cast<intptr_t>(instr), format);
  UNIMPLEMENTED_MIPS();
}

// Calls into the V8 runtime are based on this very simple interface.
// Note: To be able to return two values from some calls the code in runtime.cc
// uses the ObjectPair which is essentially two 32-bit values stuffed into a
// 64-bit value. With the code below we assume that all runtime calls return
// 64 bits of result. If they don't, the v1 result register contains a bogus
// value, which is fine because it is caller-saved.
using SimulatorRuntimeCall = ObjectPair (*)(
    int64_t arg0, int64_t arg1, int64_t arg2, int64_t arg3, int64_t arg4,
    int64_t arg5, int64_t arg6, int64_t arg7, int64_t arg8, int64_t arg9,
    int64_t arg10, int64_t arg11, int64_t arg12, int64_t arg13, int64_t arg14,
    int64_t arg15, int64_t arg16, int64_t arg17, int64_t arg18, int64_t arg19);

// These prototypes handle the four types of FP calls.
using SimulatorRuntimeCompareCall = int64_t (*)(double darg0, double darg1);
using SimulatorRuntimeFPFPCall = double (*)(double darg0, double darg1);
using SimulatorRuntimeFPCall = double (*)(double darg0);
using SimulatorRuntimeFPIntCall = double (*)(double darg0, int32_t arg0);
// Define four args for future flexibility; at the time of this writing only
// one is ever used.
using SimulatorRuntimeFPTaggedCall = double (*)(int64_t arg0, int64_t arg1,
                                                int64_t arg2, int64_t arg3);

// This signature supports direct call in to API function native callback
// (refer to InvocationCallback in v8.h).
using SimulatorRuntimeDirectApiCall = void (*)(int64_t arg0);

// This signature supports direct call to accessor getter callback.
using SimulatorRuntimeDirectGetterCall = void (*)(int64_t arg0, int64_t arg1);

using MixedRuntimeCall_0 = AnyCType (*)();

#define BRACKETS(ident, N) ident[N]

#define REP_0(expr, FMT)
#define REP_1(expr, FMT) FMT(expr, 0)
#define REP_2(expr, FMT) REP_1(expr, FMT), FMT(expr, 1)
#define REP_3(expr, FMT) REP_2(expr, FMT), FMT(expr, 2)
#define REP_4(expr, FMT) REP_3(expr, FMT), FMT(expr, 3)
#define REP_5(expr, FMT) REP_4(expr, FMT), FMT(expr, 4)
#define REP_6(expr, FMT) REP_5(expr, FMT), FMT(expr, 5)
#define REP_7(expr, FMT) REP_6(expr, FMT), FMT(expr, 6)
#define REP_8(expr, FMT) REP_7(expr, FMT), FMT(expr, 7)
#define REP_9(expr, FMT) REP_8(expr, FMT), FMT(expr, 8)
#define REP_10(expr, FMT) REP_9(expr, FMT), FMT(expr, 9)
#define REP_11(expr, FMT) REP_10(expr, FMT), FMT(expr, 10)
#define REP_12(expr, FMT) REP_11(expr, FMT), FMT(expr, 11)
#define REP_13(expr, FMT) REP_12(expr, FMT), FMT(expr, 12)
#define REP_14(expr, FMT) REP_13(expr, FMT), FMT(expr, 13)
#define REP_15(expr, FMT) REP_14(expr, FMT), FMT(expr, 14)
#define REP_16(expr, FMT) REP_15(expr, FMT), FMT(expr, 15)
#define REP_17(expr, FMT) REP_16(expr, FMT), FMT(expr, 16)
#define REP_18(expr, FMT) REP_17(expr, FMT), FMT(expr, 17)
#define REP_19(expr, FMT) REP_18(expr, FMT), FMT(expr, 18)
#define REP_20(expr, FMT) REP_19(expr, FMT), FMT(expr, 19)

#define GEN_MAX_PARAM_COUNT(V) \
  V(0)                         \
  V(1)                         \
  V(2)                         \
  V(3)                         \
  V(4)                         \
  V(5)                         \
  V(6)                         \
  V(7)                         \
  V(8)                         \
  V(9)                         \
  V(10)                        \
  V(11)                        \
  V(12)                        \
  V(13)                        \
  V(14)                        \
  V(15)                        \
  V(16)                        \
  V(17)                        \
  V(18)                        \
  V(19)                        \
  V(20)

#define MIXED_RUNTIME_CALL(N) \
  using MixedRuntimeCall_##N = AnyCType (*)(REP_##N(AnyCType arg, CONCAT));

GEN_MAX_PARAM_COUNT(MIXED_RUNTIME_CALL)
#undef MIXED_RUNTIME_CALL

#define CALL_ARGS(N) REP_##N(args, BRACKETS)
#define CALL_TARGET_VARARG(N)                                   \
  if (signature.ParameterCount() == N) { /* NOLINT */           \
    MixedRuntimeCall_##N target =                               \
        reinterpret_cast<MixedRuntimeCall_##N>(target_address); \
    result = target(CALL_ARGS(N));                              \
  } else /* NOLINT */

#define PARAM_REGISTERS a0, a1, a2, a3, a4, a5, a6, a7
#define RETURN_REGISTER v0
#define FP_PARAM_REGISTERS f12, f13, f14, f15, f16, f17, f18, f19
#define FP_RETURN_REGISTER f0

void Simulator::CallAnyCTypeFunction(Address target_address,
                                     const EncodedCSignature& signature) {
  const int64_t* stack_pointer = reinterpret_cast<int64_t*>(get_register(sp));
  const double* double_stack_pointer =
      reinterpret_cast<double*>(get_register(sp));

  const Register kParamRegisters[] = {PARAM_REGISTERS};
  const FPURegister kFPParamRegisters[] = {FP_PARAM_REGISTERS};

  int num_reg_params = 0, num_stack_params = 0;

  CHECK_LE(signature.ParameterCount(), kMaxCParameters);
  static_assert(sizeof(AnyCType) == 8, "AnyCType is assumed to be 64-bit.");
  AnyCType args[kMaxCParameters];
  for (int i = 0; i < signature.ParameterCount(); ++i) {
    if (num_reg_params < 8) {
      if (signature.IsFloat(i)) {
        args[i].double_value =
            get_fpu_register_double(kFPParamRegisters[num_reg_params++]);
      } else {
        args[i].int64_value = get_register(kParamRegisters[num_reg_params++]);
      }
    } else {
      if (signature.IsFloat(i)) {
        args[i].double_value = double_stack_pointer[num_stack_params++];
      } else {
        args[i].int64_value = stack_pointer[num_stack_params++];
      }
    }
  }
  AnyCType result;
  GEN_MAX_PARAM_COUNT(CALL_TARGET_VARARG)
  /* else */ {
    UNREACHABLE();
  }
  static_assert(20 == kMaxCParameters,
                "If you've changed kMaxCParameters, please change the "
                "GEN_MAX_PARAM_COUNT macro.");
  printf("CallAnyCTypeFunction end result \n");

#undef CALL_TARGET_VARARG
#undef CALL_ARGS
#undef GEN_MAX_PARAM_COUNT

  if (signature.IsReturnFloat()) {
    set_fpu_register_double(FP_RETURN_REGISTER, result.double_value);
  } else {
    set_register(RETURN_REGISTER, result.int64_value);
  }
}

#undef PARAM_REGISTERS
#undef RETURN_REGISTER
#undef FP_PARAM_REGISTERS
#undef FP_RETURN_REGISTER

// Software interrupt instructions are used by the simulator to call into the
// C-based V8 runtime. They are also used for debugging with simulator.
void Simulator::SoftwareInterrupt() {
  // There are several instructions that could get us here,
  // the break_ instruction, or several variants of traps. All
  // Are "SPECIAL" class opcode, and are distinuished by function.
  int32_t func = instr_.FunctionFieldRaw();
  uint32_t code = (func == BREAK) ? instr_.Bits(25, 6) : -1;
  // We first check if we met a call_rt_redirected.
  if (instr_.InstructionBits() == rtCallRedirInstr) {
    Redirection* redirection = Redirection::FromInstruction(instr_.instr());

    // This is dodgy but it works because the C entry stubs are never moved.
    int64_t saved_ra = get_register(ra);

    intptr_t external =
        reinterpret_cast<intptr_t>(redirection->external_function());

    Address func_addr =
        reinterpret_cast<Address>(redirection->external_function());
    SimulatorData* simulator_data = isolate_->simulator_data();
    DCHECK_NOT_NULL(simulator_data);
    const EncodedCSignature& signature =
        simulator_data->GetSignatureForTarget(func_addr);
    if (signature.IsValid()) {
      CHECK_EQ(redirection->type(), ExternalReference::FAST_C_CALL);
      CallAnyCTypeFunction(external, signature);
      set_register(ra, saved_ra);
      set_pc(get_register(ra));
      return;
    }

    int64_t* stack_pointer = reinterpret_cast<int64_t*>(get_register(sp));

    int64_t arg0 = get_register(a0);
    int64_t arg1 = get_register(a1);
    int64_t arg2 = get_register(a2);
    int64_t arg3 = get_register(a3);
    int64_t arg4 = get_register(a4);
    int64_t arg5 = get_register(a5);
    int64_t arg6 = get_register(a6);
    int64_t arg7 = get_register(a7);
    int64_t arg8 = stack_pointer[0];
    int64_t arg9 = stack_pointer[1];
    int64_t arg10 = stack_pointer[2];
    int64_t arg11 = stack_pointer[3];
    int64_t arg12 = stack_pointer[4];
    int64_t arg13 = stack_pointer[5];
    int64_t arg14 = stack_pointer[6];
    int64_t arg15 = stack_pointer[7];
    int64_t arg16 = stack_pointer[8];
    int64_t arg17 = stack_pointer[9];
    int64_t arg18 = stack_pointer[10];
    int64_t arg19 = stack_pointer[11];
    static_assert(kMaxCParameters == 20);

    bool fp_call =
        (redirection->type() == ExternalReference::BUILTIN_FP_FP_CALL) ||
        (redirection->type() == ExternalReference::BUILTIN_COMPARE_CALL) ||
        (redirection->type() == ExternalReference::BUILTIN_FP_CALL) ||
        (redirection->type() == ExternalReference::BUILTIN_FP_INT_CALL);

    if (!IsMipsSoftFloatABI) {
      // With the hard floating point calling convention, double
      // arguments are passed in FPU registers. Fetch the arguments
      // from there and call the builtin using soft floating point
      // convention.
      switch (redirection->type()) {
        case ExternalReference::BUILTIN_FP_FP_CALL:
        case ExternalReference::BUILTIN_COMPARE_CALL:
          arg0 = get_fpu_register(f12);
          arg1 = get_fpu_register(f13);
          arg2 = get_fpu_register(f14);
          arg3 = get_fpu_register(f15);
          break;
        case ExternalReference::BUILTIN_FP_CALL:
          arg0 = get_fpu_register(f12);
          arg1 = get_fpu_register(f13);
          break;
        case ExternalReference::BUILTIN_FP_INT_CALL:
          arg0 = get_fpu_register(f12);
          arg1 = get_fpu_register(f13);
          arg2 = get_register(a2);
          break;
        default:
          break;
      }
    }

    // Based on CpuFeatures::IsSupported(FPU), Mips will use either hardware
    // FPU, or gcc soft-float routines. Hardware FPU is simulated in this
    // simulator. Soft-float has additional abstraction of ExternalReference,
    // to support serialization.
    if (fp_call) {
      double dval0, dval1;  // one or two double parameters
      int32_t ival;         // zero or one integer parameters
      int64_t iresult = 0;  // integer return value
      double dresult = 0;   // double return value
      GetFpArgs(&dval0, &dval1, &ival);
      SimulatorRuntimeCall generic_target =
          reinterpret_cast<SimulatorRuntimeCall>(external);
      if (v8_flags.trace_sim) {
        switch (redirection->type()) {
          case ExternalReference::BUILTIN_FP_FP_CALL:
          case ExternalReference::BUILTIN_COMPARE_CALL:
            PrintF("Call to host function at %p with args %f, %f",
                   reinterpret_cast<void*>(FUNCTION_ADDR(generic_target)),
                   dval0, dval1);
            break;
          case ExternalReference::BUILTIN_FP_CALL:
            PrintF("Call to host function at %p with arg %f",
                   reinterpret_cast<void*>(FUNCTION_ADDR(generic_target)),
                   dval0);
            break;
          case ExternalReference::BUILTIN_FP_INT_CALL:
            PrintF("Call to host function at %p with args %f, %d",
                   reinterpret_cast<void*>(FUNCTION_ADDR(generic_target)),
                   dval0, ival);
            break;
          default:
            UNREACHABLE();
        }
      }
      switch (redirection->type()) {
        case ExternalReference::BUILTIN_COMPARE_CALL: {
          SimulatorRuntimeCompareCall target =
              reinterpret_cast<SimulatorRuntimeCompareCall>(external);
          iresult = target(dval0, dval1);
          set_register(v0, static_cast<int64_t>(iresult));
          //  set_register(v1, static_cast<int64_t>(iresult >> 32));
          break;
        }
        case ExternalReference::BUILTIN_FP_FP_CALL: {
          SimulatorRuntimeFPFPCall target =
              reinterpret_cast<SimulatorRuntimeFPFPCall>(external);
          dresult = target(dval0, dval1);
          SetFpResult(dresult);
          break;
        }
        case ExternalReference::BUILTIN_FP_CALL: {
          SimulatorRuntimeFPCall target =
              reinterpret_cast<SimulatorRuntimeFPCall>(external);
          dresult = target(dval0);
          SetFpResult(dresult);
          break;
        }
        case ExternalReference::BUILTIN_FP_INT_CALL: {
          SimulatorRuntimeFPIntCall target =
              reinterpret_cast<SimulatorRuntimeFPIntCall>(external);
          dresult = target(dval0, ival);
          SetFpResult(dresult);
          break;
        }
        default:
          UNREACHABLE();
      }
      if (v8_flags.trace_sim) {
        switch (redirection->type()) {
          case ExternalReference::BUILTIN_COMPARE_CALL:
            PrintF("Returned %08x\n", static_cast<int32_t>(iresult));
            break;
          case ExternalReference::BUILTIN_FP_FP_CALL:
          case ExternalReference::BUILTIN_FP_CALL:
          case ExternalReference::BUILTIN_FP_INT_CALL:
            PrintF("Returned %f\n", dresult);
            break;
          default:
            UNREACHABLE();
        }
      }
    } else if (redirection->type() ==
               ExternalReference::BUILTIN_FP_POINTER_CALL) {
      if (v8_flags.trace_sim) {
        PrintF("Call to host function at %p args %08" PRIx64 " \n",
               reinterpret_cast<void*>(external), arg0);
      }
      SimulatorRuntimeFPTaggedCall target =
          reinterpret_cast<SimulatorRuntimeFPTaggedCall>(external);
      double dresult = target(arg0, arg1, arg2, arg3);
      SetFpResult(dresult);
      if (v8_flags.trace_sim) {
        PrintF("Returned %f\n", dresult);
      }
    } else if (redirection->type() == ExternalReference::DIRECT_API_CALL) {
      if (v8_flags.trace_sim) {
        PrintF("Call to host function at %p args %08" PRIx64 " \n",
               reinterpret_cast<void*>(external), arg0);
      }
      SimulatorRuntimeDirectApiCall target =
          reinterpret_cast<SimulatorRuntimeDirectApiCall>(external);
      target(arg0);
    } else if (redirection->type() == ExternalReference::DIRECT_GETTER_CALL) {
      if (v8_flags.trace_sim) {
        PrintF("Call to host function at %p args %08" PRIx64 "  %08" PRIx64
               " \n",
               reinterpret_cast<void*>(external), arg0, arg1);
      }
      SimulatorRuntimeDirectGetterCall target =
          reinterpret_cast<SimulatorRuntimeDirectGetterCall>(external);
      target(arg0, arg1);
    } else {
      DCHECK(redirection->type() == ExternalReference::BUILTIN_CALL ||
             redirection->type() == ExternalReference::BUILTIN_CALL_PAIR);
      SimulatorRuntimeCall target =
          reinterpret_cast<SimulatorRuntimeCall>(external);
      if (v8_flags.trace_sim) {
        PrintF(
            "Call to host function at %p "
            "args %08" PRIx64 " , %08" PRIx64 " , %08" PRIx64 " , %08" PRIx64
            " , %08" PRIx64 " , %08" PRIx64 " , %08" PRIx64 " , %08" PRIx64
            " , %08" PRIx64 " , %08" PRIx64 " , %08" PRIx64 " , %08" PRIx64
            " , %08" PRIx64 " , %08" PRIx64 " , %08" PRIx64 " , %08" PRIx64
            " , %08" PRIx64 " , %08" PRIx64 " , %08" PRIx64 " , %08" PRIx64
            " \n",
            reinterpret_cast<void*>(FUNCTION_ADDR(target)), arg0, arg1, arg2,
            arg3, arg4, arg5, arg6, arg7, arg8, arg9, arg10, arg11, arg12,
            arg13, arg14, arg15, arg16, arg17, arg18, arg19);
      }
      ObjectPair result = target(arg0, arg1, arg2, arg3, arg4, arg5, arg6, arg7,
                                 arg8, arg9, arg10, arg11, arg12, arg13, arg14,
                                 arg15, arg16, arg17, arg18, arg19);
      set_register(v0, (int64_t)(result.x));
      set_register(v1, (int64_t)(result.y));
    }
    if (v8_flags.trace_sim) {
      PrintF("Returned %08" PRIx64 "  : %08" PRIx64 " \n", get_register(v1),
             get_register(v0));
    }
    set_register(ra, saved_ra);
    set_pc(get_register(ra));

  } else if (func == BREAK && code <= kMaxStopCode) {
    if (IsWatchpoint(code)) {
      PrintWatchpoint(code);
    } else {
      IncreaseStopCounter(code);
      HandleStop(code, instr_.instr());
    }
  } else {
    // All remaining break_ codes, and all traps are handled here.
    MipsDebugger dbg(this);
    dbg.Debug();
  }
}

// Stop helper functions.
bool Simulator::IsWatchpoint(uint64_t code) {
  return (code <= kMaxWatchpointCode);
}

void Simulator::PrintWatchpoint(uint64_t code) {
  MipsDebugger dbg(this);
  ++break_count_;
  PrintF("\n---- break %" PRId64 "  marker: %3d  (instr count: %8" PRId64
         " ) ----------"
         "----------------------------------",
         code, break_count_, icount_);
  dbg.PrintAllRegs();  // Print registers and continue running.
}

void Simulator::HandleStop(uint64_t code, Instruction* instr) {
  // Stop if it is enabled, otherwise go on jumping over the stop
  // and the message address.
  if (IsEnabledStop(code)) {
    MipsDebugger dbg(this);
    dbg.Stop(instr);
  }
}

bool Simulator::IsStopInstruction(Instruction* instr) {
  int32_t func = instr->FunctionFieldRaw();
  uint32_t code = static_cast<uint32_t>(instr->Bits(25, 6));
  return (func == BREAK) && code > kMaxWatchpointCode && code <= kMaxStopCode;
}

bool Simulator::IsEnabledStop(uint64_t code) {
  DCHECK_LE(code, kMaxStopCode);
  DCHECK_GT(code, kMaxWatchpointCode);
  return !(watched_stops_[code].count & kStopDisabledBit);
}

void Simulator::EnableStop(uint64_t code) {
  if (!IsEnabledStop(code)) {
    watched_stops_[code].count &= ~kStopDisabledBit;
  }
}

void Simulator::DisableStop(uint64_t code) {
  if (IsEnabledStop(code)) {
    watched_stops_[code].count |= kStopDisabledBit;
  }
}

void Simulator::IncreaseStopCounter(uint64_t code) {
  DCHECK_LE(code, kMaxStopCode);
  if ((watched_stops_[code].count & ~(1 << 31)) == 0x7FFFFFFF) {
    PrintF("Stop counter for code %" PRId64
           "  has overflowed.\n"
           "Enabling this code and reseting the counter to 0.\n",
           code);
    watched_stops_[code].count = 0;
    EnableStop(code);
  } else {
    watched_stops_[code].count++;
  }
}

// Print a stop status.
void Simulator::PrintStopInfo(uint64_t code) {
  if (code <= kMaxWatchpointCode) {
    PrintF("That is a watchpoint, not a stop.\n");
    return;
  } else if (code > kMaxStopCode) {
    PrintF("Code too large, only %u stops can be used\n", kMaxStopCode + 1);
    return;
  }
  const char* state = IsEnabledStop(code) ? "Enabled" : "Disabled";
  int32_t count = watched_stops_[code].count & ~kStopDisabledBit;
  // Don't print the state of unused breakpoints.
  if (count != 0) {
    if (watched_stops_[code].desc) {
      PrintF("stop %" PRId64 "  - 0x%" PRIx64 " : \t%s, \tcounter = %i, \t%s\n",
             code, code, state, count, watched_stops_[code].desc);
    } else {
      PrintF("stop %" PRId64 "  - 0x%" PRIx64 " : \t%s, \tcounter = %i\n", code,
             code, state, count);
    }
  }
}

void Simulator::SignalException(Exception e) {
  FATAL("Error: Exception %i raised.", static_cast<int>(e));
}

// Min/Max template functions for Double and Single arguments.

template <typename T>
static T FPAbs(T a);

template <>
double FPAbs<double>(double a) {
  return fabs(a);
}

template <>
float FPAbs<float>(float a) {
  return fabsf(a);
}

template <typename T>
static bool FPUProcessNaNsAndZeros(T a, T b, MaxMinKind kind, T* result) {
  if (std::isnan(a) && std::isnan(b)) {
    *result = a;
  } else if (std::isnan(a)) {
    *result = b;
  } else if (std::isnan(b)) {
    *result = a;
  } else if (b == a) {
    // Handle -0.0 == 0.0 case.
    // std::signbit() returns int 0 or 1 so subtracting MaxMinKind::kMax
    // negates the result.
    *result = std::signbit(b) - static_cast<int>(kind) ? b : a;
  } else {
    return false;
  }
  return true;
}

template <typename T>
static T FPUMin(T a, T b) {
  T result;
  if (FPUProcessNaNsAndZeros(a, b, MaxMinKind::kMin, &result)) {
    return result;
  } else {
    return b < a ? b : a;
  }
}

template <typename T>
static T FPUMax(T a, T b) {
  T result;
  if (FPUProcessNaNsAndZeros(a, b, MaxMinKind::kMax, &result)) {
    return result;
  } else {
    return b > a ? b : a;
  }
}

template <typename T>
static T FPUMinA(T a, T b) {
  T result;
  if (!FPUProcessNaNsAndZeros(a, b, MaxMinKind::kMin, &result)) {
    if (FPAbs(a) < FPAbs(b)) {
      result = a;
    } else if (FPAbs(b) < FPAbs(a)) {
      result = b;
    } else {
      result = a < b ? a : b;
    }
  }
  return result;
}

template <typename T>
static T FPUMaxA(T a, T b) {
  T result;
  if (!FPUProcessNaNsAndZeros(a, b, MaxMinKind::kMin, &result)) {
    if (FPAbs(a) > FPAbs(b)) {
      result = a;
    } else if (FPAbs(b) > FPAbs(a)) {
      result = b;
    } else {
      result = a > b ? a : b;
    }
  }
  return result;
}

enum class KeepSign : bool { no = false, yes };

template <typename T, typename std::enable_if<std::is_floating_point<T>::value,
                                              int>::type = 0>
T FPUCanonalizeNaNArg(T result, T arg, KeepSign keepSign = KeepSign::no) {
  DCHECK(std::isnan(arg));
  T qNaN = std::numeric_limits<T>::quiet_NaN();
  if (keepSign == KeepSign::yes) {
    return std::copysign(qNaN, result);
  }
  return qNaN;
}

template <typename T>
T FPUCanonalizeNaNArgs(T result, KeepSign keepSign, T first) {
  if (std::isnan(first)) {
    return FPUCanonalizeNaNArg(result, first, keepSign);
  }
  return result;
}

template <typename T, typename... Args>
T FPUCanonalizeNaNArgs(T result, KeepSign keepSign, T first, Args... args) {
  if (std::isnan(first)) {
    return FPUCanonalizeNaNArg(result, first, keepSign);
  }
  return FPUCanonalizeNaNArgs(result, keepSign, args...);
}

template <typename Func, typename T, typename... Args>
T FPUCanonalizeOperation(Func f, T first, Args... args) {
  return FPUCanonalizeOperation(f, KeepSign::no, first, args...);
}

template <typename Func, typename T, typename... Args>
T FPUCanonalizeOperation(Func f, KeepSign keepSign, T first, Args... args) {
  T result = f(first, args...);
  if (std::isnan(result)) {
    result = FPUCanonalizeNaNArgs(result, keepSign, first, args...);
  }
  return result;
}

// Handle execution based on instruction types.

void Simulator::DecodeTypeRegisterSRsType() {
  float fs, ft, fd;
  fs = get_fpu_register_float(fs_reg());
  ft = get_fpu_register_float(ft_reg());
  fd = get_fpu_register_float(fd_reg());
  int32_t ft_int = base::bit_cast<int32_t>(ft);
  int32_t fd_int = base::bit_cast<int32_t>(fd);
  uint32_t cc, fcsr_cc;
  cc = instr_.FCccValue();
  fcsr_cc = get_fcsr_condition_bit(cc);
  switch (instr_.FunctionFieldRaw()) {
    case RINT: {
      DCHECK_EQ(kArchVariant, kMips64r6);
      float result, temp_result;
      double temp;
      float upper = std::ceil(fs);
      float lower = std::floor(fs);
      switch (get_fcsr_rounding_mode()) {
        case kRoundToNearest:
          if (upper - fs < fs - lower) {
            result = upper;
          } else if (upper - fs > fs - lower) {
            result = lower;
          } else {
            temp_result = upper / 2;
            float reminder = modf(temp_result, &temp);
            if (reminder == 0) {
              result = upper;
            } else {
              result = lower;
            }
          }
          break;
        case kRoundToZero:
          result = (fs > 0 ? lower : upper);
          break;
        case kRoundToPlusInf:
          result = upper;
          break;
        case kRoundToMinusInf:
          result = lower;
          break;
      }
      SetFPUFloatResult(fd_reg(), result);
      if (result != fs) {
        set_fcsr_bit(kFCSRInexactFlagBit, true);
      }
      break;
    }
    case ADD_S:
      SetFPUFloatResult(
          fd_reg(),
          FPUCanonalizeOperation([](float lhs, float rhs) { return lhs + rhs; },
                                 fs, ft));
      break;
    case SUB_S:
      SetFPUFloatResult(
          fd_reg(),
          FPUCanonalizeOperation([](float lhs, float rhs) { return lhs - rhs; },
                                 fs, ft));
      break;
    case MADDF_S:
      DCHECK_EQ(kArchVariant, kMips64r6);
      SetFPUFloatResult(fd_reg(), std::fma(fs, ft, fd));
      break;
    case MSUBF_S:
      DCHECK_EQ(kArchVariant, kMips64r6);
      SetFPUFloatResult(fd_reg(), std::fma(-fs, ft, fd));
      break;
    case MUL_S:
      SetFPUFloatResult(
          fd_reg(),
          FPUCanonalizeOperation([](float lhs, float rhs) { return lhs * rhs; },
                                 fs, ft));
      break;
    case DIV_S:
      SetFPUFloatResult(
          fd_reg(),
          FPUCanonalizeOperation([](float lhs, float rhs) { return lhs / rhs; },
                                 fs, ft));
      break;
    case ABS_S:
      SetFPUFloatResult(fd_reg(), FPUCanonalizeOperation(
                                      [](float fs) { return FPAbs(fs); }, fs));
      break;
    case MOV_S:
      SetFPUFloatResult(fd_reg(), fs);
      break;
    case NEG_S:
      SetFPUFloatResult(fd_reg(),
                        FPUCanonalizeOperation([](float src) { return -src; },
                                               KeepSign::yes, fs));
      break;
    case SQRT_S:
      SetFPUFloatResult(
          fd_reg(),
          FPUCanonalizeOperation([](float src) { return std::sqrt(src); }, fs));
      break;
    case RSQRT_S:
      SetFPUFloatResult(
          fd_reg(), FPUCanonalizeOperation(
                        [](float src) { return 1.0 / std::sqrt(src); }, fs));
      break;
    case RECIP_S:
      SetFPUFloatResult(fd_reg(), FPUCanonalizeOperation(
                                      [](float src) { return 1.0 / src; }, fs));
      break;
    case C_F_D:
      set_fcsr_bit(fcsr_cc, false);
      TraceRegWr(test_fcsr_bit(fcsr_cc));
      break;
    case C_UN_D:
      set_fcsr_bit(fcsr_cc, std::isnan(fs) || std::isnan(ft));
      TraceRegWr(test_fcsr_bit(fcsr_cc));
      break;
    case C_EQ_D:
      set_fcsr_bit(fcsr_cc, (fs == ft));
      TraceRegWr(test_fcsr_bit(fcsr_cc));
      break;
    case C_UEQ_D:
      set_fcsr_bit(fcsr_cc, (fs == ft) || (std::isnan(fs) || std::isnan(ft)));
      TraceRegWr(test_fcsr_bit(fcsr_cc));
      break;
    case C_OLT_D:
      set_fcsr_bit(fcsr_cc, (fs < ft));
      TraceRegWr(test_fcsr_bit(fcsr_cc));
      break;
    case C_ULT_D:
      set_fcsr_bit(fcsr_cc, (fs < ft) || (std::isnan(fs) || std::isnan(ft)));
      TraceRegWr(test_fcsr_bit(fcsr_cc));
      break;
    case C_OLE_D:
      set_fcsr_bit(fcsr_cc, (fs <= ft));
      TraceRegWr(test_fcsr_bit(fcsr_cc));
      break;
    case C_ULE_D:
      set_fcsr_bit(fcsr_cc, (fs <= ft) || (std::isnan(fs) || std::isnan(ft)));
      TraceRegWr(test_fcsr_bit(fcsr_cc));
      break;
    case CVT_D_S:
      SetFPUDoubleResult(fd_reg(), static_cast<double>(fs));
      break;
    case CLASS_S: {  // Mips64r6 instruction
      // Convert float input to uint32_t for easier bit manipulation
      uint32_t classed = base::bit_cast<uint32_t>(fs);

      // Extracting sign, exponent and mantissa from the input float
      uint32_t sign = (classed >> 31) & 1;
      uint32_t exponent = (classed >> 23) & 0x000000FF;
      uint32_t mantissa = classed & 0x007FFFFF;
      uint32_t result;
      float fResult;

      // Setting flags if input float is negative infinity,
      // positive infinity, negative zero or positive zero
      bool negInf = (classed == 0xFF800000);
      bool posInf = (classed == 0x7F800000);
      bool negZero = (classed == 0x80000000);
      bool posZero = (classed == 0x00000000);

      bool signalingNan;
      bool quietNan;
      bool negSubnorm;
      bool posSubnorm;
      bool negNorm;
      bool posNorm;

      // Setting flags if float is NaN
      signalingNan = false;
      quietNan = false;
      if (!negInf && !posInf && (exponent == 0xFF)) {
        quietNan = ((mantissa & 0x00200000) == 0) &&
                   ((mantissa & (0x00200000 - 1)) == 0);
        signalingNan = !quietNan;
      }

      // Setting flags if float is subnormal number
      posSubnorm = false;
      negSubnorm = false;
      if ((exponent == 0) && (mantissa != 0)) {
        DCHECK(sign == 0 || sign == 1);
        posSubnorm = (sign == 0);
        negSubnorm = (sign == 1);
      }

      // Setting flags if float is normal number
      posNorm = false;
      negNorm = false;
      if (!posSubnorm && !negSubnorm && !posInf && !negInf && !signalingNan &&
          !quietNan && !negZero && !posZero) {
        DCHECK(sign == 0 || sign == 1);
        posNorm = (sign == 0);
        negNorm = (sign == 1);
      }

      // Calculating result according to description of CLASS.S instruction
      result = (posZero << 9) | (posSubnorm << 8) | (posNorm << 7) |
               (posInf << 6) | (negZero << 5) | (negSubnorm << 4) |
               (negNorm << 3) | (negInf << 2) | (quietNan << 1) | signalingNan;

      DCHECK_NE(result, 0);

      fResult = base::bit_cast<float>(result);
      SetFPUFloatResult(fd_reg(), fResult);
      break;
    }
    case CVT_L_S: {
      float rounded;
      int64_t result;
      round64_according_to_fcsr(fs, &rounded, &result, fs);
      SetFPUResult(fd_reg(), result);
      if (set_fcsr_round64_error(fs, rounded)) {
        set_fpu_register_invalid_result64(fs, rounded);
      }
      break;
    }
    case CVT_W_S: {
      float rounded;
      int32_t result;
      round_according_to_fcsr(fs, &rounded, &result, fs);
      SetFPUWordResult(fd_reg(), result);
      if (set_fcsr_round_error(fs, rounded)) {
        set_fpu_register_word_invalid_result(fs, rounded);
      }
      break;
    }
    case TRUNC_W_S: {  // Truncate single to word (round towards 0).
      float rounded = trunc(fs);
      int32_t result = static_cast<int32_t>(rounded);
      SetFPUWordResult(fd_reg(), result);
      if (set_fcsr_round_error(fs, rounded)) {
        set_fpu_register_word_invalid_result(fs, rounded);
      }
    } break;
    case TRUNC_L_S: {  // Mips64r2 instruction.
      float rounded = trunc(fs);
      int64_t result = static_cast<int64_t>(rounded);
      SetFPUResult(fd_reg(), result);
      if (set_fcsr_round64_error(fs, rounded)) {
        set_fpu_register_invalid_result64(fs, rounded);
      }
      break;
    }
    case ROUND_W_S: {
      float rounded = std::floor(fs + 0.5);
      int32_t result = static_cast<int32_t>(rounded);
      if ((result & 1) != 0 && result - fs == 0.5) {
        // If the number is halfway between two integers,
        // round to the even one.
        result--;
      }
      SetFPUWordResult(fd_reg(), result);
      if (set_fcsr_round_error(fs, rounded)) {
        set_fpu_register_word_invalid_result(fs, rounded);
      }
      break;
    }
    case ROUND_L_S: {  // Mips64r2 instruction.
      float rounded = std::floor(fs + 0.5);
      int64_t result = static_cast<int64_t>(rounded);
      if ((result & 1) != 0 && result - fs == 0.5) {
        // If the number is halfway between two integers,
        // round to the even one.
        result--;
      }
      int64_t i64 = static_cast<int64_t>(result);
      SetFPUResult(fd_reg(), i64);
      if (set_fcsr_round64_error(fs, rounded)) {
        set_fpu_register_invalid_result64(fs, rounded);
      }
      break;
    }
    case FLOOR_L_S: {  // Mips64r2 instruction.
      float rounded = floor(fs);
      int64_t result = static_cast<int64_t>(rounded);
      SetFPUResult(fd_reg(), result);
      if (set_fcsr_round64_error(fs, rounded)) {
        set_fpu_register_invalid_result64(fs, rounded);
      }
      break;
    }
    case FLOOR_W_S:  // Round double to word towards negative infinity.
    {
      float rounded = std::floor(fs);
      int32_t result = static_cast<int32_t>(rounded);
      SetFPUWordResult(fd_reg(), result);
      if (set_fcsr_round_error(fs, rounded)) {
        set_fpu_register_word_invalid_result(fs, rounded);
      }
    } break;
    case CEIL_W_S:  // Round double to word towards positive infinity.
    {
      float rounded = std::ceil(fs);
      int32_t result = static_cast<int32_t>(rounded);
      SetFPUWordResult(fd_reg(), result);
      if (set_fcsr_round_error(fs, rounded)) {
        set_fpu_register_invalid_result(fs, rounded);
      }
    } break;
    case CEIL_L_S: {  // Mips64r2 instruction.
      float rounded = ceil(fs);
      int64_t result = static_cast<int64_t>(rounded);
      SetFPUResult(fd_reg(), result);
      if (set_fcsr_round64_error(fs, rounded)) {
        set_fpu_register_invalid_result64(fs, rounded);
      }
      break;
    }
    case MINA:
      DCHECK_EQ(kArchVariant, kMips64r6);
      SetFPUFloatResult(fd_reg(), FPUMinA(ft, fs));
      break;
    case MAXA:
      DCHECK_EQ(kArchVariant, kMips64r6);
      SetFPUFloatResult(fd_reg(), FPUMaxA(ft, fs));
      break;
    case MIN:
      DCHECK_EQ(kArchVariant, kMips64r6);
      SetFPUFloatResult(fd_reg(), FPUMin(ft, fs));
      break;
    case MAX:
      DCHECK_EQ(kArchVariant, kMips64r6);
      SetFPUFloatResult(fd_reg(), FPUMax(ft, fs));
      break;
    case SEL:
      DCHECK_EQ(kArchVariant, kMips64r6);
      SetFPUFloatResult(fd_reg(), (fd_int & 0x1) == 0 ? fs : ft);
      break;
    case SELEQZ_C:
      DCHECK_EQ(kArchVariant, kMips64r6);
      SetFPUFloatResult(fd_reg(), (ft_int & 0x1) == 0
                                      ? get_fpu_register_float(fs_reg())
                                      : 0.0);
      break;
    case SELNEZ_C:
      DCHECK_EQ(kArchVariant, kMips64r6);
      SetFPUFloatResult(fd_reg(), (ft_int & 0x1) != 0
                                      ? get_fpu_register_float(fs_reg())
                                      : 0.0);
      break;
    case MOVZ_C: {
      DCHECK_EQ(kArchVariant, kMips64r2);
      if (rt() == 0) {
        SetFPUFloatResult(fd_reg(), fs);
      }
      break;
    }
    case MOVN_C: {
      DCHECK_EQ(kArchVariant, kMips64r2);
      if (rt() != 0) {
        SetFPUFloatResult(fd_reg(), fs);
      }
      break;
    }
    case MOVF: {
      // Same function field for MOVT.D and MOVF.D
      uint32_t ft_cc = (ft_reg() >> 2) & 0x7;
      ft_cc = get_fcsr_condition_bit(ft_cc);

      if (instr_.Bit(16)) {  // Read Tf bit.
        // MOVT.D
        if (test_fcsr_bit(ft_cc)) SetFPUFloatResult(fd_reg(), fs);
      } else {
        // MOVF.D
        if (!test_fcsr_bit(ft_cc)) SetFPUFloatResult(fd_reg(), fs);
      }
      break;
    }
    default:
      // TRUNC_W_S ROUND_W_S ROUND_L_S FLOOR_W_S FLOOR_L_S
      // CEIL_W_S CEIL_L_S CVT_PS_S are unimplemented.
      UNREACHABLE();
  }
}

void Simulator::DecodeTypeRegisterDRsType() {
  double ft, fs, fd;
  uint32_t cc, fcsr_cc;
  fs = get_fpu_register_double(fs_reg());
  ft = (instr_.FunctionFieldRaw() != MOVF) ? get_fpu_register_double(ft_reg())
                                           : 0.0;
  fd = get_fpu_register_double(fd_reg());
  cc = instr_.FCccValue();
  fcsr_cc = get_fcsr_condition_bit(cc);
  int64_t ft_int = base::bit_cast<int64_t>(ft);
  int64_t fd_int = base::bit_cast<int64_t>(fd);
  switch (instr_.FunctionFieldRaw()) {
    case RINT: {
      DCHECK_EQ(kArchVariant, kMips64r6);
      double result, temp, temp_result;
      double upper = std::ceil(fs);
      double lower = std::floor(fs);
      switch (get_fcsr_rounding_mode()) {
        case kRoundToNearest:
          if (upper - fs < fs - lower) {
            result = upper;
          } else if (upper - fs > fs - lower) {
            result = lower;
          } else {
            temp_result = upper / 2;
            double reminder = modf(temp_result, &temp);
            if (reminder == 0) {
              result = upper;
            } else {
              result = lower;
            }
          }
          break;
        case kRoundToZero:
          result = (fs > 0 ? lower : upper);
          break;
        case kRoundToPlusInf:
          result = upper;
          break;
        case kRoundToMinusInf:
          result = lower;
          break;
      }
      SetFPUDoubleResult(fd_reg(), result);
      if (result != fs) {
        set_fcsr_bit(kFCSRInexactFlagBit, true);
      }
      break;
    }
    case SEL:
      DCHECK_EQ(kArchVariant, kMips64r6);
      SetFPUDoubleResult(fd_reg(), (fd_int & 0x1) == 0 ? fs : ft);
      break;
    case SELEQZ_C:
      DCHECK_EQ(kArchVariant, kMips64r6);
      SetFPUDoubleResult(fd_reg(), (ft_int & 0x1) == 0 ? fs : 0.0);
      break;
    case SELNEZ_C:
      DCHECK_EQ(kArchVariant, kMips64r6);
      SetFPUDoubleResult(fd_reg(), (ft_int & 0x1) != 0 ? fs : 0.0);
      break;
    case MOVZ_C: {
      DCHECK_EQ(kArchVariant, kMips64r2);
      if (rt() == 0) {
        SetFPUDoubleResult(fd_reg(), fs);
      }
      break;
    }
    case MOVN_C: {
      DCHECK_EQ(kArchVariant, kMips64r2);
      if (rt() != 0) {
        SetFPUDoubleResult(fd_reg(), fs);
      }
      break;
    }
    case MOVF: {
      // Same function field for MOVT.D and MOVF.D
      uint32_t ft_cc = (ft_reg() >> 2) & 0x7;
      ft_cc = get_fcsr_condition_bit(ft_cc);
      if (instr_.Bit(16)) {  // Read Tf bit.
        // MOVT.D
        if (test_fcsr_bit(ft_cc)) SetFPUDoubleResult(fd_reg(), fs);
      } else {
        // MOVF.D
        if (!test_fcsr_bit(ft_cc)) SetFPUDoubleResult(fd_reg(), fs);
      }
      break;
    }
    case MINA:
      DCHECK_EQ(kArchVariant, kMips64r6);
      SetFPUDoubleResult(fd_reg(), FPUMinA(ft, fs));
      break;
    case MAXA:
      DCHECK_EQ(kArchVariant, kMips64r6);
      SetFPUDoubleResult(fd_reg(), FPUMaxA(ft, fs));
      break;
    case MIN:
      DCHECK_EQ(kArchVariant, kMips64r6);
      SetFPUDoubleResult(fd_reg(), FPUMin(ft, fs));
      break;
    case MAX:
      DCHECK_EQ(kArchVariant, kMips64r6);
      SetFPUDoubleResult(fd_reg(), FPUMax(ft, fs));
      break;
    case ADD_D:
      SetFPUDoubleResult(
          fd_reg(),
          FPUCanonalizeOperation(
              [](double lhs, double rhs) { return lhs + rhs; }, fs, ft));
      break;
    case SUB_D:
      SetFPUDoubleResult(
          fd_reg(),
          FPUCanonalizeOperation(
              [](double lhs, double rhs) { return lhs - rhs; }, fs, ft));
      break;
    case MADDF_D:
      DCHECK_EQ(kArchVariant, kMips64r6);
      SetFPUDoubleResult(fd_reg(), std::fma(fs, ft, fd));
      break;
    case MSUBF_D:
      DCHECK_EQ(kArchVariant, kMips64r6);
      SetFPUDoubleResult(fd_reg(), std::fma(-fs, ft, fd));
      break;
    case MUL_D:
      SetFPUDoubleResult(
          fd_reg(),
          FPUCanonalizeOperation(
              [](double lhs, double rhs) { return lhs * rhs; }, fs, ft));
      break;
    case DIV_D:
      SetFPUDoubleResult(
          fd_reg(),
          FPUCanonalizeOperation(
              [](double lhs, double rhs) { return lhs / rhs; }, fs, ft));
      break;
    case ABS_D:
      SetFPUDoubleResult(
          fd_reg(),
          FPUCanonalizeOperation([](double fs) { return FPAbs(fs); }, fs));
      break;
    case MOV_D:
      SetFPUDoubleResult(fd_reg(), fs);
      break;
    case NEG_D:
      SetFPUDoubleResult(fd_reg(),
                         FPUCanonalizeOperation([](double src) { return -src; },
                                                KeepSign::yes, fs));
      break;
    case SQRT_D:
      SetFPUDoubleResult(
          fd_reg(),
          FPUCanonalizeOperation([](double fs) { return std::sqrt(fs); }, fs));
      break;
    case RSQRT_D:
      SetFPUDoubleResult(
          fd_reg(), FPUCanonalizeOperation(
                        [](double fs) { return 1.0 / std::sqrt(fs); }, fs));
      break;
    case RECIP_D:
      SetFPUDoubleResult(fd_reg(), FPUCanonalizeOperation(
                                       [](double fs) { return 1.0 / fs; }, fs));
      break;
    case C_UN_D:
      set_fcsr_bit(fcsr_cc, std::isnan(fs) || std::isnan(ft));
      TraceRegWr(test_fcsr_bit(fcsr_cc));
      break;
    case C_EQ_D:
      set_fcsr_bit(fcsr_cc, (fs == ft));
      TraceRegWr(test_fcsr_bit(fcsr_cc));
      break;
    case C_UEQ_D:
      set_fcsr_bit(fcsr_cc, (fs == ft) || (std::isnan(fs) || std::isnan(ft)));
      TraceRegWr(test_fcsr_bit(fcsr_cc));
      break;
    case C_OLT_D:
      set_fcsr_bit(fcsr_cc, (fs < ft));
      TraceRegWr(test_fcsr_bit(fcsr_cc));
      break;
    case C_ULT_D:
      set_fcsr_bit(fcsr_cc, (fs < ft) || (std::isnan(fs) || std::isnan(ft)));
      TraceRegWr(test_fcsr_bit(fcsr_cc));
      break;
    case C_OLE_D:
      set_fcsr_bit(fcsr_cc, (fs <= ft));
      TraceRegWr(test_fcsr_bit(fcsr_cc));
      break;
    case C_ULE_D:
      set_fcsr_bit(fcsr_cc, (fs <= ft) || (std::isnan(fs) || std::isnan(ft)));
      TraceRegWr(test_fcsr_bit(fcsr_cc));
      break;
    case CVT_W_D: {  // Convert double to word.
      double rounded;
      int32_t result;
      round_according_to_fcsr(fs, &rounded, &result, fs);
      SetFPUWordResult(fd_reg(), result);
      if (set_fcsr_round_error(fs, rounded)) {
        set_fpu_register_word_invalid_result(fs, rounded);
      }
      break;
    }
    case ROUND_W_D:  // Round double to word (round half to even).
    {
      double rounded = std::floor(fs + 0.5);
      int32_t result = static_cast<int32_t>(rounded);
      if ((result & 1) != 0 && result - fs == 0.5) {
        // If the number is halfway between two integers,
        // round to the even one.
        result--;
      }
      SetFPUWordResult(fd_reg(), result);
      if (set_fcsr_round_error(fs, rounded)) {
        set_fpu_register_invalid_result(fs, rounded);
      }
    } break;
    case TRUNC_W_D:  // Truncate double to word (round towards 0).
    {
      double rounded = trunc(fs);
      int32_t result = static_cast<int32_t>(rounded);
      SetFPUWordResult(fd_reg(), result);
      if (set_fcsr_round_error(fs, rounded)) {
        set_fpu_register_invalid_result(fs, rounded);
      }
    } break;
    case FLOOR_W_D:  // Round double to word towards negative infinity.
    {
      double rounded = std::floor(fs);
      int32_t result = static_cast<int32_t>(rounded);
      SetFPUWordResult(fd_reg(), result);
      if (set_fcsr_round_error(fs, rounded)) {
        set_fpu_register_invalid_result(fs, rounded);
      }
    } break;
    case CEIL_W_D:  // Round double to word towards positive infinity.
    {
      double rounded = std::ceil(fs);
      int32_t result = static_cast<int32_t>(rounded);
      SetFPUWordResult2(fd_reg(), result);
      if (set_fcsr_round_error(fs, rounded)) {
        set_fpu_register_invalid_result(fs, rounded);
      }
    } break;
    case CVT_S_D:  // Convert double to float (single).
      SetFPUFloatResult(fd_reg(), static_cast<float>(fs));
      break;
    case CVT_L_D: {  // Mips64r2: Truncate double to 64-bit long-word.
      double rounded;
      int64_t result;
      round64_according_to_fcsr(fs, &rounded, &result, fs);
      SetFPUResult(fd_reg(), result);
      if (set_fcsr_round64_error(fs, rounded)) {
        set_fpu_register_invalid_result64(fs, rounded);
      }
      break;
    }
    case ROUND_L_D: {  // Mips64r2 instruction.
      double rounded = std::floor(fs + 0.5);
      int64_t result = static_cast<int64_t>(rounded);
      if ((result & 1) != 0 && result - fs == 0.5) {
        // If the number is halfway between two integers,
        // round to the even one.
        result--;
      }
      int64_t i64 = static_cast<int64_t>(result);
      SetFPUResult(fd_reg(), i64);
      if (set_fcsr_round64_error(fs, rounded)) {
        set_fpu_register_invalid_result64(fs, rounded);
      }
      break;
    }
    case TRUNC_L_D: {  // Mips64r2 instruction.
      double rounded = trunc(fs);
      int64_t result = static_cast<int64_t>(rounded);
      SetFPUResult(fd_reg(), result);
      if (set_fcsr_round64_error(fs, rounded)) {
        set_fpu_register_invalid_result64(fs, rounded);
      }
      break;
    }
    case FLOOR_L_D: {  // Mips64r2 instruction.
      double rounded = floor(fs);
      int64_t result = static_cast<int64_t>(rounded);
      SetFPUResult(fd_reg(), result);
      if (set_fcsr_round64_error(fs, rounded)) {
        set_fpu_register_invalid_result64(fs, rounded);
      }
      break;
    }
    case CEIL_L_D: {  // Mips64r2 instruction.
      double rounded = ceil(fs);
      int64_t result = static_cast<int64_t>(rounded);
      SetFPUResult(fd_reg(), result);
      if (set_fcsr_round64_error(fs, rounded)) {
        set_fpu_register_invalid_result64(fs, rounded);
      }
      break;
    }
    case CLASS_D: {  // Mips64r6 instruction
      // Convert double input to uint64_t for easier bit manipulation
      uint64_t classed = base::bit_cast<uint64_t>(fs);

      // Extracting sign, exponent and mantissa from the input double
      uint32_t sign = (classed >> 63) & 1;
      uint32_t exponent = (classed >> 52) & 0x00000000000007FF;
      uint64_t mantissa = classed & 0x000FFFFFFFFFFFFF;
      uint64_t result;
      double dResult;

      // Setting flags if input double is negative infinity,
      // positive infinity, negative zero or positive zero
      bool negInf = (classed == 0xFFF0000000000000);
      bool posInf = (classed == 0x7FF0000000000000);
      bool negZero = (classed == 0x8000000000000000);
      bool posZero = (classed == 0x0000000000000000);

      bool signalingNan;
      bool quietNan;
      bool negSubnorm;
      bool posSubnorm;
      bool negNorm;
      bool posNorm;

      // Setting flags if double is NaN
      signalingNan = false;
      quietNan = false;
      if (!negInf && !posInf && exponent == 0x7FF) {
        quietNan = ((mantissa & 0x0008000000000000) != 0) &&
                   ((mantissa & (0x0008000000000000 - 1)) == 0);
        signalingNan = !quietNan;
      }

      // Setting flags if double is subnormal number
      posSubnorm = false;
      negSubnorm = false;
      if ((exponent == 0) && (mantissa != 0)) {
        DCHECK(sign == 0 || sign == 1);
        posSubnorm = (sign == 0);
        negSubnorm = (sign == 1);
      }

      // Setting flags if double is normal number
      posNorm = false;
      negNorm = false;
      if (!posSubnorm && !negSubnorm && !posInf && !negInf && !signalingNan &&
          !quietNan && !negZero && !posZero) {
        DCHECK(sign == 0 || sign == 1);
        posNorm = (sign == 0);
        negNorm = (sign == 1);
      }

      // Calculating result according to description of CLASS.D instruction
      result = (posZero << 9) | (posSubnorm << 8) | (posNorm << 7) |
               (posInf << 6) | (negZero << 5) | (negSubnorm << 4) |
               (negNorm << 3) | (negInf << 2) | (quietNan << 1) | signalingNan;

      DCHECK_NE(result, 0);

      dResult = base::bit_cast<double>(result);
      SetFPUDoubleResult(fd_reg(), dResult);
      break;
    }
    case C_F_D: {
      set_fcsr_bit(fcsr_cc, false);
      TraceRegWr(test_fcsr_bit(fcsr_cc));
      break;
    }
    default:
      UNREACHABLE();
  }
}

void Simulator::DecodeTypeRegisterWRsType() {
  float fs = get_fpu_register_float(fs_reg());
  float ft = get_fpu_register_float(ft_reg());
  int64_t alu_out = 0x12345678;
  switch (instr_.FunctionFieldRaw()) {
    case CVT_S_W:  // Convert word to float (single).
      alu_out = get_fpu_register_signed_word(fs_reg());
      SetFPUFloatResult(fd_reg(), static_cast<float>(alu_out));
      break;
    case CVT_D_W:  // Convert word to double.
      alu_out = get_fpu_register_signed_word(fs_reg());
      SetFPUDoubleResult(fd_reg(), static_cast<double>(alu_out));
      break;
    case CMP_AF:
      SetFPUWordResult2(fd_reg(), 0);
      break;
    case CMP_UN:
      if (std::isnan(fs) || std::isnan(ft)) {
        SetFPUWordResult2(fd_reg(), -1);
      } else {
        SetFPUWordResult2(fd_reg(), 0);
      }
      break;
    case CMP_EQ:
      if (fs == ft) {
        SetFPUWordResult2(fd_reg(), -1);
      } else {
        SetFPUWordResult2(fd_reg(), 0);
      }
      break;
    case CMP_UEQ:
      if ((fs == ft) || (std::isnan(fs) || std::isnan(ft))) {
        SetFPUWordResult2(fd_reg(), -1);
      } else {
        SetFPUWordResult2(fd_reg(), 0);
      }
      break;
    case CMP_LT:
      if (fs < ft) {
        SetFPUWordResult2(fd_reg(), -1);
      } else {
        SetFPUWordResult2(fd_reg(), 0);
      }
      break;
    case CMP_ULT:
      if ((fs < ft) || (std::isnan(fs) || std::isnan(ft))) {
        SetFPUWordResult2(fd_reg(), -1);
      } else {
        SetFPUWordResult2(fd_reg(), 0);
      }
      break;
    case CMP_LE:
      if (fs <= ft) {
        SetFPUWordResult2(fd_reg(), -1);
      } else {
        SetFPUWordResult2(fd_reg(), 0);
      }
      break;
    case CMP_ULE:
      if ((fs <= ft) || (std::isnan(fs) || std::isnan(ft))) {
        SetFPUWordResult2(fd_reg(), -1);
      } else {
        SetFPUWordResult2(fd_reg(), 0);
      }
      break;
    case CMP_OR:
      if (!std::isnan(fs) && !std::isnan(ft)) {
        SetFPUWordResult2(fd_reg(), -1);
      } else {
        SetFPUWordResult2(fd_reg(), 0);
      }
      break;
    case CMP_UNE:
      if ((fs != ft) || (std::isnan(fs) || std::isnan(ft))) {
        SetFPUWordResult2(fd_reg(), -1);
      } else {
        SetFPUWordResult2(fd_reg(), 0);
      }
      break;
    case CMP_NE:
      if (fs != ft) {
        SetFPUWordResult2(fd_reg(), -1);
      } else {
        SetFPUWordResult2(fd_reg(), 0);
      }
      break;
    default:
      UNREACHABLE();
  }
}

void Simulator::DecodeTypeRegisterLRsType() {
  double fs = get_fpu_register_double(fs_reg());
  double ft = get_fpu_register_double(ft_reg());
  int64_t i64;
  switch (instr_.FunctionFieldRaw()) {
    case CVT_D_L:  // Mips32r2 instruction.
      i64 = get_fpu_register(fs_reg());
      SetFPUDoubleResult(fd_reg(), static_cast<double>(i64));
      break;
    case CVT_S_L:
      i64 = get_fpu_register(fs_reg());
      SetFPUFloatResult(fd_reg(), static_cast<float>(i64));
      break;
    case CMP_AF:
      SetFPUResult(fd_reg(), 0);
      break;
    case CMP_UN:
      if (std::isnan(fs) || std::isnan(ft)) {
        SetFPUResult(fd_reg(), -1);
      } else {
        SetFPUResult(fd_reg(), 0);
      }
      break;
    case CMP_EQ:
      if (fs == ft) {
        SetFPUResult(fd_reg(), -1);
      } else {
        SetFPUResult(fd_reg(), 0);
      }
      break;
    case CMP_UEQ:
      if ((fs == ft) || (std::isnan(fs) || std::isnan(ft))) {
        SetFPUResult(fd_reg(), -1);
      } else {
        SetFPUResult(fd_reg(), 0);
      }
      break;
    case CMP_LT:
      if (fs < ft) {
        SetFPUResult(fd_reg(), -1);
      } else {
        SetFPUResult(fd_reg(), 0);
      }
      break;
    case CMP_ULT:
      if ((fs < ft) || (std::isnan(fs) || std::isnan(ft))) {
        SetFPUResult(fd_reg(), -1);
      } else {
        SetFPUResult(fd_reg(), 0);
      }
      break;
    case CMP_LE:
      if (fs <= ft) {
        SetFPUResult(fd_reg(), -1);
      } else {
        SetFPUResult(fd_reg(), 0);
      }
      break;
    case CMP_ULE:
      if ((fs <= ft) || (std::isnan(fs) || std::isnan(ft))) {
        SetFPUResult(fd_reg(), -1);
      } else {
        SetFPUResult(fd_reg(), 0);
      }
      break;
    case CMP_OR:
      if (!std::isnan(fs) && !std::isnan(ft)) {
        SetFPUResult(fd_reg(), -1);
      } else {
        SetFPUResult(fd_reg(), 0);
      }
      break;
    case CMP_UNE:
      if ((fs != ft) || (std::isnan(fs) || std::isnan(ft))) {
        SetFPUResult(fd_reg(), -1);
      } else {
        SetFPUResult(fd_reg(), 0);
      }
      break;
    case CMP_NE:
      if (fs != ft && (!std::isnan(fs) && !std::isnan(ft))) {
        SetFPUResult(fd_reg(), -1);
      } else {
        SetFPUResult(fd_reg(), 0);
      }
      break;
    default:
      UNREACHABLE();
  }
}

void Simulator::DecodeTypeRegisterCOP1() {
  switch (instr_.RsFieldRaw()) {
    case BC1:  // Branch on coprocessor condition.
    case BC1EQZ:
    case BC1NEZ:
      UNREACHABLE();
    case CFC1:
      // At the moment only FCSR is supported.
      DCHECK_EQ(fs_reg(), kFCSRRegister);
      SetResult(rt_reg(), FCSR_);
      break;
    case MFC1:
      set_register(rt_reg(),
                   static_cast<int64_t>(get_fpu_register_word(fs_reg())));
      TraceRegWr(get_register(rt_reg()), WORD_DWORD);
      break;
    case DMFC1:
      SetResult(rt_reg(), get_fpu_register(fs_reg()));
      break;
    case MFHC1:
      SetResult(rt_reg(), get_fpu_register_hi_word(fs_reg()));
      break;
    case CTC1: {
      // At the moment only FCSR is supported.
      DCHECK_EQ(fs_reg(), kFCSRRegister);
      uint32_t reg = static_cast<uint32_t>(rt());
      if (kArchVariant == kMips64r6) {
        FCSR_ = reg | kFCSRNaN2008FlagMask;
      } else {
        DCHECK_EQ(kArchVariant, kMips64r2);
        FCSR_ = reg & ~kFCSRNaN2008FlagMask;
      }
      TraceRegWr(FCSR_);
      break;
    }
    case MTC1:
      // Hardware writes upper 32-bits to zero on mtc1.
      set_fpu_register_hi_word(fs_reg(), 0);
      set_fpu_register_word(fs_reg(), static_cast<int32_t>(rt()));
      TraceRegWr(get_fpu_register(fs_reg()), FLOAT_DOUBLE);
      break;
    case DMTC1:
      SetFPUResult2(fs_reg(), rt());
      break;
    case MTHC1:
      set_fpu_register_hi_word(fs_reg(), static_cast<int32_t>(rt()));
      TraceRegWr(get_fpu_register(fs_reg()), DOUBLE);
      break;
    case S:
      DecodeTypeRegisterSRsType();
      break;
    case D:
      DecodeTypeRegisterDRsType();
      break;
    case W:
      DecodeTypeRegisterWRsType();
      break;
    case L:
      DecodeTypeRegisterLRsType();
      break;
    default:
      UNREACHABLE();
  }
}

void Simulator::DecodeTypeRegisterCOP1X() {
  switch (instr_.FunctionFieldRaw()) {
    case MADD_S: {
      DCHECK_EQ(kArchVariant, kMips64r2);
      float fr, ft, fs;
      fr = get_fpu_register_float(fr_reg());
      fs = get_fpu_register_float(fs_reg());
      ft = get_fpu_register_float(ft_reg());
      SetFPUFloatResult(fd_reg(), fs * ft + fr);
      break;
    }
    case MSUB_S: {
      DCHECK_EQ(kArchVariant, kMips64r2);
      float fr, ft, fs;
      fr = get_fpu_register_float(fr_reg());
      fs = get_fpu_register_float(fs_reg());
      ft = get_fpu_register_float(ft_reg());
      SetFPUFloatResult(fd_reg(), fs * ft - fr);
      break;
    }
    case MADD_D: {
      DCHECK_EQ(kArchVariant, kMips64r2);
      double fr, ft, fs;
      fr = get_fpu_register_double(fr_reg());
      fs = get_fpu_register_double(fs_reg());
      ft = get_fpu_register_double(ft_reg());
      SetFPUDoubleResult(fd_reg(), fs * ft + fr);
      break;
    }
    case MSUB_D: {
      DCHECK_EQ(kArchVariant, kMips64r2);
      double fr, ft, fs;
      fr = get_fpu_register_double(fr_reg());
      fs = get_fpu_register_double(fs_reg());
      ft = get_fpu_register_double(ft_reg());
      SetFPUDoubleResult(fd_reg(), fs * ft - fr);
      break;
    }
    default:
      UNREACHABLE();
  }
}

void Simulator::DecodeTypeRegisterSPECIAL() {
  int64_t i64hilo;
  uint64_t u64hilo;
  int64_t alu_out;
  bool do_interrupt = false;

  switch (instr_.FunctionFieldRaw()) {
    case SELEQZ_S:
      DCHECK_EQ(kArchVariant, kMips64r6);
      SetResult(rd_reg(), rt() == 0 ? rs() : 0);
      break;
    case SELNEZ_S:
      DCHECK_EQ(kArchVariant, kMips64r6);
      SetResult(rd_reg(), rt() != 0 ? rs() : 0);
      break;
    case JR: {
      int64_t next_pc = rs();
      int64_t current_pc = get_pc();
      Instruction* branch_delay_instr =
          reinterpret_cast<Instruction*>(current_pc + kInstrSize);
      BranchDelayInstructionDecode(branch_delay_instr);
      set_pc(next_pc);
      pc_modified_ = true;
      break;
    }
    case JALR: {
      int64_t next_pc = rs();
      int64_t current_pc = get_pc();
      int32_t return_addr_reg = rd_reg();
      Instruction* branch_delay_instr =
          reinterpret_cast<Instruction*>(current_pc + kInstrSize);
      BranchDelayInstructionDecode(branch_delay_instr);
      set_register(return_addr_reg, current_pc + 2 * kInstrSize);
      set_pc(next_pc);
      pc_modified_ = true;
      break;
    }
    case SLL:
      SetResult(rd_reg(), static_cast<int32_t>(rt()) << sa());
      break;
    case DSLL:
      SetResult(rd_reg(), rt() << sa());
      break;
    case DSLL32:
      SetResult(rd_reg(), rt() << sa() << 32);
      break;
    case SRL:
      if (rs_reg() == 0) {
        // Regular logical right shift of a word by a fixed number of
        // bits instruction. RS field is always equal to 0.
        // Sign-extend the 32-bit result.
        alu_out = static_cast<int32_t>(static_cast<uint32_t>(rt_u()) >> sa());
      } else if (rs_reg() == 1) {
        // Logical right-rotate of a word by a fixed number of bits. This
        // is special case of SRL instruction, added in MIPS32 Release 2.
        // RS field is equal to 00001.
        alu_out = static_cast<int32_t>(
            base::bits::RotateRight32(static_cast<const uint32_t>(rt_u()),
                                      static_cast<const uint32_t>(sa())));
      } else {
        UNREACHABLE();
      }
      SetResult(rd_reg(), alu_out);
      break;
    case DSRL:
      if (rs_reg() == 0) {
        // Regular logical right shift of a word by a fixed number of
        // bits instruction. RS field is always equal to 0.
        // Sign-extend the 64-bit result.
        alu_out = static_cast<int64_t>(rt_u() >> sa());
      } else if (rs_reg() == 1) {
        // Logical right-rotate of a word by a fixed number of bits. This
        // is special case of SRL instruction, added in MIPS32 Release 2.
        // RS field is equal to 00001.
        alu_out = static_cast<int64_t>(base::bits::RotateRight64(rt_u(), sa()));
      } else {
        UNREACHABLE();
      }
      SetResult(rd_reg(), alu_out);
      break;
    case DSRL32:
      if (rs_reg() == 0) {
        // Regular logical right shift of a word by a fixed number of
        // bits instruction. RS field is always equal to 0.
        // Sign-extend the 64-bit result.
        alu_out = static_cast<int64_t>(rt_u() >> sa() >> 32);
      } else if (rs_reg() == 1) {
        // Logical right-rotate of a word by a fixed number of bits. This
        // is special case of SRL instruction, added in MIPS32 Release 2.
        // RS field is equal to 00001.
        alu_out =
            static_cast<int64_t>(base::bits::RotateRight64(rt_u(), sa() + 32));
      } else {
        UNREACHABLE();
      }
      SetResult(rd_reg(), alu_out);
      break;
    case SRA:
      SetResult(rd_reg(), (int32_t)rt() >> sa());
      break;
    case DSRA:
      SetResult(rd_reg(), rt() >> sa());
      break;
    case DSRA32:
      SetResult(rd_reg(), rt() >> sa() >> 32);
      break;
    case SLLV:
      SetResult(rd_reg(), (int32_t)rt() << rs());
      break;
    case DSLLV:
      SetResult(rd_reg(), rt() << rs());
      break;
    case SRLV:
      if (sa() == 0) {
        // Regular logical right-shift of a word by a variable number of
        // bits instruction. SA field is always equal to 0.
        alu_out = static_cast<int32_t>((uint32_t)rt_u() >> rs());
      } else {
        // Logical right-rotate of a word by a variable number of bits.
        // This is special case od SRLV instruction, added in MIPS32
        // Release 2. SA field is equal to 00001.
        alu_out = static_cast<int32_t>(
            base::bits::RotateRight32(static_cast<const uint32_t>(rt_u()),
                                      static_cast<const uint32_t>(rs_u())));
      }
      SetResult(rd_reg(), alu_out);
      break;
    case DSRLV:
      if (sa() == 0) {
        // Regular logical right-shift of a word by a variable number of
        // bits instruction. SA field is always equal to 0.
        alu_out = static_cast<int64_t>(rt_u() >> rs());
      } else {
        // Logical right-rotate of a word by a variable number of bits.
        // This is special case od SRLV instruction, added in MIPS32
        // Release 2. SA field is equal to 00001.
        alu_out =
            static_cast<int64_t>(base::bits::RotateRight64(rt_u(), rs_u()));
      }
      SetResult(rd_reg(), alu_out);
      break;
    case SRAV:
      SetResult(rd_reg(), (int32_t)rt() >> rs());
      break;
    case DSRAV:
      SetResult(rd_reg(), rt() >> rs());
      break;
    case LSA: {
      DCHECK_EQ(kArchVariant, kMips64r6);
      int8_t sa = lsa_sa() + 1;
      int32_t _rt = static_cast<int32_t>(rt());
      int32_t _rs = static_cast<int32_t>(rs());
      int32_t res = _rs << sa;
      res += _rt;
      SetResult(rd_reg(), static_cast<int64_t>(res));
      break;
    }
    case DLSA:
      DCHECK_EQ(kArchVariant, kMips64r6);
      SetResult(rd_reg(), (rs() << (lsa_sa() + 1)) + rt());
      break;
    case MFHI:  // MFHI == CLZ on R6.
      if (kArchVariant != kMips64r6) {
        DCHECK_EQ(sa(), 0);
        alu_out = get_register(HI);
      } else {
        // MIPS spec: If no bits were set in GPR rs(), the result written to
        // GPR rd() is 32.
        DCHECK_EQ(sa(), 1);
        alu_out = base::bits::CountLeadingZeros32(static_cast<int32_t>(rs_u()));
      }
      SetResult(rd_reg(), alu_out);
      break;
    case MFLO:  // MFLO == DCLZ on R6.
      if (kArchVariant != kMips64r6) {
        DCHECK_EQ(sa(), 0);
        alu_out = get_register(LO);
      } else {
        // MIPS spec: If no bits were set in GPR rs(), the result written to
        // GPR rd() is 64.
        DCHECK_EQ(sa(), 1);
        alu_out = base::bits::CountLeadingZeros64(static_cast<int64_t>(rs_u()));
      }
      SetResult(rd_reg(), alu_out);
      break;
    // Instructions using HI and LO registers.
    case MULT: {  // MULT == D_MUL_MUH.
      int32_t rs_lo = static_cast<int32_t>(rs());
      int32_t rt_lo = static_cast<int32_t>(rt());
      i64hilo = static_cast<int64_t>(rs_lo) * static_cast<int64_t>(rt_lo);
      if (kArchVariant != kMips64r6) {
        set_register(LO, static_cast<int32_t>(i64hilo & 0xFFFFFFFF));
        set_register(HI, static_cast<int32_t>(i64hilo >> 32));
      } else {
        switch (sa()) {
          case MUL_OP:
            SetResult(rd_reg(), static_cast<int32_t>(i64hilo & 0xFFFFFFFF));
            break;
          case MUH_OP:
            SetResult(rd_reg(), static_cast<int32_t>(i64hilo >> 32));
            break;
          default:
            UNIMPLEMENTED_MIPS();
            break;
        }
      }
      break;
    }
    case MULTU:
      u64hilo = static_cast<uint64_t>(rs_u() & 0xFFFFFFFF) *
                static_cast<uint64_t>(rt_u() & 0xFFFFFFFF);
      if (kArchVariant != kMips64r6) {
        set_register(LO, static_cast<int32_t>(u64hilo & 0xFFFFFFFF));
        set_register(HI, static_cast<int32_t>(u64hilo >> 32));
      } else {
        switch (sa()) {
          case MUL_OP:
            SetResult(rd_reg(), static_cast<int32_t>(u64hilo & 0xFFFFFFFF));
            break;
          case MUH_OP:
            SetResult(rd_reg(), static_cast<int32_t>(u64hilo >> 32));
            break;
          default:
            UNIMPLEMENTED_MIPS();
            break;
        }
      }
      break;
    case DMULT:  // DMULT == D_MUL_MUH.
      if (kArchVariant != kMips64r6) {
        set_register(LO, rs() * rt());
        set_register(HI, base::bits::SignedMulHigh64(rs(), rt()));
      } else {
        switch (sa()) {
          case MUL_OP:
            SetResult(rd_reg(), rs() * rt());
            break;
          case MUH_OP:
            SetResult(rd_reg(), base::bits::SignedMulHigh64(rs(), rt()));
            break;
          default:
            UNIMPLEMENTED_MIPS();
            break;
        }
      }
      break;
    case DMULTU:
      if (kArchVariant != kMips64r6) {
        set_register(LO, rs_u() * rt_u());
        set_register(HI, base::bits::UnsignedMulHigh64(rs_u(), rt_u()));
      } else {
        UNIMPLEMENTED_MIPS();
      }
      break;
    case DIV:
    case DDIV: {
      const int64_t int_min_value =
          instr_.FunctionFieldRaw() == DIV ? INT_MIN : LONG_MIN;
      switch (kArchVariant) {
        case kMips64r2:
          // Divide by zero and overflow was not checked in the
          // configuration step - div and divu do not raise exceptions. On
          // division by 0 the result will be UNPREDICTABLE. On overflow
          // (INT_MIN/-1), return INT_MIN which is what the hardware does.
          if (rs() == int_min_value && rt() == -1) {
            set_register(LO, int_min_value);
            set_register(HI, 0);
          } else if (rt() != 0) {
            set_register(LO, rs() / rt());
            set_register(HI, rs() % rt());
          }
          break;
        case kMips64r6:
          switch (sa()) {
            case DIV_OP:
              if (rs() == int_min_value && rt() == -1) {
                SetResult(rd_reg(), int_min_value);
              } else if (rt() != 0) {
                SetResult(rd_reg(), rs() / rt());
              }
              break;
            case MOD_OP:
              if (rs() == int_min_value && rt() == -1) {
                SetResult(rd_reg(), 0);
              } else if (rt() != 0) {
                SetResult(rd_reg(), rs() % rt());
              }
              break;
            default:
              UNIMPLEMENTED_MIPS();
              break;
          }
          break;
        default:
          break;
      }
      break;
    }
    case DIVU:
      switch (kArchVariant) {
        case kMips64r6: {
          uint32_t rt_u_32 = static_cast<uint32_t>(rt_u());
          uint32_t rs_u_32 = static_cast<uint32_t>(rs_u());
          switch (sa()) {
            case DIV_OP:
              if (rt_u_32 != 0) {
                SetResult(rd_reg(), static_cast<int32_t>(rs_u_32 / rt_u_32));
              }
              break;
            case MOD_OP:
              if (rt_u() != 0) {
                SetResult(rd_reg(), static_cast<int32_t>(rs_u_32 % rt_u_32));
              }
              break;
            default:
              UNIMPLEMENTED_MIPS();
              break;
          }
        } break;
        default: {
          if (rt_u() != 0) {
            uint32_t rt_u_32 = static_cast<uint32_t>(rt_u());
            uint32_t rs_u_32 = static_cast<uint32_t>(rs_u());
            set_register(LO, static_cast<int32_t>(rs_u_32 / rt_u_32));
            set_register(HI, static_cast<int32_t>(rs_u_32 % rt_u_32));
          }
        }
      }
      break;
    case DDIVU:
      switch (kArchVariant) {
        case kMips64r6: {
          switch (instr_.SaValue()) {
            case DIV_OP:
              if (rt_u() != 0) {
                SetResult(rd_reg(), rs_u() / rt_u());
              }
              break;
            case MOD_OP:
              if (rt_u() != 0) {
                SetResult(rd_reg(), rs_u() % rt_u());
              }
              break;
            default:
              UNIMPLEMENTED_MIPS();
              break;
          }
        } break;
        default: {
          if (rt_u() != 0) {
            set_register(LO, rs_u() / rt_u());
            set_register(HI, rs_u() % rt_u());
          }
        }
      }
      break;
    case ADD:
    case DADD:
      if (HaveSameSign(rs(), rt())) {
        if (rs() > 0) {
          if (rs() > (Registers::kMaxValue - rt())) {
            SignalException(kIntegerOverflow);
          }
        } else if (rs() < 0) {
          if (rs() < (Registers::kMinValue - rt())) {
            SignalException(kIntegerUnderflow);
          }
        }
      }
      SetResult(rd_reg(), rs() + rt());
      break;
    case ADDU: {
      int32_t alu32_out = static_cast<int32_t>(rs() + rt());
      // Sign-extend result of 32bit operation into 64bit register.
      SetResult(rd_reg(), static_cast<int64_t>(alu32_out));
      break;
    }
    case DADDU:
      SetResult(rd_reg(), rs() + rt());
      break;
    case SUB:
    case DSUB:
      if (!HaveSameSign(rs(), rt())) {
        if (rs() > 0) {
          if (rs() > (Registers::kMaxValue + rt())) {
            SignalException(kIntegerOverflow);
          }
        } else if (rs() < 0) {
          if (rs() < (Registers::kMinValue + rt())) {
            SignalException(kIntegerUnderflow);
          }
        }
      }
      SetResult(rd_reg(), rs() - rt());
      break;
    case SUBU: {
      int32_t alu32_out = static_cast<int32_t>(rs() - rt());
      // Sign-extend result of 32bit operation into 64bit register.
      SetResult(rd_reg(), static_cast<int64_t>(alu32_out));
      break;
    }
    case DSUBU:
      SetResult(rd_reg(), rs() - rt());
      break;
    case AND:
      SetResult(rd_reg(), rs() & rt());
      break;
    case OR:
      SetResult(rd_reg(), rs() | rt());
      break;
    case XOR:
      SetResult(rd_reg(), rs() ^ rt());
      break;
    case NOR:
      SetResult(rd_reg(), ~(rs() | rt()));
      break;
    case SLT:
      SetResult(rd_reg(), rs() < rt() ? 1 : 0);
      break;
    case SLTU:
      SetResult(rd_reg(), rs_u() < rt_u() ? 1 : 0);
      break;
    // Break and trap instructions.
    case BREAK:
      do_interrupt = true;
      break;
    case TGE:
      do_interrupt = rs() >= rt();
      break;
    case TGEU:
      do_interrupt = rs_u() >= rt_u();
      break;
    case TLT:
      do_interrupt = rs() < rt();
      break;
    case TLTU:
      do_interrupt = rs_u() < rt_u();
      break;
    case TEQ:
      do_interrupt = rs() == rt();
      break;
    case TNE:
      do_interrupt = rs() != rt();
      break;
    case SYNC:
      // TODO(palfia): Ignore sync instruction for now.
      break;
    // Conditional moves.
    case MOVN:
      if (rt()) {
        SetResult(rd_reg(), rs());
      }
      break;
    case MOVCI: {
      uint32_t cc = instr_.FBccValue();
      uint32_t fcsr_cc = get_fcsr_condition_bit(cc);
      if (instr_.Bit(16)) {  // Read Tf bit.
        if (test_fcsr_bit(fcsr_cc)) SetResult(rd_reg(), rs());
      } else {
        if (!test_fcsr_bit(fcsr_cc)) SetResult(rd_reg(), rs());
      }
      break;
    }
    case MOVZ:
      if (!rt()) {
        SetResult(rd_reg(), rs());
      }
      break;
    default:
      UNREACHABLE();
  }
  if (do_interrupt) {
    SoftwareInterrupt();
  }
}

void Simulator::DecodeTypeRegisterSPECIAL2() {
  int64_t alu_out;
  switch (instr_.FunctionFieldRaw()) {
    case MUL:
      alu_out = static_cast<int32_t>(rs_u()) * static_cast<int32_t>(rt_u());
      SetResult(rd_reg(), alu_out);
      // HI and LO are UNPREDICTABLE after the operation.
      set_register(LO, Unpredictable);
      set_register(HI, Unpredictable);
      break;
    case CLZ:
      // MIPS32 spec: If no bits were set in GPR rs(), the result written to
      // GPR rd is 32.
      alu_out = base::bits::CountLeadingZeros32(static_cast<uint32_t>(rs_u()));
      SetResult(rd_reg(), alu_out);
      break;
    case DCLZ:
      // MIPS64 spec: If no bits were set in GPR rs(), the result written to
      // GPR rd is 64.
      alu_out = base::bits::CountLeadingZeros64(static_cast<uint64_t>(rs_u()));
      SetResult(rd_reg(), alu_out);
      break;
    default:
      alu_out = 0x12345678;
      UNREACHABLE();
  }
}

void Simulator::DecodeTypeRegisterSPECIAL3() {
  int64_t alu_out;
  switch (instr_.FunctionFieldRaw()) {
    case EXT: {  // Mips32r2 instruction.
      // Interpret rd field as 5-bit msbd of extract.
      uint16_t msbd = rd_reg();
      // Interpret sa field as 5-bit lsb of extract.
      uint16_t lsb = sa();
      uint16_t size = msbd + 1;
      uint64_t mask = (1ULL << size) - 1;
      alu_out = static_cast<int32_t>((rs_u() & (mask << lsb)) >> lsb);
      SetResult(rt_reg(), alu_out);
      break;
    }
    case DEXT: {  // Mips64r2 instruction.
      // Interpret rd field as 5-bit msbd of extract.
      uint16_t msbd = rd_reg();
      // Interpret sa field as 5-bit lsb of extract.
      uint16_t lsb = sa();
      uint16_t size = msbd + 1;
      uint64_t mask = (size == 64) ? UINT64_MAX : (1ULL << size) - 1;
      alu_out = static_cast<int64_t>((rs_u() & (mask << lsb)) >> lsb);
      SetResult(rt_reg(), alu_out);
      break;
    }
    case DEXTM: {
      // Interpret rd field as 5-bit msbdminus32 of extract.
      uint16_t msbdminus32 = rd_reg();
      // Interpret sa field as 5-bit lsb of extract.
      uint16_t lsb = sa();
      uint16_t size = msbdminus32 + 1 + 32;
      uint64_t mask = (size == 64) ? UINT64_MAX : (1ULL << size) - 1;
      alu_out = static_cast<int64_t>((rs_u() & (mask << lsb)) >> lsb);
      SetResult(rt_reg(), alu_out);
      break;
    }
    case DEXTU: {
      // Interpret rd field as 5-bit msbd of extract.
      uint16_t msbd = rd_reg();
      // Interpret sa field as 5-bit lsbminus32 of extract and add 32 to get
      // lsb.
      uint16_t lsb = sa() + 32;
      uint16_t size = msbd + 1;
      uint64_t mask = (size == 64) ? UINT64_MAX : (1ULL << size) - 1;
      alu_out = static_cast<int64_t>((rs_u() & (mask << lsb)) >> lsb);
      SetResult(rt_reg(), alu_out);
      break;
    }
    case INS: {  // Mips32r2 instruction.
      // Interpret rd field as 5-bit msb of insert.
      uint16_t msb = rd_reg();
      // Interpret sa field as 5-bit lsb of insert.
      uint16_t lsb = sa();
      uint16_t size = msb - lsb + 1;
      uint64_t mask = (1ULL << size) - 1;
      alu_out = static_cast<int32_t>((rt_u() & ~(mask << lsb)) |
                                     ((rs_u() & mask) << lsb));
      SetResult(rt_reg(), alu_out);
      break;
    }
    case DINS: {  // Mips64r2 instruction.
      // Interpret rd field as 5-bit msb of insert.
      uint16_t msb = rd_reg();
      // Interpret sa field as 5-bit lsb of insert.
      uint16_t lsb = sa();
      uint16_t size = msb - lsb + 1;
      uint64_t mask = (1ULL << size) - 1;
      alu_out = (rt_u() & ~(mask << lsb)) | ((rs_u() & mask) << lsb);
      SetResult(rt_reg(), alu_out);
      break;
    }
    case DINSM: {  // Mips64r2 instruction.
      // Interpret rd field as 5-bit msbminus32 of insert.
      uint16_t msbminus32 = rd_reg();
      // Interpret sa field as 5-bit lsb of insert.
      uint16_t lsb = sa();
      uint16_t size = msbminus32 + 32 - lsb + 1;
      uint64_t mask;
      if (size < 64)
        mask = (1ULL << size) - 1;
      else
        mask = std::numeric_limits<uint64_t>::max();
      alu_out = (rt_u() & ~(mask << lsb)) | ((rs_u() & mask) << lsb);
      SetResult(rt_reg(), alu_out);
      break;
    }
    case DINSU: {  // Mips64r2 instruction.
      // Interpret rd field as 5-bit msbminus32 of insert.
      uint16_t msbminus32 = rd_reg();
      // Interpret rd field as 5-bit lsbminus32 of insert.
      uint16_t lsbminus32 = sa();
      uint16_t lsb = lsbminus32 + 32;
      uint16_t size = msbminus32 + 32 - lsb + 1;
      uint64_t mask = (1ULL << size) - 1;
      alu_out = (rt_u() & ~(mask << lsb)) | ((rs_u() & mask) << lsb);
      SetResult(rt_reg(), alu_out);
      break;
    }
    case BSHFL: {
      int32_t sa = instr_.SaFieldRaw() >> kSaShift;
      switch (sa) {
        case BITSWAP: {
          uint32_t input = static_cast<uint32_t>(rt());
          uint32_t output = 0;
          uint8_t i_byte, o_byte;

          // Reverse the bit in byte for each individual byte
          for (int i = 0; i < 4; i++) {
            output = output >> 8;
            i_byte = input & 0xFF;

            // Fast way to reverse bits in byte
            // Devised by Sean Anderson, July 13, 2001
            o_byte = static_cast<uint8_t>(((i_byte * 0x0802LU & 0x22110LU) |
                                           (i_byte * 0x8020LU & 0x88440LU)) *
                                              0x10101LU >>
                                          16);

            output = output | (static_cast<uint32_t>(o_byte << 24));
            input = input >> 8;
          }

          alu_out = static_cast<int64_t>(static_cast<int32_t>(output));
          break;
        }
        case SEB: {
          uint8_t input = static_cast<uint8_t>(rt());
          uint32_t output = input;
          uint32_t mask = 0x00000080;

          // Extending sign
          if (mask & input) {
            output |= 0xFFFFFF00;
          }

          alu_out = static_cast<int32_t>(output);
          break;
        }
        case SEH: {
          uint16_t input = static_cast<uint16_t>(rt());
          uint32_t output = input;
          uint32_t mask = 0x00008000;

          // Extending sign
          if (mask & input) {
            output |= 0xFFFF0000;
          }

          alu_out = static_cast<int32_t>(output);
          break;
        }
        case WSBH: {
          uint32_t input = static_cast<uint32_t>(rt());
          uint64_t output = 0;

          uint32_t mask = 0xFF000000;
          for (int i = 0; i < 4; i++) {
            uint32_t tmp = mask & input;
            if (i % 2 == 0) {
              tmp = tmp >> 8;
            } else {
              tmp = tmp << 8;
            }
            output = output | tmp;
            mask = mask >> 8;
          }
          mask = 0x80000000;

          // Extending sign
          if (mask & output) {
            output |= 0xFFFFFFFF00000000;
          }

          alu_out = static_cast<int64_t>(output);
          break;
        }
        default: {
          const uint8_t bp2 = instr_.Bp2Value();
          sa >>= kBp2Bits;
          switch (sa) {
            case ALIGN: {
              if (bp2 == 0) {
                alu_out = static_cast<int32_t>(rt());
              } else {
                uint64_t rt_hi = rt() << (8 * bp2);
                uint64_t rs_lo = rs() >> (8 * (4 - bp2));
                alu_out = static_cast<int32_t>(rt_hi | rs_lo);
              }
              break;
            }
            default:
              alu_out = 0x12345678;
              UNREACHABLE();
          }
          break;
        }
      }
      SetResult(rd_reg(), alu_out);
      break;
    }
    case DBSHFL: {
      int32_t sa = instr_.SaFieldRaw() >> kSaShift;
      switch (sa) {
        case DBITSWAP: {
          switch (sa) {
            case DBITSWAP_SA: {  // Mips64r6
              uint64_t input = static_cast<uint64_t>(rt());
              uint64_t output = 0;
              uint8_t i_byte, o_byte;

              // Reverse the bit in byte for each individual byte
              for (int i = 0; i < 8; i++) {
                output = output >> 8;
                i_byte = input & 0xFF;

                // Fast way to reverse bits in byte
                // Devised by Sean Anderson, July 13, 2001
                o_byte =
                    static_cast<uint8_t>(((i_byte * 0x0802LU & 0x22110LU) |
                                          (i_byte * 0x8020LU & 0x88440LU)) *
                                             0x10101LU >>
                                         16);

                output = output | ((static_cast<uint64_t>(o_byte) << 56));
                input = input >> 8;
              }

              alu_out = static_cast<int64_t>(output);
              break;
            }
          }
          break;
        }
        case DSBH: {
          uint64_t input = static_cast<uint64_t>(rt());
          uint64_t output = 0;

          uint64_t mask = 0xFF00000000000000;
          for (int i = 0; i < 8; i++) {
            uint64_t tmp = mask & input;
            if (i % 2 == 0)
              tmp = tmp >> 8;
            else
              tmp = tmp << 8;

            output = output | tmp;
            mask = mask >> 8;
          }

          alu_out = static_cast<int64_t>(output);
          break;
        }
        case DSHD: {
          uint64_t input = static_cast<uint64_t>(rt());
          uint64_t output = 0;

          uint64_t mask = 0xFFFF000000000000;
          for (int i = 0; i < 4; i++) {
            uint64_t tmp = mask & input;
            if (i == 0)
              tmp = tmp >> 48;
            else if (i == 1)
              tmp = tmp >> 16;
            else if (i == 2)
              tmp = tmp << 16;
            else
              tmp = tmp << 48;
            output = output | tmp;
            mask = mask >> 16;
          }

          alu_out = static_cast<int64_t>(output);
          break;
        }
        default: {
          const uint8_t bp3 = instr_.Bp3Value();
          sa >>= kBp3Bits;
          switch (sa) {
            case DALIGN: {
              if (bp3 == 0) {
                alu_out = static_cast<int64_t>(rt());
              } else {
                uint64_t rt_hi = rt() << (8 * bp3);
                uint64_t rs_lo = rs() >> (8 * (8 - bp3));
                alu_out = static_cast<int64_t>(rt_hi | rs_lo);
              }
              break;
            }
            default:
              alu_out = 0x12345678;
              UNREACHABLE();
          }
          break;
        }
      }
      SetResult(rd_reg(), alu_out);
      break;
    }
    default:
      UNREACHABLE();
  }
}

int Simulator::DecodeMsaDataFormat() {
  int df = -1;
  if (instr_.IsMSABranchInstr()) {
    switch (instr_.RsFieldRaw()) {
      case BZ_V:
      case BNZ_V:
        df = MSA_VECT;
        break;
      case BZ_B:
      case BNZ_B:
        df = MSA_BYTE;
        break;
      case BZ_H:
      case BNZ_H:
        df = MSA_HALF;
        break;
      case BZ_W:
      case BNZ_W:
        df = MSA_WORD;
        break;
      case BZ_D:
      case BNZ_D:
        df = MSA_DWORD;
        break;
      default:
        UNREACHABLE();
    }
  } else {
    int DF[] = {MSA_BYTE, MSA_HALF, MSA_WORD, MSA_DWORD};
    switch (instr_.MSAMinorOpcodeField()) {
      case kMsaMinorI5:
      case kMsaMinorI10:
      case kMsaMinor3R:
        df = DF[instr_.Bits(22, 21)];
        break;
      case kMsaMinorMI10:
        df = DF[instr_.Bits(1, 0)];
        break;
      case kMsaMinorBIT:
        df = DF[instr_.MsaBitDf()];
        break;
      case kMsaMinorELM:
        df = DF[instr_.MsaElmDf()];
        break;
      case kMsaMinor3RF: {
        uint32_t opcode = instr_.InstructionBits() & kMsa3RFMask;
        switch (opcode) {
          case FEXDO:
          case FTQ:
          case MUL_Q:
          case MADD_Q:
          case MSUB_Q:
          case MULR_Q:
          case MADDR_Q:
          case MSUBR_Q:
            df = DF[1 + instr_.Bit(21)];
            break;
          default:
            df = DF[2 + instr_.Bit(21)];
            break;
        }
      } break;
      case kMsaMinor2R:
        df = DF[instr_.Bits(17, 16)];
        break;
      case kMsaMinor2RF:
        df = DF[2 + instr_.Bit(16)];
        break;
      default:
        UNREACHABLE();
    }
  }
  return df;
}

void Simulator::DecodeTypeMsaI8() {
  DCHECK_EQ(kArchVariant, kMips64r6);
  DCHECK(CpuFeatures::IsSupported(MIPS_SIMD));
  uint32_t opcode = instr_.InstructionBits() & kMsaI8Mask;
  int8_t i8 = instr_.MsaImm8Value();
  msa_reg_t ws, wd;

  switch (opcode) {
    case ANDI_B:
      get_msa_register(instr_.WsValue(), ws.b);
      for (int i = 0; i < kMSALanesByte; i++) {
        wd.b[i] = ws.b[i] & i8;
      }
      set_msa_register(instr_.WdValue(), wd.b);
      TraceMSARegWr(wd.b);
      break;
    case ORI_B:
      get_msa_register(instr_.WsValue(), ws.b);
      for (int i = 0; i < kMSALanesByte; i++) {
        wd.b[i] = ws.b[i] | i8;
      }
      set_msa_register(instr_.WdValue(), wd.b);
      TraceMSARegWr(wd.b);
      break;
    case NORI_B:
      get_msa_register(instr_.WsValue(), ws.b);
      for (int i = 0; i < kMSALanesByte; i++) {
        wd.b[i] = ~(ws.b[i] | i8);
      }
      set_msa_register(instr_.WdValue(), wd.b);
      TraceMSARegWr(wd.b);
      break;
    case XORI_B:
      get_msa_register(instr_.WsValue(), ws.b);
      for (int i = 0; i < kMSALanesByte; i++) {
        wd.b[i] = ws.b[i] ^ i8;
      }
      set_msa_register(instr_.WdValue(), wd.b);
      TraceMSARegWr(wd.b);
      break;
    case BMNZI_B:
      get_msa_register(instr_.WsValue(), ws.b);
      get_msa_register(instr_.WdValue(), wd.b);
      for (int i = 0; i < kMSALanesByte; i++) {
        wd.b[i] = (ws.b[i] & i8) | (wd.b[i] & ~i8);
      }
      set_msa_register(instr_.WdValue(), wd.b);
      TraceMSARegWr(wd.b);
      break;
    case BMZI_B:
      get_msa_register(instr_.WsValue(), ws.b);
      get_msa_register(instr_.WdValue(), wd.b);
      for (int i = 0; i < kMSALanesByte; i++) {
        wd.b[i] = (ws.b[i] & ~i8) | (wd.b[i] & i8);
      }
      set_msa_register(instr_.WdValue(), wd.b);
      TraceMSARegWr(wd.b);
      break;
    case BSELI_B:
      get_msa_register(instr_.WsValue(), ws.b);
      get_msa_register(instr_.WdValue(), wd.b);
      for (int i = 0; i < kMSALanesByte; i++) {
        wd.b[i] = (ws.b[i] & ~wd.b[i]) | (wd.b[i] & i8);
      }
      set_msa_register(instr_.WdValue(), wd.b);
      TraceMSARegWr(wd.b);
      break;
    case SHF_B:
      get_msa_register(instr_.WsValue(), ws.b);
      for (int i = 0; i < kMSALanesByte; i++) {
        int j = i % 4;
        int k = (i8 >> (2 * j)) & 0x3;
        wd.b[i] = ws.b[i - j + k];
      }
      set_msa_register(instr_.WdValue(), wd.b);
      TraceMSARegWr(wd.b);
      break;
    case SHF_H:
      get_msa_register(instr_.WsValue(), ws.h);
      for (int i = 0; i < kMSALanesHalf; i++) {
        int j = i % 4;
        int k = (i8 >> (2 * j)) & 0x3;
        wd.h[i] = ws.h[i - j + k];
      }
      set_msa_register(instr_.WdValue(), wd.h);
      TraceMSARegWr(wd.h);
      break;
    case SHF_W:
      get_msa_register(instr_.WsValue(), ws.w);
      for (int i = 0; i < kMSALanesWord; i++) {
        int j = (i8 >> (2 * i)) & 0x3;
        wd.w[i] = ws.w[j];
      }
      set_msa_register(instr_.WdValue(), wd.w);
      TraceMSARegWr(wd.w);
      break;
    default:
      UNREACHABLE();
  }
}

template <typename T>
T Simulator::MsaI5InstrHelper(uint32_t opcode, T ws, int32_t i5) {
  T res;
  uint32_t ui5 = i5 & 0x1Fu;
  uint64_t ws_u64 = static_cast<uint64_t>(ws);
  uint64_t ui5_u64 = static_cast<uint64_t>(ui5);

  switch (opcode) {
    case ADDVI:
      res = static_cast<T>(ws + ui5);
      break;
    case SUBVI:
      res = static_cast<T>(ws - ui5);
      break;
    case MAXI_S:
      res = static_cast<T>(std::max(ws, static_cast<T>(i5)));
      break;
    case MINI_S:
      res = static_cast<T>(std::min(ws, static_cast<T>(i5)));
      break;
    case MAXI_U:
      res = static_cast<T>(std::max(ws_u64, ui5_u64));
      break;
    case MINI_U:
      res = static_cast<T>(std::min(ws_u64, ui5_u64));
      break;
    case CEQI:
      res = static_cast<T>(!Compare(ws, static_cast<T>(i5)) ? -1ull : 0ull);
      break;
    case CLTI_S:
      res = static_cast<T>((Compare(ws, static_cast<T>(i5)) == -1) ? -1ull
                                                                   : 0ull);
      break;
    case CLTI_U:
      res = static_cast<T>((Compare(ws_u64, ui5_u64) == -1) ? -1ull : 0ull);
      break;
    case CLEI_S:
      res =
          static_cast<T>((Compare(ws, static_cast<T>(i5)) != 1) ? -1ull : 0ull);
      break;
    case CLEI_U:
      res = static_cast<T>((Compare(ws_u64, ui5_u64) != 1) ? -1ull : 0ull);
      break;
    default:
      UNREACHABLE();
  }
  return res;
}

void Simulator::DecodeTypeMsaI5() {
  DCHECK_EQ(kArchVariant, kMips64r6);
  DCHECK(CpuFeatures::IsSupported(MIPS_SIMD));
  uint32_t opcode = instr_.InstructionBits() & kMsaI5Mask;
  msa_reg_t ws, wd;

  // sign extend 5bit value to int32_t
  int32_t i5 = static_cast<int32_t>(instr_.MsaImm5Value() << 27) >> 27;

#define MSA_I5_DF(elem, num_of_lanes)                      \
  get_msa_register(instr_.WsValue(), ws.elem);             \
  for (int i = 0; i < num_of_lanes; i++) {                 \
    wd.elem[i] = MsaI5InstrHelper(opcode, ws.elem[i], i5); \
  }                                                        \
  set_msa_register(instr_.WdValue(), wd.elem);             \
  TraceMSARegWr(wd.elem)

  switch (DecodeMsaDataFormat()) {
    case MSA_BYTE:
      MSA_I5_DF(b, kMSALanesByte);
      break;
    case MSA_HALF:
      MSA_I5_DF(h, kMSALanesHalf);
      break;
    case MSA_WORD:
      MSA_I5_DF(w, kMSALanesWord);
      break;
    case MSA_DWORD:
      MSA_I5_DF(d, kMSALanesDword);
      break;
    default:
      UNREACHABLE();
  }
#undef MSA_I5_DF
}

void Simulator::DecodeTypeMsaI10() {
  DCHECK_EQ(kArchVariant, kMips64r6);
  DCHECK(CpuFeatures::IsSupported(MIPS_SIMD));
  uint32_t opcode = instr_.InstructionBits() & kMsaI5Mask;
  int64_t s10 = (static_cast<int64_t>(instr_.MsaImm10Value()) << 54) >> 54;
  msa_reg_t wd;

#define MSA_I10_DF(elem, num_of_lanes, T)      \
  for (int i = 0; i < num_of_lanes; ++i) {     \
    wd.elem[i] = static_cast<T>(s10);          \
  }                                            \
  set_msa_register(instr_.WdValue(), wd.elem); \
  TraceMSARegWr(wd.elem)

  if (opcode == LDI) {
    switch (DecodeMsaDataFormat()) {
      case MSA_BYTE:
        MSA_I10_DF(b, kMSALanesByte, int8_t);
        break;
      case MSA_HALF:
        MSA_I10_DF(h, kMSALanesHalf, int16_t);
        break;
      case MSA_WORD:
        MSA_I10_DF(w, kMSALanesWord, int32_t);
        break;
      case MSA_DWORD:
        MSA_I10_DF(d, kMSALanesDword, int64_t);
        break;
      default:
        UNREACHABLE();
    }
  } else {
    UNREACHABLE();
  }
#undef MSA_I10_DF
}

void Simulator::DecodeTypeMsaELM() {
  DCHECK_EQ(kArchVariant, kMips64r6);
  DCHECK(CpuFeatures::IsSupported(MIPS_SIMD));
  uint32_t opcode = instr_.InstructionBits() & kMsaLongerELMMask;
  int32_t n = instr_.MsaElmNValue();
  int64_t alu_out;
  switch (opcode) {
    case CTCMSA:
      DCHECK_EQ(sa(), kMSACSRRegister);
      MSACSR_ = base::bit_cast<uint32_t>(
          static_cast<int32_t>(registers_[rd_reg()] & kMaxUInt32));
      TraceRegWr(static_cast<int32_t>(MSACSR_));
      break;
    case CFCMSA:
      DCHECK_EQ(rd_reg(), kMSACSRRegister);
      SetResult(sa(), static_cast<int64_t>(base::bit_cast<int32_t>(MSACSR_)));
      break;
    case MOVE_V: {
      msa_reg_t ws;
      get_msa_register(ws_reg(), &ws);
      set_msa_register(wd_reg(), &ws);
      TraceMSARegWr(&ws);
    } break;
    default:
      opcode &= kMsaELMMask;
      switch (opcode) {
        case COPY_S:
        case COPY_U: {
          msa_reg_t ws;
          switch (DecodeMsaDataFormat()) {
            case MSA_BYTE:
              DCHECK_LT(n, kMSALanesByte);
              get_msa_register(instr_.WsValue(), ws.b);
              alu_out = static_cast<int32_t>(ws.b[n]);
              SetResult(wd_reg(),
                        (opcode == COPY_U) ? alu_out & 0xFFu : alu_out);
              break;
            case MSA_HALF:
              DCHECK_LT(n, kMSALanesHalf);
              get_msa_register(instr_.WsValue(), ws.h);
              alu_out = static_cast<int32_t>(ws.h[n]);
              SetResult(wd_reg(),
                        (opcode == COPY_U) ? alu_out & 0xFFFFu : alu_out);
              break;
            case MSA_WORD:
              DCHECK_LT(n, kMSALanesWord);
              get_msa_register(instr_.WsValue(), ws.w);
              alu_out = static_cast<int32_t>(ws.w[n]);
              SetResult(wd_reg(),
                        (opcode == COPY_U) ? alu_out & 0xFFFFFFFFu : alu_out);
              break;
            case MSA_DWORD:
              DCHECK_LT(n, kMSALanesDword);
              get_msa_register(instr_.WsValue(), ws.d);
              alu_out = static_cast<int64_t>(ws.d[n]);
              SetResult(wd_reg(), alu_out);
              break;
            default:
              UNREACHABLE();
          }
        } break;
        case INSERT: {
          msa_reg_t wd;
          switch (DecodeMsaDataFormat()) {
            case MSA_BYTE: {
              DCHECK_LT(n, kMSALanesByte);
              int64_t rs = get_register(instr_.WsValue());
              get_msa_register(instr_.WdValue(), wd.b);
              wd.b[n] = rs & 0xFFu;
              set_msa_register(instr_.WdValue(), wd.b);
              TraceMSARegWr(wd.b);
              break;
            }
            case MSA_HALF: {
              DCHECK_LT(n, kMSALanesHalf);
              int64_t rs = get_register(instr_.WsValue());
              get_msa_register(instr_.WdValue(), wd.h);
              wd.h[n] = rs & 0xFFFFu;
              set_msa_register(instr_.WdValue(), wd.h);
              TraceMSARegWr(wd.h);
              break;
            }
            case MSA_WORD: {
              DCHECK_LT(n, kMSALanesWord);
              int64_t rs = get_register(instr_.WsValue());
              get_msa_register(instr_.WdValue(), wd.w);
              wd.w[n] = rs & 0xFFFFFFFFu;
              set_msa_register(instr_.WdValue(), wd.w);
              TraceMSARegWr(wd.w);
              break;
            }
            case MSA_DWORD: {
              DCHECK_LT(n, kMSALanesDword);
              int64_t rs = get_register(instr_.WsValue());
              get_msa_register(instr_.WdValue(), wd.d);
              wd.d[n] = rs;
              set_msa_register(instr_.WdValue(), wd.d);
              TraceMSARegWr(wd.d);
              break;
            }
            default:
              UNREACHABLE();
          }
        } break;
        case SLDI: {
          uint8_t v[32];
          msa_reg_t ws;
          msa_reg_t wd;
          get_msa_register(ws_reg(), &ws);
          get_msa_register(wd_reg(), &wd);
#define SLDI_DF(s, k)                \
  for (unsigned i = 0; i < s; i++) { \
    v[i] = ws.b[s * k + i];          \
    v[i + s] = wd.b[s * k + i];      \
  }                                  \
  for (unsigned i = 0; i < s; i++) { \
    wd.b[s * k + i] = v[i + n];      \
  }
          switch (DecodeMsaDataFormat()) {
            case MSA_BYTE:
              DCHECK(n < kMSALanesByte);
              SLDI_DF(kMSARegSize / sizeof(int8_t) / kBitsPerByte, 0)
              break;
            case MSA_HALF:
              DCHECK(n < kMSALanesHalf);
              for (int k = 0; k < 2; ++k) {
                SLDI_DF(kMSARegSize / sizeof(int16_t) / kBitsPerByte, k)
              }
              break;
            case MSA_WORD:
              DCHECK(n < kMSALanesWord);
              for (int k = 0; k < 4; ++k) {
                SLDI_DF(kMSARegSize / sizeof(int32_t) / kBitsPerByte, k)
              }
              break;
            case MSA_DWORD:
              DCHECK(n < kMSALanesDword);
              for (int k = 0; k < 8; ++k) {
                SLDI_DF(kMSARegSize / sizeof(int64_t) / kBitsPerByte, k)
              }
              break;
            default:
              UNREACHABLE();
          }
          set_msa_register(wd_reg(), &wd);
          TraceMSARegWr(&wd);
        } break;
#undef SLDI_DF
        case SPLATI:
        case INSVE:
          UNIMPLEMENTED();
        default:
          UNREACHABLE();
      }
      break;
  }
}

template <typename T>
T Simulator::MsaBitInstrHelper(uint32_t opcode, T wd, T ws, int32_t m) {
  using uT = typename std::make_unsigned<T>::type;
  T res;
  switch (opcode) {
    case SLLI:
      res = static_cast<T>(ws << m);
      break;
    case SRAI:
      res = static_cast<T>(ArithmeticShiftRight(ws, m));
      break;
    case SRLI:
      res = static_cast<T>(static_cast<uT>(ws) >> m);
      break;
    case BCLRI:
      res = static_cast<T>(static_cast<T>(~(1ull << m)) & ws);
      break;
    case BSETI:
      res = static_cast<T>(static_cast<T>(1ull << m) | ws);
      break;
    case BNEGI:
      res = static_cast<T>(static_cast<T>(1ull << m) ^ ws);
      break;
    case BINSLI: {
      int elem_size = 8 * sizeof(T);
      int bits = m + 1;
      if (bits == elem_size) {
        res = static_cast<T>(ws);
      } else {
        uint64_t mask = ((1ull << bits) - 1) << (elem_size - bits);
        res = static_cast<T>((static_cast<T>(mask) & ws) |
                             (static_cast<T>(~mask) & wd));
      }
    } break;
    case BINSRI: {
      int elem_size = 8 * sizeof(T);
      int bits = m + 1;
      if (bits == elem_size) {
        res = static_cast<T>(ws);
      } else {
        uint64_t mask = (1ull << bits) - 1;
        res = static_cast<T>((static_cast<T>(mask) & ws) |
                             (static_cast<T>(~mask) & wd));
      }
    } break;
    case SAT_S: {
#define M_MAX_INT(x) static_cast<int64_t>((1LL << ((x)-1)) - 1)
#define M_MIN_INT(x) static_cast<int64_t>(-(1LL << ((x)-1)))
      int shift = 64 - 8 * sizeof(T);
      int64_t ws_i64 = (static_cast<int64_t>(ws) << shift) >> shift;
      res = static_cast<T>(ws_i64 < M_MIN_INT(m + 1)
                               ? M_MIN_INT(m + 1)
                               : ws_i64 > M_MAX_INT(m + 1) ? M_MAX_INT(m + 1)
                                                           : ws_i64);
#undef M_MAX_INT
#undef M_MIN_INT
    } break;
    case SAT_U: {
#define M_MAX_UINT(x) static_cast<uint64_t>(-1ULL >> (64 - (x)))
      uint64_t mask = static_cast<uint64_t>(-1ULL >> (64 - 8 * sizeof(T)));
      uint64_t ws_u64 = static_cast<uint64_t>(ws) & mask;
      res = static_cast<T>(ws_u64 < M_MAX_UINT(m + 1) ? ws_u64
                                                      : M_MAX_UINT(m + 1));
#undef M_MAX_UINT
    } break;
    case SRARI:
      if (!m) {
        res = static_cast<T>(ws);
      } else {
        res = static_cast<T>(ArithmeticShiftRight(ws, m)) +
              static_cast<T>((ws >> (m - 1)) & 0x1);
      }
      break;
    case SRLRI:
      if (!m) {
        res = static_cast<T>(ws);
      } else {
        res = static_cast<T>(static_cast<uT>(ws) >> m) +
              static_cast<T>((ws >> (m - 1)) & 0x1);
      }
      break;
    default:
      UNREACHABLE();
  }
  return res;
}

void Simulator::DecodeTypeMsaBIT() {
  DCHECK_EQ(kArchVariant, kMips64r6);
  DCHECK(CpuFeatures::IsSupported(MIPS_SIMD));
  uint32_t opcode = instr_.InstructionBits() & kMsaBITMask;
  int32_t m = instr_.MsaBitMValue();
  msa_reg_t wd, ws;

#define MSA_BIT_DF(elem, num_of_lanes)                                 \
  get_msa_register(instr_.WsValue(), ws.elem);                         \
  if (opcode == BINSLI || opcode == BINSRI) {                          \
    get_msa_register(instr_.WdValue(), wd.elem);                       \
  }                                                                    \
  for (int i = 0; i < num_of_lanes; i++) {                             \
    wd.elem[i] = MsaBitInstrHelper(opcode, wd.elem[i], ws.elem[i], m); \
  }                                                                    \
  set_msa_register(instr_.WdValue(), wd.elem);                         \
  TraceMSARegWr(wd.elem)

  switch (DecodeMsaDataFormat()) {
    case MSA_BYTE:
      DCHECK(m < kMSARegSize / kMSALanesByte);
      MSA_BIT_DF(b, kMSALanesByte);
      break;
    case MSA_HALF:
      DCHECK(m < kMSARegSize / kMSALanesHalf);
      MSA_BIT_DF(h, kMSALanesHalf);
      break;
    case MSA_WORD:
      DCHECK(m < kMSARegSize / kMSALanesWord);
      MSA_BIT_DF(w, kMSALanesWord);
      break;
    case MSA_DWORD:
      DCHECK(m < kMSARegSize / kMSALanesDword);
      MSA_BIT_DF(d, kMSALanesDword);
      break;
    default:
      UNREACHABLE();
  }
#undef MSA_BIT_DF
}

void Simulator::DecodeTypeMsaMI10() {
  DCHECK_EQ(kArchVariant, kMips64r6);
  DCHECK(CpuFeatures::IsSupported(MIPS_SIMD));
  uint32_t opcode = instr_.InstructionBits() & kMsaMI10Mask;
  int64_t s10 = (static_cast<int64_t>(instr_.MsaImmMI10Value()) << 54) >> 54;
  int64_t rs = get_register(instr_.WsValue());
  int64_t addr;
  msa_reg_t wd;

#define MSA_MI10_LOAD(elem, num_of_lanes, T)       \
  for (int i = 0; i < num_of_lanes; ++i) {         \
    addr = rs + (s10 + i) * sizeof(T);             \
    wd.elem[i] = ReadMem<T>(addr, instr_.instr()); \
  }                                                \
  set_msa_register(instr_.WdValue(), wd.elem);

#define MSA_MI10_STORE(elem, num_of_lanes, T)      \
  get_msa_register(instr_.WdValue(), wd.elem);     \
  for (int i = 0; i < num_of_lanes; ++i) {         \
    addr = rs + (s10 + i) * sizeof(T);             \
    WriteMem<T>(addr, wd.elem[i], instr_.instr()); \
  }

  if (opcode == MSA_LD) {
    switch (DecodeMsaDataFormat()) {
      case MSA_BYTE:
        MSA_MI10_LOAD(b, kMSALanesByte, int8_t);
        break;
      case MSA_HALF:
        MSA_MI10_LOAD(h, kMSALanesHalf, int16_t);
        break;
      case MSA_WORD:
        MSA_MI10_LOAD(w, kMSALanesWord, int32_t);
        break;
      case MSA_DWORD:
        MSA_MI10_LOAD(d, kMSALanesDword, int64_t);
        break;
      default:
        UNREACHABLE();
    }
  } else if (opcode == MSA_ST) {
    switch (DecodeMsaDataFormat()) {
      case MSA_BYTE:
        MSA_MI10_STORE(b, kMSALanesByte, int8_t);
        break;
      case MSA_HALF:
        MSA_MI10_STORE(h, kMSALanesHalf, int16_t);
        break;
      case MSA_WORD:
        MSA_MI10_STORE(w, kMSALanesWord, int32_t);
        break;
      case MSA_DWORD:
        MSA_MI10_STORE(d, kMSALanesDword, int64_t);
        break;
      default:
        UNREACHABLE();
    }
  } else {
    UNREACHABLE();
  }

#undef MSA_MI10_LOAD
#undef MSA_MI10_STORE
}

template <typename T>
T Simulator::Msa3RInstrHelper(uint32_t opcode, T wd, T ws, T wt) {
  using uT = typename std::make_unsigned<T>::type;
  T res;
  int wt_modulo = wt % (sizeof(T) * 8);
  switch (opcode) {
    case SLL_MSA:
      res = static_cast<T>(ws << wt_modulo);
      break;
    case SRA_MSA:
      res = static_cast<T>(ArithmeticShiftRight(ws, wt_modulo));
      break;
    case SRL_MSA:
      res = static_cast<T>(static_cast<uT>(ws) >> wt_modulo);
      break;
    case BCLR:
      res = static_cast<T>(static_cast<T>(~(1ull << wt_modulo)) & ws);
      break;
    case BSET:
      res = static_cast<T>(static_cast<T>(1ull << wt_modulo) | ws);
      break;
    case BNEG:
      res = static_cast<T>(static_cast<T>(1ull << wt_modulo) ^ ws);
      break;
    case BINSL: {
      int elem_size = 8 * sizeof(T);
      int bits = wt_modulo + 1;
      if (bits == elem_size) {
        res = static_cast<T>(ws);
      } else {
        uint64_t mask = ((1ull << bits) - 1) << (elem_size - bits);
        res = static_cast<T>((static_cast<T>(mask) & ws) |
                             (static_cast<T>(~mask) & wd));
      }
    } break;
    case BINSR: {
      int elem_size = 8 * sizeof(T);
      int bits = wt_modulo + 1;
      if (bits == elem_size) {
        res = static_cast<T>(ws);
      } else {
        uint64_t mask = (1ull << bits) - 1;
        res = static_cast<T>((static_cast<T>(mask) & ws) |
                             (static_cast<T>(~mask) & wd));
      }
    } break;
    case ADDV:
      res = ws + wt;
      break;
    case SUBV:
      res = ws - wt;
      break;
    case MAX_S:
      res = std::max(ws, wt);
      break;
    case MAX_U:
      res = static_cast<T>(std::max(static_cast<uT>(ws), static_cast<uT>(wt)));
      break;
    case MIN_S:
      res = std::min(ws, wt);
      break;
    case MIN_U:
      res = static_cast<T>(std::min(static_cast<uT>(ws), static_cast<uT>(wt)));
      break;
    case MAX_A:
      // We use negative abs in order to avoid problems
      // with corner case for MIN_INT
      res = Nabs(ws) < Nabs(wt) ? ws : wt;
      break;
    case MIN_A:
      // We use negative abs in order to avoid problems
      // with corner case for MIN_INT
      res = Nabs(ws) > Nabs(wt) ? ws : wt;
      break;
    case CEQ:
      res = static_cast<T>(!Compare(ws, wt) ? -1ull : 0ull);
      break;
    case CLT_S:
      res = static_cast<T>((Compare(ws, wt) == -1) ? -1ull : 0ull);
      break;
    case CLT_U:
      res = static_cast<T>(
          (Compare(static_cast<uT>(ws), static_cast<uT>(wt)) == -1) ? -1ull
                                                                    : 0ull);
      break;
    case CLE_S:
      res = static_cast<T>((Compare(ws, wt) != 1) ? -1ull : 0ull);
      break;
    case CLE_U:
      res = static_cast<T>(
          (Compare(static_cast<uT>(ws), static_cast<uT>(wt)) != 1) ? -1ull
                                                                   : 0ull);
      break;
    case ADD_A:
      res = static_cast<T>(Abs(ws) + Abs(wt));
      break;
    case ADDS_A: {
      T ws_nabs = Nabs(ws);
      T wt_nabs = Nabs(wt);
      if (ws_nabs < -std::numeric_limits<T>::max() - wt_nabs) {
        res = std::numeric_limits<T>::max();
      } else {
        res = -(ws_nabs + wt_nabs);
      }
    } break;
    case ADDS_S:
      res = SaturateAdd(ws, wt);
      break;
    case ADDS_U: {
      uT ws_u = static_cast<uT>(ws);
      uT wt_u = static_cast<uT>(wt);
      res = static_cast<T>(SaturateAdd(ws_u, wt_u));
    } break;
    case AVE_S:
      res = static_cast<T>((wt & ws) + ((wt ^ ws) >> 1));
      break;
    case AVE_U: {
      uT ws_u = static_cast<uT>(ws);
      uT wt_u = static_cast<uT>(wt);
      res = static_cast<T>((wt_u & ws_u) + ((wt_u ^ ws_u) >> 1));
    } break;
    case AVER_S:
      res = static_cast<T>((wt | ws) - ((wt ^ ws) >> 1));
      break;
    case AVER_U: {
      uT ws_u = static_cast<uT>(ws);
      uT wt_u = static_cast<uT>(wt);
      res = static_cast<T>((wt_u | ws_u) - ((wt_u ^ ws_u) >> 1));
    } break;
    case SUBS_S:
      res = SaturateSub(ws, wt);
      break;
    case SUBS_U: {
      uT ws_u = static_cast<uT>(ws);
      uT wt_u = static_cast<uT>(wt);
      res = static_cast<T>(SaturateSub(ws_u, wt_u));
    } break;
    case SUBSUS_U: {
      uT wsu = static_cast<uT>(ws);
      if (wt > 0) {
        uT wtu = static_cast<uT>(wt);
        if (wtu > wsu) {
          res = 0;
        } else {
          res = static_cast<T>(wsu - wtu);
        }
      } else {
        if (wsu > std::numeric_limits<uT>::max() + wt) {
          res = static_cast<T>(std::numeric_limits<uT>::max());
        } else {
          res = static_cast<T>(wsu - wt);
        }
      }
    } break;
    case SUBSUU_S: {
      uT wsu = static_cast<uT>(ws);
      uT wtu = static_cast<uT>(wt);
      uT wdu;
      if (wsu > wtu) {
        wdu = wsu - wtu;
        if (wdu > std::numeric_limits<T>::max()) {
          res = std::numeric_limits<T>::max();
        } else {
          res = static_cast<T>(wdu);
        }
      } else {
        wdu = wtu - wsu;
        CHECK(-std::numeric_limits<T>::max() ==
              std::numeric_limits<T>::min() + 1);
        if (wdu <= std::numeric_limits<T>::max()) {
          res = -static_cast<T>(wdu);
        } else {
          res = std::numeric_limits<T>::min();
        }
      }
    } break;
    case ASUB_S:
      res = static_cast<T>(Abs(ws - wt));
      break;
    case ASUB_U: {
      uT wsu = static_cast<uT>(ws);
      uT wtu = static_cast<uT>(wt);
      res = static_cast<T>(wsu > wtu ? wsu - wtu : wtu - wsu);
    } break;
    case MULV:
      res = ws * wt;
      break;
    case MADDV:
      res = wd + ws * wt;
      break;
    case MSUBV:
      res = wd - ws * wt;
      break;
    case DIV_S_MSA:
      res = wt != 0 ? ws / wt : static_cast<T>(Unpredictable);
      break;
    case DIV_U:
      res = wt != 0 ? static_cast<T>(static_cast<uT>(ws) / static_cast<uT>(wt))
                    : static_cast<T>(Unpredictable);
      break;
    case MOD_S:
      res = wt != 0 ? ws % wt : static_cast<T>(Unpredictable);
      break;
    case MOD_U:
      res = wt != 0 ? static_cast<T>(static_cast<uT>(ws) % static_cast<uT>(wt))
                    : static_cast<T>(Unpredictable);
      break;
    case DOTP_S:
    case DOTP_U:
    case DPADD_S:
    case DPADD_U:
    case DPSUB_S:
    case DPSUB_U:
    case SLD:
    case SPLAT:
      UNIMPLEMENTED();
      break;
    case SRAR: {
      int bit = wt_modulo == 0 ? 0 : (ws >> (wt_modulo - 1)) & 1;
      res = static_cast<T>(ArithmeticShiftRight(ws, wt_modulo) + bit);
    } break;
    case SRLR: {
      uT wsu = static_cast<uT>(ws);
      int bit = wt_modulo == 0 ? 0 : (wsu >> (wt_modulo - 1)) & 1;
      res = static_cast<T>((wsu >> wt_modulo) + bit);
    } break;
    default:
      UNREACHABLE();
  }
  return res;
}
template <typename T_int, typename T_reg>
void Msa3RInstrHelper_shuffle(const uint32_t opcode, T_reg ws, T_reg wt,
                              T_reg wd, const int i, const int num_of_lanes) {
  T_int *ws_p, *wt_p, *wd_p;
  ws_p = reinterpret_cast<T_int*>(ws);
  wt_p = reinterpret_cast<T_int*>(wt);
  wd_p = reinterpret_cast<T_int*>(wd);
  switch (opcode) {
    case PCKEV:
      wd_p[i] = wt_p[2 * i];
      wd_p[i + num_of_lanes / 2] = ws_p[2 * i];
      break;
    case PCKOD:
      wd_p[i] = wt_p[2 * i + 1];
      wd_p[i + num_of_lanes / 2] = ws_p[2 * i + 1];
      break;
    case ILVL:
      wd_p[2 * i] = wt_p[i + num_of_lanes / 2];
      wd_p[2 * i + 1] = ws_p[i + num_of_lanes / 2];
      break;
    case ILVR:
      wd_p[2 * i] = wt_p[i];
      wd_p[2 * i + 1] = ws_p[i];
      break;
    case ILVEV:
      wd_p[2 * i] = wt_p[2 * i];
      wd_p[2 * i + 1] = ws_p[2 * i];
      break;
    case ILVOD:
      wd_p[2 * i] = wt_p[2 * i + 1];
      wd_p[2 * i + 1] = ws_p[2 * i + 1];
      break;
    case VSHF: {
      const int mask_not_valid = 0xC0;
      const int mask_6_bits = 0x3F;
      if ((wd_p[i] & mask_not_valid)) {
        wd_p[i] = 0;
      } else {
        int k = (wd_p[i] & mask_6_bits) % (num_of_lanes * 2);
        wd_p[i] = k >= num_of_lanes ? ws_p[k - num_of_lanes] : wt_p[k];
      }
    } break;
    default:
      UNREACHABLE();
  }
}

template <typename T_int, typename T_smaller_int, typename T_reg>
void Msa3RInstrHelper_horizontal(const uint32_t opcode, T_reg ws, T_reg wt,
                                 T_reg wd, const int i,
                                 const int num_of_lanes) {
  using T_uint = typename std::make_unsigned<T_int>::type;
  using T_smaller_uint = typename std::make_unsigned<T_smaller_int>::type;
  T_int* wd_p;
  T_smaller_int *ws_p, *wt_p;
  ws_p = reinterpret_cast<T_smaller_int*>(ws);
  wt_p = reinterpret_cast<T_smaller_int*>(wt);
  wd_p = reinterpret_cast<T_int*>(wd);
  T_uint* wd_pu;
  T_smaller_uint *ws_pu, *wt_pu;
  ws_pu = reinterpret_cast<T_smaller_uint*>(ws);
  wt_pu = reinterpret_cast<T_smaller_uint*>(wt);
  wd_pu = reinterpret_cast<T_uint*>(wd);
  switch (opcode) {
    case HADD_S:
      wd_p[i] =
          static_cast<T_int>(ws_p[2 * i + 1]) + static_cast<T_int>(wt_p[2 * i]);
      break;
    case HADD_U:
      wd_pu[i] = static_cast<T_uint>(ws_pu[2 * i + 1]) +
                 static_cast<T_uint>(wt_pu[2 * i]);
      break;
    case HSUB_S:
      wd_p[i] =
          static_cast<T_int>(ws_p[2 * i + 1]) - static_cast<T_int>(wt_p[2 * i]);
      break;
    case HSUB_U:
      wd_pu[i] = static_cast<T_uint>(ws_pu[2 * i + 1]) -
                 static_cast<T_uint>(wt_pu[2 * i]);
      break;
    default:
      UNREACHABLE();
  }
}

void Simulator::DecodeTypeMsa3R() {
  DCHECK_EQ(kArchVariant, kMips64r6);
  DCHECK(CpuFeatures::IsSupported(MIPS_SIMD));
  uint32_t opcode = instr_.InstructionBits() & kMsa3RMask;
  msa_reg_t ws, wd, wt;
  get_msa_register(ws_reg(), &ws);
  get_msa_register(wt_reg(), &wt);
  get_msa_register(wd_reg(), &wd);
  switch (opcode) {
    case HADD_S:
    case HADD_U:
    case HSUB_S:
    case HSUB_U:
#define HORIZONTAL_ARITHMETIC_DF(num_of_lanes, int_type, lesser_int_type) \
  for (int i = 0; i < num_of_lanes; ++i) {                                \
    Msa3RInstrHelper_horizontal<int_type, lesser_int_type>(               \
        opcode, &ws, &wt, &wd, i, num_of_lanes);                          \
  }
      switch (DecodeMsaDataFormat()) {
        case MSA_HALF:
          HORIZONTAL_ARITHMETIC_DF(kMSALanesHalf, int16_t, int8_t);
          break;
        case MSA_WORD:
          HORIZONTAL_ARITHMETIC_DF(kMSALanesWord, int32_t, int16_t);
          break;
        case MSA_DWORD:
          HORIZONTAL_ARITHMETIC_DF(kMSALanesDword, int64_t, int32_t);
          break;
        default:
          UNREACHABLE();
      }
      break;
#undef HORIZONTAL_ARITHMETIC_DF
    case VSHF:
#define VSHF_DF(num_of_lanes, int_type)                          \
  for (int i = 0; i < num_of_lanes; ++i) {                       \
    Msa3RInstrHelper_shuffle<int_type>(opcode, &ws, &wt, &wd, i, \
                                       num_of_lanes);            \
  }
      switch (DecodeMsaDataFormat()) {
        case MSA_BYTE:
          VSHF_DF(kMSALanesByte, int8_t);
          break;
        case MSA_HALF:
          VSHF_DF(kMSALanesHalf, int16_t);
          break;
        case MSA_WORD:
          VSHF_DF(kMSALanesWord, int32_t);
          break;
        case MSA_DWORD:
          VSHF_DF(kMSALanesDword, int64_t);
          break;
        default:
          UNREACHABLE();
      }
#undef VSHF_DF
      break;
    case PCKEV:
    case PCKOD:
    case ILVL:
    case ILVR:
    case ILVEV:
    case ILVOD:
#define INTERLEAVE_PACK_DF(num_of_lanes, int_type)               \
  for (int i = 0; i < num_of_lanes / 2; ++i) {                   \
    Msa3RInstrHelper_shuffle<int_type>(opcode, &ws, &wt, &wd, i, \
                                       num_of_lanes);            \
  }
      switch (DecodeMsaDataFormat()) {
        case MSA_BYTE:
          INTERLEAVE_PACK_DF(kMSALanesByte, int8_t);
          break;
        case MSA_HALF:
          INTERLEAVE_PACK_DF(kMSALanesHalf, int16_t);
          break;
        case MSA_WORD:
          INTERLEAVE_PACK_DF(kMSALanesWord, int32_t);
          break;
        case MSA_DWORD:
          INTERLEAVE_PACK_DF(kMSALanesDword, int64_t);
          break;
        default:
          UNREACHABLE();
      }
      break;
#undef INTERLEAVE_PACK_DF
    default:
#define MSA_3R_DF(elem, num_of_lanes)                                          \
  for (int i = 0; i < num_of_lanes; i++) {                                     \
    wd.elem[i] = Msa3RInstrHelper(opcode, wd.elem[i], ws.elem[i], wt.elem[i]); \
  }

      switch (DecodeMsaDataFormat()) {
        case MSA_BYTE:
          MSA_3R_DF(b, kMSALanesByte);
          break;
        case MSA_HALF:
          MSA_3R_DF(h, kMSALanesHalf);
          break;
        case MSA_WORD:
          MSA_3R_DF(w, kMSALanesWord);
          break;
        case MSA_DWORD:
          MSA_3R_DF(d, kMSALanesDword);
          break;
        default:
          UNREACHABLE();
      }
#undef MSA_3R_DF
      break;
  }
  set_msa_register(wd_reg(), &wd);
  TraceMSARegWr(&wd);
}

template <typename T_int, typename T_fp, typename T_reg>
void Msa3RFInstrHelper(uint32_t opcode, T_reg ws, T_reg wt, T_reg* wd) {
  const T_int all_ones = static_cast<T_int>(-1);
  const T_fp s_element = *reinterpret_cast<T_fp*>(&ws);
  const T_fp t_element = *reinterpret_cast<T_fp*>(&wt);
  switch (opcode) {
    case FCUN: {
      if (std::isnan(s_element) || std::isnan(t_element)) {
        *wd = all_ones;
      } else {
        *wd = 0;
      }
    } break;
    case FCEQ: {
      if (s_element != t_element || std::isnan(s_element) ||
          std::isnan(t_element)) {
        *wd = 0;
      } else {
        *wd = all_ones;
      }
    } break;
    case FCUEQ: {
      if (s_element == t_element || std::isnan(s_element) ||
          std::isnan(t_element)) {
        *wd = all_ones;
      } else {
        *wd = 0;
      }
    } break;
    case FCLT: {
      if (s_element >= t_element || std::isnan(s_element) ||
          std::isnan(t_element)) {
        *wd = 0;
      } else {
        *wd = all_ones;
      }
    } break;
    case FCULT: {
      if (s_element < t_element || std::isnan(s_element) ||
          std::isnan(t_element)) {
        *wd = all_ones;
      } else {
        *wd = 0;
      }
    } break;
    case FCLE: {
      if (s_element > t_element || std::isnan(s_element) ||
          std::isnan(t_element)) {
        *wd = 0;
      } else {
        *wd = all_ones;
      }
    } break;
    case FCULE: {
      if (s_element <= t_element || std::isnan(s_element) ||
          std::isnan(t_element)) {
        *wd = all_ones;
      } else {
        *wd = 0;
      }
    } break;
    case FCOR: {
      if (std::isnan(s_element) || std::isnan(t_element)) {
        *wd = 0;
      } else {
        *wd = all_ones;
      }
    } break;
    case FCUNE: {
      if (s_element != t_element || std::isnan(s_element) ||
          std::isnan(t_element)) {
        *wd = all_ones;
      } else {
        *wd = 0;
      }
    } break;
    case FCNE: {
      if (s_element == t_element || std::isnan(s_element) ||
          std::isnan(t_element)) {
        *wd = 0;
      } else {
        *wd = all_ones;
      }
    } break;
    case FADD:
      *wd = base::bit_cast<T_int>(s_element + t_element);
      break;
    case FSUB:
      *wd = base::bit_cast<T_int>(s_element - t_element);
      break;
    case FMUL:
      *wd = base::bit_cast<T_int>(s_element * t_element);
      break;
    case FDIV: {
      if (t_element == 0) {
        *wd = base::bit_cast<T_int>(std::numeric_limits<T_fp>::quiet_NaN());
      } else {
        *wd = base::bit_cast<T_int>(s_element / t_element);
      }
    } break;
    case FMADD:
      *wd = base::bit_cast<T_int>(
          std::fma(s_element, t_element, *reinterpret_cast<T_fp*>(wd)));
      break;
    case FMSUB:
      *wd = base::bit_cast<T_int>(
          std::fma(-s_element, t_element, *reinterpret_cast<T_fp*>(wd)));
      break;
    case FEXP2:
      *wd = base::bit_cast<T_int>(std::ldexp(s_element, static_cast<int>(wt)));
      break;
    case FMIN:
      *wd = base::bit_cast<T_int>(std::min(s_element, t_element));
      break;
    case FMAX:
      *wd = base::bit_cast<T_int>(std::max(s_element, t_element));
      break;
    case FMIN_A: {
      *wd = base::bit_cast<T_int>(
          std::fabs(s_element) < std::fabs(t_element) ? s_element : t_element);
    } break;
    case FMAX_A: {
      *wd = base::bit_cast<T_int>(
          std::fabs(s_element) > std::fabs(t_element) ? s_element : t_element);
    } break;
    case FSOR:
    case FSUNE:
    case FSNE:
    case FSAF:
    case FSUN:
    case FSEQ:
    case FSUEQ:
    case FSLT:
    case FSULT:
    case FSLE:
    case FSULE:
      UNIMPLEMENTED();
      break;
    default:
      UNREACHABLE();
  }
}

template <typename T_int, typename T_int_dbl, typename T_reg>
void Msa3RFInstrHelper2(uint32_t opcode, T_reg ws, T_reg wt, T_reg* wd) {
  //  using T_uint = typename std::make_unsigned<T_int>::type;
  using T_uint_dbl = typename std::make_unsigned<T_int_dbl>::type;
  const T_int max_int = std::numeric_limits<T_int>::max();
  const T_int min_int = std::numeric_limits<T_int>::min();
  const int shift = kBitsPerByte * sizeof(T_int) - 1;
  const T_int_dbl reg_s = ws;
  const T_int_dbl reg_t = wt;
  T_int_dbl product, result;
  product = reg_s * reg_t;
  switch (opcode) {
    case MUL_Q: {
      const T_int_dbl min_fix_dbl =
          base::bit_cast<T_uint_dbl>(std::numeric_limits<T_int_dbl>::min()) >>
          1U;
      const T_int_dbl max_fix_dbl = std::numeric_limits<T_int_dbl>::max() >> 1U;
      if (product == min_fix_dbl) {
        product = max_fix_dbl;
      }
      *wd = static_cast<T_int>(product >> shift);
    } break;
    case MADD_Q: {
      result = (product + (static_cast<T_int_dbl>(*wd) << shift)) >> shift;
      *wd = static_cast<T_int>(
          result > max_int ? max_int : result < min_int ? min_int : result);
    } break;
    case MSUB_Q: {
      result = (-product + (static_cast<T_int_dbl>(*wd) << shift)) >> shift;
      *wd = static_cast<T_int>(
          result > max_int ? max_int : result < min_int ? min_int : result);
    } break;
    case MULR_Q: {
      const T_int_dbl min_fix_dbl =
          base::bit_cast<T_uint_dbl>(std::numeric_limits<T_int_dbl>::min()) >>
          1U;
      const T_int_dbl max_fix_dbl = std::numeric_limits<T_int_dbl>::max() >> 1U;
      if (product == min_fix_dbl) {
        *wd = static_cast<T_int>(max_fix_dbl >> shift);
        break;
      }
      *wd = static_cast<T_int>((product + (1 << (shift - 1))) >> shift);
    } break;
    case MADDR_Q: {
      result = (product + (static_cast<T_int_dbl>(*wd) << shift) +
                (1 << (shift - 1))) >>
               shift;
      *wd = static_cast<T_int>(
          result > max_int ? max_int : result < min_int ? min_int : result);
    } break;
    case MSUBR_Q: {
      result = (-product + (static_cast<T_int_dbl>(*wd) << shift) +
                (1 << (shift - 1))) >>
               shift;
      *wd = static_cast<T_int>(
          result > max_int ? max_int : result < min_int ? min_int : result);
    } break;
    default:
      UNREACHABLE();
  }
}

void Simulator::DecodeTypeMsa3RF() {
  DCHECK_EQ(kArchVariant, kMips64r6);
  DCHECK(CpuFeatures::IsSupported(MIPS_SIMD));
  uint32_t opcode = instr_.InstructionBits() & kMsa3RFMask;
  msa_reg_t wd, ws, wt;
  if (opcode != FCAF) {
    get_msa_register(ws_reg(), &ws);
    get_msa_register(wt_reg(), &wt);
  }
  switch (opcode) {
    case FCAF:
      wd.d[0] = 0;
      wd.d[1] = 0;
      break;
    case FEXDO:
#define PACK_FLOAT16(sign, exp, frac) \
  static_cast<uint16_t>(((sign) << 15) + ((exp) << 10) + (frac))
#define FEXDO_DF(source, dst)                                        \
  do {                                                               \
    element = source;                                                \
    aSign = element >> 31;                                           \
    aExp = element >> 23 & 0xFF;                                     \
    aFrac = element & 0x007FFFFF;                                    \
    if (aExp == 0xFF) {                                              \
      if (aFrac) {                                                   \
        /* Input is a NaN */                                         \
        dst = 0x7DFFU;                                               \
        break;                                                       \
      }                                                              \
      /* Infinity */                                                 \
      dst = PACK_FLOAT16(aSign, 0x1F, 0);                            \
      break;                                                         \
    } else if (aExp == 0 && aFrac == 0) {                            \
      dst = PACK_FLOAT16(aSign, 0, 0);                               \
      break;                                                         \
    } else {                                                         \
      int maxexp = 29;                                               \
      uint32_t mask;                                                 \
      uint32_t increment;                                            \
      bool rounding_bumps_exp;                                       \
      aFrac |= 0x00800000;                                           \
      aExp -= 0x71;                                                  \
      if (aExp < 1) {                                                \
        /* Will be denormal in halfprec */                           \
        mask = 0x00FFFFFF;                                           \
        if (aExp >= -11) {                                           \
          mask >>= 11 + aExp;                                        \
        }                                                            \
      } else {                                                       \
        /* Normal number in halfprec */                              \
        mask = 0x00001FFF;                                           \
      }                                                              \
      switch (MSACSR_ & 3) {                                         \
        case kRoundToNearest:                                        \
          increment = (mask + 1) >> 1;                               \
          if ((aFrac & mask) == increment) {                         \
            increment = aFrac & (increment << 1);                    \
          }                                                          \
          break;                                                     \
        case kRoundToPlusInf:                                        \
          increment = aSign ? 0 : mask;                              \
          break;                                                     \
        case kRoundToMinusInf:                                       \
          increment = aSign ? mask : 0;                              \
          break;                                                     \
        case kRoundToZero:                                           \
          increment = 0;                                             \
          break;                                                     \
      }                                                              \
      rounding_bumps_exp = (aFrac + increment >= 0x01000000);        \
      if (aExp > maxexp || (aExp == maxexp && rounding_bumps_exp)) { \
        dst = PACK_FLOAT16(aSign, 0x1F, 0);                          \
        break;                                                       \
      }                                                              \
      aFrac += increment;                                            \
      if (rounding_bumps_exp) {                                      \
        aFrac >>= 1;                                                 \
        aExp++;                                                      \
      }                                                              \
      if (aExp < -10) {                                              \
        dst = PACK_FLOAT16(aSign, 0, 0);                             \
        break;                                                       \
      }                                                              \
      if (aExp < 0) {                                                \
        aFrac >>= -aExp;                                             \
        aExp = 0;                                                    \
      }                                                              \
      dst = PACK_FLOAT16(aSign, aExp, aFrac >> 13);                  \
    }                                                                \
  } while (0);
      switch (DecodeMsaDataFormat()) {
        case MSA_HALF:
          for (int i = 0; i < kMSALanesWord; i++) {
            uint_fast32_t element;
            uint_fast32_t aSign, aFrac;
            int_fast32_t aExp;
            FEXDO_DF(ws.uw[i], wd.uh[i + kMSALanesHalf / 2])
            FEXDO_DF(wt.uw[i], wd.uh[i])
          }
          break;
        case MSA_WORD:
          for (int i = 0; i < kMSALanesDword; i++) {
            wd.w[i + kMSALanesWord / 2] = base::bit_cast<int32_t>(
                static_cast<float>(base::bit_cast<double>(ws.d[i])));
            wd.w[i] = base::bit_cast<int32_t>(
                static_cast<float>(base::bit_cast<double>(wt.d[i])));
          }
          break;
        default:
          UNREACHABLE();
      }
      break;
#undef PACK_FLOAT16
#undef FEXDO_DF
    case FTQ:
#define FTQ_DF(source, dst, fp_type, int_type)                  \
  element = base::bit_cast<fp_type>(source) *                   \
            (1U << (sizeof(int_type) * kBitsPerByte - 1));      \
  if (element > std::numeric_limits<int_type>::max()) {         \
    dst = std::numeric_limits<int_type>::max();                 \
  } else if (element < std::numeric_limits<int_type>::min()) {  \
    dst = std::numeric_limits<int_type>::min();                 \
  } else if (std::isnan(element)) {                             \
    dst = 0;                                                    \
  } else {                                                      \
    int_type fixed_point;                                       \
    round_according_to_msacsr(element, &element, &fixed_point); \
    dst = fixed_point;                                          \
  }

      switch (DecodeMsaDataFormat()) {
        case MSA_HALF:
          for (int i = 0; i < kMSALanesWord; i++) {
            float element;
            FTQ_DF(ws.w[i], wd.h[i + kMSALanesHalf / 2], float, int16_t)
            FTQ_DF(wt.w[i], wd.h[i], float, int16_t)
          }
          break;
        case MSA_WORD:
          double element;
          for (int i = 0; i < kMSALanesDword; i++) {
            FTQ_DF(ws.d[i], wd.w[i + kMSALanesWord / 2], double, int32_t)
            FTQ_DF(wt.d[i], wd.w[i], double, int32_t)
          }
          break;
        default:
          UNREACHABLE();
      }
      break;
#undef FTQ_DF
#define MSA_3RF_DF(T1, T2, Lanes, ws, wt, wd)         \
  for (int i = 0; i < Lanes; i++) {                   \
    Msa3RFInstrHelper<T1, T2>(opcode, ws, wt, &(wd)); \
  }
#define MSA_3RF_DF2(T1, T2, Lanes, ws, wt, wd)         \
  for (int i = 0; i < Lanes; i++) {                    \
    Msa3RFInstrHelper2<T1, T2>(opcode, ws, wt, &(wd)); \
  }
    case MADD_Q:
    case MSUB_Q:
    case MADDR_Q:
    case MSUBR_Q:
      get_msa_register(wd_reg(), &wd);
      [[fallthrough]];
    case MUL_Q:
    case MULR_Q:
      switch (DecodeMsaDataFormat()) {
        case MSA_HALF:
          MSA_3RF_DF2(int16_t, int32_t, kMSALanesHalf, ws.h[i], wt.h[i],
                      wd.h[i])
          break;
        case MSA_WORD:
          MSA_3RF_DF2(int32_t, int64_t, kMSALanesWord, ws.w[i], wt.w[i],
                      wd.w[i])
          break;
        default:
          UNREACHABLE();
      }
      break;
    default:
      if (opcode == FMADD || opcode == FMSUB) {
        get_msa_register(wd_reg(), &wd);
      }
      switch (DecodeMsaDataFormat()) {
        case MSA_WORD:
          MSA_3RF_DF(int32_t, float, kMSALanesWord, ws.w[i], wt.w[i], wd.w[i])
          break;
        case MSA_DWORD:
          MSA_3RF_DF(int64_t, double, kMSALanesDword, ws.d[i], wt.d[i], wd.d[i])
          break;
        default:
          UNREACHABLE();
      }
      break;
#undef MSA_3RF_DF
#undef MSA_3RF_DF2
  }
  set_msa_register(wd_reg(), &wd);
  TraceMSARegWr(&wd);
}

void Simulator::DecodeTypeMsaVec() {
  DCHECK_EQ(kArchVariant, kMips64r6);
  DCHECK(CpuFeatures::IsSupported(MIPS_SIMD));
  uint32_t opcode = instr_.InstructionBits() & kMsaVECMask;
  msa_reg_t wd, ws, wt;

  get_msa_register(instr_.WsValue(), ws.d);
  get_msa_register(instr_.WtValue(), wt.d);
  if (opcode == BMNZ_V || opcode == BMZ_V || opcode == BSEL_V) {
    get_msa_register(instr_.WdValue(), wd.d);
  }

  for (int i = 0; i < kMSALanesDword; i++) {
    switch (opcode) {
      case AND_V:
        wd.d[i] = ws.d[i] & wt.d[i];
        break;
      case OR_V:
        wd.d[i] = ws.d[i] | wt.d[i];
        break;
      case NOR_V:
        wd.d[i] = ~(ws.d[i] | wt.d[i]);
        break;
      case XOR_V:
        wd.d[i] = ws.d[i] ^ wt.d[i];
        break;
      case BMNZ_V:
        wd.d[i] = (wt.d[i] & ws.d[i]) | (~wt.d[i] & wd.d[i]);
        break;
      case BMZ_V:
        wd.d[i] = (~wt.d[i] & ws.d[i]) | (wt.d[i] & wd.d[i]);
        break;
      case BSEL_V:
        wd.d[i] = (~wd.d[i] & ws.d[i]) | (wd.d[i] & wt.d[i]);
        break;
      default:
        UNREACHABLE();
    }
  }
  set_msa_register(instr_.WdValue(), wd.d);
  TraceMSARegWr(wd.d);
}

void Simulator::DecodeTypeMsa2R() {
  DCHECK_EQ(kArchVariant, kMips64r6);
  DCHECK(CpuFeatures::IsSupported(MIPS_SIMD));
  uint32_t opcode = instr_.InstructionBits() & kMsa2RMask;
  msa_reg_t wd, ws;
  switch (opcode) {
    case FILL:
      switch (DecodeMsaDataFormat()) {
        case MSA_BYTE: {
          int64_t rs = get_register(instr_.WsValue());
          for (int i = 0; i < kMSALanesByte; i++) {
            wd.b[i] = rs & 0xFFu;
          }
          set_msa_register(instr_.WdValue(), wd.b);
          TraceMSARegWr(wd.b);
          break;
        }
        case MSA_HALF: {
          int64_t rs = get_register(instr_.WsValue());
          for (int i = 0; i < kMSALanesHalf; i++) {
            wd.h[i] = rs & 0xFFFFu;
          }
          set_msa_register(instr_.WdValue(), wd.h);
          TraceMSARegWr(wd.h);
          break;
        }
        case MSA_WORD: {
          int64_t rs = get_register(instr_.WsValue());
          for (int i = 0; i < kMSALanesWord; i++) {
            wd.w[i] = rs & 0xFFFFFFFFu;
          }
          set_msa_register(instr_.WdValue(), wd.w);
          TraceMSARegWr(wd.w);
          break;
        }
        case MSA_DWORD: {
          int64_t rs = get_register(instr_.WsValue());
          wd.d[0] = wd.d[1] = rs;
          set_msa_register(instr_.WdValue(), wd.d);
          TraceMSARegWr(wd.d);
          break;
        }
        default:
          UNREACHABLE();
      }
      break;
    case PCNT:
#define PCNT_DF(elem, num_of_lanes)                       \
  get_msa_register(instr_.WsValue(), ws.elem);            \
  for (int i = 0; i < num_of_lanes; i++) {                \
    uint64_t u64elem = static_cast<uint64_t>(ws.elem[i]); \
    wd.elem[i] = base::bits::CountPopulation(u64elem);    \
  }                                                       \
  set_msa_register(instr_.WdValue(), wd.elem);            \
  TraceMSARegWr(wd.elem)

      switch (DecodeMsaDataFormat()) {
        case MSA_BYTE:
          PCNT_DF(ub, kMSALanesByte);
          break;
        case MSA_HALF:
          PCNT_DF(uh, kMSALanesHalf);
          break;
        case MSA_WORD:
          PCNT_DF(uw, kMSALanesWord);
          break;
        case MSA_DWORD:
          PCNT_DF(ud, kMSALanesDword);
          break;
        default:
          UNREACHABLE();
      }
#undef PCNT_DF
      break;
    case NLOC:
#define NLOC_DF(elem, num_of_lanes)                                         \
  get_msa_register(instr_.WsValue(), ws.elem);                              \
  for (int i = 0; i < num_of_lanes; i++) {                                  \
    const uint64_t mask = (num_of_lanes == kMSALanesDword)                  \
                              ? UINT64_MAX                                  \
                              : (1ULL << (kMSARegSize / num_of_lanes)) - 1; \
    uint64_t u64elem = static_cast<uint64_t>(~ws.elem[i]) & mask;           \
    wd.elem[i] = base::bits::CountLeadingZeros64(u64elem) -                 \
                 (64 - kMSARegSize / num_of_lanes);                         \
  }                                                                         \
  set_msa_register(instr_.WdValue(), wd.elem);                              \
  TraceMSARegWr(wd.elem)

      switch (DecodeMsaDataFormat()) {
        case MSA_BYTE:
          NLOC_DF(ub, kMSALanesByte);
          break;
        case MSA_HALF:
          NLOC_DF(uh, kMSALanesHalf);
          break;
        case MSA_WORD:
          NLOC_DF(uw, kMSALanesWord);
          break;
        case MSA_DWORD:
          NLOC_DF(ud, kMSALanesDword);
          break;
        default:
          UNREACHABLE();
      }
#undef NLOC_DF
      break;
    case NLZC:
#define NLZC_DF(elem, num_of_lanes)                         \
  get_msa_register(instr_.WsValue(), ws.elem);              \
  for (int i = 0; i < num_of_lanes; i++) {                  \
    uint64_t u64elem = static_cast<uint64_t>(ws.elem[i]);   \
    wd.elem[i] = base::bits::CountLeadingZeros64(u64elem) - \
                 (64 - kMSARegSize / num_of_lanes);         \
  }                                                         \
  set_msa_register(instr_.WdValue(), wd.elem);              \
  TraceMSARegWr(wd.elem)

      switch (DecodeMsaDataFormat()) {
        case MSA_BYTE:
          NLZC_DF(ub, kMSALanesByte);
          break;
        case MSA_HALF:
          NLZC_DF(uh, kMSALanesHalf);
          break;
        case MSA_WORD:
          NLZC_DF(uw, kMSALanesWord);
          break;
        case MSA_DWORD:
          NLZC_DF(ud, kMSALanesDword);
          break;
        default:
          UNREACHABLE();
      }
#undef NLZC_DF
      break;
    default:
      UNREACHABLE();
  }
}

#define BIT(n) (0x1LL << n)
#define QUIET_BIT_S(nan) (base::bit_cast<int32_t>(nan) & BIT(22))
#define QUIET_BIT_D(nan) (base::bit_cast<int64_t>(nan) & BIT(51))
static inline bool isSnan(float fp) { return !QUIET_BIT_S(fp); }
static inline bool isSnan(double fp) { return !QUIET_BIT_D(fp); }
#undef QUIET_BIT_S
#undef QUIET_BIT_D

template <typename T_int, typename T_fp, typename T_src, typename T_dst>
T_int Msa2RFInstrHelper(uint32_t opcode, T_src src, T_dst* dst,
                        Simulator* sim) {
  using T_uint = typename std::make_unsigned<T_int>::type;
  switch (opcode) {
    case FCLASS: {
#define SNAN_BIT BIT(0)
#define QNAN_BIT BIT(1)
#define NEG_INFINITY_BIT BIT(2)
#define NEG_NORMAL_BIT BIT(3)
#define NEG_SUBNORMAL_BIT BIT(4)
#define NEG_ZERO_BIT BIT(5)
#define POS_INFINITY_BIT BIT(6)
#define POS_NORMAL_BIT BIT(7)
#define POS_SUBNORMAL_BIT BIT(8)
#define POS_ZERO_BIT BIT(9)
      T_fp element = *reinterpret_cast<T_fp*>(&src);
      switch (std::fpclassify(element)) {
        case FP_INFINITE:
          if (std::signbit(element)) {
            *dst = NEG_INFINITY_BIT;
          } else {
            *dst = POS_INFINITY_BIT;
          }
          break;
        case FP_NAN:
          if (isSnan(element)) {
            *dst = SNAN_BIT;
          } else {
            *dst = QNAN_BIT;
          }
          break;
        case FP_NORMAL:
          if (std::signbit(element)) {
            *dst = NEG_NORMAL_BIT;
          } else {
            *dst = POS_NORMAL_BIT;
          }
          break;
        case FP_SUBNORMAL:
          if (std::signbit(element)) {
            *dst = NEG_SUBNORMAL_BIT;
          } else {
            *dst = POS_SUBNORMAL_BIT;
          }
          break;
        case FP_ZERO:
          if (std::signbit(element)) {
            *dst = NEG_ZERO_BIT;
          } else {
            *dst = POS_ZERO_BIT;
          }
          break;
        default:
          UNREACHABLE();
      }
      break;
    }
#undef BIT
#undef SNAN_BIT
#undef QNAN_BIT
#undef NEG_INFINITY_BIT
#undef NEG_NORMAL_BIT
#undef NEG_SUBNORMAL_BIT
#undef NEG_ZERO_BIT
#undef POS_INFINITY_BIT
#undef POS_NORMAL_BIT
#undef POS_SUBNORMAL_BIT
#undef POS_ZERO_BIT
    case FTRUNC_S: {
      T_fp element = base::bit_cast<T_fp>(src);
      const T_int max_int = std::numeric_limits<T_int>::max();
      const T_int min_int = std::numeric_limits<T_int>::min();
      if (std::isnan(element)) {
        *dst = 0;
      } else if (element >= static_cast<T_fp>(max_int) || element <= min_int) {
        *dst = element >= static_cast<T_fp>(max_int) ? max_int : min_int;
      } else {
        *dst = static_cast<T_int>(std::trunc(element));
      }
      break;
    }
    case FTRUNC_U: {
      T_fp element = base::bit_cast<T_fp>(src);
      const T_uint max_int = std::numeric_limits<T_uint>::max();
      if (std::isnan(element)) {
        *dst = 0;
      } else if (element >= static_cast<T_fp>(max_int) || element <= 0) {
        *dst = element >= static_cast<T_fp>(max_int) ? max_int : 0;
      } else {
        *dst = static_cast<T_uint>(std::trunc(element));
      }
      break;
    }
    case FSQRT: {
      T_fp element = base::bit_cast<T_fp>(src);
      if (element < 0 || std::isnan(element)) {
        *dst = base::bit_cast<T_int>(std::numeric_limits<T_fp>::quiet_NaN());
      } else {
        *dst = base::bit_cast<T_int>(std::sqrt(element));
      }
      break;
    }
    case FRSQRT: {
      T_fp element = base::bit_cast<T_fp>(src);
      if (element < 0 || std::isnan(element)) {
        *dst = base::bit_cast<T_int>(std::numeric_limits<T_fp>::quiet_NaN());
      } else {
        *dst = base::bit_cast<T_int>(1 / std::sqrt(element));
      }
      break;
    }
    case FRCP: {
      T_fp element = base::bit_cast<T_fp>(src);
      if (std::isnan(element)) {
        *dst = base::bit_cast<T_int>(std::numeric_limits<T_fp>::quiet_NaN());
      } else {
        *dst = base::bit_cast<T_int>(1 / element);
      }
      break;
    }
    case FRINT: {
      T_fp element = base::bit_cast<T_fp>(src);
      if (std::isnan(element)) {
        *dst = base::bit_cast<T_int>(std::numeric_limits<T_fp>::quiet_NaN());
      } else {
        T_int dummy;
        sim->round_according_to_msacsr<T_fp, T_int>(element, &element, &dummy);
        *dst = base::bit_cast<T_int>(element);
      }
      break;
    }
    case FLOG2: {
      T_fp element = base::bit_cast<T_fp>(src);
      switch (std::fpclassify(element)) {
        case FP_NORMAL:
        case FP_SUBNORMAL:
          *dst = base::bit_cast<T_int>(std::logb(element));
          break;
        case FP_ZERO:
          *dst = base::bit_cast<T_int>(-std::numeric_limits<T_fp>::infinity());
          break;
        case FP_NAN:
          *dst = base::bit_cast<T_int>(std::numeric_limits<T_fp>::quiet_NaN());
          break;
        case FP_INFINITE:
          if (element < 0) {
            *dst =
                base::bit_cast<T_int>(std::numeric_limits<T_fp>::quiet_NaN());
          } else {
            *dst = base::bit_cast<T_int>(std::numeric_limits<T_fp>::infinity());
          }
          break;
        default:
          UNREACHABLE();
      }
      break;
    }
    case FTINT_S: {
      T_fp element = base::bit_cast<T_fp>(src);
      const T_int max_int = std::numeric_limits<T_int>::max();
      const T_int min_int = std::numeric_limits<T_int>::min();
      if (std::isnan(element)) {
        *dst = 0;
      } else if (element < min_int || element > static_cast<T_fp>(max_int)) {
        *dst = element > static_cast<T_fp>(max_int) ? max_int : min_int;
      } else {
        sim->round_according_to_msacsr<T_fp, T_int>(element, &element, dst);
      }
      break;
    }
    case FTINT_U: {
      T_fp element = base::bit_cast<T_fp>(src);
      const T_uint max_uint = std::numeric_limits<T_uint>::max();
      if (std::isnan(element)) {
        *dst = 0;
      } else if (element < 0 || element > static_cast<T_fp>(max_uint)) {
        *dst = element > static_cast<T_fp>(max_uint) ? max_uint : 0;
      } else {
        T_uint res;
        sim->round_according_to_msacsr<T_fp, T_uint>(element, &element, &res);
        *dst = *reinterpret_cast<T_int*>(&res);
      }
      break;
    }
    case FFINT_S:
      *dst = base::bit_cast<T_int>(static_cast<T_fp>(src));
      break;
    case FFINT_U:
      using uT_src = typename std::make_unsigned<T_src>::type;
      *dst =
          base::bit_cast<T_int>(static_cast<T_fp>(base::bit_cast<uT_src>(src)));
      break;
    default:
      UNREACHABLE();
  }
  return 0;
}

template <typename T_int, typename T_fp, typename T_reg>
T_int Msa2RFInstrHelper2(uint32_t opcode, T_reg ws, int i) {
  switch (opcode) {
#define EXTRACT_FLOAT16_SIGN(fp16) (fp16 >> 15)
#define EXTRACT_FLOAT16_EXP(fp16) (fp16 >> 10 & 0x1F)
#define EXTRACT_FLOAT16_FRAC(fp16) (fp16 & 0x3FF)
#define PACK_FLOAT32(sign, exp, frac) \
  static_cast<uint32_t>(((sign) << 31) + ((exp) << 23) + (frac))
#define FEXUP_DF(src_index)                                                    \
  uint_fast16_t element = ws.uh[src_index];                                    \
  uint_fast32_t aSign, aFrac;                                                  \
  int_fast32_t aExp;                                                           \
  aSign = EXTRACT_FLOAT16_SIGN(element);                                       \
  aExp = EXTRACT_FLOAT16_EXP(element);                                         \
  aFrac = EXTRACT_FLOAT16_FRAC(element);                                       \
  if (V8_LIKELY(aExp && aExp != 0x1F)) {                                       \
    return PACK_FLOAT32(aSign, aExp + 0x70, aFrac << 13);                      \
  } else if (aExp == 0x1F) {                                                   \
    if (aFrac) {                                                               \
      return base::bit_cast<int32_t>(std::numeric_limits<float>::quiet_NaN()); \
    } else {                                                                   \
      return base::bit_cast<uint32_t>(                                         \
                 std::numeric_limits<float>::infinity()) |                     \
             static_cast<uint32_t>(aSign) << 31;                               \
    }                                                                          \
  } else {                                                                     \
    if (aFrac == 0) {                                                          \
      return PACK_FLOAT32(aSign, 0, 0);                                        \
    } else {                                                                   \
      int_fast16_t shiftCount =                                                \
          base::bits::CountLeadingZeros32(static_cast<uint32_t>(aFrac)) - 21;  \
      aFrac <<= shiftCount;                                                    \
      aExp = -shiftCount;                                                      \
      return PACK_FLOAT32(aSign, aExp + 0x70, aFrac << 13);                    \
    }                                                                          \
  }
    case FEXUPL:
      if (std::is_same<int32_t, T_int>::value) {
        FEXUP_DF(i + kMSALanesWord)
      } else {
        return base::bit_cast<int64_t>(static_cast<double>(
            base::bit_cast<float>(ws.w[i + kMSALanesDword])));
      }
    case FEXUPR:
      if (std::is_same<int32_t, T_int>::value) {
        FEXUP_DF(i)
      } else {
        return base::bit_cast<int64_t>(
            static_cast<double>(base::bit_cast<float>(ws.w[i])));
      }
    case FFQL: {
      if (std::is_same<int32_t, T_int>::value) {
        return base::bit_cast<int32_t>(
            static_cast<float>(ws.h[i + kMSALanesWord]) / (1U << 15));
      } else {
        return base::bit_cast<int64_t>(
            static_cast<double>(ws.w[i + kMSALanesDword]) / (1U << 31));
      }
      break;
    }
    case FFQR: {
      if (std::is_same<int32_t, T_int>::value) {
        return base::bit_cast<int32_t>(static_cast<float>(ws.h[i]) /
                                       (1U << 15));
      } else {
        return base::bit_cast<int64_t>(static_cast<double>(ws.w[i]) /
                                       (1U << 31));
      }
      break;
      default:
        UNREACHABLE();
    }
  }
#undef EXTRACT_FLOAT16_SIGN
#undef EXTRACT_FLOAT16_EXP
#undef EXTRACT_FLOAT16_FRAC
#undef PACK_FLOAT32
#undef FEXUP_DF
}

void Simulator::DecodeTypeMsa2RF() {
  DCHECK_EQ(kArchVariant, kMips64r6);
  DCHECK(CpuFeatures::IsSupported(MIPS_SIMD));
  uint32_t opcode = instr_.InstructionBits() & kMsa2RFMask;
  msa_reg_t wd, ws;
  get_msa_register(ws_reg(), &ws);
  if (opcode == FEXUPL || opcode == FEXUPR || opcode == FFQL ||
      opcode == FFQR) {
    switch (DecodeMsaDataFormat()) {
      case MSA_WORD:
        for (int i = 0; i < kMSALanesWord; i++) {
          wd.w[i] = Msa2RFInstrHelper2<int32_t, float>(opcode, ws, i);
        }
        break;
      case MSA_DWORD:
        for (int i = 0; i < kMSALanesDword; i++) {
          wd.d[i] = Msa2RFInstrHelper2<int64_t, double>(opcode, ws, i);
        }
        break;
      default:
        UNREACHABLE();
    }
  } else {
    switch (DecodeMsaDataFormat()) {
      case MSA_WORD:
        for (int i = 0; i < kMSALanesWord; i++) {
          Msa2RFInstrHelper<int32_t, float>(opcode, ws.w[i], &wd.w[i], this);
        }
        break;
      case MSA_DWORD:
        for (int i = 0; i < kMSALanesDword; i++) {
          Msa2RFInstrHelper<int64_t, double>(opcode, ws.d[i], &wd.d[i], this);
        }
        break;
      default:
        UNREACHABLE();
    }
  }
  set_msa_register(wd_reg(), &wd);
  TraceMSARegWr(&wd);
}

void Simulator::DecodeTypeRegister() {
  // ---------- Execution.
  switch (instr_.OpcodeFieldRaw()) {
    case COP1:
      DecodeTypeRegisterCOP1();
      break;
    case COP1X:
      DecodeTypeRegisterCOP1X();
      break;
    case SPECIAL:
      DecodeTypeRegisterSPECIAL();
      break;
    case SPECIAL2:
      DecodeTypeRegisterSPECIAL2();
      break;
    case SPECIAL3:
      DecodeTypeRegisterSPECIAL3();
      break;
    case MSA:
      switch (instr_.MSAMinorOpcodeField()) {
        case kMsaMinor3R:
          DecodeTypeMsa3R();
          break;
        case kMsaMinor3RF:
          DecodeTypeMsa3RF();
          break;
        case kMsaMinorVEC:
          DecodeTypeMsaVec();
          break;
        case kMsaMinor2R:
          DecodeTypeMsa2R();
          break;
        case kMsaMinor2RF:
          DecodeTypeMsa2RF();
          break;
        case kMsaMinorELM:
          DecodeTypeMsaELM();
          break;
        default:
          UNREACHABLE();
      }
      break;
    // Unimplemented opcodes raised an error in the configuration step before,
    // so we can use the default here to set the destination register in common
    // cases.
    default:
      UNREACHABLE();
  }
}

// Type 2: instructions using a 16, 21 or 26 bits immediate. (e.g. beq, beqc).
void Simulator::DecodeTypeImmediate() {
  // Instruction fields.
  Opcode op = instr_.OpcodeFieldRaw();
  int32_t rs_reg = instr_.RsValue();
  int64_t rs = get_register(instr_.RsValue());
  uint64_t rs_u = static_cast<uint64_t>(rs);
  int32_t rt_reg = instr_.RtValue();  // Destination register.
  int64_t rt = get_register(rt_reg);
  int16_t imm16 = instr_.Imm16Value();
  int32_t imm18 = instr_.Imm18Value();

  int32_t ft_reg = instr_.FtValue();  // Destination register.

  // Zero extended immediate.
  uint64_t oe_imm16 = 0xFFFF & imm16;
  // Sign extended immediate.
  int64_t se_imm16 = imm16;
  int64_t se_imm18 = imm18 | ((imm18 & 0x20000) ? 0xFFFFFFFFFFFC0000 : 0);

  // Next pc.
  int64_t next_pc = bad_ra;

  // Used for conditional branch instructions.
  bool execute_branch_delay_instruction = false;

  // Used for arithmetic instructions.
  int64_t alu_out = 0;

  // Used for memory instructions.
  int64_t addr = 0x0;
  // Alignment for 32-bit integers used in LWL, LWR, etc.
  const int kInt32AlignmentMask = sizeof(uint32_t) - 1;
  // Alignment for 64-bit integers used in LDL, LDR, etc.
  const int kInt64AlignmentMask = sizeof(uint64_t) - 1;

  // Branch instructions common part.
  auto BranchAndLinkHelper =
      [this, &next_pc, &execute_branch_delay_instruction](bool do_branch) {
        execute_branch_delay_instruction = true;
        int64_t current_pc = get_pc();
        set_register(31, current_pc + 2 * kInstrSize);
        if (do_branch) {
          int16_t imm16 = instr_.Imm16Value();
          next_pc = current_pc + (imm16 << 2) + kInstrSize;
        } else {
          next_pc = current_pc + 2 * kInstrSize;
        }
      };

  auto BranchHelper = [this, &next_pc,
                       &execute_branch_delay_instruction](bool do_branch) {
    execute_branch_delay_instruction = true;
    int64_t current_pc = get_pc();
    if (do_branch) {
      int16_t imm16 = instr_.Imm16Value();
      next_pc = current_pc + (imm16 << 2) + kInstrSize;
    } else {
      next_pc = current_pc + 2 * kInstrSize;
    }
  };

  auto BranchHelper_MSA = [this, &next_pc, imm16,
                           &execute_branch_delay_instruction](bool do_branch) {
    execute_branch_delay_instruction = true;
    int64_t current_pc = get_pc();
    const int32_t bitsIn16Int = sizeof(int16_t) * kBitsPerByte;
    if (do_branch) {
      if (v8_flags.debug_code) {
        int16_t bits = imm16 & 0xFC;
        if (imm16 >= 0) {
          CHECK_EQ(bits, 0);
        } else {
          CHECK_EQ(bits ^ 0xFC, 0);
        }
      }
      // jump range :[pc + kInstrSize - 512 * kInstrSize,
      //              pc + kInstrSize + 511 * kInstrSize]
      int16_t offset = static_cast<int16_t>(imm16 << (bitsIn16Int - 10)) >>
                       (bitsIn16Int - 12);
      next_pc = current_pc + offset + kInstrSize;
    } else {
      next_pc = current_pc + 2 * kInstrSize;
    }
  };

  auto BranchAndLinkCompactHelper = [this, &next_pc](bool do_branch, int bits) {
    int64_t current_pc = get_pc();
    CheckForbiddenSlot(current_pc);
    if (do_branch) {
      int32_t imm = instr_.ImmValue(bits);
      imm <<= 32 - bits;
      imm >>= 32 - bits;
      next_pc = current_pc + (imm << 2) + kInstrSize;
      set_register(31, current_pc + kInstrSize);
    }
  };

  auto BranchCompactHelper = [this, &next_pc](bool do_branch, int bits) {
    int64_t current_pc = get_pc();
    CheckForbiddenSlot(current_pc);
    if (do_branch) {
      int32_t imm = instr_.ImmValue(bits);
      imm <<= 32 - bits;
      imm >>= 32 - bits;
      next_pc = get_pc() + (imm << 2) + kInstrSize;
    }
  };

  switch (op) {
    // ------------- COP1. Coprocessor instructions.
    case COP1:
      switch (instr_.RsFieldRaw()) {
        case BC1: {  // Branch on coprocessor condition.
          uint32_t cc = instr_.FBccValue();
          uint32_t fcsr_cc = get_fcsr_condition_bit(cc);
          uint32_t cc_value = test_fcsr_bit(fcsr_cc);
          bool do_branch = (instr_.FBtrueValue()) ? cc_value : !cc_value;
          BranchHelper(do_branch);
          break;
        }
        case BC1EQZ:
          BranchHelper(!(get_fpu_register(ft_reg) & 0x1));
          break;
        case BC1NEZ:
          BranchHelper(get_fpu_register(ft_reg) & 0x1);
          break;
        case BZ_V: {
          msa_reg_t wt;
          get_msa_register(wt_reg(), &wt);
          BranchHelper_MSA(wt.d[0] == 0 && wt.d[1] == 0);
        } break;
#define BZ_DF(witdh, lanes)          \
  {                                  \
    msa_reg_t wt;                    \
    get_msa_register(wt_reg(), &wt); \
    int i;                           \
    for (i = 0; i < lanes; ++i) {    \
      if (wt.witdh[i] == 0) {        \
        break;                       \
      }                              \
    }                                \
    BranchHelper_MSA(i != lanes);    \
  }
        case BZ_B:
          BZ_DF(b, kMSALanesByte)
          break;
        case BZ_H:
          BZ_DF(h, kMSALanesHalf)
          break;
        case BZ_W:
          BZ_DF(w, kMSALanesWord)
          break;
        case BZ_D:
          BZ_DF(d, kMSALanesDword)
          break;
#undef BZ_DF
        case BNZ_V: {
          msa_reg_t wt;
          get_msa_register(wt_reg(), &wt);
          BranchHelper_MSA(wt.d[0] != 0 || wt.d[1] != 0);
        } break;
#define BNZ_DF(witdh, lanes)         \
  {                                  \
    msa_reg_t wt;                    \
    get_msa_register(wt_reg(), &wt); \
    int i;                           \
    for (i = 0; i < lanes; ++i) {    \
      if (wt.witdh[i] == 0) {        \
        break;                       \
      }                              \
    }                                \
    BranchHelper_MSA(i == lanes);    \
  }
        case BNZ_B:
          BNZ_DF(b, kMSALanesByte)
          break;
        case BNZ_H:
          BNZ_DF(h, kMSALanesHalf)
          break;
        case BNZ_W:
          BNZ_DF(w, kMSALanesWord)
          break;
        case BNZ_D:
          BNZ_DF(d, kMSALanesDword)
          break;
#undef BNZ_DF
        default:
          UNREACHABLE();
      }
      break;
    // ------------- REGIMM class.
    case REGIMM:
      switch (instr_.RtFieldRaw()) {
        case BLTZ:
          BranchHelper(rs < 0);
          break;
        case BGEZ:
          BranchHelper(rs >= 0);
          break;
        case BLTZAL:
          BranchAndLinkHelper(rs < 0);
          break;
        case BGEZAL:
          BranchAndLinkHelper(rs >= 0);
          break;
        case DAHI:
          SetResult(rs_reg, rs + (se_imm16 << 32));
          break;
        case DATI:
          SetResult(rs_reg, rs + (se_imm16 << 48));
          break;
        default:
          UNREACHABLE();
      }
      break;  // case REGIMM.
    // ------------- Branch instructions.
    // When comparing to zero, the encoding of rt field is always 0, so we don't
    // need to replace rt with zero.
    case BEQ:
      BranchHelper(rs == rt);
      break;
    case BNE:
      BranchHelper(rs != rt);
      break;
    case POP06:  // BLEZALC, BGEZALC, BGEUC, BLEZ (pre-r6)
      if (kArchVariant == kMips64r6) {
        if (rt_reg != 0) {
          if (rs_reg == 0) {  // BLEZALC
            BranchAndLinkCompactHelper(rt <= 0, 16);
          } else {
            if (rs_reg == rt_reg) {  // BGEZALC
              BranchAndLinkCompactHelper(rt >= 0, 16);
            } else {  // BGEUC
              BranchCompactHelper(
                  static_cast<uint64_t>(rs) >= static_cast<uint64_t>(rt), 16);
            }
          }
        } else {  // BLEZ
          BranchHelper(rs <= 0);
        }
      } else {  // BLEZ
        BranchHelper(rs <= 0);
      }
      break;
    case POP07:  // BGTZALC, BLTZALC, BLTUC, BGTZ (pre-r6)
      if (kArchVariant == kMips64r6) {
        if (rt_reg != 0) {
          if (rs_reg == 0) {  // BGTZALC
            BranchAndLinkCompactHelper(rt > 0, 16);
          } else {
            if (rt_reg == rs_reg) {  // BLTZALC
              BranchAndLinkCompactHelper(rt < 0, 16);
            } else {  // BLTUC
              BranchCompactHelper(
                  static_cast<uint64_t>(rs) < static_cast<uint64_t>(rt), 16);
            }
          }
        } else {  // BGTZ
          BranchHelper(rs > 0);
        }
      } else {  // BGTZ
        BranchHelper(rs > 0);
      }
      break;
    case POP26:  // BLEZC, BGEZC, BGEC/BLEC / BLEZL (pre-r6)
      if (kArchVariant == kMips64r6) {
        if (rt_reg != 0) {
          if (rs_reg == 0) {  // BLEZC
            BranchCompactHelper(rt <= 0, 16);
          } else {
            if (rs_reg == rt_reg) {  // BGEZC
              BranchCompactHelper(rt >= 0, 16);
            } else {  // BGEC/BLEC
              BranchCompactHelper(rs >= rt, 16);
            }
          }
        }
      } else {  // BLEZL
        BranchAndLinkHelper(rs <= 0);
      }
      break;
    case POP27:  // BGTZC, BLTZC, BLTC/BGTC / BGTZL (pre-r6)
      if (kArchVariant == kMips64r6) {
        if (rt_reg != 0) {
          if (rs_reg == 0) {  // BGTZC
            BranchCompactHelper(rt > 0, 16);
          } else {
            if (rs_reg == rt_reg) {  // BLTZC
              BranchCompactHelper(rt < 0, 16);
            } else {  // BLTC/BGTC
              BranchCompactHelper(rs < rt, 16);
            }
          }
        }
      } else {  // BGTZL
        BranchAndLinkHelper(rs > 0);
      }
      break;
    case POP66:           // BEQZC, JIC
      if (rs_reg != 0) {  // BEQZC
        BranchCompactHelper(rs == 0, 21);
      } else {  // JIC
        next_pc = rt + imm16;
      }
      break;
    case POP76:           // BNEZC, JIALC
      if (rs_reg != 0) {  // BNEZC
        BranchCompactHelper(rs != 0, 21);
      } else {  // JIALC
        int64_t current_pc = get_pc();
        set_register(31, current_pc + kInstrSize);
        next_pc = rt + imm16;
      }
      break;
    case BC:
      BranchCompactHelper(true, 26);
      break;
    case BALC:
      BranchAndLinkCompactHelper(true, 26);
      break;
    case POP10:  // BOVC, BEQZALC, BEQC / ADDI (pre-r6)
      if (kArchVariant == kMips64r6) {
        if (rs_reg >= rt_reg) {  // BOVC
          bool condition = !is_int32(rs) || !is_int32(rt) || !is_int32(rs + rt);
          BranchCompactHelper(condition, 16);
        } else {
          if (rs_reg == 0) {  // BEQZALC
            BranchAndLinkCompactHelper(rt == 0, 16);
          } else {  // BEQC
            BranchCompactHelper(rt == rs, 16);
          }
        }
      } else {  // ADDI
        if (HaveSameSign(rs, se_imm16)) {
          if (rs > 0) {
            if (rs <= Registers::kMaxValue - se_imm16) {
              SignalException(kIntegerOverflow);
            }
          } else if (rs < 0) {
            if (rs >= Registers::kMinValue - se_imm16) {
              SignalException(kIntegerUnderflow);
            }
          }
        }
        SetResult(rt_reg, rs + se_imm16);
      }
      break;
    case POP30:  // BNVC, BNEZALC, BNEC / DADDI (pre-r6)
      if (kArchVariant == kMips64r6) {
        if (rs_reg >= rt_reg) {  // BNVC
          bool condition = is_int32(rs) && is_int32(rt) && is_int32(rs + rt);
          BranchCompactHelper(condition, 16);
        } else {
          if (rs_reg == 0) {  // BNEZALC
            BranchAndLinkCompactHelper(rt != 0, 16);
          } else {  // BNEC
            BranchCompactHelper(rt != rs, 16);
          }
        }
      }
      break;
    // ------------- Arithmetic instructions.
    case ADDIU: {
      int32_t alu32_out = static_cast<int32_t>(rs + se_imm16);
      // Sign-extend result of 32bit operation into 64bit register.
      SetResult(rt_reg, static_cast<int64_t>(alu32_out));
      break;
    }
    case DADDIU:
      SetResult(rt_reg, rs + se_imm16);
      break;
    case SLTI:
      SetResult(rt_reg, rs < se_imm16 ? 1 : 0);
      break;
    case SLTIU:
      SetResult(rt_reg, rs_u < static_cast<uint64_t>(se_imm16) ? 1 : 0);
      break;
    case ANDI:
      SetResult(rt_reg, rs & oe_imm16);
      break;
    case ORI:
      SetResult(rt_reg, rs | oe_imm16);
      break;
    case XORI:
      SetResult(rt_reg, rs ^ oe_imm16);
      break;
    case LUI:
      if (rs_reg != 0) {
        // AUI instruction.
        DCHECK_EQ(kArchVariant, kMips64r6);
        int32_t alu32_out = static_cast<int32_t>(rs + (se_imm16 << 16));
        SetResult(rt_reg, static_cast<int64_t>(alu32_out));
      } else {
        // LUI instruction.
        int32_t alu32_out = static_cast<int32_t>(oe_imm16 << 16);
        // Sign-extend result of 32bit operation into 64bit register.
        SetResult(rt_reg, static_cast<int64_t>(alu32_out));
      }
      break;
    case DAUI:
      DCHECK_EQ(kArchVariant, kMips64r6);
      DCHECK_NE(rs_reg, 0);
      SetResult(rt_reg, rs + (se_imm16 << 16));
      break;
    // ------------- Memory instructions.
    case LB:
      set_register(rt_reg, ReadB(rs + se_imm16));
      break;
    case LH:
      set_register(rt_reg, ReadH(rs + se_imm16, instr_.instr()));
      break;
    case LWL: {
      local_monitor_.NotifyLoad();
      // al_offset is offset of the effective address within an aligned word.
      uint8_t al_offset = (rs + se_imm16) & kInt32AlignmentMask;
      uint8_t byte_shift = kInt32AlignmentMask - al_offset;
      uint32_t mask = (1 << byte_shift * 8) - 1;
      addr = rs + se_imm16 - al_offset;
      int32_t val = ReadW(addr, instr_.instr());
      val <<= byte_shift * 8;
      val |= rt & mask;
      set_register(rt_reg, static_cast<int64_t>(val));
      break;
    }
    case LW:
      set_register(rt_reg, ReadW(rs + se_imm16, instr_.instr()));
      break;
    case LWU:
      set_register(rt_reg, ReadWU(rs + se_imm16, instr_.instr()));
      break;
    case LD:
      set_register(rt_reg, Read2W(rs + se_imm16, instr_.instr()));
      break;
    case LBU:
      set_register(rt_reg, ReadBU(rs + se_imm16));
      break;
    case LHU:
      set_register(rt_reg, ReadHU(rs + se_imm16, instr_.instr()));
      break;
    case LWR: {
      // al_offset is offset of the effective address within an aligned word.
      uint8_t al_offset = (rs + se_imm16) & kInt32AlignmentMask;
      uint8_t byte_shift = kInt32AlignmentMask - al_offset;
      uint32_t mask = al_offset ? (~0 << (byte_shift + 1) * 8) : 0;
      addr = rs + se_imm16 - al_offset;
      alu_out = ReadW(addr, instr_.instr());
      alu_out = static_cast<uint32_t>(alu_out) >> al_offset * 8;
      alu_out |= rt & mask;
      set_register(rt_reg, alu_out);
      break;
    }
    case LDL: {
      // al_offset is offset of the effective address within an aligned word.
      uint8_t al_offset = (rs + se_imm16) & kInt64AlignmentMask;
      uint8_t byte_shift = kInt64AlignmentMask - al_offset;
      uint64_t mask = (1UL << byte_shift * 8) - 1;
      addr = rs + se_imm16 - al_offset;
      alu_out = Read2W(addr, instr_.instr());
      alu_out <<= byte_shift * 8;
      alu_out |= rt & mask;
      set_register(rt_reg, alu_out);
      break;
    }
    case LDR: {
      // al_offset is offset of the effective address within an aligned word.
      uint8_t al_offset = (rs + se_imm16) & kInt64AlignmentMask;
      uint8_t byte_shift = kInt64AlignmentMask - al_offset;
      uint64_t mask = al_offset ? (~0UL << (byte_shift + 1) * 8) : 0UL;
      addr = rs + se_imm16 - al_offset;
      alu_out = Read2W(addr, instr_.instr());
      alu_out = alu_out >> al_offset * 8;
      alu_out |= rt & mask;
      set_register(rt_reg, alu_out);
      break;
    }
    case SB:
      WriteB(rs + se_imm16, static_cast<int8_t>(rt));
      break;
    case SH:
      WriteH(rs + se_imm16, static_cast<uint16_t>(rt), instr_.instr());
      break;
    case SWL: {
      uint8_t al_offset = (rs + se_imm16) & kInt32AlignmentMask;
      uint8_t byte_shift = kInt32AlignmentMask - al_offset;
      uint32_t mask = byte_shift ? (~0 << (al_offset + 1) * 8) : 0;
      addr = rs + se_imm16 - al_offset;
      uint64_t mem_value = ReadW(addr, instr_.instr()) & mask;
      mem_value |= static_cast<uint32_t>(rt) >> byte_shift * 8;
      WriteW(addr, static_cast<int32_t>(mem_value), instr_.instr());
      break;
    }
    case SW:
      WriteW(rs + se_imm16, static_cast<int32_t>(rt), instr_.instr());
      break;
    case SD:
      Write2W(rs + se_imm16, rt, instr_.instr());
      break;
    case SWR: {
      uint8_t al_offset = (rs + se_imm16) & kInt32AlignmentMask;
      uint32_t mask = (1 << al_offset * 8) - 1;
      addr = rs + se_imm16 - al_offset;
      uint64_t mem_value = ReadW(addr, instr_.instr());
      mem_value = (rt << al_offset * 8) | (mem_value & mask);
      WriteW(addr, static_cast<int32_t>(mem_value), instr_.instr());
      break;
    }
    case SDL: {
      uint8_t al_offset = (rs + se_imm16) & kInt64AlignmentMask;
      uint8_t byte_shift = kInt64AlignmentMask - al_offset;
      uint64_t mask = byte_shift ? (~0UL << (al_offset + 1) * 8) : 0;
      addr = rs + se_imm16 - al_offset;
      uint64_t mem_value = Read2W(addr, instr_.instr()) & mask;
      mem_value |= static_cast<uint64_t>(rt) >> byte_shift * 8;
      Write2W(addr, mem_value, instr_.instr());
      break;
    }
    case SDR: {
      uint8_t al_offset = (rs + se_imm16) & kInt64AlignmentMask;
      uint64_t mask = (1UL << al_offset * 8) - 1;
      addr = rs + se_imm16 - al_offset;
      uint64_t mem_value = Read2W(addr, instr_.instr());
      mem_value = (rt << al_offset * 8) | (mem_value & mask);
      Write2W(addr, mem_value, instr_.instr());
      break;
    }
    case LL: {
      DCHECK(kArchVariant != kMips64r6);
      base::MutexGuard lock_guard(&GlobalMonitor::Get()->mutex);
      addr = rs + se_imm16;
      set_register(rt_reg, ReadW(addr, instr_.instr()));
      local_monitor_.NotifyLoadLinked(addr, TransactionSize::Word);
      GlobalMonitor::Get()->NotifyLoadLinked_Locked(addr,
                                                    &global_monitor_thread_);
      break;
    }
    case SC: {
      DCHECK(kArchVariant != kMips64r6);
      addr = rs + se_imm16;
      WriteConditionalW(addr, static_cast<int32_t>(rt), instr_.instr(), rt_reg);
      break;
    }
    case LLD: {
      DCHECK(kArchVariant != kMips64r6);
      base::MutexGuard lock_guard(&GlobalMonitor::Get()->mutex);
      addr = rs + se_imm16;
      set_register(rt_reg, Read2W(addr, instr_.instr()));
      local_monitor_.NotifyLoadLinked(addr, TransactionSize::DoubleWord);
      GlobalMonitor::Get()->NotifyLoadLinked_Locked(addr,
                                                    &global_monitor_thread_);
      break;
    }
    case SCD: {
      DCHECK(kArchVariant != kMips64r6);
      addr = rs + se_imm16;
      WriteConditional2W(addr, rt, instr_.instr(), rt_reg);
      break;
    }
    case LWC1:
      set_fpu_register(ft_reg, kFPUInvalidResult);  // Trash upper 32 bits.
      set_fpu_register_word(ft_reg,
                            ReadW(rs + se_imm16, instr_.instr(), FLOAT_DOUBLE));
      break;
    case LDC1:
      set_fpu_register_double(ft_reg, ReadD(rs + se_imm16, instr_.instr()));
      TraceMemRd(addr, get_fpu_register(ft_reg), DOUBLE);
      break;
    case SWC1: {
      int32_t alu_out_32 = static_cast<int32_t>(get_fpu_register(ft_reg));
      WriteW(rs + se_imm16, alu_out_32, instr_.instr());
      break;
    }
    case SDC1:
      WriteD(rs + se_imm16, get_fpu_register_double(ft_reg), instr_.instr());
      TraceMemWr(rs + se_imm16, get_fpu_register(ft_reg), DWORD);
      break;
    // ------------- PC-Relative instructions.
    case PCREL: {
      // rt field: checking 5-bits.
      int32_t imm21 = instr_.Imm21Value();
      int64_t current_pc = get_pc();
      uint8_t rt = (imm21 >> kImm16Bits);
      switch (rt) {
        case ALUIPC:
          addr = current_pc + (se_imm16 << 16);
          alu_out = static_cast<int64_t>(~0x0FFFF) & addr;
          break;
        case AUIPC:
          alu_out = current_pc + (se_imm16 << 16);
          break;
        default: {
          int32_t imm19 = instr_.Imm19Value();
          // rt field: checking the most significant 3-bits.
          rt = (imm21 >> kImm18Bits);
          switch (rt) {
            case LDPC:
              addr =
                  (current_pc & static_cast<int64_t>(~0x7)) + (se_imm18 << 3);
              alu_out = Read2W(addr, instr_.instr());
              break;
            default: {
              // rt field: checking the most significant 2-bits.
              rt = (imm21 >> kImm19Bits);
              switch (rt) {
                case LWUPC: {
                  // Set sign.
                  imm19 <<= (kOpcodeBits + kRsBits + 2);
                  imm19 >>= (kOpcodeBits + kRsBits + 2);
                  addr = current_pc + (imm19 << 2);
                  alu_out = ReadWU(addr, instr_.instr());
                  break;
                }
                case LWPC: {
                  // Set sign.
                  imm19 <<= (kOpcodeBits + kRsBits + 2);
                  imm19 >>= (kOpcodeBits + kRsBits + 2);
                  addr = current_pc + (imm19 << 2);
                  alu_out = ReadW(addr, instr_.instr());
                  break;
                }
                case ADDIUPC: {
                  int64_t se_imm19 =
                      imm19 | ((imm19 & 0x40000) ? 0xFFFFFFFFFFF80000 : 0);
                  alu_out = current_pc + (se_imm19 << 2);
                  break;
                }
                default:
                  UNREACHABLE();
              }
              break;
            }
          }
          break;
        }
      }
      SetResult(rs_reg, alu_out);
      break;
    }
    case SPECIAL3: {
      switch (instr_.FunctionFieldRaw()) {
        case LL_R6: {
          DCHECK_EQ(kArchVariant, kMips64r6);
          base::MutexGuard lock_guard(&GlobalMonitor::Get()->mutex);
          int64_t base = get_register(instr_.BaseValue());
          int32_t offset9 = instr_.Imm9Value();
          addr = base + offset9;
          DCHECK_EQ(addr & 0x3, 0);
          set_register(rt_reg, ReadW(addr, instr_.instr()));
          local_monitor_.NotifyLoadLinked(addr, TransactionSize::Word);
          GlobalMonitor::Get()->NotifyLoadLinked_Locked(
              addr, &global_monitor_thread_);
          break;
        }
        case LLD_R6: {
          DCHECK_EQ(kArchVariant, kMips64r6);
          base::MutexGuard lock_guard(&GlobalMonitor::Get()->mutex);
          int64_t base = get_register(instr_.BaseValue());
          int32_t offset9 = instr_.Imm9Value();
          addr = base + offset9;
          DCHECK_EQ(addr & kPointerAlignmentMask, 0);
          set_register(rt_reg, Read2W(addr, instr_.instr()));
          local_monitor_.NotifyLoadLinked(addr, TransactionSize::DoubleWord);
          GlobalMonitor::Get()->NotifyLoadLinked_Locked(
              addr, &global_monitor_thread_);
          break;
        }
        case SC_R6: {
          DCHECK_EQ(kArchVariant, kMips64r6);
          int64_t base = get_register(instr_.BaseValue());
          int32_t offset9 = instr_.Imm9Value();
          addr = base + offset9;
          DCHECK_EQ(addr & 0x3, 0);
          WriteConditionalW(addr, static_cast<int32_t>(rt), instr_.instr(),
                            rt_reg);
          break;
        }
        case SCD_R6: {
          DCHECK_EQ(kArchVariant, kMips64r6);
          int64_t base = get_register(instr_.BaseValue());
          int32_t offset9 = instr_.Imm9Value();
          addr = base + offset9;
          DCHECK_EQ(addr & kPointerAlignmentMask, 0);
          WriteConditional2W(addr, rt, instr_.instr(), rt_reg);
          break;
        }
        default:
          UNREACHABLE();
      }
      break;
    }

    case MSA:
      switch (instr_.MSAMinorOpcodeField()) {
        case kMsaMinorI8:
          DecodeTypeMsaI8();
          break;
        case kMsaMinorI5:
          DecodeTypeMsaI5();
          break;
        case kMsaMinorI10:
          DecodeTypeMsaI10();
          break;
        case kMsaMinorELM:
          DecodeTypeMsaELM();
          break;
        case kMsaMinorBIT:
          DecodeTypeMsaBIT();
          break;
        case kMsaMinorMI10:
          DecodeTypeMsaMI10();
          break;
        default:
          UNREACHABLE();
      }
      break;
    default:
      UNREACHABLE();
  }

  if (execute_branch_delay_instruction) {
    // Execute branch delay slot
    // We don't check for end_sim_pc. First it should not be met as the current
    // pc is valid. Secondly a jump should always execute its branch delay slot.
    Instruction* branch_delay_instr =
        reinterpret_cast<Instruction*>(get_pc() + kInstrSize);
    BranchDelayInstructionDecode(branch_delay_instr);
  }

  // If needed update pc after the branch delay execution.
  if (next_pc != bad_ra) {
    set_pc(next_pc);
  }
}

// Type 3: instructions using a 26 bytes immediate. (e.g. j, jal).
void Simulator::DecodeTypeJump() {
  // instr_ will be overwritten by BranchDelayInstructionDecode(), so we save
  // the result of IsLinkingInstruction now.
  bool isLinkingInstr = instr_.IsLinkingInstruction();
  // Get current pc.
  int64_t current_pc = get_pc();
  // Get unchanged bits of pc.
  int64_t pc_high_bits = current_pc & 0xFFFFFFFFF0000000;
  // Next pc.
  int64_t next_pc = pc_high_bits | (instr_.Imm26Value() << 2);

  // Execute branch delay slot.
  // We don't check for end_sim_pc. First it should not be met as the current pc
  // is valid. Secondly a jump should always execute its branch delay slot.
  Instruction* branch_delay_instr =
      reinterpret_cast<Instruction*>(current_pc + kInstrSize);
  BranchDelayInstructionDecode(branch_delay_instr);

  // Update pc and ra if necessary.
  // Do this after the branch delay execution.
  if (isLinkingInstr) {
    set_register(31, current_pc + 2 * kInstrSize);
  }
  set_pc(next_pc);
  pc_modified_ = true;
}

// Executes the current instruction.
void Simulator::InstructionDecode(Instruction* instr) {
  if (v8_flags.check_icache) {
    CheckICache(i_cache(), instr);
  }
  pc_modified_ = false;

  v8::base::EmbeddedVector<char, 256> buffer;

  if (v8_flags.trace_sim) {
    base::SNPrintF(trace_buf_, " ");
    disasm::NameConverter converter;
    disasm::Disassembler dasm(converter);
    // Use a reasonably large buffer.
    dasm.InstructionDecode(buffer, reinterpret_cast<uint8_t*>(instr));
  }

  instr_ = instr;
  switch (instr_.InstructionType()) {
    case Instruction::kRegisterType:
      DecodeTypeRegister();
      break;
    case Instruction::kImmediateType:
      DecodeTypeImmediate();
      break;
    case Instruction::kJumpType:
      DecodeTypeJump();
      break;
    default:
      UNSUPPORTED();
  }

  if (v8_flags.trace_sim) {
    PrintF("  0x%08" PRIxPTR "   %-44s   %s\n",
           reinterpret_cast<intptr_t>(instr), buffer.begin(),
           trace_buf_.begin());
  }

  if (!pc_modified_) {
    set_register(pc, reinterpret_cast<int64_t>(instr) + kInstrSize);
  }
}

void Simulator::Execute() {
  // Get the PC to simulate. Cannot use the accessor here as we need the
  // raw PC value and not the one used as input to arithmetic instructions.
  int64_t program_counter = get_pc();
  if (v8_flags.stop_sim_at == 0) {
    // Fast version of the dispatch loop without checking whether the simulator
    // should be stopping at a particular executed instruction.
    while (program_counter != end_sim_pc) {
      Instruction* instr = reinterpret_cast<Instruction*>(program_counter);
      icount_++;
      InstructionDecode(instr);
      program_counter = get_pc();
    }
  } else {
    // v8_flags.stop_sim_at is at the non-default value. Stop in the debugger
    // when we reach the particular instruction count.
    while (program_counter != end_sim_pc) {
      Instruction* instr = reinterpret_cast<Instruction*>(program_counter);
      icount_++;
      if (icount_ == static_cast<int64_t>(v8_flags.stop_sim_at)) {
        MipsDebugger dbg(this);
        dbg.Debug();
      } else {
        InstructionDecode(instr);
      }
      program_counter = get_pc();
    }
  }
}

void Simulator::CallInternal(Address entry) {
  // Adjust JS-based stack limit to C-based stack limit.
  isolate_->stack_guard()->AdjustStackLimitForSimulator();

  // Prepare to execute the code at entry.
  set_register(pc, static_cast<int64_t>(entry));
  // Put down marker for end of simulation. The simulator will stop simulation
  // when the PC reaches this value. By saving the "end simulation" value into
  // the LR the simulation stops when returning to this call point.
  set_register(ra, end_sim_pc);

  // Remember the values of callee-saved registers.
  // The code below assumes that r9 is not used as sb (static base) in
  // simulator code and therefore is regarded as a callee-saved register.
  int64_t s0_val = get_register(s0);
  int64_t s1_val = get_register(s1);
  int64_t s2_val = get_register(s2);
  int64_t s3_val = get_register(s3);
  int64_t s4_val = get_register(s4);
  int64_t s5_val = get_register(s5);
  int64_t s6_val = get_register(s6);
  int64_t s7_val = get_register(s7);
  int64_t gp_val = get_register(gp);
  int64_t sp_val = get_register(sp);
  int64_t fp_val = get_register(fp);

  // Set up the callee-saved registers with a known value. To be able to check
  // that they are preserved properly across JS execution.
  int64_t callee_saved_value = icount_;
  set_register(s0, callee_saved_value);
  set_register(s1, callee_saved_value);
  set_register(s2, callee_saved_value);
  set_register(s3, callee_saved_value);
  set_register(s4, callee_saved_value);
  set_register(s5, callee_saved_value);
  set_register(s6, callee_saved_value);
  set_register(s7, callee_saved_value);
  set_register(gp, callee_saved_value);
  set_register(fp, callee_saved_value);

  // Start the simulation.
  Execute();

  // Check that the callee-saved registers have been preserved.
  CHECK_EQ(callee_saved_value, get_register(s0));
  CHECK_EQ(callee_saved_value, get_register(s1));
  CHECK_EQ(callee_saved_value, get_register(s2));
  CHECK_EQ(callee_saved_value, get_register(s3));
  CHECK_EQ(callee_saved_value, get_register(s4));
  CHECK_EQ(callee_saved_value, get_register(s5));
  CHECK_EQ(callee_saved_value, get_register(s6));
  CHECK_EQ(callee_saved_value, get_register(s7));
  CHECK_EQ(callee_saved_value, get_register(gp));
  CHECK_EQ(callee_saved_value, get_register(fp));

  // Restore callee-saved registers with the original value.
  set_register(s0, s0_val);
  set_register(s1, s1_val);
  set_register(s2, s2_val);
  set_register(s3, s3_val);
  set_register(s4, s4_val);
  set_register(s5, s5_val);
  set_register(s6, s6_val);
  set_register(s7, s7_val);
  set_register(gp, gp_val);
  set_register(sp, sp_val);
  set_register(fp, fp_val);
}

void Simulator::CallImpl(Address entry, CallArgument* args) {
  std::vector<int64_t> stack_args(0);
  for (int i = 0; !args[i].IsEnd(); i++) {
    CallArgument arg = args[i];
    if (i < 8) {
      if (arg.IsGP()) {
        set_register(i + 4, arg.bits());
      } else {
        DCHECK(arg.IsFP());
        set_fpu_register(i + 12, arg.bits());
      }
    } else {
      DCHECK(arg.IsFP() || arg.IsGP());
      stack_args.push_back(arg.bits());
    }
  }

  // Remaining arguments passed on stack.
  int64_t original_stack = get_register(sp);
  // Compute position of stack on entry to generated code.
  int64_t stack_args_size =
      stack_args.size() * sizeof(stack_args[0]) + kCArgsSlotsSize;
  int64_t entry_stack = original_stack - stack_args_size;

  if (base::OS::ActivationFrameAlignment() != 0) {
    entry_stack &= -base::OS::ActivationFrameAlignment();
  }
  // Store remaining arguments on stack, from low to high memory.
  char* stack_argument = reinterpret_cast<char*>(entry_stack);
  memcpy(stack_argument + kCArgSlotCount, stack_args.data(),
         stack_args.size() * sizeof(int64_t));
  set_register(sp, entry_stack);

  CallInternal(entry);

  // Pop stack passed arguments.
  CHECK_EQ(entry_stack, get_register(sp));
  set_register(sp, original_stack);
}

double Simulator::CallFP(Address entry, double d0, double d1) {
  if (!IsMipsSoftFloatABI) {
    const FPURegister fparg2 = f13;
    set_fpu_register_double(f12, d0);
    set_fpu_register_double(fparg2, d1);
  } else {
    int buffer[2];
    DCHECK(sizeof(buffer[0]) * 2 == sizeof(d0));
    memcpy(buffer, &d0, sizeof(d0));
    set_dw_register(a0, buffer);
    memcpy(buffer, &d1, sizeof(d1));
    set_dw_register(a2, buffer);
  }
  CallInternal(entry);
  if (!IsMipsSoftFloatABI) {
    return get_fpu_register_double(f0);
  } else {
    return get_double_from_register_pair(v0);
  }
}

uintptr_t Simulator::PushAddress(uintptr_t address) {
  int64_t new_sp = get_register(sp) - sizeof(uintptr_t);
  uintptr_t* stack_slot = reinterpret_cast<uintptr_t*>(new_sp);
  *stack_slot = address;
  set_register(sp, new_sp);
  return new_sp;
}

uintptr_t Simulator::PopAddress() {
  int64_t current_sp = get_register(sp);
  uintptr_t* stack_slot = reinterpret_cast<uintptr_t*>(current_sp);
  uintptr_t address = *stack_slot;
  set_register(sp, current_sp + sizeof(uintptr_t));
  return address;
}

Simulator::LocalMonitor::LocalMonitor()
    : access_state_(MonitorAccess::Open),
      tagged_addr_(0),
      size_(TransactionSize::None) {}

void Simulator::LocalMonitor::Clear() {
  access_state_ = MonitorAccess::Open;
  tagged_addr_ = 0;
  size_ = TransactionSize::None;
}

void Simulator::LocalMonitor::NotifyLoad() {
  if (access_state_ == MonitorAccess::RMW) {
    // A non linked load could clear the local monitor. As a result, it's
    // most strict to unconditionally clear the local monitor on load.
    Clear();
  }
}

void Simulator::LocalMonitor::NotifyLoadLinked(uintptr_t addr,
                                               TransactionSize size) {
  access_state_ = MonitorAccess::RMW;
  tagged_addr_ = addr;
  size_ = size;
}

void Simulator::LocalMonitor::NotifyStore() {
  if (access_state_ == MonitorAccess::RMW) {
    // A non exclusive store could clear the local monitor. As a result, it's
    // most strict to unconditionally clear the local monitor on store.
    Clear();
  }
}

bool Simulator::LocalMonitor::NotifyStoreConditional(uintptr_t addr,
                                                     TransactionSize size) {
  if (access_state_ == MonitorAccess::RMW) {
    if (addr == tagged_addr_ && size_ == size) {
      Clear();
      return true;
    } else {
      return false;
    }
  } else {
    DCHECK(access_state_ == MonitorAccess::Open);
    return false;
  }
}

Simulator::GlobalMonitor::LinkedAddress::LinkedAddress()
    : access_state_(MonitorAccess::Open),
      tagged_addr_(0),
      next_(nullptr),
      prev_(nullptr),
      failure_counter_(0) {}

void Simulator::GlobalMonitor::LinkedAddress::Clear_Locked() {
  access_state_ = MonitorAccess::Open;
  tagged_addr_ = 0;
}

void Simulator::GlobalMonitor::LinkedAddress::NotifyLoadLinked_Locked(
    uintptr_t addr) {
  access_state_ = MonitorAccess::RMW;
  tagged_addr_ = addr;
}

void Simulator::GlobalMonitor::LinkedAddress::NotifyStore_Locked() {
  if (access_state_ == MonitorAccess::RMW) {
    // A non exclusive store could clear the global monitor. As a result, it's
    // most strict to unconditionally clear global monitors on store.
    Clear_Locked();
  }
}

bool Simulator::GlobalMonitor::LinkedAddress::NotifyStoreConditional_Locked(
    uintptr_t addr, bool is_requesting_thread) {
  if (access_state_ == MonitorAccess::RMW) {
    if (is_requesting_thread) {
      if (addr == tagged_addr_) {
        Clear_Locked();
        // Introduce occasional sc/scd failures. This is to simulate the
        // behavior of hardware, which can randomly fail due to background
        // cache evictions.
        if (failure_counter_++ >= kMaxFailureCounter) {
          failure_counter_ = 0;
          return false;
        } else {
          return true;
        }
      }
    } else if ((addr & kExclusiveTaggedAddrMask) ==
               (tagged_addr_ & kExclusiveTaggedAddrMask)) {
      // Check the masked addresses when responding to a successful lock by
      // another thread so the implementation is more conservative (i.e. the
      // granularity of locking is as large as possible.)
      Clear_Locked();
      return false;
    }
  }
  return false;
}

void Simulator::GlobalMonitor::NotifyLoadLinked_Locked(
    uintptr_t addr, LinkedAddress* linked_address) {
  linked_address->NotifyLoadLinked_Locked(addr);
  PrependProcessor_Locked(linked_address);
}

void Simulator::GlobalMonitor::NotifyStore_Locked(
    LinkedAddress* linked_address) {
  // Notify each thread of the store operation.
  for (LinkedAddress* iter = head_; iter; iter = iter->next_) {
    iter->NotifyStore_Locked();
  }
}

bool Simulator::GlobalMonitor::NotifyStoreConditional_Locked(
    uintptr_t addr, LinkedAddress* linked_address) {
  DCHECK(IsProcessorInLinkedList_Locked(linked_address));
  if (linked_address->NotifyStoreConditional_Locked(addr, true)) {
    // Notify the other processors that this StoreConditional succeeded.
    for (LinkedAddress* iter = head_; iter; iter = iter->next_) {
      if (iter != linked_address) {
        iter->NotifyStoreConditional_Locked(addr, false);
      }
    }
    return true;
  } else {
    return false;
  }
}

bool Simulator::GlobalMonitor::IsProcessorInLinkedList_Locked(
    LinkedAddress* linked_address) const {
  return head_ == linked_address || linked_address->next_ ||
         linked_address->prev_;
}

void Simulator::GlobalMonitor::PrependProcessor_Locked(
    LinkedAddress* linked_address) {
  if (IsProcessorInLinkedList_Locked(linked_address)) {
    return;
  }

  if (head_) {
    head_->prev_ = linked_address;
  }
  linked_address->prev_ = nullptr;
  linked_address->next_ = head_;
  head_ = linked_address;
}

void Simulator::GlobalMonitor::RemoveLinkedAddress(
    LinkedAddress* linked_address) {
  base::MutexGuard lock_guard(&mutex);
  if (!IsProcessorInLinkedList_Locked(linked_address)) {
    return;
  }

  if (linked_address->prev_) {
    linked_address->prev_->next_ = linked_address->next_;
  } else {
    head_ = linked_address->next_;
  }
  if (linked_address->next_) {
    linked_address->next_->prev_ = linked_address->prev_;
  }
  linked_address->prev_ = nullptr;
  linked_address->next_ = nullptr;
}

#undef SScanF
#undef BRACKETS
}  // namespace internal
}  // namespace v8

#endif  // USE_SIMULATOR
