// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/execution/loong64/simulator-loong64.h"

// Only build the simulator if not compiling for real LOONG64 hardware.
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
#include "src/codegen/loong64/constants-loong64.h"
#include "src/codegen/macro-assembler.h"
#include "src/diagnostics/disasm.h"
#include "src/heap/combined-heap.h"
#include "src/runtime/runtime-utils.h"
#include "src/utils/ostreams.h"

#if V8_ENABLE_WEBASSEMBLY
#include "src/trap-handler/trap-handler-simulator.h"
#endif  // V8_ENABLE_WEBASSEMBLY

namespace v8 {
namespace internal {

DEFINE_LAZY_LEAKY_OBJECT_GETTER(Simulator::GlobalMonitor,
                                Simulator::GlobalMonitor::Get)

// #define PRINT_SIM_LOG

// Util functions.
inline bool HaveSameSign(int64_t a, int64_t b) { return ((a ^ b) >= 0); }

uint32_t get_fcsr_condition_bit(uint32_t cc) {
  if (cc == 0) {
    return 23;
  } else {
    return 24 + cc;
  }
}

#ifdef PRINT_SIM_LOG
inline void printf_instr(const char* _Format, ...) {
  va_list varList;
  va_start(varList, _Format);
  vprintf(_Format, varList);
  va_end(varList);
}
#else
#define printf_instr(...)
#endif

// This macro provides a platform independent use of sscanf. The reason for
// SScanF not being implemented in a platform independent was through
// ::v8::internal::OS in the same way as base::SNPrintF is that the Windows C
// Run-Time Library does not provide vsscanf.
#define SScanF sscanf

// The Loong64Debugger class is used by the simulator while debugging simulated
// code.
class Loong64Debugger {
 public:
  explicit Loong64Debugger(Simulator* sim) : sim_(sim) {}

  void Stop(Instruction* instr);
  void Debug();
  // Print all registers with a nice formatting.
  void PrintAllRegs();
  void PrintAllRegsIncludingFPU();

 private:
  // We set the breakpoint code to 0xFFFF to easily recognize it.
  static const Instr kBreakpointInstr = BREAK | 0xFFFF;
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

void Loong64Debugger::Stop(Instruction* instr) {
  // Get the stop code.
  uint32_t code = instr->Bits(25, 6);
  PrintF("Simulator hit (%u)\n", code);
  Debug();
}

int64_t Loong64Debugger::GetRegisterValue(int regnum) {
  if (regnum == kNumSimuRegisters) {
    return sim_->get_pc();
  } else {
    return sim_->get_register(regnum);
  }
}

int64_t Loong64Debugger::GetFPURegisterValue(int regnum) {
  if (regnum == kNumFPURegisters) {
    return sim_->get_pc();
  } else {
    return sim_->get_fpu_register(regnum);
  }
}

float Loong64Debugger::GetFPURegisterValueFloat(int regnum) {
  if (regnum == kNumFPURegisters) {
    return sim_->get_pc();
  } else {
    return sim_->get_fpu_register_float(regnum);
  }
}

double Loong64Debugger::GetFPURegisterValueDouble(int regnum) {
  if (regnum == kNumFPURegisters) {
    return sim_->get_pc();
  } else {
    return sim_->get_fpu_register_double(regnum);
  }
}

bool Loong64Debugger::GetValue(const char* desc, int64_t* value) {
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

bool Loong64Debugger::SetBreakpoint(Instruction* breakpc) {
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

bool Loong64Debugger::DeleteBreakpoint(Instruction* breakpc) {
  if (sim_->break_pc_ != nullptr) {
    sim_->break_pc_->SetInstructionBits(sim_->break_instr_);
  }

  sim_->break_pc_ = nullptr;
  sim_->break_instr_ = 0;
  return true;
}

void Loong64Debugger::UndoBreakpoints() {
  if (sim_->break_pc_ != nullptr) {
    sim_->break_pc_->SetInstructionBits(sim_->break_instr_);
  }
}

void Loong64Debugger::RedoBreakpoints() {
  if (sim_->break_pc_ != nullptr) {
    sim_->break_pc_->SetInstructionBits(kBreakpointInstr);
  }
}

void Loong64Debugger::PrintAllRegs() {
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

void Loong64Debugger::PrintAllRegsIncludingFPU() {
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

void Loong64Debugger::Debug() {
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
        PrintF("No flags on LOONG64 !\n");
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
    FPUregisters_[i] = 0;
  }
  for (int i = 0; i < kNumCFRegisters; i++) {
    CFregisters_[i] = 0;
  }

  FCSR_ = 0;

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

#define FloatPoint_Covert_F32(func)                  \
  float Simulator::func(float value) {               \
    float result = std::func(value);                 \
    if (std::isnan(result)) {                        \
      uint32_t q_nan, nan;                           \
      nan = *reinterpret_cast<uint32_t*>(&result);   \
      q_nan = nan | 0x400000;                        \
      *reinterpret_cast<uint32_t*>(&result) = q_nan; \
    }                                                \
    return result;                                   \
  }
#define FloatPoint_Covert_F64(func)                  \
  FloatPoint_Covert_F32(func)                        \
  double Simulator::func(double value) {             \
    double result = std::func(value);                \
    if (std::isnan(result)) {                        \
      uint64_t q_nan, nan;                           \
      nan = *reinterpret_cast<uint64_t*>(&value);    \
      q_nan = nan | 0x8000000000000;                 \
      *reinterpret_cast<uint64_t*>(&result) = q_nan; \
    }                                                \
    return result;                                   \
  }

FloatPoint_Covert_F64(ceil)
FloatPoint_Covert_F64(floor)
FloatPoint_Covert_F64(trunc)
#undef FloatPoint_Covert_F32
#undef FloatPoint_Covert_F64

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
  FPUregisters_[fpureg] = value;
}

void Simulator::set_fpu_register_word(int fpureg, int32_t value) {
  // Set ONLY lower 32-bits, leaving upper bits untouched.
  DCHECK((fpureg >= 0) && (fpureg < kNumFPURegisters));
  int32_t* pword;
  pword = reinterpret_cast<int32_t*>(&FPUregisters_[fpureg]);

  *pword = value;
}

void Simulator::set_fpu_register_hi_word(int fpureg, int32_t value) {
  // Set ONLY upper 32-bits, leaving lower bits untouched.
  DCHECK((fpureg >= 0) && (fpureg < kNumFPURegisters));
  int32_t* phiword;
  phiword = (reinterpret_cast<int32_t*>(&FPUregisters_[fpureg])) + 1;

  *phiword = value;
}

void Simulator::set_fpu_register_float(int fpureg, float value) {
  DCHECK((fpureg >= 0) && (fpureg < kNumFPURegisters));
  memcpy(&FPUregisters_[fpureg], &value, sizeof(value));
}

void Simulator::set_fpu_register_double(int fpureg, double value) {
  DCHECK((fpureg >= 0) && (fpureg < kNumFPURegisters));
  memcpy(&FPUregisters_[fpureg], &value, sizeof(value));
}

void Simulator::set_cf_register(int cfreg, bool value) {
  DCHECK((cfreg >= 0) && (cfreg < kNumCFRegisters));
  CFregisters_[cfreg] = value;
}

// Get the register from the architecture state. This function does handle
// the special case of accessing the PC register.
int64_t Simulator::get_register(int reg) const {
  DCHECK((reg >= 0) && (reg < kNumSimuRegisters));
  if (reg == 0)
    return 0;
  else
    return registers_[reg];
}

double Simulator::get_double_from_register_pair(int reg) {
  // TODO(plind): bad ABI stuff, refactor or remove.
  DCHECK((reg >= 0) && (reg < kNumSimuRegisters));

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
  return FPUregisters_[fpureg];
}

int32_t Simulator::get_fpu_register_word(int fpureg) const {
  DCHECK((fpureg >= 0) && (fpureg < kNumFPURegisters));
  return static_cast<int32_t>(FPUregisters_[fpureg] & 0xFFFFFFFF);
}

int32_t Simulator::get_fpu_register_signed_word(int fpureg) const {
  DCHECK((fpureg >= 0) && (fpureg < kNumFPURegisters));
  return static_cast<int32_t>(FPUregisters_[fpureg] & 0xFFFFFFFF);
}

int32_t Simulator::get_fpu_register_hi_word(int fpureg) const {
  DCHECK((fpureg >= 0) && (fpureg < kNumFPURegisters));
  return static_cast<int32_t>((FPUregisters_[fpureg] >> 32) & 0xFFFFFFFF);
}

float Simulator::get_fpu_register_float(int fpureg) const {
  DCHECK((fpureg >= 0) && (fpureg < kNumFPURegisters));
  return base::bit_cast<float>(get_fpu_register_word(fpureg));
}

double Simulator::get_fpu_register_double(int fpureg) const {
  DCHECK((fpureg >= 0) && (fpureg < kNumFPURegisters));
  return base::bit_cast<double>(FPUregisters_[fpureg]);
}

bool Simulator::get_cf_register(int cfreg) const {
  DCHECK((cfreg >= 0) && (cfreg < kNumCFRegisters));
  return CFregisters_[cfreg];
}

// Runtime FP routines take up to two double arguments and zero
// or one integer arguments. All are constructed here,
// from a0-a3 or fa0 and fa1 (n64).
void Simulator::GetFpArgs(double* x, double* y, int32_t* z) {
  const int fparg2 = f1;
  *x = get_fpu_register_double(f0);
  *y = get_fpu_register_double(fparg2);
  *z = static_cast<int32_t>(get_register(a2));
}

// The return value is either in v0/v1 or f0.
void Simulator::SetFpResult(const double& result) {
  set_fpu_register_double(0, result);
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

void Simulator::set_fcsr_rounding_mode(FPURoundingMode mode) {
  FCSR_ |= mode & kFPURoundingModeMask;
}

unsigned int Simulator::get_fcsr_rounding_mode() {
  return FCSR_ & kFPURoundingModeMask;
}

// Sets the rounding error codes in FCSR based on the result of the rounding.
// Returns true if the operation was invalid.
bool Simulator::set_fcsr_round_error(double original, double rounded) {
  bool ret = false;
  double max_int32 = std::numeric_limits<int32_t>::max();
  double min_int32 = std::numeric_limits<int32_t>::min();
  set_fcsr_bit(kFCSRInvalidOpCauseBit, false);
  set_fcsr_bit(kFCSRUnderflowCauseBit, false);
  set_fcsr_bit(kFCSROverflowCauseBit, false);
  set_fcsr_bit(kFCSRInexactCauseBit, false);

  if (!std::isfinite(original) || !std::isfinite(rounded)) {
    set_fcsr_bit(kFCSRInvalidOpCauseBit, true);
    ret = true;
  }

  if (original != rounded) {
    set_fcsr_bit(kFCSRInexactCauseBit, true);
  }

  if (rounded < DBL_MIN && rounded > -DBL_MIN && rounded != 0) {
    set_fcsr_bit(kFCSRUnderflowCauseBit, true);
    ret = true;
  }

  if (rounded > max_int32 || rounded < min_int32) {
    set_fcsr_bit(kFCSROverflowCauseBit, true);
    // The reference is not really clear but it seems this is required:
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
  set_fcsr_bit(kFCSRInvalidOpCauseBit, false);
  set_fcsr_bit(kFCSRUnderflowCauseBit, false);
  set_fcsr_bit(kFCSROverflowCauseBit, false);
  set_fcsr_bit(kFCSRInexactCauseBit, false);

  if (!std::isfinite(original) || !std::isfinite(rounded)) {
    set_fcsr_bit(kFCSRInvalidOpCauseBit, true);
    ret = true;
  }

  if (original != rounded) {
    set_fcsr_bit(kFCSRInexactCauseBit, true);
  }

  if (rounded < DBL_MIN && rounded > -DBL_MIN && rounded != 0) {
    set_fcsr_bit(kFCSRUnderflowCauseBit, true);
    ret = true;
  }

  if (rounded >= max_int64 || rounded < min_int64) {
    set_fcsr_bit(kFCSROverflowCauseBit, true);
    // The reference is not really clear but it seems this is required:
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
  set_fcsr_bit(kFCSRInvalidOpCauseBit, false);
  set_fcsr_bit(kFCSRUnderflowCauseBit, false);
  set_fcsr_bit(kFCSROverflowCauseBit, false);
  set_fcsr_bit(kFCSRInexactCauseBit, false);

  if (!std::isfinite(original) || !std::isfinite(rounded)) {
    set_fcsr_bit(kFCSRInvalidOpCauseBit, true);
    ret = true;
  }

  if (original != rounded) {
    set_fcsr_bit(kFCSRInexactCauseBit, true);
  }

  if (rounded < FLT_MIN && rounded > -FLT_MIN && rounded != 0) {
    set_fcsr_bit(kFCSRUnderflowCauseBit, true);
    ret = true;
  }

  if (rounded > max_int32 || rounded < min_int32) {
    set_fcsr_bit(kFCSROverflowCauseBit, true);
    // The reference is not really clear but it seems this is required:
    set_fcsr_bit(kFCSRInvalidOpCauseBit, true);
    ret = true;
  }

  return ret;
}

void Simulator::set_fpu_register_word_invalid_result(float original,
                                                     float rounded) {
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
}

void Simulator::set_fpu_register_invalid_result(float original, float rounded) {
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
}

void Simulator::set_fpu_register_invalid_result64(float original,
                                                  float rounded) {
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
}

void Simulator::set_fpu_register_word_invalid_result(double original,
                                                     double rounded) {
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
}

void Simulator::set_fpu_register_invalid_result(double original,
                                                double rounded) {
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
}

void Simulator::set_fpu_register_invalid_result64(double original,
                                                  double rounded) {
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
}

// Sets the rounding error codes in FCSR based on the result of the rounding.
// Returns true if the operation was invalid.
bool Simulator::set_fcsr_round64_error(float original, float rounded) {
  bool ret = false;
  // The value of INT64_MAX (2^63-1) can't be represented as double exactly,
  // loading the most accurate representation into max_int64, which is 2^63.
  double max_int64 = static_cast<double>(std::numeric_limits<int64_t>::max());
  double min_int64 = std::numeric_limits<int64_t>::min();
  set_fcsr_bit(kFCSRInvalidOpCauseBit, false);
  set_fcsr_bit(kFCSRUnderflowCauseBit, false);
  set_fcsr_bit(kFCSROverflowCauseBit, false);
  set_fcsr_bit(kFCSRInexactCauseBit, false);

  if (!std::isfinite(original) || !std::isfinite(rounded)) {
    set_fcsr_bit(kFCSRInvalidOpCauseBit, true);
    ret = true;
  }

  if (original != rounded) {
    set_fcsr_bit(kFCSRInexactCauseBit, true);
  }

  if (rounded < FLT_MIN && rounded > -FLT_MIN && rounded != 0) {
    set_fcsr_bit(kFCSRUnderflowCauseBit, true);
    ret = true;
  }

  if (rounded >= max_int64 || rounded < min_int64) {
    set_fcsr_bit(kFCSROverflowCauseBit, true);
    // The reference is not really clear but it seems this is required:
    set_fcsr_bit(kFCSRInvalidOpCauseBit, true);
    ret = true;
  }

  return ret;
}

// For ftint instructions only
void Simulator::round_according_to_fcsr(double toRound, double* rounded,
                                        int32_t* rounded_int) {
  // 0 RN (round to nearest): Round a result to the nearest
  // representable value; if the result is exactly halfway between
  // two representable values, round to zero.

  // 1 RZ (round toward zero): Round a result to the closest
  // representable value whose absolute value is less than or
  // equal to the infinitely accurate result.

  // 2 RP (round up, or toward +infinity): Round a result to the
  // next representable value up.

  // 3 RN (round down, or toward −infinity): Round a result to
  // the next representable value down.
  // switch ((FCSR_ >> 8) & 3) {
  switch (FCSR_ & kFPURoundingModeMask) {
    case kRoundToNearest:
      *rounded = floor(toRound + 0.5);
      *rounded_int = static_cast<int32_t>(*rounded);
      if ((*rounded_int & 1) != 0 && *rounded_int - toRound == 0.5) {
        // If the number is halfway between two integers,
        // round to the even one.
        *rounded_int -= 1;
        *rounded -= 1.;
      }
      break;
    case kRoundToZero:
      *rounded = trunc(toRound);
      *rounded_int = static_cast<int32_t>(*rounded);
      break;
    case kRoundToPlusInf:
      *rounded = ceil(toRound);
      *rounded_int = static_cast<int32_t>(*rounded);
      break;
    case kRoundToMinusInf:
      *rounded = floor(toRound);
      *rounded_int = static_cast<int32_t>(*rounded);
      break;
  }
}

void Simulator::round64_according_to_fcsr(double toRound, double* rounded,
                                          int64_t* rounded_int) {
  // 0 RN (round to nearest): Round a result to the nearest
  // representable value; if the result is exactly halfway between
  // two representable values, round to zero.

  // 1 RZ (round toward zero): Round a result to the closest
  // representable value whose absolute value is less than or.
  // equal to the infinitely accurate result.

  // 2 RP (round up, or toward +infinity): Round a result to the
  // next representable value up.

  // 3 RN (round down, or toward −infinity): Round a result to
  // the next representable value down.
  switch (FCSR_ & kFPURoundingModeMask) {
    case kRoundToNearest:
      *rounded = floor(toRound + 0.5);
      *rounded_int = static_cast<int64_t>(*rounded);
      if ((*rounded_int & 1) != 0 && *rounded_int - toRound == 0.5) {
        // If the number is halfway between two integers,
        // round to the even one.
        *rounded_int -= 1;
        *rounded -= 1.;
      }
      break;
    case kRoundToZero:
      *rounded = trunc(toRound);
      *rounded_int = static_cast<int64_t>(*rounded);
      break;
    case kRoundToPlusInf:
      *rounded = ceil(toRound);
      *rounded_int = static_cast<int64_t>(*rounded);
      break;
    case kRoundToMinusInf:
      *rounded = floor(toRound);
      *rounded_int = static_cast<int64_t>(*rounded);
      break;
  }
}

void Simulator::round_according_to_fcsr(float toRound, float* rounded,
                                        int32_t* rounded_int) {
  // 0 RN (round to nearest): Round a result to the nearest
  // representable value; if the result is exactly halfway between
  // two representable values, round to zero.

  // 1 RZ (round toward zero): Round a result to the closest
  // representable value whose absolute value is less than or
  // equal to the infinitely accurate result.

  // 2 RP (round up, or toward +infinity): Round a result to the
  // next representable value up.

  // 3 RN (round down, or toward −infinity): Round a result to
  // the next representable value down.
  switch (FCSR_ & kFPURoundingModeMask) {
    case kRoundToNearest:
      *rounded = floor(toRound + 0.5);
      *rounded_int = static_cast<int32_t>(*rounded);
      if ((*rounded_int & 1) != 0 && *rounded_int - toRound == 0.5) {
        // If the number is halfway between two integers,
        // round to the even one.
        *rounded_int -= 1;
        *rounded -= 1.f;
      }
      break;
    case kRoundToZero:
      *rounded = trunc(toRound);
      *rounded_int = static_cast<int32_t>(*rounded);
      break;
    case kRoundToPlusInf:
      *rounded = ceil(toRound);
      *rounded_int = static_cast<int32_t>(*rounded);
      break;
    case kRoundToMinusInf:
      *rounded = floor(toRound);
      *rounded_int = static_cast<int32_t>(*rounded);
      break;
  }
}

void Simulator::round64_according_to_fcsr(float toRound, float* rounded,
                                          int64_t* rounded_int) {
  // 0 RN (round to nearest): Round a result to the nearest
  // representable value; if the result is exactly halfway between
  // two representable values, round to zero.

  // 1 RZ (round toward zero): Round a result to the closest
  // representable value whose absolute value is less than or.
  // equal to the infinitely accurate result.

  // 2 RP (round up, or toward +infinity): Round a result to the
  // next representable value up.

  // 3 RN (round down, or toward −infinity): Round a result to
  // the next representable value down.
  switch (FCSR_ & kFPURoundingModeMask) {
    case kRoundToNearest:
      *rounded = floor(toRound + 0.5);
      *rounded_int = static_cast<int64_t>(*rounded);
      if ((*rounded_int & 1) != 0 && *rounded_int - toRound == 0.5) {
        // If the number is halfway between two integers,
        // round to the even one.
        *rounded_int -= 1;
        *rounded -= 1.f;
      }
      break;
    case kRoundToZero:
      *rounded = trunc(toRound);
      *rounded_int = static_cast<int64_t>(*rounded);
      break;
    case kRoundToPlusInf:
      *rounded = ceil(toRound);
      *rounded_int = static_cast<int64_t>(*rounded);
      break;
    case kRoundToMinusInf:
      *rounded = floor(toRound);
      *rounded_int = static_cast<int64_t>(*rounded);
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

// TODO(plind): refactor this messy debug code when we do unaligned access.
void Simulator::DieOrDebug() {
  if ((1)) {  // Flag for this was removed.
    Loong64Debugger dbg(this);
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

bool Simulator::ProbeMemory(uintptr_t address, uintptr_t access_size) {
#if V8_ENABLE_WEBASSEMBLY && V8_TRAP_HANDLER_SUPPORTED
  uintptr_t last_accessed_byte = address + access_size - 1;
  uintptr_t current_pc = registers_[pc];
  uintptr_t landing_pad =
      trap_handler::ProbeMemory(last_accessed_byte, current_pc);
  if (!landing_pad) return true;
  set_pc(landing_pad);
  set_register(kWasmTrapHandlerFaultAddressRegister.code(), current_pc);
  return false;
#else
  return true;
#endif
}

int32_t Simulator::ReadW(int64_t addr, Instruction* instr, TraceType t) {
  if (addr >= 0 && addr < 0x400) {
    // This has to be a nullptr-dereference, drop into debugger.
    PrintF("Memory read from bad address: 0x%08" PRIx64 " , pc=0x%08" PRIxPTR
           " \n",
           addr, reinterpret_cast<intptr_t>(instr));
    DieOrDebug();
  }

  {
    local_monitor_.NotifyLoad();
    int32_t* ptr = reinterpret_cast<int32_t*>(addr);
    TraceMemRd(addr, static_cast<int64_t>(*ptr), t);
    return *ptr;
  }
}

uint32_t Simulator::ReadWU(int64_t addr, Instruction* instr) {
  if (addr >= 0 && addr < 0x400) {
    // This has to be a nullptr-dereference, drop into debugger.
    PrintF("Memory read from bad address: 0x%08" PRIx64 " , pc=0x%08" PRIxPTR
           " \n",
           addr, reinterpret_cast<intptr_t>(instr));
    DieOrDebug();
  }

  {
    local_monitor_.NotifyLoad();
    uint32_t* ptr = reinterpret_cast<uint32_t*>(addr);
    TraceMemRd(addr, static_cast<int64_t>(*ptr), WORD);
    return *ptr;
  }
}

void Simulator::WriteW(int64_t addr, int32_t value, Instruction* instr) {
  if (addr >= 0 && addr < 0x400) {
    // This has to be a nullptr-dereference, drop into debugger.
    PrintF("Memory write to bad address: 0x%08" PRIx64 " , pc=0x%08" PRIxPTR
           " \n",
           addr, reinterpret_cast<intptr_t>(instr));
    DieOrDebug();
  }

  {
    local_monitor_.NotifyStore();
    base::MutexGuard lock_guard(&GlobalMonitor::Get()->mutex);
    GlobalMonitor::Get()->NotifyStore_Locked(&global_monitor_thread_);
    TraceMemWr(addr, value, WORD);
    int* ptr = reinterpret_cast<int*>(addr);
    *ptr = value;
    return;
  }
}

void Simulator::WriteConditionalW(int64_t addr, int32_t value,
                                  Instruction* instr, int32_t* done) {
  if (addr >= 0 && addr < 0x400) {
    // This has to be a nullptr-dereference, drop into debugger.
    PrintF("Memory write to bad address: 0x%08" PRIx64 " , pc=0x%08" PRIxPTR
           " \n",
           addr, reinterpret_cast<intptr_t>(instr));
    DieOrDebug();
  }

  if ((addr & 0x3) == 0) {
    base::MutexGuard lock_guard(&GlobalMonitor::Get()->mutex);
    if (local_monitor_.NotifyStoreConditional(addr, TransactionSize::Word) &&
        GlobalMonitor::Get()->NotifyStoreConditional_Locked(
            addr, &global_monitor_thread_)) {
      local_monitor_.NotifyStore();
      GlobalMonitor::Get()->NotifyStore_Locked(&global_monitor_thread_);
      TraceMemWr(addr, value, WORD);
      int* ptr = reinterpret_cast<int*>(addr);
      *ptr = value;
      *done = 1;
    } else {
      *done = 0;
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

  {
    local_monitor_.NotifyLoad();
    int64_t* ptr = reinterpret_cast<int64_t*>(addr);
    TraceMemRd(addr, *ptr);
    return *ptr;
  }
}

void Simulator::Write2W(int64_t addr, int64_t value, Instruction* instr) {
  if (addr >= 0 && addr < 0x400) {
    // This has to be a nullptr-dereference, drop into debugger.
    PrintF("Memory write to bad address: 0x%08" PRIx64 " , pc=0x%08" PRIxPTR
           "\n",
           addr, reinterpret_cast<intptr_t>(instr));
    DieOrDebug();
  }

  {
    local_monitor_.NotifyStore();
    base::MutexGuard lock_guard(&GlobalMonitor::Get()->mutex);
    GlobalMonitor::Get()->NotifyStore_Locked(&global_monitor_thread_);
    TraceMemWr(addr, value, DWORD);
    int64_t* ptr = reinterpret_cast<int64_t*>(addr);
    *ptr = value;
    return;
  }
}

void Simulator::WriteConditional2W(int64_t addr, int64_t value,
                                   Instruction* instr, int32_t* done) {
  if (addr >= 0 && addr < 0x400) {
    // This has to be a nullptr-dereference, drop into debugger.
    PrintF("Memory write to bad address: 0x%08" PRIx64 " , pc=0x%08" PRIxPTR
           "\n",
           addr, reinterpret_cast<intptr_t>(instr));
    DieOrDebug();
  }

  if ((addr & kPointerAlignmentMask) == 0) {
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
      *done = 1;
    } else {
      *done = 0;
    }
    return;
  }
  PrintF("Unaligned write at 0x%08" PRIx64 " , pc=0x%08" V8PRIxPTR "\n", addr,
         reinterpret_cast<intptr_t>(instr));
  DieOrDebug();
}

double Simulator::ReadD(int64_t addr, Instruction* instr) {
  local_monitor_.NotifyLoad();
  double* ptr = reinterpret_cast<double*>(addr);
  return *ptr;
}

void Simulator::WriteD(int64_t addr, double value, Instruction* instr) {
  local_monitor_.NotifyStore();
  base::MutexGuard lock_guard(&GlobalMonitor::Get()->mutex);
  GlobalMonitor::Get()->NotifyStore_Locked(&global_monitor_thread_);
  double* ptr = reinterpret_cast<double*>(addr);
  *ptr = value;
  return;
}

uint16_t Simulator::ReadHU(int64_t addr, Instruction* instr) {
  local_monitor_.NotifyLoad();
  uint16_t* ptr = reinterpret_cast<uint16_t*>(addr);
  TraceMemRd(addr, static_cast<int64_t>(*ptr));
  return *ptr;
}

int16_t Simulator::ReadH(int64_t addr, Instruction* instr) {
  local_monitor_.NotifyLoad();
  int16_t* ptr = reinterpret_cast<int16_t*>(addr);
  TraceMemRd(addr, static_cast<int64_t>(*ptr));
  return *ptr;
}

void Simulator::WriteH(int64_t addr, uint16_t value, Instruction* instr) {
  local_monitor_.NotifyStore();
  base::MutexGuard lock_guard(&GlobalMonitor::Get()->mutex);
  GlobalMonitor::Get()->NotifyStore_Locked(&global_monitor_thread_);
  TraceMemWr(addr, value, HALF);
  uint16_t* ptr = reinterpret_cast<uint16_t*>(addr);
  *ptr = value;
  return;
}

void Simulator::WriteH(int64_t addr, int16_t value, Instruction* instr) {
  local_monitor_.NotifyStore();
  base::MutexGuard lock_guard(&GlobalMonitor::Get()->mutex);
  GlobalMonitor::Get()->NotifyStore_Locked(&global_monitor_thread_);
  TraceMemWr(addr, value, HALF);
  int16_t* ptr = reinterpret_cast<int16_t*>(addr);
  *ptr = value;
  return;
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
  if ((addr & alignment_mask) == 0) {
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
  if ((addr & alignment_mask) == 0) {
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
  UNIMPLEMENTED();
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

// Configuration for C calling convention (see c-linkage.cc).
#define PARAM_REGISTERS a0, a1, a2, a3, a4, a5, a6, a7
#define RETURN_REGISTER a0
#define FP_PARAM_REGISTERS f0, f1, f2, f3, f4, f5, f6, f7
#define FP_RETURN_REGISTER f0

void Simulator::CallAnyCTypeFunction(Address target_address,
                                     const EncodedCSignature& signature) {
  const int64_t* stack_pointer = reinterpret_cast<int64_t*>(get_register(sp));
  const double* double_stack_pointer =
      reinterpret_cast<double*>(get_register(sp));

  const Register kParamRegisters[] = {PARAM_REGISTERS};
  const FPURegister kFPParamRegisters[] = {FP_PARAM_REGISTERS};

  int num_gp_params = 0, num_fp_params = 0, num_stack_params = 0;

  CHECK_LE(signature.ParameterCount(), kMaxCParameters);
  static_assert(sizeof(AnyCType) == 8, "AnyCType is assumed to be 64-bit.");
  AnyCType args[kMaxCParameters];
  for (int i = 0; i < signature.ParameterCount(); ++i) {
    if (signature.IsFloat(i)) {
      if (num_fp_params < 8) {
        args[i].double_value =
            get_fpu_register_double(kFPParamRegisters[num_fp_params++]);
      } else if (num_gp_params < 8) {
        args[i].int64_value = get_register(kParamRegisters[num_gp_params++]);
      } else {
        args[i].double_value = double_stack_pointer[num_stack_params++];
      }
    } else {
      if (num_gp_params < 8) {
        args[i].int64_value = get_register(kParamRegisters[num_gp_params++]);
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
  int32_t opcode_hi15 = instr_.Bits(31, 17);
  CHECK_EQ(opcode_hi15, 0x15);
  uint32_t code = instr_.Bits(14, 0);
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

    {
      // With the hard floating point calling convention, double
      // arguments are passed in FPU registers. Fetch the arguments
      // from there and call the builtin using soft floating point
      // convention.
      switch (redirection->type()) {
        case ExternalReference::BUILTIN_FP_FP_CALL:
        case ExternalReference::BUILTIN_COMPARE_CALL:
          arg0 = get_fpu_register(f0);
          arg1 = get_fpu_register(f1);
          arg2 = get_fpu_register(f2);
          arg3 = get_fpu_register(f3);
          break;
        case ExternalReference::BUILTIN_FP_CALL:
          arg0 = get_fpu_register(f0);
          arg1 = get_fpu_register(f1);
          break;
        case ExternalReference::BUILTIN_FP_INT_CALL:
          arg0 = get_fpu_register(f0);
          arg1 = get_fpu_register(f1);
          arg2 = get_register(a2);
          break;
        default:
          break;
      }
    }

    // Based on CpuFeatures::IsSupported(FPU), Loong64 will use either hardware
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

  } else if (code <= kMaxStopCode) {
    if (IsWatchpoint(code)) {
      PrintWatchpoint(code);
    } else {
      IncreaseStopCounter(code);
      HandleStop(code, instr_.instr());
    }
  } else {
    // All remaining break_ codes, and all traps are handled here.
    Loong64Debugger dbg(this);
    dbg.Debug();
  }
}

// Stop helper functions.
bool Simulator::IsWatchpoint(uint64_t code) {
  return (code <= kMaxWatchpointCode);
}

void Simulator::PrintWatchpoint(uint64_t code) {
  Loong64Debugger dbg(this);
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
    Loong64Debugger dbg(this);
    dbg.Stop(instr);
  }
}

bool Simulator::IsStopInstruction(Instruction* instr) {
  int32_t opcode_hi15 = instr->Bits(31, 17);
  uint32_t code = static_cast<uint32_t>(instr->Bits(14, 0));
  return (opcode_hi15 == 0x15) && code > kMaxWatchpointCode &&
         code <= kMaxStopCode;
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
void Simulator::DecodeTypeOp6() {
  int64_t alu_out;
  // Next pc.
  int64_t next_pc = bad_ra;

  // Branch instructions common part.
  auto BranchAndLinkHelper = [this, &next_pc]() {
    int64_t current_pc = get_pc();
    set_register(ra, current_pc + kInstrSize);
    int32_t offs26_low16 =
        static_cast<uint32_t>(instr_.Bits(25, 10) << 16) >> 16;
    int32_t offs26_high10 = static_cast<int32_t>(instr_.Bits(9, 0) << 22) >> 6;
    int32_t offs26 = offs26_low16 | offs26_high10;
    next_pc = current_pc + (offs26 << 2);
    printf_instr("Offs26: %08x\n", offs26);
    set_pc(next_pc);
  };

  auto BranchOff16Helper = [this, &next_pc](bool do_branch) {
    int64_t current_pc = get_pc();
    int32_t offs16 = static_cast<int32_t>(instr_.Bits(25, 10) << 16) >> 16;
    printf_instr("Offs16: %08x\n", offs16);
    int32_t offs = do_branch ? (offs16 << 2) : kInstrSize;
    next_pc = current_pc + offs;
    set_pc(next_pc);
  };

  auto BranchOff21Helper = [this, &next_pc](bool do_branch) {
    int64_t current_pc = get_pc();
    int32_t offs21_low16 =
        static_cast<uint32_t>(instr_.Bits(25, 10) << 16) >> 16;
    int32_t offs21_high5 = static_cast<int32_t>(instr_.Bits(4, 0) << 27) >> 11;
    int32_t offs = offs21_low16 | offs21_high5;
    printf_instr("Offs21: %08x\n", offs);
    offs = do_branch ? (offs << 2) : kInstrSize;
    next_pc = current_pc + offs;
    set_pc(next_pc);
  };

  auto BranchOff26Helper = [this, &next_pc]() {
    int64_t current_pc = get_pc();
    int32_t offs26_low16 =
        static_cast<uint32_t>(instr_.Bits(25, 10) << 16) >> 16;
    int32_t offs26_high10 = static_cast<int32_t>(instr_.Bits(9, 0) << 22) >> 6;
    int32_t offs26 = offs26_low16 | offs26_high10;
    next_pc = current_pc + (offs26 << 2);
    printf_instr("Offs26: %08x\n", offs26);
    set_pc(next_pc);
  };

  auto JumpOff16Helper = [this, &next_pc]() {
    int32_t offs16 = static_cast<int32_t>(instr_.Bits(25, 10) << 16) >> 16;
    printf_instr("JIRL\t %s: %016lx, %s: %016lx, offs16: %x\n",
                 Registers::Name(rd_reg()), rd(), Registers::Name(rj_reg()),
                 rj(), offs16);
    set_register(rd_reg(), get_pc() + kInstrSize);
    next_pc = rj() + (offs16 << 2);
    set_pc(next_pc);
  };

  switch (instr_.Bits(31, 26) << 26) {
    case ADDU16I_D: {
      printf_instr("ADDU16I_D\t %s: %016lx, %s: %016lx, si16: %d\n",
                   Registers::Name(rd_reg()), rd(), Registers::Name(rj_reg()),
                   rj(), si16());
      int32_t si16_upper = static_cast<int32_t>(si16()) << 16;
      alu_out = static_cast<int64_t>(si16_upper) + rj();
      SetResult(rd_reg(), alu_out);
      break;
    }
    case BEQZ:
      printf_instr("BEQZ\t %s: %016lx, ", Registers::Name(rj_reg()), rj());
      BranchOff21Helper(rj() == 0);
      break;
    case BNEZ:
      printf_instr("BNEZ\t %s: %016lx, ", Registers::Name(rj_reg()), rj());
      BranchOff21Helper(rj() != 0);
      break;
    case BCZ: {
      if (instr_.Bits(9, 8) == 0b00) {
        // BCEQZ
        printf_instr("BCEQZ\t fcc%d: %s, ", cj_reg(), cj() ? "True" : "False");
        BranchOff21Helper(cj() == false);
      } else if (instr_.Bits(9, 8) == 0b01) {
        // BCNEZ
        printf_instr("BCNEZ\t fcc%d: %s, ", cj_reg(), cj() ? "True" : "False");
        BranchOff21Helper(cj() == true);
      } else {
        UNREACHABLE();
      }
      break;
    }
    case JIRL:
      JumpOff16Helper();
      break;
    case B:
      printf_instr("B\t ");
      BranchOff26Helper();
      break;
    case BL:
      printf_instr("BL\t ");
      BranchAndLinkHelper();
      break;
    case BEQ:
      printf_instr("BEQ\t %s: %016lx, %s, %016lx, ", Registers::Name(rj_reg()),
                   rj(), Registers::Name(rd_reg()), rd());
      BranchOff16Helper(rj() == rd());
      break;
    case BNE:
      printf_instr("BNE\t %s: %016lx, %s, %016lx, ", Registers::Name(rj_reg()),
                   rj(), Registers::Name(rd_reg()), rd());
      BranchOff16Helper(rj() != rd());
      break;
    case BLT:
      printf_instr("BLT\t %s: %016lx, %s, %016lx, ", Registers::Name(rj_reg()),
                   rj(), Registers::Name(rd_reg()), rd());
      BranchOff16Helper(rj() < rd());
      break;
    case BGE:
      printf_instr("BGE\t %s: %016lx, %s, %016lx, ", Registers::Name(rj_reg()),
                   rj(), Registers::Name(rd_reg()), rd());
      BranchOff16Helper(rj() >= rd());
      break;
    case BLTU:
      printf_instr("BLTU\t %s: %016lx, %s, %016lx, ", Registers::Name(rj_reg()),
                   rj(), Registers::Name(rd_reg()), rd());
      BranchOff16Helper(rj_u() < rd_u());
      break;
    case BGEU:
      printf_instr("BGEU\t %s: %016lx, %s, %016lx, ", Registers::Name(rj_reg()),
                   rj(), Registers::Name(rd_reg()), rd());
      BranchOff16Helper(rj_u() >= rd_u());
      break;
    default:
      UNREACHABLE();
  }
}

void Simulator::DecodeTypeOp7() {
  int64_t alu_out;

  switch (instr_.Bits(31, 25) << 25) {
    case LU12I_W: {
      printf_instr("LU12I_W\t %s: %016lx, si20: %d\n",
                   Registers::Name(rd_reg()), rd(), si20());
      int32_t si20_upper = static_cast<int32_t>(si20() << 12);
      SetResult(rd_reg(), static_cast<int64_t>(si20_upper));
      break;
    }
    case LU32I_D: {
      printf_instr("LU32I_D\t %s: %016lx, si20: %d\n",
                   Registers::Name(rd_reg()), rd(), si20());
      int32_t si20_signExtend = static_cast<int32_t>(si20() << 12) >> 12;
      int64_t lower_32bit_mask = 0xFFFFFFFF;
      alu_out = (static_cast<int64_t>(si20_signExtend) << 32) |
                (rd() & lower_32bit_mask);
      SetResult(rd_reg(), alu_out);
      break;
    }
    case PCADDI: {
      printf_instr("PCADDI\t %s: %016lx, si20: %d\n", Registers::Name(rd_reg()),
                   rd(), si20());
      int32_t si20_signExtend = static_cast<int32_t>(si20() << 12) >> 10;
      int64_t current_pc = get_pc();
      alu_out = static_cast<int64_t>(si20_signExtend) + current_pc;
      SetResult(rd_reg(), alu_out);
      break;
    }
    case PCALAU12I: {
      printf_instr("PCALAU12I\t %s: %016lx, si20: %d\n",
                   Registers::Name(rd_reg()), rd(), si20());
      int32_t si20_signExtend = static_cast<int32_t>(si20() << 12);
      int64_t current_pc = get_pc();
      int64_t clear_lower12bit_mask = 0xFFFFFFFFFFFFF000;
      alu_out = static_cast<int64_t>(si20_signExtend) + current_pc;
      SetResult(rd_reg(), alu_out & clear_lower12bit_mask);
      break;
    }
    case PCADDU12I: {
      printf_instr("PCADDU12I\t %s: %016lx, si20: %d\n",
                   Registers::Name(rd_reg()), rd(), si20());
      int32_t si20_signExtend = static_cast<int32_t>(si20() << 12);
      int64_t current_pc = get_pc();
      alu_out = static_cast<int64_t>(si20_signExtend) + current_pc;
      SetResult(rd_reg(), alu_out);
      break;
    }
    case PCADDU18I: {
      printf_instr("PCADDU18I\t %s: %016lx, si20: %d\n",
                   Registers::Name(rd_reg()), rd(), si20());
      int64_t si20_signExtend = (static_cast<int64_t>(si20()) << 44) >> 26;
      int64_t current_pc = get_pc();
      alu_out = si20_signExtend + current_pc;
      SetResult(rd_reg(), alu_out);
      break;
    }
    default:
      UNREACHABLE();
  }
}

void Simulator::DecodeTypeOp8() {
  int64_t addr = 0x0;
  int64_t si14_se = (static_cast<int64_t>(si14()) << 50) >> 48;

  switch (instr_.Bits(31, 24) << 24) {
    case LDPTR_W:
      printf_instr("LDPTR_W\t %s: %016lx, %s: %016lx, si14: %016lx\n",
                   Registers::Name(rd_reg()), rd(), Registers::Name(rj_reg()),
                   rj(), si14_se);
      if (!ProbeMemory(rj() + si14_se, sizeof(int32_t))) return;
      set_register(rd_reg(), ReadW(rj() + si14_se, instr_.instr()));
      break;
    case STPTR_W:
      printf_instr("STPTR_W\t %s: %016lx, %s: %016lx, si14: %016lx\n",
                   Registers::Name(rd_reg()), rd(), Registers::Name(rj_reg()),
                   rj(), si14_se);
      if (!ProbeMemory(rj() + si14_se, sizeof(int32_t))) return;
      WriteW(rj() + si14_se, static_cast<int32_t>(rd()), instr_.instr());
      break;
    case LDPTR_D:
      printf_instr("LDPTR_D\t %s: %016lx, %s: %016lx, si14: %016lx\n",
                   Registers::Name(rd_reg()), rd(), Registers::Name(rj_reg()),
                   rj(), si14_se);
      if (!ProbeMemory(rj() + si14_se, sizeof(int64_t))) return;
      set_register(rd_reg(), Read2W(rj() + si14_se, instr_.instr()));
      break;
    case STPTR_D:
      printf_instr("STPTR_D\t %s: %016lx, %s: %016lx, si14: %016lx\n",
                   Registers::Name(rd_reg()), rd(), Registers::Name(rj_reg()),
                   rj(), si14_se);
      if (!ProbeMemory(rj() + si14_se, sizeof(int64_t))) return;
      Write2W(rj() + si14_se, rd(), instr_.instr());
      break;
    case LL_W: {
      printf_instr("LL_W\t %s: %016lx, %s: %016lx, si14: %016lx\n",
                   Registers::Name(rd_reg()), rd(), Registers::Name(rj_reg()),
                   rj(), si14_se);
      addr = si14_se + rj();
      if (!ProbeMemory(addr, sizeof(int32_t))) return;
      {
        base::MutexGuard lock_guard(&GlobalMonitor::Get()->mutex);
        set_register(rd_reg(), ReadW(addr, instr_.instr()));
        local_monitor_.NotifyLoadLinked(addr, TransactionSize::Word);
        GlobalMonitor::Get()->NotifyLoadLinked_Locked(addr,
                                                      &global_monitor_thread_);
      }
      break;
    }
    case SC_W: {
      printf_instr("SC_W\t %s: %016lx, %s: %016lx, si14: %016lx\n",
                   Registers::Name(rd_reg()), rd(), Registers::Name(rj_reg()),
                   rj(), si14_se);
      addr = si14_se + rj();
      if (!ProbeMemory(addr, sizeof(int32_t))) return;
      int32_t LLbit = 0;
      WriteConditionalW(addr, static_cast<int32_t>(rd()), instr_.instr(),
                        &LLbit);
      set_register(rd_reg(), LLbit);
      break;
    }
    case LL_D: {
      printf_instr("LL_D\t %s: %016lx, %s: %016lx, si14: %016lx\n",
                   Registers::Name(rd_reg()), rd(), Registers::Name(rj_reg()),
                   rj(), si14_se);
      addr = si14_se + rj();
      if (!ProbeMemory(addr, sizeof(int64_t))) return;
      {
        base::MutexGuard lock_guard(&GlobalMonitor::Get()->mutex);
        set_register(rd_reg(), Read2W(addr, instr_.instr()));
        local_monitor_.NotifyLoadLinked(addr, TransactionSize::DoubleWord);
        GlobalMonitor::Get()->NotifyLoadLinked_Locked(addr,
                                                      &global_monitor_thread_);
      }
      break;
    }
    case SC_D: {
      printf_instr("SC_D\t %s: %016lx, %s: %016lx, si14: %016lx\n",
                   Registers::Name(rd_reg()), rd(), Registers::Name(rj_reg()),
                   rj(), si14_se);
      addr = si14_se + rj();
      if (!ProbeMemory(addr, sizeof(int64_t))) return;
      int32_t LLbit = 0;
      WriteConditional2W(addr, rd(), instr_.instr(), &LLbit);
      set_register(rd_reg(), LLbit);
      break;
    }
    default:
      UNREACHABLE();
  }
}

void Simulator::DecodeTypeOp10() {
  int64_t alu_out = 0x0;
  int64_t si12_se = (static_cast<int64_t>(si12()) << 52) >> 52;
  uint64_t si12_ze = (static_cast<uint64_t>(ui12()) << 52) >> 52;

  switch (instr_.Bits(31, 22) << 22) {
    case BSTR_W: {
      CHECK_EQ(instr_.Bit(21), 1);
      uint8_t lsbw_ = lsbw();
      uint8_t msbw_ = msbw();
      CHECK_LE(lsbw_, msbw_);
      uint8_t size = msbw_ - lsbw_ + 1;
      uint64_t mask = (1ULL << size) - 1;
      if (instr_.Bit(15) == 0) {
        // BSTRINS_W
        printf_instr(
            "BSTRINS_W\t %s: %016lx, %s: %016lx, msbw: %02x, lsbw: %02x\n",
            Registers::Name(rd_reg()), rd(), Registers::Name(rj_reg()), rj(),
            msbw_, lsbw_);
        alu_out = static_cast<int32_t>((rd_u() & ~(mask << lsbw_)) |
                                       ((rj_u() & mask) << lsbw_));
      } else {
        // BSTRPICK_W
        printf_instr(
            "BSTRPICK_W\t %s: %016lx, %s: %016lx, msbw: %02x, lsbw: %02x\n",
            Registers::Name(rd_reg()), rd(), Registers::Name(rj_reg()), rj(),
            msbw_, lsbw_);
        alu_out = static_cast<int32_t>((rj_u() & (mask << lsbw_)) >> lsbw_);
      }
      SetResult(rd_reg(), alu_out);
      break;
    }
    case BSTRINS_D: {
      uint8_t lsbd_ = lsbd();
      uint8_t msbd_ = msbd();
      CHECK_LE(lsbd_, msbd_);
      printf_instr(
          "BSTRINS_D\t %s: %016lx, %s: %016lx, msbw: %02x, lsbw: %02x\n",
          Registers::Name(rd_reg()), rd(), Registers::Name(rj_reg()), rj(),
          msbd_, lsbd_);
      uint8_t size = msbd_ - lsbd_ + 1;
      if (size < 64) {
        uint64_t mask = (1ULL << size) - 1;
        alu_out = (rd_u() & ~(mask << lsbd_)) | ((rj_u() & mask) << lsbd_);
        SetResult(rd_reg(), alu_out);
      } else if (size == 64) {
        SetResult(rd_reg(), rj());
      }
      break;
    }
    case BSTRPICK_D: {
      uint8_t lsbd_ = lsbd();
      uint8_t msbd_ = msbd();
      CHECK_LE(lsbd_, msbd_);
      printf_instr(
          "BSTRPICK_D\t %s: %016lx, %s: %016lx, msbw: %02x, lsbw: %02x\n",
          Registers::Name(rd_reg()), rd(), Registers::Name(rj_reg()), rj(),
          msbd_, lsbd_);
      uint8_t size = msbd_ - lsbd_ + 1;
      if (size < 64) {
        uint64_t mask = (1ULL << size) - 1;
        alu_out = (rj_u() & (mask << lsbd_)) >> lsbd_;
        SetResult(rd_reg(), alu_out);
      } else if (size == 64) {
        SetResult(rd_reg(), rj());
      }
      break;
    }
    case SLTI:
      printf_instr("SLTI\t %s: %016lx, %s: %016lx, si12: %016lx\n",
                   Registers::Name(rd_reg()), rd(), Registers::Name(rj_reg()),
                   rj(), si12_se);
      SetResult(rd_reg(), rj() < si12_se ? 1 : 0);
      break;
    case SLTUI:
      printf_instr("SLTUI\t %s: %016lx, %s: %016lx, si12: %016lx\n",
                   Registers::Name(rd_reg()), rd(), Registers::Name(rj_reg()),
                   rj(), si12_se);
      SetResult(rd_reg(), rj_u() < static_cast<uint64_t>(si12_se) ? 1 : 0);
      break;
    case ADDI_W: {
      printf_instr("ADDI_W\t %s: %016lx, %s: %016lx, si12: %016lx\n",
                   Registers::Name(rd_reg()), rd(), Registers::Name(rj_reg()),
                   rj(), si12_se);
      int32_t alu32_out =
          static_cast<int32_t>(rj()) + static_cast<int32_t>(si12_se);
      SetResult(rd_reg(), alu32_out);
      break;
    }
    case ADDI_D:
      printf_instr("ADDI_D\t %s: %016lx, %s: %016lx, si12: %016lx\n",
                   Registers::Name(rd_reg()), rd(), Registers::Name(rj_reg()),
                   rj(), si12_se);
      SetResult(rd_reg(), rj() + si12_se);
      break;
    case LU52I_D: {
      printf_instr("LU52I_D\t %s: %016lx, %s: %016lx, si12: %016lx\n",
                   Registers::Name(rd_reg()), rd(), Registers::Name(rj_reg()),
                   rj(), si12_se);
      int64_t si12_se = static_cast<int64_t>(si12()) << 52;
      uint64_t mask = (1ULL << 52) - 1;
      alu_out = si12_se + (rj() & mask);
      SetResult(rd_reg(), alu_out);
      break;
    }
    case ANDI:
      printf_instr("ANDI\t %s: %016lx, %s: %016lx, si12: %016lx\n",
                   Registers::Name(rd_reg()), rd(), Registers::Name(rj_reg()),
                   rj(), si12_ze);
      SetResult(rd_reg(), rj() & si12_ze);
      break;
    case ORI:
      printf_instr("ORI\t %s: %016lx, %s: %016lx, si12: %016lx\n",
                   Registers::Name(rd_reg()), rd(), Registers::Name(rj_reg()),
                   rj(), si12_ze);
      SetResult(rd_reg(), rj_u() | si12_ze);
      break;
    case XORI:
      printf_instr("XORI\t %s: %016lx, %s: %016lx, si12: %016lx\n",
                   Registers::Name(rd_reg()), rd(), Registers::Name(rj_reg()),
                   rj(), si12_ze);
      SetResult(rd_reg(), rj_u() ^ si12_ze);
      break;
    case LD_B:
      printf_instr("LD_B\t %s: %016lx, %s: %016lx, si12: %016lx\n",
                   Registers::Name(rd_reg()), rd(), Registers::Name(rj_reg()),
                   rj(), si12_ze);
      if (!ProbeMemory(rj() + si12_se, sizeof(int8_t))) return;
      set_register(rd_reg(), ReadB(rj() + si12_se));
      break;
    case LD_H:
      printf_instr("LD_H\t %s: %016lx, %s: %016lx, si12: %016lx\n",
                   Registers::Name(rd_reg()), rd(), Registers::Name(rj_reg()),
                   rj(), si12_ze);
      if (!ProbeMemory(rj() + si12_se, sizeof(int16_t))) return;
      set_register(rd_reg(), ReadH(rj() + si12_se, instr_.instr()));
      break;
    case LD_W:
      printf_instr("LD_W\t %s: %016lx, %s: %016lx, si12: %016lx\n",
                   Registers::Name(rd_reg()), rd(), Registers::Name(rj_reg()),
                   rj(), si12_ze);
      if (!ProbeMemory(rj() + si12_se, sizeof(int32_t))) return;
      set_register(rd_reg(), ReadW(rj() + si12_se, instr_.instr()));
      break;
    case LD_D:
      printf_instr("LD_D\t %s: %016lx, %s: %016lx, si12: %016lx\n",
                   Registers::Name(rd_reg()), rd(), Registers::Name(rj_reg()),
                   rj(), si12_ze);
      if (!ProbeMemory(rj() + si12_se, sizeof(int64_t))) return;
      set_register(rd_reg(), Read2W(rj() + si12_se, instr_.instr()));
      break;
    case ST_B:
      printf_instr("ST_B\t %s: %016lx, %s: %016lx, si12: %016lx\n",
                   Registers::Name(rd_reg()), rd(), Registers::Name(rj_reg()),
                   rj(), si12_ze);
      if (!ProbeMemory(rj() + si12_se, sizeof(int8_t))) return;
      WriteB(rj() + si12_se, static_cast<int8_t>(rd()));
      break;
    case ST_H:
      printf_instr("ST_H\t %s: %016lx, %s: %016lx, si12: %016lx\n",
                   Registers::Name(rd_reg()), rd(), Registers::Name(rj_reg()),
                   rj(), si12_ze);
      if (!ProbeMemory(rj() + si12_se, sizeof(int16_t))) return;
      WriteH(rj() + si12_se, static_cast<int16_t>(rd()), instr_.instr());
      break;
    case ST_W:
      printf_instr("ST_W\t %s: %016lx, %s: %016lx, si12: %016lx\n",
                   Registers::Name(rd_reg()), rd(), Registers::Name(rj_reg()),
                   rj(), si12_ze);
      if (!ProbeMemory(rj() + si12_se, sizeof(int32_t))) return;
      WriteW(rj() + si12_se, static_cast<int32_t>(rd()), instr_.instr());
      break;
    case ST_D:
      printf_instr("ST_D\t %s: %016lx, %s: %016lx, si12: %016lx\n",
                   Registers::Name(rd_reg()), rd(), Registers::Name(rj_reg()),
                   rj(), si12_ze);
      if (!ProbeMemory(rj() + si12_se, sizeof(int64_t))) return;
      Write2W(rj() + si12_se, rd(), instr_.instr());
      break;
    case LD_BU:
      printf_instr("LD_BU\t %s: %016lx, %s: %016lx, si12: %016lx\n",
                   Registers::Name(rd_reg()), rd(), Registers::Name(rj_reg()),
                   rj(), si12_ze);
      if (!ProbeMemory(rj() + si12_se, sizeof(uint8_t))) return;
      set_register(rd_reg(), ReadBU(rj() + si12_se));
      break;
    case LD_HU:
      printf_instr("LD_HU\t %s: %016lx, %s: %016lx, si12: %016lx\n",
                   Registers::Name(rd_reg()), rd(), Registers::Name(rj_reg()),
                   rj(), si12_ze);
      if (!ProbeMemory(rj() + si12_se, sizeof(uint16_t))) return;
      set_register(rd_reg(), ReadHU(rj() + si12_se, instr_.instr()));
      break;
    case LD_WU:
      printf_instr("LD_WU\t %s: %016lx, %s: %016lx, si12: %016lx\n",
                   Registers::Name(rd_reg()), rd(), Registers::Name(rj_reg()),
                   rj(), si12_ze);
      if (!ProbeMemory(rj() + si12_se, sizeof(uint32_t))) return;
      set_register(rd_reg(), ReadWU(rj() + si12_se, instr_.instr()));
      break;
    case FLD_S: {
      printf_instr("FLD_S\t %s: %016f, %s: %016lx, si12: %016lx\n",
                   FPURegisters::Name(fd_reg()), fd_float(),
                   Registers::Name(rj_reg()), rj(), si12_ze);
      if (!ProbeMemory(rj() + si12_se, sizeof(float))) return;
      set_fpu_register(fd_reg(), kFPUInvalidResult);  // Trash upper 32 bits.
      set_fpu_register_word(
          fd_reg(), ReadW(rj() + si12_se, instr_.instr(), FLOAT_DOUBLE));
      break;
    }
    case FST_S: {
      printf_instr("FST_S\t %s: %016f, %s: %016lx, si12: %016lx\n",
                   FPURegisters::Name(fd_reg()), fd_float(),
                   Registers::Name(rj_reg()), rj(), si12_ze);
      if (!ProbeMemory(rj() + si12_se, sizeof(float))) return;
      int32_t alu_out_32 = static_cast<int32_t>(get_fpu_register(fd_reg()));
      WriteW(rj() + si12_se, alu_out_32, instr_.instr());
      break;
    }
    case FLD_D: {
      printf_instr("FLD_D\t %s: %016f, %s: %016lx, si12: %016lx\n",
                   FPURegisters::Name(fd_reg()), fd_double(),
                   Registers::Name(rj_reg()), rj(), si12_ze);
      if (!ProbeMemory(rj() + si12_se, sizeof(double))) return;
      set_fpu_register_double(fd_reg(), ReadD(rj() + si12_se, instr_.instr()));
      TraceMemRd(rj() + si12_se, get_fpu_register(fd_reg()), DOUBLE);
      break;
    }
    case FST_D: {
      printf_instr("FST_D\t %s: %016f, %s: %016lx, si12: %016lx\n",
                   FPURegisters::Name(fd_reg()), fd_double(),
                   Registers::Name(rj_reg()), rj(), si12_ze);
      if (!ProbeMemory(rj() + si12_se, sizeof(double))) return;
      WriteD(rj() + si12_se, get_fpu_register_double(fd_reg()), instr_.instr());
      TraceMemWr(rj() + si12_se, get_fpu_register(fd_reg()), DWORD);
      break;
    }
    default:
      UNREACHABLE();
  }
}

void Simulator::DecodeTypeOp12() {
  switch (instr_.Bits(31, 20) << 20) {
    case FMADD_S:
      printf_instr("FMADD_S\t %s: %016f, %s: %016f, %s: %016f %s: %016f\n",
                   FPURegisters::Name(fd_reg()), fd_float(),
                   FPURegisters::Name(fk_reg()), fk_float(),
                   FPURegisters::Name(fa_reg()), fa_float(),
                   FPURegisters::Name(fj_reg()), fj_float());
      SetFPUFloatResult(fd_reg(), std::fma(fj_float(), fk_float(), fa_float()));
      break;
    case FMADD_D:
      printf_instr("FMADD_D\t %s: %016f, %s: %016f, %s: %016f %s: %016f\n",
                   FPURegisters::Name(fd_reg()), fd_double(),
                   FPURegisters::Name(fk_reg()), fk_double(),
                   FPURegisters::Name(fa_reg()), fa_double(),
                   FPURegisters::Name(fj_reg()), fj_double());
      SetFPUDoubleResult(fd_reg(),
                         std::fma(fj_double(), fk_double(), fa_double()));
      break;
    case FMSUB_S:
      printf_instr("FMSUB_S\t %s: %016f, %s: %016f, %s: %016f %s: %016f\n",
                   FPURegisters::Name(fd_reg()), fd_float(),
                   FPURegisters::Name(fk_reg()), fk_float(),
                   FPURegisters::Name(fa_reg()), fa_float(),
                   FPURegisters::Name(fj_reg()), fj_float());
      SetFPUFloatResult(fd_reg(),
                        std::fma(fj_float(), fk_float(), -fa_float()));
      break;
    case FMSUB_D:
      printf_instr("FMSUB_D\t %s: %016f, %s: %016f, %s: %016f %s: %016f\n",
                   FPURegisters::Name(fd_reg()), fd_double(),
                   FPURegisters::Name(fk_reg()), fk_double(),
                   FPURegisters::Name(fa_reg()), fa_double(),
                   FPURegisters::Name(fj_reg()), fj_double());
      SetFPUDoubleResult(fd_reg(),
                         std::fma(fj_double(), fk_double(), -fa_double()));
      break;
    case FNMADD_S:
      printf_instr("FNMADD_S\t %s: %016f, %s: %016f, %s: %016f %s: %016f\n",
                   FPURegisters::Name(fd_reg()), fd_float(),
                   FPURegisters::Name(fk_reg()), fk_float(),
                   FPURegisters::Name(fa_reg()), fa_float(),
                   FPURegisters::Name(fj_reg()), fj_float());
      SetFPUFloatResult(fd_reg(),
                        std::fma(-fj_float(), fk_float(), -fa_float()));
      break;
    case FNMADD_D:
      printf_instr("FNMADD_D\t %s: %016f, %s: %016f, %s: %016f %s: %016f\n",
                   FPURegisters::Name(fd_reg()), fd_double(),
                   FPURegisters::Name(fk_reg()), fk_double(),
                   FPURegisters::Name(fa_reg()), fa_double(),
                   FPURegisters::Name(fj_reg()), fj_double());
      SetFPUDoubleResult(fd_reg(),
                         std::fma(-fj_double(), fk_double(), -fa_double()));
      break;
    case FNMSUB_S:
      printf_instr("FNMSUB_S\t %s: %016f, %s: %016f, %s: %016f %s: %016f\n",
                   FPURegisters::Name(fd_reg()), fd_float(),
                   FPURegisters::Name(fk_reg()), fk_float(),
                   FPURegisters::Name(fa_reg()), fa_float(),
                   FPURegisters::Name(fj_reg()), fj_float());
      SetFPUFloatResult(fd_reg(),
                        std::fma(-fj_float(), fk_float(), fa_float()));
      break;
    case FNMSUB_D:
      printf_instr("FNMSUB_D\t %s: %016f, %s: %016f, %s: %016f %s: %016f\n",
                   FPURegisters::Name(fd_reg()), fd_double(),
                   FPURegisters::Name(fk_reg()), fk_double(),
                   FPURegisters::Name(fa_reg()), fa_double(),
                   FPURegisters::Name(fj_reg()), fj_double());
      SetFPUDoubleResult(fd_reg(),
                         std::fma(-fj_double(), fk_double(), fa_double()));
      break;
    case FCMP_COND_S: {
      CHECK_EQ(instr_.Bits(4, 3), 0);
      float fj = fj_float();
      float fk = fk_float();
      switch (cond()) {
        case CAF: {
          printf_instr("FCMP_CAF_S fcc%d\n", cd_reg());
          set_cf_register(cd_reg(), false);
          break;
        }
        case CUN: {
          printf_instr("FCMP_CUN_S fcc%d, %s: %016f, %s: %016f\n", cd_reg(),
                       FPURegisters::Name(fj_reg()), fj,
                       FPURegisters::Name(fk_reg()), fk);
          set_cf_register(cd_reg(), std::isnan(fj) || std::isnan(fk));
          break;
        }
        case CEQ: {
          printf_instr("FCMP_CEQ_S fcc%d, %s: %016f, %s: %016f\n", cd_reg(),
                       FPURegisters::Name(fj_reg()), fj,
                       FPURegisters::Name(fk_reg()), fk);
          set_cf_register(cd_reg(), fj == fk);
          break;
        }
        case CUEQ: {
          printf_instr("FCMP_CUEQ_S fcc%d, %s: %016f, %s: %016f\n", cd_reg(),
                       FPURegisters::Name(fj_reg()), fj,
                       FPURegisters::Name(fk_reg()), fk);
          set_cf_register(cd_reg(),
                          (fj == fk) || std::isnan(fj) || std::isnan(fk));
          break;
        }
        case CLT: {
          printf_instr("FCMP_CLT_S fcc%d, %s: %016f, %s: %016f\n", cd_reg(),
                       FPURegisters::Name(fj_reg()), fj,
                       FPURegisters::Name(fk_reg()), fk);
          set_cf_register(cd_reg(), fj < fk);
          break;
        }
        case CULT: {
          printf_instr("FCMP_CULT_S fcc%d, %s: %016f, %s: %016f\n", cd_reg(),
                       FPURegisters::Name(fj_reg()), fj,
                       FPURegisters::Name(fk_reg()), fk);
          set_cf_register(cd_reg(),
                          (fj < fk) || std::isnan(fj) || std::isnan(fk));
          break;
        }
        case CLE: {
          printf_instr("FCMP_CLE_S fcc%d, %s: %016f, %s: %016f\n", cd_reg(),
                       FPURegisters::Name(fj_reg()), fj,
                       FPURegisters::Name(fk_reg()), fk);
          set_cf_register(cd_reg(), fj <= fk);
          break;
        }
        case CULE: {
          printf_instr("FCMP_CULE_S fcc%d, %s: %016f, %s: %016f\n", cd_reg(),
                       FPURegisters::Name(fj_reg()), fj,
                       FPURegisters::Name(fk_reg()), fk);
          set_cf_register(cd_reg(),
                          (fj <= fk) || std::isnan(fj) || std::isnan(fk));
          break;
        }
        case CNE: {
          printf_instr("FCMP_CNE_S fcc%d, %s: %016f, %s: %016f\n", cd_reg(),
                       FPURegisters::Name(fj_reg()), fj,
                       FPURegisters::Name(fk_reg()), fk);
          set_cf_register(cd_reg(), (fj < fk) || (fj > fk));
          break;
        }
        case COR: {
          printf_instr("FCMP_COR_S fcc%d, %s: %016f, %s: %016f\n", cd_reg(),
                       FPURegisters::Name(fj_reg()), fj,
                       FPURegisters::Name(fk_reg()), fk);
          set_cf_register(cd_reg(), !std::isnan(fj) && !std::isnan(fk));
          break;
        }
        case CUNE: {
          printf_instr("FCMP_CUNE_S fcc%d, %s: %016f, %s: %016f\n", cd_reg(),
                       FPURegisters::Name(fj_reg()), fj,
                       FPURegisters::Name(fk_reg()), fk);
          set_cf_register(cd_reg(),
                          (fj != fk) || std::isnan(fj) || std::isnan(fk));
          break;
        }
        case SAF:
        case SUN:
        case SEQ:
        case SUEQ:
        case SLT:
        case SULT:
        case SLE:
        case SULE:
        case SNE:
        case SOR:
        case SUNE:
          UNIMPLEMENTED();
        default:
          UNREACHABLE();
      }
      break;
    }
    case FCMP_COND_D: {
      CHECK_EQ(instr_.Bits(4, 3), 0);
      double fj = fj_double();
      double fk = fk_double();
      switch (cond()) {
        case CAF: {
          printf_instr("FCMP_CAF_D fcc%d\n", cd_reg());
          set_cf_register(cd_reg(), false);
          break;
        }
        case CUN: {
          printf_instr("FCMP_CUN_D fcc%d, %s: %016f, %s: %016f\n", cd_reg(),
                       FPURegisters::Name(fj_reg()), fj,
                       FPURegisters::Name(fk_reg()), fk);
          set_cf_register(cd_reg(), std::isnan(fj) || std::isnan(fk));
          break;
        }
        case CEQ: {
          printf_instr("FCMP_CEQ_D fcc%d, %s: %016f, %s: %016f\n", cd_reg(),
                       FPURegisters::Name(fj_reg()), fj,
                       FPURegisters::Name(fk_reg()), fk);
          set_cf_register(cd_reg(), fj == fk);
          break;
        }
        case CUEQ: {
          printf_instr("FCMP_CUEQ_D fcc%d, %s: %016f, %s: %016f\n", cd_reg(),
                       FPURegisters::Name(fj_reg()), fj,
                       FPURegisters::Name(fk_reg()), fk);
          set_cf_register(cd_reg(),
                          (fj == fk) || std::isnan(fj) || std::isnan(fk));
          break;
        }
        case CLT: {
          printf_instr("FCMP_CLT_D fcc%d, %s: %016f, %s: %016f\n", cd_reg(),
                       FPURegisters::Name(fj_reg()), fj,
                       FPURegisters::Name(fk_reg()), fk);
          set_cf_register(cd_reg(), fj < fk);
          break;
        }
        case CULT: {
          printf_instr("FCMP_CULT_D fcc%d, %s: %016f, %s: %016f\n", cd_reg(),
                       FPURegisters::Name(fj_reg()), fj,
                       FPURegisters::Name(fk_reg()), fk);
          set_cf_register(cd_reg(),
                          (fj < fk) || std::isnan(fj) || std::isnan(fk));
          break;
        }
        case CLE: {
          printf_instr("FCMP_CLE_D fcc%d, %s: %016f, %s: %016f\n", cd_reg(),
                       FPURegisters::Name(fj_reg()), fj,
                       FPURegisters::Name(fk_reg()), fk);
          set_cf_register(cd_reg(), fj <= fk);
          break;
        }
        case CULE: {
          printf_instr("FCMP_CULE_D fcc%d, %s: %016f, %s: %016f\n", cd_reg(),
                       FPURegisters::Name(fj_reg()), fj,
                       FPURegisters::Name(fk_reg()), fk);
          set_cf_register(cd_reg(),
                          (fj <= fk) || std::isnan(fj) || std::isnan(fk));
          break;
        }
        case CNE: {
          printf_instr("FCMP_CNE_D fcc%d, %s: %016f, %s: %016f\n", cd_reg(),
                       FPURegisters::Name(fj_reg()), fj,
                       FPURegisters::Name(fk_reg()), fk);
          set_cf_register(cd_reg(), (fj < fk) || (fj > fk));
          break;
        }
        case COR: {
          printf_instr("FCMP_COR_D fcc%d, %s: %016f, %s: %016f\n", cd_reg(),
                       FPURegisters::Name(fj_reg()), fj,
                       FPURegisters::Name(fk_reg()), fk);
          set_cf_register(cd_reg(), !std::isnan(fj) && !std::isnan(fk));
          break;
        }
        case CUNE: {
          printf_instr("FCMP_CUNE_D fcc%d, %s: %016f, %s: %016f\n", cd_reg(),
                       FPURegisters::Name(fj_reg()), fj,
                       FPURegisters::Name(fk_reg()), fk);
          set_cf_register(cd_reg(),
                          (fj != fk) || std::isnan(fj) || std::isnan(fk));
          break;
        }
        case SAF:
        case SUN:
        case SEQ:
        case SUEQ:
        case SLT:
        case SULT:
        case SLE:
        case SULE:
        case SNE:
        case SOR:
        case SUNE:
          UNIMPLEMENTED();
        default:
          UNREACHABLE();
      }
      break;
    }
    case FSEL: {
      CHECK_EQ(instr_.Bits(19, 18), 0);
      printf_instr("FSEL fcc%d, %s: %016f, %s: %016f, %s: %016f\n", ca_reg(),
                   FPURegisters::Name(fd_reg()), fd_double(),
                   FPURegisters::Name(fj_reg()), fj_double(),
                   FPURegisters::Name(fk_reg()), fk_double());
      if (ca() == 0) {
        SetFPUDoubleResult(fd_reg(), fj_double());
      } else {
        SetFPUDoubleResult(fd_reg(), fk_double());
      }
      break;
    }
    default:
      UNREACHABLE();
  }
}

void Simulator::DecodeTypeOp14() {
  int64_t alu_out = 0x0;
  int32_t alu32_out = 0x0;

  switch (instr_.Bits(31, 18) << 18) {
    case ALSL: {
      uint8_t sa = sa2() + 1;
      alu32_out =
          (static_cast<int32_t>(rj()) << sa) + static_cast<int32_t>(rk());
      if (instr_.Bit(17) == 0) {
        // ALSL_W
        printf_instr("ALSL_W\t %s: %016lx, %s: %016lx, %s: %016lx, sa2: %d\n",
                     Registers::Name(rd_reg()), rd(), Registers::Name(rj_reg()),
                     rj(), Registers::Name(rk_reg()), rk(), sa2());
        SetResult(rd_reg(), alu32_out);
      } else {
        // ALSL_WU
        printf_instr("ALSL_WU\t %s: %016lx, %s: %016lx, %s: %016lx, sa2: %d\n",
                     Registers::Name(rd_reg()), rd(), Registers::Name(rj_reg()),
                     rj(), Registers::Name(rk_reg()), rk(), sa2());
        SetResult(rd_reg(), static_cast<uint32_t>(alu32_out));
      }
      break;
    }
    case BYTEPICK_W: {
      CHECK_EQ(instr_.Bit(17), 0);
      printf_instr("BYTEPICK_W\t %s: %016lx, %s: %016lx, %s: %016lx, sa2: %d\n",
                   Registers::Name(rd_reg()), rd(), Registers::Name(rj_reg()),
                   rj(), Registers::Name(rk_reg()), rk(), sa2());
      uint8_t sa = sa2() * 8;
      if (sa == 0) {
        alu32_out = static_cast<int32_t>(rk());
      } else {
        int32_t mask = (1 << 31) >> (sa - 1);
        int32_t rk_hi = (static_cast<int32_t>(rk()) & (~mask)) << sa;
        int32_t rj_lo = (static_cast<uint32_t>(rj()) & mask) >> (32 - sa);
        alu32_out = rk_hi | rj_lo;
      }
      SetResult(rd_reg(), static_cast<int64_t>(alu32_out));
      break;
    }
    case BYTEPICK_D: {
      printf_instr("BYTEPICK_D\t %s: %016lx, %s: %016lx, %s: %016lx, sa3: %d\n",
                   Registers::Name(rd_reg()), rd(), Registers::Name(rj_reg()),
                   rj(), Registers::Name(rk_reg()), rk(), sa3());
      uint8_t sa = sa3() * 8;
      if (sa == 0) {
        alu_out = rk();
      } else {
        int64_t mask = (1LL << 63) >> (sa - 1);
        int64_t rk_hi = (rk() & (~mask)) << sa;
        int64_t rj_lo = static_cast<uint64_t>(rj() & mask) >> (64 - sa);
        alu_out = rk_hi | rj_lo;
      }
      SetResult(rd_reg(), alu_out);
      break;
    }
    case ALSL_D: {
      printf_instr("ALSL_D\t %s: %016lx, %s: %016lx, %s: %016lx, sa2: %d\n",
                   Registers::Name(rd_reg()), rd(), Registers::Name(rj_reg()),
                   rj(), Registers::Name(rk_reg()), rk(), sa2());
      CHECK_EQ(instr_.Bit(17), 0);
      uint8_t sa = sa2() + 1;
      alu_out = (rj() << sa) + rk();
      SetResult(rd_reg(), alu_out);
      break;
    }
    case SLLI: {
      DCHECK_EQ(instr_.Bit(17), 0);
      if (instr_.Bits(17, 15) == 0b001) {
        // SLLI_W
        printf_instr("SLLI_W\t %s: %016lx, %s: %016lx, ui5: %d\n",
                     Registers::Name(rd_reg()), rd(), Registers::Name(rj_reg()),
                     rj(), ui5());
        alu32_out = static_cast<int32_t>(rj()) << ui5();
        SetResult(rd_reg(), static_cast<int64_t>(alu32_out));
      } else if ((instr_.Bits(17, 16) == 0b01)) {
        // SLLI_D
        printf_instr("SLLI_D\t %s: %016lx, %s: %016lx, ui6: %d\n",
                     Registers::Name(rd_reg()), rd(), Registers::Name(rj_reg()),
                     rj(), ui6());
        SetResult(rd_reg(), rj() << ui6());
      }
      break;
    }
    case SRLI: {
      DCHECK_EQ(instr_.Bit(17), 0);
      if (instr_.Bits(17, 15) == 0b001) {
        // SRLI_W
        printf_instr("SRLI_W\t %s: %016lx, %s: %016lx, ui5: %d\n",
                     Registers::Name(rd_reg()), rd(), Registers::Name(rj_reg()),
                     rj(), ui5());
        alu32_out = static_cast<uint32_t>(rj()) >> ui5();
        SetResult(rd_reg(), static_cast<int64_t>(alu32_out));
      } else if (instr_.Bits(17, 16) == 0b01) {
        // SRLI_D
        printf_instr("SRLI_D\t %s: %016lx, %s: %016lx, ui6: %d\n",
                     Registers::Name(rd_reg()), rd(), Registers::Name(rj_reg()),
                     rj(), ui6());
        SetResult(rd_reg(), rj_u() >> ui6());
      }
      break;
    }
    case SRAI: {
      DCHECK_EQ(instr_.Bit(17), 0);
      if (instr_.Bits(17, 15) == 0b001) {
        // SRAI_W
        printf_instr("SRAI_W\t %s: %016lx, %s: %016lx, ui5: %d\n",
                     Registers::Name(rd_reg()), rd(), Registers::Name(rj_reg()),
                     rj(), ui5());
        alu32_out = static_cast<int32_t>(rj()) >> ui5();
        SetResult(rd_reg(), static_cast<int64_t>(alu32_out));
      } else if (instr_.Bits(17, 16) == 0b01) {
        // SRAI_D
        printf_instr("SRAI_D\t %s: %016lx, %s: %016lx, ui6: %d\n",
                     Registers::Name(rd_reg()), rd(), Registers::Name(rj_reg()),
                     rj(), ui6());
        SetResult(rd_reg(), rj() >> ui6());
      }
      break;
    }
    case ROTRI: {
      DCHECK_EQ(instr_.Bit(17), 0);
      if (instr_.Bits(17, 15) == 0b001) {
        // ROTRI_W
        printf_instr("ROTRI_W\t %s: %016lx, %s: %016lx, ui5: %d\n",
                     Registers::Name(rd_reg()), rd(), Registers::Name(rj_reg()),
                     rj(), ui5());
        alu32_out = static_cast<int32_t>(
            base::bits::RotateRight32(static_cast<const uint32_t>(rj_u()),
                                      static_cast<const uint32_t>(ui5())));
        SetResult(rd_reg(), static_cast<int64_t>(alu32_out));
      } else if (instr_.Bits(17, 16) == 0b01) {
        // ROTRI_D
        printf_instr("ROTRI_D\t %s: %016lx, %s: %016lx, ui6: %d\n",
                     Registers::Name(rd_reg()), rd(), Registers::Name(rj_reg()),
                     rj(), ui6());
        alu_out =
            static_cast<int64_t>(base::bits::RotateRight64(rj_u(), ui6()));
        SetResult(rd_reg(), alu_out);
        printf_instr("ROTRI, %s, %s, %d\n", Registers::Name(rd_reg()),
                     Registers::Name(rj_reg()), ui6());
      }
      break;
    }
    default:
      UNREACHABLE();
  }
}

void Simulator::DecodeTypeOp17() {
  int64_t alu_out;

  switch (instr_.Bits(31, 15) << 15) {
    case ADD_W: {
      printf_instr("ADD_W\t %s: %016lx, %s, %016lx, %s, %016lx\n",
                   Registers::Name(rd_reg()), rd(), Registers::Name(rj_reg()),
                   rj(), Registers::Name(rk_reg()), rk());
      int32_t alu32_out = static_cast<int32_t>(rj() + rk());
      // Sign-extend result of 32bit operation into 64bit register.
      SetResult(rd_reg(), static_cast<int64_t>(alu32_out));
      break;
    }
    case ADD_D:
      printf_instr("ADD_D\t %s: %016lx, %s, %016lx, %s, %016lx\n",
                   Registers::Name(rd_reg()), rd(), Registers::Name(rj_reg()),
                   rj(), Registers::Name(rk_reg()), rk());
      SetResult(rd_reg(), rj() + rk());
      break;
    case SUB_W: {
      printf_instr("SUB_W\t %s: %016lx, %s, %016lx, %s, %016lx\n",
                   Registers::Name(rd_reg()), rd(), Registers::Name(rj_reg()),
                   rj(), Registers::Name(rk_reg()), rk());
      int32_t alu32_out = static_cast<int32_t>(rj() - rk());
      // Sign-extend result of 32bit operation into 64bit register.
      SetResult(rd_reg(), static_cast<int64_t>(alu32_out));
      break;
    }
    case SUB_D:
      printf_instr("SUB_D\t %s: %016lx, %s, %016lx, %s, %016lx\n",
                   Registers::Name(rd_reg()), rd(), Registers::Name(rj_reg()),
                   rj(), Registers::Name(rk_reg()), rk());
      SetResult(rd_reg(), rj() - rk());
      break;
    case SLT:
      printf_instr("SLT\t %s: %016lx, %s, %016lx, %s, %016lx\n",
                   Registers::Name(rd_reg()), rd(), Registers::Name(rj_reg()),
                   rj(), Registers::Name(rk_reg()), rk());
      SetResult(rd_reg(), rj() < rk() ? 1 : 0);
      break;
    case SLTU:
      printf_instr("SLTU\t %s: %016lx, %s, %016lx, %s, %016lx\n",
                   Registers::Name(rd_reg()), rd(), Registers::Name(rj_reg()),
                   rj(), Registers::Name(rk_reg()), rk());
      SetResult(rd_reg(), rj_u() < rk_u() ? 1 : 0);
      break;
    case MASKEQZ:
      printf_instr("MASKEQZ\t %s: %016lx, %s, %016lx, %s, %016lx\n",
                   Registers::Name(rd_reg()), rd(), Registers::Name(rj_reg()),
                   rj(), Registers::Name(rk_reg()), rk());
      SetResult(rd_reg(), rk() == 0 ? 0 : rj());
      break;
    case MASKNEZ:
      printf_instr("MASKNEZ\t %s: %016lx, %s, %016lx, %s, %016lx\n",
                   Registers::Name(rd_reg()), rd(), Registers::Name(rj_reg()),
                   rj(), Registers::Name(rk_reg()), rk());
      SetResult(rd_reg(), rk() != 0 ? 0 : rj());
      break;
    case NOR:
      printf_instr("NOR\t %s: %016lx, %s, %016lx, %s, %016lx\n",
                   Registers::Name(rd_reg()), rd(), Registers::Name(rj_reg()),
                   rj(), Registers::Name(rk_reg()), rk());
      SetResult(rd_reg(), ~(rj() | rk()));
      break;
    case AND:
      printf_instr("AND\t %s: %016lx, %s, %016lx, %s, %016lx\n",
                   Registers::Name(rd_reg()), rd(), Registers::Name(rj_reg()),
                   rj(), Registers::Name(rk_reg()), rk());
      SetResult(rd_reg(), rj() & rk());
      break;
    case OR:
      printf_instr("OR\t %s: %016lx, %s, %016lx, %s, %016lx\n",
                   Registers::Name(rd_reg()), rd(), Registers::Name(rj_reg()),
                   rj(), Registers::Name(rk_reg()), rk());
      SetResult(rd_reg(), rj() | rk());
      break;
    case XOR:
      printf_instr("XOR\t %s: %016lx, %s, %016lx, %s, %016lx\n",
                   Registers::Name(rd_reg()), rd(), Registers::Name(rj_reg()),
                   rj(), Registers::Name(rk_reg()), rk());
      SetResult(rd_reg(), rj() ^ rk());
      break;
    case ORN:
      printf_instr("ORN\t %s: %016lx, %s, %016lx, %s, %016lx\n",
                   Registers::Name(rd_reg()), rd(), Registers::Name(rj_reg()),
                   rj(), Registers::Name(rk_reg()), rk());
      SetResult(rd_reg(), rj() | (~rk()));
      break;
    case ANDN:
      printf_instr("ANDN\t %s: %016lx, %s, %016lx, %s, %016lx\n",
                   Registers::Name(rd_reg()), rd(), Registers::Name(rj_reg()),
                   rj(), Registers::Name(rk_reg()), rk());
      SetResult(rd_reg(), rj() & (~rk()));
      break;
    case SLL_W:
      printf_instr("SLL_W\t %s: %016lx, %s, %016lx, %s, %016lx\n",
                   Registers::Name(rd_reg()), rd(), Registers::Name(rj_reg()),
                   rj(), Registers::Name(rk_reg()), rk());
      SetResult(rd_reg(), (int32_t)rj() << (rk_u() % 32));
      break;
    case SRL_W: {
      printf_instr("SRL_W\t %s: %016lx, %s, %016lx, %s, %016lx\n",
                   Registers::Name(rd_reg()), rd(), Registers::Name(rj_reg()),
                   rj(), Registers::Name(rk_reg()), rk());
      alu_out = static_cast<int32_t>((uint32_t)rj_u() >> (rk_u() % 32));
      SetResult(rd_reg(), alu_out);
      break;
    }
    case SRA_W:
      printf_instr("SRA_W\t %s: %016lx, %s, %016lx, %s, %016lx\n",
                   Registers::Name(rd_reg()), rd(), Registers::Name(rj_reg()),
                   rj(), Registers::Name(rk_reg()), rk());
      SetResult(rd_reg(), (int32_t)rj() >> (rk_u() % 32));
      break;
    case SLL_D:
      printf_instr("SLL_D\t %s: %016lx, %s, %016lx, %s, %016lx\n",
                   Registers::Name(rd_reg()), rd(), Registers::Name(rj_reg()),
                   rj(), Registers::Name(rk_reg()), rk());
      SetResult(rd_reg(), rj() << (rk_u() % 64));
      break;
    case SRL_D: {
      printf_instr("SRL_D\t %s: %016lx, %s, %016lx, %s, %016lx\n",
                   Registers::Name(rd_reg()), rd(), Registers::Name(rj_reg()),
                   rj(), Registers::Name(rk_reg()), rk());
      alu_out = static_cast<int64_t>(rj_u() >> (rk_u() % 64));
      SetResult(rd_reg(), alu_out);
      break;
    }
    case SRA_D:
      printf_instr("SRA_D\t %s: %016lx, %s, %016lx, %s, %016lx\n",
                   Registers::Name(rd_reg()), rd(), Registers::Name(rj_reg()),
                   rj(), Registers::Name(rk_reg()), rk());
      SetResult(rd_reg(), rj() >> (rk_u() % 64));
      break;
    case ROTR_W: {
      printf_instr("ROTR_W\t %s: %016lx, %s, %016lx, %s, %016lx\n",
                   Registers::Name(rd_reg()), rd(), Registers::Name(rj_reg()),
                   rj(), Registers::Name(rk_reg()), rk());
      alu_out = static_cast<int32_t>(
          base::bits::RotateRight32(static_cast<const uint32_t>(rj_u()),
                                    static_cast<const uint32_t>(rk_u() % 32)));
      SetResult(rd_reg(), alu_out);
      break;
    }
    case ROTR_D: {
      printf_instr("ROTR_D\t %s: %016lx, %s, %016lx, %s, %016lx\n",
                   Registers::Name(rd_reg()), rd(), Registers::Name(rj_reg()),
                   rj(), Registers::Name(rk_reg()), rk());
      alu_out = static_cast<int64_t>(
          base::bits::RotateRight64((rj_u()), (rk_u() % 64)));
      SetResult(rd_reg(), alu_out);
      break;
    }
    case MUL_W: {
      printf_instr("MUL_W\t %s: %016lx, %s, %016lx, %s, %016lx\n",
                   Registers::Name(rd_reg()), rd(), Registers::Name(rj_reg()),
                   rj(), Registers::Name(rk_reg()), rk());
      alu_out = static_cast<int32_t>(rj()) * static_cast<int32_t>(rk());
      SetResult(rd_reg(), alu_out);
      break;
    }
    case MULH_W: {
      printf_instr("MULH_W\t %s: %016lx, %s, %016lx, %s, %016lx\n",
                   Registers::Name(rd_reg()), rd(), Registers::Name(rj_reg()),
                   rj(), Registers::Name(rk_reg()), rk());
      int32_t rj_lo = static_cast<int32_t>(rj());
      int32_t rk_lo = static_cast<int32_t>(rk());
      alu_out = static_cast<int64_t>(rj_lo) * static_cast<int64_t>(rk_lo);
      SetResult(rd_reg(), alu_out >> 32);
      break;
    }
    case MULH_WU: {
      printf_instr("MULH_WU\t %s: %016lx, %s, %016lx, %s, %016lx\n",
                   Registers::Name(rd_reg()), rd(), Registers::Name(rj_reg()),
                   rj(), Registers::Name(rk_reg()), rk());
      uint32_t rj_lo = static_cast<uint32_t>(rj_u());
      uint32_t rk_lo = static_cast<uint32_t>(rk_u());
      alu_out = static_cast<uint64_t>(rj_lo) * static_cast<uint64_t>(rk_lo);
      SetResult(rd_reg(), alu_out >> 32);
      break;
    }
    case MUL_D:
      printf_instr("MUL_D\t %s: %016lx, %s, %016lx, %s, %016lx\n",
                   Registers::Name(rd_reg()), rd(), Registers::Name(rj_reg()),
                   rj(), Registers::Name(rk_reg()), rk());
      SetResult(rd_reg(), rj() * rk());
      break;
    case MULH_D:
      printf_instr("MULH_D\t %s: %016lx, %s, %016lx, %s, %016lx\n",
                   Registers::Name(rd_reg()), rd(), Registers::Name(rj_reg()),
                   rj(), Registers::Name(rk_reg()), rk());
      SetResult(rd_reg(), base::bits::SignedMulHigh64(rj(), rk()));
      break;
    case MULH_DU:
      printf_instr("MULH_DU\t %s: %016lx, %s, %016lx, %s, %016lx\n",
                   Registers::Name(rd_reg()), rd(), Registers::Name(rj_reg()),
                   rj(), Registers::Name(rk_reg()), rk());
      SetResult(rd_reg(), base::bits::UnsignedMulHigh64(rj_u(), rk_u()));
      break;
    case MULW_D_W: {
      printf_instr("MULW_D_W\t %s: %016lx, %s, %016lx, %s, %016lx\n",
                   Registers::Name(rd_reg()), rd(), Registers::Name(rj_reg()),
                   rj(), Registers::Name(rk_reg()), rk());
      int64_t rj_i32 = static_cast<int32_t>(rj());
      int64_t rk_i32 = static_cast<int32_t>(rk());
      SetResult(rd_reg(), rj_i32 * rk_i32);
      break;
    }
    case MULW_D_WU: {
      printf_instr("MULW_D_WU\t %s: %016lx, %s, %016lx, %s, %016lx\n",
                   Registers::Name(rd_reg()), rd(), Registers::Name(rj_reg()),
                   rj(), Registers::Name(rk_reg()), rk());
      uint64_t rj_u32 = static_cast<uint32_t>(rj_u());
      uint64_t rk_u32 = static_cast<uint32_t>(rk_u());
      SetResult(rd_reg(), rj_u32 * rk_u32);
      break;
    }
    case DIV_W: {
      printf_instr("DIV_W\t %s: %016lx, %s, %016lx, %s, %016lx\n",
                   Registers::Name(rd_reg()), rd(), Registers::Name(rj_reg()),
                   rj(), Registers::Name(rk_reg()), rk());
      int32_t rj_i32 = static_cast<int32_t>(rj());
      int32_t rk_i32 = static_cast<int32_t>(rk());
      if (rj_i32 == INT_MIN && rk_i32 == -1) {
        SetResult(rd_reg(), INT_MIN);
      } else if (rk_i32 != 0) {
        SetResult(rd_reg(), rj_i32 / rk_i32);
      }
      break;
    }
    case MOD_W: {
      printf_instr("MOD_W\t %s: %016lx, %s, %016lx, %s, %016lx\n",
                   Registers::Name(rd_reg()), rd(), Registers::Name(rj_reg()),
                   rj(), Registers::Name(rk_reg()), rk());
      int32_t rj_i32 = static_cast<int32_t>(rj());
      int32_t rk_i32 = static_cast<int32_t>(rk());
      if (rj_i32 == INT_MIN && rk_i32 == -1) {
        SetResult(rd_reg(), 0);
      } else if (rk_i32 != 0) {
        SetResult(rd_reg(), rj_i32 % rk_i32);
      }
      break;
    }
    case DIV_WU: {
      printf_instr("DIV_WU\t %s: %016lx, %s, %016lx, %s, %016lx\n",
                   Registers::Name(rd_reg()), rd(), Registers::Name(rj_reg()),
                   rj(), Registers::Name(rk_reg()), rk());
      uint32_t rj_u32 = static_cast<uint32_t>(rj());
      uint32_t rk_u32 = static_cast<uint32_t>(rk());
      if (rk_u32 != 0) {
        SetResult(rd_reg(), static_cast<int32_t>(rj_u32 / rk_u32));
      }
      break;
    }
    case MOD_WU: {
      printf_instr("MOD_WU\t %s: %016lx, %s, %016lx, %s, %016lx\n",
                   Registers::Name(rd_reg()), rd(), Registers::Name(rj_reg()),
                   rj(), Registers::Name(rk_reg()), rk());
      uint32_t rj_u32 = static_cast<uint32_t>(rj());
      uint32_t rk_u32 = static_cast<uint32_t>(rk());
      if (rk_u32 != 0) {
        SetResult(rd_reg(), static_cast<int32_t>(rj_u32 % rk_u32));
      }
      break;
    }
    case DIV_D: {
      printf_instr("DIV_D\t %s: %016lx, %s, %016lx, %s, %016lx\n",
                   Registers::Name(rd_reg()), rd(), Registers::Name(rj_reg()),
                   rj(), Registers::Name(rk_reg()), rk());
      if (rj() == LONG_MIN && rk() == -1) {
        SetResult(rd_reg(), LONG_MIN);
      } else if (rk() != 0) {
        SetResult(rd_reg(), rj() / rk());
      }
      break;
    }
    case MOD_D: {
      printf_instr("MOD_D\t %s: %016lx, %s, %016lx, %s, %016lx\n",
                   Registers::Name(rd_reg()), rd(), Registers::Name(rj_reg()),
                   rj(), Registers::Name(rk_reg()), rk());
      if (rj() == LONG_MIN && rk() == -1) {
        SetResult(rd_reg(), 0);
      } else if (rk() != 0) {
        SetResult(rd_reg(), rj() % rk());
      }
      break;
    }
    case DIV_DU: {
      printf_instr("DIV_DU\t %s: %016lx, %s, %016lx, %s, %016lx\n",
                   Registers::Name(rd_reg()), rd(), Registers::Name(rj_reg()),
                   rj(), Registers::Name(rk_reg()), rk());
      if (rk_u() != 0) {
        SetResult(rd_reg(), static_cast<int64_t>(rj_u() / rk_u()));
      }
      break;
    }
    case MOD_DU: {
      printf_instr("MOD_DU\t %s: %016lx, %s, %016lx, %s, %016lx\n",
                   Registers::Name(rd_reg()), rd(), Registers::Name(rj_reg()),
                   rj(), Registers::Name(rk_reg()), rk());
      if (rk_u() != 0) {
        SetResult(rd_reg(), static_cast<int64_t>(rj_u() % rk_u()));
      }
      break;
    }
    case BREAK:
      printf_instr("BREAK\t code: %x\n", instr_.Bits(14, 0));
      SoftwareInterrupt();
      break;
    case FADD_S: {
      printf_instr("FADD_S\t %s: %016f, %s, %016f, %s, %016f\n",
                   FPURegisters::Name(fd_reg()), fd_float(),
                   FPURegisters::Name(fj_reg()), fj_float(),
                   FPURegisters::Name(fk_reg()), fk_float());
      SetFPUFloatResult(
          fd_reg(),
          FPUCanonalizeOperation([](float lhs, float rhs) { return lhs + rhs; },
                                 fj_float(), fk_float()));
      break;
    }
    case FADD_D: {
      printf_instr("FADD_D\t %s: %016f, %s, %016f, %s, %016f\n",
                   FPURegisters::Name(fd_reg()), fd_double(),
                   FPURegisters::Name(fj_reg()), fj_double(),
                   FPURegisters::Name(fk_reg()), fk_double());
      SetFPUDoubleResult(fd_reg(),
                         FPUCanonalizeOperation(
                             [](double lhs, double rhs) { return lhs + rhs; },
                             fj_double(), fk_double()));
      break;
    }
    case FSUB_S: {
      printf_instr("FSUB_S\t %s: %016f, %s, %016f, %s, %016f\n",
                   FPURegisters::Name(fd_reg()), fd_float(),
                   FPURegisters::Name(fj_reg()), fj_float(),
                   FPURegisters::Name(fk_reg()), fk_float());
      SetFPUFloatResult(
          fd_reg(),
          FPUCanonalizeOperation([](float lhs, float rhs) { return lhs - rhs; },
                                 fj_float(), fk_float()));
      break;
    }
    case FSUB_D: {
      printf_instr("FSUB_D\t %s: %016f, %s, %016f, %s, %016f\n",
                   FPURegisters::Name(fd_reg()), fd_double(),
                   FPURegisters::Name(fj_reg()), fj_double(),
                   FPURegisters::Name(fk_reg()), fk_double());
      SetFPUDoubleResult(fd_reg(),
                         FPUCanonalizeOperation(
                             [](double lhs, double rhs) { return lhs - rhs; },
                             fj_double(), fk_double()));
      break;
    }
    case FMUL_S: {
      printf_instr("FMUL_S\t %s: %016f, %s, %016f, %s, %016f\n",
                   FPURegisters::Name(fd_reg()), fd_float(),
                   FPURegisters::Name(fj_reg()), fj_float(),
                   FPURegisters::Name(fk_reg()), fk_float());
      SetFPUFloatResult(
          fd_reg(),
          FPUCanonalizeOperation([](float lhs, float rhs) { return lhs * rhs; },
                                 fj_float(), fk_float()));
      break;
    }
    case FMUL_D: {
      printf_instr("FMUL_D\t %s: %016f, %s, %016f, %s, %016f\n",
                   FPURegisters::Name(fd_reg()), fd_double(),
                   FPURegisters::Name(fj_reg()), fj_double(),
                   FPURegisters::Name(fk_reg()), fk_double());
      SetFPUDoubleResult(fd_reg(),
                         FPUCanonalizeOperation(
                             [](double lhs, double rhs) { return lhs * rhs; },
                             fj_double(), fk_double()));
      break;
    }
    case FDIV_S: {
      printf_instr("FDIV_S\t %s: %016f, %s, %016f, %s, %016f\n",
                   FPURegisters::Name(fd_reg()), fd_float(),
                   FPURegisters::Name(fj_reg()), fj_float(),
                   FPURegisters::Name(fk_reg()), fk_float());
      SetFPUFloatResult(
          fd_reg(),
          FPUCanonalizeOperation([](float lhs, float rhs) { return lhs / rhs; },
                                 fj_float(), fk_float()));
      break;
    }
    case FDIV_D: {
      printf_instr("FDIV_D\t %s: %016f, %s, %016f, %s, %016f\n",
                   FPURegisters::Name(fd_reg()), fd_double(),
                   FPURegisters::Name(fj_reg()), fj_double(),
                   FPURegisters::Name(fk_reg()), fk_double());
      SetFPUDoubleResult(fd_reg(),
                         FPUCanonalizeOperation(
                             [](double lhs, double rhs) { return lhs / rhs; },
                             fj_double(), fk_double()));
      break;
    }
    case FMAX_S:
      printf_instr("FMAX_S\t %s: %016f, %s, %016f, %s, %016f\n",
                   FPURegisters::Name(fd_reg()), fd_float(),
                   FPURegisters::Name(fj_reg()), fj_float(),
                   FPURegisters::Name(fk_reg()), fk_float());
      SetFPUFloatResult(fd_reg(), FPUMax(fk_float(), fj_float()));
      break;
    case FMAX_D:
      printf_instr("FMAX_D\t %s: %016f, %s, %016f, %s, %016f\n",
                   FPURegisters::Name(fd_reg()), fd_double(),
                   FPURegisters::Name(fj_reg()), fj_double(),
                   FPURegisters::Name(fk_reg()), fk_double());
      SetFPUDoubleResult(fd_reg(), FPUMax(fk_double(), fj_double()));
      break;
    case FMIN_S:
      printf_instr("FMIN_S\t %s: %016f, %s, %016f, %s, %016f\n",
                   FPURegisters::Name(fd_reg()), fd_float(),
                   FPURegisters::Name(fj_reg()), fj_float(),
                   FPURegisters::Name(fk_reg()), fk_float());
      SetFPUFloatResult(fd_reg(), FPUMin(fk_float(), fj_float()));
      break;
    case FMIN_D:
      printf_instr("FMIN_D\t %s: %016f, %s, %016f, %s, %016f\n",
                   FPURegisters::Name(fd_reg()), fd_double(),
                   FPURegisters::Name(fj_reg()), fj_double(),
                   FPURegisters::Name(fk_reg()), fk_double());
      SetFPUDoubleResult(fd_reg(), FPUMin(fk_double(), fj_double()));
      break;
    case FMAXA_S:
      printf_instr("FMAXA_S\t %s: %016f, %s, %016f, %s, %016f\n",
                   FPURegisters::Name(fd_reg()), fd_float(),
                   FPURegisters::Name(fj_reg()), fj_float(),
                   FPURegisters::Name(fk_reg()), fk_float());
      SetFPUFloatResult(fd_reg(), FPUMaxA(fk_float(), fj_float()));
      break;
    case FMAXA_D:
      printf_instr("FMAXA_D\t %s: %016f, %s, %016f, %s, %016f\n",
                   FPURegisters::Name(fd_reg()), fd_double(),
                   FPURegisters::Name(fj_reg()), fj_double(),
                   FPURegisters::Name(fk_reg()), fk_double());
      SetFPUDoubleResult(fd_reg(), FPUMaxA(fk_double(), fj_double()));
      break;
    case FMINA_S:
      printf_instr("FMINA_S\t %s: %016f, %s, %016f, %s, %016f\n",
                   FPURegisters::Name(fd_reg()), fd_float(),
                   FPURegisters::Name(fj_reg()), fj_float(),
                   FPURegisters::Name(fk_reg()), fk_float());
      SetFPUFloatResult(fd_reg(), FPUMinA(fk_float(), fj_float()));
      break;
    case FMINA_D:
      printf_instr("FMINA_D\t %s: %016f, %s, %016f, %s, %016f\n",
                   FPURegisters::Name(fd_reg()), fd_double(),
                   FPURegisters::Name(fj_reg()), fj_double(),
                   FPURegisters::Name(fk_reg()), fk_double());
      SetFPUDoubleResult(fd_reg(), FPUMinA(fk_double(), fj_double()));
      break;
    case LDX_B:
      printf_instr("LDX_B\t %s: %016lx, %s, %016lx, %s, %016lx\n",
                   Registers::Name(rd_reg()), rd(), Registers::Name(rj_reg()),
                   rj(), Registers::Name(rk_reg()), rk());
      if (!ProbeMemory(rj() + rk(), sizeof(int8_t))) return;
      set_register(rd_reg(), ReadB(rj() + rk()));
      break;
    case LDX_H:
      printf_instr("LDX_H\t %s: %016lx, %s, %016lx, %s, %016lx\n",
                   Registers::Name(rd_reg()), rd(), Registers::Name(rj_reg()),
                   rj(), Registers::Name(rk_reg()), rk());
      if (!ProbeMemory(rj() + rk(), sizeof(int16_t))) return;
      set_register(rd_reg(), ReadH(rj() + rk(), instr_.instr()));
      break;
    case LDX_W:
      printf_instr("LDX_W\t %s: %016lx, %s, %016lx, %s, %016lx\n",
                   Registers::Name(rd_reg()), rd(), Registers::Name(rj_reg()),
                   rj(), Registers::Name(rk_reg()), rk());
      if (!ProbeMemory(rj() + rk(), sizeof(int32_t))) return;
      set_register(rd_reg(), ReadW(rj() + rk(), instr_.instr()));
      break;
    case LDX_D:
      printf_instr("LDX_D\t %s: %016lx, %s, %016lx, %s, %016lx\n",
                   Registers::Name(rd_reg()), rd(), Registers::Name(rj_reg()),
                   rj(), Registers::Name(rk_reg()), rk());
      if (!ProbeMemory(rj() + rk(), sizeof(int64_t))) return;
      set_register(rd_reg(), Read2W(rj() + rk(), instr_.instr()));
      break;
    case STX_B:
      printf_instr("STX_B\t %s: %016lx, %s, %016lx, %s, %016lx\n",
                   Registers::Name(rd_reg()), rd(), Registers::Name(rj_reg()),
                   rj(), Registers::Name(rk_reg()), rk());
      if (!ProbeMemory(rj() + rk(), sizeof(int8_t))) return;
      WriteB(rj() + rk(), static_cast<int8_t>(rd()));
      break;
    case STX_H:
      printf_instr("STX_H\t %s: %016lx, %s, %016lx, %s, %016lx\n",
                   Registers::Name(rd_reg()), rd(), Registers::Name(rj_reg()),
                   rj(), Registers::Name(rk_reg()), rk());
      if (!ProbeMemory(rj() + rk(), sizeof(int16_t))) return;
      WriteH(rj() + rk(), static_cast<int16_t>(rd()), instr_.instr());
      break;
    case STX_W:
      printf_instr("STX_W\t %s: %016lx, %s, %016lx, %s, %016lx\n",
                   Registers::Name(rd_reg()), rd(), Registers::Name(rj_reg()),
                   rj(), Registers::Name(rk_reg()), rk());
      if (!ProbeMemory(rj() + rk(), sizeof(int32_t))) return;
      WriteW(rj() + rk(), static_cast<int32_t>(rd()), instr_.instr());
      break;
    case STX_D:
      printf_instr("STX_D\t %s: %016lx, %s, %016lx, %s, %016lx\n",
                   Registers::Name(rd_reg()), rd(), Registers::Name(rj_reg()),
                   rj(), Registers::Name(rk_reg()), rk());
      if (!ProbeMemory(rj() + rk(), sizeof(int64_t))) return;
      Write2W(rj() + rk(), rd(), instr_.instr());
      break;
    case LDX_BU:
      printf_instr("LDX_BU\t %s: %016lx, %s, %016lx, %s, %016lx\n",
                   Registers::Name(rd_reg()), rd(), Registers::Name(rj_reg()),
                   rj(), Registers::Name(rk_reg()), rk());
      if (!ProbeMemory(rj() + rk(), sizeof(uint8_t))) return;
      set_register(rd_reg(), ReadBU(rj() + rk()));
      break;
    case LDX_HU:
      printf_instr("LDX_HU\t %s: %016lx, %s, %016lx, %s, %016lx\n",
                   Registers::Name(rd_reg()), rd(), Registers::Name(rj_reg()),
                   rj(), Registers::Name(rk_reg()), rk());
      if (!ProbeMemory(rj() + rk(), sizeof(uint16_t))) return;
      set_register(rd_reg(), ReadHU(rj() + rk(), instr_.instr()));
      break;
    case LDX_WU:
      printf_instr("LDX_WU\t %s: %016lx, %s, %016lx, %s, %016lx\n",
                   Registers::Name(rd_reg()), rd(), Registers::Name(rj_reg()),
                   rj(), Registers::Name(rk_reg()), rk());
      if (!ProbeMemory(rj() + rk(), sizeof(uint32_t))) return;
      set_register(rd_reg(), ReadWU(rj() + rk(), instr_.instr()));
      break;
    case FLDX_S:
      printf_instr("FLDX_S\t %s: %016f, %s: %016lx, %s: %016lx\n",
                   FPURegisters::Name(fd_reg()), fd_float(),
                   Registers::Name(rj_reg()), rj(), Registers::Name(rk_reg()),
                   rk());
      if (!ProbeMemory(rj() + rk(), sizeof(float))) return;
      set_fpu_register(fd_reg(), kFPUInvalidResult);  // Trash upper 32 bits.
      set_fpu_register_word(fd_reg(),
                            ReadW(rj() + rk(), instr_.instr(), FLOAT_DOUBLE));
      break;
    case FLDX_D:
      printf_instr("FLDX_D\t %s: %016f, %s: %016lx, %s: %016lx\n",
                   FPURegisters::Name(fd_reg()), fd_double(),
                   Registers::Name(rj_reg()), rj(), Registers::Name(rk_reg()),
                   rk());
      if (!ProbeMemory(rj() + rk(), sizeof(double))) return;
      set_fpu_register_double(fd_reg(), ReadD(rj() + rk(), instr_.instr()));
      break;
    case FSTX_S:
      printf_instr("FSTX_S\t %s: %016f, %s: %016lx, %s: %016lx\n",
                   FPURegisters::Name(fd_reg()), fd_float(),
                   Registers::Name(rj_reg()), rj(), Registers::Name(rk_reg()),
                   rk());
      if (!ProbeMemory(rj() + rk(), sizeof(float))) return;
      WriteW(rj() + rk(), static_cast<int32_t>(get_fpu_register(fd_reg())),
             instr_.instr());
      break;
    case FSTX_D:
      printf_instr("FSTX_D\t %s: %016f, %s: %016lx, %s: %016lx\n",
                   FPURegisters::Name(fd_reg()), fd_double(),
                   Registers::Name(rj_reg()), rj(), Registers::Name(rk_reg()),
                   rk());
      if (!ProbeMemory(rj() + rk(), sizeof(double))) return;
      WriteD(rj() + rk(), get_fpu_register_double(fd_reg()), instr_.instr());
      break;
    case AMSWAP_W:
      printf("Sim UNIMPLEMENTED: AMSWAP_W\n");
      UNIMPLEMENTED();
    case AMSWAP_D:
      printf("Sim UNIMPLEMENTED: AMSWAP_D\n");
      UNIMPLEMENTED();
    case AMADD_W:
      printf("Sim UNIMPLEMENTED: AMADD_W\n");
      UNIMPLEMENTED();
    case AMADD_D:
      printf("Sim UNIMPLEMENTED: AMADD_D\n");
      UNIMPLEMENTED();
    case AMAND_W:
      printf("Sim UNIMPLEMENTED: AMAND_W\n");
      UNIMPLEMENTED();
    case AMAND_D:
      printf("Sim UNIMPLEMENTED: AMAND_D\n");
      UNIMPLEMENTED();
    case AMOR_W:
      printf("Sim UNIMPLEMENTED: AMOR_W\n");
      UNIMPLEMENTED();
    case AMOR_D:
      printf("Sim UNIMPLEMENTED: AMOR_D\n");
      UNIMPLEMENTED();
    case AMXOR_W:
      printf("Sim UNIMPLEMENTED: AMXOR_W\n");
      UNIMPLEMENTED();
    case AMXOR_D:
      printf("Sim UNIMPLEMENTED: AMXOR_D\n");
      UNIMPLEMENTED();
    case AMMAX_W:
      printf("Sim UNIMPLEMENTED: AMMAX_W\n");
      UNIMPLEMENTED();
    case AMMAX_D:
      printf("Sim UNIMPLEMENTED: AMMAX_D\n");
      UNIMPLEMENTED();
    case AMMIN_W:
      printf("Sim UNIMPLEMENTED: AMMIN_W\n");
      UNIMPLEMENTED();
    case AMMIN_D:
      printf("Sim UNIMPLEMENTED: AMMIN_D\n");
      UNIMPLEMENTED();
    case AMMAX_WU:
      printf("Sim UNIMPLEMENTED: AMMAX_WU\n");
      UNIMPLEMENTED();
    case AMMAX_DU:
      printf("Sim UNIMPLEMENTED: AMMAX_DU\n");
      UNIMPLEMENTED();
    case AMMIN_WU:
      printf("Sim UNIMPLEMENTED: AMMIN_WU\n");
      UNIMPLEMENTED();
    case AMMIN_DU:
      printf("Sim UNIMPLEMENTED: AMMIN_DU\n");
      UNIMPLEMENTED();
    case AMSWAP_DB_W: {
      printf_instr("AMSWAP_DB_W:\t %s: %016lx, %s, %016lx, %s, %016lx\n",
                   Registers::Name(rd_reg()), rd(), Registers::Name(rk_reg()),
                   rk(), Registers::Name(rj_reg()), rj());
      if (!ProbeMemory(rj(), sizeof(int32_t))) return;
      int32_t success = 0;
      do {
        {
          base::MutexGuard lock_guard(&GlobalMonitor::Get()->mutex);
          set_register(rd_reg(), ReadW(rj(), instr_.instr()));
          local_monitor_.NotifyLoadLinked(rj(), TransactionSize::Word);
          GlobalMonitor::Get()->NotifyLoadLinked_Locked(
              rj(), &global_monitor_thread_);
        }
        WriteConditionalW(rj(), static_cast<int32_t>(rk()), instr_.instr(),
                          &success);
      } while (!success);
    } break;
    case AMSWAP_DB_D: {
      printf_instr("AMSWAP_DB_D:\t %s: %016lx, %s, %016lx, %s, %016lx\n",
                   Registers::Name(rd_reg()), rd(), Registers::Name(rk_reg()),
                   rk(), Registers::Name(rj_reg()), rj());
      if (!ProbeMemory(rj(), sizeof(int64_t))) return;
      int32_t success = 0;
      do {
        {
          base::MutexGuard lock_guard(&GlobalMonitor::Get()->mutex);
          set_register(rd_reg(), Read2W(rj(), instr_.instr()));
          local_monitor_.NotifyLoadLinked(rj(), TransactionSize::DoubleWord);
          GlobalMonitor::Get()->NotifyLoadLinked_Locked(
              rj(), &global_monitor_thread_);
        }
        WriteConditional2W(rj(), rk(), instr_.instr(), &success);
      } while (!success);
    } break;
    case AMADD_DB_W: {
      printf_instr("AMADD_DB_W:\t %s: %016lx, %s, %016lx, %s, %016lx\n",
                   Registers::Name(rd_reg()), rd(), Registers::Name(rk_reg()),
                   rk(), Registers::Name(rj_reg()), rj());
      if (!ProbeMemory(rj(), sizeof(int32_t))) return;
      int32_t success = 0;
      do {
        {
          base::MutexGuard lock_guard(&GlobalMonitor::Get()->mutex);
          set_register(rd_reg(), ReadW(rj(), instr_.instr()));
          local_monitor_.NotifyLoadLinked(rj(), TransactionSize::Word);
          GlobalMonitor::Get()->NotifyLoadLinked_Locked(
              rj(), &global_monitor_thread_);
        }
        WriteConditionalW(rj(),
                          static_cast<int32_t>(static_cast<int32_t>(rk()) +
                                               static_cast<int32_t>(rd())),
                          instr_.instr(), &success);
      } while (!success);
    } break;
    case AMADD_DB_D: {
      printf_instr("AMADD_DB_D:\t %s: %016lx, %s, %016lx, %s, %016lx\n",
                   Registers::Name(rd_reg()), rd(), Registers::Name(rk_reg()),
                   rk(), Registers::Name(rj_reg()), rj());
      if (!ProbeMemory(rj(), sizeof(int64_t))) return;
      int32_t success = 0;
      do {
        {
          base::MutexGuard lock_guard(&GlobalMonitor::Get()->mutex);
          set_register(rd_reg(), Read2W(rj(), instr_.instr()));
          local_monitor_.NotifyLoadLinked(rj(), TransactionSize::DoubleWord);
          GlobalMonitor::Get()->NotifyLoadLinked_Locked(
              rj(), &global_monitor_thread_);
        }
        WriteConditional2W(rj(), rk() + rd(), instr_.instr(), &success);
      } while (!success);
    } break;
    case AMAND_DB_W: {
      printf_instr("AMAND_DB_W:\t %s: %016lx, %s, %016lx, %s, %016lx\n",
                   Registers::Name(rd_reg()), rd(), Registers::Name(rk_reg()),
                   rk(), Registers::Name(rj_reg()), rj());
      if (!ProbeMemory(rj(), sizeof(int32_t))) return;
      int32_t success = 0;
      do {
        {
          base::MutexGuard lock_guard(&GlobalMonitor::Get()->mutex);
          set_register(rd_reg(), ReadW(rj(), instr_.instr()));
          local_monitor_.NotifyLoadLinked(rj(), TransactionSize::Word);
          GlobalMonitor::Get()->NotifyLoadLinked_Locked(
              rj(), &global_monitor_thread_);
        }
        WriteConditionalW(rj(),
                          static_cast<int32_t>(static_cast<int32_t>(rk()) &
                                               static_cast<int32_t>(rd())),
                          instr_.instr(), &success);
      } while (!success);
    } break;
    case AMAND_DB_D: {
      printf_instr("AMAND_DB_D:\t %s: %016lx, %s, %016lx, %s, %016lx\n",
                   Registers::Name(rd_reg()), rd(), Registers::Name(rk_reg()),
                   rk(), Registers::Name(rj_reg()), rj());
      if (!ProbeMemory(rj(), sizeof(int64_t))) return;
      int32_t success = 0;
      do {
        {
          base::MutexGuard lock_guard(&GlobalMonitor::Get()->mutex);
          set_register(rd_reg(), Read2W(rj(), instr_.instr()));
          local_monitor_.NotifyLoadLinked(rj(), TransactionSize::DoubleWord);
          GlobalMonitor::Get()->NotifyLoadLinked_Locked(
              rj(), &global_monitor_thread_);
        }
        WriteConditional2W(rj(), rk() & rd(), instr_.instr(), &success);
      } while (!success);
    } break;
    case AMOR_DB_W: {
      printf_instr("AMOR_DB_W:\t %s: %016lx, %s, %016lx, %s, %016lx\n",
                   Registers::Name(rd_reg()), rd(), Registers::Name(rk_reg()),
                   rk(), Registers::Name(rj_reg()), rj());
      if (!ProbeMemory(rj(), sizeof(int32_t))) return;
      int32_t success = 0;
      do {
        {
          base::MutexGuard lock_guard(&GlobalMonitor::Get()->mutex);
          set_register(rd_reg(), ReadW(rj(), instr_.instr()));
          local_monitor_.NotifyLoadLinked(rj(), TransactionSize::Word);
          GlobalMonitor::Get()->NotifyLoadLinked_Locked(
              rj(), &global_monitor_thread_);
        }
        WriteConditionalW(rj(),
                          static_cast<int32_t>(static_cast<int32_t>(rk()) |
                                               static_cast<int32_t>(rd())),
                          instr_.instr(), &success);
      } while (!success);
    } break;
    case AMOR_DB_D: {
      printf_instr("AMOR_DB_D:\t %s: %016lx, %s, %016lx, %s, %016lx\n",
                   Registers::Name(rd_reg()), rd(), Registers::Name(rk_reg()),
                   rk(), Registers::Name(rj_reg()), rj());
      if (!ProbeMemory(rj(), sizeof(int64_t))) return;
      int32_t success = 0;
      do {
        {
          base::MutexGuard lock_guard(&GlobalMonitor::Get()->mutex);
          set_register(rd_reg(), Read2W(rj(), instr_.instr()));
          local_monitor_.NotifyLoadLinked(rj(), TransactionSize::DoubleWord);
          GlobalMonitor::Get()->NotifyLoadLinked_Locked(
              rj(), &global_monitor_thread_);
        }
        WriteConditional2W(rj(), rk() | rd(), instr_.instr(), &success);
      } while (!success);
    } break;
    case AMXOR_DB_W: {
      printf_instr("AMXOR_DB_W:\t %s: %016lx, %s, %016lx, %s, %016lx\n",
                   Registers::Name(rd_reg()), rd(), Registers::Name(rk_reg()),
                   rk(), Registers::Name(rj_reg()), rj());
      if (!ProbeMemory(rj(), sizeof(int32_t))) return;
      int32_t success = 0;
      do {
        {
          base::MutexGuard lock_guard(&GlobalMonitor::Get()->mutex);
          set_register(rd_reg(), ReadW(rj(), instr_.instr()));
          local_monitor_.NotifyLoadLinked(rj(), TransactionSize::Word);
          GlobalMonitor::Get()->NotifyLoadLinked_Locked(
              rj(), &global_monitor_thread_);
        }
        WriteConditionalW(rj(),
                          static_cast<int32_t>(static_cast<int32_t>(rk()) ^
                                               static_cast<int32_t>(rd())),
                          instr_.instr(), &success);
      } while (!success);
    } break;
    case AMXOR_DB_D: {
      printf_instr("AMXOR_DB_D:\t %s: %016lx, %s, %016lx, %s, %016lx\n",
                   Registers::Name(rd_reg()), rd(), Registers::Name(rk_reg()),
                   rk(), Registers::Name(rj_reg()), rj());
      if (!ProbeMemory(rj(), sizeof(int64_t))) return;
      int32_t success = 0;
      do {
        {
          base::MutexGuard lock_guard(&GlobalMonitor::Get()->mutex);
          set_register(rd_reg(), Read2W(rj(), instr_.instr()));
          local_monitor_.NotifyLoadLinked(rj(), TransactionSize::DoubleWord);
          GlobalMonitor::Get()->NotifyLoadLinked_Locked(
              rj(), &global_monitor_thread_);
        }
        WriteConditional2W(rj(), rk() ^ rd(), instr_.instr(), &success);
      } while (!success);
    } break;
    case AMMAX_DB_W:
      printf("Sim UNIMPLEMENTED: AMMAX_DB_W\n");
      UNIMPLEMENTED();
    case AMMAX_DB_D:
      printf("Sim UNIMPLEMENTED: AMMAX_DB_D\n");
      UNIMPLEMENTED();
    case AMMIN_DB_W:
      printf("Sim UNIMPLEMENTED: AMMIN_DB_W\n");
      UNIMPLEMENTED();
    case AMMIN_DB_D:
      printf("Sim UNIMPLEMENTED: AMMIN_DB_D\n");
      UNIMPLEMENTED();
    case AMMAX_DB_WU:
      printf("Sim UNIMPLEMENTED: AMMAX_DB_WU\n");
      UNIMPLEMENTED();
    case AMMAX_DB_DU:
      printf("Sim UNIMPLEMENTED: AMMAX_DB_DU\n");
      UNIMPLEMENTED();
    case AMMIN_DB_WU:
      printf("Sim UNIMPLEMENTED: AMMIN_DB_WU\n");
      UNIMPLEMENTED();
    case AMMIN_DB_DU:
      printf("Sim UNIMPLEMENTED: AMMIN_DB_DU\n");
      UNIMPLEMENTED();
    case DBAR:
      printf_instr("DBAR\n");
      break;
    case IBAR:
      printf("Sim UNIMPLEMENTED: IBAR\n");
      UNIMPLEMENTED();
    case FSCALEB_S:
      printf("Sim UNIMPLEMENTED: FSCALEB_S\n");
      UNIMPLEMENTED();
    case FSCALEB_D:
      printf("Sim UNIMPLEMENTED: FSCALEB_D\n");
      UNIMPLEMENTED();
    case FCOPYSIGN_S: {
      printf_instr("FCOPYSIGN_S\t %s: %016f, %s, %016f, %s, %016f\n",
                   FPURegisters::Name(fd_reg()), fd_float(),
                   FPURegisters::Name(fj_reg()), fj_float(),
                   FPURegisters::Name(fk_reg()), fk_float());
      SetFPUFloatResult(fd_reg(), std::copysign(fj_float(), fk_float()));
    } break;
    case FCOPYSIGN_D: {
      printf_instr("FCOPYSIGN_d\t %s: %016f, %s, %016f, %s, %016f\n",
                   FPURegisters::Name(fd_reg()), fd_double(),
                   FPURegisters::Name(fj_reg()), fj_double(),
                   FPURegisters::Name(fk_reg()), fk_double());
      SetFPUDoubleResult(fd_reg(), std::copysign(fj_double(), fk_double()));
    } break;
    default:
      UNREACHABLE();
  }
}

void Simulator::DecodeTypeOp22() {
  int64_t alu_out;

  switch (instr_.Bits(31, 10) << 10) {
    case CLZ_W: {
      printf_instr("CLZ_W\t %s: %016lx, %s, %016lx\n",
                   Registers::Name(rd_reg()), rd(), Registers::Name(rj_reg()),
                   rj());
      alu_out = base::bits::CountLeadingZeros32(static_cast<int32_t>(rj_u()));
      SetResult(rd_reg(), alu_out);
      break;
    }
    case CTZ_W: {
      printf_instr("CTZ_W\t %s: %016lx, %s, %016lx\n",
                   Registers::Name(rd_reg()), rd(), Registers::Name(rj_reg()),
                   rj());
      alu_out = base::bits::CountTrailingZeros32(static_cast<int32_t>(rj_u()));
      SetResult(rd_reg(), alu_out);
      break;
    }
    case CLZ_D: {
      printf_instr("CLZ_D\t %s: %016lx, %s, %016lx\n",
                   Registers::Name(rd_reg()), rd(), Registers::Name(rj_reg()),
                   rj());
      alu_out = base::bits::CountLeadingZeros64(static_cast<int64_t>(rj_u()));
      SetResult(rd_reg(), alu_out);
      break;
    }
    case CTZ_D: {
      printf_instr("CTZ_D\t %s: %016lx, %s, %016lx\n",
                   Registers::Name(rd_reg()), rd(), Registers::Name(rj_reg()),
                   rj());
      alu_out = base::bits::CountTrailingZeros64(static_cast<int64_t>(rj_u()));
      SetResult(rd_reg(), alu_out);
      break;
    }
    case REVB_2H: {
      printf_instr("REVB_2H\t %s: %016lx, %s, %016lx\n",
                   Registers::Name(rd_reg()), rd(), Registers::Name(rj_reg()),
                   rj());
      uint32_t input = static_cast<uint32_t>(rj());
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

      alu_out = static_cast<int64_t>(static_cast<int32_t>(output));
      SetResult(rd_reg(), alu_out);
      break;
    }
    case REVB_4H: {
      printf_instr("REVB_4H\t %s: %016lx, %s, %016lx\n",
                   Registers::Name(rd_reg()), rd(), Registers::Name(rj_reg()),
                   rj());
      uint64_t input = rj_u();
      uint64_t output = 0;

      uint64_t mask = 0xFF00000000000000;
      for (int i = 0; i < 8; i++) {
        uint64_t tmp = mask & input;
        if (i % 2 == 0) {
          tmp = tmp >> 8;
        } else {
          tmp = tmp << 8;
        }
        output = output | tmp;
        mask = mask >> 8;
      }

      alu_out = static_cast<int64_t>(output);
      SetResult(rd_reg(), alu_out);
      break;
    }
    case REVB_2W: {
      printf_instr("REVB_2W\t %s: %016lx, %s, %016lx\n",
                   Registers::Name(rd_reg()), rd(), Registers::Name(rj_reg()),
                   rj());
      uint64_t input = rj_u();
      uint64_t output = 0;

      uint64_t mask = 0xFF000000FF000000;
      for (int i = 0; i < 4; i++) {
        uint64_t tmp = mask & input;
        if (i <= 1) {
          tmp = tmp >> (24 - i * 16);
        } else {
          tmp = tmp << (i * 16 - 24);
        }
        output = output | tmp;
        mask = mask >> 8;
      }

      alu_out = static_cast<int64_t>(output);
      SetResult(rd_reg(), alu_out);
      break;
    }
    case REVB_D: {
      printf_instr("REVB_D\t %s: %016lx, %s, %016lx\n",
                   Registers::Name(rd_reg()), rd(), Registers::Name(rj_reg()),
                   rj());
      uint64_t input = rj_u();
      uint64_t output = 0;

      uint64_t mask = 0xFF00000000000000;
      for (int i = 0; i < 8; i++) {
        uint64_t tmp = mask & input;
        if (i <= 3) {
          tmp = tmp >> (56 - i * 16);
        } else {
          tmp = tmp << (i * 16 - 56);
        }
        output = output | tmp;
        mask = mask >> 8;
      }

      alu_out = static_cast<int64_t>(output);
      SetResult(rd_reg(), alu_out);
      break;
    }
    case REVH_2W: {
      printf_instr("REVH_2W\t %s: %016lx, %s, %016lx\n",
                   Registers::Name(rd_reg()), rd(), Registers::Name(rj_reg()),
                   rj());
      uint64_t input = rj_u();
      uint64_t output = 0;

      uint64_t mask = 0xFFFF000000000000;
      for (int i = 0; i < 4; i++) {
        uint64_t tmp = mask & input;
        if (i % 2 == 0) {
          tmp = tmp >> 16;
        } else {
          tmp = tmp << 16;
        }
        output = output | tmp;
        mask = mask >> 16;
      }

      alu_out = static_cast<int64_t>(output);
      SetResult(rd_reg(), alu_out);
      break;
    }
    case REVH_D: {
      printf_instr("REVH_D\t %s: %016lx, %s, %016lx\n",
                   Registers::Name(rd_reg()), rd(), Registers::Name(rj_reg()),
                   rj());
      uint64_t input = rj_u();
      uint64_t output = 0;

      uint64_t mask = 0xFFFF000000000000;
      for (int i = 0; i < 4; i++) {
        uint64_t tmp = mask & input;
        if (i <= 1) {
          tmp = tmp >> (48 - i * 32);
        } else {
          tmp = tmp << (i * 32 - 48);
        }
        output = output | tmp;
        mask = mask >> 16;
      }

      alu_out = static_cast<int64_t>(output);
      SetResult(rd_reg(), alu_out);
      break;
    }
    case BITREV_4B: {
      printf_instr("BITREV_4B\t %s: %016lx, %s, %016lx\n",
                   Registers::Name(rd_reg()), rd(), Registers::Name(rj_reg()),
                   rj());
      uint32_t input = static_cast<uint32_t>(rj());
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
      SetResult(rd_reg(), alu_out);
      break;
    }
    case BITREV_8B: {
      printf_instr("BITREV_8B\t %s: %016lx, %s, %016lx\n",
                   Registers::Name(rd_reg()), rd(), Registers::Name(rj_reg()),
                   rj());
      uint64_t input = rj_u();
      uint64_t output = 0;
      uint8_t i_byte, o_byte;

      // Reverse the bit in byte for each individual byte
      for (int i = 0; i < 8; i++) {
        output = output >> 8;
        i_byte = input & 0xFF;

        // Fast way to reverse bits in byte
        // Devised by Sean Anderson, July 13, 2001
        o_byte = static_cast<uint8_t>(((i_byte * 0x0802LU & 0x22110LU) |
                                       (i_byte * 0x8020LU & 0x88440LU)) *
                                          0x10101LU >>
                                      16);

        output = output | (static_cast<uint64_t>(o_byte) << 56);
        input = input >> 8;
      }

      alu_out = static_cast<int64_t>(output);
      SetResult(rd_reg(), alu_out);
      break;
    }
    case BITREV_W: {
      printf_instr("BITREV_W\t %s: %016lx, %s, %016lx\n",
                   Registers::Name(rd_reg()), rd(), Registers::Name(rj_reg()),
                   rj());
      uint32_t input = static_cast<uint32_t>(rj());
      uint32_t output = 0;
      output = base::bits::ReverseBits(input);
      alu_out = static_cast<int64_t>(static_cast<int32_t>(output));
      SetResult(rd_reg(), alu_out);
      break;
    }
    case BITREV_D: {
      printf_instr("BITREV_D\t %s: %016lx, %s, %016lx\n",
                   Registers::Name(rd_reg()), rd(), Registers::Name(rj_reg()),
                   rj());
      alu_out = static_cast<int64_t>(base::bits::ReverseBits(rj_u()));
      SetResult(rd_reg(), alu_out);
      break;
    }
    case EXT_W_B: {
      printf_instr("EXT_W_B\t %s: %016lx, %s, %016lx\n",
                   Registers::Name(rd_reg()), rd(), Registers::Name(rj_reg()),
                   rj());
      uint8_t input = static_cast<uint8_t>(rj());
      alu_out = static_cast<int64_t>(static_cast<int8_t>(input));
      SetResult(rd_reg(), alu_out);
      break;
    }
    case EXT_W_H: {
      printf_instr("EXT_W_H\t %s: %016lx, %s, %016lx\n",
                   Registers::Name(rd_reg()), rd(), Registers::Name(rj_reg()),
                   rj());
      uint16_t input = static_cast<uint16_t>(rj());
      alu_out = static_cast<int64_t>(static_cast<int16_t>(input));
      SetResult(rd_reg(), alu_out);
      break;
    }
    case FABS_S:
      printf_instr("FABS_S\t %s: %016f, %s, %016f\n",
                   FPURegisters::Name(fd_reg()), fd_float(),
                   FPURegisters::Name(fj_reg()), fj_float());
      SetFPUFloatResult(fd_reg(), std::abs(fj_float()));
      break;
    case FABS_D:
      printf_instr("FABS_D\t %s: %016f, %s, %016f\n",
                   FPURegisters::Name(fd_reg()), fd_double(),
                   FPURegisters::Name(fj_reg()), fj_double());
      SetFPUDoubleResult(fd_reg(), std::abs(fj_double()));
      break;
    case FNEG_S:
      printf_instr("FNEG_S\t %s: %016f, %s, %016f\n",
                   FPURegisters::Name(fd_reg()), fd_float(),
                   FPURegisters::Name(fj_reg()), fj_float());
      SetFPUFloatResult(fd_reg(), -fj_float());
      break;
    case FNEG_D:
      printf_instr("FNEG_D\t %s: %016f, %s, %016f\n",
                   FPURegisters::Name(fd_reg()), fd_double(),
                   FPURegisters::Name(fj_reg()), fj_double());
      SetFPUDoubleResult(fd_reg(), -fj_double());
      break;
    case FSQRT_S: {
      printf_instr("FSQRT_S\t %s: %016f, %s, %016f\n",
                   FPURegisters::Name(fd_reg()), fd_float(),
                   FPURegisters::Name(fj_reg()), fj_float());
      if (fj_float() >= 0) {
        SetFPUFloatResult(fd_reg(), std::sqrt(fj_float()));
        set_fcsr_bit(kFCSRInvalidOpCauseBit, false);
      } else {
        SetFPUFloatResult(fd_reg(), std::sqrt(-1));  // qnan
        set_fcsr_bit(kFCSRInvalidOpCauseBit, true);
      }
      break;
    }
    case FSQRT_D: {
      printf_instr("FSQRT_D\t %s: %016f, %s, %016f\n",
                   FPURegisters::Name(fd_reg()), fd_double(),
                   FPURegisters::Name(fj_reg()), fj_double());
      if (fj_double() >= 0) {
        SetFPUDoubleResult(fd_reg(), std::sqrt(fj_double()));
        set_fcsr_bit(kFCSRInvalidOpCauseBit, false);
      } else {
        SetFPUDoubleResult(fd_reg(), std::sqrt(-1));  // qnan
        set_fcsr_bit(kFCSRInvalidOpCauseBit, true);
      }
      break;
    }
    case FMOV_S:
      printf_instr("FMOV_S\t %s: %016f, %s, %016f\n",
                   FPURegisters::Name(fd_reg()), fd_float(),
                   FPURegisters::Name(fj_reg()), fj_float());
      SetFPUFloatResult(fd_reg(), fj_float());
      break;
    case FMOV_D:
      printf_instr("FMOV_D\t %s: %016f, %s, %016f\n",
                   FPURegisters::Name(fd_reg()), fd_float(),
                   FPURegisters::Name(fj_reg()), fj_float());
      SetFPUDoubleResult(fd_reg(), fj_double());
      break;
    case MOVGR2FR_W: {
      printf_instr("MOVGR2FR_W\t %s: %016f, %s, %016lx\n",
                   FPURegisters::Name(fd_reg()), fd_double(),
                   Registers::Name(rj_reg()), rj());
      set_fpu_register_word(fd_reg(), static_cast<int32_t>(rj()));
      TraceRegWr(get_fpu_register(fd_reg()), FLOAT_DOUBLE);
      break;
    }
    case MOVGR2FR_D:
      printf_instr("MOVGR2FR_D\t %s: %016f, %s, %016lx\n",
                   FPURegisters::Name(fd_reg()), fd_double(),
                   Registers::Name(rj_reg()), rj());
      SetFPUResult2(fd_reg(), rj());
      break;
    case MOVGR2FRH_W: {
      printf_instr("MOVGR2FRH_W\t %s: %016f, %s, %016lx\n",
                   FPURegisters::Name(fd_reg()), fd_double(),
                   Registers::Name(rj_reg()), rj());
      set_fpu_register_hi_word(fd_reg(), static_cast<int32_t>(rj()));
      TraceRegWr(get_fpu_register(fd_reg()), DOUBLE);
      break;
    }
    case MOVFR2GR_S: {
      printf_instr("MOVFR2GR_S\t %s: %016lx, %s, %016f\n",
                   Registers::Name(rd_reg()), rd(),
                   FPURegisters::Name(fj_reg()), fj_float());
      set_register(rd_reg(),
                   static_cast<int64_t>(get_fpu_register_word(fj_reg())));
      TraceRegWr(get_register(rd_reg()), WORD_DWORD);
      break;
    }
    case MOVFR2GR_D:
      printf_instr("MOVFR2GR_D\t %s: %016lx, %s, %016f\n",
                   Registers::Name(rd_reg()), rd(),
                   FPURegisters::Name(fj_reg()), fj_double());
      SetResult(rd_reg(), get_fpu_register(fj_reg()));
      break;
    case MOVFRH2GR_S:
      printf_instr("MOVFRH2GR_S\t %s: %016lx, %s, %016f\n",
                   Registers::Name(rd_reg()), rd(),
                   FPURegisters::Name(fj_reg()), fj_double());
      SetResult(rd_reg(), get_fpu_register_hi_word(fj_reg()));
      break;
    case MOVGR2FCSR: {
      printf_instr("MOVGR2FCSR\t fcsr: %016x, %s, %016lx\n", FCSR_,
                   Registers::Name(rj_reg()), rj());
      // fcsr could be 0-3
      CHECK_LT(rd_reg(), 4);
      FCSR_ = static_cast<uint32_t>(rj());
      TraceRegWr(FCSR_);
      break;
    }
    case MOVFCSR2GR: {
      printf_instr("MOVFCSR2GR\t %s, %016lx, FCSR: %016x\n",
                   Registers::Name(rd_reg()), rd(), FCSR_);
      // fcsr could be 0-3
      CHECK_LT(rj_reg(), 4);
      SetResult(rd_reg(), FCSR_);
      break;
    }
    case FCVT_S_D:
      printf_instr("FCVT_S_D\t %s: %016f, %s, %016f\n",
                   FPURegisters::Name(fd_reg()), fd_double(),
                   FPURegisters::Name(fj_reg()), fj_double());
      SetFPUFloatResult(fd_reg(), static_cast<float>(fj_double()));
      break;
    case FCVT_D_S:
      printf_instr("FCVT_D_S\t %s: %016f, %s, %016f\n",
                   FPURegisters::Name(fd_reg()), fd_double(),
                   FPURegisters::Name(fj_reg()), fj_float());
      SetFPUDoubleResult(fd_reg(), static_cast<double>(fj_float()));
      break;
    case FTINTRM_W_S: {
      printf_instr("FTINTRM_W_S\t %s: %016f, %s, %016f\n",
                   FPURegisters::Name(fd_reg()), fd_double(),
                   FPURegisters::Name(fj_reg()), fj_float());
      float fj = fj_float();
      float rounded = floor(fj);
      int32_t result = static_cast<int32_t>(rounded);
      SetFPUWordResult(fd_reg(), result);
      if (set_fcsr_round_error(fj, rounded)) {
        set_fpu_register_word_invalid_result(fj, rounded);
      }
      break;
    }
    case FTINTRM_W_D: {
      printf_instr("FTINTRM_W_D\t %s: %016f, %s, %016f\n",
                   FPURegisters::Name(fd_reg()), fd_double(),
                   FPURegisters::Name(fj_reg()), fj_double());
      double fj = fj_double();
      double rounded = floor(fj);
      int32_t result = static_cast<int32_t>(rounded);
      SetFPUWordResult(fd_reg(), result);
      if (set_fcsr_round_error(fj, rounded)) {
        set_fpu_register_invalid_result(fj, rounded);
      }
      break;
    }
    case FTINTRM_L_S: {
      printf_instr("FTINTRM_L_S\t %s: %016f, %s, %016f\n",
                   FPURegisters::Name(fd_reg()), fd_double(),
                   FPURegisters::Name(fj_reg()), fj_float());
      float fj = fj_float();
      float rounded = floor(fj);
      int64_t result = static_cast<int64_t>(rounded);
      SetFPUResult(fd_reg(), result);
      if (set_fcsr_round64_error(fj, rounded)) {
        set_fpu_register_invalid_result64(fj, rounded);
      }
      break;
    }
    case FTINTRM_L_D: {
      printf_instr("FTINTRM_L_D\t %s: %016f, %s, %016f\n",
                   FPURegisters::Name(fd_reg()), fd_double(),
                   FPURegisters::Name(fj_reg()), fj_double());
      double fj = fj_double();
      double rounded = floor(fj);
      int64_t result = static_cast<int64_t>(rounded);
      SetFPUResult(fd_reg(), result);
      if (set_fcsr_round64_error(fj, rounded)) {
        set_fpu_register_invalid_result64(fj, rounded);
      }
      break;
    }
    case FTINTRP_W_S: {
      printf_instr("FTINTRP_W_S\t %s: %016f, %s, %016f\n",
                   FPURegisters::Name(fd_reg()), fd_double(),
                   FPURegisters::Name(fj_reg()), fj_float());
      float fj = fj_float();
      float rounded = ceil(fj);
      int32_t result = static_cast<int32_t>(rounded);
      SetFPUWordResult(fd_reg(), result);
      if (set_fcsr_round_error(fj, rounded)) {
        set_fpu_register_word_invalid_result(fj, rounded);
      }
      break;
    }
    case FTINTRP_W_D: {
      printf_instr("FTINTRP_W_D\t %s: %016f, %s, %016f\n",
                   FPURegisters::Name(fd_reg()), fd_double(),
                   FPURegisters::Name(fj_reg()), fj_double());
      double fj = fj_double();
      double rounded = ceil(fj);
      int32_t result = static_cast<int32_t>(rounded);
      SetFPUWordResult(fd_reg(), result);
      if (set_fcsr_round_error(fj, rounded)) {
        set_fpu_register_invalid_result(fj, rounded);
      }
      break;
    }
    case FTINTRP_L_S: {
      printf_instr("FTINTRP_L_S\t %s: %016f, %s, %016f\n",
                   FPURegisters::Name(fd_reg()), fd_double(),
                   FPURegisters::Name(fj_reg()), fj_float());
      float fj = fj_float();
      float rounded = ceil(fj);
      int64_t result = static_cast<int64_t>(rounded);
      SetFPUResult(fd_reg(), result);
      if (set_fcsr_round64_error(fj, rounded)) {
        set_fpu_register_invalid_result64(fj, rounded);
      }
      break;
    }
    case FTINTRP_L_D: {
      printf_instr("FTINTRP_L_D\t %s: %016f, %s, %016f\n",
                   FPURegisters::Name(fd_reg()), fd_double(),
                   FPURegisters::Name(fj_reg()), fj_double());
      double fj = fj_double();
      double rounded = ceil(fj);
      int64_t result = static_cast<int64_t>(rounded);
      SetFPUResult(fd_reg(), result);
      if (set_fcsr_round64_error(fj, rounded)) {
        set_fpu_register_invalid_result64(fj, rounded);
      }
      break;
    }
    case FTINTRZ_W_S: {
      printf_instr("FTINTRZ_W_S\t %s: %016f, %s, %016f\n",
                   FPURegisters::Name(fd_reg()), fd_double(),
                   FPURegisters::Name(fj_reg()), fj_float());
      float fj = fj_float();
      float rounded = trunc(fj);
      int32_t result = static_cast<int32_t>(rounded);
      SetFPUWordResult(fd_reg(), result);
      if (set_fcsr_round_error(fj, rounded)) {
        set_fpu_register_word_invalid_result(fj, rounded);
      }
      break;
    }
    case FTINTRZ_W_D: {
      printf_instr("FTINTRZ_W_D\t %s: %016f, %s, %016f\n",
                   FPURegisters::Name(fd_reg()), fd_double(),
                   FPURegisters::Name(fj_reg()), fj_double());
      double fj = fj_double();
      double rounded = trunc(fj);
      int32_t result = static_cast<int32_t>(rounded);
      SetFPUWordResult(fd_reg(), result);
      if (set_fcsr_round_error(fj, rounded)) {
        set_fpu_register_invalid_result(fj, rounded);
      }
      break;
    }
    case FTINTRZ_L_S: {
      printf_instr("FTINTRZ_L_S\t %s: %016f, %s, %016f\n",
                   FPURegisters::Name(fd_reg()), fd_double(),
                   FPURegisters::Name(fj_reg()), fj_float());
      float fj = fj_float();
      float rounded = trunc(fj);
      int64_t result = static_cast<int64_t>(rounded);
      SetFPUResult(fd_reg(), result);
      if (set_fcsr_round64_error(fj, rounded)) {
        set_fpu_register_invalid_result64(fj, rounded);
      }
      break;
    }
    case FTINTRZ_L_D: {
      printf_instr("FTINTRZ_L_D\t %s: %016f, %s, %016f\n",
                   FPURegisters::Name(fd_reg()), fd_double(),
                   FPURegisters::Name(fj_reg()), fj_double());
      double fj = fj_double();
      double rounded = trunc(fj);
      int64_t result = static_cast<int64_t>(rounded);
      SetFPUResult(fd_reg(), result);
      if (set_fcsr_round64_error(fj, rounded)) {
        set_fpu_register_invalid_result64(fj, rounded);
      }
      break;
    }
    case FTINTRNE_W_S: {
      printf_instr("FTINTRNE_W_S\t %s: %016f, %s, %016f\n",
                   FPURegisters::Name(fd_reg()), fd_double(),
                   FPURegisters::Name(fj_reg()), fj_float());
      float fj = fj_float();
      float rounded = floor(fj + 0.5);
      int32_t result = static_cast<int32_t>(rounded);
      if ((result & 1) != 0 && result - fj == 0.5) {
        // If the number is halfway between two integers,
        // round to the even one.
        result--;
      }
      SetFPUWordResult(fd_reg(), result);
      if (set_fcsr_round_error(fj, rounded)) {
        set_fpu_register_word_invalid_result(fj, rounded);
      }
      break;
    }
    case FTINTRNE_W_D: {
      printf_instr("FTINTRNE_W_D\t %s: %016f, %s, %016f\n",
                   FPURegisters::Name(fd_reg()), fd_double(),
                   FPURegisters::Name(fj_reg()), fj_double());
      double fj = fj_double();
      double rounded = floor(fj + 0.5);
      int32_t result = static_cast<int32_t>(rounded);
      if ((result & 1) != 0 && result - fj == 0.5) {
        // If the number is halfway between two integers,
        // round to the even one.
        result--;
      }
      SetFPUWordResult(fd_reg(), result);
      if (set_fcsr_round_error(fj, rounded)) {
        set_fpu_register_invalid_result(fj, rounded);
      }
      break;
    }
    case FTINTRNE_L_S: {
      printf_instr("FTINTRNE_L_S\t %s: %016f, %s, %016f\n",
                   FPURegisters::Name(fd_reg()), fd_double(),
                   FPURegisters::Name(fj_reg()), fj_float());
      float fj = fj_float();
      float rounded = floor(fj + 0.5);
      int64_t result = static_cast<int64_t>(rounded);
      if ((result & 1) != 0 && result - fj == 0.5) {
        // If the number is halfway between two integers,
        // round to the even one.
        result--;
      }
      SetFPUResult(fd_reg(), result);
      if (set_fcsr_round64_error(fj, rounded)) {
        set_fpu_register_invalid_result64(fj, rounded);
      }
      break;
    }
    case FTINTRNE_L_D: {
      printf_instr("FTINTRNE_L_D\t %s: %016f, %s, %016f\n",
                   FPURegisters::Name(fd_reg()), fd_double(),
                   FPURegisters::Name(fj_reg()), fj_double());
      double fj = fj_double();
      double rounded = floor(fj + 0.5);
      int64_t result = static_cast<int64_t>(rounded);
      if ((result & 1) != 0 && result - fj == 0.5) {
        // If the number is halfway between two integers,
        // round to the even one.
        result--;
      }
      SetFPUResult(fd_reg(), result);
      if (set_fcsr_round64_error(fj, rounded)) {
        set_fpu_register_invalid_result64(fj, rounded);
      }
      break;
    }
    case FTINT_W_S: {
      printf_instr("FTINT_W_S\t %s: %016f, %s, %016f\n",
                   FPURegisters::Name(fd_reg()), fd_double(),
                   FPURegisters::Name(fj_reg()), fj_float());
      float fj = fj_float();
      float rounded;
      int32_t result;
      round_according_to_fcsr(fj, &rounded, &result);
      SetFPUWordResult(fd_reg(), result);
      if (set_fcsr_round_error(fj, rounded)) {
        set_fpu_register_word_invalid_result(fj, rounded);
      }
      break;
    }
    case FTINT_W_D: {
      printf_instr("FTINT_W_D\t %s: %016f, %s, %016f\n",
                   FPURegisters::Name(fd_reg()), fd_double(),
                   FPURegisters::Name(fj_reg()), fj_double());
      double fj = fj_double();
      double rounded;
      int32_t result;
      round_according_to_fcsr(fj, &rounded, &result);
      SetFPUWordResult(fd_reg(), result);
      if (set_fcsr_round_error(fj, rounded)) {
        set_fpu_register_word_invalid_result(fj, rounded);
      }
      break;
    }
    case FTINT_L_S: {
      printf_instr("FTINT_L_S\t %s: %016f, %s, %016f\n",
                   FPURegisters::Name(fd_reg()), fd_double(),
                   FPURegisters::Name(fj_reg()), fj_float());
      float fj = fj_float();
      float rounded;
      int64_t result;
      round64_according_to_fcsr(fj, &rounded, &result);
      SetFPUResult(fd_reg(), result);
      if (set_fcsr_round64_error(fj, rounded)) {
        set_fpu_register_invalid_result64(fj, rounded);
      }
      break;
    }
    case FTINT_L_D: {
      printf_instr("FTINT_L_D\t %s: %016f, %s, %016f\n",
                   FPURegisters::Name(fd_reg()), fd_double(),
                   FPURegisters::Name(fj_reg()), fj_double());
      double fj = fj_double();
      double rounded;
      int64_t result;
      round64_according_to_fcsr(fj, &rounded, &result);
      SetFPUResult(fd_reg(), result);
      if (set_fcsr_round64_error(fj, rounded)) {
        set_fpu_register_invalid_result64(fj, rounded);
      }
      break;
    }
    case FFINT_S_W: {
      alu_out = get_fpu_register_signed_word(fj_reg());
      printf_instr("FFINT_S_W\t %s: %016f, %s, %016x\n",
                   FPURegisters::Name(fd_reg()), fd_double(),
                   FPURegisters::Name(fj_reg()), static_cast<int>(alu_out));
      SetFPUFloatResult(fd_reg(), static_cast<float>(alu_out));
      break;
    }
    case FFINT_S_L: {
      alu_out = get_fpu_register(fj_reg());
      printf_instr("FFINT_S_L\t %s: %016f, %s, %016lx\n",
                   FPURegisters::Name(fd_reg()), fd_double(),
                   FPURegisters::Name(fj_reg()), alu_out);
      SetFPUFloatResult(fd_reg(), static_cast<float>(alu_out));
      break;
    }
    case FFINT_D_W: {
      alu_out = get_fpu_register_signed_word(fj_reg());
      printf_instr("FFINT_D_W\t %s: %016f, %s, %016x\n",
                   FPURegisters::Name(fd_reg()), fd_double(),
                   FPURegisters::Name(fj_reg()), static_cast<int>(alu_out));
      SetFPUDoubleResult(fd_reg(), static_cast<double>(alu_out));
      break;
    }
    case FFINT_D_L: {
      alu_out = get_fpu_register(fj_reg());
      printf_instr("FFINT_D_L\t %s: %016f, %s, %016lx\n",
                   FPURegisters::Name(fd_reg()), fd_double(),
                   FPURegisters::Name(fj_reg()), alu_out);
      SetFPUDoubleResult(fd_reg(), static_cast<double>(alu_out));
      break;
    }
    case FRINT_S: {
      printf_instr("FRINT_S\t %s: %016f, %s, %016f mode : ",
                   FPURegisters::Name(fd_reg()), fd_float(),
                   FPURegisters::Name(fj_reg()), fj_float());
      float fj = fj_float();
      float result, temp_result;
      double temp;
      float upper = ceil(fj);
      float lower = floor(fj);
      switch (get_fcsr_rounding_mode()) {
        case kRoundToNearest:
          printf_instr(" kRoundToNearest\n");
          if (upper - fj < fj - lower) {
            result = upper;
          } else if (upper - fj > fj - lower) {
            result = lower;
          } else {
            temp_result = upper / 2;
            float reminder = std::modf(temp_result, &temp);
            if (reminder == 0) {
              result = upper;
            } else {
              result = lower;
            }
          }
          break;
        case kRoundToZero:
          printf_instr(" kRoundToZero\n");
          result = (fj > 0 ? lower : upper);
          break;
        case kRoundToPlusInf:
          printf_instr(" kRoundToPlusInf\n");
          result = upper;
          break;
        case kRoundToMinusInf:
          printf_instr(" kRoundToMinusInf\n");
          result = lower;
          break;
      }
      SetFPUFloatResult(fd_reg(), result);
      set_fcsr_bit(kFCSRInexactCauseBit, result != fj);
      break;
    }
    case FRINT_D: {
      printf_instr("FRINT_D\t %s: %016f, %s, %016f mode : ",
                   FPURegisters::Name(fd_reg()), fd_double(),
                   FPURegisters::Name(fj_reg()), fj_double());
      double fj = fj_double();
      double result, temp, temp_result;
      double upper = ceil(fj);
      double lower = floor(fj);
      switch (get_fcsr_rounding_mode()) {
        case kRoundToNearest:
          printf_instr(" kRoundToNearest\n");
          if (upper - fj < fj - lower) {
            result = upper;
          } else if (upper - fj > fj - lower) {
            result = lower;
          } else {
            temp_result = upper / 2;
            double reminder = std::modf(temp_result, &temp);
            if (reminder == 0) {
              result = upper;
            } else {
              result = lower;
            }
          }
          break;
        case kRoundToZero:
          printf_instr(" kRoundToZero\n");
          result = (fj > 0 ? lower : upper);
          break;
        case kRoundToPlusInf:
          printf_instr(" kRoundToPlusInf\n");
          result = upper;
          break;
        case kRoundToMinusInf:
          printf_instr(" kRoundToMinusInf\n");
          result = lower;
          break;
      }
      SetFPUDoubleResult(fd_reg(), result);
      set_fcsr_bit(kFCSRInexactCauseBit, result != fj);
      break;
    }
    case MOVFR2CF:
      printf("Sim UNIMPLEMENTED: MOVFR2CF\n");
      UNIMPLEMENTED();
    case MOVCF2FR:
      printf("Sim UNIMPLEMENTED: MOVCF2FR\n");
      UNIMPLEMENTED();
    case MOVGR2CF:
      printf_instr("MOVGR2CF\t FCC%d, %s: %016lx\n", cd_reg(),
                   Registers::Name(rj_reg()), rj());
      set_cf_register(cd_reg(), rj() & 1);
      break;
    case MOVCF2GR:
      printf_instr("MOVCF2GR\t %s: %016lx, FCC%d\n", Registers::Name(rd_reg()),
                   rd(), cj_reg());
      SetResult(rd_reg(), cj());
      break;
    case FRECIP_S:
      printf("Sim UNIMPLEMENTED: FRECIP_S\n");
      UNIMPLEMENTED();
    case FRECIP_D:
      printf("Sim UNIMPLEMENTED: FRECIP_D\n");
      UNIMPLEMENTED();
    case FRSQRT_S:
      printf("Sim UNIMPLEMENTED: FRSQRT_S\n");
      UNIMPLEMENTED();
    case FRSQRT_D:
      printf("Sim UNIMPLEMENTED: FRSQRT_D\n");
      UNIMPLEMENTED();
    case FCLASS_S:
      printf("Sim UNIMPLEMENTED: FCLASS_S\n");
      UNIMPLEMENTED();
    case FCLASS_D:
      printf("Sim UNIMPLEMENTED: FCLASS_D\n");
      UNIMPLEMENTED();
    case FLOGB_S:
      printf("Sim UNIMPLEMENTED: FLOGB_S\n");
      UNIMPLEMENTED();
    case FLOGB_D:
      printf("Sim UNIMPLEMENTED: FLOGB_D\n");
      UNIMPLEMENTED();
    case CLO_W:
      printf("Sim UNIMPLEMENTED: CLO_W\n");
      UNIMPLEMENTED();
    case CTO_W:
      printf("Sim UNIMPLEMENTED: CTO_W\n");
      UNIMPLEMENTED();
    case CLO_D:
      printf("Sim UNIMPLEMENTED: CLO_D\n");
      UNIMPLEMENTED();
    case CTO_D:
      printf("Sim UNIMPLEMENTED: CTO_D\n");
      UNIMPLEMENTED();
    // Unimplemented opcodes raised an error in the configuration step before,
    // so we can use the default here to set the destination register in common
    // cases.
    default:
      UNREACHABLE();
  }
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

  static int instr_count = 0;
  USE(instr_count);
  instr_ = instr;
  printf_instr("\nInstr%3d: %08x, PC: %016lx\t", instr_count++,
               instr_.Bits(31, 0), get_pc());
  switch (instr_.InstructionType()) {
    case Instruction::kOp6Type:
      DecodeTypeOp6();
      break;
    case Instruction::kOp7Type:
      DecodeTypeOp7();
      break;
    case Instruction::kOp8Type:
      DecodeTypeOp8();
      break;
    case Instruction::kOp10Type:
      DecodeTypeOp10();
      break;
    case Instruction::kOp12Type:
      DecodeTypeOp12();
      break;
    case Instruction::kOp14Type:
      DecodeTypeOp14();
      break;
    case Instruction::kOp17Type:
      DecodeTypeOp17();
      break;
    case Instruction::kOp22Type:
      DecodeTypeOp22();
      break;
    default: {
      printf("instr_: %x\n", instr_.Bits(31, 0));
      UNREACHABLE();
    }
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
        Loong64Debugger dbg(this);
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
  int64_t s0_val = get_register(s0);
  int64_t s1_val = get_register(s1);
  int64_t s2_val = get_register(s2);
  int64_t s3_val = get_register(s3);
  int64_t s4_val = get_register(s4);
  int64_t s5_val = get_register(s5);
  int64_t s6_val = get_register(s6);
  int64_t s7_val = get_register(s7);
  int64_t s8_val = get_register(s8);
  int64_t gp_val = get_register(gp);
  int64_t sp_val = get_register(sp);
  int64_t tp_val = get_register(tp);
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
  set_register(s8, callee_saved_value);
  set_register(gp, callee_saved_value);
  set_register(tp, callee_saved_value);
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
  CHECK_EQ(callee_saved_value, get_register(s8));
  CHECK_EQ(callee_saved_value, get_register(gp));
  CHECK_EQ(callee_saved_value, get_register(tp));
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
  set_register(s8, s8_val);
  set_register(gp, gp_val);
  set_register(sp, sp_val);
  set_register(tp, tp_val);
  set_register(fp, fp_val);
}

void Simulator::CallImpl(Address entry, CallArgument* args) {
  int index_gp = 0;
  int index_fp = 0;

  std::vector<int64_t> stack_args(0);
  for (int i = 0; !args[i].IsEnd(); i++) {
    CallArgument arg = args[i];
    if (arg.IsGP() && (index_gp < 8)) {
      set_register(index_gp + 4, arg.bits());
      index_gp++;
    } else if (arg.IsFP() && (index_fp < 8)) {
      set_fpu_register(index_fp++, arg.bits());
    } else if (arg.IsFP() && (index_gp < 8)) {
      set_register(index_gp + 4, arg.bits());
      index_gp++;
    } else {
      DCHECK(arg.IsFP() || arg.IsGP());
      stack_args.push_back(arg.bits());
    }
  }

  // Remaining arguments passed on stack.
  int64_t original_stack = get_register(sp);
  // Compute position of stack on entry to generated code.
  int64_t stack_args_size = stack_args.size() * sizeof(stack_args[0]);
  int64_t entry_stack = original_stack - stack_args_size;

  if (base::OS::ActivationFrameAlignment() != 0) {
    entry_stack &= -base::OS::ActivationFrameAlignment();
  }
  // Store remaining arguments on stack, from low to high memory.
  char* stack_argument = reinterpret_cast<char*>(entry_stack);
  memcpy(stack_argument, stack_args.data(),
         stack_args.size() * sizeof(int64_t));
  set_register(sp, entry_stack);

  CallInternal(entry);

  // Pop stack passed arguments.
  CHECK_EQ(entry_stack, get_register(sp));
  set_register(sp, original_stack);
}

double Simulator::CallFP(Address entry, double d0, double d1) {
  const FPURegister fparg2 = f1;
  set_fpu_register_double(f0, d0);
  set_fpu_register_double(fparg2, d1);
  CallInternal(entry);
  return get_fpu_register_double(f0);
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
