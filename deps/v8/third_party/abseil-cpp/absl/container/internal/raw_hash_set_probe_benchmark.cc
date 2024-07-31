// Copyright 2018 The Abseil Authors.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      https://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
// Generates probe length statistics for many combinations of key types and key
// distributions, all using the default hash function for swisstable.

#include <memory>
#include <regex>  // NOLINT
#include <vector>

#include "absl/base/no_destructor.h"
#include "absl/container/flat_hash_map.h"
#include "absl/container/internal/hash_function_defaults.h"
#include "absl/container/internal/hashtable_debug.h"
#include "absl/container/internal/raw_hash_set.h"
#include "absl/random/distributions.h"
#include "absl/random/random.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/strings/string_view.h"
#include "absl/strings/strip.h"
#include "absl/types/optional.h"

namespace {

enum class OutputStyle { kRegular, kBenchmark };

// The --benchmark command line flag.
// This is populated from main().
// When run in "benchmark" mode, we have different output. This allows
// A/B comparisons with tools like `benchy`.
absl::string_view benchmarks;

OutputStyle output() {
  return !benchmarks.empty() ? OutputStyle::kBenchmark : OutputStyle::kRegular;
}

template <class T>
struct Policy {
  using slot_type = T;
  using key_type = T;
  using init_type = T;

  template <class allocator_type, class Arg>
  static void construct(allocator_type* alloc, slot_type* slot,
                        const Arg& arg) {
    std::allocator_traits<allocator_type>::construct(*alloc, slot, arg);
  }

  template <class allocator_type>
  static void destroy(allocator_type* alloc, slot_type* slot) {
    std::allocator_traits<allocator_type>::destroy(*alloc, slot);
  }

  static slot_type& element(slot_type* slot) { return *slot; }

  template <class F, class... Args>
  static auto apply(F&& f, const slot_type& arg)
      -> decltype(std::forward<F>(f)(arg, arg)) {
    return std::forward<F>(f)(arg, arg);
  }

  template <class Hash>
  static constexpr auto get_hash_slot_fn() {
    return nullptr;
  }
};

absl::BitGen& GlobalBitGen() {
  static absl::NoDestructor<absl::BitGen> value;
  return *value;
}

// Keeps a pool of allocations and randomly gives one out.
// This introduces more randomization to the addresses given to swisstable and
// should help smooth out this factor from probe length calculation.
template <class T>
class RandomizedAllocator {
 public:
  using value_type = T;

  RandomizedAllocator() = default;
  template <typename U>
  RandomizedAllocator(RandomizedAllocator<U>) {}  // NOLINT

  static T* allocate(size_t n) {
    auto& pointers = GetPointers(n);
    // Fill the pool
    while (pointers.size() < kRandomPool) {
      pointers.push_back(std::allocator<T>{}.allocate(n));
    }

    // Choose a random one.
    size_t i = absl::Uniform<size_t>(GlobalBitGen(), 0, pointers.size());
    T* result = pointers[i];
    pointers[i] = pointers.back();
    pointers.pop_back();
    return result;
  }

  static void deallocate(T* p, size_t n) {
    // Just put it back on the pool. No need to release the memory.
    GetPointers(n).push_back(p);
  }

 private:
  // We keep at least kRandomPool allocations for each size.
  static constexpr size_t kRandomPool = 20;

  static std::vector<T*>& GetPointers(size_t n) {
    static absl::NoDestructor<absl::flat_hash_map<size_t, std::vector<T*>>> m;
    return (*m)[n];
  }
};

template <class T>
struct DefaultHash {
  using type = absl::container_internal::hash_default_hash<T>;
};

template <class T>
using DefaultHashT = typename DefaultHash<T>::type;

template <class T>
struct Table : absl::container_internal::raw_hash_set<
                   Policy<T>, DefaultHashT<T>,
                   absl::container_internal::hash_default_eq<T>,
                   RandomizedAllocator<T>> {};

struct LoadSizes {
  size_t min_load;
  size_t max_load;
};

LoadSizes GetMinMaxLoadSizes() {
  static const auto sizes = [] {
    Table<int> t;

    // First, fill enough to have a good distribution.
    constexpr size_t kMinSize = 10000;
    while (t.size() < kMinSize) t.insert(t.size());

    const auto reach_min_load_factor = [&] {
      const double lf = t.load_factor();
      while (lf <= t.load_factor()) t.insert(t.size());
    };

    // Then, insert until we reach min load factor.
    reach_min_load_factor();
    const size_t min_load_size = t.size();

    // Keep going until we hit min load factor again, then go back one.
    t.insert(t.size());
    reach_min_load_factor();

    return LoadSizes{min_load_size, t.size() - 1};
  }();
  return sizes;
}

struct Ratios {
  double min_load;
  double avg_load;
  double max_load;
};

// See absl/container/internal/hashtable_debug.h for details on
// probe length calculation.
template <class ElemFn>
Ratios CollectMeanProbeLengths() {
  const auto min_max_sizes = GetMinMaxLoadSizes();

  ElemFn elem;
  using Key = decltype(elem());
  Table<Key> t;

  Ratios result;
  while (t.size() < min_max_sizes.min_load) t.insert(elem());
  result.min_load =
      absl::container_internal::GetHashtableDebugProbeSummary(t).mean;

  while (t.size() < (min_max_sizes.min_load + min_max_sizes.max_load) / 2)
    t.insert(elem());
  result.avg_load =
      absl::container_internal::GetHashtableDebugProbeSummary(t).mean;

  while (t.size() < min_max_sizes.max_load) t.insert(elem());
  result.max_load =
      absl::container_internal::GetHashtableDebugProbeSummary(t).mean;

  return result;
}

template <int Align>
uintptr_t PointerForAlignment() {
  alignas(Align) static constexpr uintptr_t kInitPointer = 0;
  return reinterpret_cast<uintptr_t>(&kInitPointer);
}

// This incomplete type is used for testing hash of pointers of different
// alignments.
// NOTE: We are generating invalid pointer values on the fly with
// reinterpret_cast. There are not "safely derived" pointers so using them is
// technically UB. It is unlikely to be a problem, though.
template <int Align>
struct Ptr;

template <int Align>
Ptr<Align>* MakePtr(uintptr_t v) {
  if (sizeof(v) == 8) {
    constexpr int kCopyBits = 16;
    // Ensure high bits are all the same.
    v = static_cast<uintptr_t>(static_cast<intptr_t>(v << kCopyBits) >>
                               kCopyBits);
  }
  return reinterpret_cast<Ptr<Align>*>(v);
}

struct IntIdentity {
  uint64_t i;
  friend bool operator==(IntIdentity a, IntIdentity b) { return a.i == b.i; }
  IntIdentity operator++(int) { return IntIdentity{i++}; }
};

template <int Align>
struct PtrIdentity {
  explicit PtrIdentity(uintptr_t val = PointerForAlignment<Align>()) : i(val) {}
  uintptr_t i;
  friend bool operator==(PtrIdentity a, PtrIdentity b) { return a.i == b.i; }
  PtrIdentity operator++(int) {
    PtrIdentity p(i);
    i += Align;
    return p;
  }
};

constexpr char kStringFormat[] = "/path/to/file/name-%07d-of-9999999.txt";

template <bool small>
struct String {
  std::string value;
  static std::string Make(uint32_t v) {
    return {small ? absl::StrCat(v) : absl::StrFormat(kStringFormat, v)};
  }
};

template <>
struct DefaultHash<IntIdentity> {
  struct type {
    size_t operator()(IntIdentity t) const { return t.i; }
  };
};

template <int Align>
struct DefaultHash<PtrIdentity<Align>> {
  struct type {
    size_t operator()(PtrIdentity<Align> t) const { return t.i; }
  };
};

template <class T>
struct Sequential {
  T operator()() const { return current++; }
  mutable T current{};
};

template <int Align>
struct Sequential<Ptr<Align>*> {
  Ptr<Align>* operator()() const {
    auto* result = MakePtr<Align>(current);
    current += Align;
    return result;
  }
  mutable uintptr_t current = PointerForAlignment<Align>();
};


template <bool small>
struct Sequential<String<small>> {
  std::string operator()() const { return String<small>::Make(current++); }
  mutable uint32_t current = 0;
};

template <class T, class U>
struct Sequential<std::pair<T, U>> {
  mutable Sequential<T> tseq;
  mutable Sequential<U> useq;

  using RealT = decltype(tseq());
  using RealU = decltype(useq());

  mutable std::vector<RealT> ts;
  mutable std::vector<RealU> us;
  mutable size_t ti = 0, ui = 0;

  std::pair<RealT, RealU> operator()() const {
    std::pair<RealT, RealU> value{get_t(), get_u()};
    if (ti == 0) {
      ti = ui + 1;
      ui = 0;
    } else {
      --ti;
      ++ui;
    }
    return value;
  }

  RealT get_t() const {
    while (ti >= ts.size()) ts.push_back(tseq());
    return ts[ti];
  }

  RealU get_u() const {
    while (ui >= us.size()) us.push_back(useq());
    return us[ui];
  }
};

template <class T, int percent_skip>
struct AlmostSequential {
  mutable Sequential<T> current;

  auto operator()() const -> decltype(current()) {
    while (absl::Uniform(GlobalBitGen(), 0.0, 1.0) <= percent_skip / 100.)
      current();
    return current();
  }
};

struct Uniform {
  template <typename T>
  T operator()(T) const {
    return absl::Uniform<T>(absl::IntervalClosed, GlobalBitGen(), T{0}, ~T{0});
  }
};

struct Gaussian {
  template <typename T>
  T operator()(T) const {
    double d;
    do {
      d = absl::Gaussian<double>(GlobalBitGen(), 1e6, 1e4);
    } while (d <= 0 || d > std::numeric_limits<T>::max() / 2);
    return static_cast<T>(d);
  }
};

struct Zipf {
  template <typename T>
  T operator()(T) const {
    return absl::Zipf<T>(GlobalBitGen(), std::numeric_limits<T>::max(), 1.6);
  }
};

template <class T, class Dist>
struct Random {
  T operator()() const { return Dist{}(T{}); }
};

template <class Dist, int Align>
struct Random<Ptr<Align>*, Dist> {
  Ptr<Align>* operator()() const {
    return MakePtr<Align>(Random<uintptr_t, Dist>{}() * Align);
  }
};

template <class Dist>
struct Random<IntIdentity, Dist> {
  IntIdentity operator()() const {
    return IntIdentity{Random<uint64_t, Dist>{}()};
  }
};

template <class Dist, int Align>
struct Random<PtrIdentity<Align>, Dist> {
  PtrIdentity<Align> operator()() const {
    return PtrIdentity<Align>{Random<uintptr_t, Dist>{}() * Align};
  }
};

template <class Dist, bool small>
struct Random<String<small>, Dist> {
  std::string operator()() const {
    return String<small>::Make(Random<uint32_t, Dist>{}());
  }
};

template <class T, class U, class Dist>
struct Random<std::pair<T, U>, Dist> {
  auto operator()() const
      -> decltype(std::make_pair(Random<T, Dist>{}(), Random<U, Dist>{}())) {
    return std::make_pair(Random<T, Dist>{}(), Random<U, Dist>{}());
  }
};

template <typename>
std::string Name();

std::string Name(uint32_t*) { return "u32"; }
std::string Name(uint64_t*) { return "u64"; }
std::string Name(IntIdentity*) { return "IntIdentity"; }

template <int Align>
std::string Name(Ptr<Align>**) {
  return absl::StrCat("Ptr", Align);
}

template <int Align>
std::string Name(PtrIdentity<Align>*) {
  return absl::StrCat("PtrIdentity", Align);
}

template <bool small>
std::string Name(String<small>*) {
  return small ? "StrS" : "StrL";
}

template <class T, class U>
std::string Name(std::pair<T, U>*) {
  if (output() == OutputStyle::kBenchmark)
    return absl::StrCat("P_", Name<T>(), "_", Name<U>());
  return absl::StrCat("P<", Name<T>(), ",", Name<U>(), ">");
}

template <class T>
std::string Name(Sequential<T>*) {
  return "Sequential";
}

template <class T, int P>
std::string Name(AlmostSequential<T, P>*) {
  return absl::StrCat("AlmostSeq_", P);
}

template <class T>
std::string Name(Random<T, Uniform>*) {
  return "UnifRand";
}

template <class T>
std::string Name(Random<T, Gaussian>*) {
  return "GausRand";
}

template <class T>
std::string Name(Random<T, Zipf>*) {
  return "ZipfRand";
}

template <typename T>
std::string Name() {
  return Name(static_cast<T*>(nullptr));
}

constexpr int kNameWidth = 15;
constexpr int kDistWidth = 16;

bool CanRunBenchmark(absl::string_view name) {
  static const absl::NoDestructor<absl::optional<std::regex>> filter([] {
    return benchmarks.empty() || benchmarks == "all"
               ? absl::nullopt
               : absl::make_optional(std::regex(std::string(benchmarks)));
  }());
  return !filter->has_value() || std::regex_search(std::string(name), **filter);
}

struct Result {
  std::string name;
  std::string dist_name;
  Ratios ratios;
};

template <typename T, typename Dist>
void RunForTypeAndDistribution(std::vector<Result>& results) {
  std::string name = absl::StrCat(Name<T>(), "/", Name<Dist>());
  // We have to check against all three names (min/avg/max) before we run it.
  // If any of them is enabled, we run it.
  if (!CanRunBenchmark(absl::StrCat(name, "/min")) &&
      !CanRunBenchmark(absl::StrCat(name, "/avg")) &&
      !CanRunBenchmark(absl::StrCat(name, "/max"))) {
    return;
  }
  results.push_back({Name<T>(), Name<Dist>(), CollectMeanProbeLengths<Dist>()});
}

template <class T>
void RunForType(std::vector<Result>& results) {
  RunForTypeAndDistribution<T, Sequential<T>>(results);
  RunForTypeAndDistribution<T, AlmostSequential<T, 20>>(results);
  RunForTypeAndDistribution<T, AlmostSequential<T, 50>>(results);
  RunForTypeAndDistribution<T, Random<T, Uniform>>(results);
#ifdef NDEBUG
  // Disable these in non-opt mode because they take too long.
  RunForTypeAndDistribution<T, Random<T, Gaussian>>(results);
  RunForTypeAndDistribution<T, Random<T, Zipf>>(results);
#endif  // NDEBUG
}

}  // namespace

int main(int argc, char** argv) {
  // Parse the benchmark flags. Ignore all of them except the regex pattern.
  for (int i = 1; i < argc; ++i) {
    absl::string_view arg = argv[i];
    const auto next = [&] { return argv[std::min(i + 1, argc - 1)]; };

    if (absl::ConsumePrefix(&arg, "--benchmark_filter")) {
      if (arg == "") {
        // --benchmark_filter X
        benchmarks = next();
      } else if (absl::ConsumePrefix(&arg, "=")) {
        // --benchmark_filter=X
        benchmarks = arg;
      }
    }

    // Any --benchmark flag turns on the mode.
    if (absl::ConsumePrefix(&arg, "--benchmark")) {
      if (benchmarks.empty()) benchmarks="all";
    }
  }

  std::vector<Result> results;
  RunForType<uint64_t>(results);
  RunForType<IntIdentity>(results);
  RunForType<Ptr<8>*>(results);
  RunForType<Ptr<16>*>(results);
  RunForType<Ptr<32>*>(results);
  RunForType<Ptr<64>*>(results);
  RunForType<PtrIdentity<8>>(results);
  RunForType<PtrIdentity<16>>(results);
  RunForType<PtrIdentity<32>>(results);
  RunForType<PtrIdentity<64>>(results);
  RunForType<std::pair<uint32_t, uint32_t>>(results);
  RunForType<String<true>>(results);
  RunForType<String<false>>(results);
  RunForType<std::pair<uint64_t, String<true>>>(results);
  RunForType<std::pair<String<true>, uint64_t>>(results);
  RunForType<std::pair<uint64_t, String<false>>>(results);
  RunForType<std::pair<String<false>, uint64_t>>(results);

  switch (output()) {
    case OutputStyle::kRegular:
      absl::PrintF("%-*s%-*s       Min       Avg       Max\n%s\n", kNameWidth,
                   "Type", kDistWidth, "Distribution",
                   std::string(kNameWidth + kDistWidth + 10 * 3, '-'));
      for (const auto& result : results) {
        absl::PrintF("%-*s%-*s  %8.4f  %8.4f  %8.4f\n", kNameWidth, result.name,
                     kDistWidth, result.dist_name, result.ratios.min_load,
                     result.ratios.avg_load, result.ratios.max_load);
      }
      break;
    case OutputStyle::kBenchmark: {
      absl::PrintF("{\n");
      absl::PrintF("  \"benchmarks\": [\n");
      absl::string_view comma;
      for (const auto& result : results) {
        auto print = [&](absl::string_view stat, double Ratios::*val) {
          std::string name =
              absl::StrCat(result.name, "/", result.dist_name, "/", stat);
          // Check the regex again. We might had have enabled only one of the
          // stats for the benchmark.
          if (!CanRunBenchmark(name)) return;
          absl::PrintF("    %s{\n", comma);
          absl::PrintF("      \"cpu_time\": %f,\n", 1e9 * result.ratios.*val);
          absl::PrintF("      \"real_time\": %f,\n", 1e9 * result.ratios.*val);
          absl::PrintF("      \"iterations\": 1,\n");
          absl::PrintF("      \"name\": \"%s\",\n", name);
          absl::PrintF("      \"time_unit\": \"ns\"\n");
          absl::PrintF("    }\n");
          comma = ",";
        };
        print("min", &Ratios::min_load);
        print("avg", &Ratios::avg_load);
        print("max", &Ratios::max_load);
      }
      absl::PrintF("  ],\n");
      absl::PrintF("  \"context\": {\n");
      absl::PrintF("  }\n");
      absl::PrintF("}\n");
      break;
    }
  }

  return 0;
}
