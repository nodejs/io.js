// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "include/cppgc/allocation.h"
#include "include/cppgc/garbage-collected.h"
#include "include/cppgc/persistent.h"
#include "src/base/macros.h"
#include "src/heap/cppgc/globals.h"
#include "src/heap/cppgc/heap.h"
#include "test/benchmarks/cpp/cppgc/benchmark_utils.h"
#include "third_party/google_benchmark_chrome/src/include/benchmark/benchmark.h"
#include "v8config.h"

namespace cppgc {
namespace internal {
namespace {

using Trace = testing::BenchmarkWithHeap;

class GCed : public cppgc::GarbageCollected<GCed> {
 public:
  virtual void Trace(Visitor*) const {}
};

class OtherPayload {
 public:
  virtual void* DummyGetter() { return nullptr; }
};

class Mixin : public GarbageCollectedMixin {
 public:
  void Trace(Visitor*) const override {}
};

class GCedWithMixin final : public GCed, public OtherPayload, public Mixin {
 public:
  void Trace(Visitor* visitor) const final {
    GCed::Trace(visitor);
    Mixin::Trace(visitor);
  }
};

class Holder : public cppgc::GarbageCollected<Holder> {
 public:
  explicit Holder(GCedWithMixin* object)
      : base_ref(object), mixin_ref(object) {}

  virtual void Trace(Visitor* visitor) const {
    visitor->Trace(base_ref);
    visitor->Trace(mixin_ref);
  }

  cppgc::Member<GCedWithMixin> base_ref;
  cppgc::Member<Mixin> mixin_ref;
};

template <typename T>
V8_NOINLINE void DispatchTrace(Visitor* visitor, T& ref) {
  visitor->Trace(ref);
}

BENCHMARK_F(Trace, Static)(benchmark::State& st) {
  cppgc::Persistent<Holder> holder(cppgc::MakeGarbageCollected<Holder>(
      heap().GetAllocationHandle(), cppgc::MakeGarbageCollected<GCedWithMixin>(
                                        heap().GetAllocationHandle())));
  VisitorBase visitor;
  for (auto _ : st) {
    USE(_);
    DispatchTrace(&visitor, holder->base_ref);
  }
}

BENCHMARK_F(Trace, Dynamic)(benchmark::State& st) {
  cppgc::Persistent<Holder> holder(cppgc::MakeGarbageCollected<Holder>(
      heap().GetAllocationHandle(), cppgc::MakeGarbageCollected<GCedWithMixin>(
                                        heap().GetAllocationHandle())));
  VisitorBase visitor;
  for (auto _ : st) {
    USE(_);
    DispatchTrace(&visitor, holder->mixin_ref);
  }
}

}  // namespace
}  // namespace internal
}  // namespace cppgc
