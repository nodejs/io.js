// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <set>

#include "src/objects/objects-inl.h"
#include "src/objects/smi.h"
#include "test/cctest/cctest.h"

namespace v8 {
namespace internal {

namespace {

void AddSigned(std::set<Tagged<Smi>>* smis, int64_t x) {
  if (!Smi::IsValid(x)) return;

  smis->insert(Smi::FromInt(static_cast<int>(x)));
  smis->insert(Smi::FromInt(static_cast<int>(-x)));
}

// Uses std::lexicographical_compare twice to convert the result to -1, 0 or 1.
int ExpectedCompareResult(Tagged<Smi> a, Tagged<Smi> b) {
  std::string str_a = std::to_string(a.value());
  std::string str_b = std::to_string(b.value());
  bool expected_a_lt_b = std::lexicographical_compare(
      str_a.begin(), str_a.end(), str_b.begin(), str_b.end());
  bool expected_b_lt_a = std::lexicographical_compare(
      str_b.begin(), str_b.end(), str_a.begin(), str_a.end());

  if (!expected_a_lt_b && !expected_b_lt_a) {
    return 0;
  } else if (expected_a_lt_b) {
    return -1;
  } else {
    CHECK(expected_b_lt_a);
    return 1;
  }
}

bool Test(Isolate* isolate, Tagged<Smi> a, Tagged<Smi> b) {
  int actual = Tagged<Smi>(Smi::LexicographicCompare(isolate, a, b)).value();
  int expected = ExpectedCompareResult(a, b);

  return actual == expected;
}

}  // namespace

TEST(TestSmiLexicographicCompare) {
  Isolate* isolate = CcTest::InitIsolateOnce();
  HandleScope scope(isolate);

  std::set<Tagged<Smi>> smis;

  for (int64_t xb = 1; xb <= Smi::kMaxValue; xb *= 10) {
    for (int64_t xf = 0; xf <= 9; ++xf) {
      for (int64_t xo = -1; xo <= 1; ++xo) {
        AddSigned(&smis, xb * xf + xo);
      }
    }
  }

  for (int64_t yb = 1; yb <= Smi::kMaxValue; yb *= 2) {
    for (int64_t yo = -2; yo <= 2; ++yo) {
      AddSigned(&smis, yb + yo);
    }
  }

  for (Tagged<Smi> a : smis) {
    for (Tagged<Smi> b : smis) {
      CHECK(Test(isolate, a, b));
    }
  }
}

}  // namespace internal
}  // namespace v8
