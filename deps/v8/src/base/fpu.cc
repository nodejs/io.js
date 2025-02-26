// Copyright 2025 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/base/fpu.h"

#include <float.h>

#include "include/v8config.h"

namespace v8::base {

#if defined(V8_HOST_ARCH_X64) || defined(V8_HOST_ARCH_IA32)
#if defined(V8_CC_GNU)
namespace {
// Two bits on Intel CPUs, for FTZ (flush denormalized results to zero) and DAZ
// (flush denormalized inputs to zero).
constexpr int kFlushDenormToZeroBits = 0x8040;
int GetCSR() {
  int result;
  asm volatile("stmxcsr %0" : "=m"(result));
  return result;
}

void SetCSR(int a) {
  int temp = a;
  asm volatile("ldmxcsr %0" : : "m"(temp));
}
}  // namespace

bool FPU::GetFlushDenormals() {
  int csr = GetCSR();
  return csr & kFlushDenormToZeroBits;
}

void FPU::SetFlushDenormals(bool value) {
  int old_csr = GetCSR();
  int new_csr = value ? old_csr | kFlushDenormToZeroBits
                      : old_csr & ~kFlushDenormToZeroBits;
  SetCSR(new_csr);
}
#elif defined(V8_CC_MSVC)
bool FPU::GetFlushDenormals() {
  unsigned int csr;
  _controlfp_s(&csr, 0, 0);
  return (csr & _MCW_DN) == _DN_FLUSH;
}

void FPU::SetFlushDenormals(bool value) {
  unsigned int csr;
  _controlfp_s(&csr, value ? _DN_FLUSH : _DN_SAVE, _MCW_DN);
}
#else
#error "Unsupported compiler"
#endif

#elif defined(V8_HOST_ARCH_ARM64) || defined(V8_HOST_ARCH_ARM)

namespace {
// Bit 24 is the flush-to-zero mode control bit. Setting it to 1 flushes
// denormals to 0.
constexpr int kFlushDenormToZeroBit = (1 << 24);
int GetStatusWord() {
  int result;
#if defined(V8_HOST_ARCH_ARM64)
  asm volatile("mrs %x[result], FPCR" : [result] "=r"(result));
#else
  asm volatile("vmrs %[result], FPSCR" : [result] "=r"(result));
#endif
  return result;
}

void SetStatusWord(int a) {
#if defined(V8_HOST_ARCH_ARM64)
  asm volatile("msr FPCR, %x[src]" : : [src] "r"(a));
#else
  asm volatile("vmsr FPSCR, %[src]" : : [src] "r"(a));
#endif
}
}  // namespace

bool FPU::GetFlushDenormals() {
  int csr = GetStatusWord();
  return csr & kFlushDenormToZeroBit;
}

void FPU::SetFlushDenormals(bool value) {
  int old_csr = GetStatusWord();
  int new_csr = value ? old_csr | kFlushDenormToZeroBit
                      : old_csr & ~kFlushDenormToZeroBit;
  SetStatusWord(new_csr);
}

#else

bool FPU::GetFlushDenormals() { return false; }
void FPU::SetFlushDenormals(bool value) {}

#endif

}  // namespace v8::base
