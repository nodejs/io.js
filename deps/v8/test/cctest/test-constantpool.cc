// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Test embedded constant pool builder code.

#include "src/init/v8.h"

#include "src/codegen/constant-pool.h"
#include "test/cctest/cctest.h"

namespace v8 {
namespace internal {

#if defined(V8_TARGET_ARCH_PPC64)

const ConstantPoolEntry::Type kPtrType = ConstantPoolEntry::INTPTR;
const ConstantPoolEntry::Type kDblType = ConstantPoolEntry::DOUBLE;
const ConstantPoolEntry::Access kRegAccess = ConstantPoolEntry::REGULAR;
const ConstantPoolEntry::Access kOvflAccess = ConstantPoolEntry::OVERFLOWED;

const int kReachBits = 6;  // Use reach of 64-bytes to test overflow.
const int kReach = 1 << kReachBits;


TEST(ConstantPoolPointers) {
  ConstantPoolBuilder builder(kReachBits, kReachBits);
  const int kRegularCount = kReach / kSystemPointerSize;
  ConstantPoolEntry::Access access;
  int pos = 0;
  intptr_t value = 0;
  bool sharing_ok = true;

  CHECK(builder.IsEmpty());
  while (builder.NextAccess(kPtrType) == kRegAccess) {
    access = builder.AddEntry(pos++, value++, sharing_ok);
    CHECK_EQ(access, kRegAccess);
  }
  CHECK(!builder.IsEmpty());
  CHECK_EQ(pos, kRegularCount);

  access = builder.AddEntry(pos, value, sharing_ok);
  CHECK_EQ(access, kOvflAccess);
}


TEST(ConstantPoolDoubles) {
  ConstantPoolBuilder builder(kReachBits, kReachBits);
  const int kRegularCount = kReach / kDoubleSize;
  ConstantPoolEntry::Access access;
  int pos = 0;
  double value = 0.0;

  CHECK(builder.IsEmpty());
  while (builder.NextAccess(kDblType) == kRegAccess) {
    access = builder.AddEntry(pos++, value);
    value += 0.5;
    CHECK_EQ(access, kRegAccess);
  }
  CHECK(!builder.IsEmpty());
  CHECK_EQ(pos, kRegularCount);

  access = builder.AddEntry(pos, value);
  CHECK_EQ(access, kOvflAccess);
}


TEST(ConstantPoolMixedTypes) {
  ConstantPoolBuilder builder(kReachBits, kReachBits);
  const int kRegularCount =
      (((kReach / (kDoubleSize + kSystemPointerSize)) * 2) +
       ((kSystemPointerSize < kDoubleSize) ? 1 : 0));
  ConstantPoolEntry::Type type = kPtrType;
  ConstantPoolEntry::Access access;
  int pos = 0;
  intptr_t ptrValue = 0;
  double dblValue = 0.0;
  bool sharing_ok = true;

  CHECK(builder.IsEmpty());
  while (builder.NextAccess(type) == kRegAccess) {
    if (type == kPtrType) {
      access = builder.AddEntry(pos++, ptrValue++, sharing_ok);
      type = kDblType;
    } else {
      access = builder.AddEntry(pos++, dblValue);
      dblValue += 0.5;
      type = kPtrType;
    }
    CHECK_EQ(access, kRegAccess);
  }
  CHECK(!builder.IsEmpty());
  CHECK_EQ(pos, kRegularCount);

  access = builder.AddEntry(pos++, ptrValue, sharing_ok);
  CHECK_EQ(access, kOvflAccess);
  access = builder.AddEntry(pos, dblValue);
  CHECK_EQ(access, kOvflAccess);
}


TEST(ConstantPoolMixedReach) {
  const int ptrReachBits = kReachBits + 2;
  const int ptrReach = 1 << ptrReachBits;
  const int dblReachBits = kReachBits;
  const int dblReach = kReach;
  const int dblRegularCount = std::min(
      dblReach / kDoubleSize, ptrReach / (kDoubleSize + kSystemPointerSize));
  const int ptrRegularCount =
      ((ptrReach - (dblRegularCount * (kDoubleSize + kSystemPointerSize))) /
       kSystemPointerSize) +
      dblRegularCount;
  ConstantPoolBuilder builder(ptrReachBits, dblReachBits);
  ConstantPoolEntry::Access access;
  int pos = 0;
  intptr_t ptrValue = 0;
  double dblValue = 0.0;
  bool sharing_ok = true;
  int ptrCount = 0;
  int dblCount = 0;

  CHECK(builder.IsEmpty());
  while (builder.NextAccess(kDblType) == kRegAccess) {
    access = builder.AddEntry(pos++, dblValue);
    dblValue += 0.5;
    dblCount++;
    CHECK_EQ(access, kRegAccess);

    access = builder.AddEntry(pos++, ptrValue++, sharing_ok);
    ptrCount++;
    CHECK_EQ(access, kRegAccess);
  }
  CHECK(!builder.IsEmpty());
  CHECK_EQ(dblCount, dblRegularCount);

  while (ptrCount < ptrRegularCount) {
    access = builder.AddEntry(pos++, dblValue);
    dblValue += 0.5;
    CHECK_EQ(access, kOvflAccess);

    access = builder.AddEntry(pos++, ptrValue++, sharing_ok);
    ptrCount++;
    CHECK_EQ(access, kRegAccess);
  }
  CHECK_EQ(builder.NextAccess(kPtrType), kOvflAccess);

  access = builder.AddEntry(pos++, ptrValue, sharing_ok);
  CHECK_EQ(access, kOvflAccess);
  access = builder.AddEntry(pos, dblValue);
  CHECK_EQ(access, kOvflAccess);
}


TEST(ConstantPoolSharing) {
  ConstantPoolBuilder builder(kReachBits, kReachBits);
  const int kRegularCount =
      (((kReach / (kDoubleSize + kSystemPointerSize)) * 2) +
       ((kSystemPointerSize < kDoubleSize) ? 1 : 0));
  ConstantPoolEntry::Access access;

  CHECK(builder.IsEmpty());

  ConstantPoolEntry::Type type = kPtrType;
  int pos = 0;
  intptr_t ptrValue = 0;
  double dblValue = 0.0;
  bool sharing_ok = true;
  while (builder.NextAccess(type) == kRegAccess) {
    if (type == kPtrType) {
      access = builder.AddEntry(pos++, ptrValue++, sharing_ok);
      type = kDblType;
    } else {
      access = builder.AddEntry(pos++, dblValue);
      dblValue += 0.5;
      type = kPtrType;
    }
    CHECK_EQ(access, kRegAccess);
  }
  CHECK(!builder.IsEmpty());
  CHECK_EQ(pos, kRegularCount);

  type = kPtrType;
  ptrValue = 0;
  dblValue = 0.0;
  while (pos < kRegularCount * 2) {
    if (type == kPtrType) {
      access = builder.AddEntry(pos++, ptrValue++, sharing_ok);
      type = kDblType;
    } else {
      access = builder.AddEntry(pos++, dblValue);
      dblValue += 0.5;
      type = kPtrType;
    }
    CHECK_EQ(access, kRegAccess);
  }

  access = builder.AddEntry(pos++, ptrValue, sharing_ok);
  CHECK_EQ(access, kOvflAccess);
  access = builder.AddEntry(pos, dblValue);
  CHECK_EQ(access, kOvflAccess);
}


TEST(ConstantPoolNoSharing) {
  ConstantPoolBuilder builder(kReachBits, kReachBits);
  const int kRegularCount =
      (((kReach / (kDoubleSize + kSystemPointerSize)) * 2) +
       ((kSystemPointerSize < kDoubleSize) ? 1 : 0));
  ConstantPoolEntry::Access access;

  CHECK(builder.IsEmpty());

  ConstantPoolEntry::Type type = kPtrType;
  int pos = 0;
  intptr_t ptrValue = 0;
  double dblValue = 0.0;
  bool sharing_ok = false;
  while (builder.NextAccess(type) == kRegAccess) {
    if (type == kPtrType) {
      access = builder.AddEntry(pos++, ptrValue++, sharing_ok);
      type = kDblType;
    } else {
      access = builder.AddEntry(pos++, dblValue);
      dblValue += 0.5;
      type = kPtrType;
    }
    CHECK_EQ(access, kRegAccess);
  }
  CHECK(!builder.IsEmpty());
  CHECK_EQ(pos, kRegularCount);

  type = kPtrType;
  ptrValue = 0;
  dblValue = 0.0;
  sharing_ok = true;
  while (pos < kRegularCount * 2) {
    if (type == kPtrType) {
      access = builder.AddEntry(pos++, ptrValue++, sharing_ok);
      type = kDblType;
      CHECK_EQ(access, kOvflAccess);
    } else {
      access = builder.AddEntry(pos++, dblValue);
      dblValue += 0.5;
      type = kPtrType;
      CHECK_EQ(access, kRegAccess);
    }
  }

  access = builder.AddEntry(pos++, ptrValue, sharing_ok);
  CHECK_EQ(access, kOvflAccess);
  access = builder.AddEntry(pos, dblValue);
  CHECK_EQ(access, kOvflAccess);
}

#endif  // defined(V8_TARGET_ARCH_PPC64)

}  // namespace internal
}  // namespace v8
