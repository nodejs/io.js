// Copyright 2016 the V8 project authors. All rights reserved. Use of this
// source code is governed by a BSD-style license that can be found in the
// LICENSE file.

#include <cmath>
#include <functional>
#include <limits>

#include "src/base/bits.h"
#include "src/base/overflowing-math.h"
#include "src/base/template-utils.h"
#include "src/base/utils/random-number-generator.h"
#include "src/objects/objects-inl.h"
#include "src/objects/tagged.h"
#include "test/cctest/cctest.h"
#include "test/cctest/compiler/codegen-tester.h"
#include "test/common/value-helper.h"

namespace v8 {
namespace internal {
namespace compiler {

enum TestAlignment {
  kAligned,
  kUnaligned,
};

#if V8_TARGET_LITTLE_ENDIAN
#define LSB(addr, bytes) addr
#elif V8_TARGET_BIG_ENDIAN
#define LSB(addr, bytes) reinterpret_cast<uint8_t*>(addr + 1) - (bytes)
#else
#error "Unknown Architecture"
#endif

// This is America!
#define A_BILLION 1000000000ULL
#define A_GIG (1024ULL * 1024ULL * 1024ULL)

namespace {
uint8_t* ComputeOffset(void* real_address, int32_t offset) {
  return reinterpret_cast<uint8_t*>(reinterpret_cast<Address>(real_address) -
                                    offset);
}

void RunLoadInt32(const TestAlignment t) {
  RawMachineAssemblerTester<int32_t> m;

  int32_t p1 = 0;  // loads directly from this location.

  if (t == TestAlignment::kAligned) {
    m.Return(m.LoadFromPointer(&p1, MachineType::Int32()));
  } else if (t == TestAlignment::kUnaligned) {
    m.Return(m.UnalignedLoadFromPointer(&p1, MachineType::Int32()));
  } else {
    UNREACHABLE();
  }

  FOR_INT32_INPUTS(i) {
    p1 = i;
    CHECK_EQ(p1, m.Call());
  }
}

void RunLoadInt32Offset(TestAlignment t) {
  int32_t p1 = 0;  // loads directly from this location.

  int32_t offsets[] = {-2000000, -100, -101, 1,          3,
                       7,        120,  2000, 2000000000, 0xFF};

  for (size_t i = 0; i < arraysize(offsets); i++) {
    RawMachineAssemblerTester<int32_t> m;
    int32_t offset = offsets[i];
    uint8_t* pointer = ComputeOffset(&p1, offset);

    // generate load [#base + #index]
    if (t == TestAlignment::kAligned) {
      m.Return(m.LoadFromPointer(pointer, MachineType::Int32(), offset));
    } else if (t == TestAlignment::kUnaligned) {
      m.Return(
          m.UnalignedLoadFromPointer(pointer, MachineType::Int32(), offset));
    } else {
      UNREACHABLE();
    }

    FOR_INT32_INPUTS(j) {
      p1 = j;
      CHECK_EQ(p1, m.Call());
    }
  }
}

void RunLoadStoreFloat32Offset(TestAlignment t) {
  float p1 = 0.0f;  // loads directly from this location.
  float p2 = 0.0f;  // and stores directly into this location.

  FOR_INT32_INPUTS(i) {
    int32_t magic =
        base::AddWithWraparound(0x2342AABB, base::MulWithWraparound(i, 3));
    RawMachineAssemblerTester<int32_t> m;
    int32_t offset = i;
    uint8_t* from = ComputeOffset(&p1, offset);
    uint8_t* to = ComputeOffset(&p2, offset);
    // generate load [#base + #index]
    if (t == TestAlignment::kAligned) {
      Node* load = m.Load(MachineType::Float32(), m.PointerConstant(from),
                          m.IntPtrConstant(offset));
      m.Store(MachineRepresentation::kFloat32, m.PointerConstant(to),
              m.IntPtrConstant(offset), load, kNoWriteBarrier);
    } else if (t == TestAlignment::kUnaligned) {
      Node* load =
          m.UnalignedLoad(MachineType::Float32(), m.PointerConstant(from),
                          m.IntPtrConstant(offset));
      m.UnalignedStore(MachineRepresentation::kFloat32, m.PointerConstant(to),
                       m.IntPtrConstant(offset), load);

    } else {
      UNREACHABLE();
    }
    m.Return(m.Int32Constant(magic));

    FOR_FLOAT32_INPUTS(j) {
      p1 = j;
      p2 = j - 5;
      CHECK_EQ(magic, m.Call());
      CHECK_DOUBLE_EQ(p1, p2);
    }
  }
}

void RunLoadStoreFloat64Offset(TestAlignment t) {
  double p1 = 0;  // loads directly from this location.
  double p2 = 0;  // and stores directly into this location.

  FOR_INT32_INPUTS(i) {
    int32_t magic =
        base::AddWithWraparound(0x2342AABB, base::MulWithWraparound(i, 3));
    RawMachineAssemblerTester<int32_t> m;
    int32_t offset = i;
    uint8_t* from = ComputeOffset(&p1, offset);
    uint8_t* to = ComputeOffset(&p2, offset);
    // generate load [#base + #index]
    if (t == TestAlignment::kAligned) {
      Node* load = m.Load(MachineType::Float64(), m.PointerConstant(from),
                          m.IntPtrConstant(offset));
      m.Store(MachineRepresentation::kFloat64, m.PointerConstant(to),
              m.IntPtrConstant(offset), load, kNoWriteBarrier);
    } else if (t == TestAlignment::kUnaligned) {
      Node* load =
          m.UnalignedLoad(MachineType::Float64(), m.PointerConstant(from),
                          m.IntPtrConstant(offset));
      m.UnalignedStore(MachineRepresentation::kFloat64, m.PointerConstant(to),
                       m.IntPtrConstant(offset), load);
    } else {
      UNREACHABLE();
    }
    m.Return(m.Int32Constant(magic));

    FOR_FLOAT64_INPUTS(j) {
      p1 = j;
      p2 = j - 5;
      CHECK_EQ(magic, m.Call());
      CHECK_DOUBLE_EQ(p1, p2);
    }
  }
}
}  // namespace

TEST(RunLoadInt32) { RunLoadInt32(TestAlignment::kAligned); }

TEST(RunUnalignedLoadInt32) { RunLoadInt32(TestAlignment::kUnaligned); }

TEST(RunLoadInt32Offset) { RunLoadInt32Offset(TestAlignment::kAligned); }

TEST(RunUnalignedLoadInt32Offset) {
  RunLoadInt32Offset(TestAlignment::kUnaligned);
}

TEST(RunLoadStoreFloat32Offset) {
  RunLoadStoreFloat32Offset(TestAlignment::kAligned);
}

TEST(RunUnalignedLoadStoreFloat32Offset) {
  RunLoadStoreFloat32Offset(TestAlignment::kUnaligned);
}

TEST(RunLoadStoreFloat64Offset) {
  RunLoadStoreFloat64Offset(TestAlignment::kAligned);
}

TEST(RunUnalignedLoadStoreFloat64Offset) {
  RunLoadStoreFloat64Offset(TestAlignment::kUnaligned);
}

namespace {

// Mostly same as CHECK_EQ() but customized for compressed tagged values.
template <typename CType>
void CheckEq(CType in_value, CType out_value) {
  CHECK_EQ(in_value, out_value);
}

#ifdef V8_COMPRESS_POINTERS
// Specializations for checking the result of compressing store.
template <>
void CheckEq<Tagged<Object>>(Tagged<Object> in_value,
                             Tagged<Object> out_value) {
  // Compare only lower 32-bits of the value because tagged load/stores are
  // 32-bit operations anyway.
  CHECK_EQ(static_cast<Tagged_t>(in_value.ptr()),
           static_cast<Tagged_t>(out_value.ptr()));
}

template <>
void CheckEq<Tagged<HeapObject>>(Tagged<HeapObject> in_value,
                                 Tagged<HeapObject> out_value) {
  return CheckEq<Tagged<Object>>(in_value, out_value);
}

template <>
void CheckEq<Tagged<Smi>>(Tagged<Smi> in_value, Tagged<Smi> out_value) {
  return CheckEq<Tagged<Object>>(in_value, out_value);
}
#endif

// Initializes the buffer with some raw data respecting requested representation
// of the values.
template <typename CType>
void InitBuffer(CType* buffer, size_t length, MachineType type) {
  const size_t kBufferSize = sizeof(CType) * length;
  if (!type.IsTagged()) {
    uint8_t* raw = reinterpret_cast<uint8_t*>(buffer);
    for (size_t i = 0; i < kBufferSize; i++) {
      raw[i] = static_cast<uint8_t>((i + kBufferSize) ^ 0xAA);
    }
    return;
  }

  // Tagged field loads require values to be properly tagged because of
  // pointer decompression that may be happenning during load.
  Isolate* isolate = CcTest::InitIsolateOnce();
  Tagged<Smi>* smi_view = reinterpret_cast<Tagged<Smi>*>(&buffer[0]);
  if (type.IsTaggedSigned()) {
    for (size_t i = 0; i < length; i++) {
      smi_view[i] = Smi::FromInt(static_cast<int>(i + kBufferSize) ^ 0xABCDEF0);
    }
  } else {
    memcpy(&buffer[0], &isolate->roots_table(), kBufferSize);
    if (!type.IsTaggedPointer()) {
      // Also add some Smis if we are checking AnyTagged case.
      for (size_t i = 0; i < length / 2; i++) {
        smi_view[i] =
            Smi::FromInt(static_cast<int>(i + kBufferSize) ^ 0xABCDEF0);
      }
    }
  }
}

template <typename CType>
void RunLoadImmIndex(MachineType type, TestAlignment t) {
  const int kNumElems = 16;
  CType buffer[kNumElems];

  InitBuffer(buffer, kNumElems, type);

  // Test with various large and small offsets.
  for (int offset = -1; offset <= 200000; offset *= -5) {
    for (int i = 0; i < kNumElems; i++) {
      BufferedRawMachineAssemblerTester<CType> m;
      CType* base_pointer = reinterpret_cast<CType*>(
          ComputeOffset(&buffer[0], offset * sizeof(CType)));
#ifdef V8_COMPRESS_POINTERS
      if (type.IsTagged()) {
        // When pointer compression is enabled then we need to access only
        // the lower 32-bit of the tagged value while the buffer contains
        // full 64-bit values.
        base_pointer = reinterpret_cast<CType*>(LSB(base_pointer, kTaggedSize));
      }
#endif

      Node* base = m.PointerConstant(base_pointer);
      Node* index = m.Int32Constant((offset + i) * sizeof(buffer[0]));
      if (t == TestAlignment::kAligned) {
        m.Return(m.Load(type, base, index));
      } else if (t == TestAlignment::kUnaligned) {
        m.Return(m.UnalignedLoad(type, base, index));
      } else {
        UNREACHABLE();
      }

      CheckEq<CType>(buffer[i], m.Call());
    }
  }
}

template <typename CType>
void RunLoadStore(MachineType type, TestAlignment t) {
  const int kNumElems = 16;
  CType in_buffer[kNumElems];
  CType out_buffer[kNumElems];
  uintptr_t zap_data[] = {kZapValue, kZapValue};
  CType zap_value;

  static_assert(sizeof(CType) <= sizeof(zap_data));
  MemCopy(&zap_value, &zap_data, sizeof(CType));
  InitBuffer(in_buffer, kNumElems, type);

#ifdef V8_TARGET_BIG_ENDIAN
  int offset = sizeof(CType) - ElementSizeInBytes(type.representation());
#else
  int offset = 0;
#endif

  for (int32_t x = 0; x < kNumElems; x++) {
    int32_t y = kNumElems - x - 1;

    RawMachineAssemblerTester<int32_t> m;
    int32_t OK = 0x29000 + x;
    Node* in_base = m.PointerConstant(in_buffer);
    Node* in_index = m.IntPtrConstant(x * sizeof(CType) + offset);
    Node* out_base = m.PointerConstant(out_buffer);
    Node* out_index = m.IntPtrConstant(y * sizeof(CType) + offset);
    if (t == TestAlignment::kAligned) {
      Node* load = m.Load(type, in_base, in_index);
      m.Store(type.representation(), out_base, out_index, load,
              kNoWriteBarrier);
    } else if (t == TestAlignment::kUnaligned) {
      Node* load = m.UnalignedLoad(type, in_base, in_index);
      m.UnalignedStore(type.representation(), out_base, out_index, load);
    }

    m.Return(m.Int32Constant(OK));

    for (int32_t z = 0; z < kNumElems; z++) {
      out_buffer[z] = zap_value;
    }
    CHECK_NE(in_buffer[x], out_buffer[y]);
    CHECK_EQ(OK, m.Call());
    // Mostly same as CHECK_EQ() but customized for compressed tagged values.
    CheckEq<CType>(in_buffer[x], out_buffer[y]);
    for (int32_t z = 0; z < kNumElems; z++) {
      if (z != y) CHECK_EQ(zap_value, out_buffer[z]);
    }
  }
}

template <typename CType>
void RunUnalignedLoadStoreUnalignedAccess(MachineType type) {
  CType in, out;
  uint8_t in_buffer[2 * sizeof(CType)];
  uint8_t out_buffer[2 * sizeof(CType)];

  InitBuffer(&in, 1, type);

  for (int x = 0; x < static_cast<int>(sizeof(CType)); x++) {
    // Direct write to &in_buffer[x] may cause unaligned access in C++ code so
    // we use MemCopy() to handle that.
    MemCopy(&in_buffer[x], &in, sizeof(CType));

    for (int y = 0; y < static_cast<int>(sizeof(CType)); y++) {
      RawMachineAssemblerTester<int32_t> m;
      int32_t OK = 0x29000 + x;

      Node* in_base = m.PointerConstant(in_buffer);
      Node* in_index = m.IntPtrConstant(x);
      Node* load = m.UnalignedLoad(type, in_base, in_index);

      Node* out_base = m.PointerConstant(out_buffer);
      Node* out_index = m.IntPtrConstant(y);
      m.UnalignedStore(type.representation(), out_base, out_index, load);

      m.Return(m.Int32Constant(OK));

      CHECK_EQ(OK, m.Call());
      // Direct read of &out_buffer[y] may cause unaligned access in C++ code
      // so we use MemCopy() to handle that.
      MemCopy(&out, &out_buffer[y], sizeof(CType));
      // Mostly same as CHECK_EQ() but customized for compressed tagged values.
      CheckEq<CType>(in, out);
    }
  }
}
}  // namespace

TEST(RunLoadImmIndex) {
  RunLoadImmIndex<int8_t>(MachineType::Int8(), TestAlignment::kAligned);
  RunLoadImmIndex<uint8_t>(MachineType::Uint8(), TestAlignment::kAligned);
  RunLoadImmIndex<int16_t>(MachineType::Int16(), TestAlignment::kAligned);
  RunLoadImmIndex<uint16_t>(MachineType::Uint16(), TestAlignment::kAligned);
  RunLoadImmIndex<int32_t>(MachineType::Int32(), TestAlignment::kAligned);
  RunLoadImmIndex<uint32_t>(MachineType::Uint32(), TestAlignment::kAligned);
  RunLoadImmIndex<void*>(MachineType::Pointer(), TestAlignment::kAligned);
  RunLoadImmIndex<Tagged<Smi>>(MachineType::TaggedSigned(),
                               TestAlignment::kAligned);
  RunLoadImmIndex<Tagged<HeapObject>>(MachineType::TaggedPointer(),
                                      TestAlignment::kAligned);
  RunLoadImmIndex<Tagged<Object>>(MachineType::AnyTagged(),
                                  TestAlignment::kAligned);
  RunLoadImmIndex<float>(MachineType::Float32(), TestAlignment::kAligned);
  RunLoadImmIndex<double>(MachineType::Float64(), TestAlignment::kAligned);
#if V8_TARGET_ARCH_64_BIT
  RunLoadImmIndex<int64_t>(MachineType::Int64(), TestAlignment::kAligned);
#endif
  // TODO(titzer): test various indexing modes.
}

TEST(RunUnalignedLoadImmIndex) {
  RunLoadImmIndex<int16_t>(MachineType::Int16(), TestAlignment::kUnaligned);
  RunLoadImmIndex<uint16_t>(MachineType::Uint16(), TestAlignment::kUnaligned);
  RunLoadImmIndex<int32_t>(MachineType::Int32(), TestAlignment::kUnaligned);
  RunLoadImmIndex<uint32_t>(MachineType::Uint32(), TestAlignment::kUnaligned);
  RunLoadImmIndex<void*>(MachineType::Pointer(), TestAlignment::kUnaligned);
  RunLoadImmIndex<float>(MachineType::Float32(), TestAlignment::kUnaligned);
  RunLoadImmIndex<double>(MachineType::Float64(), TestAlignment::kUnaligned);
#if V8_TARGET_ARCH_64_BIT
  RunLoadImmIndex<int64_t>(MachineType::Int64(), TestAlignment::kUnaligned);
#endif
  // TODO(titzer): test various indexing modes.
}

TEST(RunLoadStore) {
  RunLoadStore<int8_t>(MachineType::Int8(), TestAlignment::kAligned);
  RunLoadStore<uint8_t>(MachineType::Uint8(), TestAlignment::kAligned);
  RunLoadStore<int16_t>(MachineType::Int16(), TestAlignment::kAligned);
  RunLoadStore<uint16_t>(MachineType::Uint16(), TestAlignment::kAligned);
  RunLoadStore<int32_t>(MachineType::Int32(), TestAlignment::kAligned);
  RunLoadStore<uint32_t>(MachineType::Uint32(), TestAlignment::kAligned);
  RunLoadStore<void*>(MachineType::Pointer(), TestAlignment::kAligned);
  RunLoadStore<Tagged<Smi>>(MachineType::TaggedSigned(),
                            TestAlignment::kAligned);
  RunLoadStore<Tagged<HeapObject>>(MachineType::TaggedPointer(),
                                   TestAlignment::kAligned);
  RunLoadStore<Tagged<Object>>(MachineType::AnyTagged(),
                               TestAlignment::kAligned);
  RunLoadStore<float>(MachineType::Float32(), TestAlignment::kAligned);
  RunLoadStore<double>(MachineType::Float64(), TestAlignment::kAligned);
#if V8_TARGET_ARCH_64_BIT
  RunLoadStore<int64_t>(MachineType::Int64(), TestAlignment::kAligned);
#endif
}

TEST(RunUnalignedLoadStore) {
  RunLoadStore<int16_t>(MachineType::Int16(), TestAlignment::kUnaligned);
  RunLoadStore<uint16_t>(MachineType::Uint16(), TestAlignment::kUnaligned);
  RunLoadStore<int32_t>(MachineType::Int32(), TestAlignment::kUnaligned);
  RunLoadStore<uint32_t>(MachineType::Uint32(), TestAlignment::kUnaligned);
  RunLoadStore<void*>(MachineType::Pointer(), TestAlignment::kUnaligned);
  RunLoadStore<float>(MachineType::Float32(), TestAlignment::kUnaligned);
  RunLoadStore<double>(MachineType::Float64(), TestAlignment::kUnaligned);
#if V8_TARGET_ARCH_64_BIT
  RunLoadStore<int64_t>(MachineType::Int64(), TestAlignment::kUnaligned);
#endif
}

TEST(RunUnalignedLoadStoreUnalignedAccess) {
  RunUnalignedLoadStoreUnalignedAccess<int16_t>(MachineType::Int16());
  RunUnalignedLoadStoreUnalignedAccess<uint16_t>(MachineType::Uint16());
  RunUnalignedLoadStoreUnalignedAccess<int32_t>(MachineType::Int32());
  RunUnalignedLoadStoreUnalignedAccess<uint32_t>(MachineType::Uint32());
  RunUnalignedLoadStoreUnalignedAccess<void*>(MachineType::Pointer());
  RunUnalignedLoadStoreUnalignedAccess<float>(MachineType::Float32());
  RunUnalignedLoadStoreUnalignedAccess<double>(MachineType::Float64());
#if V8_TARGET_ARCH_64_BIT
  RunUnalignedLoadStoreUnalignedAccess<int64_t>(MachineType::Int64());
#endif
}

namespace {
void RunLoadStoreSignExtend32(TestAlignment t) {
  int32_t buffer[4];
  RawMachineAssemblerTester<int32_t> m;
  Node* load8 = m.LoadFromPointer(LSB(&buffer[0], 1), MachineType::Int8());
  if (t == TestAlignment::kAligned) {
    Node* load16 = m.LoadFromPointer(LSB(&buffer[0], 2), MachineType::Int16());
    Node* load32 = m.LoadFromPointer(&buffer[0], MachineType::Int32());
    m.StoreToPointer(&buffer[1], MachineRepresentation::kWord32, load8);
    m.StoreToPointer(&buffer[2], MachineRepresentation::kWord32, load16);
    m.StoreToPointer(&buffer[3], MachineRepresentation::kWord32, load32);
  } else if (t == TestAlignment::kUnaligned) {
    Node* load16 =
        m.UnalignedLoadFromPointer(LSB(&buffer[0], 2), MachineType::Int16());
    Node* load32 = m.UnalignedLoadFromPointer(&buffer[0], MachineType::Int32());
    m.StoreToPointer(&buffer[1], MachineRepresentation::kWord32, load8);
    m.UnalignedStoreToPointer(&buffer[2], MachineRepresentation::kWord32,
                              load16);
    m.UnalignedStoreToPointer(&buffer[3], MachineRepresentation::kWord32,
                              load32);
  } else {
    UNREACHABLE();
  }
  m.Return(load8);

  FOR_INT32_INPUTS(i) {
    buffer[0] = i;

    CHECK_EQ(static_cast<int8_t>(i & 0xFF), m.Call());
    CHECK_EQ(static_cast<int8_t>(i & 0xFF), buffer[1]);
    CHECK_EQ(static_cast<int16_t>(i & 0xFFFF), buffer[2]);
    CHECK_EQ(i, buffer[3]);
  }
}

void RunLoadStoreZeroExtend32(TestAlignment t) {
  uint32_t buffer[4];
  RawMachineAssemblerTester<uint32_t> m;
  Node* load8 = m.LoadFromPointer(LSB(&buffer[0], 1), MachineType::Uint8());
  if (t == TestAlignment::kAligned) {
    Node* load16 = m.LoadFromPointer(LSB(&buffer[0], 2), MachineType::Uint16());
    Node* load32 = m.LoadFromPointer(&buffer[0], MachineType::Uint32());
    m.StoreToPointer(&buffer[1], MachineRepresentation::kWord32, load8);
    m.StoreToPointer(&buffer[2], MachineRepresentation::kWord32, load16);
    m.StoreToPointer(&buffer[3], MachineRepresentation::kWord32, load32);
  } else if (t == TestAlignment::kUnaligned) {
    Node* load16 =
        m.UnalignedLoadFromPointer(LSB(&buffer[0], 2), MachineType::Uint16());
    Node* load32 =
        m.UnalignedLoadFromPointer(&buffer[0], MachineType::Uint32());
    m.StoreToPointer(&buffer[1], MachineRepresentation::kWord32, load8);
    m.UnalignedStoreToPointer(&buffer[2], MachineRepresentation::kWord32,
                              load16);
    m.UnalignedStoreToPointer(&buffer[3], MachineRepresentation::kWord32,
                              load32);
  }
  m.Return(load8);

  FOR_UINT32_INPUTS(i) {
    buffer[0] = i;

    CHECK_EQ((i & 0xFF), m.Call());
    CHECK_EQ((i & 0xFF), buffer[1]);
    CHECK_EQ((i & 0xFFFF), buffer[2]);
    CHECK_EQ(i, buffer[3]);
  }
}
}  // namespace

TEST(RunLoadStoreSignExtend32) {
  RunLoadStoreSignExtend32(TestAlignment::kAligned);
}

TEST(RunUnalignedLoadStoreSignExtend32) {
  RunLoadStoreSignExtend32(TestAlignment::kUnaligned);
}

TEST(RunLoadStoreZeroExtend32) {
  RunLoadStoreZeroExtend32(TestAlignment::kAligned);
}

TEST(RunUnalignedLoadStoreZeroExtend32) {
  RunLoadStoreZeroExtend32(TestAlignment::kUnaligned);
}

#if V8_TARGET_ARCH_64_BIT

namespace {
void RunLoadStoreSignExtend64(TestAlignment t) {
  if ((true)) return;  // TODO(titzer): sign extension of loads to 64-bit.
  int64_t buffer[5];
  RawMachineAssemblerTester<int64_t> m;
  Node* load8 = m.LoadFromPointer(LSB(&buffer[0], 1), MachineType::Int8());
  if (t == TestAlignment::kAligned) {
    Node* load16 = m.LoadFromPointer(LSB(&buffer[0], 2), MachineType::Int16());
    Node* load32 = m.LoadFromPointer(LSB(&buffer[0], 4), MachineType::Int32());
    Node* load64 = m.LoadFromPointer(&buffer[0], MachineType::Int64());
    m.StoreToPointer(&buffer[1], MachineRepresentation::kWord64, load8);
    m.StoreToPointer(&buffer[2], MachineRepresentation::kWord64, load16);
    m.StoreToPointer(&buffer[3], MachineRepresentation::kWord64, load32);
    m.StoreToPointer(&buffer[4], MachineRepresentation::kWord64, load64);
  } else if (t == TestAlignment::kUnaligned) {
    Node* load16 =
        m.UnalignedLoadFromPointer(LSB(&buffer[0], 2), MachineType::Int16());
    Node* load32 =
        m.UnalignedLoadFromPointer(LSB(&buffer[0], 4), MachineType::Int32());
    Node* load64 = m.UnalignedLoadFromPointer(&buffer[0], MachineType::Int64());
    m.StoreToPointer(&buffer[1], MachineRepresentation::kWord64, load8);
    m.UnalignedStoreToPointer(&buffer[2], MachineRepresentation::kWord64,
                              load16);
    m.UnalignedStoreToPointer(&buffer[3], MachineRepresentation::kWord64,
                              load32);
    m.UnalignedStoreToPointer(&buffer[4], MachineRepresentation::kWord64,
                              load64);
  } else {
    UNREACHABLE();
  }
  m.Return(load8);

  FOR_INT64_INPUTS(i) {
    buffer[0] = i;

    CHECK_EQ(static_cast<int8_t>(i & 0xFF), m.Call());
    CHECK_EQ(static_cast<int8_t>(i & 0xFF), buffer[1]);
    CHECK_EQ(static_cast<int16_t>(i & 0xFFFF), buffer[2]);
    CHECK_EQ(static_cast<int32_t>(i & 0xFFFFFFFF), buffer[3]);
    CHECK_EQ(i, buffer[4]);
  }
}

void RunLoadStoreZeroExtend64(TestAlignment t) {
  if (kSystemPointerSize < 8) return;
  uint64_t buffer[5];
  RawMachineAssemblerTester<uint64_t> m;
  Node* load8 = m.LoadFromPointer(LSB(&buffer[0], 1), MachineType::Uint8());
  if (t == TestAlignment::kAligned) {
    Node* load16 = m.LoadFromPointer(LSB(&buffer[0], 2), MachineType::Uint16());
    Node* load32 = m.LoadFromPointer(LSB(&buffer[0], 4), MachineType::Uint32());
    Node* load64 = m.LoadFromPointer(&buffer[0], MachineType::Uint64());
    m.StoreToPointer(&buffer[1], MachineRepresentation::kWord64, load8);
    m.StoreToPointer(&buffer[2], MachineRepresentation::kWord64, load16);
    m.StoreToPointer(&buffer[3], MachineRepresentation::kWord64, load32);
    m.StoreToPointer(&buffer[4], MachineRepresentation::kWord64, load64);
  } else if (t == TestAlignment::kUnaligned) {
    Node* load16 =
        m.UnalignedLoadFromPointer(LSB(&buffer[0], 2), MachineType::Uint16());
    Node* load32 =
        m.UnalignedLoadFromPointer(LSB(&buffer[0], 4), MachineType::Uint32());
    Node* load64 =
        m.UnalignedLoadFromPointer(&buffer[0], MachineType::Uint64());
    m.StoreToPointer(&buffer[1], MachineRepresentation::kWord64, load8);
    m.UnalignedStoreToPointer(&buffer[2], MachineRepresentation::kWord64,
                              load16);
    m.UnalignedStoreToPointer(&buffer[3], MachineRepresentation::kWord64,
                              load32);
    m.UnalignedStoreToPointer(&buffer[4], MachineRepresentation::kWord64,
                              load64);
  } else {
    UNREACHABLE();
  }
  m.Return(load8);

  FOR_UINT64_INPUTS(i) {
    buffer[0] = i;

    CHECK_EQ((i & 0xFF), m.Call());
    CHECK_EQ((i & 0xFF), buffer[1]);
    CHECK_EQ((i & 0xFFFF), buffer[2]);
    CHECK_EQ((i & 0xFFFFFFFF), buffer[3]);
    CHECK_EQ(i, buffer[4]);
  }
}

}  // namespace

TEST(RunLoadStoreSignExtend64) {
  RunLoadStoreSignExtend64(TestAlignment::kAligned);
}

TEST(RunUnalignedLoadStoreSignExtend64) {
  RunLoadStoreSignExtend64(TestAlignment::kUnaligned);
}

TEST(RunLoadStoreZeroExtend64) {
  RunLoadStoreZeroExtend64(TestAlignment::kAligned);
}

TEST(RunUnalignedLoadStoreZeroExtend64) {
  RunLoadStoreZeroExtend64(TestAlignment::kUnaligned);
}

#endif

namespace {
template <typename IntType>
void LoadStoreTruncation(MachineType kRepresentation, TestAlignment t) {
  IntType input;

  RawMachineAssemblerTester<int32_t> m;
  Node* ap1;
  if (t == TestAlignment::kAligned) {
    Node* a = m.LoadFromPointer(&input, kRepresentation);
    ap1 = m.Int32Add(a, m.Int32Constant(1));
    m.StoreToPointer(&input, kRepresentation.representation(), ap1);
  } else if (t == TestAlignment::kUnaligned) {
    Node* a = m.UnalignedLoadFromPointer(&input, kRepresentation);
    ap1 = m.Int32Add(a, m.Int32Constant(1));
    m.UnalignedStoreToPointer(&input, kRepresentation.representation(), ap1);
  } else {
    UNREACHABLE();
  }
  m.Return(ap1);

  const IntType max = std::numeric_limits<IntType>::max();
  const IntType min = std::numeric_limits<IntType>::min();

  // Test upper bound.
  input = max;
  CHECK_EQ(max + 1, m.Call());
  CHECK_EQ(min, input);

  // Test lower bound.
  input = min;
  CHECK_EQ(static_cast<IntType>(max + 2), m.Call());
  CHECK_EQ(min + 1, input);

  // Test all one byte values that are not one byte bounds.
  for (int i = -127; i < 127; i++) {
    input = i;
    int expected = i >= 0 ? i + 1 : max + (i - min) + 2;
    CHECK_EQ(static_cast<IntType>(expected), m.Call());
    CHECK_EQ(static_cast<IntType>(i + 1), input);
  }
}
}  // namespace

TEST(RunLoadStoreTruncation) {
  LoadStoreTruncation<int8_t>(MachineType::Int8(), TestAlignment::kAligned);
  LoadStoreTruncation<int16_t>(MachineType::Int16(), TestAlignment::kAligned);
}

TEST(RunUnalignedLoadStoreTruncation) {
  LoadStoreTruncation<int16_t>(MachineType::Int16(), TestAlignment::kUnaligned);
}

#undef LSB
#undef A_BILLION
#undef A_GIG

}  // namespace compiler
}  // namespace internal
}  // namespace v8
