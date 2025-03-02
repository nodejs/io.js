// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <cstdint>
#include <cstdio>
#include <ctime>
#include <iostream>  // cerr
#include <random>
#include <vector>

// clang-format off
#undef HWY_TARGET_INCLUDE
#define HWY_TARGET_INCLUDE "hwy/contrib/random/random_test.cc"  // NOLINT
#include "hwy/foreach_target.h"  // IWYU pragma: keep
#include "hwy/highway.h"
#include "hwy/contrib/random/random-inl.h"
#include "hwy/tests/test_util-inl.h"
// clang-format on

HWY_BEFORE_NAMESPACE();
namespace hwy {
namespace HWY_NAMESPACE {  // required: unique per target
namespace {

constexpr std::uint64_t tests = 1UL << 10;

std::uint64_t GetSeed() { return static_cast<uint64_t>(std::time(nullptr)); }

void RngLoop(const std::uint64_t seed, std::uint64_t* HWY_RESTRICT result,
             const size_t size) {
  const ScalableTag<std::uint64_t> d;
  VectorXoshiro generator{seed};
  for (size_t i = 0; i < size; i += Lanes(d)) {
    Store(generator(), d, result + i);
  }
}

#if HWY_HAVE_FLOAT64
void UniformLoop(const std::uint64_t seed, double* HWY_RESTRICT result,
                 const size_t size) {
  const ScalableTag<double> d;
  VectorXoshiro generator{seed};
  for (size_t i = 0; i < size; i += Lanes(d)) {
    Store(generator.Uniform(), d, result + i);
  }
}
#endif

void TestSeeding() {
  const std::uint64_t seed = GetSeed();
  VectorXoshiro generator{seed};
  internal::Xoshiro reference{seed};
  const auto& state = generator.GetState();
  const ScalableTag<std::uint64_t> d;
  const std::size_t lanes = Lanes(d);
  for (std::size_t i = 0UL; i < lanes; ++i) {
    const auto& reference_state = reference.GetState();
    for (std::size_t j = 0UL; j < reference_state.size(); ++j) {
      if (state[{j}][i] != reference_state[j]) {
        std::cerr << "SEED: " << seed << "\n";
        std::cerr << "TEST SEEDING ERROR: ";
        std::cerr << "state[" << j << "][" << i << "] -> " << state[{j}][i]
                  << " != " << reference_state[j] << "\n";
        HWY_ASSERT(0);
      }
    }
    reference.Jump();
  }
}

void TestMultiThreadSeeding() {
  const std::uint64_t seed = GetSeed();
  const std::uint64_t threadId = std::random_device()() % 1000;
  VectorXoshiro generator{seed, threadId};
  internal::Xoshiro reference{seed};

  for (std::size_t i = 0UL; i < threadId; ++i) {
    reference.LongJump();
  }

  const auto& state = generator.GetState();
  const ScalableTag<std::uint64_t> d;
  const std::size_t lanes = Lanes(d);
  for (std::size_t i = 0UL; i < lanes; ++i) {
    const auto& reference_state = reference.GetState();
    for (std::size_t j = 0UL; j < reference_state.size(); ++j) {
      if (state[{j}][i] != reference_state[j]) {
        std::cerr << "SEED: " << seed << std::endl;
        std::cerr << "TEST SEEDING ERROR: ";
        std::cerr << "state[" << j << "][" << i << "] -> " << state[{j}][i]
                  << " != " << reference_state[j] << "\n";
        HWY_ASSERT(0);
      }
    }
    reference.Jump();
  }
}

void TestRandomUint64() {
  const std::uint64_t seed = GetSeed();
  const auto result_array = hwy::MakeUniqueAlignedArray<std::uint64_t>(tests);
  RngLoop(seed, result_array.get(), tests);
  std::vector<internal::Xoshiro> reference;
  reference.emplace_back(seed);
  const ScalableTag<std::uint64_t> d;
  const std::size_t lanes = Lanes(d);
  for (std::size_t i = 1UL; i < lanes; ++i) {
    auto rng = reference.back();
    rng.Jump();
    reference.emplace_back(rng);
  }

  for (std::size_t i = 0UL; i < tests; i += lanes) {
    for (std::size_t lane = 0UL; lane < lanes; ++lane) {
      const std::uint64_t result = reference[lane]();
      if (result_array[i + lane] != result) {
        std::cerr << "SEED: " << seed << std::endl;
        std::cerr << "TEST UINT64 GENERATOR ERROR: result_array[" << i + lane
                  << "] -> " << result_array[i + lane] << " != " << result
                  << std::endl;
        HWY_ASSERT(0);
      }
    }
  }
}
void TestUniformDist() {
#if HWY_HAVE_FLOAT64
  const std::uint64_t seed = GetSeed();
  const auto result_array = hwy::MakeUniqueAlignedArray<double>(tests);
  UniformLoop(seed, result_array.get(), tests);
  internal::Xoshiro reference{seed};
  const ScalableTag<double> d;
  const std::size_t lanes = Lanes(d);
  for (std::size_t i = 0UL; i < tests; i += lanes) {
    const double result = reference.Uniform();
    if (result_array[i] != result) {
      std::cerr << "SEED: " << seed << std::endl;
      std::cerr << "TEST UNIFORM GENERATOR ERROR: result_array[" << i << "] -> "
                << result_array[i] << " != " << result << std::endl;
      HWY_ASSERT(0);
    }
  }
#endif  // HWY_HAVE_FLOAT64
}

void TestNextNRandomUint64() {
  const std::uint64_t seed = GetSeed();
  VectorXoshiro generator{seed};
  const auto result_array = generator.operator()(tests);
  std::vector<internal::Xoshiro> reference;
  reference.emplace_back(seed);
  const ScalableTag<std::uint64_t> d;
  const std::size_t lanes = Lanes(d);
  for (std::size_t i = 1UL; i < lanes; ++i) {
    auto rng = reference.back();
    rng.Jump();
    reference.emplace_back(rng);
  }

  for (std::size_t i = 0UL; i < tests; i += lanes) {
    for (std::size_t lane = 0UL; lane < lanes; ++lane) {
      const std::uint64_t result = reference[lane]();
      if (result_array[i + lane] != result) {
        std::cerr << "SEED: " << seed << std::endl;
        std::cerr << "TEST UINT64 GENERATOR ERROR: result_array[" << i + lane
                  << "] -> " << result_array[i + lane] << " != " << result
                  << std::endl;
        HWY_ASSERT(0);
      }
    }
  }
}

void TestNextFixedNRandomUint64() {
  const std::uint64_t seed = GetSeed();
  VectorXoshiro generator{seed};
  const auto result_array = generator.operator()<tests>();
  std::vector<internal::Xoshiro> reference;
  reference.emplace_back(seed);
  const ScalableTag<std::uint64_t> d;
  const std::size_t lanes = Lanes(d);
  for (std::size_t i = 1UL; i < lanes; ++i) {
    auto rng = reference.back();
    rng.Jump();
    reference.emplace_back(rng);
  }

  for (std::size_t i = 0UL; i < tests; i += lanes) {
    for (std::size_t lane = 0UL; lane < lanes; ++lane) {
      const std::uint64_t result = reference[lane]();
      if (result_array[i + lane] != result) {
        std::cerr << "SEED: " << seed << std::endl;
        std::cerr << "TEST UINT64 GENERATOR ERROR: result_array[" << i + lane
                  << "] -> " << result_array[i + lane] << " != " << result
                  << std::endl;

        HWY_ASSERT(0);
      }
    }
  }
}
void TestNextNUniformDist() {
#if HWY_HAVE_FLOAT64
  const std::uint64_t seed = GetSeed();
  VectorXoshiro generator{seed};
  const auto result_array = generator.Uniform(tests);
  internal::Xoshiro reference{seed};
  const ScalableTag<double> d;
  const std::size_t lanes = Lanes(d);
  for (std::size_t i = 0UL; i < tests; i += lanes) {
    const double result = reference.Uniform();
    if (result_array[i] != result) {
      std::cerr << "SEED: " << seed << std::endl;
      std::cerr << "TEST UNIFORM GENERATOR ERROR: result_array[" << i << "] -> "
                << result_array[i] << " != " << result << std::endl;

      HWY_ASSERT(0);
    }
  }
#endif  // HWY_HAVE_FLOAT64
}

void TestNextFixedNUniformDist() {
#if HWY_HAVE_FLOAT64
  const std::uint64_t seed = GetSeed();
  VectorXoshiro generator{seed};
  const auto result_array = generator.Uniform<tests>();
  internal::Xoshiro reference{seed};
  const ScalableTag<double> d;
  const std::size_t lanes = Lanes(d);
  for (std::size_t i = 0UL; i < tests; i += lanes) {
    const double result = reference.Uniform();
    if (result_array[i] != result) {
      std::cerr << "SEED: " << seed << std::endl;
      std::cerr << "TEST UNIFORM GENERATOR ERROR: result_array[" << i << "] -> "
                << result_array[i] << " != " << result << std::endl;
      HWY_ASSERT(0);
    }
  }
#endif  // HWY_HAVE_FLOAT64
}

void TestCachedXorshiro() {
  const std::uint64_t seed = GetSeed();

  CachedXoshiro<> generator{seed};
  std::vector<internal::Xoshiro> reference;
  reference.emplace_back(seed);
  const ScalableTag<std::uint64_t> d;
  const std::size_t lanes = Lanes(d);
  for (std::size_t i = 1UL; i < lanes; ++i) {
    auto rng = reference.back();
    rng.Jump();
    reference.emplace_back(rng);
  }

  for (std::size_t i = 0UL; i < tests; i += lanes) {
    for (std::size_t lane = 0UL; lane < lanes; ++lane) {
      const std::uint64_t result = reference[lane]();
      const std::uint64_t got = generator();
      if (got != result) {
        std::cerr << "SEED: " << seed << std::endl;
        std::cerr << "TEST CachedXoshiro GENERATOR ERROR: result_array["
                  << i + lane << "] -> " << got << " != " << result
                  << std::endl;

        HWY_ASSERT(0);
      }
    }
  }
}
void TestUniformCachedXorshiro() {
#if HWY_HAVE_FLOAT64
  const std::uint64_t seed = GetSeed();

  CachedXoshiro<> generator{seed};
  std::uniform_real_distribution<double> distribution{0., 1.};
  for (std::size_t i = 0UL; i < tests; ++i) {
    const double result = distribution(generator);

    if (result < 0. || result >= 1.) {
      std::cerr << "SEED: " << seed << std::endl;
      std::cerr << "TEST CachedXoshiro GENERATOR ERROR: result_array[" << i
                << "] -> " << result << " not in interval [0, 1)" << std::endl;
      HWY_ASSERT(0);
    }
  }
#endif  // HWY_HAVE_FLOAT64
}

}  // namespace
// NOLINTNEXTLINE(google-readability-namespace-comments)
}  // namespace HWY_NAMESPACE
}  // namespace hwy
HWY_AFTER_NAMESPACE();  // required if not using HWY_ATTR

#if HWY_ONCE
namespace hwy {
namespace {
HWY_BEFORE_TEST(HwyRandomTest);
HWY_EXPORT_AND_TEST_P(HwyRandomTest, TestSeeding);
HWY_EXPORT_AND_TEST_P(HwyRandomTest, TestMultiThreadSeeding);
HWY_EXPORT_AND_TEST_P(HwyRandomTest, TestRandomUint64);
HWY_EXPORT_AND_TEST_P(HwyRandomTest, TestNextNRandomUint64);
HWY_EXPORT_AND_TEST_P(HwyRandomTest, TestNextFixedNRandomUint64);
HWY_EXPORT_AND_TEST_P(HwyRandomTest, TestCachedXorshiro);
HWY_EXPORT_AND_TEST_P(HwyRandomTest, TestUniformDist);
HWY_EXPORT_AND_TEST_P(HwyRandomTest, TestNextNUniformDist);
HWY_EXPORT_AND_TEST_P(HwyRandomTest, TestNextFixedNUniformDist);
HWY_EXPORT_AND_TEST_P(HwyRandomTest, TestUniformCachedXorshiro);
HWY_AFTER_TEST();
}  // namespace
}  // namespace hwy
HWY_TEST_MAIN();
#endif  // HWY_ONCE
