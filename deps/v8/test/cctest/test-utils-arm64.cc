// Copyright 2013 the V8 project authors. All rights reserved.
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
//     * Redistributions of source code must retain the above copyright
//       notice, this list of conditions and the following disclaimer.
//     * Redistributions in binary form must reproduce the above
//       copyright notice, this list of conditions and the following
//       disclaimer in the documentation and/or other materials provided
//       with the distribution.
//     * Neither the name of Google Inc. nor the names of its
//       contributors may be used to endorse or promote products derived
//       from this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

#include "test/cctest/test-utils-arm64.h"

#include "src/base/template-utils.h"
#include "src/codegen/arm64/assembler-arm64-inl.h"
#include "src/codegen/macro-assembler-inl.h"

namespace v8 {
namespace internal {


#define __ masm->


bool Equal32(uint32_t expected, const RegisterDump*, uint32_t result) {
  if (result != expected) {
    printf("Expected 0x%08" PRIx32 "\t Found 0x%08" PRIx32 "\n",
           expected, result);
  }

  return expected == result;
}


bool Equal64(uint64_t expected, const RegisterDump*, uint64_t result) {
  if (result != expected) {
    printf("Expected 0x%016" PRIx64 "\t Found 0x%016" PRIx64 "\n",
           expected, result);
  }

  return expected == result;
}

bool Equal128(vec128_t expected, const RegisterDump*, vec128_t result) {
  if ((result.h != expected.h) || (result.l != expected.l)) {
    printf("Expected 0x%016" PRIx64 "%016" PRIx64
           "\t "
           "Found 0x%016" PRIx64 "%016" PRIx64 "\n",
           expected.h, expected.l, result.h, result.l);
  }

  return ((expected.h == result.h) && (expected.l == result.l));
}

bool EqualFP32(float expected, const RegisterDump*, float result) {
  if (base::bit_cast<uint32_t>(expected) == base::bit_cast<uint32_t>(result)) {
    return true;
  } else {
    if (std::isnan(expected) || (expected == 0.0)) {
      printf("Expected 0x%08" PRIx32 "\t Found 0x%08" PRIx32 "\n",
             base::bit_cast<uint32_t>(expected),
             base::bit_cast<uint32_t>(result));
    } else {
      printf("Expected %.9f (0x%08" PRIx32
             ")\t "
             "Found %.9f (0x%08" PRIx32 ")\n",
             expected, base::bit_cast<uint32_t>(expected), result,
             base::bit_cast<uint32_t>(result));
    }
    return false;
  }
}


bool EqualFP64(double expected, const RegisterDump*, double result) {
  if (base::bit_cast<uint64_t>(expected) == base::bit_cast<uint64_t>(result)) {
    return true;
  }

  if (std::isnan(expected) || (expected == 0.0)) {
    printf("Expected 0x%016" PRIx64 "\t Found 0x%016" PRIx64 "\n",
           base::bit_cast<uint64_t>(expected),
           base::bit_cast<uint64_t>(result));
  } else {
    printf("Expected %.17f (0x%016" PRIx64
           ")\t "
           "Found %.17f (0x%016" PRIx64 ")\n",
           expected, base::bit_cast<uint64_t>(expected), result,
           base::bit_cast<uint64_t>(result));
  }
  return false;
}


bool Equal32(uint32_t expected, const RegisterDump* core, const Register& reg) {
  CHECK(reg.Is32Bits());
  // Retrieve the corresponding X register so we can check that the upper part
  // was properly cleared.
  int64_t result_x = core->xreg(reg.code());
  if ((result_x & 0xFFFFFFFF00000000L) != 0) {
    printf("Expected 0x%08" PRIx32 "\t Found 0x%016" PRIx64 "\n",
           expected, result_x);
    return false;
  }
  uint32_t result_w = core->wreg(reg.code());
  return Equal32(expected, core, result_w);
}


bool Equal64(uint64_t expected,
             const RegisterDump* core,
             const Register& reg) {
  CHECK(reg.Is64Bits());
  uint64_t result = core->xreg(reg.code());
  return Equal64(expected, core, result);
}

bool Equal128(uint64_t expected_h, uint64_t expected_l,
              const RegisterDump* core, const VRegister& vreg) {
  CHECK(vreg.Is128Bits());
  vec128_t expected = {expected_l, expected_h};
  vec128_t result = core->qreg(vreg.code());
  return Equal128(expected, core, result);
}

bool EqualFP32(float expected, const RegisterDump* core,
               const VRegister& fpreg) {
  CHECK(fpreg.Is32Bits());
  // Retrieve the corresponding D register so we can check that the upper part
  // was properly cleared.
  uint64_t result_64 = core->dreg_bits(fpreg.code());
  if ((result_64 & 0xFFFFFFFF00000000L) != 0) {
    printf("Expected 0x%08" PRIx32 " (%f)\t Found 0x%016" PRIx64 "\n",
           base::bit_cast<uint32_t>(expected), expected, result_64);
    return false;
  }

  return EqualFP32(expected, core, core->sreg(fpreg.code()));
}

bool EqualFP64(double expected, const RegisterDump* core,
               const VRegister& fpreg) {
  CHECK(fpreg.Is64Bits());
  return EqualFP64(expected, core, core->dreg(fpreg.code()));
}


bool Equal64(const Register& reg0,
             const RegisterDump* core,
             const Register& reg1) {
  CHECK(reg0.Is64Bits() && reg1.Is64Bits());
  int64_t expected = core->xreg(reg0.code());
  int64_t result = core->xreg(reg1.code());
  return Equal64(expected, core, result);
}


static char FlagN(uint32_t flags) {
  return (flags & NFlag) ? 'N' : 'n';
}


static char FlagZ(uint32_t flags) {
  return (flags & ZFlag) ? 'Z' : 'z';
}


static char FlagC(uint32_t flags) {
  return (flags & CFlag) ? 'C' : 'c';
}


static char FlagV(uint32_t flags) {
  return (flags & VFlag) ? 'V' : 'v';
}


bool EqualNzcv(uint32_t expected, uint32_t result) {
  CHECK_EQ(expected & ~NZCVFlag, 0);
  CHECK_EQ(result & ~NZCVFlag, 0);
  if (result != expected) {
    printf("Expected: %c%c%c%c\t Found: %c%c%c%c\n",
        FlagN(expected), FlagZ(expected), FlagC(expected), FlagV(expected),
        FlagN(result), FlagZ(result), FlagC(result), FlagV(result));
    return false;
  }

  return true;
}

bool EqualV8Registers(const RegisterDump* a, const RegisterDump* b) {
  CPURegList available_regs = kCallerSaved;
  available_regs.Combine(kCalleeSaved);
  while (!available_regs.IsEmpty()) {
    int i = available_regs.PopLowestIndex().code();
    if (a->xreg(i) != b->xreg(i)) {
      printf("x%d\t Expected 0x%016" PRIx64 "\t Found 0x%016" PRIx64 "\n",
             i, a->xreg(i), b->xreg(i));
      return false;
    }
  }

  for (unsigned i = 0; i < kNumberOfVRegisters; i++) {
    uint64_t a_bits = a->dreg_bits(i);
    uint64_t b_bits = b->dreg_bits(i);
    if (a_bits != b_bits) {
      printf("d%d\t Expected 0x%016" PRIx64 "\t Found 0x%016" PRIx64 "\n",
             i, a_bits, b_bits);
      return false;
    }
  }

  return true;
}

RegList PopulateRegisterArray(Register* w, Register* x, Register* r,
                              int reg_size, int reg_count, RegList allowed) {
  RegList list;
  int i = 0;
  // Only assign allowed registers.
  for (Register reg : allowed) {
    if (i == reg_count) break;
    if (r) {
      r[i] = Register::Create(reg.code(), reg_size);
    }
    if (x) {
      x[i] = reg.X();
    }
    if (w) {
      w[i] = reg.W();
    }
    list.set(reg);
    i++;
  }
  // Check that we got enough registers.
  CHECK_EQ(list.Count(), reg_count);

  return list;
}

DoubleRegList PopulateVRegisterArray(VRegister* s, VRegister* d, VRegister* v,
                                     int reg_size, int reg_count,
                                     DoubleRegList allowed) {
  DoubleRegList list;
  int i = 0;
  // Only assigned allowed registers.
  for (VRegister reg : allowed) {
    if (i == reg_count) break;
    if (v) {
      v[i] = VRegister::Create(reg.code(), reg_size);
    }
    if (d) {
      d[i] = reg.D();
    }
    if (s) {
      s[i] = reg.S();
    }
    list.set(reg);
    i++;
  }
  // Check that we got enough registers.
  CHECK_EQ(list.Count(), reg_count);

  return list;
}

void Clobber(MacroAssembler* masm, RegList reg_list, uint64_t const value) {
  Register first = NoReg;
  for (Register reg : reg_list) {
    Register xn = reg.X();
    // We should never write into sp here.
    CHECK_NE(xn, sp);
    if (!xn.IsZero()) {
      if (!first.is_valid()) {
        // This is the first register we've hit, so construct the literal.
        __ Mov(xn, value);
        first = xn;
      } else {
        // We've already loaded the literal, so reuse the value already
        // loaded into the first register we hit.
        __ Mov(xn, first);
      }
    }
  }
}

void ClobberFP(MacroAssembler* masm, DoubleRegList reg_list,
               double const value) {
  VRegister first = NoVReg;
  for (VRegister reg : reg_list) {
    VRegister dn = reg.D();
    if (!first.is_valid()) {
      // This is the first register we've hit, so construct the literal.
      __ Fmov(dn, value);
      first = dn;
    } else {
      // We've already loaded the literal, so reuse the value already loaded
      // into the first register we hit.
      __ Fmov(dn, first);
    }
  }
}

void Clobber(MacroAssembler* masm, CPURegList reg_list) {
  if (reg_list.type() == CPURegister::kRegister) {
    // This will always clobber X registers.
    Clobber(masm, RegList::FromBits(static_cast<uint32_t>(reg_list.bits())));
  } else if (reg_list.type() == CPURegister::kVRegister) {
    // This will always clobber D registers.
    ClobberFP(masm,
              DoubleRegList::FromBits(static_cast<uint32_t>(reg_list.bits())));
  } else {
    UNREACHABLE();
  }
}


void RegisterDump::Dump(MacroAssembler* masm) {
  // Ensure that we don't unintentionally clobber any registers.
  uint64_t old_tmp_list = masm->TmpList()->bits();
  uint64_t old_fptmp_list = masm->FPTmpList()->bits();
  masm->TmpList()->set_bits(0);
  masm->FPTmpList()->set_bits(0);

  // Preserve some temporary registers.
  Register dump_base = x0;
  Register dump = x1;
  Register tmp = x2;
  Register dump_base_w = dump_base.W();
  Register dump_w = dump.W();
  Register tmp_w = tmp.W();

  // Offsets into the dump_ structure.
  const int x_offset = offsetof(dump_t, x_);
  const int w_offset = offsetof(dump_t, w_);
  const int d_offset = offsetof(dump_t, d_);
  const int s_offset = offsetof(dump_t, s_);
  const int q_offset = offsetof(dump_t, q_);
  const int sp_offset = offsetof(dump_t, sp_);
  const int wsp_offset = offsetof(dump_t, wsp_);
  const int flags_offset = offsetof(dump_t, flags_);

  __ Push(xzr, dump_base, dump, tmp);

  // Load the address where we will dump the state.
  __ Mov(dump_base, reinterpret_cast<uint64_t>(&dump_));

  // Dump the stack pointer (sp and wsp).
  // The stack pointer cannot be stored directly; it needs to be moved into
  // another register first. Also, we pushed four X registers, so we need to
  // compensate here.
  __ Add(tmp, sp, 4 * kXRegSize);
  __ Str(tmp, MemOperand(dump_base, sp_offset));
  __ Add(tmp_w, wsp, 4 * kXRegSize);
  __ Str(tmp_w, MemOperand(dump_base, wsp_offset));

  // Dump X registers.
  __ Add(dump, dump_base, x_offset);
  for (unsigned i = 0; i < kNumberOfRegisters; i += 2) {
    __ Stp(Register::XRegFromCode(i), Register::XRegFromCode(i + 1),
           MemOperand(dump, i * kXRegSize));
  }

  // Dump W registers.
  __ Add(dump, dump_base, w_offset);
  for (unsigned i = 0; i < kNumberOfRegisters; i += 2) {
    __ Stp(Register::WRegFromCode(i), Register::WRegFromCode(i + 1),
           MemOperand(dump, i * kWRegSize));
  }

  // Dump D registers.
  __ Add(dump, dump_base, d_offset);
  for (unsigned i = 0; i < kNumberOfVRegisters; i += 2) {
    __ Stp(VRegister::DRegFromCode(i), VRegister::DRegFromCode(i + 1),
           MemOperand(dump, i * kDRegSize));
  }

  // Dump S registers.
  __ Add(dump, dump_base, s_offset);
  for (unsigned i = 0; i < kNumberOfVRegisters; i += 2) {
    __ Stp(VRegister::SRegFromCode(i), VRegister::SRegFromCode(i + 1),
           MemOperand(dump, i * kSRegSize));
  }

  // Dump Q registers.
  __ Add(dump, dump_base, q_offset);
  for (unsigned i = 0; i < kNumberOfVRegisters; i += 2) {
    __ Stp(VRegister::QRegFromCode(i), VRegister::QRegFromCode(i + 1),
           MemOperand(dump, i * kQRegSize));
  }

  // Dump the flags.
  __ Mrs(tmp, NZCV);
  __ Str(tmp, MemOperand(dump_base, flags_offset));

  // To dump the values that were in tmp amd dump, we need a new scratch
  // register.  We can use any of the already dumped registers since we can
  // easily restore them.
  Register dump2_base = x10;
  Register dump2 = x11;
  CHECK(!AreAliased(dump_base, dump, tmp, dump2_base, dump2));

  // Don't lose the dump_ address.
  __ Mov(dump2_base, dump_base);

  __ Pop(tmp, dump, dump_base, xzr);

  __ Add(dump2, dump2_base, w_offset);
  __ Str(dump_base_w, MemOperand(dump2, dump_base.code() * kWRegSize));
  __ Str(dump_w, MemOperand(dump2, dump.code() * kWRegSize));
  __ Str(tmp_w, MemOperand(dump2, tmp.code() * kWRegSize));

  __ Add(dump2, dump2_base, x_offset);
  __ Str(dump_base, MemOperand(dump2, dump_base.code() * kXRegSize));
  __ Str(dump, MemOperand(dump2, dump.code() * kXRegSize));
  __ Str(tmp, MemOperand(dump2, tmp.code() * kXRegSize));

  // Finally, restore dump2_base and dump2.
  __ Ldr(dump2_base, MemOperand(dump2, dump2_base.code() * kXRegSize));
  __ Ldr(dump2, MemOperand(dump2, dump2.code() * kXRegSize));

  // Restore the MacroAssembler's scratch registers.
  masm->TmpList()->set_bits(old_tmp_list);
  masm->FPTmpList()->set_bits(old_fptmp_list);

  completed_ = true;
}

}  // namespace internal
}  // namespace v8

#undef __
