// Copyright 2017 The Abseil Authors.
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

// Unit tests for the variant template. The 'is' and 'IsEmpty' methods
// of variant are not explicitly tested because they are used repeatedly
// in building other tests. All other public variant methods should have
// explicit tests.

#include "absl/types/variant.h"

#include <cstddef>
#include <cstdlib>
#include <string>
#include <tuple>

#include "benchmark/benchmark.h"
#include "absl/utility/utility.h"

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace {

template <std::size_t I>
struct VariantAlternative {
  char member;
};

template <class Indices>
struct VariantOfAlternativesImpl;

template <std::size_t... Indices>
struct VariantOfAlternativesImpl<absl::index_sequence<Indices...>> {
  using type = absl::variant<VariantAlternative<Indices>...>;
};

template <std::size_t NumAlternatives>
using VariantOfAlternatives = typename VariantOfAlternativesImpl<
    absl::make_index_sequence<NumAlternatives>>::type;

struct Empty {};

template <class... T>
void Ignore(T...) noexcept {}

template <class T>
Empty DoNotOptimizeAndReturnEmpty(T&& arg) noexcept {
  benchmark::DoNotOptimize(arg);
  return {};
}

struct VisitorApplier {
  struct Visitor {
    template <class... T>
    void operator()(T&&... args) const noexcept {
      Ignore(DoNotOptimizeAndReturnEmpty(args)...);
    }
  };

  template <class... Vars>
  void operator()(const Vars&... vars) const noexcept {
    absl::visit(Visitor(), vars...);
  }
};

template <std::size_t NumIndices, std::size_t CurrIndex = NumIndices - 1>
struct MakeWithIndex {
  using Variant = VariantOfAlternatives<NumIndices>;

  static Variant Run(std::size_t index) {
    return index == CurrIndex
               ? Variant(absl::in_place_index_t<CurrIndex>())
               : MakeWithIndex<NumIndices, CurrIndex - 1>::Run(index);
  }
};

template <std::size_t NumIndices>
struct MakeWithIndex<NumIndices, 0> {
  using Variant = VariantOfAlternatives<NumIndices>;

  static Variant Run(std::size_t /*index*/) { return Variant(); }
};

template <std::size_t NumIndices, class Dimensions>
struct MakeVariantTuple;

template <class T, std::size_t /*I*/>
using always_t = T;

template <std::size_t NumIndices>
VariantOfAlternatives<NumIndices> MakeVariant(std::size_t dimension,
                                              std::size_t index) {
  return dimension == 0
             ? MakeWithIndex<NumIndices>::Run(index % NumIndices)
             : MakeVariant<NumIndices>(dimension - 1, index / NumIndices);
}

template <std::size_t NumIndices, std::size_t... Dimensions>
struct MakeVariantTuple<NumIndices, absl::index_sequence<Dimensions...>> {
  using VariantTuple =
      std::tuple<always_t<VariantOfAlternatives<NumIndices>, Dimensions>...>;

  static VariantTuple Run(int index) {
    return std::make_tuple(MakeVariant<NumIndices>(Dimensions, index)...);
  }
};

constexpr std::size_t integral_pow(std::size_t base, std::size_t power) {
  return power == 0 ? 1 : base * integral_pow(base, power - 1);
}

template <std::size_t End, std::size_t I = 0>
struct VisitTestBody {
  template <class Vars, class State>
  static bool Run(Vars& vars, State& state) {
    if (state.KeepRunning()) {
      absl::apply(VisitorApplier(), vars[I]);
      return VisitTestBody<End, I + 1>::Run(vars, state);
    }
    return false;
  }
};

template <std::size_t End>
struct VisitTestBody<End, End> {
  template <class Vars, class State>
  static bool Run(Vars& /*vars*/, State& /*state*/) {
    return true;
  }
};

// Visit operations where branch prediction is likely to give a boost.
template <std::size_t NumIndices, std::size_t NumDimensions = 1>
void BM_RedundantVisit(benchmark::State& state) {
  auto vars =
      MakeVariantTuple<NumIndices, absl::make_index_sequence<NumDimensions>>::
          Run(static_cast<std::size_t>(state.range(0)));

  for (auto _ : state) {  // NOLINT
    benchmark::DoNotOptimize(vars);
    absl::apply(VisitorApplier(), vars);
  }
}

// Visit operations where branch prediction is unlikely to give a boost.
template <std::size_t NumIndices, std::size_t NumDimensions = 1>
void BM_Visit(benchmark::State& state) {
  constexpr std::size_t num_possibilities =
      integral_pow(NumIndices, NumDimensions);

  using VariantTupleMaker =
      MakeVariantTuple<NumIndices, absl::make_index_sequence<NumDimensions>>;
  using Tuple = typename VariantTupleMaker::VariantTuple;

  Tuple vars[num_possibilities];
  for (std::size_t i = 0; i < num_possibilities; ++i)
    vars[i] = VariantTupleMaker::Run(i);

  while (VisitTestBody<num_possibilities>::Run(vars, state)) {
  }
}

// Visitation
//   Each visit is on a different variant with a different active alternative)

// Unary visit
BENCHMARK_TEMPLATE(BM_Visit, 1);
BENCHMARK_TEMPLATE(BM_Visit, 2);
BENCHMARK_TEMPLATE(BM_Visit, 3);
BENCHMARK_TEMPLATE(BM_Visit, 4);
BENCHMARK_TEMPLATE(BM_Visit, 5);
BENCHMARK_TEMPLATE(BM_Visit, 6);
BENCHMARK_TEMPLATE(BM_Visit, 7);
BENCHMARK_TEMPLATE(BM_Visit, 8);
BENCHMARK_TEMPLATE(BM_Visit, 16);
BENCHMARK_TEMPLATE(BM_Visit, 32);
BENCHMARK_TEMPLATE(BM_Visit, 64);

// Binary visit
BENCHMARK_TEMPLATE(BM_Visit, 1, 2);
BENCHMARK_TEMPLATE(BM_Visit, 2, 2);
BENCHMARK_TEMPLATE(BM_Visit, 3, 2);
BENCHMARK_TEMPLATE(BM_Visit, 4, 2);
BENCHMARK_TEMPLATE(BM_Visit, 5, 2);

// Ternary visit
BENCHMARK_TEMPLATE(BM_Visit, 1, 3);
BENCHMARK_TEMPLATE(BM_Visit, 2, 3);
BENCHMARK_TEMPLATE(BM_Visit, 3, 3);

// Quaternary visit
BENCHMARK_TEMPLATE(BM_Visit, 1, 4);
BENCHMARK_TEMPLATE(BM_Visit, 2, 4);

// Redundant Visitation
//   Each visit consistently has the same alternative active

// Unary visit
BENCHMARK_TEMPLATE(BM_RedundantVisit, 1)->Arg(0);
BENCHMARK_TEMPLATE(BM_RedundantVisit, 2)->DenseRange(0, 1);
BENCHMARK_TEMPLATE(BM_RedundantVisit, 8)->DenseRange(0, 7);

// Binary visit
BENCHMARK_TEMPLATE(BM_RedundantVisit, 1, 2)->Arg(0);
BENCHMARK_TEMPLATE(BM_RedundantVisit, 2, 2)
    ->DenseRange(0, integral_pow(2, 2) - 1);
BENCHMARK_TEMPLATE(BM_RedundantVisit, 4, 2)
    ->DenseRange(0, integral_pow(4, 2) - 1);

}  // namespace
ABSL_NAMESPACE_END
}  // namespace absl
