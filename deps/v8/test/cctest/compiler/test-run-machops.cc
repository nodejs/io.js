// Copyright 2014 the V8 project authors. All rights reserved. Use of this
// source code is governed by a BSD-style license that can be found in the
// LICENSE file.

#include <cmath>
#include <functional>
#include <limits>

#include "src/base/bits.h"
#include "src/base/ieee754.h"
#include "src/base/overflowing-math.h"
#include "src/base/safe_conversions.h"
#include "src/base/utils/random-number-generator.h"
#include "src/builtins/builtins.h"
#include "src/common/ptr-compr-inl.h"
#include "src/objects/objects-inl.h"
#include "src/utils/boxed-float.h"
#include "src/utils/utils.h"
#include "test/cctest/cctest.h"
#include "test/cctest/compiler/codegen-tester.h"
#include "test/common/flag-utils.h"
#include "test/common/value-helper.h"

namespace v8 {
namespace internal {
namespace compiler {


TEST(RunInt32Add) {
  RawMachineAssemblerTester<int32_t> m;
  Node* add = m.Int32Add(m.Int32Constant(0), m.Int32Constant(1));
  m.Return(add);
  CHECK_EQ(1, m.Call());
}

static int RunInt32AddShift(bool is_left, int32_t add_left, int32_t add_right,
                            int32_t shift_left, int32_t shift_right) {
  RawMachineAssemblerTester<int32_t> m;
  Node* shift =
      m.Word32Shl(m.Int32Constant(shift_left), m.Int32Constant(shift_right));
  Node* add = m.Int32Add(m.Int32Constant(add_left), m.Int32Constant(add_right));
  Node* lsa = is_left ? m.Int32Add(shift, add) : m.Int32Add(add, shift);
  m.Return(lsa);
  return m.Call();
}

TEST(RunInt32AddShift) {
  struct Test_case {
    int32_t add_left, add_right, shift_left, shift_right, expected;
  };

  Test_case tc[] = {
      {20, 22, 4, 2, 58},
      {20, 22, 4, 1, 50},
      {20, 22, 1, 6, 106},
      {INT_MAX - 2, 1, 1, 1, INT_MIN},  // INT_MAX - 2 + 1 + (1 << 1), overflow.
  };
  const size_t tc_size = sizeof(tc) / sizeof(Test_case);

  for (size_t i = 0; i < tc_size; ++i) {
    CHECK_EQ(tc[i].expected,
             RunInt32AddShift(false, tc[i].add_left, tc[i].add_right,
                              tc[i].shift_left, tc[i].shift_right));
    CHECK_EQ(tc[i].expected,
             RunInt32AddShift(true, tc[i].add_left, tc[i].add_right,
                              tc[i].shift_left, tc[i].shift_right));
  }
}

TEST(RunWord32ReverseBits) {
  BufferedRawMachineAssemblerTester<uint32_t> m(MachineType::Uint32());
  if (!m.machine()->Word32ReverseBits().IsSupported()) {
    // We can only test the operator if it exists on the testing platform.
    return;
  }
  m.Return(m.AddNode(m.machine()->Word32ReverseBits().op(), m.Parameter(0)));

  CHECK_EQ(uint32_t(0x00000000), m.Call(uint32_t(0x00000000)));
  CHECK_EQ(uint32_t(0x12345678), m.Call(uint32_t(0x1E6A2C48)));
  CHECK_EQ(uint32_t(0xFEDCBA09), m.Call(uint32_t(0x905D3B7F)));
  CHECK_EQ(uint32_t(0x01010101), m.Call(uint32_t(0x80808080)));
  CHECK_EQ(uint32_t(0x01020408), m.Call(uint32_t(0x10204080)));
  CHECK_EQ(uint32_t(0xF0703010), m.Call(uint32_t(0x080C0E0F)));
  CHECK_EQ(uint32_t(0x1F8D0A3A), m.Call(uint32_t(0x5C50B1F8)));
  CHECK_EQ(uint32_t(0xFFFFFFFF), m.Call(uint32_t(0xFFFFFFFF)));
}

TEST(RunWord32ReverseBytes) {
  BufferedRawMachineAssemblerTester<uint32_t> m(MachineType::Uint32());
  m.Return(m.AddNode(m.machine()->Word32ReverseBytes(), m.Parameter(0)));

  CHECK_EQ(uint32_t(0x00000000), m.Call(uint32_t(0x00000000)));
  CHECK_EQ(uint32_t(0x12345678), m.Call(uint32_t(0x78563412)));
  CHECK_EQ(uint32_t(0xFEDCBA09), m.Call(uint32_t(0x09BADCFE)));
  CHECK_EQ(uint32_t(0x01010101), m.Call(uint32_t(0x01010101)));
  CHECK_EQ(uint32_t(0x01020408), m.Call(uint32_t(0x08040201)));
  CHECK_EQ(uint32_t(0xF0703010), m.Call(uint32_t(0x103070F0)));
  CHECK_EQ(uint32_t(0x1F8D0A3A), m.Call(uint32_t(0x3A0A8D1F)));
  CHECK_EQ(uint32_t(0xFFFFFFFF), m.Call(uint32_t(0xFFFFFFFF)));
}

TEST(RunWord32Ctz) {
  BufferedRawMachineAssemblerTester<int32_t> m(MachineType::Uint32());
  if (!m.machine()->Word32Ctz().IsSupported()) {
    // We can only test the operator if it exists on the testing platform.
    return;
  }
  m.Return(m.AddNode(m.machine()->Word32Ctz().op(), m.Parameter(0)));

  CHECK_EQ(32, m.Call(uint32_t(0x00000000)));
  CHECK_EQ(31, m.Call(uint32_t(0x80000000)));
  CHECK_EQ(30, m.Call(uint32_t(0x40000000)));
  CHECK_EQ(29, m.Call(uint32_t(0x20000000)));
  CHECK_EQ(28, m.Call(uint32_t(0x10000000)));
  CHECK_EQ(27, m.Call(uint32_t(0xA8000000)));
  CHECK_EQ(26, m.Call(uint32_t(0xF4000000)));
  CHECK_EQ(25, m.Call(uint32_t(0x62000000)));
  CHECK_EQ(24, m.Call(uint32_t(0x91000000)));
  CHECK_EQ(23, m.Call(uint32_t(0xCD800000)));
  CHECK_EQ(22, m.Call(uint32_t(0x09400000)));
  CHECK_EQ(21, m.Call(uint32_t(0xAF200000)));
  CHECK_EQ(20, m.Call(uint32_t(0xAC100000)));
  CHECK_EQ(19, m.Call(uint32_t(0xE0B80000)));
  CHECK_EQ(18, m.Call(uint32_t(0x9CE40000)));
  CHECK_EQ(17, m.Call(uint32_t(0xC7920000)));
  CHECK_EQ(16, m.Call(uint32_t(0xB8F10000)));
  CHECK_EQ(15, m.Call(uint32_t(0x3B9F8000)));
  CHECK_EQ(14, m.Call(uint32_t(0xDB4C4000)));
  CHECK_EQ(13, m.Call(uint32_t(0xE9A32000)));
  CHECK_EQ(12, m.Call(uint32_t(0xFCA61000)));
  CHECK_EQ(11, m.Call(uint32_t(0x6C8A7800)));
  CHECK_EQ(10, m.Call(uint32_t(0x8CE5A400)));
  CHECK_EQ(9, m.Call(uint32_t(0xCB7D0200)));
  CHECK_EQ(8, m.Call(uint32_t(0xCB4DC100)));
  CHECK_EQ(7, m.Call(uint32_t(0xDFBEC580)));
  CHECK_EQ(6, m.Call(uint32_t(0x27A9DB40)));
  CHECK_EQ(5, m.Call(uint32_t(0xDE3BCB20)));
  CHECK_EQ(4, m.Call(uint32_t(0xD7E8A610)));
  CHECK_EQ(3, m.Call(uint32_t(0x9AFDBC88)));
  CHECK_EQ(2, m.Call(uint32_t(0x9AFDBC84)));
  CHECK_EQ(1, m.Call(uint32_t(0x9AFDBC82)));
  CHECK_EQ(0, m.Call(uint32_t(0x9AFDBC81)));
}

TEST(RunWord32Clz) {
  BufferedRawMachineAssemblerTester<int32_t> m(MachineType::Uint32());
  m.Return(m.Word32Clz(m.Parameter(0)));

  CHECK_EQ(0, m.Call(uint32_t(0x80001000)));
  CHECK_EQ(1, m.Call(uint32_t(0x40000500)));
  CHECK_EQ(2, m.Call(uint32_t(0x20000300)));
  CHECK_EQ(3, m.Call(uint32_t(0x10000003)));
  CHECK_EQ(4, m.Call(uint32_t(0x08050000)));
  CHECK_EQ(5, m.Call(uint32_t(0x04006000)));
  CHECK_EQ(6, m.Call(uint32_t(0x02000000)));
  CHECK_EQ(7, m.Call(uint32_t(0x010000A0)));
  CHECK_EQ(8, m.Call(uint32_t(0x00800C00)));
  CHECK_EQ(9, m.Call(uint32_t(0x00400000)));
  CHECK_EQ(10, m.Call(uint32_t(0x0020000D)));
  CHECK_EQ(11, m.Call(uint32_t(0x00100F00)));
  CHECK_EQ(12, m.Call(uint32_t(0x00080000)));
  CHECK_EQ(13, m.Call(uint32_t(0x00041000)));
  CHECK_EQ(14, m.Call(uint32_t(0x00020020)));
  CHECK_EQ(15, m.Call(uint32_t(0x00010300)));
  CHECK_EQ(16, m.Call(uint32_t(0x00008040)));
  CHECK_EQ(17, m.Call(uint32_t(0x00004005)));
  CHECK_EQ(18, m.Call(uint32_t(0x00002050)));
  CHECK_EQ(19, m.Call(uint32_t(0x00001700)));
  CHECK_EQ(20, m.Call(uint32_t(0x00000870)));
  CHECK_EQ(21, m.Call(uint32_t(0x00000405)));
  CHECK_EQ(22, m.Call(uint32_t(0x00000203)));
  CHECK_EQ(23, m.Call(uint32_t(0x00000101)));
  CHECK_EQ(24, m.Call(uint32_t(0x00000089)));
  CHECK_EQ(25, m.Call(uint32_t(0x00000041)));
  CHECK_EQ(26, m.Call(uint32_t(0x00000022)));
  CHECK_EQ(27, m.Call(uint32_t(0x00000013)));
  CHECK_EQ(28, m.Call(uint32_t(0x00000008)));
  CHECK_EQ(29, m.Call(uint32_t(0x00000004)));
  CHECK_EQ(30, m.Call(uint32_t(0x00000002)));
  CHECK_EQ(31, m.Call(uint32_t(0x00000001)));
  CHECK_EQ(32, m.Call(uint32_t(0x00000000)));
}


TEST(RunWord32Popcnt) {
  BufferedRawMachineAssemblerTester<int32_t> m(MachineType::Uint32());
  if (!m.machine()->Word32Popcnt().IsSupported()) {
    // We can only test the operator if it exists on the testing platform.
    return;
  }
  m.Return(m.AddNode(m.machine()->Word32Popcnt().op(), m.Parameter(0)));

  CHECK_EQ(0, m.Call(uint32_t(0x00000000)));
  CHECK_EQ(1, m.Call(uint32_t(0x00000001)));
  CHECK_EQ(1, m.Call(uint32_t(0x80000000)));
  CHECK_EQ(32, m.Call(uint32_t(0xFFFFFFFF)));
  CHECK_EQ(6, m.Call(uint32_t(0x000DC100)));
  CHECK_EQ(9, m.Call(uint32_t(0xE00DC100)));
  CHECK_EQ(11, m.Call(uint32_t(0xE00DC103)));
  CHECK_EQ(9, m.Call(uint32_t(0x000DC107)));
}


#if V8_TARGET_ARCH_64_BIT
TEST(RunWord64ReverseBits) {
  RawMachineAssemblerTester<uint64_t> m(MachineType::Uint64());
  if (!m.machine()->Word64ReverseBits().IsSupported()) {
    return;
  }

  m.Return(m.AddNode(m.machine()->Word64ReverseBits().op(), m.Parameter(0)));

  CHECK_EQ(uint64_t(0x0000000000000000), m.Call(uint64_t(0x0000000000000000)));
  CHECK_EQ(uint64_t(0x1234567890ABCDEF), m.Call(uint64_t(0xF7B3D5091E6A2C48)));
  CHECK_EQ(uint64_t(0xFEDCBA0987654321), m.Call(uint64_t(0x84C2A6E1905D3B7F)));
  CHECK_EQ(uint64_t(0x0101010101010101), m.Call(uint64_t(0x8080808080808080)));
  CHECK_EQ(uint64_t(0x0102040803060C01), m.Call(uint64_t(0x803060C010204080)));
  CHECK_EQ(uint64_t(0xF0703010E060200F), m.Call(uint64_t(0xF0040607080C0E0F)));
  CHECK_EQ(uint64_t(0x2F8A6DF01C21FA3B), m.Call(uint64_t(0xDC5F84380FB651F4)));
  CHECK_EQ(uint64_t(0xFFFFFFFFFFFFFFFF), m.Call(uint64_t(0xFFFFFFFFFFFFFFFF)));
}

TEST(RunWord64ReverseBytes) {
  BufferedRawMachineAssemblerTester<uint64_t> m(MachineType::Uint64());
  m.Return(m.AddNode(m.machine()->Word64ReverseBytes(), m.Parameter(0)));

  CHECK_EQ(uint64_t(0x0000000000000000), m.Call(uint64_t(0x0000000000000000)));
  CHECK_EQ(uint64_t(0x1234567890ABCDEF), m.Call(uint64_t(0xEFCDAB9078563412)));
  CHECK_EQ(uint64_t(0xFEDCBA0987654321), m.Call(uint64_t(0x2143658709BADCFE)));
  CHECK_EQ(uint64_t(0x0101010101010101), m.Call(uint64_t(0x0101010101010101)));
  CHECK_EQ(uint64_t(0x0102040803060C01), m.Call(uint64_t(0x010C060308040201)));
  CHECK_EQ(uint64_t(0xF0703010E060200F), m.Call(uint64_t(0x0F2060E0103070F0)));
  CHECK_EQ(uint64_t(0x2F8A6DF01C21FA3B), m.Call(uint64_t(0x3BFA211CF06D8A2F)));
  CHECK_EQ(uint64_t(0xFFFFFFFFFFFFFFFF), m.Call(uint64_t(0xFFFFFFFFFFFFFFFF)));
}

TEST(RunWord64Clz) {
  BufferedRawMachineAssemblerTester<int32_t> m(MachineType::Uint64());
  m.Return(m.Word64Clz(m.Parameter(0)));

  CHECK_EQ(0, m.Call(uint64_t(0x8000100000000000)));
  CHECK_EQ(1, m.Call(uint64_t(0x4000050000000000)));
  CHECK_EQ(2, m.Call(uint64_t(0x2000030000000000)));
  CHECK_EQ(3, m.Call(uint64_t(0x1000000300000000)));
  CHECK_EQ(4, m.Call(uint64_t(0x0805000000000000)));
  CHECK_EQ(5, m.Call(uint64_t(0x0400600000000000)));
  CHECK_EQ(6, m.Call(uint64_t(0x0200000000000000)));
  CHECK_EQ(7, m.Call(uint64_t(0x010000A000000000)));
  CHECK_EQ(8, m.Call(uint64_t(0x00800C0000000000)));
  CHECK_EQ(9, m.Call(uint64_t(0x0040000000000000)));
  CHECK_EQ(10, m.Call(uint64_t(0x0020000D00000000)));
  CHECK_EQ(11, m.Call(uint64_t(0x00100F0000000000)));
  CHECK_EQ(12, m.Call(uint64_t(0x0008000000000000)));
  CHECK_EQ(13, m.Call(uint64_t(0x0004100000000000)));
  CHECK_EQ(14, m.Call(uint64_t(0x0002002000000000)));
  CHECK_EQ(15, m.Call(uint64_t(0x0001030000000000)));
  CHECK_EQ(16, m.Call(uint64_t(0x0000804000000000)));
  CHECK_EQ(17, m.Call(uint64_t(0x0000400500000000)));
  CHECK_EQ(18, m.Call(uint64_t(0x0000205000000000)));
  CHECK_EQ(19, m.Call(uint64_t(0x0000170000000000)));
  CHECK_EQ(20, m.Call(uint64_t(0x0000087000000000)));
  CHECK_EQ(21, m.Call(uint64_t(0x0000040500000000)));
  CHECK_EQ(22, m.Call(uint64_t(0x0000020300000000)));
  CHECK_EQ(23, m.Call(uint64_t(0x0000010100000000)));
  CHECK_EQ(24, m.Call(uint64_t(0x0000008900000000)));
  CHECK_EQ(25, m.Call(uint64_t(0x0000004100000000)));
  CHECK_EQ(26, m.Call(uint64_t(0x0000002200000000)));
  CHECK_EQ(27, m.Call(uint64_t(0x0000001300000000)));
  CHECK_EQ(28, m.Call(uint64_t(0x0000000800000000)));
  CHECK_EQ(29, m.Call(uint64_t(0x0000000400000000)));
  CHECK_EQ(30, m.Call(uint64_t(0x0000000200000000)));
  CHECK_EQ(31, m.Call(uint64_t(0x0000000100000000)));
  CHECK_EQ(32, m.Call(uint64_t(0x0000000080001000)));
  CHECK_EQ(33, m.Call(uint64_t(0x0000000040000500)));
  CHECK_EQ(34, m.Call(uint64_t(0x0000000020000300)));
  CHECK_EQ(35, m.Call(uint64_t(0x0000000010000003)));
  CHECK_EQ(36, m.Call(uint64_t(0x0000000008050000)));
  CHECK_EQ(37, m.Call(uint64_t(0x0000000004006000)));
  CHECK_EQ(38, m.Call(uint64_t(0x0000000002000000)));
  CHECK_EQ(39, m.Call(uint64_t(0x00000000010000A0)));
  CHECK_EQ(40, m.Call(uint64_t(0x0000000000800C00)));
  CHECK_EQ(41, m.Call(uint64_t(0x0000000000400000)));
  CHECK_EQ(42, m.Call(uint64_t(0x000000000020000D)));
  CHECK_EQ(43, m.Call(uint64_t(0x0000000000100F00)));
  CHECK_EQ(44, m.Call(uint64_t(0x0000000000080000)));
  CHECK_EQ(45, m.Call(uint64_t(0x0000000000041000)));
  CHECK_EQ(46, m.Call(uint64_t(0x0000000000020020)));
  CHECK_EQ(47, m.Call(uint64_t(0x0000000000010300)));
  CHECK_EQ(48, m.Call(uint64_t(0x0000000000008040)));
  CHECK_EQ(49, m.Call(uint64_t(0x0000000000004005)));
  CHECK_EQ(50, m.Call(uint64_t(0x0000000000002050)));
  CHECK_EQ(51, m.Call(uint64_t(0x0000000000001700)));
  CHECK_EQ(52, m.Call(uint64_t(0x0000000000000870)));
  CHECK_EQ(53, m.Call(uint64_t(0x0000000000000405)));
  CHECK_EQ(54, m.Call(uint64_t(0x0000000000000203)));
  CHECK_EQ(55, m.Call(uint64_t(0x0000000000000101)));
  CHECK_EQ(56, m.Call(uint64_t(0x0000000000000089)));
  CHECK_EQ(57, m.Call(uint64_t(0x0000000000000041)));
  CHECK_EQ(58, m.Call(uint64_t(0x0000000000000022)));
  CHECK_EQ(59, m.Call(uint64_t(0x0000000000000013)));
  CHECK_EQ(60, m.Call(uint64_t(0x0000000000000008)));
  CHECK_EQ(61, m.Call(uint64_t(0x0000000000000004)));
  CHECK_EQ(62, m.Call(uint64_t(0x0000000000000002)));
  CHECK_EQ(63, m.Call(uint64_t(0x0000000000000001)));
  CHECK_EQ(64, m.Call(uint64_t(0x0000000000000000)));
}


TEST(RunWord64Ctz) {
  RawMachineAssemblerTester<int32_t> m(MachineType::Uint64());
  if (!m.machine()->Word64Ctz().IsSupported()) {
    return;
  }

  m.Return(m.AddNode(m.machine()->Word64Ctz().op(), m.Parameter(0)));

  CHECK_EQ(64, m.Call(uint64_t(0x0000000000000000)));
  CHECK_EQ(63, m.Call(uint64_t(0x8000000000000000)));
  CHECK_EQ(62, m.Call(uint64_t(0x4000000000000000)));
  CHECK_EQ(61, m.Call(uint64_t(0x2000000000000000)));
  CHECK_EQ(60, m.Call(uint64_t(0x1000000000000000)));
  CHECK_EQ(59, m.Call(uint64_t(0xA800000000000000)));
  CHECK_EQ(58, m.Call(uint64_t(0xF400000000000000)));
  CHECK_EQ(57, m.Call(uint64_t(0x6200000000000000)));
  CHECK_EQ(56, m.Call(uint64_t(0x9100000000000000)));
  CHECK_EQ(55, m.Call(uint64_t(0xCD80000000000000)));
  CHECK_EQ(54, m.Call(uint64_t(0x0940000000000000)));
  CHECK_EQ(53, m.Call(uint64_t(0xAF20000000000000)));
  CHECK_EQ(52, m.Call(uint64_t(0xAC10000000000000)));
  CHECK_EQ(51, m.Call(uint64_t(0xE0B8000000000000)));
  CHECK_EQ(50, m.Call(uint64_t(0x9CE4000000000000)));
  CHECK_EQ(49, m.Call(uint64_t(0xC792000000000000)));
  CHECK_EQ(48, m.Call(uint64_t(0xB8F1000000000000)));
  CHECK_EQ(47, m.Call(uint64_t(0x3B9F800000000000)));
  CHECK_EQ(46, m.Call(uint64_t(0xDB4C400000000000)));
  CHECK_EQ(45, m.Call(uint64_t(0xE9A3200000000000)));
  CHECK_EQ(44, m.Call(uint64_t(0xFCA6100000000000)));
  CHECK_EQ(43, m.Call(uint64_t(0x6C8A780000000000)));
  CHECK_EQ(42, m.Call(uint64_t(0x8CE5A40000000000)));
  CHECK_EQ(41, m.Call(uint64_t(0xCB7D020000000000)));
  CHECK_EQ(40, m.Call(uint64_t(0xCB4DC10000000000)));
  CHECK_EQ(39, m.Call(uint64_t(0xDFBEC58000000000)));
  CHECK_EQ(38, m.Call(uint64_t(0x27A9DB4000000000)));
  CHECK_EQ(37, m.Call(uint64_t(0xDE3BCB2000000000)));
  CHECK_EQ(36, m.Call(uint64_t(0xD7E8A61000000000)));
  CHECK_EQ(35, m.Call(uint64_t(0x9AFDBC8800000000)));
  CHECK_EQ(34, m.Call(uint64_t(0x9AFDBC8400000000)));
  CHECK_EQ(33, m.Call(uint64_t(0x9AFDBC8200000000)));
  CHECK_EQ(32, m.Call(uint64_t(0x9AFDBC8100000000)));
  CHECK_EQ(31, m.Call(uint64_t(0x0000000080000000)));
  CHECK_EQ(30, m.Call(uint64_t(0x0000000040000000)));
  CHECK_EQ(29, m.Call(uint64_t(0x0000000020000000)));
  CHECK_EQ(28, m.Call(uint64_t(0x0000000010000000)));
  CHECK_EQ(27, m.Call(uint64_t(0x00000000A8000000)));
  CHECK_EQ(26, m.Call(uint64_t(0x00000000F4000000)));
  CHECK_EQ(25, m.Call(uint64_t(0x0000000062000000)));
  CHECK_EQ(24, m.Call(uint64_t(0x0000000091000000)));
  CHECK_EQ(23, m.Call(uint64_t(0x00000000CD800000)));
  CHECK_EQ(22, m.Call(uint64_t(0x0000000009400000)));
  CHECK_EQ(21, m.Call(uint64_t(0x00000000AF200000)));
  CHECK_EQ(20, m.Call(uint64_t(0x00000000AC100000)));
  CHECK_EQ(19, m.Call(uint64_t(0x00000000E0B80000)));
  CHECK_EQ(18, m.Call(uint64_t(0x000000009CE40000)));
  CHECK_EQ(17, m.Call(uint64_t(0x00000000C7920000)));
  CHECK_EQ(16, m.Call(uint64_t(0x00000000B8F10000)));
  CHECK_EQ(15, m.Call(uint64_t(0x000000003B9F8000)));
  CHECK_EQ(14, m.Call(uint64_t(0x00000000DB4C4000)));
  CHECK_EQ(13, m.Call(uint64_t(0x00000000E9A32000)));
  CHECK_EQ(12, m.Call(uint64_t(0x00000000FCA61000)));
  CHECK_EQ(11, m.Call(uint64_t(0x000000006C8A7800)));
  CHECK_EQ(10, m.Call(uint64_t(0x000000008CE5A400)));
  CHECK_EQ(9, m.Call(uint64_t(0x00000000CB7D0200)));
  CHECK_EQ(8, m.Call(uint64_t(0x00000000CB4DC100)));
  CHECK_EQ(7, m.Call(uint64_t(0x00000000DFBEC580)));
  CHECK_EQ(6, m.Call(uint64_t(0x0000000027A9DB40)));
  CHECK_EQ(5, m.Call(uint64_t(0x00000000DE3BCB20)));
  CHECK_EQ(4, m.Call(uint64_t(0x00000000D7E8A610)));
  CHECK_EQ(3, m.Call(uint64_t(0x000000009AFDBC88)));
  CHECK_EQ(2, m.Call(uint64_t(0x000000009AFDBC84)));
  CHECK_EQ(1, m.Call(uint64_t(0x000000009AFDBC82)));
  CHECK_EQ(0, m.Call(uint64_t(0x000000009AFDBC81)));
}


TEST(RunWord64Popcnt) {
  BufferedRawMachineAssemblerTester<int32_t> m(MachineType::Uint64());
  if (!m.machine()->Word64Popcnt().IsSupported()) {
    return;
  }

  m.Return(m.AddNode(m.machine()->Word64Popcnt().op(), m.Parameter(0)));

  CHECK_EQ(0, m.Call(uint64_t(0x0000000000000000)));
  CHECK_EQ(1, m.Call(uint64_t(0x0000000000000001)));
  CHECK_EQ(1, m.Call(uint64_t(0x8000000000000000)));
  CHECK_EQ(64, m.Call(uint64_t(0xFFFFFFFFFFFFFFFF)));
  CHECK_EQ(12, m.Call(uint64_t(0x000DC100000DC100)));
  CHECK_EQ(18, m.Call(uint64_t(0xE00DC100E00DC100)));
  CHECK_EQ(22, m.Call(uint64_t(0xE00DC103E00DC103)));
  CHECK_EQ(18, m.Call(uint64_t(0x000DC107000DC107)));
}

#endif  // V8_TARGET_ARCH_64_BIT

TEST(RunWord32Select) {
  BufferedRawMachineAssemblerTester<int32_t> m(
      MachineType::Int32(), MachineType::Int32(), MachineType::Int32());
  if (!m.machine()->Word32Select().IsSupported()) {
    return;
  }

  Node* cmp = m.Word32Equal(m.Parameter(2), m.Int32Constant(0));
  m.Return(m.Word32Select(cmp, m.Parameter(0), m.Parameter(1)));
  constexpr int input1 = 16;
  constexpr int input2 = 3443;

  for (int i = 0; i < 2; ++i) {
    int expected = i == 0 ? input1 : input2;
    CHECK_EQ(expected, m.Call(input1, input2, i));
  }
}

TEST(RunWord64Select) {
  BufferedRawMachineAssemblerTester<int64_t> m(
      MachineType::Int64(), MachineType::Int64(), MachineType::Int32());
  if (!m.machine()->Word64Select().IsSupported()) {
    return;
  }

  Node* cmp = m.Word32Equal(m.Parameter(2), m.Int32Constant(0));
  m.Return(m.Word64Select(cmp, m.Parameter(0), m.Parameter(1)));
  constexpr int64_t input1 = 16;
  constexpr int64_t input2 = 0x123456789abc;

  for (int i = 0; i < 2; ++i) {
    int64_t expected = i == 0 ? input1 : input2;
    CHECK_EQ(expected, m.Call(input1, input2, i));
  }
}

TEST(RunSelectUnorderedEqual) {
  BufferedRawMachineAssemblerTester<int64_t> m(
      MachineType::Int64(), MachineType::Int64(), MachineType::Float32());
  if (!m.machine()->Word64Select().IsSupported()) {
    return;
  }

  Node* cmp = m.Float32Equal(m.Parameter(2), m.Float32Constant(0));
  m.Return(m.Word64Select(cmp, m.Parameter(0), m.Parameter(1)));
  constexpr int64_t input1 = 16;
  constexpr int64_t input2 = 0x123456789abc;

  CHECK_EQ(input1, m.Call(input1, input2, float{0}));
  CHECK_EQ(input2, m.Call(input1, input2, float{1}));
  CHECK_EQ(input2, m.Call(input1, input2, std::nanf("")));
}

TEST(RunSelectUnorderedNotEqual) {
  BufferedRawMachineAssemblerTester<int64_t> m(
      MachineType::Int64(), MachineType::Int64(), MachineType::Float32());
  if (!m.machine()->Word64Select().IsSupported()) {
    return;
  }

  Node* cmp = m.Float32NotEqual(m.Parameter(2), m.Float32Constant(0));
  m.Return(m.Word64Select(cmp, m.Parameter(0), m.Parameter(1)));
  constexpr int64_t input1 = 16;
  constexpr int64_t input2 = 0x123456789abc;

  CHECK_EQ(input2, m.Call(input1, input2, float{0}));
  CHECK_EQ(input1, m.Call(input1, input2, float{1}));
  CHECK_EQ(input1, m.Call(input1, input2, std::nanf("")));
}

namespace {
template <typename T>
ExternalReference ExternalRefFromFunc(RawMachineAssemblerTester<T>* m,
                                      Address func_address) {
  ExternalReference::Type func_type = ExternalReference::FAST_C_CALL;
  ApiFunction func(func_address);
  ExternalReference ref = ExternalReference::Create(&func, func_type);
#ifdef V8_USE_SIMULATOR_WITH_GENERIC_C_CALLS
  EncodedCSignature sig = m->call_descriptor()->ToEncodedCSignature();
  m->main_isolate()->simulator_data()->AddSignatureForTargetForTesting(
      func_address, sig);
#endif
  return ref;
}
}  // namespace

namespace {
void FooForSelect() {}
}  // namespace

TEST(RunWord32SelectWithMemoryInput) {
  BufferedRawMachineAssemblerTester<int32_t> m(MachineType::Int32(),
                                               MachineType::Int32());
  if (!m.machine()->Word32Select().IsSupported()) {
    return;
  }

  // Test that the generated code also works with values spilled on the stack.
  ExternalReference ref = ExternalRefFromFunc(&m, FUNCTION_ADDR(FooForSelect));
  constexpr int input1 = 16;
  int input2 = 3443;
  // Load {value2} before the function call so that it gets spilled.
  Node* value2 = m.LoadFromPointer(&input2, MachineType::Int32());
  // Call a function so that {value2} gets spilled on the stack.
  Node* function = m.ExternalConstant(ref);
  m.CallCFunction(function, MachineType::Int32());
  Node* cmp = m.Word32Equal(m.Parameter(1), m.Int32Constant(0));
  m.Return(m.Word32Select(cmp, m.Parameter(0), value2));

  for (int i = 0; i < 2; ++i) {
    int32_t expected = i == 0 ? input1 : input2;
    CHECK_EQ(expected, m.Call(input1, i));
  }
}

TEST(RunWord64SelectWithMemoryInput) {
  BufferedRawMachineAssemblerTester<int64_t> m(MachineType::Int64(),
                                               MachineType::Int32());
  if (!m.machine()->Word64Select().IsSupported()) {
    return;
  }

  // Test that the generated code also works with values spilled on the stack.

  ExternalReference ref = ExternalRefFromFunc(&m, FUNCTION_ADDR(FooForSelect));
  constexpr int64_t input1 = 16;
  int64_t input2 = 0x12345678ABCD;
  // Load {value2} before the function call so that it gets spilled.
  Node* value2 = m.LoadFromPointer(&input2, MachineType::Int64());
  // Call a function so that {value2} gets spilled on the stack.
  Node* function = m.ExternalConstant(ref);
  m.CallCFunction(function, MachineType::Int32());
  Node* cmp = m.Word32Equal(m.Parameter(1), m.Int32Constant(0));
  m.Return(m.Word64Select(cmp, m.Parameter(0), value2));

  for (int i = 0; i < 2; ++i) {
    int64_t expected = i == 0 ? input1 : input2;
    CHECK_EQ(expected, m.Call(input1, i));
  }
}

TEST(RunFloat32SelectRegFloatCompare) {
  BufferedRawMachineAssemblerTester<float> m(MachineType::Float32(),
                                             MachineType::Float32());
  if (!m.machine()->Float32Select().IsSupported()) {
    return;
  }

  Node* cmp = m.Float32Equal(m.Parameter(0), m.Parameter(1));
  m.Return(m.Float32Select(cmp, m.Parameter(0), m.Parameter(1)));

  FOR_FLOAT32_INPUTS(pl) {
    FOR_FLOAT32_INPUTS(pr) {
      float expected_result = pl == pr ? pl : pr;
      CHECK_FLOAT_EQ(expected_result, m.Call(pl, pr));
    }
  }
}

TEST(RunFloat64SelectRegFloatCompare) {
  BufferedRawMachineAssemblerTester<double> m(MachineType::Float64(),
                                              MachineType::Float64());
  if (!m.machine()->Float64Select().IsSupported()) {
    return;
  }

  Node* cmp = m.Float64LessThan(m.Parameter(0), m.Parameter(1));
  m.Return(m.Float64Select(cmp, m.Parameter(0), m.Parameter(1)));

  FOR_FLOAT64_INPUTS(pl) {
    FOR_FLOAT64_INPUTS(pr) {
      double expected_result = pl < pr ? pl : pr;
      CHECK_DOUBLE_EQ(expected_result, m.Call(pl, pr));
    }
  }
}

TEST(RunFloat32SelectImmediateOnLeftFloatCompare) {
  BufferedRawMachineAssemblerTester<float> m(MachineType::Float32());
  if (!m.machine()->Float32Select().IsSupported()) {
    return;
  }

  const float pl = -5.0;
  Node* a = m.Float32Constant(pl);
  Node* cmp = m.Float32LessThan(a, m.Parameter(0));
  m.Return(m.Float32Select(cmp, a, m.Parameter(0)));

  FOR_FLOAT32_INPUTS(pr) {
    float expected_result = pl < pr ? pl : pr;
    CHECK_FLOAT_EQ(expected_result, m.Call(pr));
  }
}

TEST(RunFloat64SelectImmediateOnRightFloatCompare) {
  BufferedRawMachineAssemblerTester<double> m(MachineType::Float64());
  if (!m.machine()->Float64Select().IsSupported()) {
    return;
  }

  double pr = 5.0;
  Node* b = m.Float64Constant(pr);
  Node* cmp = m.Float64LessThanOrEqual(m.Parameter(0), b);
  m.Return(m.Float64Select(cmp, m.Parameter(0), b));

  FOR_FLOAT64_INPUTS(pl) {
    double expected_result = pl <= pr ? pl : pr;
    CHECK_DOUBLE_EQ(expected_result, m.Call(pl));
  }
}

TEST(RunFloat32SelectImmediateIntCompare) {
  BufferedRawMachineAssemblerTester<float> m(MachineType::Int32(),
                                             MachineType::Int32());
  if (!m.machine()->Float32Select().IsSupported()) {
    return;
  }

  float tval = -0.0;
  float fval = 1.0;
  Node* cmp = m.Int32LessThanOrEqual(m.Parameter(0), m.Parameter(1));
  m.Return(
      m.Float32Select(cmp, m.Float32Constant(tval), m.Float32Constant(fval)));

  FOR_INT32_INPUTS(pl) {
    FOR_INT32_INPUTS(pr) {
      float expected_result = pl <= pr ? tval : fval;
      float actual_result = m.Call(pl, pr);
      CHECK_FLOAT_EQ(expected_result, actual_result);
      CHECK_EQ(std::signbit(expected_result), std::signbit(actual_result));
    }
  }
}

TEST(RunFloat64SelectImmediateIntCompare) {
  BufferedRawMachineAssemblerTester<double> m(MachineType::Int64(),
                                              MachineType::Int64());
  if (!m.machine()->Float64Select().IsSupported()) {
    return;
  }

  double tval = -1.0;
  double fval = 0.0;
  Node* cmp = m.Int64LessThan(m.Parameter(0), m.Parameter(1));
  m.Return(m.Float64Select(cmp, m.Float64Constant(tval),
                           m.Float64Constant(fval)));

  FOR_INT64_INPUTS(pl) {
    FOR_INT64_INPUTS(pr) {
      double expected_result = pl < pr ? tval : fval;
      double actual_result = m.Call(pl, pr);
      CHECK_DOUBLE_EQ(expected_result, actual_result);
      CHECK_EQ(std::signbit(expected_result), std::signbit(actual_result));
    }
  }
}

static Node* Int32Input(RawMachineAssemblerTester<int32_t>* m, int index) {
  switch (index) {
    case 0:
      return m->Parameter(0);
    case 1:
      return m->Parameter(1);
    case 2:
      return m->Int32Constant(0);
    case 3:
      return m->Int32Constant(1);
    case 4:
      return m->Int32Constant(-1);
    case 5:
      return m->Int32Constant(0xFF);
    case 6:
      return m->Int32Constant(0x01234567);
    case 7:
      return m->Load(MachineType::Int32(), m->PointerConstant(nullptr));
    default:
      return nullptr;
  }
}


TEST(CodeGenInt32Binop) {
  RawMachineAssemblerTester<void> m;

  const Operator* kOps[] = {
      m.machine()->Word32And(),      m.machine()->Word32Or(),
      m.machine()->Word32Xor(),      m.machine()->Word32Shl(),
      m.machine()->Word32Shr(),      m.machine()->Word32Sar(),
      m.machine()->Word32Equal(),    m.machine()->Int32Add(),
      m.machine()->Int32Sub(),       m.machine()->Int32Mul(),
      m.machine()->Int32MulHigh(),   m.machine()->Int32Div(),
      m.machine()->Uint32Div(),      m.machine()->Int32Mod(),
      m.machine()->Uint32Mod(),      m.machine()->Uint32MulHigh(),
      m.machine()->Int32LessThan(),  m.machine()->Int32LessThanOrEqual(),
      m.machine()->Uint32LessThan(), m.machine()->Uint32LessThanOrEqual()};

  for (size_t i = 0; i < arraysize(kOps); ++i) {
    for (int j = 0; j < 8; j++) {
      for (int k = 0; k < 8; k++) {
        RawMachineAssemblerTester<int32_t> t(MachineType::Int32(),
                                             MachineType::Int32());
        Node* a = Int32Input(&t, j);
        Node* b = Int32Input(&t, k);
        t.Return(t.AddNode(kOps[i], a, b));
        t.GenerateCode();
      }
    }
  }
}


TEST(CodeGenNop) {
  RawMachineAssemblerTester<void> m;
  m.Return(m.Int32Constant(0));
  m.GenerateCode();
}


#if V8_TARGET_ARCH_64_BIT
static Node* Int64Input(RawMachineAssemblerTester<int64_t>* m, int index) {
  switch (index) {
    case 0:
      return m->Parameter(0);
    case 1:
      return m->Parameter(1);
    case 2:
      return m->Int64Constant(0);
    case 3:
      return m->Int64Constant(1);
    case 4:
      return m->Int64Constant(-1);
    case 5:
      return m->Int64Constant(0xFF);
    case 6:
      return m->Int64Constant(0x0123456789ABCDEFLL);
    case 7:
      return m->Load(MachineType::Int64(), m->PointerConstant(nullptr));
    default:
      return nullptr;
  }
}


TEST(CodeGenInt64Binop) {
  RawMachineAssemblerTester<void> m;

  const Operator* kOps[] = {
      m.machine()->Word64And(), m.machine()->Word64Or(),
      m.machine()->Word64Xor(), m.machine()->Word64Shl(),
      m.machine()->Word64Shr(), m.machine()->Word64Sar(),
      m.machine()->Word64Equal(), m.machine()->Int64Add(),
      m.machine()->Int64Sub(), m.machine()->Int64Mul(), m.machine()->Int64Div(),
      m.machine()->Uint64Div(), m.machine()->Int64Mod(),
      m.machine()->Uint64Mod(), m.machine()->Int64LessThan(),
      m.machine()->Int64LessThanOrEqual(), m.machine()->Uint64LessThan(),
      m.machine()->Uint64LessThanOrEqual()};

  for (size_t i = 0; i < arraysize(kOps); ++i) {
    for (int j = 0; j < 8; j++) {
      for (int k = 0; k < 8; k++) {
        RawMachineAssemblerTester<int64_t> t(MachineType::Int64(),
                                             MachineType::Int64());
        Node* a = Int64Input(&t, j);
        Node* b = Int64Input(&t, k);
        t.Return(t.AddNode(kOps[i], a, b));
        t.GenerateCode();
      }
    }
  }
}


TEST(RunInt64AddWithOverflowP) {
  int64_t actual_val = -1;
  RawMachineAssemblerTester<int32_t> m;
  Int64BinopTester bt(&m);
  Node* add = m.Int64AddWithOverflow(bt.param0, bt.param1);
  Node* val = m.Projection(0, add);
  Node* ovf = m.Projection(1, add);
  m.StoreToPointer(&actual_val, MachineRepresentation::kWord64, val);
  bt.AddReturn(ovf);
  FOR_INT64_INPUTS(i) {
    FOR_INT64_INPUTS(j) {
      int64_t expected_val;
      int expected_ovf = base::bits::SignedAddOverflow64(i, j, &expected_val);
      CHECK_EQ(expected_ovf, bt.call(i, j));
      CHECK_EQ(expected_val, actual_val);
    }
  }
}


TEST(RunInt64AddWithOverflowImm) {
  int64_t actual_val = -1, expected_val = 0;
  FOR_INT64_INPUTS(i) {
    {
      RawMachineAssemblerTester<int32_t> m(MachineType::Int64());
      Node* add = m.Int64AddWithOverflow(m.Int64Constant(i), m.Parameter(0));
      Node* val = m.Projection(0, add);
      Node* ovf = m.Projection(1, add);
      m.StoreToPointer(&actual_val, MachineRepresentation::kWord64, val);
      m.Return(ovf);
      FOR_INT64_INPUTS(j) {
        int expected_ovf = base::bits::SignedAddOverflow64(i, j, &expected_val);
        CHECK_EQ(expected_ovf, m.Call(j));
        CHECK_EQ(expected_val, actual_val);
      }
    }
    {
      RawMachineAssemblerTester<int32_t> m(MachineType::Int64());
      Node* add = m.Int64AddWithOverflow(m.Parameter(0), m.Int64Constant(i));
      Node* val = m.Projection(0, add);
      Node* ovf = m.Projection(1, add);
      m.StoreToPointer(&actual_val, MachineRepresentation::kWord64, val);
      m.Return(ovf);
      FOR_INT64_INPUTS(j) {
        int expected_ovf = base::bits::SignedAddOverflow64(i, j, &expected_val);
        CHECK_EQ(expected_ovf, m.Call(j));
        CHECK_EQ(expected_val, actual_val);
      }
    }
    FOR_INT64_INPUTS(j) {
      RawMachineAssemblerTester<int32_t> m;
      Node* add =
          m.Int64AddWithOverflow(m.Int64Constant(i), m.Int64Constant(j));
      Node* val = m.Projection(0, add);
      Node* ovf = m.Projection(1, add);
      m.StoreToPointer(&actual_val, MachineRepresentation::kWord64, val);
      m.Return(ovf);
      int expected_ovf = base::bits::SignedAddOverflow64(i, j, &expected_val);
      CHECK_EQ(expected_ovf, m.Call());
      CHECK_EQ(expected_val, actual_val);
    }
  }
}


TEST(RunInt64AddWithOverflowInBranchP) {
  int constant = 911777;
  RawMachineLabel blocka, blockb;
  RawMachineAssemblerTester<int32_t> m;
  Int64BinopTester bt(&m);
  Node* add = m.Int64AddWithOverflow(bt.param0, bt.param1);
  Node* ovf = m.Projection(1, add);
  m.Branch(ovf, &blocka, &blockb);
  m.Bind(&blocka);
  bt.AddReturn(m.Int64Constant(constant));
  m.Bind(&blockb);
  Node* val = m.Projection(0, add);
  Node* truncated = m.TruncateInt64ToInt32(val);
  bt.AddReturn(truncated);
  FOR_INT64_INPUTS(i) {
    FOR_INT64_INPUTS(j) {
      int32_t expected = constant;
      int64_t result;
      if (!base::bits::SignedAddOverflow64(i, j, &result)) {
        expected = static_cast<int32_t>(result);
      }
      CHECK_EQ(expected, bt.call(i, j));
    }
  }
}


TEST(RunInt64SubWithOverflowP) {
  int64_t actual_val = -1;
  RawMachineAssemblerTester<int32_t> m;
  Int64BinopTester bt(&m);
  Node* add = m.Int64SubWithOverflow(bt.param0, bt.param1);
  Node* val = m.Projection(0, add);
  Node* ovf = m.Projection(1, add);
  m.StoreToPointer(&actual_val, MachineRepresentation::kWord64, val);
  bt.AddReturn(ovf);
  FOR_INT64_INPUTS(i) {
    FOR_INT64_INPUTS(j) {
      int64_t expected_val;
      int expected_ovf = base::bits::SignedSubOverflow64(i, j, &expected_val);
      CHECK_EQ(expected_ovf, bt.call(i, j));
      CHECK_EQ(expected_val, actual_val);
    }
  }
}


TEST(RunInt64SubWithOverflowImm) {
  int64_t actual_val = -1, expected_val = 0;
  FOR_INT64_INPUTS(i) {
    {
      RawMachineAssemblerTester<int32_t> m(MachineType::Int64());
      Node* add = m.Int64SubWithOverflow(m.Int64Constant(i), m.Parameter(0));
      Node* val = m.Projection(0, add);
      Node* ovf = m.Projection(1, add);
      m.StoreToPointer(&actual_val, MachineRepresentation::kWord64, val);
      m.Return(ovf);
      FOR_INT64_INPUTS(j) {
        int expected_ovf = base::bits::SignedSubOverflow64(i, j, &expected_val);
        CHECK_EQ(expected_ovf, m.Call(j));
        CHECK_EQ(expected_val, actual_val);
      }
    }
    {
      RawMachineAssemblerTester<int32_t> m(MachineType::Int64());
      Node* add = m.Int64SubWithOverflow(m.Parameter(0), m.Int64Constant(i));
      Node* val = m.Projection(0, add);
      Node* ovf = m.Projection(1, add);
      m.StoreToPointer(&actual_val, MachineRepresentation::kWord64, val);
      m.Return(ovf);
      FOR_INT64_INPUTS(j) {
        int expected_ovf = base::bits::SignedSubOverflow64(j, i, &expected_val);
        CHECK_EQ(expected_ovf, m.Call(j));
        CHECK_EQ(expected_val, actual_val);
      }
    }
    FOR_INT64_INPUTS(j) {
      RawMachineAssemblerTester<int32_t> m;
      Node* add =
          m.Int64SubWithOverflow(m.Int64Constant(i), m.Int64Constant(j));
      Node* val = m.Projection(0, add);
      Node* ovf = m.Projection(1, add);
      m.StoreToPointer(&actual_val, MachineRepresentation::kWord64, val);
      m.Return(ovf);
      int expected_ovf = base::bits::SignedSubOverflow64(i, j, &expected_val);
      CHECK_EQ(expected_ovf, m.Call());
      CHECK_EQ(expected_val, actual_val);
    }
  }
}


TEST(RunInt64SubWithOverflowInBranchP) {
  int constant = 911999;
  RawMachineLabel blocka, blockb;
  RawMachineAssemblerTester<int32_t> m;
  Int64BinopTester bt(&m);
  Node* sub = m.Int64SubWithOverflow(bt.param0, bt.param1);
  Node* ovf = m.Projection(1, sub);
  m.Branch(ovf, &blocka, &blockb);
  m.Bind(&blocka);
  bt.AddReturn(m.Int64Constant(constant));
  m.Bind(&blockb);
  Node* val = m.Projection(0, sub);
  Node* truncated = m.TruncateInt64ToInt32(val);
  bt.AddReturn(truncated);
  FOR_INT64_INPUTS(i) {
    FOR_INT64_INPUTS(j) {
      int32_t expected = constant;
      int64_t result;
      if (!base::bits::SignedSubOverflow64(i, j, &result)) {
        expected = static_cast<int32_t>(result);
      }
      CHECK_EQ(expected, static_cast<int32_t>(bt.call(i, j)));
    }
  }
}

TEST(RunInt64MulWithOverflowImm) {
  int64_t actual_val = -1, expected_val = 0;
  FOR_INT64_INPUTS(i) {
    {
      RawMachineAssemblerTester<int32_t> m(MachineType::Int64());
      Node* mul = m.Int64MulWithOverflow(m.Int64Constant(i), m.Parameter(0));
      Node* val = m.Projection(0, mul);
      Node* ovf = m.Projection(1, mul);
      m.StoreToPointer(&actual_val, MachineRepresentation::kWord64, val);
      m.Return(ovf);
      FOR_INT64_INPUTS(j) {
        int expected_ovf = base::bits::SignedMulOverflow64(i, j, &expected_val);
        CHECK_EQ(expected_ovf, m.Call(j));
        CHECK_EQ(expected_val, actual_val);
      }
    }
    {
      RawMachineAssemblerTester<int32_t> m(MachineType::Int64());
      Node* mul = m.Int64MulWithOverflow(m.Parameter(0), m.Int64Constant(i));
      Node* val = m.Projection(0, mul);
      Node* ovf = m.Projection(1, mul);
      m.StoreToPointer(&actual_val, MachineRepresentation::kWord64, val);
      m.Return(ovf);
      FOR_INT64_INPUTS(j) {
        int expected_ovf = base::bits::SignedMulOverflow64(j, i, &expected_val);
        CHECK_EQ(expected_ovf, m.Call(j));
        CHECK_EQ(expected_val, actual_val);
      }
    }
    FOR_INT64_INPUTS(j) {
      RawMachineAssemblerTester<int32_t> m;
      Node* mul =
          m.Int64MulWithOverflow(m.Int64Constant(i), m.Int64Constant(j));
      Node* val = m.Projection(0, mul);
      Node* ovf = m.Projection(1, mul);
      m.StoreToPointer(&actual_val, MachineRepresentation::kWord64, val);
      m.Return(ovf);
      int expected_ovf = base::bits::SignedMulOverflow64(i, j, &expected_val);
      CHECK_EQ(expected_ovf, m.Call());
      CHECK_EQ(expected_val, actual_val);
    }
  }
}

TEST(RunInt64MulWithOverflowInBranchP) {
  int constant = 911999;
  RawMachineLabel blocka, blockb;
  RawMachineAssemblerTester<int32_t> m;
  Int64BinopTester bt(&m);
  Node* mul = m.Int64MulWithOverflow(bt.param0, bt.param1);
  Node* ovf = m.Projection(1, mul);
  m.Branch(ovf, &blocka, &blockb);
  m.Bind(&blocka);
  bt.AddReturn(m.Int64Constant(constant));
  m.Bind(&blockb);
  Node* val = m.Projection(0, mul);
  Node* truncated = m.TruncateInt64ToInt32(val);
  bt.AddReturn(truncated);
  FOR_INT64_INPUTS(i) {
    FOR_INT64_INPUTS(j) {
      int32_t expected = constant;
      int64_t result;
      if (!base::bits::SignedMulOverflow64(i, j, &result)) {
        expected = static_cast<int32_t>(result);
      }
      CHECK_EQ(expected, static_cast<int32_t>(bt.call(i, j)));
    }
  }
}

static int64_t RunInt64AddShift(bool is_left, int64_t add_left,
                                int64_t add_right, int64_t shift_left,
                                int64_t shift_right) {
  RawMachineAssemblerTester<int64_t> m;
  Node* shift =
      m.Word64Shl(m.Int64Constant(shift_left), m.Int64Constant(shift_right));
  Node* add = m.Int64Add(m.Int64Constant(add_left), m.Int64Constant(add_right));
  Node* dlsa = is_left ? m.Int64Add(shift, add) : m.Int64Add(add, shift);
  m.Return(dlsa);
  return m.Call();
}

TEST(RunInt64AddShift) {
  struct Test_case {
    int64_t add_left, add_right, shift_left, shift_right, expected;
  };

  Test_case tc[] = {
      {20, 22, 4, 2, 58},
      {20, 22, 4, 1, 50},
      {20, 22, 1, 6, 106},
      {INT64_MAX - 2, 1, 1, 1,
       INT64_MIN},  // INT64_MAX - 2 + 1 + (1 << 1), overflow.
  };
  const size_t tc_size = sizeof(tc) / sizeof(Test_case);

  for (size_t i = 0; i < tc_size; ++i) {
    CHECK_EQ(tc[i].expected,
             RunInt64AddShift(false, tc[i].add_left, tc[i].add_right,
                              tc[i].shift_left, tc[i].shift_right));
    CHECK_EQ(tc[i].expected,
             RunInt64AddShift(true, tc[i].add_left, tc[i].add_right,
                              tc[i].shift_left, tc[i].shift_right));
  }
}

// TODO(titzer): add tests that run 64-bit integer operations.
#endif  // V8_TARGET_ARCH_64_BIT


TEST(RunGoto) {
  RawMachineAssemblerTester<int32_t> m;
  int constant = 99999;

  RawMachineLabel next;
  m.Goto(&next);
  m.Bind(&next);
  m.Return(m.Int32Constant(constant));

  CHECK_EQ(constant, m.Call());
}


TEST(RunGotoMultiple) {
  RawMachineAssemblerTester<int32_t> m;
  int constant = 9999977;

  RawMachineLabel labels[10];
  for (size_t i = 0; i < arraysize(labels); i++) {
    m.Goto(&labels[i]);
    m.Bind(&labels[i]);
  }
  m.Return(m.Int32Constant(constant));

  CHECK_EQ(constant, m.Call());
}


TEST(RunBranch) {
  RawMachineAssemblerTester<int32_t> m;
  int constant = 999777;

  RawMachineLabel blocka, blockb;
  m.Branch(m.Int32Constant(0), &blocka, &blockb);
  m.Bind(&blocka);
  m.Return(m.Int32Constant(0 - constant));
  m.Bind(&blockb);
  m.Return(m.Int32Constant(constant));

  CHECK_EQ(constant, m.Call());
}


TEST(RunDiamond2) {
  RawMachineAssemblerTester<int32_t> m;

  int constant = 995666;

  RawMachineLabel blocka, blockb, end;
  m.Branch(m.Int32Constant(0), &blocka, &blockb);
  m.Bind(&blocka);
  m.Goto(&end);
  m.Bind(&blockb);
  m.Goto(&end);
  m.Bind(&end);
  m.Return(m.Int32Constant(constant));

  CHECK_EQ(constant, m.Call());
}


TEST(RunLoop) {
  RawMachineAssemblerTester<int32_t> m;
  int constant = 999555;

  RawMachineLabel header, body, exit;
  m.Goto(&header);
  m.Bind(&header);
  m.Branch(m.Int32Constant(0), &body, &exit);
  m.Bind(&body);
  m.Goto(&header);
  m.Bind(&exit);
  m.Return(m.Int32Constant(constant));

  CHECK_EQ(constant, m.Call());
}


template <typename R>
static void BuildDiamondPhi(RawMachineAssemblerTester<R>* m, Node* cond_node,
                            MachineRepresentation rep, Node* true_node,
                            Node* false_node) {
  RawMachineLabel blocka, blockb, end;
  m->Branch(cond_node, &blocka, &blockb);
  m->Bind(&blocka);
  m->Goto(&end);
  m->Bind(&blockb);
  m->Goto(&end);

  m->Bind(&end);
  Node* phi = m->Phi(rep, true_node, false_node);
  m->Return(phi);
}


TEST(RunDiamondPhiConst) {
  RawMachineAssemblerTester<int32_t> m(MachineType::Int32());
  int false_val = 0xFF666;
  int true_val = 0x00DDD;
  Node* true_node = m.Int32Constant(true_val);
  Node* false_node = m.Int32Constant(false_val);
  BuildDiamondPhi(&m, m.Parameter(0), MachineRepresentation::kWord32, true_node,
                  false_node);
  CHECK_EQ(false_val, m.Call(0));
  CHECK_EQ(true_val, m.Call(1));
}


TEST(RunDiamondPhiNumber) {
  RawMachineAssemblerTester<Tagged<Object>> m(MachineType::Int32());
  double false_val = -11.1;
  double true_val = 200.1;
  Node* true_node = m.NumberConstant(true_val);
  Node* false_node = m.NumberConstant(false_val);
  BuildDiamondPhi(&m, m.Parameter(0), MachineRepresentation::kTagged, true_node,
                  false_node);
  m.CheckNumber(false_val, m.Call(0));
  m.CheckNumber(true_val, m.Call(1));
}


TEST(RunDiamondPhiString) {
  RawMachineAssemblerTester<Tagged<Object>> m(MachineType::Int32());
  const char* false_val = "false";
  const char* true_val = "true";
  Node* true_node = m.StringConstant(true_val);
  Node* false_node = m.StringConstant(false_val);
  BuildDiamondPhi(&m, m.Parameter(0), MachineRepresentation::kTagged, true_node,
                  false_node);
  m.CheckString(false_val, m.Call(0));
  m.CheckString(true_val, m.Call(1));
}


TEST(RunDiamondPhiParam) {
  RawMachineAssemblerTester<int32_t> m(
      MachineType::Int32(), MachineType::Int32(), MachineType::Int32());
  BuildDiamondPhi(&m, m.Parameter(0), MachineRepresentation::kWord32,
                  m.Parameter(1), m.Parameter(2));
  int32_t c1 = 0x260CB75A;
  int32_t c2 = 0xCD3E9C8B;
  int result = m.Call(0, c1, c2);
  CHECK_EQ(c2, result);
  result = m.Call(1, c1, c2);
  CHECK_EQ(c1, result);
}


TEST(RunLoopPhiConst) {
  RawMachineAssemblerTester<int32_t> m;
  int true_val = 0x44000;
  int false_val = 0x00888;

  Node* cond_node = m.Int32Constant(0);
  Node* true_node = m.Int32Constant(true_val);
  Node* false_node = m.Int32Constant(false_val);

  // x = false_val; while(false) { x = true_val; } return x;
  RawMachineLabel body, header, end;

  m.Goto(&header);
  m.Bind(&header);
  Node* phi = m.Phi(MachineRepresentation::kWord32, false_node, true_node);
  m.Branch(cond_node, &body, &end);
  m.Bind(&body);
  m.Goto(&header);
  m.Bind(&end);
  m.Return(phi);

  CHECK_EQ(false_val, m.Call());
}


TEST(RunLoopPhiParam) {
  RawMachineAssemblerTester<int32_t> m(
      MachineType::Int32(), MachineType::Int32(), MachineType::Int32());

  RawMachineLabel blocka, blockb, end;

  m.Goto(&blocka);

  m.Bind(&blocka);
  Node* phi =
      m.Phi(MachineRepresentation::kWord32, m.Parameter(1), m.Parameter(2));
  Node* cond =
      m.Phi(MachineRepresentation::kWord32, m.Parameter(0), m.Int32Constant(0));
  m.Branch(cond, &blockb, &end);

  m.Bind(&blockb);
  m.Goto(&blocka);

  m.Bind(&end);
  m.Return(phi);

  int32_t c1 = 0xA81903B4;
  int32_t c2 = 0x5A1207DA;
  int result = m.Call(0, c1, c2);
  CHECK_EQ(c1, result);
  result = m.Call(1, c1, c2);
  CHECK_EQ(c2, result);
}


TEST(RunLoopPhiInduction) {
  RawMachineAssemblerTester<int32_t> m;

  int false_val = 0x10777;

  // x = false_val; while(false) { x++; } return x;
  RawMachineLabel header, body, end;
  Node* false_node = m.Int32Constant(false_val);

  m.Goto(&header);

  m.Bind(&header);
  Node* phi = m.Phi(MachineRepresentation::kWord32, false_node, false_node);
  m.Branch(m.Int32Constant(0), &body, &end);

  m.Bind(&body);
  Node* add = m.Int32Add(phi, m.Int32Constant(1));
  phi->ReplaceInput(1, add);
  m.Goto(&header);

  m.Bind(&end);
  m.Return(phi);

  CHECK_EQ(false_val, m.Call());
}


TEST(RunLoopIncrement) {
  RawMachineAssemblerTester<int32_t> m;
  Int32BinopTester bt(&m);

  // x = 0; while(x ^ param) { x++; } return x;
  RawMachineLabel header, body, end;
  Node* zero = m.Int32Constant(0);

  m.Goto(&header);

  m.Bind(&header);
  Node* phi = m.Phi(MachineRepresentation::kWord32, zero, zero);
  m.Branch(m.WordXor(phi, bt.param0), &body, &end);

  m.Bind(&body);
  phi->ReplaceInput(1, m.Int32Add(phi, m.Int32Constant(1)));
  m.Goto(&header);

  m.Bind(&end);
  bt.AddReturn(phi);

  CHECK_EQ(11, bt.call(11, 0));
  CHECK_EQ(110, bt.call(110, 0));
  CHECK_EQ(176, bt.call(176, 0));
}


TEST(RunLoopIncrement2) {
  RawMachineAssemblerTester<int32_t> m;
  Int32BinopTester bt(&m);

  // x = 0; while(x < param) { x++; } return x;
  RawMachineLabel header, body, end;
  Node* zero = m.Int32Constant(0);

  m.Goto(&header);

  m.Bind(&header);
  Node* phi = m.Phi(MachineRepresentation::kWord32, zero, zero);
  m.Branch(m.Int32LessThan(phi, bt.param0), &body, &end);

  m.Bind(&body);
  phi->ReplaceInput(1, m.Int32Add(phi, m.Int32Constant(1)));
  m.Goto(&header);

  m.Bind(&end);
  bt.AddReturn(phi);

  CHECK_EQ(11, bt.call(11, 0));
  CHECK_EQ(110, bt.call(110, 0));
  CHECK_EQ(176, bt.call(176, 0));
  CHECK_EQ(0, bt.call(-200, 0));
}


TEST(RunLoopIncrement3) {
  RawMachineAssemblerTester<int32_t> m;
  Int32BinopTester bt(&m);

  // x = 0; while(x < param) { x++; } return x;
  RawMachineLabel header, body, end;
  Node* zero = m.Int32Constant(0);

  m.Goto(&header);

  m.Bind(&header);
  Node* phi = m.Phi(MachineRepresentation::kWord32, zero, zero);
  m.Branch(m.Uint32LessThan(phi, bt.param0), &body, &end);

  m.Bind(&body);
  phi->ReplaceInput(1, m.Int32Add(phi, m.Int32Constant(1)));
  m.Goto(&header);

  m.Bind(&end);
  bt.AddReturn(phi);

  CHECK_EQ(11, bt.call(11, 0));
  CHECK_EQ(110, bt.call(110, 0));
  CHECK_EQ(176, bt.call(176, 0));
  CHECK_EQ(200, bt.call(200, 0));
}


TEST(RunLoopDecrement) {
  RawMachineAssemblerTester<int32_t> m;
  Int32BinopTester bt(&m);

  // x = param; while(x) { x--; } return x;
  RawMachineLabel header, body, end;

  m.Goto(&header);

  m.Bind(&header);
  Node* phi =
      m.Phi(MachineRepresentation::kWord32, bt.param0, m.Int32Constant(0));
  m.Branch(phi, &body, &end);

  m.Bind(&body);
  phi->ReplaceInput(1, m.Int32Sub(phi, m.Int32Constant(1)));
  m.Goto(&header);

  m.Bind(&end);
  bt.AddReturn(phi);

  CHECK_EQ(0, bt.call(11, 0));
  CHECK_EQ(0, bt.call(110, 0));
  CHECK_EQ(0, bt.call(197, 0));
}


TEST(RunLoopIncrementFloat32) {
  RawMachineAssemblerTester<int32_t> m;

  // x = -3.0f; while(x < 10f) { x = x + 0.5f; } return (int) (double) x;
  RawMachineLabel header, body, end;
  Node* minus_3 = m.Float32Constant(-3.0f);
  Node* ten = m.Float32Constant(10.0f);

  m.Goto(&header);

  m.Bind(&header);
  Node* phi = m.Phi(MachineRepresentation::kFloat32, minus_3, ten);
  m.Branch(m.Float32LessThan(phi, ten), &body, &end);

  m.Bind(&body);
  phi->ReplaceInput(1, m.Float32Add(phi, m.Float32Constant(0.5f)));
  m.Goto(&header);

  m.Bind(&end);
  m.Return(m.ChangeFloat64ToInt32(m.ChangeFloat32ToFloat64(phi)));

  CHECK_EQ(10, m.Call());
}


TEST(RunLoopIncrementFloat64) {
  RawMachineAssemblerTester<int32_t> m;

  // x = -3.0; while(x < 10) { x = x + 0.5; } return (int) x;
  RawMachineLabel header, body, end;
  Node* minus_3 = m.Float64Constant(-3.0);
  Node* ten = m.Float64Constant(10.0);

  m.Goto(&header);

  m.Bind(&header);
  Node* phi = m.Phi(MachineRepresentation::kFloat64, minus_3, ten);
  m.Branch(m.Float64LessThan(phi, ten), &body, &end);

  m.Bind(&body);
  phi->ReplaceInput(1, m.Float64Add(phi, m.Float64Constant(0.5)));
  m.Goto(&header);

  m.Bind(&end);
  m.Return(m.ChangeFloat64ToInt32(phi));

  CHECK_EQ(10, m.Call());
}


TEST(RunSwitch1) {
  RawMachineAssemblerTester<int32_t> m;

  int constant = 11223344;

  RawMachineLabel block0, block1, def, end;
  RawMachineLabel* case_labels[] = {&block0, &block1};
  int32_t case_values[] = {0, 1};
  m.Switch(m.Int32Constant(0), &def, case_values, case_labels,
           arraysize(case_labels));
  m.Bind(&block0);
  m.Goto(&end);
  m.Bind(&block1);
  m.Goto(&end);
  m.Bind(&def);
  m.Goto(&end);
  m.Bind(&end);
  m.Return(m.Int32Constant(constant));

  CHECK_EQ(constant, m.Call());
}


TEST(RunSwitch2) {
  RawMachineAssemblerTester<int32_t> m(MachineType::Int32());

  RawMachineLabel blocka, blockb, blockc;
  RawMachineLabel* case_labels[] = {&blocka, &blockb};
  int32_t case_values[] = {std::numeric_limits<int32_t>::min(),
                           std::numeric_limits<int32_t>::max()};
  m.Switch(m.Parameter(0), &blockc, case_values, case_labels,
           arraysize(case_labels));
  m.Bind(&blocka);
  m.Return(m.Int32Constant(-1));
  m.Bind(&blockb);
  m.Return(m.Int32Constant(1));
  m.Bind(&blockc);
  m.Return(m.Int32Constant(0));

  CHECK_EQ(1, m.Call(std::numeric_limits<int32_t>::max()));
  CHECK_EQ(-1, m.Call(std::numeric_limits<int32_t>::min()));
  for (int i = -100; i < 100; i += 25) {
    CHECK_EQ(0, m.Call(i));
  }
}


TEST(RunSwitch3) {
  RawMachineAssemblerTester<int32_t> m(MachineType::Int32());

  RawMachineLabel blocka, blockb, blockc;
  RawMachineLabel* case_labels[] = {&blocka, &blockb};
  int32_t case_values[] = {std::numeric_limits<int32_t>::min() + 0,
                           std::numeric_limits<int32_t>::min() + 1};
  m.Switch(m.Parameter(0), &blockc, case_values, case_labels,
           arraysize(case_labels));
  m.Bind(&blocka);
  m.Return(m.Int32Constant(0));
  m.Bind(&blockb);
  m.Return(m.Int32Constant(1));
  m.Bind(&blockc);
  m.Return(m.Int32Constant(2));

  CHECK_EQ(0, m.Call(std::numeric_limits<int32_t>::min() + 0));
  CHECK_EQ(1, m.Call(std::numeric_limits<int32_t>::min() + 1));
  for (int i = -100; i < 100; i += 25) {
    CHECK_EQ(2, m.Call(i));
  }
}


TEST(RunSwitch4) {
  RawMachineAssemblerTester<int32_t> m(MachineType::Int32());

  const size_t kNumCases = 512;
  const size_t kNumValues = kNumCases + 1;
  int32_t values[kNumValues];
  m.main_isolate()->random_number_generator()->NextBytes(values,
                                                         sizeof(values));
  RawMachineLabel end, def;
  int32_t case_values[kNumCases];
  RawMachineLabel* case_labels[kNumCases];
  Node* results[kNumValues];
  for (size_t i = 0; i < kNumCases; ++i) {
    case_values[i] = static_cast<int32_t>(i);
    case_labels[i] = m.main_zone()->New<RawMachineLabel>();
  }
  m.Switch(m.Parameter(0), &def, case_values, case_labels,
           arraysize(case_labels));
  for (size_t i = 0; i < kNumCases; ++i) {
    m.Bind(case_labels[i]);
    results[i] = m.Int32Constant(values[i]);
    m.Goto(&end);
  }
  m.Bind(&def);
  results[kNumCases] = m.Int32Constant(values[kNumCases]);
  m.Goto(&end);
  m.Bind(&end);
  const int num_results = static_cast<int>(arraysize(results));
  Node* phi =
      m.AddNode(m.common()->Phi(MachineRepresentation::kWord32, num_results),
                num_results, results);
  m.Return(phi);

  for (size_t i = 0; i < kNumValues; ++i) {
    CHECK_EQ(values[i], m.Call(static_cast<int>(i)));
  }
}


TEST(RunInt32AddP) {
  RawMachineAssemblerTester<int32_t> m;
  Int32BinopTester bt(&m);

  bt.AddReturn(m.Int32Add(bt.param0, bt.param1));

  FOR_INT32_INPUTS(i) {
    FOR_INT32_INPUTS(j) {
      // Use uint32_t because signed overflow is UB in C.
      int expected = static_cast<int32_t>(static_cast<uint32_t>(i) +
                                          static_cast<uint32_t>(j));
      CHECK_EQ(expected, bt.call(i, j));
    }
  }
}


TEST(RunInt32AddAndWord32EqualP) {
  {
    RawMachineAssemblerTester<int32_t> m(
        MachineType::Int32(), MachineType::Int32(), MachineType::Int32());
    m.Return(m.Int32Add(m.Parameter(0),
                        m.Word32Equal(m.Parameter(1), m.Parameter(2))));
    FOR_INT32_INPUTS(i) {
      FOR_INT32_INPUTS(j) {
        FOR_INT32_INPUTS(k) {
          // Use uint32_t because signed overflow is UB in C.
          int32_t const expected =
              base::bit_cast<int32_t>(base::bit_cast<uint32_t>(i) + (j == k));
          CHECK_EQ(expected, m.Call(i, j, k));
        }
      }
    }
  }
  {
    RawMachineAssemblerTester<int32_t> m(
        MachineType::Int32(), MachineType::Int32(), MachineType::Int32());
    m.Return(m.Int32Add(m.Word32Equal(m.Parameter(0), m.Parameter(1)),
                        m.Parameter(2)));
    FOR_INT32_INPUTS(i) {
      FOR_INT32_INPUTS(j) {
        FOR_INT32_INPUTS(k) {
          // Use uint32_t because signed overflow is UB in C.
          int32_t const expected =
              base::bit_cast<int32_t>((i == j) + base::bit_cast<uint32_t>(k));
          CHECK_EQ(expected, m.Call(i, j, k));
        }
      }
    }
  }
}


TEST(RunInt32AddAndWord32EqualImm) {
  {
    FOR_INT32_INPUTS(i) {
      RawMachineAssemblerTester<int32_t> m(MachineType::Int32(),
                                           MachineType::Int32());
  m.Return(m.Int32Add(m.Int32Constant(i),
                      m.Word32Equal(m.Parameter(0), m.Parameter(1))));
  FOR_INT32_INPUTS(j) {
    FOR_INT32_INPUTS(k) {
      // Use uint32_t because signed overflow is UB in C.
      int32_t const expected =
          base::bit_cast<int32_t>(base::bit_cast<uint32_t>(i) + (j == k));
      CHECK_EQ(expected, m.Call(j, k));
    }
  }
    }
  }
  {
    FOR_INT32_INPUTS(i) {
      RawMachineAssemblerTester<int32_t> m(MachineType::Int32(),
                                           MachineType::Int32());
      m.Return(m.Int32Add(m.Word32Equal(m.Int32Constant(i), m.Parameter(0)),
                          m.Parameter(1)));
      FOR_INT32_INPUTS(j) {
        FOR_INT32_INPUTS(k) {
          // Use uint32_t because signed overflow is UB in C.
          int32_t const expected =
              base::bit_cast<int32_t>((i == j) + base::bit_cast<uint32_t>(k));
          CHECK_EQ(expected, m.Call(j, k));
        }
      }
    }
  }
}


TEST(RunInt32AddAndWord32NotEqualP) {
  {
    RawMachineAssemblerTester<int32_t> m(
        MachineType::Int32(), MachineType::Int32(), MachineType::Int32());
    m.Return(m.Int32Add(m.Parameter(0),
                        m.Word32NotEqual(m.Parameter(1), m.Parameter(2))));
    FOR_INT32_INPUTS(i) {
      FOR_INT32_INPUTS(j) {
        FOR_INT32_INPUTS(k) {
          // Use uint32_t because signed overflow is UB in C.
          int32_t const expected =
              base::bit_cast<int32_t>(base::bit_cast<uint32_t>(i) + (j != k));
          CHECK_EQ(expected, m.Call(i, j, k));
        }
      }
    }
  }
  {
    RawMachineAssemblerTester<int32_t> m(
        MachineType::Int32(), MachineType::Int32(), MachineType::Int32());
    m.Return(m.Int32Add(m.Word32NotEqual(m.Parameter(0), m.Parameter(1)),
                        m.Parameter(2)));
    FOR_INT32_INPUTS(i) {
      FOR_INT32_INPUTS(j) {
        FOR_INT32_INPUTS(k) {
          // Use uint32_t because signed overflow is UB in C.
          int32_t const expected =
              base::bit_cast<int32_t>((i != j) + base::bit_cast<uint32_t>(k));
          CHECK_EQ(expected, m.Call(i, j, k));
        }
      }
    }
  }
}


TEST(RunInt32AddAndWord32NotEqualImm) {
  {
    FOR_INT32_INPUTS(i) {
      RawMachineAssemblerTester<int32_t> m(MachineType::Int32(),
                                           MachineType::Int32());
  m.Return(m.Int32Add(m.Int32Constant(i),
                      m.Word32NotEqual(m.Parameter(0), m.Parameter(1))));
  FOR_INT32_INPUTS(j) {
    FOR_INT32_INPUTS(k) {
      // Use uint32_t because signed overflow is UB in C.
      int32_t const expected =
          base::bit_cast<int32_t>(base::bit_cast<uint32_t>(i) + (j != k));
      CHECK_EQ(expected, m.Call(j, k));
    }
  }
    }
  }
  {
    FOR_INT32_INPUTS(i) {
      RawMachineAssemblerTester<int32_t> m(MachineType::Int32(),
                                           MachineType::Int32());
      m.Return(m.Int32Add(m.Word32NotEqual(m.Int32Constant(i), m.Parameter(0)),
                          m.Parameter(1)));
      FOR_INT32_INPUTS(j) {
        FOR_INT32_INPUTS(k) {
          // Use uint32_t because signed overflow is UB in C.
          int32_t const expected =
              base::bit_cast<int32_t>((i != j) + base::bit_cast<uint32_t>(k));
          CHECK_EQ(expected, m.Call(j, k));
        }
      }
    }
  }
}


TEST(RunInt32AddAndWord32SarP) {
  {
    RawMachineAssemblerTester<int32_t> m(
        MachineType::Uint32(), MachineType::Int32(), MachineType::Uint32());
    m.Return(m.Int32Add(m.Parameter(0),
                        m.Word32Sar(m.Parameter(1), m.Parameter(2))));
    FOR_UINT32_INPUTS(i) {
      FOR_INT32_INPUTS(j) {
        FOR_UINT32_SHIFTS(shift) {
          // Use uint32_t because signed overflow is UB in C.
          int32_t expected = i + (j >> shift);
          CHECK_EQ(expected, m.Call(i, j, shift));
        }
      }
    }
  }
  {
    RawMachineAssemblerTester<int32_t> m(
        MachineType::Int32(), MachineType::Uint32(), MachineType::Uint32());
    m.Return(m.Int32Add(m.Word32Sar(m.Parameter(0), m.Parameter(1)),
                        m.Parameter(2)));
    FOR_INT32_INPUTS(i) {
      FOR_UINT32_SHIFTS(shift) {
        FOR_UINT32_INPUTS(k) {
          // Use uint32_t because signed overflow is UB in C.
          int32_t expected = (i >> shift) + k;
          CHECK_EQ(expected, m.Call(i, shift, k));
        }
      }
    }
  }
}


TEST(RunInt32AddAndWord32ShlP) {
  {
    RawMachineAssemblerTester<int32_t> m(
        MachineType::Uint32(), MachineType::Uint32(), MachineType::Uint32());
    m.Return(m.Int32Add(m.Parameter(0),
                        m.Word32Shl(m.Parameter(1), m.Parameter(2))));
    FOR_UINT32_INPUTS(i) {
      FOR_UINT32_INPUTS(j) {
        FOR_UINT32_SHIFTS(shift) {
          // Use uint32_t because signed overflow is UB in C.
          int32_t expected = i + (j << shift);
          CHECK_EQ(expected, m.Call(i, j, shift));
        }
      }
    }
  }
  {
    RawMachineAssemblerTester<int32_t> m(
        MachineType::Uint32(), MachineType::Uint32(), MachineType::Uint32());
    m.Return(m.Int32Add(m.Word32Shl(m.Parameter(0), m.Parameter(1)),
                        m.Parameter(2)));
    FOR_UINT32_INPUTS(i) {
      FOR_UINT32_SHIFTS(shift) {
        FOR_UINT32_INPUTS(k) {
          // Use uint32_t because signed overflow is UB in C.
          int32_t expected = (i << shift) + k;
          CHECK_EQ(expected, m.Call(i, shift, k));
        }
      }
    }
  }
}


TEST(RunInt32AddAndWord32ShrP) {
  {
    RawMachineAssemblerTester<int32_t> m(
        MachineType::Uint32(), MachineType::Uint32(), MachineType::Uint32());
    m.Return(m.Int32Add(m.Parameter(0),
                        m.Word32Shr(m.Parameter(1), m.Parameter(2))));
    FOR_UINT32_INPUTS(i) {
      FOR_UINT32_INPUTS(j) {
        FOR_UINT32_SHIFTS(shift) {
          // Use uint32_t because signed overflow is UB in C.
          int32_t expected = i + (j >> shift);
          CHECK_EQ(expected, m.Call(i, j, shift));
        }
      }
    }
  }
  {
    RawMachineAssemblerTester<int32_t> m(
        MachineType::Uint32(), MachineType::Uint32(), MachineType::Uint32());
    m.Return(m.Int32Add(m.Word32Shr(m.Parameter(0), m.Parameter(1)),
                        m.Parameter(2)));
    FOR_UINT32_INPUTS(i) {
      FOR_UINT32_SHIFTS(shift) {
        FOR_UINT32_INPUTS(k) {
          // Use uint32_t because signed overflow is UB in C.
          int32_t expected = (i >> shift) + k;
          CHECK_EQ(expected, m.Call(i, shift, k));
        }
      }
    }
  }
}


TEST(RunInt32AddInBranch) {
  static const int32_t constant = 987654321;
  {
    RawMachineAssemblerTester<int32_t> m;
    Int32BinopTester bt(&m);
    RawMachineLabel blocka, blockb;
    m.Branch(
        m.Word32Equal(m.Int32Add(bt.param0, bt.param1), m.Int32Constant(0)),
        &blocka, &blockb);
    m.Bind(&blocka);
    bt.AddReturn(m.Int32Constant(constant));
    m.Bind(&blockb);
    bt.AddReturn(m.Int32Constant(0 - constant));
    FOR_UINT32_INPUTS(i) {
      FOR_UINT32_INPUTS(j) {
        int32_t expected = (i + j) == 0 ? constant : 0 - constant;
        CHECK_EQ(expected, bt.call(i, j));
      }
    }
  }
  {
    RawMachineAssemblerTester<int32_t> m;
    Int32BinopTester bt(&m);
    RawMachineLabel blocka, blockb;
    m.Branch(
        m.Word32NotEqual(m.Int32Add(bt.param0, bt.param1), m.Int32Constant(0)),
        &blocka, &blockb);
    m.Bind(&blocka);
    bt.AddReturn(m.Int32Constant(constant));
    m.Bind(&blockb);
    bt.AddReturn(m.Int32Constant(0 - constant));
    FOR_UINT32_INPUTS(i) {
      FOR_UINT32_INPUTS(j) {
        int32_t expected = (i + j) != 0 ? constant : 0 - constant;
        CHECK_EQ(expected, bt.call(i, j));
      }
    }
  }
  {
    FOR_UINT32_INPUTS(i) {
      RawMachineAssemblerTester<int32_t> m(MachineType::Uint32());
      RawMachineLabel blocka, blockb;
      m.Branch(m.Word32Equal(m.Int32Add(m.Int32Constant(i), m.Parameter(0)),
                             m.Int32Constant(0)),
               &blocka, &blockb);
      m.Bind(&blocka);
      m.Return(m.Int32Constant(constant));
      m.Bind(&blockb);
      m.Return(m.Int32Constant(0 - constant));
      FOR_UINT32_INPUTS(j) {
        int32_t expected = (i + j) == 0 ? constant : 0 - constant;
        CHECK_EQ(expected, m.Call(j));
      }
    }
  }
  {
    FOR_UINT32_INPUTS(i) {
      RawMachineAssemblerTester<int32_t> m(MachineType::Uint32());
      RawMachineLabel blocka, blockb;
      m.Branch(m.Word32NotEqual(m.Int32Add(m.Int32Constant(i), m.Parameter(0)),
                                m.Int32Constant(0)),
               &blocka, &blockb);
      m.Bind(&blocka);
      m.Return(m.Int32Constant(constant));
      m.Bind(&blockb);
      m.Return(m.Int32Constant(0 - constant));
      FOR_UINT32_INPUTS(j) {
        int32_t expected = (i + j) != 0 ? constant : 0 - constant;
        CHECK_EQ(expected, m.Call(j));
      }
    }
  }
  {
    RawMachineAssemblerTester<void> m;
    const Operator* shops[] = {m.machine()->Word32Sar(),
                               m.machine()->Word32Shl(),
                               m.machine()->Word32Shr()};
    for (size_t n = 0; n < arraysize(shops); n++) {
      RawMachineAssemblerTester<int32_t> t(
          MachineType::Uint32(), MachineType::Int32(), MachineType::Uint32());
      RawMachineLabel blocka, blockb;
      t.Branch(t.Word32Equal(t.Int32Add(t.Parameter(0),
                                        t.AddNode(shops[n], t.Parameter(1),
                                                  t.Parameter(2))),
                             t.Int32Constant(0)),
               &blocka, &blockb);
      t.Bind(&blocka);
      t.Return(t.Int32Constant(constant));
      t.Bind(&blockb);
      t.Return(t.Int32Constant(0 - constant));
      FOR_UINT32_INPUTS(i) {
        FOR_INT32_INPUTS(j) {
          FOR_UINT32_SHIFTS(shift) {
            int32_t right;
            switch (shops[n]->opcode()) {
              default:
                UNREACHABLE();
              case IrOpcode::kWord32Sar:
                right = j >> shift;
                break;
              case IrOpcode::kWord32Shl:
                right = static_cast<uint32_t>(j) << shift;
                break;
              case IrOpcode::kWord32Shr:
                right = static_cast<uint32_t>(j) >> shift;
                break;
            }
            int32_t expected = ((i + right) == 0) ? constant : 0 - constant;
            CHECK_EQ(expected, t.Call(i, j, shift));
          }
        }
      }
    }
  }
}


TEST(RunInt32AddInComparison) {
  {
    RawMachineAssemblerTester<int32_t> m;
    Uint32BinopTester bt(&m);
    bt.AddReturn(
        m.Word32Equal(m.Int32Add(bt.param0, bt.param1), m.Int32Constant(0)));
    FOR_UINT32_INPUTS(i) {
      FOR_UINT32_INPUTS(j) {
        uint32_t expected = (i + j) == 0;
        CHECK_EQ(expected, bt.call(i, j));
      }
    }
  }
  {
    RawMachineAssemblerTester<int32_t> m;
    Uint32BinopTester bt(&m);
    bt.AddReturn(
        m.Word32Equal(m.Int32Constant(0), m.Int32Add(bt.param0, bt.param1)));
    FOR_UINT32_INPUTS(i) {
      FOR_UINT32_INPUTS(j) {
        uint32_t expected = (i + j) == 0;
        CHECK_EQ(expected, bt.call(i, j));
      }
    }
  }
  {
    FOR_UINT32_INPUTS(i) {
      RawMachineAssemblerTester<uint32_t> m(MachineType::Uint32());
  m.Return(m.Word32Equal(m.Int32Add(m.Int32Constant(i), m.Parameter(0)),
                         m.Int32Constant(0)));
  FOR_UINT32_INPUTS(j) {
    uint32_t expected = (i + j) == 0;
    CHECK_EQ(expected, m.Call(j));
  }
    }
  }
  {
    FOR_UINT32_INPUTS(i) {
      RawMachineAssemblerTester<uint32_t> m(MachineType::Uint32());
  m.Return(m.Word32Equal(m.Int32Add(m.Parameter(0), m.Int32Constant(i)),
                         m.Int32Constant(0)));
  FOR_UINT32_INPUTS(j) {
    uint32_t expected = (j + i) == 0;
    CHECK_EQ(expected, m.Call(j));
  }
    }
  }
  {
    RawMachineAssemblerTester<void> m;
    const Operator* shops[] = {m.machine()->Word32Sar(),
                               m.machine()->Word32Shl(),
                               m.machine()->Word32Shr()};
    for (size_t n = 0; n < arraysize(shops); n++) {
      RawMachineAssemblerTester<int32_t> t(
          MachineType::Uint32(), MachineType::Int32(), MachineType::Uint32());
      t.Return(t.Word32Equal(
          t.Int32Add(t.Parameter(0),
                     t.AddNode(shops[n], t.Parameter(1), t.Parameter(2))),
          t.Int32Constant(0)));
      FOR_UINT32_INPUTS(i) {
        FOR_INT32_INPUTS(j) {
          FOR_UINT32_SHIFTS(shift) {
            int32_t right;
            switch (shops[n]->opcode()) {
              default:
                UNREACHABLE();
              case IrOpcode::kWord32Sar:
                right = j >> shift;
                break;
              case IrOpcode::kWord32Shl:
                right = static_cast<uint32_t>(j) << shift;
                break;
              case IrOpcode::kWord32Shr:
                right = static_cast<uint32_t>(j) >> shift;
                break;
            }
            int32_t expected = (i + right) == 0;
            CHECK_EQ(expected, t.Call(i, j, shift));
          }
        }
      }
    }
  }
}


TEST(RunInt32SubP) {
  RawMachineAssemblerTester<int32_t> m;
  Uint32BinopTester bt(&m);

  m.Return(m.Int32Sub(bt.param0, bt.param1));

  FOR_UINT32_INPUTS(i) {
    FOR_UINT32_INPUTS(j) {
      uint32_t expected = i - j;
      CHECK_EQ(expected, bt.call(i, j));
    }
  }
}

TEST(RunInt32SubImm) {
  {
    FOR_UINT32_INPUTS(i) {
      RawMachineAssemblerTester<uint32_t> m(MachineType::Uint32());
  m.Return(m.Int32Sub(m.Int32Constant(i), m.Parameter(0)));
  FOR_UINT32_INPUTS(j) {
    uint32_t expected = i - j;
    CHECK_EQ(expected, m.Call(j));
  }
    }
  }
  {
    FOR_UINT32_INPUTS(i) {
      RawMachineAssemblerTester<uint32_t> m(MachineType::Uint32());
      m.Return(m.Int32Sub(m.Parameter(0), m.Int32Constant(i)));
      FOR_UINT32_INPUTS(j) {
        uint32_t expected = j - i;
        CHECK_EQ(expected, m.Call(j));
      }
    }
  }
}

TEST(RunInt32SubImm2) {
  BufferedRawMachineAssemblerTester<int32_t> r;
  r.Return(r.Int32Sub(r.Int32Constant(-1), r.Int32Constant(0)));
  CHECK_EQ(-1, r.Call());
}

TEST(RunInt32SubAndWord32SarP) {
  {
    RawMachineAssemblerTester<int32_t> m(
        MachineType::Uint32(), MachineType::Int32(), MachineType::Uint32());
    m.Return(m.Int32Sub(m.Parameter(0),
                        m.Word32Sar(m.Parameter(1), m.Parameter(2))));
    FOR_UINT32_INPUTS(i) {
      FOR_INT32_INPUTS(j) {
        FOR_UINT32_SHIFTS(shift) {
          int32_t expected = i - (j >> shift);
          CHECK_EQ(expected, m.Call(i, j, shift));
        }
      }
    }
  }
  {
    RawMachineAssemblerTester<int32_t> m(
        MachineType::Int32(), MachineType::Uint32(), MachineType::Uint32());
    m.Return(m.Int32Sub(m.Word32Sar(m.Parameter(0), m.Parameter(1)),
                        m.Parameter(2)));
    FOR_INT32_INPUTS(i) {
      FOR_UINT32_SHIFTS(shift) {
        FOR_UINT32_INPUTS(k) {
          int32_t expected = (i >> shift) - k;
          CHECK_EQ(expected, m.Call(i, shift, k));
        }
      }
    }
  }
}


TEST(RunInt32SubAndWord32ShlP) {
  {
    RawMachineAssemblerTester<int32_t> m(
        MachineType::Uint32(), MachineType::Uint32(), MachineType::Uint32());
    m.Return(m.Int32Sub(m.Parameter(0),
                        m.Word32Shl(m.Parameter(1), m.Parameter(2))));
    FOR_UINT32_INPUTS(i) {
      FOR_UINT32_INPUTS(j) {
        FOR_UINT32_SHIFTS(shift) {
          int32_t expected = i - (j << shift);
          CHECK_EQ(expected, m.Call(i, j, shift));
        }
      }
    }
  }
  {
    RawMachineAssemblerTester<int32_t> m(
        MachineType::Uint32(), MachineType::Uint32(), MachineType::Uint32());
    m.Return(m.Int32Sub(m.Word32Shl(m.Parameter(0), m.Parameter(1)),
                        m.Parameter(2)));
    FOR_UINT32_INPUTS(i) {
      FOR_UINT32_SHIFTS(shift) {
        FOR_UINT32_INPUTS(k) {
          // Use uint32_t because signed overflow is UB in C.
          int32_t expected = (i << shift) - k;
          CHECK_EQ(expected, m.Call(i, shift, k));
        }
      }
    }
  }
}


TEST(RunInt32SubAndWord32ShrP) {
  {
    RawMachineAssemblerTester<uint32_t> m(
        MachineType::Uint32(), MachineType::Uint32(), MachineType::Uint32());
    m.Return(m.Int32Sub(m.Parameter(0),
                        m.Word32Shr(m.Parameter(1), m.Parameter(2))));
    FOR_UINT32_INPUTS(i) {
      FOR_UINT32_INPUTS(j) {
        FOR_UINT32_SHIFTS(shift) {
          // Use uint32_t because signed overflow is UB in C.
          uint32_t expected = i - (j >> shift);
          CHECK_EQ(expected, m.Call(i, j, shift));
        }
      }
    }
  }
  {
    RawMachineAssemblerTester<uint32_t> m(
        MachineType::Uint32(), MachineType::Uint32(), MachineType::Uint32());
    m.Return(m.Int32Sub(m.Word32Shr(m.Parameter(0), m.Parameter(1)),
                        m.Parameter(2)));
    FOR_UINT32_INPUTS(i) {
      FOR_UINT32_SHIFTS(shift) {
        FOR_UINT32_INPUTS(k) {
          // Use uint32_t because signed overflow is UB in C.
          uint32_t expected = (i >> shift) - k;
          CHECK_EQ(expected, m.Call(i, shift, k));
        }
      }
    }
  }
}


TEST(RunInt32SubInBranch) {
  static const int constant = 987654321;
  {
    RawMachineAssemblerTester<int32_t> m;
    Int32BinopTester bt(&m);
    RawMachineLabel blocka, blockb;
    m.Branch(
        m.Word32Equal(m.Int32Sub(bt.param0, bt.param1), m.Int32Constant(0)),
        &blocka, &blockb);
    m.Bind(&blocka);
    bt.AddReturn(m.Int32Constant(constant));
    m.Bind(&blockb);
    bt.AddReturn(m.Int32Constant(0 - constant));
    FOR_UINT32_INPUTS(i) {
      FOR_UINT32_INPUTS(j) {
        int32_t expected = (i - j) == 0 ? constant : 0 - constant;
        CHECK_EQ(expected, bt.call(i, j));
      }
    }
  }
  {
    RawMachineAssemblerTester<int32_t> m;
    Int32BinopTester bt(&m);
    RawMachineLabel blocka, blockb;
    m.Branch(
        m.Word32NotEqual(m.Int32Sub(bt.param0, bt.param1), m.Int32Constant(0)),
        &blocka, &blockb);
    m.Bind(&blocka);
    bt.AddReturn(m.Int32Constant(constant));
    m.Bind(&blockb);
    bt.AddReturn(m.Int32Constant(0 - constant));
    FOR_UINT32_INPUTS(i) {
      FOR_UINT32_INPUTS(j) {
        int32_t expected = (i - j) != 0 ? constant : 0 - constant;
        CHECK_EQ(expected, bt.call(i, j));
      }
    }
  }
  {
    FOR_UINT32_INPUTS(i) {
      RawMachineAssemblerTester<int32_t> m(MachineType::Uint32());
      RawMachineLabel blocka, blockb;
      m.Branch(m.Word32Equal(m.Int32Sub(m.Int32Constant(i), m.Parameter(0)),
                             m.Int32Constant(0)),
               &blocka, &blockb);
      m.Bind(&blocka);
      m.Return(m.Int32Constant(constant));
      m.Bind(&blockb);
      m.Return(m.Int32Constant(0 - constant));
      FOR_UINT32_INPUTS(j) {
        int32_t expected = (i - j) == 0 ? constant : 0 - constant;
        CHECK_EQ(expected, m.Call(j));
      }
    }
  }
  {
    FOR_UINT32_INPUTS(i) {
      RawMachineAssemblerTester<int32_t> m(MachineType::Uint32());
      RawMachineLabel blocka, blockb;
      m.Branch(m.Word32NotEqual(m.Int32Sub(m.Int32Constant(i), m.Parameter(0)),
                                m.Int32Constant(0)),
               &blocka, &blockb);
      m.Bind(&blocka);
      m.Return(m.Int32Constant(constant));
      m.Bind(&blockb);
      m.Return(m.Int32Constant(0 - constant));
      FOR_UINT32_INPUTS(j) {
        int32_t expected = (i - j) != 0 ? constant : 0 - constant;
        CHECK_EQ(expected, m.Call(j));
      }
    }
  }
  {
    RawMachineAssemblerTester<void> m;
    const Operator* shops[] = {m.machine()->Word32Sar(),
                               m.machine()->Word32Shl(),
                               m.machine()->Word32Shr()};
    for (size_t n = 0; n < arraysize(shops); n++) {
      RawMachineAssemblerTester<int32_t> t(
          MachineType::Uint32(), MachineType::Int32(), MachineType::Uint32());
      RawMachineLabel blocka, blockb;
      t.Branch(t.Word32Equal(t.Int32Sub(t.Parameter(0),
                                        t.AddNode(shops[n], t.Parameter(1),
                                                  t.Parameter(2))),
                             t.Int32Constant(0)),
               &blocka, &blockb);
      t.Bind(&blocka);
      t.Return(t.Int32Constant(constant));
      t.Bind(&blockb);
      t.Return(t.Int32Constant(0 - constant));
      FOR_UINT32_INPUTS(i) {
        FOR_INT32_INPUTS(j) {
          FOR_UINT32_SHIFTS(shift) {
            int32_t right;
            switch (shops[n]->opcode()) {
              default:
                UNREACHABLE();
              case IrOpcode::kWord32Sar:
                right = j >> shift;
                break;
              case IrOpcode::kWord32Shl:
                right = static_cast<uint32_t>(j) << shift;
                break;
              case IrOpcode::kWord32Shr:
                right = static_cast<uint32_t>(j) >> shift;
                break;
            }
            int32_t expected = ((i - right) == 0) ? constant : 0 - constant;
            CHECK_EQ(expected, t.Call(i, j, shift));
          }
        }
      }
    }
  }
}


TEST(RunInt32SubInComparison) {
  {
    RawMachineAssemblerTester<int32_t> m;
    Uint32BinopTester bt(&m);
    bt.AddReturn(
        m.Word32Equal(m.Int32Sub(bt.param0, bt.param1), m.Int32Constant(0)));
    FOR_UINT32_INPUTS(i) {
      FOR_UINT32_INPUTS(j) {
        uint32_t expected = (i - j) == 0;
        CHECK_EQ(expected, bt.call(i, j));
      }
    }
  }
  {
    RawMachineAssemblerTester<int32_t> m;
    Uint32BinopTester bt(&m);
    bt.AddReturn(
        m.Word32Equal(m.Int32Constant(0), m.Int32Sub(bt.param0, bt.param1)));
    FOR_UINT32_INPUTS(i) {
      FOR_UINT32_INPUTS(j) {
        uint32_t expected = (i - j) == 0;
        CHECK_EQ(expected, bt.call(i, j));
      }
    }
  }
  {
    FOR_UINT32_INPUTS(i) {
      RawMachineAssemblerTester<uint32_t> m(MachineType::Uint32());
  m.Return(m.Word32Equal(m.Int32Sub(m.Int32Constant(i), m.Parameter(0)),
                         m.Int32Constant(0)));
  FOR_UINT32_INPUTS(j) {
    uint32_t expected = (i - j) == 0;
    CHECK_EQ(expected, m.Call(j));
  }
    }
  }
  {
    FOR_UINT32_INPUTS(i) {
      RawMachineAssemblerTester<uint32_t> m(MachineType::Uint32());
  m.Return(m.Word32Equal(m.Int32Sub(m.Parameter(0), m.Int32Constant(i)),
                         m.Int32Constant(0)));
  FOR_UINT32_INPUTS(j) {
    uint32_t expected = (j - i) == 0;
    CHECK_EQ(expected, m.Call(j));
  }
    }
  }
  {
    RawMachineAssemblerTester<void> m;
    const Operator* shops[] = {m.machine()->Word32Sar(),
                               m.machine()->Word32Shl(),
                               m.machine()->Word32Shr()};
    for (size_t n = 0; n < arraysize(shops); n++) {
      RawMachineAssemblerTester<int32_t> t(
          MachineType::Uint32(), MachineType::Int32(), MachineType::Uint32());
      t.Return(t.Word32Equal(
          t.Int32Sub(t.Parameter(0),
                     t.AddNode(shops[n], t.Parameter(1), t.Parameter(2))),
          t.Int32Constant(0)));
      FOR_UINT32_INPUTS(i) {
        FOR_INT32_INPUTS(j) {
          FOR_UINT32_SHIFTS(shift) {
            int32_t right;
            switch (shops[n]->opcode()) {
              default:
                UNREACHABLE();
              case IrOpcode::kWord32Sar:
                right = j >> shift;
                break;
              case IrOpcode::kWord32Shl:
                right = static_cast<uint32_t>(j) << shift;
                break;
              case IrOpcode::kWord32Shr:
                right = static_cast<uint32_t>(j) >> shift;
                break;
            }
            int32_t expected = (i - right) == 0;
            CHECK_EQ(expected, t.Call(i, j, shift));
          }
        }
      }
    }
  }
}


TEST(RunInt32MulP) {
  {
    RawMachineAssemblerTester<int32_t> m;
    Int32BinopTester bt(&m);
    bt.AddReturn(m.Int32Mul(bt.param0, bt.param1));
    FOR_INT32_INPUTS(i) {
      FOR_INT32_INPUTS(j) {
        int expected = base::MulWithWraparound(i, j);
        CHECK_EQ(expected, bt.call(i, j));
      }
    }
  }
  {
    RawMachineAssemblerTester<int32_t> m;
    Uint32BinopTester bt(&m);
    bt.AddReturn(m.Int32Mul(bt.param0, bt.param1));
    FOR_UINT32_INPUTS(i) {
      FOR_UINT32_INPUTS(j) {
        uint32_t expected = i * j;
        CHECK_EQ(expected, bt.call(i, j));
      }
    }
  }
}


TEST(RunInt32MulHighP) {
  RawMachineAssemblerTester<int32_t> m;
  Int32BinopTester bt(&m);
  bt.AddReturn(m.Int32MulHigh(bt.param0, bt.param1));
  FOR_INT32_INPUTS(i) {
    FOR_INT32_INPUTS(j) {
      int32_t expected = static_cast<int32_t>(
          (static_cast<int64_t>(i) * static_cast<int64_t>(j)) >> 32);
      CHECK_EQ(expected, bt.call(i, j));
    }
  }
}


TEST(RunInt32MulImm) {
  {
    FOR_UINT32_INPUTS(i) {
      RawMachineAssemblerTester<uint32_t> m(MachineType::Uint32());
  m.Return(m.Int32Mul(m.Int32Constant(i), m.Parameter(0)));
  FOR_UINT32_INPUTS(j) {
    uint32_t expected = i * j;
    CHECK_EQ(expected, m.Call(j));
  }
    }
  }
  {
    FOR_UINT32_INPUTS(i) {
      RawMachineAssemblerTester<uint32_t> m(MachineType::Uint32());
      m.Return(m.Int32Mul(m.Parameter(0), m.Int32Constant(i)));
      FOR_UINT32_INPUTS(j) {
        uint32_t expected = j * i;
        CHECK_EQ(expected, m.Call(j));
      }
    }
  }
}

TEST(RunInt32MulAndInt32AddP) {
  {
    FOR_INT32_INPUTS(i) {
      FOR_INT32_INPUTS(j) {
        RawMachineAssemblerTester<int32_t> m(MachineType::Int32());
  int32_t p0 = i;
  int32_t p1 = j;
  m.Return(m.Int32Add(m.Int32Constant(p0),
                      m.Int32Mul(m.Parameter(0), m.Int32Constant(p1))));
  FOR_INT32_INPUTS(k) {
    int32_t p2 = k;
    int expected = base::AddWithWraparound(p0, base::MulWithWraparound(p1, p2));
    CHECK_EQ(expected, m.Call(p2));
  }
      }
    }
  }
  {
    RawMachineAssemblerTester<int32_t> m(
        MachineType::Int32(), MachineType::Int32(), MachineType::Int32());
    m.Return(
        m.Int32Add(m.Parameter(0), m.Int32Mul(m.Parameter(1), m.Parameter(2))));
    FOR_INT32_INPUTS(i) {
      FOR_INT32_INPUTS(j) {
        FOR_INT32_INPUTS(k) {
          int32_t p0 = i;
          int32_t p1 = j;
          int32_t p2 = k;
          int expected =
              base::AddWithWraparound(p0, base::MulWithWraparound(p1, p2));
          CHECK_EQ(expected, m.Call(p0, p1, p2));
        }
      }
    }
  }
  {
    RawMachineAssemblerTester<int32_t> m(
        MachineType::Int32(), MachineType::Int32(), MachineType::Int32());
    m.Return(
        m.Int32Add(m.Int32Mul(m.Parameter(0), m.Parameter(1)), m.Parameter(2)));
    FOR_INT32_INPUTS(i) {
      FOR_INT32_INPUTS(j) {
        FOR_INT32_INPUTS(k) {
          int32_t p0 = i;
          int32_t p1 = j;
          int32_t p2 = k;
          int expected =
              base::AddWithWraparound(base::MulWithWraparound(p0, p1), p2);
          CHECK_EQ(expected, m.Call(p0, p1, p2));
        }
      }
    }
  }
  {
    FOR_INT32_INPUTS(i) {
      RawMachineAssemblerTester<int32_t> m;
      Int32BinopTester bt(&m);
      bt.AddReturn(
          m.Int32Add(m.Int32Constant(i), m.Int32Mul(bt.param0, bt.param1)));
      FOR_INT32_INPUTS(j) {
        FOR_INT32_INPUTS(k) {
          int32_t p0 = j;
          int32_t p1 = k;
          int expected =
              base::AddWithWraparound(i, base::MulWithWraparound(p0, p1));
          CHECK_EQ(expected, bt.call(p0, p1));
        }
      }
    }
  }
}


TEST(RunInt32MulAndInt32SubP) {
  {
    RawMachineAssemblerTester<int32_t> m(
        MachineType::Int32(), MachineType::Int32(), MachineType::Int32());
    m.Return(
        m.Int32Sub(m.Parameter(0), m.Int32Mul(m.Parameter(1), m.Parameter(2))));
    FOR_INT32_INPUTS(i) {
      FOR_INT32_INPUTS(j) {
        FOR_INT32_INPUTS(k) {
          int32_t p0 = i;
          int32_t p1 = j;
          int32_t p2 = k;
          int expected =
              base::SubWithWraparound(p0, base::MulWithWraparound(p1, p2));
          CHECK_EQ(expected, m.Call(p0, p1, p2));
        }
      }
    }
  }
  {
    FOR_INT32_INPUTS(i) {
      RawMachineAssemblerTester<int32_t> m;
      Int32BinopTester bt(&m);
      bt.AddReturn(
          m.Int32Sub(m.Int32Constant(i), m.Int32Mul(bt.param0, bt.param1)));
      FOR_INT32_INPUTS(j) {
        FOR_INT32_INPUTS(k) {
          int32_t p0 = j;
          int32_t p1 = k;
          int expected =
              base::SubWithWraparound(i, base::MulWithWraparound(p0, p1));
          CHECK_EQ(expected, bt.call(p0, p1));
        }
      }
    }
  }
}


TEST(RunUint32MulHighP) {
  RawMachineAssemblerTester<int32_t> m;
  Int32BinopTester bt(&m);
  bt.AddReturn(m.Uint32MulHigh(bt.param0, bt.param1));
  FOR_UINT32_INPUTS(i) {
    FOR_UINT32_INPUTS(j) {
      int32_t expected = base::bit_cast<int32_t>(static_cast<uint32_t>(
          (static_cast<uint64_t>(i) * static_cast<uint64_t>(j)) >> 32));
      CHECK_EQ(expected,
               bt.call(base::bit_cast<int32_t>(i), base::bit_cast<int32_t>(j)));
    }
  }
}


TEST(RunInt32DivP) {
  {
    RawMachineAssemblerTester<int32_t> m;
    Int32BinopTester bt(&m);
    bt.AddReturn(m.Int32Div(bt.param0, bt.param1));
    FOR_INT32_INPUTS(i) {
      FOR_INT32_INPUTS(j) {
        int p0 = i;
        int p1 = j;
        if (p1 != 0 && (static_cast<uint32_t>(p0) != 0x80000000 || p1 != -1)) {
          int expected = static_cast<int32_t>(p0 / p1);
          CHECK_EQ(expected, bt.call(p0, p1));
        }
      }
    }
  }
  {
    RawMachineAssemblerTester<int32_t> m;
    Int32BinopTester bt(&m);
    bt.AddReturn(m.Int32Add(bt.param0, m.Int32Div(bt.param0, bt.param1)));
    FOR_INT32_INPUTS(i) {
      FOR_INT32_INPUTS(j) {
        int p0 = i;
        int p1 = j;
        if (p1 != 0 && (static_cast<uint32_t>(p0) != 0x80000000 || p1 != -1)) {
          int expected =
              static_cast<int32_t>(base::AddWithWraparound(p0, (p0 / p1)));
          CHECK_EQ(expected, bt.call(p0, p1));
        }
      }
    }
  }
}


TEST(RunUint32DivP) {
  {
    RawMachineAssemblerTester<int32_t> m;
    Int32BinopTester bt(&m);
    bt.AddReturn(m.Uint32Div(bt.param0, bt.param1));
    FOR_UINT32_INPUTS(i) {
      FOR_UINT32_INPUTS(j) {
        uint32_t p0 = i;
        uint32_t p1 = j;
        if (p1 != 0) {
          int32_t expected = base::bit_cast<int32_t>(p0 / p1);
          CHECK_EQ(expected, bt.call(p0, p1));
        }
      }
    }
  }
  {
    RawMachineAssemblerTester<int32_t> m;
    Int32BinopTester bt(&m);
    bt.AddReturn(m.Int32Add(bt.param0, m.Uint32Div(bt.param0, bt.param1)));
    FOR_UINT32_INPUTS(i) {
      FOR_UINT32_INPUTS(j) {
        uint32_t p0 = i;
        uint32_t p1 = j;
        if (p1 != 0) {
          int32_t expected = base::bit_cast<int32_t>(p0 + (p0 / p1));
          CHECK_EQ(expected, bt.call(p0, p1));
        }
      }
    }
  }
}


TEST(RunInt32ModP) {
  {
    RawMachineAssemblerTester<int32_t> m;
    Int32BinopTester bt(&m);
    bt.AddReturn(m.Int32Mod(bt.param0, bt.param1));
    FOR_INT32_INPUTS(i) {
      FOR_INT32_INPUTS(j) {
        int p0 = i;
        int p1 = j;
        if (p1 != 0 && (static_cast<uint32_t>(p0) != 0x80000000 || p1 != -1)) {
          int expected = static_cast<int32_t>(p0 % p1);
          CHECK_EQ(expected, bt.call(p0, p1));
        }
      }
    }
  }
  {
    RawMachineAssemblerTester<int32_t> m;
    Int32BinopTester bt(&m);
    bt.AddReturn(m.Int32Add(bt.param0, m.Int32Mod(bt.param0, bt.param1)));
    FOR_INT32_INPUTS(i) {
      FOR_INT32_INPUTS(j) {
        int p0 = i;
        int p1 = j;
        if (p1 != 0 && (static_cast<uint32_t>(p0) != 0x80000000 || p1 != -1)) {
          int expected =
              static_cast<int32_t>(base::AddWithWraparound(p0, (p0 % p1)));
          CHECK_EQ(expected, bt.call(p0, p1));
        }
      }
    }
  }
}


TEST(RunUint32ModP) {
  {
    RawMachineAssemblerTester<int32_t> m;
    Uint32BinopTester bt(&m);
    bt.AddReturn(m.Uint32Mod(bt.param0, bt.param1));
    FOR_UINT32_INPUTS(i) {
      FOR_UINT32_INPUTS(j) {
        uint32_t p0 = i;
        uint32_t p1 = j;
        if (p1 != 0) {
          uint32_t expected = static_cast<uint32_t>(p0 % p1);
          CHECK_EQ(expected, bt.call(p0, p1));
        }
      }
    }
  }
  {
    RawMachineAssemblerTester<int32_t> m;
    Uint32BinopTester bt(&m);
    bt.AddReturn(m.Int32Add(bt.param0, m.Uint32Mod(bt.param0, bt.param1)));
    FOR_UINT32_INPUTS(i) {
      FOR_UINT32_INPUTS(j) {
        uint32_t p0 = i;
        uint32_t p1 = j;
        if (p1 != 0) {
          uint32_t expected = static_cast<uint32_t>(p0 + (p0 % p1));
          CHECK_EQ(expected, bt.call(p0, p1));
        }
      }
    }
  }
}


TEST(RunWord32AndP) {
  {
    RawMachineAssemblerTester<int32_t> m;
    Int32BinopTester bt(&m);
    bt.AddReturn(m.Word32And(bt.param0, bt.param1));
    FOR_UINT32_INPUTS(i) {
      FOR_UINT32_INPUTS(j) {
        int32_t expected = i & j;
        CHECK_EQ(expected, bt.call(i, j));
      }
    }
  }
  {
    RawMachineAssemblerTester<int32_t> m;
    Int32BinopTester bt(&m);
    bt.AddReturn(m.Word32And(bt.param0, m.Word32BitwiseNot(bt.param1)));
    FOR_UINT32_INPUTS(i) {
      FOR_UINT32_INPUTS(j) {
        int32_t expected = i & ~(j);
        CHECK_EQ(expected, bt.call(i, j));
      }
    }
  }
  {
    RawMachineAssemblerTester<int32_t> m;
    Int32BinopTester bt(&m);
    bt.AddReturn(m.Word32And(m.Word32BitwiseNot(bt.param0), bt.param1));
    FOR_UINT32_INPUTS(i) {
      FOR_UINT32_INPUTS(j) {
        int32_t expected = ~(i)&j;
        CHECK_EQ(expected, bt.call(i, j));
      }
    }
  }
}


TEST(RunWord32AndAndWord32ShlP) {
  {
    RawMachineAssemblerTester<int32_t> m;
    Uint32BinopTester bt(&m);
    bt.AddReturn(
        m.Word32Shl(bt.param0, m.Word32And(bt.param1, m.Int32Constant(0x1F))));
    FOR_UINT32_INPUTS(i) {
      FOR_UINT32_INPUTS(j) {
        uint32_t expected = i << (j & 0x1F);
        CHECK_EQ(expected, bt.call(i, j));
      }
    }
  }
  {
    RawMachineAssemblerTester<int32_t> m;
    Uint32BinopTester bt(&m);
    bt.AddReturn(
        m.Word32Shl(bt.param0, m.Word32And(m.Int32Constant(0x1F), bt.param1)));
    FOR_UINT32_INPUTS(i) {
      FOR_UINT32_INPUTS(j) {
        uint32_t expected = i << (0x1F & j);
        CHECK_EQ(expected, bt.call(i, j));
      }
    }
  }
}


TEST(RunWord32AndAndWord32ShrP) {
  {
    RawMachineAssemblerTester<int32_t> m;
    Uint32BinopTester bt(&m);
    bt.AddReturn(
        m.Word32Shr(bt.param0, m.Word32And(bt.param1, m.Int32Constant(0x1F))));
    FOR_UINT32_INPUTS(i) {
      FOR_UINT32_INPUTS(j) {
        uint32_t expected = i >> (j & 0x1F);
        CHECK_EQ(expected, bt.call(i, j));
      }
    }
  }
  {
    RawMachineAssemblerTester<int32_t> m;
    Uint32BinopTester bt(&m);
    bt.AddReturn(
        m.Word32Shr(bt.param0, m.Word32And(m.Int32Constant(0x1F), bt.param1)));
    FOR_UINT32_INPUTS(i) {
      FOR_UINT32_INPUTS(j) {
        uint32_t expected = i >> (0x1F & j);
        CHECK_EQ(expected, bt.call(i, j));
      }
    }
  }
}


TEST(RunWord32AndAndWord32SarP) {
  {
    RawMachineAssemblerTester<int32_t> m;
    Int32BinopTester bt(&m);
    bt.AddReturn(
        m.Word32Sar(bt.param0, m.Word32And(bt.param1, m.Int32Constant(0x1F))));
    FOR_INT32_INPUTS(i) {
      FOR_INT32_INPUTS(j) {
        int32_t expected = i >> (j & 0x1F);
        CHECK_EQ(expected, bt.call(i, j));
      }
    }
  }
  {
    RawMachineAssemblerTester<int32_t> m;
    Int32BinopTester bt(&m);
    bt.AddReturn(
        m.Word32Sar(bt.param0, m.Word32And(m.Int32Constant(0x1F), bt.param1)));
    FOR_INT32_INPUTS(i) {
      FOR_INT32_INPUTS(j) {
        int32_t expected = i >> (0x1F & j);
        CHECK_EQ(expected, bt.call(i, j));
      }
    }
  }
}


TEST(RunWord32AndImm) {
  {
    FOR_UINT32_INPUTS(i) {
      RawMachineAssemblerTester<uint32_t> m(MachineType::Uint32());
  m.Return(m.Word32And(m.Int32Constant(i), m.Parameter(0)));
  FOR_UINT32_INPUTS(j) {
    uint32_t expected = i & j;
    CHECK_EQ(expected, m.Call(j));
  }
    }
  }
  {
    FOR_UINT32_INPUTS(i) {
      RawMachineAssemblerTester<uint32_t> m(MachineType::Uint32());
      m.Return(
          m.Word32And(m.Int32Constant(i), m.Word32BitwiseNot(m.Parameter(0))));
      FOR_UINT32_INPUTS(j) {
        uint32_t expected = i & ~(j);
        CHECK_EQ(expected, m.Call(j));
      }
    }
  }
}


TEST(RunWord32AndInBranch) {
  static const int constant = 987654321;
  {
    RawMachineAssemblerTester<int32_t> m;
    Int32BinopTester bt(&m);
    RawMachineLabel blocka, blockb;
    m.Branch(
        m.Word32Equal(m.Word32And(bt.param0, bt.param1), m.Int32Constant(0)),
        &blocka, &blockb);
    m.Bind(&blocka);
    bt.AddReturn(m.Int32Constant(constant));
    m.Bind(&blockb);
    bt.AddReturn(m.Int32Constant(0 - constant));
    FOR_UINT32_INPUTS(i) {
      FOR_UINT32_INPUTS(j) {
        int32_t expected = (i & j) == 0 ? constant : 0 - constant;
        CHECK_EQ(expected, bt.call(i, j));
      }
    }
  }
  {
    RawMachineAssemblerTester<int32_t> m;
    Int32BinopTester bt(&m);
    RawMachineLabel blocka, blockb;
    m.Branch(
        m.Word32NotEqual(m.Word32And(bt.param0, bt.param1), m.Int32Constant(0)),
        &blocka, &blockb);
    m.Bind(&blocka);
    bt.AddReturn(m.Int32Constant(constant));
    m.Bind(&blockb);
    bt.AddReturn(m.Int32Constant(0 - constant));
    FOR_UINT32_INPUTS(i) {
      FOR_UINT32_INPUTS(j) {
        int32_t expected = (i & j) != 0 ? constant : 0 - constant;
        CHECK_EQ(expected, bt.call(i, j));
      }
    }
  }
  {
    FOR_UINT32_INPUTS(i) {
      RawMachineAssemblerTester<int32_t> m(MachineType::Uint32());
      RawMachineLabel blocka, blockb;
      m.Branch(m.Word32Equal(m.Word32And(m.Int32Constant(i), m.Parameter(0)),
                             m.Int32Constant(0)),
               &blocka, &blockb);
      m.Bind(&blocka);
      m.Return(m.Int32Constant(constant));
      m.Bind(&blockb);
      m.Return(m.Int32Constant(0 - constant));
      FOR_UINT32_INPUTS(j) {
        int32_t expected = (i & j) == 0 ? constant : 0 - constant;
        CHECK_EQ(expected, m.Call(j));
      }
    }
  }
  {
    FOR_UINT32_INPUTS(i) {
      RawMachineAssemblerTester<int32_t> m(MachineType::Uint32());
      RawMachineLabel blocka, blockb;
      m.Branch(m.Word32NotEqual(m.Word32And(m.Int32Constant(i), m.Parameter(0)),
                                m.Int32Constant(0)),
               &blocka, &blockb);
      m.Bind(&blocka);
      m.Return(m.Int32Constant(constant));
      m.Bind(&blockb);
      m.Return(m.Int32Constant(0 - constant));
      FOR_UINT32_INPUTS(j) {
        int32_t expected = (i & j) != 0 ? constant : 0 - constant;
        CHECK_EQ(expected, m.Call(j));
      }
    }
  }
  {
    RawMachineAssemblerTester<void> m;
    const Operator* shops[] = {m.machine()->Word32Sar(),
                               m.machine()->Word32Shl(),
                               m.machine()->Word32Shr()};
    for (size_t n = 0; n < arraysize(shops); n++) {
      RawMachineAssemblerTester<int32_t> t(
          MachineType::Uint32(), MachineType::Int32(), MachineType::Uint32());
      RawMachineLabel blocka, blockb;
      t.Branch(t.Word32Equal(t.Word32And(t.Parameter(0),
                                         t.AddNode(shops[n], t.Parameter(1),
                                                   t.Parameter(2))),
                             t.Int32Constant(0)),
               &blocka, &blockb);
      t.Bind(&blocka);
      t.Return(t.Int32Constant(constant));
      t.Bind(&blockb);
      t.Return(t.Int32Constant(0 - constant));
      FOR_UINT32_INPUTS(i) {
        FOR_INT32_INPUTS(j) {
          FOR_UINT32_SHIFTS(shift) {
            int32_t right;
            switch (shops[n]->opcode()) {
              default:
                UNREACHABLE();
              case IrOpcode::kWord32Sar:
                right = j >> shift;
                break;
              case IrOpcode::kWord32Shl:
                right = static_cast<uint32_t>(j) << shift;
                break;
              case IrOpcode::kWord32Shr:
                right = static_cast<uint32_t>(j) >> shift;
                break;
            }
            int32_t expected = ((i & right) == 0) ? constant : 0 - constant;
            CHECK_EQ(expected, t.Call(i, j, shift));
          }
        }
      }
    }
  }
}


TEST(RunWord32AndInComparison) {
  {
    RawMachineAssemblerTester<int32_t> m;
    Uint32BinopTester bt(&m);
    bt.AddReturn(
        m.Word32Equal(m.Word32And(bt.param0, bt.param1), m.Int32Constant(0)));
    FOR_UINT32_INPUTS(i) {
      FOR_UINT32_INPUTS(j) {
        uint32_t expected = (i & j) == 0;
        CHECK_EQ(expected, bt.call(i, j));
      }
    }
  }
  {
    RawMachineAssemblerTester<int32_t> m;
    Uint32BinopTester bt(&m);
    bt.AddReturn(
        m.Word32Equal(m.Int32Constant(0), m.Word32And(bt.param0, bt.param1)));
    FOR_UINT32_INPUTS(i) {
      FOR_UINT32_INPUTS(j) {
        uint32_t expected = (i & j) == 0;
        CHECK_EQ(expected, bt.call(i, j));
      }
    }
  }
  {
    FOR_UINT32_INPUTS(i) {
      RawMachineAssemblerTester<uint32_t> m(MachineType::Uint32());
  m.Return(m.Word32Equal(m.Word32And(m.Int32Constant(i), m.Parameter(0)),
                         m.Int32Constant(0)));
  FOR_UINT32_INPUTS(j) {
    uint32_t expected = (i & j) == 0;
    CHECK_EQ(expected, m.Call(j));
  }
    }
  }
  {
    FOR_UINT32_INPUTS(i) {
      RawMachineAssemblerTester<uint32_t> m(MachineType::Uint32());
      m.Return(m.Word32Equal(m.Word32And(m.Parameter(0), m.Int32Constant(i)),
                             m.Int32Constant(0)));
      FOR_UINT32_INPUTS(j) {
        uint32_t expected = (j & i) == 0;
        CHECK_EQ(expected, m.Call(j));
      }
    }
  }
}


TEST(RunWord32OrP) {
  {
    RawMachineAssemblerTester<int32_t> m;
    Uint32BinopTester bt(&m);
    bt.AddReturn(m.Word32Or(bt.param0, bt.param1));
    FOR_UINT32_INPUTS(i) {
      FOR_UINT32_INPUTS(j) {
        uint32_t expected = i | j;
        CHECK_EQ(expected, bt.call(i, j));
      }
    }
  }
  {
    RawMachineAssemblerTester<int32_t> m;
    Uint32BinopTester bt(&m);
    bt.AddReturn(m.Word32Or(bt.param0, m.Word32BitwiseNot(bt.param1)));
    FOR_UINT32_INPUTS(i) {
      FOR_UINT32_INPUTS(j) {
        uint32_t expected = i | ~(j);
        CHECK_EQ(expected, bt.call(i, j));
      }
    }
  }
  {
    RawMachineAssemblerTester<int32_t> m;
    Uint32BinopTester bt(&m);
    bt.AddReturn(m.Word32Or(m.Word32BitwiseNot(bt.param0), bt.param1));
    FOR_UINT32_INPUTS(i) {
      FOR_UINT32_INPUTS(j) {
        uint32_t expected = ~(i) | j;
        CHECK_EQ(expected, bt.call(i, j));
      }
    }
  }
}


TEST(RunWord32OrImm) {
  {
    FOR_UINT32_INPUTS(i) {
      RawMachineAssemblerTester<uint32_t> m(MachineType::Uint32());
  m.Return(m.Word32Or(m.Int32Constant(i), m.Parameter(0)));
  FOR_UINT32_INPUTS(j) {
    uint32_t expected = i | j;
    CHECK_EQ(expected, m.Call(j));
  }
    }
  }
  {
    FOR_UINT32_INPUTS(i) {
      RawMachineAssemblerTester<uint32_t> m(MachineType::Uint32());
      m.Return(
          m.Word32Or(m.Int32Constant(i), m.Word32BitwiseNot(m.Parameter(0))));
      FOR_UINT32_INPUTS(j) {
        uint32_t expected = i | ~(j);
        CHECK_EQ(expected, m.Call(j));
      }
    }
  }
}


TEST(RunWord32OrInBranch) {
  static const int constant = 987654321;
  {
    RawMachineAssemblerTester<int32_t> m;
    Int32BinopTester bt(&m);
    RawMachineLabel blocka, blockb;
    m.Branch(
        m.Word32Equal(m.Word32Or(bt.param0, bt.param1), m.Int32Constant(0)),
        &blocka, &blockb);
    m.Bind(&blocka);
    bt.AddReturn(m.Int32Constant(constant));
    m.Bind(&blockb);
    bt.AddReturn(m.Int32Constant(0 - constant));
    FOR_INT32_INPUTS(i) {
      FOR_INT32_INPUTS(j) {
        int32_t expected = (i | j) == 0 ? constant : 0 - constant;
        CHECK_EQ(expected, bt.call(i, j));
      }
    }
  }
  {
    RawMachineAssemblerTester<int32_t> m;
    Int32BinopTester bt(&m);
    RawMachineLabel blocka, blockb;
    m.Branch(
        m.Word32NotEqual(m.Word32Or(bt.param0, bt.param1), m.Int32Constant(0)),
        &blocka, &blockb);
    m.Bind(&blocka);
    bt.AddReturn(m.Int32Constant(constant));
    m.Bind(&blockb);
    bt.AddReturn(m.Int32Constant(0 - constant));
    FOR_INT32_INPUTS(i) {
      FOR_INT32_INPUTS(j) {
        int32_t expected = (i | j) != 0 ? constant : 0 - constant;
        CHECK_EQ(expected, bt.call(i, j));
      }
    }
  }
  {
    FOR_INT32_INPUTS(i) {
      RawMachineAssemblerTester<int32_t> m(MachineType::Int32());
      RawMachineLabel blocka, blockb;
      m.Branch(m.Word32Equal(m.Word32Or(m.Int32Constant(i), m.Parameter(0)),
                             m.Int32Constant(0)),
               &blocka, &blockb);
      m.Bind(&blocka);
      m.Return(m.Int32Constant(constant));
      m.Bind(&blockb);
      m.Return(m.Int32Constant(0 - constant));
      FOR_INT32_INPUTS(j) {
        int32_t expected = (i | j) == 0 ? constant : 0 - constant;
        CHECK_EQ(expected, m.Call(j));
      }
    }
  }
  {
    FOR_INT32_INPUTS(i) {
      RawMachineAssemblerTester<int32_t> m(MachineType::Int32());
      RawMachineLabel blocka, blockb;
      m.Branch(m.Word32NotEqual(m.Word32Or(m.Int32Constant(i), m.Parameter(0)),
                                m.Int32Constant(0)),
               &blocka, &blockb);
      m.Bind(&blocka);
      m.Return(m.Int32Constant(constant));
      m.Bind(&blockb);
      m.Return(m.Int32Constant(0 - constant));
      FOR_INT32_INPUTS(j) {
        int32_t expected = (i | j) != 0 ? constant : 0 - constant;
        CHECK_EQ(expected, m.Call(j));
      }
    }
  }
  {
    RawMachineAssemblerTester<void> m;
    const Operator* shops[] = {m.machine()->Word32Sar(),
                               m.machine()->Word32Shl(),
                               m.machine()->Word32Shr()};
    for (size_t n = 0; n < arraysize(shops); n++) {
      RawMachineAssemblerTester<int32_t> t(
          MachineType::Uint32(), MachineType::Int32(), MachineType::Uint32());
      RawMachineLabel blocka, blockb;
      t.Branch(t.Word32Equal(t.Word32Or(t.Parameter(0),
                                        t.AddNode(shops[n], t.Parameter(1),
                                                  t.Parameter(2))),
                             t.Int32Constant(0)),
               &blocka, &blockb);
      t.Bind(&blocka);
      t.Return(t.Int32Constant(constant));
      t.Bind(&blockb);
      t.Return(t.Int32Constant(0 - constant));
      FOR_UINT32_INPUTS(i) {
        FOR_INT32_INPUTS(j) {
          FOR_UINT32_SHIFTS(shift) {
            int32_t right;
            switch (shops[n]->opcode()) {
              default:
                UNREACHABLE();
              case IrOpcode::kWord32Sar:
                right = j >> shift;
                break;
              case IrOpcode::kWord32Shl:
                right = static_cast<uint32_t>(j) << shift;
                break;
              case IrOpcode::kWord32Shr:
                right = static_cast<uint32_t>(j) >> shift;
                break;
            }
            int32_t expected = ((i | right) == 0) ? constant : 0 - constant;
            CHECK_EQ(expected, t.Call(i, j, shift));
          }
        }
      }
    }
  }
}


TEST(RunWord32OrInComparison) {
  {
    RawMachineAssemblerTester<int32_t> m;
    Int32BinopTester bt(&m);
    bt.AddReturn(
        m.Word32Equal(m.Word32Or(bt.param0, bt.param1), m.Int32Constant(0)));
    FOR_UINT32_INPUTS(i) {
      FOR_UINT32_INPUTS(j) {
        int32_t expected = (i | j) == 0;
        CHECK_EQ(expected, bt.call(i, j));
      }
    }
  }
  {
    RawMachineAssemblerTester<int32_t> m;
    Int32BinopTester bt(&m);
    bt.AddReturn(
        m.Word32Equal(m.Int32Constant(0), m.Word32Or(bt.param0, bt.param1)));
    FOR_UINT32_INPUTS(i) {
      FOR_UINT32_INPUTS(j) {
        int32_t expected = (i | j) == 0;
        CHECK_EQ(expected, bt.call(i, j));
      }
    }
  }
  {
    FOR_UINT32_INPUTS(i) {
      RawMachineAssemblerTester<uint32_t> m(MachineType::Uint32());
  m.Return(m.Word32Equal(m.Word32Or(m.Int32Constant(i), m.Parameter(0)),
                         m.Int32Constant(0)));
  FOR_UINT32_INPUTS(j) {
    uint32_t expected = (i | j) == 0;
    CHECK_EQ(expected, m.Call(j));
  }
    }
  }
  {
    FOR_UINT32_INPUTS(i) {
      RawMachineAssemblerTester<uint32_t> m(MachineType::Uint32());
      m.Return(m.Word32Equal(m.Word32Or(m.Parameter(0), m.Int32Constant(i)),
                             m.Int32Constant(0)));
      FOR_UINT32_INPUTS(j) {
        uint32_t expected = (j | i) == 0;
        CHECK_EQ(expected, m.Call(j));
      }
    }
  }
}


TEST(RunWord32XorP) {
  {
    FOR_UINT32_INPUTS(i) {
      RawMachineAssemblerTester<uint32_t> m(MachineType::Uint32());
  m.Return(m.Word32Xor(m.Int32Constant(i), m.Parameter(0)));
  FOR_UINT32_INPUTS(j) {
    uint32_t expected = i ^ j;
    CHECK_EQ(expected, m.Call(j));
  }
    }
  }
  {
    RawMachineAssemblerTester<int32_t> m;
    Uint32BinopTester bt(&m);
    bt.AddReturn(m.Word32Xor(bt.param0, bt.param1));
    FOR_UINT32_INPUTS(i) {
      FOR_UINT32_INPUTS(j) {
        uint32_t expected = i ^ j;
        CHECK_EQ(expected, bt.call(i, j));
      }
    }
  }
  {
    RawMachineAssemblerTester<int32_t> m;
    Int32BinopTester bt(&m);
    bt.AddReturn(m.Word32Xor(bt.param0, m.Word32BitwiseNot(bt.param1)));
    FOR_INT32_INPUTS(i) {
      FOR_INT32_INPUTS(j) {
        int32_t expected = i ^ ~(j);
        CHECK_EQ(expected, bt.call(i, j));
      }
    }
  }
  {
    RawMachineAssemblerTester<int32_t> m;
    Int32BinopTester bt(&m);
    bt.AddReturn(m.Word32Xor(m.Word32BitwiseNot(bt.param0), bt.param1));
    FOR_INT32_INPUTS(i) {
      FOR_INT32_INPUTS(j) {
        int32_t expected = ~(i) ^ j;
        CHECK_EQ(expected, bt.call(i, j));
      }
    }
  }
  {
    FOR_UINT32_INPUTS(i) {
      RawMachineAssemblerTester<uint32_t> m(MachineType::Uint32());
      m.Return(
          m.Word32Xor(m.Int32Constant(i), m.Word32BitwiseNot(m.Parameter(0))));
      FOR_UINT32_INPUTS(j) {
        uint32_t expected = i ^ ~(j);
        CHECK_EQ(expected, m.Call(j));
      }
    }
  }
}


TEST(RunWord32XorInBranch) {
  static const uint32_t constant = 987654321;
  {
    RawMachineAssemblerTester<int32_t> m;
    Uint32BinopTester bt(&m);
    RawMachineLabel blocka, blockb;
    m.Branch(
        m.Word32Equal(m.Word32Xor(bt.param0, bt.param1), m.Int32Constant(0)),
        &blocka, &blockb);
    m.Bind(&blocka);
    bt.AddReturn(m.Int32Constant(constant));
    m.Bind(&blockb);
    bt.AddReturn(m.Int32Constant(0 - constant));
    FOR_UINT32_INPUTS(i) {
      FOR_UINT32_INPUTS(j) {
        uint32_t expected = (i ^ j) == 0 ? constant : 0 - constant;
        CHECK_EQ(expected, bt.call(i, j));
      }
    }
  }
  {
    RawMachineAssemblerTester<int32_t> m;
    Uint32BinopTester bt(&m);
    RawMachineLabel blocka, blockb;
    m.Branch(
        m.Word32NotEqual(m.Word32Xor(bt.param0, bt.param1), m.Int32Constant(0)),
        &blocka, &blockb);
    m.Bind(&blocka);
    bt.AddReturn(m.Int32Constant(constant));
    m.Bind(&blockb);
    bt.AddReturn(m.Int32Constant(0 - constant));
    FOR_UINT32_INPUTS(i) {
      FOR_UINT32_INPUTS(j) {
        uint32_t expected = (i ^ j) != 0 ? constant : 0 - constant;
        CHECK_EQ(expected, bt.call(i, j));
      }
    }
  }
  {
    FOR_UINT32_INPUTS(i) {
      RawMachineAssemblerTester<int32_t> m(MachineType::Uint32());
      RawMachineLabel blocka, blockb;
      m.Branch(m.Word32Equal(m.Word32Xor(m.Int32Constant(i), m.Parameter(0)),
                             m.Int32Constant(0)),
               &blocka, &blockb);
      m.Bind(&blocka);
      m.Return(m.Int32Constant(constant));
      m.Bind(&blockb);
      m.Return(m.Int32Constant(0 - constant));
      FOR_UINT32_INPUTS(j) {
        int32_t expected = (i ^ j) == 0 ? constant : 0 - constant;
        CHECK_EQ(expected, m.Call(j));
      }
    }
  }
  {
    FOR_UINT32_INPUTS(i) {
      RawMachineAssemblerTester<int32_t> m(MachineType::Uint32());
      RawMachineLabel blocka, blockb;
      m.Branch(m.Word32NotEqual(m.Word32Xor(m.Int32Constant(i), m.Parameter(0)),
                                m.Int32Constant(0)),
               &blocka, &blockb);
      m.Bind(&blocka);
      m.Return(m.Int32Constant(constant));
      m.Bind(&blockb);
      m.Return(m.Int32Constant(0 - constant));
      FOR_UINT32_INPUTS(j) {
        int32_t expected = (i ^ j) != 0 ? constant : 0 - constant;
        CHECK_EQ(expected, m.Call(j));
      }
    }
  }
  {
    RawMachineAssemblerTester<void> m;
    const Operator* shops[] = {m.machine()->Word32Sar(),
                               m.machine()->Word32Shl(),
                               m.machine()->Word32Shr()};
    for (size_t n = 0; n < arraysize(shops); n++) {
      RawMachineAssemblerTester<int32_t> t(
          MachineType::Uint32(), MachineType::Int32(), MachineType::Uint32());
      RawMachineLabel blocka, blockb;
      t.Branch(t.Word32Equal(t.Word32Xor(t.Parameter(0),
                                         t.AddNode(shops[n], t.Parameter(1),
                                                   t.Parameter(2))),
                             t.Int32Constant(0)),
               &blocka, &blockb);
      t.Bind(&blocka);
      t.Return(t.Int32Constant(constant));
      t.Bind(&blockb);
      t.Return(t.Int32Constant(0 - constant));
      FOR_UINT32_INPUTS(i) {
        FOR_INT32_INPUTS(j) {
          FOR_UINT32_SHIFTS(shift) {
            int32_t right;
            switch (shops[n]->opcode()) {
              default:
                UNREACHABLE();
              case IrOpcode::kWord32Sar:
                right = j >> shift;
                break;
              case IrOpcode::kWord32Shl:
                right = static_cast<uint32_t>(j) << shift;
                break;
              case IrOpcode::kWord32Shr:
                right = static_cast<uint32_t>(j) >> shift;
                break;
            }
            int32_t expected = ((i ^ right) == 0) ? constant : 0 - constant;
            CHECK_EQ(expected, t.Call(i, j, shift));
          }
        }
      }
    }
  }
}


TEST(RunWord32ShlP) {
  {
    FOR_UINT32_SHIFTS(shift) {
      RawMachineAssemblerTester<uint32_t> m(MachineType::Uint32());
      m.Return(m.Word32Shl(m.Parameter(0), m.Int32Constant(shift)));
      FOR_UINT32_INPUTS(j) {
        uint32_t expected = j << shift;
        CHECK_EQ(expected, m.Call(j));
      }
    }
  }
  {
    RawMachineAssemblerTester<int32_t> m;
    Uint32BinopTester bt(&m);
    bt.AddReturn(m.Word32Shl(bt.param0, bt.param1));
    FOR_UINT32_INPUTS(i) {
      FOR_UINT32_SHIFTS(shift) {
        uint32_t expected = i << shift;
        CHECK_EQ(expected, bt.call(i, shift));
      }
    }
  }
}


TEST(RunWord32ShlInComparison) {
  {
    RawMachineAssemblerTester<int32_t> m;
    Uint32BinopTester bt(&m);
    bt.AddReturn(
        m.Word32Equal(m.Word32Shl(bt.param0, bt.param1), m.Int32Constant(0)));
    FOR_UINT32_INPUTS(i) {
      FOR_UINT32_SHIFTS(shift) {
        uint32_t expected = 0 == (i << shift);
        CHECK_EQ(expected, bt.call(i, shift));
      }
    }
  }
  {
    RawMachineAssemblerTester<int32_t> m;
    Uint32BinopTester bt(&m);
    bt.AddReturn(
        m.Word32Equal(m.Int32Constant(0), m.Word32Shl(bt.param0, bt.param1)));
    FOR_UINT32_INPUTS(i) {
      FOR_UINT32_SHIFTS(shift) {
        uint32_t expected = 0 == (i << shift);
        CHECK_EQ(expected, bt.call(i, shift));
      }
    }
  }
  {
    FOR_UINT32_SHIFTS(shift) {
      RawMachineAssemblerTester<uint32_t> m(MachineType::Uint32());
      m.Return(
          m.Word32Equal(m.Int32Constant(0),
                        m.Word32Shl(m.Parameter(0), m.Int32Constant(shift))));
      FOR_UINT32_INPUTS(i) {
        uint32_t expected = 0 == (i << shift);
        CHECK_EQ(expected, m.Call(i));
      }
    }
  }
  {
    FOR_UINT32_SHIFTS(shift) {
      RawMachineAssemblerTester<uint32_t> m(MachineType::Uint32());
      m.Return(
          m.Word32Equal(m.Word32Shl(m.Parameter(0), m.Int32Constant(shift)),
                        m.Int32Constant(0)));
      FOR_UINT32_INPUTS(i) {
        uint32_t expected = 0 == (i << shift);
        CHECK_EQ(expected, m.Call(i));
      }
    }
  }
}


TEST(RunWord32ShrP) {
  {
    FOR_UINT32_SHIFTS(shift) {
      RawMachineAssemblerTester<uint32_t> m(MachineType::Uint32());
      m.Return(m.Word32Shr(m.Parameter(0), m.Int32Constant(shift)));
      FOR_UINT32_INPUTS(j) {
        uint32_t expected = j >> shift;
        CHECK_EQ(expected, m.Call(j));
      }
    }
  }
  {
    RawMachineAssemblerTester<int32_t> m;
    Uint32BinopTester bt(&m);
    bt.AddReturn(m.Word32Shr(bt.param0, bt.param1));
    FOR_UINT32_INPUTS(i) {
      FOR_UINT32_SHIFTS(shift) {
        uint32_t expected = i >> shift;
        CHECK_EQ(expected, bt.call(i, shift));
      }
    }
    CHECK_EQ(0x00010000u, bt.call(0x80000000, 15));
  }
}

TEST(RunWordShiftInBranch) {
  static const uint32_t constant = 987654321;
  FOR_UINT32_SHIFTS(shift) {
    RawMachineAssemblerTester<int32_t> m(MachineType::Uint32());
    RawMachineLabel blocka, blockb;
    m.Branch(m.Word32Equal(m.Word32Shl(m.Parameter(0), m.Int32Constant(shift)),
                           m.Int32Constant(0)),
             &blocka, &blockb);
    m.Bind(&blocka);
    m.Return(m.Int32Constant(constant));
    m.Bind(&blockb);
    m.Return(m.Int32Constant(0 - constant));
    FOR_UINT32_INPUTS(i) {
      int32_t expected = ((i << shift) == 0) ? constant : 0 - constant;
      CHECK_EQ(expected, m.Call(i));
    }
  }
  FOR_UINT32_SHIFTS(shift) {
    RawMachineAssemblerTester<int32_t> m(MachineType::Uint32());
    RawMachineLabel blocka, blockb;
    m.Branch(m.Word32Equal(m.Word32Shr(m.Parameter(0), m.Int32Constant(shift)),
                           m.Int32Constant(0)),
             &blocka, &blockb);
    m.Bind(&blocka);
    m.Return(m.Int32Constant(constant));
    m.Bind(&blockb);
    m.Return(m.Int32Constant(0 - constant));
    FOR_UINT32_INPUTS(i) {
      int32_t expected = ((i >> shift) == 0) ? constant : 0 - constant;
      CHECK_EQ(expected, m.Call(i));
    }
  }
  FOR_UINT32_SHIFTS(shift) {
    RawMachineAssemblerTester<int32_t> m(MachineType::Int32());
    RawMachineLabel blocka, blockb;
    m.Branch(m.Word32Equal(m.Word32Sar(m.Parameter(0), m.Int32Constant(shift)),
                           m.Int32Constant(0)),
             &blocka, &blockb);
    m.Bind(&blocka);
    m.Return(m.Int32Constant(constant));
    m.Bind(&blockb);
    m.Return(m.Int32Constant(0 - constant));
    FOR_INT32_INPUTS(i) {
      int32_t expected = ((i >> shift) == 0) ? constant : 0 - constant;
      CHECK_EQ(expected, m.Call(i));
    }
  }
}

TEST(RunWord32ShrInComparison) {
  {
    RawMachineAssemblerTester<int32_t> m;
    Uint32BinopTester bt(&m);
    bt.AddReturn(
        m.Word32Equal(m.Word32Shr(bt.param0, bt.param1), m.Int32Constant(0)));
    FOR_UINT32_INPUTS(i) {
      FOR_UINT32_SHIFTS(shift) {
        uint32_t expected = 0 == (i >> shift);
        CHECK_EQ(expected, bt.call(i, shift));
      }
    }
  }
  {
    RawMachineAssemblerTester<int32_t> m;
    Uint32BinopTester bt(&m);
    bt.AddReturn(
        m.Word32Equal(m.Int32Constant(0), m.Word32Shr(bt.param0, bt.param1)));
    FOR_UINT32_INPUTS(i) {
      FOR_UINT32_SHIFTS(shift) {
        uint32_t expected = 0 == (i >> shift);
        CHECK_EQ(expected, bt.call(i, shift));
      }
    }
  }
  {
    FOR_UINT32_SHIFTS(shift) {
      RawMachineAssemblerTester<uint32_t> m(MachineType::Uint32());
      m.Return(
          m.Word32Equal(m.Int32Constant(0),
                        m.Word32Shr(m.Parameter(0), m.Int32Constant(shift))));
      FOR_UINT32_INPUTS(i) {
        uint32_t expected = 0 == (i >> shift);
        CHECK_EQ(expected, m.Call(i));
      }
    }
  }
  {
    FOR_UINT32_SHIFTS(shift) {
      RawMachineAssemblerTester<uint32_t> m(MachineType::Uint32());
      m.Return(
          m.Word32Equal(m.Word32Shr(m.Parameter(0), m.Int32Constant(shift)),
                        m.Int32Constant(0)));
      FOR_UINT32_INPUTS(i) {
        uint32_t expected = 0 == (i >> shift);
        CHECK_EQ(expected, m.Call(i));
      }
    }
  }
}


TEST(RunWord32SarP) {
  {
    FOR_INT32_SHIFTS(shift) {
      RawMachineAssemblerTester<int32_t> m(MachineType::Int32());
      m.Return(m.Word32Sar(m.Parameter(0), m.Int32Constant(shift)));
      FOR_INT32_INPUTS(j) {
        int32_t expected = j >> shift;
        CHECK_EQ(expected, m.Call(j));
      }
    }
  }
  {
    RawMachineAssemblerTester<int32_t> m;
    Int32BinopTester bt(&m);
    bt.AddReturn(m.Word32Sar(bt.param0, bt.param1));
    FOR_INT32_INPUTS(i) {
      FOR_INT32_SHIFTS(shift) {
        int32_t expected = i >> shift;
        CHECK_EQ(expected, bt.call(i, shift));
      }
    }
    CHECK_EQ(base::bit_cast<int32_t>(0xFFFF0000), bt.call(0x80000000, 15));
  }
}


TEST(RunWord32SarInComparison) {
  {
    RawMachineAssemblerTester<int32_t> m;
    Int32BinopTester bt(&m);
    bt.AddReturn(
        m.Word32Equal(m.Word32Sar(bt.param0, bt.param1), m.Int32Constant(0)));
    FOR_INT32_INPUTS(i) {
      FOR_INT32_SHIFTS(shift) {
        int32_t expected = 0 == (i >> shift);
        CHECK_EQ(expected, bt.call(i, shift));
      }
    }
  }
  {
    RawMachineAssemblerTester<int32_t> m;
    Int32BinopTester bt(&m);
    bt.AddReturn(
        m.Word32Equal(m.Int32Constant(0), m.Word32Sar(bt.param0, bt.param1)));
    FOR_INT32_INPUTS(i) {
      FOR_INT32_SHIFTS(shift) {
        int32_t expected = 0 == (i >> shift);
        CHECK_EQ(expected, bt.call(i, shift));
      }
    }
  }
  {
    FOR_INT32_SHIFTS(shift) {
      RawMachineAssemblerTester<int32_t> m(MachineType::Int32());
      m.Return(
          m.Word32Equal(m.Int32Constant(0),
                        m.Word32Sar(m.Parameter(0), m.Int32Constant(shift))));
      FOR_INT32_INPUTS(i) {
        int32_t expected = 0 == (i >> shift);
        CHECK_EQ(expected, m.Call(i));
      }
    }
  }
  {
    FOR_INT32_SHIFTS(shift) {
      RawMachineAssemblerTester<int32_t> m(MachineType::Int32());
      m.Return(
          m.Word32Equal(m.Word32Sar(m.Parameter(0), m.Int32Constant(shift)),
                        m.Int32Constant(0)));
      FOR_INT32_INPUTS(i) {
        int32_t expected = 0 == (i >> shift);
        CHECK_EQ(expected, m.Call(i));
      }
    }
  }
}


TEST(RunWord32RorP) {
  {
    FOR_UINT32_SHIFTS(shift) {
      RawMachineAssemblerTester<int32_t> m(MachineType::Uint32());
      m.Return(m.Word32Ror(m.Parameter(0), m.Int32Constant(shift)));
      FOR_UINT32_INPUTS(j) {
        int32_t expected = base::bits::RotateRight32(j, shift);
        CHECK_EQ(expected, m.Call(j));
      }
    }
  }
  {
    RawMachineAssemblerTester<int32_t> m;
    Uint32BinopTester bt(&m);
    bt.AddReturn(m.Word32Ror(bt.param0, bt.param1));
    FOR_UINT32_INPUTS(i) {
      FOR_UINT32_SHIFTS(shift) {
        uint32_t expected = base::bits::RotateRight32(i, shift);
        CHECK_EQ(expected, bt.call(i, shift));
      }
    }
  }
}


TEST(RunWord32RorInComparison) {
  {
    RawMachineAssemblerTester<int32_t> m;
    Uint32BinopTester bt(&m);
    bt.AddReturn(
        m.Word32Equal(m.Word32Ror(bt.param0, bt.param1), m.Int32Constant(0)));
    FOR_UINT32_INPUTS(i) {
      FOR_UINT32_SHIFTS(shift) {
        uint32_t expected = 0 == base::bits::RotateRight32(i, shift);
        CHECK_EQ(expected, bt.call(i, shift));
      }
    }
  }
  {
    RawMachineAssemblerTester<int32_t> m;
    Uint32BinopTester bt(&m);
    bt.AddReturn(
        m.Word32Equal(m.Int32Constant(0), m.Word32Ror(bt.param0, bt.param1)));
    FOR_UINT32_INPUTS(i) {
      FOR_UINT32_SHIFTS(shift) {
        uint32_t expected = 0 == base::bits::RotateRight32(i, shift);
        CHECK_EQ(expected, bt.call(i, shift));
      }
    }
  }
  {
    FOR_UINT32_SHIFTS(shift) {
      RawMachineAssemblerTester<uint32_t> m(MachineType::Uint32());
      m.Return(
          m.Word32Equal(m.Int32Constant(0),
                        m.Word32Ror(m.Parameter(0), m.Int32Constant(shift))));
      FOR_UINT32_INPUTS(i) {
        uint32_t expected = 0 == base::bits::RotateRight32(i, shift);
        CHECK_EQ(expected, m.Call(i));
      }
    }
  }
  {
    FOR_UINT32_SHIFTS(shift) {
      RawMachineAssemblerTester<uint32_t> m(MachineType::Uint32());
      m.Return(
          m.Word32Equal(m.Word32Ror(m.Parameter(0), m.Int32Constant(shift)),
                        m.Int32Constant(0)));
      FOR_UINT32_INPUTS(i) {
        uint32_t expected = 0 == base::bits::RotateRight32(i, shift);
        CHECK_EQ(expected, m.Call(i));
      }
    }
  }
}

TEST(RunWord32BitwiseNotP) {
  RawMachineAssemblerTester<int32_t> m(MachineType::Int32());
  m.Return(m.Word32BitwiseNot(m.Parameter(0)));
  FOR_INT32_INPUTS(i) {
    int expected = ~(i);
    CHECK_EQ(expected, m.Call(i));
  }
}


TEST(RunInt32NegP) {
  RawMachineAssemblerTester<int32_t> m(MachineType::Int32());
  m.Return(m.Int32Neg(m.Parameter(0)));
  FOR_INT32_INPUTS(i) {
    int expected = base::NegateWithWraparound(i);
    CHECK_EQ(expected, m.Call(i));
  }
}


TEST(RunWord32EqualAndWord32SarP) {
  {
    RawMachineAssemblerTester<int32_t> m(
        MachineType::Int32(), MachineType::Int32(), MachineType::Uint32());
    m.Return(m.Word32Equal(m.Parameter(0),
                           m.Word32Sar(m.Parameter(1), m.Parameter(2))));
    FOR_INT32_INPUTS(i) {
      FOR_INT32_INPUTS(j) {
        FOR_UINT32_SHIFTS(shift) {
          int32_t expected = (i == (j >> shift));
          CHECK_EQ(expected, m.Call(i, j, shift));
        }
      }
    }
  }
  {
    RawMachineAssemblerTester<int32_t> m(
        MachineType::Int32(), MachineType::Uint32(), MachineType::Int32());
    m.Return(m.Word32Equal(m.Word32Sar(m.Parameter(0), m.Parameter(1)),
                           m.Parameter(2)));
    FOR_INT32_INPUTS(i) {
      FOR_UINT32_SHIFTS(shift) {
        FOR_INT32_INPUTS(k) {
          int32_t expected = ((i >> shift) == k);
          CHECK_EQ(expected, m.Call(i, shift, k));
        }
      }
    }
  }
}


TEST(RunWord32EqualAndWord32ShlP) {
  {
    RawMachineAssemblerTester<int32_t> m(
        MachineType::Uint32(), MachineType::Uint32(), MachineType::Uint32());
    m.Return(m.Word32Equal(m.Parameter(0),
                           m.Word32Shl(m.Parameter(1), m.Parameter(2))));
    FOR_UINT32_INPUTS(i) {
      FOR_UINT32_INPUTS(j) {
        FOR_UINT32_SHIFTS(shift) {
          int32_t expected = (i == (j << shift));
          CHECK_EQ(expected, m.Call(i, j, shift));
        }
      }
    }
  }
  {
    RawMachineAssemblerTester<int32_t> m(
        MachineType::Uint32(), MachineType::Uint32(), MachineType::Uint32());
    m.Return(m.Word32Equal(m.Word32Shl(m.Parameter(0), m.Parameter(1)),
                           m.Parameter(2)));
    FOR_UINT32_INPUTS(i) {
      FOR_UINT32_SHIFTS(shift) {
        FOR_UINT32_INPUTS(k) {
          int32_t expected = ((i << shift) == k);
          CHECK_EQ(expected, m.Call(i, shift, k));
        }
      }
    }
  }
}


TEST(RunWord32EqualAndWord32ShrP) {
  {
    RawMachineAssemblerTester<int32_t> m(
        MachineType::Uint32(), MachineType::Uint32(), MachineType::Uint32());
    m.Return(m.Word32Equal(m.Parameter(0),
                           m.Word32Shr(m.Parameter(1), m.Parameter(2))));
    FOR_UINT32_INPUTS(i) {
      FOR_UINT32_INPUTS(j) {
        FOR_UINT32_SHIFTS(shift) {
          int32_t expected = (i == (j >> shift));
          CHECK_EQ(expected, m.Call(i, j, shift));
        }
      }
    }
  }
  {
    RawMachineAssemblerTester<int32_t> m(
        MachineType::Uint32(), MachineType::Uint32(), MachineType::Uint32());
    m.Return(m.Word32Equal(m.Word32Shr(m.Parameter(0), m.Parameter(1)),
                           m.Parameter(2)));
    FOR_UINT32_INPUTS(i) {
      FOR_UINT32_SHIFTS(shift) {
        FOR_UINT32_INPUTS(k) {
          int32_t expected = ((i >> shift) == k);
          CHECK_EQ(expected, m.Call(i, shift, k));
        }
      }
    }
  }
}


TEST(RunDeadNodes) {
  for (int i = 0; true; i++) {
    RawMachineAssemblerTester<int32_t> m_v;
    RawMachineAssemblerTester<int32_t> m_i(MachineType::Int32());
    RawMachineAssemblerTester<int32_t>& m = i == 5 ? m_i : m_v;

    int constant = 0x55 + i;
    switch (i) {
      case 0:
        m.Int32Constant(44);
        break;
      case 1:
        m.StringConstant("unused");
        break;
      case 2:
        m.NumberConstant(11.1);
        break;
      case 3:
        m.PointerConstant(&constant);
        break;
      case 4:
        m.LoadFromPointer(&constant, MachineType::Int32());
        break;
      case 5:
        m.Parameter(0);
        break;
      default:
        return;
    }
    m.Return(m.Int32Constant(constant));
    if (i != 5) {
      CHECK_EQ(constant, m.Call());
    } else {
      CHECK_EQ(constant, m.Call(0));
    }
  }
}


TEST(RunDeadInt32Binops) {
  RawMachineAssemblerTester<int32_t> m;

  const Operator* kOps[] = {
      m.machine()->Word32And(),            m.machine()->Word32Or(),
      m.machine()->Word32Xor(),            m.machine()->Word32Shl(),
      m.machine()->Word32Shr(),            m.machine()->Word32Sar(),
      m.machine()->Word32Ror(),            m.machine()->Word32Equal(),
      m.machine()->Int32Add(),             m.machine()->Int32Sub(),
      m.machine()->Int32Mul(),             m.machine()->Int32MulHigh(),
      m.machine()->Int32Div(),             m.machine()->Uint32Div(),
      m.machine()->Int32Mod(),             m.machine()->Uint32Mod(),
      m.machine()->Uint32MulHigh(),        m.machine()->Int32LessThan(),
      m.machine()->Int32LessThanOrEqual(), m.machine()->Uint32LessThan(),
      m.machine()->Uint32LessThanOrEqual()};

  for (size_t i = 0; i < arraysize(kOps); ++i) {
    RawMachineAssemblerTester<int32_t> t(MachineType::Int32(),
                                         MachineType::Int32());
    int32_t constant = static_cast<int32_t>(0x55555 + i);
    t.AddNode(kOps[i], t.Parameter(0), t.Parameter(1));
    t.Return(t.Int32Constant(constant));

    CHECK_EQ(constant, t.Call(1, 1));
  }
}


TEST(RunFloat32Add) {
  BufferedRawMachineAssemblerTester<float> m(MachineType::Float32(),
                                             MachineType::Float32());
  m.Return(m.Float32Add(m.Parameter(0), m.Parameter(1)));

  FOR_FLOAT32_INPUTS(i) {
    FOR_FLOAT32_INPUTS(j) { CHECK_FLOAT_EQ(i + j, m.Call(i, j)); }
  }
}


TEST(RunFloat32Sub) {
  BufferedRawMachineAssemblerTester<float> m(MachineType::Float32(),
                                             MachineType::Float32());
  m.Return(m.Float32Sub(m.Parameter(0), m.Parameter(1)));

  FOR_FLOAT32_INPUTS(i) {
    FOR_FLOAT32_INPUTS(j) { CHECK_FLOAT_EQ(i - j, m.Call(i, j)); }
  }
}

TEST(RunFloat32Neg) {
  BufferedRawMachineAssemblerTester<float> m(MachineType::Float32());
  m.Return(m.AddNode(m.machine()->Float32Neg(), m.Parameter(0)));
  FOR_FLOAT32_INPUTS(i) { CHECK_FLOAT_EQ(-0.0f - i, m.Call(i)); }
}

TEST(RunFloat32Mul) {
  BufferedRawMachineAssemblerTester<float> m(MachineType::Float32(),
                                             MachineType::Float32());
  m.Return(m.Float32Mul(m.Parameter(0), m.Parameter(1)));

  FOR_FLOAT32_INPUTS(i) {
    FOR_FLOAT32_INPUTS(j) { CHECK_FLOAT_EQ(i * j, m.Call(i, j)); }
  }
}


TEST(RunFloat32Div) {
  BufferedRawMachineAssemblerTester<float> m(MachineType::Float32(),
                                             MachineType::Float32());
  m.Return(m.Float32Div(m.Parameter(0), m.Parameter(1)));

  FOR_FLOAT32_INPUTS(i) {
    FOR_FLOAT32_INPUTS(j) { CHECK_FLOAT_EQ(base::Divide(i, j), m.Call(i, j)); }
  }
}


TEST(RunFloat64Add) {
  BufferedRawMachineAssemblerTester<double> m(MachineType::Float64(),
                                              MachineType::Float64());
  m.Return(m.Float64Add(m.Parameter(0), m.Parameter(1)));

  FOR_FLOAT64_INPUTS(i) {
    FOR_FLOAT64_INPUTS(j) { CHECK_DOUBLE_EQ(i + j, m.Call(i, j)); }
  }
}


TEST(RunFloat64Sub) {
  BufferedRawMachineAssemblerTester<double> m(MachineType::Float64(),
                                              MachineType::Float64());
  m.Return(m.Float64Sub(m.Parameter(0), m.Parameter(1)));

  FOR_FLOAT64_INPUTS(i) {
    FOR_FLOAT64_INPUTS(j) { CHECK_DOUBLE_EQ(i - j, m.Call(i, j)); }
  }
}

TEST(RunFloat64Neg) {
  BufferedRawMachineAssemblerTester<double> m(MachineType::Float64());
  m.Return(m.AddNode(m.machine()->Float64Neg(), m.Parameter(0)));
  FOR_FLOAT64_INPUTS(i) { CHECK_DOUBLE_EQ(-0.0 - i, m.Call(i)); }
}

TEST(RunFloat64Mul) {
  BufferedRawMachineAssemblerTester<double> m(MachineType::Float64(),
                                              MachineType::Float64());
  m.Return(m.Float64Mul(m.Parameter(0), m.Parameter(1)));

  FOR_FLOAT64_INPUTS(i) {
    FOR_FLOAT64_INPUTS(j) { CHECK_DOUBLE_EQ(i * j, m.Call(i, j)); }
  }
}


TEST(RunFloat64Div) {
  BufferedRawMachineAssemblerTester<double> m(MachineType::Float64(),
                                              MachineType::Float64());
  m.Return(m.Float64Div(m.Parameter(0), m.Parameter(1)));

  FOR_FLOAT64_INPUTS(i) {
    FOR_FLOAT64_INPUTS(j) { CHECK_DOUBLE_EQ(base::Divide(i, j), m.Call(i, j)); }
  }
}


TEST(RunFloat64Mod) {
  BufferedRawMachineAssemblerTester<double> m(MachineType::Float64(),
                                              MachineType::Float64());
  m.Return(m.Float64Mod(m.Parameter(0), m.Parameter(1)));

  FOR_FLOAT64_INPUTS(i) {
    FOR_FLOAT64_INPUTS(j) { CHECK_DOUBLE_EQ(Modulo(i, j), m.Call(i, j)); }
  }
}


TEST(RunDeadFloat32Binops) {
  RawMachineAssemblerTester<int32_t> m;

  const Operator* ops[] = {m.machine()->Float32Add(), m.machine()->Float32Sub(),
                           m.machine()->Float32Mul(), m.machine()->Float32Div(),
                           nullptr};

  for (int i = 0; ops[i] != nullptr; i++) {
    RawMachineAssemblerTester<int32_t> t;
    int constant = 0x53355 + i;
    t.AddNode(ops[i], t.Float32Constant(0.1f), t.Float32Constant(1.11f));
    t.Return(t.Int32Constant(constant));
    CHECK_EQ(constant, t.Call());
  }
}


TEST(RunDeadFloat64Binops) {
  RawMachineAssemblerTester<int32_t> m;

  const Operator* ops[] = {m.machine()->Float64Add(), m.machine()->Float64Sub(),
                           m.machine()->Float64Mul(), m.machine()->Float64Div(),
                           m.machine()->Float64Mod(), nullptr};

  for (int i = 0; ops[i] != nullptr; i++) {
    RawMachineAssemblerTester<int32_t> t;
    int constant = 0x53355 + i;
    t.AddNode(ops[i], t.Float64Constant(0.1), t.Float64Constant(1.11));
    t.Return(t.Int32Constant(constant));
    CHECK_EQ(constant, t.Call());
  }
}


TEST(RunFloat32AddP) {
  RawMachineAssemblerTester<int32_t> m;
  Float32BinopTester bt(&m);

  bt.AddReturn(m.Float32Add(bt.param0, bt.param1));

  FOR_FLOAT32_INPUTS(pl) {
    FOR_FLOAT32_INPUTS(pr) { CHECK_FLOAT_EQ(pl + pr, bt.call(pl, pr)); }
  }
}


TEST(RunFloat64AddP) {
  RawMachineAssemblerTester<int32_t> m;
  Float64BinopTester bt(&m);

  bt.AddReturn(m.Float64Add(bt.param0, bt.param1));

  FOR_FLOAT64_INPUTS(pl) {
    FOR_FLOAT64_INPUTS(pr) { CHECK_DOUBLE_EQ(pl + pr, bt.call(pl, pr)); }
  }
}

TEST(RunFloat64MaxP) {
  RawMachineAssemblerTester<int32_t> m;
  Float64BinopTester bt(&m);
  bt.AddReturn(m.Float64Max(bt.param0, bt.param1));

  FOR_FLOAT64_INPUTS(pl) {
    FOR_FLOAT64_INPUTS(pr) { CHECK_DOUBLE_EQ(JSMax(pl, pr), bt.call(pl, pr)); }
  }
}


TEST(RunFloat64MinP) {
  RawMachineAssemblerTester<int32_t> m;
  Float64BinopTester bt(&m);
  bt.AddReturn(m.Float64Min(bt.param0, bt.param1));

  FOR_FLOAT64_INPUTS(pl) {
    FOR_FLOAT64_INPUTS(pr) { CHECK_DOUBLE_EQ(JSMin(pl, pr), bt.call(pl, pr)); }
  }
}

TEST(RunFloat32Max) {
  RawMachineAssemblerTester<int32_t> m;
  Float32BinopTester bt(&m);
  bt.AddReturn(m.Float32Max(bt.param0, bt.param1));

  FOR_FLOAT32_INPUTS(pl) {
    FOR_FLOAT32_INPUTS(pr) { CHECK_FLOAT_EQ(JSMax(pl, pr), bt.call(pl, pr)); }
  }
}

TEST(RunFloat32Min) {
  RawMachineAssemblerTester<int32_t> m;
  Float32BinopTester bt(&m);
  bt.AddReturn(m.Float32Min(bt.param0, bt.param1));

  FOR_FLOAT32_INPUTS(pl) {
    FOR_FLOAT32_INPUTS(pr) { CHECK_FLOAT_EQ(JSMin(pl, pr), bt.call(pl, pr)); }
  }
}

TEST(RunFloat64Max) {
  RawMachineAssemblerTester<int32_t> m;
  Float64BinopTester bt(&m);
  bt.AddReturn(m.Float64Max(bt.param0, bt.param1));

  FOR_FLOAT64_INPUTS(pl) {
    FOR_FLOAT64_INPUTS(pr) { CHECK_DOUBLE_EQ(JSMax(pl, pr), bt.call(pl, pr)); }
  }
}

TEST(RunFloat64Min) {
  RawMachineAssemblerTester<int32_t> m;
  Float64BinopTester bt(&m);
  bt.AddReturn(m.Float64Min(bt.param0, bt.param1));

  FOR_FLOAT64_INPUTS(pl) {
    FOR_FLOAT64_INPUTS(pr) { CHECK_DOUBLE_EQ(JSMin(pl, pr), bt.call(pl, pr)); }
  }
}

TEST(RunFloat32SubP) {
  RawMachineAssemblerTester<int32_t> m;
  Float32BinopTester bt(&m);

  bt.AddReturn(m.Float32Sub(bt.param0, bt.param1));

  FOR_FLOAT32_INPUTS(pl) {
    FOR_FLOAT32_INPUTS(pr) { CHECK_FLOAT_EQ(pl - pr, bt.call(pl, pr)); }
  }
}


TEST(RunFloat32SubImm1) {
  FOR_FLOAT32_INPUTS(i) {
    BufferedRawMachineAssemblerTester<float> m(MachineType::Float32());
    m.Return(m.Float32Sub(m.Float32Constant(i), m.Parameter(0)));

    FOR_FLOAT32_INPUTS(j) { CHECK_FLOAT_EQ(i - j, m.Call(j)); }
  }
}


TEST(RunFloat32SubImm2) {
  FOR_FLOAT32_INPUTS(i) {
    BufferedRawMachineAssemblerTester<float> m(MachineType::Float32());
    m.Return(m.Float32Sub(m.Parameter(0), m.Float32Constant(i)));

    FOR_FLOAT32_INPUTS(j) { CHECK_FLOAT_EQ(j - i, m.Call(j)); }
  }
}


TEST(RunFloat64SubImm1) {
  FOR_FLOAT64_INPUTS(i) {
    BufferedRawMachineAssemblerTester<double> m(MachineType::Float64());
    m.Return(m.Float64Sub(m.Float64Constant(i), m.Parameter(0)));

    FOR_FLOAT64_INPUTS(j) { CHECK_DOUBLE_EQ(i - j, m.Call(j)); }
  }
}


TEST(RunFloat64SubImm2) {
  FOR_FLOAT64_INPUTS(i) {
    BufferedRawMachineAssemblerTester<double> m(MachineType::Float64());
    m.Return(m.Float64Sub(m.Parameter(0), m.Float64Constant(i)));

    FOR_FLOAT64_INPUTS(j) { CHECK_DOUBLE_EQ(j - i, m.Call(j)); }
  }
}


TEST(RunFloat64SubP) {
  RawMachineAssemblerTester<int32_t> m;
  Float64BinopTester bt(&m);

  bt.AddReturn(m.Float64Sub(bt.param0, bt.param1));

  FOR_FLOAT64_INPUTS(pl) {
    FOR_FLOAT64_INPUTS(pr) {
      double expected = pl - pr;
      CHECK_DOUBLE_EQ(expected, bt.call(pl, pr));
    }
  }
}


TEST(RunFloat32MulP) {
  RawMachineAssemblerTester<int32_t> m;
  Float32BinopTester bt(&m);

  bt.AddReturn(m.Float32Mul(bt.param0, bt.param1));

  FOR_FLOAT32_INPUTS(pl) {
    FOR_FLOAT32_INPUTS(pr) { CHECK_FLOAT_EQ(pl * pr, bt.call(pl, pr)); }
  }
}


TEST(RunFloat64MulP) {
  RawMachineAssemblerTester<int32_t> m;
  Float64BinopTester bt(&m);

  bt.AddReturn(m.Float64Mul(bt.param0, bt.param1));

  FOR_FLOAT64_INPUTS(pl) {
    FOR_FLOAT64_INPUTS(pr) {
      double expected = pl * pr;
      CHECK_DOUBLE_EQ(expected, bt.call(pl, pr));
    }
  }
}

TEST(RunFloat32MulAndFloat32Neg) {
  BufferedRawMachineAssemblerTester<float> m(MachineType::Float32(),
                                             MachineType::Float32());
  m.Return(m.Float32Neg(m.Float32Mul(m.Parameter(0), m.Parameter(1))));

  FOR_FLOAT32_INPUTS(i) {
    FOR_FLOAT32_INPUTS(j) { CHECK_FLOAT_EQ(-(i * j), m.Call(i, j)); }
  }
}

TEST(RunFloat64MulAndFloat64Neg) {
  BufferedRawMachineAssemblerTester<double> m(MachineType::Float64(),
                                              MachineType::Float64());
  m.Return(m.Float64Neg(m.Float64Mul(m.Parameter(0), m.Parameter(1))));

  FOR_FLOAT64_INPUTS(i) {
    FOR_FLOAT64_INPUTS(j) { CHECK_DOUBLE_EQ(-(i * j), m.Call(i, j)); }
  }
}

TEST(RunFloat32NegAndFloat32Mul1) {
  BufferedRawMachineAssemblerTester<float> m(MachineType::Float32(),
                                             MachineType::Float32());
  m.Return(m.Float32Mul(m.Float32Neg(m.Parameter(0)), m.Parameter(1)));

  FOR_FLOAT32_INPUTS(i) {
    FOR_FLOAT32_INPUTS(j) { CHECK_FLOAT_EQ((-i * j), m.Call(i, j)); }
  }
}

TEST(RunFloat64NegAndFloat64Mul1) {
  BufferedRawMachineAssemblerTester<double> m(MachineType::Float64(),
                                              MachineType::Float64());
  m.Return(m.Float64Mul(m.Float64Neg(m.Parameter(0)), m.Parameter(1)));

  FOR_FLOAT64_INPUTS(i) {
    FOR_FLOAT64_INPUTS(j) { CHECK_DOUBLE_EQ((-i * j), m.Call(i, j)); }
  }
}

TEST(RunFloat32NegAndFloat32Mul2) {
  BufferedRawMachineAssemblerTester<float> m(MachineType::Float32(),
                                             MachineType::Float32());
  m.Return(m.Float32Mul(m.Parameter(0), m.Float32Neg(m.Parameter(1))));

  FOR_FLOAT32_INPUTS(i) {
    FOR_FLOAT32_INPUTS(j) { CHECK_FLOAT_EQ((i * -j), m.Call(i, j)); }
  }
}

TEST(RunFloat64NegAndFloat64Mul2) {
  BufferedRawMachineAssemblerTester<double> m(MachineType::Float64(),
                                              MachineType::Float64());
  m.Return(m.Float64Mul(m.Parameter(0), m.Float64Neg(m.Parameter(1))));

  FOR_FLOAT64_INPUTS(i) {
    FOR_FLOAT64_INPUTS(j) { CHECK_DOUBLE_EQ((i * -j), m.Call(i, j)); }
  }
}

TEST(RunFloat32NegAndFloat32Mul3) {
  BufferedRawMachineAssemblerTester<float> m(MachineType::Float32(),
                                             MachineType::Float32());
  m.Return(
      m.Float32Mul(m.Float32Neg(m.Parameter(0)), m.Float32Neg(m.Parameter(1))));

  FOR_FLOAT32_INPUTS(i) {
    FOR_FLOAT32_INPUTS(j) { CHECK_FLOAT_EQ((-i * -j), m.Call(i, j)); }
  }
}

TEST(RunFloat64NegAndFloat64Mul3) {
  BufferedRawMachineAssemblerTester<double> m(MachineType::Float64(),
                                              MachineType::Float64());
  m.Return(
      m.Float64Mul(m.Float64Neg(m.Parameter(0)), m.Float64Neg(m.Parameter(1))));

  FOR_FLOAT64_INPUTS(i) {
    FOR_FLOAT64_INPUTS(j) { CHECK_DOUBLE_EQ((-i * -j), m.Call(i, j)); }
  }
}

TEST(RunFloat64MulAndFloat64Add1) {
  BufferedRawMachineAssemblerTester<double> m(
      MachineType::Float64(), MachineType::Float64(), MachineType::Float64());
  m.Return(m.Float64Add(m.Float64Mul(m.Parameter(0), m.Parameter(1)),
                        m.Parameter(2)));

  FOR_FLOAT64_INPUTS(i) {
    FOR_FLOAT64_INPUTS(j) {
      FOR_FLOAT64_INPUTS(k) { CHECK_DOUBLE_EQ((i * j) + k, m.Call(i, j, k)); }
    }
  }
}


TEST(RunFloat64MulAndFloat64Add2) {
  BufferedRawMachineAssemblerTester<double> m(
      MachineType::Float64(), MachineType::Float64(), MachineType::Float64());
  m.Return(m.Float64Add(m.Parameter(0),
                        m.Float64Mul(m.Parameter(1), m.Parameter(2))));

  FOR_FLOAT64_INPUTS(i) {
    FOR_FLOAT64_INPUTS(j) {
      FOR_FLOAT64_INPUTS(k) { CHECK_DOUBLE_EQ(i + (j * k), m.Call(i, j, k)); }
    }
  }
}


TEST(RunFloat64MulAndFloat64Sub1) {
  BufferedRawMachineAssemblerTester<double> m(
      MachineType::Float64(), MachineType::Float64(), MachineType::Float64());
  m.Return(m.Float64Sub(m.Float64Mul(m.Parameter(0), m.Parameter(1)),
                        m.Parameter(2)));

  FOR_FLOAT64_INPUTS(i) {
    FOR_FLOAT64_INPUTS(j) {
      FOR_FLOAT64_INPUTS(k) { CHECK_DOUBLE_EQ((i * j) - k, m.Call(i, j, k)); }
    }
  }
}


TEST(RunFloat64MulAndFloat64Sub2) {
  BufferedRawMachineAssemblerTester<double> m(
      MachineType::Float64(), MachineType::Float64(), MachineType::Float64());
  m.Return(m.Float64Sub(m.Parameter(0),
                        m.Float64Mul(m.Parameter(1), m.Parameter(2))));

  FOR_FLOAT64_INPUTS(i) {
    FOR_FLOAT64_INPUTS(j) {
      FOR_FLOAT64_INPUTS(k) { CHECK_DOUBLE_EQ(i - (j * k), m.Call(i, j, k)); }
    }
  }
}


TEST(RunFloat64MulImm1) {
  FOR_FLOAT64_INPUTS(i) {
    BufferedRawMachineAssemblerTester<double> m(MachineType::Float64());
    m.Return(m.Float64Mul(m.Float64Constant(i), m.Parameter(0)));

    FOR_FLOAT64_INPUTS(j) { CHECK_DOUBLE_EQ(i * j, m.Call(j)); }
  }
}


TEST(RunFloat64MulImm2) {
  FOR_FLOAT64_INPUTS(i) {
    BufferedRawMachineAssemblerTester<double> m(MachineType::Float64());
    m.Return(m.Float64Mul(m.Parameter(0), m.Float64Constant(i)));

    FOR_FLOAT64_INPUTS(j) { CHECK_DOUBLE_EQ(j * i, m.Call(j)); }
  }
}


TEST(RunFloat32DivP) {
  RawMachineAssemblerTester<int32_t> m;
  Float32BinopTester bt(&m);

  bt.AddReturn(m.Float32Div(bt.param0, bt.param1));

  FOR_FLOAT32_INPUTS(pl) {
    FOR_FLOAT32_INPUTS(pr) {
      CHECK_FLOAT_EQ(base::Divide(pl, pr), bt.call(pl, pr));
    }
  }
}


TEST(RunFloat64DivP) {
  RawMachineAssemblerTester<int32_t> m;
  Float64BinopTester bt(&m);

  bt.AddReturn(m.Float64Div(bt.param0, bt.param1));

  FOR_FLOAT64_INPUTS(pl) {
    FOR_FLOAT64_INPUTS(pr) {
      CHECK_DOUBLE_EQ(base::Divide(pl, pr), bt.call(pl, pr));
    }
  }
}


TEST(RunFloat64ModP) {
  RawMachineAssemblerTester<int32_t> m;
  Float64BinopTester bt(&m);

  bt.AddReturn(m.Float64Mod(bt.param0, bt.param1));

  FOR_FLOAT64_INPUTS(i) {
    FOR_FLOAT64_INPUTS(j) { CHECK_DOUBLE_EQ(Modulo(i, j), bt.call(i, j)); }
  }
}


TEST(RunChangeInt32ToFloat64_A) {
  int32_t magic = 0x986234;
  BufferedRawMachineAssemblerTester<double> m;
  m.Return(m.ChangeInt32ToFloat64(m.Int32Constant(magic)));
  CHECK_DOUBLE_EQ(static_cast<double>(magic), m.Call());
}


TEST(RunChangeInt32ToFloat64_B) {
  BufferedRawMachineAssemblerTester<double> m(MachineType::Int32());
  m.Return(m.ChangeInt32ToFloat64(m.Parameter(0)));

  FOR_INT32_INPUTS(i) { CHECK_DOUBLE_EQ(static_cast<double>(i), m.Call(i)); }
}


TEST(RunChangeUint32ToFloat64) {
  BufferedRawMachineAssemblerTester<double> m(MachineType::Uint32());
  m.Return(m.ChangeUint32ToFloat64(m.Parameter(0)));

  FOR_UINT32_INPUTS(i) { CHECK_DOUBLE_EQ(static_cast<double>(i), m.Call(i)); }
}


TEST(RunTruncateFloat32ToInt32) {
  // The upper bound is (INT32_MAX + 1), which is the lowest float-representable
  // number above INT32_MAX which cannot be represented as int32.
  float upper_bound = 2147483648.0f;
  // We use INT32_MIN as a lower bound because (INT32_MIN - 1) is not
  // representable as float, and no number between (INT32_MIN - 1) and INT32_MIN
  // is.
  float lower_bound = static_cast<float>(INT32_MIN);
  {
    BufferedRawMachineAssemblerTester<int32_t> m(MachineType::Float32());
    m.Return(m.TruncateFloat32ToInt32(m.Parameter(0),
                                      TruncateKind::kArchitectureDefault));
    FOR_FLOAT32_INPUTS(i) {
      if (i < upper_bound && i >= lower_bound) {
        CHECK_EQ(static_cast<int32_t>(i), m.Call(i));
      } else if (i < lower_bound) {
#if V8_TARGET_ARCH_MIPS64 && !_MIPS_ARCH_MIPS64R6
        CHECK_EQ(std::numeric_limits<int32_t>::max(), m.Call(i));
#else
        CHECK_EQ(std::numeric_limits<int32_t>::min(), m.Call(i));
#endif
      } else if (i >= upper_bound) {
#if V8_TARGET_ARCH_IA32 || V8_TARGET_ARCH_X64
        CHECK_EQ(std::numeric_limits<int32_t>::min(), m.Call(i));
#else
        CHECK_EQ(std::numeric_limits<int32_t>::max(), m.Call(i));
#endif
      } else {
        DCHECK(std::isnan(i));
#if V8_TARGET_ARCH_IA32 || V8_TARGET_ARCH_X64 || V8_TARGET_ARCH_S390X || \
    V8_TARGET_ARCH_PPC64
        CHECK_EQ(std::numeric_limits<int32_t>::min(), m.Call(i));
#elif V8_TARGET_ARCH_ARM64 || V8_TARGET_ARCH_ARM || V8_TARGET_ARCH_LOONG64
        CHECK_EQ(0, m.Call(i));
#elif V8_TARGET_ARCH_RISCV64 || V8_TARGET_ARCH_RISCV32
        CHECK_EQ(std::numeric_limits<int32_t>::max(), m.Call(i));
#endif
      }
    }
  }
  {
    BufferedRawMachineAssemblerTester<int32_t> m(MachineType::Float32());
    m.Return(m.TruncateFloat32ToInt32(m.Parameter(0),
                                      TruncateKind::kSetOverflowToMin));
    FOR_FLOAT32_INPUTS(i) {
      if (i < upper_bound && i >= lower_bound) {
        CHECK_EQ(static_cast<int32_t>(i), m.Call(i));
      } else if (!std::isnan(i)) {
        CHECK_EQ(std::numeric_limits<int32_t>::min(), m.Call(i));
      } else {
        DCHECK(std::isnan(i));
#if V8_TARGET_ARCH_IA32 || V8_TARGET_ARCH_X64 || V8_TARGET_ARCH_S390X || \
    V8_TARGET_ARCH_PPC64
        CHECK_EQ(std::numeric_limits<int32_t>::min(), m.Call(i));
#elif V8_TARGET_ARCH_ARM64 || V8_TARGET_ARCH_ARM || V8_TARGET_ARCH_LOONG64
        CHECK_EQ(0, m.Call(i));
#endif
      }
    }
  }
}

TEST(RunTruncateFloat32ToUint32) {
  // The upper bound is (UINT32_MAX + 1), which is the lowest
  // float-representable number above UINT32_MAX which cannot be represented as
  // uint32.
  double upper_bound = 4294967296.0f;
  double lower_bound = -1.0f;

  // No tests outside the range of UINT32 are performed, as the semantics are
  // tricky on x64. On this architecture, the assembler transforms float32 into
  // a signed int64 instead of an unsigned int32. Overflow can then be detected
  // by converting back to float and testing for equality as done in
  // wasm-compiler.cc .
  //
  // On arm architectures, TruncateKind::kArchitectureDefault rounds towards 0
  // upon overflow and returns 0 if the input is NaN.
  // TruncateKind::kSetOverflowToMin returns 0 on overflow and NaN.
  {
    BufferedRawMachineAssemblerTester<uint32_t> m(MachineType::Float32());
    m.Return(m.TruncateFloat32ToUint32(m.Parameter(0),
                                       TruncateKind::kArchitectureDefault));
    FOR_UINT32_INPUTS(i) {
      volatile float input = static_cast<float>(i);
      if (input < upper_bound) {
        CHECK_EQ(static_cast<uint32_t>(input), m.Call(input));
      }
    }
    FOR_FLOAT32_INPUTS(j) {
      if ((j < upper_bound) && (j > lower_bound)) {
        CHECK_EQ(static_cast<uint32_t>(j), m.Call(j));
      }
    }
  }
  {
    BufferedRawMachineAssemblerTester<uint32_t> m(MachineType::Float32());
    m.Return(m.TruncateFloat32ToUint32(m.Parameter(0),
                                       TruncateKind::kSetOverflowToMin));
    FOR_UINT32_INPUTS(i) {
      volatile float input = static_cast<float>(i);
      if (input < upper_bound) {
        CHECK_EQ(static_cast<uint32_t>(input), m.Call(input));
      }
    }
    FOR_FLOAT32_INPUTS(j) {
      if ((j < upper_bound) && (j > lower_bound)) {
        CHECK_EQ(static_cast<uint32_t>(j), m.Call(j));
      }
    }
  }
}


TEST(RunChangeFloat64ToInt32_A) {
  BufferedRawMachineAssemblerTester<int32_t> m;
  double magic = 11.1;
  m.Return(m.ChangeFloat64ToInt32(m.Float64Constant(magic)));
  CHECK_EQ(static_cast<int32_t>(magic), m.Call());
}


TEST(RunChangeFloat64ToInt32_B) {
  BufferedRawMachineAssemblerTester<int32_t> m(MachineType::Float64());
  m.Return(m.ChangeFloat64ToInt32(m.Parameter(0)));

  // Note we don't check fractional inputs, or inputs outside the range of
  // int32, because these Convert operators really should be Change operators.
  FOR_INT32_INPUTS(i) { CHECK_EQ(i, m.Call(static_cast<double>(i))); }

  for (int32_t n = 1; n < 31; ++n) {
    CHECK_EQ(1 << n, m.Call(static_cast<double>(1 << n)));
  }

  for (int32_t n = 1; n < 31; ++n) {
    CHECK_EQ(3 << n, m.Call(static_cast<double>(3 << n)));
  }
}

TEST(RunChangeFloat64ToUint32) {
  BufferedRawMachineAssemblerTester<uint32_t> m(MachineType::Float64());
  m.Return(m.ChangeFloat64ToUint32(m.Parameter(0)));

  {
    FOR_UINT32_INPUTS(i) { CHECK_EQ(i, m.Call(static_cast<double>(i))); }
  }

  // Check various powers of 2.
  for (int32_t n = 1; n < 31; ++n) {
    { CHECK_EQ(1u << n, m.Call(static_cast<double>(1u << n))); }

    { CHECK_EQ(3u << n, m.Call(static_cast<double>(3u << n))); }
  }
  // Note we don't check fractional inputs, because these Convert operators
  // really should be Change operators.
}


TEST(RunTruncateFloat64ToFloat32) {
  BufferedRawMachineAssemblerTester<float> m(MachineType::Float64());

  m.Return(m.TruncateFloat64ToFloat32(m.Parameter(0)));

  FOR_FLOAT64_INPUTS(i) { CHECK_FLOAT_EQ(DoubleToFloat32(i), m.Call(i)); }
}

uint64_t ToInt64(uint32_t low, uint32_t high) {
  return (static_cast<uint64_t>(high) << 32) | static_cast<uint64_t>(low);
}

#if V8_TARGET_ARCH_32_BIT
TEST(RunInt32PairAdd) {
  BufferedRawMachineAssemblerTester<int32_t> m(
      MachineType::Uint32(), MachineType::Uint32(), MachineType::Uint32(),
      MachineType::Uint32());

  uint32_t high;
  uint32_t low;

  Node* PairAdd = m.Int32PairAdd(m.Parameter(0), m.Parameter(1), m.Parameter(2),
                                 m.Parameter(3));

  m.StoreToPointer(&low, MachineRepresentation::kWord32,
                   m.Projection(0, PairAdd));
  m.StoreToPointer(&high, MachineRepresentation::kWord32,
                   m.Projection(1, PairAdd));
  m.Return(m.Int32Constant(74));

  FOR_UINT64_INPUTS(i) {
    FOR_UINT64_INPUTS(j) {
      m.Call(static_cast<uint32_t>(i & 0xFFFFFFFF),
             static_cast<uint32_t>(i >> 32),
             static_cast<uint32_t>(j & 0xFFFFFFFF),
             static_cast<uint32_t>(j >> 32));
      CHECK_EQ(i + j, ToInt64(low, high));
    }
  }
}

TEST(RunInt32PairAddUseOnlyHighWord) {
  BufferedRawMachineAssemblerTester<int32_t> m(
      MachineType::Uint32(), MachineType::Uint32(), MachineType::Uint32(),
      MachineType::Uint32());

  m.Return(m.Projection(1, m.Int32PairAdd(m.Parameter(0), m.Parameter(1),
                                          m.Parameter(2), m.Parameter(3))));

  FOR_UINT64_INPUTS(i) {
    FOR_UINT64_INPUTS(j) {
      CHECK_EQ(
          static_cast<uint32_t>((i + j) >> 32),
          static_cast<uint32_t>(m.Call(static_cast<uint32_t>(i & 0xFFFFFFFF),
                                       static_cast<uint32_t>(i >> 32),
                                       static_cast<uint32_t>(j & 0xFFFFFFFF),
                                       static_cast<uint32_t>(j >> 32))));
    }
  }
}

void TestInt32PairAddWithSharedInput(int a, int b, int c, int d) {
  BufferedRawMachineAssemblerTester<int32_t> m(MachineType::Uint32(),
                                               MachineType::Uint32());

  uint32_t high;
  uint32_t low;

  Node* PairAdd = m.Int32PairAdd(m.Parameter(a), m.Parameter(b), m.Parameter(c),
                                 m.Parameter(d));

  m.StoreToPointer(&low, MachineRepresentation::kWord32,
                   m.Projection(0, PairAdd));
  m.StoreToPointer(&high, MachineRepresentation::kWord32,
                   m.Projection(1, PairAdd));
  m.Return(m.Int32Constant(74));

  FOR_UINT32_INPUTS(i) {
    FOR_UINT32_INPUTS(j) {
      m.Call(i, j);
      uint32_t inputs[] = {i, j};
      CHECK_EQ(ToInt64(inputs[a], inputs[b]) + ToInt64(inputs[c], inputs[d]),
               ToInt64(low, high));
    }
  }
}

TEST(RunInt32PairAddWithSharedInput) {
  TestInt32PairAddWithSharedInput(0, 0, 0, 0);
  TestInt32PairAddWithSharedInput(1, 0, 0, 0);
  TestInt32PairAddWithSharedInput(0, 1, 0, 0);
  TestInt32PairAddWithSharedInput(0, 0, 1, 0);
  TestInt32PairAddWithSharedInput(0, 0, 0, 1);
  TestInt32PairAddWithSharedInput(1, 1, 0, 0);
}

TEST(RunInt32PairSub) {
  BufferedRawMachineAssemblerTester<int32_t> m(
      MachineType::Uint32(), MachineType::Uint32(), MachineType::Uint32(),
      MachineType::Uint32());

  uint32_t high;
  uint32_t low;

  Node* PairSub = m.Int32PairSub(m.Parameter(0), m.Parameter(1), m.Parameter(2),
                                 m.Parameter(3));

  m.StoreToPointer(&low, MachineRepresentation::kWord32,
                   m.Projection(0, PairSub));
  m.StoreToPointer(&high, MachineRepresentation::kWord32,
                   m.Projection(1, PairSub));
  m.Return(m.Int32Constant(74));

  FOR_UINT64_INPUTS(i) {
    FOR_UINT64_INPUTS(j) {
      m.Call(static_cast<uint32_t>(i & 0xFFFFFFFF),
             static_cast<uint32_t>(i >> 32),
             static_cast<uint32_t>(j & 0xFFFFFFFF),
             static_cast<uint32_t>(j >> 32));
      CHECK_EQ(i - j, ToInt64(low, high));
    }
  }
}

TEST(RunInt32PairSubUseOnlyHighWord) {
  BufferedRawMachineAssemblerTester<int32_t> m(
      MachineType::Uint32(), MachineType::Uint32(), MachineType::Uint32(),
      MachineType::Uint32());

  m.Return(m.Projection(1, m.Int32PairSub(m.Parameter(0), m.Parameter(1),
                                          m.Parameter(2), m.Parameter(3))));

  FOR_UINT64_INPUTS(i) {
    FOR_UINT64_INPUTS(j) {
      CHECK_EQ(
          static_cast<uint32_t>((i - j) >> 32),
          static_cast<uint32_t>(m.Call(static_cast<uint32_t>(i & 0xFFFFFFFF),
                                       static_cast<uint32_t>(i >> 32),
                                       static_cast<uint32_t>(j & 0xFFFFFFFF),
                                       static_cast<uint32_t>(j >> 32))));
    }
  }
}

void TestInt32PairSubWithSharedInput(int a, int b, int c, int d) {
  BufferedRawMachineAssemblerTester<int32_t> m(MachineType::Uint32(),
                                               MachineType::Uint32());

  uint32_t high;
  uint32_t low;

  Node* PairSub = m.Int32PairSub(m.Parameter(a), m.Parameter(b), m.Parameter(c),
                                 m.Parameter(d));

  m.StoreToPointer(&low, MachineRepresentation::kWord32,
                   m.Projection(0, PairSub));
  m.StoreToPointer(&high, MachineRepresentation::kWord32,
                   m.Projection(1, PairSub));
  m.Return(m.Int32Constant(74));

  FOR_UINT32_INPUTS(i) {
    FOR_UINT32_INPUTS(j) {
      m.Call(i, j);
      uint32_t inputs[] = {i, j};
      CHECK_EQ(ToInt64(inputs[a], inputs[b]) - ToInt64(inputs[c], inputs[d]),
               ToInt64(low, high));
    }
  }
}

TEST(RunInt32PairSubWithSharedInput) {
  TestInt32PairSubWithSharedInput(0, 0, 0, 0);
  TestInt32PairSubWithSharedInput(1, 0, 0, 0);
  TestInt32PairSubWithSharedInput(0, 1, 0, 0);
  TestInt32PairSubWithSharedInput(0, 0, 1, 0);
  TestInt32PairSubWithSharedInput(0, 0, 0, 1);
  TestInt32PairSubWithSharedInput(1, 1, 0, 0);
}

TEST(RunInt32PairMul) {
  BufferedRawMachineAssemblerTester<int32_t> m(
      MachineType::Uint32(), MachineType::Uint32(), MachineType::Uint32(),
      MachineType::Uint32());

  uint32_t high;
  uint32_t low;

  Node* PairMul = m.Int32PairMul(m.Parameter(0), m.Parameter(1), m.Parameter(2),
                                 m.Parameter(3));

  m.StoreToPointer(&low, MachineRepresentation::kWord32,
                   m.Projection(0, PairMul));
  m.StoreToPointer(&high, MachineRepresentation::kWord32,
                   m.Projection(1, PairMul));
  m.Return(m.Int32Constant(74));

  FOR_UINT64_INPUTS(i) {
    FOR_UINT64_INPUTS(j) {
      m.Call(static_cast<uint32_t>(i & 0xFFFFFFFF),
             static_cast<uint32_t>(i >> 32),
             static_cast<uint32_t>(j & 0xFFFFFFFF),
             static_cast<uint32_t>(j >> 32));
      CHECK_EQ(i * j, ToInt64(low, high));
    }
  }
}

TEST(RunInt32PairMulUseOnlyHighWord) {
  BufferedRawMachineAssemblerTester<int32_t> m(
      MachineType::Uint32(), MachineType::Uint32(), MachineType::Uint32(),
      MachineType::Uint32());

  m.Return(m.Projection(1, m.Int32PairMul(m.Parameter(0), m.Parameter(1),
                                          m.Parameter(2), m.Parameter(3))));

  FOR_UINT64_INPUTS(i) {
    FOR_UINT64_INPUTS(j) {
      CHECK_EQ(
          static_cast<uint32_t>((i * j) >> 32),
          static_cast<uint32_t>(m.Call(static_cast<uint32_t>(i & 0xFFFFFFFF),
                                       static_cast<uint32_t>(i >> 32),
                                       static_cast<uint32_t>(j & 0xFFFFFFFF),
                                       static_cast<uint32_t>(j >> 32))));
    }
  }
}

void TestInt32PairMulWithSharedInput(int a, int b, int c, int d) {
  BufferedRawMachineAssemblerTester<int32_t> m(MachineType::Uint32(),
                                               MachineType::Uint32());

  uint32_t high;
  uint32_t low;

  Node* PairMul = m.Int32PairMul(m.Parameter(a), m.Parameter(b), m.Parameter(c),
                                 m.Parameter(d));

  m.StoreToPointer(&low, MachineRepresentation::kWord32,
                   m.Projection(0, PairMul));
  m.StoreToPointer(&high, MachineRepresentation::kWord32,
                   m.Projection(1, PairMul));
  m.Return(m.Int32Constant(74));

  FOR_UINT32_INPUTS(i) {
    FOR_UINT32_INPUTS(j) {
      m.Call(i, j);
      uint32_t inputs[] = {i, j};
      CHECK_EQ(ToInt64(inputs[a], inputs[b]) * ToInt64(inputs[c], inputs[d]),
               ToInt64(low, high));
    }
  }
}

TEST(RunInt32PairMulWithSharedInput) {
  TestInt32PairMulWithSharedInput(0, 0, 0, 0);
  TestInt32PairMulWithSharedInput(1, 0, 0, 0);
  TestInt32PairMulWithSharedInput(0, 1, 0, 0);
  TestInt32PairMulWithSharedInput(0, 0, 1, 0);
  TestInt32PairMulWithSharedInput(0, 0, 0, 1);
  TestInt32PairMulWithSharedInput(1, 1, 0, 0);
  TestInt32PairMulWithSharedInput(0, 1, 1, 0);
}

TEST(RunWord32PairShl) {
  BufferedRawMachineAssemblerTester<int32_t> m(
      MachineType::Uint32(), MachineType::Uint32(), MachineType::Uint32());

  uint32_t high;
  uint32_t low;

  Node* PairShl =
      m.Word32PairShl(m.Parameter(0), m.Parameter(1), m.Parameter(2));

  m.StoreToPointer(&low, MachineRepresentation::kWord32,
                   m.Projection(0, PairShl));
  m.StoreToPointer(&high, MachineRepresentation::kWord32,
                   m.Projection(1, PairShl));
  m.Return(m.Int32Constant(74));

  FOR_UINT64_INPUTS(i) {
    for (uint32_t j = 0; j < 64; j++) {
      m.Call(static_cast<uint32_t>(i & 0xFFFFFFFF),
             static_cast<uint32_t>(i >> 32), j);
      CHECK_EQ(i << j, ToInt64(low, high));
    }
  }
}

TEST(RunWord32PairShlUseOnlyHighWord) {
  BufferedRawMachineAssemblerTester<int32_t> m(
      MachineType::Uint32(), MachineType::Uint32(), MachineType::Uint32());

  m.Return(m.Projection(
      1, m.Word32PairShl(m.Parameter(0), m.Parameter(1), m.Parameter(2))));

  FOR_UINT64_INPUTS(i) {
    for (uint32_t j = 0; j < 64; j++) {
      CHECK_EQ(
          static_cast<uint32_t>((i << j) >> 32),
          static_cast<uint32_t>(m.Call(static_cast<uint32_t>(i & 0xFFFFFFFF),
                                       static_cast<uint32_t>(i >> 32), j)));
    }
  }
}

void TestWord32PairShlWithSharedInput(int a, int b) {
  BufferedRawMachineAssemblerTester<int32_t> m(MachineType::Uint32(),
                                               MachineType::Uint32());

  uint32_t high;
  uint32_t low;

  Node* PairAdd =
      m.Word32PairShl(m.Parameter(a), m.Parameter(b), m.Parameter(1));

  m.StoreToPointer(&low, MachineRepresentation::kWord32,
                   m.Projection(0, PairAdd));
  m.StoreToPointer(&high, MachineRepresentation::kWord32,
                   m.Projection(1, PairAdd));
  m.Return(m.Int32Constant(74));

  FOR_UINT32_INPUTS(i) {
    for (uint32_t j = 0; j < 64; j++) {
      m.Call(i, j);
      uint32_t inputs[] = {i, j};
      CHECK_EQ(ToInt64(inputs[a], inputs[b]) << j, ToInt64(low, high));
    }
  }
}

TEST(RunWord32PairShlWithSharedInput) {
  TestWord32PairShlWithSharedInput(0, 0);
  TestWord32PairShlWithSharedInput(0, 1);
  TestWord32PairShlWithSharedInput(1, 0);
  TestWord32PairShlWithSharedInput(1, 1);
}

TEST(RunWord32PairShr) {
  BufferedRawMachineAssemblerTester<int32_t> m(
      MachineType::Uint32(), MachineType::Uint32(), MachineType::Uint32());

  uint32_t high;
  uint32_t low;

  Node* PairAdd =
      m.Word32PairShr(m.Parameter(0), m.Parameter(1), m.Parameter(2));

  m.StoreToPointer(&low, MachineRepresentation::kWord32,
                   m.Projection(0, PairAdd));
  m.StoreToPointer(&high, MachineRepresentation::kWord32,
                   m.Projection(1, PairAdd));
  m.Return(m.Int32Constant(74));

  FOR_UINT64_INPUTS(i) {
    for (uint32_t j = 0; j < 64; j++) {
      m.Call(static_cast<uint32_t>(i & 0xFFFFFFFF),
             static_cast<uint32_t>(i >> 32), j);
      CHECK_EQ(i >> j, ToInt64(low, high));
    }
  }
}

TEST(RunWord32PairShrUseOnlyHighWord) {
  BufferedRawMachineAssemblerTester<int32_t> m(
      MachineType::Uint32(), MachineType::Uint32(), MachineType::Uint32());

  m.Return(m.Projection(
      1, m.Word32PairShr(m.Parameter(0), m.Parameter(1), m.Parameter(2))));

  FOR_UINT64_INPUTS(i) {
    for (uint32_t j = 0; j < 64; j++) {
      CHECK_EQ(
          static_cast<uint32_t>((i >> j) >> 32),
          static_cast<uint32_t>(m.Call(static_cast<uint32_t>(i & 0xFFFFFFFF),
                                       static_cast<uint32_t>(i >> 32), j)));
    }
  }
}

TEST(RunWord32PairSar) {
  BufferedRawMachineAssemblerTester<int32_t> m(
      MachineType::Uint32(), MachineType::Uint32(), MachineType::Uint32());

  uint32_t high;
  uint32_t low;

  Node* PairAdd =
      m.Word32PairSar(m.Parameter(0), m.Parameter(1), m.Parameter(2));

  m.StoreToPointer(&low, MachineRepresentation::kWord32,
                   m.Projection(0, PairAdd));
  m.StoreToPointer(&high, MachineRepresentation::kWord32,
                   m.Projection(1, PairAdd));
  m.Return(m.Int32Constant(74));

  FOR_INT64_INPUTS(i) {
    for (uint32_t j = 0; j < 64; j++) {
      m.Call(static_cast<uint32_t>(i & 0xFFFFFFFF),
             static_cast<uint32_t>(i >> 32), j);
      CHECK_EQ(i >> j, static_cast<int64_t>(ToInt64(low, high)));
    }
  }
}

TEST(RunWord32PairSarUseOnlyHighWord) {
  BufferedRawMachineAssemblerTester<int32_t> m(
      MachineType::Uint32(), MachineType::Uint32(), MachineType::Uint32());

  m.Return(m.Projection(
      1, m.Word32PairSar(m.Parameter(0), m.Parameter(1), m.Parameter(2))));

  FOR_INT64_INPUTS(i) {
    for (uint32_t j = 0; j < 64; j++) {
      CHECK_EQ(
          static_cast<uint32_t>((i >> j) >> 32),
          static_cast<uint32_t>(m.Call(static_cast<uint32_t>(i & 0xFFFFFFFF),
                                       static_cast<uint32_t>(i >> 32), j)));
    }
  }
}
#endif

TEST(RunDeadChangeFloat64ToInt32) {
  RawMachineAssemblerTester<int32_t> m;
  const int magic = 0x88ABCDA4;
  m.ChangeFloat64ToInt32(m.Float64Constant(999.78));
  m.Return(m.Int32Constant(magic));
  CHECK_EQ(magic, m.Call());
}


TEST(RunDeadChangeInt32ToFloat64) {
  RawMachineAssemblerTester<int32_t> m;
  const int magic = 0x8834ABCD;
  m.ChangeInt32ToFloat64(m.Int32Constant(magic - 6888));
  m.Return(m.Int32Constant(magic));
  CHECK_EQ(magic, m.Call());
}


TEST(RunLoopPhiInduction2) {
  RawMachineAssemblerTester<int32_t> m;

  int false_val = 0x10777;

  // x = false_val; while(false) { x++; } return x;
  RawMachineLabel header, body, end;
  Node* false_node = m.Int32Constant(false_val);
  m.Goto(&header);
  m.Bind(&header);
  Node* phi = m.Phi(MachineRepresentation::kWord32, false_node, false_node);
  m.Branch(m.Int32Constant(0), &body, &end);
  m.Bind(&body);
  Node* add = m.Int32Add(phi, m.Int32Constant(1));
  phi->ReplaceInput(1, add);
  m.Goto(&header);
  m.Bind(&end);
  m.Return(phi);

  CHECK_EQ(false_val, m.Call());
}


TEST(RunFloatDiamond) {
  RawMachineAssemblerTester<int32_t> m;

  const int magic = 99645;
  float buffer = 0.1f;
  float constant = 99.99f;

  RawMachineLabel blocka, blockb, end;
  Node* k1 = m.Float32Constant(constant);
  Node* k2 = m.Float32Constant(0 - constant);
  m.Branch(m.Int32Constant(0), &blocka, &blockb);
  m.Bind(&blocka);
  m.Goto(&end);
  m.Bind(&blockb);
  m.Goto(&end);
  m.Bind(&end);
  Node* phi = m.Phi(MachineRepresentation::kFloat32, k2, k1);
  m.Store(MachineRepresentation::kFloat32, m.PointerConstant(&buffer),
          m.IntPtrConstant(0), phi, kNoWriteBarrier);
  m.Return(m.Int32Constant(magic));

  CHECK_EQ(magic, m.Call());
  CHECK(constant == buffer);
}


TEST(RunDoubleDiamond) {
  RawMachineAssemblerTester<int32_t> m;

  const int magic = 99645;
  double buffer = 0.1;
  double constant = 99.99;

  RawMachineLabel blocka, blockb, end;
  Node* k1 = m.Float64Constant(constant);
  Node* k2 = m.Float64Constant(0 - constant);
  m.Branch(m.Int32Constant(0), &blocka, &blockb);
  m.Bind(&blocka);
  m.Goto(&end);
  m.Bind(&blockb);
  m.Goto(&end);
  m.Bind(&end);
  Node* phi = m.Phi(MachineRepresentation::kFloat64, k2, k1);
  m.Store(MachineRepresentation::kFloat64, m.PointerConstant(&buffer),
          m.Int32Constant(0), phi, kNoWriteBarrier);
  m.Return(m.Int32Constant(magic));

  CHECK_EQ(magic, m.Call());
  CHECK_EQ(constant, buffer);
}


TEST(RunRefDiamond) {
  RawMachineAssemblerTester<int32_t> m;

  const int magic = 99644;
  DirectHandle<String> rexpected =
      CcTest::i_isolate()->factory()->InternalizeUtf8String("A");
  Tagged<String> buffer;

  RawMachineLabel blocka, blockb, end;
  Node* k1 = m.StringConstant("A");
  Node* k2 = m.StringConstant("B");
  m.Branch(m.Int32Constant(0), &blocka, &blockb);
  m.Bind(&blocka);
  m.Goto(&end);
  m.Bind(&blockb);
  m.Goto(&end);
  m.Bind(&end);
  Node* phi = m.Phi(MachineRepresentation::kTagged, k2, k1);
  if (COMPRESS_POINTERS_BOOL) {
    // Since |buffer| is located off-heap, use full pointer store.
    m.Store(MachineType::PointerRepresentation(), m.PointerConstant(&buffer),
            m.Int32Constant(0), m.BitcastTaggedToWord(phi), kNoWriteBarrier);
  } else {
    m.Store(MachineRepresentation::kTagged, m.PointerConstant(&buffer),
            m.Int32Constant(0), phi, kNoWriteBarrier);
  }
  m.Return(m.Int32Constant(magic));

  CHECK_EQ(magic, m.Call());
  CHECK(Object::SameValue(*rexpected, buffer));
}


TEST(RunDoubleRefDiamond) {
  RawMachineAssemblerTester<int32_t> m;

  const int magic = 99648;
  double dbuffer = 0.1;
  double dconstant = 99.99;
  DirectHandle<String> rexpected =
      CcTest::i_isolate()->factory()->InternalizeUtf8String("AX");
  Tagged<String> rbuffer;

  RawMachineLabel blocka, blockb, end;
  Node* d1 = m.Float64Constant(dconstant);
  Node* d2 = m.Float64Constant(0 - dconstant);
  Node* r1 = m.StringConstant("AX");
  Node* r2 = m.StringConstant("BX");
  m.Branch(m.Int32Constant(0), &blocka, &blockb);
  m.Bind(&blocka);
  m.Goto(&end);
  m.Bind(&blockb);
  m.Goto(&end);
  m.Bind(&end);
  Node* dphi = m.Phi(MachineRepresentation::kFloat64, d2, d1);
  Node* rphi = m.Phi(MachineRepresentation::kTagged, r2, r1);
  m.Store(MachineRepresentation::kFloat64, m.PointerConstant(&dbuffer),
          m.Int32Constant(0), dphi, kNoWriteBarrier);
  if (COMPRESS_POINTERS_BOOL) {
    // Since |buffer| is located off-heap, use full pointer store.
    m.Store(MachineType::PointerRepresentation(), m.PointerConstant(&rbuffer),
            m.Int32Constant(0), m.BitcastTaggedToWord(rphi), kNoWriteBarrier);
  } else {
    m.Store(MachineRepresentation::kTagged, m.PointerConstant(&rbuffer),
            m.Int32Constant(0), rphi, kNoWriteBarrier);
  }
  m.Return(m.Int32Constant(magic));

  CHECK_EQ(magic, m.Call());
  CHECK_EQ(dconstant, dbuffer);
  CHECK(Object::SameValue(*rexpected, rbuffer));
}


TEST(RunDoubleRefDoubleDiamond) {
  RawMachineAssemblerTester<int32_t> m;

  const int magic = 99649;
  double dbuffer = 0.1;
  double dconstant = 99.997;
  DirectHandle<String> rexpected =
      CcTest::i_isolate()->factory()->InternalizeUtf8String("AD");
  Tagged<String> rbuffer;

  RawMachineLabel blocka, blockb, mid, blockd, blocke, end;
  Node* d1 = m.Float64Constant(dconstant);
  Node* d2 = m.Float64Constant(0 - dconstant);
  Node* r1 = m.StringConstant("AD");
  Node* r2 = m.StringConstant("BD");
  m.Branch(m.Int32Constant(0), &blocka, &blockb);
  m.Bind(&blocka);
  m.Goto(&mid);
  m.Bind(&blockb);
  m.Goto(&mid);
  m.Bind(&mid);
  Node* dphi1 = m.Phi(MachineRepresentation::kFloat64, d2, d1);
  Node* rphi1 = m.Phi(MachineRepresentation::kTagged, r2, r1);
  m.Branch(m.Int32Constant(0), &blockd, &blocke);

  m.Bind(&blockd);
  m.Goto(&end);
  m.Bind(&blocke);
  m.Goto(&end);
  m.Bind(&end);
  Node* dphi2 = m.Phi(MachineRepresentation::kFloat64, d1, dphi1);
  Node* rphi2 = m.Phi(MachineRepresentation::kTagged, r1, rphi1);

  m.Store(MachineRepresentation::kFloat64, m.PointerConstant(&dbuffer),
          m.Int32Constant(0), dphi2, kNoWriteBarrier);
  if (COMPRESS_POINTERS_BOOL) {
    // Since |buffer| is located off-heap, use full pointer store.
    m.Store(MachineType::PointerRepresentation(), m.PointerConstant(&rbuffer),
            m.Int32Constant(0), m.BitcastTaggedToWord(rphi2), kNoWriteBarrier);
  } else {
    m.Store(MachineRepresentation::kTagged, m.PointerConstant(&rbuffer),
            m.Int32Constant(0), rphi2, kNoWriteBarrier);
  }
  m.Return(m.Int32Constant(magic));

  CHECK_EQ(magic, m.Call());
  CHECK_EQ(dconstant, dbuffer);
  CHECK(Object::SameValue(*rexpected, rbuffer));
}


TEST(RunDoubleLoopPhi) {
  RawMachineAssemblerTester<int32_t> m;
  RawMachineLabel header, body, end;

  int magic = 99773;
  double buffer = 0.99;
  double dconstant = 777.1;

  Node* zero = m.Int32Constant(0);
  Node* dk = m.Float64Constant(dconstant);

  m.Goto(&header);
  m.Bind(&header);
  Node* phi = m.Phi(MachineRepresentation::kFloat64, dk, dk);
  phi->ReplaceInput(1, phi);
  m.Branch(zero, &body, &end);
  m.Bind(&body);
  m.Goto(&header);
  m.Bind(&end);
  m.Store(MachineRepresentation::kFloat64, m.PointerConstant(&buffer),
          m.Int32Constant(0), phi, kNoWriteBarrier);
  m.Return(m.Int32Constant(magic));

  CHECK_EQ(magic, m.Call());
}


TEST(RunCountToTenAccRaw) {
  RawMachineAssemblerTester<int32_t> m;

  Node* zero = m.Int32Constant(0);
  Node* ten = m.Int32Constant(10);
  Node* one = m.Int32Constant(1);

  RawMachineLabel header, body, body_cont, end;

  m.Goto(&header);

  m.Bind(&header);
  Node* i = m.Phi(MachineRepresentation::kWord32, zero, zero);
  Node* j = m.Phi(MachineRepresentation::kWord32, zero, zero);
  m.Goto(&body);

  m.Bind(&body);
  Node* next_i = m.Int32Add(i, one);
  Node* next_j = m.Int32Add(j, one);
  m.Branch(m.Word32Equal(next_i, ten), &end, &body_cont);

  m.Bind(&body_cont);
  i->ReplaceInput(1, next_i);
  j->ReplaceInput(1, next_j);
  m.Goto(&header);

  m.Bind(&end);
  m.Return(ten);

  CHECK_EQ(10, m.Call());
}


TEST(RunCountToTenAccRaw2) {
  RawMachineAssemblerTester<int32_t> m;

  Node* zero = m.Int32Constant(0);
  Node* ten = m.Int32Constant(10);
  Node* one = m.Int32Constant(1);

  RawMachineLabel header, body, body_cont, end;

  m.Goto(&header);

  m.Bind(&header);
  Node* i = m.Phi(MachineRepresentation::kWord32, zero, zero);
  Node* j = m.Phi(MachineRepresentation::kWord32, zero, zero);
  Node* k = m.Phi(MachineRepresentation::kWord32, zero, zero);
  m.Goto(&body);

  m.Bind(&body);
  Node* next_i = m.Int32Add(i, one);
  Node* next_j = m.Int32Add(j, one);
  Node* next_k = m.Int32Add(j, one);
  m.Branch(m.Word32Equal(next_i, ten), &end, &body_cont);

  m.Bind(&body_cont);
  i->ReplaceInput(1, next_i);
  j->ReplaceInput(1, next_j);
  k->ReplaceInput(1, next_k);
  m.Goto(&header);

  m.Bind(&end);
  m.Return(ten);

  CHECK_EQ(10, m.Call());
}


TEST(RunAddTree) {
  RawMachineAssemblerTester<int32_t> m;
  int32_t inputs[] = {11, 12, 13, 14, 15, 16, 17, 18};

  Node* base = m.PointerConstant(inputs);
  Node* n0 =
      m.Load(MachineType::Int32(), base, m.Int32Constant(0 * sizeof(int32_t)));
  Node* n1 =
      m.Load(MachineType::Int32(), base, m.Int32Constant(1 * sizeof(int32_t)));
  Node* n2 =
      m.Load(MachineType::Int32(), base, m.Int32Constant(2 * sizeof(int32_t)));
  Node* n3 =
      m.Load(MachineType::Int32(), base, m.Int32Constant(3 * sizeof(int32_t)));
  Node* n4 =
      m.Load(MachineType::Int32(), base, m.Int32Constant(4 * sizeof(int32_t)));
  Node* n5 =
      m.Load(MachineType::Int32(), base, m.Int32Constant(5 * sizeof(int32_t)));
  Node* n6 =
      m.Load(MachineType::Int32(), base, m.Int32Constant(6 * sizeof(int32_t)));
  Node* n7 =
      m.Load(MachineType::Int32(), base, m.Int32Constant(7 * sizeof(int32_t)));

  Node* i1 = m.Int32Add(n0, n1);
  Node* i2 = m.Int32Add(n2, n3);
  Node* i3 = m.Int32Add(n4, n5);
  Node* i4 = m.Int32Add(n6, n7);

  Node* i5 = m.Int32Add(i1, i2);
  Node* i6 = m.Int32Add(i3, i4);

  Node* i7 = m.Int32Add(i5, i6);

  m.Return(i7);

  CHECK_EQ(116, m.Call());
}


static const int kFloat64CompareHelperTestCases = 15;
static const int kFloat64CompareHelperNodeType = 4;

static int Float64CompareHelper(RawMachineAssemblerTester<int32_t>* m,
                                int test_case, int node_type, double x,
                                double y) {
  static double buffer[2];
  buffer[0] = x;
  buffer[1] = y;
  CHECK(0 <= test_case && test_case < kFloat64CompareHelperTestCases);
  CHECK(0 <= node_type && node_type < kFloat64CompareHelperNodeType);
  CHECK(x < y);
  bool load_a = node_type / 2 == 1;
  bool load_b = node_type % 2 == 1;
  Node* a =
      load_a ? m->Load(MachineType::Float64(), m->PointerConstant(&buffer[0]))
             : m->Float64Constant(x);
  Node* b =
      load_b ? m->Load(MachineType::Float64(), m->PointerConstant(&buffer[1]))
             : m->Float64Constant(y);
  Node* cmp = nullptr;
  bool expected = false;
  switch (test_case) {
    // Equal tests.
    case 0:
      cmp = m->Float64Equal(a, b);
      expected = false;
      break;
    case 1:
      cmp = m->Float64Equal(a, a);
      expected = true;
      break;
    // LessThan tests.
    case 2:
      cmp = m->Float64LessThan(a, b);
      expected = true;
      break;
    case 3:
      cmp = m->Float64LessThan(b, a);
      expected = false;
      break;
    case 4:
      cmp = m->Float64LessThan(a, a);
      expected = false;
      break;
    // LessThanOrEqual tests.
    case 5:
      cmp = m->Float64LessThanOrEqual(a, b);
      expected = true;
      break;
    case 6:
      cmp = m->Float64LessThanOrEqual(b, a);
      expected = false;
      break;
    case 7:
      cmp = m->Float64LessThanOrEqual(a, a);
      expected = true;
      break;
    // NotEqual tests.
    case 8:
      cmp = m->Float64NotEqual(a, b);
      expected = true;
      break;
    case 9:
      cmp = m->Float64NotEqual(b, a);
      expected = true;
      break;
    case 10:
      cmp = m->Float64NotEqual(a, a);
      expected = false;
      break;
    // GreaterThan tests.
    case 11:
      cmp = m->Float64GreaterThan(a, a);
      expected = false;
      break;
    case 12:
      cmp = m->Float64GreaterThan(a, b);
      expected = false;
      break;
    // GreaterThanOrEqual tests.
    case 13:
      cmp = m->Float64GreaterThanOrEqual(a, a);
      expected = true;
      break;
    case 14:
      cmp = m->Float64GreaterThanOrEqual(b, a);
      expected = true;
      break;
    default:
      UNREACHABLE();
  }
  m->Return(cmp);
  return expected;
}


TEST(RunFloat64Compare) {
  double inf = V8_INFINITY;
  // All pairs (a1, a2) are of the form a1 < a2.
  double inputs[] = {0.0,  1.0,  -1.0, 0.22, -1.22, 0.22,
                     -inf, 0.22, 0.22, inf,  -inf,  inf};

  for (int test = 0; test < kFloat64CompareHelperTestCases; test++) {
    for (int node_type = 0; node_type < kFloat64CompareHelperNodeType;
         node_type++) {
      for (size_t input = 0; input < arraysize(inputs); input += 2) {
        RawMachineAssemblerTester<int32_t> m;
        int expected = Float64CompareHelper(&m, test, node_type, inputs[input],
                                            inputs[input + 1]);
        CHECK_EQ(expected, m.Call());
      }
    }
  }
}


TEST(RunFloat64UnorderedCompare) {
  RawMachineAssemblerTester<int32_t> m;

  const Operator* operators[] = {m.machine()->Float64Equal(),
                                 m.machine()->Float64LessThan(),
                                 m.machine()->Float64LessThanOrEqual()};

  double nan = std::numeric_limits<double>::quiet_NaN();

  FOR_FLOAT64_INPUTS(i) {
    for (size_t o = 0; o < arraysize(operators); ++o) {
      for (int j = 0; j < 2; j++) {
        RawMachineAssemblerTester<int32_t> t;
        Node* a = t.Float64Constant(i);
        Node* b = t.Float64Constant(nan);
        if (j == 1) std::swap(a, b);
        t.Return(t.AddNode(operators[o], a, b));
        CHECK_EQ(0, t.Call());
      }
    }
  }
}


TEST(RunFloat64Equal) {
  double input_a = 0.0;
  double input_b = 0.0;

  RawMachineAssemblerTester<int32_t> m;
  Node* a = m.LoadFromPointer(&input_a, MachineType::Float64());
  Node* b = m.LoadFromPointer(&input_b, MachineType::Float64());
  m.Return(m.Float64Equal(a, b));

  CompareWrapper cmp(IrOpcode::kFloat64Equal);
  FOR_FLOAT64_INPUTS(pl) {
    FOR_FLOAT64_INPUTS(pr) {
      input_a = pl;
      input_b = pr;
      int32_t expected = cmp.Float64Compare(input_a, input_b) ? 1 : 0;
      CHECK_EQ(expected, m.Call());
    }
  }
}


TEST(RunFloat64LessThan) {
  double input_a = 0.0;
  double input_b = 0.0;

  RawMachineAssemblerTester<int32_t> m;
  Node* a = m.LoadFromPointer(&input_a, MachineType::Float64());
  Node* b = m.LoadFromPointer(&input_b, MachineType::Float64());
  m.Return(m.Float64LessThan(a, b));

  CompareWrapper cmp(IrOpcode::kFloat64LessThan);
  FOR_FLOAT64_INPUTS(pl) {
    FOR_FLOAT64_INPUTS(pr) {
      input_a = pl;
      input_b = pr;
      int32_t expected = cmp.Float64Compare(input_a, input_b) ? 1 : 0;
      CHECK_EQ(expected, m.Call());
    }
  }
}


static void IntPtrCompare(intptr_t left, intptr_t right) {
  for (int test = 0; test < 7; test++) {
    RawMachineAssemblerTester<bool> m(MachineType::Pointer(),
                                      MachineType::Pointer());
    Node* p0 = m.Parameter(0);
    Node* p1 = m.Parameter(1);
    Node* res = nullptr;
    bool expected = false;
    switch (test) {
      case 0:
        res = m.IntPtrLessThan(p0, p1);
        expected = true;
        break;
      case 1:
        res = m.IntPtrLessThanOrEqual(p0, p1);
        expected = true;
        break;
      case 2:
        res = m.IntPtrEqual(p0, p1);
        expected = false;
        break;
      case 3:
        res = m.IntPtrGreaterThanOrEqual(p0, p1);
        expected = false;
        break;
      case 4:
        res = m.IntPtrGreaterThan(p0, p1);
        expected = false;
        break;
      case 5:
        res = m.IntPtrEqual(p0, p0);
        expected = true;
        break;
      case 6:
        res = m.IntPtrNotEqual(p0, p1);
        expected = true;
        break;
      default:
        UNREACHABLE();
    }
    m.Return(res);
    CHECK_EQ(expected, m.Call(reinterpret_cast<int32_t*>(left),
                              reinterpret_cast<int32_t*>(right)));
  }
}


TEST(RunIntPtrCompare) {
  intptr_t min = std::numeric_limits<intptr_t>::min();
  intptr_t max = std::numeric_limits<intptr_t>::max();
  // An ascending chain of intptr_t
  intptr_t inputs[] = {min, min / 2, -1, 0, 1, max / 2, max};
  for (size_t i = 0; i < arraysize(inputs) - 1; i++) {
    IntPtrCompare(inputs[i], inputs[i + 1]);
  }
}


TEST(RunTestIntPtrArithmetic) {
  static const int kInputSize = 10;
  int32_t inputs[kInputSize];
  int32_t outputs[kInputSize];
  for (int i = 0; i < kInputSize; i++) {
    inputs[i] = i;
    outputs[i] = -1;
  }
  RawMachineAssemblerTester<int32_t*> m;
  Node* input = m.PointerConstant(&inputs[0]);
  Node* output = m.PointerConstant(&outputs[kInputSize - 1]);
  Node* elem_size = m.IntPtrConstant(sizeof(inputs[0]));
  for (int i = 0; i < kInputSize; i++) {
    m.Store(MachineRepresentation::kWord32, output,
            m.Load(MachineType::Int32(), input), kNoWriteBarrier);
    input = m.IntPtrAdd(input, elem_size);
    output = m.IntPtrSub(output, elem_size);
  }
  m.Return(input);
  CHECK_EQ(&inputs[kInputSize], m.Call());
  for (int i = 0; i < kInputSize; i++) {
    CHECK_EQ(i, inputs[i]);
    CHECK_EQ(kInputSize - i - 1, outputs[i]);
  }
}


TEST(RunSpillLotsOfThings) {
  static const int kInputSize = 1000;
  RawMachineAssemblerTester<int32_t> m;
  Node* accs[kInputSize];
  int32_t outputs[kInputSize];
  Node* one = m.Int32Constant(1);
  Node* acc = one;
  for (int i = 0; i < kInputSize; i++) {
    acc = m.Int32Add(acc, one);
    accs[i] = acc;
  }
  for (int i = 0; i < kInputSize; i++) {
    m.StoreToPointer(&outputs[i], MachineRepresentation::kWord32, accs[i]);
  }
  m.Return(one);
  m.Call();
  for (int i = 0; i < kInputSize; i++) {
    CHECK_EQ(outputs[i], i + 2);
  }
}


TEST(RunSpillConstantsAndParameters) {
  static const int kInputSize = 1000;
  static const int32_t kBase = 987;
  RawMachineAssemblerTester<int32_t> m(MachineType::Int32(),
                                       MachineType::Int32());
  int32_t outputs[kInputSize];
  Node* csts[kInputSize];
  Node* accs[kInputSize];
  Node* acc = m.Int32Constant(0);
  for (int i = 0; i < kInputSize; i++) {
    csts[i] = m.Int32Constant(base::AddWithWraparound(kBase, i));
  }
  for (int i = 0; i < kInputSize; i++) {
    acc = m.Int32Add(acc, csts[i]);
    accs[i] = acc;
  }
  for (int i = 0; i < kInputSize; i++) {
    m.StoreToPointer(&outputs[i], MachineRepresentation::kWord32, accs[i]);
  }
  m.Return(m.Int32Add(acc, m.Int32Add(m.Parameter(0), m.Parameter(1))));
  FOR_INT32_INPUTS(i) {
    FOR_INT32_INPUTS(j) {
      int32_t expected = base::AddWithWraparound(i, j);
      for (int k = 0; k < kInputSize; k++) {
        expected = base::AddWithWraparound(expected, kBase + k);
      }
      CHECK_EQ(expected, m.Call(i, j));
      expected = 0;
      for (int k = 0; k < kInputSize; k++) {
        expected += kBase + k;
        CHECK_EQ(expected, outputs[k]);
      }
    }
  }
}


TEST(RunNewSpaceConstantsInPhi) {
  RawMachineAssemblerTester<Tagged<Object>> m(MachineType::Int32());

  Isolate* isolate = CcTest::i_isolate();
  Handle<HeapNumber> true_val = isolate->factory()->NewHeapNumber(11.2);
  Handle<HeapNumber> false_val = isolate->factory()->NewHeapNumber(11.3);
  Node* true_node = m.HeapConstant(true_val);
  Node* false_node = m.HeapConstant(false_val);

  RawMachineLabel blocka, blockb, end;
  m.Branch(m.Parameter(0), &blocka, &blockb);
  m.Bind(&blocka);
  m.Goto(&end);
  m.Bind(&blockb);
  m.Goto(&end);

  m.Bind(&end);
  Node* phi = m.Phi(MachineRepresentation::kTagged, true_node, false_node);
  m.Return(phi);

  CHECK_EQ(*false_val, m.Call(0));
  CHECK_EQ(*true_val, m.Call(1));
}


TEST(RunInt32AddWithOverflowP) {
  int32_t actual_val = -1;
  RawMachineAssemblerTester<int32_t> m;
  Int32BinopTester bt(&m);
  Node* add = m.Int32AddWithOverflow(bt.param0, bt.param1);
  Node* val = m.Projection(0, add);
  Node* ovf = m.Projection(1, add);
  m.StoreToPointer(&actual_val, MachineRepresentation::kWord32, val);
  bt.AddReturn(ovf);
  FOR_INT32_INPUTS(i) {
    FOR_INT32_INPUTS(j) {
      int32_t expected_val;
      int expected_ovf = base::bits::SignedAddOverflow32(i, j, &expected_val);
      CHECK_EQ(expected_ovf, bt.call(i, j));
      CHECK_EQ(expected_val, actual_val);
    }
  }
}


TEST(RunInt32AddWithOverflowImm) {
  int32_t actual_val = -1, expected_val = 0;
  FOR_INT32_INPUTS(i) {
    {
      RawMachineAssemblerTester<int32_t> m(MachineType::Int32());
      Node* add = m.Int32AddWithOverflow(m.Int32Constant(i), m.Parameter(0));
      Node* val = m.Projection(0, add);
      Node* ovf = m.Projection(1, add);
      m.StoreToPointer(&actual_val, MachineRepresentation::kWord32, val);
      m.Return(ovf);
      FOR_INT32_INPUTS(j) {
        int expected_ovf = base::bits::SignedAddOverflow32(i, j, &expected_val);
        CHECK_EQ(expected_ovf, m.Call(j));
        CHECK_EQ(expected_val, actual_val);
      }
    }
    {
      RawMachineAssemblerTester<int32_t> m(MachineType::Int32());
      Node* add = m.Int32AddWithOverflow(m.Parameter(0), m.Int32Constant(i));
      Node* val = m.Projection(0, add);
      Node* ovf = m.Projection(1, add);
      m.StoreToPointer(&actual_val, MachineRepresentation::kWord32, val);
      m.Return(ovf);
      FOR_INT32_INPUTS(j) {
        int expected_ovf = base::bits::SignedAddOverflow32(i, j, &expected_val);
        CHECK_EQ(expected_ovf, m.Call(j));
        CHECK_EQ(expected_val, actual_val);
      }
    }
    FOR_INT32_INPUTS(j) {
      RawMachineAssemblerTester<int32_t> m;
      Node* add =
          m.Int32AddWithOverflow(m.Int32Constant(i), m.Int32Constant(j));
      Node* val = m.Projection(0, add);
      Node* ovf = m.Projection(1, add);
      m.StoreToPointer(&actual_val, MachineRepresentation::kWord32, val);
      m.Return(ovf);
      int expected_ovf = base::bits::SignedAddOverflow32(i, j, &expected_val);
      CHECK_EQ(expected_ovf, m.Call());
      CHECK_EQ(expected_val, actual_val);
    }
  }
}


TEST(RunInt32AddWithOverflowInBranchP) {
  int constant = 911777;
  RawMachineLabel blocka, blockb;
  RawMachineAssemblerTester<int32_t> m;
  Int32BinopTester bt(&m);
  Node* add = m.Int32AddWithOverflow(bt.param0, bt.param1);
  Node* ovf = m.Projection(1, add);
  m.Branch(ovf, &blocka, &blockb);
  m.Bind(&blocka);
  bt.AddReturn(m.Int32Constant(constant));
  m.Bind(&blockb);
  Node* val = m.Projection(0, add);
  bt.AddReturn(val);
  FOR_INT32_INPUTS(i) {
    FOR_INT32_INPUTS(j) {
      int32_t expected;
      if (base::bits::SignedAddOverflow32(i, j, &expected)) expected = constant;
      CHECK_EQ(expected, bt.call(i, j));
    }
  }
}


TEST(RunInt32SubWithOverflowP) {
  int32_t actual_val = -1;
  RawMachineAssemblerTester<int32_t> m;
  Int32BinopTester bt(&m);
  Node* add = m.Int32SubWithOverflow(bt.param0, bt.param1);
  Node* val = m.Projection(0, add);
  Node* ovf = m.Projection(1, add);
  m.StoreToPointer(&actual_val, MachineRepresentation::kWord32, val);
  bt.AddReturn(ovf);
  FOR_INT32_INPUTS(i) {
    FOR_INT32_INPUTS(j) {
      int32_t expected_val;
      int expected_ovf = base::bits::SignedSubOverflow32(i, j, &expected_val);
      CHECK_EQ(expected_ovf, bt.call(i, j));
      CHECK_EQ(expected_val, actual_val);
    }
  }
}


TEST(RunInt32SubWithOverflowImm) {
  int32_t actual_val = -1, expected_val = 0;
  FOR_INT32_INPUTS(i) {
    {
      RawMachineAssemblerTester<int32_t> m(MachineType::Int32());
      Node* add = m.Int32SubWithOverflow(m.Int32Constant(i), m.Parameter(0));
      Node* val = m.Projection(0, add);
      Node* ovf = m.Projection(1, add);
      m.StoreToPointer(&actual_val, MachineRepresentation::kWord32, val);
      m.Return(ovf);
      FOR_INT32_INPUTS(j) {
        int expected_ovf = base::bits::SignedSubOverflow32(i, j, &expected_val);
        CHECK_EQ(expected_ovf, m.Call(j));
        CHECK_EQ(expected_val, actual_val);
      }
    }
    {
      RawMachineAssemblerTester<int32_t> m(MachineType::Int32());
      Node* add = m.Int32SubWithOverflow(m.Parameter(0), m.Int32Constant(i));
      Node* val = m.Projection(0, add);
      Node* ovf = m.Projection(1, add);
      m.StoreToPointer(&actual_val, MachineRepresentation::kWord32, val);
      m.Return(ovf);
      FOR_INT32_INPUTS(j) {
        int expected_ovf = base::bits::SignedSubOverflow32(j, i, &expected_val);
        CHECK_EQ(expected_ovf, m.Call(j));
        CHECK_EQ(expected_val, actual_val);
      }
    }
    FOR_INT32_INPUTS(j) {
      RawMachineAssemblerTester<int32_t> m;
      Node* add =
          m.Int32SubWithOverflow(m.Int32Constant(i), m.Int32Constant(j));
      Node* val = m.Projection(0, add);
      Node* ovf = m.Projection(1, add);
      m.StoreToPointer(&actual_val, MachineRepresentation::kWord32, val);
      m.Return(ovf);
      int expected_ovf = base::bits::SignedSubOverflow32(i, j, &expected_val);
      CHECK_EQ(expected_ovf, m.Call());
      CHECK_EQ(expected_val, actual_val);
    }
  }
}


TEST(RunInt32SubWithOverflowInBranchP) {
  int constant = 911999;
  RawMachineLabel blocka, blockb;
  RawMachineAssemblerTester<int32_t> m;
  Int32BinopTester bt(&m);
  Node* sub = m.Int32SubWithOverflow(bt.param0, bt.param1);
  Node* ovf = m.Projection(1, sub);
  m.Branch(ovf, &blocka, &blockb);
  m.Bind(&blocka);
  bt.AddReturn(m.Int32Constant(constant));
  m.Bind(&blockb);
  Node* val = m.Projection(0, sub);
  bt.AddReturn(val);
  FOR_INT32_INPUTS(i) {
    FOR_INT32_INPUTS(j) {
      int32_t expected;
      if (base::bits::SignedSubOverflow32(i, j, &expected)) expected = constant;
      CHECK_EQ(expected, bt.call(i, j));
    }
  }
}

TEST(RunInt32MulWithOverflowP) {
  int32_t actual_val = -1;
  RawMachineAssemblerTester<int32_t> m;
  Int32BinopTester bt(&m);
  Node* add = m.Int32MulWithOverflow(bt.param0, bt.param1);
  Node* val = m.Projection(0, add);
  Node* ovf = m.Projection(1, add);
  m.StoreToPointer(&actual_val, MachineRepresentation::kWord32, val);
  bt.AddReturn(ovf);
  FOR_INT32_INPUTS(i) {
    FOR_INT32_INPUTS(j) {
      int32_t expected_val;
      int expected_ovf = base::bits::SignedMulOverflow32(i, j, &expected_val);
      CHECK_EQ(expected_ovf, bt.call(i, j));
      if (!expected_ovf) {
        CHECK_EQ(expected_val, actual_val);
      }
    }
  }
}

TEST(RunInt32MulWithOverflowImm) {
  int32_t actual_val = -1, expected_val = 0;
  FOR_INT32_INPUTS(i) {
    {
      RawMachineAssemblerTester<int32_t> m(MachineType::Int32());
      Node* add = m.Int32MulWithOverflow(m.Int32Constant(i), m.Parameter(0));
      Node* val = m.Projection(0, add);
      Node* ovf = m.Projection(1, add);
      m.StoreToPointer(&actual_val, MachineRepresentation::kWord32, val);
      m.Return(ovf);
      FOR_INT32_INPUTS(j) {
        int expected_ovf = base::bits::SignedMulOverflow32(i, j, &expected_val);
        CHECK_EQ(expected_ovf, m.Call(j));
        if (!expected_ovf) {
          CHECK_EQ(expected_val, actual_val);
        }
      }
    }
    {
      RawMachineAssemblerTester<int32_t> m(MachineType::Int32());
      Node* add = m.Int32MulWithOverflow(m.Parameter(0), m.Int32Constant(i));
      Node* val = m.Projection(0, add);
      Node* ovf = m.Projection(1, add);
      m.StoreToPointer(&actual_val, MachineRepresentation::kWord32, val);
      m.Return(ovf);
      FOR_INT32_INPUTS(j) {
        int expected_ovf = base::bits::SignedMulOverflow32(i, j, &expected_val);
        CHECK_EQ(expected_ovf, m.Call(j));
        if (!expected_ovf) {
          CHECK_EQ(expected_val, actual_val);
        }
      }
    }
    FOR_INT32_INPUTS(j) {
      RawMachineAssemblerTester<int32_t> m;
      Node* add =
          m.Int32MulWithOverflow(m.Int32Constant(i), m.Int32Constant(j));
      Node* val = m.Projection(0, add);
      Node* ovf = m.Projection(1, add);
      m.StoreToPointer(&actual_val, MachineRepresentation::kWord32, val);
      m.Return(ovf);
      int expected_ovf = base::bits::SignedMulOverflow32(i, j, &expected_val);
      CHECK_EQ(expected_ovf, m.Call());
      if (!expected_ovf) {
        CHECK_EQ(expected_val, actual_val);
      }
    }
  }
}

TEST(RunInt32MulWithOverflowInBranchP) {
  int constant = 911777;
  RawMachineLabel blocka, blockb;
  RawMachineAssemblerTester<int32_t> m;
  Int32BinopTester bt(&m);
  Node* add = m.Int32MulWithOverflow(bt.param0, bt.param1);
  Node* ovf = m.Projection(1, add);
  m.Branch(ovf, &blocka, &blockb);
  m.Bind(&blocka);
  bt.AddReturn(m.Int32Constant(constant));
  m.Bind(&blockb);
  Node* val = m.Projection(0, add);
  bt.AddReturn(val);
  FOR_INT32_INPUTS(i) {
    FOR_INT32_INPUTS(j) {
      int32_t expected;
      if (base::bits::SignedMulOverflow32(i, j, &expected)) expected = constant;
      CHECK_EQ(expected, bt.call(i, j));
    }
  }
}

TEST(RunWord64EqualInBranchP) {
  int64_t input;
  RawMachineLabel blocka, blockb;
  RawMachineAssemblerTester<int32_t> m;
  if (!m.machine()->Is64()) return;
  Node* value = m.LoadFromPointer(&input, MachineType::Int64());
  m.Branch(m.Word64Equal(value, m.Int64Constant(0)), &blocka, &blockb);
  m.Bind(&blocka);
  m.Return(m.Int32Constant(1));
  m.Bind(&blockb);
  m.Return(m.Int32Constant(2));
  input = int64_t{0};
  CHECK_EQ(1, m.Call());
  input = int64_t{1};
  CHECK_EQ(2, m.Call());
  input = int64_t{0x100000000};
  CHECK_EQ(2, m.Call());
}


TEST(RunChangeInt32ToInt64P) {
  if (kSystemPointerSize < 8) return;
  int64_t actual = -1;
  RawMachineAssemblerTester<int32_t> m(MachineType::Int32());
  m.StoreToPointer(&actual, MachineRepresentation::kWord64,
                   m.ChangeInt32ToInt64(m.Parameter(0)));
  m.Return(m.Int32Constant(0));
  FOR_INT32_INPUTS(i) {
    int64_t expected = i;
    CHECK_EQ(0, m.Call(i));
    CHECK_EQ(expected, actual);
  }
}


TEST(RunChangeUint32ToUint64P) {
  if (kSystemPointerSize < 8) return;
  int64_t actual = -1;
  RawMachineAssemblerTester<int32_t> m(MachineType::Uint32());
  m.StoreToPointer(&actual, MachineRepresentation::kWord64,
                   m.ChangeUint32ToUint64(m.Parameter(0)));
  m.Return(m.Int32Constant(0));
  FOR_UINT32_INPUTS(i) {
    int64_t expected = static_cast<uint64_t>(i);
    CHECK_EQ(0, m.Call(i));
    CHECK_EQ(expected, actual);
  }
}


TEST(RunTruncateInt64ToInt32P) {
  if (kSystemPointerSize < 8) return;
  int64_t expected = -1;
  RawMachineAssemblerTester<int32_t> m;
  m.Return(m.TruncateInt64ToInt32(
      m.LoadFromPointer(&expected, MachineType::Int64())));
  FOR_UINT32_INPUTS(i) {
    FOR_UINT32_INPUTS(j) {
      expected = (static_cast<uint64_t>(j) << 32) | i;
      CHECK_EQ(static_cast<int32_t>(expected), m.Call());
    }
  }
}

TEST(RunTruncateFloat64ToWord32P) {
  struct {
    double from;
    double raw;
  } kValues[] = {{0, 0},
                 {0.5, 0},
                 {-0.5, 0},
                 {1.5, 1},
                 {-1.5, -1},
                 {5.5, 5},
                 {-5.0, -5},
                 {std::numeric_limits<double>::quiet_NaN(), 0},
                 {std::numeric_limits<double>::infinity(), 0},
                 {-std::numeric_limits<double>::quiet_NaN(), 0},
                 {-std::numeric_limits<double>::infinity(), 0},
                 {4.94065645841e-324, 0},
                 {-4.94065645841e-324, 0},
                 {0.9999999999999999, 0},
                 {-0.9999999999999999, 0},
                 {4294967296.0, 0},
                 {-4294967296.0, 0},
                 {9223372036854775000.0, 4294966272.0},
                 {-9223372036854775000.0, -4294966272.0},
                 {4.5036e+15, 372629504},
                 {-4.5036e+15, -372629504},
                 {287524199.5377777, 0x11234567},
                 {-287524199.5377777, -0x11234567},
                 {2300193596.302222, 2300193596.0},
                 {-2300193596.302222, -2300193596.0},
                 {4600387192.604444, 305419896},
                 {-4600387192.604444, -305419896},
                 {4823855600872397.0, 1737075661},
                 {-4823855600872397.0, -1737075661},
                 {4503603922337791.0, -1},
                 {-4503603922337791.0, 1},
                 {4503601774854143.0, 2147483647},
                 {-4503601774854143.0, -2147483647},
                 {9007207844675582.0, -2},
                 {-9007207844675582.0, 2},
                 {2.4178527921507624e+24, -536870912},
                 {-2.4178527921507624e+24, 536870912},
                 {2.417853945072267e+24, -536870912},
                 {-2.417853945072267e+24, 536870912},
                 {4.8357055843015248e+24, -1073741824},
                 {-4.8357055843015248e+24, 1073741824},
                 {4.8357078901445341e+24, -1073741824},
                 {-4.8357078901445341e+24, 1073741824},
                 {2147483647.0, 2147483647.0},
                 {-2147483648.0, -2147483648.0},
                 {9.6714111686030497e+24, -2147483648.0},
                 {-9.6714111686030497e+24, -2147483648.0},
                 {9.6714157802890681e+24, -2147483648.0},
                 {-9.6714157802890681e+24, -2147483648.0},
                 {1.9342813113834065e+25, 2147483648.0},
                 {-1.9342813113834065e+25, 2147483648.0},
                 {3.868562622766813e+25, 0},
                 {-3.868562622766813e+25, 0},
                 {1.7976931348623157e+308, 0},
                 {-1.7976931348623157e+308, 0}};
  double input = -1.0;
  RawMachineAssemblerTester<int32_t> m;
  m.Return(m.TruncateFloat64ToWord32(
      m.LoadFromPointer(&input, MachineType::Float64())));
  for (size_t i = 0; i < arraysize(kValues); ++i) {
    input = kValues[i].from;
    uint64_t expected = static_cast<int64_t>(kValues[i].raw);
    CHECK_EQ(static_cast<int>(expected), m.Call());
  }
}

TEST(RunTruncateFloat64ToWord32SignExtension) {
  BufferedRawMachineAssemblerTester<int32_t> r;
  r.Return(r.Int32Sub(r.TruncateFloat64ToWord32(r.Float64Constant(-1.0)),
                      r.Int32Constant(0)));
  CHECK_EQ(-1, r.Call());
}

TEST(RunChangeFloat32ToFloat64) {
  BufferedRawMachineAssemblerTester<double> m(MachineType::Float32());

  m.Return(m.ChangeFloat32ToFloat64(m.Parameter(0)));

  FOR_FLOAT32_INPUTS(i) { CHECK_DOUBLE_EQ(static_cast<double>(i), m.Call(i)); }
}


TEST(RunFloat32Constant) {
  FOR_FLOAT32_INPUTS(i) {
    BufferedRawMachineAssemblerTester<float> m;
    m.Return(m.Float32Constant(i));
    CHECK_FLOAT_EQ(i, m.Call());
  }
}


TEST(RunFloat64ExtractLowWord32) {
  BufferedRawMachineAssemblerTester<uint32_t> m(MachineType::Float64());
  m.Return(m.Float64ExtractLowWord32(m.Parameter(0)));
  FOR_FLOAT64_INPUTS(i) {
    uint32_t expected = static_cast<uint32_t>(base::bit_cast<uint64_t>(i));
    CHECK_EQ(expected, m.Call(i));
  }
}


TEST(RunFloat64ExtractHighWord32) {
  BufferedRawMachineAssemblerTester<uint32_t> m(MachineType::Float64());
  m.Return(m.Float64ExtractHighWord32(m.Parameter(0)));
  FOR_FLOAT64_INPUTS(i) {
    uint32_t expected =
        static_cast<uint32_t>(base::bit_cast<uint64_t>(i) >> 32);
    CHECK_EQ(expected, m.Call(i));
  }
}


TEST(RunFloat64InsertLowWord32) {
  BufferedRawMachineAssemblerTester<double> m(MachineType::Float64(),
                                              MachineType::Int32());
  m.Return(m.Float64InsertLowWord32(m.Parameter(0), m.Parameter(1)));
  FOR_FLOAT64_INPUTS(i) {
    FOR_INT32_INPUTS(j) {
      double expected = base::bit_cast<double>(
          (base::bit_cast<uint64_t>(i) & ~(uint64_t{0xFFFFFFFF})) |
          (static_cast<uint64_t>(base::bit_cast<uint32_t>(j))));
      CHECK_DOUBLE_EQ(expected, m.Call(i, j));
    }
  }
}


TEST(RunFloat64InsertHighWord32) {
  BufferedRawMachineAssemblerTester<double> m(MachineType::Float64(),
                                              MachineType::Uint32());
  m.Return(m.Float64InsertHighWord32(m.Parameter(0), m.Parameter(1)));
  FOR_FLOAT64_INPUTS(i) {
    FOR_UINT32_INPUTS(j) {
      uint64_t expected = (base::bit_cast<uint64_t>(i) & 0xFFFFFFFF) |
                          (static_cast<uint64_t>(j) << 32);

      CHECK_DOUBLE_EQ(base::bit_cast<double>(expected), m.Call(i, j));
    }
  }
}


TEST(RunFloat32Abs) {
  BufferedRawMachineAssemblerTester<float> m(MachineType::Float32());
  m.Return(m.Float32Abs(m.Parameter(0)));
  FOR_FLOAT32_INPUTS(i) { CHECK_FLOAT_EQ(std::abs(i), m.Call(i)); }
}


TEST(RunFloat64Abs) {
  BufferedRawMachineAssemblerTester<double> m(MachineType::Float64());
  m.Return(m.Float64Abs(m.Parameter(0)));
  FOR_FLOAT64_INPUTS(i) { CHECK_DOUBLE_EQ(std::abs(i), m.Call(i)); }
}

TEST(RunFloat64Acos) {
  BufferedRawMachineAssemblerTester<double> m(MachineType::Float64());
  m.Return(m.Float64Acos(m.Parameter(0)));
  FOR_FLOAT64_INPUTS(i) { CHECK_DOUBLE_EQ(base::ieee754::acos(i), m.Call(i)); }
}

TEST(RunFloat64Acosh) {
  BufferedRawMachineAssemblerTester<double> m(MachineType::Float64());
  m.Return(m.Float64Acosh(m.Parameter(0)));
  FOR_FLOAT64_INPUTS(i) { CHECK_DOUBLE_EQ(base::ieee754::acosh(i), m.Call(i)); }
}

TEST(RunFloat64Asin) {
  BufferedRawMachineAssemblerTester<double> m(MachineType::Float64());
  m.Return(m.Float64Asin(m.Parameter(0)));
  FOR_FLOAT64_INPUTS(i) { CHECK_DOUBLE_EQ(base::ieee754::asin(i), m.Call(i)); }
}

TEST(RunFloat64Asinh) {
  BufferedRawMachineAssemblerTester<double> m(MachineType::Float64());
  m.Return(m.Float64Asinh(m.Parameter(0)));
  FOR_FLOAT64_INPUTS(i) { CHECK_DOUBLE_EQ(base::ieee754::asinh(i), m.Call(i)); }
}

TEST(RunFloat64Atan) {
  BufferedRawMachineAssemblerTester<double> m(MachineType::Float64());
  m.Return(m.Float64Atan(m.Parameter(0)));
  CHECK(std::isnan(m.Call(std::numeric_limits<double>::quiet_NaN())));
  CHECK(std::isnan(m.Call(std::numeric_limits<double>::signaling_NaN())));
  CHECK_DOUBLE_EQ(-0.0, m.Call(-0.0));
  CHECK_DOUBLE_EQ(0.0, m.Call(0.0));
  FOR_FLOAT64_INPUTS(i) { CHECK_DOUBLE_EQ(base::ieee754::atan(i), m.Call(i)); }
}

TEST(RunFloat64Atanh) {
  BufferedRawMachineAssemblerTester<double> m(MachineType::Float64());
  m.Return(m.Float64Atanh(m.Parameter(0)));
  CHECK(std::isnan(m.Call(std::numeric_limits<double>::quiet_NaN())));
  CHECK(std::isnan(m.Call(std::numeric_limits<double>::signaling_NaN())));
  CHECK_DOUBLE_EQ(std::numeric_limits<double>::infinity(), m.Call(1.0));
  CHECK_DOUBLE_EQ(-std::numeric_limits<double>::infinity(), m.Call(-1.0));
  CHECK_DOUBLE_EQ(-0.0, m.Call(-0.0));
  CHECK_DOUBLE_EQ(0.0, m.Call(0.0));
  FOR_FLOAT64_INPUTS(i) { CHECK_DOUBLE_EQ(base::ieee754::atanh(i), m.Call(i)); }
}

TEST(RunFloat64Atan2) {
  BufferedRawMachineAssemblerTester<double> m(MachineType::Float64(),
                                              MachineType::Float64());
  m.Return(m.Float64Atan2(m.Parameter(0), m.Parameter(1)));
  FOR_FLOAT64_INPUTS(i) {
    FOR_FLOAT64_INPUTS(j) {
      CHECK_DOUBLE_EQ(base::ieee754::atan2(i, j), m.Call(i, j));
    }
  }
}

TEST(RunFloat64Cos) {
  BufferedRawMachineAssemblerTester<double> m(MachineType::Float64());
  m.Return(m.Float64Cos(m.Parameter(0)));
  CHECK(std::isnan(m.Call(std::numeric_limits<double>::quiet_NaN())));
  CHECK(std::isnan(m.Call(std::numeric_limits<double>::signaling_NaN())));
  FOR_FLOAT64_INPUTS(i) { CHECK_DOUBLE_EQ(COS_IMPL(i), m.Call(i)); }
}

TEST(RunFloat64Cosh) {
  BufferedRawMachineAssemblerTester<double> m(MachineType::Float64());
  m.Return(m.Float64Cosh(m.Parameter(0)));
  CHECK(std::isnan(m.Call(std::numeric_limits<double>::quiet_NaN())));
  CHECK(std::isnan(m.Call(std::numeric_limits<double>::signaling_NaN())));
  FOR_FLOAT64_INPUTS(i) { CHECK_DOUBLE_EQ(base::ieee754::cosh(i), m.Call(i)); }
}

TEST(RunFloat64Exp) {
  BufferedRawMachineAssemblerTester<double> m(MachineType::Float64());
  m.Return(m.Float64Exp(m.Parameter(0)));
  CHECK(std::isnan(m.Call(std::numeric_limits<double>::quiet_NaN())));
  CHECK(std::isnan(m.Call(std::numeric_limits<double>::signaling_NaN())));
  CHECK_EQ(0.0, m.Call(-std::numeric_limits<double>::infinity()));
  CHECK_DOUBLE_EQ(1.0, m.Call(-0.0));
  CHECK_DOUBLE_EQ(1.0, m.Call(0.0));
  CHECK_DOUBLE_EQ(std::numeric_limits<double>::infinity(),
                  m.Call(std::numeric_limits<double>::infinity()));
  FOR_FLOAT64_INPUTS(i) { CHECK_DOUBLE_EQ(base::ieee754::exp(i), m.Call(i)); }
}

TEST(RunFloat64Expm1) {
  BufferedRawMachineAssemblerTester<double> m(MachineType::Float64());
  m.Return(m.Float64Expm1(m.Parameter(0)));
  CHECK(std::isnan(m.Call(std::numeric_limits<double>::quiet_NaN())));
  CHECK(std::isnan(m.Call(std::numeric_limits<double>::signaling_NaN())));
  CHECK_EQ(-1.0, m.Call(-std::numeric_limits<double>::infinity()));
  CHECK_DOUBLE_EQ(std::numeric_limits<double>::infinity(),
                  m.Call(std::numeric_limits<double>::infinity()));
  FOR_FLOAT64_INPUTS(i) { CHECK_DOUBLE_EQ(base::ieee754::expm1(i), m.Call(i)); }
}

TEST(RunFloat64Log) {
  BufferedRawMachineAssemblerTester<double> m(MachineType::Float64());
  m.Return(m.Float64Log(m.Parameter(0)));
  CHECK(std::isnan(m.Call(std::numeric_limits<double>::quiet_NaN())));
  CHECK(std::isnan(m.Call(std::numeric_limits<double>::signaling_NaN())));
  CHECK(std::isnan(m.Call(-std::numeric_limits<double>::infinity())));
  CHECK(std::isnan(m.Call(-1.0)));
  CHECK_DOUBLE_EQ(-std::numeric_limits<double>::infinity(), m.Call(-0.0));
  CHECK_DOUBLE_EQ(-std::numeric_limits<double>::infinity(), m.Call(0.0));
  CHECK_DOUBLE_EQ(0.0, m.Call(1.0));
  CHECK_DOUBLE_EQ(std::numeric_limits<double>::infinity(),
                  m.Call(std::numeric_limits<double>::infinity()));
  FOR_FLOAT64_INPUTS(i) { CHECK_DOUBLE_EQ(base::ieee754::log(i), m.Call(i)); }
}

TEST(RunFloat64Log1p) {
  BufferedRawMachineAssemblerTester<double> m(MachineType::Float64());
  m.Return(m.Float64Log1p(m.Parameter(0)));
  CHECK(std::isnan(m.Call(std::numeric_limits<double>::quiet_NaN())));
  CHECK(std::isnan(m.Call(std::numeric_limits<double>::signaling_NaN())));
  CHECK(std::isnan(m.Call(-std::numeric_limits<double>::infinity())));
  CHECK_DOUBLE_EQ(-std::numeric_limits<double>::infinity(), m.Call(-1.0));
  CHECK_DOUBLE_EQ(0.0, m.Call(0.0));
  CHECK_DOUBLE_EQ(-0.0, m.Call(-0.0));
  CHECK_DOUBLE_EQ(std::numeric_limits<double>::infinity(),
                  m.Call(std::numeric_limits<double>::infinity()));
  FOR_FLOAT64_INPUTS(i) { CHECK_DOUBLE_EQ(base::ieee754::log1p(i), m.Call(i)); }
}

TEST(RunFloat64Log2) {
  BufferedRawMachineAssemblerTester<double> m(MachineType::Float64());
  m.Return(m.Float64Log2(m.Parameter(0)));
  CHECK(std::isnan(m.Call(std::numeric_limits<double>::quiet_NaN())));
  CHECK(std::isnan(m.Call(std::numeric_limits<double>::signaling_NaN())));
  CHECK(std::isnan(m.Call(-std::numeric_limits<double>::infinity())));
  CHECK(std::isnan(m.Call(-1.0)));
  CHECK_DOUBLE_EQ(-std::numeric_limits<double>::infinity(), m.Call(-0.0));
  CHECK_DOUBLE_EQ(-std::numeric_limits<double>::infinity(), m.Call(0.0));
  CHECK_DOUBLE_EQ(0.0, m.Call(1.0));
  CHECK_DOUBLE_EQ(std::numeric_limits<double>::infinity(),
                  m.Call(std::numeric_limits<double>::infinity()));
  FOR_FLOAT64_INPUTS(i) { CHECK_DOUBLE_EQ(base::ieee754::log2(i), m.Call(i)); }
}

TEST(RunFloat64Log10) {
  BufferedRawMachineAssemblerTester<double> m(MachineType::Float64());
  m.Return(m.Float64Log10(m.Parameter(0)));
  CHECK(std::isnan(m.Call(std::numeric_limits<double>::quiet_NaN())));
  CHECK(std::isnan(m.Call(std::numeric_limits<double>::signaling_NaN())));
  CHECK(std::isnan(m.Call(-std::numeric_limits<double>::infinity())));
  CHECK(std::isnan(m.Call(-1.0)));
  CHECK_DOUBLE_EQ(-std::numeric_limits<double>::infinity(), m.Call(-0.0));
  CHECK_DOUBLE_EQ(-std::numeric_limits<double>::infinity(), m.Call(0.0));
  CHECK_DOUBLE_EQ(std::numeric_limits<double>::infinity(),
                  m.Call(std::numeric_limits<double>::infinity()));
  FOR_FLOAT64_INPUTS(i) { CHECK_DOUBLE_EQ(base::ieee754::log10(i), m.Call(i)); }
}

TEST(RunFloat64Cbrt) {
  BufferedRawMachineAssemblerTester<double> m(MachineType::Float64());
  m.Return(m.Float64Cbrt(m.Parameter(0)));
  CHECK(std::isnan(m.Call(std::numeric_limits<double>::quiet_NaN())));
  CHECK(std::isnan(m.Call(std::numeric_limits<double>::signaling_NaN())));
  CHECK_DOUBLE_EQ(std::numeric_limits<double>::infinity(),
                  m.Call(std::numeric_limits<double>::infinity()));
  CHECK_DOUBLE_EQ(-std::numeric_limits<double>::infinity(),
                  m.Call(-std::numeric_limits<double>::infinity()));
  FOR_FLOAT64_INPUTS(i) { CHECK_DOUBLE_EQ(base::ieee754::cbrt(i), m.Call(i)); }
}

TEST(RunFloat64Sin) {
  BufferedRawMachineAssemblerTester<double> m(MachineType::Float64());
  m.Return(m.Float64Sin(m.Parameter(0)));
  CHECK(std::isnan(m.Call(std::numeric_limits<double>::quiet_NaN())));
  CHECK(std::isnan(m.Call(std::numeric_limits<double>::signaling_NaN())));
  FOR_FLOAT64_INPUTS(i) { CHECK_DOUBLE_EQ(SIN_IMPL(i), m.Call(i)); }
}

TEST(RunFloat64Sinh) {
  BufferedRawMachineAssemblerTester<double> m(MachineType::Float64());
  m.Return(m.Float64Sinh(m.Parameter(0)));
  CHECK(std::isnan(m.Call(std::numeric_limits<double>::quiet_NaN())));
  CHECK(std::isnan(m.Call(std::numeric_limits<double>::signaling_NaN())));
  FOR_FLOAT64_INPUTS(i) { CHECK_DOUBLE_EQ(base::ieee754::sinh(i), m.Call(i)); }
}

TEST(RunFloat64Tan) {
  BufferedRawMachineAssemblerTester<double> m(MachineType::Float64());
  m.Return(m.Float64Tan(m.Parameter(0)));
  CHECK(std::isnan(m.Call(std::numeric_limits<double>::quiet_NaN())));
  CHECK(std::isnan(m.Call(std::numeric_limits<double>::signaling_NaN())));
  FOR_FLOAT64_INPUTS(i) { CHECK_DOUBLE_EQ(base::ieee754::tan(i), m.Call(i)); }
}

TEST(RunFloat64Tanh) {
  BufferedRawMachineAssemblerTester<double> m(MachineType::Float64());
  m.Return(m.Float64Tanh(m.Parameter(0)));
  CHECK(std::isnan(m.Call(std::numeric_limits<double>::quiet_NaN())));
  CHECK(std::isnan(m.Call(std::numeric_limits<double>::signaling_NaN())));
  FOR_FLOAT64_INPUTS(i) { CHECK_DOUBLE_EQ(base::ieee754::tanh(i), m.Call(i)); }
}

static double two_30 = 1 << 30;             // 2^30 is a smi boundary.
static double two_52 = two_30 * (1 << 22);  // 2^52 is a precision boundary.
static double kValues[] = {0.1,
                           0.2,
                           0.49999999999999994,
                           0.5,
                           0.7,
                           1.0 - std::numeric_limits<double>::epsilon(),
                           -0.1,
                           -0.49999999999999994,
                           -0.5,
                           -0.7,
                           1.1,
                           1.0 + std::numeric_limits<double>::epsilon(),
                           1.5,
                           1.7,
                           -1,
                           -1 + std::numeric_limits<double>::epsilon(),
                           -1 - std::numeric_limits<double>::epsilon(),
                           -1.1,
                           -1.5,
                           -1.7,
                           std::numeric_limits<double>::min(),
                           -std::numeric_limits<double>::min(),
                           std::numeric_limits<double>::max(),
                           -std::numeric_limits<double>::max(),
                           std::numeric_limits<double>::infinity(),
                           -std::numeric_limits<double>::infinity(),
                           two_30,
                           two_30 + 0.1,
                           two_30 + 0.5,
                           two_30 + 0.7,
                           two_30 - 1,
                           two_30 - 1 + 0.1,
                           two_30 - 1 + 0.5,
                           two_30 - 1 + 0.7,
                           -two_30,
                           -two_30 + 0.1,
                           -two_30 + 0.5,
                           -two_30 + 0.7,
                           -two_30 + 1,
                           -two_30 + 1 + 0.1,
                           -two_30 + 1 + 0.5,
                           -two_30 + 1 + 0.7,
                           two_52,
                           two_52 + 0.1,
                           two_52 + 0.5,
                           two_52 + 0.5,
                           two_52 + 0.7,
                           two_52 + 0.7,
                           two_52 - 1,
                           two_52 - 1 + 0.1,
                           two_52 - 1 + 0.5,
                           two_52 - 1 + 0.7,
                           -two_52,
                           -two_52 + 0.1,
                           -two_52 + 0.5,
                           -two_52 + 0.7,
                           -two_52 + 1,
                           -two_52 + 1 + 0.1,
                           -two_52 + 1 + 0.5,
                           -two_52 + 1 + 0.7,
                           two_30,
                           two_30 - 0.1,
                           two_30 - 0.5,
                           two_30 - 0.7,
                           two_30 - 1,
                           two_30 - 1 - 0.1,
                           two_30 - 1 - 0.5,
                           two_30 - 1 - 0.7,
                           -two_30,
                           -two_30 - 0.1,
                           -two_30 - 0.5,
                           -two_30 - 0.7,
                           -two_30 + 1,
                           -two_30 + 1 - 0.1,
                           -two_30 + 1 - 0.5,
                           -two_30 + 1 - 0.7,
                           two_52,
                           two_52 - 0.1,
                           two_52 - 0.5,
                           two_52 - 0.5,
                           two_52 - 0.7,
                           two_52 - 0.7,
                           two_52 - 1,
                           two_52 - 1 - 0.1,
                           two_52 - 1 - 0.5,
                           two_52 - 1 - 0.7,
                           -two_52,
                           -two_52 - 0.1,
                           -two_52 - 0.5,
                           -two_52 - 0.7,
                           -two_52 + 1,
                           -two_52 + 1 - 0.1,
                           -two_52 + 1 - 0.5,
                           -two_52 + 1 - 0.7};


TEST(RunFloat32RoundDown) {
  BufferedRawMachineAssemblerTester<float> m(MachineType::Float32());
  if (!m.machine()->Float32RoundDown().IsSupported()) return;

  m.Return(m.Float32RoundDown(m.Parameter(0)));

  FOR_FLOAT32_INPUTS(i) { CHECK_FLOAT_EQ(floorf(i), m.Call(i)); }
}


TEST(RunFloat64RoundDown1) {
  BufferedRawMachineAssemblerTester<double> m(MachineType::Float64());
  if (!m.machine()->Float64RoundDown().IsSupported()) return;

  m.Return(m.Float64RoundDown(m.Parameter(0)));

  FOR_FLOAT64_INPUTS(i) { CHECK_DOUBLE_EQ(floor(i), m.Call(i)); }
}


TEST(RunFloat64RoundDown2) {
  BufferedRawMachineAssemblerTester<double> m(MachineType::Float64());
  if (!m.machine()->Float64RoundDown().IsSupported()) return;
  m.Return(m.Float64Sub(m.Float64Constant(-0.0),
                        m.Float64RoundDown(m.Float64Sub(m.Float64Constant(-0.0),
                                                        m.Parameter(0)))));

  for (size_t i = 0; i < arraysize(kValues); ++i) {
    CHECK_EQ(ceil(kValues[i]), m.Call(kValues[i]));
  }
}


TEST(RunFloat32RoundUp) {
  BufferedRawMachineAssemblerTester<float> m(MachineType::Float32());
  if (!m.machine()->Float32RoundUp().IsSupported()) return;
  m.Return(m.Float32RoundUp(m.Parameter(0)));

  FOR_FLOAT32_INPUTS(i) { CHECK_FLOAT_EQ(ceilf(i), m.Call(i)); }
}


TEST(RunFloat64RoundUp) {
  BufferedRawMachineAssemblerTester<double> m(MachineType::Float64());
  if (!m.machine()->Float64RoundUp().IsSupported()) return;
  m.Return(m.Float64RoundUp(m.Parameter(0)));

  FOR_FLOAT64_INPUTS(i) { CHECK_DOUBLE_EQ(ceil(i), m.Call(i)); }
}


TEST(RunFloat32RoundTiesEven) {
  BufferedRawMachineAssemblerTester<float> m(MachineType::Float32());
  if (!m.machine()->Float32RoundTiesEven().IsSupported()) return;
  m.Return(m.Float32RoundTiesEven(m.Parameter(0)));

  FOR_FLOAT32_INPUTS(i) { CHECK_FLOAT_EQ(nearbyint(i), m.Call(i)); }
}


TEST(RunFloat64RoundTiesEven) {
  BufferedRawMachineAssemblerTester<double> m(MachineType::Float64());
  if (!m.machine()->Float64RoundTiesEven().IsSupported()) return;
  m.Return(m.Float64RoundTiesEven(m.Parameter(0)));

  FOR_FLOAT64_INPUTS(i) { CHECK_DOUBLE_EQ(nearbyint(i), m.Call(i)); }
}


TEST(RunFloat32RoundTruncate) {
  BufferedRawMachineAssemblerTester<float> m(MachineType::Float32());
  if (!m.machine()->Float32RoundTruncate().IsSupported()) return;

  m.Return(m.Float32RoundTruncate(m.Parameter(0)));

  FOR_FLOAT32_INPUTS(i) { CHECK_FLOAT_EQ(truncf(i), m.Call(i)); }
}


TEST(RunFloat64RoundTruncate) {
  BufferedRawMachineAssemblerTester<double> m(MachineType::Float64());
  if (!m.machine()->Float64RoundTruncate().IsSupported()) return;
  m.Return(m.Float64RoundTruncate(m.Parameter(0)));
  for (size_t i = 0; i < arraysize(kValues); ++i) {
    CHECK_EQ(trunc(kValues[i]), m.Call(kValues[i]));
  }
}


TEST(RunFloat64RoundTiesAway) {
  BufferedRawMachineAssemblerTester<double> m(MachineType::Float64());
  if (!m.machine()->Float64RoundTiesAway().IsSupported()) return;
  m.Return(m.Float64RoundTiesAway(m.Parameter(0)));
  for (size_t i = 0; i < arraysize(kValues); ++i) {
    CHECK_EQ(round(kValues[i]), m.Call(kValues[i]));
  }
}


#if !USE_SIMULATOR

namespace {

int32_t const kMagicFoo0 = 0xDEADBEEF;

int32_t foo0() { return kMagicFoo0; }


int32_t foo1(int32_t x) { return x; }

int32_t foo2(int32_t x, int32_t y) { return base::SubWithWraparound(x, y); }

uint32_t foo8(uint32_t a, uint32_t b, uint32_t c, uint32_t d, uint32_t e,
              uint32_t f, uint32_t g, uint32_t h) {
  return a + b + c + d + e + f + g + h;
}

uint32_t foo9(uint32_t a, uint32_t b, uint32_t c, uint32_t d, uint32_t e,
              uint32_t f, uint32_t g, uint32_t h, uint32_t i) {
  return a + b + c + d + e + f + g + h + i;
}

}  // namespace


TEST(RunCallCFunction0) {
  auto* foo0_ptr = &foo0;
  RawMachineAssemblerTester<int32_t> m;
  Node* function = m.LoadFromPointer(&foo0_ptr, MachineType::Pointer());
  m.Return(m.CallCFunction(function, MachineType::Int32()));
  CHECK_EQ(kMagicFoo0, m.Call());
}


TEST(RunCallCFunction1) {
  auto* foo1_ptr = &foo1;
  RawMachineAssemblerTester<int32_t> m(MachineType::Int32());
  Node* function = m.LoadFromPointer(&foo1_ptr, MachineType::Pointer());
  m.Return(
      m.CallCFunction(function, MachineType::Int32(),
                      std::make_pair(MachineType::Int32(), m.Parameter(0))));
  FOR_INT32_INPUTS(i) {
    int32_t const expected = i;
    CHECK_EQ(expected, m.Call(expected));
  }
}


TEST(RunCallCFunction2) {
  auto* foo2_ptr = &foo2;
  RawMachineAssemblerTester<int32_t> m(MachineType::Int32(),
                                       MachineType::Int32());
  Node* function = m.LoadFromPointer(&foo2_ptr, MachineType::Pointer());
  m.Return(
      m.CallCFunction(function, MachineType::Int32(),
                      std::make_pair(MachineType::Int32(), m.Parameter(0)),
                      std::make_pair(MachineType::Int32(), m.Parameter(1))));
  FOR_INT32_INPUTS(i) {
    int32_t const x = i;
    FOR_INT32_INPUTS(j) {
      int32_t const y = j;
      CHECK_EQ(base::SubWithWraparound(x, y), m.Call(x, y));
    }
  }
}


TEST(RunCallCFunction8) {
  auto* foo8_ptr = &foo8;
  RawMachineAssemblerTester<int32_t> m(MachineType::Int32());
  Node* function = m.LoadFromPointer(&foo8_ptr, MachineType::Pointer());
  Node* param = m.Parameter(0);
  m.Return(m.CallCFunction(function, MachineType::Int32(),
      std::make_pair(MachineType::Int32(), param),
      std::make_pair(MachineType::Int32(), param),
      std::make_pair(MachineType::Int32(), param),
      std::make_pair(MachineType::Int32(), param),
      std::make_pair(MachineType::Int32(), param),
      std::make_pair(MachineType::Int32(), param),
      std::make_pair(MachineType::Int32(), param),
      std::make_pair(MachineType::Int32(), param)));
  FOR_INT32_INPUTS(i) {
    int32_t const x = i;
    CHECK_EQ(base::MulWithWraparound(x, 8), m.Call(x));
  }
}

TEST(RunCallCFunction9) {
  auto* foo9_ptr = &foo9;
  RawMachineAssemblerTester<int32_t> m(MachineType::Int32());
  Node* function = m.LoadFromPointer(&foo9_ptr, MachineType::Pointer());
  Node* param = m.Parameter(0);
  m.Return(
      m.CallCFunction(function, MachineType::Int32(),
                      std::make_pair(MachineType::Int32(), param),
                      std::make_pair(MachineType::Int32(),
                                     m.Int32Add(param, m.Int32Constant(1))),
                      std::make_pair(MachineType::Int32(),
                                     m.Int32Add(param, m.Int32Constant(2))),
                      std::make_pair(MachineType::Int32(),
                                     m.Int32Add(param, m.Int32Constant(3))),
                      std::make_pair(MachineType::Int32(),
                                     m.Int32Add(param, m.Int32Constant(4))),
                      std::make_pair(MachineType::Int32(),
                                     m.Int32Add(param, m.Int32Constant(5))),
                      std::make_pair(MachineType::Int32(),
                                     m.Int32Add(param, m.Int32Constant(6))),
                      std::make_pair(MachineType::Int32(),
                                     m.Int32Add(param, m.Int32Constant(7))),
                      std::make_pair(MachineType::Int32(),
                                     m.Int32Add(param, m.Int32Constant(8)))));
  FOR_INT32_INPUTS(i) {
    int32_t const x = i;
    CHECK_EQ(base::AddWithWraparound(base::MulWithWraparound(x, 9), 36),
             m.Call(x));
  }
}

#endif  // !USE_SIMULATOR

#ifdef V8_ENABLE_FP_PARAMS_IN_C_LINKAGE

namespace {

void CheckEqual(double expected, double actual) {
  if (std::isnan(expected)) {
    CHECK(std::isnan(actual));
  } else {
    CHECK_EQ(actual, expected);
  }
}

void CheckLessOrEqual(double actual, double expected) {
  if (std::isnan(expected)) {
    CHECK(std::isnan(actual));
  } else if (std::isnan(actual)) {
    return;
  } else {
    CHECK_LE(actual, expected);
  }
}

const double foo_result = 3.14;

#ifdef V8_USE_SIMULATOR_WITH_GENERIC_C_CALLS
union Int64OrDoubleUnion {
  int64_t int64_value;
  double double_value;
};

Int64OrDoubleUnion double_foo0() {
  Int64OrDoubleUnion ret;
  ret.double_value = foo_result;
  return ret;
}

Int64OrDoubleUnion double_foo1(Int64OrDoubleUnion x) {
  Int64OrDoubleUnion ret;
  ret.double_value = x.double_value;
  return ret;
}

Int64OrDoubleUnion double_foo2(Int64OrDoubleUnion x, Int64OrDoubleUnion y) {
  Int64OrDoubleUnion ret;
  ret.double_value = x.double_value * 10 + y.double_value;
  return ret;
}

Int64OrDoubleUnion double_foo8(Int64OrDoubleUnion a, Int64OrDoubleUnion b,
                               Int64OrDoubleUnion c, Int64OrDoubleUnion d,
                               Int64OrDoubleUnion e, Int64OrDoubleUnion f,
                               Int64OrDoubleUnion g, Int64OrDoubleUnion h) {
  Int64OrDoubleUnion ret;
  ret.double_value = a.double_value + b.double_value + c.double_value +
                     d.double_value + e.double_value + f.double_value +
                     g.double_value + h.double_value;
  return ret;
}

Int64OrDoubleUnion double_foo9(Int64OrDoubleUnion a, Int64OrDoubleUnion b,
                               Int64OrDoubleUnion c, Int64OrDoubleUnion d,
                               Int64OrDoubleUnion e, Int64OrDoubleUnion f,
                               Int64OrDoubleUnion g, Int64OrDoubleUnion h,
                               Int64OrDoubleUnion i) {
  Int64OrDoubleUnion ret;
  ret.double_value = a.double_value + b.double_value + c.double_value +
                     d.double_value + e.double_value + f.double_value +
                     g.double_value + h.double_value + i.double_value;
  return ret;
}

Int64OrDoubleUnion double_foo10(Int64OrDoubleUnion a, Int64OrDoubleUnion b,
                                Int64OrDoubleUnion c, Int64OrDoubleUnion d,
                                Int64OrDoubleUnion e, Int64OrDoubleUnion f,
                                Int64OrDoubleUnion g, Int64OrDoubleUnion h,
                                Int64OrDoubleUnion i, Int64OrDoubleUnion j) {
  Int64OrDoubleUnion ret;
  ret.double_value = a.double_value + b.double_value + c.double_value +
                     d.double_value + e.double_value + f.double_value +
                     g.double_value + h.double_value + i.double_value +
                     j.int64_value;
  return ret;
}

Int64OrDoubleUnion int_foo10(Int64OrDoubleUnion a, Int64OrDoubleUnion b,
                             Int64OrDoubleUnion c, Int64OrDoubleUnion d,
                             Int64OrDoubleUnion e, Int64OrDoubleUnion f,
                             Int64OrDoubleUnion g, Int64OrDoubleUnion h,
                             Int64OrDoubleUnion i, Int64OrDoubleUnion j) {
  Int64OrDoubleUnion ret;
  ret.double_value = a.int64_value + b.int64_value + c.int64_value +
                     d.int64_value + e.int64_value + f.int64_value +
                     g.int64_value + h.int64_value + i.int64_value +
                     j.double_value;
  return ret;
}
#else   // def V8_USE_SIMULATOR_WITH_GENERIC_C_CALLS
double double_foo0() { return foo_result; }

double double_foo1(double x) { return x; }

double double_foo2(double x, double y) { return x * 10 + y; }

double double_foo8(double a, double b, double c, double d, double e, double f,
                   double g, double h) {
  return a + b + c + d + e + f + g + h;
}

double double_foo9(double a, double b, double c, double d, double e, double f,
                   double g, double h, double i) {
  return a + b + c + d + e + f + g + h + i;
}

double double_foo10(double a, double b, double c, double d, double e, double f,
                    double g, double h, double i, int64_t j) {
  return a + b + c + d + e + f + g + h + i + j;
}

double int_foo10(int64_t a, int64_t b, int64_t c, int64_t d, int64_t e,
                 int64_t f, int64_t g, int64_t h, int64_t i, double j) {
  return a + b + c + d + e + f + g + h + i + j;
}
#endif  // V8_USE_SIMULATOR_WITH_GENERIC_C_CALLS

}  // namespace

TEST(RunCallDoubleCFunction0) {
  RawMachineAssemblerTester<double> m;
  ExternalReference ref = ExternalRefFromFunc(&m, FUNCTION_ADDR(double_foo0));

  Node* function = m.ExternalConstant(ref);
  m.Return(m.CallCFunction(function, MachineType::Float64()));
  CheckEqual(foo_result, m.Call());
}

TEST(RunCallDoubleCFunction1) {
  RawMachineAssemblerTester<double> m(MachineType::Float64());
  ExternalReference ref = ExternalRefFromFunc(&m, FUNCTION_ADDR(double_foo1));

  Node* function = m.ExternalConstant(ref);
  m.Return(
      m.CallCFunction(function, MachineType::Float64(),
                      std::make_pair(MachineType::Float64(), m.Parameter(0))));
  FOR_FLOAT64_INPUTS(x) { CheckEqual(x, m.Call(x)); }
}

TEST(RunCallDoubleCFunction2) {
  RawMachineAssemblerTester<double> m(MachineType::Float64(),
                                      MachineType::Float64());
  ExternalReference ref = ExternalRefFromFunc(&m, FUNCTION_ADDR(double_foo2));

  Node* function = m.ExternalConstant(ref);
  m.Return(
      m.CallCFunction(function, MachineType::Float64(),
                      std::make_pair(MachineType::Float64(), m.Parameter(0)),
                      std::make_pair(MachineType::Float64(), m.Parameter(1))));
  FOR_FLOAT64_INPUTS(x) {
    if (std::isnan(x)) continue;
    FOR_FLOAT64_INPUTS(y) { CheckEqual(x * 10 + y, m.Call(x, y)); }
  }
}

TEST(RunCallDoubleCFunction8) {
  RawMachineAssemblerTester<double> m(
      MachineType::Float64(), MachineType::Float64(), MachineType::Float64(),
      MachineType::Float64(), MachineType::Float64(), MachineType::Float64(),
      MachineType::Float64(), MachineType::Float64());
  ExternalReference ref = ExternalRefFromFunc(&m, FUNCTION_ADDR(double_foo8));

  Node* function = m.ExternalConstant(ref);
  Node* param = m.Parameter(0);
  m.Return(m.CallCFunction(function, MachineType::Float64(),
                           std::make_pair(MachineType::Float64(), param),
                           std::make_pair(MachineType::Float64(), param),
                           std::make_pair(MachineType::Float64(), param),
                           std::make_pair(MachineType::Float64(), param),
                           std::make_pair(MachineType::Float64(), param),
                           std::make_pair(MachineType::Float64(), param),
                           std::make_pair(MachineType::Float64(), param),
                           std::make_pair(MachineType::Float64(), param)));
  FOR_FLOAT64_INPUTS(x) {
    double diff = std::fabs(x * 8.0 - m.Call(x));
    CheckLessOrEqual(diff, std::numeric_limits<double>::epsilon());
  }
}

TEST(RunCallDoubleCFunction9) {
  RawMachineAssemblerTester<double> m(
      MachineType::Float64(), MachineType::Float64(), MachineType::Float64(),
      MachineType::Float64(), MachineType::Float64(), MachineType::Float64(),
      MachineType::Float64(), MachineType::Float64(), MachineType::Float64());
  ExternalReference ref = ExternalRefFromFunc(&m, FUNCTION_ADDR(double_foo9));

  Node* function = m.ExternalConstant(ref);
  Node* param = m.Parameter(0);
  m.Return(m.CallCFunction(
      function, MachineType::Float64(),
      std::make_pair(MachineType::Float64(), param),
      std::make_pair(MachineType::Float64(),
                     m.Float64Add(param, m.Float64Constant(1))),
      std::make_pair(MachineType::Float64(),
                     m.Float64Add(param, m.Float64Constant(2))),
      std::make_pair(MachineType::Float64(),
                     m.Float64Add(param, m.Float64Constant(3))),
      std::make_pair(MachineType::Float64(),
                     m.Float64Add(param, m.Float64Constant(4))),
      std::make_pair(MachineType::Float64(),
                     m.Float64Add(param, m.Float64Constant(5))),
      std::make_pair(MachineType::Float64(),
                     m.Float64Add(param, m.Float64Constant(6))),
      std::make_pair(MachineType::Float64(),
                     m.Float64Add(param, m.Float64Constant(7))),
      std::make_pair(MachineType::Float64(),
                     m.Float64Add(param, m.Float64Constant(8)))));
  FOR_FLOAT64_INPUTS(x) {
    double diff = x * 9.0 + 36.0 - m.Call(x);
    CheckLessOrEqual(diff, std::numeric_limits<double>::epsilon());
  }
}

TEST(RunCallDoubleCFunction10) {
  RawMachineAssemblerTester<double> m(
      MachineType::Float64(), MachineType::Float64(), MachineType::Float64(),
      MachineType::Float64(), MachineType::Float64(), MachineType::Float64(),
      MachineType::Float64(), MachineType::Float64(), MachineType::Float64(),
      MachineType::Int64());
  ExternalReference ref = ExternalRefFromFunc(&m, FUNCTION_ADDR(double_foo10));

  Node* function = m.ExternalConstant(ref);
  m.Return(
      m.CallCFunction(function, MachineType::Float64(),
                      std::make_pair(MachineType::Float64(), m.Parameter(0)),
                      std::make_pair(MachineType::Float64(), m.Parameter(1)),
                      std::make_pair(MachineType::Float64(), m.Parameter(2)),
                      std::make_pair(MachineType::Float64(), m.Parameter(3)),
                      std::make_pair(MachineType::Float64(), m.Parameter(4)),
                      std::make_pair(MachineType::Float64(), m.Parameter(5)),
                      std::make_pair(MachineType::Float64(), m.Parameter(6)),
                      std::make_pair(MachineType::Float64(), m.Parameter(7)),
                      std::make_pair(MachineType::Float64(), m.Parameter(8)),
                      std::make_pair(MachineType::Int64(), m.Parameter(9))));
  FOR_INT64_INPUTS(x) {
    double c = m.Call(1.0, 2.0, 3.0, 4.0, 5.0, 6.0, 7.0, 8.0, 9.0, x);
    double diff = 45.0 + x - c;
    CheckLessOrEqual(fabs(diff), std::numeric_limits<double>::epsilon());
  }
}

TEST(RunCallIntCFunction10) {
  RawMachineAssemblerTester<double> m(
      MachineType::Int64(), MachineType::Int64(), MachineType::Int64(),
      MachineType::Int64(), MachineType::Int64(), MachineType::Int64(),
      MachineType::Int64(), MachineType::Int64(), MachineType::Int64(),
      MachineType::Float64());
  ExternalReference ref = ExternalRefFromFunc(&m, FUNCTION_ADDR(int_foo10));

  Node* function = m.ExternalConstant(ref);
  m.Return(
      m.CallCFunction(function, MachineType::Float64(),
                      std::make_pair(MachineType::Int64(), m.Parameter(0)),
                      std::make_pair(MachineType::Int64(), m.Parameter(1)),
                      std::make_pair(MachineType::Int64(), m.Parameter(2)),
                      std::make_pair(MachineType::Int64(), m.Parameter(3)),
                      std::make_pair(MachineType::Int64(), m.Parameter(4)),
                      std::make_pair(MachineType::Int64(), m.Parameter(5)),
                      std::make_pair(MachineType::Int64(), m.Parameter(6)),
                      std::make_pair(MachineType::Int64(), m.Parameter(7)),
                      std::make_pair(MachineType::Int64(), m.Parameter(8)),
                      std::make_pair(MachineType::Float64(), m.Parameter(9))));
  FOR_FLOAT64_INPUTS(x) {
    double c = m.Call(static_cast<int64_t>(1), static_cast<int64_t>(2),
                      static_cast<int64_t>(3), static_cast<int64_t>(4),
                      static_cast<int64_t>(5), static_cast<int64_t>(6),
                      static_cast<int64_t>(7), static_cast<int64_t>(8),
                      static_cast<int64_t>(9), x);
    double diff = 45.0 + x - c;
    CheckLessOrEqual(fabs(diff), std::numeric_limits<double>::epsilon());
  }
}
#endif  // V8_ENABLE_FP_PARAMS_IN_C_LINKAGE

#if V8_TARGET_ARCH_64_BIT
// TODO(titzer): run int64 tests on all platforms when supported.

TEST(RunChangeFloat64ToInt64) {
  BufferedRawMachineAssemblerTester<int64_t> m(MachineType::Float64());
  m.Return(m.ChangeFloat64ToInt64(m.Parameter(0)));

  FOR_FLOAT64_INPUTS(i) {
    if (base::IsValueInRangeForNumericType<int64_t>(i)) {
      CHECK_EQ(static_cast<int64_t>(i), m.Call(i));
    }
  }
}

TEST(RunChangeInt64ToFloat64) {
  BufferedRawMachineAssemblerTester<double> m(MachineType::Int64());
  m.Return(m.ChangeInt64ToFloat64(m.Parameter(0)));
  FOR_INT64_INPUTS(i) {
    double output = static_cast<double>(i);
    CHECK_EQ(output, m.Call(i));
  }
}

TEST(RunBitcastInt64ToFloat64) {
  int64_t input = 1;
  Float64 output;
  RawMachineAssemblerTester<int32_t> m;
  m.StoreToPointer(
      output.get_bits_address(), MachineRepresentation::kFloat64,
      m.BitcastInt64ToFloat64(m.LoadFromPointer(&input, MachineType::Int64())));
  m.Return(m.Int32Constant(11));
  FOR_INT64_INPUTS(i) {
    input = i;
    CHECK_EQ(11, m.Call());
    Float64 expected = Float64::FromBits(input);
    CHECK_EQ(expected.get_bits(), output.get_bits());
  }
}


TEST(RunBitcastFloat64ToInt64) {
  BufferedRawMachineAssemblerTester<int64_t> m(MachineType::Float64());

  m.Return(m.BitcastFloat64ToInt64(m.Parameter(0)));
  FOR_FLOAT64_INPUTS(i) { CHECK_EQ(base::bit_cast<int64_t>(i), m.Call(i)); }
}


TEST(RunTryTruncateFloat32ToInt64WithoutCheck) {
  BufferedRawMachineAssemblerTester<int64_t> m(MachineType::Float32());
  m.Return(m.Projection(0, m.TryTruncateFloat32ToInt64(m.Parameter(0))));

  FOR_INT64_INPUTS(i) {
    float input = static_cast<float>(i);
    if (input < static_cast<float>(INT64_MAX) &&
        input >= static_cast<float>(INT64_MIN)) {
      CHECK_EQ(static_cast<int64_t>(input), m.Call(input));
    }
  }
}


TEST(RunTryTruncateFloat32ToInt64WithCheck) {
  int64_t success = 0;
  BufferedRawMachineAssemblerTester<int64_t> m(MachineType::Float32());
  Node* trunc = m.TryTruncateFloat32ToInt64(m.Parameter(0));
  Node* val = m.Projection(0, trunc);
  Node* check = m.Projection(1, trunc);
  m.StoreToPointer(&success, MachineRepresentation::kWord64, check);
  m.Return(val);

  FOR_FLOAT32_INPUTS(i) {
    if (i < static_cast<float>(INT64_MAX) &&
        i >= static_cast<float>(INT64_MIN)) {
      CHECK_EQ(static_cast<int64_t>(i), m.Call(i));
      CHECK_NE(0, success);
    } else {
      m.Call(i);
      CHECK_EQ(0, success);
    }
  }
}


TEST(RunTryTruncateFloat64ToInt64WithoutCheck) {
  BufferedRawMachineAssemblerTester<int64_t> m(MachineType::Float64());
  m.Return(m.Projection(0, m.TryTruncateFloat64ToInt64(m.Parameter(0))));

  FOR_FLOAT64_INPUTS(i) {
    if (base::IsValueInRangeForNumericType<int64_t>(i)) {
      double input = static_cast<double>(i);
      CHECK_EQ(static_cast<int64_t>(input), m.Call(input));
    }
  }
}


TEST(RunTryTruncateFloat64ToInt64WithCheck) {
  int64_t success = 0;
  BufferedRawMachineAssemblerTester<int64_t> m(MachineType::Float64());
  Node* trunc = m.TryTruncateFloat64ToInt64(m.Parameter(0));
  Node* val = m.Projection(0, trunc);
  Node* check = m.Projection(1, trunc);
  m.StoreToPointer(&success, MachineRepresentation::kWord64, check);
  m.Return(val);

  FOR_FLOAT64_INPUTS(i) {
    if (i < static_cast<double>(INT64_MAX) &&
        i >= static_cast<double>(INT64_MIN)) {
      // Conversions within this range should succeed.
      CHECK_EQ(static_cast<int64_t>(i), m.Call(i));
      CHECK_NE(0, success);
    } else {
      m.Call(i);
      CHECK_EQ(0, success);
    }
  }
}


TEST(RunTryTruncateFloat32ToUint64WithoutCheck) {
  BufferedRawMachineAssemblerTester<uint64_t> m(MachineType::Float32());
  m.Return(m.Projection(0, m.TryTruncateFloat32ToUint64(m.Parameter(0))));

  FOR_UINT64_INPUTS(i) {
    float input = static_cast<float>(i);
    // This condition on 'input' is required because
    // static_cast<float>(UINT64_MAX) results in a value outside uint64 range.
    if (input < static_cast<float>(UINT64_MAX)) {
      CHECK_EQ(static_cast<uint64_t>(input), m.Call(input));
    }
  }
}


TEST(RunTryTruncateFloat32ToUint64WithCheck) {
  int64_t success = 0;
  BufferedRawMachineAssemblerTester<uint64_t> m(MachineType::Float32());
  Node* trunc = m.TryTruncateFloat32ToUint64(m.Parameter(0));
  Node* val = m.Projection(0, trunc);
  Node* check = m.Projection(1, trunc);
  m.StoreToPointer(&success, MachineRepresentation::kWord64, check);
  m.Return(val);

  FOR_FLOAT32_INPUTS(i) {
    if (i < static_cast<float>(UINT64_MAX) && i > -1.0) {
      // Conversions within this range should succeed.
      CHECK_EQ(static_cast<uint64_t>(i), m.Call(i));
      CHECK_NE(0, success);
    } else {
      m.Call(i);
      CHECK_EQ(0, success);
    }
  }
}


TEST(RunTryTruncateFloat64ToUint64WithoutCheck) {
  BufferedRawMachineAssemblerTester<uint64_t> m(MachineType::Float64());
  m.Return(m.Projection(0, m.TryTruncateFloat64ToUint64(m.Parameter(0))));

  FOR_UINT64_INPUTS(j) {
    double input = static_cast<double>(j);

    if (input < static_cast<float>(UINT64_MAX)) {
      CHECK_EQ(static_cast<uint64_t>(input), m.Call(input));
    }
  }
}


TEST(RunTryTruncateFloat64ToUint64WithCheck) {
  int64_t success = 0;
  BufferedRawMachineAssemblerTester<int64_t> m(MachineType::Float64());
  Node* trunc = m.TryTruncateFloat64ToUint64(m.Parameter(0));
  Node* val = m.Projection(0, trunc);
  Node* check = m.Projection(1, trunc);
  m.StoreToPointer(&success, MachineRepresentation::kWord64, check);
  m.Return(val);

  FOR_FLOAT64_INPUTS(i) {
    if (i < 18446744073709551616.0 && i > -1) {
      // Conversions within this range should succeed.
      CHECK_EQ(static_cast<uint64_t>(i), static_cast<uint64_t>(m.Call(i)));
      CHECK_NE(0, success);
    } else {
      m.Call(i);
      CHECK_EQ(0, success);
    }
  }
}


TEST(RunRoundInt64ToFloat32) {
  BufferedRawMachineAssemblerTester<float> m(MachineType::Int64());
  m.Return(m.RoundInt64ToFloat32(m.Parameter(0)));
  FOR_INT64_INPUTS(i) { CHECK_EQ(static_cast<float>(i), m.Call(i)); }
}


TEST(RunRoundInt64ToFloat64) {
  BufferedRawMachineAssemblerTester<double> m(MachineType::Int64());
  m.Return(m.RoundInt64ToFloat64(m.Parameter(0)));
  FOR_INT64_INPUTS(i) { CHECK_EQ(static_cast<double>(i), m.Call(i)); }
}


TEST(RunRoundUint64ToFloat64) {
  struct {
    uint64_t input;
    uint64_t expected;
  } values[] = {{0x0, 0x0},
                {0x1, 0x3FF0000000000000},
                {0xFFFFFFFF, 0x41EFFFFFFFE00000},
                {0x1B09788B, 0x41BB09788B000000},
                {0x4C5FCE8, 0x419317F3A0000000},
                {0xCC0DE5BF, 0x41E981BCB7E00000},
                {0x2, 0x4000000000000000},
                {0x3, 0x4008000000000000},
                {0x4, 0x4010000000000000},
                {0x5, 0x4014000000000000},
                {0x8, 0x4020000000000000},
                {0x9, 0x4022000000000000},
                {0xFFFFFFFFFFFFFFFF, 0x43F0000000000000},
                {0xFFFFFFFFFFFFFFFE, 0x43F0000000000000},
                {0xFFFFFFFFFFFFFFFD, 0x43F0000000000000},
                {0x100000000, 0x41F0000000000000},
                {0xFFFFFFFF00000000, 0x43EFFFFFFFE00000},
                {0x1B09788B00000000, 0x43BB09788B000000},
                {0x4C5FCE800000000, 0x439317F3A0000000},
                {0xCC0DE5BF00000000, 0x43E981BCB7E00000},
                {0x200000000, 0x4200000000000000},
                {0x300000000, 0x4208000000000000},
                {0x400000000, 0x4210000000000000},
                {0x500000000, 0x4214000000000000},
                {0x800000000, 0x4220000000000000},
                {0x900000000, 0x4222000000000000},
                {0x273A798E187937A3, 0x43C39D3CC70C3C9C},
                {0xECE3AF835495A16B, 0x43ED9C75F06A92B4},
                {0xB668ECC11223344, 0x43A6CD1D98224467},
                {0x9E, 0x4063C00000000000},
                {0x43, 0x4050C00000000000},
                {0xAF73, 0x40E5EE6000000000},
                {0x116B, 0x40B16B0000000000},
                {0x658ECC, 0x415963B300000000},
                {0x2B3B4C, 0x41459DA600000000},
                {0x88776655, 0x41E10EECCAA00000},
                {0x70000000, 0x41DC000000000000},
                {0x7200000, 0x419C800000000000},
                {0x7FFFFFFF, 0x41DFFFFFFFC00000},
                {0x56123761, 0x41D5848DD8400000},
                {0x7FFFFF00, 0x41DFFFFFC0000000},
                {0x761C4761EEEEEEEE, 0x43DD8711D87BBBBC},
                {0x80000000EEEEEEEE, 0x43E00000001DDDDE},
                {0x88888888DDDDDDDD, 0x43E11111111BBBBC},
                {0xA0000000DDDDDDDD, 0x43E40000001BBBBC},
                {0xDDDDDDDDAAAAAAAA, 0x43EBBBBBBBB55555},
                {0xE0000000AAAAAAAA, 0x43EC000000155555},
                {0xEEEEEEEEEEEEEEEE, 0x43EDDDDDDDDDDDDE},
                {0xFFFFFFFDEEEEEEEE, 0x43EFFFFFFFBDDDDE},
                {0xF0000000DDDDDDDD, 0x43EE0000001BBBBC},
                {0x7FFFFFDDDDDDDD, 0x435FFFFFF7777777},
                {0x3FFFFFAAAAAAAA, 0x434FFFFFD5555555},
                {0x1FFFFFAAAAAAAA, 0x433FFFFFAAAAAAAA},
                {0xFFFFF, 0x412FFFFE00000000},
                {0x7FFFF, 0x411FFFFC00000000},
                {0x3FFFF, 0x410FFFF800000000},
                {0x1FFFF, 0x40FFFFF000000000},
                {0xFFFF, 0x40EFFFE000000000},
                {0x7FFF, 0x40DFFFC000000000},
                {0x3FFF, 0x40CFFF8000000000},
                {0x1FFF, 0x40BFFF0000000000},
                {0xFFF, 0x40AFFE0000000000},
                {0x7FF, 0x409FFC0000000000},
                {0x3FF, 0x408FF80000000000},
                {0x1FF, 0x407FF00000000000},
                {0x3FFFFFFFFFFF, 0x42CFFFFFFFFFFF80},
                {0x1FFFFFFFFFFF, 0x42BFFFFFFFFFFF00},
                {0xFFFFFFFFFFF, 0x42AFFFFFFFFFFE00},
                {0x7FFFFFFFFFF, 0x429FFFFFFFFFFC00},
                {0x3FFFFFFFFFF, 0x428FFFFFFFFFF800},
                {0x1FFFFFFFFFF, 0x427FFFFFFFFFF000},
                {0x8000008000000000, 0x43E0000010000000},
                {0x8000008000000001, 0x43E0000010000000},
                {0x8000000000000400, 0x43E0000000000000},
                {0x8000000000000401, 0x43E0000000000001}};

  BufferedRawMachineAssemblerTester<double> m(MachineType::Uint64());
  m.Return(m.RoundUint64ToFloat64(m.Parameter(0)));

  for (size_t i = 0; i < arraysize(values); i++) {
    CHECK_EQ(base::bit_cast<double>(values[i].expected),
             m.Call(values[i].input));
  }
}


TEST(RunRoundUint64ToFloat32) {
  struct {
    uint64_t input;
    uint32_t expected;
  } values[] = {{0x0, 0x0},
                {0x1, 0x3F800000},
                {0xFFFFFFFF, 0x4F800000},
                {0x1B09788B, 0x4DD84BC4},
                {0x4C5FCE8, 0x4C98BF9D},
                {0xCC0DE5BF, 0x4F4C0DE6},
                {0x2, 0x40000000},
                {0x3, 0x40400000},
                {0x4, 0x40800000},
                {0x5, 0x40A00000},
                {0x8, 0x41000000},
                {0x9, 0x41100000},
                {0xFFFFFFFFFFFFFFFF, 0x5F800000},
                {0xFFFFFFFFFFFFFFFE, 0x5F800000},
                {0xFFFFFFFFFFFFFFFD, 0x5F800000},
                {0x0, 0x0},
                {0x100000000, 0x4F800000},
                {0xFFFFFFFF00000000, 0x5F800000},
                {0x1B09788B00000000, 0x5DD84BC4},
                {0x4C5FCE800000000, 0x5C98BF9D},
                {0xCC0DE5BF00000000, 0x5F4C0DE6},
                {0x200000000, 0x50000000},
                {0x300000000, 0x50400000},
                {0x400000000, 0x50800000},
                {0x500000000, 0x50A00000},
                {0x800000000, 0x51000000},
                {0x900000000, 0x51100000},
                {0x273A798E187937A3, 0x5E1CE9E6},
                {0xECE3AF835495A16B, 0x5F6CE3B0},
                {0xB668ECC11223344, 0x5D3668ED},
                {0x9E, 0x431E0000},
                {0x43, 0x42860000},
                {0xAF73, 0x472F7300},
                {0x116B, 0x458B5800},
                {0x658ECC, 0x4ACB1D98},
                {0x2B3B4C, 0x4A2CED30},
                {0x88776655, 0x4F087766},
                {0x70000000, 0x4EE00000},
                {0x7200000, 0x4CE40000},
                {0x7FFFFFFF, 0x4F000000},
                {0x56123761, 0x4EAC246F},
                {0x7FFFFF00, 0x4EFFFFFE},
                {0x761C4761EEEEEEEE, 0x5EEC388F},
                {0x80000000EEEEEEEE, 0x5F000000},
                {0x88888888DDDDDDDD, 0x5F088889},
                {0xA0000000DDDDDDDD, 0x5F200000},
                {0xDDDDDDDDAAAAAAAA, 0x5F5DDDDE},
                {0xE0000000AAAAAAAA, 0x5F600000},
                {0xEEEEEEEEEEEEEEEE, 0x5F6EEEEF},
                {0xFFFFFFFDEEEEEEEE, 0x5F800000},
                {0xF0000000DDDDDDDD, 0x5F700000},
                {0x7FFFFFDDDDDDDD, 0x5B000000},
                {0x3FFFFFAAAAAAAA, 0x5A7FFFFF},
                {0x1FFFFFAAAAAAAA, 0x59FFFFFD},
                {0xFFFFF, 0x497FFFF0},
                {0x7FFFF, 0x48FFFFE0},
                {0x3FFFF, 0x487FFFC0},
                {0x1FFFF, 0x47FFFF80},
                {0xFFFF, 0x477FFF00},
                {0x7FFF, 0x46FFFE00},
                {0x3FFF, 0x467FFC00},
                {0x1FFF, 0x45FFF800},
                {0xFFF, 0x457FF000},
                {0x7FF, 0x44FFE000},
                {0x3FF, 0x447FC000},
                {0x1FF, 0x43FF8000},
                {0x3FFFFFFFFFFF, 0x56800000},
                {0x1FFFFFFFFFFF, 0x56000000},
                {0xFFFFFFFFFFF, 0x55800000},
                {0x7FFFFFFFFFF, 0x55000000},
                {0x3FFFFFFFFFF, 0x54800000},
                {0x1FFFFFFFFFF, 0x54000000},
                {0x8000008000000000, 0x5F000000},
                {0x8000008000000001, 0x5F000001},
                {0x8000000000000400, 0x5F000000},
                {0x8000000000000401, 0x5F000000}};

  BufferedRawMachineAssemblerTester<float> m(MachineType::Uint64());
  m.Return(m.RoundUint64ToFloat32(m.Parameter(0)));

  for (size_t i = 0; i < arraysize(values); i++) {
    CHECK_EQ(base::bit_cast<float>(values[i].expected),
             m.Call(values[i].input));
  }
}


#endif


TEST(RunBitcastFloat32ToInt32) {
  float input = 32.25;
  RawMachineAssemblerTester<int32_t> m;
  m.Return(m.BitcastFloat32ToInt32(
      m.LoadFromPointer(&input, MachineType::Float32())));
  FOR_FLOAT32_INPUTS(i) {
    input = i;
    int32_t expected = base::bit_cast<int32_t>(input);
    CHECK_EQ(expected, m.Call());
  }
}


TEST(RunRoundInt32ToFloat32) {
  BufferedRawMachineAssemblerTester<float> m(MachineType::Int32());
  m.Return(m.RoundInt32ToFloat32(m.Parameter(0)));
  FOR_INT32_INPUTS(i) {
    volatile float expected = static_cast<float>(i);
    CHECK_EQ(expected, m.Call(i));
  }
}


TEST(RunRoundUint32ToFloat32) {
  BufferedRawMachineAssemblerTester<float> m(MachineType::Uint32());
  m.Return(m.RoundUint32ToFloat32(m.Parameter(0)));
  FOR_UINT32_INPUTS(i) {
    volatile float expected = static_cast<float>(i);
    CHECK_EQ(expected, m.Call(i));
  }
}


TEST(RunBitcastInt32ToFloat32) {
  int32_t input = 1;
  Float32 output;
  RawMachineAssemblerTester<int32_t> m;
  m.StoreToPointer(
      output.get_bits_address(), MachineRepresentation::kFloat32,
      m.BitcastInt32ToFloat32(m.LoadFromPointer(&input, MachineType::Int32())));
  m.Return(m.Int32Constant(11));
  FOR_INT32_INPUTS(i) {
    input = i;
    CHECK_EQ(11, m.Call());
    Float32 expected = Float32::FromBits(input);
    CHECK_EQ(expected.get_bits(), output.get_bits());
  }
}


TEST(RunComputedCodeObject) {
  RawMachineAssemblerTester<int32_t> a;
  a.Return(a.Int32Constant(33));
  CHECK_EQ(33, a.Call());

  RawMachineAssemblerTester<int32_t> b;
  b.Return(b.Int32Constant(44));
  CHECK_EQ(44, b.Call());

  RawMachineAssemblerTester<int32_t> r(MachineType::Int32());
  RawMachineLabel tlabel;
  RawMachineLabel flabel;
  RawMachineLabel merge;
  r.Branch(r.Parameter(0), &tlabel, &flabel);
  r.Bind(&tlabel);
  Node* fa = r.HeapConstant(a.GetCode());
  r.Goto(&merge);
  r.Bind(&flabel);
  Node* fb = r.HeapConstant(b.GetCode());
  r.Goto(&merge);
  r.Bind(&merge);
  Node* phi = r.Phi(MachineRepresentation::kWord32, fa, fb);

  // TODO(titzer): all this descriptor hackery is just to call the above
  // functions as code objects instead of direct addresses.
  CSignatureOf<int32_t> sig;
  CallDescriptor* c = Linkage::GetSimplifiedCDescriptor(r.zone(), &sig);
  LinkageLocation ret[] = {c->GetReturnLocation(0)};
  Signature<LinkageLocation> loc(1, 0, ret);
  auto call_descriptor = r.zone()->New<CallDescriptor>(  // --
      CallDescriptor::kCallCodeObject,                   // kind
      kDefaultCodeEntrypointTag,                         // tag
      MachineType::AnyTagged(),                          // target_type
      c->GetInputLocation(0),                            // target_loc
      &loc,                                              // location_sig
      0,                                                 // stack count
      Operator::kNoProperties,                           // properties
      c->CalleeSavedRegisters(),                         // callee saved
      c->CalleeSavedFPRegisters(),                       // callee saved FP
      CallDescriptor::kNoFlags,                          // flags
      "c-call-as-code");
  Node* call = r.AddNode(r.common()->Call(call_descriptor), phi);
  r.Return(call);

  CHECK_EQ(33, r.Call(1));
  CHECK_EQ(44, r.Call(0));
}

TEST(ParentFramePointer) {
  RawMachineAssemblerTester<int32_t> r(MachineType::Int32());
  RawMachineLabel tlabel;
  RawMachineLabel flabel;
  RawMachineLabel merge;
  Node* frame = r.LoadFramePointer();
  Node* parent_frame = r.LoadParentFramePointer();
  frame = r.Load(MachineType::IntPtr(), frame);
  r.Branch(r.WordEqual(frame, parent_frame), &tlabel, &flabel);
  r.Bind(&tlabel);
  Node* fa = r.Int32Constant(1);
  r.Goto(&merge);
  r.Bind(&flabel);
  Node* fb = r.Int32Constant(0);
  r.Goto(&merge);
  r.Bind(&merge);
  Node* phi = r.Phi(MachineRepresentation::kWord32, fa, fb);
  r.Return(phi);
  CHECK_EQ(1, r.Call(1));
}

#if V8_TARGET_ARCH_64_BIT

TEST(Regression5923) {
  {
    BufferedRawMachineAssemblerTester<int64_t> m(MachineType::Int64());
    m.Return(m.Int64Add(
        m.Word64Shr(m.Parameter(0), m.Int64Constant(4611686018427387888)),
        m.Parameter(0)));
    int64_t input = 16;
    m.Call(input);
  }
  {
    BufferedRawMachineAssemblerTester<int64_t> m(MachineType::Int64());
    m.Return(m.Int64Add(
        m.Parameter(0),
        m.Word64Shr(m.Parameter(0), m.Int64Constant(4611686018427387888))));
    int64_t input = 16;
    m.Call(input);
  }
}

TEST(Regression5951) {
  BufferedRawMachineAssemblerTester<int64_t> m(MachineType::Int64());
  m.Return(m.Word64And(m.Word64Shr(m.Parameter(0), m.Int64Constant(0)),
                       m.Int64Constant(0xFFFFFFFFFFFFFFFFl)));
  int64_t input = 1234;
  CHECK_EQ(input, m.Call(input));
}

TEST(Regression6046a) {
  BufferedRawMachineAssemblerTester<int64_t> m;
  m.Return(m.Word64Shr(m.Word64And(m.Int64Constant(0), m.Int64Constant(0)),
                       m.Int64Constant(64)));
  CHECK_EQ(0, m.Call());
}

TEST(Regression6122) {
  BufferedRawMachineAssemblerTester<int64_t> m;
  m.Return(m.Word64Shr(m.Word64And(m.Int64Constant(59), m.Int64Constant(-1)),
                       m.Int64Constant(0)));
  CHECK_EQ(59, m.Call());
}

#endif  // V8_TARGET_ARCH_64_BIT

TEST(Regression6046b) {
  BufferedRawMachineAssemblerTester<int32_t> m;
  m.Return(m.Word32Shr(m.Word32And(m.Int32Constant(0), m.Int32Constant(0)),
                       m.Int32Constant(32)));
  CHECK_EQ(0, m.Call());
}

TEST(Regression6122b) {
  BufferedRawMachineAssemblerTester<int32_t> m;
  m.Return(m.Word32Shr(m.Word32And(m.Int32Constant(59), m.Int32Constant(-1)),
                       m.Int32Constant(0)));
  CHECK_EQ(59, m.Call());
}

TEST(Regression6028) {
  BufferedRawMachineAssemblerTester<int32_t> m;
  m.Return(m.Word32Equal(
      m.Word32And(m.Int32Constant(0x23),
                  m.Word32Sar(m.Int32Constant(1), m.Int32Constant(18))),
      m.Int32Constant(0)));
  CHECK_EQ(1, m.Call());
}

TEST(Regression5951_32bit) {
  BufferedRawMachineAssemblerTester<int32_t> m(MachineType::Int32());
  m.Return(m.Word32And(m.Word32Shr(m.Parameter(0), m.Int32Constant(0)),
                       m.Int32Constant(0xFFFFFFFF)));
  int32_t input = 1234;
  CHECK_EQ(input, m.Call(input));
}

TEST(Regression738952) {
  RawMachineAssemblerTester<int32_t> m;

  int32_t sentinel = 1234;
  // The index can be any value where the lower bits are 0 and the upper bits
  // are not 0;
  int64_t index = 3224;
  index <<= 32;
  double d = static_cast<double>(index);
  m.Return(m.Load(MachineType::Int32(), m.PointerConstant(&sentinel),
                  m.TruncateFloat64ToWord32(m.Float64Constant(d))));
  CHECK_EQ(sentinel, m.Call());
}

#if V8_TARGET_ARCH_64_BIT
TEST(Regression12373) {
  FOR_INT64_INPUTS(i) {
    RawMachineAssemblerTester<int64_t> m(MachineType::Int64(),
                                         MachineType::Int64());
    RawMachineAssemblerTester<int64_t> n(MachineType::Int64());

    Node* mul_rr = m.Int64Mul(m.Parameter(0), m.Parameter(1));
    Node* mul_ri = n.Int64Mul(n.Parameter(0), n.Int64Constant(i));
    m.Return(mul_rr);
    n.Return(mul_ri);
    FOR_INT64_INPUTS(j) { CHECK_EQ(m.Call(j, i), n.Call(j)); }
  }
}
#endif  // V8_TARGET_ARCH_64_BIT

}  // namespace compiler
}  // namespace internal
}  // namespace v8
