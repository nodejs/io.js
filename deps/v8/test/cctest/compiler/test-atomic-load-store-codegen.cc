// Copyright 2021 the V8 project authors. All rights reserved. Use of this
// source code is governed by a BSD-style license that can be found in the
// LICENSE file.

#include "src/base/bits.h"
#include "src/objects/objects-inl.h"
#include "test/cctest/cctest.h"
#include "test/cctest/compiler/codegen-tester.h"
#include "test/common/value-helper.h"

namespace v8 {
namespace internal {
namespace compiler {

#if V8_TARGET_LITTLE_ENDIAN
#define LSB(addr, bytes) addr
#elif V8_TARGET_BIG_ENDIAN
#define LSB(addr, bytes) reinterpret_cast<uint8_t*>(addr + 1) - (bytes)
#else
#error "Unknown Architecture"
#endif

#define TEST_ATOMIC_LOAD_INTEGER(ctype, itype, mach_type, order) \
  do {                                                           \
    ctype buffer[1];                                             \
                                                                 \
    RawMachineAssemblerTester<ctype> m;                          \
    Node* base = m.PointerConstant(&buffer[0]);                  \
    Node* index = m.IntPtrConstant(0);                           \
    AtomicLoadParameters params(mach_type, order);               \
    if (mach_type.MemSize() == 8) {                              \
      m.Return(m.AtomicLoad64(params, base, index));             \
    } else {                                                     \
      m.Return(m.AtomicLoad(params, base, index));               \
    }                                                            \
                                                                 \
    FOR_INPUTS(ctype, itype, i) {                                \
      buffer[0] = i;                                             \
      CHECK_EQ(i, m.Call());                                     \
    }                                                            \
  } while (false)

TEST(AcquireLoadInteger) {
  TEST_ATOMIC_LOAD_INTEGER(int8_t, int8, MachineType::Int8(),
                           AtomicMemoryOrder::kAcqRel);
  TEST_ATOMIC_LOAD_INTEGER(uint8_t, uint8, MachineType::Uint8(),
                           AtomicMemoryOrder::kAcqRel);
  TEST_ATOMIC_LOAD_INTEGER(int16_t, int16, MachineType::Int16(),
                           AtomicMemoryOrder::kAcqRel);
  TEST_ATOMIC_LOAD_INTEGER(uint16_t, uint16, MachineType::Uint16(),
                           AtomicMemoryOrder::kAcqRel);
  TEST_ATOMIC_LOAD_INTEGER(int32_t, int32, MachineType::Int32(),
                           AtomicMemoryOrder::kAcqRel);
  TEST_ATOMIC_LOAD_INTEGER(uint32_t, uint32, MachineType::Uint32(),
                           AtomicMemoryOrder::kAcqRel);
#if V8_TARGET_ARCH_64_BIT
  TEST_ATOMIC_LOAD_INTEGER(uint64_t, uint64, MachineType::Uint64(),
                           AtomicMemoryOrder::kAcqRel);
#endif
}

TEST(SeqCstLoadInteger) {
  TEST_ATOMIC_LOAD_INTEGER(int8_t, int8, MachineType::Int8(),
                           AtomicMemoryOrder::kSeqCst);
  TEST_ATOMIC_LOAD_INTEGER(uint8_t, uint8, MachineType::Uint8(),
                           AtomicMemoryOrder::kSeqCst);
  TEST_ATOMIC_LOAD_INTEGER(int16_t, int16, MachineType::Int16(),
                           AtomicMemoryOrder::kSeqCst);
  TEST_ATOMIC_LOAD_INTEGER(uint16_t, uint16, MachineType::Uint16(),
                           AtomicMemoryOrder::kSeqCst);
  TEST_ATOMIC_LOAD_INTEGER(int32_t, int32, MachineType::Int32(),
                           AtomicMemoryOrder::kSeqCst);
  TEST_ATOMIC_LOAD_INTEGER(uint32_t, uint32, MachineType::Uint32(),
                           AtomicMemoryOrder::kSeqCst);
#if V8_TARGET_ARCH_64_BIT
  TEST_ATOMIC_LOAD_INTEGER(uint64_t, uint64, MachineType::Uint64(),
                           AtomicMemoryOrder::kSeqCst);
#endif
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

template <typename T>
void InitBuffer(Tagged<T>* buffer, size_t length, MachineType type) {
  const size_t kBufferSize = sizeof(Tagged<T>) * length;

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

template <typename T>
void AtomicLoadTagged(MachineType type, AtomicMemoryOrder order) {
  const int kNumElems = 16;
  Tagged<T> buffer[kNumElems];

  InitBuffer(buffer, kNumElems, type);

  for (int i = 0; i < kNumElems; i++) {
    BufferedRawMachineAssemblerTester<Tagged<T>> m;
    Tagged<T>* base_pointer = &buffer[0];
    if (COMPRESS_POINTERS_BOOL) {
      base_pointer =
          reinterpret_cast<Tagged<T>*>(LSB(base_pointer, kTaggedSize));
    }
    Node* base = m.PointerConstant(base_pointer);
    Node* index = m.IntPtrConstant(i * sizeof(buffer[0]));
    AtomicLoadParameters params(type, order);
    Node* load;
    if (kTaggedSize == 8) {
      load = m.AtomicLoad64(params, base, index);
    } else {
      load = m.ChangeInt32ToIntPtr(m.AtomicLoad(params, base, index));
    }
    m.Return(load);
    CheckEq<Tagged<T>>(buffer[i], m.Call());
  }
}
}  // namespace

TEST(AcquireLoadTagged) {
  AtomicLoadTagged<Smi>(MachineType::TaggedSigned(),
                        AtomicMemoryOrder::kAcqRel);
  AtomicLoadTagged<HeapObject>(MachineType::TaggedPointer(),
                               AtomicMemoryOrder::kAcqRel);
  AtomicLoadTagged<Object>(MachineType::AnyTagged(),
                           AtomicMemoryOrder::kAcqRel);
}

TEST(SeqCstLoadTagged) {
  AtomicLoadTagged<Smi>(MachineType::TaggedSigned(),
                        AtomicMemoryOrder::kSeqCst);
  AtomicLoadTagged<HeapObject>(MachineType::TaggedPointer(),
                               AtomicMemoryOrder::kSeqCst);
  AtomicLoadTagged<Object>(MachineType::AnyTagged(),
                           AtomicMemoryOrder::kSeqCst);
}

#define TEST_ATOMIC_STORE_INTEGER(ctype, itype, mach_type, order)             \
  do {                                                                        \
    ctype buffer[1];                                                          \
    buffer[0] = static_cast<ctype>(-1);                                       \
                                                                              \
    BufferedRawMachineAssemblerTester<int32_t> m(mach_type);                  \
    Node* value = m.Parameter(0);                                             \
    Node* base = m.PointerConstant(&buffer[0]);                               \
    Node* index = m.IntPtrConstant(0);                                        \
    AtomicStoreParameters params(mach_type.representation(), kNoWriteBarrier, \
                                 order);                                      \
    if (mach_type.MemSize() == 8) {                                           \
      m.AtomicStore64(params, base, index, value, nullptr);                   \
    } else {                                                                  \
      m.AtomicStore(params, base, index, value);                              \
    }                                                                         \
                                                                              \
    int32_t OK = 0x29000;                                                     \
    m.Return(m.Int32Constant(OK));                                            \
                                                                              \
    FOR_INPUTS(ctype, itype, i) {                                             \
      CHECK_EQ(OK, m.Call(i));                                                \
      CHECK_EQ(i, buffer[0]);                                                 \
    }                                                                         \
  } while (false)

TEST(ReleaseStoreInteger) {
  TEST_ATOMIC_STORE_INTEGER(int8_t, int8, MachineType::Int8(),
                            AtomicMemoryOrder::kAcqRel);
  TEST_ATOMIC_STORE_INTEGER(uint8_t, uint8, MachineType::Uint8(),
                            AtomicMemoryOrder::kAcqRel);
  TEST_ATOMIC_STORE_INTEGER(int16_t, int16, MachineType::Int16(),
                            AtomicMemoryOrder::kAcqRel);
  TEST_ATOMIC_STORE_INTEGER(uint16_t, uint16, MachineType::Uint16(),
                            AtomicMemoryOrder::kAcqRel);
  TEST_ATOMIC_STORE_INTEGER(int32_t, int32, MachineType::Int32(),
                            AtomicMemoryOrder::kAcqRel);
  TEST_ATOMIC_STORE_INTEGER(uint32_t, uint32, MachineType::Uint32(),
                            AtomicMemoryOrder::kAcqRel);
#if V8_TARGET_ARCH_64_BIT
  TEST_ATOMIC_STORE_INTEGER(uint64_t, uint64, MachineType::Uint64(),
                            AtomicMemoryOrder::kAcqRel);
#endif
}

TEST(SeqCstStoreInteger) {
  TEST_ATOMIC_STORE_INTEGER(int8_t, int8, MachineType::Int8(),
                            AtomicMemoryOrder::kSeqCst);
  TEST_ATOMIC_STORE_INTEGER(uint8_t, uint8, MachineType::Uint8(),
                            AtomicMemoryOrder::kSeqCst);
  TEST_ATOMIC_STORE_INTEGER(int16_t, int16, MachineType::Int16(),
                            AtomicMemoryOrder::kSeqCst);
  TEST_ATOMIC_STORE_INTEGER(uint16_t, uint16, MachineType::Uint16(),
                            AtomicMemoryOrder::kSeqCst);
  TEST_ATOMIC_STORE_INTEGER(int32_t, int32, MachineType::Int32(),
                            AtomicMemoryOrder::kSeqCst);
  TEST_ATOMIC_STORE_INTEGER(uint32_t, uint32, MachineType::Uint32(),
                            AtomicMemoryOrder::kSeqCst);
#if V8_TARGET_ARCH_64_BIT
  TEST_ATOMIC_STORE_INTEGER(uint64_t, uint64, MachineType::Uint64(),
                            AtomicMemoryOrder::kSeqCst);
#endif
}

namespace {
template <typename T>
void AtomicStoreTagged(MachineType type, AtomicMemoryOrder order) {
  // This tests that tagged values are correctly transferred by atomic loads and
  // stores from in_buffer to out_buffer. For each particular element in
  // in_buffer, it is copied to a different index in out_buffer, and all other
  // indices are zapped, to test instructions of the correct width are emitted.

  const int kNumElems = 16;
  Tagged<T> in_buffer[kNumElems];
  Tagged<T> out_buffer[kNumElems];
  uintptr_t zap_data[] = {kZapValue, kZapValue};
  Tagged<T> zap_value;

  static_assert(sizeof(Tagged<T>) <= sizeof(zap_data));
  MemCopy(&zap_value, &zap_data, sizeof(Tagged<T>));
  InitBuffer(in_buffer, kNumElems, type);

#ifdef V8_TARGET_BIG_ENDIAN
  int offset = sizeof(Tagged<T>) - ElementSizeInBytes(type.representation());
#else
  int offset = 0;
#endif

  for (int32_t x = 0; x < kNumElems; x++) {
    int32_t y = kNumElems - x - 1;

    RawMachineAssemblerTester<int32_t> m;
    int32_t OK = 0x29000 + x;
    Node* in_base = m.PointerConstant(in_buffer);
    Node* in_index = m.IntPtrConstant(x * sizeof(Tagged<T>) + offset);
    Node* out_base = m.PointerConstant(out_buffer);
    Node* out_index = m.IntPtrConstant(y * sizeof(Tagged<T>) + offset);

    Node* load;
    AtomicLoadParameters load_params(type, order);
    AtomicStoreParameters store_params(type.representation(), kNoWriteBarrier,
                                       order);
    if (kTaggedSize == 4) {
      load = m.AtomicLoad(load_params, in_base, in_index);
      m.AtomicStore(store_params, out_base, out_index, load);
    } else {
      DCHECK(m.machine()->Is64());
      load = m.AtomicLoad64(load_params, in_base, in_index);
      m.AtomicStore64(store_params, out_base, out_index, load, nullptr);
    }

    m.Return(m.Int32Constant(OK));

    for (int32_t z = 0; z < kNumElems; z++) {
      out_buffer[z] = zap_value;
    }
    CHECK_NE(in_buffer[x], out_buffer[y]);
    CHECK_EQ(OK, m.Call());
    // Mostly same as CHECK_EQ() but customized for compressed tagged values.
    CheckEq<Tagged<T>>(in_buffer[x], out_buffer[y]);
    for (int32_t z = 0; z < kNumElems; z++) {
      if (z != y) CHECK_EQ(zap_value, out_buffer[z]);
    }
  }
}
}  // namespace

TEST(ReleaseStoreTagged) {
  AtomicStoreTagged<Smi>(MachineType::TaggedSigned(),
                         AtomicMemoryOrder::kAcqRel);
  AtomicStoreTagged<HeapObject>(MachineType::TaggedPointer(),
                                AtomicMemoryOrder::kAcqRel);
  AtomicStoreTagged<Object>(MachineType::AnyTagged(),
                            AtomicMemoryOrder::kAcqRel);
}

TEST(SeqCstStoreTagged) {
  AtomicStoreTagged<Smi>(MachineType::TaggedSigned(),
                         AtomicMemoryOrder::kSeqCst);
  AtomicStoreTagged<HeapObject>(MachineType::TaggedPointer(),
                                AtomicMemoryOrder::kSeqCst);
  AtomicStoreTagged<Object>(MachineType::AnyTagged(),
                            AtomicMemoryOrder::kSeqCst);
}

#if V8_TARGET_ARCH_32_BIT

namespace {
void TestAtomicPairLoadInteger(AtomicMemoryOrder order) {
  uint64_t buffer[1];
  uint32_t high;
  uint32_t low;

  BufferedRawMachineAssemblerTester<int32_t> m;
  Node* base = m.PointerConstant(&buffer[0]);
  Node* index = m.Int32Constant(0);

  Node* pair_load = m.AtomicLoad64(
      AtomicLoadParameters(MachineType::Uint64(), order), base, index);
  m.StoreToPointer(&low, MachineRepresentation::kWord32,
                   m.Projection(0, pair_load));
  m.StoreToPointer(&high, MachineRepresentation::kWord32,
                   m.Projection(1, pair_load));

  int32_t OK = 0x29000;
  m.Return(m.Int32Constant(OK));

  FOR_UINT64_INPUTS(i) {
    buffer[0] = i;
    CHECK_EQ(OK, m.Call());
    CHECK_EQ(i, make_uint64(high, low));
  }
}
}  // namespace

TEST(AcquirePairLoadInteger) {
  TestAtomicPairLoadInteger(AtomicMemoryOrder::kAcqRel);
}

TEST(SeqCstPairLoadInteger) {
  TestAtomicPairLoadInteger(AtomicMemoryOrder::kSeqCst);
}

namespace {
void TestAtomicPairStoreInteger(AtomicMemoryOrder order) {
  uint64_t buffer[1];

  BufferedRawMachineAssemblerTester<int32_t> m(MachineType::Uint32(),
                                               MachineType::Uint32());
  Node* base = m.PointerConstant(&buffer[0]);
  Node* index = m.Int32Constant(0);

  m.AtomicStore64(AtomicStoreParameters(MachineRepresentation::kWord64,
                                        kNoWriteBarrier, order),
                  base, index, m.Parameter(0), m.Parameter(1));

  int32_t OK = 0x29000;
  m.Return(m.Int32Constant(OK));

  FOR_UINT64_INPUTS(i) {
    CHECK_EQ(OK, m.Call(static_cast<uint32_t>(i & 0xFFFFFFFF),
                        static_cast<uint32_t>(i >> 32)));
    CHECK_EQ(i, buffer[0]);
  }
}
}  // namespace

TEST(ReleasePairStoreInteger) {
  TestAtomicPairStoreInteger(AtomicMemoryOrder::kAcqRel);
}

TEST(SeqCstPairStoreInteger) {
  TestAtomicPairStoreInteger(AtomicMemoryOrder::kSeqCst);
}

#endif  // V8_TARGET_ARCH_32_BIT

}  // namespace compiler
}  // namespace internal
}  // namespace v8
