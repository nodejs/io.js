// Copyright 2022 The Abseil Authors
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     https://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstring>
#include <memory>
#include <new>
#include <random>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "absl/base/config.h"
#include "absl/container/internal/layout.h"
#include "absl/log/internal/vlog_config.h"
#include "absl/memory/memory.h"
#include "absl/random/distributions.h"
#include "absl/strings/str_cat.h"
#include "benchmark/benchmark.h"

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace log_internal {
// Performance of `UpdateLogSites` depends upon the number and organization of
// `VLogSite`s in the program.  We can synthesize some on the heap to mimic
// their layout and linkage in static data.
class SyntheticBinary {
 public:
  explicit SyntheticBinary(const size_t num_tus,
                           const size_t max_extra_static_data_bytes_per_tu,
                           const size_t max_sites_per_tu,
                           const int num_shuffles) {
    per_tu_data_.reserve(num_tus);
    auto sites = absl::make_unique<VLogSite *[]>(num_tus * max_sites_per_tu);
    for (size_t i = 0; i < num_tus; i++) {
      const std::string filename =
          absl::StrCat("directory-", i / 100, "/subdirectory-", i % 100 / 10,
                       "/file-", i % 10, ".cc");
      container_internal::Layout<char, VLogSite, char> layout(
          filename.size() + 1,
          absl::LogUniform<size_t>(bitgen_, 1, max_sites_per_tu),
          absl::LogUniform<size_t>(bitgen_, 0,
                                   max_extra_static_data_bytes_per_tu));
      auto buf = absl::make_unique<char[]>(layout.AllocSize());
      layout.PoisonPadding(buf.get());
      memcpy(layout.Pointer<0>(buf.get()), filename.c_str(),
             filename.size() + 1);
      for (VLogSite &site : layout.Slice<1>(buf.get())) {
        sites[num_sites_++] =
            new (&site) VLogSite(layout.Pointer<0>(buf.get()));
        // The last one is a dangling pointer but will be fixed below.
        site.next_.store(&site + 1, std::memory_order_seq_cst);
      }
      num_extra_static_data_bytes_ += layout.Size<2>();
      per_tu_data_.push_back(PerTU{layout, std::move(buf)});
    }
    // Now link the files together back-to-front into a circular list.
    for (size_t i = 0; i < num_tus; i++) {
      auto &tu = per_tu_data_[i];
      auto &next_tu = per_tu_data_[(i + 1) % num_tus];
      tu.layout.Slice<1>(tu.buf.get())
          .back()
          .next_.store(next_tu.layout.Pointer<1>(next_tu.buf.get()),
                       std::memory_order_seq_cst);
    }
    // Now do some shufflin'.
    auto new_sites = absl::make_unique<VLogSite *[]>(num_sites_);
    for (int shuffle_num = 0; shuffle_num < num_shuffles; shuffle_num++) {
      // Each shuffle cuts the ring into three pieces and rearranges them.
      const size_t cut_a = absl::Uniform(bitgen_, size_t{0}, num_sites_);
      const size_t cut_b = absl::Uniform(bitgen_, size_t{0}, num_sites_);
      const size_t cut_c = absl::Uniform(bitgen_, size_t{0}, num_sites_);
      if (cut_a == cut_b || cut_b == cut_c || cut_a == cut_c) continue;
      // The same cuts, sorted:
      const size_t cut_1 = std::min({cut_a, cut_b, cut_c});
      const size_t cut_3 = std::max({cut_a, cut_b, cut_c});
      const size_t cut_2 = cut_a ^ cut_b ^ cut_c ^ cut_1 ^ cut_3;
      VLogSite *const tmp = sites[cut_1]->next_.load(std::memory_order_seq_cst);
      sites[cut_1]->next_.store(
          sites[cut_2]->next_.load(std::memory_order_seq_cst),
          std::memory_order_seq_cst);
      sites[cut_2]->next_.store(
          sites[cut_3]->next_.load(std::memory_order_seq_cst),
          std::memory_order_seq_cst);
      sites[cut_3]->next_.store(tmp, std::memory_order_seq_cst);
      memcpy(&new_sites[0], &sites[0], sizeof(VLogSite *) * (cut_1 + 1));
      memcpy(&new_sites[cut_1 + 1], &sites[cut_2 + 1],
             sizeof(VLogSite *) * (cut_3 - cut_2));
      memcpy(&new_sites[cut_1 + 1 + cut_3 - cut_2], &sites[cut_1 + 1],
             sizeof(VLogSite *) * (cut_2 - cut_1));
      memcpy(&new_sites[cut_3 + 1], &sites[cut_3 + 1],
             sizeof(VLogSite *) * (num_sites_ - cut_3 - 1));
      sites.swap(new_sites);
    }
    const char *last_file = nullptr;
    for (size_t i = 0; i < num_sites_; i++) {
      if (sites[i]->file_ == last_file) continue;
      last_file = sites[i]->file_;
      num_new_files_++;
    }
    absl::log_internal::SetVModuleListHeadForTestOnly(sites[0]);
    sites[num_tus - 1]->next_.store(nullptr, std::memory_order_seq_cst);
  }
  ~SyntheticBinary() {
    static_assert(std::is_trivially_destructible<VLogSite>::value, "");
    absl::log_internal::SetVModuleListHeadForTestOnly(nullptr);
  }

  size_t num_sites() const { return num_sites_; }
  size_t num_new_files() const { return num_new_files_; }
  size_t num_extra_static_data_bytes() const {
    return num_extra_static_data_bytes_;
  }

 private:
  struct PerTU {
    container_internal::Layout<char, VLogSite, char> layout;
    std::unique_ptr<char[]> buf;
  };

  std::mt19937 bitgen_;
  std::vector<PerTU> per_tu_data_;
  size_t num_sites_ = 0;
  size_t num_new_files_ = 0;
  size_t num_extra_static_data_bytes_ = 0;
};

namespace {
void BM_UpdateVModuleEmpty(benchmark::State& state) {
  SyntheticBinary bin(static_cast<size_t>(state.range(0)), 10 * 1024 * 1024,
                      256, state.range(1));
  for (auto s : state) {
    absl::log_internal::UpdateVModule("");
  }
  state.SetItemsProcessed(static_cast<int>(bin.num_new_files()));
}
BENCHMARK(BM_UpdateVModuleEmpty)
    ->ArgPair(100, 200)
    ->ArgPair(1000, 2000)
    ->ArgPair(10000, 20000);

void BM_UpdateVModuleShort(benchmark::State& state) {
  SyntheticBinary bin(static_cast<size_t>(state.range(0)), 10 * 1024 * 1024,
                      256, state.range(1));
  for (auto s : state) {
    absl::log_internal::UpdateVModule("directory2/*=4");
  }
  state.SetItemsProcessed(static_cast<int>(bin.num_new_files()));
}
BENCHMARK(BM_UpdateVModuleShort)
    ->ArgPair(100, 200)
    ->ArgPair(1000, 2000)
    ->ArgPair(10000, 20000);

void BM_UpdateVModuleLong(benchmark::State& state) {
  SyntheticBinary bin(static_cast<size_t>(state.range(0)), 10 * 1024 * 1024,
                      256, state.range(1));
  for (auto s : state) {
    absl::log_internal::UpdateVModule(
        "d?r?c?o?y2/*=4,d?*r?*c?**o?*y1/*=2,d?*rc?**o?*y3/*=2,,"
        "d?*r?*c?**o?*1/*=1,d?*r?**o?*y1/*=2,d?*r???***y1/*=7,"
        "d?*r?**o?*y1/aaa=6");
  }
  state.SetItemsProcessed(static_cast<int>(bin.num_new_files()));
}
BENCHMARK(BM_UpdateVModuleLong)
    ->ArgPair(100, 200)
    ->ArgPair(1000, 2000)
    ->ArgPair(10000, 20000);
}  // namespace
}  // namespace log_internal
ABSL_NAMESPACE_END
}  // namespace absl
