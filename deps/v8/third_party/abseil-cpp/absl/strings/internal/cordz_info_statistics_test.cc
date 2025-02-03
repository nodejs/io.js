// Copyright 2021 The Abseil Authors
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

#include <iostream>
#include <random>
#include <vector>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/base/config.h"
#include "absl/crc/internal/crc_cord_state.h"
#include "absl/strings/cord.h"
#include "absl/strings/internal/cord_internal.h"
#include "absl/strings/internal/cord_rep_btree.h"
#include "absl/strings/internal/cord_rep_crc.h"
#include "absl/strings/internal/cord_rep_flat.h"
#include "absl/strings/internal/cordz_info.h"
#include "absl/strings/internal/cordz_sample_token.h"
#include "absl/strings/internal/cordz_statistics.h"
#include "absl/strings/internal/cordz_update_scope.h"
#include "absl/strings/internal/cordz_update_tracker.h"
#include "absl/synchronization/internal/thread_pool.h"
#include "absl/synchronization/notification.h"

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace cord_internal {

// Do not print statistics contents, the matcher prints them as needed.
inline void PrintTo(const CordzStatistics& stats, std::ostream* s) {
  if (s) *s << "CordzStatistics{...}";
}

namespace {

using ::testing::Ge;

// Creates a flat of the specified allocated size
CordRepFlat* Flat(size_t size) {
  // Round up to a tag size, as we are going to poke an exact tag size back into
  // the allocated flat. 'size returning allocators' could grant us more than we
  // wanted, but we are ok to poke the 'requested' size in the tag, even in the
  // presence of sized deletes, so we need to make sure the size rounds
  // perfectly to a tag value.
  assert(size >= kMinFlatSize);
  size = RoundUpForTag(size);
  CordRepFlat* flat = CordRepFlat::New(size - kFlatOverhead);
  flat->tag = AllocatedSizeToTag(size);
  flat->length = size - kFlatOverhead;
  return flat;
}

// Creates an external of the specified length
CordRepExternal* External(size_t length = 512) {
  return static_cast<CordRepExternal*>(
      NewExternalRep(absl::string_view("", length), [](absl::string_view) {}));
}

// Creates a substring on the provided rep of length - 1
CordRepSubstring* Substring(CordRep* rep) {
  auto* substring = new CordRepSubstring;
  substring->length = rep->length - 1;
  substring->tag = SUBSTRING;
  substring->child = rep;
  return substring;
}

// Reference count helper
struct RefHelper {
  std::vector<CordRep*> refs;

  ~RefHelper() {
    for (CordRep* rep : refs) {
      CordRep::Unref(rep);
    }
  }

  // Invokes CordRep::Unref() on `rep` when this instance is destroyed.
  template <typename T>
  T* NeedsUnref(T* rep) {
    refs.push_back(rep);
    return rep;
  }

  // Adds `n` reference counts to `rep` which will be unreffed when this
  // instance is destroyed.
  template <typename T>
  T* Ref(T* rep, size_t n = 1) {
    while (n--) {
      NeedsUnref(CordRep::Ref(rep));
    }
    return rep;
  }
};

// Sizeof helper. Returns the allocated size of `p`, excluding any child
// elements for substring, concat and ring cord reps.
template <typename T>
size_t SizeOf(const T* rep) {
  return sizeof(T);
}

template <>
size_t SizeOf(const CordRepFlat* rep) {
  return rep->AllocatedSize();
}

template <>
size_t SizeOf(const CordRepExternal* rep) {
  // See cord.cc
  return sizeof(CordRepExternalImpl<intptr_t>) + rep->length;
}

// Computes fair share memory used in a naive 'we dare to recurse' way.
double FairShareImpl(CordRep* rep, size_t ref) {
  double self = 0.0, children = 0.0;
  ref *= rep->refcount.Get();
  if (rep->tag >= FLAT) {
    self = SizeOf(rep->flat());
  } else if (rep->tag == EXTERNAL) {
    self = SizeOf(rep->external());
  } else if (rep->tag == SUBSTRING) {
    self = SizeOf(rep->substring());
    children = FairShareImpl(rep->substring()->child, ref);
  } else if (rep->tag == BTREE) {
    self = SizeOf(rep->btree());
    for (CordRep*edge : rep->btree()->Edges()) {
      children += FairShareImpl(edge, ref);
    }
  } else {
    assert(false);
  }
  return self / ref + children;
}

// Returns the fair share memory size from `ShareFhareImpl()` as a size_t.
size_t FairShare(CordRep* rep, size_t ref = 1) {
  return static_cast<size_t>(FairShareImpl(rep, ref));
}

// Samples the cord and returns CordzInfo::GetStatistics()
CordzStatistics SampleCord(CordRep* rep) {
  InlineData cord(rep);
  CordzInfo::TrackCord(cord, CordzUpdateTracker::kUnknown, 1);
  CordzStatistics stats = cord.cordz_info()->GetCordzStatistics();
  cord.cordz_info()->Untrack();
  return stats;
}

MATCHER_P(EqStatistics, stats, "Statistics equal expected values") {
  bool ok = true;

#define STATS_MATCHER_EXPECT_EQ(member)                              \
  if (stats.member != arg.member) {                                  \
    *result_listener << "\n    stats." << #member                    \
                     << ": actual = " << arg.member << ", expected " \
                     << stats.member;                                \
    ok = false;                                                      \
  }

  STATS_MATCHER_EXPECT_EQ(size);
  STATS_MATCHER_EXPECT_EQ(node_count);
  STATS_MATCHER_EXPECT_EQ(node_counts.flat);
  STATS_MATCHER_EXPECT_EQ(node_counts.flat_64);
  STATS_MATCHER_EXPECT_EQ(node_counts.flat_128);
  STATS_MATCHER_EXPECT_EQ(node_counts.flat_256);
  STATS_MATCHER_EXPECT_EQ(node_counts.flat_512);
  STATS_MATCHER_EXPECT_EQ(node_counts.flat_1k);
  STATS_MATCHER_EXPECT_EQ(node_counts.external);
  STATS_MATCHER_EXPECT_EQ(node_counts.concat);
  STATS_MATCHER_EXPECT_EQ(node_counts.substring);
  STATS_MATCHER_EXPECT_EQ(node_counts.ring);
  STATS_MATCHER_EXPECT_EQ(node_counts.btree);
  STATS_MATCHER_EXPECT_EQ(estimated_memory_usage);
  STATS_MATCHER_EXPECT_EQ(estimated_fair_share_memory_usage);

#undef STATS_MATCHER_EXPECT_EQ

  return ok;
}

TEST(CordzInfoStatisticsTest, Flat) {
  RefHelper ref;
  auto* flat = ref.NeedsUnref(Flat(512));

  CordzStatistics expected;
  expected.size = flat->length;
  expected.estimated_memory_usage = SizeOf(flat);
  expected.estimated_fair_share_memory_usage = expected.estimated_memory_usage;
  expected.node_count = 1;
  expected.node_counts.flat = 1;
  expected.node_counts.flat_512 = 1;

  EXPECT_THAT(SampleCord(flat), EqStatistics(expected));
}

TEST(CordzInfoStatisticsTest, SharedFlat) {
  RefHelper ref;
  auto* flat = ref.Ref(ref.NeedsUnref(Flat(64)));

  CordzStatistics expected;
  expected.size = flat->length;
  expected.estimated_memory_usage = SizeOf(flat);
  expected.estimated_fair_share_memory_usage = SizeOf(flat) / 2;
  expected.node_count = 1;
  expected.node_counts.flat = 1;
  expected.node_counts.flat_64 = 1;

  EXPECT_THAT(SampleCord(flat), EqStatistics(expected));
}

TEST(CordzInfoStatisticsTest, External) {
  RefHelper ref;
  auto* external = ref.NeedsUnref(External());

  CordzStatistics expected;
  expected.size = external->length;
  expected.estimated_memory_usage = SizeOf(external);
  expected.estimated_fair_share_memory_usage = SizeOf(external);
  expected.node_count = 1;
  expected.node_counts.external = 1;

  EXPECT_THAT(SampleCord(external), EqStatistics(expected));
}

TEST(CordzInfoStatisticsTest, SharedExternal) {
  RefHelper ref;
  auto* external = ref.Ref(ref.NeedsUnref(External()));

  CordzStatistics expected;
  expected.size = external->length;
  expected.estimated_memory_usage = SizeOf(external);
  expected.estimated_fair_share_memory_usage = SizeOf(external) / 2;
  expected.node_count = 1;
  expected.node_counts.external = 1;

  EXPECT_THAT(SampleCord(external), EqStatistics(expected));
}

TEST(CordzInfoStatisticsTest, Substring) {
  RefHelper ref;
  auto* flat = Flat(1024);
  auto* substring = ref.NeedsUnref(Substring(flat));

  CordzStatistics expected;
  expected.size = substring->length;
  expected.estimated_memory_usage = SizeOf(substring) + SizeOf(flat);
  expected.estimated_fair_share_memory_usage = expected.estimated_memory_usage;
  expected.node_count = 2;
  expected.node_counts.flat = 1;
  expected.node_counts.flat_1k = 1;
  expected.node_counts.substring = 1;

  EXPECT_THAT(SampleCord(substring), EqStatistics(expected));
}

TEST(CordzInfoStatisticsTest, SharedSubstring) {
  RefHelper ref;
  auto* flat = ref.Ref(Flat(511), 2);
  auto* substring = ref.Ref(ref.NeedsUnref(Substring(flat)));

  CordzStatistics expected;
  expected.size = substring->length;
  expected.estimated_memory_usage = SizeOf(flat) + SizeOf(substring);
  expected.estimated_fair_share_memory_usage =
      SizeOf(substring) / 2 + SizeOf(flat) / 6;
  expected.node_count = 2;
  expected.node_counts.flat = 1;
  expected.node_counts.flat_512 = 1;
  expected.node_counts.substring = 1;

  EXPECT_THAT(SampleCord(substring), EqStatistics(expected));
}

TEST(CordzInfoStatisticsTest, BtreeLeaf) {
  ASSERT_THAT(CordRepBtree::kMaxCapacity, Ge(3u));
  RefHelper ref;
  auto* flat1 = Flat(2000);
  auto* flat2 = Flat(200);
  auto* substr = Substring(flat2);
  auto* external = External(3000);

  CordRepBtree* tree = CordRepBtree::Create(flat1);
  tree = CordRepBtree::Append(tree, substr);
  tree = CordRepBtree::Append(tree, external);
  size_t flat3_count = CordRepBtree::kMaxCapacity - 3;
  size_t flat3_size = 0;
  for (size_t i = 0; i < flat3_count; ++i) {
    auto* flat3 = Flat(70);
    flat3_size += SizeOf(flat3);
    tree = CordRepBtree::Append(tree, flat3);
  }
  ref.NeedsUnref(tree);

  CordzStatistics expected;
  expected.size = tree->length;
  expected.estimated_memory_usage = SizeOf(tree) + SizeOf(flat1) +
                                    SizeOf(flat2) + SizeOf(substr) +
                                    flat3_size + SizeOf(external);
  expected.estimated_fair_share_memory_usage = expected.estimated_memory_usage;
  expected.node_count = 1 + 3 + 1 + flat3_count;
  expected.node_counts.flat = 2 + flat3_count;
  expected.node_counts.flat_128 = flat3_count;
  expected.node_counts.flat_256 = 1;
  expected.node_counts.external = 1;
  expected.node_counts.substring = 1;
  expected.node_counts.btree = 1;

  EXPECT_THAT(SampleCord(tree), EqStatistics(expected));
}

TEST(CordzInfoStatisticsTest, BtreeNodeShared) {
  RefHelper ref;
  static constexpr int leaf_count = 3;
  const size_t flat3_count = CordRepBtree::kMaxCapacity - 3;
  ASSERT_THAT(flat3_count, Ge(0u));

  CordRepBtree* tree = nullptr;
  size_t mem_size = 0;
  for (int i = 0; i < leaf_count; ++i) {
    auto* flat1 = ref.Ref(Flat(2000), 9);
    mem_size += SizeOf(flat1);
    if (i == 0) {
      tree = CordRepBtree::Create(flat1);
    } else {
      tree = CordRepBtree::Append(tree, flat1);
    }

    auto* flat2 = Flat(200);
    auto* substr = Substring(flat2);
    mem_size += SizeOf(flat2) + SizeOf(substr);
    tree = CordRepBtree::Append(tree, substr);

    auto* external = External(30);
    mem_size += SizeOf(external);
    tree = CordRepBtree::Append(tree, external);

    for (size_t i = 0; i < flat3_count; ++i) {
      auto* flat3 = Flat(70);
      mem_size += SizeOf(flat3);
      tree = CordRepBtree::Append(tree, flat3);
    }

    if (i == 0) {
      mem_size += SizeOf(tree);
    } else {
      mem_size += SizeOf(tree->Edges().back()->btree());
    }
  }
  ref.NeedsUnref(tree);

  // Ref count: 2 for top (add 1), 5 for leaf 0 (add 4).
  ref.Ref(tree, 1);
  ref.Ref(tree->Edges().front(), 4);

  CordzStatistics expected;
  expected.size = tree->length;
  expected.estimated_memory_usage = SizeOf(tree) + mem_size;
  expected.estimated_fair_share_memory_usage = FairShare(tree);

  expected.node_count = 1 + leaf_count * (1 + 3 + 1 + flat3_count);
  expected.node_counts.flat = leaf_count * (2 + flat3_count);
  expected.node_counts.flat_128 = leaf_count * flat3_count;
  expected.node_counts.flat_256 = leaf_count;
  expected.node_counts.external = leaf_count;
  expected.node_counts.substring = leaf_count;
  expected.node_counts.btree = 1 + leaf_count;

  EXPECT_THAT(SampleCord(tree), EqStatistics(expected));
}

TEST(CordzInfoStatisticsTest, Crc) {
  RefHelper ref;
  auto* left = Flat(1000);
  auto* crc = ref.NeedsUnref(CordRepCrc::New(left, {}));

  CordzStatistics expected;
  expected.size = left->length;
  expected.estimated_memory_usage = SizeOf(crc) + SizeOf(left);
  expected.estimated_fair_share_memory_usage = expected.estimated_memory_usage;
  expected.node_count = 2;
  expected.node_counts.flat = 1;
  expected.node_counts.flat_1k = 1;
  expected.node_counts.crc = 1;

  EXPECT_THAT(SampleCord(crc), EqStatistics(expected));
}

TEST(CordzInfoStatisticsTest, EmptyCrc) {
  RefHelper ref;
  auto* crc = ref.NeedsUnref(CordRepCrc::New(nullptr, {}));

  CordzStatistics expected;
  expected.size = 0;
  expected.estimated_memory_usage = SizeOf(crc);
  expected.estimated_fair_share_memory_usage = expected.estimated_memory_usage;
  expected.node_count = 1;
  expected.node_counts.crc = 1;

  EXPECT_THAT(SampleCord(crc), EqStatistics(expected));
}

TEST(CordzInfoStatisticsTest, ThreadSafety) {
  Notification stop;
  static constexpr int kNumThreads = 8;
  int64_t sampled_node_count = 0;

  {
    absl::synchronization_internal::ThreadPool pool(kNumThreads);

    // Run analyzer thread emulating a CordzHandler collection.
    pool.Schedule([&]() {
      while (!stop.HasBeenNotified()) {
        // Run every 10us (about 100K total collections).
        absl::SleepFor(absl::Microseconds(10));
        CordzSampleToken token;
        for (const CordzInfo& cord_info : token) {
          CordzStatistics stats = cord_info.GetCordzStatistics();
          sampled_node_count += stats.node_count;
        }
      }
    });

    // Run 'application threads'
    for (int i = 0; i < kNumThreads; ++i) {
      pool.Schedule([&]() {
        // Track 0 - 2 cordz infos at a time, providing permutations of 0, 1
        // and 2 CordzHandle and CordzInfo queues being active, with plenty of
        // 'empty to non empty' transitions.
        InlineData cords[2];
        std::minstd_rand gen;
        std::uniform_int_distribution<int> coin_toss(0, 1);
        std::uniform_int_distribution<int> dice_roll(1, 6);

        while (!stop.HasBeenNotified()) {
          for (InlineData& cord : cords) {
            // 50/50 flip the state of the cord
            if (coin_toss(gen) != 0) {
              if (cord.is_tree()) {
                // 50/50 simulate delete (untrack) or 'edit to empty'
                if (coin_toss(gen) != 0) {
                  CordzInfo::MaybeUntrackCord(cord.cordz_info());
                } else {
                  CordzUpdateScope scope(cord.cordz_info(),
                                         CordzUpdateTracker::kUnknown);
                  scope.SetCordRep(nullptr);
                }
                CordRep::Unref(cord.as_tree());
                cord.set_inline_size(0);
              } else {
                // Coin toss to 50% btree, and 50% flat.
                CordRep* rep = Flat(256);
                if (coin_toss(gen) != 0) {
                  rep = CordRepBtree::Create(rep);
                }

                // Maybe CRC this cord
                if (dice_roll(gen) == 6) {
                  if (dice_roll(gen) == 6) {
                    // Empty CRC rep
                    CordRep::Unref(rep);
                    rep = CordRepCrc::New(nullptr, {});
                  } else {
                    // Regular CRC rep
                    rep = CordRepCrc::New(rep, {});
                  }
                }
                cord.make_tree(rep);

                // 50/50 sample
                if (coin_toss(gen) != 0) {
                  CordzInfo::TrackCord(cord, CordzUpdateTracker::kUnknown, 1);
                }
              }
            }
          }
        }
        for (InlineData& cord : cords) {
          if (cord.is_tree()) {
            CordzInfo::MaybeUntrackCord(cord.cordz_info());
            CordRep::Unref(cord.as_tree());
          }
        }
      });
    }

    // Run for 1 second to give memory and thread safety analyzers plenty of
    // time to detect any mishaps or undefined behaviors.
    absl::SleepFor(absl::Seconds(1));
    stop.Notify();
  }

  std::cout << "Sampled " << sampled_node_count << " nodes\n";
}

}  // namespace
}  // namespace cord_internal
ABSL_NAMESPACE_END
}  // namespace absl
