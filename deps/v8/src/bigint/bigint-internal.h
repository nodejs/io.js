// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_BIGINT_BIGINT_INTERNAL_H_
#define V8_BIGINT_BIGINT_INTERNAL_H_

#include <memory>

#include "src/bigint/bigint.h"

namespace v8 {
namespace bigint {

constexpr int kKaratsubaThreshold = 34;
constexpr int kToomThreshold = 193;
constexpr int kFftThreshold = 1500;
constexpr int kFftInnerThreshold = 200;

constexpr int kBurnikelThreshold = 57;
constexpr int kNewtonInversionThreshold = 50;
// kBarrettThreshold is defined in bigint.h.

constexpr int kToStringFastThreshold = 43;
constexpr int kFromStringLargeThreshold = 300;

class ProcessorImpl : public Processor {
 public:
  explicit ProcessorImpl(Platform* platform);
  ~ProcessorImpl();

  Status get_and_clear_status();

  void Multiply(RWDigits Z, Digits X, Digits Y);
  void MultiplySingle(RWDigits Z, Digits X, digit_t y);
  void MultiplySchoolbook(RWDigits Z, Digits X, Digits Y);

  void MultiplyKaratsuba(RWDigits Z, Digits X, Digits Y);
  void KaratsubaStart(RWDigits Z, Digits X, Digits Y, RWDigits scratch, int k);
  void KaratsubaChunk(RWDigits Z, Digits X, Digits Y, RWDigits scratch);
  void KaratsubaMain(RWDigits Z, Digits X, Digits Y, RWDigits scratch, int n);

  void Divide(RWDigits Q, Digits A, Digits B);
  void DivideSingle(RWDigits Q, digit_t* remainder, Digits A, digit_t b);
  void DivideSchoolbook(RWDigits Q, RWDigits R, Digits A, Digits B);
  void DivideBurnikelZiegler(RWDigits Q, RWDigits R, Digits A, Digits B);

  void Modulo(RWDigits R, Digits A, Digits B);

#if V8_ADVANCED_BIGINT_ALGORITHMS
  void MultiplyToomCook(RWDigits Z, Digits X, Digits Y);
  void Toom3Main(RWDigits Z, Digits X, Digits Y);

  void MultiplyFFT(RWDigits Z, Digits X, Digits Y);

  void DivideBarrett(RWDigits Q, RWDigits R, Digits A, Digits B);
  void DivideBarrett(RWDigits Q, RWDigits R, Digits A, Digits B, Digits I,
                     RWDigits scratch);

  void Invert(RWDigits Z, Digits V, RWDigits scratch);
  void InvertBasecase(RWDigits Z, Digits V, RWDigits scratch);
  void InvertNewton(RWDigits Z, Digits V, RWDigits scratch);
#endif  // V8_ADVANCED_BIGINT_ALGORITHMS

  // {out_length} initially contains the allocated capacity of {out}, and
  // upon return will be set to the actual length of the result string.
  void ToString(char* out, uint32_t* out_length, Digits X, int radix,
                bool sign);
  void ToStringImpl(char* out, uint32_t* out_length, Digits X, int radix,
                    bool sign, bool use_fast_algorithm);

  void FromString(RWDigits Z, FromStringAccumulator* accumulator);
  void FromStringClassic(RWDigits Z, FromStringAccumulator* accumulator);
  void FromStringLarge(RWDigits Z, FromStringAccumulator* accumulator);
  void FromStringBasePowerOfTwo(RWDigits Z, FromStringAccumulator* accumulator);

  bool should_terminate() { return status_ == Status::kInterrupted; }

  // Each unit is supposed to represent approximately one CPU {mul} instruction.
  // Doesn't need to be accurate; we just want to make sure to check for
  // interrupt requests every now and then (roughly every 10-100 ms; often
  // enough not to appear stuck, rarely enough not to cause noticeable
  // overhead).
  static const uintptr_t kWorkEstimateThreshold = 5000000;

  void AddWorkEstimate(uintptr_t estimate) {
    work_estimate_ += estimate;
    if (work_estimate_ >= kWorkEstimateThreshold) {
      work_estimate_ = 0;
      if (platform_->InterruptRequested()) {
        status_ = Status::kInterrupted;
      }
    }
  }

 private:
  uintptr_t work_estimate_{0};
  Status status_{Status::kOk};
  Platform* platform_;
};

// These constants are primarily needed for Barrett division in div-barrett.cc,
// and they're also needed by fast to-string conversion in tostring.cc.
constexpr int DivideBarrettScratchSpace(int n) { return n + 2; }
// Local values S and W need "n plus a few" digits; U needs 2*n "plus a few".
// In all tested cases the "few" were either 2 or 3, so give 5 to be safe.
// S and W are not live at the same time.
constexpr int kInvertNewtonExtraSpace = 5;
constexpr int InvertNewtonScratchSpace(int n) {
  return 3 * n + 2 * kInvertNewtonExtraSpace;
}
constexpr int InvertScratchSpace(int n) {
  return n < kNewtonInversionThreshold ? 2 * n : InvertNewtonScratchSpace(n);
}

#define CHECK(cond)                                   \
  if (!(cond)) {                                      \
    std::cerr << __FILE__ << ":" << __LINE__ << ": "; \
    std::cerr << "Assertion failed: " #cond "\n";     \
    abort();                                          \
  }

#ifdef DEBUG
#define DCHECK(cond) CHECK(cond)
#else
#define DCHECK(cond) (void(0))
#endif

#define USE(var) ((void)var)

// RAII memory for a Digits array.
class Storage {
 public:
  explicit Storage(int count) : ptr_(new digit_t[count]) {}

  digit_t* get() { return ptr_.get(); }

 private:
  std::unique_ptr<digit_t[]> ptr_;
};

// A writable Digits array with attached storage.
class ScratchDigits : public RWDigits {
 public:
  explicit ScratchDigits(int len) : RWDigits(nullptr, len), storage_(len) {
    digits_ = storage_.get();
  }

 private:
  Storage storage_;
};

}  // namespace bigint
}  // namespace v8

#endif  // V8_BIGINT_BIGINT_INTERNAL_H_
