// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_BASE_IMMEDIATE_CRASH_H_
#define V8_BASE_IMMEDIATE_CRASH_H_

#include "include/v8config.h"
#include "src/base/build_config.h"

// Crashes in the fastest possible way with no attempt at logging.
// There are several constraints; see http://crbug.com/664209 for more context.
//
// - TRAP_SEQUENCE_() must be fatal. It should not be possible to ignore the
//   resulting exception or simply hit 'continue' to skip over it in a debugger.
// - Different instances of TRAP_SEQUENCE_() must not be folded together, to
//   ensure crash reports are debuggable. Unlike __builtin_trap(), asm volatile
//   blocks will not be folded together.
//   Note: TRAP_SEQUENCE_() previously required an instruction with a unique
//   nonce since unlike clang, GCC folds together identical asm volatile
//   blocks.
// - TRAP_SEQUENCE_() must produce a signal that is distinct from an invalid
//   memory access.
// - TRAP_SEQUENCE_() must be treated as a set of noreturn instructions.
//   __builtin_unreachable() is used to provide that hint here. clang also uses
//   this as a heuristic to pack the instructions in the function epilogue to
//   improve code density.
//
// Additional properties that are nice to have:
// - TRAP_SEQUENCE_() should be as compact as possible.
// - The first instruction of TRAP_SEQUENCE_() should not change, to avoid
//   shifting crash reporting clusters. As a consequence of this, explicit
//   assembly is preferred over intrinsics.
//   Note: this last bullet point may no longer be true, and may be removed in
//   the future.

// Note: TRAP_SEQUENCE Is currently split into two macro helpers due to the fact
// that clang emits an actual instruction for __builtin_unreachable() on certain
// platforms (see https://crbug.com/958675). In addition, the int3/bkpt/brk will
// be removed in followups, so splitting it up like this now makes it easy to
// land the followups.

#if V8_CC_GNU

#if V8_HOST_ARCH_X64 || V8_HOST_ARCH_IA32

// TODO(https://crbug.com/958675): In theory, it should be possible to use just
// int3. However, there are a number of crashes with SIGILL as the exception
// code, so it seems likely that there's a signal handler that allows execution
// to continue after SIGTRAP.
#define TRAP_SEQUENCE1_() asm volatile("int3")

#if V8_OS_DARWIN
// Intentionally empty: __builtin_unreachable() is always part of the sequence
// (see IMMEDIATE_CRASH below) and already emits a ud2 on Mac.
#define TRAP_SEQUENCE2_() asm volatile("")
#else
#define TRAP_SEQUENCE2_() asm volatile("ud2")
#endif  // V8_OS_DARWIN

#elif V8_HOST_ARCH_ARM

// bkpt will generate a SIGBUS when running on armv7 and a SIGTRAP when running
// as a 32 bit userspace app on arm64. There doesn't seem to be any way to
// cause a SIGTRAP from userspace without using a syscall (which would be a
// problem for sandboxing).
// TODO(https://crbug.com/958675): Remove bkpt from this sequence.
#define TRAP_SEQUENCE1_() asm volatile("bkpt #0")
#define TRAP_SEQUENCE2_() asm volatile("udf #0")

#elif V8_HOST_ARCH_ARM64

// This will always generate a SIGTRAP on arm64.
// TODO(https://crbug.com/958675): Remove brk from this sequence.
#define TRAP_SEQUENCE1_() asm volatile("brk #0")
#define TRAP_SEQUENCE2_() asm volatile("hlt #0")

#elif V8_HOST_ARCH_PPC64

// GDB software breakpoint instruction.
// Same as `bkpt` under the assembler.
#if V8_OS_AIX
#define TRAP_SEQUENCE1_() asm volatile(".vbyte 4,0x7D821008");
#else
#define TRAP_SEQUENCE1_() asm volatile(".4byte 0x7D821008");
#endif
#define TRAP_SEQUENCE2_() asm volatile("")

#elif V8_OS_ZOS

#define TRAP_SEQUENCE1_() __builtin_trap()
#define TRAP_SEQUENCE2_() asm volatile("")

#elif V8_HOST_ARCH_S390

// GDB software breakpoint instruction.
// Same as `bkpt` under the assembler.
#define TRAP_SEQUENCE1_() asm volatile(".2byte 0x0001");
#define TRAP_SEQUENCE2_() asm volatile("")

#else

// Crash report accuracy will not be guaranteed on other architectures, but at
// least this will crash as expected.
#define TRAP_SEQUENCE1_() __builtin_trap()
#define TRAP_SEQUENCE2_() asm volatile("")

#endif  // V8_HOST_ARCH_*

#elif V8_CC_MSVC

#if !defined(__clang__)

// MSVC x64 doesn't support inline asm, so use the MSVC intrinsic.
#define TRAP_SEQUENCE1_() __debugbreak()
#define TRAP_SEQUENCE2_()

#elif V8_HOST_ARCH_ARM64

// Windows ARM64 uses "BRK #F000" as its breakpoint instruction, and
// __debugbreak() generates that in both VC++ and clang.
#define TRAP_SEQUENCE1_() __debugbreak()
// Intentionally empty: __builtin_unreachable() is always part of the sequence
// (see IMMEDIATE_CRASH below) and already emits a ud2 on Win64,
// https://crbug.com/958373
#define TRAP_SEQUENCE2_() __asm volatile("")

#else

#define TRAP_SEQUENCE1_() asm volatile("int3")
#define TRAP_SEQUENCE2_() asm volatile("ud2")

#endif  // __clang__

#else

#error No supported trap sequence!

#endif  // V8_CC_GNU

#define TRAP_SEQUENCE_() \
  do {                   \
    TRAP_SEQUENCE1_();   \
    TRAP_SEQUENCE2_();   \
  } while (false)

// CHECK() and the trap sequence can be invoked from a constexpr function.
// This could make compilation fail on GCC, as it forbids directly using inline
// asm inside a constexpr function. However, it allows calling a lambda
// expression including the same asm.
// The side effect is that the top of the stacktrace will not point to the
// calling function, but to this anonymous lambda. This is still useful as the
// full name of the lambda will typically include the name of the function that
// calls CHECK() and the debugger will still break at the right line of code.
#if !V8_CC_GNU

#define WRAPPED_TRAP_SEQUENCE_() TRAP_SEQUENCE_()

#else

#define WRAPPED_TRAP_SEQUENCE_() \
  do {                           \
    [] { TRAP_SEQUENCE_(); }();  \
  } while (false)

#endif  // !V8_CC_GNU

#if defined(__clang__) || V8_CC_GNU

// __builtin_unreachable() hints to the compiler that this is noreturn and can
// be packed in the function epilogue.
#define IMMEDIATE_CRASH()     \
  ({                          \
    WRAPPED_TRAP_SEQUENCE_(); \
    __builtin_unreachable();  \
  })

#else

// This is supporting build with MSVC where there is no __builtin_unreachable().
#define IMMEDIATE_CRASH() WRAPPED_TRAP_SEQUENCE_()

#endif  // defined(__clang__) || defined(COMPILER_GCC)

#endif  // V8_BASE_IMMEDIATE_CRASH_H_
