// Copyright 2012 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/execution/arm/simulator-arm.h"

#include "src/base/logging.h"

#if defined(USE_SIMULATOR)

#include <stdarg.h>
#include <stdlib.h>

#include <cmath>

#include "src/base/bits.h"
#include "src/base/lazy-instance.h"
#include "src/base/memory.h"
#include "src/base/overflowing-math.h"
#include "src/base/platform/memory.h"
#include "src/base/platform/platform.h"
#include "src/base/vector.h"
#include "src/codegen/arm/constants-arm.h"
#include "src/codegen/assembler-inl.h"
#include "src/codegen/macro-assembler.h"
#include "src/diagnostics/disasm.h"
#include "src/heap/combined-heap.h"
#include "src/heap/heap-inl.h"  // For CodeSpaceMemoryModificationScope.
#include "src/objects/objects-inl.h"
#include "src/runtime/runtime-utils.h"
#include "src/utils/ostreams.h"
#include "src/utils/utils.h"

// Only build the simulator if not compiling for real ARM hardware.
namespace v8 {
namespace internal {

DEFINE_LAZY_LEAKY_OBJECT_GETTER(Simulator::GlobalMonitor,
                                Simulator::GlobalMonitor::Get)

// This macro provides a platform independent use of sscanf. The reason for
// SScanF not being implemented in a platform independent way through
// ::v8::internal::OS in the same way as SNPrintF is that the
// Windows C Run-Time Library does not provide vsscanf.
#define SScanF sscanf

// The ArmDebugger class is used by the simulator while debugging simulated ARM
// code.
class ArmDebugger {
 public:
  explicit ArmDebugger(Simulator* sim) : sim_(sim) {}
  void Debug();
  bool ExecDebugCommand(ArrayUniquePtr<char> line_ptr);

 private:
  static const Instr kBreakpointInstr =
      (al | (7 * B25) | (1 * B24) | kBreakpoint);
  static const Instr kNopInstr = (al | (13 * B21));

  Simulator* sim_;

  int32_t GetRegisterValue(int regnum);
  double GetRegisterPairDoubleValue(int regnum);
  double GetVFPDoubleRegisterValue(int regnum);
  bool GetValue(const char* desc, int32_t* value);
  bool GetVFPSingleValue(const char* desc, float* value);
  bool GetVFPDoubleValue(const char* desc, double* value);

  // Set or delete breakpoint (there can be only one).
  bool SetBreakpoint(Instruction* breakpc);
  void DeleteBreakpoint();

  // Undo and redo the breakpoint. This is needed to bracket disassembly and
  // execution to skip past the breakpoint when run from the debugger.
  void UndoBreakpoint();
  void RedoBreakpoint();
};

void Simulator::DebugAtNextPC() {
  PrintF("Starting debugger on the next instruction:\n");
  set_pc(get_pc() + kInstrSize);
  ArmDebugger(this).Debug();
}

void Simulator::AdvancedSIMDElementOrStructureLoadStoreWriteback(int Rn, int Rm,
                                                                 int ebytes) {
  if (Rm != 15) {
    if (Rm == 13) {
      set_register(Rn, get_register(Rn) + ebytes);
    } else {
      set_register(Rn, get_register(Rn) + get_register(Rm));
    }
  }
}

int32_t ArmDebugger::GetRegisterValue(int regnum) {
  if (regnum == kPCRegister) {
    return sim_->get_pc();
  } else {
    return sim_->get_register(regnum);
  }
}

double ArmDebugger::GetRegisterPairDoubleValue(int regnum) {
  return sim_->get_double_from_register_pair(regnum);
}

double ArmDebugger::GetVFPDoubleRegisterValue(int regnum) {
  return sim_->get_double_from_d_register(regnum).get_scalar();
}

bool ArmDebugger::GetValue(const char* desc, int32_t* value) {
  int regnum = Registers::Number(desc);
  if (regnum != kNoRegister) {
    *value = GetRegisterValue(regnum);
    return true;
  }
  if (strncmp(desc, "0x", 2) == 0)
    return SScanF(desc + 2, "%x", reinterpret_cast<uint32_t*>(value)) == 1;
  return SScanF(desc, "%u", reinterpret_cast<uint32_t*>(value)) == 1;
}

bool ArmDebugger::GetVFPSingleValue(const char* desc, float* value) {
  bool is_double;
  int regnum = VFPRegisters::Number(desc, &is_double);
  if (regnum != kNoRegister && !is_double) {
    *value = sim_->get_float_from_s_register(regnum).get_scalar();
    return true;
  }
  return false;
}

bool ArmDebugger::GetVFPDoubleValue(const char* desc, double* value) {
  bool is_double;
  int regnum = VFPRegisters::Number(desc, &is_double);
  if (regnum != kNoRegister && is_double) {
    *value = sim_->get_double_from_d_register(regnum).get_scalar();
    return true;
  }
  return false;
}

bool ArmDebugger::SetBreakpoint(Instruction* breakpc) {
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

void ArmDebugger::DeleteBreakpoint() {
  UndoBreakpoint();
  sim_->break_pc_ = nullptr;
  sim_->break_instr_ = 0;
}

void ArmDebugger::UndoBreakpoint() {
  if (sim_->break_pc_ != nullptr) {
    SetInstructionBitsInCodeSpace(sim_->break_pc_, sim_->break_instr_,
                                  sim_->isolate_->heap());
  }
}

void ArmDebugger::RedoBreakpoint() {
  if (sim_->break_pc_ != nullptr) {
    SetInstructionBitsInCodeSpace(sim_->break_pc_, kBreakpointInstr,
                                  sim_->isolate_->heap());
  }
}

void ArmDebugger::Debug() {
  if (v8_flags.correctness_fuzzer_suppressions) {
    PrintF("Debugger disabled for differential fuzzing.\n");
    return;
  }
  intptr_t last_pc = -1;
  bool done = false;

  // Unset breakpoint while running in the debugger shell, making it invisible
  // to all commands.
  UndoBreakpoint();

  while (!done && !sim_->has_bad_pc()) {
    if (last_pc != sim_->get_pc()) {
      disasm::NameConverter converter;
      disasm::Disassembler dasm(converter);
      // use a reasonably large buffer
      v8::base::EmbeddedVector<char, 256> buffer;
      dasm.InstructionDecode(buffer,
                             reinterpret_cast<uint8_t*>(sim_->get_pc()));
      PrintF("  0x%08x  %s\n", sim_->get_pc(), buffer.begin());
      last_pc = sim_->get_pc();
    }
    ArrayUniquePtr<char> line(ReadLine("sim> "));

    done = ExecDebugCommand(std::move(line));
  }

  // Reinstall breakpoint to stop execution and enter the debugger shell when
  // hit.
  RedoBreakpoint();
}

bool ArmDebugger::ExecDebugCommand(ArrayUniquePtr<char> line_ptr) {
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

  if (line_ptr == nullptr) return true;

  // Repeat last command by default.
  const char* line = line_ptr.get();
  const char* last_input = sim_->last_debugger_input();
  if (strcmp(line, "\n") == 0 && (last_input != nullptr)) {
    line_ptr.reset();
    line = last_input;
  } else {
    // Update the latest command ran
    sim_->set_last_debugger_input(std::move(line_ptr));
  }

  // Use sscanf to parse the individual parts of the command line. At the
  // moment no command expects more than two parameters.
  int argc = SScanF(line,
                      "%" XSTR(COMMAND_SIZE) "s "
                      "%" XSTR(ARG_SIZE) "s "
                      "%" XSTR(ARG_SIZE) "s",
                      cmd, arg1, arg2);
  if ((strcmp(cmd, "si") == 0) || (strcmp(cmd, "stepi") == 0)) {
    sim_->InstructionDecode(reinterpret_cast<Instruction*>(sim_->get_pc()));
  } else if ((strcmp(cmd, "c") == 0) || (strcmp(cmd, "cont") == 0)) {
    // Execute the one instruction we broke at with breakpoints disabled.
    sim_->InstructionDecode(reinterpret_cast<Instruction*>(sim_->get_pc()));
    // Leave the debugger shell.
    return true;
  } else if ((strcmp(cmd, "p") == 0) || (strcmp(cmd, "print") == 0)) {
    if (argc == 2 || (argc == 3 && strcmp(arg2, "fp") == 0)) {
      int32_t value;
      float svalue;
      double dvalue;
      if (strcmp(arg1, "all") == 0) {
        for (int i = 0; i < kNumRegisters; i++) {
          value = GetRegisterValue(i);
          PrintF("%3s: 0x%08x %10d", RegisterName(Register::from_code(i)),
                 value, value);
          if ((argc == 3 && strcmp(arg2, "fp") == 0) && i < 8 && (i % 2) == 0) {
            dvalue = GetRegisterPairDoubleValue(i);
            PrintF(" (%f)\n", dvalue);
          } else {
            PrintF("\n");
          }
        }
        for (int i = 0; i < DwVfpRegister::SupportedRegisterCount(); i++) {
          dvalue = GetVFPDoubleRegisterValue(i);
          uint64_t as_words = base::bit_cast<uint64_t>(dvalue);
          PrintF("%3s: %f 0x%08x %08x\n", VFPRegisters::Name(i, true), dvalue,
                 static_cast<uint32_t>(as_words >> 32),
                 static_cast<uint32_t>(as_words & 0xFFFFFFFF));
        }
      } else {
        if (GetValue(arg1, &value)) {
          PrintF("%s: 0x%08x %d \n", arg1, value, value);
        } else if (GetVFPSingleValue(arg1, &svalue)) {
          uint32_t as_word = base::bit_cast<uint32_t>(svalue);
          PrintF("%s: %f 0x%08x\n", arg1, svalue, as_word);
        } else if (GetVFPDoubleValue(arg1, &dvalue)) {
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
  } else if ((strcmp(cmd, "po") == 0) || (strcmp(cmd, "printobject") == 0)) {
    if (argc == 2) {
      int32_t value;
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
    int32_t* cur = nullptr;
    int32_t* end = nullptr;
    int next_arg = 1;

    if (strcmp(cmd, "stack") == 0) {
      cur = reinterpret_cast<int32_t*>(sim_->get_register(Simulator::sp));
    } else {  // "mem"
      int32_t value;
      if (!GetValue(arg1, &value)) {
        PrintF("%s unrecognized\n", arg1);
        return false;
      }
      cur = reinterpret_cast<int32_t*>(value);
      next_arg++;
    }

    int32_t words;
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
      PrintF("  0x%08" V8PRIxPTR ":  0x%08x %10d",
             reinterpret_cast<intptr_t>(cur), *cur, *cur);
      Tagged<Object> obj(*cur);
      Heap* current_heap = sim_->isolate_->heap();
      if (!skip_obj_print) {
        if (IsSmi(obj) ||
            IsValidHeapObject(current_heap, Cast<HeapObject>(obj))) {
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
        int32_t value;
        if (GetValue(arg1, &value)) {
          cur = reinterpret_cast<uint8_t*>(value);
          // Disassemble 10 instructions at <arg1>.
          end = cur + (10 * kInstrSize);
        }
      } else {
        // The argument is the number of instructions.
        int32_t value;
        if (GetValue(arg1, &value)) {
          cur = reinterpret_cast<uint8_t*>(sim_->get_pc());
          // Disassemble <arg1> instructions.
          end = cur + (value * kInstrSize);
        }
      }
    } else {
      int32_t value1;
      int32_t value2;
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
      int32_t value;
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
  } else if (strcmp(cmd, "backtrace") == 0 || strcmp(cmd, "bt") == 0) {
    int32_t pc = sim_->get_pc();
    int32_t lr = sim_->get_register(Simulator::lr);
    int32_t sp = sim_->get_register(Simulator::sp);
    int32_t fp = sim_->get_register(Simulator::fp);

    int i = 0;
    while (true) {
      PrintF("#%d: 0x%08x (sp=0x%08x, fp=0x%08x)\n", i, pc, sp, fp);
      pc = lr;
      sp = fp;
      if (pc == Simulator::end_sim_pc) {
        break;
      }
      lr = *(reinterpret_cast<int32_t*>(fp) + 1);
      fp = *reinterpret_cast<int32_t*>(fp);
      i++;
      if (i > 100) {
        PrintF("Too many frames\n");
        break;
      }
    }
  } else if (strcmp(cmd, "del") == 0) {
    DeleteBreakpoint();
  } else if (strcmp(cmd, "flags") == 0) {
    PrintF("N flag: %d; ", sim_->n_flag_);
    PrintF("Z flag: %d; ", sim_->z_flag_);
    PrintF("C flag: %d; ", sim_->c_flag_);
    PrintF("V flag: %d\n", sim_->v_flag_);
    PrintF("INVALID OP flag: %d; ", sim_->inv_op_vfp_flag_);
    PrintF("DIV BY ZERO flag: %d; ", sim_->div_zero_vfp_flag_);
    PrintF("OVERFLOW flag: %d; ", sim_->overflow_vfp_flag_);
    PrintF("UNDERFLOW flag: %d; ", sim_->underflow_vfp_flag_);
    PrintF("INEXACT flag: %d;\n", sim_->inexact_vfp_flag_);
  } else if (strcmp(cmd, "stop") == 0) {
    int32_t value;
    intptr_t stop_pc = sim_->get_pc() - kInstrSize;
    Instruction* stop_instr = reinterpret_cast<Instruction*>(stop_pc);
    if ((argc == 2) && (strcmp(arg1, "unstop") == 0)) {
      // Remove the current stop.
      if (stop_instr->IsStop()) {
        SetInstructionBitsInCodeSpace(stop_instr, kNopInstr,
                                      sim_->isolate_->heap());
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
    sim_->ToggleInstructionTracing();
    PrintF("Trace of executed instructions is %s\n",
           sim_->InstructionTracingEnabled() ? "on" : "off");
  } else if ((strcmp(cmd, "h") == 0) || (strcmp(cmd, "help") == 0)) {
    PrintF("cont\n");
    PrintF("  continue execution (alias 'c')\n");
    PrintF("stepi\n");
    PrintF("  step one instruction (alias 'si')\n");
    PrintF("print <register>\n");
    PrintF("  print register content (alias 'p')\n");
    PrintF("  use register name 'all' to print all registers\n");
    PrintF("  add argument 'fp' to print register pair double values\n");
    PrintF("printobject <register>\n");
    PrintF("  print an object from a register (alias 'po')\n");
    PrintF("flags\n");
    PrintF("  print flags\n");
    PrintF("stack [<words>]\n");
    PrintF("  dump stack content, default dump 10 words)\n");
    PrintF("mem <address> [<words>]\n");
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
    PrintF("backtrace / bt\n");
    PrintF("  Walk the frame pointers, dumping the pc/sp/fp for each frame.\n");
    PrintF("del\n");
    PrintF("  delete the breakpoint\n");
    PrintF("trace (alias 't')\n");
    PrintF("  toogle the tracing of all executed statements\n");
    PrintF("stop feature:\n");
    PrintF("  Description:\n");
    PrintF("    Stops are debug instructions inserted by\n");
    PrintF("    the Assembler::stop() function.\n");
    PrintF("    When hitting a stop, the Simulator will\n");
    PrintF("    stop and give control to the ArmDebugger.\n");
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
  return false;

#undef COMMAND_SIZE
#undef ARG_SIZE

#undef STR
#undef XSTR
}

bool Simulator::InstructionTracingEnabled() { return instruction_tracing_; }

void Simulator::ToggleInstructionTracing() {
  instruction_tracing_ = !instruction_tracing_;
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

void Simulator::SetRedirectInstruction(Instruction* instruction) {
  instruction->SetInstructionBits(al | (0xF * B24) | kCallRtRedirected);
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
    DCHECK_EQ(0, start & CachePage::kPageMask);
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
  stack_ = reinterpret_cast<uint8_t*>(base::Malloc(kAllocatedStackSize));
  pc_modified_ = false;
  icount_ = 0;
  break_pc_ = nullptr;
  break_instr_ = 0;

  // Set up architecture state.
  // All registers are initialized to zero to start with.
  for (int i = 0; i < num_registers; i++) {
    registers_[i] = 0;
  }
  n_flag_ = false;
  z_flag_ = false;
  c_flag_ = false;
  v_flag_ = false;

  // Initializing VFP registers.
  // All registers are initialized to zero to start with
  // even though s_registers_ & d_registers_ share the same
  // physical registers in the target.
  for (int i = 0; i < num_d_registers * 2; i++) {
    vfp_registers_[i] = 0;
  }
  n_flag_FPSCR_ = false;
  z_flag_FPSCR_ = false;
  c_flag_FPSCR_ = false;
  v_flag_FPSCR_ = false;
  FPSCR_rounding_mode_ = RN;
  FPSCR_default_NaN_mode_ = false;

  inv_op_vfp_flag_ = false;
  div_zero_vfp_flag_ = false;
  overflow_vfp_flag_ = false;
  underflow_vfp_flag_ = false;
  inexact_vfp_flag_ = false;

  // The sp is initialized to point to the bottom (high address) of the
  // usable stack area.
  registers_[sp] = reinterpret_cast<int32_t>(stack_) + kUsableStackSize;
  // The lr and pc are initialized to a known bad value that will cause an
  // access violation if the simulator ever tries to execute it.
  registers_[pc] = bad_lr;
  registers_[lr] = bad_lr;

  last_debugger_input_ = nullptr;
}

Simulator::~Simulator() {
  GlobalMonitor::Get()->RemoveProcessor(&global_monitor_processor_);
  base::Free(stack_);
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
void Simulator::set_register(int reg, int32_t value) {
  DCHECK((reg >= 0) && (reg < num_registers));
  if (reg == pc) {
    pc_modified_ = true;
  }
  registers_[reg] = value;
}

// Get the register from the architecture state. This function does handle
// the special case of accessing the PC register.
int32_t Simulator::get_register(int reg) const {
  DCHECK((reg >= 0) && (reg < num_registers));
  // Stupid code added to avoid bug in GCC.
  // See: http://gcc.gnu.org/bugzilla/show_bug.cgi?id=43949
  if (reg >= num_registers) return 0;
  // End stupid code.
  return registers_[reg] + ((reg == pc) ? Instruction::kPcLoadDelta : 0);
}

double Simulator::get_double_from_register_pair(int reg) {
  DCHECK((reg >= 0) && (reg < num_registers) && ((reg % 2) == 0));

  double dm_val = 0.0;
  // Read the bits from the unsigned integer register_[] array
  // into the double precision floating point value and return it.
  char buffer[2 * sizeof(vfp_registers_[0])];
  memcpy(buffer, &registers_[reg], 2 * sizeof(registers_[0]));
  memcpy(&dm_val, buffer, 2 * sizeof(registers_[0]));
  return (dm_val);
}

void Simulator::set_register_pair_from_double(int reg, double* value) {
  DCHECK((reg >= 0) && (reg < num_registers) && ((reg % 2) == 0));
  memcpy(registers_ + reg, value, sizeof(*value));
}

void Simulator::set_dw_register(int dreg, const int* dbl) {
  DCHECK((dreg >= 0) && (dreg < num_d_registers));
  registers_[dreg] = dbl[0];
  registers_[dreg + 1] = dbl[1];
}

void Simulator::get_d_register(int dreg, uint64_t* value) {
  DCHECK((dreg >= 0) && (dreg < DwVfpRegister::SupportedRegisterCount()));
  memcpy(value, vfp_registers_ + dreg * 2, sizeof(*value));
}

void Simulator::set_d_register(int dreg, const uint64_t* value) {
  DCHECK((dreg >= 0) && (dreg < DwVfpRegister::SupportedRegisterCount()));
  memcpy(vfp_registers_ + dreg * 2, value, sizeof(*value));
}

void Simulator::get_d_register(int dreg, uint32_t* value) {
  DCHECK((dreg >= 0) && (dreg < DwVfpRegister::SupportedRegisterCount()));
  memcpy(value, vfp_registers_ + dreg * 2, sizeof(*value) * 2);
}

void Simulator::set_d_register(int dreg, const uint32_t* value) {
  DCHECK((dreg >= 0) && (dreg < DwVfpRegister::SupportedRegisterCount()));
  memcpy(vfp_registers_ + dreg * 2, value, sizeof(*value) * 2);
}

template <typename T, int SIZE>
void Simulator::get_neon_register(int reg, T (&value)[SIZE / sizeof(T)]) {
  DCHECK(SIZE == kSimd128Size || SIZE == kDoubleSize);
  DCHECK_LE(0, reg);
  DCHECK_GT(SIZE == kSimd128Size ? num_q_registers : num_d_registers, reg);
  memcpy(value, vfp_registers_ + reg * (SIZE / 4), SIZE);
}

template <typename T, int SIZE>
void Simulator::set_neon_register(int reg, const T (&value)[SIZE / sizeof(T)]) {
  DCHECK(SIZE == kSimd128Size || SIZE == kDoubleSize);
  DCHECK_LE(0, reg);
  DCHECK_GT(SIZE == kSimd128Size ? num_q_registers : num_d_registers, reg);
  memcpy(vfp_registers_ + reg * (SIZE / 4), value, SIZE);
}

// Raw access to the PC register.
void Simulator::set_pc(int32_t value) {
  pc_modified_ = true;
  registers_[pc] = value;
}

bool Simulator::has_bad_pc() const {
  return ((registers_[pc] == bad_lr) || (registers_[pc] == end_sim_pc));
}

// Raw access to the PC register without the special adjustment when reading.
int32_t Simulator::get_pc() const { return registers_[pc]; }

// Getting from and setting into VFP registers.
void Simulator::set_s_register(int sreg, unsigned int value) {
  DCHECK((sreg >= 0) && (sreg < num_s_registers));
  vfp_registers_[sreg] = value;
}

unsigned int Simulator::get_s_register(int sreg) const {
  DCHECK((sreg >= 0) && (sreg < num_s_registers));
  return vfp_registers_[sreg];
}

template <class InputType, int register_size>
void Simulator::SetVFPRegister(int reg_index, const InputType& value) {
  unsigned bytes = register_size * sizeof(vfp_registers_[0]);
  DCHECK_EQ(sizeof(InputType), bytes);
  DCHECK_GE(reg_index, 0);
  if (register_size == 1) DCHECK(reg_index < num_s_registers);
  if (register_size == 2)
    DCHECK(reg_index < DwVfpRegister::SupportedRegisterCount());

  memcpy(&vfp_registers_[reg_index * register_size], &value, bytes);
}

template <class ReturnType, int register_size>
ReturnType Simulator::GetFromVFPRegister(int reg_index) {
  unsigned bytes = register_size * sizeof(vfp_registers_[0]);
  DCHECK_EQ(sizeof(ReturnType), bytes);
  DCHECK_GE(reg_index, 0);
  if (register_size == 1) DCHECK(reg_index < num_s_registers);
  if (register_size == 2)
    DCHECK(reg_index < DwVfpRegister::SupportedRegisterCount());

  ReturnType value;
  memcpy(&value, &vfp_registers_[register_size * reg_index], bytes);
  return value;
}

void Simulator::SetSpecialRegister(SRegisterFieldMask reg_and_mask,
                                   uint32_t value) {
  // Only CPSR_f is implemented. Of that, only N, Z, C and V are implemented.
  if ((reg_and_mask == CPSR_f) && ((value & ~kSpecialCondition) == 0)) {
    n_flag_ = ((value & (1 << 31)) != 0);
    z_flag_ = ((value & (1 << 30)) != 0);
    c_flag_ = ((value & (1 << 29)) != 0);
    v_flag_ = ((value & (1 << 28)) != 0);
  } else {
    UNIMPLEMENTED();
  }
}

uint32_t Simulator::GetFromSpecialRegister(SRegister reg) {
  uint32_t result = 0;
  // Only CPSR_f is implemented.
  if (reg == CPSR) {
    if (n_flag_) result |= (1 << 31);
    if (z_flag_) result |= (1 << 30);
    if (c_flag_) result |= (1 << 29);
    if (v_flag_) result |= (1 << 28);
  } else {
    UNIMPLEMENTED();
  }
  return result;
}

// Runtime FP routines take:
// - two double arguments
// - one double argument and zero or one integer arguments.
// All are consructed here from r0-r3 or d0, d1 and r0.
void Simulator::GetFpArgs(double* x, double* y, int32_t* z) {
  if (use_eabi_hardfloat()) {
    *x = get_double_from_d_register(0).get_scalar();
    *y = get_double_from_d_register(1).get_scalar();
    *z = get_register(0);
  } else {
    // Registers 0 and 1 -> x.
    *x = get_double_from_register_pair(0);
    // Register 2 and 3 -> y.
    *y = get_double_from_register_pair(2);
    // Register 2 -> z
    *z = get_register(2);
  }
}

// The return value is either in r0/r1 or d0.
void Simulator::SetFpResult(const double& result) {
  if (use_eabi_hardfloat()) {
    char buffer[2 * sizeof(vfp_registers_[0])];
    memcpy(buffer, &result, sizeof(buffer));
    // Copy result to d0.
    memcpy(vfp_registers_, buffer, sizeof(buffer));
  } else {
    char buffer[2 * sizeof(registers_[0])];
    memcpy(buffer, &result, sizeof(buffer));
    // Copy result to r0 and r1.
    memcpy(registers_, buffer, sizeof(buffer));
  }
}

void Simulator::TrashCallerSaveRegisters() {
  // Return registers.
  registers_[0] = 0x50BAD4U;
  registers_[1] = 0x50BAD4U;
  // Caller-saved registers.
  registers_[2] = 0x50BAD4U;
  registers_[3] = 0x50BAD4U;
  registers_[12] = 0x50BAD4U;
  // This value is a NaN in both 32-bit and 64-bit FP.
  static const uint64_t v = 0x7ff000007f801000UL;
  // d0 - d7 are caller-saved.
  for (int i = 0; i < 8; i++) {
    set_d_register(i, &v);
  }
  if (DoubleRegister::SupportedRegisterCount() > 16) {
    // d16 - d31 (if supported) are caller-saved.
    for (int i = 16; i < 32; i++) {
      set_d_register(i, &v);
    }
  }
}

int Simulator::ReadW(int32_t addr) {
  // All supported ARM targets allow unaligned accesses, so we don't need to
  // check the alignment here.
  base::MutexGuard lock_guard(&GlobalMonitor::Get()->mutex);
  local_monitor_.NotifyLoad(addr);
  return base::ReadUnalignedValue<intptr_t>(addr);
}

int Simulator::ReadExW(int32_t addr) {
  base::MutexGuard lock_guard(&GlobalMonitor::Get()->mutex);
  local_monitor_.NotifyLoadExcl(addr, TransactionSize::Word);
  GlobalMonitor::Get()->NotifyLoadExcl_Locked(addr, &global_monitor_processor_);
  return base::ReadUnalignedValue<intptr_t>(addr);
}

void Simulator::WriteW(int32_t addr, int value) {
  // All supported ARM targets allow unaligned accesses, so we don't need to
  // check the alignment here.
  base::MutexGuard lock_guard(&GlobalMonitor::Get()->mutex);
  local_monitor_.NotifyStore(addr);
  GlobalMonitor::Get()->NotifyStore_Locked(addr, &global_monitor_processor_);
  base::WriteUnalignedValue<intptr_t>(addr, value);
}

int Simulator::WriteExW(int32_t addr, int value) {
  base::MutexGuard lock_guard(&GlobalMonitor::Get()->mutex);
  if (local_monitor_.NotifyStoreExcl(addr, TransactionSize::Word) &&
      GlobalMonitor::Get()->NotifyStoreExcl_Locked(
          addr, &global_monitor_processor_)) {
    base::WriteUnalignedValue<intptr_t>(addr, value);
    return 0;
  } else {
    return 1;
  }
}

uint16_t Simulator::ReadHU(int32_t addr) {
  // All supported ARM targets allow unaligned accesses, so we don't need to
  // check the alignment here.
  base::MutexGuard lock_guard(&GlobalMonitor::Get()->mutex);
  local_monitor_.NotifyLoad(addr);
  return base::ReadUnalignedValue<uint16_t>(addr);
}

int16_t Simulator::ReadH(int32_t addr) {
  // All supported ARM targets allow unaligned accesses, so we don't need to
  // check the alignment here.
  base::MutexGuard lock_guard(&GlobalMonitor::Get()->mutex);
  local_monitor_.NotifyLoad(addr);
  return base::ReadUnalignedValue<int16_t>(addr);
}

uint16_t Simulator::ReadExHU(int32_t addr) {
  base::MutexGuard lock_guard(&GlobalMonitor::Get()->mutex);
  local_monitor_.NotifyLoadExcl(addr, TransactionSize::HalfWord);
  GlobalMonitor::Get()->NotifyLoadExcl_Locked(addr, &global_monitor_processor_);
  return base::ReadUnalignedValue<uint16_t>(addr);
}

void Simulator::WriteH(int32_t addr, uint16_t value) {
  // All supported ARM targets allow unaligned accesses, so we don't need to
  // check the alignment here.
  base::MutexGuard lock_guard(&GlobalMonitor::Get()->mutex);
  local_monitor_.NotifyStore(addr);
  GlobalMonitor::Get()->NotifyStore_Locked(addr, &global_monitor_processor_);
  base::WriteUnalignedValue(addr, value);
}

void Simulator::WriteH(int32_t addr, int16_t value) {
  // All supported ARM targets allow unaligned accesses, so we don't need to
  // check the alignment here.
  base::MutexGuard lock_guard(&GlobalMonitor::Get()->mutex);
  local_monitor_.NotifyStore(addr);
  GlobalMonitor::Get()->NotifyStore_Locked(addr, &global_monitor_processor_);
  base::WriteUnalignedValue(addr, value);
}

int Simulator::WriteExH(int32_t addr, uint16_t value) {
  base::MutexGuard lock_guard(&GlobalMonitor::Get()->mutex);
  if (local_monitor_.NotifyStoreExcl(addr, TransactionSize::HalfWord) &&
      GlobalMonitor::Get()->NotifyStoreExcl_Locked(
          addr, &global_monitor_processor_)) {
    base::WriteUnalignedValue(addr, value);
    return 0;
  } else {
    return 1;
  }
}

uint8_t Simulator::ReadBU(int32_t addr) {
  base::MutexGuard lock_guard(&GlobalMonitor::Get()->mutex);
  local_monitor_.NotifyLoad(addr);
  return base::ReadUnalignedValue<uint8_t>(addr);
}

int8_t Simulator::ReadB(int32_t addr) {
  base::MutexGuard lock_guard(&GlobalMonitor::Get()->mutex);
  local_monitor_.NotifyLoad(addr);
  return base::ReadUnalignedValue<int8_t>(addr);
}

uint8_t Simulator::ReadExBU(int32_t addr) {
  base::MutexGuard lock_guard(&GlobalMonitor::Get()->mutex);
  local_monitor_.NotifyLoadExcl(addr, TransactionSize::Byte);
  GlobalMonitor::Get()->NotifyLoadExcl_Locked(addr, &global_monitor_processor_);
  return base::ReadUnalignedValue<uint8_t>(addr);
}

void Simulator::WriteB(int32_t addr, uint8_t value) {
  base::MutexGuard lock_guard(&GlobalMonitor::Get()->mutex);
  local_monitor_.NotifyStore(addr);
  GlobalMonitor::Get()->NotifyStore_Locked(addr, &global_monitor_processor_);
  base::WriteUnalignedValue(addr, value);
}

void Simulator::WriteB(int32_t addr, int8_t value) {
  base::MutexGuard lock_guard(&GlobalMonitor::Get()->mutex);
  local_monitor_.NotifyStore(addr);
  GlobalMonitor::Get()->NotifyStore_Locked(addr, &global_monitor_processor_);
  base::WriteUnalignedValue(addr, value);
}

int Simulator::WriteExB(int32_t addr, uint8_t value) {
  base::MutexGuard lock_guard(&GlobalMonitor::Get()->mutex);
  if (local_monitor_.NotifyStoreExcl(addr, TransactionSize::Byte) &&
      GlobalMonitor::Get()->NotifyStoreExcl_Locked(
          addr, &global_monitor_processor_)) {
    base::WriteUnalignedValue(addr, value);
    return 0;
  } else {
    return 1;
  }
}

int32_t* Simulator::ReadDW(int32_t addr) {
  // All supported ARM targets allow unaligned accesses, so we don't need to
  // check the alignment here.
  base::MutexGuard lock_guard(&GlobalMonitor::Get()->mutex);
  local_monitor_.NotifyLoad(addr);
  return reinterpret_cast<int32_t*>(addr);
}

int32_t* Simulator::ReadExDW(int32_t addr) {
  base::MutexGuard lock_guard(&GlobalMonitor::Get()->mutex);
  local_monitor_.NotifyLoadExcl(addr, TransactionSize::DoubleWord);
  GlobalMonitor::Get()->NotifyLoadExcl_Locked(addr, &global_monitor_processor_);
  return reinterpret_cast<int32_t*>(addr);
}

void Simulator::WriteDW(int32_t addr, int32_t value1, int32_t value2) {
  // All supported ARM targets allow unaligned accesses, so we don't need to
  // check the alignment here.
  base::MutexGuard lock_guard(&GlobalMonitor::Get()->mutex);
  local_monitor_.NotifyStore(addr);
  GlobalMonitor::Get()->NotifyStore_Locked(addr, &global_monitor_processor_);
  base::WriteUnalignedValue(addr, value1);
  base::WriteUnalignedValue(addr + sizeof(value1), value2);
}

int Simulator::WriteExDW(int32_t addr, int32_t value1, int32_t value2) {
  base::MutexGuard lock_guard(&GlobalMonitor::Get()->mutex);
  if (local_monitor_.NotifyStoreExcl(addr, TransactionSize::DoubleWord) &&
      GlobalMonitor::Get()->NotifyStoreExcl_Locked(
          addr, &global_monitor_processor_)) {
    base::WriteUnalignedValue(addr, value1);
    base::WriteUnalignedValue(addr + sizeof(value1), value2);
    return 0;
  } else {
    return 1;
  }
}

// Returns the limit of the stack area to enable checking for stack overflows.
uintptr_t Simulator::StackLimit(uintptr_t c_limit) const {
  // The simulator uses a separate JS stack. If we have exhausted the C stack,
  // we also drop down the JS limit to reflect the exhaustion on the JS stack.
  if (base::Stack::GetCurrentStackPosition() < c_limit) {
    return reinterpret_cast<uintptr_t>(get_sp());
  }

  // Otherwise the limit is the JS stack. Leave a safety margin to prevent
  // overrunning the stack when pushing values.
  return reinterpret_cast<uintptr_t>(stack_) + kAdditionalStackMargin;
}

base::Vector<uint8_t> Simulator::GetCurrentStackView() const {
  // We do not add an additional safety margin as above in
  // Simulator::StackLimit, as this is currently only used in wasm::StackMemory,
  // which adds its own margin.
  return base::VectorOf(stack_, kUsableStackSize);
}

// Unsupported instructions use Format to print an error and stop execution.
void Simulator::Format(Instruction* instr, const char* format) {
  PrintF("Simulator found unsupported instruction:\n 0x%08" V8PRIxPTR ": %s\n",
         reinterpret_cast<intptr_t>(instr), format);
  UNIMPLEMENTED();
}

// Checks if the current instruction should be executed based on its
// condition bits.
bool Simulator::ConditionallyExecute(Instruction* instr) {
  switch (instr->ConditionField()) {
    case eq:
      return z_flag_;
    case ne:
      return !z_flag_;
    case cs:
      return c_flag_;
    case cc:
      return !c_flag_;
    case mi:
      return n_flag_;
    case pl:
      return !n_flag_;
    case vs:
      return v_flag_;
    case vc:
      return !v_flag_;
    case hi:
      return c_flag_ && !z_flag_;
    case ls:
      return !c_flag_ || z_flag_;
    case ge:
      return n_flag_ == v_flag_;
    case lt:
      return n_flag_ != v_flag_;
    case gt:
      return !z_flag_ && (n_flag_ == v_flag_);
    case le:
      return z_flag_ || (n_flag_ != v_flag_);
    case al:
      return true;
    default:
      UNREACHABLE();
  }
}

// Calculate and set the Negative and Zero flags.
void Simulator::SetNZFlags(int32_t val) {
  n_flag_ = (val < 0);
  z_flag_ = (val == 0);
}

// Set the Carry flag.
void Simulator::SetCFlag(bool val) { c_flag_ = val; }

// Set the oVerflow flag.
void Simulator::SetVFlag(bool val) { v_flag_ = val; }

// Calculate C flag value for additions.
bool Simulator::CarryFrom(int32_t left, int32_t right, int32_t carry) {
  uint32_t uleft = static_cast<uint32_t>(left);
  uint32_t uright = static_cast<uint32_t>(right);
  uint32_t urest = 0xFFFFFFFFU - uleft;

  return (uright > urest) ||
         (carry && (((uright + 1) > urest) || (uright > (urest - 1))));
}

// Calculate C flag value for subtractions.
bool Simulator::BorrowFrom(int32_t left, int32_t right, int32_t carry) {
  uint32_t uleft = static_cast<uint32_t>(left);
  uint32_t uright = static_cast<uint32_t>(right);

  return (uright > uleft) ||
         (!carry && (((uright + 1) > uleft) || (uright > (uleft - 1))));
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

// Support for VFP comparisons.
void Simulator::Compute_FPSCR_Flags(float val1, float val2) {
  if (std::isnan(val1) || std::isnan(val2)) {
    n_flag_FPSCR_ = false;
    z_flag_FPSCR_ = false;
    c_flag_FPSCR_ = true;
    v_flag_FPSCR_ = true;
    // All non-NaN cases.
  } else if (val1 == val2) {
    n_flag_FPSCR_ = false;
    z_flag_FPSCR_ = true;
    c_flag_FPSCR_ = true;
    v_flag_FPSCR_ = false;
  } else if (val1 < val2) {
    n_flag_FPSCR_ = true;
    z_flag_FPSCR_ = false;
    c_flag_FPSCR_ = false;
    v_flag_FPSCR_ = false;
  } else {
    // Case when (val1 > val2).
    n_flag_FPSCR_ = false;
    z_flag_FPSCR_ = false;
    c_flag_FPSCR_ = true;
    v_flag_FPSCR_ = false;
  }
}

void Simulator::Compute_FPSCR_Flags(double val1, double val2) {
  if (std::isnan(val1) || std::isnan(val2)) {
    n_flag_FPSCR_ = false;
    z_flag_FPSCR_ = false;
    c_flag_FPSCR_ = true;
    v_flag_FPSCR_ = true;
    // All non-NaN cases.
  } else if (val1 == val2) {
    n_flag_FPSCR_ = false;
    z_flag_FPSCR_ = true;
    c_flag_FPSCR_ = true;
    v_flag_FPSCR_ = false;
  } else if (val1 < val2) {
    n_flag_FPSCR_ = true;
    z_flag_FPSCR_ = false;
    c_flag_FPSCR_ = false;
    v_flag_FPSCR_ = false;
  } else {
    // Case when (val1 > val2).
    n_flag_FPSCR_ = false;
    z_flag_FPSCR_ = false;
    c_flag_FPSCR_ = true;
    v_flag_FPSCR_ = false;
  }
}

void Simulator::Copy_FPSCR_to_APSR() {
  n_flag_ = n_flag_FPSCR_;
  z_flag_ = z_flag_FPSCR_;
  c_flag_ = c_flag_FPSCR_;
  v_flag_ = v_flag_FPSCR_;
}

// Addressing Mode 1 - Data-processing operands:
// Get the value based on the shifter_operand with register.
int32_t Simulator::GetShiftRm(Instruction* instr, bool* carry_out) {
  ShiftOp shift = instr->ShiftField();
  int shift_amount = instr->ShiftAmountValue();
  int32_t result = get_register(instr->RmValue());
  if (instr->Bit(4) == 0) {
    // by immediate
    if ((shift == ROR) && (shift_amount == 0)) {
      UNIMPLEMENTED();
    } else if (((shift == LSR) || (shift == ASR)) && (shift_amount == 0)) {
      shift_amount = 32;
    }
    switch (shift) {
      case ASR: {
        if (shift_amount == 0) {
          if (result < 0) {
            result = 0xFFFFFFFF;
            *carry_out = true;
          } else {
            result = 0;
            *carry_out = false;
          }
        } else {
          result >>= (shift_amount - 1);
          *carry_out = (result & 1) == 1;
          result >>= 1;
        }
        break;
      }

      case LSL: {
        if (shift_amount == 0) {
          *carry_out = c_flag_;
        } else {
          result = static_cast<uint32_t>(result) << (shift_amount - 1);
          *carry_out = (result < 0);
          result = static_cast<uint32_t>(result) << 1;
        }
        break;
      }

      case LSR: {
        if (shift_amount == 0) {
          result = 0;
          *carry_out = c_flag_;
        } else {
          uint32_t uresult = static_cast<uint32_t>(result);
          uresult >>= (shift_amount - 1);
          *carry_out = (uresult & 1) == 1;
          uresult >>= 1;
          result = static_cast<int32_t>(uresult);
        }
        break;
      }

      case ROR: {
        if (shift_amount == 0) {
          *carry_out = c_flag_;
        } else {
          result = base::bits::RotateRight32(result, shift_amount);
          *carry_out = (static_cast<uint32_t>(result) >> 31) != 0;
        }
        break;
      }

      default: {
        UNREACHABLE();
      }
    }
  } else {
    // by register
    int rs = instr->RsValue();
    shift_amount = get_register(rs) & 0xFF;
    switch (shift) {
      case ASR: {
        if (shift_amount == 0) {
          *carry_out = c_flag_;
        } else if (shift_amount < 32) {
          result >>= (shift_amount - 1);
          *carry_out = (result & 1) == 1;
          result >>= 1;
        } else {
          DCHECK_GE(shift_amount, 32);
          if (result < 0) {
            *carry_out = true;
            result = 0xFFFFFFFF;
          } else {
            *carry_out = false;
            result = 0;
          }
        }
        break;
      }

      case LSL: {
        if (shift_amount == 0) {
          *carry_out = c_flag_;
        } else if (shift_amount < 32) {
          result = static_cast<uint32_t>(result) << (shift_amount - 1);
          *carry_out = (result < 0);
          result = static_cast<uint32_t>(result) << 1;
        } else if (shift_amount == 32) {
          *carry_out = (result & 1) == 1;
          result = 0;
        } else {
          DCHECK_GT(shift_amount, 32);
          *carry_out = false;
          result = 0;
        }
        break;
      }

      case LSR: {
        if (shift_amount == 0) {
          *carry_out = c_flag_;
        } else if (shift_amount < 32) {
          uint32_t uresult = static_cast<uint32_t>(result);
          uresult >>= (shift_amount - 1);
          *carry_out = (uresult & 1) == 1;
          uresult >>= 1;
          result = static_cast<int32_t>(uresult);
        } else if (shift_amount == 32) {
          *carry_out = (result < 0);
          result = 0;
        } else {
          *carry_out = false;
          result = 0;
        }
        break;
      }

      case ROR: {
        if (shift_amount == 0) {
          *carry_out = c_flag_;
        } else {
          // Avoid undefined behavior. Rotating by multiples of 32 is no-op.
          result = base::bits::RotateRight32(result, shift_amount & 31);
          *carry_out = (static_cast<uint32_t>(result) >> 31) != 0;
        }
        break;
      }

      default: {
        UNREACHABLE();
      }
    }
  }
  return result;
}

// Addressing Mode 1 - Data-processing operands:
// Get the value based on the shifter_operand with immediate.
int32_t Simulator::GetImm(Instruction* instr, bool* carry_out) {
  int rotate = instr->RotateValue() * 2;
  int immed8 = instr->Immed8Value();
  int imm = base::bits::RotateRight32(immed8, rotate);
  *carry_out = (rotate == 0) ? c_flag_ : (imm < 0);
  return imm;
}

static int count_bits(int bit_vector) {
  int count = 0;
  while (bit_vector != 0) {
    if ((bit_vector & 1) != 0) {
      count++;
    }
    bit_vector >>= 1;
  }
  return count;
}

int32_t Simulator::ProcessPU(Instruction* instr, int num_regs, int reg_size,
                             intptr_t* start_address, intptr_t* end_address) {
  int rn = instr->RnValue();
  int32_t rn_val = get_register(rn);
  switch (instr->PUField()) {
    case da_x: {
      UNIMPLEMENTED();
    }
    case ia_x: {
      *start_address = rn_val;
      *end_address = rn_val + (num_regs * reg_size) - reg_size;
      rn_val = rn_val + (num_regs * reg_size);
      break;
    }
    case db_x: {
      *start_address = rn_val - (num_regs * reg_size);
      *end_address = rn_val - reg_size;
      rn_val = *start_address;
      break;
    }
    case ib_x: {
      *start_address = rn_val + reg_size;
      *end_address = rn_val + (num_regs * reg_size);
      rn_val = *end_address;
      break;
    }
    default: {
      UNREACHABLE();
    }
  }
  return rn_val;
}

// Addressing Mode 4 - Load and Store Multiple
void Simulator::HandleRList(Instruction* instr, bool load) {
  int rlist = instr->RlistValue();
  int num_regs = count_bits(rlist);

  intptr_t start_address = 0;
  intptr_t end_address = 0;
  int32_t rn_val =
      ProcessPU(instr, num_regs, kPointerSize, &start_address, &end_address);

  intptr_t* address = reinterpret_cast<intptr_t*>(start_address);
  // Catch null pointers a little earlier.
  DCHECK(start_address > 8191 || start_address < 0);
  int reg = 0;
  while (rlist != 0) {
    if ((rlist & 1) != 0) {
      if (load) {
        set_register(reg, *address);
      } else {
        *address = get_register(reg);
      }
      address += 1;
    }
    reg++;
    rlist >>= 1;
  }
  DCHECK(end_address == ((intptr_t)address) - 4);
  if (instr->HasW()) {
    set_register(instr->RnValue(), rn_val);
  }
}

// Addressing Mode 6 - Load and Store Multiple Coprocessor registers.
void Simulator::HandleVList(Instruction* instr) {
  VFPRegPrecision precision =
      (instr->SzValue() == 0) ? kSinglePrecision : kDoublePrecision;
  int operand_size = (precision == kSinglePrecision) ? 4 : 8;

  bool load = (instr->VLValue() == 0x1);

  int vd;
  int num_regs;
  vd = instr->VFPDRegValue(precision);
  if (precision == kSinglePrecision) {
    num_regs = instr->Immed8Value();
  } else {
    num_regs = instr->Immed8Value() / 2;
  }

  intptr_t start_address = 0;
  intptr_t end_address = 0;
  int32_t rn_val =
      ProcessPU(instr, num_regs, operand_size, &start_address, &end_address);

  intptr_t* address = reinterpret_cast<intptr_t*>(start_address);
  for (int reg = vd; reg < vd + num_regs; reg++) {
    if (precision == kSinglePrecision) {
      if (load) {
        set_s_register_from_sinteger(reg,
                                     ReadW(reinterpret_cast<int32_t>(address)));
      } else {
        WriteW(reinterpret_cast<int32_t>(address),
               get_sinteger_from_s_register(reg));
      }
      address += 1;
    } else {
      if (load) {
        int32_t data[] = {ReadW(reinterpret_cast<int32_t>(address)),
                          ReadW(reinterpret_cast<int32_t>(address + 1))};
        set_d_register(reg, reinterpret_cast<uint32_t*>(data));
      } else {
        uint32_t data[2];
        get_d_register(reg, data);
        WriteW(reinterpret_cast<int32_t>(address), data[0]);
        WriteW(reinterpret_cast<int32_t>(address + 1), data[1]);
      }
      address += 2;
    }
  }
  DCHECK(reinterpret_cast<intptr_t>(address) - operand_size == end_address);
  if (instr->HasW()) {
    set_register(instr->RnValue(), rn_val);
  }
}

// Calls into the V8 runtime are based on this very simple interface.
// Note: To be able to return two values from some calls the code in runtime.cc
// uses the ObjectPair which is essentially two 32-bit values stuffed into a
// 64-bit value. With the code below we assume that all runtime calls return
// 64 bits of result. If they don't, the r1 result register contains a bogus
// value, which is fine because it is caller-saved.
using SimulatorRuntimeCall = int64_t (*)(
    int32_t arg0, int32_t arg1, int32_t arg2, int32_t arg3, int32_t arg4,
    int32_t arg5, int32_t arg6, int32_t arg7, int32_t arg8, int32_t arg9,
    int32_t arg10, int32_t arg11, int32_t arg12, int32_t arg13, int32_t arg14,
    int32_t arg15, int32_t arg16, int32_t arg17, int32_t arg18, int32_t arg19);

// These prototypes handle the four types of FP calls.
using SimulatorRuntimeCompareCall = int64_t (*)(double darg0, double darg1);
using SimulatorRuntimeFPFPCall = double (*)(double darg0, double darg1);
using SimulatorRuntimeFPCall = double (*)(double darg0);
using SimulatorRuntimeFPIntCall = double (*)(double darg0, int32_t arg0);
// Define four args for future flexibility; at the time of this writing only
// one is ever used.
using SimulatorRuntimeFPTaggedCall = double (*)(int32_t arg0, int32_t arg1,
                                                int32_t arg2, int32_t arg3);

// This signature supports direct call in to API function native callback
// (refer to InvocationCallback in v8.h).
using SimulatorRuntimeDirectApiCall = void (*)(int32_t arg0);

// This signature supports direct call to accessor getter callback.
using SimulatorRuntimeDirectGetterCall = void (*)(int32_t arg0, int32_t arg1);

// Separate for fine-grained UBSan blocklisting. Casting any given C++
// function to {SimulatorRuntimeCall} is undefined behavior; but since
// the target function can indeed be any function that's exposed via
// the "fast C call" mechanism, we can't reconstruct its signature here.
int64_t UnsafeGenericFunctionCall(intptr_t function, int32_t arg0, int32_t arg1,
                                  int32_t arg2, int32_t arg3, int32_t arg4,
                                  int32_t arg5, int32_t arg6, int32_t arg7,
                                  int32_t arg8, int32_t arg9, int32_t arg10,
                                  int32_t arg11, int32_t arg12, int32_t arg13,
                                  int32_t arg14, int32_t arg15, int32_t arg16,
                                  int32_t arg17, int32_t arg18, int32_t arg19) {
  SimulatorRuntimeCall target =
      reinterpret_cast<SimulatorRuntimeCall>(function);
  return target(arg0, arg1, arg2, arg3, arg4, arg5, arg6, arg7, arg8, arg9,
                arg10, arg11, arg12, arg13, arg14, arg15, arg16, arg17, arg18,
                arg19);
}

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
      int32_t arg0 = get_register(r0);
      int32_t arg1 = get_register(r1);
      int32_t arg2 = get_register(r2);
      int32_t arg3 = get_register(r3);
      int32_t* stack_pointer = reinterpret_cast<int32_t*>(get_register(sp));
      int32_t arg4 = stack_pointer[0];
      int32_t arg5 = stack_pointer[1];
      int32_t arg6 = stack_pointer[2];
      int32_t arg7 = stack_pointer[3];
      int32_t arg8 = stack_pointer[4];
      int32_t arg9 = stack_pointer[5];
      int32_t arg10 = stack_pointer[6];
      int32_t arg11 = stack_pointer[7];
      int32_t arg12 = stack_pointer[8];
      int32_t arg13 = stack_pointer[9];
      int32_t arg14 = stack_pointer[10];
      int32_t arg15 = stack_pointer[11];
      int32_t arg16 = stack_pointer[12];
      int32_t arg17 = stack_pointer[13];
      int32_t arg18 = stack_pointer[14];
      int32_t arg19 = stack_pointer[15];
      static_assert(kMaxCParameters == 20);

      bool fp_call =
          (redirection->type() == ExternalReference::BUILTIN_FP_FP_CALL) ||
          (redirection->type() == ExternalReference::BUILTIN_COMPARE_CALL) ||
          (redirection->type() == ExternalReference::BUILTIN_FP_CALL) ||
          (redirection->type() == ExternalReference::BUILTIN_FP_INT_CALL);
      // This is dodgy but it works because the C entry stubs are never moved.
      // See comment in codegen-arm.cc and bug 1242173.
      int32_t saved_lr = get_register(lr);
      intptr_t external =
          reinterpret_cast<intptr_t>(redirection->external_function());
      if (fp_call) {
        double dval0, dval1;  // one or two double parameters
        int32_t ival;         // zero or one integer parameters
        int64_t iresult = 0;  // integer return value
        double dresult = 0;   // double return value
        GetFpArgs(&dval0, &dval1, &ival);
        if (InstructionTracingEnabled() || !stack_aligned) {
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
              PrintF("Call to host function at %p with args %f, %d",
                     reinterpret_cast<void*>(FUNCTION_ADDR(generic_target)),
                     dval0, ival);
              break;
            default:
              UNREACHABLE();
          }
          if (!stack_aligned) {
            PrintF(" with unaligned stack %08x\n", get_register(sp));
          }
          PrintF("\n");
        }
        CHECK(stack_aligned);
        switch (redirection->type()) {
          case ExternalReference::BUILTIN_COMPARE_CALL: {
            SimulatorRuntimeCompareCall target =
                reinterpret_cast<SimulatorRuntimeCompareCall>(external);
            iresult = target(dval0, dval1);
#ifdef DEBUG
            TrashCallerSaveRegisters();
#endif
            set_register(r0, static_cast<int32_t>(iresult));
            set_register(r1, static_cast<int32_t>(iresult >> 32));
            break;
          }
          case ExternalReference::BUILTIN_FP_FP_CALL: {
            SimulatorRuntimeFPFPCall target =
                reinterpret_cast<SimulatorRuntimeFPFPCall>(external);
            dresult = target(dval0, dval1);
#ifdef DEBUG
            TrashCallerSaveRegisters();
#endif
            SetFpResult(dresult);
            break;
          }
          case ExternalReference::BUILTIN_FP_CALL: {
            SimulatorRuntimeFPCall target =
                reinterpret_cast<SimulatorRuntimeFPCall>(external);
            dresult = target(dval0);
#ifdef DEBUG
            TrashCallerSaveRegisters();
#endif
            SetFpResult(dresult);
            break;
          }
          case ExternalReference::BUILTIN_FP_INT_CALL: {
            SimulatorRuntimeFPIntCall target =
                reinterpret_cast<SimulatorRuntimeFPIntCall>(external);
            dresult = target(dval0, ival);
#ifdef DEBUG
            TrashCallerSaveRegisters();
#endif
            SetFpResult(dresult);
            break;
          }
          default:
            UNREACHABLE();
        }
        if (InstructionTracingEnabled()) {
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
        if (InstructionTracingEnabled() || !stack_aligned) {
          PrintF("Call to host function at %p args %08x",
                 reinterpret_cast<void*>(external), arg0);
          if (!stack_aligned) {
            PrintF(" with unaligned stack %08x\n", get_register(sp));
          }
          PrintF("\n");
        }
        CHECK(stack_aligned);
        SimulatorRuntimeFPTaggedCall target =
            reinterpret_cast<SimulatorRuntimeFPTaggedCall>(external);
        double dresult = target(arg0, arg1, arg2, arg3);
#ifdef DEBUG
        TrashCallerSaveRegisters();
#endif
        SetFpResult(dresult);
        if (InstructionTracingEnabled()) {
          PrintF("Returned %f\n", dresult);
        }
      } else if (redirection->type() == ExternalReference::DIRECT_API_CALL) {
        // void f(v8::FunctionCallbackInfo&)
        if (InstructionTracingEnabled() || !stack_aligned) {
          PrintF("Call to host function at %p args %08x",
                 reinterpret_cast<void*>(external), arg0);
          if (!stack_aligned) {
            PrintF(" with unaligned stack %08x\n", get_register(sp));
          }
          PrintF("\n");
        }
        CHECK(stack_aligned);
        SimulatorRuntimeDirectApiCall target =
            reinterpret_cast<SimulatorRuntimeDirectApiCall>(external);
        target(arg0);
#ifdef DEBUG
        TrashCallerSaveRegisters();
#endif
      } else if (redirection->type() == ExternalReference::DIRECT_GETTER_CALL) {
        // void f(v8::Local<String> property, v8::PropertyCallbackInfo& info)
        if (InstructionTracingEnabled() || !stack_aligned) {
          PrintF("Call to host function at %p args %08x %08x",
                 reinterpret_cast<void*>(external), arg0, arg1);
          if (!stack_aligned) {
            PrintF(" with unaligned stack %08x\n", get_register(sp));
          }
          PrintF("\n");
        }
        CHECK(stack_aligned);
        SimulatorRuntimeDirectGetterCall target =
            reinterpret_cast<SimulatorRuntimeDirectGetterCall>(external);
        target(arg0, arg1);
#ifdef DEBUG
        TrashCallerSaveRegisters();
#endif
      } else {
        // builtin call.
        // FAST_C_CALL is temporarily handled here as well, because we lack
        // proper support for direct C calls with FP params in the simulator.
        // The generic BUILTIN_CALL path assumes all parameters are passed in
        // the GP registers, thus supporting calling the slow callback without
        // crashing. The reason for that is that in the mjsunit tests we check
        // the `fast_c_api.supports_fp_params` (which is false on non-simulator
        // builds for arm/arm64), thus we expect that the slow path will be
        // called. And since the slow path passes the arguments as a `const
        // FunctionCallbackInfo<Value>&` (which is a GP argument), the call is
        // made correctly.
        DCHECK(redirection->type() == ExternalReference::BUILTIN_CALL ||
               redirection->type() == ExternalReference::BUILTIN_CALL_PAIR ||
               redirection->type() == ExternalReference::FAST_C_CALL);
        if (InstructionTracingEnabled() || !stack_aligned) {
          PrintF(
              "Call to host function at %p "
              "args %08x, %08x, %08x, %08x, %08x, %08x, %08x, %08x, %08x, "
              "%08x, %08x, %08x, %08x, %08x, %08x, %08x, %08x, %08x, %08x, "
              "%08x",
              reinterpret_cast<void*>(external), arg0, arg1, arg2, arg3, arg4,
              arg5, arg6, arg7, arg8, arg9, arg10, arg11, arg12, arg13, arg14,
              arg15, arg16, arg17, arg18, arg19);
          if (!stack_aligned) {
            PrintF(" with unaligned stack %08x\n", get_register(sp));
          }
          PrintF("\n");
        }
        CHECK(stack_aligned);
        int64_t result = UnsafeGenericFunctionCall(
            external, arg0, arg1, arg2, arg3, arg4, arg5, arg6, arg7, arg8,
            arg9, arg10, arg11, arg12, arg13, arg14, arg15, arg16, arg17, arg18,
            arg19);
#ifdef DEBUG
        TrashCallerSaveRegisters();
#endif
        int32_t lo_res = static_cast<int32_t>(result);
        int32_t hi_res = static_cast<int32_t>(result >> 32);
        if (InstructionTracingEnabled()) {
          PrintF("Returned %08x\n", lo_res);
        }
        set_register(r0, lo_res);
        set_register(r1, hi_res);
      }
      set_register(lr, saved_lr);
      set_pc(get_register(lr));
      break;
    }
    case kBreakpoint:
      ArmDebugger(this).Debug();
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
        }
      } else {
        // This is not a valid svc code.
        UNREACHABLE();
      }
  }
}

float Simulator::canonicalizeNaN(float value) {
  // Default NaN value, see "NaN handling" in "IEEE 754 standard implementation
  // choices" of the ARM Reference Manual.
  constexpr uint32_t kDefaultNaN = 0x7FC00000u;
  if (FPSCR_default_NaN_mode_ && std::isnan(value)) {
    value = base::bit_cast<float>(kDefaultNaN);
  }
  return value;
}

Float32 Simulator::canonicalizeNaN(Float32 value) {
  // Default NaN value, see "NaN handling" in "IEEE 754 standard implementation
  // choices" of the ARM Reference Manual.
  constexpr Float32 kDefaultNaN = Float32::FromBits(0x7FC00000u);
  return FPSCR_default_NaN_mode_ && value.is_nan() ? kDefaultNaN : value;
}

double Simulator::canonicalizeNaN(double value) {
  // Default NaN value, see "NaN handling" in "IEEE 754 standard implementation
  // choices" of the ARM Reference Manual.
  constexpr uint64_t kDefaultNaN = uint64_t{0x7FF8000000000000};
  if (FPSCR_default_NaN_mode_ && std::isnan(value)) {
    value = base::bit_cast<double>(kDefaultNaN);
  }
  return value;
}

Float64 Simulator::canonicalizeNaN(Float64 value) {
  // Default NaN value, see "NaN handling" in "IEEE 754 standard implementation
  // choices" of the ARM Reference Manual.
  constexpr Float64 kDefaultNaN =
      Float64::FromBits(uint64_t{0x7FF8000000000000});
  return FPSCR_default_NaN_mode_ && value.is_nan() ? kDefaultNaN : value;
}

// Stop helper functions.
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

// Handle execution based on instruction types.

// Instruction types 0 and 1 are both rolled into one function because they
// only differ in the handling of the shifter_operand.
void Simulator::DecodeType01(Instruction* instr) {
  int type = instr->TypeValue();
  if ((type == 0) && instr->IsSpecialType0()) {
    // multiply instruction or extra loads and stores
    if (instr->Bits(7, 4) == 9) {
      if (instr->Bit(24) == 0) {
        // Raw field decoding here. Multiply instructions have their Rd in
        // funny places.
        int rn = instr->RnValue();
        int rm = instr->RmValue();
        int rs = instr->RsValue();
        int32_t rs_val = get_register(rs);
        int32_t rm_val = get_register(rm);
        if (instr->Bit(23) == 0) {
          if (instr->Bit(21) == 0) {
            // The MUL instruction description (A 4.1.33) refers to Rd as being
            // the destination for the operation, but it confusingly uses the
            // Rn field to encode it.
            // Format(instr, "mul'cond's 'rn, 'rm, 'rs");
            int rd = rn;  // Remap the rn field to the Rd register.
            int32_t alu_out = base::MulWithWraparound(rm_val, rs_val);
            set_register(rd, alu_out);
            if (instr->HasS()) {
              SetNZFlags(alu_out);
            }
          } else {
            int rd = instr->RdValue();
            int32_t acc_value = get_register(rd);
            if (instr->Bit(22) == 0) {
              // The MLA instruction description (A 4.1.28) refers to the order
              // of registers as "Rd, Rm, Rs, Rn". But confusingly it uses the
              // Rn field to encode the Rd register and the Rd field to encode
              // the Rn register.
              // Format(instr, "mla'cond's 'rn, 'rm, 'rs, 'rd");
              int32_t mul_out = base::MulWithWraparound(rm_val, rs_val);
              int32_t result = base::AddWithWraparound(acc_value, mul_out);
              set_register(rn, result);
            } else {
              // Format(instr, "mls'cond's 'rn, 'rm, 'rs, 'rd");
              int32_t mul_out = base::MulWithWraparound(rm_val, rs_val);
              int32_t result = base::SubWithWraparound(acc_value, mul_out);
              set_register(rn, result);
            }
          }
        } else {
          // The signed/long multiply instructions use the terms RdHi and RdLo
          // when referring to the target registers. They are mapped to the Rn
          // and Rd fields as follows:
          // RdLo == Rd
          // RdHi == Rn (This is confusingly stored in variable rd here
          //             because the mul instruction from above uses the
          //             Rn field to encode the Rd register. Good luck figuring
          //             this out without reading the ARM instruction manual
          //             at a very detailed level.)
          // Format(instr, "'um'al'cond's 'rd, 'rn, 'rs, 'rm");
          int rd_hi = rn;  // Remap the rn field to the RdHi register.
          int rd_lo = instr->RdValue();
          int32_t hi_res = 0;
          int32_t lo_res = 0;
          if (instr->Bit(22) == 1) {
            int64_t left_op = static_cast<int32_t>(rm_val);
            int64_t right_op = static_cast<int32_t>(rs_val);
            uint64_t result = left_op * right_op;
            hi_res = static_cast<int32_t>(result >> 32);
            lo_res = static_cast<int32_t>(result & 0xFFFFFFFF);
          } else {
            // unsigned multiply
            uint64_t left_op = static_cast<uint32_t>(rm_val);
            uint64_t right_op = static_cast<uint32_t>(rs_val);
            uint64_t result = left_op * right_op;
            hi_res = static_cast<int32_t>(result >> 32);
            lo_res = static_cast<int32_t>(result & 0xFFFFFFFF);
          }
          set_register(rd_lo, lo_res);
          set_register(rd_hi, hi_res);
          if (instr->HasS()) {
            UNIMPLEMENTED();
          }
        }
      } else {
        if (instr->Bits(24, 23) == 3) {
          if (instr->Bit(20) == 1) {
            // ldrex
            int rt = instr->RtValue();
            int rn = instr->RnValue();
            int32_t addr = get_register(rn);
            switch (instr->Bits(22, 21)) {
              case 0: {
                // Format(instr, "ldrex'cond 'rt, ['rn]");
                int value = ReadExW(addr);
                set_register(rt, value);
                break;
              }
              case 1: {
                // Format(instr, "ldrexd'cond 'rt, ['rn]");
                int* rn_data = ReadExDW(addr);
                set_dw_register(rt, rn_data);
                break;
              }
              case 2: {
                // Format(instr, "ldrexb'cond 'rt, ['rn]");
                uint8_t value = ReadExBU(addr);
                set_register(rt, value);
                break;
              }
              case 3: {
                // Format(instr, "ldrexh'cond 'rt, ['rn]");
                uint16_t value = ReadExHU(addr);
                set_register(rt, value);
                break;
              }
              default:
                UNREACHABLE();
            }
          } else {
            // The instruction is documented as strex rd, rt, [rn], but the
            // "rt" register is using the rm bits.
            int rd = instr->RdValue();
            int rt = instr->RmValue();
            int rn = instr->RnValue();
            DCHECK_NE(rd, rn);
            DCHECK_NE(rd, rt);
            int32_t addr = get_register(rn);
            switch (instr->Bits(22, 21)) {
              case 0: {
                // Format(instr, "strex'cond 'rd, 'rm, ['rn]");
                int value = get_register(rt);
                int status = WriteExW(addr, value);
                set_register(rd, status);
                break;
              }
              case 1: {
                // Format(instr, "strexd'cond 'rd, 'rm, ['rn]");
                DCHECK_EQ(rt % 2, 0);
                int32_t value1 = get_register(rt);
                int32_t value2 = get_register(rt + 1);
                int status = WriteExDW(addr, value1, value2);
                set_register(rd, status);
                break;
              }
              case 2: {
                // Format(instr, "strexb'cond 'rd, 'rm, ['rn]");
                uint8_t value = get_register(rt);
                int status = WriteExB(addr, value);
                set_register(rd, status);
                break;
              }
              case 3: {
                // Format(instr, "strexh'cond 'rd, 'rm, ['rn]");
                uint16_t value = get_register(rt);
                int status = WriteExH(addr, value);
                set_register(rd, status);
                break;
              }
              default:
                UNREACHABLE();
            }
          }
        } else {
          UNIMPLEMENTED();  // Not used by V8.
        }
      }
    } else {
      // extra load/store instructions
      int rd = instr->RdValue();
      int rn = instr->RnValue();
      int32_t rn_val = get_register(rn);
      int32_t addr = 0;
      if (instr->Bit(22) == 0) {
        int rm = instr->RmValue();
        int32_t rm_val = get_register(rm);
        switch (instr->PUField()) {
          case da_x: {
            // Format(instr, "'memop'cond'sign'h 'rd, ['rn], -'rm");
            DCHECK(!instr->HasW());
            addr = rn_val;
            rn_val = base::SubWithWraparound(rn_val, rm_val);
            set_register(rn, rn_val);
            break;
          }
          case ia_x: {
            // Format(instr, "'memop'cond'sign'h 'rd, ['rn], +'rm");
            DCHECK(!instr->HasW());
            addr = rn_val;
            rn_val = base::AddWithWraparound(rn_val, rm_val);
            set_register(rn, rn_val);
            break;
          }
          case db_x: {
            // Format(instr, "'memop'cond'sign'h 'rd, ['rn, -'rm]'w");
            rn_val = base::SubWithWraparound(rn_val, rm_val);
            addr = rn_val;
            if (instr->HasW()) {
              set_register(rn, rn_val);
            }
            break;
          }
          case ib_x: {
            // Format(instr, "'memop'cond'sign'h 'rd, ['rn, +'rm]'w");
            rn_val = base::AddWithWraparound(rn_val, rm_val);
            addr = rn_val;
            if (instr->HasW()) {
              set_register(rn, rn_val);
            }
            break;
          }
          default: {
            // The PU field is a 2-bit field.
            UNREACHABLE();
          }
        }
      } else {
        int32_t imm_val = (instr->ImmedHValue() << 4) | instr->ImmedLValue();
        switch (instr->PUField()) {
          case da_x: {
            // Format(instr, "'memop'cond'sign'h 'rd, ['rn], #-'off8");
            DCHECK(!instr->HasW());
            addr = rn_val;
            rn_val = base::SubWithWraparound(rn_val, imm_val);
            set_register(rn, rn_val);
            break;
          }
          case ia_x: {
            // Format(instr, "'memop'cond'sign'h 'rd, ['rn], #+'off8");
            DCHECK(!instr->HasW());
            addr = rn_val;
            rn_val = base::AddWithWraparound(rn_val, imm_val);
            set_register(rn, rn_val);
            break;
          }
          case db_x: {
            // Format(instr, "'memop'cond'sign'h 'rd, ['rn, #-'off8]'w");
            rn_val = base::SubWithWraparound(rn_val, imm_val);
            addr = rn_val;
            if (instr->HasW()) {
              set_register(rn, rn_val);
            }
            break;
          }
          case ib_x: {
            // Format(instr, "'memop'cond'sign'h 'rd, ['rn, #+'off8]'w");
            rn_val = base::AddWithWraparound(rn_val, imm_val);
            addr = rn_val;
            if (instr->HasW()) {
              set_register(rn, rn_val);
            }
            break;
          }
          default: {
            // The PU field is a 2-bit field.
            UNREACHABLE();
          }
        }
      }
      if (((instr->Bits(7, 4) & 0xD) == 0xD) && (instr->Bit(20) == 0)) {
        DCHECK_EQ(rd % 2, 0);
        if (instr->HasH()) {
          // The strd instruction.
          int32_t value1 = get_register(rd);
          int32_t value2 = get_register(rd + 1);
          WriteDW(addr, value1, value2);
        } else {
          // The ldrd instruction.
          int* rn_data = ReadDW(addr);
          set_dw_register(rd, rn_data);
        }
      } else if (instr->HasH()) {
        if (instr->HasSign()) {
          if (instr->HasL()) {
            int16_t val = ReadH(addr);
            set_register(rd, val);
          } else {
            int16_t val = get_register(rd);
            WriteH(addr, val);
          }
        } else {
          if (instr->HasL()) {
            uint16_t val = ReadHU(addr);
            set_register(rd, val);
          } else {
            uint16_t val = get_register(rd);
            WriteH(addr, val);
          }
        }
      } else {
        // signed byte loads
        DCHECK(instr->HasSign());
        DCHECK(instr->HasL());
        int8_t val = ReadB(addr);
        set_register(rd, val);
      }
      return;
    }
  } else if ((type == 0) && instr->IsMiscType0()) {
    if ((instr->Bits(27, 23) == 2) && (instr->Bits(21, 20) == 2) &&
        (instr->Bits(15, 4) == 0xF00)) {
      // MSR
      int rm = instr->RmValue();
      DCHECK_NE(pc, rm);  // UNPREDICTABLE
      SRegisterFieldMask sreg_and_mask =
          instr->BitField(22, 22) | instr->BitField(19, 16);
      SetSpecialRegister(sreg_and_mask, get_register(rm));
    } else if ((instr->Bits(27, 23) == 2) && (instr->Bits(21, 20) == 0) &&
               (instr->Bits(11, 0) == 0)) {
      // MRS
      int rd = instr->RdValue();
      DCHECK_NE(pc, rd);  // UNPREDICTABLE
      SRegister sreg = static_cast<SRegister>(instr->BitField(22, 22));
      set_register(rd, GetFromSpecialRegister(sreg));
    } else if (instr->Bits(22, 21) == 1) {
      int rm = instr->RmValue();
      switch (instr->BitField(7, 4)) {
        case BX:
          set_pc(get_register(rm));
          break;
        case BLX: {
          uint32_t old_pc = get_pc();
          set_pc(get_register(rm));
          set_register(lr, old_pc + kInstrSize);
          break;
        }
        case BKPT:
          PrintF("Simulator hit BKPT. ");
          DebugAtNextPC();
          break;
        default:
          UNIMPLEMENTED();
      }
    } else if (instr->Bits(22, 21) == 3) {
      int rm = instr->RmValue();
      int rd = instr->RdValue();
      switch (instr->BitField(7, 4)) {
        case CLZ: {
          uint32_t bits = get_register(rm);
          int leading_zeros = 0;
          if (bits == 0) {
            leading_zeros = 32;
          } else {
            while ((bits & 0x80000000u) == 0) {
              bits <<= 1;
              leading_zeros++;
            }
          }
          set_register(rd, leading_zeros);
          break;
        }
        default:
          UNIMPLEMENTED();
      }
    } else {
      PrintF("%08x\n", instr->InstructionBits());
      UNIMPLEMENTED();
    }
  } else if ((type == 1) && instr->IsNopLikeType1()) {
    if (instr->BitField(7, 0) == 0) {
      // NOP.
    } else if (instr->BitField(7, 0) == 20) {
      // CSDB.
    } else {
      PrintF("%08x\n", instr->InstructionBits());
      UNIMPLEMENTED();
    }
  } else {
    int rd = instr->RdValue();
    int rn = instr->RnValue();
    int32_t rn_val = get_register(rn);
    int32_t shifter_operand = 0;
    bool shifter_carry_out = false;
    if (type == 0) {
      shifter_operand = GetShiftRm(instr, &shifter_carry_out);
    } else {
      DCHECK_EQ(instr->TypeValue(), 1);
      shifter_operand = GetImm(instr, &shifter_carry_out);
    }
    int32_t alu_out;

    switch (instr->OpcodeField()) {
      case AND: {
        // Format(instr, "and'cond's 'rd, 'rn, 'shift_rm");
        // Format(instr, "and'cond's 'rd, 'rn, 'imm");
        alu_out = rn_val & shifter_operand;
        set_register(rd, alu_out);
        if (instr->HasS()) {
          SetNZFlags(alu_out);
          SetCFlag(shifter_carry_out);
        }
        break;
      }

      case EOR: {
        // Format(instr, "eor'cond's 'rd, 'rn, 'shift_rm");
        // Format(instr, "eor'cond's 'rd, 'rn, 'imm");
        alu_out = rn_val ^ shifter_operand;
        set_register(rd, alu_out);
        if (instr->HasS()) {
          SetNZFlags(alu_out);
          SetCFlag(shifter_carry_out);
        }
        break;
      }

      case SUB: {
        // Format(instr, "sub'cond's 'rd, 'rn, 'shift_rm");
        // Format(instr, "sub'cond's 'rd, 'rn, 'imm");
        alu_out = base::SubWithWraparound(rn_val, shifter_operand);
        set_register(rd, alu_out);
        if (instr->HasS()) {
          SetNZFlags(alu_out);
          SetCFlag(!BorrowFrom(rn_val, shifter_operand));
          SetVFlag(OverflowFrom(alu_out, rn_val, shifter_operand, false));
        }
        break;
      }

      case RSB: {
        // Format(instr, "rsb'cond's 'rd, 'rn, 'shift_rm");
        // Format(instr, "rsb'cond's 'rd, 'rn, 'imm");
        alu_out = base::SubWithWraparound(shifter_operand, rn_val);
        set_register(rd, alu_out);
        if (instr->HasS()) {
          SetNZFlags(alu_out);
          SetCFlag(!BorrowFrom(shifter_operand, rn_val));
          SetVFlag(OverflowFrom(alu_out, shifter_operand, rn_val, false));
        }
        break;
      }

      case ADD: {
        // Format(instr, "add'cond's 'rd, 'rn, 'shift_rm");
        // Format(instr, "add'cond's 'rd, 'rn, 'imm");
        alu_out = base::AddWithWraparound(rn_val, shifter_operand);
        set_register(rd, alu_out);
        if (instr->HasS()) {
          SetNZFlags(alu_out);
          SetCFlag(CarryFrom(rn_val, shifter_operand));
          SetVFlag(OverflowFrom(alu_out, rn_val, shifter_operand, true));
        }
        break;
      }

      case ADC: {
        // Format(instr, "adc'cond's 'rd, 'rn, 'shift_rm");
        // Format(instr, "adc'cond's 'rd, 'rn, 'imm");
        alu_out = base::AddWithWraparound(
            base::AddWithWraparound(rn_val, shifter_operand), GetCarry());
        set_register(rd, alu_out);
        if (instr->HasS()) {
          SetNZFlags(alu_out);
          SetCFlag(CarryFrom(rn_val, shifter_operand, GetCarry()));
          SetVFlag(OverflowFrom(alu_out, rn_val, shifter_operand, true));
        }
        break;
      }

      case SBC: {
        //        Format(instr, "sbc'cond's 'rd, 'rn, 'shift_rm");
        //        Format(instr, "sbc'cond's 'rd, 'rn, 'imm");
        alu_out = base::SubWithWraparound(
            base::SubWithWraparound(rn_val, shifter_operand),
            (GetCarry() ? 0 : 1));
        set_register(rd, alu_out);
        if (instr->HasS()) {
          SetNZFlags(alu_out);
          SetCFlag(!BorrowFrom(rn_val, shifter_operand, GetCarry()));
          SetVFlag(OverflowFrom(alu_out, rn_val, shifter_operand, false));
        }
        break;
      }

      case RSC: {
        Format(instr, "rsc'cond's 'rd, 'rn, 'shift_rm");
        Format(instr, "rsc'cond's 'rd, 'rn, 'imm");
        break;
      }

      case TST: {
        if (instr->HasS()) {
          // Format(instr, "tst'cond 'rn, 'shift_rm");
          // Format(instr, "tst'cond 'rn, 'imm");
          alu_out = rn_val & shifter_operand;
          SetNZFlags(alu_out);
          SetCFlag(shifter_carry_out);
        } else {
          // Format(instr, "movw'cond 'rd, 'imm").
          alu_out = instr->ImmedMovwMovtValue();
          set_register(rd, alu_out);
        }
        break;
      }

      case TEQ: {
        if (instr->HasS()) {
          // Format(instr, "teq'cond 'rn, 'shift_rm");
          // Format(instr, "teq'cond 'rn, 'imm");
          alu_out = rn_val ^ shifter_operand;
          SetNZFlags(alu_out);
          SetCFlag(shifter_carry_out);
        } else {
          // Other instructions matching this pattern are handled in the
          // miscellaneous instructions part above.
          UNREACHABLE();
        }
        break;
      }

      case CMP: {
        if (instr->HasS()) {
          // Format(instr, "cmp'cond 'rn, 'shift_rm");
          // Format(instr, "cmp'cond 'rn, 'imm");
          alu_out = base::SubWithWraparound(rn_val, shifter_operand);
          SetNZFlags(alu_out);
          SetCFlag(!BorrowFrom(rn_val, shifter_operand));
          SetVFlag(OverflowFrom(alu_out, rn_val, shifter_operand, false));
        } else {
          // Format(instr, "movt'cond 'rd, 'imm").
          alu_out =
              (get_register(rd) & 0xFFFF) | (instr->ImmedMovwMovtValue() << 16);
          set_register(rd, alu_out);
        }
        break;
      }

      case CMN: {
        if (instr->HasS()) {
          // Format(instr, "cmn'cond 'rn, 'shift_rm");
          // Format(instr, "cmn'cond 'rn, 'imm");
          alu_out = base::AddWithWraparound(rn_val, shifter_operand);
          SetNZFlags(alu_out);
          SetCFlag(CarryFrom(rn_val, shifter_operand));
          SetVFlag(OverflowFrom(alu_out, rn_val, shifter_operand, true));
        } else {
          // Other instructions matching this pattern are handled in the
          // miscellaneous instructions part above.
          UNREACHABLE();
        }
        break;
      }

      case ORR: {
        // Format(instr, "orr'cond's 'rd, 'rn, 'shift_rm");
        // Format(instr, "orr'cond's 'rd, 'rn, 'imm");
        alu_out = rn_val | shifter_operand;
        set_register(rd, alu_out);
        if (instr->HasS()) {
          SetNZFlags(alu_out);
          SetCFlag(shifter_carry_out);
        }
        break;
      }

      case MOV: {
        // Format(instr, "mov'cond's 'rd, 'shift_rm");
        // Format(instr, "mov'cond's 'rd, 'imm");
        alu_out = shifter_operand;
        set_register(rd, alu_out);
        if (instr->HasS()) {
          SetNZFlags(alu_out);
          SetCFlag(shifter_carry_out);
        }
        break;
      }

      case BIC: {
        // Format(instr, "bic'cond's 'rd, 'rn, 'shift_rm");
        // Format(instr, "bic'cond's 'rd, 'rn, 'imm");
        alu_out = rn_val & ~shifter_operand;
        set_register(rd, alu_out);
        if (instr->HasS()) {
          SetNZFlags(alu_out);
          SetCFlag(shifter_carry_out);
        }
        break;
      }

      case MVN: {
        // Format(instr, "mvn'cond's 'rd, 'shift_rm");
        // Format(instr, "mvn'cond's 'rd, 'imm");
        alu_out = ~shifter_operand;
        set_register(rd, alu_out);
        if (instr->HasS()) {
          SetNZFlags(alu_out);
          SetCFlag(shifter_carry_out);
        }
        break;
      }

      default: {
        UNREACHABLE();
      }
    }
  }
}

void Simulator::DecodeType2(Instruction* instr) {
  int rd = instr->RdValue();
  int rn = instr->RnValue();
  int32_t rn_val = get_register(rn);
  int32_t im_val = instr->Offset12Value();
  int32_t addr = 0;
  switch (instr->PUField()) {
    case da_x: {
      // Format(instr, "'memop'cond'b 'rd, ['rn], #-'off12");
      DCHECK(!instr->HasW());
      addr = rn_val;
      rn_val -= im_val;
      set_register(rn, rn_val);
      break;
    }
    case ia_x: {
      // Format(instr, "'memop'cond'b 'rd, ['rn], #+'off12");
      DCHECK(!instr->HasW());
      addr = rn_val;
      rn_val += im_val;
      set_register(rn, rn_val);
      break;
    }
    case db_x: {
      // Format(instr, "'memop'cond'b 'rd, ['rn, #-'off12]'w");
      rn_val -= im_val;
      addr = rn_val;
      if (instr->HasW()) {
        set_register(rn, rn_val);
      }
      break;
    }
    case ib_x: {
      // Format(instr, "'memop'cond'b 'rd, ['rn, #+'off12]'w");
      rn_val += im_val;
      addr = rn_val;
      if (instr->HasW()) {
        set_register(rn, rn_val);
      }
      break;
    }
    default: {
      UNREACHABLE();
    }
  }
  if (instr->HasB()) {
    if (instr->HasL()) {
      uint8_t val = ReadBU(addr);
      set_register(rd, val);
    } else {
      uint8_t val = get_register(rd);
      WriteB(addr, val);
    }
  } else {
    if (instr->HasL()) {
      set_register(rd, ReadW(addr));
    } else {
      WriteW(addr, get_register(rd));
    }
  }
}

void Simulator::DecodeType3(Instruction* instr) {
  int rd = instr->RdValue();
  int rn = instr->RnValue();
  int32_t rn_val = get_register(rn);
  bool shifter_carry_out = false;
  int32_t shifter_operand = GetShiftRm(instr, &shifter_carry_out);
  int32_t addr = 0;
  switch (instr->PUField()) {
    case da_x: {
      DCHECK(!instr->HasW());
      Format(instr, "'memop'cond'b 'rd, ['rn], -'shift_rm");
      UNIMPLEMENTED();
    }
    case ia_x: {
      if (instr->Bit(4) == 0) {
        // Memop.
      } else {
        if (instr->Bit(5) == 0) {
          switch (instr->Bits(22, 21)) {
            case 0:
              if (instr->Bit(20) == 0) {
                if (instr->Bit(6) == 0) {
                  // Pkhbt.
                  uint32_t rn_val = get_register(rn);
                  uint32_t rm_val = get_register(instr->RmValue());
                  int32_t shift = instr->Bits(11, 7);
                  rm_val <<= shift;
                  set_register(rd, (rn_val & 0xFFFF) | (rm_val & 0xFFFF0000U));
                } else {
                  // Pkhtb.
                  uint32_t rn_val = get_register(rn);
                  int32_t rm_val = get_register(instr->RmValue());
                  int32_t shift = instr->Bits(11, 7);
                  if (shift == 0) {
                    shift = 32;
                  }
                  rm_val >>= shift;
                  set_register(rd, (rn_val & 0xFFFF0000U) | (rm_val & 0xFFFF));
                }
              } else {
                UNIMPLEMENTED();
              }
              break;
            case 1:
              UNIMPLEMENTED();
            case 2:
              UNIMPLEMENTED();
            case 3: {
              // Usat.
              int32_t sat_pos = instr->Bits(20, 16);
              int32_t sat_val = (1 << sat_pos) - 1;
              int32_t shift = instr->Bits(11, 7);
              int32_t shift_type = instr->Bit(6);
              int32_t rm_val = get_register(instr->RmValue());
              if (shift_type == 0) {  // LSL
                rm_val <<= shift;
              } else {  // ASR
                rm_val >>= shift;
              }
              // If saturation occurs, the Q flag should be set in the CPSR.
              // There is no Q flag yet, and no instruction (MRS) to read the
              // CPSR directly.
              if (rm_val > sat_val) {
                rm_val = sat_val;
              } else if (rm_val < 0) {
                rm_val = 0;
              }
              set_register(rd, rm_val);
              break;
            }
          }
        } else {
          switch (instr->Bits(22, 21)) {
            case 0:
              UNIMPLEMENTED();
            case 1:
              if (instr->Bits(9, 6) == 1) {
                if (instr->Bit(20) == 0) {
                  if (instr->Bits(19, 16) == 0xF) {
                    // Sxtb.
                    int32_t rm_val = get_register(instr->RmValue());
                    int32_t rotate = instr->Bits(11, 10);
                    switch (rotate) {
                      case 0:
                        break;
                      case 1:
                        rm_val = (rm_val >> 8) | (rm_val << 24);
                        break;
                      case 2:
                        rm_val = (rm_val >> 16) | (rm_val << 16);
                        break;
                      case 3:
                        rm_val = (rm_val >> 24) | (rm_val << 8);
                        break;
                    }
                    set_register(rd, static_cast<int8_t>(rm_val));
                  } else {
                    // Sxtab.
                    int32_t rn_val = get_register(rn);
                    int32_t rm_val = get_register(instr->RmValue());
                    int32_t rotate = instr->Bits(11, 10);
                    switch (rotate) {
                      case 0:
                        break;
                      case 1:
                        rm_val = (rm_val >> 8) | (rm_val << 24);
                        break;
                      case 2:
                        rm_val = (rm_val >> 16) | (rm_val << 16);
                        break;
                      case 3:
                        rm_val = (rm_val >> 24) | (rm_val << 8);
                        break;
                    }
                    set_register(rd, rn_val + static_cast<int8_t>(rm_val));
                  }
                } else {
                  if (instr->Bits(19, 16) == 0xF) {
                    // Sxth.
                    int32_t rm_val = get_register(instr->RmValue());
                    int32_t rotate = instr->Bits(11, 10);
                    switch (rotate) {
                      case 0:
                        break;
                      case 1:
                        rm_val = (rm_val >> 8) | (rm_val << 24);
                        break;
                      case 2:
                        rm_val = (rm_val >> 16) | (rm_val << 16);
                        break;
                      case 3:
                        rm_val = (rm_val >> 24) | (rm_val << 8);
                        break;
                    }
                    set_register(rd, static_cast<int16_t>(rm_val));
                  } else {
                    // Sxtah.
                    int32_t rn_val = get_register(rn);
                    int32_t rm_val = get_register(instr->RmValue());
                    int32_t rotate = instr->Bits(11, 10);
                    switch (rotate) {
                      case 0:
                        break;
                      case 1:
                        rm_val = (rm_val >> 8) | (rm_val << 24);
                        break;
                      case 2:
                        rm_val = (rm_val >> 16) | (rm_val << 16);
                        break;
                      case 3:
                        rm_val = (rm_val >> 24) | (rm_val << 8);
                        break;
                    }
                    set_register(rd, rn_val + static_cast<int16_t>(rm_val));
                  }
                }
              } else if (instr->Bits(27, 16) == 0x6BF &&
                         instr->Bits(11, 4) == 0xF3) {
                // Rev.
                uint32_t rm_val = get_register(instr->RmValue());
                set_register(rd, ByteReverse(rm_val));
              } else {
                UNREACHABLE();
              }
              break;
            case 2:
              if ((instr->Bit(20) == 0) && (instr->Bits(9, 6) == 1)) {
                if (instr->Bits(19, 16) == 0xF) {
                  // Uxtb16.
                  uint32_t rm_val = get_register(instr->RmValue());
                  int32_t rotate = instr->Bits(11, 10);
                  switch (rotate) {
                    case 0:
                      break;
                    case 1:
                      rm_val = (rm_val >> 8) | (rm_val << 24);
                      break;
                    case 2:
                      rm_val = (rm_val >> 16) | (rm_val << 16);
                      break;
                    case 3:
                      rm_val = (rm_val >> 24) | (rm_val << 8);
                      break;
                  }
                  set_register(rd, (rm_val & 0xFF) | (rm_val & 0xFF0000));
                } else {
                  UNIMPLEMENTED();
                }
              } else {
                UNIMPLEMENTED();
              }
              break;
            case 3:
              if ((instr->Bits(9, 6) == 1)) {
                if (instr->Bit(20) == 0) {
                  if (instr->Bits(19, 16) == 0xF) {
                    // Uxtb.
                    uint32_t rm_val = get_register(instr->RmValue());
                    int32_t rotate = instr->Bits(11, 10);
                    switch (rotate) {
                      case 0:
                        break;
                      case 1:
                        rm_val = (rm_val >> 8) | (rm_val << 24);
                        break;
                      case 2:
                        rm_val = (rm_val >> 16) | (rm_val << 16);
                        break;
                      case 3:
                        rm_val = (rm_val >> 24) | (rm_val << 8);
                        break;
                    }
                    set_register(rd, (rm_val & 0xFF));
                  } else {
                    // Uxtab.
                    uint32_t rn_val = get_register(rn);
                    uint32_t rm_val = get_register(instr->RmValue());
                    int32_t rotate = instr->Bits(11, 10);
                    switch (rotate) {
                      case 0:
                        break;
                      case 1:
                        rm_val = (rm_val >> 8) | (rm_val << 24);
                        break;
                      case 2:
                        rm_val = (rm_val >> 16) | (rm_val << 16);
                        break;
                      case 3:
                        rm_val = (rm_val >> 24) | (rm_val << 8);
                        break;
                    }
                    set_register(rd, rn_val + (rm_val & 0xFF));
                  }
                } else {
                  if (instr->Bits(19, 16) == 0xF) {
                    // Uxth.
                    uint32_t rm_val = get_register(instr->RmValue());
                    int32_t rotate = instr->Bits(11, 10);
                    switch (rotate) {
                      case 0:
                        break;
                      case 1:
                        rm_val = (rm_val >> 8) | (rm_val << 24);
                        break;
                      case 2:
                        rm_val = (rm_val >> 16) | (rm_val << 16);
                        break;
                      case 3:
                        rm_val = (rm_val >> 24) | (rm_val << 8);
                        break;
                    }
                    set_register(rd, (rm_val & 0xFFFF));
                  } else {
                    // Uxtah.
                    uint32_t rn_val = get_register(rn);
                    uint32_t rm_val = get_register(instr->RmValue());
                    int32_t rotate = instr->Bits(11, 10);
                    switch (rotate) {
                      case 0:
                        break;
                      case 1:
                        rm_val = (rm_val >> 8) | (rm_val << 24);
                        break;
                      case 2:
                        rm_val = (rm_val >> 16) | (rm_val << 16);
                        break;
                      case 3:
                        rm_val = (rm_val >> 24) | (rm_val << 8);
                        break;
                    }
                    set_register(rd, rn_val + (rm_val & 0xFFFF));
                  }
                }
              } else {
                // PU == 0b01, BW == 0b11, Bits(9, 6) != 0b0001
                if ((instr->Bits(20, 16) == 0x1F) &&
                    (instr->Bits(11, 4) == 0xF3)) {
                  // Rbit.
                  uint32_t rm_val = get_register(instr->RmValue());
                  set_register(rd, base::bits::ReverseBits(rm_val));
                } else {
                  UNIMPLEMENTED();
                }
              }
              break;
          }
        }
        return;
      }
      break;
    }
    case db_x: {
      if (instr->Bits(22, 20) == 0x5) {
        if (instr->Bits(7, 4) == 0x1) {
          int rm = instr->RmValue();
          int32_t rm_val = get_register(rm);
          int rs = instr->RsValue();
          int32_t rs_val = get_register(rs);
          if (instr->Bits(15, 12) == 0xF) {
            // SMMUL (in V8 notation matching ARM ISA format)
            // Format(instr, "smmul'cond 'rn, 'rm, 'rs");
            rn_val = base::bits::SignedMulHigh32(rm_val, rs_val);
          } else {
            // SMMLA (in V8 notation matching ARM ISA format)
            // Format(instr, "smmla'cond 'rn, 'rm, 'rs, 'rd");
            int rd = instr->RdValue();
            int32_t rd_val = get_register(rd);
            rn_val = base::bits::SignedMulHighAndAdd32(rm_val, rs_val, rd_val);
          }
          set_register(rn, rn_val);
          return;
        }
      }
      if (instr->Bits(5, 4) == 0x1) {
        if ((instr->Bit(22) == 0x0) && (instr->Bit(20) == 0x1)) {
          // (s/u)div (in V8 notation matching ARM ISA format) rn = rm/rs
          // Format(instr, "'(s/u)div'cond'b 'rn, 'rm, 'rs);
          int rm = instr->RmValue();
          int32_t rm_val = get_register(rm);
          int rs = instr->RsValue();
          int32_t rs_val = get_register(rs);
          int32_t ret_val = 0;
          // udiv
          if (instr->Bit(21) == 0x1) {
            ret_val = base::bit_cast<int32_t>(
                base::bits::UnsignedDiv32(base::bit_cast<uint32_t>(rm_val),
                                          base::bit_cast<uint32_t>(rs_val)));
          } else {
            ret_val = base::bits::SignedDiv32(rm_val, rs_val);
          }
          set_register(rn, ret_val);
          return;
        }
      }
      // Format(instr, "'memop'cond'b 'rd, ['rn, -'shift_rm]'w");
      addr = rn_val - shifter_operand;
      if (instr->HasW()) {
        set_register(rn, addr);
      }
      break;
    }
    case ib_x: {
      if (instr->HasW() && (instr->Bits(6, 4) == 0x5)) {
        uint32_t widthminus1 = static_cast<uint32_t>(instr->Bits(20, 16));
        uint32_t lsbit = static_cast<uint32_t>(instr->Bits(11, 7));
        uint32_t msbit = widthminus1 + lsbit;
        if (msbit <= 31) {
          if (instr->Bit(22)) {
            // ubfx - unsigned bitfield extract.
            uint32_t rm_val =
                static_cast<uint32_t>(get_register(instr->RmValue()));
            uint32_t extr_val = rm_val << (31 - msbit);
            extr_val = extr_val >> (31 - widthminus1);
            set_register(instr->RdValue(), extr_val);
          } else {
            // sbfx - signed bitfield extract.
            int32_t rm_val = get_register(instr->RmValue());
            int32_t extr_val = static_cast<uint32_t>(rm_val) << (31 - msbit);
            extr_val = extr_val >> (31 - widthminus1);
            set_register(instr->RdValue(), extr_val);
          }
        } else {
          UNREACHABLE();
        }
        return;
      } else if (!instr->HasW() && (instr->Bits(6, 4) == 0x1)) {
        uint32_t lsbit = static_cast<uint32_t>(instr->Bits(11, 7));
        uint32_t msbit = static_cast<uint32_t>(instr->Bits(20, 16));
        if (msbit >= lsbit) {
          // bfc or bfi - bitfield clear/insert.
          uint32_t rd_val =
              static_cast<uint32_t>(get_register(instr->RdValue()));
          uint32_t bitcount = msbit - lsbit + 1;
          uint32_t mask = 0xFFFFFFFFu >> (32 - bitcount);
          rd_val &= ~(mask << lsbit);
          if (instr->RmValue() != 15) {
            // bfi - bitfield insert.
            uint32_t rm_val =
                static_cast<uint32_t>(get_register(instr->RmValue()));
            rm_val &= mask;
            rd_val |= rm_val << lsbit;
          }
          set_register(instr->RdValue(), rd_val);
        } else {
          UNREACHABLE();
        }
        return;
      } else {
        // Format(instr, "'memop'cond'b 'rd, ['rn, +'shift_rm]'w");
        addr = base::AddWithWraparound(rn_val, shifter_operand);
        if (instr->HasW()) {
          set_register(rn, addr);
        }
      }
      break;
    }
    default: {
      UNREACHABLE();
    }
  }
  if (instr->HasB()) {
    if (instr->HasL()) {
      uint8_t byte = ReadB(addr);
      set_register(rd, byte);
    } else {
      uint8_t byte = get_register(rd);
      WriteB(addr, byte);
    }
  } else {
    if (instr->HasL()) {
      set_register(rd, ReadW(addr));
    } else {
      WriteW(addr, get_register(rd));
    }
  }
}

void Simulator::DecodeType4(Instruction* instr) {
  DCHECK_EQ(instr->Bit(22), 0);  // only allowed to be set in privileged mode
  if (instr->HasL()) {
    // Format(instr, "ldm'cond'pu 'rn'w, 'rlist");
    HandleRList(instr, true);
  } else {
    // Format(instr, "stm'cond'pu 'rn'w, 'rlist");
    HandleRList(instr, false);
  }
}

void Simulator::DecodeType5(Instruction* instr) {
  // Format(instr, "b'l'cond 'target");
  int off =
      static_cast<int>(static_cast<uint32_t>(instr->SImmed24Value()) << 2);
  intptr_t pc_address = get_pc();
  if (instr->HasLink()) {
    set_register(lr, pc_address + kInstrSize);
  }
  int pc_reg = get_register(pc);
  set_pc(pc_reg + off);
}

void Simulator::DecodeType6(Instruction* instr) {
  DecodeType6CoprocessorIns(instr);
}

void Simulator::DecodeType7(Instruction* instr) {
  if (instr->Bit(24) == 1) {
    SoftwareInterrupt(instr);
  } else {
    switch (instr->CoprocessorValue()) {
      case 10:  // Fall through.
      case 11:
        DecodeTypeVFP(instr);
        break;
      case 15:
        DecodeTypeCP15(instr);
        break;
      default:
        UNIMPLEMENTED();
    }
  }
}

// void Simulator::DecodeTypeVFP(Instruction* instr)
// The Following ARMv7 VFPv instructions are currently supported.
// vmov :Sn = Rt
// vmov :Rt = Sn
// vcvt: Dd = Sm
// vcvt: Sd = Dm
// vcvt.f64.s32 Dd, Dd, #<fbits>
// Dd = vabs(Dm)
// Sd = vabs(Sm)
// Dd = vneg(Dm)
// Sd = vneg(Sm)
// Dd = vadd(Dn, Dm)
// Sd = vadd(Sn, Sm)
// Dd = vsub(Dn, Dm)
// Sd = vsub(Sn, Sm)
// Dd = vmul(Dn, Dm)
// Sd = vmul(Sn, Sm)
// Dd = vdiv(Dn, Dm)
// Sd = vdiv(Sn, Sm)
// vcmp(Dd, Dm)
// vcmp(Sd, Sm)
// Dd = vsqrt(Dm)
// Sd = vsqrt(Sm)
// vmrs
// vdup.size Qd, Rt.
void Simulator::DecodeTypeVFP(Instruction* instr) {
  DCHECK((instr->TypeValue() == 7) && (instr->Bit(24) == 0x0));
  DCHECK_EQ(instr->Bits(11, 9), 0x5);
  // Obtain single precision register codes.
  int m = instr->VFPMRegValue(kSinglePrecision);
  int d = instr->VFPDRegValue(kSinglePrecision);
  int n = instr->VFPNRegValue(kSinglePrecision);
  // Obtain double precision register codes.
  int vm = instr->VFPMRegValue(kDoublePrecision);
  int vd = instr->VFPDRegValue(kDoublePrecision);
  int vn = instr->VFPNRegValue(kDoublePrecision);

  if (instr->Bit(4) == 0) {
    if (instr->Opc1Value() == 0x7) {
      // Other data processing instructions
      if ((instr->Opc2Value() == 0x0) && (instr->Opc3Value() == 0x1)) {
        // vmov register to register.
        if (instr->SzValue() == 0x1) {
          uint32_t data[2];
          get_d_register(vm, data);
          set_d_register(vd, data);
        } else {
          set_s_register(d, get_s_register(m));
        }
      } else if ((instr->Opc2Value() == 0x0) && (instr->Opc3Value() == 0x3)) {
        // vabs
        if (instr->SzValue() == 0x1) {
          Float64 dm = get_double_from_d_register(vm);
          constexpr uint64_t kSignBit64 = uint64_t{1} << 63;
          Float64 dd = Float64::FromBits(dm.get_bits() & ~kSignBit64);
          dd = canonicalizeNaN(dd);
          set_d_register_from_double(vd, dd);
        } else {
          Float32 sm = get_float_from_s_register(m);
          constexpr uint32_t kSignBit32 = uint32_t{1} << 31;
          Float32 sd = Float32::FromBits(sm.get_bits() & ~kSignBit32);
          sd = canonicalizeNaN(sd);
          set_s_register_from_float(d, sd);
        }
      } else if ((instr->Opc2Value() == 0x1) && (instr->Opc3Value() == 0x1)) {
        // vneg
        if (instr->SzValue() == 0x1) {
          Float64 dm = get_double_from_d_register(vm);
          constexpr uint64_t kSignBit64 = uint64_t{1} << 63;
          Float64 dd = Float64::FromBits(dm.get_bits() ^ kSignBit64);
          dd = canonicalizeNaN(dd);
          set_d_register_from_double(vd, dd);
        } else {
          Float32 sm = get_float_from_s_register(m);
          constexpr uint32_t kSignBit32 = uint32_t{1} << 31;
          Float32 sd = Float32::FromBits(sm.get_bits() ^ kSignBit32);
          sd = canonicalizeNaN(sd);
          set_s_register_from_float(d, sd);
        }
      } else if ((instr->Opc2Value() == 0x7) && (instr->Opc3Value() == 0x3)) {
        DecodeVCVTBetweenDoubleAndSingle(instr);
      } else if ((instr->Opc2Value() == 0x8) && (instr->Opc3Value() & 0x1)) {
        DecodeVCVTBetweenFloatingPointAndInteger(instr);
      } else if ((instr->Opc2Value() == 0xA) && (instr->Opc3Value() == 0x3) &&
                 (instr->Bit(8) == 1)) {
        // vcvt.f64.s32 Dd, Dd, #<fbits>
        int fraction_bits = 32 - ((instr->Bits(3, 0) << 1) | instr->Bit(5));
        int fixed_value = get_sinteger_from_s_register(vd * 2);
        double divide = 1 << fraction_bits;
        set_d_register_from_double(vd, fixed_value / divide);
      } else if (((instr->Opc2Value() >> 1) == 0x6) &&
                 (instr->Opc3Value() & 0x1)) {
        DecodeVCVTBetweenFloatingPointAndInteger(instr);
      } else if (((instr->Opc2Value() == 0x4) || (instr->Opc2Value() == 0x5)) &&
                 (instr->Opc3Value() & 0x1)) {
        DecodeVCMP(instr);
      } else if (((instr->Opc2Value() == 0x1)) && (instr->Opc3Value() == 0x3)) {
        // vsqrt
        if (instr->SzValue() == 0x1) {
          double dm_value = get_double_from_d_register(vm).get_scalar();
          double dd_value = std::sqrt(dm_value);
          dd_value = canonicalizeNaN(dd_value);
          set_d_register_from_double(vd, dd_value);
        } else {
          float sm_value = get_float_from_s_register(m).get_scalar();
          float sd_value = std::sqrt(sm_value);
          sd_value = canonicalizeNaN(sd_value);
          set_s_register_from_float(d, sd_value);
        }
      } else if (instr->Opc3Value() == 0x0) {
        // vmov immediate.
        if (instr->SzValue() == 0x1) {
          set_d_register_from_double(vd, instr->DoubleImmedVmov());
        } else {
          // Cast double to float.
          float value = instr->DoubleImmedVmov().get_scalar();
          set_s_register_from_float(d, value);
        }
      } else if (((instr->Opc2Value() == 0x6)) && (instr->Opc3Value() == 0x3)) {
        // vrintz - truncate
        if (instr->SzValue() == 0x1) {
          double dm_value = get_double_from_d_register(vm).get_scalar();
          double dd_value = trunc(dm_value);
          dd_value = canonicalizeNaN(dd_value);
          set_d_register_from_double(vd, dd_value);
        } else {
          float sm_value = get_float_from_s_register(m).get_scalar();
          float sd_value = truncf(sm_value);
          sd_value = canonicalizeNaN(sd_value);
          set_s_register_from_float(d, sd_value);
        }
      } else {
        UNREACHABLE();  // Not used by V8.
      }
    } else if (instr->Opc1Value() == 0x3) {
      if (instr->Opc3Value() & 0x1) {
        // vsub
        if (instr->SzValue() == 0x1) {
          double dn_value = get_double_from_d_register(vn).get_scalar();
          double dm_value = get_double_from_d_register(vm).get_scalar();
          double dd_value = dn_value - dm_value;
          dd_value = canonicalizeNaN(dd_value);
          set_d_register_from_double(vd, dd_value);
        } else {
          float sn_value = get_float_from_s_register(n).get_scalar();
          float sm_value = get_float_from_s_register(m).get_scalar();
          float sd_value = sn_value - sm_value;
          sd_value = canonicalizeNaN(sd_value);
          set_s_register_from_float(d, sd_value);
        }
      } else {
        // vadd
        if (instr->SzValue() == 0x1) {
          double dn_value = get_double_from_d_register(vn).get_scalar();
          double dm_value = get_double_from_d_register(vm).get_scalar();
          double dd_value = dn_value + dm_value;
          dd_value = canonicalizeNaN(dd_value);
          set_d_register_from_double(vd, dd_value);
        } else {
          float sn_value = get_float_from_s_register(n).get_scalar();
          float sm_value = get_float_from_s_register(m).get_scalar();
          float sd_value = sn_value + sm_value;
          sd_value = canonicalizeNaN(sd_value);
          set_s_register_from_float(d, sd_value);
        }
      }
    } else if ((instr->Opc1Value() == 0x2) && !(instr->Opc3Value() & 0x1)) {
      // vmul
      if (instr->SzValue() == 0x1) {
        double dn_value = get_double_from_d_register(vn).get_scalar();
        double dm_value = get_double_from_d_register(vm).get_scalar();
        double dd_value = dn_value * dm_value;
        dd_value = canonicalizeNaN(dd_value);
        set_d_register_from_double(vd, dd_value);
      } else {
        float sn_value = get_float_from_s_register(n).get_scalar();
        float sm_value = get_float_from_s_register(m).get_scalar();
        float sd_value = sn_value * sm_value;
        sd_value = canonicalizeNaN(sd_value);
        set_s_register_from_float(d, sd_value);
      }
    } else if ((instr->Opc1Value() == 0x0)) {
      // vmla, vmls
      const bool is_vmls = (instr->Opc3Value() & 0x1);
      if (instr->SzValue() == 0x1) {
        const double dd_val = get_double_from_d_register(vd).get_scalar();
        const double dn_val = get_double_from_d_register(vn).get_scalar();
        const double dm_val = get_double_from_d_register(vm).get_scalar();

        // Note: we do the mul and add/sub in separate steps to avoid getting a
        // result with too high precision.
        const double res = dn_val * dm_val;
        set_d_register_from_double(vd, res);
        if (is_vmls) {
          set_d_register_from_double(vd, canonicalizeNaN(dd_val - res));
        } else {
          set_d_register_from_double(vd, canonicalizeNaN(dd_val + res));
        }
      } else {
        const float sd_val = get_float_from_s_register(d).get_scalar();
        const float sn_val = get_float_from_s_register(n).get_scalar();
        const float sm_val = get_float_from_s_register(m).get_scalar();

        // Note: we do the mul and add/sub in separate steps to avoid getting a
        // result with too high precision.
        const float res = sn_val * sm_val;
        set_s_register_from_float(d, res);
        if (is_vmls) {
          set_s_register_from_float(d, canonicalizeNaN(sd_val - res));
        } else {
          set_s_register_from_float(d, canonicalizeNaN(sd_val + res));
        }
      }
    } else if ((instr->Opc1Value() == 0x4) && !(instr->Opc3Value() & 0x1)) {
      // vdiv
      if (instr->SzValue() == 0x1) {
        double dn_value = get_double_from_d_register(vn).get_scalar();
        double dm_value = get_double_from_d_register(vm).get_scalar();
        double dd_value = base::Divide(dn_value, dm_value);
        div_zero_vfp_flag_ = (dm_value == 0);
        dd_value = canonicalizeNaN(dd_value);
        set_d_register_from_double(vd, dd_value);
      } else {
        float sn_value = get_float_from_s_register(n).get_scalar();
        float sm_value = get_float_from_s_register(m).get_scalar();
        float sd_value = base::Divide(sn_value, sm_value);
        div_zero_vfp_flag_ = (sm_value == 0);
        sd_value = canonicalizeNaN(sd_value);
        set_s_register_from_float(d, sd_value);
      }
    } else {
      UNIMPLEMENTED();  // Not used by V8.
    }
  } else {
    if ((instr->VCValue() == 0x0) && (instr->VAValue() == 0x0)) {
      DecodeVMOVBetweenCoreAndSinglePrecisionRegisters(instr);
    } else if ((instr->VLValue() == 0x0) && (instr->VCValue() == 0x1)) {
      if (instr->Bit(23) == 0) {
        // vmov (ARM core register to scalar)
        int vd = instr->VFPNRegValue(kDoublePrecision);
        int rt = instr->RtValue();
        int opc1_opc2 = (instr->Bits(22, 21) << 2) | instr->Bits(6, 5);
        if ((opc1_opc2 & 0xB) == 0) {
          // NeonS32/NeonU32
          uint32_t data[2];
          get_d_register(vd, data);
          data[instr->Bit(21)] = get_register(rt);
          set_d_register(vd, data);
        } else {
          uint64_t data;
          get_d_register(vd, &data);
          uint64_t rt_value = get_register(rt);
          if ((opc1_opc2 & 0x8) != 0) {
            // NeonS8 / NeonU8
            int i = opc1_opc2 & 0x7;
            int shift = i * kBitsPerByte;
            const uint64_t mask = 0xFF;
            data &= ~(mask << shift);
            data |= (rt_value & mask) << shift;
            set_d_register(vd, &data);
          } else if ((opc1_opc2 & 0x1) != 0) {
            // NeonS16 / NeonU16
            int i = (opc1_opc2 >> 1) & 0x3;
            int shift = i * kBitsPerByte * kShortSize;
            const uint64_t mask = 0xFFFF;
            data &= ~(mask << shift);
            data |= (rt_value & mask) << shift;
            set_d_register(vd, &data);
          } else {
            UNREACHABLE();  // Not used by V8.
          }
        }
      } else {
        // vdup.size Qd, Rt.
        NeonSize size = Neon32;
        if (instr->Bit(5) != 0)
          size = Neon16;
        else if (instr->Bit(22) != 0)
          size = Neon8;
        int vd = instr->VFPNRegValue(kSimd128Precision);
        int rt = instr->RtValue();
        uint32_t rt_value = get_register(rt);
        uint32_t q_data[4];
        switch (size) {
          case Neon8: {
            rt_value &= 0xFF;
            uint8_t* dst = reinterpret_cast<uint8_t*>(q_data);
            for (int i = 0; i < 16; i++) {
              dst[i] = rt_value;
            }
            break;
          }
          case Neon16: {
            // Perform pairwise op.
            rt_value &= 0xFFFFu;
            uint32_t rt_rt = (rt_value << 16) | (rt_value & 0xFFFFu);
            for (int i = 0; i < 4; i++) {
              q_data[i] = rt_rt;
            }
            break;
          }
          case Neon32: {
            for (int i = 0; i < 4; i++) {
              q_data[i] = rt_value;
            }
            break;
          }
          default:
            UNREACHABLE();
        }
        set_neon_register(vd, q_data);
      }
    } else if ((instr->VLValue() == 0x1) && (instr->VCValue() == 0x1)) {
      // vmov (scalar to ARM core register)
      int vn = instr->VFPNRegValue(kDoublePrecision);
      int rt = instr->RtValue();
      int opc1_opc2 = (instr->Bits(22, 21) << 2) | instr->Bits(6, 5);
      uint64_t data;
      get_d_register(vn, &data);
      if ((opc1_opc2 & 0xB) == 0) {
        // NeonS32 / NeonU32
        DCHECK_EQ(0, instr->Bit(23));
        int32_t int_data[2];
        memcpy(int_data, &data, sizeof(int_data));
        set_register(rt, int_data[instr->Bit(21)]);
      } else {
        uint64_t data;
        get_d_register(vn, &data);
        bool u = instr->Bit(23) != 0;
        if ((opc1_opc2 & 0x8) != 0) {
          // NeonS8 / NeonU8
          int i = opc1_opc2 & 0x7;
          int shift = i * kBitsPerByte;
          uint32_t scalar = (data >> shift) & 0xFFu;
          if (!u && (scalar & 0x80) != 0) scalar |= 0xFFFFFF00;
          set_register(rt, scalar);
        } else if ((opc1_opc2 & 0x1) != 0) {
          // NeonS16 / NeonU16
          int i = (opc1_opc2 >> 1) & 0x3;
          int shift = i * kBitsPerByte * kShortSize;
          uint32_t scalar = (data >> shift) & 0xFFFFu;
          if (!u && (scalar & 0x8000) != 0) scalar |= 0xFFFF0000;
          set_register(rt, scalar);
        } else {
          UNREACHABLE();  // Not used by V8.
        }
      }
    } else if ((instr->VLValue() == 0x1) && (instr->VCValue() == 0x0) &&
               (instr->VAValue() == 0x7) && (instr->Bits(19, 16) == 0x1)) {
      // vmrs
      uint32_t rt = instr->RtValue();
      if (rt == 0xF) {
        Copy_FPSCR_to_APSR();
      } else {
        // Emulate FPSCR from the Simulator flags.
        uint32_t fpscr = (n_flag_FPSCR_ << 31) | (z_flag_FPSCR_ << 30) |
                         (c_flag_FPSCR_ << 29) | (v_flag_FPSCR_ << 28) |
                         (FPSCR_default_NaN_mode_ << 25) |
                         (inexact_vfp_flag_ << 4) | (underflow_vfp_flag_ << 3) |
                         (overflow_vfp_flag_ << 2) | (div_zero_vfp_flag_ << 1) |
                         (inv_op_vfp_flag_ << 0) | (FPSCR_rounding_mode_);
        set_register(rt, fpscr);
      }
    } else if ((instr->VLValue() == 0x0) && (instr->VCValue() == 0x0) &&
               (instr->VAValue() == 0x7) && (instr->Bits(19, 16) == 0x1)) {
      // vmsr
      uint32_t rt = instr->RtValue();
      if (rt == pc) {
        UNREACHABLE();
      } else {
        uint32_t rt_value = get_register(rt);
        n_flag_FPSCR_ = (rt_value >> 31) & 1;
        z_flag_FPSCR_ = (rt_value >> 30) & 1;
        c_flag_FPSCR_ = (rt_value >> 29) & 1;
        v_flag_FPSCR_ = (rt_value >> 28) & 1;
        FPSCR_default_NaN_mode_ = (rt_value >> 25) & 1;
        inexact_vfp_flag_ = (rt_value >> 4) & 1;
        underflow_vfp_flag_ = (rt_value >> 3) & 1;
        overflow_vfp_flag_ = (rt_value >> 2) & 1;
        div_zero_vfp_flag_ = (rt_value >> 1) & 1;
        inv_op_vfp_flag_ = (rt_value >> 0) & 1;
        FPSCR_rounding_mode_ =
            static_cast<VFPRoundingMode>((rt_value)&kVFPRoundingModeMask);
      }
    } else {
      UNIMPLEMENTED();  // Not used by V8.
    }
  }
}

void Simulator::DecodeTypeCP15(Instruction* instr) {
  DCHECK((instr->TypeValue() == 7) && (instr->Bit(24) == 0x0));
  DCHECK_EQ(instr->CoprocessorValue(), 15);

  if (instr->Bit(4) == 1) {
    // mcr
    int crn = instr->Bits(19, 16);
    int crm = instr->Bits(3, 0);
    int opc1 = instr->Bits(23, 21);
    int opc2 = instr->Bits(7, 5);
    if ((opc1 == 0) && (crn == 7)) {
      // ARMv6 memory barrier operations.
      // Details available in ARM DDI 0406C.b, B3-1750.
      if (((crm == 10) && (opc2 == 5)) ||  // CP15DMB
          ((crm == 10) && (opc2 == 4)) ||  // CP15DSB
          ((crm == 5) && (opc2 == 4))) {   // CP15ISB
        // These are ignored by the simulator for now.
      } else {
        UNIMPLEMENTED();
      }
    }
  } else {
    UNIMPLEMENTED();
  }
}

void Simulator::DecodeVMOVBetweenCoreAndSinglePrecisionRegisters(
    Instruction* instr) {
  DCHECK((instr->Bit(4) == 1) && (instr->VCValue() == 0x0) &&
         (instr->VAValue() == 0x0));

  int t = instr->RtValue();
  int n = instr->VFPNRegValue(kSinglePrecision);
  bool to_arm_register = (instr->VLValue() == 0x1);

  if (to_arm_register) {
    int32_t int_value = get_sinteger_from_s_register(n);
    set_register(t, int_value);
  } else {
    int32_t rs_val = get_register(t);
    set_s_register_from_sinteger(n, rs_val);
  }
}

void Simulator::DecodeVCMP(Instruction* instr) {
  DCHECK((instr->Bit(4) == 0) && (instr->Opc1Value() == 0x7));
  DCHECK(((instr->Opc2Value() == 0x4) || (instr->Opc2Value() == 0x5)) &&
         (instr->Opc3Value() & 0x1));
  // Comparison.

  VFPRegPrecision precision = kSinglePrecision;
  if (instr->SzValue() == 0x1) {
    precision = kDoublePrecision;
  }

  int d = instr->VFPDRegValue(precision);
  int m = 0;
  if (instr->Opc2Value() == 0x4) {
    m = instr->VFPMRegValue(precision);
  }

  if (precision == kDoublePrecision) {
    double dd_value = get_double_from_d_register(d).get_scalar();
    double dm_value = 0.0;
    if (instr->Opc2Value() == 0x4) {
      dm_value = get_double_from_d_register(m).get_scalar();
    }

    // Raise exceptions for quiet NaNs if necessary.
    if (instr->Bit(7) == 1) {
      if (std::isnan(dd_value)) {
        inv_op_vfp_flag_ = true;
      }
    }

    Compute_FPSCR_Flags(dd_value, dm_value);
  } else {
    float sd_value = get_float_from_s_register(d).get_scalar();
    float sm_value = 0.0;
    if (instr->Opc2Value() == 0x4) {
      sm_value = get_float_from_s_register(m).get_scalar();
    }

    // Raise exceptions for quiet NaNs if necessary.
    if (instr->Bit(7) == 1) {
      if (std::isnan(sd_value)) {
        inv_op_vfp_flag_ = true;
      }
    }

    Compute_FPSCR_Flags(sd_value, sm_value);
  }
}

void Simulator::DecodeVCVTBetweenDoubleAndSingle(Instruction* instr) {
  DCHECK((instr->Bit(4) == 0) && (instr->Opc1Value() == 0x7));
  DCHECK((instr->Opc2Value() == 0x7) && (instr->Opc3Value() == 0x3));

  VFPRegPrecision dst_precision = kDoublePrecision;
  VFPRegPrecision src_precision = kSinglePrecision;
  if (instr->SzValue() == 1) {
    dst_precision = kSinglePrecision;
    src_precision = kDoublePrecision;
  }

  int dst = instr->VFPDRegValue(dst_precision);
  int src = instr->VFPMRegValue(src_precision);

  if (dst_precision == kSinglePrecision) {
    double val = get_double_from_d_register(src).get_scalar();
    set_s_register_from_float(dst, static_cast<float>(val));
  } else {
    float val = get_float_from_s_register(src).get_scalar();
    set_d_register_from_double(dst, static_cast<double>(val));
  }
}

bool get_inv_op_vfp_flag(VFPRoundingMode mode, double val, bool unsigned_) {
  DCHECK((mode == RN) || (mode == RM) || (mode == RZ));
  double max_uint = static_cast<double>(0xFFFFFFFFu);
  double max_int = static_cast<double>(kMaxInt);
  double min_int = static_cast<double>(kMinInt);

  // Check for NaN.
  if (val != val) {
    return true;
  }

  // Check for overflow. This code works because 32bit integers can be
  // exactly represented by ieee-754 64bit floating-point values.
  switch (mode) {
    case RN:
      return unsigned_ ? (val >= (max_uint + 0.5)) || (val < -0.5)
                       : (val >= (max_int + 0.5)) || (val < (min_int - 0.5));

    case RM:
      return unsigned_ ? (val >= (max_uint + 1.0)) || (val < 0)
                       : (val >= (max_int + 1.0)) || (val < min_int);

    case RZ:
      return unsigned_ ? (val >= (max_uint + 1.0)) || (val <= -1)
                       : (val >= (max_int + 1.0)) || (val <= (min_int - 1.0));
    default:
      UNREACHABLE();
  }
}

// We call this function only if we had a vfp invalid exception.
// It returns the correct saturated value.
int VFPConversionSaturate(double val, bool unsigned_res) {
  if (val != val) {
    return 0;
  } else {
    if (unsigned_res) {
      return (val < 0) ? 0 : 0xFFFFFFFFu;
    } else {
      return (val < 0) ? kMinInt : kMaxInt;
    }
  }
}

int32_t Simulator::ConvertDoubleToInt(double val, bool unsigned_integer,
                                      VFPRoundingMode mode) {
  int32_t result;
  if (unsigned_integer) {
    // The FastD2UI helper does not have the rounding behavior we want here
    // (it doesn't guarantee any particular rounding, and it doesn't check
    // for or handle overflow), so do the conversion by hand.
    using limits = std::numeric_limits<uint32_t>;
    if (val > limits::max()) {
      result = limits::max();
    } else if (!(val >= 0)) {  // Negation to catch NaNs.
      result = 0;
    } else {
      result = static_cast<uint32_t>(val);
    }
  } else {
    result = FastD2IChecked(val);
  }

  inv_op_vfp_flag_ = get_inv_op_vfp_flag(mode, val, unsigned_integer);

  double abs_diff = unsigned_integer
                        ? std::fabs(val - static_cast<uint32_t>(result))
                        : std::fabs(val - result);

  inexact_vfp_flag_ = (abs_diff != 0);

  if (inv_op_vfp_flag_) {
    result = VFPConversionSaturate(val, unsigned_integer);
  } else {
    switch (mode) {
      case RN: {
        int val_sign = (val > 0) ? 1 : -1;
        if (abs_diff > 0.5) {
          result += val_sign;
        } else if (abs_diff == 0.5) {
          // Round to even if exactly halfway.
          result = ((result % 2) == 0)
                       ? result
                       : base::AddWithWraparound(result, val_sign);
        }
        break;
      }

      case RM:
        result = result > val ? result - 1 : result;
        break;

      case RZ:
        // Nothing to do.
        break;

      default:
        UNREACHABLE();
    }
  }
  return result;
}

void Simulator::DecodeVCVTBetweenFloatingPointAndInteger(Instruction* instr) {
  DCHECK((instr->Bit(4) == 0) && (instr->Opc1Value() == 0x7) &&
         (instr->Bits(27, 23) == 0x1D));
  DCHECK(((instr->Opc2Value() == 0x8) && (instr->Opc3Value() & 0x1)) ||
         (((instr->Opc2Value() >> 1) == 0x6) && (instr->Opc3Value() & 0x1)));

  // Conversion between floating-point and integer.
  bool to_integer = (instr->Bit(18) == 1);

  VFPRegPrecision src_precision =
      (instr->SzValue() == 1) ? kDoublePrecision : kSinglePrecision;

  if (to_integer) {
    // We are playing with code close to the C++ standard's limits below,
    // hence the very simple code and heavy checks.
    //
    // Note:
    // C++ defines default type casting from floating point to integer as
    // (close to) rounding toward zero ("fractional part discarded").

    int dst = instr->VFPDRegValue(kSinglePrecision);
    int src = instr->VFPMRegValue(src_precision);

    // Bit 7 in vcvt instructions indicates if we should use the FPSCR rounding
    // mode or the default Round to Zero mode.
    VFPRoundingMode mode = (instr->Bit(7) != 1) ? FPSCR_rounding_mode_ : RZ;
    DCHECK((mode == RM) || (mode == RZ) || (mode == RN));

    bool unsigned_integer = (instr->Bit(16) == 0);
    bool double_precision = (src_precision == kDoublePrecision);

    double val = double_precision ? get_double_from_d_register(src).get_scalar()
                                  : get_float_from_s_register(src).get_scalar();

    int32_t temp = ConvertDoubleToInt(val, unsigned_integer, mode);

    // Update the destination register.
    set_s_register_from_sinteger(dst, temp);

  } else {
    bool unsigned_integer = (instr->Bit(7) == 0);

    int dst = instr->VFPDRegValue(src_precision);
    int src = instr->VFPMRegValue(kSinglePrecision);

    int val = get_sinteger_from_s_register(src);

    if (src_precision == kDoublePrecision) {
      if (unsigned_integer) {
        set_d_register_from_double(
            dst, static_cast<double>(static_cast<uint32_t>(val)));
      } else {
        set_d_register_from_double(dst, static_cast<double>(val));
      }
    } else {
      if (unsigned_integer) {
        set_s_register_from_float(
            dst, static_cast<float>(static_cast<uint32_t>(val)));
      } else {
        set_s_register_from_float(dst, static_cast<float>(val));
      }
    }
  }
}

// void Simulator::DecodeType6CoprocessorIns(Instruction* instr)
// Decode Type 6 coprocessor instructions.
// Dm = vmov(Rt, Rt2)
// <Rt, Rt2> = vmov(Dm)
// Ddst = MEM(Rbase + 4*offset).
// MEM(Rbase + 4*offset) = Dsrc.
void Simulator::DecodeType6CoprocessorIns(Instruction* instr) {
  DCHECK_EQ(instr->TypeValue(), 6);

  if (instr->CoprocessorValue() == 0xA) {
    switch (instr->OpcodeValue()) {
      case 0x8:
      case 0xA:
      case 0xC:
      case 0xE: {  // Load and store single precision float to memory.
        int rn = instr->RnValue();
        int vd = instr->VFPDRegValue(kSinglePrecision);
        int offset = instr->Immed8Value();
        if (!instr->HasU()) {
          offset = -offset;
        }

        int32_t address = get_register(rn) + 4 * offset;
        // Load and store address for singles must be at least four-byte
        // aligned.
        DCHECK_EQ(address % 4, 0);
        if (instr->HasL()) {
          // Load single from memory: vldr.
          set_s_register_from_sinteger(vd, ReadW(address));
        } else {
          // Store single to memory: vstr.
          WriteW(address, get_sinteger_from_s_register(vd));
        }
        break;
      }
      case 0x4:
      case 0x5:
      case 0x6:
      case 0x7:
      case 0x9:
      case 0xB:
        // Load/store multiple single from memory: vldm/vstm.
        HandleVList(instr);
        break;
      default:
        UNIMPLEMENTED();  // Not used by V8.
    }
  } else if (instr->CoprocessorValue() == 0xB) {
    switch (instr->OpcodeValue()) {
      case 0x2:
        // Load and store double to two GP registers
        if (instr->Bits(7, 6) != 0 || instr->Bit(4) != 1) {
          UNIMPLEMENTED();  // Not used by V8.
        } else {
          int rt = instr->RtValue();
          int rn = instr->RnValue();
          int vm = instr->VFPMRegValue(kDoublePrecision);
          if (instr->HasL()) {
            uint32_t data[2];
            get_d_register(vm, data);
            set_register(rt, data[0]);
            set_register(rn, data[1]);
          } else {
            int32_t data[] = {get_register(rt), get_register(rn)};
            set_d_register(vm, reinterpret_cast<uint32_t*>(data));
          }
        }
        break;
      case 0x8:
      case 0xA:
      case 0xC:
      case 0xE: {  // Load and store double to memory.
        int rn = instr->RnValue();
        int vd = instr->VFPDRegValue(kDoublePrecision);
        int offset = instr->Immed8Value();
        if (!instr->HasU()) {
          offset = -offset;
        }
        int32_t address = get_register(rn) + 4 * offset;
        // Load and store address for doubles must be at least four-byte
        // aligned.
        DCHECK_EQ(address % 4, 0);
        if (instr->HasL()) {
          // Load double from memory: vldr.
          int32_t data[] = {ReadW(address), ReadW(address + 4)};
          set_d_register(vd, reinterpret_cast<uint32_t*>(data));
        } else {
          // Store double to memory: vstr.
          uint32_t data[2];
          get_d_register(vd, data);
          WriteW(address, data[0]);
          WriteW(address + 4, data[1]);
        }
        break;
      }
      case 0x4:
      case 0x5:
      case 0x6:
      case 0x7:
      case 0x9:
      case 0xB:
        // Load/store multiple double from memory: vldm/vstm.
        HandleVList(instr);
        break;
      default:
        UNIMPLEMENTED();  // Not used by V8.
    }
  } else {
    UNIMPLEMENTED();  // Not used by V8.
  }
}

// Helper functions for implementing NEON ops. Unop applies a unary op to each
// lane. Binop applies a binary operation to matching input lanes.
template <typename T, int SIZE = kSimd128Size>
void Unop(Simulator* simulator, int Vd, int Vm, std::function<T(T)> unop) {
  static const int kLanes = SIZE / sizeof(T);
  T src[kLanes];
  simulator->get_neon_register<T, SIZE>(Vm, src);
  for (int i = 0; i < kLanes; i++) {
    src[i] = unop(src[i]);
  }
  simulator->set_neon_register<T, SIZE>(Vd, src);
}

template <typename T, int SIZE = kSimd128Size>
void Binop(Simulator* simulator, int Vd, int Vm, int Vn,
           std::function<T(T, T)> binop) {
  static const int kLanes = SIZE / sizeof(T);
  T src1[kLanes], src2[kLanes];
  simulator->get_neon_register<T, SIZE>(Vn, src1);
  simulator->get_neon_register<T, SIZE>(Vm, src2);
  for (int i = 0; i < kLanes; i++) {
    src1[i] = binop(src1[i], src2[i]);
  }
  simulator->set_neon_register<T, SIZE>(Vd, src1);
}

// Templated operations for NEON instructions.
template <typename T, typename U>
U Widen(T value) {
  static_assert(sizeof(int64_t) > sizeof(T), "T must be int32_t or smaller");
  static_assert(sizeof(U) > sizeof(T), "T must smaller than U");
  return static_cast<U>(value);
}

template <typename T, typename U>
void Widen(Simulator* simulator, int Vd, int Vm) {
  static const int kLanes = 8 / sizeof(T);
  T src[kLanes];
  U dst[kLanes];
  simulator->get_neon_register<T, kDoubleSize>(Vm, src);
  for (int i = 0; i < kLanes; i++) {
    dst[i] = Widen<T, U>(src[i]);
  }
  simulator->set_neon_register(Vd, dst);
}

template <typename T, int SIZE>
void Abs(Simulator* simulator, int Vd, int Vm) {
  Unop<T>(simulator, Vd, Vm, [](T x) { return std::abs(x); });
}

template <typename T, int SIZE>
void Neg(Simulator* simulator, int Vd, int Vm) {
  Unop<T>(simulator, Vd, Vm, [](T x) {
    // The respective minimum (negative) value maps to itself.
    return x == std::numeric_limits<T>::min() ? x : -x;
  });
}

template <typename T, typename U>
void SaturatingNarrow(Simulator* simulator, int Vd, int Vm) {
  static const int kLanes = 16 / sizeof(T);
  T src[kLanes];
  U dst[kLanes];
  simulator->get_neon_register(Vm, src);
  for (int i = 0; i < kLanes; i++) {
    dst[i] = base::saturated_cast<U>(src[i]);
  }
  simulator->set_neon_register<U, kDoubleSize>(Vd, dst);
}

template <typename T>
void AddSat(Simulator* simulator, int Vd, int Vm, int Vn) {
  Binop<T>(simulator, Vd, Vm, Vn, SaturateAdd<T>);
}

template <typename T>
void SubSat(Simulator* simulator, int Vd, int Vm, int Vn) {
  Binop<T>(simulator, Vd, Vm, Vn, SaturateSub<T>);
}

template <typename T, int SIZE>
void Zip(Simulator* simulator, int Vd, int Vm) {
  static const int kElems = SIZE / sizeof(T);
  static const int kPairs = kElems / 2;
  T src1[kElems], src2[kElems], dst1[kElems], dst2[kElems];
  simulator->get_neon_register<T, SIZE>(Vd, src1);
  simulator->get_neon_register<T, SIZE>(Vm, src2);
  for (int i = 0; i < kPairs; i++) {
    dst1[i * 2] = src1[i];
    dst1[i * 2 + 1] = src2[i];
    dst2[i * 2] = src1[i + kPairs];
    dst2[i * 2 + 1] = src2[i + kPairs];
  }
  simulator->set_neon_register<T, SIZE>(Vd, dst1);
  simulator->set_neon_register<T, SIZE>(Vm, dst2);
}

template <typename T, int SIZE>
void Unzip(Simulator* simulator, int Vd, int Vm) {
  static const int kElems = SIZE / sizeof(T);
  static const int kPairs = kElems / 2;
  T src1[kElems], src2[kElems], dst1[kElems], dst2[kElems];
  simulator->get_neon_register<T, SIZE>(Vd, src1);
  simulator->get_neon_register<T, SIZE>(Vm, src2);
  for (int i = 0; i < kPairs; i++) {
    dst1[i] = src1[i * 2];
    dst1[i + kPairs] = src2[i * 2];
    dst2[i] = src1[i * 2 + 1];
    dst2[i + kPairs] = src2[i * 2 + 1];
  }
  simulator->set_neon_register<T, SIZE>(Vd, dst1);
  simulator->set_neon_register<T, SIZE>(Vm, dst2);
}

template <typename T, int SIZE>
void Transpose(Simulator* simulator, int Vd, int Vm) {
  static const int kElems = SIZE / sizeof(T);
  static const int kPairs = kElems / 2;
  T src1[kElems], src2[kElems];
  simulator->get_neon_register<T, SIZE>(Vd, src1);
  simulator->get_neon_register<T, SIZE>(Vm, src2);
  for (int i = 0; i < kPairs; i++) {
    std::swap(src1[2 * i + 1], src2[2 * i]);
  }
  simulator->set_neon_register<T, SIZE>(Vd, src1);
  simulator->set_neon_register<T, SIZE>(Vm, src2);
}

template <typename T, int SIZE>
void Test(Simulator* simulator, int Vd, int Vm, int Vn) {
  auto test = [](T x, T y) { return (x & y) ? -1 : 0; };
  Binop<T>(simulator, Vd, Vm, Vn, test);
}

template <typename T, int SIZE>
void Add(Simulator* simulator, int Vd, int Vm, int Vn) {
  Binop<T>(simulator, Vd, Vm, Vn, std::plus<T>());
}

template <typename T, int SIZE>
void Sub(Simulator* simulator, int Vd, int Vm, int Vn) {
  Binop<T>(simulator, Vd, Vm, Vn, std::minus<T>());
}

namespace {
uint32_t Multiply(uint32_t a, uint32_t b) { return a * b; }
uint8_t Multiply(uint8_t a, uint8_t b) { return a * b; }
// 16-bit integers are special due to C++'s implicit conversion rules.
// See https://bugs.llvm.org/show_bug.cgi?id=25580.
uint16_t Multiply(uint16_t a, uint16_t b) {
  uint32_t result = static_cast<uint32_t>(a) * static_cast<uint32_t>(b);
  return static_cast<uint16_t>(result);
}

void VmovImmediate(Simulator* simulator, Instruction* instr) {
  uint8_t cmode = instr->Bits(11, 8);
  int vd = instr->VFPDRegValue(kDoublePrecision);
  int q = instr->Bit(6);
  int regs = q ? 2 : 1;
  uint8_t imm = instr->Bit(24) << 7;  // i
  imm |= instr->Bits(18, 16) << 4;    // imm3
  imm |= instr->Bits(3, 0);           // imm4
  switch (cmode) {
    case 0: {
      // Set the LSB of each 64-bit halves.
      uint64_t imm64 = imm;
      for (int r = 0; r < regs; r++) {
        simulator->set_d_register(vd + r, &imm64);
      }
      break;
    }
    case 0xe: {
      uint8_t imms[kSimd128Size];
      // Set all bytes of register.
      std::fill_n(imms, kSimd128Size, imm);
      uint64_t imm64;
      memcpy(&imm64, imms, 8);
      for (int r = 0; r < regs; r++) {
        simulator->set_d_register(vd + r, &imm64);
      }
      break;
    }
    default: {
      UNIMPLEMENTED();
    }
  }
}
}  // namespace

template <typename T, int SIZE>
void Mul(Simulator* simulator, int Vd, int Vm, int Vn) {
  static const int kElems = SIZE / sizeof(T);
  T src1[kElems], src2[kElems];
  simulator->get_neon_register<T, SIZE>(Vn, src1);
  simulator->get_neon_register<T, SIZE>(Vm, src2);
  for (int i = 0; i < kElems; i++) {
    src1[i] = Multiply(src1[i], src2[i]);
  }
  simulator->set_neon_register<T, SIZE>(Vd, src1);
}

template <typename T, int SIZE>
void ShiftLeft(Simulator* simulator, int Vd, int Vm, int shift) {
  Unop<T>(simulator, Vd, Vm, [shift](T x) { return x << shift; });
}

template <typename T, int SIZE>
void LogicalShiftRight(Simulator* simulator, int Vd, int Vm, int shift) {
  Unop<T, SIZE>(simulator, Vd, Vm, [shift](T x) { return x >> shift; });
}

template <typename T, int SIZE>
void ArithmeticShiftRight(Simulator* simulator, int Vd, int Vm, int shift) {
  auto shift_fn =
      std::bind(ArithmeticShiftRight<T>, std::placeholders::_1, shift);
  Unop<T, SIZE>(simulator, Vd, Vm, shift_fn);
}

template <typename T, int SIZE>
void ShiftRight(Simulator* simulator, int Vd, int Vm, int shift,
                bool is_unsigned) {
  if (is_unsigned) {
    using unsigned_T = typename std::make_unsigned<T>::type;
    LogicalShiftRight<unsigned_T, SIZE>(simulator, Vd, Vm, shift);
  } else {
    ArithmeticShiftRight<T, SIZE>(simulator, Vd, Vm, shift);
  }
}

template <typename T, int SIZE>
void ShiftRightAccumulate(Simulator* simulator, int Vd, int Vm, int shift) {
  Binop<T, SIZE>(simulator, Vd, Vm, Vd,
                 [shift](T a, T x) { return a + (x >> shift); });
}

template <typename T, int SIZE>
void ArithmeticShiftRightAccumulate(Simulator* simulator, int Vd, int Vm,
                                    int shift) {
  Binop<T, SIZE>(simulator, Vd, Vm, Vd, [shift](T a, T x) {
    T result = ArithmeticShiftRight<T>(x, shift);
    return a + result;
  });
}

template <typename T, int SIZE>
void ShiftLeftAndInsert(Simulator* simulator, int Vd, int Vm, int shift) {
  static const int kElems = SIZE / sizeof(T);
  T src[kElems];
  T dst[kElems];
  simulator->get_neon_register<T, SIZE>(Vm, src);
  simulator->get_neon_register<T, SIZE>(Vd, dst);
  uint64_t mask = (1llu << shift) - 1llu;
  for (int i = 0; i < kElems; i++) {
    dst[i] = (src[i] << shift) | (dst[i] & mask);
  }
  simulator->set_neon_register<T, SIZE>(Vd, dst);
}

template <typename T, int SIZE>
void ShiftRightAndInsert(Simulator* simulator, int Vd, int Vm, int shift) {
  static const int kElems = SIZE / sizeof(T);
  T src[kElems];
  T dst[kElems];
  simulator->get_neon_register<T, SIZE>(Vm, src);
  simulator->get_neon_register<T, SIZE>(Vd, dst);
  uint64_t mask = ~((1llu << (kBitsPerByte * SIZE - shift)) - 1llu);
  for (int i = 0; i < kElems; i++) {
    dst[i] = (src[i] >> shift) | (dst[i] & mask);
  }
  simulator->set_neon_register<T, SIZE>(Vd, dst);
}

template <typename T, typename S_T, int SIZE>
void ShiftByRegister(Simulator* simulator, int Vd, int Vm, int Vn) {
  static const int kElems = SIZE / sizeof(T);
  T src[kElems];
  S_T shift[kElems];
  simulator->get_neon_register<T, SIZE>(Vm, src);
  simulator->get_neon_register<S_T, SIZE>(Vn, shift);
  for (int i = 0; i < kElems; i++) {
    // Take lowest 8 bits of shift value (see F6.1.217 of ARM Architecture
    // Reference Manual ARMv8), as signed 8-bit value.
    int8_t shift_value = static_cast<int8_t>(shift[i]);
    int size = static_cast<int>(sizeof(T) * 8);
    // When shift value is greater/equal than size, we end up relying on
    // undefined behavior, handle that and emulate what the hardware does.
    if ((shift_value) >= 0) {
      // If the shift value is greater/equal than size, zero out the result.
      if (shift_value >= size) {
        src[i] = 0;
      } else {
        using unsignedT = typename std::make_unsigned<T>::type;
        src[i] = static_cast<unsignedT>(src[i]) << shift_value;
      }
    } else {
      // If the shift value is greater/equal than size, always end up with -1.
      if (-shift_value >= size) {
        src[i] = -1;
      } else {
        src[i] = ArithmeticShiftRight(src[i], -shift_value);
      }
    }
  }
  simulator->set_neon_register<T, SIZE>(Vd, src);
}

template <typename T, int SIZE>
void CompareEqual(Simulator* simulator, int Vd, int Vm, int Vn) {
  Binop<T>(simulator, Vd, Vm, Vn, [](T x, T y) { return x == y ? -1 : 0; });
}

template <typename T, int SIZE>
void CompareGreater(Simulator* simulator, int Vd, int Vm, int Vn, bool ge) {
  if (ge) {
    Binop<T>(simulator, Vd, Vm, Vn, [](T x, T y) { return x >= y ? -1 : 0; });
  } else {
    Binop<T>(simulator, Vd, Vm, Vn, [](T x, T y) { return x > y ? -1 : 0; });
  }
}

float MinMax(float a, float b, bool is_min) {
  return is_min ? JSMin(a, b) : JSMax(a, b);
}
template <typename T>
T MinMax(T a, T b, bool is_min) {
  return is_min ? std::min(a, b) : std::max(a, b);
}

template <typename T, int SIZE>
void MinMax(Simulator* simulator, int Vd, int Vm, int Vn, bool min) {
  if (min) {
    Binop<T>(simulator, Vd, Vm, Vn,
             [](auto x, auto y) { return std::min<T>(x, y); });
  } else {
    Binop<T>(simulator, Vd, Vm, Vn,
             [](auto x, auto y) { return std::max<T>(x, y); });
  }
}

template <typename T>
void PairwiseMinMax(Simulator* simulator, int Vd, int Vm, int Vn, bool min) {
  static const int kElems = kDoubleSize / sizeof(T);
  static const int kPairs = kElems / 2;
  T dst[kElems], src1[kElems], src2[kElems];
  simulator->get_neon_register<T, kDoubleSize>(Vn, src1);
  simulator->get_neon_register<T, kDoubleSize>(Vm, src2);
  for (int i = 0; i < kPairs; i++) {
    dst[i] = MinMax(src1[i * 2], src1[i * 2 + 1], min);
    dst[i + kPairs] = MinMax(src2[i * 2], src2[i * 2 + 1], min);
  }
  simulator->set_neon_register<T, kDoubleSize>(Vd, dst);
}

template <typename T>
void PairwiseAdd(Simulator* simulator, int Vd, int Vm, int Vn) {
  static const int kElems = kDoubleSize / sizeof(T);
  static const int kPairs = kElems / 2;
  T dst[kElems], src1[kElems], src2[kElems];
  simulator->get_neon_register<T, kDoubleSize>(Vn, src1);
  simulator->get_neon_register<T, kDoubleSize>(Vm, src2);
  for (int i = 0; i < kPairs; i++) {
    dst[i] = src1[i * 2] + src1[i * 2 + 1];
    dst[i + kPairs] = src2[i * 2] + src2[i * 2 + 1];
  }
  simulator->set_neon_register<T, kDoubleSize>(Vd, dst);
}

template <typename NarrowType, typename WideType, int SIZE = kSimd128Size>
void PairwiseAddLong(Simulator* simulator, int Vd, int Vm) {
  DCHECK_EQ(sizeof(WideType), 2 * sizeof(NarrowType));
  static constexpr int kSElems = SIZE / sizeof(NarrowType);
  static constexpr int kTElems = SIZE / sizeof(WideType);
  NarrowType src[kSElems];
  WideType dst[kTElems];
  simulator->get_neon_register<NarrowType, SIZE>(Vm, src);
  for (int i = 0; i < kTElems; i++) {
    dst[i] = WideType{src[i * 2]} + WideType{src[i * 2 + 1]};
  }
  simulator->set_neon_register<WideType, SIZE>(Vd, dst);
}

template <typename NarrowType, typename WideType, int SIZE = kSimd128Size>
void PairwiseAddAccumulateLong(Simulator* simulator, int Vd, int Vm) {
  DCHECK_EQ(sizeof(WideType), 2 * sizeof(NarrowType));
  static constexpr int kSElems = SIZE / sizeof(NarrowType);
  static constexpr int kTElems = SIZE / sizeof(WideType);
  NarrowType src[kSElems];
  WideType dst[kTElems];
  simulator->get_neon_register<NarrowType, SIZE>(Vm, src);
  simulator->get_neon_register<WideType, SIZE>(Vd, dst);
  for (int i = 0; i < kTElems; i++) {
    dst[i] += WideType{src[i * 2]} + WideType{src[i * 2 + 1]};
  }
  simulator->set_neon_register<WideType, SIZE>(Vd, dst);
}

template <typename NarrowType, typename WideType>
void MultiplyLong(Simulator* simulator, int Vd, int Vn, int Vm) {
  DCHECK_EQ(sizeof(WideType), 2 * sizeof(NarrowType));
  static const int kElems = kSimd128Size / sizeof(WideType);
  NarrowType src1[kElems], src2[kElems];
  WideType dst[kElems];

  // Get the entire d reg, then memcpy it to an array so we can address the
  // underlying datatype easily.
  uint64_t tmp;
  simulator->get_d_register(Vn, &tmp);
  memcpy(src1, &tmp, sizeof(tmp));
  simulator->get_d_register(Vm, &tmp);
  memcpy(src2, &tmp, sizeof(tmp));

  for (int i = 0; i < kElems; i++) {
    dst[i] = WideType{src1[i]} * WideType{src2[i]};
  }

  simulator->set_neon_register<WideType>(Vd, dst);
}

void Simulator::DecodeUnconditional(Instruction* instr) {
  // This follows the decoding in F4.1.18 Unconditional instructions.
  int op0 = instr->Bits(26, 25);
  int op1 = instr->Bit(20);

  // Four classes of decoding:
  // - Miscellaneous (omitted, no instructions used in V8).
  // - Advanced SIMD data-processing.
  // - Memory hints and barriers.
  // - Advanced SIMD element or structure load/store.
  if (op0 == 0b01) {
    DecodeAdvancedSIMDDataProcessing(instr);
  } else if ((op0 & 0b10) == 0b10 && op1) {
    DecodeMemoryHintsAndBarriers(instr);
  } else if (op0 == 0b10 && !op1) {
    DecodeAdvancedSIMDElementOrStructureLoadStore(instr);
  } else {
    UNIMPLEMENTED();
  }
}

void Simulator::DecodeAdvancedSIMDTwoOrThreeRegisters(Instruction* instr) {
  // Advanced SIMD two registers, or three registers of different lengths.
  int op0 = instr->Bit(24);
  int op1 = instr->Bits(21, 20);
  int op2 = instr->Bits(11, 10);
  int op3 = instr->Bit(6);
  if (!op0 && op1 == 0b11) {
    // vext.8 Qd, Qm, Qn, imm4
    int imm4 = instr->Bits(11, 8);
    int Vd = instr->VFPDRegValue(kSimd128Precision);
    int Vm = instr->VFPMRegValue(kSimd128Precision);
    int Vn = instr->VFPNRegValue(kSimd128Precision);
    uint8_t src1[16], src2[16], dst[16];
    get_neon_register(Vn, src1);
    get_neon_register(Vm, src2);
    int boundary = kSimd128Size - imm4;
    int i = 0;
    for (; i < boundary; i++) {
      dst[i] = src1[i + imm4];
    }
    for (; i < 16; i++) {
      dst[i] = src2[i - boundary];
    }
    set_neon_register(Vd, dst);
  } else if (op0 && op1 == 0b11 && ((op2 >> 1) == 0)) {
    // Advanced SIMD two registers misc
    int size = instr->Bits(19, 18);
    int opc1 = instr->Bits(17, 16);
    int opc2 = instr->Bits(10, 7);
    int q = instr->Bit(6);

    if (opc1 == 0 && (opc2 >> 2) == 0) {
      // vrev<op>.size Qd, Qm
      int Vd = instr->VFPDRegValue(kSimd128Precision);
      int Vm = instr->VFPMRegValue(kSimd128Precision);
      NeonSize size = static_cast<NeonSize>(instr->Bits(19, 18));
      NeonSize op =
          static_cast<NeonSize>(static_cast<int>(Neon64) - instr->Bits(8, 7));
      switch (op) {
        case Neon16: {
          DCHECK_EQ(Neon8, size);
          uint8_t src[16];
          get_neon_register(Vm, src);
          for (int i = 0; i < 16; i += 2) {
            std::swap(src[i], src[i + 1]);
          }
          set_neon_register(Vd, src);
          break;
        }
        case Neon32: {
          switch (size) {
            case Neon16: {
              uint16_t src[8];
              get_neon_register(Vm, src);
              for (int i = 0; i < 8; i += 2) {
                std::swap(src[i], src[i + 1]);
              }
              set_neon_register(Vd, src);
              break;
            }
            case Neon8: {
              uint8_t src[16];
              get_neon_register(Vm, src);
              for (int i = 0; i < 4; i++) {
                std::swap(src[i * 4], src[i * 4 + 3]);
                std::swap(src[i * 4 + 1], src[i * 4 + 2]);
              }
              set_neon_register(Vd, src);
              break;
            }
            default:
              UNREACHABLE();
          }
          break;
        }
        case Neon64: {
          switch (size) {
            case Neon32: {
              uint32_t src[4];
              get_neon_register(Vm, src);
              std::swap(src[0], src[1]);
              std::swap(src[2], src[3]);
              set_neon_register(Vd, src);
              break;
            }
            case Neon16: {
              uint16_t src[8];
              get_neon_register(Vm, src);
              for (int i = 0; i < 2; i++) {
                std::swap(src[i * 4], src[i * 4 + 3]);
                std::swap(src[i * 4 + 1], src[i * 4 + 2]);
              }
              set_neon_register(Vd, src);
              break;
            }
            case Neon8: {
              uint8_t src[16];
              get_neon_register(Vm, src);
              for (int i = 0; i < 4; i++) {
                std::swap(src[i], src[7 - i]);
                std::swap(src[i + 8], src[15 - i]);
              }
              set_neon_register(Vd, src);
              break;
            }
            default:
              UNREACHABLE();
          }
          break;
        }
        default:
          UNREACHABLE();
      }
    } else if (opc1 == 0 && (opc2 == 0b0100 || opc2 == 0b0101)) {
      DCHECK_EQ(1, instr->Bit(6));  // Only support Q regs.
      int Vd = instr->VFPDRegValue(kSimd128Precision);
      int Vm = instr->VFPMRegValue(kSimd128Precision);
      int is_signed = instr->Bit(7) == 0;
      // vpaddl Qd, Qm.
      switch (size) {
        case Neon8:
          is_signed ? PairwiseAddLong<int8_t, int16_t>(this, Vd, Vm)
                    : PairwiseAddLong<uint8_t, uint16_t>(this, Vd, Vm);
          break;
        case Neon16:
          is_signed ? PairwiseAddLong<int16_t, int32_t>(this, Vd, Vm)
                    : PairwiseAddLong<uint16_t, uint32_t>(this, Vd, Vm);
          break;
        case Neon32:
          is_signed ? PairwiseAddLong<int32_t, int64_t>(this, Vd, Vm)
                    : PairwiseAddLong<uint32_t, uint64_t>(this, Vd, Vm);
          break;
        case Neon64:
          UNREACHABLE();
      }
    } else if (opc1 == 0 && (opc2 == 0b1100 || opc2 == 0b1101)) {
      DCHECK_EQ(1, instr->Bit(6));  // Only support Q regs.
      int Vd = instr->VFPDRegValue(kSimd128Precision);
      int Vm = instr->VFPMRegValue(kSimd128Precision);
      int is_signed = instr->Bit(7) == 0;
      // vpadal Qd, Qm
      switch (size) {
        case Neon8:
          is_signed
              ? PairwiseAddAccumulateLong<int8_t, int16_t>(this, Vd, Vm)
              : PairwiseAddAccumulateLong<uint8_t, uint16_t>(this, Vd, Vm);
          break;
        case Neon16:
          is_signed
              ? PairwiseAddAccumulateLong<int16_t, int32_t>(this, Vd, Vm)
              : PairwiseAddAccumulateLong<uint16_t, uint32_t>(this, Vd, Vm);
          break;
        case Neon32:
          is_signed
              ? PairwiseAddAccumulateLong<int32_t, int64_t>(this, Vd, Vm)
              : PairwiseAddAccumulateLong<uint32_t, uint64_t>(this, Vd, Vm);
          break;
        case Neon64:
          UNREACHABLE();
      }
    } else if (size == 0 && opc1 == 0b10 && opc2 == 0) {
      if (instr->Bit(6) == 0) {
        // vswp Dd, Dm.
        uint64_t dval, mval;
        int vd = instr->VFPDRegValue(kDoublePrecision);
        int vm = instr->VFPMRegValue(kDoublePrecision);
        get_d_register(vd, &dval);
        get_d_register(vm, &mval);
        set_d_register(vm, &dval);
        set_d_register(vd, &mval);
      } else {
        // vswp Qd, Qm.
        uint32_t dval[4], mval[4];
        int vd = instr->VFPDRegValue(kSimd128Precision);
        int vm = instr->VFPMRegValue(kSimd128Precision);
        get_neon_register(vd, dval);
        get_neon_register(vm, mval);
        set_neon_register(vm, dval);
        set_neon_register(vd, mval);
      }
    } else if (opc1 == 0 && opc2 == 0b1010) {
      // vcnt Qd, Qm.
      DCHECK_EQ(0, size);
      int vd = instr->VFPDRegValue(q ? kSimd128Precision : kDoublePrecision);
      int vm = instr->VFPMRegValue(q ? kSimd128Precision : kDoublePrecision);
      uint8_t q_data[16];
      get_neon_register(vm, q_data);
      for (int i = 0; i < 16; i++) {
        q_data[i] = base::bits::CountPopulation(q_data[i]);
      }
      set_neon_register(vd, q_data);
    } else if (opc1 == 0 && opc2 == 0b1011) {
      // vmvn Qd, Qm.
      int vd = instr->VFPDRegValue(kSimd128Precision);
      int vm = instr->VFPMRegValue(kSimd128Precision);
      uint32_t q_data[4];
      get_neon_register(vm, q_data);
      for (int i = 0; i < 4; i++) q_data[i] = ~q_data[i];
      set_neon_register(vd, q_data);
    } else if (opc1 == 0b01 && opc2 == 0b0010) {
      // vceq.<dt> Qd, Qm, #0 (signed integers).
      int Vd = instr->VFPDRegValue(kSimd128Precision);
      int Vm = instr->VFPMRegValue(kSimd128Precision);
      switch (size) {
        case Neon8:
          Unop<int8_t>(this, Vd, Vm, [](int8_t x) { return x == 0 ? -1 : 0; });
          break;
        case Neon16:
          Unop<int16_t>(this, Vd, Vm,
                        [](int16_t x) { return x == 0 ? -1 : 0; });
          break;
        case Neon32:
          Unop<int32_t>(this, Vd, Vm,
                        [](int32_t x) { return x == 0 ? -1 : 0; });
          break;
        case Neon64:
          UNREACHABLE();
      }
    } else if (opc1 == 0b01 && opc2 == 0b0100) {
      // vclt.<dt> Qd, Qm, #0 (signed integers).
      int Vd = instr->VFPDRegValue(kSimd128Precision);
      int Vm = instr->VFPMRegValue(kSimd128Precision);
      switch (size) {
        case Neon8:
          Unop<int8_t>(this, Vd, Vm, [](int8_t x) { return x < 0 ? -1 : 0; });
          break;
        case Neon16:
          Unop<int16_t>(this, Vd, Vm, [](int16_t x) { return x < 0 ? -1 : 0; });
          break;
        case Neon32:
          Unop<int32_t>(this, Vd, Vm, [](int32_t x) { return x < 0 ? -1 : 0; });
          break;
        case Neon64:
          UNREACHABLE();
      }
    } else if (opc1 == 0b01 && (opc2 & 0b0111) == 0b110) {
      // vabs<type>.<size> Qd, Qm
      int Vd = instr->VFPDRegValue(kSimd128Precision);
      int Vm = instr->VFPMRegValue(kSimd128Precision);
      if (instr->Bit(10) != 0) {
        // floating point (clear sign bits)
        uint32_t src[4];
        get_neon_register(Vm, src);
        for (int i = 0; i < 4; i++) {
          src[i] &= ~0x80000000;
        }
        set_neon_register(Vd, src);
      } else {
        // signed integer
        switch (size) {
          case Neon8:
            Abs<int8_t, kSimd128Size>(this, Vd, Vm);
            break;
          case Neon16:
            Abs<int16_t, kSimd128Size>(this, Vd, Vm);
            break;
          case Neon32:
            Abs<int32_t, kSimd128Size>(this, Vd, Vm);
            break;
          default:
            UNIMPLEMENTED();
        }
      }
    } else if (opc1 == 0b01 && (opc2 & 0b0111) == 0b111) {
      int Vd = instr->VFPDRegValue(kSimd128Precision);
      int Vm = instr->VFPMRegValue(kSimd128Precision);
      // vneg<type>.<size> Qd, Qm (signed integer)
      if (instr->Bit(10) != 0) {
        // floating point (toggle sign bits)
        uint32_t src[4];
        get_neon_register(Vm, src);
        for (int i = 0; i < 4; i++) {
          src[i] ^= 0x80000000;
        }
        set_neon_register(Vd, src);
      } else {
        // signed integer
        switch (size) {
          case Neon8:
            Neg<int8_t, kSimd128Size>(this, Vd, Vm);
            break;
          case Neon16:
            Neg<int16_t, kSimd128Size>(this, Vd, Vm);
            break;
          case Neon32:
            Neg<int32_t, kSimd128Size>(this, Vd, Vm);
            break;
          default:
            UNIMPLEMENTED();
        }
      }
    } else if (opc1 == 0b10 && opc2 == 0b0001) {
      if (q) {
        int Vd = instr->VFPDRegValue(kSimd128Precision);
        int Vm = instr->VFPMRegValue(kSimd128Precision);
        // vtrn.<size> Qd, Qm.
        switch (size) {
          case Neon8:
            Transpose<uint8_t, kSimd128Size>(this, Vd, Vm);
            break;
          case Neon16:
            Transpose<uint16_t, kSimd128Size>(this, Vd, Vm);
            break;
          case Neon32:
            Transpose<uint32_t, kSimd128Size>(this, Vd, Vm);
            break;
          default:
            UNREACHABLE();
        }
      } else {
        int Vd = instr->VFPDRegValue(kDoublePrecision);
        int Vm = instr->VFPMRegValue(kDoublePrecision);
        // vtrn.<size> Dd, Dm.
        switch (size) {
          case Neon8:
            Transpose<uint8_t, kDoubleSize>(this, Vd, Vm);
            break;
          case Neon16:
            Transpose<uint16_t, kDoubleSize>(this, Vd, Vm);
            break;
          case Neon32:
            Transpose<uint32_t, kDoubleSize>(this, Vd, Vm);
            break;
          default:
            UNREACHABLE();
        }
      }
    } else if (opc1 == 0b10 && (opc2 & 0b1110) == 0b0010) {
      NeonSize size = static_cast<NeonSize>(instr->Bits(19, 18));
      if (q) {
        int Vd = instr->VFPDRegValue(kSimd128Precision);
        int Vm = instr->VFPMRegValue(kSimd128Precision);
        if (instr->Bit(7) == 1) {
          // vzip.<size> Qd, Qm.
          switch (size) {
            case Neon8:
              Zip<uint8_t, kSimd128Size>(this, Vd, Vm);
              break;
            case Neon16:
              Zip<uint16_t, kSimd128Size>(this, Vd, Vm);
              break;
            case Neon32:
              Zip<uint32_t, kSimd128Size>(this, Vd, Vm);
              break;
            default:
              UNREACHABLE();
          }
        } else {
          // vuzp.<size> Qd, Qm.
          switch (size) {
            case Neon8:
              Unzip<uint8_t, kSimd128Size>(this, Vd, Vm);
              break;
            case Neon16:
              Unzip<uint16_t, kSimd128Size>(this, Vd, Vm);
              break;
            case Neon32:
              Unzip<uint32_t, kSimd128Size>(this, Vd, Vm);
              break;
            default:
              UNREACHABLE();
          }
        }
      } else {
        int Vd = instr->VFPDRegValue(kDoublePrecision);
        int Vm = instr->VFPMRegValue(kDoublePrecision);
        if (instr->Bit(7) == 1) {
          // vzip.<size> Dd, Dm.
          switch (size) {
            case Neon8:
              Zip<uint8_t, kDoubleSize>(this, Vd, Vm);
              break;
            case Neon16:
              Zip<uint16_t, kDoubleSize>(this, Vd, Vm);
              break;
            case Neon32:
              UNIMPLEMENTED();
            default:
              UNREACHABLE();
          }
        } else {
          // vuzp.<size> Dd, Dm.
          switch (size) {
            case Neon8:
              Unzip<uint8_t, kDoubleSize>(this, Vd, Vm);
              break;
            case Neon16:
              Unzip<uint16_t, kDoubleSize>(this, Vd, Vm);
              break;
            case Neon32:
              UNIMPLEMENTED();
            default:
              UNREACHABLE();
          }
        }
      }
    } else if (opc1 == 0b10 && (opc2 & 0b1110) == 0b0100) {
      // vqmovn.<type><size> Dd, Qm.
      int Vd = instr->VFPDRegValue(kDoublePrecision);
      int Vm = instr->VFPMRegValue(kSimd128Precision);
      NeonSize size = static_cast<NeonSize>(instr->Bits(19, 18));
      bool dst_unsigned = instr->Bit(6) != 0;
      bool src_unsigned = instr->Bits(7, 6) == 0b11;
      DCHECK_IMPLIES(src_unsigned, dst_unsigned);
      switch (size) {
        case Neon8: {
          if (src_unsigned) {
            SaturatingNarrow<uint16_t, uint8_t>(this, Vd, Vm);
          } else if (dst_unsigned) {
            SaturatingNarrow<int16_t, uint8_t>(this, Vd, Vm);
          } else {
            SaturatingNarrow<int16_t, int8_t>(this, Vd, Vm);
          }
          break;
        }
        case Neon16: {
          if (src_unsigned) {
            SaturatingNarrow<uint32_t, uint16_t>(this, Vd, Vm);
          } else if (dst_unsigned) {
            SaturatingNarrow<int32_t, uint16_t>(this, Vd, Vm);
          } else {
            SaturatingNarrow<int32_t, int16_t>(this, Vd, Vm);
          }
          break;
        }
        case Neon32: {
          if (src_unsigned) {
            SaturatingNarrow<uint64_t, uint32_t>(this, Vd, Vm);
          } else if (dst_unsigned) {
            SaturatingNarrow<int64_t, uint32_t>(this, Vd, Vm);
          } else {
            SaturatingNarrow<int64_t, int32_t>(this, Vd, Vm);
          }
          break;
        }
        case Neon64:
          UNREACHABLE();
      }
    } else if (opc1 == 0b10 && instr->Bit(10) == 1) {
      // vrint<q>.<dt> <Dd>, <Dm>
      // vrint<q>.<dt> <Qd>, <Qm>
      // See F6.1.205
      int regs = instr->Bit(6) + 1;
      int rounding_mode = instr->Bits(9, 7);
      float (*fproundint)(float) = nullptr;
      switch (rounding_mode) {
        case 0:
          fproundint = &nearbyintf;
          break;
        case 3:
          fproundint = &truncf;
          break;
        case 5:
          fproundint = &floorf;
          break;
        case 7:
          fproundint = &ceilf;
          break;
        default:
          UNIMPLEMENTED();
      }
      int vm = instr->VFPMRegValue(kDoublePrecision);
      int vd = instr->VFPDRegValue(kDoublePrecision);

      float floats[2];
      for (int r = 0; r < regs; r++) {
        // We cannot simply use GetVFPSingleValue since our Q registers
        // might not map to any S registers at all.
        get_neon_register<float, kDoubleSize>(vm + r, floats);
        for (int e = 0; e < 2; e++) {
          floats[e] = canonicalizeNaN(fproundint(floats[e]));
        }
        set_neon_register<float, kDoubleSize>(vd + r, floats);
      }
    } else if (opc1 == 0b11 && (opc2 & 0b1100) == 0b1000) {
      // vrecpe/vrsqrte.f32 Qd, Qm.
      int Vd = instr->VFPDRegValue(kSimd128Precision);
      int Vm = instr->VFPMRegValue(kSimd128Precision);
      uint32_t src[4];
      get_neon_register(Vm, src);
      if (instr->Bit(7) == 0) {
        for (int i = 0; i < 4; i++) {
          float denom = base::bit_cast<float>(src[i]);
          div_zero_vfp_flag_ = (denom == 0);
          float result = 1.0f / denom;
          result = canonicalizeNaN(result);
          src[i] = base::bit_cast<uint32_t>(result);
        }
      } else {
        for (int i = 0; i < 4; i++) {
          float radicand = base::bit_cast<float>(src[i]);
          float result = 1.0f / std::sqrt(radicand);
          result = canonicalizeNaN(result);
          src[i] = base::bit_cast<uint32_t>(result);
        }
      }
      set_neon_register(Vd, src);
    } else if (opc1 == 0b11 && (opc2 & 0b1100) == 0b1100) {
      // vcvt.<Td>.<Tm> Qd, Qm.
      int Vd = instr->VFPDRegValue(kSimd128Precision);
      int Vm = instr->VFPMRegValue(kSimd128Precision);
      uint32_t q_data[4];
      get_neon_register(Vm, q_data);
      int op = instr->Bits(8, 7);
      for (int i = 0; i < 4; i++) {
        switch (op) {
          case 0:
            // f32 <- s32, round towards nearest.
            q_data[i] = base::bit_cast<uint32_t>(std::round(
                static_cast<float>(base::bit_cast<int32_t>(q_data[i]))));
            break;
          case 1:
            // f32 <- u32, round towards nearest.
            q_data[i] = base::bit_cast<uint32_t>(
                std::round(static_cast<float>(q_data[i])));
            break;
          case 2:
            // s32 <- f32, round to zero.
            q_data[i] = static_cast<uint32_t>(ConvertDoubleToInt(
                base::bit_cast<float>(q_data[i]), false, RZ));
            break;
          case 3:
            // u32 <- f32, round to zero.
            q_data[i] = static_cast<uint32_t>(
                ConvertDoubleToInt(base::bit_cast<float>(q_data[i]), true, RZ));
            break;
        }
      }
      set_neon_register(Vd, q_data);
    } else {
      UNIMPLEMENTED();
    }
  } else if (op0 && op1 == 0b11 && op2 == 0b10) {
    // vtb[l,x] Dd, <list>, Dm.
    int vd = instr->VFPDRegValue(kDoublePrecision);
    int vn = instr->VFPNRegValue(kDoublePrecision);
    int vm = instr->VFPMRegValue(kDoublePrecision);
    int table_len = (instr->Bits(9, 8) + 1) * kDoubleSize;
    bool vtbx = instr->Bit(6) != 0;  // vtbl / vtbx
    uint64_t destination = 0, indices = 0, result = 0;
    get_d_register(vd, &destination);
    get_d_register(vm, &indices);
    for (int i = 0; i < kDoubleSize; i++) {
      int shift = i * kBitsPerByte;
      int index = (indices >> shift) & 0xFF;
      if (index < table_len) {
        uint64_t table;
        get_d_register(vn + index / kDoubleSize, &table);
        result |= ((table >> ((index % kDoubleSize) * kBitsPerByte)) & 0xFF)
                  << shift;
      } else if (vtbx) {
        result |= destination & (0xFFull << shift);
      }
    }
    set_d_register(vd, &result);
  } else if (op0 && op1 == 0b11 && op2 == 0b11) {
    // Advanced SIMD duplicate (scalar)
    if (instr->Bits(9, 7) == 0) {
      // vdup.<size> Dd, Dm[index].
      // vdup.<size> Qd, Dm[index].
      int vm = instr->VFPMRegValue(kDoublePrecision);
      int imm4 = instr->Bits(19, 16);
      int size = 0, index = 0, mask = 0;
      if ((imm4 & 0x1) != 0) {
        size = 8;
        index = imm4 >> 1;
        mask = 0xFFu;
      } else if ((imm4 & 0x2) != 0) {
        size = 16;
        index = imm4 >> 2;
        mask = 0xFFFFu;
      } else {
        size = 32;
        index = imm4 >> 3;
        mask = 0xFFFFFFFFu;
      }
      uint64_t d_data;
      get_d_register(vm, &d_data);
      uint32_t scalar = (d_data >> (size * index)) & mask;
      uint32_t duped = scalar;
      for (int i = 1; i < 32 / size; i++) {
        scalar <<= size;
        duped |= scalar;
      }
      uint32_t result[4] = {duped, duped, duped, duped};
      if (instr->Bit(6) == 0) {
        int vd = instr->VFPDRegValue(kDoublePrecision);
        set_d_register(vd, result);
      } else {
        int vd = instr->VFPDRegValue(kSimd128Precision);
        set_neon_register(vd, result);
      }
    } else {
      UNIMPLEMENTED();
    }
  } else if (op1 != 0b11 && !op3) {
    // Advanced SIMD three registers of different lengths.
    int u = instr->Bit(24);
    int opc = instr->Bits(11, 8);
    NeonSize size = static_cast<NeonSize>(instr->Bits(21, 20));
    if (opc == 0b1000) {
      // vmlal.u<size> Qd, Dn, Dm
      if (size != Neon32) UNIMPLEMENTED();

      int Vd = instr->VFPDRegValue(kSimd128Precision);
      int Vn = instr->VFPNRegValue(kDoublePrecision);
      int Vm = instr->VFPMRegValue(kDoublePrecision);
      uint64_t src1, src2, dst[2];

      get_neon_register<uint64_t>(Vd, dst);
      get_d_register(Vn, &src1);
      get_d_register(Vm, &src2);
      dst[0] += (src1 & 0xFFFFFFFFULL) * (src2 & 0xFFFFFFFFULL);
      dst[1] += (src1 >> 32) * (src2 >> 32);
      set_neon_register<uint64_t>(Vd, dst);
    } else if (opc == 0b1100) {
      int Vd = instr->VFPDRegValue(kSimd128Precision);
      int Vn = instr->VFPNRegValue(kDoublePrecision);
      int Vm = instr->VFPMRegValue(kDoublePrecision);
      if (u) {
        // vmull.u<size> Qd, Dn, Dm
        switch (size) {
          case Neon8: {
            MultiplyLong<uint8_t, uint16_t>(this, Vd, Vn, Vm);
            break;
          }
          case Neon16: {
            MultiplyLong<uint16_t, uint32_t>(this, Vd, Vn, Vm);
            break;
          }
          case Neon32: {
            MultiplyLong<uint32_t, uint64_t>(this, Vd, Vn, Vm);
            break;
          }
          case Neon64: {
            UNIMPLEMENTED();
          }
        }
      } else {
        // vmull.s<size> Qd, Dn, Dm
        switch (size) {
          case Neon8: {
            MultiplyLong<int8_t, int16_t>(this, Vd, Vn, Vm);
            break;
          }
          case Neon16: {
            MultiplyLong<int16_t, int32_t>(this, Vd, Vn, Vm);
            break;
          }
          case Neon32: {
            MultiplyLong<int32_t, int64_t>(this, Vd, Vn, Vm);
            break;
          }
          case Neon64: {
            UNIMPLEMENTED();
          }
        }
      }
    }
  } else if (op1 != 0b11 && op3) {
    // The instructions specified by this encoding are not used in V8.
    UNIMPLEMENTED();
  } else {
    UNIMPLEMENTED();
  }
}

void Simulator::DecodeAdvancedSIMDDataProcessing(Instruction* instr) {
  int op0 = instr->Bit(23);
  int op1 = instr->Bit(4);

  if (op0 == 0) {
    // Advanced SIMD three registers of same length.
    int u = instr->Bit(24);
    int opc = instr->Bits(11, 8);
    int q = instr->Bit(6);
    int sz = instr->Bits(21, 20);
    int Vd, Vm, Vn;
    if (q) {
      Vd = instr->VFPDRegValue(kSimd128Precision);
      Vm = instr->VFPMRegValue(kSimd128Precision);
      Vn = instr->VFPNRegValue(kSimd128Precision);
    } else {
      Vd = instr->VFPDRegValue(kDoublePrecision);
      Vm = instr->VFPMRegValue(kDoublePrecision);
      Vn = instr->VFPNRegValue(kDoublePrecision);
    }

    if (!u && opc == 0 && op1) {
      // vqadd.s<size> Qd, Qm, Qn.
      NeonSize size = static_cast<NeonSize>(instr->Bits(21, 20));
      switch (size) {
        case Neon8:
          AddSat<int8_t>(this, Vd, Vm, Vn);
          break;
        case Neon16:
          AddSat<int16_t>(this, Vd, Vm, Vn);
          break;
        case Neon32:
          AddSat<int32_t>(this, Vd, Vm, Vn);
          break;
        default:
          UNREACHABLE();
      }
    } else if (!u && opc == 1 && sz == 2 && q && op1) {
      // vmov Qd, Qm.
      // vorr, Qd, Qm, Qn.
      uint32_t src1[4];
      get_neon_register(Vm, src1);
      if (Vm != Vn) {
        uint32_t src2[4];
        get_neon_register(Vn, src2);
        for (int i = 0; i < 4; i++) {
          src1[i] = src1[i] | src2[i];
        }
      }
      set_neon_register(Vd, src1);
    } else if (!u && opc == 1 && sz == 3 && q && op1) {
      // vorn, Qd, Qm, Qn.
      // NeonSize does not matter.
      Binop<uint32_t>(this, Vd, Vm, Vn,
                      [](uint32_t x, uint32_t y) { return x | (~y); });
    } else if (!u && opc == 1 && sz == 0 && q && op1) {
      // vand Qd, Qm, Qn.
      uint32_t src1[4], src2[4];
      get_neon_register(Vn, src1);
      get_neon_register(Vm, src2);
      for (int i = 0; i < 4; i++) {
        src1[i] = src1[i] & src2[i];
      }
      set_neon_register(Vd, src1);
    } else if (!u && opc == 1 && sz == 1 && q && op1) {
      // vbic Qd, Qm, Qn.
      uint32_t src1[4], src2[4];
      get_neon_register(Vn, src1);
      get_neon_register(Vm, src2);
      for (int i = 0; i < 4; i++) {
        src1[i] = src1[i] & ~src2[i];
      }
      set_neon_register(Vd, src1);
    } else if (!u && opc == 2 && op1) {
      // vqsub.s<size> Qd, Qm, Qn.
      NeonSize size = static_cast<NeonSize>(instr->Bits(21, 20));
      switch (size) {
        case Neon8:
          SubSat<int8_t>(this, Vd, Vm, Vn);
          break;
        case Neon16:
          SubSat<int16_t>(this, Vd, Vm, Vn);
          break;
        case Neon32:
          SubSat<int32_t>(this, Vd, Vm, Vn);
          break;
        case Neon64:
          SubSat<int64_t>(this, Vd, Vm, Vn);
          break;
        default:
          UNREACHABLE();
      }
    } else if (!u && opc == 3) {
      // vcge/vcgt.s<size> Qd, Qm, Qn.
      bool ge = instr->Bit(4) == 1;
      NeonSize size = static_cast<NeonSize>(instr->Bits(21, 20));
      switch (size) {
        case Neon8:
          CompareGreater<int8_t, kSimd128Size>(this, Vd, Vm, Vn, ge);
          break;
        case Neon16:
          CompareGreater<int16_t, kSimd128Size>(this, Vd, Vm, Vn, ge);
          break;
        case Neon32:
          CompareGreater<int32_t, kSimd128Size>(this, Vd, Vm, Vn, ge);
          break;
        default:
          UNREACHABLE();
      }
    } else if (!u && opc == 4 && !op1) {
      // vshl s<size> Qd, Qm, Qn.
      NeonSize size = static_cast<NeonSize>(instr->Bits(21, 20));
      switch (size) {
        case Neon8:
          ShiftByRegister<int8_t, int8_t, kSimd128Size>(this, Vd, Vm, Vn);
          break;
        case Neon16:
          ShiftByRegister<int16_t, int16_t, kSimd128Size>(this, Vd, Vm, Vn);
          break;
        case Neon32:
          ShiftByRegister<int32_t, int32_t, kSimd128Size>(this, Vd, Vm, Vn);
          break;
        case Neon64:
          ShiftByRegister<int64_t, int64_t, kSimd128Size>(this, Vd, Vm, Vn);
          break;
        default:
          UNREACHABLE();
      }
    } else if (!u && opc == 6) {
      // vmin/vmax.s<size> Qd, Qm, Qn.
      NeonSize size = static_cast<NeonSize>(instr->Bits(21, 20));
      bool min = instr->Bit(4) != 0;
      switch (size) {
        case Neon8:
          MinMax<int8_t, kSimd128Size>(this, Vd, Vm, Vn, min);
          break;
        case Neon16:
          MinMax<int16_t, kSimd128Size>(this, Vd, Vm, Vn, min);
          break;
        case Neon32:
          MinMax<int32_t, kSimd128Size>(this, Vd, Vm, Vn, min);
          break;
        default:
          UNREACHABLE();
      }
    } else if (!u && opc == 8 && op1) {
      // vtst.i<size> Qd, Qm, Qn.
      NeonSize size = static_cast<NeonSize>(instr->Bits(21, 20));
      switch (size) {
        case Neon8:
          Test<uint8_t, kSimd128Size>(this, Vd, Vm, Vn);
          break;
        case Neon16:
          Test<uint16_t, kSimd128Size>(this, Vd, Vm, Vn);
          break;
        case Neon32:
          Test<uint32_t, kSimd128Size>(this, Vd, Vm, Vn);
          break;
        default:
          UNREACHABLE();
      }
    } else if (!u && opc == 8 && !op1) {
      // vadd.i<size> Qd, Qm, Qn.
      NeonSize size = static_cast<NeonSize>(instr->Bits(21, 20));
      switch (size) {
        case Neon8:
          Add<uint8_t, kSimd128Size>(this, Vd, Vm, Vn);
          break;
        case Neon16:
          Add<uint16_t, kSimd128Size>(this, Vd, Vm, Vn);
          break;
        case Neon32:
          Add<uint32_t, kSimd128Size>(this, Vd, Vm, Vn);
          break;
        case Neon64:
          Add<uint64_t, kSimd128Size>(this, Vd, Vm, Vn);
          break;
      }
    } else if (opc == 9 && op1) {
      // vmul.i<size> Qd, Qm, Qn.
      NeonSize size = static_cast<NeonSize>(instr->Bits(21, 20));
      switch (size) {
        case Neon8:
          Mul<uint8_t, kSimd128Size>(this, Vd, Vm, Vn);
          break;
        case Neon16:
          Mul<uint16_t, kSimd128Size>(this, Vd, Vm, Vn);
          break;
        case Neon32:
          Mul<uint32_t, kSimd128Size>(this, Vd, Vm, Vn);
          break;
        default:
          UNREACHABLE();
      }
    } else if (!u && opc == 0xA) {
      // vpmin/vpmax.s<size> Dd, Dm, Dn.
      NeonSize size = static_cast<NeonSize>(instr->Bits(21, 20));
      bool min = instr->Bit(4) != 0;
      switch (size) {
        case Neon8:
          PairwiseMinMax<int8_t>(this, Vd, Vm, Vn, min);
          break;
        case Neon16:
          PairwiseMinMax<int16_t>(this, Vd, Vm, Vn, min);
          break;
        case Neon32:
          PairwiseMinMax<int32_t>(this, Vd, Vm, Vn, min);
          break;
        default:
          UNREACHABLE();
      }
    } else if (!u && opc == 0xB) {
      // vpadd.i<size> Dd, Dm, Dn.
      NeonSize size = static_cast<NeonSize>(instr->Bits(21, 20));
      switch (size) {
        case Neon8:
          PairwiseAdd<int8_t>(this, Vd, Vm, Vn);
          break;
        case Neon16:
          PairwiseAdd<int16_t>(this, Vd, Vm, Vn);
          break;
        case Neon32:
          PairwiseAdd<int32_t>(this, Vd, Vm, Vn);
          break;
        default:
          UNREACHABLE();
      }
    } else if (!u && opc == 0xD && !op1) {
      float src1[4], src2[4];
      get_neon_register(Vn, src1);
      get_neon_register(Vm, src2);
      for (int i = 0; i < 4; i++) {
        if (instr->Bit(21) == 0) {
          // vadd.f32 Qd, Qm, Qn.
          src1[i] = src1[i] + src2[i];
        } else {
          // vsub.f32 Qd, Qm, Qn.
          src1[i] = src1[i] - src2[i];
        }
      }
      set_neon_register(Vd, src1);
    } else if (!u && opc == 0xE && !sz && !op1) {
      // vceq.f32.
      float src1[4], src2[4];
      get_neon_register(Vn, src1);
      get_neon_register(Vm, src2);
      uint32_t dst[4];
      for (int i = 0; i < 4; i++) {
        dst[i] = (src1[i] == src2[i]) ? 0xFFFFFFFF : 0;
      }
      set_neon_register(Vd, dst);
    } else if (!u && opc == 0xF && op1) {
      float src1[4], src2[4];
      get_neon_register(Vn, src1);
      get_neon_register(Vm, src2);
      if (instr->Bit(21) == 0) {
        // vrecps.f32 Qd, Qm, Qn.
        for (int i = 0; i < 4; i++) {
          src1[i] = 2.0f - src1[i] * src2[i];
        }
      } else {
        // vrsqrts.f32 Qd, Qm, Qn.
        for (int i = 0; i < 4; i++) {
          src1[i] = (3.0f - src1[i] * src2[i]) * 0.5f;
        }
      }
      set_neon_register(Vd, src1);
    } else if (!u && opc == 0xF && !op1) {
      float src1[4], src2[4];
      get_neon_register(Vn, src1);
      get_neon_register(Vm, src2);
      // vmin/vmax.f32 Qd, Qm, Qn.
      bool min = instr->Bit(21) == 1;
      bool saved = FPSCR_default_NaN_mode_;
      FPSCR_default_NaN_mode_ = true;
      for (int i = 0; i < 4; i++) {
        // vmin returns default NaN if any input is NaN.
        src1[i] = canonicalizeNaN(MinMax(src1[i], src2[i], min));
      }
      FPSCR_default_NaN_mode_ = saved;
      set_neon_register(Vd, src1);
    } else if (u && opc == 0 && op1) {
      // vqadd.u<size> Qd, Qm, Qn.
      NeonSize size = static_cast<NeonSize>(instr->Bits(21, 20));
      switch (size) {
        case Neon8:
          AddSat<uint8_t>(this, Vd, Vm, Vn);
          break;
        case Neon16:
          AddSat<uint16_t>(this, Vd, Vm, Vn);
          break;
        case Neon32:
          AddSat<uint32_t>(this, Vd, Vm, Vn);
          break;
        default:
          UNREACHABLE();
      }
    } else if (u && opc == 1 && sz == 1 && op1) {
      // vbsl.size Qd, Qm, Qn.
      uint32_t dst[4], src1[4], src2[4];
      get_neon_register(Vd, dst);
      get_neon_register(Vn, src1);
      get_neon_register(Vm, src2);
      for (int i = 0; i < 4; i++) {
        dst[i] = (dst[i] & src1[i]) | (~dst[i] & src2[i]);
      }
      set_neon_register(Vd, dst);
    } else if (u && opc == 1 && sz == 0 && !q && op1) {
      // veor Dd, Dn, Dm
      uint64_t src1, src2;
      get_d_register(Vn, &src1);
      get_d_register(Vm, &src2);
      src1 ^= src2;
      set_d_register(Vd, &src1);
    } else if (u && opc == 1 && sz == 0 && q && op1) {
      // veor Qd, Qn, Qm
      uint32_t src1[4], src2[4];
      get_neon_register(Vn, src1);
      get_neon_register(Vm, src2);
      for (int i = 0; i < 4; i++) src1[i] ^= src2[i];
      set_neon_register(Vd, src1);
    } else if (u && opc == 1 && !op1) {
      // vrhadd.u<size> Qd, Qm, Qn.
      NeonSize size = static_cast<NeonSize>(instr->Bits(21, 20));
      switch (size) {
        case Neon8:
          Binop<uint8_t>(this, Vd, Vm, Vn, RoundingAverageUnsigned<uint8_t>);
          break;
        case Neon16:
          Binop<uint16_t>(this, Vd, Vm, Vn, RoundingAverageUnsigned<uint16_t>);
          break;
        case Neon32:
          Binop<uint32_t>(this, Vd, Vm, Vn, RoundingAverageUnsigned<uint32_t>);
          break;
        default:
          UNREACHABLE();
      }
    } else if (u && opc == 2 && op1) {
      // vqsub.u<size> Qd, Qm, Qn.
      NeonSize size = static_cast<NeonSize>(instr->Bits(21, 20));
      switch (size) {
        case Neon8:
          SubSat<uint8_t>(this, Vd, Vm, Vn);
          break;
        case Neon16:
          SubSat<uint16_t>(this, Vd, Vm, Vn);
          break;
        case Neon32:
          SubSat<uint32_t>(this, Vd, Vm, Vn);
          break;
        default:
          UNREACHABLE();
      }
    } else if (u && opc == 3) {
      // vcge/vcgt.u<size> Qd, Qm, Qn.
      bool ge = instr->Bit(4) == 1;
      NeonSize size = static_cast<NeonSize>(instr->Bits(21, 20));
      switch (size) {
        case Neon8:
          CompareGreater<uint8_t, kSimd128Size>(this, Vd, Vm, Vn, ge);
          break;
        case Neon16:
          CompareGreater<uint16_t, kSimd128Size>(this, Vd, Vm, Vn, ge);
          break;
        case Neon32:
          CompareGreater<uint32_t, kSimd128Size>(this, Vd, Vm, Vn, ge);
          break;
        default:
          UNREACHABLE();
      }
    } else if (u && opc == 4 && !op1) {
      // vshl u<size> Qd, Qm, Qn.
      NeonSize size = static_cast<NeonSize>(instr->Bits(21, 20));
      switch (size) {
        case Neon8:
          ShiftByRegister<uint8_t, int8_t, kSimd128Size>(this, Vd, Vm, Vn);
          break;
        case Neon16:
          ShiftByRegister<uint16_t, int16_t, kSimd128Size>(this, Vd, Vm, Vn);
          break;
        case Neon32:
          ShiftByRegister<uint32_t, int32_t, kSimd128Size>(this, Vd, Vm, Vn);
          break;
        case Neon64:
          ShiftByRegister<uint64_t, int64_t, kSimd128Size>(this, Vd, Vm, Vn);
          break;
        default:
          UNREACHABLE();
      }
    } else if (u && opc == 6) {
      // vmin/vmax.u<size> Qd, Qm, Qn.
      NeonSize size = static_cast<NeonSize>(instr->Bits(21, 20));
      bool min = instr->Bit(4) != 0;
      switch (size) {
        case Neon8:
          MinMax<uint8_t, kSimd128Size>(this, Vd, Vm, Vn, min);
          break;
        case Neon16:
          MinMax<uint16_t, kSimd128Size>(this, Vd, Vm, Vn, min);
          break;
        case Neon32:
          MinMax<uint32_t, kSimd128Size>(this, Vd, Vm, Vn, min);
          break;
        default:
          UNREACHABLE();
      }
    } else if (u && opc == 8 && !op1) {
      // vsub.size Qd, Qm, Qn.
      NeonSize size = static_cast<NeonSize>(instr->Bits(21, 20));
      switch (size) {
        case Neon8:
          Sub<uint8_t, kSimd128Size>(this, Vd, Vm, Vn);
          break;
        case Neon16:
          Sub<uint16_t, kSimd128Size>(this, Vd, Vm, Vn);
          break;
        case Neon32:
          Sub<uint32_t, kSimd128Size>(this, Vd, Vm, Vn);
          break;
        case Neon64:
          Sub<uint64_t, kSimd128Size>(this, Vd, Vm, Vn);
          break;
      }
    } else if (u && opc == 8 && op1) {
      // vceq.size Qd, Qm, Qn.
      NeonSize size = static_cast<NeonSize>(instr->Bits(21, 20));
      switch (size) {
        case Neon8:
          CompareEqual<uint8_t, kSimd128Size>(this, Vd, Vm, Vn);
          break;
        case Neon16:
          CompareEqual<uint16_t, kSimd128Size>(this, Vd, Vm, Vn);
          break;
        case Neon32:
          CompareEqual<uint32_t, kSimd128Size>(this, Vd, Vm, Vn);
          break;
        default:
          UNREACHABLE();
      }
    } else if (u && opc == 0xA) {
      // vpmin/vpmax.u<size> Dd, Dm, Dn.
      NeonSize size = static_cast<NeonSize>(instr->Bits(21, 20));
      bool min = instr->Bit(4) != 0;
      switch (size) {
        case Neon8:
          PairwiseMinMax<uint8_t>(this, Vd, Vm, Vn, min);
          break;
        case Neon16:
          PairwiseMinMax<uint16_t>(this, Vd, Vm, Vn, min);
          break;
        case Neon32:
          PairwiseMinMax<uint32_t>(this, Vd, Vm, Vn, min);
          break;
        default:
          UNREACHABLE();
      }
    } else if (u && opc == 0xD && sz == 0 && q && op1) {
      // vmul.f32 Qd, Qn, Qm
      float src1[4], src2[4];
      get_neon_register(Vn, src1);
      get_neon_register(Vm, src2);
      for (int i = 0; i < 4; i++) {
        src1[i] = src1[i] * src2[i];
      }
      set_neon_register(Vd, src1);
    } else if (u && opc == 0xD && sz == 0 && !q && !op1) {
      // vpadd.f32 Dd, Dn, Dm
      PairwiseAdd<float>(this, Vd, Vm, Vn);
    } else if (u && opc == 0xE && !op1) {
      // vcge/vcgt.f32 Qd, Qm, Qn
      bool ge = instr->Bit(21) == 0;
      float src1[4], src2[4];
      get_neon_register(Vn, src1);
      get_neon_register(Vm, src2);
      uint32_t dst[4];
      for (int i = 0; i < 4; i++) {
        if (ge) {
          dst[i] = src1[i] >= src2[i] ? 0xFFFFFFFFu : 0;
        } else {
          dst[i] = src1[i] > src2[i] ? 0xFFFFFFFFu : 0;
        }
      }
      set_neon_register(Vd, dst);
    } else if (u && opc == 0xB) {
      // vqrdmulh.<dt> Qd, Qm, Qn
      NeonSize size = static_cast<NeonSize>(instr->Bits(21, 20));
      if (size == Neon16) {
        Binop<int16_t>(this, Vd, Vm, Vn, SaturateRoundingQMul<int16_t>);
      } else {
        DCHECK_EQ(Neon32, size);
        Binop<int32_t>(this, Vd, Vm, Vn, SaturateRoundingQMul<int32_t>);
      }
    } else {
      UNIMPLEMENTED();
    }
    return;
  } else if (op0 == 1 && op1 == 0) {
    DecodeAdvancedSIMDTwoOrThreeRegisters(instr);
  } else if (op0 == 1 && op1 == 1) {
    // Advanced SIMD shifts and immediate generation.
    if (instr->Bits(21, 19) == 0 && instr->Bit(7) == 0) {
      VmovImmediate(this, instr);
    } else {
      // Advanced SIMD two registers and shift amount.
      int u = instr->Bit(24);
      int imm3H = instr->Bits(21, 19);
      int imm3L = instr->Bits(18, 16);
      int opc = instr->Bits(11, 8);
      int l = instr->Bit(7);
      int q = instr->Bit(6);
      int imm3H_L = imm3H << 1 | l;
      int imm7 = instr->Bits(21, 16);
      imm7 += (l << 6);
      int size = base::bits::RoundDownToPowerOfTwo32(imm7);
      NeonSize ns =
          static_cast<NeonSize>(base::bits::WhichPowerOfTwo(size >> 3));

      if (imm3H_L != 0 && opc == 0) {
        // vshr.s/u<size> Qd, Qm, shift
        int shift = 2 * size - imm7;
        int Vd = instr->VFPDRegValue(q ? kSimd128Precision : kDoublePrecision);
        int Vm = instr->VFPMRegValue(q ? kSimd128Precision : kDoublePrecision);
        switch (ns) {
          case Neon8:
            q ? ShiftRight<int8_t, kSimd128Size>(this, Vd, Vm, shift, u)
              : ShiftRight<int8_t, kDoubleSize>(this, Vd, Vm, shift, u);
            break;
          case Neon16:
            q ? ShiftRight<int16_t, kSimd128Size>(this, Vd, Vm, shift, u)
              : ShiftRight<int16_t, kDoubleSize>(this, Vd, Vm, shift, u);
            break;
          case Neon32:
            q ? ShiftRight<int32_t, kSimd128Size>(this, Vd, Vm, shift, u)
              : ShiftRight<int32_t, kDoubleSize>(this, Vd, Vm, shift, u);
            break;
          case Neon64:
            q ? ShiftRight<int64_t, kSimd128Size>(this, Vd, Vm, shift, u)
              : ShiftRight<int64_t, kDoubleSize>(this, Vd, Vm, shift, u);
            break;
        }
      } else if (imm3H_L != 0 && opc == 1) {
        // vsra Dd, Dm, #imm
        DCHECK(!q);  // Unimplemented for now.
        int shift = 2 * size - imm7;
        int Vd = instr->VFPDRegValue(kDoublePrecision);
        int Vm = instr->VFPMRegValue(kDoublePrecision);
        if (u) {
          switch (ns) {
            case Neon8:
              ShiftRightAccumulate<uint8_t, kDoubleSize>(this, Vd, Vm, shift);
              break;
            case Neon16:
              ShiftRightAccumulate<uint16_t, kDoubleSize>(this, Vd, Vm, shift);
              break;
            case Neon32:
              ShiftRightAccumulate<uint32_t, kDoubleSize>(this, Vd, Vm, shift);
              break;
            case Neon64:
              ShiftRightAccumulate<uint64_t, kDoubleSize>(this, Vd, Vm, shift);
              break;
          }
        } else {
          switch (ns) {
            case Neon8:
              ArithmeticShiftRightAccumulate<int8_t, kDoubleSize>(this, Vd, Vm,
                                                                  shift);
              break;
            case Neon16:
              ArithmeticShiftRightAccumulate<int16_t, kDoubleSize>(this, Vd, Vm,
                                                                   shift);
              break;
            case Neon32:
              ArithmeticShiftRightAccumulate<int32_t, kDoubleSize>(this, Vd, Vm,
                                                                   shift);
              break;
            case Neon64:
              ArithmeticShiftRightAccumulate<int64_t, kDoubleSize>(this, Vd, Vm,
                                                                   shift);
              break;
          }
        }
      } else if (imm3H_L != 0 && imm3L == 0 && opc == 0b1010 && !q) {
        if (u) {
          // vmovl unsigned
          if ((instr->VdValue() & 1) != 0) UNIMPLEMENTED();
          int Vd = instr->VFPDRegValue(kSimd128Precision);
          int Vm = instr->VFPMRegValue(kDoublePrecision);
          switch (imm3H) {
            case 1:
              Widen<uint8_t, uint16_t>(this, Vd, Vm);
              break;
            case 2:
              Widen<uint16_t, uint32_t>(this, Vd, Vm);
              break;
            case 4:
              Widen<uint32_t, uint64_t>(this, Vd, Vm);
              break;
            default:
              UNIMPLEMENTED();
          }
        } else {
          // vmovl signed
          if ((instr->VdValue() & 1) != 0) UNIMPLEMENTED();
          int Vd = instr->VFPDRegValue(kSimd128Precision);
          int Vm = instr->VFPMRegValue(kDoublePrecision);
          switch (imm3H) {
            case 1:
              Widen<int8_t, int16_t>(this, Vd, Vm);
              break;
            case 2:
              Widen<int16_t, int32_t>(this, Vd, Vm);
              break;
            case 4:
              Widen<int32_t, int64_t>(this, Vd, Vm);
              break;
            default:
              UNIMPLEMENTED();
          }
        }
      } else if (!u && imm3H_L != 0 && opc == 0b0101) {
        // vshl.i<size> Qd, Qm, shift
        int shift = imm7 - size;
        int Vd = instr->VFPDRegValue(kSimd128Precision);
        int Vm = instr->VFPMRegValue(kSimd128Precision);
        NeonSize ns =
            static_cast<NeonSize>(base::bits::WhichPowerOfTwo(size >> 3));
        switch (ns) {
          case Neon8:
            ShiftLeft<uint8_t, kSimd128Size>(this, Vd, Vm, shift);
            break;
          case Neon16:
            ShiftLeft<uint16_t, kSimd128Size>(this, Vd, Vm, shift);
            break;
          case Neon32:
            ShiftLeft<uint32_t, kSimd128Size>(this, Vd, Vm, shift);
            break;
          case Neon64:
            ShiftLeft<uint64_t, kSimd128Size>(this, Vd, Vm, shift);
            break;
        }
      } else if (u && imm3H_L != 0 && opc == 0b0100) {
        // vsri.<size> Dd, Dm, shift
        int shift = 2 * size - imm7;
        int Vd = instr->VFPDRegValue(kDoublePrecision);
        int Vm = instr->VFPMRegValue(kDoublePrecision);
        switch (size) {
          case 8:
            ShiftRightAndInsert<uint8_t, kDoubleSize>(this, Vd, Vm, shift);
            break;
          case 16:
            ShiftRightAndInsert<uint16_t, kDoubleSize>(this, Vd, Vm, shift);
            break;
          case 32:
            ShiftRightAndInsert<uint32_t, kDoubleSize>(this, Vd, Vm, shift);
            break;
          case 64:
            ShiftRightAndInsert<uint64_t, kDoubleSize>(this, Vd, Vm, shift);
            break;
          default:
            UNREACHABLE();
        }
      } else if (u && imm3H_L != 0 && opc == 0b0101) {
        // vsli.<size> Dd, Dm, shift
        int shift = imm7 - size;
        int Vd = instr->VFPDRegValue(kDoublePrecision);
        int Vm = instr->VFPMRegValue(kDoublePrecision);
        switch (size) {
          case 8:
            ShiftLeftAndInsert<uint8_t, kDoubleSize>(this, Vd, Vm, shift);
            break;
          case 16:
            ShiftLeftAndInsert<uint16_t, kDoubleSize>(this, Vd, Vm, shift);
            break;
          case 32:
            ShiftLeftAndInsert<uint32_t, kDoubleSize>(this, Vd, Vm, shift);
            break;
          case 64:
            ShiftLeftAndInsert<uint64_t, kDoubleSize>(this, Vd, Vm, shift);
            break;
          default:
            UNREACHABLE();
        }
      }
    }
    return;
  }
}

void Simulator::DecodeMemoryHintsAndBarriers(Instruction* instr) {
  switch (instr->SpecialValue()) {
    case 0xA:
    case 0xB:
      if ((instr->Bits(22, 20) == 5) && (instr->Bits(15, 12) == 0xF)) {
        // pld: ignore instruction.
      } else if (instr->SpecialValue() == 0xA && instr->Bits(22, 20) == 7) {
        // dsb, dmb, isb: ignore instruction for now.
        // TODO(binji): implement
        // Also refer to the ARMv6 CP15 equivalents in DecodeTypeCP15.
      } else {
        UNIMPLEMENTED();
      }
      break;
    default:
      UNIMPLEMENTED();
  }
}

void Simulator::DecodeAdvancedSIMDElementOrStructureLoadStore(
    Instruction* instr) {
  int op0 = instr->Bit(23);
  int op1 = instr->Bits(11, 10);

  if (!op0) {
    DecodeAdvancedSIMDLoadStoreMultipleStructures(instr);
  } else if (op1 == 0b11) {
    DecodeAdvancedSIMDLoadSingleStructureToAllLanes(instr);
  } else {
    DecodeAdvancedSIMDLoadStoreSingleStructureToOneLane(instr);
  }
}

void Simulator::DecodeAdvancedSIMDLoadStoreMultipleStructures(
    Instruction* instr) {
  int Vd = instr->VFPDRegValue(kDoublePrecision);
  int Rn = instr->VnValue();
  int Rm = instr->VmValue();
  int type = instr->Bits(11, 8);
  int32_t address = get_register(Rn);
  int regs = 0;
  switch (type) {
    case nlt_1:
      regs = 1;
      break;
    case nlt_2:
      regs = 2;
      break;
    case nlt_3:
      regs = 3;
      break;
    case nlt_4:
      regs = 4;
      break;
    default:
      UNIMPLEMENTED();
  }
  if (instr->Bit(21)) {
    // vld1
    int r = 0;
    while (r < regs) {
      uint32_t data[2];
      data[0] = ReadW(address);
      data[1] = ReadW(address + 4);
      set_d_register(Vd + r, data);
      address += 8;
      r++;
    }
  } else {
    // vst1
    int r = 0;
    while (r < regs) {
      uint32_t data[2];
      get_d_register(Vd + r, data);
      WriteW(address, data[0]);
      WriteW(address + 4, data[1]);
      address += 8;
      r++;
    }
  }
  AdvancedSIMDElementOrStructureLoadStoreWriteback(Rn, Rm, 8 * regs);
}

void Simulator::DecodeAdvancedSIMDLoadSingleStructureToAllLanes(
    Instruction* instr) {
  DCHECK_NE(0, instr->Bit(21));
  int N = instr->Bits(9, 8);

  int Vd = instr->VFPDRegValue(kDoublePrecision);
  int Rn = instr->VnValue();
  int Rm = instr->VmValue();
  int32_t address = get_register(Rn);

  if (!N) {
    // vld1 (single element to all lanes).
    int regs = instr->Bit(5) + 1;
    int size = instr->Bits(7, 6);
    uint32_t q_data[2];
    switch (size) {
      case Neon8: {
        uint8_t data = ReadBU(address);
        uint8_t* dst = reinterpret_cast<uint8_t*>(q_data);
        for (int i = 0; i < 8; i++) {
          dst[i] = data;
        }
        break;
      }
      case Neon16: {
        uint16_t data = ReadHU(address);
        uint16_t* dst = reinterpret_cast<uint16_t*>(q_data);
        for (int i = 0; i < 4; i++) {
          dst[i] = data;
        }
        break;
      }
      case Neon32: {
        uint32_t data = ReadW(address);
        for (int i = 0; i < 2; i++) {
          q_data[i] = data;
        }
        break;
      }
    }
    for (int r = 0; r < regs; r++) {
      set_neon_register<uint32_t, kDoubleSize>(Vd + r, q_data);
    }
    AdvancedSIMDElementOrStructureLoadStoreWriteback(Rn, Rm, 1 << size);
  } else {
    UNIMPLEMENTED();
  }
}

void Simulator::DecodeAdvancedSIMDLoadStoreSingleStructureToOneLane(
    Instruction* instr) {
  int L = instr->Bit(21);
  int size = instr->Bits(11, 10);
  int N = instr->Bits(9, 8);
  int Vd = instr->VFPDRegValue(kDoublePrecision);
  int Rn = instr->VnValue();
  int Rm = instr->VmValue();
  int32_t address = get_register(Rn);

  if (L && N == 0) {
    // vld1 (single element to one lane)
    DCHECK_NE(3, size);
    uint64_t dreg;
    get_d_register(Vd, &dreg);
    switch (size) {
      case Neon8: {
        uint64_t data = ReadBU(address);
        DCHECK_EQ(0, instr->Bit(4));
        int i = instr->Bits(7, 5) * 8;
        dreg = (dreg & ~(uint64_t{0xff} << i)) | (data << i);
        break;
      }
      case Neon16: {
        DCHECK_EQ(0, instr->Bits(5, 4));  // Alignment not supported.
        uint64_t data = ReadHU(address);
        int i = instr->Bits(7, 6) * 16;
        dreg = (dreg & ~(uint64_t{0xffff} << i)) | (data << i);
        break;
      }
      case Neon32: {
        DCHECK_EQ(0, instr->Bits(6, 4));  // Alignment not supported.
        uint64_t data = static_cast<unsigned>(ReadW(address));
        int i = instr->Bit(7) * 32;
        dreg = (dreg & ~(uint64_t{0xffffffff} << i)) | (data << i);
        break;
      }
      case Neon64: {
        // Should have been handled by vld1 (single element to all lanes).
        UNREACHABLE();
      }
    }
    set_d_register(Vd, &dreg);
    AdvancedSIMDElementOrStructureLoadStoreWriteback(Rn, Rm, 1 << size);
  } else if (!L && N == 0) {
    // vst1s (single element from one lane).
    DCHECK_NE(3, size);
    uint64_t dreg;
    get_d_register(Vd, &dreg);
    switch (size) {
      case Neon8: {
        DCHECK_EQ(0, instr->Bit(4));
        int i = instr->Bits(7, 5) * 8;
        dreg = (dreg >> i) & 0xff;
        WriteB(address, static_cast<uint8_t>(dreg));
        break;
      }
      case Neon16: {
        DCHECK_EQ(0, instr->Bits(5, 4));  // Alignment not supported.
        int i = instr->Bits(7, 6) * 16;
        dreg = (dreg >> i) & 0xffff;
        WriteH(address, static_cast<uint16_t>(dreg));
        break;
      }
      case Neon32: {
        DCHECK_EQ(0, instr->Bits(6, 4));  // Alignment not supported.
        int i = instr->Bit(7) * 32;
        dreg = (dreg >> i) & 0xffffffff;
        WriteW(address, base::bit_cast<int>(static_cast<uint32_t>(dreg)));
        break;
      }
      case Neon64: {
        // Should have been handled by vst1 (single element to all lanes).
        UNREACHABLE();
      }
    }
    AdvancedSIMDElementOrStructureLoadStoreWriteback(Rn, Rm, 1 << size);
  } else {
    UNIMPLEMENTED();
  }
}

void Simulator::DecodeFloatingPointDataProcessing(Instruction* instr) {
  switch (instr->SpecialValue()) {
    case 0x1D:
      if (instr->Opc1Value() == 0x7 && instr->Opc3Value() == 0x1 &&
          instr->Bits(11, 9) == 0x5 && instr->Bits(19, 18) == 0x2) {
        if (instr->SzValue() == 0x1) {
          int vm = instr->VFPMRegValue(kDoublePrecision);
          int vd = instr->VFPDRegValue(kDoublePrecision);
          double dm_value = get_double_from_d_register(vm).get_scalar();
          double dd_value = 0.0;
          int rounding_mode = instr->Bits(17, 16);
          switch (rounding_mode) {
            case 0x0:  // vrinta - round with ties to away from zero
              dd_value = round(dm_value);
              break;
            case 0x1: {  // vrintn - round with ties to even
              dd_value = nearbyint(dm_value);
              break;
            }
            case 0x2:  // vrintp - ceil
              dd_value = ceil(dm_value);
              break;
            case 0x3:  // vrintm - floor
              dd_value = floor(dm_value);
              break;
            default:
              UNREACHABLE();  // Case analysis is exhaustive.
          }
          dd_value = canonicalizeNaN(dd_value);
          set_d_register_from_double(vd, dd_value);
        } else {
          int m = instr->VFPMRegValue(kSinglePrecision);
          int d = instr->VFPDRegValue(kSinglePrecision);
          float sm_value = get_float_from_s_register(m).get_scalar();
          float sd_value = 0.0;
          int rounding_mode = instr->Bits(17, 16);
          switch (rounding_mode) {
            case 0x0:  // vrinta - round with ties to away from zero
              sd_value = roundf(sm_value);
              break;
            case 0x1: {  // vrintn - round with ties to even
              sd_value = nearbyintf(sm_value);
              break;
            }
            case 0x2:  // vrintp - ceil
              sd_value = ceilf(sm_value);
              break;
            case 0x3:  // vrintm - floor
              sd_value = floorf(sm_value);
              break;
            default:
              UNREACHABLE();  // Case analysis is exhaustive.
          }
          sd_value = canonicalizeNaN(sd_value);
          set_s_register_from_float(d, sd_value);
        }
      } else if ((instr->Opc1Value() == 0x4) && (instr->Bits(11, 9) == 0x5) &&
                 (instr->Bit(4) == 0x0)) {
        if (instr->SzValue() == 0x1) {
          int m = instr->VFPMRegValue(kDoublePrecision);
          int n = instr->VFPNRegValue(kDoublePrecision);
          int d = instr->VFPDRegValue(kDoublePrecision);
          double dn_value = get_double_from_d_register(n).get_scalar();
          double dm_value = get_double_from_d_register(m).get_scalar();
          double dd_value;
          if (instr->Bit(6) == 0x1) {  // vminnm
            if ((dn_value < dm_value) || std::isnan(dm_value)) {
              dd_value = dn_value;
            } else if ((dm_value < dn_value) || std::isnan(dn_value)) {
              dd_value = dm_value;
            } else {
              DCHECK_EQ(dn_value, dm_value);
              // Make sure that we pick the most negative sign for +/-0.
              dd_value = std::signbit(dn_value) ? dn_value : dm_value;
            }
          } else {  // vmaxnm
            if ((dn_value > dm_value) || std::isnan(dm_value)) {
              dd_value = dn_value;
            } else if ((dm_value > dn_value) || std::isnan(dn_value)) {
              dd_value = dm_value;
            } else {
              DCHECK_EQ(dn_value, dm_value);
              // Make sure that we pick the most positive sign for +/-0.
              dd_value = std::signbit(dn_value) ? dm_value : dn_value;
            }
          }
          dd_value = canonicalizeNaN(dd_value);
          set_d_register_from_double(d, dd_value);
        } else {
          int m = instr->VFPMRegValue(kSinglePrecision);
          int n = instr->VFPNRegValue(kSinglePrecision);
          int d = instr->VFPDRegValue(kSinglePrecision);
          float sn_value = get_float_from_s_register(n).get_scalar();
          float sm_value = get_float_from_s_register(m).get_scalar();
          float sd_value;
          if (instr->Bit(6) == 0x1) {  // vminnm
            if ((sn_value < sm_value) || std::isnan(sm_value)) {
              sd_value = sn_value;
            } else if ((sm_value < sn_value) || std::isnan(sn_value)) {
              sd_value = sm_value;
            } else {
              DCHECK_EQ(sn_value, sm_value);
              // Make sure that we pick the most negative sign for +/-0.
              sd_value = std::signbit(sn_value) ? sn_value : sm_value;
            }
          } else {  // vmaxnm
            if ((sn_value > sm_value) || std::isnan(sm_value)) {
              sd_value = sn_value;
            } else if ((sm_value > sn_value) || std::isnan(sn_value)) {
              sd_value = sm_value;
            } else {
              DCHECK_EQ(sn_value, sm_value);
              // Make sure that we pick the most positive sign for +/-0.
              sd_value = std::signbit(sn_value) ? sm_value : sn_value;
            }
          }
          sd_value = canonicalizeNaN(sd_value);
          set_s_register_from_float(d, sd_value);
        }
      } else {
        UNIMPLEMENTED();
      }
      break;
    case 0x1C:
      if ((instr->Bits(11, 9) == 0x5) && (instr->Bit(6) == 0) &&
          (instr->Bit(4) == 0)) {
        // VSEL* (floating-point)
        bool condition_holds;
        switch (instr->Bits(21, 20)) {
          case 0x0:  // VSELEQ
            condition_holds = (z_flag_ == 1);
            break;
          case 0x1:  // VSELVS
            condition_holds = (v_flag_ == 1);
            break;
          case 0x2:  // VSELGE
            condition_holds = (n_flag_ == v_flag_);
            break;
          case 0x3:  // VSELGT
            condition_holds = ((z_flag_ == 0) && (n_flag_ == v_flag_));
            break;
          default:
            UNREACHABLE();  // Case analysis is exhaustive.
        }
        if (instr->SzValue() == 0x1) {
          int n = instr->VFPNRegValue(kDoublePrecision);
          int m = instr->VFPMRegValue(kDoublePrecision);
          int d = instr->VFPDRegValue(kDoublePrecision);
          Float64 result = get_double_from_d_register(condition_holds ? n : m);
          set_d_register_from_double(d, result);
        } else {
          int n = instr->VFPNRegValue(kSinglePrecision);
          int m = instr->VFPMRegValue(kSinglePrecision);
          int d = instr->VFPDRegValue(kSinglePrecision);
          Float32 result = get_float_from_s_register(condition_holds ? n : m);
          set_s_register_from_float(d, result);
        }
      } else {
        UNIMPLEMENTED();
      }
      break;
    default:
      UNIMPLEMENTED();
  }
}

void Simulator::DecodeSpecialCondition(Instruction* instr) {
  int op0 = instr->Bits(25, 24);
  int op1 = instr->Bits(11, 9);
  int op2 = instr->Bit(4);

  if (instr->Bit(27) == 0) {
    DecodeUnconditional(instr);
  } else if ((instr->Bits(27, 26) == 0b11) && (op0 == 0b10) &&
             ((op1 >> 1) == 0b10) && !op2) {
    DecodeFloatingPointDataProcessing(instr);
  } else {
    UNIMPLEMENTED();
  }
}

// Executes the current instruction.
void Simulator::InstructionDecode(Instruction* instr) {
  if (v8_flags.check_icache) {
    CheckICache(i_cache(), instr);
  }
  pc_modified_ = false;
  if (InstructionTracingEnabled()) {
    disasm::NameConverter converter;
    disasm::Disassembler dasm(converter);
    // use a reasonably large buffer
    v8::base::EmbeddedVector<char, 256> buffer;
    dasm.InstructionDecode(buffer, reinterpret_cast<uint8_t*>(instr));
    PrintF("  0x%08" V8PRIxPTR "  %s\n", reinterpret_cast<intptr_t>(instr),
           buffer.begin());
  }
  if (instr->ConditionField() == kSpecialCondition) {
    DecodeSpecialCondition(instr);
  } else if (ConditionallyExecute(instr)) {
    switch (instr->TypeValue()) {
      case 0:
      case 1: {
        DecodeType01(instr);
        break;
      }
      case 2: {
        DecodeType2(instr);
        break;
      }
      case 3: {
        DecodeType3(instr);
        break;
      }
      case 4: {
        DecodeType4(instr);
        break;
      }
      case 5: {
        DecodeType5(instr);
        break;
      }
      case 6: {
        DecodeType6(instr);
        break;
      }
      case 7: {
        DecodeType7(instr);
        break;
      }
      default: {
        UNIMPLEMENTED();
      }
    }
  }
  if (!pc_modified_) {
    set_register(pc, reinterpret_cast<int32_t>(instr) + kInstrSize);
  }
}

void Simulator::Execute() {
  // Get the PC to simulate. Cannot use the accessor here as we need the
  // raw PC value and not the one used as input to arithmetic instructions.
  int program_counter = get_pc();

  if (v8_flags.stop_sim_at == 0) {
    // Fast version of the dispatch loop without checking whether the simulator
    // should be stopping at a particular executed instruction.
    while (program_counter != end_sim_pc) {
      Instruction* instr = reinterpret_cast<Instruction*>(program_counter);
      icount_ = base::AddWithWraparound(icount_, 1);
      InstructionDecode(instr);
      program_counter = get_pc();
    }
  } else {
    // v8_flags.stop_sim_at is at the non-default value. Stop in the debugger
    // when we reach the particular instruction count.
    while (program_counter != end_sim_pc) {
      Instruction* instr = reinterpret_cast<Instruction*>(program_counter);
      icount_ = base::AddWithWraparound(icount_, 1);
      if (icount_ == v8_flags.stop_sim_at) {
        ArmDebugger dbg(this);
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

  // Prepare to execute the code at entry
  set_register(pc, static_cast<int32_t>(entry));
  // Put down marker for end of simulation. The simulator will stop simulation
  // when the PC reaches this value. By saving the "end simulation" value into
  // the LR the simulation stops when returning to this call point.
  set_register(lr, end_sim_pc);

  // Remember the values of callee-saved registers.
  // The code below assumes that r9 is not used as sb (static base) in
  // simulator code and therefore is regarded as a callee-saved register.
  int32_t r4_val = get_register(r4);
  int32_t r5_val = get_register(r5);
  int32_t r6_val = get_register(r6);
  int32_t r7_val = get_register(r7);
  int32_t r8_val = get_register(r8);
  int32_t r9_val = get_register(r9);
  int32_t r10_val = get_register(r10);
  int32_t r11_val = get_register(r11);

  // Set up the callee-saved registers with a known value. To be able to check
  // that they are preserved properly across JS execution.
  int32_t callee_saved_value = icount_;
  set_register(r4, callee_saved_value);
  set_register(r5, callee_saved_value);
  set_register(r6, callee_saved_value);
  set_register(r7, callee_saved_value);
  set_register(r8, callee_saved_value);
  set_register(r9, callee_saved_value);
  set_register(r10, callee_saved_value);
  set_register(r11, callee_saved_value);

  // Start the simulation
  Execute();

  // Check that the callee-saved registers have been preserved.
  CHECK_EQ(callee_saved_value, get_register(r4));
  CHECK_EQ(callee_saved_value, get_register(r5));
  CHECK_EQ(callee_saved_value, get_register(r6));
  CHECK_EQ(callee_saved_value, get_register(r7));
  CHECK_EQ(callee_saved_value, get_register(r8));
  CHECK_EQ(callee_saved_value, get_register(r9));
  CHECK_EQ(callee_saved_value, get_register(r10));
  CHECK_EQ(callee_saved_value, get_register(r11));

  // Restore callee-saved registers with the original value.
  set_register(r4, r4_val);
  set_register(r5, r5_val);
  set_register(r6, r6_val);
  set_register(r7, r7_val);
  set_register(r8, r8_val);
  set_register(r9, r9_val);
  set_register(r10, r10_val);
  set_register(r11, r11_val);
}

intptr_t Simulator::CallImpl(Address entry, int argument_count,
                             const intptr_t* arguments) {
  // Set up arguments

  // First four arguments passed in registers.
  int reg_arg_count = std::min(4, argument_count);
  if (reg_arg_count > 0) set_register(r0, arguments[0]);
  if (reg_arg_count > 1) set_register(r1, arguments[1]);
  if (reg_arg_count > 2) set_register(r2, arguments[2]);
  if (reg_arg_count > 3) set_register(r3, arguments[3]);

  // Remaining arguments passed on stack.
  int original_stack = get_register(sp);
  // Compute position of stack on entry to generated code.
  int entry_stack = (original_stack - (argument_count - 4) * sizeof(int32_t));
  if (base::OS::ActivationFrameAlignment() != 0) {
    entry_stack &= -base::OS::ActivationFrameAlignment();
  }
  // Store remaining arguments on stack, from low to high memory.
  memcpy(reinterpret_cast<intptr_t*>(entry_stack), arguments + reg_arg_count,
         (argument_count - reg_arg_count) * sizeof(*arguments));
  set_register(sp, entry_stack);

  CallInternal(entry);

  // Pop stack passed arguments.
  CHECK_EQ(entry_stack, get_register(sp));
  set_register(sp, original_stack);

  return get_register(r0);
}

intptr_t Simulator::CallFPImpl(Address entry, double d0, double d1) {
  if (use_eabi_hardfloat()) {
    set_d_register_from_double(0, d0);
    set_d_register_from_double(1, d1);
  } else {
    set_register_pair_from_double(0, &d0);
    set_register_pair_from_double(2, &d1);
  }
  CallInternal(entry);
  return get_register(r0);
}

uintptr_t Simulator::PushAddress(uintptr_t address) {
  int new_sp = get_register(sp) - sizeof(uintptr_t);
  uintptr_t* stack_slot = reinterpret_cast<uintptr_t*>(new_sp);
  *stack_slot = address;
  set_register(sp, new_sp);
  return new_sp;
}

uintptr_t Simulator::PopAddress() {
  int current_sp = get_register(sp);
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

void Simulator::LocalMonitor::NotifyLoad(int32_t addr) {
  if (access_state_ == MonitorAccess::Exclusive) {
    // A load could cause a cache eviction which will affect the monitor. As a
    // result, it's most strict to unconditionally clear the local monitor on
    // load.
    Clear();
  }
}

void Simulator::LocalMonitor::NotifyLoadExcl(int32_t addr,
                                             TransactionSize size) {
  access_state_ = MonitorAccess::Exclusive;
  tagged_addr_ = addr;
  size_ = size;
}

void Simulator::LocalMonitor::NotifyStore(int32_t addr) {
  if (access_state_ == MonitorAccess::Exclusive) {
    // It is implementation-defined whether a non-exclusive store to an address
    // covered by the local monitor during exclusive access transitions to open
    // or exclusive access. See ARM DDI 0406C.b, A3.4.1.
    //
    // However, a store could cause a cache eviction which will affect the
    // monitor. As a result, it's most strict to unconditionally clear the
    // local monitor on store.
    Clear();
  }
}

bool Simulator::LocalMonitor::NotifyStoreExcl(int32_t addr,
                                              TransactionSize size) {
  if (access_state_ == MonitorAccess::Exclusive) {
    // It is allowed for a processor to require that the address matches
    // exactly (A3.4.5), so this comparison does not mask addr.
    if (addr == tagged_addr_ && size_ == size) {
      Clear();
      return true;
    } else {
      // It is implementation-defined whether an exclusive store to a
      // non-tagged address will update memory. Behavior is unpredictable if
      // the transaction size of the exclusive store differs from that of the
      // exclusive load. See ARM DDI 0406C.b, A3.4.5.
      Clear();
      return false;
    }
  } else {
    DCHECK(access_state_ == MonitorAccess::Open);
    return false;
  }
}

Simulator::GlobalMonitor::Processor::Processor()
    : access_state_(MonitorAccess::Open),
      tagged_addr_(0),
      next_(nullptr),
      prev_(nullptr),
      failure_counter_(0) {}

void Simulator::GlobalMonitor::Processor::Clear_Locked() {
  access_state_ = MonitorAccess::Open;
  tagged_addr_ = 0;
}

void Simulator::GlobalMonitor::Processor::NotifyLoadExcl_Locked(int32_t addr) {
  access_state_ = MonitorAccess::Exclusive;
  tagged_addr_ = addr;
}

void Simulator::GlobalMonitor::Processor::NotifyStore_Locked(
    int32_t addr, bool is_requesting_processor) {
  if (access_state_ == MonitorAccess::Exclusive) {
    // It is implementation-defined whether a non-exclusive store by the
    // requesting processor to an address covered by the global monitor
    // during exclusive access transitions to open or exclusive access.
    //
    // For any other processor, the access state always transitions to open
    // access.
    //
    // See ARM DDI 0406C.b, A3.4.2.
    //
    // However, similar to the local monitor, it is possible that a store
    // caused a cache eviction, which can affect the montior, so
    // conservatively, we always clear the monitor.
    Clear_Locked();
  }
}

bool Simulator::GlobalMonitor::Processor::NotifyStoreExcl_Locked(
    int32_t addr, bool is_requesting_processor) {
  if (access_state_ == MonitorAccess::Exclusive) {
    if (is_requesting_processor) {
      // It is allowed for a processor to require that the address matches
      // exactly (A3.4.5), so this comparison does not mask addr.
      if (addr == tagged_addr_) {
        // The access state for the requesting processor after a successful
        // exclusive store is implementation-defined, but according to the ARM
        // DDI, this has no effect on the subsequent operation of the global
        // monitor.
        Clear_Locked();
        // Introduce occasional strex failures. This is to simulate the
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
      // another processor so the implementation is more conservative (i.e. the
      // granularity of locking is as large as possible.)
      Clear_Locked();
      return false;
    }
  }
  return false;
}

void Simulator::GlobalMonitor::NotifyLoadExcl_Locked(int32_t addr,
                                                     Processor* processor) {
  processor->NotifyLoadExcl_Locked(addr);
  PrependProcessor_Locked(processor);
}

void Simulator::GlobalMonitor::NotifyStore_Locked(int32_t addr,
                                                  Processor* processor) {
  // Notify each processor of the store operation.
  for (Processor* iter = head_; iter; iter = iter->next_) {
    bool is_requesting_processor = iter == processor;
    iter->NotifyStore_Locked(addr, is_requesting_processor);
  }
}

bool Simulator::GlobalMonitor::NotifyStoreExcl_Locked(int32_t addr,
                                                      Processor* processor) {
  DCHECK(IsProcessorInLinkedList_Locked(processor));
  if (processor->NotifyStoreExcl_Locked(addr, true)) {
    // Notify the other processors that this StoreExcl succeeded.
    for (Processor* iter = head_; iter; iter = iter->next_) {
      if (iter != processor) {
        iter->NotifyStoreExcl_Locked(addr, false);
      }
    }
    return true;
  } else {
    return false;
  }
}

bool Simulator::GlobalMonitor::IsProcessorInLinkedList_Locked(
    Processor* processor) const {
  return head_ == processor || processor->next_ || processor->prev_;
}

void Simulator::GlobalMonitor::PrependProcessor_Locked(Processor* processor) {
  if (IsProcessorInLinkedList_Locked(processor)) {
    return;
  }

  if (head_) {
    head_->prev_ = processor;
  }
  processor->prev_ = nullptr;
  processor->next_ = head_;
  head_ = processor;
}

void Simulator::GlobalMonitor::RemoveProcessor(Processor* processor) {
  base::MutexGuard lock_guard(&mutex);
  if (!IsProcessorInLinkedList_Locked(processor)) {
    return;
  }

  if (processor->prev_) {
    processor->prev_->next_ = processor->next_;
  } else {
    head_ = processor->next_;
  }
  if (processor->next_) {
    processor->next_->prev_ = processor->prev_;
  }
  processor->prev_ = nullptr;
  processor->next_ = nullptr;
}

#undef SScanF

}  // namespace internal
}  // namespace v8

//
// The following functions are used by our gdb macros.
//
V8_DONT_STRIP_SYMBOL
V8_EXPORT_PRIVATE extern bool _v8_internal_Simulator_ExecDebugCommand(
    const char* command) {
  i::Isolate* isolate = i::Isolate::Current();
  if (!isolate) {
    fprintf(stderr, "No V8 Isolate found\n");
    return false;
  }
  i::Simulator* simulator = i::Simulator::current(isolate);
  if (!simulator) {
    fprintf(stderr, "No Arm simulator found\n");
    return false;
  }
  // Copy the command so that the simulator can take ownership of it.
  size_t len = strlen(command);
  i::ArrayUniquePtr<char> command_copy(i::NewArray<char>(len + 1));
  i::MemCopy(command_copy.get(), command, len + 1);
  return i::ArmDebugger(simulator).ExecDebugCommand(std::move(command_copy));
}

#endif  // USE_SIMULATOR
