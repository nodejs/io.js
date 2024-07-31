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

#include "absl/profiling/internal/sample_recorder.h"

#include <atomic>
#include <random>
#include <vector>

#include "gmock/gmock.h"
#include "absl/base/thread_annotations.h"
#include "absl/synchronization/internal/thread_pool.h"
#include "absl/synchronization/mutex.h"
#include "absl/synchronization/notification.h"
#include "absl/time/time.h"

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace profiling_internal {

namespace {
using ::absl::synchronization_internal::ThreadPool;
using ::testing::IsEmpty;
using ::testing::UnorderedElementsAre;

struct Info : public Sample<Info> {
 public:
  void PrepareForSampling(int64_t w) { weight = w; }
  std::atomic<size_t> size;
  absl::Time create_time;
};

std::vector<size_t> GetSizes(SampleRecorder<Info>* s) {
  std::vector<size_t> res;
  s->Iterate([&](const Info& info) {
    res.push_back(info.size.load(std::memory_order_acquire));
  });
  return res;
}

std::vector<int64_t> GetWeights(SampleRecorder<Info>* s) {
  std::vector<int64_t> res;
  s->Iterate([&](const Info& info) { res.push_back(info.weight); });
  return res;
}

Info* Register(SampleRecorder<Info>* s, int64_t weight, size_t size) {
  auto* info = s->Register(weight);
  assert(info != nullptr);
  info->size.store(size);
  return info;
}

TEST(SampleRecorderTest, Registration) {
  SampleRecorder<Info> sampler;
  auto* info1 = Register(&sampler, 31, 1);
  EXPECT_THAT(GetSizes(&sampler), UnorderedElementsAre(1));
  EXPECT_THAT(GetWeights(&sampler), UnorderedElementsAre(31));

  auto* info2 = Register(&sampler, 32, 2);
  EXPECT_THAT(GetSizes(&sampler), UnorderedElementsAre(1, 2));
  info1->size.store(3);
  EXPECT_THAT(GetSizes(&sampler), UnorderedElementsAre(3, 2));
  EXPECT_THAT(GetWeights(&sampler), UnorderedElementsAre(31, 32));

  sampler.Unregister(info1);
  sampler.Unregister(info2);
}

TEST(SampleRecorderTest, Unregistration) {
  SampleRecorder<Info> sampler;
  std::vector<Info*> infos;
  for (size_t i = 0; i < 3; ++i) {
    infos.push_back(Register(&sampler, 33 + i, i));
  }
  EXPECT_THAT(GetSizes(&sampler), UnorderedElementsAre(0, 1, 2));
  EXPECT_THAT(GetWeights(&sampler), UnorderedElementsAre(33, 34, 35));

  sampler.Unregister(infos[1]);
  EXPECT_THAT(GetSizes(&sampler), UnorderedElementsAre(0, 2));
  EXPECT_THAT(GetWeights(&sampler), UnorderedElementsAre(33, 35));

  infos.push_back(Register(&sampler, 36, 3));
  infos.push_back(Register(&sampler, 37, 4));
  EXPECT_THAT(GetSizes(&sampler), UnorderedElementsAre(0, 2, 3, 4));
  EXPECT_THAT(GetWeights(&sampler), UnorderedElementsAre(33, 35, 36, 37));
  sampler.Unregister(infos[3]);
  EXPECT_THAT(GetSizes(&sampler), UnorderedElementsAre(0, 2, 4));
  EXPECT_THAT(GetWeights(&sampler), UnorderedElementsAre(33, 35, 37));

  sampler.Unregister(infos[0]);
  sampler.Unregister(infos[2]);
  sampler.Unregister(infos[4]);
  EXPECT_THAT(GetSizes(&sampler), IsEmpty());
}

TEST(SampleRecorderTest, MultiThreaded) {
  SampleRecorder<Info> sampler;
  Notification stop;
  ThreadPool pool(10);

  for (int i = 0; i < 10; ++i) {
    pool.Schedule([&sampler, &stop, i]() {
      std::random_device rd;
      std::mt19937 gen(rd());

      std::vector<Info*> infoz;
      while (!stop.HasBeenNotified()) {
        if (infoz.empty()) {
          infoz.push_back(sampler.Register(i));
        }
        switch (std::uniform_int_distribution<>(0, 2)(gen)) {
          case 0: {
            infoz.push_back(sampler.Register(i));
            break;
          }
          case 1: {
            size_t p =
                std::uniform_int_distribution<>(0, infoz.size() - 1)(gen);
            Info* info = infoz[p];
            infoz[p] = infoz.back();
            infoz.pop_back();
            EXPECT_EQ(info->weight, i);
            sampler.Unregister(info);
            break;
          }
          case 2: {
            absl::Duration oldest = absl::ZeroDuration();
            sampler.Iterate([&](const Info& info) {
              oldest = std::max(oldest, absl::Now() - info.create_time);
            });
            ASSERT_GE(oldest, absl::ZeroDuration());
            break;
          }
        }
      }
    });
  }
  // The threads will hammer away.  Give it a little bit of time for tsan to
  // spot errors.
  absl::SleepFor(absl::Seconds(3));
  stop.Notify();
}

TEST(SampleRecorderTest, Callback) {
  SampleRecorder<Info> sampler;

  auto* info1 = Register(&sampler, 39, 1);
  auto* info2 = Register(&sampler, 40, 2);

  static const Info* expected;

  auto callback = [](const Info& info) {
    // We can't use `info` outside of this callback because the object will be
    // disposed as soon as we return from here.
    EXPECT_EQ(&info, expected);
  };

  // Set the callback.
  EXPECT_EQ(sampler.SetDisposeCallback(callback), nullptr);
  expected = info1;
  sampler.Unregister(info1);

  // Unset the callback.
  EXPECT_EQ(callback, sampler.SetDisposeCallback(nullptr));
  expected = nullptr;  // no more calls.
  sampler.Unregister(info2);
}

}  // namespace
}  // namespace profiling_internal
ABSL_NAMESPACE_END
}  // namespace absl
