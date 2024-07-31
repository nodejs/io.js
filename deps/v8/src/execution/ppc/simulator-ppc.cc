// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/execution/ppc/simulator-ppc.h"

#if defined(USE_SIMULATOR)

#include <stdarg.h>
#include <stdlib.h>

#include <cmath>

#include "src/base/bits.h"
#include "src/base/lazy-instance.h"
#include "src/base/overflowing-math.h"
#include "src/base/platform/memory.h"
#include "src/base/platform/platform.h"
#include "src/codegen/assembler.h"
#include "src/codegen/macro-assembler.h"
#include "src/codegen/ppc/constants-ppc.h"
#include "src/codegen/register-configuration.h"
#include "src/diagnostics/disasm.h"
#include "src/execution/ppc/frame-constants-ppc.h"
#include "src/heap/combined-heap.h"
#include "src/heap/heap-inl.h"  // For CodeSpaceMemoryModificationScope.
#include "src/objects/objects-inl.h"
#include "src/runtime/runtime-utils.h"
#include "src/utils/ostreams.h"

// Only build the simulator if not compiling for real PPC hardware.
namespace v8 {
namespace internal {

DEFINE_LAZY_LEAKY_OBJECT_GETTER(Simulator::GlobalMonitor,
                                Simulator::GlobalMonitor::Get)

// This macro provides a platform independent use of sscanf. The reason for
// SScanF not being implemented in a platform independent way through
// ::v8::internal::OS in the same way as SNPrintF is that the
// Windows C Run-Time Library does not provide vsscanf.
#define SScanF sscanf

// The PPCDebugger class is used by the simulator while debugging simulated
// PowerPC code.
class PPCDebugger {
 public:
  explicit PPCDebugger(Simulator* sim) : sim_(sim) {}
  void Debug();

 private:
  static const Instr kBreakpointInstr = (TWI | 0x1F * B21);
  static const Instr kNopInstr = (ORI);  // ori, 0,0,0

  Simulator* sim_;

  intptr_t GetRegisterValue(int regnum);
  double GetRegisterPairDoubleValue(int regnum);
  double GetFPDoubleRegisterValue(int regnum);
  bool GetValue(const char* desc, intptr_t* value);
  bool GetFPDoubleValue(const char* desc, double* value);

  // Set or delete breakpoint (there can be only one).
  bool SetBreakpoint(Instruction* break_pc);
  void DeleteBreakpoint();

  // Undo and redo the breakpoint. This is needed to bracket disassembly and
  // execution to skip past the breakpoint when run from the debugger.
  void UndoBreakpoint();
  void RedoBreakpoint();
};

void Simulator::DebugAtNextPC() {
  PrintF("Starting debugger on the next instruction:\n");
  set_pc(get_pc() + kInstrSize);
  PPCDebugger(this).Debug();
}

intptr_t PPCDebugger::GetRegisterValue(int regnum) {
  return sim_->get_register(regnum);
}

double PPCDebugger::GetRegisterPairDoubleValue(int regnum) {
  return sim_->get_double_from_register_pair(regnum);
}

double PPCDebugger::GetFPDoubleRegisterValue(int regnum) {
  return sim_->get_double_from_d_register(regnum);
}

bool PPCDebugger::GetValue(const char* desc, intptr_t* value) {
  int regnum = Registers::Number(desc);
  if (regnum != kNoRegister) {
    *value = GetRegisterValue(regnum);
    return true;
  }
  if (strncmp(desc, "0x", 2) == 0) {
    return SScanF(desc + 2, "%" V8PRIxPTR,
                  reinterpret_cast<uintptr_t*>(value)) == 1;
  }
  return SScanF(desc, "%" V8PRIuPTR, reinterpret_cast<uintptr_t*>(value)) == 1;
}

bool PPCDebugger::GetFPDoubleValue(const char* desc, double* value) {
  int regnum = DoubleRegisters::Number(desc);
  if (regnum != kNoRegister) {
    *value = sim_->get_double_from_d_register(regnum);
    return true;
  }
  return false;
}

bool PPCDebugger::SetBreakpoint(Instruction* break_pc) {
  // Check if a breakpoint can be set. If not return without any side-effects.
  if (sim_->break_pc_ != nullptr) {
    return false;
  }

  // Set the breakpoint.
  sim_->break_pc_ = break_pc;
  sim_->break_instr_ = break_pc->InstructionBits();
  // Not setting the breakpoint instruction in the code itself. It will be set
  // when the debugger shell continues.
  return true;
}

namespace {
// This function is dangerous, but it's only available in non-production
// (simulator) builds.
void SetInstructionBitsInCodeSpace(Instruction* instr, Instr value,
                                   Heap* heap) {
  CodePageMemoryModificationScopeForDebugging scope(
      MemoryChunkMetadata::FromAddress(reinterpret_cast<Address>(instr)));
  instr->SetInstructionBits(value);
}
}  // namespace

void PPCDebugger::DeleteBreakpoint() {
  UndoBreakpoint();
  sim_->break_pc_ = nullptr;
  sim_->break_instr_ = 0;
}

void PPCDebugger::UndoBreakpoint() {
  if (sim_->break_pc_ != nullptr) {
    SetInstructionBitsInCodeSpace(sim_->break_pc_, sim_->break_instr_,
                                  sim_->isolate_->heap());
  }
}

void PPCDebugger::RedoBreakpoint() {
  if (sim_->break_pc_ != nullptr) {
    SetInstructionBitsInCodeSpace(sim_->break_pc_, kBreakpointInstr,
                                  sim_->isolate_->heap());
  }
}

void PPCDebugger::Debug() {
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

  // make sure to have a proper terminating character if reaching the limit
  cmd[COMMAND_SIZE] = 0;
  arg1[ARG_SIZE] = 0;
  arg2[ARG_SIZE] = 0;

  // Unset breakpoint while running in the debugger shell, making it invisible
  // to all commands.
  UndoBreakpoint();
  // Disable tracing while simulating
  bool trace = v8_flags.trace_sim;
  v8_flags.trace_sim = false;

  while (!done && !sim_->has_bad_pc()) {
    if (last_pc != sim_->get_pc()) {
      disasm::NameConverter converter;
      disasm::Disassembler dasm(converter);
      // use a reasonably large buffer
      v8::base::EmbeddedVector<char, 256> buffer;
      dasm.InstructionDecode(buffer,
                             reinterpret_cast<uint8_t*>(sim_->get_pc()));
      PrintF("  0x%08" V8PRIxPTR "  %s\n", sim_->get_pc(), buffer.begin());
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
        intptr_t value;

        // If at a breakpoint, proceed past it.
        if ((reinterpret_cast<Instruction*>(sim_->get_pc()))
                ->InstructionBits() == 0x7D821008) {
          sim_->set_pc(sim_->get_pc() + kInstrSize);
        } else {
          sim_->ExecuteInstruction(
              reinterpret_cast<Instruction*>(sim_->get_pc()));
        }

        if (argc == 2 && last_pc != sim_->get_pc() && GetValue(arg1, &value)) {
          for (int i = 1; i < value; i++) {
            disasm::NameConverter converter;
            disasm::Disassembler dasm(converter);
            // use a reasonably large buffer
            v8::base::EmbeddedVector<char, 256> buffer;
            dasm.InstructionDecode(buffer,
                                   reinterpret_cast<uint8_t*>(sim_->get_pc()));
            PrintF("  0x%08" V8PRIxPTR "  %s\n", sim_->get_pc(),
                   buffer.begin());
            sim_->ExecuteInstruction(
                reinterpret_cast<Instruction*>(sim_->get_pc()));
          }
        }
      } else if ((strcmp(cmd, "c") == 0) || (strcmp(cmd, "cont") == 0)) {
        // If at a breakpoint, proceed past it.
        if ((reinterpret_cast<Instruction*>(sim_->get_pc()))
                ->InstructionBits() == 0x7D821008) {
          sim_->set_pc(sim_->get_pc() + kInstrSize);
        } else {
          // Execute the one instruction we broke at with breakpoints disabled.
          sim_->ExecuteInstruction(
              reinterpret_cast<Instruction*>(sim_->get_pc()));
        }
        // Leave the debugger shell.
        done = true;
      } else if ((strcmp(cmd, "p") == 0) || (strcmp(cmd, "print") == 0)) {
        if (argc == 2 || (argc == 3 && strcmp(arg2, "fp") == 0)) {
          intptr_t value;
          double dvalue;
          if (strcmp(arg1, "all") == 0) {
            for (int i = 0; i < kNumRegisters; i++) {
              value = GetRegisterValue(i);
              PrintF("    %3s: %08" V8PRIxPTR,
                     RegisterName(Register::from_code(i)), value);
              if ((argc == 3 && strcmp(arg2, "fp") == 0) && i < 8 &&
                  (i % 2) == 0) {
                dvalue = GetRegisterPairDoubleValue(i);
                PrintF(" (%f)\n", dvalue);
              } else if (i != 0 && !((i + 1) & 3)) {
                PrintF("\n");
              }
            }
            PrintF("  pc: %08" V8PRIxPTR "  lr: %08" V8PRIxPTR
                   "  "
                   "ctr: %08" V8PRIxPTR "  xer: %08x  cr: %08x\n",
                   sim_->special_reg_pc_, sim_->special_reg_lr_,
                   sim_->special_reg_ctr_, sim_->special_reg_xer_,
                   sim_->condition_reg_);
          } else if (strcmp(arg1, "alld") == 0) {
            for (int i = 0; i < kNumRegisters; i++) {
              value = GetRegisterValue(i);
              PrintF("     %3s: %08" V8PRIxPTR " %11" V8PRIdPTR,
                     RegisterName(Register::from_code(i)), value, value);
              if ((argc == 3 && strcmp(arg2, "fp") == 0) && i < 8 &&
                  (i % 2) == 0) {
                dvalue = GetRegisterPairDoubleValue(i);
                PrintF(" (%f)\n", dvalue);
              } else if (!((i + 1) % 2)) {
                PrintF("\n");
              }
            }
            PrintF("   pc: %08" V8PRIxPTR "  lr: %08" V8PRIxPTR
                   "  "
                   "ctr: %08" V8PRIxPTR "  xer: %08x  cr: %08x\n",
                   sim_->special_reg_pc_, sim_->special_reg_lr_,
                   sim_->special_reg_ctr_, sim_->special_reg_xer_,
                   sim_->condition_reg_);
          } else if (strcmp(arg1, "allf") == 0) {
            for (int i = 0; i < DoubleRegister::kNumRegisters; i++) {
              dvalue = GetFPDoubleRegisterValue(i);
              uint64_t as_words = base::bit_cast<uint64_t>(dvalue);
              PrintF("%3s: %f 0x%08x %08x\n",
                     RegisterName(DoubleRegister::from_code(i)), dvalue,
                     static_cast<uint32_t>(as_words >> 32),
                     static_cast<uint32_t>(as_words & 0xFFFFFFFF));
            }
          } else if (arg1[0] == 'r' &&
                     (arg1[1] >= '0' && arg1[1] <= '9' &&
                      (arg1[2] == '\0' || (arg1[2] >= '0' && arg1[2] <= '9' &&
                                           arg1[3] == '\0')))) {
            int regnum = strtoul(&arg1[1], 0, 10);
            if (regnum != kNoRegister) {
              value = GetRegisterValue(regnum);
              PrintF("%s: 0x%08" V8PRIxPTR " %" V8PRIdPTR "\n", arg1, value,
                     value);
            } else {
              PrintF("%s unrecognized\n", arg1);
            }
          } else {
            if (GetValue(arg1, &value)) {
              PrintF("%s: 0x%08" V8PRIxPTR " %" V8PRIdPTR "\n", arg1, value,
                     value);
            } else if (GetFPDoubleValue(arg1, &dvalue)) {
              uint64_t as_words = base::bit_cast<uint64_t>(dvalue);
              PrintF("%s: %f 0x%08x %08x\n", arg1, dvalue,
                     static_cast<uint32_t>(as_words >> 32),
                     static_cast<uint32_t>(as_words & 0xFFFFFFFF));
            } else {
              PrintF("%s unrecognized\n", arg1);
            }
          }
        } else {
          PrintF("print <register>\n");
        }
      } else if ((strcmp(cmd, "po") == 0) ||
                 (strcmp(cmd, "printobject") == 0)) {
        if (argc == 2) {
          intptr_t value;
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
      } else if (strcmp(cmd, "setpc") == 0) {
        intptr_t value;

        if (!GetValue(arg1, &value)) {
          PrintF("%s unrecognized\n", arg1);
          continue;
        }
        sim_->set_pc(value);
      } else if (strcmp(cmd, "stack") == 0 || strcmp(cmd, "mem") == 0 ||
                 strcmp(cmd, "dump") == 0) {
        intptr_t* cur = nullptr;
        intptr_t* end = nullptr;
        int next_arg = 1;

        if (strcmp(cmd, "stack") == 0) {
          cur = reinterpret_cast<intptr_t*>(sim_->get_register(Simulator::sp));
        } else {  // "mem"
          intptr_t value;
          if (!GetValue(arg1, &value)) {
            PrintF("%s unrecognized\n", arg1);
            continue;
          }
          cur = reinterpret_cast<intptr_t*>(value);
          next_arg++;
        }

        intptr_t words;  // likely inaccurate variable name for 64bit
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
          PrintF("  0x%08" V8PRIxPTR ":  0x%08" V8PRIxPTR " %10" V8PRIdPTR,
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
      } else if (strcmp(cmd, "disasm") == 0 || strcmp(cmd, "di") == 0) {
        disasm::NameConverter converter;
        disasm::Disassembler dasm(converter);
        // use a reasonably large buffer
        v8::base::EmbeddedVector<char, 256> buffer;

        uint8_t* prev = nullptr;
        uint8_t* cur = nullptr;
        uint8_t* end = nullptr;

        if (argc == 1) {
          cur = reinterpret_cast<uint8_t*>(sim_->get_pc());
          end = cur + (10 * kInstrSize);
        } else if (argc == 2) {
          int regnum = Registers::Number(arg1);
          if (regnum != kNoRegister || strncmp(arg1, "0x", 2) == 0) {
            // The argument is an address or a register name.
            intptr_t value;
            if (GetValue(arg1, &value)) {
              cur = reinterpret_cast<uint8_t*>(value);
              // Disassemble 10 instructions at <arg1>.
              end = cur + (10 * kInstrSize);
            }
          } else {
            // The argument is the number of instructions.
            intptr_t value;
            if (GetValue(arg1, &value)) {
              cur = reinterpret_cast<uint8_t*>(sim_->get_pc());
              // Disassemble <arg1> instructions.
              end = cur + (value * kInstrSize);
            }
          }
        } else {
          intptr_t value1;
          intptr_t value2;
          if (GetValue(arg1, &value1) && GetValue(arg2, &value2)) {
            cur = reinterpret_cast<uint8_t*>(value1);
            end = cur + (value2 * kInstrSize);
          }
        }

        while (cur < end) {
          prev = cur;
          cur += dasm.InstructionDecode(buffer, cur);
          PrintF("  0x%08" V8PRIxPTR "  %s\n", reinterpret_cast<intptr_t>(prev),
                 buffer.begin());
        }
      } else if (strcmp(cmd, "gdb") == 0) {
        PrintF("relinquishing control to gdb\n");
        v8::base::OS::DebugBreak();
        PrintF("regaining control from gdb\n");
      } else if (strcmp(cmd, "break") == 0) {
        if (argc == 2) {
          intptr_t value;
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
        DeleteBreakpoint();
      } else if (strcmp(cmd, "cr") == 0) {
        PrintF("Condition reg: %08x\n", sim_->condition_reg_);
      } else if (strcmp(cmd, "lr") == 0) {
        PrintF("Link reg: %08" V8PRIxPTR "\n", sim_->special_reg_lr_);
      } else if (strcmp(cmd, "ctr") == 0) {
        PrintF("Ctr reg: %08" V8PRIxPTR "\n", sim_->special_reg_ctr_);
      } else if (strcmp(cmd, "xer") == 0) {
        PrintF("XER: %08x\n", sim_->special_reg_xer_);
      } else if (strcmp(cmd, "fpscr") == 0) {
        PrintF("FPSCR: %08x\n", sim_->fp_condition_reg_);
      } else if (strcmp(cmd, "stop") == 0) {
        intptr_t value;
        intptr_t stop_pc = sim_->get_pc() - (kInstrSize + kSystemPointerSize);
        Instruction* stop_instr = reinterpret_cast<Instruction*>(stop_pc);
        Instruction* msg_address =
            reinterpret_cast<Instruction*>(stop_pc + kInstrSize);
        if ((argc == 2) && (strcmp(arg1, "unstop") == 0)) {
          // Remove the current stop.
          if (sim_->isStopInstruction(stop_instr)) {
            SetInstructionBitsInCodeSpace(stop_instr, kNopInstr,
                                          sim_->isolate_->heap());
            msg_address->SetInstructionBits(kNopInstr);
          } else {
            PrintF("Not at debugger stop.\n");
          }
        } else if (argc == 3) {
          // Print information about all/the specified breakpoint(s).
          if (strcmp(arg1, "info") == 0) {
            if (strcmp(arg2, "all") == 0) {
              PrintF("Stop information:\n");
              for (uint32_t i = 0; i < sim_->kNumOfWatchedStops; i++) {
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
              for (uint32_t i = 0; i < sim_->kNumOfWatchedStops; i++) {
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
              for (uint32_t i = 0; i < sim_->kNumOfWatchedStops; i++) {
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
      } else if ((strcmp(cmd, "t") == 0) || strcmp(cmd, "trace") == 0) {
        v8_flags.trace_sim = !v8_flags.trace_sim;
        PrintF("Trace of executed instructions is %s\n",
               v8_flags.trace_sim ? "on" : "off");
      } else if ((strcmp(cmd, "h") == 0) || (strcmp(cmd, "help") == 0)) {
        PrintF("cont\n");
        PrintF("  continue execution (alias 'c')\n");
        PrintF("stepi [num instructions]\n");
        PrintF("  step one/num instruction(s) (alias 'si')\n");
        PrintF("print <register>\n");
        PrintF("  print register content (alias 'p')\n");
        PrintF("  use register name 'all' to display all integer registers\n");
        PrintF(
            "  use register name 'alld' to display integer registers "
            "with decimal values\n");
        PrintF("  use register name 'rN' to display register number 'N'\n");
        PrintF("  add argument 'fp' to print register pair double values\n");
        PrintF(
            "  use register name 'allf' to display floating-point "
            "registers\n");
        PrintF("printobject <register>\n");
        PrintF("  print an object from a register (alias 'po')\n");
        PrintF("cr\n");
        PrintF("  print condition register\n");
        PrintF("lr\n");
        PrintF("  print link register\n");
        PrintF("ctr\n");
        PrintF("  print ctr register\n");
        PrintF("xer\n");
        PrintF("  print XER\n");
        PrintF("fpscr\n");
        PrintF("  print FPSCR\n");
        PrintF("stack [<num words>]\n");
        PrintF("  dump stack content, default dump 10 words)\n");
        PrintF("mem <address> [<num words>]\n");
        PrintF("  dump memory content, default dump 10 words)\n");
        PrintF("dump [<words>]\n");
        PrintF(
            "  dump memory content without pretty printing JS objects, default "
            "dump 10 words)\n");
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
        PrintF("trace (alias 't')\n");
        PrintF("  toogle the tracing of all executed statements\n");
        PrintF("stop feature:\n");
        PrintF("  Description:\n");
        PrintF("    Stops are debug instructions inserted by\n");
        PrintF("    the Assembler::stop() function.\n");
        PrintF("    When hitting a stop, the Simulator will\n");
        PrintF("    stop and give control to the PPCDebugger.\n");
        PrintF("    The first %d stop codes are watched:\n",
               Simulator::kNumOfWatchedStops);
        PrintF("    - They can be enabled / disabled: the Simulator\n");
        PrintF("      will / won't stop when hitting them.\n");
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

  // Reinstall breakpoint to stop execution and enter the debugger shell when
  // hit.
  RedoBreakpoint();
  // Restore tracing
  v8_flags.trace_sim = trace;

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

static bool AllOnOnePage(uintptr_t start, int size) {
  intptr_t start_page = (start & ~CachePage::kPageMask);
  intptr_t end_page = ((start + size) & ~CachePage::kPageMask);
  return start_page == end_page;
}

static bool is_snan(float input) {
  uint32_t kQuietNanFPBit = 1 << 22;
  uint32_t InputAsUint = base::bit_cast<uint32_t>(input);
  return isnan(input) && ((InputAsUint & kQuietNanFPBit) == 0);
}

static bool is_snan(double input) {
  uint64_t kQuietNanDPBit = 1L << 51;
  uint64_t InputAsUint = base::bit_cast<uint64_t>(input);
  return isnan(input) && ((InputAsUint & kQuietNanDPBit) == 0);
}

void Simulator::set_last_debugger_input(char* input) {
  DeleteArray(last_debugger_input_);
  last_debugger_input_ = input;
}

void Simulator::SetRedirectInstruction(Instruction* instruction) {
  instruction->SetInstructionBits(rtCallRedirInstr | kCallRtRedirected);
}

void Simulator::FlushICache(base::CustomMatcherHashMap* i_cache,
                            void* start_addr, size_t size) {
  intptr_t start = reinterpret_cast<intptr_t>(start_addr);
  int intra_line = (start & CachePage::kLineMask);
  start -= intra_line;
  size += intra_line;
  size = ((size - 1) | CachePage::kLineMask) + 1;
  int offset = (start & CachePage::kPageMask);
  while (!AllOnOnePage(start, size - 1)) {
    int bytes_to_flush = CachePage::kPageSize - offset;
    FlushOnePage(i_cache, start, bytes_to_flush);
    start += bytes_to_flush;
    size -= bytes_to_flush;
    DCHECK_EQ(0, static_cast<int>(start & CachePage::kPageMask));
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
                             intptr_t start, int size) {
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
  intptr_t address = reinterpret_cast<intptr_t>(instr);
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
  stack_ = reinterpret_cast<uint8_t*>(base::Malloc(AllocatedStackSize()));
  pc_modified_ = false;
  icount_ = 0;
  break_pc_ = nullptr;
  break_instr_ = 0;

  // Set up architecture state.
  // All registers are initialized to zero to start with.
  for (int i = 0; i < kNumGPRs; i++) {
    registers_[i] = 0;
  }
  condition_reg_ = 0;
  fp_condition_reg_ = 0;
  special_reg_pc_ = 0;
  special_reg_lr_ = 0;
  special_reg_ctr_ = 0;

  // Initializing FP registers.
  for (int i = 0; i < kNumFPRs; i++) {
    fp_registers_[i] = 0.0;
  }

  // The sp is initialized to point to the bottom (high address) of the
  // allocated stack area. To be safe in potential stack underflows we leave
  // some buffer below.
  registers_[sp] = reinterpret_cast<intptr_t>(stack_) + UsableStackSize();

  last_debugger_input_ = nullptr;
}

Simulator::~Simulator() { base::Free(stack_); }

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

// Sets the register in the architecture state.
void Simulator::set_register(int reg, intptr_t value) {
  DCHECK((reg >= 0) && (reg < kNumGPRs));
  registers_[reg] = value;
}

// Get the register from the architecture state.
intptr_t Simulator::get_register(int reg) const {
  DCHECK((reg >= 0) && (reg < kNumGPRs));
  // Stupid code added to avoid bug in GCC.
  // See: http://gcc.gnu.org/bugzilla/show_bug.cgi?id=43949
  if (reg >= kNumGPRs) return 0;
  // End stupid code.
  return registers_[reg];
}

double Simulator::get_double_from_register_pair(int reg) {
  DCHECK((reg >= 0) && (reg < kNumGPRs) && ((reg % 2) == 0));

  double dm_val = 0.0;
#if !V8_TARGET_ARCH_PPC64  // doesn't make sense in 64bit mode
  // Read the bits from the unsigned integer register_[] array
  // into the double precision floating point value and return it.
  char buffer[sizeof(fp_registers_[0])];
  memcpy(buffer, &registers_[reg], 2 * sizeof(registers_[0]));
  memcpy(&dm_val, buffer, 2 * sizeof(registers_[0]));
#endif
  return (dm_val);
}

// Raw access to the PC register.
void Simulator::set_pc(intptr_t value) {
  pc_modified_ = true;
  special_reg_pc_ = value;
}

bool Simulator::has_bad_pc() const {
  return ((special_reg_pc_ == bad_lr) || (special_reg_pc_ == end_sim_pc));
}

// Raw access to the PC register without the special adjustment when reading.
intptr_t Simulator::get_pc() const { return special_reg_pc_; }

// Accessor to the internal Link Register
intptr_t Simulator::get_lr() const { return special_reg_lr_; }

// Runtime FP routines take:
// - two double arguments
// - one double argument and zero or one integer arguments.
// All are consructed here from d1, d2 and r3.
void Simulator::GetFpArgs(double* x, double* y, intptr_t* z) {
  *x = get_double_from_d_register(1);
  *y = get_double_from_d_register(2);
  *z = get_register(3);
}

// The return value is in d1.
void Simulator::SetFpResult(const double& result) {
  set_d_register_from_double(1, result);
}

void Simulator::TrashCallerSaveRegisters() {
// We don't trash the registers with the return value.
#if 0  // A good idea to trash volatile registers, needs to be done
  registers_[2] = 0x50BAD4U;
  registers_[3] = 0x50BAD4U;
  registers_[12] = 0x50BAD4U;
#endif
}

#define GENERATE_RW_FUNC(size, type)                             \
  type Simulator::Read##size(uintptr_t addr) {                   \
    type value;                                                  \
    Read(addr, &value);                                          \
    return value;                                                \
  }                                                              \
  type Simulator::ReadEx##size(uintptr_t addr) {                 \
    type value;                                                  \
    ReadEx(addr, &value);                                        \
    return value;                                                \
  }                                                              \
  void Simulator::Write##size(uintptr_t addr, type value) {      \
    Write(addr, value);                                          \
  }                                                              \
  int32_t Simulator::WriteEx##size(uintptr_t addr, type value) { \
    return WriteEx(addr, value);                                 \
  }

RW_VAR_LIST(GENERATE_RW_FUNC)
#undef GENERATE_RW_FUNC

// Returns the limit of the stack area to enable checking for stack overflows.
uintptr_t Simulator::StackLimit(uintptr_t c_limit) const {
  // The simulator uses a separate JS stack. If we have exhausted the C stack,
  // we also drop down the JS limit to reflect the exhaustion on the JS stack.
  if (base::Stack::GetCurrentStackPosition() < c_limit) {
    return reinterpret_cast<uintptr_t>(get_sp());
  }

  // Otherwise the limit is the JS stack. Leave a safety margin to prevent
  // overrunning the stack when pushing values.
  return reinterpret_cast<uintptr_t>(stack_) + kStackProtectionSize;
}

base::Vector<uint8_t> Simulator::GetCurrentStackView() const {
  // We do not add an additional safety margin as above in
  // Simulator::StackLimit, as this is currently only used in wasm::StackMemory,
  // which adds its own margin.
  return base::VectorOf(stack_, UsableStackSize());
}

// Unsupported instructions use Format to print an error and stop execution.
void Simulator::Format(Instruction* instr, const char* format) {
  PrintF("Simulator found unsupported instruction:\n 0x%08" V8PRIxPTR ": %s\n",
         reinterpret_cast<intptr_t>(instr), format);
  UNIMPLEMENTED();
}

// Calculate C flag value for additions.
bool Simulator::CarryFrom(int32_t left, int32_t right, int32_t carry) {
  uint32_t uleft = static_cast<uint32_t>(left);
  uint32_t uright = static_cast<uint32_t>(right);
  uint32_t urest = 0xFFFFFFFFU - uleft;

  return (uright > urest) ||
         (carry && (((uright + 1) > urest) || (uright > (urest - 1))));
}

// Calculate C flag value for subtractions.
bool Simulator::BorrowFrom(int32_t left, int32_t right) {
  uint32_t uleft = static_cast<uint32_t>(left);
  uint32_t uright = static_cast<uint32_t>(right);

  return (uright > uleft);
}

// Calculate V flag value for additions and subtractions.
bool Simulator::OverflowFrom(int32_t alu_out, int32_t left, int32_t right,
                             bool addition) {
  bool overflow;
  if (addition) {
    // operands have the same sign
    overflow = ((left >= 0 && right >= 0) || (left < 0 && right < 0))
               // and operands and result have different sign
               && ((left < 0 && alu_out >= 0) || (left >= 0 && alu_out < 0));
  } else {
    // operands have different signs
    overflow = ((left < 0 && right >= 0) || (left >= 0 && right < 0))
               // and first operand and result have different signs
               && ((left < 0 && alu_out >= 0) || (left >= 0 && alu_out < 0));
  }
  return overflow;
}

static void decodeObjectPair(ObjectPair* pair, intptr_t* x, intptr_t* y) {
  *x = static_cast<intptr_t>(pair->x);
  *y = static_cast<intptr_t>(pair->y);
}

// Calls into the V8 runtime.
using SimulatorRuntimeCall = intptr_t (*)(
    intptr_t arg0, intptr_t arg1, intptr_t arg2, intptr_t arg3, intptr_t arg4,
    intptr_t arg5, intptr_t arg6, intptr_t arg7, intptr_t arg8, intptr_t arg9,
    intptr_t arg10, intptr_t arg11, intptr_t arg12, intptr_t arg13,
    intptr_t arg14, intptr_t arg15, intptr_t arg16, intptr_t arg17,
    intptr_t arg18, intptr_t arg19);
using SimulatorRuntimePairCall = ObjectPair (*)(
    intptr_t arg0, intptr_t arg1, intptr_t arg2, intptr_t arg3, intptr_t arg4,
    intptr_t arg5, intptr_t arg6, intptr_t arg7, intptr_t arg8, intptr_t arg9,
    intptr_t arg10, intptr_t arg11, intptr_t arg12, intptr_t arg13,
    intptr_t arg14, intptr_t arg15, intptr_t arg16, intptr_t arg17,
    intptr_t arg18, intptr_t arg19);

// These prototypes handle the four types of FP calls.
using SimulatorRuntimeCompareCall = int (*)(double darg0, double darg1);
using SimulatorRuntimeFPFPCall = double (*)(double darg0, double darg1);
using SimulatorRuntimeFPCall = double (*)(double darg0);
using SimulatorRuntimeFPIntCall = double (*)(double darg0, intptr_t arg0);
// Define four args for future flexibility; at the time of this writing only
// one is ever used.
using SimulatorRuntimeFPTaggedCall = double (*)(int32_t arg0, int32_t arg1,
                                                int32_t arg2, int32_t arg3);

// This signature supports direct call in to API function native callback
// (refer to InvocationCallback in v8.h).
using SimulatorRuntimeDirectApiCall = void (*)(intptr_t arg0);

// This signature supports direct call to accessor getter callback.
using SimulatorRuntimeDirectGetterCall = void (*)(intptr_t arg0, intptr_t arg1);

// Software interrupt instructions are used by the simulator to call into the
// C-based V8 runtime.
void Simulator::SoftwareInterrupt(Instruction* instr) {
  int svc = instr->SvcValue();
  switch (svc) {
    case kCallRtRedirected: {
      // Check if stack is aligned. Error if not aligned is reported below to
      // include information on the function called.
      bool stack_aligned =
          (get_register(sp) & (v8_flags.sim_stack_alignment - 1)) == 0;
      Redirection* redirection = Redirection::FromInstruction(instr);
      const int kArgCount = 20;
      const int kRegisterArgCount = 8;
      int arg0_regnum = 3;
      intptr_t result_buffer = 0;
      bool uses_result_buffer =
          (redirection->type() == ExternalReference::BUILTIN_CALL_PAIR &&
           !ABI_RETURNS_OBJECT_PAIRS_IN_REGS);
      if (uses_result_buffer) {
        result_buffer = get_register(r3);
        arg0_regnum++;
      }
      intptr_t arg[kArgCount];
      // First eight arguments in registers r3-r10.
      for (int i = 0; i < kRegisterArgCount; i++) {
        arg[i] = get_register(arg0_regnum + i);
      }
      intptr_t* stack_pointer = reinterpret_cast<intptr_t*>(get_register(sp));
      // Remaining argument on stack
      for (int i = kRegisterArgCount, j = 0; i < kArgCount; i++, j++) {
        arg[i] = stack_pointer[kStackFrameExtraParamSlot + j];
      }
      static_assert(kArgCount == kRegisterArgCount + 12);
      static_assert(kMaxCParameters == kArgCount);
      bool fp_call =
          (redirection->type() == ExternalReference::BUILTIN_FP_FP_CALL) ||
          (redirection->type() == ExternalReference::BUILTIN_COMPARE_CALL) ||
          (redirection->type() == ExternalReference::BUILTIN_FP_CALL) ||
          (redirection->type() == ExternalReference::BUILTIN_FP_INT_CALL);
      // This is dodgy but it works because the C entry stubs are never moved.
      // See comment in codegen-arm.cc and bug 1242173.
      intptr_t saved_lr = special_reg_lr_;
      intptr_t external =
          reinterpret_cast<intptr_t>(redirection->external_function());
      if (fp_call) {
        double dval0, dval1;  // one or two double parameters
        intptr_t ival;        // zero or one integer parameters
        int iresult = 0;      // integer return value
        double dresult = 0;   // double return value
        GetFpArgs(&dval0, &dval1, &ival);
        if (v8_flags.trace_sim || !stack_aligned) {
          SimulatorRuntimeCall generic_target =
              reinterpret_cast<SimulatorRuntimeCall>(external);
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
              PrintF("Call to host function at %p with args %f, %" V8PRIdPTR,
                     reinterpret_cast<void*>(FUNCTION_ADDR(generic_target)),
                     dval0, ival);
              break;
            default:
              UNREACHABLE();
          }
          if (!stack_aligned) {
            PrintF(" with unaligned stack %08" V8PRIxPTR "\n",
                   get_register(sp));
          }
          PrintF("\n");
        }
        CHECK(stack_aligned);
        switch (redirection->type()) {
          case ExternalReference::BUILTIN_COMPARE_CALL: {
            SimulatorRuntimeCompareCall target =
                reinterpret_cast<SimulatorRuntimeCompareCall>(external);
            iresult = target(dval0, dval1);
            set_register(r3, iresult);
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
              PrintF("Returned %08x\n", iresult);
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
        if (v8_flags.trace_sim || !stack_aligned) {
          PrintF("Call to host function at %p args %08" V8PRIxPTR,
                 reinterpret_cast<void*>(external), arg[0]);
          if (!stack_aligned) {
            PrintF(" with unaligned stack %08" V8PRIxPTR "\n",
                   get_register(sp));
          }
          PrintF("\n");
        }
        CHECK(stack_aligned);
        SimulatorRuntimeFPTaggedCall target =
            reinterpret_cast<SimulatorRuntimeFPTaggedCall>(external);
        double dresult = target(arg[0], arg[1], arg[2], arg[3]);
#ifdef DEBUG
        TrashCallerSaveRegisters();
#endif
        SetFpResult(dresult);
        if (v8_flags.trace_sim) {
          PrintF("Returned %f\n", dresult);
        }
      } else if (redirection->type() == ExternalReference::DIRECT_API_CALL) {
        // See callers of MacroAssembler::CallApiFunctionAndReturn for
        // explanation of register usage.
        // void f(v8::FunctionCallbackInfo&)
        if (v8_flags.trace_sim || !stack_aligned) {
          PrintF("Call to host function at %p args %08" V8PRIxPTR,
                 reinterpret_cast<void*>(external), arg[0]);
          if (!stack_aligned) {
            PrintF(" with unaligned stack %08" V8PRIxPTR "\n",
                   get_register(sp));
          }
          PrintF("\n");
        }
        CHECK(stack_aligned);
        SimulatorRuntimeDirectApiCall target =
            reinterpret_cast<SimulatorRuntimeDirectApiCall>(external);
        target(arg[0]);
      } else if (redirection->type() == ExternalReference::DIRECT_GETTER_CALL) {
        // See callers of MacroAssembler::CallApiFunctionAndReturn for
        // explanation of register usage.
        // void f(v8::Local<String> property, v8::PropertyCallbackInfo& info)
        if (v8_flags.trace_sim || !stack_aligned) {
          PrintF("Call to host function at %p args %08" V8PRIxPTR
                 " %08" V8PRIxPTR,
                 reinterpret_cast<void*>(external), arg[0], arg[1]);
          if (!stack_aligned) {
            PrintF(" with unaligned stack %08" V8PRIxPTR "\n",
                   get_register(sp));
          }
          PrintF("\n");
        }
        CHECK(stack_aligned);
        SimulatorRuntimeDirectGetterCall target =
            reinterpret_cast<SimulatorRuntimeDirectGetterCall>(external);
        if (!ABI_PASSES_HANDLES_IN_REGS) {
          arg[0] = base::bit_cast<intptr_t>(arg[0]);
        }
        target(arg[0], arg[1]);
      } else {
        // builtin call.
        if (v8_flags.trace_sim || !stack_aligned) {
          SimulatorRuntimeCall target =
              reinterpret_cast<SimulatorRuntimeCall>(external);
          PrintF(
              "Call to host function at %p,\n"
              "\t\t\t\targs %08" V8PRIxPTR ", %08" V8PRIxPTR ", %08" V8PRIxPTR
              ", %08" V8PRIxPTR ", %08" V8PRIxPTR ", %08" V8PRIxPTR
              ", %08" V8PRIxPTR ", %08" V8PRIxPTR ", %08" V8PRIxPTR
              ", %08" V8PRIxPTR ", %08" V8PRIxPTR ", %08" V8PRIxPTR
              ", %08" V8PRIxPTR ", %08" V8PRIxPTR ", %08" V8PRIxPTR
              ", %08" V8PRIxPTR ", %08" V8PRIxPTR ", %08" V8PRIxPTR
              ", %08" V8PRIxPTR ", %08" V8PRIxPTR,
              reinterpret_cast<void*>(FUNCTION_ADDR(target)), arg[0], arg[1],
              arg[2], arg[3], arg[4], arg[5], arg[6], arg[7], arg[8], arg[9],
              arg[10], arg[11], arg[12], arg[13], arg[14], arg[15], arg[16],
              arg[17], arg[18], arg[19]);
          if (!stack_aligned) {
            PrintF(" with unaligned stack %08" V8PRIxPTR "\n",
                   get_register(sp));
          }
          PrintF("\n");
        }
        CHECK(stack_aligned);
        if (redirection->type() == ExternalReference::BUILTIN_CALL_PAIR) {
          SimulatorRuntimePairCall target =
              reinterpret_cast<SimulatorRuntimePairCall>(external);
          ObjectPair result =
              target(arg[0], arg[1], arg[2], arg[3], arg[4], arg[5], arg[6],
                     arg[7], arg[8], arg[9], arg[10], arg[11], arg[12], arg[13],
                     arg[14], arg[15], arg[16], arg[17], arg[18], arg[19]);
          intptr_t x;
          intptr_t y;
          decodeObjectPair(&result, &x, &y);
          if (v8_flags.trace_sim) {
            PrintF("Returned {%08" V8PRIxPTR ", %08" V8PRIxPTR "}\n", x, y);
          }
          if (ABI_RETURNS_OBJECT_PAIRS_IN_REGS) {
            set_register(r3, x);
            set_register(r4, y);
          } else {
            memcpy(reinterpret_cast<void*>(result_buffer), &result,
                   sizeof(ObjectPair));
            set_register(r3, result_buffer);
          }
        } else {
          // FAST_C_CALL is temporarily handled here as well, because we lack
          // proper support for direct C calls with FP params in the simulator.
          // The generic BUILTIN_CALL path assumes all parameters are passed in
          // the GP registers, thus supporting calling the slow callback without
          // crashing. The reason for that is that in the mjsunit tests we check
          // the `fast_c_api.supports_fp_params` (which is false on
          // non-simulator builds for arm/arm64), thus we expect that the slow
          // path will be called. And since the slow path passes the arguments
          // as a `const FunctionCallbackInfo<Value>&` (which is a GP argument),
          // the call is made correctly.
          DCHECK(redirection->type() == ExternalReference::BUILTIN_CALL ||
                 redirection->type() == ExternalReference::FAST_C_CALL);
          SimulatorRuntimeCall target =
              reinterpret_cast<SimulatorRuntimeCall>(external);
          intptr_t result =
              target(arg[0], arg[1], arg[2], arg[3], arg[4], arg[5], arg[6],
                     arg[7], arg[8], arg[9], arg[10], arg[11], arg[12], arg[13],
                     arg[14], arg[15], arg[16], arg[17], arg[18], arg[19]);
          if (v8_flags.trace_sim) {
            PrintF("Returned %08" V8PRIxPTR "\n", result);
          }
          set_register(r3, result);
        }
      }
      set_pc(saved_lr);
      break;
    }
    case kBreakpoint:
      PPCDebugger(this).Debug();
      break;
    // stop uses all codes greater than 1 << 23.
    default:
      if (svc >= (1 << 23)) {
        uint32_t code = svc & kStopCodeMask;
        if (isWatchedStop(code)) {
          IncreaseStopCounter(code);
        }
        // Stop if it is enabled, otherwise go on jumping over the stop
        // and the message address.
        if (isEnabledStop(code)) {
          if (code != kMaxStopCode) {
            PrintF("Simulator hit stop %u. ", code);
          } else {
            PrintF("Simulator hit stop. ");
          }
          DebugAtNextPC();
        } else {
          set_pc(get_pc() + kInstrSize + kSystemPointerSize);
        }
      } else {
        // This is not a valid svc code.
        UNREACHABLE();
      }
  }
}

// Stop helper functions.
bool Simulator::isStopInstruction(Instruction* instr) {
  return (instr->Bits(27, 24) == 0xF) && (instr->SvcValue() >= kStopCode);
}

bool Simulator::isWatchedStop(uint32_t code) {
  DCHECK_LE(code, kMaxStopCode);
  return code < kNumOfWatchedStops;
}

bool Simulator::isEnabledStop(uint32_t code) {
  DCHECK_LE(code, kMaxStopCode);
  // Unwatched stops are always enabled.
  return !isWatchedStop(code) ||
         !(watched_stops_[code].count & kStopDisabledBit);
}

void Simulator::EnableStop(uint32_t code) {
  DCHECK(isWatchedStop(code));
  if (!isEnabledStop(code)) {
    watched_stops_[code].count &= ~kStopDisabledBit;
  }
}

void Simulator::DisableStop(uint32_t code) {
  DCHECK(isWatchedStop(code));
  if (isEnabledStop(code)) {
    watched_stops_[code].count |= kStopDisabledBit;
  }
}

void Simulator::IncreaseStopCounter(uint32_t code) {
  DCHECK_LE(code, kMaxStopCode);
  DCHECK(isWatchedStop(code));
  if ((watched_stops_[code].count & ~(1 << 31)) == 0x7FFFFFFF) {
    PrintF(
        "Stop counter for code %i has overflowed.\n"
        "Enabling this code and reseting the counter to 0.\n",
        code);
    watched_stops_[code].count = 0;
    EnableStop(code);
  } else {
    watched_stops_[code].count++;
  }
}

// Print a stop status.
void Simulator::PrintStopInfo(uint32_t code) {
  DCHECK_LE(code, kMaxStopCode);
  if (!isWatchedStop(code)) {
    PrintF("Stop not watched.");
  } else {
    const char* state = isEnabledStop(code) ? "Enabled" : "Disabled";
    int32_t count = watched_stops_[code].count & ~kStopDisabledBit;
    // Don't print the state of unused breakpoints.
    if (count != 0) {
      if (watched_stops_[code].desc) {
        PrintF("stop %i - 0x%x: \t%s, \tcounter = %i, \t%s\n", code, code,
               state, count, watched_stops_[code].desc);
      } else {
        PrintF("stop %i - 0x%x: \t%s, \tcounter = %i\n", code, code, state,
               count);
      }
    }
  }
}

void Simulator::SetCR0(intptr_t result, bool setSO) {
  int bf = 0;
  if (result < 0) {
    bf |= 0x80000000;
  }
  if (result > 0) {
    bf |= 0x40000000;
  }
  if (result == 0) {
    bf |= 0x20000000;
  }
  if (setSO) {
    bf |= 0x10000000;
  }
  condition_reg_ = (condition_reg_ & ~0xF0000000) | bf;
}

void Simulator::SetCR6(bool true_for_all) {
  int32_t clear_cr6_mask = 0xFFFFFF0F;
  if (true_for_all) {
    condition_reg_ = (condition_reg_ & clear_cr6_mask) | 0x80;
  } else {
    condition_reg_ = (condition_reg_ & clear_cr6_mask) | 0x20;
  }
}

void Simulator::ExecuteBranchConditional(Instruction* instr, BCType type) {
  int bo = instr->Bits(25, 21) << 21;
  int condition_bit = instr->Bits(20, 16);
  int condition_mask = 0x80000000 >> condition_bit;
  switch (bo) {
    case DCBNZF:  // Decrement CTR; branch if CTR != 0 and condition false
    case DCBEZF:  // Decrement CTR; branch if CTR == 0 and condition false
      UNIMPLEMENTED();
    case BF: {  // Branch if condition false
      if (condition_reg_ & condition_mask) return;
      break;
    }
    case DCBNZT:  // Decrement CTR; branch if CTR != 0 and condition true
    case DCBEZT:  // Decrement CTR; branch if CTR == 0 and condition true
      UNIMPLEMENTED();
    case BT: {  // Branch if condition true
      if (!(condition_reg_ & condition_mask)) return;
      break;
    }
    case DCBNZ:  // Decrement CTR; branch if CTR != 0
    case DCBEZ:  // Decrement CTR; branch if CTR == 0
      special_reg_ctr_ -= 1;
      if ((special_reg_ctr_ == 0) != (bo == DCBEZ)) return;
      break;
    case BA: {  // Branch always
      break;
    }
    default:
      UNIMPLEMENTED();  // Invalid encoding
  }

  intptr_t old_pc = get_pc();

  switch (type) {
    case BC_OFFSET: {
      int offset = (instr->Bits(15, 2) << 18) >> 16;
      set_pc(old_pc + offset);
      break;
    }
    case BC_LINK_REG:
      set_pc(special_reg_lr_);
      break;
    case BC_CTR_REG:
      set_pc(special_reg_ctr_);
      break;
  }

  if (instr->Bit(0) == 1) {  // LK flag set
    special_reg_lr_ = old_pc + 4;
  }
}

// Vector instruction helpers.
#define GET_ADDRESS(a, b, a_val, b_val)          \
  intptr_t a_val = a == 0 ? 0 : get_register(a); \
  intptr_t b_val = get_register(b);
#define DECODE_VX_INSTRUCTION(d, a, b, source_or_target) \
  int d = instr->R##source_or_target##Value();           \
  int a = instr->RAValue();                              \
  int b = instr->RBValue();
#define FOR_EACH_LANE(i, type) \
  for (uint32_t i = 0; i < kSimd128Size / sizeof(type); i++)
template <typename A, typename T, typename Operation>
void VectorCompareOp(Simulator* sim, Instruction* instr, bool is_fp,
                     Operation op) {
  DECODE_VX_INSTRUCTION(t, a, b, T)
  bool true_for_all = true;
  FOR_EACH_LANE(i, A) {
    A a_val = sim->get_simd_register_by_lane<A>(a, i);
    A b_val = sim->get_simd_register_by_lane<A>(b, i);
    T t_val = 0;
    bool is_not_nan = is_fp ? !isnan(a_val) && !isnan(b_val) : true;
    if (is_not_nan && op(a_val, b_val)) {
      t_val = -1;  // Set all bits to 1 indicating true.
    } else {
      true_for_all = false;
    }
    sim->set_simd_register_by_lane<T>(t, i, t_val);
  }
  if (instr->Bit(10)) {  // RC bit set.
    sim->SetCR6(true_for_all);
  }
}

template <typename S, typename T>
void VectorConverFromFPSaturate(Simulator* sim, Instruction* instr, T min_val,
                                T max_val, bool even_lane_result = false) {
  int t = instr->RTValue();
  int b = instr->RBValue();
  FOR_EACH_LANE(i, S) {
    T t_val;
    double b_val = static_cast<double>(sim->get_simd_register_by_lane<S>(b, i));
    if (isnan(b_val)) {
      t_val = min_val;
    } else {
      // Round Towards Zero.
      b_val = std::trunc(b_val);
      if (b_val < min_val) {
        t_val = min_val;
      } else if (b_val > max_val) {
        t_val = max_val;
      } else {
        t_val = static_cast<T>(b_val);
      }
    }
    sim->set_simd_register_by_lane<T>(t, even_lane_result ? 2 * i : i, t_val);
  }
}

template <typename S, typename T>
void VectorPackSaturate(Simulator* sim, Instruction* instr, S min_val,
                        S max_val) {
  DECODE_VX_INSTRUCTION(t, a, b, T)
  int src = a;
  int count = 0;
  S value = 0;
  // Setup a temp array to avoid overwriting dst mid loop.
  T temps[kSimd128Size / sizeof(T)] = {0};
  for (size_t i = 0; i < kSimd128Size / sizeof(T); i++, count++) {
    if (count == kSimd128Size / sizeof(S)) {
      src = b;
      count = 0;
    }
    value = sim->get_simd_register_by_lane<S>(src, count);
    if (value > max_val) {
      value = max_val;
    } else if (value < min_val) {
      value = min_val;
    }
    temps[i] = static_cast<T>(value);
  }
  FOR_EACH_LANE(i, T) { sim->set_simd_register_by_lane<T>(t, i, temps[i]); }
}

template <typename T>
T VSXFPMin(T x, T y) {
  // Handle NaN.
  // TODO(miladfarca): include the payload of src1.
  if (std::isnan(x) && std::isnan(y)) return NAN;
  // Handle +0 and -0.
  if (std::signbit(x) < std::signbit(y)) return y;
  if (std::signbit(y) < std::signbit(x)) return x;
  return std::fmin(x, y);
}

template <typename T>
T VSXFPMax(T x, T y) {
  // Handle NaN.
  // TODO(miladfarca): include the payload of src1.
  if (std::isnan(x) && std::isnan(y)) return NAN;
  // Handle +0 and -0.
  if (std::signbit(x) < std::signbit(y)) return x;
  if (std::signbit(y) < std::signbit(x)) return y;
  return std::fmax(x, y);
}

float VMXFPMin(float x, float y) {
  // Handle NaN.
  if (std::isnan(x) || std::isnan(y)) return NAN;
  // Handle +0 and -0.
  if (std::signbit(x) < std::signbit(y)) return y;
  if (std::signbit(y) < std::signbit(x)) return x;
  return x < y ? x : y;
}

float VMXFPMax(float x, float y) {
  // Handle NaN.
  if (std::isnan(x) || std::isnan(y)) return NAN;
  // Handle +0 and -0.
  if (std::signbit(x) < std::signbit(y)) return x;
  if (std::signbit(y) < std::signbit(x)) return y;
  return x > y ? x : y;
}

void Simulator::ExecuteGeneric(Instruction* instr) {
  uint32_t opcode = instr->OpcodeBase();
  switch (opcode) {
      // Prefixed instructions.
    case PLOAD_STORE_8LS:
    case PLOAD_STORE_MLS: {
      // TODO(miladfarca): Simulate PC-relative capability indicated by the R
      // bit.
      DCHECK_NE(instr->Bit(20), 1);
      // Read prefix value.
      uint64_t prefix_value = instr->Bits(17, 0);
      // Read suffix (next instruction).
      Instruction* next_instr =
          reinterpret_cast<Instruction*>(get_pc() + kInstrSize);
      uint16_t suffix_value = next_instr->Bits(15, 0);
      int64_t im_val = SIGN_EXT_IMM34((prefix_value << 16) | suffix_value);
      switch (next_instr->OpcodeBase()) {
          // Prefixed ADDI.
        case ADDI: {
          int rt = next_instr->RTValue();
          int ra = next_instr->RAValue();
          intptr_t alu_out;
          if (ra == 0) {
            alu_out = im_val;
          } else {
            intptr_t ra_val = get_register(ra);
            alu_out = ra_val + im_val;
          }
          set_register(rt, alu_out);
          break;
        }
          // Prefixed LBZ.
        case LBZ: {
          int ra = next_instr->RAValue();
          int rt = next_instr->RTValue();
          intptr_t ra_val = ra == 0 ? 0 : get_register(ra);
          set_register(rt, ReadB(ra_val + im_val) & 0xFF);
          break;
        }
          // Prefixed LHZ.
        case LHZ: {
          int ra = next_instr->RAValue();
          int rt = next_instr->RTValue();
          intptr_t ra_val = ra == 0 ? 0 : get_register(ra);
          uintptr_t result = ReadHU(ra_val + im_val) & 0xFFFF;
          set_register(rt, result);
          break;
        }
          // Prefixed LHA.
        case LHA: {
          int ra = next_instr->RAValue();
          int rt = next_instr->RTValue();
          intptr_t ra_val = ra == 0 ? 0 : get_register(ra);
          intptr_t result = ReadH(ra_val + im_val);
          set_register(rt, result);
          break;
        }
          // Prefixed LWZ.
        case LWZ: {
          int ra = next_instr->RAValue();
          int rt = next_instr->RTValue();
          intptr_t ra_val = ra == 0 ? 0 : get_register(ra);
          set_register(rt, ReadWU(ra_val + im_val));
          break;
        }
          // Prefixed LWA.
        case PPLWA: {
          int ra = next_instr->RAValue();
          int rt = next_instr->RTValue();
          int64_t ra_val = ra == 0 ? 0 : get_register(ra);
          set_register(rt, ReadW(ra_val + im_val));
          break;
        }
          // Prefixed LD.
        case PPLD: {
          int ra = next_instr->RAValue();
          int rt = next_instr->RTValue();
          int64_t ra_val = ra == 0 ? 0 : get_register(ra);
          set_register(rt, ReadDW(ra_val + im_val));
          break;
        }
          // Prefixed LFS.
        case LFS: {
          int frt = next_instr->RTValue();
          int ra = next_instr->RAValue();
          intptr_t ra_val = ra == 0 ? 0 : get_register(ra);
          int32_t val = ReadW(ra_val + im_val);
          float* fptr = reinterpret_cast<float*>(&val);
#if V8_HOST_ARCH_IA32 || V8_HOST_ARCH_X64
          // Conversion using double changes sNan to qNan on ia32/x64
          if ((val & 0x7F800000) == 0x7F800000) {
            int64_t dval = static_cast<int64_t>(val);
            dval = ((dval & 0xC0000000) << 32) | ((dval & 0x40000000) << 31) |
                   ((dval & 0x40000000) << 30) | ((dval & 0x7FFFFFFF) << 29) |
                   0x0;
            set_d_register(frt, dval);
          } else {
            set_d_register_from_double(frt, static_cast<double>(*fptr));
          }
#else
          set_d_register_from_double(frt, static_cast<double>(*fptr));
#endif
          break;
        }
          // Prefixed LFD.
        case LFD: {
          int frt = next_instr->RTValue();
          int ra = next_instr->RAValue();
          intptr_t ra_val = ra == 0 ? 0 : get_register(ra);
          int64_t dptr = ReadDW(ra_val + im_val);
          set_d_register(frt, dptr);
          break;
        }
        // Prefixed STB.
        case STB: {
          int ra = next_instr->RAValue();
          int rs = next_instr->RSValue();
          intptr_t ra_val = ra == 0 ? 0 : get_register(ra);
          WriteB(ra_val + im_val, get_register(rs));
          break;
        }
        // Prefixed STH.
        case STH: {
          int ra = next_instr->RAValue();
          int rs = next_instr->RSValue();
          intptr_t ra_val = ra == 0 ? 0 : get_register(ra);
          WriteH(ra_val + im_val, get_register(rs));
          break;
        }
        // Prefixed STW.
        case STW: {
          int ra = next_instr->RAValue();
          int rs = next_instr->RSValue();
          intptr_t ra_val = ra == 0 ? 0 : get_register(ra);
          WriteW(ra_val + im_val, get_register(rs));
          break;
        }
        // Prefixed STD.
        case PPSTD: {
          int ra = next_instr->RAValue();
          int rs = next_instr->RSValue();
          intptr_t ra_val = ra == 0 ? 0 : get_register(ra);
          WriteDW(ra_val + im_val, get_register(rs));
          break;
        }
        // Prefixed STFS.
        case STFS: {
          int frs = next_instr->RSValue();
          int ra = next_instr->RAValue();
          intptr_t ra_val = ra == 0 ? 0 : get_register(ra);
          float frs_val = static_cast<float>(get_double_from_d_register(frs));
          int32_t* p;
#if V8_HOST_ARCH_IA32 || V8_HOST_ARCH_X64
          // Conversion using double changes sNan to qNan on ia32/x64
          int32_t sval = 0;
          int64_t dval = get_d_register(frs);
          if ((dval & 0x7FF0000000000000) == 0x7FF0000000000000) {
            sval = ((dval & 0xC000000000000000) >> 32) |
                   ((dval & 0x07FFFFFFE0000000) >> 29);
            p = &sval;
          } else {
            p = reinterpret_cast<int32_t*>(&frs_val);
          }
#else
          p = reinterpret_cast<int32_t*>(&frs_val);
#endif
          WriteW(ra_val + im_val, *p);
          break;
        }
        // Prefixed STFD.
        case STFD: {
          int frs = next_instr->RSValue();
          int ra = next_instr->RAValue();
          intptr_t ra_val = ra == 0 ? 0 : get_register(ra);
          int64_t frs_val = get_d_register(frs);
          WriteDW(ra_val + im_val, frs_val);
          break;
        }
        default:
          UNREACHABLE();
      }
      // We have now executed instructions at this as well as next pc.
      set_pc(get_pc() + (2 * kInstrSize));
      break;
    }
    case SUBFIC: {
      int rt = instr->RTValue();
      int ra = instr->RAValue();
      intptr_t ra_val = get_register(ra);
      int32_t im_val = instr->Bits(15, 0);
      im_val = SIGN_EXT_IMM16(im_val);
      intptr_t alu_out = im_val - ra_val;
      set_register(rt, alu_out);
      // todo - handle RC bit
      break;
    }
    case CMPLI: {
      int ra = instr->RAValue();
      uint32_t im_val = instr->Bits(15, 0);
      int cr = instr->Bits(25, 23);
      uint32_t bf = 0;
#if V8_TARGET_ARCH_PPC64
      int L = instr->Bit(21);
      if (L) {
#endif
        uintptr_t ra_val = get_register(ra);
        if (ra_val < im_val) {
          bf |= 0x80000000;
        }
        if (ra_val > im_val) {
          bf |= 0x40000000;
        }
        if (ra_val == im_val) {
          bf |= 0x20000000;
        }
#if V8_TARGET_ARCH_PPC64
      } else {
        uint32_t ra_val = get_register(ra);
        if (ra_val < im_val) {
          bf |= 0x80000000;
        }
        if (ra_val > im_val) {
          bf |= 0x40000000;
        }
        if (ra_val == im_val) {
          bf |= 0x20000000;
        }
      }
#endif
      uint32_t condition_mask = 0xF0000000U >> (cr * 4);
      uint32_t condition = bf >> (cr * 4);
      condition_reg_ = (condition_reg_ & ~condition_mask) | condition;
      break;
    }
    case CMPI: {
      int ra = instr->RAValue();
      int32_t im_val = instr->Bits(15, 0);
      im_val = SIGN_EXT_IMM16(im_val);
      int cr = instr->Bits(25, 23);
      uint32_t bf = 0;
#if V8_TARGET_ARCH_PPC64
      int L = instr->Bit(21);
      if (L) {
#endif
        intptr_t ra_val = get_register(ra);
        if (ra_val < im_val) {
          bf |= 0x80000000;
        }
        if (ra_val > im_val) {
          bf |= 0x40000000;
        }
        if (ra_val == im_val) {
          bf |= 0x20000000;
        }
#if V8_TARGET_ARCH_PPC64
      } else {
        int32_t ra_val = get_register(ra);
        if (ra_val < im_val) {
          bf |= 0x80000000;
        }
        if (ra_val > im_val) {
          bf |= 0x40000000;
        }
        if (ra_val == im_val) {
          bf |= 0x20000000;
        }
      }
#endif
      uint32_t condition_mask = 0xF0000000U >> (cr * 4);
      uint32_t condition = bf >> (cr * 4);
      condition_reg_ = (condition_reg_ & ~condition_mask) | condition;
      break;
    }
    case ADDIC: {
      int rt = instr->RTValue();
      int ra = instr->RAValue();
      uintptr_t ra_val = get_register(ra);
      uintptr_t im_val = SIGN_EXT_IMM16(instr->Bits(15, 0));
      uintptr_t alu_out = ra_val + im_val;
      // Check overflow
      if (~ra_val < im_val) {
        special_reg_xer_ = (special_reg_xer_ & ~0xF0000000) | 0x20000000;
      } else {
        special_reg_xer_ &= ~0xF0000000;
      }
      set_register(rt, alu_out);
      break;
    }
    case ADDI: {
      int rt = instr->RTValue();
      int ra = instr->RAValue();
      int32_t im_val = SIGN_EXT_IMM16(instr->Bits(15, 0));
      intptr_t alu_out;
      if (ra == 0) {
        alu_out = im_val;
      } else {
        intptr_t ra_val = get_register(ra);
        alu_out = ra_val + im_val;
      }
      set_register(rt, alu_out);
      // todo - handle RC bit
      break;
    }
    case ADDIS: {
      int rt = instr->RTValue();
      int ra = instr->RAValue();
      int32_t im_val = (instr->Bits(15, 0) << 16);
      intptr_t alu_out;
      if (ra == 0) {  // treat r0 as zero
        alu_out = im_val;
      } else {
        intptr_t ra_val = get_register(ra);
        alu_out = ra_val + im_val;
      }
      set_register(rt, alu_out);
      break;
    }
    case BCX: {
      ExecuteBranchConditional(instr, BC_OFFSET);
      break;
    }
    case BX: {
      int offset = (instr->Bits(25, 2) << 8) >> 6;
      if (instr->Bit(0) == 1) {  // LK flag set
        special_reg_lr_ = get_pc() + 4;
      }
      set_pc(get_pc() + offset);
      // todo - AA flag
      break;
    }
    case MCRF:
      UNIMPLEMENTED();  // Not used by V8.
    case BCLRX:
      ExecuteBranchConditional(instr, BC_LINK_REG);
      break;
    case BCCTRX:
      ExecuteBranchConditional(instr, BC_CTR_REG);
      break;
    case CRNOR:
    case RFI:
    case CRANDC:
      UNIMPLEMENTED();
    case ISYNC: {
      // todo - simulate isync
      break;
    }
    case CRXOR: {
      int bt = instr->Bits(25, 21);
      int ba = instr->Bits(20, 16);
      int bb = instr->Bits(15, 11);
      int ba_val = ((0x80000000 >> ba) & condition_reg_) == 0 ? 0 : 1;
      int bb_val = ((0x80000000 >> bb) & condition_reg_) == 0 ? 0 : 1;
      int bt_val = ba_val ^ bb_val;
      bt_val = bt_val << (31 - bt);  // shift bit to correct destination
      condition_reg_ &= ~(0x80000000 >> bt);
      condition_reg_ |= bt_val;
      break;
    }
    case CREQV: {
      int bt = instr->Bits(25, 21);
      int ba = instr->Bits(20, 16);
      int bb = instr->Bits(15, 11);
      int ba_val = ((0x80000000 >> ba) & condition_reg_) == 0 ? 0 : 1;
      int bb_val = ((0x80000000 >> bb) & condition_reg_) == 0 ? 0 : 1;
      int bt_val = 1 - (ba_val ^ bb_val);
      bt_val = bt_val << (31 - bt);  // shift bit to correct destination
      condition_reg_ &= ~(0x80000000 >> bt);
      condition_reg_ |= bt_val;
      break;
    }
    case CRNAND:
    case CRAND:
    case CRORC:
    case CROR: {
      UNIMPLEMENTED();  // Not used by V8.
    }
    case RLWIMIX: {
      int ra = instr->RAValue();
      int rs = instr->RSValue();
      uint32_t rs_val = get_register(rs);
      int32_t ra_val = get_register(ra);
      int sh = instr->Bits(15, 11);
      int mb = instr->Bits(10, 6);
      int me = instr->Bits(5, 1);
      uint32_t result = base::bits::RotateLeft32(rs_val, sh);
      int mask = 0;
      if (mb < me + 1) {
        int bit = 0x80000000 >> mb;
        for (; mb <= me; mb++) {
          mask |= bit;
          bit >>= 1;
        }
      } else if (mb == me + 1) {
        mask = 0xFFFFFFFF;
      } else {                             // mb > me+1
        int bit = 0x80000000 >> (me + 1);  // needs to be tested
        mask = 0xFFFFFFFF;
        for (; me < mb; me++) {
          mask ^= bit;
          bit >>= 1;
        }
      }
      result &= mask;
      ra_val &= ~mask;
      result |= ra_val;
      set_register(ra, result);
      if (instr->Bit(0)) {  // RC bit set
        SetCR0(result);
      }
      break;
    }
    case RLWINMX:
    case RLWNMX: {
      int ra = instr->RAValue();
      int rs = instr->RSValue();
      uint32_t rs_val = get_register(rs);
      int sh = 0;
      if (opcode == RLWINMX) {
        sh = instr->Bits(15, 11);
      } else {
        int rb = instr->RBValue();
        uint32_t rb_val = get_register(rb);
        sh = (rb_val & 0x1F);
      }
      int mb = instr->Bits(10, 6);
      int me = instr->Bits(5, 1);
      uint32_t result = base::bits::RotateLeft32(rs_val, sh);
      int mask = 0;
      if (mb < me + 1) {
        int bit = 0x80000000 >> mb;
        for (; mb <= me; mb++) {
          mask |= bit;
          bit >>= 1;
        }
      } else if (mb == me + 1) {
        mask = 0xFFFFFFFF;
      } else {                             // mb > me+1
        int bit = 0x80000000 >> (me + 1);  // needs to be tested
        mask = 0xFFFFFFFF;
        for (; me < mb; me++) {
          mask ^= bit;
          bit >>= 1;
        }
      }
      result &= mask;
      set_register(ra, result);
      if (instr->Bit(0)) {  // RC bit set
        SetCR0(result);
      }
      break;
    }
    case ORI: {
      int rs = instr->RSValue();
      int ra = instr->RAValue();
      intptr_t rs_val = get_register(rs);
      uint32_t im_val = instr->Bits(15, 0);
      intptr_t alu_out = rs_val | im_val;
      set_register(ra, alu_out);
      break;
    }
    case ORIS: {
      int rs = instr->RSValue();
      int ra = instr->RAValue();
      intptr_t rs_val = get_register(rs);
      uint32_t im_val = instr->Bits(15, 0);
      intptr_t alu_out = rs_val | (im_val << 16);
      set_register(ra, alu_out);
      break;
    }
    case XORI: {
      int rs = instr->RSValue();
      int ra = instr->RAValue();
      intptr_t rs_val = get_register(rs);
      uint32_t im_val = instr->Bits(15, 0);
      intptr_t alu_out = rs_val ^ im_val;
      set_register(ra, alu_out);
      // todo - set condition based SO bit
      break;
    }
    case XORIS: {
      int rs = instr->RSValue();
      int ra = instr->RAValue();
      intptr_t rs_val = get_register(rs);
      uint32_t im_val = instr->Bits(15, 0);
      intptr_t alu_out = rs_val ^ (im_val << 16);
      set_register(ra, alu_out);
      break;
    }
    case ANDIx: {
      int rs = instr->RSValue();
      int ra = instr->RAValue();
      intptr_t rs_val = get_register(rs);
      uint32_t im_val = instr->Bits(15, 0);
      intptr_t alu_out = rs_val & im_val;
      set_register(ra, alu_out);
      SetCR0(alu_out);
      break;
    }
    case ANDISx: {
      int rs = instr->RSValue();
      int ra = instr->RAValue();
      intptr_t rs_val = get_register(rs);
      uint32_t im_val = instr->Bits(15, 0);
      intptr_t alu_out = rs_val & (im_val << 16);
      set_register(ra, alu_out);
      SetCR0(alu_out);
      break;
    }
    case SRWX: {
      int rs = instr->RSValue();
      int ra = instr->RAValue();
      int rb = instr->RBValue();
      uint32_t rs_val = get_register(rs);
      uintptr_t rb_val = get_register(rb) & 0x3F;
      intptr_t result = (rb_val > 31) ? 0 : rs_val >> rb_val;
      set_register(ra, result);
      if (instr->Bit(0)) {  // RC bit set
        SetCR0(result);
      }
      break;
    }
#if V8_TARGET_ARCH_PPC64
    case SRDX: {
      int rs = instr->RSValue();
      int ra = instr->RAValue();
      int rb = instr->RBValue();
      uintptr_t rs_val = get_register(rs);
      uintptr_t rb_val = get_register(rb) & 0x7F;
      intptr_t result = (rb_val > 63) ? 0 : rs_val >> rb_val;
      set_register(ra, result);
      if (instr->Bit(0)) {  // RC bit set
        SetCR0(result);
      }
      break;
    }
#endif
    case MODUW: {
      int rt = instr->RTValue();
      int ra = instr->RAValue();
      int rb = instr->RBValue();
      uint32_t ra_val = get_register(ra);
      uint32_t rb_val = get_register(rb);
      uint32_t alu_out = (rb_val == 0) ? -1 : ra_val % rb_val;
      set_register(rt, alu_out);
      break;
    }
#if V8_TARGET_ARCH_PPC64
    case MODUD: {
      int rt = instr->RTValue();
      int ra = instr->RAValue();
      int rb = instr->RBValue();
      uint64_t ra_val = get_register(ra);
      uint64_t rb_val = get_register(rb);
      uint64_t alu_out = (rb_val == 0) ? -1 : ra_val % rb_val;
      set_register(rt, alu_out);
      break;
    }
#endif
    case MODSW: {
      int rt = instr->RTValue();
      int ra = instr->RAValue();
      int rb = instr->RBValue();
      int32_t ra_val = get_register(ra);
      int32_t rb_val = get_register(rb);
      bool overflow = (ra_val == kMinInt && rb_val == -1);
      // result is undefined if divisor is zero or if operation
      // is 0x80000000 / -1.
      int32_t alu_out = (rb_val == 0 || overflow) ? -1 : ra_val % rb_val;
      set_register(rt, alu_out);
      break;
    }
#if V8_TARGET_ARCH_PPC64
    case MODSD: {
      int rt = instr->RTValue();
      int ra = instr->RAValue();
      int rb = instr->RBValue();
      int64_t ra_val = get_register(ra);
      int64_t rb_val = get_register(rb);
      int64_t one = 1;  // work-around gcc
      int64_t kMinLongLong = (one << 63);
      // result is undefined if divisor is zero or if operation
      // is 0x80000000_00000000 / -1.
      int64_t alu_out =
          (rb_val == 0 || (ra_val == kMinLongLong && rb_val == -1))
              ? -1
              : ra_val % rb_val;
      set_register(rt, alu_out);
      break;
    }
#endif
    case SRAW: {
      int rs = instr->RSValue();
      int ra = instr->RAValue();
      int rb = instr->RBValue();
      int32_t rs_val = get_register(rs);
      intptr_t rb_val = get_register(rb) & 0x3F;
      intptr_t result = (rb_val > 31) ? rs_val >> 31 : rs_val >> rb_val;
      set_register(ra, result);
      if (instr->Bit(0)) {  // RC bit set
        SetCR0(result);
      }
      break;
    }
#if V8_TARGET_ARCH_PPC64
    case SRAD: {
      int rs = instr->RSValue();
      int ra = instr->RAValue();
      int rb = instr->RBValue();
      intptr_t rs_val = get_register(rs);
      intptr_t rb_val = get_register(rb) & 0x7F;
      intptr_t result = (rb_val > 63) ? rs_val >> 63 : rs_val >> rb_val;
      set_register(ra, result);
      if (instr->Bit(0)) {  // RC bit set
        SetCR0(result);
      }
      break;
    }
#endif
    case SRAWIX: {
      int ra = instr->RAValue();
      int rs = instr->RSValue();
      int sh = instr->Bits(15, 11);
      int32_t rs_val = get_register(rs);
      intptr_t result = rs_val >> sh;
      set_register(ra, result);
      if (instr->Bit(0)) {  // RC bit set
        SetCR0(result);
      }
      break;
    }
#if V8_TARGET_ARCH_PPC64
    case EXTSW: {
      const int shift = kBitsPerSystemPointer - 32;
      int ra = instr->RAValue();
      int rs = instr->RSValue();
      intptr_t rs_val = get_register(rs);
      intptr_t ra_val = (rs_val << shift) >> shift;
      set_register(ra, ra_val);
      if (instr->Bit(0)) {  // RC bit set
        SetCR0(ra_val);
      }
      break;
    }
#endif
    case EXTSH: {
      const int shift = kBitsPerSystemPointer - 16;
      int ra = instr->RAValue();
      int rs = instr->RSValue();
      intptr_t rs_val = get_register(rs);
      intptr_t ra_val = (rs_val << shift) >> shift;
      set_register(ra, ra_val);
      if (instr->Bit(0)) {  // RC bit set
        SetCR0(ra_val);
      }
      break;
    }
    case EXTSB: {
      const int shift = kBitsPerSystemPointer - 8;
      int ra = instr->RAValue();
      int rs = instr->RSValue();
      intptr_t rs_val = get_register(rs);
      intptr_t ra_val = (rs_val << shift) >> shift;
      set_register(ra, ra_val);
      if (instr->Bit(0)) {  // RC bit set
        SetCR0(ra_val);
      }
      break;
    }
    case LFSUX:
    case LFSX: {
      int frt = instr->RTValue();
      int ra = instr->RAValue();
      int rb = instr->RBValue();
      intptr_t ra_val = ra == 0 ? 0 : get_register(ra);
      intptr_t rb_val = get_register(rb);
      int32_t val = ReadW(ra_val + rb_val);
      float* fptr = reinterpret_cast<float*>(&val);
#if V8_HOST_ARCH_IA32 || V8_HOST_ARCH_X64
      // Conversion using double changes sNan to qNan on ia32/x64
      if ((val & 0x7F800000) == 0x7F800000) {
        int64_t dval = static_cast<int64_t>(val);
        dval = ((dval & 0xC0000000) << 32) | ((dval & 0x40000000) << 31) |
               ((dval & 0x40000000) << 30) | ((dval & 0x7FFFFFFF) << 29) | 0x0;
        set_d_register(frt, dval);
      } else {
        set_d_register_from_double(frt, static_cast<double>(*fptr));
      }
#else
      set_d_register_from_double(frt, static_cast<double>(*fptr));
#endif
      if (opcode == LFSUX) {
        DCHECK_NE(ra, 0);
        set_register(ra, ra_val + rb_val);
      }
      break;
    }
    case LFDUX:
    case LFDX: {
      int frt = instr->RTValue();
      int ra = instr->RAValue();
      int rb = instr->RBValue();
      intptr_t ra_val = ra == 0 ? 0 : get_register(ra);
      intptr_t rb_val = get_register(rb);
      int64_t dptr = ReadDW(ra_val + rb_val);
      set_d_register(frt, dptr);
      if (opcode == LFDUX) {
        DCHECK_NE(ra, 0);
        set_register(ra, ra_val + rb_val);
      }
      break;
    }
    case STFSUX:
      [[fallthrough]];
    case STFSX: {
      int frs = instr->RSValue();
      int ra = instr->RAValue();
      int rb = instr->RBValue();
      intptr_t ra_val = ra == 0 ? 0 : get_register(ra);
      intptr_t rb_val = get_register(rb);
      float frs_val = static_cast<float>(get_double_from_d_register(frs));
      int32_t* p = reinterpret_cast<int32_t*>(&frs_val);
#if V8_HOST_ARCH_IA32 || V8_HOST_ARCH_X64
      // Conversion using double changes sNan to qNan on ia32/x64
      int32_t sval = 0;
      int64_t dval = get_d_register(frs);
      if ((dval & 0x7FF0000000000000) == 0x7FF0000000000000) {
        sval = ((dval & 0xC000000000000000) >> 32) |
               ((dval & 0x07FFFFFFE0000000) >> 29);
        p = &sval;
      } else {
        p = reinterpret_cast<int32_t*>(&frs_val);
      }
#else
      p = reinterpret_cast<int32_t*>(&frs_val);
#endif
      WriteW(ra_val + rb_val, *p);
      if (opcode == STFSUX) {
        DCHECK_NE(ra, 0);
        set_register(ra, ra_val + rb_val);
      }
      break;
    }
    case STFDUX:
      [[fallthrough]];
    case STFDX: {
      int frs = instr->RSValue();
      int ra = instr->RAValue();
      int rb = instr->RBValue();
      intptr_t ra_val = ra == 0 ? 0 : get_register(ra);
      intptr_t rb_val = get_register(rb);
      int64_t frs_val = get_d_register(frs);
      WriteDW(ra_val + rb_val, frs_val);
      if (opcode == STFDUX) {
        DCHECK_NE(ra, 0);
        set_register(ra, ra_val + rb_val);
      }
      break;
    }
    case POPCNTW: {
      int rs = instr->RSValue();
      int ra = instr->RAValue();
      uintptr_t rs_val = get_register(rs);
      uintptr_t count = 0;
      int n = 0;
      uintptr_t bit = 0x80000000;
      for (; n < 32; n++) {
        if (bit & rs_val) count++;
        bit >>= 1;
      }
      set_register(ra, count);
      break;
    }
#if V8_TARGET_ARCH_PPC64
    case POPCNTD: {
      int rs = instr->RSValue();
      int ra = instr->RAValue();
      uintptr_t rs_val = get_register(rs);
      uintptr_t count = 0;
      int n = 0;
      uintptr_t bit = 0x8000000000000000UL;
      for (; n < 64; n++) {
        if (bit & rs_val) count++;
        bit >>= 1;
      }
      set_register(ra, count);
      break;
    }
#endif
    case SYNC: {
      // todo - simulate sync
      __sync_synchronize();
      break;
    }
    case ICBI: {
      // todo - simulate icbi
      break;
    }

    case LWZU:
    case LWZ: {
      int ra = instr->RAValue();
      int rt = instr->RTValue();
      intptr_t ra_val = ra == 0 ? 0 : get_register(ra);
      int offset = SIGN_EXT_IMM16(instr->Bits(15, 0));
      set_register(rt, ReadWU(ra_val + offset));
      if (opcode == LWZU) {
        DCHECK_NE(ra, 0);
        set_register(ra, ra_val + offset);
      }
      break;
    }

    case LBZU:
    case LBZ: {
      int ra = instr->RAValue();
      int rt = instr->RTValue();
      intptr_t ra_val = ra == 0 ? 0 : get_register(ra);
      int offset = SIGN_EXT_IMM16(instr->Bits(15, 0));
      set_register(rt, ReadB(ra_val + offset) & 0xFF);
      if (opcode == LBZU) {
        DCHECK_NE(ra, 0);
        set_register(ra, ra_val + offset);
      }
      break;
    }

    case STWU:
    case STW: {
      int ra = instr->RAValue();
      int rs = instr->RSValue();
      intptr_t ra_val = ra == 0 ? 0 : get_register(ra);
      int32_t rs_val = get_register(rs);
      int offset = SIGN_EXT_IMM16(instr->Bits(15, 0));
      WriteW(ra_val + offset, rs_val);
      if (opcode == STWU) {
        DCHECK_NE(ra, 0);
        set_register(ra, ra_val + offset);
      }
      break;
    }
    case SRADIX: {
      int ra = instr->RAValue();
      int rs = instr->RSValue();
      int sh = (instr->Bits(15, 11) | (instr->Bit(1) << 5));
      intptr_t rs_val = get_register(rs);
      intptr_t result = rs_val >> sh;
      set_register(ra, result);
      if (instr->Bit(0)) {  // RC bit set
        SetCR0(result);
      }
      break;
    }
    case STBCX: {
      int rs = instr->RSValue();
      int ra = instr->RAValue();
      int rb = instr->RBValue();
      intptr_t ra_val = ra == 0 ? 0 : get_register(ra);
      int8_t rs_val = get_register(rs);
      intptr_t rb_val = get_register(rb);
      SetCR0(WriteExB(ra_val + rb_val, rs_val));
      break;
    }
    case STHCX: {
      int rs = instr->RSValue();
      int ra = instr->RAValue();
      int rb = instr->RBValue();
      intptr_t ra_val = ra == 0 ? 0 : get_register(ra);
      int16_t rs_val = get_register(rs);
      intptr_t rb_val = get_register(rb);
      SetCR0(WriteExH(ra_val + rb_val, rs_val));
      break;
    }
    case STWCX: {
      int rs = instr->RSValue();
      int ra = instr->RAValue();
      int rb = instr->RBValue();
      intptr_t ra_val = ra == 0 ? 0 : get_register(ra);
      int32_t rs_val = get_register(rs);
      intptr_t rb_val = get_register(rb);
      SetCR0(WriteExW(ra_val + rb_val, rs_val));
      break;
    }
    case STDCX: {
      int rs = instr->RSValue();
      int ra = instr->RAValue();
      int rb = instr->RBValue();
      intptr_t ra_val = ra == 0 ? 0 : get_register(ra);
      int64_t rs_val = get_register(rs);
      intptr_t rb_val = get_register(rb);
      SetCR0(WriteExDW(ra_val + rb_val, rs_val));
      break;
    }
    case TW: {
      // used for call redirection in simulation mode
      SoftwareInterrupt(instr);
      break;
    }
    case CMP: {
      int ra = instr->RAValue();
      int rb = instr->RBValue();
      int cr = instr->Bits(25, 23);
      uint32_t bf = 0;
#if V8_TARGET_ARCH_PPC64
      int L = instr->Bit(21);
      if (L) {
#endif
        intptr_t ra_val = get_register(ra);
        intptr_t rb_val = get_register(rb);
        if (ra_val < rb_val) {
          bf |= 0x80000000;
        }
        if (ra_val > rb_val) {
          bf |= 0x40000000;
        }
        if (ra_val == rb_val) {
          bf |= 0x20000000;
        }
#if V8_TARGET_ARCH_PPC64
      } else {
        int32_t ra_val = get_register(ra);
        int32_t rb_val = get_register(rb);
        if (ra_val < rb_val) {
          bf |= 0x80000000;
        }
        if (ra_val > rb_val) {
          bf |= 0x40000000;
        }
        if (ra_val == rb_val) {
          bf |= 0x20000000;
        }
      }
#endif
      uint32_t condition_mask = 0xF0000000U >> (cr * 4);
      uint32_t condition = bf >> (cr * 4);
      condition_reg_ = (condition_reg_ & ~condition_mask) | condition;
      break;
    }
    case SUBFCX: {
      int rt = instr->RTValue();
      int ra = instr->RAValue();
      int rb = instr->RBValue();
      // int oe = instr->Bit(10);
      uintptr_t ra_val = get_register(ra);
      uintptr_t rb_val = get_register(rb);
      uintptr_t alu_out = ~ra_val + rb_val + 1;
      // Set carry
      if (ra_val <= rb_val) {
        special_reg_xer_ = (special_reg_xer_ & ~0xF0000000) | 0x20000000;
      } else {
        special_reg_xer_ &= ~0xF0000000;
      }
      set_register(rt, alu_out);
      if (instr->Bit(0)) {  // RC bit set
        SetCR0(alu_out);
      }
      // todo - handle OE bit
      break;
    }
    case SUBFEX: {
      int rt = instr->RTValue();
      int ra = instr->RAValue();
      int rb = instr->RBValue();
      // int oe = instr->Bit(10);
      uintptr_t ra_val = get_register(ra);
      uintptr_t rb_val = get_register(rb);
      uintptr_t alu_out = ~ra_val + rb_val;
      if (special_reg_xer_ & 0x20000000) {
        alu_out += 1;
      }
      set_register(rt, alu_out);
      if (instr->Bit(0)) {  // RC bit set
        SetCR0(static_cast<intptr_t>(alu_out));
      }
      // todo - handle OE bit
      break;
    }
    case ADDCX: {
      int rt = instr->RTValue();
      int ra = instr->RAValue();
      int rb = instr->RBValue();
      // int oe = instr->Bit(10);
      uintptr_t ra_val = get_register(ra);
      uintptr_t rb_val = get_register(rb);
      uintptr_t alu_out = ra_val + rb_val;
      // Set carry
      if (~ra_val < rb_val) {
        special_reg_xer_ = (special_reg_xer_ & ~0xF0000000) | 0x20000000;
      } else {
        special_reg_xer_ &= ~0xF0000000;
      }
      set_register(rt, alu_out);
      if (instr->Bit(0)) {  // RC bit set
        SetCR0(static_cast<intptr_t>(alu_out));
      }
      // todo - handle OE bit
      break;
    }
    case ADDEX: {
      int rt = instr->RTValue();
      int ra = instr->RAValue();
      int rb = instr->RBValue();
      // int oe = instr->Bit(10);
      uintptr_t ra_val = get_register(ra);
      uintptr_t rb_val = get_register(rb);
      uintptr_t alu_out = ra_val + rb_val;
      if (special_reg_xer_ & 0x20000000) {
        alu_out += 1;
      }
      set_register(rt, alu_out);
      if (instr->Bit(0)) {  // RC bit set
        SetCR0(static_cast<intptr_t>(alu_out));
      }
      // todo - handle OE bit
      break;
    }
    case MULHWX: {
      int rt = instr->RTValue();
      int ra = instr->RAValue();
      int rb = instr->RBValue();
      int32_t ra_val = (get_register(ra) & 0xFFFFFFFF);
      int32_t rb_val = (get_register(rb) & 0xFFFFFFFF);
      int64_t alu_out = (int64_t)ra_val * (int64_t)rb_val;
      // High 32 bits of the result is undefined,
      // Which is simulated here by adding random bits.
      alu_out = (alu_out >> 32) | 0x421000000000000;
      set_register(rt, alu_out);
      if (instr->Bit(0)) {  // RC bit set
        SetCR0(static_cast<intptr_t>(alu_out));
      }
      break;
    }
    case MULHWUX: {
      int rt = instr->RTValue();
      int ra = instr->RAValue();
      int rb = instr->RBValue();
      uint32_t ra_val = (get_register(ra) & 0xFFFFFFFF);
      uint32_t rb_val = (get_register(rb) & 0xFFFFFFFF);
      uint64_t alu_out = (uint64_t)ra_val * (uint64_t)rb_val;
      // High 32 bits of the result is undefined,
      // Which is simulated here by adding random bits.
      alu_out = (alu_out >> 32) | 0x421000000000000;
      set_register(rt, alu_out);
      if (instr->Bit(0)) {  // RC bit set
        SetCR0(static_cast<intptr_t>(alu_out));
      }
      break;
    }
    case MULHD: {
      int rt = instr->RTValue();
      int ra = instr->RAValue();
      int rb = instr->RBValue();
      int64_t ra_val = get_register(ra);
      int64_t rb_val = get_register(rb);
      int64_t alu_out = base::bits::SignedMulHigh64(ra_val, rb_val);
      set_register(rt, alu_out);
      if (instr->Bit(0)) {  // RC bit set
        SetCR0(static_cast<intptr_t>(alu_out));
      }
      break;
    }
    case MULHDU: {
      int rt = instr->RTValue();
      int ra = instr->RAValue();
      int rb = instr->RBValue();
      uint64_t ra_val = get_register(ra);
      uint64_t rb_val = get_register(rb);
      uint64_t alu_out = base::bits::UnsignedMulHigh64(ra_val, rb_val);
      set_register(rt, alu_out);
      if (instr->Bit(0)) {  // RC bit set
        SetCR0(static_cast<intptr_t>(alu_out));
      }
      break;
    }
    case NEGX: {
      int rt = instr->RTValue();
      int ra = instr->RAValue();
      intptr_t ra_val = get_register(ra);
      intptr_t alu_out = 1 + ~ra_val;
#if V8_TARGET_ARCH_PPC64
      intptr_t one = 1;  // work-around gcc
      intptr_t kOverflowVal = (one << 63);
#else
      intptr_t kOverflowVal = kMinInt;
#endif
      set_register(rt, alu_out);
      if (instr->Bit(10)) {  // OE bit set
        if (ra_val == kOverflowVal) {
          special_reg_xer_ |= 0xC0000000;  // set SO,OV
        } else {
          special_reg_xer_ &= ~0x40000000;  // clear OV
        }
      }
      if (instr->Bit(0)) {  // RC bit set
        bool setSO = (special_reg_xer_ & 0x80000000);
        SetCR0(alu_out, setSO);
      }
      break;
    }
    case SLWX: {
      int rs = instr->RSValue();
      int ra = instr->RAValue();
      int rb = instr->RBValue();
      uint32_t rs_val = get_register(rs);
      uintptr_t rb_val = get_register(rb) & 0x3F;
      uint32_t result = (rb_val > 31) ? 0 : rs_val << rb_val;
      set_register(ra, result);
      if (instr->Bit(0)) {  // RC bit set
        SetCR0(result);
      }
      break;
    }
#if V8_TARGET_ARCH_PPC64
    case SLDX: {
      int rs = instr->RSValue();
      int ra = instr->RAValue();
      int rb = instr->RBValue();
      uintptr_t rs_val = get_register(rs);
      uintptr_t rb_val = get_register(rb) & 0x7F;
      uintptr_t result = (rb_val > 63) ? 0 : rs_val << rb_val;
      set_register(ra, result);
      if (instr->Bit(0)) {  // RC bit set
        SetCR0(result);
      }
      break;
    }
    case MFVSRD: {
      int frt = instr->RTValue();
      int ra = instr->RAValue();
      int64_t frt_val;
      if (!instr->Bit(0)) {
        // if double reg (TX=0).
        frt_val = get_d_register(frt);
      } else {
        // if simd reg (TX=1).
        DCHECK_EQ(instr->Bit(0), 1);
        frt_val = get_simd_register_by_lane<int64_t>(frt, 0);
      }
      set_register(ra, frt_val);
      break;
    }
    case MFVSRWZ: {
      DCHECK(!instr->Bit(0));
      int frt = instr->RTValue();
      int ra = instr->RAValue();
      int64_t frt_val = get_d_register(frt);
      set_register(ra, static_cast<uint32_t>(frt_val));
      break;
    }
    case MTVSRD: {
      int frt = instr->RTValue();
      int ra = instr->RAValue();
      int64_t ra_val = get_register(ra);
      if (!instr->Bit(0)) {
        // if double reg (TX=0).
        set_d_register(frt, ra_val);
      } else {
        // if simd reg (TX=1).
        DCHECK_EQ(instr->Bit(0), 1);
        set_simd_register_by_lane<int64_t>(frt, 0,
                                           static_cast<int64_t>(ra_val));
        // Low 64 bits of the result is undefined,
        // Which is simulated here by adding random bits.
        set_simd_register_by_lane<int64_t>(
            frt, 1, static_cast<int64_t>(0x123456789ABCD));
      }
      break;
    }
    case MTVSRDD: {
      int xt = instr->RTValue();
      int ra = instr->RAValue();
      int rb = instr->RBValue();
      set_simd_register_by_lane<int64_t>(
          xt, 0, static_cast<int64_t>(get_register(ra)));
      set_simd_register_by_lane<int64_t>(
          xt, 1, static_cast<int64_t>(get_register(rb)));
      break;
    }
    case MTVSRWA: {
      DCHECK(!instr->Bit(0));
      int frt = instr->RTValue();
      int ra = instr->RAValue();
      int64_t ra_val = static_cast<int32_t>(get_register(ra));
      set_d_register(frt, ra_val);
      break;
    }
    case MTVSRWZ: {
      DCHECK(!instr->Bit(0));
      int frt = instr->RTValue();
      int ra = instr->RAValue();
      uint64_t ra_val = static_cast<uint32_t>(get_register(ra));
      set_d_register(frt, ra_val);
      break;
    }
#endif
    case CNTLZWX: {
      int rs = instr->RSValue();
      int ra = instr->RAValue();
      uintptr_t rs_val = get_register(rs);
      uintptr_t count = 0;
      int n = 0;
      uintptr_t bit = 0x80000000;
      for (; n < 32; n++) {
        if (bit & rs_val) break;
        count++;
        bit >>= 1;
      }
      set_register(ra, count);
      if (instr->Bit(0)) {  // RC Bit set
        int bf = 0;
        if (count > 0) {
          bf |= 0x40000000;
        }
        if (count == 0) {
          bf |= 0x20000000;
        }
        condition_reg_ = (condition_reg_ & ~0xF0000000) | bf;
      }
      break;
    }
    case CNTLZDX: {
      int rs = instr->RSValue();
      int ra = instr->RAValue();
      uintptr_t rs_val = get_register(rs);
      uintptr_t count = 0;
      int n = 0;
      uintptr_t bit = 0x8000000000000000UL;
      for (; n < 64; n++) {
        if (bit & rs_val) break;
        count++;
        bit >>= 1;
      }
      set_register(ra, count);
      if (instr->Bit(0)) {  // RC Bit set
        int bf = 0;
        if (count > 0) {
          bf |= 0x40000000;
        }
        if (count == 0) {
          bf |= 0x20000000;
        }
        condition_reg_ = (condition_reg_ & ~0xF0000000) | bf;
      }
      break;
    }
    case CNTTZWX: {
      int rs = instr->RSValue();
      int ra = instr->RAValue();
      uint32_t rs_val = static_cast<uint32_t>(get_register(rs));
      uintptr_t count = rs_val == 0 ? 32 : __builtin_ctz(rs_val);
      set_register(ra, count);
      if (instr->Bit(0)) {  // RC Bit set
        int bf = 0;
        if (count > 0) {
          bf |= 0x40000000;
        }
        if (count == 0) {
          bf |= 0x20000000;
        }
        condition_reg_ = (condition_reg_ & ~0xF0000000) | bf;
      }
      break;
    }
    case CNTTZDX: {
      int rs = instr->RSValue();
      int ra = instr->RAValue();
      uint64_t rs_val = get_register(rs);
      uintptr_t count = rs_val == 0 ? 64 : __builtin_ctzl(rs_val);
      set_register(ra, count);
      if (instr->Bit(0)) {  // RC Bit set
        int bf = 0;
        if (count > 0) {
          bf |= 0x40000000;
        }
        if (count == 0) {
          bf |= 0x20000000;
        }
        condition_reg_ = (condition_reg_ & ~0xF0000000) | bf;
      }
      break;
    }
    case ANDX: {
      int rs = instr->RSValue();
      int ra = instr->RAValue();
      int rb = instr->RBValue();
      intptr_t rs_val = get_register(rs);
      intptr_t rb_val = get_register(rb);
      intptr_t alu_out = rs_val & rb_val;
      set_register(ra, alu_out);
      if (instr->Bit(0)) {  // RC Bit set
        SetCR0(alu_out);
      }
      break;
    }
    case ANDCX: {
      int rs = instr->RSValue();
      int ra = instr->RAValue();
      int rb = instr->RBValue();
      intptr_t rs_val = get_register(rs);
      intptr_t rb_val = get_register(rb);
      intptr_t alu_out = rs_val & ~rb_val;
      set_register(ra, alu_out);
      if (instr->Bit(0)) {  // RC Bit set
        SetCR0(alu_out);
      }
      break;
    }
    case CMPL: {
      int ra = instr->RAValue();
      int rb = instr->RBValue();
      int cr = instr->Bits(25, 23);
      uint32_t bf = 0;
#if V8_TARGET_ARCH_PPC64
      int L = instr->Bit(21);
      if (L) {
#endif
        uintptr_t ra_val = get_register(ra);
        uintptr_t rb_val = get_register(rb);
        if (ra_val < rb_val) {
          bf |= 0x80000000;
        }
        if (ra_val > rb_val) {
          bf |= 0x40000000;
        }
        if (ra_val == rb_val) {
          bf |= 0x20000000;
        }
#if V8_TARGET_ARCH_PPC64
      } else {
        uint32_t ra_val = get_register(ra);
        uint32_t rb_val = get_register(rb);
        if (ra_val < rb_val) {
          bf |= 0x80000000;
        }
        if (ra_val > rb_val) {
          bf |= 0x40000000;
        }
        if (ra_val == rb_val) {
          bf |= 0x20000000;
        }
      }
#endif
      uint32_t condition_mask = 0xF0000000U >> (cr * 4);
      uint32_t condition = bf >> (cr * 4);
      condition_reg_ = (condition_reg_ & ~condition_mask) | condition;
      break;
    }
    case SUBFX: {
      int rt = instr->RTValue();
      int ra = instr->RAValue();
      int rb = instr->RBValue();
      // int oe = instr->Bit(10);
      intptr_t ra_val = get_register(ra);
      intptr_t rb_val = get_register(rb);
      intptr_t alu_out = rb_val - ra_val;
      // todo - figure out underflow
      set_register(rt, alu_out);
      if (instr->Bit(0)) {  // RC Bit set
        SetCR0(alu_out);
      }
      // todo - handle OE bit
      break;
    }
    case ADDZEX: {
      int rt = instr->RTValue();
      int ra = instr->RAValue();
      intptr_t ra_val = get_register(ra);
      if (special_reg_xer_ & 0x20000000) {
        ra_val += 1;
      }
      set_register(rt, ra_val);
      if (instr->Bit(0)) {  // RC bit set
        SetCR0(ra_val);
      }
      // todo - handle OE bit
      break;
    }
    case NORX: {
      int rs = instr->RSValue();
      int ra = instr->RAValue();
      int rb = instr->RBValue();
      intptr_t rs_val = get_register(rs);
      intptr_t rb_val = get_register(rb);
      intptr_t alu_out = ~(rs_val | rb_val);
      set_register(ra, alu_out);
      if (instr->Bit(0)) {  // RC bit set
        SetCR0(alu_out);
      }
      break;
    }
    case MULLW: {
      int rt = instr->RTValue();
      int ra = instr->RAValue();
      int rb = instr->RBValue();
      int32_t ra_val = (get_register(ra) & 0xFFFFFFFF);
      int32_t rb_val = (get_register(rb) & 0xFFFFFFFF);
      int32_t alu_out = ra_val * rb_val;
      set_register(rt, alu_out);
      if (instr->Bit(0)) {  // RC bit set
        SetCR0(alu_out);
      }
      // todo - handle OE bit
      break;
    }
#if V8_TARGET_ARCH_PPC64
    case MULLD: {
      int rt = instr->RTValue();
      int ra = instr->RAValue();
      int rb = instr->RBValue();
      int64_t ra_val = get_register(ra);
      int64_t rb_val = get_register(rb);
      int64_t alu_out = ra_val * rb_val;
      set_register(rt, alu_out);
      if (instr->Bit(0)) {  // RC bit set
        SetCR0(alu_out);
      }
      // todo - handle OE bit
      break;
    }
#endif
    case DIVW: {
      int rt = instr->RTValue();
      int ra = instr->RAValue();
      int rb = instr->RBValue();
      int32_t ra_val = get_register(ra);
      int32_t rb_val = get_register(rb);
      bool overflow = (ra_val == kMinInt && rb_val == -1);
      // result is undefined if divisor is zero or if operation
      // is 0x80000000 / -1.
      int32_t alu_out = (rb_val == 0 || overflow) ? -1 : ra_val / rb_val;
      set_register(rt, alu_out);
      if (instr->Bit(10)) {  // OE bit set
        if (overflow) {
          special_reg_xer_ |= 0xC0000000;  // set SO,OV
        } else {
          special_reg_xer_ &= ~0x40000000;  // clear OV
        }
      }
      if (instr->Bit(0)) {  // RC bit set
        bool setSO = (special_reg_xer_ & 0x80000000);
        SetCR0(alu_out, setSO);
      }
      break;
    }
    case DIVWU: {
      int rt = instr->RTValue();
      int ra = instr->RAValue();
      int rb = instr->RBValue();
      uint32_t ra_val = get_register(ra);
      uint32_t rb_val = get_register(rb);
      bool overflow = (rb_val == 0);
      // result is undefined if divisor is zero
      uint32_t alu_out = (overflow) ? -1 : ra_val / rb_val;
      set_register(rt, alu_out);
      if (instr->Bit(10)) {  // OE bit set
        if (overflow) {
          special_reg_xer_ |= 0xC0000000;  // set SO,OV
        } else {
          special_reg_xer_ &= ~0x40000000;  // clear OV
        }
      }
      if (instr->Bit(0)) {  // RC bit set
        bool setSO = (special_reg_xer_ & 0x80000000);
        SetCR0(alu_out, setSO);
      }
      break;
    }
#if V8_TARGET_ARCH_PPC64
    case DIVD: {
      int rt = instr->RTValue();
      int ra = instr->RAValue();
      int rb = instr->RBValue();
      int64_t ra_val = get_register(ra);
      int64_t rb_val = get_register(rb);
      int64_t one = 1;  // work-around gcc
      int64_t kMinLongLong = (one << 63);
      // result is undefined if divisor is zero or if operation
      // is 0x80000000_00000000 / -1.
      int64_t alu_out =
          (rb_val == 0 || (ra_val == kMinLongLong && rb_val == -1))
              ? -1
              : ra_val / rb_val;
      set_register(rt, alu_out);
      if (instr->Bit(0)) {  // RC bit set
        SetCR0(alu_out);
      }
      // todo - handle OE bit
      break;
    }
    case DIVDU: {
      int rt = instr->RTValue();
      int ra = instr->RAValue();
      int rb = instr->RBValue();
      uint64_t ra_val = get_register(ra);
      uint64_t rb_val = get_register(rb);
      // result is undefined if divisor is zero
      uint64_t alu_out = (rb_val == 0) ? -1 : ra_val / rb_val;
      set_register(rt, alu_out);
      if (instr->Bit(0)) {  // RC bit set
        SetCR0(alu_out);
      }
      // todo - handle OE bit
      break;
    }
#endif
    case ADDX: {
      int rt = instr->RTValue();
      int ra = instr->RAValue();
      int rb = instr->RBValue();
      // int oe = instr->Bit(10);
      intptr_t ra_val = get_register(ra);
      intptr_t rb_val = get_register(rb);
      intptr_t alu_out = ra_val + rb_val;
      set_register(rt, alu_out);
      if (instr->Bit(0)) {  // RC bit set
        SetCR0(alu_out);
      }
      // todo - handle OE bit
      break;
    }
    case XORX: {
      int rs = instr->RSValue();
      int ra = instr->RAValue();
      int rb = instr->RBValue();
      intptr_t rs_val = get_register(rs);
      intptr_t rb_val = get_register(rb);
      intptr_t alu_out = rs_val ^ rb_val;
      set_register(ra, alu_out);
      if (instr->Bit(0)) {  // RC bit set
        SetCR0(alu_out);
      }
      break;
    }
    case ORX: {
      int rs = instr->RSValue();
      int ra = instr->RAValue();
      int rb = instr->RBValue();
      intptr_t rs_val = get_register(rs);
      intptr_t rb_val = get_register(rb);
      intptr_t alu_out = rs_val | rb_val;
      set_register(ra, alu_out);
      if (instr->Bit(0)) {  // RC bit set
        SetCR0(alu_out);
      }
      break;
    }
    case ORC: {
      int rs = instr->RSValue();
      int ra = instr->RAValue();
      int rb = instr->RBValue();
      intptr_t rs_val = get_register(rs);
      intptr_t rb_val = get_register(rb);
      intptr_t alu_out = rs_val | ~rb_val;
      set_register(ra, alu_out);
      if (instr->Bit(0)) {  // RC bit set
        SetCR0(alu_out);
      }
      break;
    }
    case MFSPR: {
      int rt = instr->RTValue();
      int spr = instr->Bits(20, 11);
      if (spr != 256) {
        UNIMPLEMENTED();  // Only LRLR supported
      }
      set_register(rt, special_reg_lr_);
      break;
    }
    case MTSPR: {
      int rt = instr->RTValue();
      intptr_t rt_val = get_register(rt);
      int spr = instr->Bits(20, 11);
      if (spr == 256) {
        special_reg_lr_ = rt_val;
      } else if (spr == 288) {
        special_reg_ctr_ = rt_val;
      } else if (spr == 32) {
        special_reg_xer_ = rt_val;
      } else {
        UNIMPLEMENTED();  // Only LR supported
      }
      break;
    }
    case MFCR: {
      int rt = instr->RTValue();
      set_register(rt, condition_reg_);
      break;
    }
    case STWUX:
    case STWX: {
      int rs = instr->RSValue();
      int ra = instr->RAValue();
      int rb = instr->RBValue();
      intptr_t ra_val = ra == 0 ? 0 : get_register(ra);
      int32_t rs_val = get_register(rs);
      intptr_t rb_val = get_register(rb);
      WriteW(ra_val + rb_val, rs_val);
      if (opcode == STWUX) {
        DCHECK_NE(ra, 0);
        set_register(ra, ra_val + rb_val);
      }
      break;
    }
    case STBUX:
    case STBX: {
      int rs = instr->RSValue();
      int ra = instr->RAValue();
      int rb = instr->RBValue();
      intptr_t ra_val = ra == 0 ? 0 : get_register(ra);
      int8_t rs_val = get_register(rs);
      intptr_t rb_val = get_register(rb);
      WriteB(ra_val + rb_val, rs_val);
      if (opcode == STBUX) {
        DCHECK_NE(ra, 0);
        set_register(ra, ra_val + rb_val);
      }
      break;
    }
    case STHUX:
    case STHX: {
      int rs = instr->RSValue();
      int ra = instr->RAValue();
      int rb = instr->RBValue();
      intptr_t ra_val = ra == 0 ? 0 : get_register(ra);
      int16_t rs_val = get_register(rs);
      intptr_t rb_val = get_register(rb);
      WriteH(ra_val + rb_val, rs_val);
      if (opcode == STHUX) {
        DCHECK_NE(ra, 0);
        set_register(ra, ra_val + rb_val);
      }
      break;
    }
    case LWZX:
    case LWZUX: {
      int rt = instr->RTValue();
      int ra = instr->RAValue();
      int rb = instr->RBValue();
      intptr_t ra_val = ra == 0 ? 0 : get_register(ra);
      intptr_t rb_val = get_register(rb);
      set_register(rt, ReadWU(ra_val + rb_val));
      if (opcode == LWZUX) {
        DCHECK(ra != 0 && ra != rt);
        set_register(ra, ra_val + rb_val);
      }
      break;
    }
#if V8_TARGET_ARCH_PPC64
    case LWAX: {
      int rt = instr->RTValue();
      int ra = instr->RAValue();
      int rb = instr->RBValue();
      intptr_t ra_val = ra == 0 ? 0 : get_register(ra);
      intptr_t rb_val = get_register(rb);
      set_register(rt, ReadW(ra_val + rb_val));
      break;
    }
    case LDX:
    case LDUX: {
      int rt = instr->RTValue();
      int ra = instr->RAValue();
      int rb = instr->RBValue();
      intptr_t ra_val = ra == 0 ? 0 : get_register(ra);
      intptr_t rb_val = get_register(rb);
      intptr_t result = ReadDW(ra_val + rb_val);
      set_register(rt, result);
      if (opcode == LDUX) {
        DCHECK(ra != 0 && ra != rt);
        set_register(ra, ra_val + rb_val);
      }
      break;
    }
    case LDBRX: {
      int rt = instr->RTValue();
      int ra = instr->RAValue();
      int rb = instr->RBValue();
      intptr_t ra_val = ra == 0 ? 0 : get_register(ra);
      intptr_t rb_val = get_register(rb);
      intptr_t result = ByteReverse<int64_t>(ReadDW(ra_val + rb_val));
      set_register(rt, result);
      break;
    }
    case LWBRX: {
      int rt = instr->RTValue();
      int ra = instr->RAValue();
      int rb = instr->RBValue();
      intptr_t ra_val = ra == 0 ? 0 : get_register(ra);
      intptr_t rb_val = get_register(rb);
      intptr_t result = ByteReverse<int32_t>(ReadW(ra_val + rb_val));
      set_register(rt, result);
      break;
    }
    case STDBRX: {
      int rs = instr->RSValue();
      int ra = instr->RAValue();
      int rb = instr->RBValue();
      intptr_t ra_val = ra == 0 ? 0 : get_register(ra);
      intptr_t rs_val = get_register(rs);
      intptr_t rb_val = get_register(rb);
      WriteDW(ra_val + rb_val, ByteReverse<int64_t>(rs_val));
      break;
    }
    case STWBRX: {
      int rs = instr->RSValue();
      int ra = instr->RAValue();
      int rb = instr->RBValue();
      intptr_t ra_val = ra == 0 ? 0 : get_register(ra);
      intptr_t rs_val = get_register(rs);
      intptr_t rb_val = get_register(rb);
      WriteW(ra_val + rb_val, ByteReverse<int32_t>(rs_val));
      break;
    }
    case STHBRX: {
      int rs = instr->RSValue();
      int ra = instr->RAValue();
      int rb = instr->RBValue();
      intptr_t ra_val = ra == 0 ? 0 : get_register(ra);
      intptr_t rs_val = get_register(rs);
      intptr_t rb_val = get_register(rb);
      WriteH(ra_val + rb_val, ByteReverse<int16_t>(rs_val));
      break;
    }
    case STDX:
    case STDUX: {
      int rs = instr->RSValue();
      int ra = instr->RAValue();
      int rb = instr->RBValue();
      intptr_t ra_val = ra == 0 ? 0 : get_register(ra);
      intptr_t rs_val = get_register(rs);
      intptr_t rb_val = get_register(rb);
      WriteDW(ra_val + rb_val, rs_val);
      if (opcode == STDUX) {
        DCHECK_NE(ra, 0);
        set_register(ra, ra_val + rb_val);
      }
      break;
    }
#endif
    case LBZX:
    case LBZUX: {
      int rt = instr->RTValue();
      int ra = instr->RAValue();
      int rb = instr->RBValue();
      intptr_t ra_val = ra == 0 ? 0 : get_register(ra);
      intptr_t rb_val = get_register(rb);
      set_register(rt, ReadBU(ra_val + rb_val) & 0xFF);
      if (opcode == LBZUX) {
        DCHECK(ra != 0 && ra != rt);
        set_register(ra, ra_val + rb_val);
      }
      break;
    }
    case LHZX:
    case LHZUX: {
      int rt = instr->RTValue();
      int ra = instr->RAValue();
      int rb = instr->RBValue();
      intptr_t ra_val = ra == 0 ? 0 : get_register(ra);
      intptr_t rb_val = get_register(rb);
      set_register(rt, ReadHU(ra_val + rb_val) & 0xFFFF);
      if (opcode == LHZUX) {
        DCHECK(ra != 0 && ra != rt);
        set_register(ra, ra_val + rb_val);
      }
      break;
    }
    case LHAX: {
      int rt = instr->RTValue();
      int ra = instr->RAValue();
      int rb = instr->RBValue();
      intptr_t ra_val = ra == 0 ? 0 : get_register(ra);
      intptr_t rb_val = get_register(rb);
      set_register(rt, ReadH(ra_val + rb_val));
      break;
    }
    case LBARX: {
      int rt = instr->RTValue();
      int ra = instr->RAValue();
      int rb = instr->RBValue();
      intptr_t ra_val = ra == 0 ? 0 : get_register(ra);
      intptr_t rb_val = get_register(rb);
      set_register(rt, ReadExBU(ra_val + rb_val) & 0xFF);
      break;
    }
    case LHARX: {
      int rt = instr->RTValue();
      int ra = instr->RAValue();
      int rb = instr->RBValue();
      intptr_t ra_val = ra == 0 ? 0 : get_register(ra);
      intptr_t rb_val = get_register(rb);
      set_register(rt, ReadExHU(ra_val + rb_val));
      break;
    }
    case LWARX: {
      int rt = instr->RTValue();
      int ra = instr->RAValue();
      int rb = instr->RBValue();
      intptr_t ra_val = ra == 0 ? 0 : get_register(ra);
      intptr_t rb_val = get_register(rb);
      set_register(rt, ReadExWU(ra_val + rb_val));
      break;
    }
    case LDARX: {
      int rt = instr->RTValue();
      int ra = instr->RAValue();
      int rb = instr->RBValue();
      intptr_t ra_val = ra == 0 ? 0 : get_register(ra);
      intptr_t rb_val = get_register(rb);
      set_register(rt, ReadExDWU(ra_val + rb_val));
      break;
    }
    case DCBF: {
      // todo - simulate dcbf
      break;
    }
    case ISEL: {
      int rt = instr->RTValue();
      int ra = instr->RAValue();
      int rb = instr->RBValue();
      int condition_bit = instr->RCValue();
      int condition_mask = 0x80000000 >> condition_bit;
      intptr_t ra_val = (ra == 0) ? 0 : get_register(ra);
      intptr_t rb_val = get_register(rb);
      intptr_t value = (condition_reg_ & condition_mask) ? ra_val : rb_val;
      set_register(rt, value);
      break;
    }

    case STBU:
    case STB: {
      int ra = instr->RAValue();
      int rs = instr->RSValue();
      intptr_t ra_val = ra == 0 ? 0 : get_register(ra);
      int8_t rs_val = get_register(rs);
      int offset = SIGN_EXT_IMM16(instr->Bits(15, 0));
      WriteB(ra_val + offset, rs_val);
      if (opcode == STBU) {
        DCHECK_NE(ra, 0);
        set_register(ra, ra_val + offset);
      }
      break;
    }

    case LHZU:
    case LHZ: {
      int ra = instr->RAValue();
      int rt = instr->RTValue();
      intptr_t ra_val = ra == 0 ? 0 : get_register(ra);
      int offset = SIGN_EXT_IMM16(instr->Bits(15, 0));
      uintptr_t result = ReadHU(ra_val + offset) & 0xFFFF;
      set_register(rt, result);
      if (opcode == LHZU) {
        set_register(ra, ra_val + offset);
      }
      break;
    }

    case LHA:
    case LHAU: {
      int ra = instr->RAValue();
      int rt = instr->RTValue();
      intptr_t ra_val = ra == 0 ? 0 : get_register(ra);
      int offset = SIGN_EXT_IMM16(instr->Bits(15, 0));
      intptr_t result = ReadH(ra_val + offset);
      set_register(rt, result);
      if (opcode == LHAU) {
        set_register(ra, ra_val + offset);
      }
      break;
    }

    case STHU:
    case STH: {
      int ra = instr->RAValue();
      int rs = instr->RSValue();
      intptr_t ra_val = ra == 0 ? 0 : get_register(ra);
      int16_t rs_val = get_register(rs);
      int offset = SIGN_EXT_IMM16(instr->Bits(15, 0));
      WriteH(ra_val + offset, rs_val);
      if (opcode == STHU) {
        DCHECK_NE(ra, 0);
        set_register(ra, ra_val + offset);
      }
      break;
    }

    case LMW:
    case STMW: {
      UNIMPLEMENTED();
    }

    case LFSU:
    case LFS: {
      int frt = instr->RTValue();
      int ra = instr->RAValue();
      int32_t offset = SIGN_EXT_IMM16(instr->Bits(15, 0));
      intptr_t ra_val = ra == 0 ? 0 : get_register(ra);
      int32_t val = ReadW(ra_val + offset);
      float* fptr = reinterpret_cast<float*>(&val);
#if V8_HOST_ARCH_IA32 || V8_HOST_ARCH_X64
      // Conversion using double changes sNan to qNan on ia32/x64
      if ((val & 0x7F800000) == 0x7F800000) {
        int64_t dval = static_cast<int64_t>(val);
        dval = ((dval & 0xC0000000) << 32) | ((dval & 0x40000000) << 31) |
               ((dval & 0x40000000) << 30) | ((dval & 0x7FFFFFFF) << 29) | 0x0;
        set_d_register(frt, dval);
      } else {
        set_d_register_from_double(frt, static_cast<double>(*fptr));
      }
#else
      set_d_register_from_double(frt, static_cast<double>(*fptr));
#endif
      if (opcode == LFSU) {
        DCHECK_NE(ra, 0);
        set_register(ra, ra_val + offset);
      }
      break;
    }

    case LFDU:
    case LFD: {
      int frt = instr->RTValue();
      int ra = instr->RAValue();
      int32_t offset = SIGN_EXT_IMM16(instr->Bits(15, 0));
      intptr_t ra_val = ra == 0 ? 0 : get_register(ra);
      int64_t dptr = ReadDW(ra_val + offset);
      set_d_register(frt, dptr);
      if (opcode == LFDU) {
        DCHECK_NE(ra, 0);
        set_register(ra, ra_val + offset);
      }
      break;
    }

    case STFSU:
      [[fallthrough]];
    case STFS: {
      int frs = instr->RSValue();
      int ra = instr->RAValue();
      int32_t offset = SIGN_EXT_IMM16(instr->Bits(15, 0));
      intptr_t ra_val = ra == 0 ? 0 : get_register(ra);
      float frs_val = static_cast<float>(get_double_from_d_register(frs));
      int32_t* p;
#if V8_HOST_ARCH_IA32 || V8_HOST_ARCH_X64
      // Conversion using double changes sNan to qNan on ia32/x64
      int32_t sval = 0;
      int64_t dval = get_d_register(frs);
      if ((dval & 0x7FF0000000000000) == 0x7FF0000000000000) {
        sval = ((dval & 0xC000000000000000) >> 32) |
               ((dval & 0x07FFFFFFE0000000) >> 29);
        p = &sval;
      } else {
        p = reinterpret_cast<int32_t*>(&frs_val);
      }
#else
      p = reinterpret_cast<int32_t*>(&frs_val);
#endif
      WriteW(ra_val + offset, *p);
      if (opcode == STFSU) {
        DCHECK_NE(ra, 0);
        set_register(ra, ra_val + offset);
      }
      break;
    }
    case STFDU:
    case STFD: {
      int frs = instr->RSValue();
      int ra = instr->RAValue();
      int32_t offset = SIGN_EXT_IMM16(instr->Bits(15, 0));
      intptr_t ra_val = ra == 0 ? 0 : get_register(ra);
      int64_t frs_val = get_d_register(frs);
      WriteDW(ra_val + offset, frs_val);
      if (opcode == STFDU) {
        DCHECK_NE(ra, 0);
        set_register(ra, ra_val + offset);
      }
      break;
    }
    case BRW: {
      constexpr int kBitsPerWord = 32;
      int rs = instr->RSValue();
      int ra = instr->RAValue();
      uint64_t rs_val = get_register(rs);
      uint32_t rs_high = rs_val >> kBitsPerWord;
      uint32_t rs_low = (rs_val << kBitsPerWord) >> kBitsPerWord;
      uint64_t result = ByteReverse<int32_t>(rs_high);
      result = (result << kBitsPerWord) | ByteReverse<int32_t>(rs_low);
      set_register(ra, result);
      break;
    }
    case BRD: {
      int rs = instr->RSValue();
      int ra = instr->RAValue();
      uint64_t rs_val = get_register(rs);
      set_register(ra, ByteReverse<int64_t>(rs_val));
      break;
    }
    case FCFIDS: {
      // fcfids
      int frt = instr->RTValue();
      int frb = instr->RBValue();
      int64_t frb_val = get_d_register(frb);
      double frt_val = static_cast<float>(frb_val);
      set_d_register_from_double(frt, frt_val);
      return;
    }
    case FCFIDUS: {
      // fcfidus
      int frt = instr->RTValue();
      int frb = instr->RBValue();
      uint64_t frb_val = get_d_register(frb);
      double frt_val = static_cast<float>(frb_val);
      set_d_register_from_double(frt, frt_val);
      return;
    }

    case FDIV: {
      int frt = instr->RTValue();
      int fra = instr->RAValue();
      int frb = instr->RBValue();
      double fra_val = get_double_from_d_register(fra);
      double frb_val = get_double_from_d_register(frb);
      double frt_val = fra_val / frb_val;
      set_d_register_from_double(frt, frt_val);
      return;
    }
    case FSUB: {
      int frt = instr->RTValue();
      int fra = instr->RAValue();
      int frb = instr->RBValue();
      double fra_val = get_double_from_d_register(fra);
      double frb_val = get_double_from_d_register(frb);
      double frt_val = fra_val - frb_val;
      set_d_register_from_double(frt, frt_val);
      return;
    }
    case FADD: {
      int frt = instr->RTValue();
      int fra = instr->RAValue();
      int frb = instr->RBValue();
      double fra_val = get_double_from_d_register(fra);
      double frb_val = get_double_from_d_register(frb);
      double frt_val = fra_val + frb_val;
      set_d_register_from_double(frt, frt_val);
      return;
    }
    case FSQRT: {
      int frt = instr->RTValue();
      int frb = instr->RBValue();
      double frb_val = get_double_from_d_register(frb);
      double frt_val = std::sqrt(frb_val);
      set_d_register_from_double(frt, frt_val);
      return;
    }
    case FSEL: {
      int frt = instr->RTValue();
      int fra = instr->RAValue();
      int frb = instr->RBValue();
      int frc = instr->RCValue();
      double fra_val = get_double_from_d_register(fra);
      double frb_val = get_double_from_d_register(frb);
      double frc_val = get_double_from_d_register(frc);
      double frt_val = ((fra_val >= 0.0) ? frc_val : frb_val);
      set_d_register_from_double(frt, frt_val);
      return;
    }
    case FMUL: {
      int frt = instr->RTValue();
      int fra = instr->RAValue();
      int frc = instr->RCValue();
      double fra_val = get_double_from_d_register(fra);
      double frc_val = get_double_from_d_register(frc);
      double frt_val = fra_val * frc_val;
      set_d_register_from_double(frt, frt_val);
      return;
    }
    case FMSUB: {
      int frt = instr->RTValue();
      int fra = instr->RAValue();
      int frb = instr->RBValue();
      int frc = instr->RCValue();
      double fra_val = get_double_from_d_register(fra);
      double frb_val = get_double_from_d_register(frb);
      double frc_val = get_double_from_d_register(frc);
      double frt_val = (fra_val * frc_val) - frb_val;
      set_d_register_from_double(frt, frt_val);
      return;
    }
    case FMADD: {
      int frt = instr->RTValue();
      int fra = instr->RAValue();
      int frb = instr->RBValue();
      int frc = instr->RCValue();
      double fra_val = get_double_from_d_register(fra);
      double frb_val = get_double_from_d_register(frb);
      double frc_val = get_double_from_d_register(frc);
      double frt_val = (fra_val * frc_val) + frb_val;
      set_d_register_from_double(frt, frt_val);
      return;
    }
    case FCMPU: {
      int fra = instr->RAValue();
      int frb = instr->RBValue();
      double fra_val = get_double_from_d_register(fra);
      double frb_val = get_double_from_d_register(frb);
      int cr = instr->Bits(25, 23);
      int bf = 0;
      if (fra_val < frb_val) {
        bf |= 0x80000000;
      }
      if (fra_val > frb_val) {
        bf |= 0x40000000;
      }
      if (fra_val == frb_val) {
        bf |= 0x20000000;
      }
      if (std::isunordered(fra_val, frb_val)) {
        bf |= 0x10000000;
      }
      int condition_mask = 0xF0000000 >> (cr * 4);
      int condition = bf >> (cr * 4);
      condition_reg_ = (condition_reg_ & ~condition_mask) | condition;
      return;
    }
    case FRIN: {
      int frt = instr->RTValue();
      int frb = instr->RBValue();
      double frb_val = get_double_from_d_register(frb);
      double frt_val = std::round(frb_val);
      set_d_register_from_double(frt, frt_val);
      if (instr->Bit(0)) {  // RC bit set
                            //  UNIMPLEMENTED();
      }
      return;
    }
    case FRIZ: {
      int frt = instr->RTValue();
      int frb = instr->RBValue();
      double frb_val = get_double_from_d_register(frb);
      double frt_val = std::trunc(frb_val);
      set_d_register_from_double(frt, frt_val);
      if (instr->Bit(0)) {  // RC bit set
                            //  UNIMPLEMENTED();
      }
      return;
    }
    case FRIP: {
      int frt = instr->RTValue();
      int frb = instr->RBValue();
      double frb_val = get_double_from_d_register(frb);
      double frt_val = std::ceil(frb_val);
      set_d_register_from_double(frt, frt_val);
      if (instr->Bit(0)) {  // RC bit set
                            //  UNIMPLEMENTED();
      }
      return;
    }
    case FRIM: {
      int frt = instr->RTValue();
      int frb = instr->RBValue();
      double frb_val = get_double_from_d_register(frb);
      double frt_val = std::floor(frb_val);
      set_d_register_from_double(frt, frt_val);
      if (instr->Bit(0)) {  // RC bit set
                            //  UNIMPLEMENTED();
      }
      return;
    }
    case FRSP: {
      int frt = instr->RTValue();
      int frb = instr->RBValue();
      // frsp round 8-byte double-precision value to
      // single-precision value
      double frb_val = get_double_from_d_register(frb);
      double frt_val = static_cast<float>(frb_val);
      set_d_register_from_double(frt, frt_val);
      if (instr->Bit(0)) {  // RC bit set
                            //  UNIMPLEMENTED();
      }
      return;
    }
    case FCFID: {
      int frt = instr->RTValue();
      int frb = instr->RBValue();
      int64_t frb_val = get_d_register(frb);
      double frt_val = static_cast<double>(frb_val);
      set_d_register_from_double(frt, frt_val);
      return;
    }
    case FCFIDU: {
      int frt = instr->RTValue();
      int frb = instr->RBValue();
      uint64_t frb_val = get_d_register(frb);
      double frt_val = static_cast<double>(frb_val);
      set_d_register_from_double(frt, frt_val);
      return;
    }
    case FCTID:
    case FCTIDZ: {
      int frt = instr->RTValue();
      int frb = instr->RBValue();
      double frb_val = get_double_from_d_register(frb);
      int mode = (opcode == FCTIDZ) ? kRoundToZero
                                    : (fp_condition_reg_ & kFPRoundingModeMask);
      int64_t frt_val;
      int64_t one = 1;  // work-around gcc
      int64_t kMinVal = (one << 63);
      int64_t kMaxVal = kMinVal - 1;
      bool invalid_convert = false;

      if (std::isnan(frb_val)) {
        frt_val = kMinVal;
        invalid_convert = true;
      } else {
        switch (mode) {
          case kRoundToZero:
            frb_val = std::trunc(frb_val);
            break;
          case kRoundToPlusInf:
            frb_val = std::ceil(frb_val);
            break;
          case kRoundToMinusInf:
            frb_val = std::floor(frb_val);
            break;
          default:
            UNIMPLEMENTED();  // Not used by V8.
        }
        if (frb_val < static_cast<double>(kMinVal)) {
          frt_val = kMinVal;
          invalid_convert = true;
        } else if (frb_val >= static_cast<double>(kMaxVal)) {
          frt_val = kMaxVal;
          invalid_convert = true;
        } else {
          frt_val = (int64_t)frb_val;
        }
      }
      set_d_register(frt, frt_val);
      if (invalid_convert) SetFPSCR(VXCVI);
      return;
    }
    case FCTIDU:
    case FCTIDUZ: {
      int frt = instr->RTValue();
      int frb = instr->RBValue();
      double frb_val = get_double_from_d_register(frb);
      int mode = (opcode == FCTIDUZ)
                     ? kRoundToZero
                     : (fp_condition_reg_ & kFPRoundingModeMask);
      uint64_t frt_val;
      uint64_t kMinVal = 0;
      uint64_t kMaxVal = kMinVal - 1;
      bool invalid_convert = false;

      if (std::isnan(frb_val)) {
        frt_val = kMinVal;
        invalid_convert = true;
      } else {
        switch (mode) {
          case kRoundToZero:
            frb_val = std::trunc(frb_val);
            break;
          case kRoundToPlusInf:
            frb_val = std::ceil(frb_val);
            break;
          case kRoundToMinusInf:
            frb_val = std::floor(frb_val);
            break;
          default:
            UNIMPLEMENTED();  // Not used by V8.
        }
        if (frb_val < static_cast<double>(kMinVal)) {
          frt_val = kMinVal;
          invalid_convert = true;
        } else if (frb_val >= static_cast<double>(kMaxVal)) {
          frt_val = kMaxVal;
          invalid_convert = true;
        } else {
          frt_val = (uint64_t)frb_val;
        }
      }
      set_d_register(frt, frt_val);
      if (invalid_convert) SetFPSCR(VXCVI);
      return;
    }
    case FCTIW:
    case FCTIWZ: {
      int frt = instr->RTValue();
      int frb = instr->RBValue();
      double frb_val = get_double_from_d_register(frb);
      int mode = (opcode == FCTIWZ) ? kRoundToZero
                                    : (fp_condition_reg_ & kFPRoundingModeMask);
      int64_t frt_val;
      int64_t kMinVal = kMinInt;
      int64_t kMaxVal = kMaxInt;
      bool invalid_convert = false;

      if (std::isnan(frb_val)) {
        frt_val = kMinVal;
      } else {
        switch (mode) {
          case kRoundToZero:
            frb_val = std::trunc(frb_val);
            break;
          case kRoundToPlusInf:
            frb_val = std::ceil(frb_val);
            break;
          case kRoundToMinusInf:
            frb_val = std::floor(frb_val);
            break;
          case kRoundToNearest: {
            double orig = frb_val;
            frb_val = lround(frb_val);
            // Round to even if exactly halfway.  (lround rounds up)
            if (std::fabs(frb_val - orig) == 0.5 && ((int64_t)frb_val % 2)) {
              frb_val += ((frb_val > 0) ? -1.0 : 1.0);
            }
            break;
          }
          default:
            UNIMPLEMENTED();  // Not used by V8.
        }
        if (frb_val < kMinVal) {
          frt_val = kMinVal;
          invalid_convert = true;
        } else if (frb_val > kMaxVal) {
          frt_val = kMaxVal;
          invalid_convert = true;
        } else {
          frt_val = (int64_t)frb_val;
        }
      }
      set_d_register(frt, frt_val);
      if (invalid_convert) SetFPSCR(VXCVI);
      return;
    }
    case FCTIWU:
    case FCTIWUZ: {
      int frt = instr->RTValue();
      int frb = instr->RBValue();
      double frb_val = get_double_from_d_register(frb);
      int mode = (opcode == FCTIWUZ)
                     ? kRoundToZero
                     : (fp_condition_reg_ & kFPRoundingModeMask);
      uint64_t frt_val;
      uint64_t kMinVal = kMinUInt32;
      uint64_t kMaxVal = kMaxUInt32;
      bool invalid_convert = false;

      if (std::isnan(frb_val)) {
        frt_val = kMinVal;
      } else {
        switch (mode) {
          case kRoundToZero:
            frb_val = std::trunc(frb_val);
            break;
          case kRoundToPlusInf:
            frb_val = std::ceil(frb_val);
            break;
          case kRoundToMinusInf:
            frb_val = std::floor(frb_val);
            break;
          default:
            UNIMPLEMENTED();  // Not used by V8.
        }
        if (frb_val < kMinVal) {
          frt_val = kMinVal;
          invalid_convert = true;
        } else if (frb_val > kMaxVal) {
          frt_val = kMaxVal;
          invalid_convert = true;
        } else {
          frt_val = (uint64_t)frb_val;
        }
      }
      set_d_register(frt, frt_val);
      if (invalid_convert) SetFPSCR(VXCVI);
      return;
    }
    case FNEG: {
      int frt = instr->RTValue();
      int frb = instr->RBValue();
      double frb_val = get_double_from_d_register(frb);
      double frt_val = -frb_val;
      set_d_register_from_double(frt, frt_val);
      return;
    }
    case FCPSGN: {
      int frt = instr->RTValue();
      int frb = instr->RBValue();
      int fra = instr->RAValue();
      double frb_val = get_double_from_d_register(frb);
      double fra_val = get_double_from_d_register(fra);
      double frt_val = std::copysign(frb_val, fra_val);
      set_d_register_from_double(frt, frt_val);
      return;
    }
    case FMR: {
      int frt = instr->RTValue();
      int frb = instr->RBValue();
      int64_t frb_val = get_d_register(frb);
      set_d_register(frt, frb_val);
      return;
    }
    case MTFSFI: {
      int bf = instr->Bits(25, 23);
      int imm = instr->Bits(15, 12);
      int fp_condition_mask = 0xF0000000 >> (bf * 4);
      fp_condition_reg_ &= ~fp_condition_mask;
      fp_condition_reg_ |= (imm << (28 - (bf * 4)));
      if (instr->Bit(0)) {  // RC bit set
        condition_reg_ &= 0xF0FFFFFF;
        condition_reg_ |= (imm << 23);
      }
      return;
    }
    case MTFSF: {
      int frb = instr->RBValue();
      int64_t frb_dval = get_d_register(frb);
      int32_t frb_ival = static_cast<int32_t>((frb_dval)&0xFFFFFFFF);
      int l = instr->Bits(25, 25);
      if (l == 1) {
        fp_condition_reg_ = frb_ival;
      } else {
        UNIMPLEMENTED();
      }
      if (instr->Bit(0)) {  // RC bit set
        UNIMPLEMENTED();
        // int w = instr->Bits(16, 16);
        // int flm = instr->Bits(24, 17);
      }
      return;
    }
    case MFFS: {
      int frt = instr->RTValue();
      int64_t lval = static_cast<int64_t>(fp_condition_reg_);
      set_d_register(frt, lval);
      return;
    }
    case MCRFS: {
      int bf = instr->Bits(25, 23);
      int bfa = instr->Bits(20, 18);
      int cr_shift = (7 - bf) * CRWIDTH;
      int fp_shift = (7 - bfa) * CRWIDTH;
      int field_val = (fp_condition_reg_ >> fp_shift) & 0xF;
      condition_reg_ &= ~(0x0F << cr_shift);
      condition_reg_ |= (field_val << cr_shift);
      // Clear copied exception bits
      switch (bfa) {
        case 5:
          ClearFPSCR(VXSOFT);
          ClearFPSCR(VXSQRT);
          ClearFPSCR(VXCVI);
          break;
        default:
          UNIMPLEMENTED();
      }
      return;
    }
    case MTFSB0: {
      int bt = instr->Bits(25, 21);
      ClearFPSCR(bt);
      if (instr->Bit(0)) {  // RC bit set
        UNIMPLEMENTED();
      }
      return;
    }
    case MTFSB1: {
      int bt = instr->Bits(25, 21);
      SetFPSCR(bt);
      if (instr->Bit(0)) {  // RC bit set
        UNIMPLEMENTED();
      }
      return;
    }
    case FABS: {
      int frt = instr->RTValue();
      int frb = instr->RBValue();
      double frb_val = get_double_from_d_register(frb);
      double frt_val = std::fabs(frb_val);
      set_d_register_from_double(frt, frt_val);
      return;
    }

#if V8_TARGET_ARCH_PPC64
    case RLDICL: {
      int ra = instr->RAValue();
      int rs = instr->RSValue();
      uintptr_t rs_val = get_register(rs);
      int sh = (instr->Bits(15, 11) | (instr->Bit(1) << 5));
      int mb = (instr->Bits(10, 6) | (instr->Bit(5) << 5));
      DCHECK(sh >= 0 && sh <= 63);
      DCHECK(mb >= 0 && mb <= 63);
      uintptr_t result = base::bits::RotateLeft64(rs_val, sh);
      uintptr_t mask = 0xFFFFFFFFFFFFFFFF >> mb;
      result &= mask;
      set_register(ra, result);
      if (instr->Bit(0)) {  // RC bit set
        SetCR0(result);
      }
      return;
    }
    case RLDICR: {
      int ra = instr->RAValue();
      int rs = instr->RSValue();
      uintptr_t rs_val = get_register(rs);
      int sh = (instr->Bits(15, 11) | (instr->Bit(1) << 5));
      int me = (instr->Bits(10, 6) | (instr->Bit(5) << 5));
      DCHECK(sh >= 0 && sh <= 63);
      DCHECK(me >= 0 && me <= 63);
      uintptr_t result = base::bits::RotateLeft64(rs_val, sh);
      uintptr_t mask = 0xFFFFFFFFFFFFFFFF << (63 - me);
      result &= mask;
      set_register(ra, result);
      if (instr->Bit(0)) {  // RC bit set
        SetCR0(result);
      }
      return;
    }
    case RLDIC: {
      int ra = instr->RAValue();
      int rs = instr->RSValue();
      uintptr_t rs_val = get_register(rs);
      int sh = (instr->Bits(15, 11) | (instr->Bit(1) << 5));
      int mb = (instr->Bits(10, 6) | (instr->Bit(5) << 5));
      DCHECK(sh >= 0 && sh <= 63);
      DCHECK(mb >= 0 && mb <= 63);
      uintptr_t result = base::bits::RotateLeft64(rs_val, sh);
      uintptr_t mask = (0xFFFFFFFFFFFFFFFF >> mb) & (0xFFFFFFFFFFFFFFFF << sh);
      result &= mask;
      set_register(ra, result);
      if (instr->Bit(0)) {  // RC bit set
        SetCR0(result);
      }
      return;
    }
    case RLDIMI: {
      int ra = instr->RAValue();
      int rs = instr->RSValue();
      uintptr_t rs_val = get_register(rs);
      intptr_t ra_val = get_register(ra);
      int sh = (instr->Bits(15, 11) | (instr->Bit(1) << 5));
      int mb = (instr->Bits(10, 6) | (instr->Bit(5) << 5));
      int me = 63 - sh;
      uintptr_t result = base::bits::RotateLeft64(rs_val, sh);
      uintptr_t mask = 0;
      if (mb < me + 1) {
        uintptr_t bit = 0x8000000000000000 >> mb;
        for (; mb <= me; mb++) {
          mask |= bit;
          bit >>= 1;
        }
      } else if (mb == me + 1) {
        mask = 0xFFFFFFFFFFFFFFFF;
      } else {                                           // mb > me+1
        uintptr_t bit = 0x8000000000000000 >> (me + 1);  // needs to be tested
        mask = 0xFFFFFFFFFFFFFFFF;
        for (; me < mb; me++) {
          mask ^= bit;
          bit >>= 1;
        }
      }
      result &= mask;
      ra_val &= ~mask;
      result |= ra_val;
      set_register(ra, result);
      if (instr->Bit(0)) {  // RC bit set
        SetCR0(result);
      }
      return;
    }
    case RLDCL: {
      int ra = instr->RAValue();
      int rs = instr->RSValue();
      int rb = instr->RBValue();
      uintptr_t rs_val = get_register(rs);
      uintptr_t rb_val = get_register(rb);
      int sh = (rb_val & 0x3F);
      int mb = (instr->Bits(10, 6) | (instr->Bit(5) << 5));
      DCHECK(sh >= 0 && sh <= 63);
      DCHECK(mb >= 0 && mb <= 63);
      uintptr_t result = base::bits::RotateLeft64(rs_val, sh);
      uintptr_t mask = 0xFFFFFFFFFFFFFFFF >> mb;
      result &= mask;
      set_register(ra, result);
      if (instr->Bit(0)) {  // RC bit set
        SetCR0(result);
      }
      return;
    }

    case LD:
    case LDU:
    case LWA: {
      int ra = instr->RAValue();
      int rt = instr->RTValue();
      int64_t ra_val = ra == 0 ? 0 : get_register(ra);
      int offset = SIGN_EXT_IMM16(instr->Bits(15, 0) & ~3);
      switch (instr->Bits(1, 0)) {
        case 0: {  // ld
          intptr_t result = ReadDW(ra_val + offset);
          set_register(rt, result);
          break;
        }
        case 1: {  // ldu
          intptr_t result = ReadDW(ra_val + offset);
          set_register(rt, result);
          DCHECK_NE(ra, 0);
          set_register(ra, ra_val + offset);
          break;
        }
        case 2: {  // lwa
          intptr_t result = ReadW(ra_val + offset);
          set_register(rt, result);
          break;
        }
      }
      break;
    }

    case STD:
    case STDU: {
      int ra = instr->RAValue();
      int rs = instr->RSValue();
      int64_t ra_val = ra == 0 ? 0 : get_register(ra);
      int64_t rs_val = get_register(rs);
      int offset = SIGN_EXT_IMM16(instr->Bits(15, 0) & ~3);
      WriteDW(ra_val + offset, rs_val);
      if (opcode == STDU) {
        DCHECK_NE(ra, 0);
        set_register(ra, ra_val + offset);
      }
      break;
    }
#endif

    case XSADDDP: {
      int frt = instr->RTValue();
      int fra = instr->RAValue();
      int frb = instr->RBValue();
      double fra_val = get_double_from_d_register(fra);
      double frb_val = get_double_from_d_register(frb);
      double frt_val = fra_val + frb_val;
      set_d_register_from_double(frt, frt_val);
      return;
    }
    case XSSUBDP: {
      int frt = instr->RTValue();
      int fra = instr->RAValue();
      int frb = instr->RBValue();
      double fra_val = get_double_from_d_register(fra);
      double frb_val = get_double_from_d_register(frb);
      double frt_val = fra_val - frb_val;
      set_d_register_from_double(frt, frt_val);
      return;
    }
    case XSMULDP: {
      int frt = instr->RTValue();
      int fra = instr->RAValue();
      int frb = instr->RBValue();
      double fra_val = get_double_from_d_register(fra);
      double frb_val = get_double_from_d_register(frb);
      double frt_val = fra_val * frb_val;
      set_d_register_from_double(frt, frt_val);
      return;
    }
    case XSDIVDP: {
      int frt = instr->RTValue();
      int fra = instr->RAValue();
      int frb = instr->RBValue();
      double fra_val = get_double_from_d_register(fra);
      double frb_val = get_double_from_d_register(frb);
      double frt_val = fra_val / frb_val;
      set_d_register_from_double(frt, frt_val);
      return;
    }
    case MTCRF: {
      int rs = instr->RSValue();
      uint32_t rs_val = static_cast<int32_t>(get_register(rs));
      uint8_t fxm = instr->Bits(19, 12);
      uint8_t bit_mask = 0x80;
      const int field_bit_count = 4;
      const int max_field_index = 7;
      uint32_t result = 0;
      for (int i = 0; i <= max_field_index; i++) {
        result <<= field_bit_count;
        uint32_t source = condition_reg_;
        if ((bit_mask & fxm) != 0) {
          // take it from rs.
          source = rs_val;
        }
        result |= ((source << i * field_bit_count) >> i * field_bit_count) >>
                  (max_field_index - i) * field_bit_count;
        bit_mask >>= 1;
      }
      condition_reg_ = result;
      break;
    }
    // Vector instructions.
    case LVX: {
      DECODE_VX_INSTRUCTION(vrt, ra, rb, T)
      GET_ADDRESS(ra, rb, ra_val, rb_val)
      intptr_t addr = (ra_val + rb_val) & 0xFFFFFFFFFFFFFFF0;
      simdr_t* ptr = reinterpret_cast<simdr_t*>(addr);
      set_simd_register(vrt, *ptr);
      break;
    }
    case STVX: {
      DECODE_VX_INSTRUCTION(vrs, ra, rb, S)
      GET_ADDRESS(ra, rb, ra_val, rb_val)
      __int128 vrs_val = base::bit_cast<__int128>(get_simd_register(vrs).int8);
      WriteQW((ra_val + rb_val) & 0xFFFFFFFFFFFFFFF0, vrs_val);
      break;
    }
    case LXVD: {
      DECODE_VX_INSTRUCTION(xt, ra, rb, T)
      GET_ADDRESS(ra, rb, ra_val, rb_val)
      set_simd_register_by_lane<int64_t>(xt, 0, ReadDW(ra_val + rb_val));
      set_simd_register_by_lane<int64_t>(
          xt, 1, ReadDW(ra_val + rb_val + kSystemPointerSize));
      break;
    }
    case LXVX: {
      DECODE_VX_INSTRUCTION(vrt, ra, rb, T)
      GET_ADDRESS(ra, rb, ra_val, rb_val)
      intptr_t addr = ra_val + rb_val;
      simdr_t* ptr = reinterpret_cast<simdr_t*>(addr);
      set_simd_register(vrt, *ptr);
      break;
    }
    case STXVD: {
      DECODE_VX_INSTRUCTION(xs, ra, rb, S)
      GET_ADDRESS(ra, rb, ra_val, rb_val)
      WriteDW(ra_val + rb_val, get_simd_register_by_lane<int64_t>(xs, 0));
      WriteDW(ra_val + rb_val + kSystemPointerSize,
              get_simd_register_by_lane<int64_t>(xs, 1));
      break;
    }
    case STXVX: {
      DECODE_VX_INSTRUCTION(vrs, ra, rb, S)
      GET_ADDRESS(ra, rb, ra_val, rb_val)
      intptr_t addr = ra_val + rb_val;
      __int128 vrs_val = base::bit_cast<__int128>(get_simd_register(vrs).int8);
      WriteQW(addr, vrs_val);
      break;
    }
    case LXSIBZX: {
      DECODE_VX_INSTRUCTION(xt, ra, rb, T)
      GET_ADDRESS(ra, rb, ra_val, rb_val)
      set_simd_register_by_lane<uint64_t>(xt, 0, ReadBU(ra_val + rb_val));
      break;
    }
    case LXSIHZX: {
      DECODE_VX_INSTRUCTION(xt, ra, rb, T)
      GET_ADDRESS(ra, rb, ra_val, rb_val)
      set_simd_register_by_lane<uint64_t>(xt, 0, ReadHU(ra_val + rb_val));
      break;
    }
    case LXSIWZX: {
      DECODE_VX_INSTRUCTION(xt, ra, rb, T)
      GET_ADDRESS(ra, rb, ra_val, rb_val)
      set_simd_register_by_lane<uint64_t>(xt, 0, ReadWU(ra_val + rb_val));
      break;
    }
    case LXSDX: {
      DECODE_VX_INSTRUCTION(xt, ra, rb, T)
      GET_ADDRESS(ra, rb, ra_val, rb_val)
      set_simd_register_by_lane<int64_t>(xt, 0, ReadDW(ra_val + rb_val));
      break;
    }
    case STXSIBX: {
      DECODE_VX_INSTRUCTION(xs, ra, rb, S)
      GET_ADDRESS(ra, rb, ra_val, rb_val)
      WriteB(ra_val + rb_val, get_simd_register_by_lane<int8_t>(xs, 7));
      break;
    }
    case STXSIHX: {
      DECODE_VX_INSTRUCTION(xs, ra, rb, S)
      GET_ADDRESS(ra, rb, ra_val, rb_val)
      WriteH(ra_val + rb_val, get_simd_register_by_lane<int16_t>(xs, 3));
      break;
    }
    case STXSIWX: {
      DECODE_VX_INSTRUCTION(xs, ra, rb, S)
      GET_ADDRESS(ra, rb, ra_val, rb_val)
      WriteW(ra_val + rb_val, get_simd_register_by_lane<int32_t>(xs, 1));
      break;
    }
    case STXSDX: {
      DECODE_VX_INSTRUCTION(xs, ra, rb, S)
      GET_ADDRESS(ra, rb, ra_val, rb_val)
      WriteDW(ra_val + rb_val, get_simd_register_by_lane<int64_t>(xs, 0));
      break;
    }
    case XXBRQ: {
      int t = instr->RTValue();
      int b = instr->RBValue();
      __int128 xb_val = base::bit_cast<__int128>(get_simd_register(b).int8);
      __int128 xb_val_reversed = __builtin_bswap128(xb_val);
      simdr_t simdr_xb = base::bit_cast<simdr_t>(xb_val_reversed);
      set_simd_register(t, simdr_xb);
      break;
    }
#define VSPLT(type)                                       \
  uint8_t uim = instr->Bits(19, 16);                      \
  int vrt = instr->RTValue();                             \
  int vrb = instr->RBValue();                             \
  type value = get_simd_register_by_lane<type>(vrb, uim); \
  FOR_EACH_LANE(i, type) { set_simd_register_by_lane<type>(vrt, i, value); }
    case VSPLTW: {
      VSPLT(int32_t)
      break;
    }
    case VSPLTH: {
      VSPLT(int16_t)
      break;
    }
    case VSPLTB: {
      VSPLT(int8_t)
      break;
    }
    case XXSPLTIB: {
      int8_t imm8 = instr->Bits(18, 11);
      int t = instr->RTValue();
      FOR_EACH_LANE(i, int8_t) {
        set_simd_register_by_lane<int8_t>(t, i, imm8);
      }
      break;
    }
#undef VSPLT
#define VSPLTI(type)                                                \
  type sim = static_cast<type>(SIGN_EXT_IMM5(instr->Bits(20, 16))); \
  int vrt = instr->RTValue();                                       \
  FOR_EACH_LANE(i, type) { set_simd_register_by_lane<type>(vrt, i, sim); }
    case VSPLTISW: {
      VSPLTI(int32_t)
      break;
    }
    case VSPLTISH: {
      VSPLTI(int16_t)
      break;
    }
    case VSPLTISB: {
      VSPLTI(int8_t)
      break;
    }
#undef VSPLTI
#define VINSERT(type, element)       \
  uint8_t uim = instr->Bits(19, 16); \
  int vrt = instr->RTValue();        \
  int vrb = instr->RBValue();        \
  set_simd_register_bytes<type>(     \
      vrt, uim, get_simd_register_by_lane<type>(vrb, element));
    case VINSERTD: {
      VINSERT(int64_t, 0)
      break;
    }
    case VINSERTW: {
      VINSERT(int32_t, 1)
      break;
    }
    case VINSERTH: {
      VINSERT(int16_t, 3)
      break;
    }
    case VINSERTB: {
      VINSERT(int8_t, 7)
      break;
    }
#undef VINSERT
#define VINSERT_IMMEDIATE(type)                   \
  uint8_t uim = instr->Bits(19, 16);              \
  int vrt = instr->RTValue();                     \
  int rb = instr->RBValue();                      \
  type src = static_cast<type>(get_register(rb)); \
  set_simd_register_bytes<type>(vrt, uim, src);
    case VINSD: {
      VINSERT_IMMEDIATE(int64_t)
      break;
    }
    case VINSW: {
      VINSERT_IMMEDIATE(int32_t)
      break;
    }
#undef VINSERT_IMMEDIATE
#define VEXTRACT(type, element)                       \
  uint8_t uim = instr->Bits(19, 16);                  \
  int vrt = instr->RTValue();                         \
  int vrb = instr->RBValue();                         \
  type val = get_simd_register_bytes<type>(vrb, uim); \
  set_simd_register_by_lane<uint64_t>(vrt, 0, 0);     \
  set_simd_register_by_lane<uint64_t>(vrt, 1, 0);     \
  set_simd_register_by_lane<type>(vrt, element, val);
    case VEXTRACTD: {
      VEXTRACT(uint64_t, 0)
      break;
    }
    case VEXTRACTUW: {
      VEXTRACT(uint32_t, 1)
      break;
    }
    case VEXTRACTUH: {
      VEXTRACT(uint16_t, 3)
      break;
    }
    case VEXTRACTUB: {
      VEXTRACT(uint8_t, 7)
      break;
    }
#undef VEXTRACT
#define VECTOR_LOGICAL_OP(expr)                               \
  DECODE_VX_INSTRUCTION(t, a, b, T)                           \
  FOR_EACH_LANE(i, int64_t) {                                 \
    int64_t a_val = get_simd_register_by_lane<int64_t>(a, i); \
    int64_t b_val = get_simd_register_by_lane<int64_t>(b, i); \
    set_simd_register_by_lane<int64_t>(t, i, expr);           \
  }
    case VAND: {
      VECTOR_LOGICAL_OP(a_val & b_val)
      break;
    }
    case VANDC: {
      VECTOR_LOGICAL_OP(a_val & (~b_val))
      break;
    }
    case VOR: {
      VECTOR_LOGICAL_OP(a_val | b_val)
      break;
    }
    case VNOR: {
      VECTOR_LOGICAL_OP(~(a_val | b_val))
      break;
    }
    case VXOR: {
      VECTOR_LOGICAL_OP(a_val ^ b_val)
      break;
    }
#undef VECTOR_LOGICAL_OP
#define VECTOR_ARITHMETIC_OP(type, op)                 \
  DECODE_VX_INSTRUCTION(t, a, b, T)                    \
  FOR_EACH_LANE(i, type) {                             \
    set_simd_register_by_lane<type>(                   \
        t, i,                                          \
        get_simd_register_by_lane<type>(a, i)          \
            op get_simd_register_by_lane<type>(b, i)); \
  }
    case XVADDDP: {
      VECTOR_ARITHMETIC_OP(double, +)
      break;
    }
    case XVSUBDP: {
      VECTOR_ARITHMETIC_OP(double, -)
      break;
    }
    case XVMULDP: {
      VECTOR_ARITHMETIC_OP(double, *)
      break;
    }
    case XVDIVDP: {
      VECTOR_ARITHMETIC_OP(double, /)
      break;
    }
    case VADDFP: {
      VECTOR_ARITHMETIC_OP(float, +)
      break;
    }
    case VSUBFP: {
      VECTOR_ARITHMETIC_OP(float, -)
      break;
    }
    case XVMULSP: {
      VECTOR_ARITHMETIC_OP(float, *)
      break;
    }
    case XVDIVSP: {
      VECTOR_ARITHMETIC_OP(float, /)
      break;
    }
    case VADDUDM: {
      VECTOR_ARITHMETIC_OP(int64_t, +)
      break;
    }
    case VSUBUDM: {
      VECTOR_ARITHMETIC_OP(int64_t, -)
      break;
    }
    case VMULLD: {
      VECTOR_ARITHMETIC_OP(int64_t, *)
      break;
    }
    case VADDUWM: {
      VECTOR_ARITHMETIC_OP(int32_t, +)
      break;
    }
    case VSUBUWM: {
      VECTOR_ARITHMETIC_OP(int32_t, -)
      break;
    }
    case VMULUWM: {
      VECTOR_ARITHMETIC_OP(int32_t, *)
      break;
    }
    case VADDUHM: {
      VECTOR_ARITHMETIC_OP(int16_t, +)
      break;
    }
    case VSUBUHM: {
      VECTOR_ARITHMETIC_OP(int16_t, -)
      break;
    }
    case VADDUBM: {
      VECTOR_ARITHMETIC_OP(int8_t, +)
      break;
    }
    case VSUBUBM: {
      VECTOR_ARITHMETIC_OP(int8_t, -)
      break;
    }
#define VECTOR_MULTIPLY_EVEN_ODD(input_type, result_type, is_odd)              \
  DECODE_VX_INSTRUCTION(t, a, b, T)                                            \
  size_t i = 0, j = 0, k = 0;                                                  \
  size_t lane_size = sizeof(input_type);                                       \
  if (is_odd) {                                                                \
    i = 1;                                                                     \
    j = lane_size;                                                             \
  }                                                                            \
  for (; j < kSimd128Size; i += 2, j += lane_size * 2, k++) {                  \
    result_type src0 =                                                         \
        static_cast<result_type>(get_simd_register_by_lane<input_type>(a, i)); \
    result_type src1 =                                                         \
        static_cast<result_type>(get_simd_register_by_lane<input_type>(b, i)); \
    set_simd_register_by_lane<result_type>(t, k, src0 * src1);                 \
  }
    case VMULEUB: {
      VECTOR_MULTIPLY_EVEN_ODD(uint8_t, uint16_t, false)
      break;
    }
    case VMULESB: {
      VECTOR_MULTIPLY_EVEN_ODD(int8_t, int16_t, false)
      break;
    }
    case VMULOUB: {
      VECTOR_MULTIPLY_EVEN_ODD(uint8_t, uint16_t, true)
      break;
    }
    case VMULOSB: {
      VECTOR_MULTIPLY_EVEN_ODD(int8_t, int16_t, true)
      break;
    }
    case VMULEUH: {
      VECTOR_MULTIPLY_EVEN_ODD(uint16_t, uint32_t, false)
      break;
    }
    case VMULESH: {
      VECTOR_MULTIPLY_EVEN_ODD(int16_t, int32_t, false)
      break;
    }
    case VMULOUH: {
      VECTOR_MULTIPLY_EVEN_ODD(uint16_t, uint32_t, true)
      break;
    }
    case VMULOSH: {
      VECTOR_MULTIPLY_EVEN_ODD(int16_t, int32_t, true)
      break;
    }
    case VMULEUW: {
      VECTOR_MULTIPLY_EVEN_ODD(uint32_t, uint64_t, false)
      break;
    }
    case VMULESW: {
      VECTOR_MULTIPLY_EVEN_ODD(int32_t, int64_t, false)
      break;
    }
    case VMULOUW: {
      VECTOR_MULTIPLY_EVEN_ODD(uint32_t, uint64_t, true)
      break;
    }
    case VMULOSW: {
      VECTOR_MULTIPLY_EVEN_ODD(int32_t, int64_t, true)
      break;
    }
#undef VECTOR_MULTIPLY_EVEN_ODD
#define VECTOR_MERGE(type, is_low_side)                                    \
  DECODE_VX_INSTRUCTION(t, a, b, T)                                        \
  constexpr size_t index_limit = (kSimd128Size / sizeof(type)) / 2;        \
  for (size_t i = 0, source_index = is_low_side ? i + index_limit : i;     \
       i < index_limit; i++, source_index++) {                             \
    set_simd_register_by_lane<type>(                                       \
        t, 2 * i, get_simd_register_by_lane<type>(a, source_index));       \
    set_simd_register_by_lane<type>(                                       \
        t, (2 * i) + 1, get_simd_register_by_lane<type>(b, source_index)); \
  }
    case VMRGLW: {
      VECTOR_MERGE(int32_t, true)
      break;
    }
    case VMRGHW: {
      VECTOR_MERGE(int32_t, false)
      break;
    }
    case VMRGLH: {
      VECTOR_MERGE(int16_t, true)
      break;
    }
    case VMRGHH: {
      VECTOR_MERGE(int16_t, false)
      break;
    }
#undef VECTOR_MERGE
#undef VECTOR_ARITHMETIC_OP
#define VECTOR_MIN_MAX_OP(type, op)                                        \
  DECODE_VX_INSTRUCTION(t, a, b, T)                                        \
  FOR_EACH_LANE(i, type) {                                                 \
    type a_val = get_simd_register_by_lane<type>(a, i);                    \
    type b_val = get_simd_register_by_lane<type>(b, i);                    \
    set_simd_register_by_lane<type>(t, i, a_val op b_val ? a_val : b_val); \
  }
    case XSMINDP: {
      DECODE_VX_INSTRUCTION(t, a, b, T)
      double a_val = get_double_from_d_register(a);
      double b_val = get_double_from_d_register(b);
      set_d_register_from_double(t, VSXFPMin<double>(a_val, b_val));
      break;
    }
    case XSMAXDP: {
      DECODE_VX_INSTRUCTION(t, a, b, T)
      double a_val = get_double_from_d_register(a);
      double b_val = get_double_from_d_register(b);
      set_d_register_from_double(t, VSXFPMax<double>(a_val, b_val));
      break;
    }
    case XVMINDP: {
      DECODE_VX_INSTRUCTION(t, a, b, T)
      FOR_EACH_LANE(i, double) {
        double a_val = get_simd_register_by_lane<double>(a, i);
        double b_val = get_simd_register_by_lane<double>(b, i);
        set_simd_register_by_lane<double>(t, i, VSXFPMin<double>(a_val, b_val));
      }
      break;
    }
    case XVMAXDP: {
      DECODE_VX_INSTRUCTION(t, a, b, T)
      FOR_EACH_LANE(i, double) {
        double a_val = get_simd_register_by_lane<double>(a, i);
        double b_val = get_simd_register_by_lane<double>(b, i);
        set_simd_register_by_lane<double>(t, i, VSXFPMax<double>(a_val, b_val));
      }
      break;
    }
    case VMINFP: {
      DECODE_VX_INSTRUCTION(t, a, b, T)
      FOR_EACH_LANE(i, float) {
        float a_val = get_simd_register_by_lane<float>(a, i);
        float b_val = get_simd_register_by_lane<float>(b, i);
        set_simd_register_by_lane<float>(t, i, VMXFPMin(a_val, b_val));
      }
      break;
    }
    case VMAXFP: {
      DECODE_VX_INSTRUCTION(t, a, b, T)
      FOR_EACH_LANE(i, float) {
        float a_val = get_simd_register_by_lane<float>(a, i);
        float b_val = get_simd_register_by_lane<float>(b, i);
        set_simd_register_by_lane<float>(t, i, VMXFPMax(a_val, b_val));
      }
      break;
    }
    case VMINSD: {
      VECTOR_MIN_MAX_OP(int64_t, <)
      break;
    }
    case VMINUD: {
      VECTOR_MIN_MAX_OP(uint64_t, <)
      break;
    }
    case VMINSW: {
      VECTOR_MIN_MAX_OP(int32_t, <)
      break;
    }
    case VMINUW: {
      VECTOR_MIN_MAX_OP(uint32_t, <)
      break;
    }
    case VMINSH: {
      VECTOR_MIN_MAX_OP(int16_t, <)
      break;
    }
    case VMINUH: {
      VECTOR_MIN_MAX_OP(uint16_t, <)
      break;
    }
    case VMINSB: {
      VECTOR_MIN_MAX_OP(int8_t, <)
      break;
    }
    case VMINUB: {
      VECTOR_MIN_MAX_OP(uint8_t, <)
      break;
    }
    case VMAXSD: {
      VECTOR_MIN_MAX_OP(int64_t, >)
      break;
    }
    case VMAXUD: {
      VECTOR_MIN_MAX_OP(uint64_t, >)
      break;
    }
    case VMAXSW: {
      VECTOR_MIN_MAX_OP(int32_t, >)
      break;
    }
    case VMAXUW: {
      VECTOR_MIN_MAX_OP(uint32_t, >)
      break;
    }
    case VMAXSH: {
      VECTOR_MIN_MAX_OP(int16_t, >)
      break;
    }
    case VMAXUH: {
      VECTOR_MIN_MAX_OP(uint16_t, >)
      break;
    }
    case VMAXSB: {
      VECTOR_MIN_MAX_OP(int8_t, >)
      break;
    }
    case VMAXUB: {
      VECTOR_MIN_MAX_OP(uint8_t, >)
      break;
    }
#undef VECTOR_MIN_MAX_OP
#define VECTOR_SHIFT_OP(type, op, mask)                        \
  DECODE_VX_INSTRUCTION(t, a, b, T)                            \
  FOR_EACH_LANE(i, type) {                                     \
    set_simd_register_by_lane<type>(                           \
        t, i,                                                  \
        get_simd_register_by_lane<type>(a, i)                  \
            op(get_simd_register_by_lane<type>(b, i) & mask)); \
  }
    case VSLD: {
      VECTOR_SHIFT_OP(int64_t, <<, 0x3f)
      break;
    }
    case VSRAD: {
      VECTOR_SHIFT_OP(int64_t, >>, 0x3f)
      break;
    }
    case VSRD: {
      VECTOR_SHIFT_OP(uint64_t, >>, 0x3f)
      break;
    }
    case VSLW: {
      VECTOR_SHIFT_OP(int32_t, <<, 0x1f)
      break;
    }
    case VSRAW: {
      VECTOR_SHIFT_OP(int32_t, >>, 0x1f)
      break;
    }
    case VSRW: {
      VECTOR_SHIFT_OP(uint32_t, >>, 0x1f)
      break;
    }
    case VSLH: {
      VECTOR_SHIFT_OP(int16_t, <<, 0xf)
      break;
    }
    case VSRAH: {
      VECTOR_SHIFT_OP(int16_t, >>, 0xf)
      break;
    }
    case VSRH: {
      VECTOR_SHIFT_OP(uint16_t, >>, 0xf)
      break;
    }
    case VSLB: {
      VECTOR_SHIFT_OP(int8_t, <<, 0x7)
      break;
    }
    case VSRAB: {
      VECTOR_SHIFT_OP(int8_t, >>, 0x7)
      break;
    }
    case VSRB: {
      VECTOR_SHIFT_OP(uint8_t, >>, 0x7)
      break;
    }
#undef VECTOR_SHIFT_OP
#define VECTOR_COMPARE_OP(type_in, type_out, is_fp, op) \
  VectorCompareOp<type_in, type_out>(                   \
      this, instr, is_fp, [](type_in a, type_in b) { return a op b; });
    case XVCMPEQDP: {
      VECTOR_COMPARE_OP(double, int64_t, true, ==)
      break;
    }
    case XVCMPGEDP: {
      VECTOR_COMPARE_OP(double, int64_t, true, >=)
      break;
    }
    case XVCMPGTDP: {
      VECTOR_COMPARE_OP(double, int64_t, true, >)
      break;
    }
    case XVCMPEQSP: {
      VECTOR_COMPARE_OP(float, int32_t, true, ==)
      break;
    }
    case XVCMPGESP: {
      VECTOR_COMPARE_OP(float, int32_t, true, >=)
      break;
    }
    case XVCMPGTSP: {
      VECTOR_COMPARE_OP(float, int32_t, true, >)
      break;
    }
    case VCMPEQUD: {
      VECTOR_COMPARE_OP(uint64_t, int64_t, false, ==)
      break;
    }
    case VCMPGTSD: {
      VECTOR_COMPARE_OP(int64_t, int64_t, false, >)
      break;
    }
    case VCMPGTUD: {
      VECTOR_COMPARE_OP(uint64_t, int64_t, false, >)
      break;
    }
    case VCMPEQUW: {
      VECTOR_COMPARE_OP(uint32_t, int32_t, false, ==)
      break;
    }
    case VCMPGTSW: {
      VECTOR_COMPARE_OP(int32_t, int32_t, false, >)
      break;
    }
    case VCMPGTUW: {
      VECTOR_COMPARE_OP(uint32_t, int32_t, false, >)
      break;
    }
    case VCMPEQUH: {
      VECTOR_COMPARE_OP(uint16_t, int16_t, false, ==)
      break;
    }
    case VCMPGTSH: {
      VECTOR_COMPARE_OP(int16_t, int16_t, false, >)
      break;
    }
    case VCMPGTUH: {
      VECTOR_COMPARE_OP(uint16_t, int16_t, false, >)
      break;
    }
    case VCMPEQUB: {
      VECTOR_COMPARE_OP(uint8_t, int8_t, false, ==)
      break;
    }
    case VCMPGTSB: {
      VECTOR_COMPARE_OP(int8_t, int8_t, false, >)
      break;
    }
    case VCMPGTUB: {
      VECTOR_COMPARE_OP(uint8_t, int8_t, false, >)
      break;
    }
#undef VECTOR_COMPARE_OP
    case XVCVSPSXWS: {
      VectorConverFromFPSaturate<float, int32_t>(this, instr, kMinInt, kMaxInt);
      break;
    }
    case XVCVSPUXWS: {
      VectorConverFromFPSaturate<float, uint32_t>(this, instr, 0, kMaxUInt32);
      break;
    }
    case XVCVDPSXWS: {
      VectorConverFromFPSaturate<double, int32_t>(this, instr, kMinInt, kMaxInt,
                                                  true);
      break;
    }
    case XVCVDPUXWS: {
      VectorConverFromFPSaturate<double, uint32_t>(this, instr, 0, kMaxUInt32,
                                                   true);
      break;
    }
    case XVCVSXWSP: {
      int t = instr->RTValue();
      int b = instr->RBValue();
      FOR_EACH_LANE(i, int32_t) {
        int32_t b_val = get_simd_register_by_lane<int32_t>(b, i);
        set_simd_register_by_lane<float>(t, i, static_cast<float>(b_val));
      }
      break;
    }
    case XVCVUXWSP: {
      int t = instr->RTValue();
      int b = instr->RBValue();
      FOR_EACH_LANE(i, uint32_t) {
        uint32_t b_val = get_simd_register_by_lane<uint32_t>(b, i);
        set_simd_register_by_lane<float>(t, i, static_cast<float>(b_val));
      }
      break;
    }
    case XVCVSXDDP: {
      int t = instr->RTValue();
      int b = instr->RBValue();
      FOR_EACH_LANE(i, int64_t) {
        int64_t b_val = get_simd_register_by_lane<int64_t>(b, i);
        set_simd_register_by_lane<double>(t, i, static_cast<double>(b_val));
      }
      break;
    }
    case XVCVUXDDP: {
      int t = instr->RTValue();
      int b = instr->RBValue();
      FOR_EACH_LANE(i, uint64_t) {
        uint64_t b_val = get_simd_register_by_lane<uint64_t>(b, i);
        set_simd_register_by_lane<double>(t, i, static_cast<double>(b_val));
      }
      break;
    }
    case XVCVSPDP: {
      int t = instr->RTValue();
      int b = instr->RBValue();
      FOR_EACH_LANE(i, double) {
        float b_val = get_simd_register_by_lane<float>(b, 2 * i);
        set_simd_register_by_lane<double>(t, i, static_cast<double>(b_val));
      }
      break;
    }
    case XVCVDPSP: {
      int t = instr->RTValue();
      int b = instr->RBValue();
      FOR_EACH_LANE(i, double) {
        double b_val = get_simd_register_by_lane<double>(b, i);
        set_simd_register_by_lane<float>(t, 2 * i, static_cast<float>(b_val));
      }
      break;
    }
    case XSCVSPDPN: {
      int t = instr->RTValue();
      int b = instr->RBValue();
      uint64_t double_bits = get_d_register(b);
      // Value is at the high 32 bits of the register.
      float f = base::bit_cast<float, uint32_t>(
          static_cast<uint32_t>(double_bits >> 32));
      double_bits = base::bit_cast<uint64_t, double>(static_cast<double>(f));
      // Preserve snan.
      if (is_snan(f)) {
        double_bits &= 0xFFF7FFFFFFFFFFFFU;  // Clear bit 51.
      }
      set_d_register(t, double_bits);
      break;
    }
    case XSCVDPSPN: {
      int t = instr->RTValue();
      int b = instr->RBValue();
      double b_val = get_double_from_d_register(b);
      uint64_t float_bits = static_cast<uint64_t>(
          base::bit_cast<uint32_t, float>(static_cast<float>(b_val)));
      // Preserve snan.
      if (is_snan(b_val)) {
        float_bits &= 0xFFBFFFFFU;  // Clear bit 22.
      }
      // fp result is placed in both 32bit halfs of the dst.
      float_bits = (float_bits << 32) | float_bits;
      set_d_register(t, float_bits);
      break;
    }
#define VECTOR_UNPACK(S, D, if_high_side)                           \
  int t = instr->RTValue();                                         \
  int b = instr->RBValue();                                         \
  constexpr size_t kItemCount = kSimd128Size / sizeof(D);           \
  D temps[kItemCount] = {0};                                        \
  /* Avoid overwriting src if src and dst are the same register. */ \
  FOR_EACH_LANE(i, D) {                                             \
    temps[i] = get_simd_register_by_lane<S>(b, i, if_high_side);    \
  }                                                                 \
  FOR_EACH_LANE(i, D) {                                             \
    set_simd_register_by_lane<D>(t, i, temps[i], if_high_side);     \
  }
    case VUPKHSB: {
      VECTOR_UNPACK(int8_t, int16_t, true)
      break;
    }
    case VUPKHSH: {
      VECTOR_UNPACK(int16_t, int32_t, true)
      break;
    }
    case VUPKHSW: {
      VECTOR_UNPACK(int32_t, int64_t, true)
      break;
    }
    case VUPKLSB: {
      VECTOR_UNPACK(int8_t, int16_t, false)
      break;
    }
    case VUPKLSH: {
      VECTOR_UNPACK(int16_t, int32_t, false)
      break;
    }
    case VUPKLSW: {
      VECTOR_UNPACK(int32_t, int64_t, false)
      break;
    }
#undef VECTOR_UNPACK
    case VPKSWSS: {
      VectorPackSaturate<int32_t, int16_t>(this, instr, kMinInt16, kMaxInt16);
      break;
    }
    case VPKSWUS: {
      VectorPackSaturate<int32_t, uint16_t>(this, instr, 0, kMaxUInt16);
      break;
    }
    case VPKSHSS: {
      VectorPackSaturate<int16_t, int8_t>(this, instr, kMinInt8, kMaxInt8);
      break;
    }
    case VPKSHUS: {
      VectorPackSaturate<int16_t, uint8_t>(this, instr, 0, kMaxUInt8);
      break;
    }
#define VECTOR_ADD_SUB_SATURATE(intermediate_type, result_type, op, min_val, \
                                max_val)                                     \
  DECODE_VX_INSTRUCTION(t, a, b, T)                                          \
  FOR_EACH_LANE(i, result_type) {                                            \
    intermediate_type a_val = static_cast<intermediate_type>(                \
        get_simd_register_by_lane<result_type>(a, i));                       \
    intermediate_type b_val = static_cast<intermediate_type>(                \
        get_simd_register_by_lane<result_type>(b, i));                       \
    intermediate_type t_val = a_val op b_val;                                \
    if (t_val > max_val)                                                     \
      t_val = max_val;                                                       \
    else if (t_val < min_val)                                                \
      t_val = min_val;                                                       \
    set_simd_register_by_lane<result_type>(t, i,                             \
                                           static_cast<result_type>(t_val)); \
  }
    case VADDSHS: {
      VECTOR_ADD_SUB_SATURATE(int32_t, int16_t, +, kMinInt16, kMaxInt16)
      break;
    }
    case VSUBSHS: {
      VECTOR_ADD_SUB_SATURATE(int32_t, int16_t, -, kMinInt16, kMaxInt16)
      break;
    }
    case VADDUHS: {
      VECTOR_ADD_SUB_SATURATE(int32_t, uint16_t, +, 0, kMaxUInt16)
      break;
    }
    case VSUBUHS: {
      VECTOR_ADD_SUB_SATURATE(int32_t, uint16_t, -, 0, kMaxUInt16)
      break;
    }
    case VADDSBS: {
      VECTOR_ADD_SUB_SATURATE(int16_t, int8_t, +, kMinInt8, kMaxInt8)
      break;
    }
    case VSUBSBS: {
      VECTOR_ADD_SUB_SATURATE(int16_t, int8_t, -, kMinInt8, kMaxInt8)
      break;
    }
    case VADDUBS: {
      VECTOR_ADD_SUB_SATURATE(int16_t, uint8_t, +, 0, kMaxUInt8)
      break;
    }
    case VSUBUBS: {
      VECTOR_ADD_SUB_SATURATE(int16_t, uint8_t, -, 0, kMaxUInt8)
      break;
    }
#undef VECTOR_ADD_SUB_SATURATE
#define VECTOR_FP_ROUNDING(type, op)                       \
  int t = instr->RTValue();                                \
  int b = instr->RBValue();                                \
  FOR_EACH_LANE(i, type) {                                 \
    type b_val = get_simd_register_by_lane<type>(b, i);    \
    set_simd_register_by_lane<type>(t, i, std::op(b_val)); \
  }
    case XVRDPIP: {
      VECTOR_FP_ROUNDING(double, ceil)
      break;
    }
    case XVRDPIM: {
      VECTOR_FP_ROUNDING(double, floor)
      break;
    }
    case XVRDPIZ: {
      VECTOR_FP_ROUNDING(double, trunc)
      break;
    }
    case XVRDPI: {
      VECTOR_FP_ROUNDING(double, nearbyint)
      break;
    }
    case XVRSPIP: {
      VECTOR_FP_ROUNDING(float, ceilf)
      break;
    }
    case XVRSPIM: {
      VECTOR_FP_ROUNDING(float, floorf)
      break;
    }
    case XVRSPIZ: {
      VECTOR_FP_ROUNDING(float, truncf)
      break;
    }
    case XVRSPI: {
      VECTOR_FP_ROUNDING(float, nearbyintf)
      break;
    }
#undef VECTOR_FP_ROUNDING
    case VSEL: {
      int vrt = instr->RTValue();
      int vra = instr->RAValue();
      int vrb = instr->RBValue();
      int vrc = instr->RCValue();
      unsigned __int128 src_1 =
          base::bit_cast<__int128>(get_simd_register(vra).int8);
      unsigned __int128 src_2 =
          base::bit_cast<__int128>(get_simd_register(vrb).int8);
      unsigned __int128 src_3 =
          base::bit_cast<__int128>(get_simd_register(vrc).int8);
      unsigned __int128 tmp = (src_1 & ~src_3) | (src_2 & src_3);
      simdr_t* result = reinterpret_cast<simdr_t*>(&tmp);
      set_simd_register(vrt, *result);
      break;
    }
    case VPERM: {
      int vrt = instr->RTValue();
      int vra = instr->RAValue();
      int vrb = instr->RBValue();
      int vrc = instr->RCValue();
      int8_t temp[kSimd128Size] = {0};
      FOR_EACH_LANE(i, int8_t) {
        int8_t lane_num = get_simd_register_by_lane<int8_t>(vrc, i);
        // Get the five least significant bits.
        lane_num = (lane_num << 3) >> 3;
        int reg = vra;
        if (lane_num >= kSimd128Size) {
          lane_num = lane_num - kSimd128Size;
          reg = vrb;
        }
        temp[i] = get_simd_register_by_lane<int8_t>(reg, lane_num);
      }
      FOR_EACH_LANE(i, int8_t) {
        set_simd_register_by_lane<int8_t>(vrt, i, temp[i]);
      }
      break;
    }
    case VBPERMQ: {
      DECODE_VX_INSTRUCTION(t, a, b, T)
      uint16_t result_bits = 0;
      unsigned __int128 src_bits =
          base::bit_cast<__int128>(get_simd_register(a).int8);
      for (int i = 0; i < kSimd128Size; i++) {
        result_bits <<= 1;
        uint8_t selected_bit_index = get_simd_register_by_lane<uint8_t>(b, i);
        if (selected_bit_index < (kSimd128Size * kBitsPerByte)) {
          unsigned __int128 bit_value = (src_bits << selected_bit_index) >>
                                        (kSimd128Size * kBitsPerByte - 1);
          result_bits |= bit_value;
        }
      }
      set_simd_register_by_lane<uint64_t>(t, 0, 0);
      set_simd_register_by_lane<uint64_t>(t, 1, 0);
      set_simd_register_by_lane<uint16_t>(t, 3, result_bits);
      break;
    }
#define VECTOR_FP_QF(type, sign)                             \
  DECODE_VX_INSTRUCTION(t, a, b, T)                          \
  FOR_EACH_LANE(i, type) {                                   \
    type a_val = get_simd_register_by_lane<type>(a, i);      \
    type b_val = get_simd_register_by_lane<type>(b, i);      \
    type t_val = get_simd_register_by_lane<type>(t, i);      \
    type reuslt = sign * ((sign * b_val) + (a_val * t_val)); \
    if (isinf(a_val)) reuslt = a_val;                        \
    if (isinf(b_val)) reuslt = b_val;                        \
    if (isinf(t_val)) reuslt = t_val;                        \
    set_simd_register_by_lane<type>(t, i, reuslt);           \
  }
    case XVMADDMDP: {
      VECTOR_FP_QF(double, +1)
      break;
    }
    case XVNMSUBMDP: {
      VECTOR_FP_QF(double, -1)
      break;
    }
    case XVMADDMSP: {
      VECTOR_FP_QF(float, +1)
      break;
    }
    case XVNMSUBMSP: {
      VECTOR_FP_QF(float, -1)
      break;
    }
#undef VECTOR_FP_QF
    case VMHRADDSHS: {
      int vrt = instr->RTValue();
      int vra = instr->RAValue();
      int vrb = instr->RBValue();
      int vrc = instr->RCValue();
      FOR_EACH_LANE(i, int16_t) {
        int16_t vra_val = get_simd_register_by_lane<int16_t>(vra, i);
        int16_t vrb_val = get_simd_register_by_lane<int16_t>(vrb, i);
        int16_t vrc_val = get_simd_register_by_lane<int16_t>(vrc, i);
        int32_t temp = vra_val * vrb_val;
        temp = (temp + 0x00004000) >> 15;
        temp += vrc_val;
        if (temp > kMaxInt16)
          temp = kMaxInt16;
        else if (temp < kMinInt16)
          temp = kMinInt16;
        set_simd_register_by_lane<int16_t>(vrt, i, static_cast<int16_t>(temp));
      }
      break;
    }
    case VMSUMMBM: {
      int vrt = instr->RTValue();
      int vra = instr->RAValue();
      int vrb = instr->RBValue();
      int vrc = instr->RCValue();
      FOR_EACH_LANE(i, int32_t) {
        int8_t vra_1_val = get_simd_register_by_lane<int8_t>(vra, 4 * i),
               vra_2_val = get_simd_register_by_lane<int8_t>(vra, (4 * i) + 1),
               vra_3_val = get_simd_register_by_lane<int8_t>(vra, (4 * i) + 2),
               vra_4_val = get_simd_register_by_lane<int8_t>(vra, (4 * i) + 3);
        uint8_t vrb_1_val = get_simd_register_by_lane<uint8_t>(vrb, 4 * i),
                vrb_2_val =
                    get_simd_register_by_lane<uint8_t>(vrb, (4 * i) + 1),
                vrb_3_val =
                    get_simd_register_by_lane<uint8_t>(vrb, (4 * i) + 2),
                vrb_4_val =
                    get_simd_register_by_lane<uint8_t>(vrb, (4 * i) + 3);
        int32_t vrc_val = get_simd_register_by_lane<int32_t>(vrc, i);
        int32_t temp1 = vra_1_val * vrb_1_val, temp2 = vra_2_val * vrb_2_val,
                temp3 = vra_3_val * vrb_3_val, temp4 = vra_4_val * vrb_4_val;
        temp1 = temp1 + temp2 + temp3 + temp4 + vrc_val;
        set_simd_register_by_lane<int32_t>(vrt, i, temp1);
      }
      break;
    }
    case VMSUMSHM: {
      int vrt = instr->RTValue();
      int vra = instr->RAValue();
      int vrb = instr->RBValue();
      int vrc = instr->RCValue();
      FOR_EACH_LANE(i, int32_t) {
        int16_t vra_1_val = get_simd_register_by_lane<int16_t>(vra, 2 * i);
        int16_t vra_2_val =
            get_simd_register_by_lane<int16_t>(vra, (2 * i) + 1);
        int16_t vrb_1_val = get_simd_register_by_lane<int16_t>(vrb, 2 * i);
        int16_t vrb_2_val =
            get_simd_register_by_lane<int16_t>(vrb, (2 * i) + 1);
        int32_t vrc_val = get_simd_register_by_lane<int32_t>(vrc, i);
        int32_t temp1 = vra_1_val * vrb_1_val, temp2 = vra_2_val * vrb_2_val;
        temp1 = temp1 + temp2 + vrc_val;
        set_simd_register_by_lane<int32_t>(vrt, i, temp1);
      }
      break;
    }
    case VMLADDUHM: {
      int vrt = instr->RTValue();
      int vra = instr->RAValue();
      int vrb = instr->RBValue();
      int vrc = instr->RCValue();
      FOR_EACH_LANE(i, uint16_t) {
        uint16_t vra_val = get_simd_register_by_lane<uint16_t>(vra, i);
        uint16_t vrb_val = get_simd_register_by_lane<uint16_t>(vrb, i);
        uint16_t vrc_val = get_simd_register_by_lane<uint16_t>(vrc, i);
        set_simd_register_by_lane<uint16_t>(vrt, i,
                                            (vra_val * vrb_val) + vrc_val);
      }
      break;
    }
#define VECTOR_UNARY_OP(type, op)                         \
  int t = instr->RTValue();                               \
  int b = instr->RBValue();                               \
  FOR_EACH_LANE(i, type) {                                \
    set_simd_register_by_lane<type>(                      \
        t, i, op(get_simd_register_by_lane<type>(b, i))); \
  }
    case XVABSDP: {
      VECTOR_UNARY_OP(double, std::abs)
      break;
    }
    case XVNEGDP: {
      VECTOR_UNARY_OP(double, -)
      break;
    }
    case XVSQRTDP: {
      VECTOR_UNARY_OP(double, std::sqrt)
      break;
    }
    case XVABSSP: {
      VECTOR_UNARY_OP(float, std::abs)
      break;
    }
    case XVNEGSP: {
      VECTOR_UNARY_OP(float, -)
      break;
    }
    case XVSQRTSP: {
      VECTOR_UNARY_OP(float, std::sqrt)
      break;
    }
    case XVRESP: {
      VECTOR_UNARY_OP(float, base::Recip)
      break;
    }
    case XVRSQRTESP: {
      VECTOR_UNARY_OP(float, base::RecipSqrt)
      break;
    }
    case VNEGW: {
      VECTOR_UNARY_OP(int32_t, -)
      break;
    }
    case VNEGD: {
      VECTOR_UNARY_OP(int64_t, -)
      break;
    }
#undef VECTOR_UNARY_OP
#define VECTOR_ROUNDING_AVERAGE(intermediate_type, result_type)              \
  DECODE_VX_INSTRUCTION(t, a, b, T)                                          \
  FOR_EACH_LANE(i, result_type) {                                            \
    intermediate_type a_val = static_cast<intermediate_type>(                \
        get_simd_register_by_lane<result_type>(a, i));                       \
    intermediate_type b_val = static_cast<intermediate_type>(                \
        get_simd_register_by_lane<result_type>(b, i));                       \
    intermediate_type t_val = ((a_val + b_val) + 1) >> 1;                    \
    set_simd_register_by_lane<result_type>(t, i,                             \
                                           static_cast<result_type>(t_val)); \
  }
    case VAVGUH: {
      VECTOR_ROUNDING_AVERAGE(uint32_t, uint16_t)
      break;
    }
    case VAVGUB: {
      VECTOR_ROUNDING_AVERAGE(uint16_t, uint8_t)
      break;
    }
#undef VECTOR_ROUNDING_AVERAGE
    case VPOPCNTB: {
      int t = instr->RTValue();
      int b = instr->RBValue();
      FOR_EACH_LANE(i, uint8_t) {
        set_simd_register_by_lane<uint8_t>(
            t, i,
            base::bits::CountPopulation(
                get_simd_register_by_lane<uint8_t>(b, i)));
      }
      break;
    }
#define EXTRACT_MASK(type)                                           \
  int rt = instr->RTValue();                                         \
  int vrb = instr->RBValue();                                        \
  uint64_t result = 0;                                               \
  FOR_EACH_LANE(i, type) {                                           \
    if (i > 0) result <<= 1;                                         \
    result |= std::signbit(get_simd_register_by_lane<type>(vrb, i)); \
  }                                                                  \
  set_register(rt, result);
    case VEXTRACTDM: {
      EXTRACT_MASK(int64_t)
      break;
    }
    case VEXTRACTWM: {
      EXTRACT_MASK(int32_t)
      break;
    }
    case VEXTRACTHM: {
      EXTRACT_MASK(int16_t)
      break;
    }
    case VEXTRACTBM: {
      EXTRACT_MASK(int8_t)
      break;
    }
#undef EXTRACT_MASK
#undef FOR_EACH_LANE
#undef DECODE_VX_INSTRUCTION
#undef GET_ADDRESS
    default: {
      UNIMPLEMENTED();
    }
  }
}

void Simulator::Trace(Instruction* instr) {
  disasm::NameConverter converter;
  disasm::Disassembler dasm(converter);
  // use a reasonably large buffer
  v8::base::EmbeddedVector<char, 256> buffer;
  dasm.InstructionDecode(buffer, reinterpret_cast<uint8_t*>(instr));
  PrintF("%05d  %08" V8PRIxPTR "  %s\n", icount_,
         reinterpret_cast<intptr_t>(instr), buffer.begin());
}

// Executes the current instruction.
void Simulator::ExecuteInstruction(Instruction* instr) {
  if (v8_flags.check_icache) {
    CheckICache(i_cache(), instr);
  }
  pc_modified_ = false;
  if (v8_flags.trace_sim) {
    Trace(instr);
  }
  uint32_t opcode = instr->OpcodeField();
  if (opcode == TWI) {
    SoftwareInterrupt(instr);
  } else {
    ExecuteGeneric(instr);
  }
  if (!pc_modified_) {
    set_pc(reinterpret_cast<intptr_t>(instr) + kInstrSize);
  }
}

void Simulator::Execute() {
  // Get the PC to simulate. Cannot use the accessor here as we need the
  // raw PC value and not the one used as input to arithmetic instructions.
  intptr_t program_counter = get_pc();

  if (v8_flags.stop_sim_at == 0) {
    // Fast version of the dispatch loop without checking whether the simulator
    // should be stopping at a particular executed instruction.
    while (program_counter != end_sim_pc) {
      Instruction* instr = reinterpret_cast<Instruction*>(program_counter);
      icount_++;
      ExecuteInstruction(instr);
      program_counter = get_pc();
    }
  } else {
    // v8_flags.stop_sim_at is at the non-default value. Stop in the debugger
    // when we reach the particular instruction count.
    while (program_counter != end_sim_pc) {
      Instruction* instr = reinterpret_cast<Instruction*>(program_counter);
      icount_++;
      if (icount_ == v8_flags.stop_sim_at) {
        PPCDebugger dbg(this);
        dbg.Debug();
      } else {
        ExecuteInstruction(instr);
      }
      program_counter = get_pc();
    }
  }
}

void Simulator::CallInternal(Address entry) {
  // Adjust JS-based stack limit to C-based stack limit.
  isolate_->stack_guard()->AdjustStackLimitForSimulator();

  // Prepare to execute the code at entry
  if (ABI_USES_FUNCTION_DESCRIPTORS) {
    // entry is the function descriptor
    set_pc(*(reinterpret_cast<intptr_t*>(entry)));
  } else {
    // entry is the instruction address
    set_pc(static_cast<intptr_t>(entry));
  }

  if (ABI_CALL_VIA_IP) {
    // Put target address in ip (for JS prologue).
    set_register(r12, get_pc());
  }

  // Put down marker for end of simulation. The simulator will stop simulation
  // when the PC reaches this value. By saving the "end simulation" value into
  // the LR the simulation stops when returning to this call point.
  special_reg_lr_ = end_sim_pc;

  // Remember the values of non-volatile registers.
  intptr_t r2_val = get_register(r2);
  intptr_t r13_val = get_register(r13);
  intptr_t r14_val = get_register(r14);
  intptr_t r15_val = get_register(r15);
  intptr_t r16_val = get_register(r16);
  intptr_t r17_val = get_register(r17);
  intptr_t r18_val = get_register(r18);
  intptr_t r19_val = get_register(r19);
  intptr_t r20_val = get_register(r20);
  intptr_t r21_val = get_register(r21);
  intptr_t r22_val = get_register(r22);
  intptr_t r23_val = get_register(r23);
  intptr_t r24_val = get_register(r24);
  intptr_t r25_val = get_register(r25);
  intptr_t r26_val = get_register(r26);
  intptr_t r27_val = get_register(r27);
  intptr_t r28_val = get_register(r28);
  intptr_t r29_val = get_register(r29);
  intptr_t r30_val = get_register(r30);
  intptr_t r31_val = get_register(fp);

  // Set up the non-volatile registers with a known value. To be able to check
  // that they are preserved properly across JS execution.
  intptr_t callee_saved_value = icount_;
  set_register(r2, callee_saved_value);
  set_register(r13, callee_saved_value);
  set_register(r14, callee_saved_value);
  set_register(r15, callee_saved_value);
  set_register(r16, callee_saved_value);
  set_register(r17, callee_saved_value);
  set_register(r18, callee_saved_value);
  set_register(r19, callee_saved_value);
  set_register(r20, callee_saved_value);
  set_register(r21, callee_saved_value);
  set_register(r22, callee_saved_value);
  set_register(r23, callee_saved_value);
  set_register(r24, callee_saved_value);
  set_register(r25, callee_saved_value);
  set_register(r26, callee_saved_value);
  set_register(r27, callee_saved_value);
  set_register(r28, callee_saved_value);
  set_register(r29, callee_saved_value);
  set_register(r30, callee_saved_value);
  set_register(fp, callee_saved_value);

  // Start the simulation
  Execute();

  // Check that the non-volatile registers have been preserved.
  if (ABI_TOC_REGISTER != 2) {
    CHECK_EQ(callee_saved_value, get_register(r2));
  }
  if (ABI_TOC_REGISTER != 13) {
    CHECK_EQ(callee_saved_value, get_register(r13));
  }
  CHECK_EQ(callee_saved_value, get_register(r14));
  CHECK_EQ(callee_saved_value, get_register(r15));
  CHECK_EQ(callee_saved_value, get_register(r16));
  CHECK_EQ(callee_saved_value, get_register(r17));
  CHECK_EQ(callee_saved_value, get_register(r18));
  CHECK_EQ(callee_saved_value, get_register(r19));
  CHECK_EQ(callee_saved_value, get_register(r20));
  CHECK_EQ(callee_saved_value, get_register(r21));
  CHECK_EQ(callee_saved_value, get_register(r22));
  CHECK_EQ(callee_saved_value, get_register(r23));
  CHECK_EQ(callee_saved_value, get_register(r24));
  CHECK_EQ(callee_saved_value, get_register(r25));
  CHECK_EQ(callee_saved_value, get_register(r26));
  CHECK_EQ(callee_saved_value, get_register(r27));
  CHECK_EQ(callee_saved_value, get_register(r28));
  CHECK_EQ(callee_saved_value, get_register(r29));
  CHECK_EQ(callee_saved_value, get_register(r30));
  CHECK_EQ(callee_saved_value, get_register(fp));

  // Restore non-volatile registers with the original value.
  set_register(r2, r2_val);
  set_register(r13, r13_val);
  set_register(r14, r14_val);
  set_register(r15, r15_val);
  set_register(r16, r16_val);
  set_register(r17, r17_val);
  set_register(r18, r18_val);
  set_register(r19, r19_val);
  set_register(r20, r20_val);
  set_register(r21, r21_val);
  set_register(r22, r22_val);
  set_register(r23, r23_val);
  set_register(r24, r24_val);
  set_register(r25, r25_val);
  set_register(r26, r26_val);
  set_register(r27, r27_val);
  set_register(r28, r28_val);
  set_register(r29, r29_val);
  set_register(r30, r30_val);
  set_register(fp, r31_val);
}

intptr_t Simulator::CallImpl(Address entry, int argument_count,
                             const intptr_t* arguments) {
  // Set up arguments

  // First eight arguments passed in registers r3-r10.
  int reg_arg_count = std::min(8, argument_count);
  int stack_arg_count = argument_count - reg_arg_count;
  for (int i = 0; i < reg_arg_count; i++) {
    set_register(i + 3, arguments[i]);
  }

  // Remaining arguments passed on stack.
  intptr_t original_stack = get_register(sp);
  // Compute position of stack on entry to generated code.
  intptr_t entry_stack =
      (original_stack -
       (kNumRequiredStackFrameSlots + stack_arg_count) * sizeof(intptr_t));
  if (base::OS::ActivationFrameAlignment() != 0) {
    entry_stack &= -base::OS::ActivationFrameAlignment();
  }
  // Store remaining arguments on stack, from low to high memory.
  // +2 is a hack for the LR slot + old SP on PPC
  intptr_t* stack_argument =
      reinterpret_cast<intptr_t*>(entry_stack) + kStackFrameExtraParamSlot;
  memcpy(stack_argument, arguments + reg_arg_count,
         stack_arg_count * sizeof(*arguments));
  set_register(sp, entry_stack);

  CallInternal(entry);

  // Pop stack passed arguments.
  CHECK_EQ(entry_stack, get_register(sp));
  set_register(sp, original_stack);

  return get_register(r3);
}

void Simulator::CallFP(Address entry, double d0, double d1) {
  set_d_register_from_double(1, d0);
  set_d_register_from_double(2, d1);
  CallInternal(entry);
}

int32_t Simulator::CallFPReturnsInt(Address entry, double d0, double d1) {
  CallFP(entry, d0, d1);
  int32_t result = get_register(r3);
  return result;
}

double Simulator::CallFPReturnsDouble(Address entry, double d0, double d1) {
  CallFP(entry, d0, d1);
  return get_double_from_d_register(1);
}

uintptr_t Simulator::PushAddress(uintptr_t address) {
  uintptr_t new_sp = get_register(sp) - sizeof(uintptr_t);
  uintptr_t* stack_slot = reinterpret_cast<uintptr_t*>(new_sp);
  *stack_slot = address;
  set_register(sp, new_sp);
  return new_sp;
}

uintptr_t Simulator::PopAddress() {
  uintptr_t current_sp = get_register(sp);
  uintptr_t* stack_slot = reinterpret_cast<uintptr_t*>(current_sp);
  uintptr_t address = *stack_slot;
  set_register(sp, current_sp + sizeof(uintptr_t));
  return address;
}

void Simulator::GlobalMonitor::Clear() {
  access_state_ = MonitorAccess::Open;
  tagged_addr_ = 0;
  size_ = TransactionSize::None;
  thread_id_ = ThreadId::Invalid();
}

void Simulator::GlobalMonitor::NotifyLoadExcl(uintptr_t addr,
                                              TransactionSize size,
                                              ThreadId thread_id) {
  // TODO(s390): By using Global Monitors, we are effectively limiting one
  // active reservation across all processors. This would potentially serialize
  // parallel threads executing load&reserve + store conditional on unrelated
  // memory. Technically, this implementation would still make the simulator
  // adhere to the spec, but seems overly heavy-handed.
  access_state_ = MonitorAccess::Exclusive;
  tagged_addr_ = addr;
  size_ = size;
  thread_id_ = thread_id;
}

void Simulator::GlobalMonitor::NotifyStore(uintptr_t addr, TransactionSize size,
                                           ThreadId thread_id) {
  if (access_state_ == MonitorAccess::Exclusive) {
    // Calculate if the transaction has been overlapped
    uintptr_t transaction_start = addr;
    uintptr_t transaction_end = addr + static_cast<uintptr_t>(size);
    uintptr_t exclusive_transaction_start = tagged_addr_;
    uintptr_t exclusive_transaction_end =
        tagged_addr_ + static_cast<uintptr_t>(size_);
    bool is_not_overlapped = transaction_end < exclusive_transaction_start ||
                             exclusive_transaction_end < transaction_start;
    if (!is_not_overlapped && thread_id_ != thread_id) {
      Clear();
    }
  }
}

bool Simulator::GlobalMonitor::NotifyStoreExcl(uintptr_t addr,
                                               TransactionSize size,
                                               ThreadId thread_id) {
  bool permission = access_state_ == MonitorAccess::Exclusive &&
                    addr == tagged_addr_ && size_ == size &&
                    thread_id_ == thread_id;
  // The reservation is cleared if the processor holding the reservation
  // executes a store conditional instruction to any address.
  Clear();
  return permission;
}

}  // namespace internal
}  // namespace v8

#undef SScanF
#endif  // USE_SIMULATOR
