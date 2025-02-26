// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/init/v8.h"

#include "src/ast/ast-value-factory.h"
#include "src/execution/isolate.h"
#include "src/handles/handles-inl.h"
#include "src/heap/factory.h"
#include "src/interpreter/constant-array-builder.h"
#include "src/numbers/hash-seed-inl.h"
#include "src/objects/objects-inl.h"
#include "test/unittests/test-utils.h"

namespace v8 {
namespace internal {
namespace interpreter {

class ConstantArrayBuilderTest : public TestWithIsolateAndZone {
 public:
  ConstantArrayBuilderTest() = default;
  ~ConstantArrayBuilderTest() override = default;

  static const size_t k8BitCapacity = ConstantArrayBuilder::k8BitCapacity;
  static const size_t k16BitCapacity = ConstantArrayBuilder::k16BitCapacity;
};

STATIC_CONST_MEMBER_DEFINITION const size_t
    ConstantArrayBuilderTest::k16BitCapacity;
STATIC_CONST_MEMBER_DEFINITION const size_t
    ConstantArrayBuilderTest::k8BitCapacity;

TEST_F(ConstantArrayBuilderTest, AllocateAllEntries) {
  ConstantArrayBuilder builder(zone());
  AstValueFactory ast_factory(zone(), isolate()->ast_string_constants(),
                              HashSeed(isolate()));
  for (size_t i = 0; i < k16BitCapacity; i++) {
    builder.Insert(i + 0.5);
  }
  CHECK_EQ(builder.size(), k16BitCapacity);
  ast_factory.Internalize(isolate());
  for (size_t i = 0; i < k16BitCapacity; i++) {
    CHECK_EQ(
        Cast<HeapNumber>(builder.At(i, isolate()).ToHandleChecked())->value(),
        i + 0.5);
  }
}

TEST_F(ConstantArrayBuilderTest, ToFixedArray) {
  ConstantArrayBuilder builder(zone());
  static const int kNumberOfElements = 37;
  for (int i = 0; i < kNumberOfElements; i++) {
    builder.Insert(i + 0.5);
  }
  DirectHandle<TrustedFixedArray> constant_array =
      builder.ToFixedArray(isolate());
  ASSERT_EQ(kNumberOfElements, constant_array->length());
  for (int i = 0; i < kNumberOfElements; i++) {
    DirectHandle<Object> actual(constant_array->get(i), isolate());
    DirectHandle<Object> expected = builder.At(i, isolate()).ToHandleChecked();
    ASSERT_EQ(Object::NumberValue(*expected), Object::NumberValue(*actual))
        << "Failure at index " << i;
  }
}

TEST_F(ConstantArrayBuilderTest, ToLargeFixedArray) {
  ConstantArrayBuilder builder(zone());
  static const int kNumberOfElements = 37373;
  for (int i = 0; i < kNumberOfElements; i++) {
    builder.Insert(i + 0.5);
  }
  DirectHandle<TrustedFixedArray> constant_array =
      builder.ToFixedArray(isolate());
  ASSERT_EQ(kNumberOfElements, constant_array->length());
  for (int i = 0; i < kNumberOfElements; i++) {
    DirectHandle<Object> actual(constant_array->get(i), isolate());
    DirectHandle<Object> expected = builder.At(i, isolate()).ToHandleChecked();
    ASSERT_EQ(Object::NumberValue(*expected), Object::NumberValue(*actual))
        << "Failure at index " << i;
  }
}

TEST_F(ConstantArrayBuilderTest, ToLargeFixedArrayWithReservations) {
  ConstantArrayBuilder builder(zone());
  AstValueFactory ast_factory(zone(), isolate()->ast_string_constants(),
                              HashSeed(isolate()));
  static const int kNumberOfElements = 37373;
  for (int i = 0; i < kNumberOfElements; i++) {
    builder.CommitReservedEntry(builder.CreateReservedEntry(), Smi::FromInt(i));
  }
  ast_factory.Internalize(isolate());
  DirectHandle<TrustedFixedArray> constant_array =
      builder.ToFixedArray(isolate());
  ASSERT_EQ(kNumberOfElements, constant_array->length());
  for (int i = 0; i < kNumberOfElements; i++) {
    DirectHandle<Object> actual(constant_array->get(i), isolate());
    DirectHandle<Object> expected = builder.At(i, isolate()).ToHandleChecked();
    ASSERT_EQ(Object::NumberValue(*expected), Object::NumberValue(*actual))
        << "Failure at index " << i;
  }
}

TEST_F(ConstantArrayBuilderTest, AllocateEntriesWithIdx8Reservations) {
  for (size_t reserved = 1; reserved < k8BitCapacity; reserved *= 3) {
    ConstantArrayBuilder builder(zone());
    AstValueFactory ast_factory(zone(), isolate()->ast_string_constants(),
                                HashSeed(isolate()));
    for (size_t i = 0; i < reserved; i++) {
      OperandSize operand_size = builder.CreateReservedEntry();
      CHECK_EQ(operand_size, OperandSize::kByte);
    }
    for (size_t i = 0; i < 2 * k8BitCapacity; i++) {
      builder.CommitReservedEntry(builder.CreateReservedEntry(),
                                  Smi::FromInt(static_cast<int>(i)));
      if (i + reserved < k8BitCapacity) {
        CHECK_LE(builder.size(), k8BitCapacity);
        CHECK_EQ(builder.size(), i + 1);
      } else {
        CHECK_GE(builder.size(), k8BitCapacity);
        CHECK_EQ(builder.size(), i + reserved + 1);
      }
    }
    CHECK_EQ(builder.size(), 2 * k8BitCapacity + reserved);

    // Commit reserved entries with duplicates and check size does not change.
    DCHECK_EQ(reserved + 2 * k8BitCapacity, builder.size());
    size_t duplicates_in_idx8_space =
        std::min(reserved, k8BitCapacity - reserved);
    for (size_t i = 0; i < duplicates_in_idx8_space; i++) {
      builder.CommitReservedEntry(OperandSize::kByte,
                                  Smi::FromInt(static_cast<int>(i)));
      DCHECK_EQ(reserved + 2 * k8BitCapacity, builder.size());
    }

    // Now make reservations, and commit them with unique entries.
    for (size_t i = 0; i < duplicates_in_idx8_space; i++) {
      OperandSize operand_size = builder.CreateReservedEntry();
      CHECK_EQ(operand_size, OperandSize::kByte);
    }
    for (size_t i = 0; i < duplicates_in_idx8_space; i++) {
      Tagged<Smi> value = Smi::FromInt(static_cast<int>(2 * k8BitCapacity + i));
      size_t index = builder.CommitReservedEntry(OperandSize::kByte, value);
      CHECK_EQ(index, k8BitCapacity - reserved + i);
    }

    // Clear any remaining uncommited reservations.
    for (size_t i = 0; i < reserved - duplicates_in_idx8_space; i++) {
      builder.DiscardReservedEntry(OperandSize::kByte);
    }

    ast_factory.Internalize(isolate());
    DirectHandle<TrustedFixedArray> constant_array =
        builder.ToFixedArray(isolate());
    CHECK_EQ(constant_array->length(),
             static_cast<int>(2 * k8BitCapacity + reserved));

    // Check all committed values match expected
    for (size_t i = 0; i < k8BitCapacity - reserved; i++) {
      Tagged<Object> value = constant_array->get(static_cast<int>(i));
      Tagged<Smi> smi = Smi::FromInt(static_cast<int>(i));
      CHECK(Object::SameValue(value, smi));
    }
    for (size_t i = k8BitCapacity; i < 2 * k8BitCapacity + reserved; i++) {
      Tagged<Object> value = constant_array->get(static_cast<int>(i));
      Tagged<Smi> smi = Smi::FromInt(static_cast<int>(i - reserved));
      CHECK(Object::SameValue(value, smi));
    }
  }
}

TEST_F(ConstantArrayBuilderTest, AllocateEntriesWithWideReservations) {
  for (size_t reserved = 1; reserved < k8BitCapacity; reserved *= 3) {
    ConstantArrayBuilder builder(zone());
    AstValueFactory ast_factory(zone(), isolate()->ast_string_constants(),
                                HashSeed(isolate()));
    for (size_t i = 0; i < k8BitCapacity; i++) {
      builder.CommitReservedEntry(builder.CreateReservedEntry(),
                                  Smi::FromInt(static_cast<int>(i)));
      CHECK_EQ(builder.size(), i + 1);
    }
    for (size_t i = 0; i < reserved; i++) {
      OperandSize operand_size = builder.CreateReservedEntry();
      CHECK_EQ(operand_size, OperandSize::kShort);
      CHECK_EQ(builder.size(), k8BitCapacity);
    }
    for (size_t i = 0; i < reserved; i++) {
      builder.DiscardReservedEntry(OperandSize::kShort);
      CHECK_EQ(builder.size(), k8BitCapacity);
    }
    for (size_t i = 0; i < reserved; i++) {
      OperandSize operand_size = builder.CreateReservedEntry();
      CHECK_EQ(operand_size, OperandSize::kShort);
      builder.CommitReservedEntry(operand_size,
                                  Smi::FromInt(static_cast<int>(i)));
      CHECK_EQ(builder.size(), k8BitCapacity);
    }
    for (size_t i = k8BitCapacity; i < k8BitCapacity + reserved; i++) {
      OperandSize operand_size = builder.CreateReservedEntry();
      CHECK_EQ(operand_size, OperandSize::kShort);
      builder.CommitReservedEntry(operand_size,
                                  Smi::FromInt(static_cast<int>(i)));
      CHECK_EQ(builder.size(), i + 1);
    }

    ast_factory.Internalize(isolate());
    DirectHandle<TrustedFixedArray> constant_array =
        builder.ToFixedArray(isolate());
    CHECK_EQ(constant_array->length(),
             static_cast<int>(k8BitCapacity + reserved));
    for (size_t i = 0; i < k8BitCapacity + reserved; i++) {
      Tagged<Object> value = constant_array->get(static_cast<int>(i));
      CHECK(Object::SameValue(value,
                              *isolate()->factory()->NewNumberFromSize(i)));
    }
  }
}

TEST_F(ConstantArrayBuilderTest, GapFilledWhenLowReservationCommitted) {
  ConstantArrayBuilder builder(zone());
  AstValueFactory ast_factory(zone(), isolate()->ast_string_constants(),
                              HashSeed(isolate()));
  for (size_t i = 0; i < k8BitCapacity; i++) {
    OperandSize operand_size = builder.CreateReservedEntry();
    CHECK_EQ(OperandSize::kByte, operand_size);
    CHECK_EQ(builder.size(), 0u);
  }
  for (size_t i = 0; i < k8BitCapacity; i++) {
    builder.CommitReservedEntry(builder.CreateReservedEntry(),
                                Smi::FromInt(static_cast<int>(i)));
    CHECK_EQ(builder.size(), i + k8BitCapacity + 1);
  }
  for (size_t i = 0; i < k8BitCapacity; i++) {
    builder.CommitReservedEntry(OperandSize::kByte,
                                Smi::FromInt(static_cast<int>(i)));
    CHECK_EQ(builder.size(), 2 * k8BitCapacity);
  }
  ast_factory.Internalize(isolate());
  DirectHandle<TrustedFixedArray> constant_array =
      builder.ToFixedArray(isolate());
  CHECK_EQ(constant_array->length(), static_cast<int>(2 * k8BitCapacity));
  for (size_t i = 0; i < k8BitCapacity; i++) {
    Tagged<Object> original =
        constant_array->get(static_cast<int>(k8BitCapacity + i));
    Tagged<Object> duplicate = constant_array->get(static_cast<int>(i));
    CHECK(Object::SameValue(original, duplicate));
    DirectHandle<Object> reference = isolate()->factory()->NewNumberFromSize(i);
    CHECK(Object::SameValue(original, *reference));
  }
}

TEST_F(ConstantArrayBuilderTest, GapNotFilledWhenLowReservationDiscarded) {
  ConstantArrayBuilder builder(zone());
  for (size_t i = 0; i < k8BitCapacity; i++) {
    OperandSize operand_size = builder.CreateReservedEntry();
    CHECK_EQ(OperandSize::kByte, operand_size);
    CHECK_EQ(builder.size(), 0u);
  }
  double values[k8BitCapacity];
  for (size_t i = 0; i < k8BitCapacity; i++) {
    values[i] = i + 0.5;
  }

  for (size_t i = 0; i < k8BitCapacity; i++) {
    builder.Insert(values[i]);
    CHECK_EQ(builder.size(), i + k8BitCapacity + 1);
  }
  for (size_t i = 0; i < k8BitCapacity; i++) {
    builder.DiscardReservedEntry(OperandSize::kByte);
    builder.Insert(values[i]);
    CHECK_EQ(builder.size(), 2 * k8BitCapacity);
  }
  for (size_t i = 0; i < k8BitCapacity; i++) {
    DirectHandle<Object> reference = isolate()->factory()->NewNumber(i + 0.5);
    DirectHandle<Object> original =
        builder.At(k8BitCapacity + i, isolate()).ToHandleChecked();
    CHECK(Object::SameValue(*original, *reference));
    MaybeDirectHandle<Object> duplicate = builder.At(i, isolate());
    CHECK(duplicate.is_null());
  }
}

TEST_F(ConstantArrayBuilderTest, HolesWithUnusedReservations) {
  static int kNumberOfHoles = 128;
  static int k8BitCapacity = ConstantArrayBuilder::k8BitCapacity;
  ConstantArrayBuilder builder(zone());
  AstValueFactory ast_factory(zone(), isolate()->ast_string_constants(),
                              HashSeed(isolate()));
  for (int i = 0; i < kNumberOfHoles; ++i) {
    CHECK_EQ(builder.CreateReservedEntry(), OperandSize::kByte);
  }
  // Values are placed before the reserved entries in the same slice.
  for (int i = 0; i < k8BitCapacity - kNumberOfHoles; ++i) {
    CHECK_EQ(builder.Insert(i + 0.5), static_cast<size_t>(i));
  }
  // The next value is pushed into the next slice.
  CHECK_EQ(builder.Insert(k8BitCapacity + 0.5), k8BitCapacity);

  // Discard the reserved entries.
  for (int i = 0; i < kNumberOfHoles; ++i) {
    builder.DiscardReservedEntry(OperandSize::kByte);
  }

  ast_factory.Internalize(isolate());
  DirectHandle<TrustedFixedArray> constant_array =
      builder.ToFixedArray(isolate());
  CHECK_EQ(constant_array->length(), k8BitCapacity + 1);
  for (int i = kNumberOfHoles; i < k8BitCapacity; i++) {
    CHECK(Object::SameValue(constant_array->get(i),
                            *isolate()->factory()->the_hole_value()));
  }
  CHECK(!Object::SameValue(constant_array->get(kNumberOfHoles - 1),
                           *isolate()->factory()->the_hole_value()));
  CHECK(!Object::SameValue(constant_array->get(k8BitCapacity),
                           *isolate()->factory()->the_hole_value()));
}

TEST_F(ConstantArrayBuilderTest, ReservationsAtAllScales) {
  ConstantArrayBuilder builder(zone());
  AstValueFactory ast_factory(zone(), isolate()->ast_string_constants(),
                              HashSeed(isolate()));
  for (int i = 0; i < 256; i++) {
    CHECK_EQ(builder.CreateReservedEntry(), OperandSize::kByte);
  }
  for (int i = 256; i < 65536; ++i) {
    CHECK_EQ(builder.CreateReservedEntry(), OperandSize::kShort);
  }
  for (int i = 65536; i < 131072; ++i) {
    CHECK_EQ(builder.CreateReservedEntry(), OperandSize::kQuad);
  }
  CHECK_EQ(builder.CommitReservedEntry(OperandSize::kByte, Smi::FromInt(1)),
           0u);
  CHECK_EQ(builder.CommitReservedEntry(OperandSize::kShort, Smi::FromInt(2)),
           256u);
  CHECK_EQ(builder.CommitReservedEntry(OperandSize::kQuad, Smi::FromInt(3)),
           65536u);
  for (int i = 1; i < 256; i++) {
    builder.DiscardReservedEntry(OperandSize::kByte);
  }
  for (int i = 257; i < 65536; ++i) {
    builder.DiscardReservedEntry(OperandSize::kShort);
  }
  for (int i = 65537; i < 131072; ++i) {
    builder.DiscardReservedEntry(OperandSize::kQuad);
  }

  ast_factory.Internalize(isolate());
  DirectHandle<TrustedFixedArray> constant_array =
      builder.ToFixedArray(isolate());
  CHECK_EQ(constant_array->length(), 65537);
  int count = 1;
  for (int i = 0; i < constant_array->length(); ++i) {
    DirectHandle<Object> expected;
    if (i == 0 || i == 256 || i == 65536) {
      expected = isolate()->factory()->NewNumber(count++);
    } else {
      expected = isolate()->factory()->the_hole_value();
    }
    CHECK(Object::SameValue(constant_array->get(i), *expected));
  }
}

TEST_F(ConstantArrayBuilderTest, AllocateEntriesWithFixedReservations) {
  ConstantArrayBuilder builder(zone());
  for (size_t i = 0; i < k16BitCapacity; i++) {
    if ((i % 2) == 0) {
      CHECK_EQ(i, builder.InsertDeferred());
    } else {
      builder.Insert(Smi::FromInt(static_cast<int>(i)));
    }
  }
  CHECK_EQ(builder.size(), k16BitCapacity);

  // Check values before reserved entries are inserted.
  for (size_t i = 0; i < k16BitCapacity; i++) {
    if ((i % 2) == 0) {
      // Check reserved values are null.
      MaybeDirectHandle<Object> empty = builder.At(i, isolate());
      CHECK(empty.is_null());
    } else {
      CHECK_EQ(Cast<Smi>(*builder.At(i, isolate()).ToHandleChecked()).value(),
               static_cast<int>(i));
    }
  }

  // Insert reserved entries.
  for (size_t i = 0; i < k16BitCapacity; i += 2) {
    builder.SetDeferredAt(i,
                          handle(Smi::FromInt(static_cast<int>(i)), isolate()));
  }

  // Check values after reserved entries are inserted.
  for (size_t i = 0; i < k16BitCapacity; i++) {
    CHECK_EQ(Cast<Smi>(*builder.At(i, isolate()).ToHandleChecked()).value(),
             static_cast<int>(i));
  }
}

}  // namespace interpreter
}  // namespace internal
}  // namespace v8
