// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/heap/base/stack.h"

#include <memory>
#include <ostream>

#include "include/v8config.h"
#include "testing/gtest/include/gtest/gtest.h"

#if V8_OS_LINUX && (V8_HOST_ARCH_IA32 || V8_HOST_ARCH_X64)
#include <xmmintrin.h>
#endif

namespace cppgc {
namespace internal {

using heap::base::Stack;
using heap::base::StackVisitor;

namespace {

class GCStackTest : public ::testing::Test {
 public:
  GCStackTest() : stack_(std::make_unique<Stack>()) { stack_->SetStackStart(); }

  Stack* GetStack() const { return stack_.get(); }

 private:
  std::unique_ptr<Stack> stack_;
};

}  // namespace

#if !V8_OS_FUCHSIA
TEST_F(GCStackTest, IsOnStackForStackValue) {
  void* dummy;
  EXPECT_TRUE(GetStack()->IsOnStack(&dummy));
}
#endif  // !V8_OS_FUCHSIA

TEST_F(GCStackTest, IsOnStackForHeapValue) {
  auto dummy = std::make_unique<int>();
  EXPECT_FALSE(GetStack()->IsOnStack(dummy.get()));
}

namespace {

class StackScanner final : public StackVisitor {
 public:
  struct Container {
    std::unique_ptr<int> value;
  };

  StackScanner() : container_(new Container{}) {
    container_->value = std::make_unique<int>();
  }

  void VisitPointer(const void* address) final {
    if (address == container_->value.get()) found_ = true;
  }

  void Reset() { found_ = false; }
  bool found() const { return found_; }
  int* needle() const { return container_->value.get(); }

 private:
  std::unique_ptr<Container> container_;
  bool found_ = false;
};

}  // namespace

TEST_F(GCStackTest, IteratePointersFindsOnStackValue) {
  auto scanner = std::make_unique<StackScanner>();

  // No check that the needle is initially not found as on some platforms it
  // may be part of  temporaries after setting it up through StackScanner.
  {
    int* volatile tmp = scanner->needle();
    USE(tmp);
    GetStack()->IteratePointersForTesting(scanner.get());
    EXPECT_TRUE(scanner->found());
  }
}

TEST_F(GCStackTest, IteratePointersFindsOnStackValuePotentiallyUnaligned) {
  auto scanner = std::make_unique<StackScanner>();

  // No check that the needle is initially not found as on some platforms it
  // may be part of  temporaries after setting it up through StackScanner.
  {
    char a = 'c';
    USE(a);
    int* volatile tmp = scanner->needle();
    USE(tmp);
    GetStack()->IteratePointersForTesting(scanner.get());
    EXPECT_TRUE(scanner->found());
  }
}

namespace {

// Prevent inlining as that would allow the compiler to prove that the parameter
// must not actually be materialized.
//
// Parameter positions are explicit to test various calling conventions.
V8_NOINLINE void* RecursivelyPassOnParameterImpl(void* p1, void* p2, void* p3,
                                                 void* p4, void* p5, void* p6,
                                                 void* p7, void* p8,
                                                 Stack* stack,
                                                 StackVisitor* visitor) {
  if (p1) {
    return RecursivelyPassOnParameterImpl(nullptr, p1, nullptr, nullptr,
                                          nullptr, nullptr, nullptr, nullptr,
                                          stack, visitor);
  } else if (p2) {
    return RecursivelyPassOnParameterImpl(nullptr, nullptr, p2, nullptr,
                                          nullptr, nullptr, nullptr, nullptr,
                                          stack, visitor);
  } else if (p3) {
    return RecursivelyPassOnParameterImpl(nullptr, nullptr, nullptr, p3,
                                          nullptr, nullptr, nullptr, nullptr,
                                          stack, visitor);
  } else if (p4) {
    return RecursivelyPassOnParameterImpl(nullptr, nullptr, nullptr, nullptr,
                                          p4, nullptr, nullptr, nullptr, stack,
                                          visitor);
  } else if (p5) {
    return RecursivelyPassOnParameterImpl(nullptr, nullptr, nullptr, nullptr,
                                          nullptr, p5, nullptr, nullptr, stack,
                                          visitor);
  } else if (p6) {
    return RecursivelyPassOnParameterImpl(nullptr, nullptr, nullptr, nullptr,
                                          nullptr, nullptr, p6, nullptr, stack,
                                          visitor);
  } else if (p7) {
    return RecursivelyPassOnParameterImpl(nullptr, nullptr, nullptr, nullptr,
                                          nullptr, nullptr, nullptr, p7, stack,
                                          visitor);
  } else if (p8) {
    stack->IteratePointersForTesting(visitor);
    return p8;
  }
  return nullptr;
}

V8_NOINLINE void* RecursivelyPassOnParameter(size_t num, void* parameter,
                                             Stack* stack,
                                             StackVisitor* visitor) {
  switch (num) {
    case 0:
      stack->IteratePointersForTesting(visitor);
      return parameter;
    case 1:
      return RecursivelyPassOnParameterImpl(nullptr, nullptr, nullptr, nullptr,
                                            nullptr, nullptr, nullptr,
                                            parameter, stack, visitor);
    case 2:
      return RecursivelyPassOnParameterImpl(nullptr, nullptr, nullptr, nullptr,
                                            nullptr, nullptr, parameter,
                                            nullptr, stack, visitor);
    case 3:
      return RecursivelyPassOnParameterImpl(nullptr, nullptr, nullptr, nullptr,
                                            nullptr, parameter, nullptr,
                                            nullptr, stack, visitor);
    case 4:
      return RecursivelyPassOnParameterImpl(nullptr, nullptr, nullptr, nullptr,
                                            parameter, nullptr, nullptr,
                                            nullptr, stack, visitor);
    case 5:
      return RecursivelyPassOnParameterImpl(nullptr, nullptr, nullptr,
                                            parameter, nullptr, nullptr,
                                            nullptr, nullptr, stack, visitor);
    case 6:
      return RecursivelyPassOnParameterImpl(nullptr, nullptr, parameter,
                                            nullptr, nullptr, nullptr, nullptr,
                                            nullptr, stack, visitor);
    case 7:
      return RecursivelyPassOnParameterImpl(nullptr, parameter, nullptr,
                                            nullptr, nullptr, nullptr, nullptr,
                                            nullptr, stack, visitor);
    case 8:
      return RecursivelyPassOnParameterImpl(parameter, nullptr, nullptr,
                                            nullptr, nullptr, nullptr, nullptr,
                                            nullptr, stack, visitor);
    default:
      UNREACHABLE();
  }
  UNREACHABLE();
}

}  // namespace

TEST_F(GCStackTest, IteratePointersFindsParameterNesting0) {
  auto scanner = std::make_unique<StackScanner>();
  void* needle = RecursivelyPassOnParameter(0, scanner->needle(), GetStack(),
                                            scanner.get());
  EXPECT_EQ(scanner->needle(), needle);
  EXPECT_TRUE(scanner->found());
}

TEST_F(GCStackTest, IteratePointersFindsParameterNesting1) {
  auto scanner = std::make_unique<StackScanner>();
  void* needle = RecursivelyPassOnParameter(1, scanner->needle(), GetStack(),
                                            scanner.get());
  EXPECT_EQ(scanner->needle(), needle);
  EXPECT_TRUE(scanner->found());
}

TEST_F(GCStackTest, IteratePointersFindsParameterNesting2) {
  auto scanner = std::make_unique<StackScanner>();
  void* needle = RecursivelyPassOnParameter(2, scanner->needle(), GetStack(),
                                            scanner.get());
  EXPECT_EQ(scanner->needle(), needle);
  EXPECT_TRUE(scanner->found());
}

TEST_F(GCStackTest, IteratePointersFindsParameterNesting3) {
  auto scanner = std::make_unique<StackScanner>();
  void* needle = RecursivelyPassOnParameter(3, scanner->needle(), GetStack(),
                                            scanner.get());
  EXPECT_EQ(scanner->needle(), needle);
  EXPECT_TRUE(scanner->found());
}

TEST_F(GCStackTest, IteratePointersFindsParameterNesting4) {
  auto scanner = std::make_unique<StackScanner>();
  void* needle = RecursivelyPassOnParameter(4, scanner->needle(), GetStack(),
                                            scanner.get());
  EXPECT_EQ(scanner->needle(), needle);
  EXPECT_TRUE(scanner->found());
}

TEST_F(GCStackTest, IteratePointersFindsParameterNesting5) {
  auto scanner = std::make_unique<StackScanner>();
  void* needle = RecursivelyPassOnParameter(5, scanner->needle(), GetStack(),
                                            scanner.get());
  EXPECT_EQ(scanner->needle(), needle);
  EXPECT_TRUE(scanner->found());
}

TEST_F(GCStackTest, IteratePointersFindsParameterNesting6) {
  auto scanner = std::make_unique<StackScanner>();
  void* needle = RecursivelyPassOnParameter(6, scanner->needle(), GetStack(),
                                            scanner.get());
  EXPECT_EQ(scanner->needle(), needle);
  EXPECT_TRUE(scanner->found());
}

TEST_F(GCStackTest, IteratePointersFindsParameterNesting7) {
  auto scanner = std::make_unique<StackScanner>();
  void* needle = RecursivelyPassOnParameter(7, scanner->needle(), GetStack(),
                                            scanner.get());
  EXPECT_EQ(scanner->needle(), needle);
  EXPECT_TRUE(scanner->found());
}

// Disabled on msvc, due to miscompilation, see https://crbug.com/v8/10658.
#if !defined(_MSC_VER) || defined(__clang__)
TEST_F(GCStackTest, IteratePointersFindsParameterNesting8) {
  auto scanner = std::make_unique<StackScanner>();
  void* needle = RecursivelyPassOnParameter(8, scanner->needle(), GetStack(),
                                            scanner.get());
  EXPECT_EQ(scanner->needle(), needle);
  EXPECT_TRUE(scanner->found());
}
#endif  // !_MSC_VER || __clang__

namespace {
// We manually call into this function from inline assembly. Therefore we need
// to make sure that:
// 1) there is no .plt indirection (i.e. visibility is hidden);
// 2) stack is realigned in the function prologue.
extern "C" V8_NOINLINE
#if defined(__clang__)
    __attribute__((used))
#if !defined(V8_OS_WIN)
    __attribute__((visibility("hidden")))
#endif  // !defined(V8_OS_WIN)
#ifdef __has_attribute
#if __has_attribute(force_align_arg_pointer)
    __attribute__((force_align_arg_pointer))
#endif  // __has_attribute(force_align_arg_pointer)
#endif  //  __has_attribute
#endif  // defined(__clang__)
    void
    IteratePointersNoMangling(Stack* stack, StackVisitor* visitor) {
  stack->IteratePointersForTesting(visitor);
}
}  // namespace

// The following tests use inline assembly and have been checked to work on
// clang to verify that the stack-scanning trampoline pushes callee-saved
// registers.
//
// The test uses a macro loop as asm() can only be passed string literals.
#ifdef __clang__
#ifdef V8_TARGET_ARCH_X64
#ifdef V8_OS_WIN

// Excluded from test: rbp
#define FOR_ALL_CALLEE_SAVED_REGS(V) \
  V("rdi")                           \
  V("rsi")                           \
  V("rbx")                           \
  V("r12")                           \
  V("r13")                           \
  V("r14")                           \
  V("r15")

#else  // !V8_OS_WIN

// Excluded from test: rbp
#define FOR_ALL_CALLEE_SAVED_REGS(V) \
  V("rbx")                           \
  V("r12")                           \
  V("r13")                           \
  V("r14")                           \
  V("r15")

#endif  // !V8_OS_WIN
#endif  // V8_TARGET_ARCH_X64
#endif  // __clang__

#ifdef FOR_ALL_CALLEE_SAVED_REGS

TEST_F(GCStackTest, IteratePointersFindsCalleeSavedRegisters) {
  auto scanner = std::make_unique<StackScanner>();

  // No check that the needle is initially not found as on some platforms it
  // may be part of  temporaries after setting it up through StackScanner.

// First, clear all callee-saved registers.
#define CLEAR_REGISTER(reg) asm("mov $0, %%" reg : : : reg);

  FOR_ALL_CALLEE_SAVED_REGS(CLEAR_REGISTER)
#undef CLEAR_REGISTER

  // Keep local raw pointers to keep instruction sequences small below.
  auto* local_stack = GetStack();
  auto* local_scanner = scanner.get();

#define MOVE_TO_REG_AND_CALL_IMPL(needle_reg, arg1, arg2)           \
  asm volatile("mov %0, %%" needle_reg "\n mov %1, %%" arg1         \
               "\n mov %2, %%" arg2                                 \
               "\n call %P3"                                        \
               "\n mov $0, %%" needle_reg                           \
               :                                                    \
               : "r"(local_scanner->needle()), "r"(local_stack),    \
                 "r"(local_scanner), "i"(IteratePointersNoMangling) \
               : "memory", needle_reg, arg1, arg2, "cc");

#ifdef V8_OS_WIN
#define MOVE_TO_REG_AND_CALL(reg) MOVE_TO_REG_AND_CALL_IMPL(reg, "rcx", "rdx")
#else  // !V8_OS_WIN
#define MOVE_TO_REG_AND_CALL(reg) MOVE_TO_REG_AND_CALL_IMPL(reg, "rdi", "rsi")
#endif  // V8_OS_WIN

// Moves |local_scanner->needle()| into a callee-saved register, leaving the
// callee-saved register as the only register referencing the needle.
// (Ignoring implementation-dependent dirty registers/stack.)
#define KEEP_ALIVE_FROM_CALLEE_SAVED(reg)                                     \
  local_scanner->Reset();                                                     \
  /* Wrap the inline assembly in a lambda to rely on the compiler for saving  \
  caller-saved registers. */                                                  \
  [local_stack, local_scanner]() V8_NOINLINE { MOVE_TO_REG_AND_CALL(reg) }(); \
  EXPECT_TRUE(local_scanner->found())                                         \
      << "pointer in callee-saved register not found. register: " << reg      \
      << std::endl;

  FOR_ALL_CALLEE_SAVED_REGS(KEEP_ALIVE_FROM_CALLEE_SAVED)
#undef MOVE_TO_REG_AND_CALL
#undef MOVE_TO_REG_AND_CALL_IMPL
#undef KEEP_ALIVE_FROM_CALLEE_SAVED
#undef FOR_ALL_CALLEE_SAVED_REGS
}
#endif  // FOR_ALL_CALLEE_SAVED_REGS

#if defined(__clang__) && defined(V8_TARGET_ARCH_X64) && defined(V8_OS_WIN)

#define FOR_ALL_XMM_CALLEE_SAVED_REGS(V) \
  V("xmm6")                              \
  V("xmm7")                              \
  V("xmm8")                              \
  V("xmm9")                              \
  V("xmm10")                             \
  V("xmm11")                             \
  V("xmm12")                             \
  V("xmm13")                             \
  V("xmm14")                             \
  V("xmm15")

TEST_F(GCStackTest, IteratePointersFindsCalleeSavedXMMRegisters) {
  auto scanner = std::make_unique<StackScanner>();

  // No check that the needle is initially not found as on some platforms it
  // may be part of  temporaries after setting it up through StackScanner.

// First, clear all callee-saved xmm registers.
#define CLEAR_REGISTER(reg) asm("pxor %%" reg ", %%" reg : : : reg);

  FOR_ALL_XMM_CALLEE_SAVED_REGS(CLEAR_REGISTER)
#undef CLEAR_REGISTER

  // Keep local raw pointers to keep instruction sequences small below.
  auto* local_stack = GetStack();
  auto* local_scanner = scanner.get();

// Moves |local_scanner->needle()| into a callee-saved register, leaving the
// callee-saved register as the only register referencing the needle.
// (Ignoring implementation-dependent dirty registers/stack.)
#define KEEP_ALIVE_FROM_CALLEE_SAVED(reg)                                     \
  local_scanner->Reset();                                                     \
  [local_stack, local_scanner]() V8_NOINLINE { MOVE_TO_REG_AND_CALL(reg) }(); \
  EXPECT_TRUE(local_scanner->found())                                         \
      << "pointer in callee-saved xmm register not found. register: " << reg  \
      << std::endl;

  // First, test the pointer in the low quadword.
#define MOVE_TO_REG_AND_CALL(reg)                                   \
  asm volatile("mov %0, %%rax \n movq %%rax, %%" reg                \
               "\n mov %1, %%rcx \n mov %2, %%rdx"                  \
               "\n call %P3"                                        \
               "\n pxor %%" reg ", %%" reg                          \
               :                                                    \
               : "r"(local_scanner->needle()), "r"(local_stack),    \
                 "r"(local_scanner), "i"(IteratePointersNoMangling) \
               : "memory", "rax", reg, "rcx", "rdx", "cc");

  FOR_ALL_XMM_CALLEE_SAVED_REGS(KEEP_ALIVE_FROM_CALLEE_SAVED)

#undef MOVE_TO_REG_AND_CALL
  // Then, test the pointer in the upper quadword.
#define MOVE_TO_REG_AND_CALL(reg)                                   \
  asm volatile("mov %0, %%rax \n movq %%rax, %%" reg                \
               "\n pshufd $0b01001110, %%" reg ", %%" reg           \
               "\n mov %1, %%rcx \n mov %2, %%rdx"                  \
               "\n call %P3"                                        \
               "\n pxor %%" reg ", %%" reg                          \
               :                                                    \
               : "r"(local_scanner->needle()), "r"(local_stack),    \
                 "r"(local_scanner), "i"(IteratePointersNoMangling) \
               : "memory", "rax", reg, "rcx", "rdx", "cc");

  FOR_ALL_XMM_CALLEE_SAVED_REGS(KEEP_ALIVE_FROM_CALLEE_SAVED)
#undef MOVE_TO_REG_AND_CALL
#undef KEEP_ALIVE_FROM_CALLEE_SAVED
#undef FOR_ALL_XMM_CALLEE_SAVED_REGS
}

#endif  // defined(__clang__) && defined(V8_TARGET_ARCH_X64) &&
        // defined(V8_OS_WIN)

#if V8_OS_LINUX && (V8_HOST_ARCH_IA32 || V8_HOST_ARCH_X64)
class CheckStackAlignmentVisitor final : public StackVisitor {
 public:
  void VisitPointer(const void*) final {
    float f[4] = {0.};
    volatile auto xmm = ::_mm_load_ps(f);
    USE(xmm);
  }
};

TEST_F(GCStackTest, StackAlignment) {
  auto checker = std::make_unique<CheckStackAlignmentVisitor>();
  GetStack()->IteratePointersForTesting(checker.get());
}
#endif  // V8_OS_LINUX && (V8_HOST_ARCH_IA32 || V8_HOST_ARCH_X64)

}  // namespace internal
}  // namespace cppgc
