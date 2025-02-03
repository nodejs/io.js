// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <optional>

#include "src/codegen/arm64/decoder-arm64-inl.h"
#include "src/execution/arm64/simulator-arm64.h"
#include "src/execution/pointer-authentication.h"
#include "test/unittests/test-utils.h"
#include "testing/gtest/include/gtest/gtest.h"

#ifdef V8_OS_LINUX

#include <sys/prctl.h>  // for prctl

#ifndef PR_PAC_APIBKEY
#define PR_PAC_APIBKEY (1UL << 1)
#endif

#ifndef PR_PAC_GET_ENABLED_KEYS
#define PR_PAC_GET_ENABLED_KEYS 61
#endif

#endif

namespace v8 {
namespace internal {

namespace {
Address SignPCForTesting(Isolate* isolate, Address pc, Address sp) {
  if constexpr (!ENABLE_CONTROL_FLOW_INTEGRITY_BOOL) return pc;

#ifdef USE_SIMULATOR
  pc = Simulator::AddPAC(pc, sp, Simulator::kPACKeyIB,
                         Simulator::kInstructionPointer);
#else
  asm volatile(
      "  mov x17, %[pc]\n"
      "  mov x16, %[sp]\n"
      "  pacib1716\n"
      "  mov %[pc], x17\n"
      : [pc] "+r"(pc)
      : [sp] "r"(sp)
      : "x16", "x17");
#endif
  return pc;
}

void FunctionToUseForPointerAuthentication() { PrintF("hello, "); }
void AlternativeFunctionToUseForPointerAuthentication() { PrintF("world\n"); }

Address GetRawCodeAddress() {
  return PointerAuthentication::StripPAC(
      reinterpret_cast<Address>(&FunctionToUseForPointerAuthentication));
}

Address GetAlternativeRawCodeAddress() {
  return PointerAuthentication::StripPAC(reinterpret_cast<Address>(
      &AlternativeFunctionToUseForPointerAuthentication));
}

// If the platform supports it, corrupt the PAC of |address| so that
// authenticating it will cause a crash.
std::optional<Address> CorruptPACIfSupported(Isolate* isolate,
                                             Address address) {
  if constexpr (!ENABLE_CONTROL_FLOW_INTEGRITY_BOOL) return std::nullopt;

  // First, find where in an address the PAC is located.

  // Produce a valid user address with all bits sets. This way, stripping the
  // PAC from the address will reveal which bits are used for it. We need to
  // clear the TTBR bit, as it differentiates between user and kernel addresses.
  Address user_address_all_ones = 0xffff'ffff'ffff'ffff & ~kTTBRMask;
  Address pac_mask = PointerAuthentication::StripPAC(user_address_all_ones) ^
                     user_address_all_ones;

  // If the PAC bits are zero then StripPAC() was a no-op. This means pointer
  // authentication isn't supported.
  if (pac_mask == 0) {
    return std::nullopt;
  }

  // At this point, pointer authentication is supported, but individual keys
  // may still be disabled by the OS. We check that it's enabled, to ensure
  // that corrupting the PAC will result in a crash.

#if defined(V8_OS_LINUX) && defined(V8_HOST_ARCH_ARM64)
  // On Linux 5.13 and later, we can ask the OS what keys are enabled directly.
  int enabled_keys = prctl(PR_PAC_GET_ENABLED_KEYS, 0, 0, 0, 0);
  if ((enabled_keys != -1) &&
      ((enabled_keys & PR_PAC_APIBKEY) == PR_PAC_APIBKEY)) {
    return address ^ pac_mask;
  }
#endif

  // Do a "best-effort" check to see if PAC is enabled, by signing values on the
  // stack and check if bits are set in the PAC range. We do this check a few
  // times because 0 is a valid PAC.

  Address stack[] = {0xa, 0xb, 0xc, 0xd};
  for (int slot = 0; slot < 4; slot++) {
    stack[slot] = SignPCForTesting(isolate, stack[slot],
                                   reinterpret_cast<Address>(&stack[slot]));
    if ((stack[slot] & pac_mask) != 0) return address ^ pac_mask;
  }

  // None of the slots were signed with a non-zero PAC, assume this means PAC
  // isn't enabled.
  return std::nullopt;
}

}  // namespace

using PointerAuthArm64Test = TestWithIsolate;

TEST_F(PointerAuthArm64Test, AuthenticatePC) {
  Address pc = GetRawCodeAddress();
  Address stack = 0;

  stack = SignPCForTesting(i_isolate(), pc, reinterpret_cast<Address>(&stack));

  pc = PointerAuthentication::AuthenticatePC(&stack, 0);
  EXPECT_EQ(pc, GetRawCodeAddress());

  if (auto corrupted_stack = CorruptPACIfSupported(i_isolate(), stack)) {
    stack = *corrupted_stack;
    EXPECT_DEATH_IF_SUPPORTED(PointerAuthentication::AuthenticatePC(&stack, 0),
                              "");
  }
}

TEST_F(PointerAuthArm64Test, ReplacePC) {
  Address pc = GetRawCodeAddress();
  Address stack = 0;

  stack = SignPCForTesting(i_isolate(), pc, reinterpret_cast<Address>(&stack));

  PointerAuthentication::ReplacePC(&stack, GetAlternativeRawCodeAddress(), 0);

  if (auto corrupted_stack = CorruptPACIfSupported(i_isolate(), stack)) {
    stack = *corrupted_stack;
    EXPECT_DEATH_IF_SUPPORTED(PointerAuthentication::ReplacePC(
                                  &stack, GetAlternativeRawCodeAddress(), 0),
                              "");
  }
}

TEST_F(PointerAuthArm64Test, ReplacePCAfterGC) {
  v8::PageAllocator* page_allocator = v8::internal::GetPlatformPageAllocator();
  size_t page_size = v8::internal::AllocatePageSize();

  // Allocate a page and mark it inaccessible, to simulate a code address to a
  // page that was reclaimed after a GC.
  Address pc = reinterpret_cast<Address>(v8::internal::AllocatePages(
      page_allocator, page_allocator->GetRandomMmapAddr(), page_size, page_size,
      PageAllocator::Permission::kReadWrite));
  CHECK(SetPermissions(page_allocator, pc, page_size,
                       PageAllocator::Permission::kNoAccess));

  // Replacing the signed PC on the stack should work even when the previous PC
  // points to an inaccessible page.

  Address stack = 0;
  stack = SignPCForTesting(i_isolate(), pc, reinterpret_cast<Address>(&stack));

  PointerAuthentication::ReplacePC(&stack, GetRawCodeAddress(), 0);
}

#ifdef USE_SIMULATOR
TEST_F(PointerAuthArm64Test, SimulatorComputePAC) {
  Decoder<DispatchingDecoderVisitor>* decoder =
      new Decoder<DispatchingDecoderVisitor>();
  Simulator simulator(decoder);

  uint64_t data1 = 0xfb623599da6e8127;
  uint64_t data2 = 0x27979fadf7d53cb7;
  uint64_t context = 0x477d469dec0b8762;
  Simulator::PACKey key = {0x84be85ce9804e94b, 0xec2802d4e0a488e9, -1};

  uint64_t pac1 = simulator.ComputePAC(data1, context, key);
  uint64_t pac2 = simulator.ComputePAC(data2, context, key);

  // NOTE: If the PAC implementation is changed, this may fail due to a hash
  // collision.
  CHECK_NE(pac1, pac2);
}

TEST_F(PointerAuthArm64Test, SimulatorAddAndAuthPAC) {
  i::v8_flags.sim_abort_on_bad_auth = false;
  Decoder<DispatchingDecoderVisitor>* decoder =
      new Decoder<DispatchingDecoderVisitor>();
  Simulator simulator(decoder);

  uint64_t ptr = 0x0000000012345678;
  uint64_t context = 0x477d469dec0b8762;
  Simulator::PACKey key_a = {0x84be85ce9804e94b, 0xec2802d4e0a488e9, 0};
  Simulator::PACKey key_b = {0xec1119e288704d13, 0xd7f6b76e1cea585e, 1};

  uint64_t ptr_a =
      simulator.AddPAC(ptr, context, key_a, Simulator::kInstructionPointer);

  // Attempt to authenticate the pointer with PAC using different keys.
  uint64_t success =
      simulator.AuthPAC(ptr_a, context, key_a, Simulator::kInstructionPointer);
  uint64_t fail =
      simulator.AuthPAC(ptr_a, context, key_b, Simulator::kInstructionPointer);

  uint64_t pac_mask =
      simulator.CalculatePACMask(ptr, Simulator::kInstructionPointer, 0);

  // NOTE: If the PAC implementation is changed, this may fail due to a hash
  // collision.
  CHECK_NE((ptr_a & pac_mask), 0);
  CHECK_EQ(success, ptr);
  CHECK_NE(fail, ptr);
}

TEST_F(PointerAuthArm64Test, SimulatorAddAndStripPAC) {
  Decoder<DispatchingDecoderVisitor>* decoder =
      new Decoder<DispatchingDecoderVisitor>();
  Simulator simulator(decoder);

  uint64_t ptr = 0xff00000012345678;
  uint64_t pac_mask =
      simulator.CalculatePACMask(ptr, Simulator::kInstructionPointer, 0);
  uint64_t ptr_a = ptr | pac_mask;

  CHECK_EQ(simulator.StripPAC(ptr_a, Simulator::kInstructionPointer), ptr);
}
#endif  // USE_SIMULATOR

}  // namespace internal
}  // namespace v8
