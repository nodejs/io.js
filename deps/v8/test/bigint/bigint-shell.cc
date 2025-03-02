// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <cmath>
#include <memory>
#include <string>

#include "src/bigint/bigint-internal.h"
#include "src/bigint/util.h"

namespace v8 {
namespace bigint {
namespace test {

int PrintHelp(char** argv) {
  std::cerr << "Usage:\n"
            << argv[0] << " --help\n"
            << "    Print this help and exit.\n"
            << argv[0] << " --list\n"
            << "    List supported tests.\n"
            << argv[0] << " <testname>\n"
            << "    Run the specified test (see --list for a list).\n"
            << "\nOptions when running tests:\n"
            << "--random-seed R\n"
            << "    Initialize the random number generator with this seed.\n"
            << "--runs N\n"
            << "    Repeat the test N times.\n";
  return 1;
}

#define TESTS(V)                     \
  V(kBarrett, "barrett")             \
  V(kBurnikel, "burnikel")           \
  V(kFFT, "fft")                     \
  V(kFromString, "fromstring")       \
  V(kFromStringBase2, "fromstring2") \
  V(kKaratsuba, "karatsuba")         \
  V(kToom, "toom")                   \
  V(kToString, "tostring")

enum Operation { kNoOp, kList, kTest };

enum Test {
#define TEST(kName, name) kName,
  TESTS(TEST)
#undef TEST
};

class RNG {
 public:
  RNG() = default;

  void Initialize(int64_t seed) {
    state0_ = MurmurHash3(static_cast<uint64_t>(seed));
    state1_ = MurmurHash3(~state0_);
    CHECK(state0_ != 0 || state1_ != 0);
  }

  uint64_t NextUint64() {
    XorShift128(&state0_, &state1_);
    return static_cast<uint64_t>(state0_ + state1_);
  }

  static inline void XorShift128(uint64_t* state0, uint64_t* state1) {
    uint64_t s1 = *state0;
    uint64_t s0 = *state1;
    *state0 = s0;
    s1 ^= s1 << 23;
    s1 ^= s1 >> 17;
    s1 ^= s0;
    s1 ^= s0 >> 26;
    *state1 = s1;
  }

  static uint64_t MurmurHash3(uint64_t h) {
    h ^= h >> 33;
    h *= uint64_t{0xFF51AFD7ED558CCD};
    h ^= h >> 33;
    h *= uint64_t{0xC4CEB9FE1A85EC53};
    h ^= h >> 33;
    return h;
  }

 private:
  uint64_t state0_;
  uint64_t state1_;
};

static constexpr int kCharsPerDigit = kDigitBits / 4;

static const char kConversionChars[] = "0123456789abcdefghijklmnopqrstuvwxyz";

std::string FormatHex(Digits X) {
  X.Normalize();
  if (X.len() == 0) return "0";
  digit_t msd = X.msd();
  const int msd_leading_zeros = CountLeadingZeros(msd);
  const size_t bit_length = X.len() * kDigitBits - msd_leading_zeros;
  const size_t chars = DIV_CEIL(bit_length, 4);

  if (chars > 100000) {
    return std::string("<BigInt with ") + std::to_string(bit_length) +
           std::string(" bits>");
  }

  std::unique_ptr<char[]> result(new char[chars]);
  for (size_t i = 0; i < chars; i++) result[i] = '?';
  // Print the number into the string, starting from the last position.
  int pos = static_cast<int>(chars - 1);
  for (int i = 0; i < X.len() - 1; i++) {
    digit_t d = X[i];
    for (int j = 0; j < kCharsPerDigit; j++) {
      result[pos--] = kConversionChars[d & 15];
      d = static_cast<digit_t>(d >> 4u);
    }
  }
  while (msd != 0) {
    result[pos--] = kConversionChars[msd & 15];
    msd = static_cast<digit_t>(msd >> 4u);
  }
  CHECK(pos == -1);
  return std::string(result.get(), chars);
}

class Runner {
 public:
  Runner() = default;

  void Initialize() {
    rng_.Initialize(random_seed_);
    processor_.reset(Processor::New(new Platform()));
  }

  ProcessorImpl* processor() {
    return static_cast<ProcessorImpl*>(processor_.get());
  }

  int Run() {
    if (op_ == kList) {
      ListTests();
    } else if (op_ == kTest) {
      RunTest();
    } else {
      DCHECK(false);  // Unreachable.
    }
    return 0;
  }

  void ListTests() {
#define PRINT(kName, name) std::cout << name << "\n";
    TESTS(PRINT)
#undef PRINT
  }

  void AssertEquals(Digits input1, Digits input2, Digits expected,
                    Digits actual) {
    if (Compare(expected, actual) == 0) return;
    std::cerr << "Input 1:  " << FormatHex(input1) << "\n";
    std::cerr << "Input 2:  " << FormatHex(input2) << "\n";
    std::cerr << "Expected: " << FormatHex(expected) << "\n";
    std::cerr << "Actual:   " << FormatHex(actual) << "\n";
    error_ = true;
  }

  void AssertEquals(Digits X, int radix, char* expected, int expected_length,
                    char* actual, int actual_length) {
    if (expected_length == actual_length &&
        std::memcmp(expected, actual, actual_length) == 0) {
      return;
    }
    std::cerr << "Input:    " << FormatHex(X) << "\n";
    std::cerr << "Radix:    " << radix << "\n";
    std::cerr << "Expected: " << std::string(expected, expected_length) << "\n";
    std::cerr << "Actual:   " << std::string(actual, actual_length) << "\n";
    error_ = true;
  }

  void AssertEquals(const char* input, int input_length, int radix,
                    Digits expected, Digits actual) {
    if (Compare(expected, actual) == 0) return;
    std::cerr << "Input:    " << std::string(input, input_length) << "\n";
    std::cerr << "Radix:    " << radix << "\n";
    std::cerr << "Expected: " << FormatHex(expected) << "\n";
    std::cerr << "Actual:   " << FormatHex(actual) << "\n";
    error_ = true;
  }

  int RunTest() {
    int count = 0;
    if (test_ == kBarrett) {
      for (int i = 0; i < runs_; i++) {
        TestBarrett(&count);
      }
    } else if (test_ == kBurnikel) {
      for (int i = 0; i < runs_; i++) {
        TestBurnikel(&count);
      }
    } else if (test_ == kFFT) {
      for (int i = 0; i < runs_; i++) {
        TestFFT(&count);
      }
    } else if (test_ == kKaratsuba) {
      for (int i = 0; i < runs_; i++) {
        TestKaratsuba(&count);
      }
    } else if (test_ == kToom) {
      for (int i = 0; i < runs_; i++) {
        TestToom(&count);
      }
    } else if (test_ == kToString) {
      for (int i = 0; i < runs_; i++) {
        TestToString(&count);
      }
    } else if (test_ == kFromString) {
      for (int i = 0; i < runs_; i++) {
        TestFromString(&count);
      }
    } else if (test_ == kFromStringBase2) {
      for (int i = 0; i < runs_; i++) {
        TestFromStringBaseTwo(&count);
      }
    } else {
      DCHECK(false);  // Unreachable.
    }
    if (error_) return 1;
    std::cout << count << " tests run, no error reported.\n";
    return 0;
  }

  void TestKaratsuba(int* count) {
    // Calling {MultiplyKaratsuba} directly is only valid if
    // left_size >= right_size and right_size >= kKaratsubaThreshold.
    constexpr int kMin = kKaratsubaThreshold;
    constexpr int kMax = 3 * kKaratsubaThreshold;
    for (int right_size = kMin; right_size <= kMax; right_size++) {
      for (int left_size = right_size; left_size <= kMax; left_size++) {
        ScratchDigits A(left_size);
        ScratchDigits B(right_size);
        int result_len = MultiplyResultLength(A, B);
        ScratchDigits result(result_len);
        ScratchDigits result_schoolbook(result_len);
        GenerateRandom(A);
        GenerateRandom(B);
        processor()->MultiplyKaratsuba(result, A, B);
        processor()->MultiplySchoolbook(result_schoolbook, A, B);
        AssertEquals(A, B, result_schoolbook, result);
        if (error_) return;
        (*count)++;
      }
    }
  }

  void TestToom(int* count) {
#if V8_ADVANCED_BIGINT_ALGORITHMS
    // {MultiplyToomCook} works fine even below the threshold, so we can
    // save some time by starting small.
    constexpr int kMin = kToomThreshold - 60;
    constexpr int kMax = kToomThreshold + 10;
    for (int right_size = kMin; right_size <= kMax; right_size++) {
      for (int left_size = right_size; left_size <= kMax; left_size++) {
        ScratchDigits A(left_size);
        ScratchDigits B(right_size);
        int result_len = MultiplyResultLength(A, B);
        ScratchDigits result(result_len);
        ScratchDigits result_karatsuba(result_len);
        GenerateRandom(A);
        GenerateRandom(B);
        processor()->MultiplyToomCook(result, A, B);
        // Using Karatsuba as reference.
        processor()->MultiplyKaratsuba(result_karatsuba, A, B);
        AssertEquals(A, B, result_karatsuba, result);
        if (error_) return;
        (*count)++;
      }
    }
#endif  // V8_ADVANCED_BIGINT_ALGORITHMS
  }

  void TestFFT(int* count) {
#if V8_ADVANCED_BIGINT_ALGORITHMS
    // Larger multiplications are slower, so to keep individual runs fast,
    // we test a few random samples. With build bots running 24/7, we'll
    // get decent coverage over time.
    uint64_t random_bits = rng_.NextUint64();
    int min = kFftThreshold - static_cast<int>(random_bits & 1023);
    random_bits >>= 10;
    int max = kFftThreshold + static_cast<int>(random_bits & 1023);
    random_bits >>= 10;
    // If delta is too small, then this run gets too slow. If it happened
    // to be zero, we'd even loop forever!
    int delta = 10 + (random_bits & 127);
    std::cout << "min " << min << " max " << max << " delta " << delta << "\n";
    for (int right_size = min; right_size <= max; right_size += delta) {
      for (int left_size = right_size; left_size <= max; left_size += delta) {
        ScratchDigits A(left_size);
        ScratchDigits B(right_size);
        int result_len = MultiplyResultLength(A, B);
        ScratchDigits result(result_len);
        ScratchDigits result_toom(result_len);
        GenerateRandom(A);
        GenerateRandom(B);
        processor()->MultiplyFFT(result, A, B);
        // Using Toom-Cook as reference.
        processor()->MultiplyToomCook(result_toom, A, B);
        AssertEquals(A, B, result_toom, result);
        if (error_) return;
        (*count)++;
      }
    }
#endif  // V8_ADVANCED_BIGINT_ALGORITHMS
  }

  void TestBurnikel(int* count) {
    // Start small to save test execution time.
    constexpr int kMin = kBurnikelThreshold / 2;
    constexpr int kMax = 2 * kBurnikelThreshold;
    for (int right_size = kMin; right_size <= kMax; right_size++) {
      for (int left_size = right_size; left_size <= kMax; left_size++) {
        ScratchDigits A(left_size);
        ScratchDigits B(right_size);
        GenerateRandom(A);
        GenerateRandom(B);
        int quotient_len = DivideResultLength(A, B);
        int remainder_len = right_size;
        ScratchDigits quotient(quotient_len);
        ScratchDigits quotient_schoolbook(quotient_len);
        ScratchDigits remainder(remainder_len);
        ScratchDigits remainder_schoolbook(remainder_len);
        processor()->DivideBurnikelZiegler(quotient, remainder, A, B);
        processor()->DivideSchoolbook(quotient_schoolbook, remainder_schoolbook,
                                      A, B);
        AssertEquals(A, B, quotient_schoolbook, quotient);
        AssertEquals(A, B, remainder_schoolbook, remainder);
        if (error_) return;
        (*count)++;
      }
    }
  }

#if V8_ADVANCED_BIGINT_ALGORITHMS
  void TestBarrett_Internal(int left_size, int right_size) {
    ScratchDigits A(left_size);
    ScratchDigits B(right_size);
    GenerateRandom(A);
    GenerateRandom(B);
    int quotient_len = DivideResultLength(A, B);
    // {DivideResultLength} doesn't expect to be called for sizes below
    // {kBarrettThreshold} (which we do here to save time), so we have to
    // manually adjust the allocated result length.
    if (B.len() < kBarrettThreshold) quotient_len++;
    int remainder_len = right_size;
    ScratchDigits quotient(quotient_len);
    ScratchDigits quotient_burnikel(quotient_len);
    ScratchDigits remainder(remainder_len);
    ScratchDigits remainder_burnikel(remainder_len);
    processor()->DivideBarrett(quotient, remainder, A, B);
    processor()->DivideBurnikelZiegler(quotient_burnikel, remainder_burnikel, A,
                                       B);
    AssertEquals(A, B, quotient_burnikel, quotient);
    AssertEquals(A, B, remainder_burnikel, remainder);
  }

  void TestBarrett(int* count) {
    // We pick a range around kBurnikelThreshold (instead of kBarrettThreshold)
    // to save test execution time.
    constexpr int kMin = kBurnikelThreshold / 2;
    constexpr int kMax = 2 * kBurnikelThreshold;
    // {DivideBarrett(A, B)} requires that A.len > B.len!
    for (int right_size = kMin; right_size <= kMax; right_size++) {
      for (int left_size = right_size + 1; left_size <= kMax; left_size++) {
        TestBarrett_Internal(left_size, right_size);
        if (error_) return;
        (*count)++;
      }
    }
    // We also test one random large case.
    uint64_t random_bits = rng_.NextUint64();
    int right_size = kBarrettThreshold + static_cast<int>(random_bits & 0x3FF);
    random_bits >>= 10;
    int left_size = right_size + 1 + static_cast<int>(random_bits & 0x3FFF);
    random_bits >>= 14;
    TestBarrett_Internal(left_size, right_size);
    if (error_) return;
    (*count)++;
  }
#else
  void TestBarrett(int* count) {}
#endif  // V8_ADVANCED_BIGINT_ALGORITHMS

  void TestToString(int* count) {
    constexpr int kMin = kToStringFastThreshold / 2;
    constexpr int kMax = kToStringFastThreshold * 2;
    for (int size = kMin; size < kMax; size++) {
      ScratchDigits X(size);
      GenerateRandom(X);
      for (int radix = 2; radix <= 36; radix++) {
        uint32_t chars_required = ToStringResultLength(X, radix, false);
        uint32_t result_len = chars_required;
        uint32_t reference_len = chars_required;
        std::unique_ptr<char[]> result(new char[result_len]);
        std::unique_ptr<char[]> reference(new char[reference_len]);
        processor()->ToStringImpl(result.get(), &result_len, X, radix, false,
                                  true);
        processor()->ToStringImpl(reference.get(), &reference_len, X, radix,
                                  false, false);
        AssertEquals(X, radix, reference.get(), reference_len, result.get(),
                     result_len);
        if (error_) return;
        (*count)++;
      }
    }
  }

  void TestFromString(int* count) {
    constexpr int kMaxDigits = 1 << 20;  // Any large-enough value will do.
    constexpr int kMin = kFromStringLargeThreshold / 2;
    constexpr int kMax = kFromStringLargeThreshold * 2;
    for (int size = kMin; size < kMax; size++) {
      // To keep test execution times low, test one random radix every time.
      // Generally, radixes 2 through 36 (inclusive) are supported; however
      // the functions {FromStringLarge} and {FromStringClassic} can't deal
      // with the data format that {Parse} creates for power-of-two radixes,
      // so we skip power-of-two radixes here (and test them separately below).
      // We round up the number of radixes in the list to 32 by padding with
      // 10, giving decimal numbers extra test coverage, and making it easy
      // to evenly map a random number into the index space.
      constexpr uint8_t radixes[] = {3,  5,  6,  7,  9,  10, 11, 12, 13, 14, 15,
                                     17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27,
                                     28, 29, 30, 31, 33, 34, 35, 36, 10, 10};
      int radix_index = (rng_.NextUint64() & 31);
      int radix = radixes[radix_index];
      int num_chars = std::round(size * kDigitBits / std::log2(radix));
      std::unique_ptr<char[]> chars(new char[num_chars]);
      GenerateRandomString(chars.get(), num_chars, radix);
      FromStringAccumulator accumulator(kMaxDigits);
      FromStringAccumulator ref_accumulator(kMaxDigits);
      const char* start = chars.get();
      const char* end = chars.get() + num_chars;
      accumulator.Parse(start, end, radix);
      ref_accumulator.Parse(start, end, radix);
      ScratchDigits result(accumulator.ResultLength());
      ScratchDigits reference(ref_accumulator.ResultLength());
      processor()->FromStringLarge(result, &accumulator);
      processor()->FromStringClassic(reference, &ref_accumulator);
      AssertEquals(start, num_chars, radix, result, reference);
      if (error_) return;
      (*count)++;
    }
  }

  void TestFromStringBaseTwo(int* count) {
    constexpr int kMaxDigits = 1 << 20;  // Any large-enough value will do.
    constexpr int kMin = 1;
    constexpr int kMax = 100;
    for (int size = kMin; size < kMax; size++) {
      ScratchDigits X(size);
      GenerateRandom(X);
      for (int bits = 1; bits <= 5; bits++) {
        uint32_t radix = 1 << bits;
        uint32_t chars_required = ToStringResultLength(X, radix, false);
        uint32_t string_len = chars_required;
        std::unique_ptr<char[]> chars(new char[string_len]);
        processor()->ToStringImpl(chars.get(), &string_len, X, radix, false,
                                  true);
        // Fill any remaining allocated characters with garbage to test that
        // too.
        for (uint32_t i = string_len; i < chars_required; i++) {
          chars[i] = '?';
        }
        const char* start = chars.get();
        const char* end = start + chars_required;
        FromStringAccumulator accumulator(kMaxDigits);
        accumulator.Parse(start, end, radix);
        ScratchDigits result(accumulator.ResultLength());
        processor()->FromString(result, &accumulator);
        AssertEquals(start, chars_required, radix, X, result);
        if (error_) return;
        (*count)++;
      }
    }
  }

  template <typename I>
  bool ParseInt(char* s, I* out) {
    char* end;
    if (s[0] == '\0') return false;
    errno = 0;
    long l = strtol(s, &end, 10);
    if (errno != 0 || *end != '\0' || l > std::numeric_limits<I>::max() ||
        l < std::numeric_limits<I>::min()) {
      return false;
    }
    *out = static_cast<I>(l);
    return true;
  }

  int ParseOptions(int argc, char** argv) {
    for (int i = 1; i < argc; i++) {
      if (strcmp(argv[i], "--list") == 0) {
        op_ = kList;
      } else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
        PrintHelp(argv);
        return 0;
      } else if (strcmp(argv[i], "--random-seed") == 0 ||
                 strcmp(argv[i], "--random_seed") == 0) {
        if (++i == argc || !ParseInt(argv[i], &random_seed_)) {
          return PrintHelp(argv);
        }
      } else if (strncmp(argv[i], "--random-seed=", 14) == 0 ||
                 strncmp(argv[i], "--random_seed=", 14) == 0) {
        if (!ParseInt(argv[i] + 14, &random_seed_)) return PrintHelp(argv);
      } else if (strcmp(argv[i], "--runs") == 0) {
        if (++i == argc || !ParseInt(argv[i], &runs_)) return PrintHelp(argv);
      } else if (strncmp(argv[i], "--runs=", 7) == 0) {
        if (!ParseInt(argv[i] + 7, &runs_)) return PrintHelp(argv);
      }
#define TEST(kName, name)                \
  else if (strcmp(argv[i], name) == 0) { \
    op_ = kTest;                         \
    test_ = kName;                       \
  }
      TESTS(TEST)
#undef TEST
      else {
        std::cerr << "Warning: ignored argument: " << argv[i] << "\n";
      }
    }
    if (op_ == kNoOp) return PrintHelp(argv);  // op is mandatory.
    return 0;
  }

 private:
  void GenerateRandom(RWDigits Z) {
    if (Z.len() == 0) return;
    int mode = static_cast<int>(rng_.NextUint64() & 3);
    if (mode == 0) {
      // Generate random bits.
      if (sizeof(digit_t) == 8) {
        for (int i = 0; i < Z.len(); i++) {
          Z[i] = static_cast<digit_t>(rng_.NextUint64());
        }
      } else {
        for (int i = 0; i < Z.len(); i += 2) {
          uint64_t random = rng_.NextUint64();
          Z[i] = static_cast<digit_t>(random);
          if (i + 1 < Z.len()) Z[i + 1] = static_cast<digit_t>(random >> 32);
        }
      }
      // Special case: we don't want the MSD to be zero.
      while (Z.msd() == 0) {
        Z[Z.len() - 1] = static_cast<digit_t>(rng_.NextUint64());
      }
      return;
    }
    if (mode == 1) {
      // Generate a power of 2, with the lone 1-bit somewhere in the MSD.
      int bit_in_msd = static_cast<int>(rng_.NextUint64() % kDigitBits);
      Z[Z.len() - 1] = digit_t{1} << bit_in_msd;
      for (int i = 0; i < Z.len() - 1; i++) Z[i] = 0;
      return;
    }
    // For mode == 2 and mode == 3, generate a random number of 1-bits in the
    // MSD, aligned to the least-significant end.
    int bits_in_msd = static_cast<int>(rng_.NextUint64() % kDigitBits);
    digit_t msd = (digit_t{1} << bits_in_msd) - 1;
    if (msd == 0) msd = ~digit_t{0};
    Z[Z.len() - 1] = msd;
    if (mode == 2) {
      // The non-MSD digits are all 1-bits.
      for (int i = 0; i < Z.len() - 1; i++) Z[i] = ~digit_t{0};
    } else {
      // mode == 3
      // Each non-MSD digit is either all ones or all zeros.
      uint64_t random;
      int random_bits = 0;
      for (int i = 0; i < Z.len() - 1; i++) {
        if (random_bits == 0) {
          random = rng_.NextUint64();
          random_bits = 64;
        }
        Z[i] = random & 1 ? ~digit_t{0} : digit_t{0};
        random >>= 1;
        random_bits--;
      }
    }
  }

  void GenerateRandomString(char* str, int len, int radix) {
    DCHECK(2 <= radix && radix <= 36);
    if (len == 0) return;
    uint64_t random;
    int available_bits = 0;
    const int char_bits = BitLength(radix - 1);
    const uint64_t char_mask = (1u << char_bits) - 1u;
    for (int i = 0; i < len; i++) {
      while (true) {
        if (available_bits < char_bits) {
          random = rng_.NextUint64();
          available_bits = 64;
        }
        int next_char = static_cast<int>(random & char_mask);
        random = random >> char_bits;
        available_bits -= char_bits;
        if (next_char >= radix) continue;
        *str = kConversionChars[next_char];
        str++;
        break;
      };
    }
  }

  Operation op_{kNoOp};
  Test test_;
  bool error_{false};
  int runs_ = 1;
  int64_t random_seed_{314159265359};
  RNG rng_;
  std::unique_ptr<Processor, Processor::Destroyer> processor_;
};

}  // namespace test
}  // namespace bigint
}  // namespace v8

int main(int argc, char** argv) {
  v8::bigint::test::Runner runner;
  int ret = runner.ParseOptions(argc, argv);
  if (ret != 0) return ret;
  runner.Initialize();
  return runner.Run();
}
